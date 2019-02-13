import socket
import errno
import traceback
from event import Event
from collections import deque

import logging
import loglevel
_logger = loglevel.get_logger('non-blocking')
_logger.setLevel(loglevel.DEFAULT_LEVEL)


class NonBlocking(object):

    class _ReceiveHandlerContext(object):

        def __init__(self, handler):
            self.handler = handler
            self.remain = ''

    def __init__(self, fd, prefix=None):
        self._fd = fd
        self._prefix = prefix
        self.set_non_blocking()

        self._to_send = deque()

        self._encoders = []
        self._decoders = []

        self._remain = ''

        self._error = False

        self._connected = False
        self._fin_received = False
        self._fin_appended = False
        self._fin_sent = False
        self._closed = False

        self._on_ready_to_send = None
        self._on_send_buffer_full = None
        self._on_received = None
        self._on_fin_received = None
        self._on_closed = None

        self._cev = None

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

    def __hash__(self):
        return hash(self._fd.fileno())

    def __eq__(self, other):
        if not isinstance(other, NonBlocking):
            return False
        return self._fd.fileno() == other._fd.fileno()

    def __str__(self):
        if self._closed:
            return "%s: #" % self._prefix
        else:
            return "%s: %d" % (self._prefix, self._fd.fileno())

    def set_prefix(self, prefix):
        self._prefix = prefix

    def set_non_blocking(self):
        raise NotImplemented

    def set_on_ready_to_send(self, handler):
        self._on_ready_to_send = handler

    def set_on_send_buffer_full(self, handler):
        self._on_send_buffer_full = handler

    def set_on_received(self, on_received):
        self._on_received = on_received

    def set_on_fin_received(self, on_fin_received):
        self._on_fin_received = on_fin_received

    def set_on_closed(self, on_closed):
        self._on_closed = on_closed

    def set_on_decode_error(self, on_decode_error):
        self._onDecodeError = on_decode_error

    def append_send_handler(self, handler):
        self._encoders.append(handler)

    def append_receive_handler(self, handler):
        self._decoders.append(self._ReceiveHandlerContext(handler))

    def start_receiving(self):
        _logger.debug('%s, start_receiving', str(self))
        if not self._connected or self._fin_received or self.is_closed():
            return
        if not Event.isEventSet(self._rev):
            _logger.debug('%s, start_receiving::addEvent', str(self))
            Event.addEvent(self._rev)

    def stop_receiving(self):
        _logger.debug('%s, stop_receiving', str(self))
        if Event.isEventSet(self._rev):
            _logger.debug('%s, stop_receiving::delEvent', str(self))
            Event.delEvent(self._rev)

    def _do_close(self):
        _logger.debug('%s, _do_close', str(self))
        if self._error:
            self._stop_sending()
            self.stop_receiving()
        if self._cev is None:
            self._cev = Event.add_timer(0)
            self._cev.set_handler(lambda ev: self._on_close())

    def _receive_fin(self):
        _logger.debug('%s, _receive_fin', str(self))
        self._fin_received = True
        self.stop_receiving()
        if self._fin_sent:
            self._stop_sending()
            self._do_close()
        if self._on_fin_received is not None:
            try:
                self._on_fin_received(self)
            except Exception as ex:
                _logger.error('_on_fin_received: %s', str(ex))
                if _logger.level <= logging.DEBUG:
                    _logger.error('%s', traceback.format_exc())
                self._error = True
                self._do_close()
                return

    def _send_fin(self):
        raise NotImplemented

    def _send(self, data, addr):
        raise NotImplemented

    def _start_sending(self):
        _logger.debug('%s, _start_sending', str(self))
        if not Event.isEventSet(self._wev):
            _logger.debug('%s, _start_sending::addEvent', str(self))
            Event.addEvent(self._wev)

    def _stop_sending(self):
        _logger.debug('%s, _stop_sending', str(self))
        if Event.isEventSet(self._wev):
            _logger.debug('%s, _stop_sending::delEvent', str(self))
            Event.delEvent(self._wev)

    def _on_send(self):
        _logger.debug('%s, _on_send', str(self))
        if self._fin_sent:
            self._stop_sending()
            if self._fin_received:
                self._do_close()
            else:
                self.start_receiving()
            return

        sent_bytes = 0
        ready_to_send = True
        while len(self._to_send) > 0:
            data, addr = self._to_send.popleft()
            if data is None:
                left = len(self._to_send)
                if left != 0:
                    _logger.info('%s, discard %d packages', str(self), left)
                    self._to_send.clear()
                self._shutdown()
                break
            try:
                sent = self._send(data, addr)
                sent_bytes += sent
                if sent < len(data):
                    self._to_send.appendleft((data[sent:], addr))
            except self._errorType as msg:
                self._to_send.appendleft((data, addr))
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    _logger.error('%s, send(%d): %s',
                                  str(self), msg.errno, msg.strerror)
                    self._error = True
                    self._do_close()
                    return
                else:
                    ready_to_send = False
                    break

        _logger.debug("%s, sent %d bytes", str(self), sent_bytes)

        if len(self._to_send) == 0:
            self._stop_sending()

        if ready_to_send:
            if self._on_ready_to_send:
                try:
                    self._on_ready_to_send(self)
                except Exception as ex:
                    _logger.error('_on_ready_to_send: %s', str(ex))
                    if _logger.level <= logging.DEBUG:
                        _logger.error('%s', traceback.format_exc())
                    self._error = True
                    self._do_close()
        else:
            if self._on_send_buffer_full:
                try:
                    self._on_send_buffer_full(self)
                except Exception as ex:
                    _logger.error('_on_send_buffer_full: %s', str(ex))
                    if _logger.level <= logging.DEBUG:
                        _logger.error('%s', traceback.format_exc())
                    self._error = True
                    self._do_close()

    def send(self, data, addr=None):
        _logger.debug('%s, send', str(self))
        if self._fin_appended or self._fin_sent or self.is_closed():
            _logger.warning('%s, already shutdown, discard %d bytes', str(self), len(data))
            return

        to_send = data
        try:
            for encoder in self._encoders:
                to_send = encoder(to_send)
        except Exception as ex:
            _logger.error('failed to encode %d bytes: %s', len(data), str(ex))
            raise ex

        self._to_send.append((to_send, addr))

        if addr is not None:
            _logger.debug('%s, sending %d(%d) bytes to %s:%d', str(self), len(to_send), len(data), *addr)
        else:
            _logger.debug('%s, sending %d(%d) bytes', str(self), len(to_send), len(data))

        if self._connected:
            self._start_sending()

    def is_ready_to_send(self):
        if self.is_closed() or not self._connected:
            return False

        return Event.isEventSet(self._wev)

    def _recv(self, size):
        raise NotImplemented

    def _decode_single_depth(self, depth, data):
        # _logger.debug('%s, _decode_single_depth', str(self))
        context = self._decoders[depth]
        if len(context.remain) > 0:
            data = context.remain + data
        total_consumed = []
        total_processed = 0
        while True:
            consumed, processed = context.handler(data[total_processed:])
            if processed == 0:
                break
            if consumed is not None:
                total_consumed.append(consumed)
            total_processed += processed
        if total_processed < len(data):
            context.remain = data[total_processed:]
        else:
            context.remain = ''
        return total_consumed, total_processed

    def _decode(self, data):
        _logger.debug('%s, _decode', str(self))
        if len(self._decoders) == 0:
            return [data], len(data)

        total_consumed = []
        total_processed = 0
        while True:
            loop_processed = 0
            to_consume = data[total_processed:]
            for depth in range(len(self._decoders)):
                consumed, processed = self._decode_single_depth(depth, to_consume)
                if processed == 0:
                    break
                if depth == 0:
                    total_processed += processed
                    loop_processed += processed
                if depth == len(self._decoders) - 1:
                    total_consumed.extend(consumed)
                to_consume = reduce(lambda l, r: l + r, consumed, '')
            if loop_processed == 0:
                break
        return total_consumed, total_processed

    def _on_receive(self):
        _logger.debug('%s, _on_receive', str(self))
        buff_size = 2 ** 16
        while True:
            try:
                recv, addr = self._recv(buff_size)
                if len(recv) == 0:
                    self._receive_fin()
                    return
                _logger.debug("%s, received %d bytes", str(self), len(recv))
            except self._errorType as msg:
                if msg.errno != errno.EAGAIN and msg.errno != errno.EINPROGRESS:
                    _logger.error('%s, recv occurs error(%d): %s',
                                  str(self), msg.errno, msg.strerror)
                    self._error = True
                    self._do_close()
                return

            if len(self._remain) > 0:
                recv = self._remain + recv

            if self._decodeError:
                consumed, processed = [recv], len(recv)
            else:
                try:
                    consumed, processed = self._decode(recv)
                except Exception as ex:
                    _logger.error('decode error: %s', str(ex))
                    if _logger.level <= logging.DEBUG:
                        _logger.error('%s', traceback.format_exc())
                    if self._onDecodeError is not None:
                        self._decodeError = True
                    else:
                        self._error = True
                        self._do_close()
                        return
                    consumed, processed = [recv], len(recv)

            if processed < len(recv):
                self._remain = recv[processed:]
            else:
                self._remain = ''

            stop = False
            try:
                for data in consumed:
                    if self._decodeError:
                        ret = self._onDecodeError(self, data)
                    else:
                        ret = self._on_received(self, data, addr)
                    if not ret:
                        stop = True
            except Exception as ex:
                if self._decodeError:
                    _logger.error('_onDecodeError error: %s', str(ex))
                else:
                    _logger.error('_on_received error: %s', str(ex))
                if _logger.level <= logging.DEBUG:
                    _logger.error('%s', traceback.format_exc())
                self._error = True
                self._do_close()
                return

            if stop:
                self.stop_receiving()

            if not Event.isEventSet(self._rev):
                break

    def _close(self):
        raise NotImplemented

    def is_closed(self):
        return self._cev is not None or self._closed

    def _on_close(self):
        _logger.debug('%s, _on_close', str(self))

        assert(not Event.isEventSet(self._wev))
        assert(not Event.isEventSet(self._rev))

        self._close()
        self._connected = False
        self._closed = True
        if self._on_closed is not None:
            try:
                self._on_closed(self)
            except Exception as ex:
                _logger.error('_on_closed: %s', ex)
                if _logger.level <= logging.DEBUG:
                    _logger.error('%s', traceback.format_exc())

    def _shutdown(self, force=False):
        _logger.debug('%s, _shutdown', str(self))
        if self.is_closed() or (self._fin_sent and force is False):
            return

        if force:
            self._error = True
            self._do_close()
            return

        if len(self._to_send) == 0:
            self._fin_sent = True
            if self._fin_received:
                self._stop_sending()
                self._do_close()
            else:
                self._start_sending()
                self._send_fin()
        else:
            _logger.debug('delay shutdown')
            # append poison
            if self._fin_appended is False:
                self._to_send.append((None, None))
                self._fin_appended = True

    def shutdown(self):
        _logger.debug('%s, shutdown', str(self))
        self._shutdown()

    def close(self):
        _logger.debug('%s, close', str(self))
        self._shutdown(force=True)
