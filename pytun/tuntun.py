import uuid
import struct
import socket
import re
import copy
import random
from watchdog.observers import Observer
from watchdog.events import PatternMatchingEventHandler
from tundevice import TunDevice
from tunnel import Tunnel
from dns import DNSRecord
from dns import QTYPE
from packet import Packet
from delegation import Delegation

import loglevel
_logger = loglevel.get_logger('tuntun')


def ip_string_to_long(ip):
    return struct.unpack('!I', socket.inet_pton(socket.AF_INET, ip))[0]


FAST_DNS_SERVER = ip_string_to_long('119.29.29.29')
CLEAN_DNS_SERVER = ip_string_to_long('8.8.8.8')
TEST_DNS_SERVER = ip_string_to_long('35.201.154.22')


poisoned_domain = set()
blocked_domain = set()
blocked_address = set()
normal_address = set()
modified_query = {}


def update_blocked_domain():
    global blocked_domain
    try:
        fp = open('blocked.txt', 'r')
    except IOError as e:
        _logger.warning("Failed to open blocked.txt: %s", str(e))
        return False
    new_blocked_domain = set()
    for line in fp.readlines():
        line = line.strip()
        if len(line) == 0 or line.startswith('#'):
            continue
        new_blocked_domain.add(line)
    fp.close()
    blocked_domain = new_blocked_domain
    _logger.info('updated %d blocked items', len(blocked_domain))


def update_poisoned_domain(domain):
    parts = domain.split('.')
    if len(parts) > 0 and len(parts[-1].strip()) == 0:
        parts = parts[: -1]
    if len(parts[-1]) == 2:
        name = '.'.join(parts[-3:]) + '.'
    else:
        name = '.'.join(parts[-2:]) + '.'
    old_len = len(poisoned_domain)
    poisoned_domain.add(name)
    if len(poisoned_domain) != old_len:
        try:
            fp = open('poisoned.txt', 'w')
            fp.write('\n'.join(poisoned_domain))
            fp.close()
        except IOError as e:
            _logger.warning("Failed to open poisoned.txt: %s", str(e))


def load_poisoned_domain():
    global poisoned_domain
    try:
        fp = open('poisoned.txt', 'r')
    except IOError as e:
        _logger.warning("Failed to open poisoned.txt: %s", str(e))
        return
    new_poisoned_domain = set()
    for line in fp.readlines():
        line = line.strip()
        if len(line) == 0 or line.startswith('#'):
            continue
        new_poisoned_domain.add(line)
    fp.close()
    poisoned_domain = new_poisoned_domain
    _logger.info('loaded %d poisoned items', len(poisoned_domain))


class MyHandler(PatternMatchingEventHandler):

    def __init__(self):
        super(MyHandler, self).__init__(patterns=['*blocked.txt'])

    def on_modified(self, event):
        update_blocked_domain()

    def on_created(self, event):
        update_blocked_domain()


update_blocked_domain()
load_poisoned_domain()
event_handler = MyHandler()
observer = Observer()
observer.schedule(event_handler, path='./', recursive=False)
observer.start()


def is_domain_blocked(domain):
    global blocked_domain
    global poisoned_domain
    m = re.search('([^.]+\.){2}[^.]{2}\.$', domain)
    if m is not None and (m.group(0) in blocked_domain or m.group(0) in poisoned_domain):
        return True
    else:
        m = re.search('[^.]+\.[^.]+\.$', domain)
        if m is not None and (m.group(0) in blocked_domain or m.group(0) in poisoned_domain):
            return True
    return False


def change_src(packet):
    packet.set_raw_source_ip(packet.get_raw_source_ip() + 1)


def need_restore(original, packet):
    return packet.get_raw_destination_ip() == original + 1


def restore_dst(packet):
    packet.set_raw_destination_ip(packet.get_raw_destination_ip() - 1)


def try_parse_dns_query(packet):
    if packet.is_udp() and packet.get_destination_port() == 53:
        try:
            ret = []
            query = DNSRecord.parse(packet.get_udp_load())
            for question in query.questions:
                name = str(question.get_qname())
                ret.append(name)
            return ret, query.header.id
        except object:
            _logger.warning("Failed to parse DNS query")
    return None, None


def try_parse_dns_result(packet):
    if packet.is_udp() and packet.get_source_port() == 53:
        try:
            ret = []
            result = DNSRecord.parse(packet.get_udp_load())
            for rr in result.rr:
                if rr.rtype == QTYPE.A:
                    addr, = struct.unpack('!I', struct.pack('!BBBB', *rr.rdata.data))
                    ret.append(addr)
            return ret, result.header.id, str(result.get_q().get_qname())
        except object:
            _logger.warning("Failed to parse DNS result")
    return None, None, None


def address2uuid(tun_addr, prefix, addr, port):
    packed = struct.pack('!IIHIH', 0, ip_string_to_long(tun_addr), prefix, addr, port)
    return uuid.UUID(bytes=packed)


def uuid2address(id_):
    _, tun_addr, prefix, addr, port = struct.unpack('!I4sHIH', id_.get_bytes())
    return socket.inet_ntop(socket.AF_INET, tun_addr), prefix, addr, port


def pack_dns_key(addr, port, id_):
    return struct.pack('!IHH', addr, port, id_)


