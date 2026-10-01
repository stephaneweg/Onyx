# xmlsrv.py <root> <port> -- the server of xmltest.sh (docs/06 section 43): the files of <root>
# served with the Content-Type their extension names (.xhtml application/xhtml+xml, .xml
# application/xml, .txml text/xml, .svg image/svg+xml, .xsl text/xsl, .unk
# application/x-onyx-unknown...), or the one a "?type=<mime>" query asks for. Logs each request
# (REQ path status type).
import http.server, os, socketserver, sys, urllib.parse

root, port = os.path.abspath(sys.argv[1]), int(sys.argv[2])

TYPES = {
    '.html': 'text/html; charset=utf-8',
    '.xhtml': 'application/xhtml+xml',
    '.xml': 'application/xml',
    '.txml': 'text/xml',
    '.svg': 'image/svg+xml',
    '.xsl': 'text/xsl',
    '.css': 'text/css',
    '.js': 'text/javascript',
    '.png': 'image/png',
    '.unk': 'application/x-onyx-unknown',
}


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *a):
        pass

    def do_GET(self):
        u = urllib.parse.urlsplit(self.path)
        q = urllib.parse.parse_qs(u.query)
        name = urllib.parse.unquote(u.path).lstrip('/') or 'index.html'
        f = os.path.normpath(os.path.join(root, name))
        status = 200
        if f.startswith(root) and os.path.isfile(f):
            body = open(f, 'rb').read()
            ctype = TYPES.get(os.path.splitext(f)[1], 'application/octet-stream')
        else:
            body = b'<!DOCTYPE html><title>404</title><p>Not Found'
            ctype, status = 'text/html; charset=utf-8', 404
        if 'type' in q:
            ctype = q['type'][0]
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
