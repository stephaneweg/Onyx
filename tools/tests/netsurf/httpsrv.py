# httpsrv.py <root> <port> -- the HTTP/1.1 server of httptest.sh: keep-alive, some answers chunked,
# some gzip (the pages and style sheets), a redirect (/redir) with a Set-Cookie, a Set-Cookie on
# each page; logs each connection (CONN n) and request (REQ n path cookie= referer=).
import gzip, http.server, os, socketserver, sys, threading

root, port = sys.argv[1], int(sys.argv[2])
conns = 0
lock = threading.Lock()


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def setup(self):
        global conns
        super().setup()
        with lock:
            conns += 1
            self.cid = conns
        print('CONN', self.cid, flush=True)

    def log_message(self, *a):
        pass

    def do_GET(self):
        path = self.path.split('?')[0]
        f = os.path.join(root, path.lstrip('/'))
        if os.path.isdir(f):
            f = os.path.join(f, 'index.html')
        print('REQ', self.cid, self.path, 'cookie=%s' % self.headers.get('Cookie'),
              'referer=%s' % self.headers.get('Referer'), flush=True)
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
        self.send_response(200)
        self.send_header('Content-Type', ctype)
        if f.endswith('.html'):
            self.send_header('Set-Cookie', 'sid=abc123; Path=/')
        gz = f.endswith('.css') or f.endswith('.html')
        if gz:
            data = gzip.compress(data)
            self.send_header('Content-Encoding', 'gzip')
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
