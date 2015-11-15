import socket
import errno
from event import Event
from collections import deque
import logging

_logger = logging.getLogger('Stream')
_logger.setLevel(logging.WARNING)

class Stream:

    def __init__(self, conn = None):
        self.__connected = False
        self.__tosend = deque()
        self.__rev = None
        self.__wev = None
        self.__cev = None

        self.__encoders = []
        self.__decoders = []

        self.__error = False

        self.onConnected = None
        self.onReceived = None
        self.onClosed = None

        if conn == None:
            self.__fd = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.__fd.setblocking(False)
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

    def __onConnected(self):
        _logger.debug('__onConnected')
        err = self.__fd.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
        if err != 0:
            self.__error = True
            self.__closeAgain()
            return

        self.__connected = True
        self.__wev.setHandler(lambda ev: self.__onSend())
        if len(self.__tosend) == 0:
            Event.delEvent(self.__wev)
            
        if self.onConnected != None:
            self.onConnected()
        else:
            self.beginReceiving()

    def connect(self, addr, port):
        self.__wev.setHandler(lambda ev: self.__onConnected())
        try:
            self.__fd.connect((addr, port))
        except socket.error, msg:
            if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                self.__error = True
                self.__closeAgain()
                return
        Event.addEvent(self.__wev)

    def __onSend(self):
        _logger.debug('__onSend')
        while len(self.__tosend) > 0:
            data = self.__tosend.popleft()
            try:
                sent = self.__fd.send(data)
                if sent < len(data):
                    data = data[sent:]
                    self.__tosend.appendleft(data)
            except socket.error, msg:
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    self.__error = True
                    self.__closeAgain()
                    return

        Event.delEvent(self.__wev)
        if self.__cev != None:
            self.__closeAgain()

    def send(self, data):
        if len(self.__tosend) == 0 and self.__connected:
            Event.addEvent(self.__wev)
        for encoder in self.__encoders:
            data = encoder(data)
        self.__tosend.append(data)

    def __decode(self, depth, data):
        if len(self.__decoders) == 0:
            self.onReceived(self, data)
            return

        decoder, remain = self.__decoders[depth]
        remain[0] = remain[0] + data

        while len(remain[0]) > 0:
            processed, processed_bytes = decoder(remain[0])
            if processed_bytes > 0:
                if depth == len(self.__decoders) - 1:
                    self.onReceived(self, processed)
                else:
                    self.__decode(depth + 1, processed) < 0
                remain[0] = remain[0][processed_bytes:]
            elif processed_bytes == 0:
                return 0

    def __onReceive(self):
        while True:
            try:
                recv = self.__fd.recv(4096)
            except socket.error, msg:
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    self.__error = True
                    self.__closeAgain()
                    return
                else:
                    return
            if len(recv) == 0:
                break
            self.__decode(0, recv)
        self.close()

    def beginReceiving(self):
        Event.addEvent(self.__rev)

    def stopReceiving(self):
        Event.delEvent(self.__rev)

    def appendSendHandler(self, handler):
        self.__encoders.append(handler)

    def appendReceiveHandler(self, handler):
        self.__decoders.append((handler, ['']))

    def __onClose(self):
        _logger.debug("__onClose: %d closed", self.__fd.fileno())
        self.__fd.close()
        if self.onClosed != None:
            self.onClosed(self)

    def __closeAgain(self):
        if self.__cev != None:
            self.__cev.delTimer()
            self.__cev = None
        self.close()

    def close(self):
        if self.__cev != None:
            return
        _logger.debug("close: %d closing", self.__fd.fileno())

        timeout = 0
        if not self.__error and self.__connected and len(self.__tosend) > 0:
            timeout = 60000

        if timeout == 0:
            Event.delEvent(self.__wev)

        Event.delEvent(self.__rev)
        self.__cev = Event.addTimer(timeout)
        self.__cev.setHandler(lambda ev: self.__onClose())

