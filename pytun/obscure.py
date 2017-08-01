import base64
import struct
from Crypto.Cipher import AES, XOR
from Crypto import Random

key = '\x71\x56\x03\xa9\x71\x56\x03\xa9\x71\x56\x03\xa9\x71\x56\x03\xa9'

def packData(data):
    remain = len(data)
    sent = 0
    out = ''
    while remain > 0:
        tosend = min(remain, 65536)
        out += struct.pack('!H', tosend - 1) + data[sent:sent+tosend]
        remain -= tosend
        sent += tosend
        
    return out

def unpackData(data):
    if len(data) < 2:
        return ('', 0)
    size = struct.unpack('!H', data[:2])[0] + 1
    if len(data) < 2 + size:
        return ('', 0)
    return (data[2:2+size], 2 + size)

BS = AES.block_size
pad = lambda s: s + (BS - len(s) % BS) * chr(BS - len(s) % BS) 
unpad = lambda s : s[:-ord(s[len(s)-1:])]

def genAesEncrypt():
    init = [False]
    iv = Random.new().read(AES.block_size)
    cipher = AES.new(key, AES.MODE_CBC, iv)

    def aesEncrypt(raw):
        if init[0] is False:
            init[0] = True
            return iv + cipher.encrypt(pad(raw))
        else:
            return cipher.encrypt(pad(raw))

    return aesEncrypt

def genAesDecrypt():
    cipher = [None]

    def aesDecrypt(raw):
        if cipher[0] is None:
            if len(raw) < AES.block_size:
                return ('', 0)
            iv = raw[:AES.block_size]
            cipher[0] = AES.new(key, AES.MODE_CBC, iv)
            return ('', AES.block_size)
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
    'Host: li.binasc.com\r\n'
    'Connection: keep-alive\r\n'
    'Content-Type: text/plain\r\n'
    'Content-Length: '
)

http_response = (
    'HTTP/1.1 200 OK\r\n'
    'Server: li.binasc.com\r\n'
    'Connection: keep-alive\r\n'
    'Content-Type: text/plain\r\n'
    'Content-Length: '
)

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
            remain[0] = 64 * 1024
            http = http_request if request else http_response
            tosend += http + str(remain[0]) + '\r\n\r\n'
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
                    if content.strip() != 'li.binasc.com':
                        raise Exception('unknown host: ' + content.strip()[0:32])
            else:
                remain[0] = contentlength
                data, payloadlength = consumeData(data)
                return (data, httplength + payloadlength)
    return httpDecode

