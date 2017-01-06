import select
from event import Event
import logging
import loglevel

_logger = logging.getLogger('Epoll')
_logger.setLevel(loglevel.gLevel)

class Epoll:

    _epoll = None

    def __init__(self):
        self._registered_read = {}
        self._registered_write = {}
        self._fd_mask = {}

        self._ready = []

        self._fd = select.epoll()

    @staticmethod
    def debugPrint():
        print("read: " + str(Epoll._epoll._registered_read))
        print("write: " + str(Epoll._epoll._registered_write))
        print("mask: " + str(Epoll._epoll._fd_mask))

    @staticmethod
    def init():
        Epoll._epoll = Epoll()
        # only one instance is allowed
        Event.addEvent = staticmethod(lambda ev: Epoll._epoll.register(ev))
        Event.delEvent = staticmethod(lambda ev: Epoll._epoll.unregister(ev))
        Event.isEventSet = staticmethod(lambda ev: Epoll._epoll.isset(ev))
        Event.processEvents = staticmethod(lambda t: Epoll._epoll.process_events(t))

    def register(self, event):
        if event.isWrite():
            mask = select.EPOLLOUT
        else:
            mask = select.EPOLLIN

        fd = event.getFd()
        if fd not in self._fd_mask:
            self._fd.register(fd, mask)
        else:
            mask |= self._fd_mask[fd]
            self._fd.modify(fd, mask)
        self._fd_mask[fd] = mask

        if event.isWrite():
            self._registered_write[fd] = event
        else:
            self._registered_read[fd] = event;

    def unregister(self, event):
        if event.isWrite():
            mask = select.EPOLLOUT
        else:
            mask = select.EPOLLIN

        fd = event.getFd()
        if fd not in self._fd_mask or self._fd_mask[fd] & mask == 0:
            return
        else:
            mask = self._fd_mask[fd] & ~mask

        if event.isWrite():
            del self._registered_write[fd]
        else:
            del self._registered_read[fd]

        if fd in self._registered_write or fd in self._registered_read:
            assert(mask != 0)
            self._fd.modify(fd, mask)
            self._fd_mask[fd] = mask
        else:
            assert(mask == 0)
            self._fd.unregister(fd)
            del self._fd_mask[fd]

    def isset(self, event):
        fd = event.getFd()
        if fd in self._fd_mask:
            if event.isWrite():
                return fd in self._registered_write
            else:
                return fd in self._registered_read
        return False

    def process_events(self, timeout):
        self._ready = []
        ready_list = self._fd.poll(timeout)
        for fd, ev_type in ready_list:
            handled = False
            if ev_type & select.EPOLLOUT:
                if fd in self._registered_write:
                    self._ready.append(self._registered_write[fd])
                    handled = True
            if ev_type & select.EPOLLIN:
                if fd in self._registered_read:
                    self._ready.append(self._registered_read[fd])
                    handled = True
            if not handled:
                _logger.warning("fd: %d, type: %s" % (fd, str(ev_type)))
                if fd in self._registered_write:
                    self._ready.append(self._registered_write[fd])
                if fd in self._registered_read:
                    self._ready.append(self._registered_read[fd])

        for event in self._ready:
            try:
                event.getHandler()(event)
            except Exception as ex:
                _logger.warning('event handler exception: %s' % str(ex))

