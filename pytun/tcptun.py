import json
import common
from stream import Stream

import logging
import loglevel
_logger = loglevel.get_logger('tcptun', logging.INFO)


BUFFER_SIZE = 2 * (1024 ** 2)


def gen_on_client_accepted(via, to):

    connect_info = common.wrap_content(json.dumps({
        'p': 'tcp',
        'type': 'connect',
        'addr': to[0],
        'port': to[1]
    }))

    hb = common.wrap_content(json.dumps({'type': 'hb'}))

    data_header = json.dumps({'type': 'data'})

    def on_accepted(front, from_):
        _logger.debug('on_accepted')

        tunnel = Stream()
        common.initialize_tunnel(tunnel)

        _logger.info("from %s:%d (%s to %s) connected" % (from_[0], from_[1], str(front), str(tunnel)))

        def tunnel_sent(_self, sent, _remain):
            _logger.debug("%s:%d --> %s:%d %d bytes" % (
                         from_[0], from_[1], to[0], int(to[1]), sent))

        def tunnel_received(_self, data, _addr):
            _logger.debug("%s:%d <-- %s:%d %d bytes" % (
                         from_[0], from_[1], to[0], int(to[1]), len(data)))
            front.send(data)

        def tunnel_send_heartbeat(self):
            self.send(hb)
            self.add_timer('hb', 15 * 1000, tunnel_send_heartbeat)

        def tunnel_closed(_self):
            _logger.info("%s:%d <-> %s:%d closed" % (
                         from_[0], from_[1], to[0], int(to[1])))
            front.close()

        def frontend_received(_self, data, _):
            tunnel.send(common.wrap_content(data_header, data))

        def frontend_closed(_self):
            tunnel.close()

        tunnel.set_on_sent(tunnel_sent)
        tunnel.set_on_received(tunnel_received)
        tunnel.set_on_closed(tunnel_closed)
        tunnel.add_timer('hb', 15 * 1000, tunnel_send_heartbeat)

        tunnel.connect(via[0], via[1])
        tunnel.send(connect_info)

        front.set_on_received(frontend_received)
        front.set_on_closed(frontend_closed)
        front.begin_receiving()

    return on_accepted


def on_server_side_connected(tunnel, addr, port):
    back = Stream()

    _logger.info('connect to: %s:%d (%s to %s)', addr, port, str(tunnel), str(back))

    def tunnel_sent(_self, _sent, remain):
        if remain <= BUFFER_SIZE:
            back.begin_receiving()

    def tunnel_received(_self, data, _addr):
        json_str, data = common.unwrap_content(data)
        header = json.loads(json_str)

        type_ = header['type']
        if type_ == 'data':
            back.send(data)
        elif type_ == 'hb':
            _logger.debug('Received heartbeat packet')
        else:
            _logger.warning('Unknown packet type: %s', type_)

    def tunnel_closed(_self):
        back.close()

    def backend_received(self, data, _):
        tunnel.send(data)
        pending = tunnel.pending_bytes()
        if pending > BUFFER_SIZE:
            self.stop_receiving()

    def backend_closed(_self):
        tunnel.close()

    tunnel.set_on_sent(tunnel_sent)
    tunnel.set_on_received(tunnel_received)
    tunnel.set_on_closed(tunnel_closed)
    back.set_on_received(backend_received)
    back.set_on_closed(backend_closed)
    back.connect(addr, port)


def on_server_side_received_unknown_connection(tunnel, addr, port, recv):
    back = Stream()

    _logger.info('try to connect to: %s:%d (%s to %s)', addr, port, str(tunnel), str(back))

    def tunnel_sent(_self, _sent, remain):
        if remain <= BUFFER_SIZE:
            back.begin_receiving()

    def tunnel_received(_self, data, _addr):
        back.send(data)

    def tunnel_closed(_self):
        back.close()

    def backend_received(self, data, _addr):
        tunnel.send(data)
        pending = tunnel.pending_bytes()
        if pending > BUFFER_SIZE:
            self.stop_receiving()

    def backend_closed(_self):
        tunnel.close()

    tunnel.set_on_sent(tunnel_sent)
    tunnel.set_on_received(tunnel_received)
    tunnel.set_on_closed(tunnel_closed)
    back.set_on_received(backend_received)
    back.set_on_closed(backend_closed)
    if recv is not None:
        back.send(recv)
    back.connect(addr, port)
