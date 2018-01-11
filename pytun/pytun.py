#!/usr/bin/env python
import sys
import epoll
import event
import getopt
import threading
import subprocess

import loglevel
_logger = loglevel.getLogger('main')

from acceptor import Acceptor
from dgram import Dgram
from tundevice import TunDevice

import tcptun
import udptun
import tuntun

import server

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

def acceptorOnClosed(self):
    _logger.exception('Acceptor closed!')
    sys.exit(-1)

_helpText = '''Usage:
Connect Side: -C from/via/to/{tcp,udp,tun},...
Accept Side: -A addr0:port0,addr1:port1,...'''

if __name__ == '__main__':
    try:
        import epoll
        epoll.Epoll.init()
        _logger.debug("epoll")
    except:
        try:
            import kqueue
            kqueue.Kqueue.init()
            _logger.debug("kqueue")
        except:
            raise Exception("Failed to init")

    ServerList = []
    AcceptMode = False
    ConnectMode = False

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

    if not AcceptMode and not ConnectMode:
        print(_helpText)
        sys.exit(0)

    for addr, port, type_, arg in ServerList:
        if AcceptMode:
            acceptor = Acceptor()
            acceptor.bind(addr, port)
            acceptor.listen()
            acceptor.setOnAccepted(server.serverSideOnAccepted)
            acceptor.setOnClosed(acceptorOnClosed)
        else:
            via, to = arg
            if type_ == 'tcp':
                acceptor = Acceptor()
                acceptor.bind(addr, port)
                acceptor.listen()
                acceptor.setOnAccepted(tcptun.genOnClientAccepted(via, to))
                acceptor.setOnClosed(acceptorOnClosed)
            elif type_ == 'udp':
                receiver = Dgram()
                receiver.bind(addr, port)
                receiver.setOnReceived(udptun.genOnReceived(via, to))
                receiver.setOnClosed(acceptorOnClosed)
                receiver.beginReceiving()
            elif type_ == 'tun':
                # port as cidr prefix
                receiver = TunDevice('tun', addr, port)
                receiver.setOnReceived(tuntun.genOnReceived(via, to))
                receiver.setOnClosed(acceptorOnClosed)
                receiver.beginReceiving()
                def pingThread():
                    subprocess.Popen(['ping', '-c', '1', to[0]]).wait()

                t = threading.Thread(target=pingThread)
                t.start()

    event.Event.processLoop()

