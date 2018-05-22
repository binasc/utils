import json
from stream import Stream
from tunnel import Tunnel

import logging
import loglevel
_logger = loglevel.get_logger('tcptun', logging.INFO)


def gen_on_client_side_accepted(via, to):

    initial_data = json.dumps({
        'addr': to[0],
        'port': to[1]
    })

    def on_accepted(frontend, from_):
        tunnel = Tunnel(connect_to=via)

        def on_payload(_, data):
            frontend.send(data)

        def on_tunnel_closed(_):
            frontend.close()

        def on_frontend_received(_, data, _addr):
            tunnel.send_payload(data)

        def on_frontend_closed(_):
            tunnel.close()

        tunnel.set_on_payload(on_payload)
        tunnel.set_on_closed(on_tunnel_closed)
        tunnel.initialize()
        tunnel.send_tcp_initial_data(initial_data)
        frontend.set_on_received(on_frontend_received)
        frontend.set_on_closed(on_frontend_closed)
        frontend.begin_receiving()

        _logger.info("from %s:%d (%s to %s) connected" % (from_[0], from_[1], str(frontend), str(tunnel)))

    return on_accepted


def on_server_side_initialized(tunnel, initial_data):
    json_data = json.loads(initial_data)
    address, port = json_data['addr'], json_data['port']

    backend = Stream()

    def on_payload(_, data):
        backend.send(data)

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
    backend.connect(address, port)

    _logger.info('connect to: %s:%d (%s to %s)', address, port, str(tunnel), str(backend))


Tunnel.set_tcp_initial_handler(on_server_side_initialized)
