import base64
import struct
import random
from Crypto.Cipher import AES, XOR
from Crypto import Random
import loglevel

_logger = loglevel.get_logger('obscure')
_logger.setLevel(loglevel.DEFAULT_LEVEL)

random.seed()


def pack_data(data):
    remain = len(data)
    sent = 0
    out = ''
    while remain > 0:
        to_send = min(remain, 2 ** 16 - 2)
        out += struct.pack('!H', to_send) + data[sent: sent + to_send]
        remain -= to_send
        sent += to_send
    return out


def unpack_data(data):
    length = len(data)
    if length < 2:
        return '', 0
    size = struct.unpack('!H', data[: 2])[0]
    if length < 2 + size:
        return '', 0
    return data[2: 2 + size], 2 + size


def random_padding(data):
    data_length = len(data)
    pad_length = 0
    if data_length < 128:
        pad_length = random.randint(5, 64)
    elif data_length < 512:
        if random.random() < 0.9:
            pad_length = random.randint(5, 32)
    else:
        if random.random() < 0.2:
            pad_length = random.randint(5, 16)

    pad = ''
    if pad_length > 0:
        pad = struct.pack('!HH', random.randrange(0, 100, 2), pad_length - 1) + chr(random.randint(0, 255)) * pad_length

    _logger.debug("encode len(pad): %d, body: %d", pad_length, data_length)

    if random.random() < 0.5:
        return pad + struct.pack('!HH', random.randrange(1, 100, 2), data_length - 1) + data
    else:
        return struct.pack('!HH', random.randrange(1, 100, 2), data_length - 1) + data + pad


def unpad_random(data):
    real = ''
    data_length = len(data)
    if data_length < 4:
        return '', 0
    flag, length = struct.unpack('!HH', data[:4])
    length += 1
    if flag % 2 == 1:
        if data_length >= 4 + length:
            real = data[4: 4 + length]
            _logger.debug("body: %d", length)
    if data_length < 4 + length:
        return '', 0
    else:
        return real, 4 + length


def pad_data(data):
    data_length = len(data)
    pad_length = (AES.block_size - data_length % AES.block_size)
    return data + pad_length * chr(pad_length)


def gen_aes_encrypt():
    init = [False]
    iv = Random.new().read(AES.block_size)
    key = Random.new().read(AES.block_size)
    cipher = AES.new(key, AES.MODE_CBC, iv)

    def aes_encrypt(raw):
        if init[0] is False:
            init[0] = True
            return iv + key + cipher.encrypt(pad_data(raw))
        else:
            return cipher.encrypt(pad_data(raw))

    return aes_encrypt


def unpad_data(data):
    return data[: -ord(data[len(data)-1:])]


def gen_aes_decrypt():
    cipher = [None]

    def aes_decrypt(raw):
        if cipher[0] is None:
            if len(raw) < AES.block_size + AES.block_size:
                return '', 0
            iv = raw[: AES.block_size]
            key = raw[AES.block_size: AES.block_size + AES.block_size]
            cipher[0] = AES.new(key, AES.MODE_CBC, iv)
            return '', AES.block_size + AES.block_size
        return unpad_data(cipher[0].decrypt(raw)), len(raw)

    return aes_decrypt


def gen_xor_encrypt():
    init = [False]
    key = Random.new().read(AES.block_size)
    cipher = XOR.new(key)

    def xor_encrypt(raw):
        if init[0] is False:
            init[0] = True
            return key + cipher.encrypt(raw)
        return cipher.encrypt(raw)

    return xor_encrypt


def gen_xor_decrypt():
    cipher = [None]

    def xor_decrypt(raw):
        if cipher[0] is None:
            if len(raw) < AES.block_size:
                return '', 0
            cipher[0] = XOR.new(raw[: AES.block_size])
            return '', AES.block_size
        return cipher[0].decrypt(raw), len(raw)

    return xor_decrypt


def base64_encode(data):
    return base64.b64encode(data)


def base64_decode(data):
    def ceil(v, d):
        return v / d + (0 if v % d == 0 else 1)

    decoded = base64.b64decode(data[0:len(data) / 4 * 4])
    return decoded, ceil(len(decoded), 3) * 4


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


def size_of_raw_bmp(width, height):
    return (24 * width + 31) / 32 * 4 * height


def gen_bmp_header(width, height):
    size = size_of_raw_bmp(width, height)
    return struct.pack('<HIIIIIIHHIIIIII', 0x4d42,
                       BM_HEADER_SIZE + size, 0, BM_HEADER_SIZE, 40,
                       width, height, 1, 24, 0, size, 2834, 2834, 0, 0)


def gen_http_encode(request):
    remain = [0]

    def http_encode(data):
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
            remain[0] = size_of_raw_bmp(width, height)
            http = http_request if request else http_response
            tosend += http + str(BM_HEADER_SIZE + remain[0]) + '\r\n\r\n' + gen_bmp_header(width, height)
            if len(data) < remain[0]:
                tosend += data
                remain[0] -= len(data)
                data = ''
            else:
                tosend += data[0: remain[0]]
                data = data[remain[0]:]
                remain[0] = 0
        return tosend
    return http_encode


def gen_http_decode(request):
    remain = [0]

    def get_line(data):
        pos = data.find('\r\n')
        if pos == -1:
            if len(data) > 256:
                raise Exception('http header too long')
            return None, data
        return data[0: pos], data[pos+2:]

    def consume_data(data):
        length = remain[0]
        if length < len(data):
            remain[0] = 0
            return data[0:length], length
        else:
            remain[0] -= len(data)
            return data, len(data)

    def http_decode(data):
        if remain[0] > 0:
            return consume_data(data)

        first_line = True
        http_length = 0
        content_length = 0
        while True:
            line, data = get_line(data)
            if line is None:
                return None, 0
            http_length += len(line) + len('\r\n')
            if http_length > 8192:
                raise Exception('http headers too large')
            if first_line:
                if not request and line != 'POST /upload HTTP/1.1':
                    raise Exception('unknown first line: ' + line.strip()[0:32])
                first_line = False
                continue
            if len(line) > 0:
                header, content = line.split(':')
                if header.strip().lower() == 'content-length':
                    content_length = int(content.strip())
                elif not request and header.strip().lower() == 'host':
                    if content.strip() != 'cdn.binasc.com':
                        raise Exception('unknown host: ' + content.strip()[0:32])
            else:
                if len(data) > BM_HEADER_SIZE:
                    data = data[BM_HEADER_SIZE:]
                else:
                    return None, 0
                remain[0] = content_length - BM_HEADER_SIZE
                data, payloadlength = consume_data(data)
                return data, http_length + BM_HEADER_SIZE + payloadlength
    return http_decode
