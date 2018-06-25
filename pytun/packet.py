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

    def _parse_ip(self):
        if self._ip is None:
            ver_ihl, _, packet_length, _, _, protocol, checksum, sip, dip =\
                struct.unpack('!BBHIBBHII', self._packet[:20])
            ihl = ver_ihl & 0x0f
            ver = (ver_ihl >> 4) & 0x0f
            self._ip = {
                'length': ihl * 4,
                'version': ver,
                'protocol': protocol,
                'checksum': checksum,
                'sip': sip,
                'dip': dip,
                'packet_length': packet_length
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
        sport, dport, _, _, _, checksum = struct.unpack('!HHIIIH', self._packet[offset: offset + 18])
        self._tcp = {
            'sport': sport,
            'dport': dport,
            'checksum': checksum
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

    def is_udp(self):
        self._parse_ip()
        return self._ip['protocol'] == self.PROTO_UDP

    def is_tcp(self):
        self._parse_ip()
        return self._ip['protocol'] == self.PROTO_TCP

    def get_raw_source_ip(self):
        self._parse_ip()
        return self._ip['sip']

    def get_source_ip(self):
        self._parse_ip()
        return socket.inet_ntop(socket.AF_INET, struct.pack('!I', self._ip['sip']))

    def get_raw_destination_ip(self):
        self._parse_ip()
        return self._ip['dip']

    def get_destination_ip(self):
        self._parse_ip()
        return socket.inet_ntop(socket.AF_INET, struct.pack('!I', self._ip['dip']))

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

    def get_udp_load(self):
        offset = self._ip['length']
        return self._packet[offset + 8:]
