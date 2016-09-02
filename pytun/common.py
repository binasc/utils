import struct
import obscure
import logging

import loglevel
_logger = logging.getLogger('common')
_logger.setLevel(loglevel.gLevel)

# 1MB
BUFFSIZE = 1024 ** 2

def initializeTunnel(tunnel, isRequest=True):
    tunnel.setBufferSize(BUFFSIZE)
    try:
        tunnel.setCongAlgorithm('hybla')
    except Exception as ex:
        _logger.warning('setCongAlgorithm failed: %s' % str(ex))
    tunnel.appendSendHandler(obscure.packData)
    tunnel.appendSendHandler(obscure.genXorEncode())
    tunnel.appendSendHandler(obscure.base64encode)
    tunnel.appendSendHandler(obscure.genHttpEncode(isRequest))
    tunnel.appendReceiveHandler(obscure.genHttpDecode())
    tunnel.appendReceiveHandler(obscure.base64deocde)
    tunnel.appendReceiveHandler(obscure.genXorDecode())
    tunnel.appendReceiveHandler(obscure.unpackData)

def wrapContent(content):
    return struct.pack('!I', len(content)) + content

def unwrapContent(data):
    if len(data) < 4:
        return (None, 0)

    length = struct.unpack('!I', data[:4])[0]
    if len(data) - 4 < length:
        return (None, 0)

    return (data[4:length+4], length+4)
