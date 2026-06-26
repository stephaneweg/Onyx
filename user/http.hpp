//
// http.hpp -- a small reusable HTTP/1.1 client class for Onyx user apps.
//
// Built on the ABI v21 TCP socket kapis (kapi_tcp_connect/send/recv/close +
// kapi_net_status). Aimed at REST / web-API clients: custom request headers
// (Authorization, Accept, ...), a request body with a chosen Content-Type, and a
// parsed response (status code + headers + body, with chunked transfer decoded).
//
// Design (matches the Onyx user model, like httpc.h / uikit.hpp):
//   * Header-only, freestanding C++ -- works in EVERY app (integer-only GUI apps and
//     newlib apps alike). It does NOT use new/delete, the STL, or libc; only kapi and
//     its own byte-loop helpers. Compile it into one translation unit (one app).
//   * The CALLER provides the response buffer (char buf[N]); the response body/headers
//     point INTO it. No dynamic allocation. `truncated` flags an overflow.
//   * Connection: close -- one request per connection; we read until the server closes
//     (or Content-Length / chunked tells us the body is complete), then parse.
//   * Blocking: polls the non-blocking kapi_tcp_recv with a timeout (briefly stalls the
//     calling app, like an IRC connect). Fine for occasional API calls.
//
// HTTPS: NetSurf and friends get HTTPS from an external TLS stack (libcurl+OpenSSL);
// Onyx has no TLS yet. This class RECOGNIZES https:// and returns HTTP_ERR_HTTPS so
// call sites are already HTTPS-shaped. When mbedTLS lands, only the transport helpers
// (connect/send/recv) below change -- the public API stays identical.
//
//   Example (REST):
//     char buf[32*1024];
//     HttpClient api;
//     api.user_agent("myapp/1.0").bearer(token).accept("application/json");
//     HttpResponse r = api.get("http://api.example.com/v1/items", buf, sizeof buf);
//     if (r.ok()) { /* parse r.body (NUL-terminated), r.body_len */ }
//     HttpResponse p = api.post_json("http://api.example.com/v1/items",
//                                    "{\"name\":\"x\"}", buf, sizeof buf);
//
#ifndef ONYX_HTTP_HPP
#define ONYX_HTTP_HPP

#include "kapi.h"

// Define ONYX_HTTP_TLS (and link the mbedTLS libs) BEFORE including this header to
// enable https://. Without it the class is fully freestanding and https:// returns
// HTTP_ERR_HTTPS. The TLS glue (transport) lives in user/tls/onyx_tls.hpp.
#ifdef ONYX_HTTP_TLS
#include "tls/onyx_tls.hpp"
#endif

// Transport / protocol errors are reported as a NEGATIVE HttpResponse::status.
enum HttpError
{
	HTTP_ERR_BAD_URL = -1,	// could not parse the URL / empty host
	HTTP_ERR_HTTPS   = -2,	// https:// requested but TLS not compiled in (no ONYX_HTTP_TLS)
	HTTP_ERR_NO_NET  = -3,	// the network link is down
	HTTP_ERR_CONNECT = -4,	// DNS or TCP connect failed
	HTTP_ERR_SEND    = -5,	// send() failed
	HTTP_ERR_TIMEOUT = -6,	// no response within the timeout
	HTTP_ERR_EMPTY   = -7,	// connection closed with no data
	HTTP_ERR_TLS     = -8,	// TLS handshake / protocol error
};

namespace http_detail
{
	inline int  slen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
	inline char lc (char c) { return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c; }

