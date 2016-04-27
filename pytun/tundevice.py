import socket
import errno
import os
import fcntl
import subprocess
import struct
from event import Event
from collections import deque
import logging
import loglevel

_logger = logging.getLogger('TunDevice')
_logger.setLevel(loglevel.gLevel)

class TunDevice(object):

    TUNSETIFF = 0x400454ca
    IFF_TUN   = 0x0001
    IFF_TAP   = 0x0002
    IFF_NO_PI = 0x1000

    def __init__(self, prefix, ip, netmask):
        TUNMODE = self.IFF_TUN | self.IFF_NO_PI

        self.__fd = os.open('/dev/net/tun', os.O_RDWR)

        flag = fcntl.fcntl(self.__fd, fcntl.F_GETFL)
        fcntl.fcntl(self.__fd, fcntl.F_SETFL, flag | os.O_NONBLOCK)

        ctlstr = struct.pack('16sH', prefix + '%d', TUNMODE)
        ifs = fcntl.ioctl(self.__fd, self.TUNSETIFF, ctlstr)
        self.__ifname = ifs[:16].strip("\x00")

        cmd = 'ifconfig %s %s netmask %s mtu 9000 up' % (self.__ifname, ip, netmask)
        _logger.debug('ifconfig cmd: ' + cmd)
        subprocess.check_call(cmd, shell=True)

        self.__tosend = deque()
        self.__rev = None
        self.__wev = None
        self.__cev = None
        self.__timeout = 0
        self.__timeoutEv = None

        self.__decoders = [(self.__ipv4Decoder, [''])]

        self.__error = False

        self.__onReceived = None
        self.__onClosed = None

        self.__wev = Event()
        self.__wev.setWrite(True)
        self.__wev.setFd(self.__fd)
        self.__wev.setHandler(lambda ev: self.__onSend())

        self.__rev = Event()
        self.__rev.setWrite(False)
        self.__rev.setFd(self.__fd)
        self.__rev.setHandler(lambda ev: self.__onReceive())

    def setOnReceived(self, onReceived):
        self.__onReceived = onReceived

    def setOnClosed(self, onClosed):
        self.__onClosed = onClosed

    def __onSend(self):
        _logger.debug('__onSend')
        while len(self.__tosend) > 0:
            data = self.__tosend.popleft()
            try:
                sent = os.write(self.__fd, data)
                _logger.debug('fd: %d sent %d bytes', self.__fd, sent)
                if sent < len(data):
                    _logger.debug('fd: %d sent less than %d bytes', self.__fd, len(data))
                    self.__tosend.appendleft(data[sent:])
            except IOError as msg:
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    _logger.error('fd: %d, send: %s', self.__fd, os.strerror(msg.errno))
                    self.__error = True
                    self.__closeAgain()
                else:
                    self.__tosend.appendleft(data)
                return

        Event.delEvent(self.__wev)
        if self.__cev != None:
            self.__closeAgain()

    def send(self, data):
        _logger.debug('sending %d bytes', len(data))
        if self.__cev != None:
            return

        if len(self.__tosend) == 0:
            Event.addEvent(self.__wev)

        self.__tosend.append(data)

    @staticmethod
    def __ipv4Decoder(data):
        if len(data) < 20:
            return ('', 0)

        _, _, length = struct.unpack('!BBH', data[:4])
        if len(data) < length:
            return ('', 0)

        return (data[:length], length)

    @staticmethod
    def __parseIpv4(packet):
    
        PROTO_TCP = 6
        PROTO_UDP = 17

        ver_ihl, _, total_length, _, _, protocol, _, sip, dip = struct.unpack('!BBHIBBH4s4s', packet[:20])
        ihl = ver_ihl & 0x0f
        ver = (ver_ihl >> 4) & 0x0f
        #print('version: %d' % ver)
        #print('total length: %d' % total_length)
        #print('protocol: %d' % protocol)
        #print('source ip: ' + socket.inet_ntop(socket.AF_INET, sip))
        #print('dest ip: ' + socket.inet_ntop(socket.AF_INET, dip))
        sip = socket.inet_ntop(socket.AF_INET, sip)
        dip = socket.inet_ntop(socket.AF_INET, dip)

        sport, dport = 0, 0
        if protocol == PROTO_TCP or protocol == PROTO_UDP:
            offset = ihl * 4
            sport, dport = struct.unpack('!HH', packet[offset:offset+4])
            #print('source port: %d' % sport)
            #print('dest port: %d' % dport)

        return total_length, (sip, sport), (dip, dport)

    def __decode(self, depth, data):
        decoder, remain = self.__decoders[depth]
        remain[0] += data

        while len(remain[0]) > 0:
            processed, processed_bytes = decoder(remain[0])
            if processed_bytes > 0:
                if depth == len(self.__decoders) - 1:
                    try:
                        _, src, dst = self.__parseIpv4(processed)
                        self.__onReceived(self, processed, src, dst)
                    except Exception as e:
                        _logger.error('onReceived: %s', e)
                        raise e
                else:
                    self.__decode(depth + 1, processed) < 0
                remain[0] = remain[0][processed_bytes:]
            elif processed_bytes == 0:
                return 0

    def __onReceive(self):
        _logger.debug('__onReceive')
        while True:
            try:
                recv = os.read(self.__fd, 65536)
                if len(recv) == 0:
                    self.close()
                    return
                _logger.debug('fd: %d recv %d bytes', self.__fd, len(recv))
            except OSError as msg:
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    _logger.error('fd: %d, recv: %s', self.__fd, os.strerror(msg.errno))
                    self.__error = True
                    self.__closeAgain()
                return

            try:
                self.__decode(0, recv)
            except Exception as e:
                _logger.error('decode: %s', e)
                self.__error = True
                self.__closeAgain()

    def beginReceiving(self):
        _logger.debug('beginReceiving')
        if self.__cev != None:
            return
        Event.addEvent(self.__rev)

    def stopReceiving(self):
        _logger.debug('stopReceiving')
        Event.delEvent(self.__rev)

    def __onClose(self):
        _logger.debug('__onClose')
        _logger.debug('fd: %d closed', self.__fd)
        # in case of timeout happened
        Event.delEvent(self.__wev)

        if self.__timeoutEv is not None:
            self.__timeoutEv.delTimer()
            self.__timeoutEv = None

        os.close(self.__fd)
        if self.__onClosed != None:
            try:
                self.__onClosed(self)
            except:
                pass

    def __closeAgain(self):
        _logger.debug('__closeAgain')
        if self.__cev != None:
            self.__cev.delTimer()
            self.__cev = None
        self.close()

    def close(self):
        _logger.debug('close')
        if self.__cev != None:
            return
        _logger.debug('fd: %d closing', self.__fd)

        timeout = 0
        if not self.__error and len(self.__tosend) > 0:
            timeout = 60000

        if timeout == 0:
            Event.delEvent(self.__wev)

        Event.delEvent(self.__rev)
        self.__cev = Event.addTimer(timeout)
        self.__cev.setHandler(lambda ev: self.__onClose())

