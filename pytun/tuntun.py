import json
import uuid
from tundevice import TunDevice
from tunnel import Tunnel

import loglevel
_logger = loglevel.get_logger('tuntun')


BUFFER_SIZE = 8 * (1024 ** 2)
MAX_CONNECTION = 64


def gen_on_client_side_received(via, to):

    def gen_initial_data(src):
        return json.dumps({
            'addr': to[0],
            'prefix': to[1],
            'srcAddr': src[0],
            'srcPort': src[1],
        })

    def connect_side_multiplex(tun_device, data, _proto, src, _dst):
        src_addr, src_port = src
        addr_port = src_addr + ':' + str(src_port)
        sid = hash(addr_port) % MAX_CONNECTION

        tunnel = None
        try:
            tunnel = tun_device.src2Stream[sid]
        except AttributeError:
            tun_device.src2Stream = [None] * MAX_CONNECTION

        if tunnel is None:
            tunnel = Tunnel(connect_to=via)
            tun_device.src2Stream[sid] = tunnel

            def on_payload(_, data_):
                tun_device.send(data_)

            def on_tunnel_closed(_):
                tun_device.src2Stream[sid] = None

            tunnel.set_on_payload(on_payload)
            tunnel.set_on_closed(on_tunnel_closed)
            tunnel.initialize()
            tunnel.send_tun_initial_data(gen_initial_data(src))

            _logger.debug('new TunCon from: %s:%d (%s)', src_addr, src_port, str(tunnel))

        # if proto == 'tcp':
        #     data = data * 2
        tunnel.send_payload(data)

    return connect_side_multiplex


to2tun = {}


def on_server_side_initialized(tunnel, initial_data):
    json_data = json.loads(initial_data)
    addr = json_data['addr']
    prefix = json_data['prefix']
    src_addr = json_data['srcAddr']
    src_port = json_data['srcPort']

    def tun_device_received(tun_device_, data, _proto, _src, dst):
        dst_addr, dst_port = dst
        dst_sid = hash(dst_addr + ':' + str(dst_port)) % MAX_CONNECTION
        # if proto == 'tcp':
        #     data = data * 2
        if dst_sid in tun_device_.dst2Stream:
            tunnel_ = tun_device_.dst2Stream[dst_sid]
        else:
            dst_sid = hash(dst_addr + ':0') % MAX_CONNECTION
            if dst_sid in tun_device_.dst2Stream:
                tunnel_ = tun_device_.dst2Stream[dst_sid]
            else:
                _logger.warning('unknown dst %s:%d', dst_addr, dst_port)
                return

        tunnel_.send_payload(data)

    addr_prefix = addr + '/' + str(prefix)
    if addr_prefix in to2tun:
        tun_device = to2tun[addr_prefix]
    else:
        tun_device = TunDevice('tun', addr, prefix)
        tun_device.dst2Stream = {}
        to2tun[addr_prefix] = tun_device
        tun_device.set_on_received(tun_device_received)
        tun_device.begin_receiving()

    src_sid = hash(src_addr + ':' + str(src_port)) % MAX_CONNECTION
    try:
        _ = tunnel.uuid
    except AttributeError:
        tunnel.uuid = str(uuid.uuid4())
    if src_sid in tun_device.dst2Stream:
        if tun_device.dst2Stream[src_sid].uuid != tunnel.uuid:
            tun_device.dst2Stream[src_sid].close()
    tun_device.dst2Stream[src_sid] = tunnel

    def on_payload(_, data):
        tun_device.send(data)

    def on_closed(_):
        if src_sid in tun_device.dst2Stream:
            if tun_device.dst2Stream[src_sid].uuid == tunnel.uuid:
                del tun_device.dst2Stream[src_sid]

    tunnel.set_on_payload(on_payload)
    tunnel.set_on_closed(on_closed)
    tunnel.set_on_buffer_high(lambda _: tun_device.stop_receiving())
    tunnel.set_on_buffer_low(lambda _: tun_device.begin_receiving())


Tunnel.set_tun_initial_handler(on_server_side_initialized)
