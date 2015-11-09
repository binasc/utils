#!/usr/bin/env python
import logging
import sys
import socket
import select
from event import Event

class Epoll:

    __registered_read = {}
    __registered_write = {}

    __ready = []

    def __init__(self):
        self.__fd = select.epoll()

    def register(self, event):
        if event.isWrite():
            mask = select.EPOLLOUT
        else:
            mask = select.EPOLLIN
        fd = event.getFd()
        self.__fd.register(fd, mask)
        if event.isWrite():
            self.__registered_write[fd] = event
        else:
            self.__registered_read[fd] = event;

    def process_events(self, timeout = -1):
        self.__ready = []
        ready_list = self.__fd.poll(timeout)
        for fd, ev_type in ready_list:
            if ev_type & select.EPOLLOUT:
                if fd in self.__registered_write:
                    self.__ready.append(self.__registered_write[fd])
            elif ev_type & select.EPOLLIN:
                if fd in self.__registered_read:
                    self.__ready.append(self.__registered_read[fd])

    def process_events_and_timers(self):
        print 'loop'
        self.process_events(5)

        for event in self.__ready:
            event.getHandler()(event)

    def process_loop(self):
        while True:
            self.process_events_and_timers()


def on_read(event):
    print("on read")

def on_write(event):
    print("on write")

if __name__ == '__main__':
    epoll = Epoll()

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    print("fd: " + str(server.fileno()))
    server.setblocking(False)
    server.bind(('0.0.0.0', 12345))
    server.listen(5)

    rev = Event();
    rev.setFd(server.fileno())
    rev.setHandler(on_read)

    epoll.register(rev)

    epoll.process_loop()
