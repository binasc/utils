#!/usr/bin/env python
import sys
import event
import getopt
from acceptor import Acceptor
from dgram import Dgram
from tundevice import TunDevice

import tcptun
import udptun
import tuntun
from tunnel import Tunnel
from delegation import Delegation

import tunnel

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
    protocols = {'tcp', 'udp', 'tun'}
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


def server_side_on_accepted(sock, _):
    tunnel = Tunnel(sock)
    tunnel.set_on_payload(Delegation.on_payload)
    tunnel.set_on_closed(Delegation.on_closed)
    tunnel.set_on_buffer_high(Delegation.set_on_buffer_high)
    tunnel.set_on_buffer_low(Delegation.set_on_buffer_low)
    tunnel.initialize()


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

    Tunnel.set_tcp_closed_handler(tcptun.on_stream_closed)
    Tunnel.set_udp_closed_handler(udptun.on_dgram_closed)
    if accept_mode:
        Tunnel.set_tcp_initial_handler(tcptun.on_server_side_initialized)
        Tunnel.set_udp_initial_handler(udptun.on_server_side_initialized)
        Delegation.set_type(Delegation.ACCEPT)
    else:
        Delegation.set_type(Delegation.CONNECT)

    for addr, port, type_, arg in server_list:
        if accept_mode:
            acceptor = Acceptor()
            acceptor.bind(addr, port)
            acceptor.listen()
            acceptor.set_on_accepted(server_side_on_accepted)
            acceptor.set_on_closed(acceptor_on_closed)
        else:
            via, to = arg
            if type_ == 'tcp':
                acceptor = Acceptor()
                acceptor.bind(addr, port)
                acceptor.listen()
                acceptor.set_on_accepted(tcptun.gen_on_client_side_accepted(via, to))
                acceptor.set_on_closed(acceptor_on_closed)
            elif type_ == 'udp':
                receiver = Dgram()
                receiver.bind(addr, port)
                receiver.set_on_received(udptun.gen_on_client_side_received(via, to))
                receiver.set_on_closed(acceptor_on_closed)
                receiver.on_tunnel_received = udptun.on_tunnel_received
                receiver.on_tunnel_closed = udptun.on_tunnel_closed
                receiver.begin_receiving()
            elif type_ == 'tun':
                # port as cidr prefix
                receiver = TunDevice('tun', addr, port)
                receiver.set_on_received(tuntun.gen_on_client_side_received(receiver, (addr, port), via, to))
                receiver.set_on_closed(acceptor_on_closed)
                receiver.begin_receiving()

    event.Event.process_loop()
