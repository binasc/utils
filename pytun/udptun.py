import json
import common
import logging
from stream import Stream
from dgram import Dgram

import loglevel
_logger = logging.getLogger('Udptun')
_logger.setLevel(loglevel.gLevel)

addr2Stream = {}

def genOnReceivedFrom(via, to):

    header = common.wrapContent(json.dumps({
        'type': 'udp',
        'addr': to[0],
        'port': to[1]
    }))

    def connectSideReceiver(front, data, fr0m):
        addr, port = fr0m
        addrPort = addr + ':' + str(port)

        if addrPort in addr2Stream:
            tunnel = addr2Stream[addrPort]
        else:
            addr, port = via
            tunnel = Stream()
            tunnel.connect(addr, port)
            common.initializeTunnel(tunnel)
            tunnel.send(header)

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

def acceptSideReceiver(tunnel, header):
    addr = header['addr']
    port = header['port']

    back = Dgram()

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
