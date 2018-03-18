import json

import common
import tcptun
import udptun
import tuntun


UNKNOWN_CONN_ADDR = "127.0.0.1"
UNKNOWN_CONN_PORT = 8080

def serverSideUnknownConnection(tunnel, recv):
    # TODO: pay attention to BUFFER SIZE
    tunnel._encoders = []
    tcptun.onServerSideReceivedUnknownConnection(tunnel,
                                                 UNKNOWN_CONN_ADDR, UNKNOWN_CONN_PORT, recv)


def serverSideFirstTimeReceived(tunnel, data, _):
    jsonStr, data = common.unwrapContent(data)
    header = json.loads(jsonStr)

    protocol = 'unknown'
    if 'p' in header:
        protocol = header['p']

    # will hand over receive callback of tunnel
    if protocol == 'tcp':
        tcptun.onServerSideConnected(tunnel, header['addr'], header['port'])
        return

    if header['type'] == 'udp':
        udptun.acceptSideReceiver(tunnel, header)
    elif header['type'] == 'tun':
        tuntun.acceptSideReceiver(tunnel, header)
    #elif header['type'] == 'hb':
    #    _logger.error('RECEIVE HB')
    #    tunnel.refreshTimeout()

def serverSideOnAccepted(tunnel, _):
    common.initializeTunnel(tunnel, isRequest=False)

    tunnel.setOnReceived(serverSideFirstTimeReceived)
    tunnel.setOnDecodeError(serverSideUnknownConnection)
    tunnel.beginReceiving()