def change_to_dns_server(packet, id_, server):
    key = pack_dns_key(packet.get_raw_source_ip(), packet.get_source_port(), id_)
    modified_query[key] = {
        'original': packet.get_raw_destination_ip(),
        'replaced': server
    }
    packet.set_raw_destination_ip(server)


def try_restore_dns(packet, id_):
    key = pack_dns_key(packet.get_raw_destination_ip(), packet.get_destination_port(), id_)
    if key in modified_query:
        original = modified_query[key]['original']
        replaced = modified_query[key]['replaced']
        if packet.get_raw_source_ip() == replaced:
            packet.set_raw_source_ip(original)
        del modified_query[key]


def test_domain_poisoned(tun, packet):
    copied = copy.deepcopy(packet)
    copied.set_raw_destination_ip(TEST_DNS_SERVER)
    copied.set_udp_load(0, 2, struct.pack('!H', random.randint(0, 0xffff)))
    change_src(copied)
    tun.send(copied.get_packet())


def gen_on_client_side_received(tundev, from_, via, to):

    from_addr = ip_string_to_long(from_[0])

    def on_tunnel_received(self, _, data):
        packet = Packet(data)
        addr_list, id_, _ = try_parse_dns_result(packet)
        if addr_list is not None:
            blocked_address.update(addr_list)
            try_restore_dns(packet, id_)
        self.send(packet.get_packet())

    def on_tunnel_closed(_):
        pass

    tundev.on_tunnel_received = on_tunnel_received
    tundev.on_tunnel_closed = on_tunnel_closed

    def connect_side_multiplex(tun_device, _, packet):
        if need_restore(from_addr, packet):
            restore_dst(packet)
            if packet.is_rst():
                _logger.info('%s has been reset', packet.get_source_ip())

            addr_list, id_, domain = try_parse_dns_result(packet)
            if addr_list is not None:
                if packet.get_raw_source_ip() == TEST_DNS_SERVER:
                    _logger.error('POISONED DOMAIN: %s', domain)
                    update_poisoned_domain(domain)
                    return
                else:
                    normal_address.update(addr_list)
                    try_restore_dns(packet, id_)
            tun_device.send(packet.get_packet())
            return

        through_tunnel = False
        dns_query = False

        domain_list, id_ = try_parse_dns_query(packet)
        if domain_list is not None:
            dns_query = True
            for domain in domain_list:
                through_tunnel = is_domain_blocked(domain)
                if through_tunnel:
                    break
            if through_tunnel:
                _logger.info("query: %s through tunnel", ', '.join(domain_list))
                change_to_dns_server(packet, id_, CLEAN_DNS_SERVER)
            else:
                _logger.info("query: %s through directly", ', '.join(domain_list))
                change_to_dns_server(packet, id_, FAST_DNS_SERVER)
                test_domain_poisoned(tun_device, packet)

        dst_ip = packet.get_raw_destination_ip()
        if not through_tunnel:
            if dst_ip in blocked_address:
                _logger.debug('address: %s sent via tunnel',
                              socket.inet_ntop(socket.AF_INET, struct.pack('!I', dst_ip)))
                through_tunnel = True

        if not through_tunnel and not dns_query and dst_ip not in normal_address:
            _logger.info('unknown address: %s sent directly',
                         socket.inet_ntop(socket.AF_INET, struct.pack('!I', dst_ip)))
            through_tunnel = False

        if not through_tunnel:
            change_src(packet)
            tun_device.send(packet.get_packet())
            return

        id_ = address2uuid(to[0], to[1], packet.get_raw_source_ip(), packet.get_source_port())
        tunnel = Delegation.get_tunnel(id_)
        if tunnel is None:
            tunnel = Tunnel(connect_to=via)
            tunnel.set_on_payload(Delegation.on_payload)
            tunnel.set_on_closed(Delegation.on_closed)
            tunnel.set_on_buffer_high(Delegation.set_on_buffer_high)
            tunnel.set_on_buffer_low(Delegation.set_on_buffer_low)
            tunnel.initialize()
        if Delegation.query_endpoint(id_) is None:
            Delegation.register(id_, tunnel, tun_device)
        # if proto == 'tcp':
        #     data = data * 2
        tunnel.send_tun_initial_data(id_, packet.get_packet())

    return connect_side_multiplex


to2tun = {}


def on_server_side_initialized(tunnel, id_, data):

    tun_addr, prefix, addr, port = uuid2address(id_)

    def on_received(_, data_, packet):
        id__ = address2uuid(tun_addr, prefix, packet.get_raw_destination_ip(), packet.get_destination_port())
        tunnel_ = Delegation.get_tunnel(id__)
        if tunnel_ is not None:
            tunnel_.send_payload(id__, data_)
        else:
            _logger.warning('unknown dst %s:%d', packet.get_destination_ip(), packet.get_destination_port())

    tun_addr_prefix = tun_addr + '/' + str(prefix)
    if tun_addr_prefix in to2tun:
        tun_device = to2tun[tun_addr_prefix]
    else:
        tun_device = TunDevice('tun', tun_addr, prefix)
        to2tun[tun_addr_prefix] = tun_device
        tun_device.set_on_received(on_received)
        tun_device.begin_receiving()
    if Delegation.query_endpoint(id_) is None:
        Delegation.register(id_, tunnel, tun_device)
    tun_device.send(data)


Tunnel.set_tun_initial_handler(on_server_side_initialized)
