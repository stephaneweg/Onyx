# acidsrv.py <root> <port> -- the Acid3 test's server for acidtest.sh: pages/acid3/ served with the
# statuses and content types acid3.acidtests.org gives them (the test checks some: empty.css is
# text/html and must not apply, support-a.png is a 404, support-b.png text/html, the xhtml.N
# files text/xml...). Logs each request (REQ path status type).
import http.server, os, socketserver, sys

root, port = sys.argv[1], int(sys.argv[2])

TYPES = {
    '': ('text/html; charset=utf-8', 200, 'index.html'),
    'index.html': ('text/html; charset=utf-8', 200, None),
    'empty.css': ('text/html; charset=utf-8', 200, None),
    'empty.html': ('text/html; charset=utf-8', 200, None),
    'empty.png': ('image/png', 200, None),
    'empty.txt': ('text/plain; charset=utf-8', 200, None),
    'empty.xml': ('application/xml;charset=utf-8', 200, None),
    'reference.html': ('text/html; charset=utf-8', 200, None),
    'reference.png': ('image/png', 200, None),
    'support-a.png': ('image/png', 404, None),
    'support-b.png': ('text/html; charset=utf-8', 200, None),
    'support-c.png': ('image/png', 200, None),
    'svg.xml': ('image/svg+xml', 200, None),
    'xhtml.1': ('text/xml', 200, None),
    'xhtml.2': ('text/xml', 200, None),
    'xhtml.3': ('text/xml', 200, None),
    'font.ttf': ('application/x-truetype-font', 200, None),
    'font.svg': ('image/svg+xml', 200, None),
    'favicon.ico': ('image/vnd.microsoft.icon', 200, None),
}


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *a):
        pass

    def do_GET(self):
        name = self.path.split('?')[0].split('#')[0].lstrip('/')
        ctype, status, alias = TYPES.get(name, ('text/html; charset=utf-8', 404, None))
        f = os.path.join(root, alias or name)
        if name in TYPES and os.path.isfile(f):
            body = open(f, 'rb').read()
        else:
            body = b'<!DOCTYPE html><title>404</title><p>Not Found'
            ctype, status = 'text/html; charset=utf-8', 404
        print('REQ', self.path, status, ctype, flush=True)
        self.send_response(status)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-cache')
        self.end_headers()
        self.wfile.write(body)


class S(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


S(('127.0.0.1', port), H).serve_forever()
