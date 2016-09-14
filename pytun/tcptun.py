import json
import common
import logging
from stream import Stream

import loglevel
_logger = logging.getLogger('Tcptun')
_logger.setLevel(loglevel.gLevel)

BUFFERSIZE = 512 * 1024

def genOnAccepted(via, to):

    header = common.wrapContent(json.dumps({
        'type': 'tcp',
        'addr': to[0],
        'port': to[1]
    }))

    def onAccepted(front):
        _logger.debug('onAccepted')

        tunnel = Stream()
        common.initializeTunnel(tunnel)
        tunnel.connect(via[0], via[1])
        tunnel.send(header)

        def tunnelReceived(self, data, _):
            front.send(data)

        def tunnelClosed(self):
            front.close()

        def frontendReceived(self, data, _):
            tunnel.send(data)

        def frontendClosed(self):
            tunnel.close()

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
        pending = tunnel.send(data)
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

