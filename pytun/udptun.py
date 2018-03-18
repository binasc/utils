import json
import common
from stream import Stream
from dgram import Dgram

import logging
import loglevel
_logger = loglevel.getLogger('udptun', logging.INFO)

BUFFERSIZE = 4 * (1024 ** 2)

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
            tunnel = Stream()
            common.initializeTunnel(tunnel)
            addr2Stream[addrPort] = tunnel

            _logger.info('new Dgram from: %s:%d (%s to %s)' % (from_[0], from_[1], str(front), str(tunnel)))

            def tunnelSent(self, sent, remain):
                _logger.debug("%s:%d --> %s:%d %d bytes" % (
                             from_[0], from_[1], to[0], int(to[1]), sent))

            def tunnelReceived(self, data, _):
                _logger.debug("%s:%d <-- %s:%d %d bytes" % (
                             from_[0], from_[1], to[0], int(to[1]), len(data)))
                front.send(data, from_)

            def tunnelClosed(self):
                del addr2Stream[addrPort]

            tunnel.setOnSent(tunnelSent)
            tunnel.setOnReceived(tunnelReceived)
            tunnel.setOnClosed(tunnelClosed)

            tunnel.connect(via[0], via[1])
            tunnel.send(header)

        tunnel.send(data)

    return onReceived

def acceptSideReceiver(tunnel, header):
    addr = header['addr']
    port = header['port']

    back = Dgram()

    _logger.info('new Dgram from: %s:%d (%s to %s)', addr, port, str(tunnel), str(back))

    def udpTunnelSent(self, sent, remain):
        if remain <= BUFFERSIZE:
            back.beginReceiving()

    def udpTunnelReceived(self, data, _):
        back.send(data, (addr, port))

    def udpTunnelClosed(self):
        back.close()

    def udpBackendReceived(self, data, _):
        tunnel.send(data)
        pending = tunnel.pendingBytes()
        if pending > BUFFERSIZE:
            self.stopReceiving()

    def udpBackendClosed(self):
        tunnel.close()

    tunnel.setOnSent(udpTunnelSent)
    tunnel.setOnReceived(udpTunnelReceived)
    tunnel.setOnClosed(udpTunnelClosed)
    back.setOnReceived(udpBackendReceived)
    back.setOnClosed(udpBackendClosed)
    back.beginReceiving()
    # 10 min
    back.setTimeout(10 * 60 * 1000)

