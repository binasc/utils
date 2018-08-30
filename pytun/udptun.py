import json
import socket
import struct
import uuid
from dgram import Dgram
from tunnel import Tunnel
from delegation import Delegation

import logging
import loglevel
_logger = loglevel.get_logger('udptun', logging.INFO)


def address2uuid(addr, port):
    addr_str = socket.inet_pton(socket.AF_INET, addr)
    packed = struct.pack('!QH4sH', uuid.getnode(), 0, addr_str, port)
    return uuid.UUID(bytes=packed)


def uuid2address(id_):
    _, _, addr_str, port = struct.unpack('!QH4sH', id_.get_bytes())
    return socket.inet_ntop(socket.AF_INET, addr_str), port


def on_tunnel_received(self_, id_, data):
    if hasattr(self_, 'to'):
        addr, port = self_.to
    else:
        addr, port = uuid2address(id_)
    self_.send(data, (addr, port))


def on_tunnel_closed(_):
    pass


def gen_on_client_side_received(via, to):

    initial_data = json.dumps({
        'addr': to[0],
        'port': to[1]
    })

    def on_received(endpoint, data, from_):
        id_ = address2uuid(*from_)
        tunnel = Delegation.get_tunnel(id_)
        if tunnel is None:
            tunnel = Tunnel(connect_to=via)
            tunnel.set_on_payload(Delegation.on_payload)
            tunnel.set_on_closed(Delegation.on_closed)
            tunnel.set_on_buffer_high(Delegation.set_on_buffer_high)
            tunnel.set_on_buffer_low(Delegation.set_on_buffer_low)
            tunnel.initialize()
        if Delegation.query_endpoint(id_) is None:
            tunnel.send_udp_initial_data(id_, initial_data)
            Delegation.register(id_, tunnel, endpoint)
        tunnel.send_payload(id_, data)

    return on_received


def on_server_side_initialized(tunnel, id_, initial_data):
    json_data = json.loads(initial_data)
    address, port = json_data['addr'], json_data['port']

    endpoint = Delegation.query_endpoint(id_)
    if endpoint is None:
        endpoint = Dgram()
        endpoint.uuid = id_
        endpoint.to = (address, port)
    Delegation.register(endpoint.uuid, tunnel, endpoint)

    def on_received(self_, data, _):
        tunnel_ = Delegation.get_tunnel(self_.uuid)
        if tunnel_ is not None:
            tunnel_.send_payload(self_.uuid, data)

    def on_closed(self_):
        tunnel_ = Delegation.get_tunnel(self_.uuid)
        if tunnel_ is not None:
            tunnel_.send_udp_closed_data(self_.uuid)

    endpoint.set_on_received(on_received)
    endpoint.set_on_closed(on_closed)
    endpoint.on_tunnel_received = on_tunnel_received
    endpoint.on_tunnel_closed = on_tunnel_closed
    endpoint.begin_receiving()
    # 10 min
    endpoint.set_timeout(10 * 60 * 1000)

    _logger.info('new datagram to: %s:%d (%s)', address, port, str(endpoint))


def on_dgram_closed(_tunnel, id_, _):
    Delegation.de_register(id_)
