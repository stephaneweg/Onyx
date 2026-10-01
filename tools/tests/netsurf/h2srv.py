# h2srv.py <root> <port> <cert.pem> <key.pem> [h1] -- the HTTPS server of httptest.sh's HTTP/2 part
# (Python's ssl and the h2 package: pip install h2 -- brotli / zstandard optional). TLS with ALPN
# "h2" only (h1: "http/1.1" only, the plain HTTP/1.1 fallback -- the client must see it and go
# on over HTTP/1.1). Serves the files of <root> (the pages gzip, the style sheets br, the PNG
# images zstd, as httpsrv.py, a Set-Cookie on the pages) and
#   /redir        302 to /kotonviolins.com/index.html with a Set-Cookie
#   /echo         (any method) "<METHOD> <body> cookie=<Cookie>" as text/plain
#   /big?n=N      N bytes (flow control: past the stream and connection windows)
#   /stream?n=K   K text parts 200 ms apart (a streamed response: fetch / XHR as it comes)
#   /small?i=I    "small I"
#   /pages/x      the file x of $H2SRV_PAGES (the test pages)
# The stream data is sent as the peer's flow-control windows allow. Logs each connection
# ("CONN n alpn") and request ("REQ n stream path cookie= referer= method=") and coding
# ("ENC n path coding").
import gzip, http.server, os, select, socket, socketserver, ssl, sys, threading, time
from urllib.parse import urlsplit, parse_qs
try:
    import brotli
except ImportError:
    brotli = None
try:
    import zstandard
except ImportError:
    zstandard = None

root, port, cert, key = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
h1 = len(sys.argv) > 5 and sys.argv[5] == 'h1'
plock = threading.Lock()
conns = 0


def log(*a):
    with plock:
        print(*a, flush=True)


def ctype_of(f):
    return 'text/html' if f.endswith('.html') else 'text/css' if f.endswith('.css') else \
        'application/javascript' if f.endswith('.js') else 'image/png' if f.endswith('.png') else \
        'image/jpeg' if f.endswith('.jpg') else 'font/ttf' if f.endswith('.ttf') else \
        'application/octet-stream'


def encode(f, data, acc):
    if f.endswith('.css') and brotli is not None and 'br' in acc:
        return brotli.compress(data), 'br'
    if f.endswith('.png') and zstandard is not None and 'zstd' in acc:
        return zstandard.ZstdCompressor(level=10).compress(data), 'zstd'
    if f.endswith('.css') or f.endswith('.html'):
        return gzip.compress(data), 'gzip'
    return data, None


