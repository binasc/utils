import json
import common
from stream import Stream

import logging
import loglevel
_logger = loglevel.getLogger('tcptun', logging.INFO)

BUFFERSIZE = 512 * 1024

def genOnClientAccepted(via, to):

    connectInfo = common.wrapContent(json.dumps({
        'p': 'tcp',
        'type': 'connect',
        'addr': to[0],
        'port': to[1]
    }))

    hb = common.wrapContent(json.dumps({'type': 'hb'}))

    dataHeader = json.dumps({'type': 'data'})

    def onAccepted(front, from_):
        _logger.debug('onAccepted')

        _logger.info("from %s:%d connected" % from_)

        tunnel = Stream()
        common.initializeTunnel(tunnel)
        tunnel.connect(via[0], via[1])
        tunnel.send(connectInfo)

        def tunnelSent(self, sent, remain):
            _logger.info("%s:%d --> %s:%d %d bytes" % (
                         from_[0], from_[1], to[0], int(to[1]), sent))

        def tunnelReceived(self, data, _):
            _logger.info("%s:%d <-- %s:%d %d bytes" % (
                         from_[0], from_[1], to[0], int(to[1]), len(data)))
            front.send(data)

        def tunnelSendHeartbeat(self):
            self.send(hb)
            self.addTimer('hb', 15 * 1000, tunnelSendHeartbeat)

        def tunnelClosed(self):
            _logger.info("%s:%d <-> %s:%d closed" % (
                         from_[0], from_[1], to[0], int(to[1])))
            front.close()

        def frontendReceived(self, data, _):
            tunnel.send(common.wrapContent(dataHeader, data))

        def frontendClosed(self):
            tunnel.close()

        tunnel.setOnSent(tunnelSent)
        tunnel.setOnReceived(tunnelReceived)
        tunnel.setOnClosed(tunnelClosed)
        tunnel.addTimer('hb', 15 * 1000, tunnelSendHeartbeat)

        front.setOnReceived(frontendReceived)
        front.setOnClosed(frontendClosed)
        front.beginReceiving()

    return onAccepted 

def onServerSideConnected(tunnel, addr, port):
    _logger.debug('connect to: %s:%d', addr, port)
    back = Stream()
    back.connect(addr, port)

    def tunnelSent(self, sent, remain):
        if remain <= BUFFERSIZE:
            back.beginReceiving()

    def tunnelReceived(self, data, _):
        jsonStr, data = common.unwrapContent(data)
        header = json.loads(jsonStr)

        type_ = header['type']
        if type_ == 'data':
            back.send(data)
        elif type_ == 'hb':
            _logger.debug('Received heartbeat packet')
        else:
            _logger.warning('Unknown packet type: %s', type_)

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
    back.setOnReceived(backendReceived)
    back.setOnClosed(backendClosed)

def onServerSideReceivedUnknownConnection(tunnel, addr, port, recv):
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
    back.setOnReceived(backendReceived)
    back.setOnClosed(backendClosed)
    if recv is not None:
        back.send(recv)

