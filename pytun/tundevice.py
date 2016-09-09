import socket
import errno
import os
import fcntl
import subprocess
import struct
from event import Event
from collections import deque
from functools import partial
import logging
import traceback

import loglevel
_logger = logging.getLogger('TunDevice')
_logger.setLevel(loglevel.gLevel)

from nonblocking import NonBlocking

class FileWrapper(object):
    def __init__(self, fd):
        self._fd = fd

    def fileno(self):
        return self._fd

class TunDevice(NonBlocking):

    TUNSETIFF = 0x400454ca
    IFF_TUN   = 0x0001
    IFF_TAP   = 0x0002
    IFF_NO_PI = 0x1000

    @staticmethod
    def _ipv4Decoder(data):
        if len(data) < 20:
            return ('', 0)

        _, _, length = struct.unpack('!BBH', data[:4])
        if len(data) < length:
            return ('', 0)

        return (data[:length], length)

    @staticmethod
    def _parseIpv4(packet):
    
        PROTO_TCP = 6
        PROTO_UDP = 17

        ver_ihl, _, total_length, _, _, protocol, _, sip, dip = struct.unpack('!BBHIBBH4s4s', packet[:20])
        ihl = ver_ihl & 0x0f
        ver = (ver_ihl >> 4) & 0x0f
        sip = socket.inet_ntop(socket.AF_INET, sip)
        dip = socket.inet_ntop(socket.AF_INET, dip)

        proto = 'other'
        sport, dport = 0, 0
        if protocol == PROTO_TCP or protocol == PROTO_UDP:
            proto = 'tcp' if protocol == PROTO_TCP else 'udp'
            offset = ihl * 4
            sport, dport = struct.unpack('!HH', packet[offset:offset+4])

        return total_length, proto, (sip, sport), (dip, dport)

    def __init__(self, prefix, ip, netmask):
        fd = os.open('/dev/net/tun', os.O_RDWR)

        TUNMODE = self.IFF_TUN | self.IFF_NO_PI
        ctlstr = struct.pack('16sH', prefix + '%d', TUNMODE)
        ifs = fcntl.ioctl(fd, self.TUNSETIFF, ctlstr)
        self._ifname = ifs[:16].strip("\x00")

        cmd = 'ifconfig %s %s netmask %s mtu 9000 up' % (self._ifname, ip, netmask)
        _logger.debug('ifconfig cmd: ' + cmd)
        subprocess.check_call(cmd, shell=True)

        NonBlocking.__init__(self, FileWrapper(fd))
        self._connected = True
        self._decoders = [(self._ipv4Decoder, [''])]

        self._errorType = OSError

    def setNonBlocking(self):
        flag = fcntl.fcntl(self._fd.fileno(), fcntl.F_GETFL)
        fcntl.fcntl(self._fd.fileno(), fcntl.F_SETFL, flag | os.O_NONBLOCK)

    def setOnReceived(self, onReceived):

        def onReceivedWrapper(onReceived, self, out, addr):
            _, proto, src, dst = self._parseIpv4(out)
            onReceived(self, out, proto, src, dst)

        self._onReceived = partial(onReceivedWrapper, onReceived)

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

