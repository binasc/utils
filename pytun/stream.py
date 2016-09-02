import socket
import errno
import os
from event import Event
from collections import deque
import logging
import loglevel
import traceback

_logger = logging.getLogger('Stream')
_logger.setLevel(loglevel.gLevel)

class Stream:

    def __init__(self, conn = None):
        self.__connected = False
        self.__tosend = deque()
        self.__rev = None
        self.__wev = None
        self.__cev = None
        self.__timeout = 0
        self.__timeoutEv = None


        self.__encoders = []
        self.__decoders = [(lambda data: (data, len(data)), [''])]

        self.__error = False

        self.__onConnected = None
        self.__onReceived = None
        self.__onClosed = None

        if conn == None:
            self.__fd = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.__fd.setblocking(False)
            _logger.debug('fd: %d created', self.__fd.fileno())
        else:
            self.__fd = conn 
            self.__connected = True

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

    def setBufferSize(self, bsize):
        self.__fd.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, bsize)
        self.__fd.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, bsize)

    def setCongAlgorithm(self, algo):
        TCP_CONGESTION = getattr(socket, 'TCP_CONGESTION', 13)
        self.__fd.setsockopt(socket.IPPROTO_TCP, TCP_CONGESTION, algo)

    def setOnConnected(self, onConnected):
        self.__onConnected = onConnected

    def setOnReceived(self, onReceived):
        self.__onReceived = onReceived

    def setOnClosed(self, onClosed):
        self.__onClosed = onClosed

    def __checkConnected(self):
        _logger.debug('__checkConnected')
        err = self.__fd.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
        if err != 0:
            self.__error = True
            self.__closeAgain()
            return

        self.__connected = True
        self.__wev.setHandler(lambda ev: self.__onSend())
        if len(self.__tosend) == 0:
            Event.delEvent(self.__wev)
            
        if self.__onConnected != None:
            try:
                self.__onConnected(self)
            except Exception as e:
                _logger.error('__onConnected: %s', e)
                exstr = traceback.format_exc()
                _logger.error('%s', exstr)
                self.__error = True
                self.__closeAgain()
                return
        else:
            self.beginReceiving()

    def connect(self, addr, port):
        _logger.debug('connect')
        if self.__cev != None:
            return

        self.__wev.setHandler(lambda ev: self.__checkConnected())
        try:
            self.__fd.connect((addr, port))
        except socket.error as msg:
            if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                _logger.error('fd: %d, connect: %s', self.__fd.fileno(), os.strerror(msg.errno))
                self.__error = True
                self.__closeAgain()
            else:
                Event.addEvent(self.__wev)
            return
        else:
            timer = Event.addTimer(0)
            timer.setHandler(lambda ev: self.__wev.getHandler()(self.__wev))


    def __onSend(self):
        _logger.debug('__onSend')
        while len(self.__tosend) > 0:
            data = self.__tosend.popleft()
            try:
                sent = self.__fd.send(data)
                _logger.debug('fd: %d sent %d bytes', self.__fd.fileno(), sent)
                if sent < len(data):
                    _logger.debug('fd: %d sent less than %d bytes', self.__fd.fileno(), len(data))
                    self.__tosend.appendleft(data[sent:])
            except socket.error as msg:
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    _logger.error('fd: %d, send: %s', self.__fd.fileno(), os.strerror(msg.errno))
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

        if len(self.__tosend) == 0 and self.__connected:
            Event.addEvent(self.__wev)
        for encoder in self.__encoders:
            data = encoder(data)
        self.__tosend.append(data)
        self.refreshTimer()

    class RecvCBException(Exception):
        pass

    def __decode(self, depth, data):
        decoder, remain = self.__decoders[depth]
        remain[0] += data

        while len(remain[0]) > 0:
            processed, processed_bytes = decoder(remain[0])
            if processed_bytes > 0:
                if depth == len(self.__decoders) - 1:
                    try:
                        self.__onReceived(self, processed)
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
                recv = self.__fd.recv(65536)
                if len(recv) == 0:
                    self.close()
                    return
                _logger.debug('fd: %d recv %d bytes', self.__fd.fileno(), len(recv))
                self.refreshTimer()
            except socket.error as msg:
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    _logger.error('fd: %d, recv: %s', self.__fd.fileno(), os.strerror(msg.errno))
                    self.__error = True
                    self.__closeAgain()
                return

            try:
                self.__decode(0, recv)
            except self.RecvCBException:
                pass
            except Exception as e:
                _logger.error('decode: %s', e)
                exstr = traceback.format_exc()
                _logger.error('%s', exstr)
                self.__error = True
                self.__closeAgain()

    def beginReceiving(self):
        _logger.debug('beginReceiving')
        if self.__cev != None and not self.__connected:
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
        self.__connected = False
        if self.__onClosed != None:
            try:
                self.__onClosed(self)
            except Exception as ex:
                _logger.warning('Exception on closed: %s', str(ex))
                exstr = traceback.format_exc()
                _logger.error('%s', exstr)

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
        if not self.__error and self.__connected and len(self.__tosend) > 0:
            timeout = 60000

        if timeout == 0:
            Event.delEvent(self.__wev)

        # remove timeout event
        self.setTimeout(0)

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
        if self.__cev != None:
            return

        if self.__timeoutEv is not None:
            self.__timeoutEv.delTimer()
            self.__timeoutEv = None

        self.__timeout = timeout
        if self.__timeout > 0:
            self.__timeoutEv = Event.addTimer(self.__timeout)
            self.__timeoutEv.setHandler(lambda ev: self.__onTimeout())

