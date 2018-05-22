import os
import socket
import errno
from event import Event
from collections import deque
import traceback

import loglevel
_logger = loglevel.get_logger('nonblocking')
_logger.setLevel(loglevel.DEFAULT_LEVEL)


class NonBlocking(object):

    def __init__(self, fd):
        self._fd = fd
        self.set_non_blocking()

        self._to_send_bytes = 0
        self._to_send = deque()

        self._encoders = []
        self._decoders = [(lambda data: (data, len(data)), [''])]

        self._error = False

        self._connected = False

        self._onSent = None
        self._on_received = None
        self._onClosed = None

        self._cev = None

        self._timeout = 0
        self._timeoutEv = None

        self._wev = Event()
        self._wev.set_write(True)
        self._wev.set_fd(self._fd.fileno())
        self._wev.set_handler(lambda ev: self._on_send())

        self._rev = Event()
        self._rev.set_write(False)
        self._rev.set_fd(self._fd.fileno())
        self._rev.set_handler(lambda ev: self._on_receive())

        self._errorType = socket.error

        self._decodeError = False
        self._onDecodeError = None

        self._timers = {}

    def __hash__(self):
        return hash(self._fd.fileno())

    def __eq__(self, other):
        if not isinstance(other, NonBlocking):
            return False
        return self._fd.fileno() == other._fd.fileno()

    def __str__(self):
        return "fd: %d" % (self._fd.fileno())

    def set_non_blocking(self):
        raise NotImplemented

    def set_on_sent(self, on_sent):
        self._onSent = on_sent

    def set_on_received(self, on_received):
        self._on_received = on_received

    def set_on_closed(self, on_closed):
        self._onClosed = on_closed

    def set_on_decode_error(self, on_decode_error):
        self._onDecodeError = on_decode_error

    def append_send_handler(self, handler):
        self._encoders.append(handler)

    def append_receive_handler(self, handler):
        self._decoders.append((handler, ['']))

    def begin_receiving(self):
        _logger.debug('begin_receiving')
        if not self._connected or self._cev is not None:
            return
        if not Event.isEventSet(self._rev):
            Event.addEvent(self._rev)

    def stop_receiving(self):
        _logger.debug('stop_receiving')
        Event.delEvent(self._rev)

    def _send(self, data, addr):
        raise NotImplemented

    def _on_send(self):
        _logger.debug('_on_send')
        sent_bytes = 0
        while len(self._to_send) > 0:
            data, addr = self._to_send.popleft()
            try:
                sent = self._send(data, addr)
                sent_bytes += sent
                if sent < len(data):
                    self._to_send.appendleft((data[sent:], addr))
            except self._errorType as msg:
                self._to_send.appendleft((data, addr))
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    _logger.error('%s, send: %s',
                                  str(self), os.strerror(msg.errno))
                    self._error = True
                    self._close_again()
                break

        self._to_send_bytes -= sent_bytes
        _logger.debug("%s, sent %d bytes, remain %d bytes", str(self), sent_bytes, self._to_send_bytes)
        if sent_bytes > 0 and self._onSent:
            try:
                self._onSent(self, sent_bytes, self._to_send_bytes)
            except Exception as e:
                _logger.error('_onSent: %s', e)
                _logger.exception(traceback.format_exc())
                self._error = True
                self._close_again()

        if len(self._to_send) == 0:
            assert(self._to_send_bytes == 0)
            Event.delEvent(self._wev)
            if self._cev is not None:
                self._close_again()

    def send(self, data, addr=None):
        _logger.debug('send')
        if self._cev is not None:
            return

        if addr is not None:
            _logger.debug('%s, sending %d bytes to %s:%d', str(self), len(data), addr)
        else:
            _logger.debug('%s, sending %d bytes', str(self), len(data))

        try:
            for encoder in self._encoders:
                data = encoder(data)
        except Exception as ex:
            _logger.error('failed to encode %d bytes: %s', len(data), str(ex))
            raise ex

        self._to_send.append((data, addr))
        self._to_send_bytes += len(data)
        self.refresh_timeout()

        if self._connected and not Event.isEventSet(self._wev):
            Event.addEvent(self._wev)

    def pending_bytes(self):
        return self._to_send_bytes

    class _RecvCBException(Exception):
        pass

    def _decode(self, depth, data, addr):
        decoder, remain = self._decoders[depth]
        remain[0] += data

        while len(remain[0]) > 0:
            out, processed_bytes = decoder(remain[0])
            assert(processed_bytes >= 0)
            if processed_bytes == 0:
                break
            if depth == len(self._decoders) - 1:
                try:
                    if out is not None and len(out) > 0:
                        self._on_received(self, out, addr)
                except Exception as e:
                    _logger.error('_decode: %s', e)
                    _logger.exception(traceback.format_exc())
                    self._error = True
                    self._close_again()
                    raise self._RecvCBException(e)
            else:
                self._decode(depth + 1, out, addr)
            remain[0] = remain[0][processed_bytes:]

    def _recv(self, size):
        raise NotImplemented

    def _on_receive(self):
        _logger.debug('_on_receive')
        while True:
            try:
                recv, addr = self._recv(2 ** 16 - 512)
                if len(recv) == 0:
                    self.close()
                    return
                _logger.debug("%s, received %d bytes", str(self), len(recv))
                self.refresh_timeout()
            except self._errorType as msg:
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    _logger.error('%s, recv occurs error: %s',
                                  str(self), os.strerror(msg.errno))
                    self._error = True
                    self._close_again()
                return

            try:
                if self._decodeError:
                    self._onDecodeError(self, recv)
                else:
                    self._decode(0, recv, addr)
                self.refresh_timeout()
            except self._RecvCBException:
                pass
            except Exception as ex:
                _logger.error('decode: %s', str(ex))
                if self._onDecodeError is not None:
                    try:
                        self._decodeError = True
                        self._onDecodeError(self, recv)
                    except Exception as ex:
                        _logger.error("_onDecodeError: %s", str(ex))
                        self._error = True
                        self._close_again()
                else:
                    self._error = True
                    self._close_again()

            if not Event.isEventSet(self._rev):
                break

    def _close(self):
        raise NotImplemented

    def _on_close(self):
        _logger.debug('_on_close')
        _logger.debug('%s, closed', str(self))

        for name in list(self._timers.keys()):
            self.del_timer(name)

        # in case of timeout happened
        Event.delEvent(self._wev)

        assert(self._timeoutEv is None)
        assert(not Event.isEventSet(self._rev))

        self._close()
        self._connected = False
        if self._onClosed is not None:
            try:
                self._onClosed(self)
            except Exception as ex:
                _logger.error('_onClosed: %s', ex)
                _logger.exception(traceback.format_exc())

    def _close_again(self):
        _logger.debug('_close_again')
        if self._cev is not None:
            self._cev.del_timer()
            self._cev = None
        self.close()

    def close(self):
        _logger.debug('close')
        if self._cev is not None:
            return
        _logger.debug('%s, closing', str(self))

        self.remove_timeout()

        timeout = 0
        if not self._error and self._connected and len(self._to_send) > 0:
            timeout = 60000

        if timeout == 0:
            Event.delEvent(self._wev)

        Event.delEvent(self._rev)
        self._cev = Event.add_timer(timeout)
        self._cev.set_handler(lambda ev: self._on_close())

    def _on_timeout(self):
        _logger.debug('_on_timeout')
        self.close()

    def refresh_timeout(self):
        _logger.debug('refresh_timeout')
        if self._timeoutEv is not None:
            self.set_timeout(self._timeout)

    def set_timeout(self, timeout):
        _logger.debug('set_timeout')
        if self._cev is not None:
            return

        if self._timeoutEv is not None:
            self.remove_timeout()

        self._timeout = timeout
        self._timeoutEv = Event.add_timer(self._timeout)
        self._timeoutEv.set_handler(lambda ev: self._on_timeout())

    def remove_timeout(self):
        _logger.debug('remove_timeout')
        if self._timeoutEv is not None:
            self._timeoutEv.del_timer()
            self._timeoutEv = None

    def _gen_on_timer(self, name, handler):
        def on_timer(_):
            if name not in self._timers:
                return
            del self._timers[name]
            try:
                handler(self)
            except Exception as ex:
                _logger.error('timer handler exception: ' + str(ex))

        return on_timer

    def add_timer(self, name, timer, handler):
        _logger.debug('add_timer: %s', name)
        if self._cev is not None:
            return

        if name in self._timers:
            raise Exception('timer: ' + name + ' already exists')

        timer_ev = Event.add_timer(timer)
        timer_ev.set_handler(self._gen_on_timer(name, handler))
        self._timers[name] = timer_ev

    def del_timer(self, name):
        _logger.debug('del_timer: %s', name)
        self._timers[name].del_timer()
        del self._timers[name]
