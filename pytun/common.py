import struct
import obscure

import loglevel
_logger = loglevel.getLogger('common')

# 1MB
BUFFSIZE = 1024 ** 2

def initializeTunnel(tunnel, isRequest=True):
    tunnel.setBufferSize(BUFFSIZE)
    tunnel.setTCPNoDelay()
    try:
        tunnel.setCongAlgorithm('hybla')
    except Exception as ex:
        _logger.warning('setCongAlgorithm failed: %s' % str(ex))
    tunnel.appendSendHandler(obscure.packData)
    #tunnel.appendSendHandler(obscure.genAesEncrypt())
    tunnel.appendSendHandler(obscure.genXorEncrypt())
    tunnel.appendSendHandler(obscure.base64encode)
    tunnel.appendSendHandler(obscure.genHttpEncode(isRequest))
    tunnel.appendReceiveHandler(obscure.genHttpDecode(isRequest))
    tunnel.appendReceiveHandler(obscure.base64deocde)
    tunnel.appendReceiveHandler(obscure.genXorDecrypt())
    #tunnel.appendReceiveHandler(obscure.genAesDecrypt())
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
