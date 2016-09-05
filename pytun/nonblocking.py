import os
import socket
import errno
from event import Event
from collections import deque
import logging
import traceback

import loglevel
_logger = logging.getLogger('Stream')
_logger.setLevel(loglevel.gLevel)

class NonBlocking(object):

    def __init__(self, sock):
        self._fd = sock
        self._fd.setblocking(False)

        self._tosend = deque()
        self._cev = None
        self._timeout = 0
        self._timeoutEv = None

        self._encoders = []
        self._decoders = [(lambda data: (data, len(data)), [''])]

        self._error = False

        self._connected = False

        self._onReceived = None
        self._onClosed = None

        self._wev = Event()
        self._wev.setWrite(True)
        self._wev.setFd(self._fd.fileno())
        self._wev.setHandler(lambda ev: self._onSend())

        self._rev = Event()
        self._rev.setWrite(False)
        self._rev.setFd(self._fd.fileno())
        self._rev.setHandler(lambda ev: self._onReceive())

    def __hash__(self):
        return hash(self._fd.fileno())

    def __eq__(self, another):
        return self._fd.fileno() == another._fd.fileno()

    def bind(self, addr, port):
        self._fd.bind((addr, port))

    def setBufferSize(self, bsize):
        self._fd.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, bsize)
        self._fd.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, bsize)

    def setOnReceived(self, onReceived):
        self._onReceived = onReceived

    def setOnClosed(self, onClosed):
        self._onClosed = onClosed

    def appendSendHandler(self, handler):
        self._encoders.append(handler)

    def appendReceiveHandler(self, handler):
        self._decoders.append((handler, ['']))

    def beginReceiving(self):
        _logger.debug('beginReceiving')
        if self._cev != None and not self._connected:
            _logger.warning('fd: %d closed or not connected', self._fd.fileno())
            return
        Event.addEvent(self._rev)

    def stopReceiving(self):
        _logger.debug('stopReceiving')
        Event.delEvent(self._rev)

    def _onSend(self):
        _logger.debug('_onSend')
        while len(self._tosend) > 0:
            data, addr = self._tosend.popleft()
            try:
                if addr is None:
                    sent = self._fd.send(data)
                else:
                    sent = self._fd.sendto(data, addr)
                if addr is None:
                    _logger.debug('fd: %d sent %d bytes', self._fd.fileno(), sent)
                else:
                    _logger.debug('fd: %d sent %d bytes to %s:%d',
                                  self._fd.fileno(), sent, addr[0], addr[1])
                if sent < len(data):
                    _logger.debug('fd: %d sent less than %d bytes',
                                  self._fd.fileno(), len(data))
                    self._tosend.appendleft((data[sent:], addr))
            except socket.error as msg:
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    _logger.error('fd: %d, send: %s',
                                  self._fd.fileno(), os.strerror(msg.errno))
                    self._error = True
                    self._closeAgain()
                else:
                    self._tosend.appendleft((data, addr))
                return

        Event.delEvent(self._wev)
        if self._cev != None:
            self._closeAgain()

    def send(self, data, addr=None):
        if addr is not None:
            _logger.debug('sendto %s:%d %d bytes', addr[0], addr[1], len(data))
        else:
            _logger.debug('sending %d bytes', len(data))
        if self._cev != None:
            return

        if len(self._tosend) == 0 and self._connected:
            Event.addEvent(self._wev)
        for encoder in self._encoders:
            data = encoder(data)
        self._tosend.append((data, addr))
        self.refreshTimer()

    class RecvCBException(Exception):
        pass

    def _decode(self, depth, data, addr):
        decoder, remain = self._decoders[depth]
        remain[0] += data

        while len(remain[0]) > 0:
            out, processed_bytes = decoder(remain[0])
            assert(processed_bytes >= 0)
            if processed_bytes == 0:
                break
            if depth == len(self._decoders) - 1:
                try:
                    self._onReceived(self, out, addr)
                except Exception as e:
                    _logger.error('_onReceived: %s', e)
                    _logger.exception(traceback.format_exc())
                    self._error = True
                    self._closeAgain()
                    raise self.RecvCBException(e)
            else:
                self._decode(depth + 1, out, addr)
            remain[0] = remain[0][processed_bytes:]

    def _onReceive(self):
        _logger.debug('_onReceive')
        while True:
            try:
                recv, addr = self._fd.recvfrom(65536)
                if len(recv) == 0:
                    self.close()
                    return
                if addr is None:
                    _logger.debug('fd: %d recv %d bytes',
                                  self._fd.fileno(), len(recv))
                else:
                    _logger.debug('fd: %d recv %d bytes from %s:%d',
                                  self._fd.fileno(), len(recv), addr[0], addr[1])
                self.refreshTimer()
            except socket.error as msg:
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    _logger.error('fd: %d, recv: %s',
                                  self._fd.fileno(), os.strerror(msg.errno))
                    self._error = True
                    self._closeAgain()
                return

            try:
                self._decode(0, recv, addr)
            except self.RecvCBException:
                pass
            except Exception as e:
                _logger.error('decode: %s', e)
                self._error = True
                self._closeAgain()

    def _onClose(self):
        _logger.debug('_onClose')
        _logger.debug('fd: %d closed', self._fd.fileno())
        # in case of timeout happened
        Event.delEvent(self._wev)

        if self._timeoutEv is not None:
            self._timeoutEv.delTimer()
            self._timeoutEv = None

        self._fd.close()
        self._connected = False
        if self._onClosed != None:
            try:
                self._onClosed(self)
            except Exception as ex:
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
        _logger.debug('fd: %d closing', self._fd.fileno())

        timeout = 0
        if not self._error and self._connected and len(self._tosend) > 0:
            timeout = 60000

        if timeout == 0:
            Event.delEvent(self._wev)

        # remove timeout event
        self.setTimeout(0)

        Event.delEvent(self._rev)
        self._cev = Event.addTimer(timeout)
        self._cev.setHandler(lambda ev: self._onClose())

    def _onTimeout(self):
        _logger.debug('_onTimeout')
        self.close()

    def refreshTimer(self):
        if self._timeoutEv is None:
            return

        self._timeoutEv.delTimer()
        if self._timeout > 0:
            self._timeoutEv = Event.addTimer(self._timeout)
            self._timeoutEv.setHandler(lambda ev: self._onTimeout())

    def setTimeout(self, timeout):
        if self._cev != None:
            return

        if self._timeoutEv is not None:
            self._timeoutEv.delTimer()
            self._timeoutEv = None

        self._timeout = timeout
        if self._timeout > 0:
            self._timeoutEv = Event.addTimer(self._timeout)
            self._timeoutEv.setHandler(lambda ev: self._onTimeout())