	// Append s (or n bytes of s) to d[*o], never overflowing cap; keeps d NUL-terminated.
	inline void cat (char *d, int cap, int *o, const char *s)
	{
		while (s && *s && *o + 1 < cap) d[(*o)++] = *s++;
		if (*o < cap) d[*o] = '\0';
	}
	inline void catn (char *d, int cap, int *o, const char *s, int n)
	{
		for (int i = 0; i < n && *o + 1 < cap; i++) d[(*o)++] = s[i];
		if (*o < cap) d[*o] = '\0';
	}
	inline void cati (char *d, int cap, int *o, long v)	// non-negative decimal
	{
		char t[20]; int n = 0;
		if (v < 0) v = 0;
		if (v == 0) t[n++] = '0';
		while (v) { t[n++] = (char) ('0' + v % 10); v /= 10; }
		while (n-- > 0 && *o + 1 < cap) d[(*o)++] = t[n];
		if (*o < cap) d[*o] = '\0';
	}

	// Case-insensitive lookup of a header in [hdr, hdr+hlen). Returns the value start
	// (trimmed) + its length via *vlen, or 0 if absent.
	inline const char *find (const char *hdr, int hlen, const char *name, int *vlen)
	{
		int nl = slen (name);
		int i = 0;
		while (i < hlen)
		{
			int j = 0;
			while (j < nl && i + j < hlen && lc (hdr[i + j]) == lc (name[j])) j++;
			if (j == nl && i + j < hlen && hdr[i + j] == ':')
			{
				int v = i + j + 1;
				while (v < hlen && (hdr[v] == ' ' || hdr[v] == '\t')) v++;
				int e = v;
				while (e < hlen && hdr[e] != '\r' && hdr[e] != '\n') e++;
				if (vlen) *vlen = e - v;
				return hdr + v;
			}
			while (i < hlen && hdr[i] != '\n') i++;	// next line
			i++;
		}
		if (vlen) *vlen = 0;
		return 0;
	}

	inline int parse_int (const char *s, int n)
	{
		int v = 0;
		for (int i = 0; i < n && s[i] >= '0' && s[i] <= '9'; i++) v = v * 10 + (s[i] - '0');
		return v;
	}

	// Case-insensitive: does [s,s+n) contain the word w?
	inline bool contains_ci (const char *s, int n, const char *w)
	{
		int wl = slen (w);
		for (int i = 0; i + wl <= n; i++)
		{
			int j = 0;
			while (j < wl && lc (s[i + j]) == lc (w[j])) j++;
			if (j == wl) return true;
		}
		return false;
	}

	// Decode HTTP chunked transfer-coding in place over [b, b+len). Returns new length.
	inline int dechunk (char *b, int len)
	{
		int r = 0, w = 0;
		while (r < len)
		{
			int sz = 0, any = 0;
			while (r < len && b[r] != '\r' && b[r] != '\n' && b[r] != ';')
			{
				char c = b[r++]; int d;
				if (c >= '0' && c <= '9') d = c - '0';
				else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
				else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
				else { any = 0; break; }
				sz = sz * 16 + d; any = 1;
			}
			while (r < len && b[r] != '\n') r++;	// skip chunk-ext + CR
			if (r < len) r++;			// skip LF
			if (!any || sz == 0) break;		// malformed or last chunk
			for (int i = 0; i < sz && r < len; i++) b[w++] = b[r++];
			if (r < len && b[r] == '\r') r++;	// trailing CRLF after data
			if (r < len && b[r] == '\n') r++;
		}
		return w;
	}

	inline bool starts_ci (const char *s, const char *pfx)
	{
		for (int i = 0; pfx[i]; i++) if (lc (s[i]) != lc (pfx[i])) return false;
		return true;
	}

	// Parse "[http(s)://]host[:port][/path]". Sets *https, *port, host, path("/").
	inline bool parse_url (const char *url, char *host, int hcap, unsigned *port,
			       char *path, int pcap, bool *https)
	{
		*https = false; *port = 80;
		const char *p = url;
		if (starts_ci (url, "https://")) { *https = true; *port = 443; p = url + 8; }
		else if (starts_ci (url, "http://")) { p = url + 7; }
		else for (const char *q = url; *q; q++)
			if (q[0] == ':' && q[1] == '/' && q[2] == '/') { p = q + 3; break; }

		int i = 0;
		while (*p && *p != ':' && *p != '/') { if (i + 1 < hcap) host[i++] = *p; p++; }
		host[i] = '\0';

		if (*p == ':')
		{
			p++; unsigned v = 0, any = 0;
			while (*p >= '0' && *p <= '9') { v = v * 10 + (unsigned) (*p - '0'); p++; any = 1; }
			if (any) *port = v;
		}

		int j = 0;
		if (*p != '/') { if (pcap > 1) path[j++] = '/'; }
		while (*p) { if (j + 1 < pcap) path[j++] = *p; p++; }
		path[j] = '\0';
		return host[0] != '\0';
	}

