import os
import socket
import errno
import traceback
from event import Event
from nonblocking import NonBlocking

import loglevel
_logger = loglevel.get_logger('stream')
_logger.setLevel(loglevel.DEFAULT_LEVEL)


class Stream(NonBlocking):

    def __init__(self, conn=None, prefix='TCP'):
        if conn is None:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            _logger.debug('fd: %d created', sock.fileno())
            NonBlocking.__init__(self, sock, prefix)
        else:
            NonBlocking.__init__(self, conn, prefix)

        self._fd.setsockopt(socket.IPPROTO_TCP, socket.SO_KEEPALIVE, 1)
        if hasattr(socket, "TCP_KEEPIDLE"):
            self._fd.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPIDLE, 60)

        if hasattr(socket, "TCP_KEEPINTVL"):
            self._fd.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPINTVL, 10)

        if hasattr(socket, "TCP_KEEPCNT"):
            self._fd.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPCNT, 3)

        self._onConnected = None

    def set_cong_algorithm(self, algorithm):
        tcp_congestion = getattr(socket, 'TCP_CONGESTION', 13)
        self._fd.setsockopt(socket.IPPROTO_TCP, tcp_congestion, algorithm)

    def set_tcp_no_delay(self):
        self._fd.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def set_on_connected(self, on_connected):
        self._onConnected = on_connected

    def _check_connected(self):
        _logger.debug('_check_connected')
        err = self._fd.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
        if err != 0:
            self._error = True
            self._do_close()
            return

        self._connected = True
        self._wev.set_handler(lambda ev: self._on_send())
        if len(self._to_send) == 0:
            Event.delEvent(self._wev)

        if self._onConnected is not None:
            try:
                self._onConnected(self)
            except Exception as e:
                _logger.error('_on_connected: %s', e)
                _logger.exception(traceback.format_exc())
                self._error = True
                self._do_close()
                return
        else:
            self.start_receiving()

    def connect(self, addr, port):
        _logger.debug('connect')
        if self.is_closed():
            return

        self._wev.set_handler(lambda ev: self._check_connected())
        try:
            _logger.debug('connecting to %s:%d', addr, port)
            self._fd.connect((addr, port))
        except socket.error as msg:
            if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                _logger.error('fd: %d, connect: %s',
                              self._fd.fileno(), os.strerror(msg.errno))
                self._error = True
                self._do_close()
            else:
                Event.addEvent(self._wev)
        else:
            timer = Event.add_timer(0)
            timer.set_handler(lambda ev: self._wev.get_handler()(self._wev))

    def set_non_blocking(self):
        self._fd.setblocking(False)

    def bind(self, addr, port):
        self._fd.bind((addr, port))

    def set_buffer_size(self, bsize):
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

    def _send_fin(self):
        self._fd.shutdown(socket.SHUT_WR)

    def _close(self):
        self._fd.close()
