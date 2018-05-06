import socket
import os
import fcntl
import subprocess
import struct
from functools import partial
from nonblocking import NonBlocking

import loglevel
_logger = loglevel.get_logger('tundevice')


class FileWrapper(object):
    def __init__(self, fd):
        self._fd = fd

    def fileno(self):
        return self._fd


class TunDevice(NonBlocking):

    TUNSETIFF = 0x400454ca
    IFF_TUN = 0x0001
    IFF_TAP = 0x0002
    IFF_NO_PI = 0x1000

    PROTO_TCP = 6
    PROTO_UDP = 17

    @staticmethod
    def _ipv4_decoder(data):
        if len(data) < 20:
            return '', 0

        _, _, length = struct.unpack('!BBH', data[:4])
        if len(data) < length:
            return '', 0

        return data[:length], length

    @staticmethod
    def __init__(self, prefix, ip, netmask):
        fd = os.open('/dev/net/tun', os.O_RDWR)

        mode = self.IFF_TUN | self.IFF_NO_PI
        ctrl_str = struct.pack('16sH', prefix + '%d', mode)
        ifs = fcntl.ioctl(fd, self.TUNSETIFF, ctrl_str)
        self._ifname = ifs[:16].strip("\x00")

        if isinstance(netmask, int):
            # convert cidr prefix to netmask
            netmask = '.'.join([str((0xffffffff << (32 - netmask) >> i) & 0xff)
                                for i in [24, 16, 8, 0]])
        cmd = 'ifconfig %s %s netmask %s up' % (self._ifname, ip, netmask)
        _logger.debug('ifconfig cmd: ' + cmd)
        subprocess.check_call(cmd, shell=True)

        NonBlocking.__init__(self, FileWrapper(fd))
        self._connected = True
        self._decoders = [(self._ipv4_decoder, [''])]

        self._errorType = OSError

    def set_non_blocking(self):
        flag = fcntl.fcntl(self._fd.fileno(), fcntl.F_GETFL)
        fcntl.fcntl(self._fd.fileno(), fcntl.F_SETFL, flag | os.O_NONBLOCK)

    def set_on_received(self, on_received):

        def parse_ipv4(packet):
            ver_ihl, _, total_length, _, _, protocol, _, sip, dip = struct.unpack('!BBHIBBH4s4s', packet[:20])
            ihl = ver_ihl & 0x0f
            _ = (ver_ihl >> 4) & 0x0f  # version
            sip = socket.inet_ntop(socket.AF_INET, sip)
            dip = socket.inet_ntop(socket.AF_INET, dip)

            proto = 'other'
            sport, dport = 0, 0
            if protocol == TunDevice.PROTO_TCP or protocol == TunDevice.PROTO_UDP:
                proto = 'tcp' if protocol == TunDevice.PROTO_TCP else 'udp'
                offset = ihl * 4
                sport, dport = struct.unpack('!HH', packet[offset:offset+4])

            return total_length, proto, (sip, sport), (dip, dport)

        def on_received_wrapper(_on_received, _self, out, _):
            _, proto, src, dst = parse_ipv4(out)
            _on_received(_self, out, proto, src, dst)

        self._onReceived = partial(on_received_wrapper, on_received)

    def _send(self, data, _):
        sent = os.write(self._fd.fileno(), data)
        _logger.debug('fd: %d sent %d bytes', self._fd.fileno(), sent)
        return sent

    def _recv(self, size):
        recv = os.read(self._fd.fileno(), size)
        _logger.debug('fd: %d recv %d bytes', self._fd.fileno(), len(recv))
        return recv, None

    def _close(self):
        os.close(self._fd.fileno())