	// Resolve a redirect Location against the current absolute URL `base`, into out[cap].
	// Handles absolute (http(s)://), protocol-relative (//host/...), absolute-path (/...)
	// and path-relative Locations. Returns false on parse error.
	inline bool resolve_redirect (const char *base, const char *loc, char *out, int cap)
	{
		if (starts_ci (loc, "http://") || starts_ci (loc, "https://"))
		{ int o = 0; out[0] = '\0'; cat (out, cap, &o, loc); return out[0] != '\0'; }

		char host[160], path[1024]; unsigned port; bool https;
		if (!parse_url (base, host, (int) sizeof host, &port, path, (int) sizeof path, &https))
			return false;

		int o = 0; out[0] = '\0';
		if (loc[0] == '/' && loc[1] == '/')		// protocol-relative: //host/path
		{ cat (out, cap, &o, https ? "https:" : "http:"); cat (out, cap, &o, loc); return out[0] != '\0'; }

		cat (out, cap, &o, https ? "https://" : "http://");
		cat (out, cap, &o, host);
		if ((https && port != 443) || (!https && port != 80))
		{ cat (out, cap, &o, ":"); cati (out, cap, &o, (long) port); }

		if (loc[0] == '/')				// absolute path
		{ cat (out, cap, &o, loc); }
		else						// relative to the current path's directory
		{
			int cut = 0;
			for (int i = 0; path[i]; i++) if (path[i] == '/') cut = i + 1;
			catn (out, cap, &o, path, cut);		// up to and including the last '/'
			cat (out, cap, &o, loc);
		}
		return out[0] != '\0';
	}
}

// ---- transport: plain TCP, or TLS (mbedTLS) when ONYX_HTTP_TLS is defined --------
// A thin send/recv/close seam so HttpClient is oblivious to http vs https. recv()
// keeps the kapi_tcp_recv convention: >0 bytes, 0 = nothing yet (poll), <0 = closed.
struct Transport
{
	int  sock;	// kapi TCP handle
	bool tls;
#ifdef ONYX_HTTP_TLS
	onyx_tls::Session ssl;
#endif
};

namespace http_detail
{
	// Open the connection (and TLS handshake if requested). 0 ok, else an HttpError;
	// on connect failure *raw gets the raw kapi_tcp_connect code (for diagnostics).
	inline int tp_open (Transport &t, const char *host, unsigned port, bool tls, int *raw)
	{
		t.tls = tls;
		t.sock = kapi_tcp_connect (host, port);
		if (t.sock < 0) { if (raw) *raw = t.sock; return HTTP_ERR_CONNECT; }
#ifdef ONYX_HTTP_TLS
		if (tls && onyx_tls::start (t.ssl, t.sock, host) != 0)
		{ kapi_tcp_close (t.sock); return HTTP_ERR_TLS; }
#else
		(void) tls;
#endif
		return 0;
	}
	inline int tp_send (Transport &t, const void *b, int n)
	{
#ifdef ONYX_HTTP_TLS
		if (t.tls) return onyx_tls::send (t.ssl, b, n);
#endif
		return kapi_tcp_send (t.sock, b, (unsigned) n);
	}
	inline int tp_recv (Transport &t, void *b, int n)	// >0 / 0 = none yet / <0 = closed
	{
#ifdef ONYX_HTTP_TLS
		if (t.tls) return onyx_tls::recv (t.ssl, b, n);
#endif
		return kapi_tcp_recv (t.sock, b, (unsigned) n);
	}
	inline void tp_close (Transport &t)
	{
#ifdef ONYX_HTTP_TLS
		if (t.tls) onyx_tls::stop (t.ssl);
#endif
		kapi_tcp_close (t.sock);
	}
}

