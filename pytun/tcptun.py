import json
import uuid
from stream import Stream
from tunnel import Tunnel
from delegation import Delegation

import logging
import loglevel
_logger = loglevel.get_logger('tcptun', logging.INFO)


def gen_on_client_side_accepted(via, to):

    initial_data = json.dumps({
        'addr': to[0],
        'port': to[1]
    })

    def on_accepted(endpoint, from_):
        endpoint.uuid = uuid.uuid4()
        tunnel = Delegation.get_tunnel(endpoint.uuid)
        if tunnel is None:
            tunnel = Tunnel(connect_to=via)
            tunnel.set_on_payload(Delegation.on_payload)
            tunnel.set_on_closed(Delegation.on_closed)
            tunnel.initialize()
        tunnel.send_tcp_initial_data(endpoint.uuid, initial_data)
        Delegation.register(endpoint.uuid, tunnel, endpoint)

        def on_received(self_, data, _):
            tunnel_ = Delegation.get_tunnel(self_.uuid)
            if tunnel_ is not None:
                tunnel_.send_payload(self_.uuid, data)
            else:
                self_.close()

        def on_closed(self_):
            tunnel_ = Delegation.get_tunnel(self_.uuid)
            if tunnel_ is not None:
                tunnel_.send_tcp_closed_data(self_.uuid)

        def on_tunnel_received(self_, _, data):
            self_.send(data)

        def on_tunnel_closed(self_):
            self_.close()

        endpoint.set_on_received(on_received)
        endpoint.set_on_closed(on_closed)
        endpoint.on_tunnel_received = on_tunnel_received
        endpoint.on_tunnel_closed = on_tunnel_closed
        endpoint.begin_receiving()

        _logger.info("from %s:%d (%s) connected", from_[0], from_[1], str(endpoint))

    return on_accepted


def on_server_side_initialized(tunnel, id_, initial_data):
    json_data = json.loads(initial_data)
    address, port = json_data['addr'], json_data['port']

    endpoint = Stream()
    endpoint.uuid = id_
    Delegation.register(endpoint.uuid, tunnel, endpoint)

    def on_received(self_, data, _):
        tunnel_ = Delegation.get_tunnel(self_.uuid)
        if tunnel_ is not None:
            tunnel_.send_payload(self_.uuid, data)
        else:
            self_.close()

    def on_closed(self_):
        tunnel_ = Delegation.get_tunnel(self_.uuid)
        if tunnel_ is not None:
            tunnel_.send_tcp_closed_data(self_.uuid)

    def on_tunnel_received(self_, _, data):
        self_.send(data)

    def on_tunnel_closed(self_):
        self_.close()

    endpoint.set_on_received(on_received)
    endpoint.set_on_closed(on_closed)
    endpoint.on_tunnel_received = on_tunnel_received
    endpoint.on_tunnel_closed = on_tunnel_closed
    endpoint.connect(address, port)

    _logger.info('connect to: %s:%d (%s)', address, port, str(endpoint))


def on_stream_closed(_tunnel, id_, _):
    endpoint = Delegation.query_endpoint(id_)
    if endpoint is not None:
        endpoint.close()
    Delegation.de_register(id_)
