import json
import common
import logging
from event import Event
from stream import Stream
from tundevice import TunDevice
import uuid

import loglevel
_logger = logging.getLogger('Tuntun')
_logger.setLevel(loglevel.gLevel)

BUFFERSIZE =  8 * (1024 ** 2)

MAX_CONNECTION = 500
src2Stream = [None] * MAX_CONNECTION
dst2stream = [None] * MAX_CONNECTION

to2tun = {}

def genOnReceived(via, to):
    viaAddr, viaPort = via
    toAddr, toPort = to

    def generateHeader(src):
        return common.wrapContent(json.dumps({
            'type': 'tun',
            'addr': to[0],
            'port': to[1],
            'srcAddr': src[0],
            'srcPort': src[1],
        }))

    def connectSideMultiplex(front, data, proto, src, dst):
        srcAddr, srcPort = src 
        addrPort = srcAddr + ':' + str(srcPort)

        sid = hash(addrPort) % MAX_CONNECTION
        if src2Stream[sid] is not None:
            tunnel = src2Stream[sid]
        else:
            tunnel = Stream()
            tunnel.connect(viaAddr, viaPort)
            common.initializeTunnel(tunnel)
            src2Stream[sid] = tunnel
            _logger.debug('new TunCon from: %s:%d', srcAddr, srcPort)

            tunnel.send(generateHeader(src))

            if srcPort == 0:
                tunnel.setTimeout(60 * 1000)
            else:
                tunnel.setTimeout(10 * 60 * 1000)

            def tunnelReceived(self, data, _):
                front.send(data)

            def reconnectHandler(ev):
                tunnel = Stream()
                tunnel.connect(viaAddr, viaPort)
                common.initializeTunnel(tunnel)
                src2Stream[sid] = tunnel
                _logger.debug('reconnect TunCon from: %s:%d', srcAddr, srcPort)
                tunnel.send(generateHeader(src))
                tunnel.setTimeout(60 * 1000)
                tunnel.setOnReceived(tunnelReceived)
                tunnel.setOnClosed(tunnelClosed)

            def tunnelClosed(self):
                if srcPort == 0:
                    reconnectEv = Event.addTimer(500)
                    reconnectEv.setHandler(reconnectHandler)
                else:
                    src2Stream[sid] = None

            tunnel.setOnReceived(tunnelReceived)
            tunnel.setOnClosed(tunnelClosed)

        #if proto == 'tcp':
        #    data = data * 2
        tunnel.send(data)

    return connectSideMultiplex

def acceptSideReceiver(tunnel, header):
    addr = header['addr']
    port = header['port']
    srcAddr = header['srcAddr']
    srcPort = header['srcPort']

    srcSid = hash(srcAddr + ':' + str(srcPort)) % MAX_CONNECTION
    tunnel.uuid = str(uuid.uuid4())
    dst2stream[srcSid] = tunnel

    def tunDeviceReceived(self, data, proto, src, dst):
        dstAddr, dstPort = dst
        dstSid  = hash(dstAddr + ':' + str(dstPort)) % MAX_CONNECTION
        #if proto == 'tcp':
        #    data = data * 2
        if dst2stream[dstSid] is not None:
            tunnel = dst2stream[dstSid]
        else:
            dstSid  = hash(dstAddr + ':0') % MAX_CONNECTION
            if dst2stream[dstSid] is not None:
                tunnel = dst2stream[dstSid]
            else:
                _logger.warning('unknown dst %s:%s', dstAddr, str(dstPort))
                return

        before = tunnel.send(None)
        after = tunnel.send(data)
        self.pending += (after - before)
        if self.pending > BUFFERSIZE:
            self.stopReceiving()

    addrPort = addr + ':' + str(port)
    if addrPort in to2tun:
        tunDevice = to2tun[addrPort]
    else:
        # TODO
        tunDevice = TunDevice('tun', addr, '255.255.255.0')
        tunDevice.pending = 0
        tunDevice.setOnReceived(tunDeviceReceived)
        tunDevice.beginReceiving()
        to2tun[addrPort] = tunDevice

    def tunDeviceTunnelSent(self, sent, remain):
        tunDevice.pending -= sent
        if tunDevice.pending <= BUFFERSIZE:
            tunDevice.beginReceiving()

    def tunDeviceTunnelReceived(self, data, _):
        tunDevice.send(data)

    def tunDeviceTunnelClosed(self):
        left = self.send(None)
        tunDevice.pending -= left
        if tunDevice.pending <= BUFFERSIZE:
            tunDevice.beginReceiving()

        if dst2stream[srcSid] is not None:
            if dst2stream[srcSid].uuid == self.uuid:
                dst2stream[srcSid] = None

    tunnel.setOnSent(tunDeviceTunnelSent)
    tunnel.setOnReceived(tunDeviceTunnelReceived)
    tunnel.setOnClosed(tunDeviceTunnelClosed)

