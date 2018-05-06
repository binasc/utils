import struct
import obscure

import loglevel
_logger = loglevel.get_logger('common')

# 1MB
BUFF_SIZE = 1024 ** 2


def initialize_tunnel(tunnel, is_request=True):
    tunnel.set_buffer_size(BUFF_SIZE)
    tunnel.set_tcp_no_delay()
    try:
        tunnel.set_cong_algorithm('hybla')
    except Exception as ex:
        _logger.warning('set_cong_algorithm failed: %s' % str(ex))
    tunnel.append_send_handler(obscure.pack_data)
    tunnel.append_send_handler(obscure.random_padding)
    # tunnel.append_send_handler(obscure.gen_aes_encrypt())
    tunnel.append_send_handler(obscure.gen_xor_encrypt())
    # tunnel.append_send_handler(obscure.base64_encode)
    tunnel.append_send_handler(obscure.gen_http_encode(is_request))
    tunnel.append_receive_handler(obscure.gen_http_decode(is_request))
    # tunnel.append_receive_handler(obscure.base64_decode)
    tunnel.append_receive_handler(obscure.gen_xor_decrypt())
    # tunnel.append_receive_handler(obscure.gen_aes_decrypt())
    tunnel.append_receive_handler(obscure.unpad_random)
    tunnel.append_receive_handler(obscure.unpack_data)


def wrap_content(json, data=''):
    _logger.debug('header: %s, len(data): %d', json, len(data))
    return struct.pack('!HI', len(json), len(data)) + json + data


def unwrap_content(data):
    if len(data) < 6:
        raise Exception('corrupted data')

    json_length, data_length = struct.unpack('!HI', data[: 6])
    total_length = data_length + json_length
    if len(data) - 6 != total_length:
        raise Exception('corrupted data')

    json = data[6: 6 + json_length]
    data = data[6 + json_length: 6 + total_length]
    _logger.debug('header: %s, len(data): %d', json, len(data))
    return json, data
