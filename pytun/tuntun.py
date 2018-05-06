import json
import common
from event import Event
from stream import Stream
from tundevice import TunDevice
import uuid

import loglevel
_logger = loglevel.get_logger('tuntun')


BUFFER_SIZE = 8 * (1024 ** 2)
MAX_CONNECTION = 64

to2tun = {}


def gen_on_received(via, to):
    via_addr, via_port = via
    # to_addr, to_port = to

    def generate_header(src):
        return common.wrap_content(json.dumps({
            'type': 'tun',
            'addr': to[0],
            'port': to[1],
            'srcAddr': src[0],
            'srcPort': src[1],
        }))

    def connect_side_multiplex(tun_device, data, _proto, src, _dst):
        src_addr, src_port = src
        addr_port = src_addr + ':' + str(src_port)
        sid = hash(addr_port) % MAX_CONNECTION

        tunnel = None
        try:
            tunnel = tun_device.src2Stream[sid]
        except Exception:
            tun_device.src2Stream = [None] * MAX_CONNECTION

        if tunnel is None:
            tunnel = Stream()
            tun_device.src2Stream[sid] = tunnel
            _logger.debug('new TunCon from: %s:%d (%s)', src_addr, src_port, str(tunnel))

            tunnel.send(generate_header(src))

            if src_port == 0:
                tunnel.set_timeout(60 * 1000)
            else:
                tunnel.set_timeout(10 * 60 * 1000)

            def tunnel_received(_self, data_, _addr):
                tun_device.send(data_)

            def reconnect_handler(_ev):
                if tun_device.src2Stream[sid] is not None:
                    return
                tunnel_ = Stream()
                tunnel_.connect(via_addr, via_port)
                common.initialize_tunnel(tunnel_)
                tun_device.src2Stream[sid] = tunnel_
                _logger.debug('reconnect TunCon from: %s:%d', src_addr, src_port)
                tunnel_.send(generate_header(src))
                tunnel_.set_timeout(60 * 1000)
                tunnel_.set_on_received(tunnel_received)
                tunnel_.set_on_closed(tunnel_closed)

            def tunnel_closed(_self):
                tun_device.src2Stream[sid] = None
                if src_port == 0:
                    reconnect_ev = Event.add_timer(500)
                    reconnect_ev.set_handler(reconnect_handler)

            tunnel.set_on_received(tunnel_received)
            tunnel.set_on_closed(tunnel_closed)

            tunnel.connect(via_addr, via_port)
            common.initialize_tunnel(tunnel)

        # if proto == 'tcp':
        #     data = data * 2
        tunnel.send(data)

    return connect_side_multiplex


def accept_side_receiver(tunnel, header):
    addr = header['addr']
    port = header['port']
    src_addr = header['srcAddr']
    src_port = header['srcPort']

    def tun_device_received(self, data, _proto, _src, dst):
        dst_addr, dst_port = dst
        dst_sid = hash(dst_addr + ':' + str(dst_port)) % MAX_CONNECTION
        # if proto == 'tcp':
        #     data = data * 2
        if dst_sid in self.dst2Stream:
            tunnel_ = self.dst2Stream[dst_sid]
        else:
            dst_sid = hash(dst_addr + ':0') % MAX_CONNECTION
            if dst_sid in self.dst2Stream:
                tunnel_ = self.dst2Stream[dst_sid]
            else:
                _logger.warning('unknown dst %s:%d', dst_addr, dst_port)
                return

        before = tunnel_.pending_bytes()
        tunnel_.send(data)
        after = tunnel_.pending_bytes()
        self.pending += (after - before)
        if self.pending > BUFFER_SIZE:
            self.stop_receiving()

    addr_port = addr + ':' + str(port)
    if addr_port in to2tun:
        tun_device = to2tun[addr_port]
    else:
        # port as cidr prefix
        tun_device = TunDevice('tun', addr, port, None)
        tun_device.pending = 0
        tun_device.dst2Stream = {}
        tun_device.set_on_received(tun_device_received)
        tun_device.begin_receiving()
        to2tun[addr_port] = tun_device

    src_sid = hash(src_addr + ':' + str(src_port)) % MAX_CONNECTION
    try:
        _ = tunnel.uuid
    except:
        tunnel.uuid = str(uuid.uuid4())
    if src_sid in tun_device.dst2Stream:
        if tun_device.dst2Stream[src_sid].uuid != tunnel.uuid:
            tun_device.dst2Stream[src_sid].close()
    tun_device.dst2Stream[src_sid] = tunnel

    def tun_device_tunnel_sent(_self, sent, _remain):
        tun_device.pending -= sent
        if tun_device.pending <= BUFFER_SIZE:
            tun_device.begin_receiving()

    def tun_device_tunnel_received(_self, data, _addr):
        tun_device.send(data)

    def tun_device_tunnel_closed(self):
        left = self.pending_bytes()
        tun_device.pending -= left
        if tun_device.pending <= BUFFER_SIZE:
            tun_device.begin_receiving()

        if src_sid in tun_device.dst2Stream:
            if tun_device.dst2Stream[src_sid].uuid == self.uuid:
                del tun_device.dst2Stream[src_sid]

    tunnel.set_on_sent(tun_device_tunnel_sent)
    tunnel.set_on_received(tun_device_tunnel_received)
    tunnel.set_on_closed(tun_device_tunnel_closed)
