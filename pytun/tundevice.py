import os
import fcntl
import subprocess
import struct
from functools import partial
from nonblocking import NonBlocking
from packet import Packet

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

        ver_ihl, _, length = struct.unpack('!BBH', data[:4])
        if len(data) < length:
            return '', 0

        version = (ver_ihl >> 4) & 0x0f
        if version == 6:
            if len(data) < 40:
                return '', 0
            length, = struct.unpack('!H', data[4: 6])
            if len(data) < 40 + length:
                return '', 0
            else:
                return '', 40 + length

        return data[:length], length

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

        def on_received_wrapper(_on_received, self_, data, _):
            _on_received(self_, data, Packet(data))

        self._on_received = partial(on_received_wrapper, on_received)

    def _send(self, data, _):
        sent = os.write(self._fd.fileno(), data)
        _logger.debug('%s, write %d bytes', str(self), sent)
        return sent

    def _recv(self, size):
        recv = os.read(self._fd.fileno(), size)
        _logger.debug('%s, read %d bytes', str(self), len(recv))
        return recv, None

    def _close(self):
        os.close(self._fd.fileno())
