# wssrv.py <root> <port> -- the server of nettest.sh (Python's standard library only): the files of
# <root> (the test pages, so that they are same-origin), and
#   /ws/echo     a WebSocket echo (text and binary; permessage-deflate when offered; the first of
#                the subprotocols "chat" / "superchat" offered; a Set-Cookie on the handshake)
#   /ws/frag     a text message in three fragments with a ping between them, a 200 KB binary
#                message, then the server's close (4001 "bye")
#   /ws/bad      a masked frame from the server (a protocol error: the client fails the socket)
#   /sse         an event stream: named events, ids, multi-line data, a comment, retry: 300, then
#                the connection closed; a reconnection (Last-Event-ID) gets the rest and "done"
#   /sse404      an event stream refused (404: no reconnection)
#   /chunks      a chunked text/plain in five parts 150 ms apart (streamed fetch, XHR progress)
#   POST *       the body sent back
#   /cors?...    a cross-origin target (any method, OPTIONS the preflight): acao=origin|star (its
#                Access-Control-Allow-Origin: the request's Origin, "*"; none without), acac=1
#                (Allow-Credentials: true), expose=<h>, methods=<m>, headers=<h> (Allow-Methods /
#                -Headers), cookie=1 (a Set-Cookie); the body: "cors <method> cookie=<Cookie>";
#                an X-Secret header always; logged "CORS <method> path cookie= origin= acrm= acrh="
# Logs each request ("REQ path cookie= origin= last-event-id= protocol=") and each WebSocket
# message ("WS got text|binary n"), each close ("WS close code").
import base64, hashlib, http.server, os, socketserver, struct, sys, threading, time, zlib

root, port = sys.argv[1], int(sys.argv[2])
GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'


def log(*a):
    print(*a, flush=True)


