# mediasrv.py <root> <port> -- the HTTP/1.1 server of mediatest.sh: the files of <root>, with
# Range requests (206 + Content-Range; a range past the end: 416), keep-alive, and a line per
# request on stdout: "GET <path> <range or -> <status> <bytes>".
import http.server, os, re, socketserver, sys, threading

root, port = sys.argv[1], int(sys.argv[2])
plock = threading.Lock()
TYPES = {'.html': 'text/html', '.webm': 'video/webm', '.mp4': 'video/mp4', '.json': 'application/json',
         '.flac': 'audio/flac', '.wav': 'audio/wav', '.js': 'text/javascript', '.mkv': 'video/x-matroska'}


def log(*a):
    with plock:
        print(*a, flush=True)


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *a):
        pass

    def do_GET(self):
        path = self.path.split('?')[0]
        f = os.path.normpath(os.path.join(root, path.lstrip('/')))
        if not f.startswith(os.path.abspath(root)) or not os.path.isfile(f):
            self.send_response(404)
            self.send_header('Content-Length', '0')
            self.end_headers()
            log('GET', path, '-', 404, 0)
            return
        data = open(f, 'rb').read()
        ctype = TYPES.get(os.path.splitext(f)[1], 'application/octet-stream')
        rng = self.headers.get('Range')
        m = re.match(r'bytes=(\d+)-(\d*)$', rng or '')
        if m:
            a = int(m.group(1))
            b = int(m.group(2)) if m.group(2) else len(data) - 1
            if a >= len(data):
                self.send_response(416)
                self.send_header('Content-Range', 'bytes */%d' % len(data))
                self.send_header('Content-Length', '0')
                self.end_headers()
                log('GET', path, rng, 416, 0)
                return
            b = min(b, len(data) - 1)
            body = data[a:b + 1]
            self.send_response(206)
            self.send_header('Content-Range', 'bytes %d-%d/%d' % (a, b, len(data)))
        else:
            body = data
            self.send_response(200)
        self.send_header('Content-Type', ctype)
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)
        log('GET', path, rng or '-', 206 if m else 200, len(body))


class S(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


S(('127.0.0.1', port), H).serve_forever()
