import os
import socket
import errno
from event import Event
from stream import Stream
import traceback

import loglevel
_logger = loglevel.getLogger('acceptor')

class Acceptor:

    def __init__(self):
        self._fd = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._fd.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._fd.setblocking(False)

        self._rev = Event()
        self._rev.setWrite(False)
        self._rev.setFd(self._fd.fileno())
        self._rev.setHandler(lambda ev: self._onAccept())

        self._onAccepted = None
        self._onClosed = None

    def bind(self, addr, port):
        _logger.debug('bind')
        self._fd.bind((addr, port))
        _logger.debug('bind to: %s:%d', addr, port)

    def listen(self, backlog=0):
        _logger.debug('listen')
        self._fd.listen(backlog)
        Event.addEvent(self._rev)

    def _onAccept(self):
        _logger.debug('_onAccept')
        while True:
            try:
                sock, addr = self._fd.accept()
                _logger.debug('fd: %d accept fd: %d',
                              self._fd.fileno(), sock.fileno())
            except socket.error as msg:
                if msg.errno == errno.ECONNABORTED:
                    continue
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    _logger.error('fd: %d, accept: %s',
                                  self._fd.fileno(), os.strerror(msg.errno))
                    self._fd.close()
                    if self._onClosed != None:
                        try:
                            self._onClosed(self)
                        except Exception as ex:
                            _logger.error('_onClosed: %s', str(ex))
                            _logger.exception(traceback.format_exc())
                return
            else:
                newstream = Stream(sock)
                newstream._connected = True
                try:
                    self._onAccepted(newstream, addr)
                except Exception as e:
                    _logger.error('_onAccepted: %s', e)
                    _logger.exception(traceback.format_exc())
                    newstream.close()

    def setOnAccepted(self, onAccepted):
        self._onAccepted = onAccepted

    def setOnClosed(self, onClosed):
        self._onClosed = onClosed

