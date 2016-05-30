import socket
import errno
import os
from event import Event
from collections import deque
import logging
import loglevel
import traceback

_logger = logging.getLogger('Dgram')
_logger.setLevel(loglevel.gLevel)

class Dgram:

    def __init__(self):
        self.__tosend = deque()
        self.__rev = None
        self.__wev = None
        self.__cev = None
        self.__timeout = 0
        self.__timeoutEv = None

        self.__encoders = []
        self.__decoders = [(lambda data: (data, len(data)), [''])]

        self.__error = False

        self.__onReceivedFrom = None
        self.__onClosed = None

        self.__fd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.__fd.setblocking(False)
        self.__fd.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

        self.__wev = Event()
        self.__wev.setWrite(True)
        self.__wev.setFd(self.__fd.fileno())
        self.__wev.setHandler(lambda ev: self.__onSend())

        self.__rev = Event()
        self.__rev.setWrite(False)
        self.__rev.setFd(self.__fd.fileno())
        self.__rev.setHandler(lambda ev: self.__onReceive())

    def __hash__(self):
        return hash(self.__fd.fileno())

    def __eq__(self, another):
        return self.__fd.fileno() == another.__fd.fileno()

    def setOnReceivedFrom(self, onReceivedFrom):
        self.__onReceivedFrom = onReceivedFrom

    def setOnClosed(self, onClosed):
        self.__onClosed = onClosed

    def bind(self, addr, port):
        self.__fd.bind((addr, port))

    def __onSend(self):
        _logger.debug('__onSend')
        while len(self.__tosend) > 0:
            data, addr = self.__tosend.popleft()
            try:
                sent = self.__fd.sendto(data, addr)
                _logger.debug('fd: %d sent %d bytes to %s:%d', self.__fd.fileno(), sent, addr[0], addr[1])
                if sent < len(data):
                    _logger.debug('fd: %d sent less than %d bytes', self.__fd.fileno(), len(data))
                    self.__tosend.appendleft((data[sent:], addr))
            except socket.error as msg:
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    _logger.error('fd: %d, send: %s', self.__fd.fileno(), os.strerror(msg.errno))
                    self.__error = True
                    self.__closeAgain()
                else:
                    self.__tosend.appendleft((data, addr))
                return

        Event.delEvent(self.__wev)
        if self.__cev != None:
            self.__closeAgain()

    def sendto(self, data, addr):
        _logger.debug('sendto %s:%d %d bytes', addr[0], addr[1], len(data))
        if self.__cev != None:
            return

        if len(self.__tosend) == 0:
            Event.addEvent(self.__wev)
        for encoder in self.__encoders:
            data = encoder(data)
        self.__tosend.append((data, addr))
        self.refreshTimer()

    class RecvCBException(Exception):
        pass

    def __decode(self, depth, data, addr):
        decoder, remain = self.__decoders[depth]
        remain[0] += data

        while len(remain[0]) > 0:
            processed, processed_bytes = decoder(remain[0])
            if processed_bytes > 0:
                if depth == len(self.__decoders) - 1:
                    try:
                        self.__onReceivedFrom(self, processed, addr)
                    except Exception as e:
                        _logger.error('onReceived: %s', e)
                        exstr = traceback.format_exc()
                        _logger.error('%s', exstr)
                        raise self.RecvCBException(e)
                else:
                    self.__decode(depth + 1, processed) < 0
                remain[0] = remain[0][processed_bytes:]
            elif processed_bytes == 0:
                return 0

    def __onReceive(self):
        _logger.debug('__onReceive')
        while True:
            try:
                recv, addr = self.__fd.recvfrom(65536)
                if len(recv) == 0:
                    self.close()
                    return
                _logger.debug('fd: %d recv %d bytes from %s:%d', self.__fd.fileno(), len(recv), addr[0], addr[1])
                self.refreshTimer()
            except socket.error as msg:
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    _logger.error('fd: %d, recv: %s', self.__fd.fileno(), os.strerror(msg.errno))
                    self.__error = True
                    self.__closeAgain()
                return

            try:
                self.__decode(0, recv, addr)
            except self.RecvCBException:
                pass
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

    def appendSendHandler(self, handler):
        self.__encoders.append(handler)

    def appendReceiveHandler(self, handler):
        self.__decoders.append((handler, ['']))

    def __onClose(self):
        _logger.debug('__onClose')
        _logger.debug('fd: %d closed', self.__fd.fileno())
        # in case of timeout happened
        Event.delEvent(self.__wev)

        if self.__timeoutEv is not None:
            self.__timeoutEv.delTimer()
            self.__timeoutEv = None

        self.__fd.close()
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
        _logger.debug('fd: %d closing', self.__fd.fileno())

        timeout = 0
        if not self.__error and len(self.__tosend) > 0:
            timeout = 60000

        if timeout == 0:
            Event.delEvent(self.__wev)

        Event.delEvent(self.__rev)
        self.__cev = Event.addTimer(timeout)
        self.__cev.setHandler(lambda ev: self.__onClose())

    def __onTimeout(self):
        _logger.debug('__onTimeout')
        self.close()

    def refreshTimer(self):
        if self.__timeoutEv is None:
            return

        self.__timeoutEv.delTimer()
        if self.__timeout > 0:
            self.__timeoutEv = Event.addTimer(self.__timeout)
            self.__timeoutEv.setHandler(lambda ev: self.__onTimeout())

    def setTimeout(self, timeout):
        if self.__timeoutEv is not None:
            self.__timeoutEv.delTimer()
            self.__timeoutEv = None

        self.__timeout = timeout
        if self.__timeout > 0:
            self.__timeoutEv = Event.addTimer(self.__timeout)
            self.__timeoutEv.setHandler(lambda ev: self.__onTimeout())