class Conn:
    def __init__(self, sock, cid):
        import h2.config, h2.connection
        self.s, self.cid = sock, cid
        self.h2 = h2.connection.H2Connection(h2.config.H2Configuration(
            client_side=False, header_encoding='utf-8'))
        self.req = {}		# stream -> (headers, body)
        self.out = {}		# stream -> [data, end]
        self.tasks = []		# (time, fn)

    def flush(self):
        d = self.h2.data_to_send()
        if d:
            self.s.sendall(d)

    def pump(self):
        for sid in list(self.out):
            data, end = self.out[sid]
            while data:
                try:
                    n = min(len(data), self.h2.local_flow_control_window(sid),
                            self.h2.max_outbound_frame_size)
                except Exception:
                    del self.out[sid]
                    break
                if n <= 0:
                    break
                self.h2.send_data(sid, bytes(data[:n]))
                del data[:n]
            if sid in self.out and not data and end:
                self.h2.end_stream(sid)
                del self.out[sid]

    def send(self, sid, data, end):
        o = self.out.setdefault(sid, [bytearray(), False])
        o[0] += data
        o[1] = end
        self.pump()

    def respond(self, sid):
        hd, body = self.req.pop(sid)
        u = urlsplit(hd.get(':path', '/'))
        q = parse_qs(u.query)
        m = hd.get(':method', 'GET')
        log('REQ', self.cid, sid, u.path, 'cookie=%s' % hd.get('cookie'),
            'referer=%s' % hd.get('referer'), 'method=%s' % m)
        if u.path == '/redir':
            self.h2.send_headers(sid, [(':status', '302'), ('location', '/kotonviolins.com/index.html'),
                                       ('set-cookie', 'redir=1; Path=/'), ('content-length', '0')],
                                 end_stream=True)
            return
        if u.path == '/echo':
            b = ('%s %s cookie=%s' % (m, bytes(body).decode('utf-8', 'replace'),
                                       hd.get('cookie'))).encode()
            self.h2.send_headers(sid, [(':status', '200'), ('content-type', 'text/plain'),
                                       ('content-length', str(len(b)))])
            self.send(sid, b, True)
            return
        if u.path == '/big':
            n = int(q.get('n', ['1000000'])[0])
            b = bytes(i % 251 for i in range(n))
            self.h2.send_headers(sid, [(':status', '200'), ('content-type', 'application/octet-stream'),
                                       ('content-length', str(n))])
            self.send(sid, b, True)
            return
        if u.path == '/small':
            b = ('small %s' % q.get('i', ['?'])[0]).encode()
            self.h2.send_headers(sid, [(':status', '200'), ('content-type', 'text/plain'),
                                       ('content-length', str(len(b)))])
            self.send(sid, b, True)
            return
        if u.path == '/stream':
            k = int(q.get('n', ['5'])[0])
            self.h2.send_headers(sid, [(':status', '200'), ('content-type', 'text/plain')])
            t = time.time()
            for i in range(k):
                self.tasks.append((t + 0.2 * (i + 1), (lambda i=i: self.send(
                    sid, ('part %d\n' % i).encode(), i == k - 1))))
            return
        f = os.path.join(root, u.path.lstrip('/'))
        if u.path.startswith('/pages/') and os.environ.get('H2SRV_PAGES'):
            f = os.path.join(os.environ['H2SRV_PAGES'], u.path[7:])
        if os.path.isdir(f):
            f = os.path.join(f, 'index.html')
        if not os.path.isfile(f):
            self.h2.send_headers(sid, [(':status', '404'), ('content-length', '9')])
            self.send(sid, b'not found', True)
            return
        data, enc = encode(f, open(f, 'rb').read(), hd.get('accept-encoding') or '')
        h = [(':status', '200'), ('content-type', ctype_of(f)), ('content-length', str(len(data)))]
        if enc:
            h.append(('content-encoding', enc))
        if f.endswith('.html'):
            h.append(('set-cookie', 'sid=abc123; Path=/'))
        log('ENC', self.cid, u.path, enc)
        self.h2.send_headers(sid, h)
        self.send(sid, data, True)

    def run(self):
        import h2.events
        self.h2.initiate_connection()
        self.flush()
        while True:
            now = time.time()
            wait = min([t for t, _ in self.tasks], default=now + 1.0) - now
            r, _, _ = select.select([self.s], [], [], max(0, wait))
            if r:
                try:
                    d = self.s.recv(65536)
                except (ssl.SSLWantReadError, BlockingIOError):
                    d = None
                except OSError:
                    return
                if d == b'':
                    return
                for ev in self.h2.receive_data(d) if d else []:
                    if isinstance(ev, h2.events.RequestReceived):
                        self.req[ev.stream_id] = (dict(ev.headers), bytearray())
                        if ev.stream_ended:
                            self.respond(ev.stream_id)
                    elif isinstance(ev, h2.events.DataReceived):
                        self.req[ev.stream_id][1].extend(ev.data)
                        self.h2.acknowledge_received_data(ev.flow_controlled_length, ev.stream_id)
                    elif isinstance(ev, h2.events.StreamEnded):
                        if ev.stream_id in self.req:
                            self.respond(ev.stream_id)
                    elif isinstance(ev, h2.events.StreamReset):
                        self.out.pop(ev.stream_id, None)
                        self.req.pop(ev.stream_id, None)
                        log('RESET', self.cid, ev.stream_id)
                    elif isinstance(ev, h2.events.ConnectionTerminated):
                        self.flush()
                        return
            now = time.time()
            due = [t for t in self.tasks if t[0] <= now]
            self.tasks = [t for t in self.tasks if t[0] > now]
            for _, fn in due:
                try:
                    fn()
                except Exception:
                    pass
            self.pump()
            try:
                self.flush()
            except OSError:
                return


class H1(http.server.BaseHTTPRequestHandler):
    """The fallback: plain HTTP/1.1 over TLS (ALPN http/1.1)."""
    protocol_version = 'HTTP/1.1'

    def log_message(self, *a):
        pass

    def do_GET(self):
        path = self.path.split('?')[0]
        log('REQ', 'h1', 0, path, 'cookie=%s' % self.headers.get('Cookie'),
            'referer=%s' % self.headers.get('Referer'), 'method=GET')
        f = os.path.join(root, path.lstrip('/'))
        if os.path.isdir(f):
            f = os.path.join(f, 'index.html')
        data = open(f, 'rb').read() if os.path.isfile(f) else b'not found'
        self.send_response(200 if os.path.isfile(f) else 404)
        self.send_header('Content-Type', ctype_of(f))
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)


ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(cert, key)
ctx.set_alpn_protocols(['http/1.1'] if h1 else ['h2'])

if h1:
    class S(socketserver.ThreadingMixIn, http.server.HTTPServer):
        daemon_threads = True
        allow_reuse_address = True

        def get_request(self):
            s, a = self.socket.accept()
            log('CONN', 'h1')
            return ctx.wrap_socket(s, server_side=True), a
    S(('127.0.0.1', port), H1).serve_forever()

ls = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
ls.bind(('127.0.0.1', port))
ls.listen(64)


def serve(s, cid):
    try:
        t = ctx.wrap_socket(s, server_side=True)
    except (ssl.SSLError, OSError) as e:
        log('TLSFAIL', cid, e)
        return
    log('CONN', cid, t.selected_alpn_protocol())
    try:
        if t.selected_alpn_protocol() == 'h2':
            Conn(t, cid).run()
    finally:
        try:
            t.close()
        except OSError:
            pass


while True:
    s, _ = ls.accept()
    conns += 1
    threading.Thread(target=serve, args=(s, conns), daemon=True).start()
