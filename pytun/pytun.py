#!/usr/bin/env python
import sys
import epoll
import event
import threading
from acceptor import Acceptor
from dgram import Dgram
from tundevice import TunDevice
import getopt
import logging
import struct
import subprocess
import json

import common
import tcptun
import udptun
import tuntun

import loglevel
_logger = logging.getLogger('Main')
_logger.setLevel(loglevel.gLevel)

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

def processConnectSideArgument(arg):
    protocols = set(['tcp', 'udp', 'tun'])
    serverlist = []
    hostslist = arg.split(',')
    for hosts in hostslist:
        if len(hosts) == 0:
            continue
        from_, via, to, type_ = hosts.split('/')
        addr, port = from_.split(':')
        via = via.split(':')
        via[1] = int(via[1])
        to = to.split(':')
        to[1] = int(to[1])
        if type_ not in protocols:
            raise Exception('Unknown protocol')
        port = int(port)
        serverlist.append((addr, port, type_, (via, to)))
    return serverlist

def acceptSideOnReceived(tunnel, header):
    try:
        header = json.loads(header)
    except Exception as ex:
        raise Exception('unrecognizable data: %s' % str(ex))

    if 'type' not in header:
        raise Exception('unrecognizable data')

    if header['type']  == 'tcp':
        tcptun.acceptSideReceiver(tunnel, header)
    elif header['type'] == 'udp':
        udptun.acceptSideReceiver(tunnel, header)
    elif header['type'] == 'tun':
        tuntun.acceptSideReceiver(tunnel, header)

def acceptSideOnAccepted(tunnel):

    def genHeaderHandler():
        enable = [True]
        def headerHandler(data):
            # TODO: check integration
            if enable[0] == False:
                return (data, len(data))
            header, processed = common.unwrapContent(data)
            if header is None:
                return (None, 0)

            _logger.debug('Header: %s', header)
            enable[0] = False
            return (header, processed)

        return headerHandler

    common.initializeTunnel(tunnel, isRequest=False)

    tunnel.appendReceiveHandler(genHeaderHandler())
    tunnel.setOnReceived(acceptSideOnReceived)
    tunnel.beginReceiving()

def acceptorOnClosed(self):
    _logger.exception('Acceptor closed!')
    sys.exit(-1)

_helpText = '''Usage:
Connect Side: -C from/via/to/{tcp,udp,tun},...
Accept Side: -A addr0:port0,addr1:port1,...'''

if __name__ == '__main__':
    ServerList = []
    AcceptMode = False
    ConnectMode = False

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
            print(_helpText)
            sys.exit(0)

    epoll.Epoll.init()

    for addr, port, type_, arg in ServerList:
        if AcceptMode:
            acceptor = Acceptor()
            acceptor.bind(addr, port)
            _logger.debug('listening at: %s:%d', addr, port)
            acceptor.listen()
            acceptor.setOnAccepted(acceptSideOnAccepted)
            acceptor.setOnClosed(acceptorOnClosed)
        else:
            via, to = arg
            if type_ == 'tcp':
                acceptor = Acceptor()
                acceptor.bind(addr, port)
                acceptor.listen()
                acceptor.setOnAccepted(tcptun.genOnAccepted(via, to))
                acceptor.setOnClosed(acceptorOnClosed)
            elif type_ == 'udp':
                acceptor = Dgram()
                acceptor.bind(addr, port)
                acceptor.setOnReceivedFrom(udptun.genOnReceivedFrom(via, to))
                acceptor.beginReceiving()
            elif type_ == 'tun':
                # XXX: use port as prefix
                acceptor = TunDevice('tun', addr, '255.255.255.0')
                acceptor.setOnReceived(tuntun.genOnReceived(via, to))
                acceptor.beginReceiving()
                def pingThread():
                    subprocess.Popen(['ping', '-c', '1', to[0]]).wait()

                t = threading.Thread(target=pingThread)
                t.start()

    event.Event.processLoop()