class WS:
    """A server's end of a WebSocket (RFC 6455) on a socket."""

    def __init__(self, sock, rfile, deflate):
        self.s, self.r, self.deflate = sock, rfile, deflate
        self.inf = zlib.decompressobj(-15) if deflate else None
        self.dfl = zlib.compressobj(zlib.Z_DEFAULT_COMPRESSION, zlib.DEFLATED, -15) if deflate else None

    def frame(self, op, data, fin=True, rsv1=False, mask=False):
        b0 = (0x80 if fin else 0) | (0x40 if rsv1 else 0) | op
        n = len(data)
        h = bytes([b0])
        mb = 0x80 if mask else 0
        if n < 126:
            h += bytes([mb | n])
        elif n < 65536:
            h += bytes([mb | 126]) + struct.pack('>H', n)
        else:
            h += bytes([mb | 127]) + struct.pack('>Q', n)
        if mask:
            k = b'\x01\x02\x03\x04'
            h += k
            data = bytes(c ^ k[i % 4] for i, c in enumerate(data))
        self.s.sendall(h + data)

    def send(self, data, binary=False, compress=True):
        if isinstance(data, str):
            data = data.encode()
        op = 2 if binary else 1
        if self.deflate and compress:
            c = self.dfl.compress(data) + self.dfl.flush(zlib.Z_SYNC_FLUSH)
            self.frame(op, c[:-4], rsv1=True)
        else:
            self.frame(op, data)

    def read(self, n):
        b = b''
        while len(b) < n:
            c = self.r.read(n - len(b))
            if not c:
                raise EOFError
            b += c
        return b

    def recv(self):
        """the next message: (opcode, payload); pings answered"""
        msg, mop, comp = b'', None, False
        while True:
            b0, b1 = self.read(2)
            fin, rsv1, op = b0 & 0x80, b0 & 0x40, b0 & 15
            n = b1 & 127
            if n == 126:
                n = struct.unpack('>H', self.read(2))[0]
            elif n == 127:
                n = struct.unpack('>Q', self.read(8))[0]
            if not b1 & 0x80:
                raise ValueError('client frame not masked')
            k = self.read(4)
            data = bytes(c ^ k[i % 4] for i, c in enumerate(self.read(n)))
            if op == 9:
                self.frame(10, data)
                continue
            if op == 10:
                continue
            if op == 8:
                return 8, data
            if op != 0:
                mop, comp = op, bool(rsv1)
            msg += data
            if fin:
                if comp:
                    msg = self.inf.decompress(msg + b'\x00\x00\xff\xff')
                return mop, msg


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *a):
        pass

    def upgrade(self):
        key = self.headers.get('Sec-WebSocket-Key')
        if not key or 'websocket' not in (self.headers.get('Upgrade') or '').lower():
            self.send_error(400)
            return None
        acc = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
        offered = [p.strip() for p in (self.headers.get('Sec-WebSocket-Protocol') or '').split(',') if p.strip()]
        proto = next((p for p in offered if p in ('chat', 'superchat')), None)
        deflate = 'permessage-deflate' in (self.headers.get('Sec-WebSocket-Extensions') or '')
        self.send_response(101, 'Switching Protocols')
        self.send_header('Upgrade', 'websocket')
        self.send_header('Connection', 'Upgrade')
        self.send_header('Sec-WebSocket-Accept', acc)
        self.send_header('Set-Cookie', 'wscookie=yes; Path=/')
        if proto:
            self.send_header('Sec-WebSocket-Protocol', proto)
        if deflate:
            self.send_header('Sec-WebSocket-Extensions', 'permessage-deflate')
        self.end_headers()
        self.wfile.flush()
        return WS(self.connection, self.rfile, deflate)

    def cors(self):
        import urllib.parse
        q = dict(urllib.parse.parse_qsl(self.path.partition('?')[2]))
        n = int(self.headers.get('Content-Length') or 0)
        if n:
            self.rfile.read(n)
        log('CORS', self.command, self.path, 'cookie=%s' % self.headers.get('Cookie'),
            'origin=%s' % self.headers.get('Origin'),
            'acrm=%s' % self.headers.get('Access-Control-Request-Method'),
            'acrh=%s' % self.headers.get('Access-Control-Request-Headers'))
        body = ('cors %s cookie=%s' % (self.command, self.headers.get('Cookie'))).encode()
        self.send_response(204 if self.command == 'OPTIONS' else 200)
        if q.get('acao') == 'origin':
            self.send_header('Access-Control-Allow-Origin', self.headers.get('Origin') or 'null')
        elif q.get('acao') == 'star':
            self.send_header('Access-Control-Allow-Origin', '*')
        if q.get('acac'):
            self.send_header('Access-Control-Allow-Credentials', 'true')
        for k, h in (('expose', 'Expose-Headers'), ('methods', 'Allow-Methods'), ('headers', 'Allow-Headers')):
            if q.get(k):
                self.send_header('Access-Control-' + h, q[k])
        if q.get('cookie') and self.command != 'OPTIONS':
            self.send_header('Set-Cookie', 'corscookie=yes; Path=/')
        self.send_header('X-Secret', 'hidden')
        self.send_header('Content-Type', 'text/plain')
        if self.command == 'OPTIONS':
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    do_OPTIONS = do_PUT = cors

    def do_GET(self):
        path = self.path.split('?')[0]
        if path == '/cors':
            return self.cors()
        log('REQ', self.path, 'cookie=%s' % self.headers.get('Cookie'),
            'origin=%s' % self.headers.get('Origin'),
            'last-event-id=%s' % self.headers.get('Last-Event-ID'),
            'protocol=%s' % self.headers.get('Sec-WebSocket-Protocol'),
            'extensions=%s' % self.headers.get('Sec-WebSocket-Extensions'))
        if path == '/ws/echo':
            ws = self.upgrade()
            if ws is None:
                return
            try:
                while True:
                    op, data = ws.recv()
                    if op == 8:
                        code = struct.unpack('>H', data[:2])[0] if len(data) >= 2 else 1005
                        log('WS close', code, data[2:].decode(errors='replace'))
                        ws.frame(8, data)
                        break
                    log('WS got', 'text' if op == 1 else 'binary', len(data))
                    if op == 1 and data == b'close-me':
                        ws.frame(8, struct.pack('>H', 4002) + b'as asked')
                        continue
                    ws.send(data, binary=op == 2)
            except (EOFError, ConnectionError, ValueError) as e:
                log('WS end', type(e).__name__)
            self.close_connection = True
            return
        if path == '/ws/frag':
            ws = self.upgrade()
            if ws is None:
                return
            try:
                ws.frame(1, b'frag', fin=False)
                ws.frame(9, b'are you there')
                ws.frame(0, 'menté'.encode(), fin=False)
                ws.frame(0, b'd', fin=True)
                big = bytes(i & 255 for i in range(200000))
                ws.send(big, binary=True, compress=False)
                time.sleep(0.3)
                ws.frame(8, struct.pack('>H', 4001) + b'bye')
                op, data = ws.recv()
                log('WS frag close answer', op, struct.unpack('>H', data[:2])[0] if len(data) >= 2 else 0)
            except (EOFError, ConnectionError, ValueError) as e:
                log('WS end', type(e).__name__)
            self.close_connection = True
            return
        if path == '/ws/bad':
            ws = self.upgrade()
            if ws is None:
                return
            ws.frame(1, b'masked!', mask=True)
            try:
                op, data = ws.recv()
                log('WS bad close', op, struct.unpack('>H', data[:2])[0] if len(data) >= 2 else 0)
            except (EOFError, ConnectionError, ValueError) as e:
                log('WS end', type(e).__name__)
            self.close_connection = True
            return
        if path in ('/sse', '/sse404'):
            if path == '/sse404':
                self.send_response(404)
                self.send_header('Content-Length', '0')
                self.end_headers()
                return
            self.send_response(200)
            self.send_header('Content-Type', 'text/event-stream')
            self.send_header('Cache-Control', 'no-cache')
            self.send_header('Transfer-Encoding', 'chunked')
            self.end_headers()

            def chunk(s):
                b = s.encode()
                self.wfile.write(b'%x\r\n' % len(b) + b + b'\r\n')
                self.wfile.flush()
            last = self.headers.get('Last-Event-ID')
            if last is None:
                chunk('retry: 300\n: a comment\n\n')
                chunk('data: first\n\n')
                chunk('id: 7\nevent: tick\ndata: {"n":1}\n\n')
                time.sleep(0.1)
                chunk('data: line one\r\ndata: line two\r\n\r')  # (a CRLF cut in two)
                chunk('\nid: 8\ndata: third\n\n')
                chunk('data: no end')          # (an event never finished: not dispatched)
            else:
                chunk('event: resumed\ndata: after %s\n\n' % last)
                chunk('event: done\ndata: bye\n\n')
                time.sleep(3)
            self.wfile.write(b'0\r\n\r\n')
            self.close_connection = True
            return
        if path == '/chunks':
            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.send_header('Transfer-Encoding', 'chunked')
            self.end_headers()
            for i in range(5):
                b = ('part%d;' % i).encode() * 200
                self.wfile.write(b'%x\r\n' % len(b) + b + b'\r\n')
                self.wfile.flush()
                time.sleep(0.15)
            self.wfile.write(b'0\r\n\r\n')
            return
        f = os.path.join(root, path.lstrip('/'))
        if not os.path.isfile(f):
            body = b'not found'
            self.send_response(404)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        data = open(f, 'rb').read()
        ctype = 'text/html' if f.endswith('.html') else 'text/javascript' if f.endswith('.js') else \
            'application/octet-stream'
        self.send_response(200)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)


    def do_POST(self):
        if self.path.startswith('/cors'):
            return self.cors()
        n = int(self.headers.get('Content-Length') or 0)
        body = self.rfile.read(n) if n else b''
        log('POST', self.path, len(body), body[:40].decode(errors='replace'))
        self.send_response(200)
        self.send_header('Content-Type', 'text/plain')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class S(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def handle_error(self, request, client_address):
        pass            # (a client gone: the aborts of the tests)


S(('127.0.0.1', port), H).serve_forever()
