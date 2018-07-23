import select
import time
import os
from event import Event

import loglevel
_logger = loglevel.get_logger('epoll')
_logger.setLevel(loglevel.DEFAULT_LEVEL)


class Epoll:

    _epoll = None

    @staticmethod
    def init():
        Epoll._epoll = Epoll()
        # only one instance is allowed
        Event.addEvent = staticmethod(lambda ev: Epoll._epoll.register(ev))
        Event.delEvent = staticmethod(lambda ev: Epoll._epoll.deregister(ev))
        Event.isEventSet = staticmethod(lambda ev: Epoll._epoll.is_set(ev))
        Event.processEvents = staticmethod(lambda t: Epoll._epoll.process_events(t))


    def __init__(self):
        self._fd = select.epoll()

        self._registered_read = {}
        self._registered_write = {}
        self._fd_mask = {}

        self._last_time = time.time()

    def register(self, event):
        if event.is_write():
            mask = select.EPOLLOUT
        else:
            mask = select.EPOLLIN

        fd = event.get_fd()
        if fd not in self._fd_mask:
            self._fd.register(fd, mask)
        else:
            mask |= self._fd_mask[fd]
            self._fd.modify(fd, mask)
        self._fd_mask[fd] = mask

        if event.is_write():
            assert(fd not in self._registered_write)
            self._registered_write[fd] = event
        else:
            assert(fd not in self._registered_read)
            self._registered_read[fd] = event

    def deregister(self, event):
        if event.is_write():
            mask = select.EPOLLOUT
        else:
            mask = select.EPOLLIN

        fd = event.get_fd()
        if fd not in self._fd_mask or self._fd_mask[fd] & mask == 0:
            return
        else:
            mask = self._fd_mask[fd] & ~mask

        if event.is_write():
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

    def is_set(self, event):
        fd = event.get_fd()
        if fd in self._fd_mask:
            if event.is_write():
                return fd in self._registered_write
            else:
                return fd in self._registered_read
        return False

    def _close_fd(self, fd):
        if fd in self._registered_read:
            del self._registered_read[fd]
        if fd in self._registered_write:
            del self._registered_write[fd]
        if fd in self._fd_mask:
            del self._fd_mask[fd]
            try:
                self._fd.unregister(fd)
            finally:
                pass
            try:
                os.close(fd)
            finally:
                pass

    def process_events(self, timeout):
        _ready = []
        ready_list = self._fd.poll(timeout)
        for fd, ev_type in ready_list:
            handled = False
            if ev_type & select.EPOLLOUT:
                if fd in self._registered_write:
                    _ready.append(self._registered_write[fd])
                    handled = True
            if ev_type & select.EPOLLIN:
                if fd in self._registered_read:
                    _ready.append(self._registered_read[fd])
                    handled = True
            if not handled and fd not in self._fd_mask:
                _logger.warning("wild fd: %d closed with type: %s", fd, str(ev_type))
                self._close_fd(fd)
                handled = True
            if not handled:
                _logger.warning("fd: %d, type: %s" % (fd, str(ev_type)))
                if fd in self._registered_write:
                    _ready.append(self._registered_write[fd])
                if fd in self._registered_read:
                    _ready.append(self._registered_read[fd])

        for event in _ready:
            try:
                if self.is_set(event):
                    event.get_handler()(event)
            except Exception as ex:
                _logger.warning('event handler exception: %s', str(ex))
                self._close_fd(event.get_fd())

        current_time = time.time()
        if current_time - self._last_time > 60.0:
            _logger.info("current number of opened fd: %d", len(self._fd_mask))
            self._last_time = current_time
