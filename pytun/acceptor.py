import os
import socket
import errno
from event import Event
from stream import Stream
import traceback

import loglevel
_logger = loglevel.get_logger('acceptor')


class Acceptor:

    def __init__(self):
        self._fd = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._fd.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._fd.setblocking(False)

        self._rev = Event()
        self._rev.set_write(False)
        self._rev.set_fd(self._fd.fileno())
        self._rev.set_handler(lambda ev: self._on_accept())

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

    def _on_accept(self):
        _logger.debug('_on_accept')
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
                    if self._onClosed is not None:
                        try:
                            self._onClosed(self)
                        except Exception as ex:
                            _logger.error('_onClosed: %s', str(ex))
                            _logger.exception(traceback.format_exc())
                return
            else:
                new_stream = Stream(sock)
                new_stream._connected = True
                try:
                    self._onAccepted(new_stream, addr)
                except Exception as e:
                    _logger.error('_onAccepted: %s', e)
                    _logger.exception(traceback.format_exc())
                    new_stream.close()

    def set_on_accepted(self, on_accepted):
        self._onAccepted = on_accepted

    def set_on_closed(self, on_closed):
        self._onClosed = on_closed
