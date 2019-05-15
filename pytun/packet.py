import struct
import socket


class Packet(object):

    PROTO_TCP = 6
    PROTO_UDP = 17

    def __init__(self, packet):
        self._packet = packet
        self._ip = None
        self._udp = None
        self._tcp = None
        self._ip_delta = 0
        self._udp_delta = 0
        self._tcp_delta = 0

        # cache
        self._source_ip_str = None
        self._destination_ip_str = None

    def _parse_ip(self):
        if self._ip is None:
            ver_ihl, = struct.unpack('!B', self._packet[:1])
            ver = (ver_ihl >> 4) & 0x0f
            if ver == 4:
                ihl = ver_ihl & 0x0f
                packet_length, _, _, protocol, checksum, sip, dip =\
                    struct.unpack('!HIBBHII', self._packet[2:20])
                self._ip = {
                    'length': ihl * 4,
                    'version': ver,
                    'protocol': protocol,
                    'checksum': checksum,
                    'sip': sip,
                    'dip': dip,
                    'packet_length': packet_length
                }
            elif ver == 6:
                payload_length, _, sip_hi, sip_lo, dip_hi, dip_lo =\
                    struct.unpack('!HHQQQQ', self._packet[4: 40])
                self._ip = {
                    'length': 40,
                    'version': ver,
                    'protocol': None,
                    'checksum': None,
                    'sip': (sip_hi << 64) | sip_lo,
                    'dip': (dip_hi << 64) | dip_lo,
                    'packet_length': 40 + payload_length
                }

    @staticmethod
    def _checksum(original, delta):
        if delta > 0:
            checksum = (~original) & 0xffff
            checksum += delta
            while checksum > 0xffff:
                checksum = (checksum & 0xffff) + (((checksum & 0xffff0000) >> 16) & 0xffff)
            return (~checksum) & 0xffff
        else:
            checksum = original - delta
            while checksum > 0xffff:
                checksum = (checksum & 0xffff) + (((checksum & 0xffff0000) >> 16) & 0xffff)
            return checksum

    def get_packet(self):
        if self._ip_delta != 0:
            checksum = self._checksum(self._ip['checksum'], self._ip_delta)
            self._packet = self._packet[0: 10] + struct.pack('!H', checksum) + self._packet[12:]
            self._ip['checksum'] = checksum
        if self._udp_delta != 0:
            if self._udp['checksum'] != 0:
                checksum = self._checksum(self._udp['checksum'], self._udp_delta)
                offset = self._ip['length']
                self._packet = self._packet[0: offset + 6] + struct.pack('!H', checksum) + self._packet[offset + 8:]
                self._udp['checksum'] = checksum
        if self._tcp_delta != 0:
            checksum = self._checksum(self._tcp['checksum'], self._tcp_delta)
            offset = self._ip['length']
            self._packet = self._packet[0: offset + 16] + struct.pack('!H', checksum) + self._packet[offset + 18:]
            self._tcp['checksum'] = checksum
        return self._packet

    def _parse_udp(self):
        self._parse_ip()
        offset = self._ip['length']
        sport, dport, _, checksum = struct.unpack('!HHHH', self._packet[offset: offset + 8])
        self._udp = {
            'sport': sport,
            'dport': dport,
            'checksum': checksum
        }

    def _parse_tcp(self):
        self._parse_ip()
        offset = self._ip['length']
        sport, dport, _, _, _, flags, _, checksum = struct.unpack('!HHIIBBHH', self._packet[offset: offset + 18])
        self._tcp = {
            'sport': sport,
            'dport': dport,
            'checksum': checksum,
            'flags': flags
        }

    def get_protocol(self):
        if self.is_udp():
            return 'udp'
        if self.is_tcp():
            return 'tcp'
        return 'other'

    def get_packet_length(self):
        self._parse_ip()
        return self._ip['packet_length']

    def is_ipv4(self):
        self._parse_ip()
        return self._ip['version'] == 4

    def is_ipv6(self):
        self._parse_ip()
        return self._ip['version'] == 6

    def is_udp(self):
        self._parse_ip()
        return self._ip['protocol'] == self.PROTO_UDP

    def is_tcp(self):
        self._parse_ip()
        return self._ip['protocol'] == self.PROTO_TCP

    def is_rst(self):
        if self.is_tcp():
            self._parse_tcp()
            return self._tcp['flags'] & 0x20 != 0
        return False

    def get_raw_source_ip(self):
        self._parse_ip()
        return self._ip['sip']

    def get_source_ip(self):
        self._parse_ip()
        if self._source_ip_str is None:
            if self.is_ipv4():
                self._source_ip_str = socket.inet_ntop(
                    socket.AF_INET, struct.pack('!I', self._ip['sip']))
            elif self.is_ipv6():
                sip = self._ip['sip']
                self._source_ip_str = socket.inet_ntop(
                    socket.AF_INET6, struct.pack('!QQ', sip >> 64, sip & 0xFFFFFFFFFFFFFFFFL))
        return self._source_ip_str

    def get_raw_destination_ip(self):
        self._parse_ip()
        return self._ip['dip']

    def get_destination_ip(self):
        self._parse_ip()
        if self._destination_ip_str is None:
            if self.is_ipv4():
                self._destination_ip_str = socket.inet_ntop(
                    socket.AF_INET, struct.pack('!I', self._ip['dip']))
            elif self.is_ipv6():
                dip = self._ip['dip']
                self._destination_ip_str = socket.inet_ntop(
                    socket.AF_INET6, struct.pack('!QQ', dip >> 64, dip & 0xFFFFFFFFFFFFFFFFL))
        return self._destination_ip_str

    def get_source_port(self):
        if not self.is_udp() and not self.is_tcp():
            return 0
        if self.is_udp():
            self._parse_udp()
            return self._udp['sport']
        if self.is_tcp():
            self._parse_tcp()
            return self._tcp['sport']

    def get_destination_port(self):
        if not self.is_udp() and not self.is_tcp():
            return 0
        if self.is_udp():
            self._parse_udp()
            return self._udp['dport']
        if self.is_tcp():
            self._parse_tcp()
            return self._tcp['dport']

    def set_raw_source_ip(self, ip):
        self._parse_ip()
        delta = (ip & 0xffff) - (self._ip['sip'] & 0xffff)
        delta += ((ip >> 16) & 0xffff) - ((self._ip['sip'] >> 16) & 0xffff)
        self._ip['sip'] = ip
        self._packet = self._packet[0: 12] + struct.pack('!I', ip) + self._packet[16:]
        self._ip_delta += delta
        if self.is_udp():
            self._parse_udp()
            self._udp_delta += delta
        if self.is_tcp():
            self._parse_tcp()
            self._tcp_delta += delta
        self._source_ip_str = None

    def set_raw_destination_ip(self, ip):
        self._parse_ip()
        delta = (ip & 0xffff) - (self._ip['dip'] & 0xffff)
        delta += ((ip >> 16) & 0xffff) - ((self._ip['dip'] >> 16) & 0xffff)
        self._ip['dip'] = ip
        self._packet = self._packet[0: 16] + struct.pack('!I', ip) + self._packet[20:]
        self._ip_delta += delta
        if self.is_udp():
            self._parse_udp()
            self._udp_delta += delta
        if self.is_tcp():
            self._parse_tcp()
            self._tcp_delta += delta
        self._destination_ip_str = None

    def get_udp_load(self):
        offset = self._ip['length']
        return self._packet[offset + 8:]

    def set_udp_load(self, begin, length, replacement):
        offset = self._ip['length'] + 8 + begin
        # TODO: support checksum entire packet
        new, old = struct.unpack('!HH', replacement + self._packet[offset: offset + length])
        self._packet = self._packet[: offset] + replacement + self._packet[offset + length:]
        delta = new - old
        if self.is_udp():
            self._parse_udp()
            self._udp_delta += delta
