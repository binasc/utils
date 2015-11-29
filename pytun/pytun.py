#!/usr/bin/env python
import sys
import epoll
import event
from acceptor import Acceptor
from stream import Stream
import getopt
import logging
import struct
import obscure

_logger = logging.getLogger('Stream')

AcceptMode = False
ConnectMode = False
ServerList = []

def processAcceptSideArgument(arg):
    serverlist = []
    hostlist = arg.split('/')
    for host in hostlist:
        if len(host) == 0:
            continue
        addr, port = host.split(':')
        port = int(port)
        serverlist.append((addr, port, None))
    return serverlist

def acceptSideReceiveTo(self, data):
    addr, port = data.split(':')
    port = int(port)
    _logger.debug('connect to: %s:%d', addr, port)

    back = Stream()
    back.connect(addr, port)
    tunnel = self

    def tunnelReceived(self, data):
        back.send(data)

    def tunnelClosed(self):
        back.close()

    def backendReceived(self, data):
        tunnel.send(data)

    def backendClosed(self):
        tunnel.close()

    self.setOnReceived(tunnelReceived)
    self.setOnClosed(tunnelClosed)
    back.setOnReceived(backendReceived)
    back.setOnClosed(backendClosed)

def acceptSideAcceptor(tunnel):

    def genOnToHandler():
        enable = [True]
        def getToHandler(data):
            # TODO: check integration
            if enable[0] == False:
                return (data, len(data))
            if len(data) < 4:
                return (None, 0)
            length, typ3 = struct.unpack('!HH', data[0:4])
            if len(data) < length:
                return (None, 0)
            if typ3 == 1:
                addr, port = struct.unpack('!%dsH' % (length-6), data[4:length])
            addr_port = addr + ':' + str(port)
            enable[0] = False
            return (addr_port, length)
        return getToHandler

    tunnel.appendSendHandler(obscure.genXorEncode())
    tunnel.appendSendHandler(obscure.base64encode)
    tunnel.appendSendHandler(obscure.genHttpEncode(False))
    tunnel.appendReceiveHandler(obscure.genHttpDecode())
    tunnel.appendReceiveHandler(obscure.base64deocde)
    tunnel.appendReceiveHandler(obscure.genXorDecode())
    tunnel.appendReceiveHandler(genOnToHandler())
    tunnel.setOnReceived(acceptSideReceiveTo)
    tunnel.beginReceiving()


def processConnectSideArgument(arg):
    serverlist = []
    hostslist = arg.split(',')
    for hosts in hostslist:
        if len(hosts) == 0:
            continue
        fr0m, via, to = hosts.split('/')
        addr, port = fr0m.split(':')
        port = int(port)
        serverlist.append((addr, port, (via.split(':'), to.split(':'))))
    return serverlist

def genConnectSideAcceptor(via, to):
    def generateTo(to):
        addr, port = to
        return struct.pack('!HH%dsH' % (len(addr)), 6 + len(addr), 1, addr, port)

    def connectSideAcceptor(front):
        tunnel = Stream()
        addr, port = via
        tunnel.connect(addr, port)
        tunnel.appendSendHandler(obscure.genXorEncode())
        tunnel.appendSendHandler(obscure.base64encode)
        tunnel.appendSendHandler(obscure.genHttpEncode(True))
        tunnel.appendReceiveHandler(obscure.genHttpDecode())
        tunnel.appendReceiveHandler(obscure.base64deocde)
        tunnel.appendReceiveHandler(obscure.genXorDecode())
        tunnel.send(generateTo(to))

        def tunnelReceived(self, data):
            front.send(data)

        def tunnelClosed(self):
            front.close()

        def frontendReceived(self, data):
            tunnel.send(data)

        def frontendClosed(self):
            tunnel.close()

        tunnel.setOnReceived(tunnelReceived)
        tunnel.setOnClosed(tunnelClosed)
        front.setOnReceived(frontendReceived)
        front.setOnClosed(frontendClosed)
        front.beginReceiving()

    return connectSideAcceptor

if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    optlist, args = getopt.getopt(sys.argv[1:], 'A:C:')
    for cmd, arg in optlist:
        if cmd == '-A':
            AcceptMode = True
            if ConnectMode == True:
                raise Exception('Already in Connect Mode')
            ServerList = processAcceptSideArgument(arg)
        if cmd == '-C':
            ConnectMode = True
            if AcceptMode == True:
                raise Exception('Already in Accept Mode')
            ServerList = processConnectSideArgument(arg)


    epoll.Epoll.init()

    for addr, port, arg in ServerList:
        acceptor = Acceptor()
        acceptor.bind(addr, port)
        acceptor.listen()
        if AcceptMode == True:
            acceptor.setOnAccepted(acceptSideAcceptor)
        else:
            via, to = arg
            via[1] = int(via[1])
            to[1] = int(to[1])
            acceptor.setOnAccepted(genConnectSideAcceptor(via, to))

    event.Event.processLoop()