// A parsed HTTP response. body/hdr point INTO the caller's buffer; body is
// NUL-terminated. status is the HTTP code (e.g. 200, 404) or a negative HttpError.
struct HttpResponse
{
	int         status;
	bool        truncated;	// the response did not fit in the buffer
	const char *body;
	int         body_len;
	const char *hdr;	// header block (between the status line and the blank line)
	int         hdr_len;
	int         net_err;	// raw kapi_tcp_connect code when status==HTTP_ERR_CONNECT
				// (-1 no net, -2 too many sockets, -3 DNS fail, -5 connect); else 0

	bool ok (void)       const { return status >= 200 && status < 300; }
	bool is_error (void) const { return status < 0; }	// transport/protocol error

	// Copy a response header value (case-insensitive) into out[cap], NUL-terminated.
	// Returns the value length (may exceed cap-1 if truncated), or -1 if absent.
	int header (const char *name, char *out, int cap) const
	{
		int vl = 0;
		const char *v = http_detail::find (hdr, hdr_len, name, &vl);
		if (!v) { if (cap > 0) out[0] = '\0'; return -1; }
		int n = vl < cap - 1 ? vl : cap - 1;
		for (int i = 0; i < n; i++) out[i] = v[i];
		if (cap > 0) out[n] = '\0';
		return vl;
	}
};

// Reusable HTTP/1.1 client. Configure default headers once (chainable), then issue
// any number of requests. Stateless across calls except for the default headers.
class HttpClient
{
public:
	HttpClient (void) : m_hdrs_len (0), m_timeout (15000), m_ua ("Onyx/1.0")
	{
		m_hdrs[0] = '\0';
	}

	// --- configuration (chainable) ------------------------------------------
	HttpClient &user_agent (const char *ua) { m_ua = ua ? ua : "Onyx/1.0"; return *this; }
	HttpClient &timeout_ms (unsigned ms)    { m_timeout = ms; return *this; }
	void        reset_headers (void)        { m_hdrs_len = 0; m_hdrs[0] = '\0'; }

	// Add a default header line ("Name: Value") sent with every request.
	HttpClient &header (const char *name, const char *value)
	{
		using namespace http_detail;
		cat (m_hdrs, (int) sizeof m_hdrs, &m_hdrs_len, name);
		cat (m_hdrs, (int) sizeof m_hdrs, &m_hdrs_len, ": ");
		cat (m_hdrs, (int) sizeof m_hdrs, &m_hdrs_len, value);
		cat (m_hdrs, (int) sizeof m_hdrs, &m_hdrs_len, "\r\n");
		return *this;
	}
	HttpClient &bearer (const char *token)	// Authorization: Bearer <token>
	{
		using namespace http_detail;
		cat (m_hdrs, (int) sizeof m_hdrs, &m_hdrs_len, "Authorization: Bearer ");
		cat (m_hdrs, (int) sizeof m_hdrs, &m_hdrs_len, token);
		cat (m_hdrs, (int) sizeof m_hdrs, &m_hdrs_len, "\r\n");
		return *this;
	}
	HttpClient &accept (const char *type) { return header ("Accept", type); }

	// --- requests -----------------------------------------------------------
	HttpResponse get (const char *url, char *buf, int cap)
	{ return send ("GET", url, 0, 0, 0, buf, cap); }
	HttpResponse del (const char *url, char *buf, int cap)
	{ return send ("DELETE", url, 0, 0, 0, buf, cap); }
	HttpResponse post (const char *url, const char *ctype, const void *body, int len,
			   char *buf, int cap)
	{ return send ("POST", url, ctype, body, len, buf, cap); }
	HttpResponse put (const char *url, const char *ctype, const void *body, int len,
			  char *buf, int cap)
	{ return send ("PUT", url, ctype, body, len, buf, cap); }
	HttpResponse patch (const char *url, const char *ctype, const void *body, int len,
			    char *buf, int cap)
	{ return send ("PATCH", url, ctype, body, len, buf, cap); }

