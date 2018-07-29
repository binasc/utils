from stream import Stream
import obscure
import struct
import uuid

import loglevel
_logger = loglevel.get_logger('tunnel')


BUFF_SIZE = 1024 ** 2
UNKNOWN_CONN_ADDR = "127.0.0.1"
UNKNOWN_CONN_PORT = 8080


class Tunnel(object):
    _TCP_INITIAL_DATA = 0
    _TCP_CLOSED_DATA = 1
    _UDP_INITIAL_DATA = 2
    _UDP_CLOSED_DATA = 3
    _TUN_INITIAL_DATA = 4
    _PAYLOAD = 10

    _static_handlers = {}

    @staticmethod
    def set_tcp_initial_handler(handler):
        Tunnel._static_handlers[Tunnel._TCP_INITIAL_DATA] = handler

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
        self._on_buffer_low = None
        self._on_buffer_high = None

    def __hash__(self):
        return hash(self._stream)

    def __eq__(self, other):
        if not isinstance(other, Tunnel):
            return False
        return self._stream == other._stream

    def __str__(self):
        return str(self._stream)

    def initialize(self):
        if self._stream is None:
            self._stream = Stream()

        self._stream.set_buffer_size(BUFF_SIZE)
        self._stream.set_tcp_no_delay()
        #try:
        #    self._stream.set_cong_algorithm('hybla')
        #except Exception as ex:
        #    _logger.warning('set_cong_algorithm failed: %s' % str(ex))
        self._stream.append_send_handler(obscure.pack_data)
        self._stream.append_send_handler(obscure.random_padding)
        # self._stream.append_send_handler(obscure.gen_aes_encrypt())
        self._stream.append_send_handler(obscure.gen_xor_encrypt())
        # self._stream.append_send_handler(obscure.base64_encode)
        self._stream.append_send_handler(obscure.gen_http_encode(self._connect_to is not None))
        self._stream.append_receive_handler(obscure.gen_http_decode(self._connect_to is not None))
        # self._stream.append_receive_handler(obscure.base64_decode)
        self._stream.append_receive_handler(obscure.gen_xor_decrypt())
        # self._stream.append_receive_handler(obscure.gen_aes_decrypt())
        self._stream.append_receive_handler(obscure.unpad_random)
        self._stream.append_receive_handler(obscure.unpack_data)

        self._stream.set_on_sent(lambda _, sent, remain: self._on_sent(sent, remain))
        self._stream.set_on_received(lambda _, data, addr: self._on_received(data, addr))
        self._stream.set_on_closed(lambda _: self._on_closed())
        self._stream.set_on_decode_error(lambda _, received: self._on_decode_error(received))

        if self._connect_to is not None:
            self._stream.connect(*self._connect_to)
        else:
            self._stream.begin_receiving()

    def _send_content(self, type_, id_, content):
        to_send = struct.pack('!HI', type_, len(content)) + id_.get_bytes() + content
        self._stream.send(to_send)
        if self._stream.pending_bytes() >= BUFF_SIZE:
            if self._on_buffer_high is not None:
                self._on_buffer_high(self)

    def _on_sent(self, _sent, remain):
        _logger.debug("tunnel %s sent %d bytes" % (str(self), _sent))
        if remain < BUFF_SIZE:
            if self._on_buffer_low is not None:
                self._on_buffer_low(self)

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

    def send_tcp_initial_data(self, id_, data):
        self._send_content(Tunnel._TCP_INITIAL_DATA, id_, data)

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

    def set_on_buffer_high(self, handler):
        self._on_buffer_high = handler

    def set_on_buffer_low(self, handler):
        self._on_buffer_low = handler

    def _on_closed(self):
        if self._on_stream_closed is not None:
            self._on_stream_closed(self)

    def set_on_closed(self, handler):
        self._on_stream_closed = handler

    def close(self):
        self._stream.close()

    def _on_decode_error(self, received):
        # TODO: pay attention to BUFF_SIZE
        self._stream._encoders = []
        backend = Stream()

        def tunnel_sent(_, _sent, remain):
            if remain <= BUFF_SIZE:
                backend.begin_receiving()

        def tunnel_received(_, data, _addr):
            backend.send(data)

        def tunnel_closed(_):
            backend.close()

        def backend_received(_, data, _addr):
            self._stream.send(data)
            pending = self._stream.pending_bytes()
            if pending > BUFF_SIZE:
                self._stream.stop_receiving()

        def backend_closed(_self):
            self._stream.close()

        self._stream.set_on_sent(tunnel_sent)
        self._stream.set_on_received(tunnel_received)
        self._stream.set_on_closed(tunnel_closed)
        backend.set_on_received(backend_received)
        backend.set_on_closed(backend_closed)
        if received is not None and len(received) > 0:
            backend.send(received)
        backend.connect(UNKNOWN_CONN_ADDR, UNKNOWN_CONN_PORT)
