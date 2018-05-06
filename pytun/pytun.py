#!/usr/bin/env python
import sys
import event
import getopt
import threading
import subprocess
from acceptor import Acceptor
from dgram import Dgram
from tundevice import TunDevice

import tcptun
import udptun
import tuntun

import server

import loglevel
_logger = loglevel.get_logger('main')


def process_accept_side_argument(argument):
    server_list = []
    host_list = argument.split(',')
    for host in host_list:
        if len(host) == 0:
            continue
        addr, port = host.split(':')
        server_list.append((addr, int(port), None, None))
    return server_list


def process_connect_side_argument(argument):
    protocols = set(['tcp', 'udp', 'tun'])
    server_list = []
    hosts_list = argument.split(',')
    for hosts in hosts_list:
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
        server_list.append((addr, port, type_, (via, to)))
    return server_list


def acceptor_on_closed(_self):
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

    server_list = []
    accept_mode = False
    connect_mode = False

    optlist, args = getopt.getopt(sys.argv[1:], 'A:C:h')
    for cmd, arg in optlist:
        if cmd == '-A':
            accept_mode = True
            if connect_mode is True:
                raise Exception('Already in Connect Mode')
            server_list = process_accept_side_argument(arg)
        if cmd == '-C':
            connect_mode = True
            if accept_mode is True:
                raise Exception('Already in Accept Mode')
            server_list = process_connect_side_argument(arg)
        if cmd == '-h':
            print(_helpText)
            sys.exit(0)

    if not accept_mode and not connect_mode:
        print(_helpText)
        sys.exit(0)

    for addr, port, type_, arg in server_list:
        if accept_mode:
            acceptor = Acceptor()
            acceptor.bind(addr, port)
            acceptor.listen()
            acceptor.set_on_accepted(server.server_side_on_accepted)
            acceptor.set_on_closed(acceptor_on_closed)
        else:
            via, to = arg
            if type_ == 'tcp':
                acceptor = Acceptor()
                acceptor.bind(addr, port)
                acceptor.listen()
                acceptor.set_on_accepted(tcptun.gen_on_client_accepted(via, to))
                acceptor.set_on_closed(acceptor_on_closed)
            elif type_ == 'udp':
                receiver = Dgram()
                receiver.bind(addr, port)
                receiver.set_on_received(udptun.gen_on_received(via, to))
                receiver.set_on_closed(acceptor_on_closed)
                receiver.begin_receiving()
            elif type_ == 'tun':
                # port as cidr prefix
                receiver = TunDevice('tun', addr, port)
                receiver.set_on_received(tuntun.gen_on_received(via, to))
                receiver.set_on_closed(acceptor_on_closed)
                receiver.begin_receiving()

                def ping_thread():
                    subprocess.Popen(['ping', '-c', '1', to[0]]).wait()

                t = threading.Thread(target=ping_thread)
                t.start()

    event.Event.process_loop()
