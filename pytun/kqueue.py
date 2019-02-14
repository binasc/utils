import select
import time
import os
import traceback
from event import Event

import loglevel
_logger = loglevel.get_logger('kqueue')
_logger.setLevel(loglevel.DEFAULT_LEVEL)


class Kqueue:

    _kqueue = None
    KQUEUE_MAX_EVENTS = pow(2, 31) - 1

    @staticmethod
    def init():
        Kqueue._kqueue = Kqueue()
        # only one instance is allowed
        Event.addEvent = staticmethod(lambda ev: Kqueue._kqueue.register(ev))
        Event.delEvent = staticmethod(lambda ev: Kqueue._kqueue.deregister(ev))
        Event.isEventSet = staticmethod(lambda ev: Kqueue._kqueue.is_set(ev))
        Event.processEvents = staticmethod(lambda t: Kqueue._kqueue.process_events(t))

    def __init__(self):
        self._fd = select.kqueue()
        self._registered_read = {}
        self._registered_write = {}

        self._last_time = time.time()

    def register(self, event):
        fd = event.get_fd()
        if event.is_write():
            assert(fd not in self._registered_write)
            self._registered_write[fd] = event
            filter_ = select.KQ_FILTER_WRITE
        else:
            assert(fd not in self._registered_read)
            self._registered_read[fd] = event
            filter_ = select.KQ_FILTER_READ
        self._fd.control([select.kevent(fd, filter=filter_, flags=select.KQ_EV_ADD)], 0, 0)

    def deregister(self, event):
        fd = event.get_fd()
        if event.is_write():
            if fd not in self._registered_write:
                _logger.warn("No write event registered for fd: %d", fd)
                return
            del self._registered_write[fd]
            filter_ = select.KQ_FILTER_WRITE
        else:
            if fd not in self._registered_read:
                _logger.warn("No read event registered for fd: %d", fd)
                return
            del self._registered_read[fd]
            filter_ = select.KQ_FILTER_READ
        self._fd.control([select.kevent(fd, filter=filter_, flags=select.KQ_EV_DELETE)], 0, 0)

    def is_set(self, event):
        fd = event.get_fd()
        if event.is_write():
            return fd in self._registered_write
        else:
            return fd in self._registered_read

    def _close_fd(self, fd):
        if fd in self._registered_read:
            self._fd.control([select.kevent(fd, filter=select.KQ_FILTER_WRITE, flags=select.KQ_EV_DELETE)], 0, 0)
            del self._registered_read[fd]
        if fd in self._registered_write:
            self._fd.control([select.kevent(fd, filter=select.KQ_FILTER_READ, flags=select.KQ_EV_DELETE)], 0, 0)
            del self._registered_write[fd]
        try:
            os.close(fd)
        finally:
            pass

    def process_events(self, timeout):
        ready = []
        ready_list = self._fd.control([], Kqueue.KQUEUE_MAX_EVENTS, None if timeout < 0 else timeout)
        for k_event in ready_list:
            fd = k_event.ident
            filter_ = k_event.filter
            handled = False
            if filter_ == select.KQ_FILTER_WRITE:
                if fd in self._registered_write:
                    ready.append(self._registered_write[fd])
                    handled = True
            if filter_ == select.KQ_FILTER_READ:
                if fd in self._registered_read:
                    ready.append(self._registered_read[fd])
                    handled = True
            if filter_ == select.KQ_FILTER_WRITE or filter_ == select.KQ_FILTER_READ:
                if handled is False:
                    _logger.debug('%s not handled', str(k_event))
                assert(handled is True)

        for event in ready:
            try:
                if self.is_set(event):
                    event.get_handler()(event)
            except Exception as ex:
                self._close_fd(event.get_fd())
                _logger.warning('event handler exception: %s', str(ex))
                traceback.print_exc()

        current_time = time.time()
        if current_time - self._last_time > 60.0:
            _logger.info('current number of opened fd: %d',
                         len(self._registered_write.viewkeys() | self._registered_read.viewkeys()))
            self._last_time = current_time
