import json
import uuid
from stream import Stream
from tunnel import Tunnel

import loglevel
_logger = loglevel.get_logger('tcptun', loglevel.DEFAULT_LEVEL)


key_to_tunnels = {}


def gen_on_client_side_accepted(via, to):

    initial_data = json.dumps({
        'addr': to[0],
        'port': to[1]
    })

    def on_accepted(endpoint, from_):

        def on_received(self_, data, _):
            tunnel.send_payload(self_.uuid, data)
            return tunnel.is_ready_to_send()

        def on_fin_received(self_):
            tunnel.send_tcp_fin_data(self_.uuid)

        def on_closed(self_):
            if self_.close_by_tunnel is False:
                tunnel.send_tcp_closed_data(self_.uuid)
            tunnel.deregister(self_.uuid)

        def on_tunnel_received(_, id_, data):
            endpoint_ = tunnel.get_connection(id_)
            if endpoint_ is None:
                _logger.warning('connection: %s has gone, %d bytes data not sent', str(id_), len(data))
            else:
                endpoint_.send(data)

        def on_tunnel_ready_to_send(_):
            for endpoint_ in tunnel.connections.values():
                endpoint_.start_receiving()

        def on_tunnel_send_buffer_full(_):
            for endpoint_ in tunnel.connections.values():
                endpoint_.stop_receiving()

        def on_tunnel_closed(_):
            for endpoint_ in tunnel.connections.values():
                endpoint_.close()
            tunnel.clear_connections()

        endpoint.uuid = uuid.uuid4()
        endpoint.close_by_tunnel = False

        key = endpoint.uuid.int % 64

        tunnel = None
        if key in key_to_tunnels:
            tunnel = key_to_tunnels[key]
        if tunnel is None or tunnel.is_closed():
            tunnel = Tunnel(connect_to=via)
            tunnel.set_on_ready_to_send(on_tunnel_ready_to_send)
            tunnel.set_on_send_buffer_full(on_tunnel_send_buffer_full)
            tunnel.set_on_payload(on_tunnel_received)
            tunnel.set_on_closed(on_tunnel_closed)
            tunnel.initialize()
            key_to_tunnels[key] = tunnel

        tunnel.register(endpoint.uuid, endpoint)

        endpoint.set_on_received(on_received)
        endpoint.set_on_fin_received(on_fin_received)
        endpoint.set_on_closed(on_closed)
        endpoint.start_receiving()

        tunnel.send_tcp_initial_data(endpoint.uuid, initial_data)

        _logger.info("from %s:%d (%s) connected", from_[0], from_[1], str(endpoint))

    return on_accepted


def on_server_side_initialized(tunnel, id_, initial_data):

    def on_received(self_, data, _):
        tunnel.send_payload(self_.uuid, data)
        return tunnel.is_ready_to_send()

    def on_fin_received(self_):
        tunnel.send_tcp_fin_data(self_.uuid)

    def on_closed(self_):
        if self_.close_by_tunnel is False:
            tunnel.send_tcp_closed_data(self_.uuid)
        tunnel.deregister(self_.uuid)

    def on_tunnel_received(_, id__, data):
        endpoint_ = None
        if id__ in tunnel.connections:
            endpoint_ = tunnel.connections[id__]
        if endpoint_ is None:
            _logger.warning('connection: %s has gone, %d bytes data not sent', str(id__), len(data))
        else:
            endpoint_.send(data)

    def on_tunnel_ready_to_send(_):
        for endpoint_ in tunnel.connections.values():
            endpoint_.start_receiving()

    def on_tunnel_send_buffer_full(_):
        for endpoint_ in tunnel.connections.values():
            endpoint_.stop_receiving()

    def on_tunnel_closed(_):
        for endpoint_ in tunnel.connections.values():
            endpoint_.close()
        tunnel.clear_connections()

    endpoint = Stream()
    endpoint.uuid = id_
    endpoint.close_by_tunnel = False

    tunnel.register(endpoint.uuid, endpoint)

    tunnel.set_on_ready_to_send(on_tunnel_ready_to_send)
    tunnel.set_on_send_buffer_full(on_tunnel_send_buffer_full)
    tunnel.set_on_payload(on_tunnel_received)
    tunnel.set_on_closed(on_tunnel_closed)

    endpoint.set_on_received(on_received)
    endpoint.set_on_fin_received(on_fin_received)
    endpoint.set_on_closed(on_closed)

    json_data = json.loads(initial_data)
    address, port = json_data['addr'], json_data['port']
    endpoint.connect(address, port)
    _logger.info('connect to: %s:%d (%s)', address, port, str(endpoint))


def on_stream_fin_received(tunnel_, id_, _):
    endpoint = tunnel_.get_connection(id_)
    if endpoint is not None:
        _logger.info("(%s) %s fin received", str(endpoint), str(id_))
        endpoint.shutdown()
        endpoint.fin_by_tunnel = True
    else:
        _logger.warning('no such tcp connection: %s', str(id_))


def on_stream_closed(tunnel_, id_, _):
    endpoint = tunnel_.get_connection(id_)
    if endpoint is not None:
        _logger.info("close (%s) %s by other side", str(endpoint), str(id_))
        endpoint.close()
        endpoint.close_by_tunnel = True
    else:
        _logger.warning('no such tcp connection: %s', str(id_))
