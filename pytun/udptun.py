import json
import common
from stream import Stream
from dgram import Dgram

import logging
import loglevel
_logger = loglevel.get_logger('udptun', logging.INFO)


BUFFER_SIZE = 4 * (1024 ** 2)

addr2Stream = {}


def gen_on_received(via, to):

    header = common.wrap_content(json.dumps({
        'type': 'udp',
        'addr': to[0],
        'port': to[1]
    }))

    def on_received(front, data, from_):
        addr_port = '%s:%d' % from_

        if addr_port in addr2Stream:
            tunnel = addr2Stream[addr_port]
        else:
            tunnel = Stream()
            common.initialize_tunnel(tunnel)
            addr2Stream[addr_port] = tunnel

            _logger.info('new Dgram from: %s:%d (%s to %s)' % (from_[0], from_[1], str(front), str(tunnel)))

            def tunnel_sent(_self, sent, _remain):
                _logger.debug("%s:%d --> %s:%d %d bytes" % (
                             from_[0], from_[1], to[0], int(to[1]), sent))

            def tunnel_received(_self, data_, _addr):
                _logger.debug("%s:%d <-- %s:%d %d bytes" % (
                             from_[0], from_[1], to[0], int(to[1]), len(data_)))
                front.send(data_, from_)

            def tunnel_closed(_self):
                del addr2Stream[addr_port]

            tunnel.set_on_sent(tunnel_sent)
            tunnel.set_on_received(tunnel_received)
            tunnel.set_on_closed(tunnel_closed)

            tunnel.connect(via[0], via[1])
            tunnel.send(header)

        tunnel.send(data)

    return on_received


def accept_side_receiver(tunnel, header):
    addr = header['addr']
    port = header['port']

    back = Dgram()

    _logger.info('new Dgram from: %s:%d (%s to %s)', addr, port, str(tunnel), str(back))

    def udp_tunnel_sent(_self, _sent, remain):
        if remain <= BUFFER_SIZE:
            back.begin_receiving()

    def udp_tunnel_received(_self, data, _addr):
        back.send(data, (addr, port))

    def udp_tunnel_closed(_self):
        back.close()

    def udp_backend_received(self, data, _addr):
        tunnel.send(data)
        pending = tunnel.pending_bytes()
        if pending > BUFFER_SIZE:
            self.stop_receiving()

    def udp_backend_closed(_self):
        tunnel.close()

    tunnel.set_on_sent(udp_tunnel_sent)
    tunnel.set_on_received(udp_tunnel_received)
    tunnel.set_on_closed(udp_tunnel_closed)
    back.set_on_received(udp_backend_received)
    back.set_on_closed(udp_backend_closed)
    back.begin_receiving()
    # 10 min
    back.set_timeout(10 * 60 * 1000)
