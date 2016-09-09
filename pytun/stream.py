import os
import socket
import errno
from event import Event
import logging
import traceback

import loglevel
_logger = logging.getLogger('Stream')
_logger.setLevel(loglevel.gLevel)

from nonblocking import NonBlocking

class Stream(NonBlocking):

    def __init__(self, conn=None):
        if conn is None:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            _logger.debug('fd: %d created', sock.fileno())
            NonBlocking.__init__(self, sock)
        else:
            NonBlocking.__init__(self, conn)

        self._onConnected = None

    def setCongAlgorithm(self, algo):
        TCP_CONGESTION = getattr(socket, 'TCP_CONGESTION', 13)
        self._fd.setsockopt(socket.IPPROTO_TCP, TCP_CONGESTION, algo)

    def setOnConnected(self, onConnected):
        self._onConnected = onConnected

    def _checkConnected(self):
        _logger.debug('_checkConnected')
        err = self._fd.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
        if err != 0:
            self._error = True
            self._closeAgain()
            return

        self._connected = True
        self._wev.setHandler(lambda ev: self._onSend())
        if len(self._tosend) == 0:
            Event.delEvent(self._wev)
            
        if self._onConnected != None:
            try:
                self._onConnected(self)
            except Exception as e:
                _logger.error('_onConnected: %s', e)
                _logger.exception(traceback.format_exc())
                self._error = True
                self._closeAgain()
                return
        else:
            self.beginReceiving()

    def connect(self, addr, port):
        _logger.debug('connect')
        if self._cev != None:
            return

        self._wev.setHandler(lambda ev: self._checkConnected())
        try:
            _logger.debug('connecting to %s:%d', addr, port)
            self._fd.connect((addr, port))
        except socket.error as msg:
            if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                _logger.error('fd: %d, connect: %s',
                              self._fd.fileno(), os.strerror(msg.errno))
                self._error = True
                self._closeAgain()
            else:
                Event.addEvent(self._wev)
        else:
            timer = Event.addTimer(0)
            timer.setHandler(lambda ev: self._wev.getHandler()(self._wev))

    def setNonBlocking(self):
        self._fd.setblocking(False)

    def bind(self, addr, port):
        self._fd.bind((addr, port))

    def setBufferSize(self, bsize):
        self._fd.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, bsize)
        self._fd.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, bsize)

    def _send(self, data, _):
        sent = self._fd.send(data)
        _logger.debug('fd: %d sent %d bytes', self._fd.fileno(), sent)
        return sent

    def _recv(self, size):
        recv = self._fd.recv(size)
        _logger.debug('fd: %d recv %d bytes', self._fd.fileno(), len(recv))
        return recv, None

    def _close(self):
        self._fd.close()

