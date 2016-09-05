import json
import common
import logging
from stream import Stream
from dgram import Dgram

import loglevel
_logger = logging.getLogger('Udptun')
_logger.setLevel(loglevel.gLevel)

addr2Stream = {}

def genOnReceived(via, to):

    header = common.wrapContent(json.dumps({
        'type': 'udp',
        'addr': to[0],
        'port': to[1]
    }))

    def onReceived(front, data, from_):
        addrPort = '%s:%d' % from_

        if addrPort in addr2Stream:
            tunnel = addr2Stream[addrPort]
        else:
            _logger.debug('new Dgram from: %s:%d' % from_)

            tunnel = Stream()
            common.initializeTunnel(tunnel)
            tunnel.connect(via[0], via[1])
            tunnel.send(header)

            addr2Stream[addrPort] = tunnel

            def tunnelReceived(self, data, _):
                front.send(data, from_)

            def tunnelClosed(self):
                del addr2Stream[addrPort]

            tunnel.setOnReceived(tunnelReceived)
            tunnel.setOnClosed(tunnelClosed)

        tunnel.send(data)

    return onReceived

def acceptSideReceiver(tunnel, header):
    addr = header['addr']
    port = header['port']

    back = Dgram()

    def udpTunnelReceived(self, data, _):
        back.send(data, (addr, port))

    def udpTunnelClosed(self):
        back.close()

    def udpBackendReceived(self, data, _):
        tunnel.send(data)

    def udpBackendClosed(self):
        tunnel.close()

    tunnel.setOnReceived(udpTunnelReceived)
    tunnel.setOnClosed(udpTunnelClosed)
    back.setOnReceived(udpBackendReceived)
    back.setOnClosed(udpBackendClosed)
    back.beginReceiving()
    # 10 min
    back.setTimeout(10 * 60 * 1000)