	// JSON convenience (Content-Type: application/json); `json` is a C string.
	HttpResponse post_json (const char *url, const char *json, char *buf, int cap)
	{ return send ("POST", url, "application/json", json, http_detail::slen (json), buf, cap); }
	HttpResponse put_json (const char *url, const char *json, char *buf, int cap)
	{ return send ("PUT", url, "application/json", json, http_detail::slen (json), buf, cap); }

	// The general request, following up to 8 HTTP 3xx redirects (resolving relative
	// Locations). 301/302/303 switch a non-GET to GET (curl's default); 307/308 keep
	// the method + body. Returns the FINAL response (or the last 3xx if it has no
	// Location / the hop limit is hit). body/hdr point into buf[cap]; <0 = HttpError.
	HttpResponse send (const char *method, const char *url, const char *ctype,
			   const void *body, int len, char *buf, int cap)
	{
		using namespace http_detail;
		char cur[2048]; int o = 0; cur[0] = '\0';
		cat (cur, (int) sizeof cur, &o, url);

		HttpResponse r = send_once (method, cur, ctype, body, len, buf, cap);
		for (int hop = 0; hop < 8; hop++)
		{
			if (r.status < 300 || r.status >= 400 || r.status == 304)
				return r;			/* not a redirect we follow */
			char loc[2048];
			if (r.header ("Location", loc, (int) sizeof loc) <= 0)
				return r;			/* 3xx without Location: hand it back */
			char next[2048];
			if (!resolve_redirect (cur, loc, next, (int) sizeof next))
				return r;
			o = 0; cur[0] = '\0';
			cat (cur, (int) sizeof cur, &o, next);
			if (r.status != 307 && r.status != 308)
			{ method = "GET"; body = 0; len = 0; ctype = 0; }	/* 301/302/303 -> GET */
			r = send_once (method, cur, ctype, body, len, buf, cap);
		}
		return r;					/* too many redirects */
	}

