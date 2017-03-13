import json
import common
from stream import Stream

import logging
import loglevel
_logger = loglevel.getLogger('tcptun', logging.INFO)

BUFFERSIZE = 512 * 1024

def genOnAccepted(via, to):

    header = common.wrapContent(json.dumps({
        'type': 'tcp',
        'addr': to[0],
        'port': to[1]
    }))

    def onAccepted(front, from_):
        _logger.debug('onAccepted')

        _logger.info("from %s:%d connected" % from_)

        tunnel = Stream()
        common.initializeTunnel(tunnel)
        tunnel.connect(via[0], via[1])
        tunnel.send(header)

        def tunnelSent(self, sent, remain):
            _logger.info("%s:%d --> %s:%d %d bytes" % (
                         from_[0], from_[1], to[0], int(to[1]), sent))

        def tunnelReceived(self, data, _):
            _logger.info("%s:%d <-- %s:%d %d bytes" % (
                         from_[0], from_[1], to[0], int(to[1]), len(data)))
            front.send(data)

        def tunnelClosed(self):
            _logger.info("%s:%d <-> %s:%d closed" % (
                         from_[0], from_[1], to[0], int(to[1])))
            front.close()

        def frontendReceived(self, data, _):
            tunnel.send(data)

        def frontendClosed(self):
            tunnel.close()

        tunnel.setOnSent(tunnelSent)
        tunnel.setOnReceived(tunnelReceived)
        tunnel.setOnClosed(tunnelClosed)
        try:
            front.setCongAlgorithm('hybla')
        except Exception as ex:
            _logger.warning('setCongAlgorithm failed: %s' % str(ex))
        front.setOnReceived(frontendReceived)
        front.setOnClosed(frontendClosed)
        front.beginReceiving()

    return onAccepted 

def acceptSideReceiver(tunnel, header):
    addr = header['addr']
    port = header['port']

    _logger.debug('connect to: %s:%d', addr, port)
    back = Stream()
    back.connect(addr, port)

    def tunnelSent(self, sent, remain):
        if remain <= BUFFERSIZE:
            back.beginReceiving()

    def tunnelReceived(self, data, _):
        back.send(data)

    def tunnelClosed(self):
        back.close()

    def backendReceived(self, data, _):
        tunnel.send(data)
        pending = tunnel.pendingBytes()
        if pending > BUFFERSIZE:
            self.stopReceiving()

    def backendClosed(self):
        tunnel.close()

    tunnel.setOnSent(tunnelSent)
    tunnel.setOnReceived(tunnelReceived)
    tunnel.setOnClosed(tunnelClosed)
    try:
        back.setCongAlgorithm('hybla')
    except Exception as ex:
        _logger.warning('setCongAlgorithm failed: %s' % str(ex))
    back.setOnReceived(backendReceived)
    back.setOnClosed(backendClosed)

