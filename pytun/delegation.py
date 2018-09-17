from tunnel import Tunnel

import loglevel
_logger = loglevel.get_logger('delegation')


TUNNEL_SIZE = 32


class Delegation(object):

    CONNECT = 0

    ACCEPT = 1

    _type = None

    _id_2_tunnel = {}

    _id_2_endpoint = {}

    @staticmethod
    def on_payload(_, id_, data_):
        endpoint = Delegation.query_endpoint(id_)
        if endpoint is None:
            _logger.error('no endpoint for id: %s', id_)
        else:
            endpoint.on_tunnel_received(endpoint, id_, data_)

    @staticmethod
    def on_closed(tunnel):
        id_ = None
        if hasattr(tunnel, 'nonblockings'):
            for kv in tunnel.nonblockings.items():
                id_ = kv[0]
                nb = kv[1]
                if hasattr(nb, 'on_tunnel_closed'):
                    try:
                        nb.on_tunnel_closed(nb)
                    except:
                        continue
        if id_ is not None:
            key = Delegation.get_key(id_)
            if key in Delegation._id_2_tunnel:
                del Delegation._id_2_tunnel[key]

    @staticmethod
    def set_on_buffer_high(tunnel):
        if hasattr(tunnel, 'nonblockings'):
            for _, nonblocking in tunnel.nonblockings.items():
                nonblocking.stop_receiving()

    @staticmethod
    def set_on_buffer_low(tunnel):
        if hasattr(tunnel, 'nonblockings'):
            for _, nonblocking in tunnel.nonblockings.items():
                nonblocking.begin_receiving()

    @classmethod
    def set_type(cls, type):
        cls._type = type

    @classmethod
    def get_key(cls, id_):
        if cls._type == cls.CONNECT:
            return int(id_) % TUNNEL_SIZE
        elif cls._type == cls.ACCEPT:
            return id_
        else:
            raise Exception('Bad type')

    @classmethod
    def get_tunnel(cls, id_):
        key = cls.get_key(id_)
        if key in cls._id_2_tunnel:
            return cls._id_2_tunnel[key]
        return None

    @classmethod
    def register(cls, id_, tunnel, endpoint):
        if not hasattr(tunnel, 'nonblockings'):
            tunnel.nonblockings = {}
        assert(id_ not in tunnel.nonblockings)
        tunnel.nonblockings[id_] = endpoint
        key = cls.get_key(id_)
        if key not in cls._id_2_tunnel:
            cls._id_2_tunnel[key] = tunnel
        assert(tunnel == cls._id_2_tunnel[key])
        if id_ not in cls._id_2_endpoint:
            cls._id_2_endpoint[id_] = endpoint
        assert(endpoint == cls._id_2_endpoint[id_])

    @classmethod
    def de_register(cls, id_):
        key = cls.get_key(id_)
        if key in cls._id_2_tunnel:
            tunnel = cls._id_2_tunnel[key]
            if hasattr(tunnel, 'nonblockings') and id_ in tunnel.nonblockings:
                del tunnel.nonblockings[id_]
        if id_ in cls._id_2_endpoint:
            del cls._id_2_endpoint[id_]

    @classmethod
    def query_endpoint(cls, id_):
        key = cls.get_key(id_)
        if key in cls._id_2_tunnel:
            tunnel = cls._id_2_tunnel[key]
            if hasattr(tunnel, 'nonblockings'):
                if id_ in tunnel.nonblockings:
                    return tunnel.nonblockings[id_]
        else:
            if id_ in cls._id_2_endpoint:
                return cls._id_2_endpoint[id_]
        return None
