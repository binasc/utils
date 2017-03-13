import json
import common
from event import Event
from stream import Stream
from tundevice import TunDevice
import uuid

import loglevel
_logger = loglevel.getLogger('tuntun')

BUFFERSIZE =  8 * (1024 ** 2)

MAX_CONNECTION = 100

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

    def connectSideMultiplex(tunDevice, data, proto, src, dst):
        srcAddr, srcPort = src 
        addrPort = srcAddr + ':' + str(srcPort)
        sid = hash(addrPort) % MAX_CONNECTION

        tunnel = None
        try:
            tunnel = tunDevice.src2Stream[sid]
        except:
            tunDevice.src2Stream = [None] * MAX_CONNECTION

        if tunnel is None:
            tunnel = Stream()
            tunnel.connect(viaAddr, viaPort)
            common.initializeTunnel(tunnel)
            tunDevice.src2Stream[sid] = tunnel
            _logger.debug('new TunCon from: %s:%d', srcAddr, srcPort)

            tunnel.send(generateHeader(src))

            if srcPort == 0:
                tunnel.setTimeout(60 * 1000)
            else:
                tunnel.setTimeout(10 * 60 * 1000)

            def tunnelReceived(self, data, _):
                tunDevice.send(data)

            def reconnectHandler(ev):
                if tunDevice.src2Stream[sid] is not None:
                    return
                tunnel = Stream()
                tunnel.connect(viaAddr, viaPort)
                common.initializeTunnel(tunnel)
                tunDevice.src2Stream[sid] = tunnel
                _logger.debug('reconnect TunCon from: %s:%d', srcAddr, srcPort)
                tunnel.send(generateHeader(src))
                tunnel.setTimeout(60 * 1000)
                tunnel.setOnReceived(tunnelReceived)
                tunnel.setOnClosed(tunnelClosed)

            def tunnelClosed(self):
                tunDevice.src2Stream[sid] = None
                if srcPort == 0:
                    reconnectEv = Event.addTimer(500)
                    reconnectEv.setHandler(reconnectHandler)

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

    def tunDeviceReceived(self, data, proto, src, dst):
        dstAddr, dstPort = dst
        dstSid  = hash(dstAddr + ':' + str(dstPort)) % MAX_CONNECTION
        #if proto == 'tcp':
        #    data = data * 2
        if dstSid in self.dst2Stream:
            tunnel = self.dst2Stream[dstSid]
        else:
            dstSid  = hash(dstAddr + ':0') % MAX_CONNECTION
            if dstSid in self.dst2Stream:
                tunnel = self.dst2Stream[dstSid]
            else:
                _logger.warning('unknown dst %s:%d', dstAddr, dstPort)
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
        # port as cidr prefix
        tunDevice = TunDevice('tun', addr, port)
        tunDevice.pending = 0
        tunDevice.dst2Stream = {}
        tunDevice.setOnReceived(tunDeviceReceived)
        tunDevice.beginReceiving()
        to2tun[addrPort] = tunDevice

    srcSid = hash(srcAddr + ':' + str(srcPort)) % MAX_CONNECTION
    try:
        _ = tunnel.uuid
    except:
        tunnel.uuid = str(uuid.uuid4())
    if srcSid in tunDevice.dst2Stream:
        if tunDevice.dst2Stream[srcSid].uuid != tunnel.uuid:
            tunDevice.dst2Stream[srcSid].close()
    tunDevice.dst2Stream[srcSid] = tunnel

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

        if srcSid in tunDevice.dst2Stream:
            if tunDevice.dst2Stream[srcSid].uuid == self.uuid:
                del tunDevice.dst2Stream[srcSid]

    tunnel.setOnSent(tunDeviceTunnelSent)
    tunnel.setOnReceived(tunDeviceTunnelReceived)
    tunnel.setOnClosed(tunDeviceTunnelClosed)

