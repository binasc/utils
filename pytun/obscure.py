import base64

key = bytearray('\x71\x56\x03\xa9')

def genXorEncode():
    current = [0]
    def xorEncode(data):
        b = bytearray(data)
        for i in range(len(b)):
            b[i] ^= key[current[0]]
            current[0] += 1
            current[0] %= len(key)
        return str(b)
    return xorEncode

def genXorDecode():
    xor = genXorEncode()
    def xorDecode(data):
        return (xor(data), len(data))
    return xorDecode

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
            #remain[0] = 2 * 1024
            remain[0] = 2 * 1024
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

def genHttpDecode():
    def getLine(data):
        pos = data.find('\r\n')
        if pos == -1:
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
            if firstline:
                firstline = False
                continue
            if len(line) > 0:
                header, content = line.split(':')
                if header.strip().lower() == 'content-length':
                    contentlength = int(content.strip())
            else:
                remain[0] = contentlength
                data, payloadlength = consumeData(data)
                return (data, httplength + payloadlength)
    return httpDecode










