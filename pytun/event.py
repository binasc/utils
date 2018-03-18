import time
import heapq

import loglevel
_logger = loglevel.getLogger('event')
_logger.setLevel(loglevel.gLevel)

class Event:

    __timers = []

    def __init__(self):
        self.__fd = 0
        self.__write = False
        self.__handler = None
        self.__timer_set = False
        self._active = False

    def setFd(self, fd):
        self.__fd = fd

    def getFd(self):
        return self.__fd

    def isWrite(self):
        return self.__write

    def setWrite(self, write):
        self.__write = write

    def getHandler(self):
        return self.__handler

    def setHandler(self, handler):
        self.__handler = handler

    def set_active(self, active):
        self._active = active

    def is_active(self):
        return self._active

    @staticmethod
    def addTimer(mseconds):
        event = Event()
        event.__timer_set = True
        timeout = time.time() + mseconds / 1000.0
        heapq.heappush(Event.__timers, (timeout, event))
        return event

    def delTimer(self):
        self.__timer_set = False

    @staticmethod
    def findTimer():
        current = time.time()
        while len(Event.__timers) > 0:
            timeout, event = Event.__timers[0]
            if not event.__timer_set:
                heapq.heappop(Event.__timers)
            elif timeout > current:
                return timeout - current
            else:
                return 0
        #return -1
        return 5

    @staticmethod
    def expireTimers():
        current = time.time()
        while len(Event.__timers) > 0:
            timeout, event = Event.__timers[0]
            if timeout > current:
                break
            else:
                heapq.heappop(Event.__timers)
                if event.__timer_set:
                    try:
                        event.__handler(event)
                    except Exception as ex:
                        _logger.warning("timer handler exception: %s", str(ex))

    addEvent = None
    delEvent = None
    isEventSet = None
    processEvents = None

    @staticmethod
    def processEventsAndTimers():
        timeout = Event.findTimer()
        _logger.debug('loop: %f s', timeout)
        Event.processEvents(timeout)

        Event.expireTimers()

    @staticmethod
    def processLoop():
        while True:
            Event.processEventsAndTimers()


