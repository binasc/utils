import select
from event import Event

import loglevel
_logger = loglevel.getLogger('kqueue')


class Kqueue:

    _kqueue = None
    KQUEUE_MAX_EVENTS = pow(2, 31) - 1

    @staticmethod
    def init():
        Kqueue._kqueue = Kqueue()
        # only one instance is allowed
        Event.addEvent = staticmethod(lambda ev: Kqueue._kqueue.register(ev))
        Event.delEvent = staticmethod(lambda ev: Kqueue._kqueue.unregister(ev))
        Event.isEventSet = staticmethod(lambda ev: Kqueue._kqueue.isset(ev))
        Event.processEvents = staticmethod(lambda t: Kqueue._kqueue.process_events(t))

    @staticmethod
    def debug_print():
        print("read: " + str(Kqueue._kqueue._registered_read))
        print("write: " + str(Kqueue._kqueue._registered_write))
        print("changelist: " + str(Kqueue._kqueue._change_list))

    def __init__(self):
        self._fd = select.kqueue()
        self._change_list = []
        self._registered_read = {}
        self._registered_write = {}

    def register(self, event):
        if event.is_active():
            return

        fd = event.getFd()
        if event.isWrite():
            if fd in self._registered_write:
                _logger.error("Duplicate write registration of fd: %d", fd)
                raise Exception("Duplicate write registration")
            filter_ = select.KQ_FILTER_WRITE
            self._registered_write[fd] = event
        else:
            if fd in self._registered_read:
                _logger.error("Duplicate read registration of fd: %d", fd)
                raise Exception("Duplicate read registration")
            self._registered_read[fd] = event
            filter_ = select.KQ_FILTER_READ

        self._change_list.append(select.kevent(fd, filter=filter_, flags=select.KQ_EV_ADD))
        event.set_active(True)

    def unregister(self, event):
        if not event.is_active():
            return

        fd = event.getFd()
        if event.isWrite():
            if fd not in self._registered_write:
                _logger.warn("No write event registered for fd: %d", fd)
                return
            filter_ = select.KQ_FILTER_WRITE
            del self._registered_write[fd]
        else:
            if fd not in self._registered_read:
                _logger.warn("No read event registered for fd: %d", fd)
                return
            filter_ = select.KQ_FILTER_READ
            del self._registered_read[fd]

        self._change_list.append(select.kevent(fd, filter=filter_, flags=select.KQ_EV_DELETE))
        event.set_active(False)

    def isset(self, event):
        fd = event.getFd()
        if event.isWrite():
            return fd in self._registered_write
        else:
            return fd in self._registered_read

    def process_events(self, timeout):
        ready = []
        ready_list = self._fd.control(self._change_list, Kqueue.KQUEUE_MAX_EVENTS, None if timeout < 0 else timeout)
        _logger.debug("kqueue returned %d events", len(ready_list))
        _logger.debug("kqueue returned events: %s", str(ready_list))
        for kevent in ready_list:
            fd = kevent.ident
            filter_ = kevent.filter
            handled = False
            if filter_ == select.KQ_FILTER_WRITE:
                if fd in self._registered_write:
                    ready.append(self._registered_write[fd])
                    handled = True
            if filter_ == select.KQ_FILTER_READ:
                if fd in self._registered_read:
                    ready.append(self._registered_read[fd])
                    handled = True
            if not handled:
                if filter_ == select.KQ_FILTER_READ and kevent.flags == select.KQ_EV_ERROR:
                    _logger.debug("Unhandled fd: %d with event filter: %d", fd, filter_)
                else:
                    _logger.warn("Unhandled fd: %d with event filter: %d", fd, filter_)
        self._change_list = []

        for event in ready:
            try:
                if self.isset(event):
                    event.getHandler()(event)
            except Exception as ex:
                _logger.warning('Event handler exception: %s' % str(ex))
                import traceback
                traceback.print_exc()