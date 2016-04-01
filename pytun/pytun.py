#!/usr/bin/env python
import sys
import epoll
import event
from acceptor import Acceptor
from stream import Stream
from dgram import Dgram
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
    hostlist = arg.split(',')
    for host in hostlist:
        if len(host) == 0:
            continue
        addr, port = host.split(':')
        port = int(port)
        serverlist.append((addr, port, None, None))
    return serverlist

def acceptSideReceiveTo(self, data):
    addr, port, proto = data.split(':')
    port = int(port)

    if proto == 'tcp':
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

        tunnel.setOnReceived(tunnelReceived)
        tunnel.setOnClosed(tunnelClosed)
        back.setOnReceived(backendReceived)
        back.setOnClosed(backendClosed)

    elif proto == 'udp':
        back = Dgram()
        tunnel = self

        def udpTunnelReceived(self, data):
            back.sendto(data, (addr, port))

        def udpTunnelClosed(self):
            back.close()

        def udpBackendReceivedFrom(self, data, addr):
            tunnel.send(data)

        def udpBackendClosed(self):
            tunnel.close()

        tunnel.setOnReceived(udpTunnelReceived)
        tunnel.setOnClosed(udpTunnelClosed)
        back.setOnReceivedFrom(udpBackendReceivedFrom)
        back.setOnClosed(udpBackendClosed)
        back.beginReceiving()
        # 10 min
        back.setTimeout(10 * 60 * 1000)

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
            if typ3 == 1 or typ3 == 2:
                addr, port = struct.unpack('!%dsH' % (length-6), data[4:length])
            else:
                raise Exception('Unknown message type')
            addr_port_proto = addr + ':' + str(port) + (':tcp' if typ3 == 1 else ':udp')
            _logger.debug('Message: %s', addr_port_proto)
            enable[0] = False
            return (addr_port_proto, length)
        return getToHandler

    tunnel.appendSendHandler(obscure.packData)
    tunnel.appendSendHandler(obscure.genXorEncode())
    tunnel.appendSendHandler(obscure.base64encode)
    tunnel.appendSendHandler(obscure.genHttpEncode(False))
    tunnel.appendReceiveHandler(obscure.genHttpDecode())
    tunnel.appendReceiveHandler(obscure.base64deocde)
    tunnel.appendReceiveHandler(obscure.genXorDecode())
    tunnel.appendReceiveHandler(obscure.unpackData)
    tunnel.appendReceiveHandler(genOnToHandler())
    tunnel.setOnReceived(acceptSideReceiveTo)
    tunnel.beginReceiving()

def processConnectSideArgument(arg):
    serverlist = []
    hostslist = arg.split(',')
    for hosts in hostslist:
        if len(hosts) == 0:
            continue
        fr0m, via, to, typ3 = hosts.split('/')
        addr, port = fr0m.split(':')
        via = via.split(':')
        via[1] = int(via[1])
        to = to.split(':')
        to[1] = int(to[1])
        if typ3 != 'tcp' and typ3 != 'udp':
            raise Exception('Unknown protocol')
        port = int(port)
        serverlist.append((addr, port, typ3, (via, to)))
    return serverlist

def genConnectSideAcceptor(via, to):
    def generateTo(to):
        addr, port = to
        return struct.pack('!HH%dsH' % (len(addr)), 6 + len(addr), 1, addr, port)

    def connectSideAcceptor(front):
        addr, port = via
        tunnel = Stream()
        tunnel.connect(addr, port)
        tunnel.appendSendHandler(obscure.packData)
        tunnel.appendSendHandler(obscure.genXorEncode())
        tunnel.appendSendHandler(obscure.base64encode)
        tunnel.appendSendHandler(obscure.genHttpEncode(True))
        tunnel.appendReceiveHandler(obscure.genHttpDecode())
        tunnel.appendReceiveHandler(obscure.base64deocde)
        tunnel.appendReceiveHandler(obscure.genXorDecode())
        tunnel.appendReceiveHandler(obscure.unpackData)
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

addr2Stream = {}

def genConnectSideReceiver(via, to):
    def generateTo(to):
        addr, port = to
        return struct.pack('!HH%dsH' % (len(addr)), 6 + len(addr), 2, addr, port)

    def connectSideReceiver(front, data, fr0m):
        addr, port = fr0m
        addrPort = addr + ':' + str(port)

        if addrPort in addr2Stream:
            tunnel = addr2Stream[addrPort]
        else:
            _logger.debug('new Dgram from: %s:%d', addr, port)
            addr, port = via
            tunnel = Stream()
            addr2Stream[addrPort] = tunnel
            tunnel.connect(addr, port)
            tunnel.appendSendHandler(obscure.packData)
            tunnel.appendSendHandler(obscure.genXorEncode())
            tunnel.appendSendHandler(obscure.base64encode)
            tunnel.appendSendHandler(obscure.genHttpEncode(True))
            tunnel.appendReceiveHandler(obscure.genHttpDecode())
            tunnel.appendReceiveHandler(obscure.base64deocde)
            tunnel.appendReceiveHandler(obscure.genXorDecode())
            tunnel.appendReceiveHandler(obscure.unpackData)
            tunnel.send(generateTo(to))

            def tunnelReceived(self, data):
                front.sendto(data, fr0m)

            def tunnelClosed(self):
                del addr2Stream[addrPort]

            tunnel.setOnReceived(tunnelReceived)
            tunnel.setOnClosed(tunnelClosed)

        tunnel.send(data)

    return connectSideReceiver

if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    optlist, args = getopt.getopt(sys.argv[1:], 'A:C:h')
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
        if cmd == '-h':
            print '''
Connect Side: -C from/via/to/{tcp,udp},...
Accept Side: -A addr0:port0,addr1:port1,...
'''
            sys.exit(0)

    epoll.Epoll.init()

    for addr, port, type_, arg in ServerList:
        if AcceptMode:
            acceptor = Acceptor()
            acceptor.bind(addr, port)
            acceptor.listen()
            acceptor.setOnAccepted(acceptSideAcceptor)
        else:
            via, to = arg
            if type_ == 'tcp':
                acceptor = Acceptor()
                acceptor.bind(addr, port)
                acceptor.listen()
                acceptor.setOnAccepted(genConnectSideAcceptor(via, to))
            else:
                acceptor = Dgram()
                acceptor.bind(addr, port)
                acceptor.setOnReceivedFrom(genConnectSideReceiver(via, to))
                acceptor.beginReceiving()

    event.Event.processLoop()

