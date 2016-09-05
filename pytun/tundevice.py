import socket
import errno
import os
import fcntl
import subprocess
import struct
from event import Event
from collections import deque
import logging
import traceback

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

        self._fd = os.open('/dev/net/tun', os.O_RDWR)

        flag = fcntl.fcntl(self._fd, fcntl.F_GETFL)
        fcntl.fcntl(self._fd, fcntl.F_SETFL, flag | os.O_NONBLOCK)

        ctlstr = struct.pack('16sH', prefix + '%d', TUNMODE)
        ifs = fcntl.ioctl(self._fd, self.TUNSETIFF, ctlstr)
        self._ifname = ifs[:16].strip("\x00")

        cmd = 'ifconfig %s %s netmask %s mtu 9000 up' % (self._ifname, ip, netmask)
        _logger.debug('ifconfig cmd: ' + cmd)
        subprocess.check_call(cmd, shell=True)

        self._tosend = deque()
        self._cev = None
        self._timeout = 0
        self._timeoutEv = None

        self._decoders = [(self._ipv4Decoder, [''])]

        self._error = False

        self._onReceived = None
        self._onClosed = None

        self._wev = Event()
        self._wev.setWrite(True)
        self._wev.setFd(self._fd)
        self._wev.setHandler(lambda ev: self._onSend())

        self._rev = Event()
        self._rev.setWrite(False)
        self._rev.setFd(self._fd)
        self._rev.setHandler(lambda ev: self._onReceive())

    def setOnReceived(self, onReceived):
        self._onReceived = onReceived

    def setOnClosed(self, onClosed):
        self._onClosed = onClosed

    def beginReceiving(self):
        _logger.debug('beginReceiving')
        if self._cev != None:
            _logger.warning('fd: %d closed', self._fd.fileno())
            return
        Event.addEvent(self._rev)

    def stopReceiving(self):
        _logger.debug('stopReceiving')
        Event.delEvent(self._rev)

    def _onSend(self):
        _logger.debug('_onSend')
        while len(self._tosend) > 0:
            data = self._tosend.popleft()
            try:
                sent = os.write(self._fd, data)
                _logger.debug('fd: %d sent %d bytes', self._fd, sent)
                if sent < len(data):
                    _logger.debug('fd: %d sent less than %d bytes',
                                  self._fd, len(data))
                    self._tosend.appendleft(data[sent:])
            except IOError as msg:
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    _logger.error('fd: %d, send: %s',
                                  self._fd, os.strerror(msg.errno))
                    self._error = True
                    self._closeAgain()
                else:
                    self._tosend.appendleft(data)
                return

        Event.delEvent(self._wev)
        if self._cev != None:
            self._closeAgain()

    def send(self, data):
        _logger.debug('sending %d bytes', len(data))
        if self._cev != None:
            return

        if len(self._tosend) == 0:
            Event.addEvent(self._wev)

        self._tosend.append(data)

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
        #print('version: %d' % ver)
        #print('total length: %d' % total_length)
        #print('protocol: %d' % protocol)
        #print('source ip: ' + socket.inet_ntop(socket.AF_INET, sip))
        #print('dest ip: ' + socket.inet_ntop(socket.AF_INET, dip))
        sip = socket.inet_ntop(socket.AF_INET, sip)
        dip = socket.inet_ntop(socket.AF_INET, dip)

        proto = 'other'
        sport, dport = 0, 0
        if protocol == PROTO_TCP or protocol == PROTO_UDP:
            proto = 'tcp' if protocol == PROTO_TCP else 'udp'
            offset = ihl * 4
            sport, dport = struct.unpack('!HH', packet[offset:offset+4])
            #print('source port: %d' % sport)
            #print('dest port: %d' % dport)

        return total_length, proto, (sip, sport), (dip, dport)

    class RecvCBException(Exception):
        pass

    def _decode(self, depth, data):
        decoder, remain = self._decoders[depth]
        remain[0] += data

        while len(remain[0]) > 0:
            out, processed_bytes = decoder(remain[0])
            assert(processed_bytes >= 0)
            if processed_bytes == 0:
                break
            if depth == len(self._decoders) - 1:
                try:
                    _, proto, src, dst = self._parseIpv4(out)
                    self._onReceived(self, out, proto, src, dst)
                except Exception as e:
                    _logger.error('_onReceived: %s', e)
                    _logger.exception(traceback.format_exc())
                    self._error = True
                    self._closeAgain()
                    raise self.RecvCBException(e)
            else:
                self._decode(depth + 1, out)
            remain[0] = remain[0][processed_bytes:]

    def _onReceive(self):
        _logger.debug('_onReceive')
        while True:
            try:
                recv = os.read(self._fd, 65536)
                if len(recv) == 0:
                    self.close()
                    return
                _logger.debug('fd: %d recv %d bytes', self._fd, len(recv))
            except OSError as msg:
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    _logger.error('fd: %d, recv: %s',
                                  self._fd, os.strerror(msg.errno))
                    self._error = True
                    self._closeAgain()
                return

            try:
                self._decode(0, recv)
            except self.RecvCBException:
                pass
            except Exception as e:
                _logger.error('decode: %s', e)
                self._error = True
                self._closeAgain()

    def _onClose(self):
        _logger.debug('_onClose')
        _logger.debug('fd: %d closed', self._fd)
        # in case of timeout happened
        Event.delEvent(self._wev)

        if self._timeoutEv is not None:
            self._timeoutEv.delTimer()
            self._timeoutEv = None

        os.close(self._fd)
        if self._onClosed != None:
            try:
                self._onClosed(self)
            except:
                _logger.error('_onClosed: %s', ex)
                _logger.exception(traceback.format_exc())

    def _closeAgain(self):
        _logger.debug('_closeAgain')
        if self._cev != None:
            self._cev.delTimer()
            self._cev = None
        self.close()

    def close(self):
        _logger.debug('close')
        if self._cev != None:
            return
        _logger.debug('fd: %d closing', self._fd)

        timeout = 0
        if not self._error and len(self._tosend) > 0:
            timeout = 60000

        if timeout == 0:
            Event.delEvent(self._wev)

        Event.delEvent(self._rev)
        self._cev = Event.addTimer(timeout)
        self._cev.setHandler(lambda ev: self._onClose())

