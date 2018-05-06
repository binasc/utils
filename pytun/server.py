import json

import common
import tcptun
import udptun
import tuntun


UNKNOWN_CONN_ADDR = "127.0.0.1"
UNKNOWN_CONN_PORT = 8080


def server_side_unknown_connection(tunnel, recv):
    # TODO: pay attention to BUFFER SIZE
    tunnel._encoders = []
    tcptun.on_server_side_received_unknown_connection(tunnel,
                                                      UNKNOWN_CONN_ADDR, UNKNOWN_CONN_PORT, recv)


def server_side_first_time_received(tunnel, data, _):
    json_str, data = common.unwrap_content(data)
    header = json.loads(json_str)

    protocol = 'unknown'
    if 'p' in header:
        protocol = header['p']

    # will hand over receive callback of tunnel
    if protocol == 'tcp':
        tcptun.on_server_side_connected(tunnel, header['addr'], header['port'])
        return

    if header['type'] == 'udp':
        udptun.accept_side_receiver(tunnel, header)
    elif header['type'] == 'tun':
        tuntun.accept_side_receiver(tunnel, header)
    # elif header['type'] == 'hb':
    #     _logger.error('RECEIVE HB')
    #     tunnel.refresh_timeout()


def server_side_on_accepted(tunnel, _):
    common.initialize_tunnel(tunnel, is_request=False)

    tunnel.set_on_received(server_side_first_time_received)
    tunnel.set_on_decode_error(server_side_unknown_connection)
    tunnel.begin_receiving()
