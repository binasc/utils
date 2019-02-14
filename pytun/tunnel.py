from stream import Stream
from event import Event
import obscure
import struct
import uuid

import loglevel
_logger = loglevel.get_logger('tunnel', loglevel.DEFAULT_LEVEL)


BUFF_SIZE = 1024 ** 2
UNKNOWN_CONN_ADDR = "127.0.0.1"
UNKNOWN_CONN_PORT = 8000
HEARTBEAT_INTERVAL = 60 * 1000


class Tunnel(object):
    _TCP_INITIAL_DATA = 0
    _TCP_FIN_DATA = 1
    _TCP_CLOSED_DATA = 2
    _UDP_INITIAL_DATA = 3
    _UDP_CLOSED_DATA = 4
    _TUN_INITIAL_DATA = 5
    _PAYLOAD = 10
    _HEARTBEAT = 100

    _static_handlers = {
        _HEARTBEAT: (lambda _, __, ___: None)
    }

    @staticmethod
    def set_tcp_initial_handler(handler):
        Tunnel._static_handlers[Tunnel._TCP_INITIAL_DATA] = handler

    @staticmethod
    def set_tcp_fin_received_handler(handler):
        Tunnel._static_handlers[Tunnel._TCP_FIN_DATA] = handler

    @staticmethod
    def set_tcp_closed_handler(handler):
        Tunnel._static_handlers[Tunnel._TCP_CLOSED_DATA] = handler

    @staticmethod
    def set_udp_initial_handler(handler):
        Tunnel._static_handlers[Tunnel._UDP_INITIAL_DATA] = handler

    @staticmethod
    def set_udp_closed_handler(handler):
        Tunnel._static_handlers[Tunnel._UDP_CLOSED_DATA] = handler

    @staticmethod
    def set_tun_initial_handler(handler):
        Tunnel._static_handlers[Tunnel._TUN_INITIAL_DATA] = handler

    def __init__(self, connection=None, connect_to=None):
        self._stream = connection
        self._connect_to = connect_to
        self._on_initial_data = None
        self._on_payload = None
        self._on_stream_closed = None
        self._handlers = self._static_handlers.copy()
        self._handlers.update({
            Tunnel._PAYLOAD: lambda _, id_, data: self._on_payload(self, id_, data)
        })
        self._on_ready_to_send = None
        self._on_send_buffer_full = None
        self._hb_event = None
        self.connections = {}

    def __hash__(self):
        return hash(self._stream)

    def __eq__(self, other):
        if not isinstance(other, Tunnel):
            return False
        return self._stream == other._stream

    def __str__(self):
        return str(self._stream)

    def _send_heartbeat(self):
        self._send_content(Tunnel._HEARTBEAT, None, None)
        self._enable_heartbeat()

    def _enable_heartbeat(self):
        self._hb_event = Event.add_timer(HEARTBEAT_INTERVAL)
        self._hb_event.set_handler(lambda ev: self._send_heartbeat())

    def _disable_heartbeat(self):
        if self._hb_event is not None:
            self._hb_event.del_timer()
            self._hb_event = None

    def initialize(self):
        if self._stream is None:
            self._stream = Stream(prefix='TUNNEL')
        else:
            self._stream.set_prefix('TUNNEL')

        self._stream.set_buffer_size(BUFF_SIZE)
        self._stream.set_tcp_no_delay()
        # try:
        #     self._stream.set_cong_algorithm('hybla')
        # except Exception as ex:
        #     _logger.warning('set_cong_algorithm failed: %s' % str(ex))
        self._stream.append_send_handler(obscure.pack_data)
        self._stream.append_send_handler(obscure.random_padding)
        # self._stream.append_send_handler(obscure.gen_aes_encrypt())
        self._stream.append_send_handler(obscure.gen_xor_encrypt())
        self._stream.append_send_handler(obscure.base64_encode)
        self._stream.append_send_handler(obscure.gen_http_encode(self._connect_to is not None))
        self._stream.append_receive_handler(obscure.gen_http_decode(self._connect_to is not None))
        self._stream.append_receive_handler(obscure.base64_decode)
        self._stream.append_receive_handler(obscure.gen_xor_decrypt())
        # self._stream.append_receive_handler(obscure.gen_aes_decrypt())
        self._stream.append_receive_handler(obscure.unpad_random)
        self._stream.append_receive_handler(obscure.unpack_data)

        self._stream.set_on_ready_to_send(lambda _: self._on_tunnel_ready_to_send())
        self._stream.set_on_send_buffer_full(lambda _: self._on_tunnel_send_buffer_full())
        self._stream.set_on_received(lambda _, data, addr: self._on_received(data, addr))
        self._stream.set_on_fin_received(lambda _: self._disable_heartbeat())
        self._stream.set_on_closed(lambda _: self._on_closed())
        self._stream.set_on_decode_error(lambda _, received: self._on_decode_error(received))

        if self._connect_to is not None:
            self._stream.connect(*self._connect_to)
        else:
            self._stream.start_receiving()
        self._enable_heartbeat()

    def register(self, key, conn):
        _logger.debug('%s, register: %s(%s)', str(self), str(key), str(conn))
        assert(key not in self.connections)
        self.connections[key] = conn

    def deregister(self, key):
        _logger.debug('%s, deregister(%s)', str(self), str(key))
        assert(key in self.connections)
        del self.connections[key]

    def get_connection(self, key):
        if key in self.connections:
            return self.connections[key]
        else:
            _logger.debug('no such connection: %s', str(key))
            return None

    def clear_connections(self):
        _logger.debug('%s, clear_connections (%d)', str(self), len(self.connections))
        self.connections.clear()

    def is_ready_to_send(self):
        return self._stream.is_ready_to_send()

    def _send_content(self, type_, id_, content):
        if id_ is None:
            to_send = struct.pack('!HI', type_, 0) + '\x00' * 16
        else:
            to_send = struct.pack('!HI', type_, len(content)) + id_.get_bytes() + content
        self._stream.send(to_send)

    def _on_tunnel_ready_to_send(self):
        if self._on_ready_to_send is not None:
            self._on_ready_to_send(self)

    def _on_tunnel_send_buffer_full(self):
        if self._on_send_buffer_full is not None:
            self._on_send_buffer_full(self)

    def _on_received(self, data, _addr):
        _logger.debug("tunnel %s received %d bytes" % (str(self), len(data)))
        if len(data) < 6 + 16:
            raise Exception('corrupted data')

        type_, content_length = struct.unpack('!HI', data[: 6])
        id_ = uuid.UUID(bytes=data[6: 6 + 16])

        if type_ not in self._handlers or self._handlers[type_] is None:
            _logger.warning("tunnel message type %d can not be handled", type_)

        if len(data) - 6 - 16 != content_length:
            raise Exception('corrupted data')

        self._handlers[type_](self, id_, data[6 + 16:])

        return True

    def send_tcp_initial_data(self, id_, data):
        self._send_content(Tunnel._TCP_INITIAL_DATA, id_, data)

    def send_tcp_fin_data(self, id_, data=''):
        self._send_content(Tunnel._TCP_FIN_DATA, id_, data)

    def send_tcp_closed_data(self, id_, data=''):
        self._send_content(Tunnel._TCP_CLOSED_DATA, id_, data)

    def send_udp_initial_data(self, id_, data):
        self._send_content(Tunnel._UDP_INITIAL_DATA, id_, data)

    def send_udp_closed_data(self, id_, data=''):
        self._send_content(Tunnel._UDP_CLOSED_DATA, id_, data)

    def send_tun_initial_data(self, id_, data):
        self._send_content(Tunnel._TUN_INITIAL_DATA, id_, data)

    def send_payload(self, id_, payload):
        self._send_content(Tunnel._PAYLOAD, id_, payload)

    def set_on_payload(self, handler):
        self._on_payload = handler

    def set_on_ready_to_send(self, handler):
        self._on_ready_to_send = handler

    def set_on_send_buffer_full(self, handler):
        self._on_send_buffer_full = handler

    def _on_closed(self):
        self._disable_heartbeat()
        if self._on_stream_closed is not None:
            self._on_stream_closed(self)

    def set_on_closed(self, handler):
        self._on_stream_closed = handler

    def close(self):
        self._stream.close()

    def is_closed(self):
        return self._stream.is_closed()

    def _on_decode_error(self, received):
        self._disable_heartbeat()
        self._stream._encoders = []
        backend = Stream()

        def tunnel_ready_to_send(_):
            backend.start_receiving()

        def tunnel_send_buffer_full(_):
            backend.stop_receiving()

        def tunnel_received(_, data, _addr):
            backend.send(data)
            return backend.is_ready_to_send()

        def tunnel_closed(_):
            backend.close()

        def backend_received(_, data, _addr):
            self._stream.send(data)
            return self._stream.is_ready_to_send()

        def backend_closed(_self):
            self._stream.close()

        self._stream.set_on_ready_to_send(tunnel_ready_to_send)
        self._stream.set_on_send_buffer_full(tunnel_send_buffer_full)
        self._stream.set_on_received(tunnel_received)
        self._stream.set_on_closed(tunnel_closed)
        backend.set_on_received(backend_received)
        backend.set_on_closed(backend_closed)
        if received is not None and len(received) > 0:
            backend.send(received)
        backend.connect(UNKNOWN_CONN_ADDR, UNKNOWN_CONN_PORT)
