import select
from event import Event
import logging
import loglevel

_logger = logging.getLogger('Epoll')
_logger.setLevel(loglevel.gLevel)

class Epoll:

    __epoll = None

    def __init__(self):
        self.__registered_read = {}
        self.__registered_write = {}
        self.__fd_mask = {}

        self.__ready = []

        self.__fd = select.epoll()

    @staticmethod
    def debugPrint():
        print("read: " + str(Epoll.__epoll.__registered_read))
        print("write: " + str(Epoll.__epoll.__registered_write))
        print("mask: " + str(Epoll.__epoll.__fd_mask))

    @staticmethod
    def init():
        Epoll.__epoll = Epoll()
        # only one instance is allowed
        Event.addEvent = staticmethod(lambda ev: Epoll.__epoll.register(ev))
        Event.delEvent = staticmethod(lambda ev: Epoll.__epoll.unregister(ev))
        Event.processEvents = staticmethod(lambda t: Epoll.__epoll.process_events(t))

    def register(self, event):
        if event.isWrite():
            mask = select.EPOLLOUT
        else:
            mask = select.EPOLLIN

        fd = event.getFd()
        if not fd in self.__fd_mask:
            self.__fd.register(fd, mask)
        else:
            mask |= self.__fd_mask[fd]
            self.__fd.modify(fd, mask)
        self.__fd_mask[fd] = mask

        if event.isWrite():
            self.__registered_write[fd] = event
        else:
            self.__registered_read[fd] = event;

    def unregister(self, event):
        if event.isWrite():
            mask = select.EPOLLOUT
        else:
            mask = select.EPOLLIN

        fd = event.getFd()
        if not fd in self.__fd_mask:
            return
        else:
            if self.__fd_mask[fd] & mask == 0:
                return
            mask = self.__fd_mask[fd] & ~mask

        if event.isWrite():
            del self.__registered_write[fd]
        else:
            del self.__registered_read[fd]

        if fd in self.__registered_write or fd in self.__registered_read:
            assert self.__fd_mask[fd] != 0
            self.__fd.modify(fd, mask)
            self.__fd_mask[fd] = mask
        else:
            assert mask == 0
            self.__fd.unregister(fd)
            del self.__fd_mask[fd]

    def process_events(self, timeout):
        self.__ready = []
        ready_list = self.__fd.poll(timeout)
        for fd, ev_type in ready_list:
            handled = False
            if ev_type & select.EPOLLOUT:
                if fd in self.__registered_write:
                    self.__ready.append(self.__registered_write[fd])
                    handled = True
            if ev_type & select.EPOLLIN:
                if fd in self.__registered_read:
                    self.__ready.append(self.__registered_read[fd])
                    handled = True
            if not handled:
                raise Exception("fd: %d, type: %s" % (fd, str(ev_type)))

        for event in self.__ready:
            event.getHandler()(event)

