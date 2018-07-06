import json
import uuid
import struct
import socket
import re
import time
from tundevice import TunDevice
from tunnel import Tunnel
from dns import DNSRecord
from dns import QTYPE
from packet import Packet

import loglevel
_logger = loglevel.get_logger('tuntun')


BUFFER_SIZE = 8 * (1024 ** 2)
MAX_CONNECTION = 64


blocked_domain_last_update = 0
blocked_domain = set()
blocked_address = set()
normal_address = set()


def is_domain_blocked(domain):
    global blocked_domain
    global blocked_domain_last_update
    current = time.time()
    if current - blocked_domain_last_update > 10 * 60:
        fp = open('blocked.txt', 'r')
        new_blocked_domain = set()
        for line in fp.readlines():
            line = line.strip()
            if len(line) == 0 or line.startswith('#'):
                continue
            new_blocked_domain.add(line)
        fp.close()
        blocked_domain = new_blocked_domain
        blocked_domain_last_update = current
        _logger.info('updated %d items', len(blocked_domain))

    m = re.search('([^.]+\.){2}[^.]{2}\.$', domain)
    if m is not None and m.group(0) in blocked_domain:
        return True
    else:
        m = re.search('[^.]+\.[^.]+\.$', domain)
        if m is not None and m.group(0) in blocked_domain:
            return True
    return False


def gen_on_client_side_received(from_, via, to):

    from_addr, = struct.unpack('!I', socket.inet_pton(socket.AF_INET, from_[0]))

    def change_src(packet):
        packet.set_raw_source_ip(packet.get_raw_source_ip() + 1)

    def restore_dst(packet):
        packet.set_raw_destination_ip(packet.get_raw_destination_ip() - 1)

    def gen_initial_data(src):
        return json.dumps({
            'addr': to[0],
            'prefix': to[1],
            'srcAddr': src[0],
            'srcPort': src[1],
        })

    def connect_side_multiplex(tun_device, data, packet):
        dst_ip = packet.get_raw_destination_ip()
        if dst_ip == from_addr + 1:
            restore_dst(packet)
            if packet.is_udp() and packet.get_source_port() == 53:
                try:
                    result = DNSRecord.parse(packet.get_udp_load())
                    for rr in result.rr:
                        if rr.rtype == QTYPE.A:
                            addr, = struct.unpack('!I', struct.pack('!BBBB', *rr.rdata.data))
                            _logger.info('normal address: %s has been recorded',
                                         socket.inet_ntop(socket.AF_INET, struct.pack('!I', addr)))
                            normal_address.add(addr)
                except object:
                    _logger.warning("Failed to parse DNS packet")
            tun_device.send(packet.get_packet())
            return

        src = (packet.get_source_ip(), packet.get_source_port())
        src_addr, src_port = src[0], src[1]
        addr_port = src_addr + ':' + str(src_port)
        sid = hash(addr_port) % MAX_CONNECTION

        through_tunnel = False
        dns_query = False

        if packet.is_udp() and packet.get_destination_port() == 53:
            try:
                query = DNSRecord.parse(packet.get_udp_load())
                for question in query.questions:
                    name = str(question.get_qname())
                    through_tunnel = is_domain_blocked(name)
                    if through_tunnel:
                        _logger.info("query: %s through tunnel", name)
                    else:
                        _logger.info("query: %s directly", name)
                    dns_query = True
            except object:
                _logger.warning("Failed to parse DNS packet")

        if not through_tunnel and dst_ip in blocked_address:
            _logger.debug('address: %s sent via tunnel', socket.inet_ntop(socket.AF_INET, struct.pack('!I', dst_ip)))
            through_tunnel = True

        if not through_tunnel and not dns_query and dst_ip not in normal_address:
            _logger.info('unknown address: %s sent via tunnel', socket.inet_ntop(socket.AF_INET, struct.pack('!I', dst_ip)))
            through_tunnel = True

        if not through_tunnel:
            change_src(packet)
            tun_device.send(packet.get_packet())
            return

        tunnel = None
        try:
            tunnel = tun_device.src2Stream[sid]
        except AttributeError:
            tun_device.src2Stream = [None] * MAX_CONNECTION

        if tunnel is None:
            tunnel = Tunnel(connect_to=via)
            tun_device.src2Stream[sid] = tunnel

            def on_payload(_, data_):
                packet_ = Packet(data_)
                if packet_.is_udp() and packet_.get_source_port() == 53:
                    try:
                        result_ = DNSRecord.parse(packet_.get_udp_load())
                        for rr_ in result_.rr:
                            if rr_.rtype == QTYPE.A:
                                addr_, = struct.unpack('!I', struct.pack('!BBBB', *rr_.rdata.data))
                                blocked_address.add(addr_)
                                _logger.info('blocked address: %s has been recorded',
                                              socket.inet_ntop(socket.AF_INET, struct.pack('!I', addr_)))
                    except object:
                        _logger.warning("Failed to parse DNS packet")

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

    def tun_device_received(tun_device_, data, packet):
        dst_addr, dst_port = packet.get_destination_ip(), packet.get_destination_port()
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

    def on_tunnel_high(_):
        tun_device.stop_receiving()

    def on_tunnel_low(_):
        tun_device.begin_receiving()

    tunnel.set_on_payload(on_payload)
    tunnel.set_on_closed(on_closed)
    tunnel.set_on_buffer_high(on_tunnel_high)
    tunnel.set_on_buffer_low(on_tunnel_low)


Tunnel.set_tun_initial_handler(on_server_side_initialized)
