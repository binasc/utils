import socket
import errno
from event import Event
from stream import Stream
import logging

_logger = logging.getLogger('Acceptor')
_logger.setLevel(logging.DEBUG)

class Acceptor:

    def __init__(self):
        self.__rev = None
        self.__onAccepted = None

        self.__fd = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.__fd.setblocking(False)
        self.__fd.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    def bind(self, addr, port):
        self.__fd.bind((addr, port))

    def listen(self, backlog = 0):
        self.__fd.listen(backlog)
        self.__rev = Event()
        self.__rev.setFd(self.__fd.fileno())
        self.__rev.setHandler(lambda ev: self.__onAccept())
        Event.addEvent(self.__rev)

    def __onAccept(self):
        _logger.debug('__onAccept')
        while True:
            try:
                sock, addr = self.__fd.accept()
                _logger.debug('fd: %d accept fd: %d', self.__fd.fileno(), sock.fileno())
                sock.setblocking(False)
            except socket.error as msg:
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    _logger.error('fd: %d, accept: %s', self.__fd.fileno(), os.strerror(msg.errno))
                    self.__fd.close()
                return
            else:
                newstream = Stream(sock)
                try:
                    self.__onAccepted(newstream)
                except Exception as e:
                    _logger.error('onAccepted: %s', e)
                    newstream.close()

    def setOnAccepted(self, onAccepted):
        self.__onAccepted = onAccepted

