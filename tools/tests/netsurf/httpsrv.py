# httpsrv.py <root> <port> -- the HTTP/1.1 server of httptest.sh: keep-alive, some answers chunked,
# encoded (the pages gzip, the style sheets br, the PNG images zstd), a redirect (/redir) with a
# Set-Cookie, a Set-Cookie on each page; logs each connection (CONN n), request (REQ n path
# cookie= referer=) and coding (ENC n path coding); /ua shows and logs the User-Agent (UA n ...); the cache: the style sheets no-cache with an
# ETag (a 304 to a matching If-None-Match: NOTMOD n path), the PNG images max-age=3600; /dl/<name>?n=&cd=
# (dltest.sh: n bytes to download, a Content-Disposition per cd=, logged DL n path size) and /status/<code>
# (a page with that HTTP status).
import gzip, http.server, os, socketserver, sys, threading, time, zlib
try:
    import brotli
except ImportError:
    brotli = None
try:
    import zstandard
except ImportError:
    zstandard = None

root, port = sys.argv[1], int(sys.argv[2])
conns = 0
lock = threading.Lock()
plock = threading.Lock()


def log(*a):
    with plock:	# (whole lines: the connections' threads print at once)
        print(*a, flush=True)


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def setup(self):
        global conns
        super().setup()
        with lock:
            conns += 1
            self.cid = conns
        log('CONN', self.cid)

    def log_message(self, *a):
        pass

    def do_GET(self):
        path = self.path.split('?')[0]
        f = os.path.join(root, path.lstrip('/'))
        if os.path.isdir(f):
            f = os.path.join(f, 'index.html')
        log('REQ', self.cid, self.path, 'cookie=%s' % self.headers.get('Cookie'),
            'referer=%s' % self.headers.get('Referer'))
        if path == '/ua':	# (the site's version: uatest.sh) -- the User-Agent sent, shown and logged
            ua = self.headers.get('User-Agent') or ''
            log('UA', self.cid, ua)
            body = ('<!doctype html><title>User-Agent</title><body style="font:20px sans-serif;'
                    'margin:24px"><h1>Your User-Agent</h1><p id=ua>%s</p><script>console.log("page-ua " + '
                    'document.getElementById("ua").textContent + " | nav " + navigator.userAgent)'
                    '</script></body>' %
                    ua.replace('&', '&amp;').replace('<', '&lt;')).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Cache-Control', 'max-age=3600')	# (fresh: the disk cache's key)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if path.startswith('/dl/'):	# (the downloads: dltest.sh) -- n bytes (i*7+3 mod 251), named per cd=
            q = dict(p.split('=', 1) for p in self.path.split('?', 1)[1].split('&')) if '?' in self.path else {}
            n = int(q.get('n', '1000'))
            body = bytes((i * 7 + 3) % 251 for i in range(n))
            cd = q.get('cd', '')
            log('DL', self.cid, self.path, n)
            self.send_response(200)
            self.send_header('Content-Type', q.get('type', 'application/octet-stream').replace('%2F', '/'))
            if cd == 'attach':
                self.send_header('Content-Disposition', 'attachment; filename="report 2026.pdf"')
            elif cd == 'star':
                self.send_header('Content-Disposition',
                                 "attachment; filename=\"fallback.txt\"; filename*=UTF-8''r%C3%A9sum%C3%A9%20%E2%82%AC.txt")
            elif cd == 'bare':
                self.send_header('Content-Disposition', 'attachment')
            elif cd == 'evil':
                self.send_header('Content-Disposition', 'attachment; filename="../x/a:b*c?.txt"')
            if q.get('chunked'):
                self.send_header('Transfer-Encoding', 'chunked')
                self.end_headers()
                for i in range(0, len(body), 65536):
                    c = body[i:i + 65536]
                    try:
                        self.wfile.write(b'%x\r\n' % len(c) + c + b'\r\n')
                        self.wfile.flush()
                    except OSError:	# (the download cancelled: the connection closed)
                        log('DLABORT', self.cid, self.path, i)
                        return
                    if q.get('slow'):
                        time.sleep(0.1)
                self.wfile.write(b'0\r\n\r\n')
            else:
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            return
        if path.startswith('/status/'):	# (the status bar: dltest.sh) -- that HTTP status, a page
            code = int(path.split('/')[2])
            body = b'<!doctype html><title>Error %d</title><h1>The server says %d</h1>' % (code, code)
            self.send_response(code)
            self.send_header('Content-Type', 'text/html')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if path == '/redir':
            self.send_response(302)
            self.send_header('Location', '/kotonviolins.com/index.html')
            self.send_header('Set-Cookie', 'redir=1; Path=/')
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        if not os.path.isfile(f):
            body = b'not found'
            self.send_response(404)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        data = open(f, 'rb').read()
        ctype = 'text/html' if f.endswith('.html') else 'text/css' if f.endswith('.css') else \
            'application/javascript' if f.endswith('.js') else 'image/png' if f.endswith('.png') else \
            'image/jpeg' if f.endswith('.jpg') else 'font/ttf' if f.endswith('.ttf') else \
            'application/octet-stream'
        # the cache: the style sheets always revalidated (no-cache + an ETag: a 304 when it
        # matches), the PNG images fresh for an hour (no request at all), the pages uncached
        etag = '"%08x"' % (zlib.crc32(data) & 0xffffffff)
        if f.endswith('.css') and self.headers.get('If-None-Match') == etag:
            log('NOTMOD', self.cid, self.path)
            self.send_response(304)
            self.send_header('ETag', etag)
            self.send_header('Cache-Control', 'no-cache')
            self.end_headers()
            return
        self.send_response(200)
        self.send_header('Content-Type', ctype)
        if f.endswith('.css'):
            self.send_header('ETag', etag)
            self.send_header('Cache-Control', 'no-cache')
        elif f.endswith('.png'):
            self.send_header('Cache-Control', 'max-age=3600')
        if f.endswith('.html'):
            self.send_header('Set-Cookie', 'sid=abc123; Path=/')
        # the codings: the pages gzip, the style sheets br, the PNG images zstd -- each when the
        # request accepts it (and Python has the module: pip install brotli zstandard)
        acc = self.headers.get('Accept-Encoding') or ''
        enc = None
        if f.endswith('.css') and brotli is not None and 'br' in acc:
            data, enc = brotli.compress(data), 'br'
        elif f.endswith('.png') and zstandard is not None and 'zstd' in acc:
            data, enc = zstandard.ZstdCompressor(level=10).compress(data), 'zstd'
        elif f.endswith('.css') or f.endswith('.html'):
            data, enc = gzip.compress(data), 'gzip'
        if enc:
            self.send_header('Content-Encoding', enc)
        log('ENC', self.cid, self.path, enc)
        if hash(f) % 2 == 0 or f.endswith('.html'):
            self.send_header('Transfer-Encoding', 'chunked')
            self.end_headers()
            for i in range(0, len(data), 3000):
                c = data[i:i + 3000]
                self.wfile.write(b'%x\r\n' % len(c) + c + b'\r\n')
            self.wfile.write(b'0\r\n\r\n')
        else:
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)


class S(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


S(('127.0.0.1', port), H).serve_forever()
