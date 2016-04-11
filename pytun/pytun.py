#!/usr/bin/env python
import sys
import epoll
import event
from acceptor import Acceptor
from stream import Stream
from dgram import Dgram
from tun import TunDevice
import getopt
import logging
import struct
import obscure
import loglevel

_logger = logging.getLogger('Stream')
_logger.setLevel(loglevel.gLevel)

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

to2tun = {}
dst2tunnel = {}

def acceptSideReceiveTo(self, data):
    args = data.split(':')
    proto = args[0]
    addr = args[1]
    port = int(args[2])

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

    elif proto == 'tun':
        srcAddr = args[3]
        srcPort = args[4]
        tunnel = self

        dst2tunnel[srcAddr + srcPort] = tunnel

        def tunDeviceReceived(self, data, src, dst):
            dstAddr, dstPort = dst
            if dstAddr + str(dstPort) in dst2tunnel:
                tunnel = dst2tunnel[dstAddr + str(dstPort)]
                tunnel.send(data)
            else:
                _logger.warning('unknown dst %s:%s', dstAddr, str(dstPort))

        addrPort = addr + str(port)
        if addrPort in to2tun:
            tunDevice = to2tun[addrPort]
        else:
            # TODO
            tunDevice = TunDevice('tun', addr, '255.255.255.0')
            tunDevice.setOnReceived(tunDeviceReceived)
            tunDevice.beginReceiving()
            to2tun[addrPort] = tunDevice

        def tunDeviceTunnelReceived(self, data):
            # TODO: do check here?
            tunDevice.send(data)

        def tunDeviceTunnelClosed(self):
            del dst2tunnel[srcAddr + srcPort]

        tunnel.setOnReceived(tunDeviceTunnelReceived)
        tunnel.setOnClosed(tunDeviceTunnelClosed)


def acceptSideAcceptor(tunnel):

    PROTO_STR = [ 'tcp', 'udp', 'tun' ]

    def genOnToHandler():
        enable = [True]
        def getToHandler(data):
            # TODO: check integration
            if enable[0] == False:
                return (data, len(data))
            if len(data) < 4:
                return (None, 0)
            length, type_ = struct.unpack('!HH', data[0:4])
            if len(data) < length:
                return (None, 0)
            if type_ == 1 or type_ == 2 or type_ == 3:
                addr, port = struct.unpack('!%dsH' % (length-6), data[4:length])
            else:
                raise Exception('Unknown message type')
            ret = PROTO_STR[type_-1] + ':' + addr + ':' + str(port)
            if type_ == 3:
                data = data[length:]
                remain = struct.unpack('!H', data[:2])[0]
                addr, port = struct.unpack('!%dsH' % (remain-4), data[2:remain])
                ret = ret + ':' + addr + ':' + str(port)
                length += remain
                
            _logger.debug('Message: %s', ret)
            enable[0] = False
            return (ret, length)
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
        if typ3 != 'tcp' and typ3 != 'udp' and typ3 != 'tun':
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

            _logger.debug('new Dgram from: %s:%d', addr, port)
            addr2Stream[addrPort] = tunnel

            def tunnelReceived(self, data):
                front.sendto(data, fr0m)

            def tunnelClosed(self):
                del addr2Stream[addrPort]

            tunnel.setOnReceived(tunnelReceived)
            tunnel.setOnClosed(tunnelClosed)

        tunnel.send(data)

    return connectSideReceiver

src2Stream = {}

def genConnectSideMultiplex(via, to):
    def generateTo(to, src):
        toAddr, toPort = to
        srcAddr, srcPort = src
        return struct.pack('!HH%dsHH%dsH' % (len(toAddr), len(srcAddr)),
                           6 + len(toAddr), 3, toAddr, toPort,
                           4 + len(srcAddr), srcAddr, srcPort)

    def connectSideMultiplex(front, data, src, dst):
        addr, port = src 
        addrPort = addr + ':' + str(port)

        if addrPort in src2Stream:
            tunnel = src2Stream[addrPort]
        else:
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
            tunnel.send(generateTo(to, src))

            _logger.debug('new TunCon from: %s:%d', addr, port)
            src2Stream[addrPort] = tunnel

            # 10 min
            tunnel.setTimeout(10 * 60 * 1000)

            def tunnelReceived(self, data):
                front.send(data)

            def tunnelClosed(self):
                del src2Stream[addrPort]

            tunnel.setOnReceived(tunnelReceived)
            tunnel.setOnClosed(tunnelClosed)

        tunnel.send(data)

    return connectSideMultiplex

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
Connect Side: -C from/via/to/{tcp,udp,tun},...
Accept Side: -A addr0:port0,addr1:port1,...
'''
            sys.exit(0)

    epoll.Epoll.init()

    for addr, port, type_, arg in ServerList:
        if AcceptMode:
            acceptor = Acceptor()
            acceptor.bind(addr, port)
            _logger.debug('listening at: %s:%d', addr, port)
            acceptor.listen()
            acceptor.setOnAccepted(acceptSideAcceptor)
        else:
            via, to = arg
            if type_ == 'tcp':
                acceptor = Acceptor()
                acceptor.bind(addr, port)
                acceptor.listen()
                acceptor.setOnAccepted(genConnectSideAcceptor(via, to))
            elif type_ == 'udp':
                acceptor = Dgram()
                acceptor.bind(addr, port)
                acceptor.setOnReceivedFrom(genConnectSideReceiver(via, to))
                acceptor.beginReceiving()
            elif type_ == 'tun':
                # XXX: use port as prefix
                acceptor = TunDevice('tun', addr, '255.255.255.0')
                acceptor.setOnReceived(genConnectSideMultiplex(via, to))
                acceptor.beginReceiving()

    event.Event.processLoop()

