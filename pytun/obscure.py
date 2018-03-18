import base64
import struct
import random
from Crypto.Cipher import AES, XOR
from Crypto import Random

random.seed()

import loglevel
_logger = loglevel.getLogger('obscure')
_logger.setLevel(loglevel.gLevel)


def packData(data):
    remain = len(data)
    sent = 0
    out = ''
    while remain > 0:
        tosend = min(remain, 2 ** 16 - 2)
        out += struct.pack('!H', tosend) + data[sent:sent+tosend]
        remain -= tosend
        sent += tosend
    return out

def unpackData(data):
    length = len(data)
    if length < 2:
        return ('', 0)
    size = struct.unpack('!H', data[:2])[0]
    if length < 2 + size:
        return ('', 0)
    return (data[2:2+size], 2 + size)

def randomPadding(data):
    lenDat = len(data)
    lenPad = 0
    if lenDat < 128:
        lenPad = random.randint(5, 64)
    elif lenDat < 512:
        if random.random() < 0.9:
            lenPad = random.randint(5, 32)
    else:
        if random.random() < 0.2:
            lenPad = random.randint(5, 16)

    pad = ''
    if lenPad > 0:
        pad = struct.pack('!HH', random.randrange(0, 100, 2), lenPad - 1) + chr(random.randint(0, 255)) * lenPad

    _logger.debug("encode len(pad): %d, body: %d", lenPad, lenDat)

    if random.random() < 0.5:
        return pad + struct.pack('!HH', random.randrange(1, 100, 2), lenDat - 1) + data
    else:
        return struct.pack('!HH', random.randrange(1, 100, 2), lenDat - 1) + data + pad

def unpadRandom(data):
    real = ''
    lenDat = len(data)
    if lenDat < 4:
        return ('', 0)
    flag, length = struct.unpack('!HH', data[:4])
    length += 1
    if flag % 2 == 1:
        if lenDat >= 4 + length:
            real = data[4 : 4 + length]
            _logger.debug("body: %d", length)
    if lenDat < 4 + length:
        return ('', 0)
    else:
        return (real, 4 + length)

BS = AES.block_size
pad = lambda s: s + (BS - len(s) % BS) * chr(BS - len(s) % BS) 
unpad = lambda s : s[:-ord(s[len(s)-1:])]

def genAesEncrypt():
    init = [False]
    iv = Random.new().read(AES.block_size)
    key = Random.new().read(BS)
    cipher = AES.new(key, AES.MODE_CBC, iv)

    def aesEncrypt(raw):
        if init[0] is False:
            init[0] = True
            return iv + key + cipher.encrypt(pad(raw))
        else:
            return cipher.encrypt(pad(raw))

    return aesEncrypt

def genAesDecrypt():
    cipher = [None]

    def aesDecrypt(raw):
        if cipher[0] is None:
            if len(raw) < AES.block_size + BS:
                return ('', 0)
            iv = raw[: AES.block_size]
            key = raw[AES.block_size: AES.block_size + BS]
            cipher[0] = AES.new(key, AES.MODE_CBC, iv)
            return ('', AES.block_size + BS)
        return (unpad(cipher[0].decrypt(raw)), len(raw))

    return aesDecrypt

def genXorEncrypt():
    init = [False]
    key = Random.new().read(BS)
    cipher = XOR.new(key)

    def xorEncrypt(raw):
        if init[0] is False:
            init[0] = True
            return key + cipher.encrypt(raw)
        return cipher.encrypt(raw)

    return xorEncrypt

def genXorDecrypt():
    cipher = [None]

    def xorDecrypt(raw):
        if cipher[0] is None:
            if len(raw) < BS:
                return ('', 0)
            cipher[0] = XOR.new(raw[:BS])
            return ('', BS)
        return (cipher[0].decrypt(raw), len(raw))

    return xorDecrypt

def base64encode(data):
    return base64.b64encode(data)

