import time
import heapq
import logging
import loglevel

_logger = logging.getLogger('Event')
_logger.setLevel(loglevel.gLevel)

class Event:

    __timers = []

    def __init__(self):
        self.__fd = 0
        self.__write = False
        self.__handler = None
        self.__timer_set = False

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
        return -1

    @staticmethod
    def expireTimers():
        current = time.time()
        while len(Event.__timers) > 0:
            timeout, event = Event.__timers[0]
            if not event.__timer_set:
                heapq.heappop(Event.__timers)
            elif timeout > current:
                break
            else:
                heapq.heappop(Event.__timers)
                event.__handler(event)

    addEvent = None
    # TODO: Timer?
    delEvent = None
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


