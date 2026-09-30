//
// net.h -- Courier's HTTP engine: a request prepared (its variables resolved, its auth, its body made:
// raw, x-www-form-urlencoded, multipart form-data with files, a binary file; the cookies of the jar),
// sent on a thread of its own (kapi v67; the UI keeps running, Cancel works) over the kapi TCP
// sockets, or TLS (mbedTLS: user/tls/onyx_tls.hpp) for https://; the response read whole (any size,
// up to a limit), chunked and gzip / deflate decoded, the redirects followed (the cookies they set
// kept), timed (connect, first byte, total).
//
#ifndef _courier_net_h
#define _courier_net_h

#ifndef COURIER_NO_TLS
#define ONYX_HTTP_TLS
#endif
#include "http.hpp"			// the Transport (plain / TLS)
#include "img/imgload.hpp"		// img_inflate (gzip, deflate)
#include "vars.h"

namespace cr {

static const char *const USER_AGENT = "Courier/1.0 (Onyx)";
enum { RESP_MAX = 16 << 20 };		// a response bigger than that is cut (truncated)

// ---- the cookie jar ------------------------------------------------------------------------------------------
struct Cookie
{
	Str domain, path, name, value;
	long long expires;			// unix seconds, 0: a session cookie
	bool secure, hostOnly, httpOnly;
	Cookie () : expires (0), secure (false), hostOnly (true), httpOnly (false) {}
};
struct CookieJar
{
	Vec<Cookie> list;
	bool dirty;
	CookieJar () : dirty (false) {}
	static bool domainMatch (const char *host, const Cookie &c)
	{
		if (c.hostOnly) return s_eqi (host, c.domain.c ());
		int hl = (int) strlen (host), dl = c.domain.len ();
		if (hl < dl) return false;
		if (!s_eqi (host + hl - dl, c.domain.c ())) return false;
		return hl == dl || host[hl - dl - 1] == '.';
	}
	static bool pathMatch (const char *path, const Cookie &c)
	{
		int pl = c.path.len ();
		if (pl == 0 || c.path.eq ("/")) return true;
		if (strncmp (path, c.path.c (), pl)) return false;
		return path[pl] == 0 || path[pl] == '/' || path[pl] == '?' || c.path.c ()[pl - 1] == '/';
	}
	// the Cookie header for a request (empty: none)
	void header (Str &out, const char *host, const char *path, bool https)
	{
		long long now = now_unix ();
		for (int i = 0; i < list.size (); i++)
		{
			const Cookie &c = list[i];
			if (c.expires && c.expires < now) continue;
			if (c.secure && !https) continue;
			if (!domainMatch (host, c) || !pathMatch (path, c)) continue;
			if (!out.empty ()) out.add ("; ");
			out.add (c.name.c ()); out.add ('='); out.add (c.value.c ());
		}
	}
	// a Set-Cookie header's value, from a response of host / path
	void set (const char *v, const char *host, const char *path, Cookie *parsed = 0)
	{
		Cookie c;
		const char *p = v;
		const char *e = p; while (*e && *e != ';') e++;
		const char *eq = p; while (eq < e && *eq != '=') eq++;
		if (eq >= e) return;
		s_trim (c.name, p, (int) (eq - p)); s_trim (c.value, eq + 1, (int) (e - eq - 1));
		c.domain = host;
		{ Str dp; const char *q = strchr (path, '?'); int n = q ? (int) (q - path) : (int) strlen (path);
		  dp.set (path, n); int sl = -1; for (int i = 0; i < dp.len (); i++) if (dp.c ()[i] == '/') sl = i;
		  if (sl <= 0) c.path = "/"; else c.path.set (dp.c (), sl); }
		long long maxAge = -1;
		while (*e == ';')
		{
			p = e + 1; while (*p == ' ') p++;
			e = p; while (*e && *e != ';') e++;
			const char *q = p; while (q < e && *q != '=') q++;
			Str k, val; s_trim (k, p, (int) (q - p)); if (q < e) s_trim (val, q + 1, (int) (e - q - 1));
			if (s_eqi (k.c (), "domain") && !val.empty ())
			{ const char *d = val.c (); if (*d == '.') d++; c.domain = d; c.hostOnly = false; }
			else if (s_eqi (k.c (), "path") && !val.empty ()) c.path = val;
			else if (s_eqi (k.c (), "secure")) c.secure = true;
			else if (s_eqi (k.c (), "httponly")) c.httpOnly = true;
			else if (s_eqi (k.c (), "max-age")) maxAge = atoll (val.c ());
			else if (s_eqi (k.c (), "expires")) c.expires = parse_http_date (val.c ());
		}
		if (maxAge >= 0) c.expires = maxAge == 0 ? 1 : now_unix () + maxAge;
		if (parsed) *parsed = c;
		for (int i = 0; i < list.size (); i++)
			if (list[i].name.eq (c.name.c ()) && s_eqi (list[i].domain.c (), c.domain.c ()) && list[i].path.eq (c.path.c ()))
			{
				if (c.expires && c.expires < now_unix ()) list.remove (i); else list[i] = c;
				dirty = true;
				return;
			}
		if (c.expires && c.expires < now_unix ()) return;
		list.push (c); dirty = true;
	}
	// "Wed, 21 Oct 2026 07:28:00 GMT" -> unix seconds (0: not understood)
	static long long parse_http_date (const char *s)
	{
		static const char *const MON[] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };
		int d = 0, y = 0, h = 0, mi = 0, se = 0, mo = -1;
		const char *p = s;
		while (*p && !(*p >= '0' && *p <= '9')) p++;
		d = atoi (p); while (*p >= '0' && *p <= '9') p++;
		while (*p == ' ' || *p == '-') p++;
		for (int i = 0; i < 12; i++) if (s_eqin (p, MON[i], 3)) mo = i + 1;
		while (*p && *p != ' ' && *p != '-') p++;
		while (*p == ' ' || *p == '-') p++;
		y = atoi (p); if (y < 100) y += y < 70 ? 2000 : 1900;
		while (*p >= '0' && *p <= '9') p++;
		while (*p == ' ') p++;
		sscanf (p, "%d:%d:%d", &h, &mi, &se);
		if (mo < 0 || !d || !y) return 0;
		return (long long) days_from_civil (y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
	}
};

// ---- a request ready to send, and what came back ---------------------------------------------------------------
struct Prepared
{
	Str method, url;
	KVList headers;			// what is sent (resolved; the defaults, the auth, the content type)
	char *body; int bodyLen;
	bool follow; int timeoutMs;
	Str error;			// could not be prepared (a file missing): not sent
	Prepared () : body (0), bodyLen (0), follow (true), timeoutMs (30000) {}
	~Prepared () { free (body); }
private:
	Prepared (const Prepared &); Prepared &operator= (const Prepared &);
};

struct Response
{
	int status;			// the HTTP status, or < 0: it failed (error says why)
	Str reason, error, httpVersion;
	KVList headers;
	char *body; int bodyLen;	// the body, decoded (NUL-terminated)
	int wireLen, headLen;		// bytes received (compressed), the headers' size
	int msTotal, msConnect, msFirst;
	Str finalUrl;
	Str console;			// the exchange as text: each hop's request and response headers
	Vec<Cookie> setCookies;		// the cookies this response set
	bool truncated, cancelled;
	int redirects;
	Response () : status (0), body (0), bodyLen (0), wireLen (0), headLen (0), msTotal (0), msConnect (0), msFirst (0),
		      truncated (false), cancelled (false), redirects (0) {}
	~Response () { free (body); }
	const char *header (const char *name) const
	{
		for (int i = 0; i < headers.size (); i++) if (s_eqi (headers[i].key.c (), name)) return headers[i].value.c ();
		return 0;
	}
private:
	Response (const Response &); Response &operator= (const Response &);
};

// ---- a growing byte buffer -----------------------------------------------------------------------------------------
struct Buf
{
	char *p; int n, cap;
	Buf () : p (0), n (0), cap (0) {}
	~Buf () { free (p); }
	bool reserve (int need)
	{
		if (need + 1 <= cap) return true;
		int c = cap ? cap : 16384;
		while (c < need + 1) c *= 2;
		char *q = (char *) realloc (p, c);
		if (!q) return false;
		p = q; cap = c;
		return true;
	}
	void add (const void *d, int len) { if (len <= 0 || !reserve (n + len)) return; memcpy (p + n, d, len); n += len; p[n] = 0; }
	void add (const char *s) { add (s, (int) strlen (s)); }
	char *take () { char *q = p; p = 0; n = cap = 0; return q; }
};

// ---- the URL -----------------------------------------------------------------------------------------------------
struct Url
{
	bool https; unsigned port; Str host, path;	// path: with its query, without the fragment
	bool parse (const char *u)
	{
		https = false; port = 80;
		const char *p = u;
		while (*p == ' ') p++;
		if (s_startsi (p, "https://")) { https = true; port = 443; p += 8; }
		else if (s_startsi (p, "http://")) p += 7;
		else if (strstr (p, "://")) return false;	// (another scheme)
		const char *h = p;
		while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#') p++;
		host.set (h, (int) (p - h));
		if (*p == ':')
		{
			p++; unsigned v = 0; bool any = false;
			while (*p >= '0' && *p <= '9') { v = v * 10 + (unsigned) (*p - '0'); p++; any = true; }
			if (any) port = v;
		}
		path.clear ();
		if (*p != '/') path.add ('/');
		const char *e = p; while (*e && *e != '#') e++;
		Str rest; rest.set (p, (int) (e - p));
		url_encode_soft (path, rest.c ());
		return !host.empty ();
	}
};

// ---- preparing -------------------------------------------------------------------------------------------------------
static inline void set_header (KVList &h, const char *k, const char *v, bool replace = true)
{
	for (int i = 0; i < h.size (); i++)
		if (s_eqi (h[i].key.c (), k)) { if (replace) h[i].value = v; return; }
	h.push (KV (k, v));
}
static inline bool has_header (const KVList &h, const char *k)
{
	for (int i = 0; i < h.size (); i++) if (s_eqi (h[i].key.c (), k)) return true;
	return false;
}
// the auth that applies to a request: its own, or its folders', or its collection's
static inline const Auth &effective_auth (const Request &r, const Item *it, const Collection *c)
{
	if (r.auth.type != AUTH_INHERIT) return r.auth;
	for (const Item *p = it ? it->parent : 0; p; p = p->parent)
		if (p->parent && p->auth.type != AUTH_INHERIT) return p->auth;		// (a folder's)
	static Auth none; none.type = AUTH_NONE;
	return c ? c->auth : none;
}
static inline const char *mime_of (const char *path)
{
	const char *e = strrchr (path, '.');
	if (!e) return "application/octet-stream";
	e++;
	if (s_eqi (e, "json")) return "application/json";
	if (s_eqi (e, "txt")) return "text/plain";
	if (s_eqi (e, "png")) return "image/png";
	if (s_eqi (e, "jpg") || s_eqi (e, "jpeg")) return "image/jpeg";
	if (s_eqi (e, "gif")) return "image/gif";
	if (s_eqi (e, "xml")) return "application/xml";
	if (s_eqi (e, "html") || s_eqi (e, "htm")) return "text/html";
	if (s_eqi (e, "csv")) return "text/csv";
	if (s_eqi (e, "pdf")) return "application/pdf";
	return "application/octet-stream";
}
// The request, its variables resolved, made ready to send (the cookies of `jar`).
static inline void prepare (Prepared &p, const Request &r, const Scope &sc, const Auth &auth, CookieJar &jar)
{
	Str v;
	p.method = r.method.empty () ? "GET" : r.method.c ();
	for (int i = 0; i < p.method.len (); i++) if (p.method.c ()[i] >= 'a' && p.method.c ()[i] <= 'z') p.method.data ()[i] -= 32;
	resolve (v, sc, r.url.c ());
	Str url; s_trim (url, v.c ());
	if (!url.empty () && !strstr (url.c (), "://")) { Str t ("http://"); t.add (url.c ()); url = t; }
	p.follow = r.followRedirects;
	p.timeoutMs = r.timeoutMs > 0 ? r.timeoutMs : 30000;
	// the headers: the request's, enabled, resolved
	for (int i = 0; i < r.headers.size (); i++)
	{
		const KV &h = r.headers[i];
		if (!h.on || h.key.empty ()) continue;
		KV o; resolve (o.key, sc, h.key.c ()); resolve (o.value, sc, h.value.c ());
		Str k; s_trim (k, o.key.c ()); o.key = k;
		p.headers.push (o);
	}
	// the auth
	switch (auth.type)
	{
	case AUTH_BEARER:
		resolve (v, sc, auth.token.c ());
		{ Str h ("Bearer "); h.add (v.c ()); set_header (p.headers, "Authorization", h.c (), false); }
		break;
	case AUTH_BASIC:
	{
		Str u, pw, both, h ("Basic ");
		resolve (u, sc, auth.user.c ()); resolve (pw, sc, auth.pass.c ());
		both.add (u.c ()); both.add (':'); both.add (pw.c ());
		base64 (h, (const unsigned char *) both.c (), both.len ());
		set_header (p.headers, "Authorization", h.c (), false);
		break;
	}
	case AUTH_APIKEY:
	{
		Str k, val; resolve (k, sc, auth.key.c ()); resolve (val, sc, auth.value.c ());
		if (k.empty ()) break;
		if (auth.inQuery)
		{
			Str q; url_encode (q, k.c ()); q.add ('='); url_encode (q, val.c ());
			url.add (strchr (url.c (), '?') ? '&' : '?'); url.add (q.c ());
		}
		else set_header (p.headers, k.c (), val.c (), false);
		break;
	}
	}
	p.url = url;
	// the body
	Buf b; const char *ctype = 0; Str ctBuf;
	if (r.bodyMode == BODY_RAW)
	{
		resolve (v, sc, r.raw.c ());
		b.add (v.c (), v.len ());
		ctype = RAW_CTYPE[r.rawLang];
	}
	else if (r.bodyMode == BODY_URLENC)
	{
		Str s;
		for (int i = 0; i < r.urlenc.size (); i++)
		{
			const KV &kv = r.urlenc[i];
			if (!kv.on || kv.key.empty ()) continue;
			Str k, val; resolve (k, sc, kv.key.c ()); resolve (val, sc, kv.value.c ());
			if (!s.empty ()) s.add ('&');
			url_encode (s, k.c (), true); s.add ('='); url_encode (s, val.c (), true);
		}
		b.add (s.c (), s.len ());
		ctype = "application/x-www-form-urlencoded";
	}
	else if (r.bodyMode == BODY_FORM)
	{
		Str boundary ("----CourierFormBoundary");
		for (int i = 0; i < 16; i++) boundary.add ("0123456789abcdefghijklmnopqrstuvwxyz"[rnd32 () % 36]);
		for (int i = 0; i < r.form.size (); i++)
		{
			const KV &kv = r.form[i];
			if (!kv.on || kv.key.empty ()) continue;
			Str k, val; resolve (k, sc, kv.key.c ()); resolve (val, sc, kv.value.c ());
			b.add ("--"); b.add (boundary.c ()); b.add ("\r\n");
			if (kv.file)
			{
				int n = 0; char *f = read_file (val.c (), &n);
				if (!f) { p.error.addf ("The file of the form's field \"%s\" cannot be read: %s", k.c (), val.c ()); return; }
				Str d; d.addf ("Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\nContent-Type: %s\r\n\r\n", k.c (), base_name (val.c ()), mime_of (val.c ()));
				b.add (d.c ()); b.add (f, n); b.add ("\r\n");
				free (f);
			}
			else
			{
				Str d; d.addf ("Content-Disposition: form-data; name=\"%s\"\r\n\r\n", k.c ());
				b.add (d.c ()); b.add (val.c (), val.len ()); b.add ("\r\n");
			}
		}
		b.add ("--"); b.add (boundary.c ()); b.add ("--\r\n");
		ctBuf.add ("multipart/form-data; boundary="); ctBuf.add (boundary.c ());
		ctype = ctBuf.c ();
	}
	else if (r.bodyMode == BODY_BINARY && !r.binary.empty ())
	{
		resolve (v, sc, r.binary.c ());
		int n = 0; char *f = read_file (v.c (), &n);
		if (!f) { p.error.addf ("The body's file cannot be read: %s", v.c ()); return; }
		b.add (f, n); free (f);
		ctype = mime_of (v.c ());
	}
	if (ctype && !has_header (p.headers, "Content-Type")) p.headers.push (KV ("Content-Type", ctype));
	p.bodyLen = b.n; p.body = b.take ();
	// the defaults (as Postman sends), unless the request has its own
	set_header (p.headers, "User-Agent", USER_AGENT, false);
	set_header (p.headers, "Accept", "*/*", false);
	set_header (p.headers, "Accept-Encoding", "gzip, deflate", false);
	// the jar's cookies (unless the request sets its own Cookie header)
	if (!has_header (p.headers, "Cookie"))
	{
		Url u;
		if (u.parse (url.c ()))
		{
			Str c; jar.header (c, u.host.c (), u.path.c (), u.https);
			if (!c.empty ()) p.headers.push (KV ("Cookie", c.c ()));
		}
	}
}

// ---- decoding ---------------------------------------------------------------------------------------------------
// the chunked body b[0..n) decoded in place -> its length
static inline int dechunk (char *b, int n)
{
	int r = 0, w = 0;
	while (r < n)
	{
		int sz = 0, any = 0;
		while (r < n && hexval (b[r]) >= 0) { sz = sz * 16 + hexval (b[r]); r++; any = 1; }
		while (r < n && b[r] != '\n') r++;
		if (r < n) r++;
		if (!any || sz == 0) break;
		if (sz > n - r) sz = n - r;
		memmove (b + w, b + r, sz); w += sz; r += sz;
		if (r < n && b[r] == '\r') r++;
		if (r < n && b[r] == '\n') r++;
	}
	return w;
}
// gzip / deflate -> a malloc'd buffer (0: could not)
static inline char *inflate_body (const char *b, int n, bool gzip, int *outLen)
{
	const unsigned char *d = (const unsigned char *) b;
	int off = 0; bool zlib = false;
	if (gzip)
	{
		if (n < 18 || d[0] != 0x1F || d[1] != 0x8B) return 0;
		int flg = d[3]; off = 10;
		if (flg & 4) { if (off + 2 > n) return 0; off += 2 + (d[off] | d[off + 1] << 8); }
		if (flg & 8) { while (off < n && d[off]) off++; off++; }
		if (flg & 16) { while (off < n && d[off]) off++; off++; }
		if (flg & 2) off += 2;
		if (off >= n) return 0;
	}
	else zlib = n >= 2 && (d[0] & 0x0F) == 8 && ((d[0] << 8) | d[1]) % 31 == 0;
	unsigned len = 0;
	unsigned char *o = img_inflate (d + off, (unsigned) (n - off), zlib, &len);
	if (!o) return 0;
	char *r = (char *) malloc (len + 1);
	if (r) { memcpy (r, o, len); r[len] = 0; *outLen = (int) len; }
	delete [] o;
	return r;
}

// ---- sending --------------------------------------------------------------------------------------------------------
static inline unsigned now_ms () { return kapi_clock_us () / 1000; }

// One exchange (no redirect). The response's head parsed into resp; its body in body (raw bytes).
// Returns false when it failed (resp.status < 0, resp.error set).
static inline bool exchange (const char *method, const char *url, const KVList &headers, const char *body, int bodyLen,
			     int timeoutMs, volatile int *cancel, Response &resp, Buf &raw, int *hdrEnd, bool first)
{
	using namespace http_detail;
	Url u;
	if (!u.parse (url)) { resp.status = -1; resp.error.addf ("The URL is not valid: %s", url); return false; }
#ifndef ONYX_HTTP_TLS
	if (u.https) { resp.status = -2; resp.error = "https:// is not available in this build (no TLS)."; return false; }
#endif
	char ip[40];
	if (!kapi_net_status (ip, sizeof ip)) { resp.status = -3; resp.error = "The network is down (no link, no address)."; return false; }

	// the request's head
	Str head;
	head.addf ("%s %s HTTP/1.1\r\n", method, u.path.c ());
	head.add ("Host: "); head.add (u.host.c ());
	if ((u.https && u.port != 443) || (!u.https && u.port != 80)) head.addf (":%u", u.port);
	head.add ("\r\n");
	for (int i = 0; i < headers.size (); i++)
	{
		if (s_eqi (headers[i].key.c (), "Host") || s_eqi (headers[i].key.c (), "Content-Length") || s_eqi (headers[i].key.c (), "Connection")) continue;
		head.add (headers[i].key.c ()); head.add (": "); head.add (headers[i].value.c ()); head.add ("\r\n");
	}
	if (bodyLen > 0 || s_eq (method, "POST") || s_eq (method, "PUT") || s_eq (method, "PATCH")) head.addf ("Content-Length: %d\r\n", bodyLen);
	head.add ("Connection: close\r\n\r\n");

	resp.console.addf ("%s %s\r\n", method, url);
	resp.console.add (head.c ());
	if (bodyLen > 0)
	{
		bool text = true;
		for (int i = 0; i < bodyLen && i < 4096; i++) { unsigned char c = (unsigned char) body[i]; if (c < 9 || (c > 13 && c < 32)) text = false; }
		if (text) { resp.console.add (body, imin (bodyLen, 8192)); if (bodyLen > 8192) resp.console.addf ("\n... (%d bytes in all)", bodyLen); }
		else resp.console.addf ("(%d bytes of binary data)", bodyLen);
		resp.console.add ("\r\n\r\n");
	}

	unsigned t0 = now_ms ();
	Transport tp;
	int rawErr = 0;
	int orc = tp_open (tp, u.host.c (), u.port, u.https, &rawErr);
	if (orc != 0)
	{
		resp.status = orc;
		if (orc == HTTP_ERR_TLS) resp.error.addf ("The TLS handshake with %s failed.", u.host.c ());
		else resp.error.addf ("Could not connect to %s:%u (error %d): the host is unknown, refused the connection or did not answer.", u.host.c (), u.port, rawErr);
		return false;
	}
	if (first) resp.msConnect = (int) (now_ms () - t0);
	if (tp_send (tp, head.c (), head.len ()) < 0 || (bodyLen > 0 && tp_send (tp, body, bodyLen) < 0))
	{
		tp_close (tp); resp.status = HTTP_ERR_SEND; resp.error = "The request could not be sent (the connection closed)."; return false;
	}
	// read: until the server closes, the body is complete (Content-Length, the last chunk), or the timeout
	raw.n = 0;
	unsigned last = now_ms ();
	bool gotFirst = false;
	int he = -1; long long want = -1; bool chunked = false, noBody = s_eq (method, "HEAD");
	char tmp[8192];
	for (;;)
	{
		if (cancel && *cancel) { tp_close (tp); resp.status = -9; resp.cancelled = true; resp.error = "Cancelled."; return false; }
		if (raw.n >= RESP_MAX) { resp.truncated = true; break; }
		int n = tp_recv (tp, tmp, imin ((int) sizeof tmp, RESP_MAX - raw.n));
		if (n > 0)
		{
			if (!gotFirst) { gotFirst = true; if (first) resp.msFirst = (int) (now_ms () - t0); }
			raw.add (tmp, n); last = now_ms ();
			if (he < 0)
			{
				for (int k = imax (0, raw.n - n - 3); k + 3 < raw.n; k++)
					if (raw.p[k] == '\r' && raw.p[k + 1] == '\n' && raw.p[k + 2] == '\r' && raw.p[k + 3] == '\n') { he = k; break; }
				if (he >= 0)
				{
					// the status, then what says where the body ends
					int code = 0; const char *sp = strchr (raw.p, ' ');
					if (sp) code = atoi (sp + 1);
					if (code == 204 || code == 304 || (code >= 100 && code < 200)) noBody = true;
					int vl = 0;
					const char *te = find (raw.p, he, "Transfer-Encoding", &vl);
					if (te && contains_ci (te, vl, "chunked")) chunked = true;
					const char *cl = find (raw.p, he, "Content-Length", &vl);
					if (cl && !chunked) want = parse_int (cl, vl);
					if (code >= 100 && code < 200 && code != 101)	// (an interim response: skip it)
					{
						int rest = raw.n - (he + 4);
						memmove (raw.p, raw.p + he + 4, rest); raw.n = rest; raw.p[raw.n] = 0;
						he = -1; noBody = s_eq (method, "HEAD"); chunked = false; want = -1;
						continue;
					}
				}
			}
			if (he >= 0)
			{
				int got = raw.n - (he + 4);
				if (noBody) break;
				if (want >= 0 && got >= want) break;
				if (chunked && got >= 5 && !memcmp (raw.p + raw.n - 5, "0\r\n\r\n", 5)) break;
			}
		}
		else if (n == 0)
		{
			if ((int) (now_ms () - last) > timeoutMs)
			{
				if (raw.n == 0 || he < 0)
				{
					tp_close (tp); resp.status = HTTP_ERR_TIMEOUT;
					resp.error.addf ("No response within %d ms (the timeout: the request's Settings).", timeoutMs);
					return false;
				}
				break;
			}
			kapi_msleep (2);
		}
		else break;			// the server closed: the response is complete
	}
	tp_close (tp);
	if (raw.n == 0) { resp.status = HTTP_ERR_EMPTY; resp.error = "The server closed the connection without an answer."; return false; }
	if (he < 0) he = raw.n;
	*hdrEnd = he;
	resp.wireLen += raw.n;
	// the status line and the headers
	resp.headers.clear ();
	const char *s = raw.p;
	const char *eol = strchr (s, '\r'); if (!eol || eol - s > he) eol = s + he;
	{
		const char *sp = s; while (sp < eol && *sp != ' ') sp++;
		resp.httpVersion.set (s, (int) (sp - s));
		resp.status = atoi (sp);
		const char *rs = sp; while (rs < eol && *rs == ' ') rs++;
		while (rs < eol && *rs >= '0' && *rs <= '9') rs++;
		while (rs < eol && *rs == ' ') rs++;
		resp.reason.set (rs, (int) (eol - rs));
		if (resp.status <= 0) { resp.status = -7; resp.error = "The answer is not HTTP."; return false; }
	}
	const char *p = eol;
	while (p < s + he)
	{
		while (p < s + he && (*p == '\r' || *p == '\n')) p++;
		const char *e = p; while (e < s + he && *e != '\r' && *e != '\n') e++;
		const char *c = p; while (c < e && *c != ':') c++;
		if (c < e) { KV kv; s_trim (kv.key, p, (int) (c - p)); s_trim (kv.value, c + 1, (int) (e - c - 1)); resp.headers.push (kv); }
		p = e;
	}
	resp.headLen = he + 4;
	resp.console.add (raw.p, imin (he + 4, raw.n));
	resp.console.add ("\r\n");
	return true;
}

// The whole request (the redirects followed if asked), into resp. The jar gets the cookies set.
static inline void perform (const Prepared &p, CookieJar &jar, volatile int *cancel, Response &resp)
{
	if (!p.error.empty ()) { resp.status = -10; resp.error = p.error; return; }
	if (p.url.empty ()) { resp.status = -1; resp.error = "Enter a URL to send the request to."; return; }
	unsigned t0 = now_ms ();
	Str url = p.url, method = p.method;
	KVList headers = p.headers;
	const char *body = p.body; int bodyLen = p.bodyLen;
	Buf raw; int he = 0;
	for (int hop = 0; ; hop++)
	{
		if (!exchange (method.c (), url.c (), headers, body, bodyLen, p.timeoutMs, cancel, resp, raw, &he, hop == 0)) break;
		// the cookies set
		Url u; u.parse (url.c ());
		for (int i = 0; i < resp.headers.size (); i++)
			if (s_eqi (resp.headers[i].key.c (), "Set-Cookie"))
			{
				Cookie c; jar.set (resp.headers[i].value.c (), u.host.c (), u.path.c (), &c);
				resp.setCookies.push (c);
			}
		const char *loc = resp.header ("Location");
		bool redirect = resp.status >= 300 && resp.status < 400 && resp.status != 304 && loc && loc[0];
		if (redirect && p.follow && hop < 10)
		{
			char next[2048];
			if (!http_detail::resolve_redirect (url.c (), loc, next, (int) sizeof next)) redirect = false;
			else
			{
				resp.console.addf ("-> redirected (%d) to %s\r\n\r\n", resp.status, next);
				if (resp.status != 307 && resp.status != 308) { if (!method.eq ("HEAD")) method = "GET"; body = 0; bodyLen = 0; }
				url = next;
				resp.redirects++;
				// the new host's cookies (not the old ones), no body headers after a switch to GET
				for (int i = headers.size () - 1; i >= 0; i--)
					if (s_eqi (headers[i].key.c (), "Cookie") || (!body && s_eqi (headers[i].key.c (), "Content-Type"))) headers.remove (i);
				Url nu;
				if (nu.parse (url.c ())) { Str c; jar.header (c, nu.host.c (), nu.path.c (), nu.https); if (!c.empty ()) headers.push (KV ("Cookie", c.c ())); }
				continue;
			}
		}
		// the body: the chunks joined, then inflated
		int bstart = he + 4;
		int blen = raw.n > bstart ? raw.n - bstart : 0;
		char *b = raw.p + (raw.n > bstart ? bstart : raw.n);
		const char *te = resp.header ("Transfer-Encoding");
		if (te && s_findi (te, (int) strlen (te), "chunked") >= 0) blen = dechunk (b, blen);
		else { const char *cl = resp.header ("Content-Length"); if (cl) { int n = atoi (cl); if (n >= 0 && n < blen) blen = n; } }
		const char *ce = resp.header ("Content-Encoding");
		char *dec = 0; int dlen = 0;
		if (ce && blen > 0 && (s_eqi (ce, "gzip") || s_eqi (ce, "x-gzip") || s_eqi (ce, "deflate")))
			dec = inflate_body (b, blen, !s_eqi (ce, "deflate"), &dlen);
		if (dec) { resp.body = dec; resp.bodyLen = dlen; }
		else
		{
			resp.body = (char *) malloc (blen + 1);
			if (resp.body) { memcpy (resp.body, b, blen); resp.body[blen] = 0; resp.bodyLen = blen; }
		}
		break;
	}
	resp.finalUrl = url;
	resp.msTotal = (int) (now_ms () - t0);
}

// ---- the job: a request on a thread ------------------------------------------------------------------------------
struct Job
{
	Prepared prep;
	Response resp;
	CookieJar jar;				// a copy of the app's, updated by the job
	volatile int cancel;
	volatile int done;
	int tid;
	void (*onDone) (Job *);			// run by the UI's pump (kapi_post) once the job is done
	void *user;
	Job () : cancel (0), done (0), tid (0), onDone (0), user (0) {}
};
static void job_posted (void *ctx, long) { Job *j = (Job *) ctx; if (j->tid > 0) kapi_thread_join (j->tid, 0, 0); if (j->onDone) j->onDone (j); }
static int job_thread (void *arg)
{
	Job *j = (Job *) arg;
	perform (j->prep, j->jar, &j->cancel, j->resp);
	j->done = 1;
	kapi_post (job_posted, j, 0);
	return 0;
}
// start it: on a thread when the kernel has them (true), else done here and now (false; onDone called)
static inline bool job_start (Job *j)
{
	int tid = kapi_thread_create (job_thread, j, 512 * 1024, "courier-http");
	if (tid > 0) { j->tid = tid; return true; }
	j->tid = 0;
	perform (j->prep, j->jar, &j->cancel, j->resp);
	j->done = 1;
	if (j->onDone) j->onDone (j);
	return false;
}

} // namespace cr

#endif