def base64deocde(data):
    def myceil(v, d):
        return v / d + (0 if v % d == 0 else 1)

    decoded = base64.b64decode(data[0:len(data) / 4 * 4])
    return (decoded, myceil(len(decoded), 3) * 4)

http_request = (
    'POST /upload HTTP/1.1\r\n'
    'Host: cdn.binasc.com\r\n'
    'User-Agent: curl/7.54.0\r\n'
    'Accept: */*\r\n'
    'Cache-Control: no-cache\r\n'
    'Connection: keep-alive\r\n'
    'Content-Type: image/x-ms-bmp\r\n'
    'Content-Length: '
)

http_response = (
    'HTTP/1.1 200 OK\r\n'
    'Server: nginx/1.10.3 (Ubuntu)\r\n'
    'Connection: keep-alive\r\n'
    'Content-Type: image/x-ms-bmp\r\n'
    'Content-Length: '
)

BM_HEADER_SIZE = 54

def sizeOfRawBMP(width, height):
    return (24 * width + 31) / 32 * 4 * height

def genBMPHeader(width, height):
    size = sizeOfRawBMP(width, height)
    return struct.pack('<HIIIIIIHHIIIIII', 0x4d42, BM_HEADER_SIZE + size, 0, BM_HEADER_SIZE, 40, width, height, 1, 24, 0, size, 2834, 2834, 0, 0)

def genHttpEncode(request):
    remain = [0]
    def httpEncode(data):
        tosend = ''
        if len(data) < remain[0]:
            remain[0] -= len(data)
            return data

        if remain[0] > 0:
            tosend = data[0:remain[0]]
            data = data[remain[0]:]
            remain[0] = 0

        while len(data) > 0:
            width = random.randint(500, 1000)
            height = random.randint(500, 1000)
            remain[0] = sizeOfRawBMP(width, height)
            http = http_request if request else http_response
            tosend += http + str(BM_HEADER_SIZE + remain[0]) + '\r\n\r\n' + genBMPHeader(width, height)
            if len(data) < remain[0]:
                tosend += data
                remain[0] -= len(data)
                data = ''
            else:
                tosend += data[0:remain[0]]
                data = data[remain[0]:]
                remain[0] = 0
        return tosend
    return httpEncode

def genHttpDecode(request):
    def getLine(data):
        pos = data.find('\r\n')
        if pos == -1:
            if len(data) > 256:
                raise Exception('http header too long')
            return (None, data)
        return (data[0:pos], data[pos+2:])

    remain = [0]
    def consumeData(data):
        length = remain[0]
        if length < len(data):
            remain[0] = 0
            return (data[0:length], length)
        else:
            remain[0] -= len(data)
            return (data, len(data))

    def httpDecode(data):
        if remain[0] > 0:
            return consumeData(data)

        firstline = True
        httplength = 0
        contentlength = 0
        while True:
            line, data = getLine(data)
            if line == None:
                return (None, 0)
            httplength += len(line) + len('\r\n')
            if httplength > 8192:
                raise Exception('http headers too large')
            if firstline:
                if not request and line != 'POST /upload HTTP/1.1':
                    raise Exception('unknown first line: ' + line.strip()[0:32])
                firstline = False
                continue
            if len(line) > 0:
                header, content = line.split(':')
                if header.strip().lower() == 'content-length':
                    contentlength = int(content.strip())
                elif not request and header.strip().lower() == 'host':
                    if content.strip() != 'cdn.binasc.com':
                        raise Exception('unknown host: ' + content.strip()[0:32])
            else:
                if len(data) > BM_HEADER_SIZE:
                    data = data[BM_HEADER_SIZE:]
                else:
                    return (None, 0)
                remain[0] = contentlength - BM_HEADER_SIZE
                data, payloadlength = consumeData(data)
                return (data, httplength + BM_HEADER_SIZE + payloadlength)
    return httpDecode

