import socket
import logging

import loglevel
_logger = logging.getLogger('Dgram')
_logger.setLevel(loglevel.gLevel)

from nonblocking import NonBlocking

class Dgram(NonBlocking):

    def __init__(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        _logger.debug('fd: %d created', sock.fileno())
        NonBlocking.__init__(self, sock)
        self._connected = True

