import socket
from nonblocking import NonBlocking

import loglevel
_logger = loglevel.get_logger('dgram')


class Dgram(NonBlocking):

    def __init__(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        _logger.debug('fd: %d created', sock.fileno())
        NonBlocking.__init__(self, sock)
        self._connected = True

    def set_non_blocking(self):
        self._fd.setblocking(False)

    def bind(self, addr, port):
        self._fd.bind((addr, port))

    def set_buffer_size(self, bsize):
        self._fd.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, bsize)
        self._fd.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, bsize)

    def _send(self, data, addr):
        sent = self._fd.sendto(data, addr)
        _logger.debug('fd: %d sent %d bytes to %s:%d',
                      self._fd.fileno(), sent, addr[0], addr[1])
        return sent

    def _recv(self, size):
        recv, addr = self._fd.recvfrom(size)
        _logger.debug('fd: %d recv %d bytes from %s:%d',
                      self._fd.fileno(), len(recv), addr[0], addr[1])
        return recv, addr

    def _close(self):
        self._fd.close()
