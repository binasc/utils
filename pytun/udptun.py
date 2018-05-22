import json
from dgram import Dgram
from tunnel import Tunnel

import logging
import loglevel
_logger = loglevel.get_logger('udptun', logging.INFO)


addr2Stream = {}


def gen_on_client_side_received(via, to):

    initial_data = json.dumps({
        'addr': to[0],
        'port': to[1]
    })

    def on_received(frontend, data, from_):
        addr_port = '%s:%d' % from_

        if addr_port in addr2Stream:
            tunnel = addr2Stream[addr_port]
        else:
            tunnel = Tunnel(connect_to=via)
            addr2Stream[addr_port] = tunnel

            def on_payload(_, data_):
                _logger.debug("%s:%d <-- %s:%d %d bytes" % (
                    from_[0], from_[1], to[0], int(to[1]), len(data_)))
                frontend.send(data_, from_)

            def on_tunnel_closed(_):
                del addr2Stream[addr_port]

            tunnel.set_on_payload(on_payload)
            tunnel.set_on_closed(on_tunnel_closed)
            tunnel.initialize()
            tunnel.send_udp_initial_data(initial_data)

            _logger.info('new datagram from: %s:%d (%s to %s)' % (from_[0], from_[1], str(frontend), str(tunnel)))

        tunnel.send_payload(data)

    return on_received


def on_server_side_initialized(tunnel, initial_data):
    json_data = json.loads(initial_data)
    address, port = json_data['addr'], json_data['port']

    backend = Dgram()

    def on_payload(_, data):
        backend.send(data, (address, port))

    def on_tunnel_closed(_):
        backend.close()

    def on_backend_received(_, data, _addr):
        tunnel.send_payload(data)

    def on_backend_closed(_):
        tunnel.close()

    tunnel.set_on_payload(on_payload)
    tunnel.set_on_closed(on_tunnel_closed)
    tunnel.set_on_buffer_high(lambda _: backend.stop_receiving())
    tunnel.set_on_buffer_low(lambda _: backend.begin_receiving())
    backend.set_on_received(on_backend_received)
    backend.set_on_closed(on_backend_closed)
    backend.begin_receiving()
    # 10 min
    backend.set_timeout(10 * 60 * 1000)

    _logger.info('new datagram to: %s:%d (%s to %s)', address, port, str(tunnel), str(backend))


Tunnel.set_udp_initial_handler(on_server_side_initialized)
