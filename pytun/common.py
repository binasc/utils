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
    tunnel.appendSendHandler(obscure.randomPadding)
    #tunnel.appendSendHandler(obscure.genAesEncrypt())
    tunnel.appendSendHandler(obscure.genXorEncrypt())
    #tunnel.appendSendHandler(obscure.base64encode)
    tunnel.appendSendHandler(obscure.genHttpEncode(isRequest))
    tunnel.appendReceiveHandler(obscure.genHttpDecode(isRequest))
    #tunnel.appendReceiveHandler(obscure.base64deocde)
    tunnel.appendReceiveHandler(obscure.genXorDecrypt())
    #tunnel.appendReceiveHandler(obscure.genAesDecrypt())
    tunnel.appendReceiveHandler(obscure.unpadRandom)
    tunnel.appendReceiveHandler(obscure.unpackData)

def wrapContent(json, data=''):
    _logger.debug('header: %s, len(data): %d', json, len(data))
    return struct.pack('!HI', len(json), len(data)) + json + data

def unwrapContent(data):
    if len(data) < 6:
        raise Exception('corrupted data')

    jsonLength, dataLength = struct.unpack('!HI', data[:6])
    totalLength = dataLength + jsonLength
    if len(data) - 6 != totalLength:
        raise Exception('corrupted data')

    json = data[6 : 6 + jsonLength]
    data = data[6 + jsonLength : 6 + totalLength]
    _logger.debug('header: %s, len(data): %d', json, len(data))
    return (json, data)
