import time
import heapq
import traceback

import logging
import loglevel
_logger = loglevel.get_logger('event')
_logger.setLevel(loglevel.DEFAULT_LEVEL)


class Event:

    _timers = []

    def __init__(self):
        self._fd = 0
        self._write = False
        self._handler = None
        self._timer_set = False

    def set_fd(self, fd):
        self._fd = fd

    def get_fd(self):
        return self._fd

    def is_write(self):
        return self._write

    def set_write(self, write):
        self._write = write

    def get_handler(self):
        return self._handler

    def set_handler(self, handler):
        self._handler = handler

    @staticmethod
    def add_timer(milliseconds):
        event = Event()
        event._timer_set = True
        timeout = time.time() + milliseconds / 1000.0
        heapq.heappush(Event._timers, (timeout, event))
        return event

    def del_timer(self):
        self._timer_set = False

    def is_timer(self):
        return self._timer_set

    @staticmethod
    def find_timer():
        current = time.time()
        while len(Event._timers) > 0:
            timeout, event = Event._timers[0]
            if not event.is_timer():
                heapq.heappop(Event._timers)
            elif timeout > current:
                return timeout - current
            else:
                return 0
        # return -1
        return 5

    @staticmethod
    def expire_timers():
        current = time.time()
        while len(Event._timers) > 0:
            timeout, event = Event._timers[0]
            if timeout > current:
                break
            else:
                heapq.heappop(Event._timers)
                if event.is_timer():
                    try:
                        event.get_handler()(event)
                    except Exception as ex:
                        _logger.warning("timer handler exception: %s", str(ex))
                        if _logger.level <= logging.DEBUG:
                            traceback.print_exc()

    addEvent = None
    delEvent = None
    isEventSet = None
    processEvents = None

    @staticmethod
    def process_events_and_timers():
        timeout = Event.find_timer()
        _logger.debug('loop: %f s', timeout)
        Event.processEvents(timeout)

        Event.expire_timers()

    @staticmethod
    def process_loop():
        while True:
            Event.process_events_and_timers()