	// One request, no redirect handling. method = "GET"/"POST"/...; ctype/body optional.
	// The response is read into buf[cap]; body/hdr point into it. Negative status = HttpError.
	HttpResponse send_once (const char *method, const char *url, const char *ctype,
			   const void *body, int len, char *buf, int cap)
	{
		using namespace http_detail;

		HttpResponse r;
		r.status = 0; r.truncated = false;
		r.body = buf; r.body_len = 0; r.hdr = buf; r.hdr_len = 0; r.net_err = 0;
		if (cap < 16) { r.status = HTTP_ERR_BAD_URL; return r; }

		char host[160], path[1024]; unsigned port; bool https;
		if (!parse_url (url, host, (int) sizeof host, &port, path, (int) sizeof path, &https))
		{ r.status = HTTP_ERR_BAD_URL; return r; }
#ifndef ONYX_HTTP_TLS
		if (https) { r.status = HTTP_ERR_HTTPS; return r; }
#endif

		char ip[40];
		if (!kapi_net_status (ip, sizeof ip)) { r.status = HTTP_ERR_NO_NET; return r; }

		Transport tp;
		{ int orc = tp_open (tp, host, port, https, &r.net_err); if (orc != 0) { r.status = orc; return r; } }

		// Build the request line + headers.
		char req[2048]; int o = 0; req[0] = '\0';
		cat (req, (int) sizeof req, &o, method);
		cat (req, (int) sizeof req, &o, " ");
		cat (req, (int) sizeof req, &o, path);
		cat (req, (int) sizeof req, &o, " HTTP/1.1\r\nHost: ");
		cat (req, (int) sizeof req, &o, host);
		if (port != 80) { cat (req, (int) sizeof req, &o, ":"); cati (req, (int) sizeof req, &o, (long) port); }
		cat (req, (int) sizeof req, &o, "\r\nUser-Agent: ");
		cat (req, (int) sizeof req, &o, m_ua);
		cat (req, (int) sizeof req, &o, "\r\nConnection: close\r\n");
		catn (req, (int) sizeof req, &o, m_hdrs, m_hdrs_len);
		if (ctype && ctype[0])
		{ cat (req, (int) sizeof req, &o, "Content-Type: "); cat (req, (int) sizeof req, &o, ctype);
		  cat (req, (int) sizeof req, &o, "\r\n"); }
		if (body && len > 0)
		{ cat (req, (int) sizeof req, &o, "Content-Length: "); cati (req, (int) sizeof req, &o, len);
		  cat (req, (int) sizeof req, &o, "\r\n"); }
		cat (req, (int) sizeof req, &o, "\r\n");

		if (tp_send (tp, req, o) < 0)
		{ tp_close (tp); r.status = HTTP_ERR_SEND; return r; }
		if (body && len > 0 && tp_send (tp, body, len) < 0)
		{ tp_close (tp); r.status = HTTP_ERR_SEND; return r; }

		// Read until the server closes, the buffer fills, or we idle past the timeout.
		int total = 0;
		unsigned start = kapi_get_ticks ();
		for (;;)
		{
			if (total >= cap - 1) { r.truncated = true; break; }
			int n = tp_recv (tp, buf + total, cap - 1 - total);
			if (n > 0) { total += n; start = kapi_get_ticks (); }
			else if (n == 0)
			{
				if (kapi_get_ticks () - start > m_timeout)
				{
					if (total == 0)
					{ tp_close (tp); r.status = HTTP_ERR_TIMEOUT; return r; }
					break;
				}
				kapi_msleep (5);
			}
			else break;	// connection closed -> response complete
		}
		buf[total] = '\0';
		tp_close (tp);
		if (total == 0) { r.status = HTTP_ERR_EMPTY; return r; }

		// Status line: "HTTP/1.x NNN reason".
		int i = 0;
		while (i < total && buf[i] != ' ') i++;
		int code = 0;
		if (i < total) { i++; while (i < total && buf[i] >= '0' && buf[i] <= '9') { code = code * 10 + (buf[i] - '0'); i++; } }
		r.status = code;

		// Header block = after the status-line LF, up to the blank line.
		int hs = 0; while (hs < total && buf[hs] != '\n') hs++; if (hs < total) hs++;
		int he = -1;
		for (int k = hs; k + 3 < total; k++)
			if (buf[k] == '\r' && buf[k+1] == '\n' && buf[k+2] == '\r' && buf[k+3] == '\n') { he = k; break; }
		if (he < 0) { r.hdr = buf + hs; r.hdr_len = total - hs; r.body = buf + total; r.body_len = 0; return r; }
		r.hdr = buf + hs; r.hdr_len = he - hs;

		int bstart = he + 4;
		int blen = total - bstart;

		int vl = 0;
		const char *te = find (r.hdr, r.hdr_len, "Transfer-Encoding", &vl);
		if (te && contains_ci (te, vl, "chunked"))
			blen = dechunk (buf + bstart, blen);
		else
		{
			const char *cl = find (r.hdr, r.hdr_len, "Content-Length", &vl);
			if (cl) { int n = parse_int (cl, vl); if (n >= 0 && n < blen) blen = n; }
		}

		r.body = buf + bstart;
		r.body_len = blen;
		buf[bstart + blen] = '\0';	// bstart+blen <= total <= cap-1
		return r;
	}

private:
	char     m_hdrs[1024];	// concatenated default header lines
	int      m_hdrs_len;
	unsigned m_timeout;	// idle timeout in ms
	const char *m_ua;
};

#endif // ONYX_HTTP_HPP
