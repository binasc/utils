import os
import socket
import errno
from event import Event
from collections import deque
import traceback

import loglevel
_logger = loglevel.getLogger('nonblocking')
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

        self._decodeError = False
        self._onDecodeError = None

        self._timers = {}

    def __hash__(self):
        return hash(self._fd.fileno())

    def __eq__(self, another):
        return self._fd.fileno() == another._fd.fileno()

    def __str__(self):
        return "fd: %d" % (self._fd.fileno())

    def setNonBlocking(self):
        raise Exception('not implemented')

    def setOnSent(self, onSent):
        self._onSent = onSent

    def setOnReceived(self, onReceived):
        self._onReceived = onReceived

    def setOnClosed(self, onClosed):
        self._onClosed = onClosed

    def setOnDecodeError(self, onDecodeError):
        self._onDecodeError = onDecodeError

    def appendSendHandler(self, handler):
        self._encoders.append(handler)

    def appendReceiveHandler(self, handler):
        self._decoders.append((handler, ['']))

    def beginReceiving(self):
        _logger.debug('beginReceiving')
        if not self._connected or self._cev is not None:
            return
        if not Event.isEventSet(self._rev):
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
        _logger.debug("fd: %d sent %d bytes, remain %d bytes", self._fd.fileno(), sent_bytes, self._tosend_bytes)
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
        if self._cev is not None:
            return

        if addr is not None:
            _logger.debug('sending %d bytes to %s:%d', len(data), addr[0], addr[1])
        else:
            _logger.debug('sending %d bytes', len(data))

        try:
            for encoder in self._encoders:
                data = encoder(data)
        except Exception as ex:
            _logger.error('failed to encode %d bytes: %s', len(data), str(ex))
            raise ex

        self._tosend.append((data, addr))
        self._tosend_bytes += len(data)
        self.refreshTimeout()

        if self._connected and not Event.isEventSet(self._wev):
            Event.addEvent(self._wev)

    def pendingBytes(self):
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
                recv, addr = self._recv(2 ** 16 - 512)
                if len(recv) == 0:
                    self.close()
                    return
                self.refreshTimeout()
            except self._errorType as msg:
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    _logger.error('fd: %d, recv occurs error: %s',
                                  self._fd.fileno(), os.strerror(msg.errno))
                    self._error = True
                    self._closeAgain()
                return

            try:
                if self._decodeError:
                    self._onDecodeError(self, recv)
                else:
                    self._decode(0, recv, addr)
            except self.RecvCBException:
                pass
            except Exception as ex:
                _logger.error('decode: %s', str(ex))
                if self._onDecodeError is not None:
                    try:
                        self._decodeError = True
                        self._onDecodeError(self, recv)
                    except Exception as ex:
                        _logger.error("_onDecodeError: %s", str(ex))
                        self._error = True
                        self._closeAgain()
                else:
                    self._error = True
                    self._closeAgain()

            if not Event.isEventSet(self._rev):
                break

    def _close(self):
        raise Exception('not implemented')

    def _onClose(self):
        _logger.debug('_onClose')
        _logger.debug('fd: %d closed', self._fd.fileno())

        for name in list(self._timers.keys()):
            self.delTimer(name)

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

        self.removeTimeout()

        timeout = 0
        if not self._error and self._connected and len(self._tosend) > 0:
            timeout = 60000

        if timeout == 0:
            Event.delEvent(self._wev)

        Event.delEvent(self._rev)
        self._cev = Event.addTimer(timeout)
        self._cev.setHandler(lambda ev: self._onClose())

    def _onTimeout(self):
        _logger.debug('_onTimeout')
        self.close()

    def refreshTimeout(self):
        _logger.debug('refreshTimeout')
        if self._timeoutEv is not None:
            self.setTimeout(self._timeout)

    def setTimeout(self, timeout):
        _logger.debug('setTimeout')
        if self._cev is not None:
            return

        if self._timeoutEv is not None:
            self.removeTimeout()

        self._timeout = timeout
        self._timeoutEv = Event.addTimer(self._timeout)
        self._timeoutEv.setHandler(lambda ev: self._onTimeout())

    def removeTimeout(self):
        _logger.debug('removeTimeout')
        if self._timeoutEv is not None:
            self._timeoutEv.delTimer()
            self._timeoutEv = None

    def _genOnTimer(self, name, handler):
        def onTimer(ev):
            if not name in self._timers:
                return
            del self._timers[name]
            try:
                handler(self)
            except Exception as ex:
                _logger.error('timer handler exception: ' + str(ex))

        return onTimer

    def addTimer(self, name, timer, handler):
        _logger.debug('addTimer: %s', name)
        if self._cev is not None:
            return

        if name in self._timers:
            raise Exception('timer: ' + name + ' already exists')

        timerEv = Event.addTimer(timer)
        timerEv.setHandler(self._genOnTimer(name, handler))
        self._timers[name] = timerEv

    def delTimer(self, name):
        _logger.debug('delTimer: %s', name)
        self._timers[name].delTimer()
        del self._timers[name]

