import os
import socket
import errno
from event import Event
from collections import deque
import logging
import traceback

import loglevel
_logger = logging.getLogger('NonBlocking')
_logger.setLevel(loglevel.gLevel)

class NonBlocking(object):

    def __init__(self, fd):
        self._fd = fd
        self.setNonBlocking()

        self._tosend_bytes = 0
        self._tosend = deque()

        self._encoders = []
        self._decoders = [(lambda data: (data, len(data)), [''])]

        self._error = False

        self._connected = False

        self._onSent = None
        self._onReceived = None
        self._onClosed = None

        self._cev = None

        self._timeout = 0
        self._timeoutEv = None

        self._wev = Event()
        self._wev.setWrite(True)
        self._wev.setFd(self._fd.fileno())
        self._wev.setHandler(lambda ev: self._onSend())

        self._rev = Event()
        self._rev.setWrite(False)
        self._rev.setFd(self._fd.fileno())
        self._rev.setHandler(lambda ev: self._onReceive())

        self._errorType = socket.error

    def __hash__(self):
        return hash(self._fd.fileno())

    def __eq__(self, another):
        return self._fd.fileno() == another._fd.fileno()

    def setNonBlocking(self):
        raise Exception('not implemented')

    def setOnSent(self, onSent):
        self._onSent = onSent

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
        if not self._connected or self._cev is not None:
            return
        Event.addEvent(self._rev)

    def stopReceiving(self):
        _logger.debug('stopReceiving')
        Event.delEvent(self._rev)

    def _send(self, data, addr):
        raise Exception('not implemented')

    def _onSend(self):
        _logger.debug('_onSend')
        sent_bytes = 0
        while len(self._tosend) > 0:
            data, addr = self._tosend.popleft()
            try:
                sent = self._send(data, addr)
                sent_bytes += sent
                if sent < len(data):
                    self._tosend.appendleft((data[sent:], addr))
            except self._errorType as msg:
                self._tosend.appendleft((data, addr))
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    _logger.error('fd: %d, send: %s',
                                  self._fd.fileno(), os.strerror(msg.errno))
                    self._error = True
                    self._closeAgain()
                break

        self._tosend_bytes -= sent_bytes
        if sent_bytes > 0 and self._onSent:
            try:
                self._onSent(self, sent_bytes, self._tosend_bytes)
            except Exception as e:
                _logger.error('_onSent: %s', e)
                _logger.exception(traceback.format_exc())
                self._error = True
                self._closeAgain()

        if len(self._tosend) == 0:
            assert(self._tosend_bytes == 0)
            Event.delEvent(self._wev)
            if self._cev is not None:
                self._closeAgain()

    def send(self, data, addr=None):
        _logger.debug('send')
        if not data or self._cev is not None:
            return self._tosend_bytes

        if addr is not None:
            _logger.debug('sending %d bytes to %s:%d', len(data), addr[0], addr[1])
        else:
            _logger.debug('sending %d bytes', len(data))

        if self._connected and len(self._tosend) == 0:
            Event.addEvent(self._wev)

        for encoder in self._encoders:
            data = encoder(data)
        self._tosend.append((data, addr))
        self._tosend_bytes += len(data)
        self.refreshTimer()
        return self._tosend_bytes

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

    def _recv(self, size):
        raise Exception('not implemented')

    def _onReceive(self):
        _logger.debug('_onReceive')
        while True:
            try:
                recv, addr = self._recv(65536)
                if len(recv) == 0:
                    self.close()
                    return
                self.refreshTimer()
            except self._errorType as msg:
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

            if not Event.isEventSet(self._rev):
                break

    def _close(self):
        raise Exception('not implemented')

    def _onClose(self):
        _logger.debug('_onClose')
        _logger.debug('fd: %d closed', self._fd.fileno())
        # in case of timeout happened
        Event.delEvent(self._wev)

        assert(self._timeoutEv is None)
        assert(not Event.isEventSet(self._rev))

        self._close()
        self._connected = False
        if self._onClosed != None:
            try:
                self._onClosed(self)
            except Exception as ex:
                _logger.error('_onClosed: %s', ex)
                _logger.exception(traceback.format_exc())

    def _closeAgain(self):
        _logger.debug('_closeAgain')
        if self._cev is not None:
            self._cev.delTimer()
            self._cev = None
        self.close()

    def close(self):
        _logger.debug('close')
        if self._cev is not None:
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
        _logger.debug('refreshTimer')
        if self._timeoutEv is None:
            return

        self._timeoutEv.delTimer()
        if self._timeout > 0:
            self._timeoutEv = Event.addTimer(self._timeout)
            self._timeoutEv.setHandler(lambda ev: self._onTimeout())

    def setTimeout(self, timeout):
        _logger.debug('setTimeout')
        if self._cev is not None:
            return

        if self._timeoutEv is not None:
            self._timeoutEv.delTimer()
            self._timeoutEv = None

        self._timeout = timeout
        if self._timeout > 0:
            self._timeoutEv = Event.addTimer(self._timeout)
            self._timeoutEv.setHandler(lambda ev: self._onTimeout())

