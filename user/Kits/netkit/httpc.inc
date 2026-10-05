//
// httpc.inc -- the code of httpc.h (NetKit: compiled into netkit.so; a PC build has it inline).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifdef NK_IMPL
#define NK_FN	extern "C"
#else
#define NK_FN	static inline
#endif
NK_FN int hc__len (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
NK_FN void hc__cat (char *d, unsigned cap, unsigned *o, const char *s)
{
	while (s && *s && *o + 1 < cap) d[(*o)++] = *s++;
	if (*o < cap) d[*o] = '\0';
}
NK_FN void hc__cat_uint (char *d, unsigned cap, unsigned *o, unsigned v)
{
	char tmp[12]; int n = 0;
	if (v == 0) tmp[n++] = '0';
	while (v) { tmp[n++] = (char) ('0' + v % 10); v /= 10; }
	while (n-- > 0 && *o + 1 < cap) d[(*o)++] = tmp[n];
	if (*o < cap) d[*o] = '\0';
}
NK_FN int hc__parse_url (const char *url, char *host, unsigned hcap,
				 unsigned *port, char *path, unsigned pcap)
{
	*port = 80;
	const char *p = url;
	for (const char *q = url; *q; q++)			// skip a "://" scheme
		if (q[0] == ':' && q[1] == '/' && q[2] == '/') { p = q + 3; break; }

	unsigned i = 0;
	while (*p && *p != ':' && *p != '/') { if (i + 1 < hcap) host[i++] = *p; p++; }
	host[i] = '\0';

	if (*p == ':')						// explicit port
	{
		p++; unsigned v = 0, any = 0;
		while (*p >= '0' && *p <= '9') { v = v * 10 + (unsigned) (*p - '0'); p++; any = 1; }
		if (any) *port = v;
	}

	unsigned j = 0;
	if (*p != '/') { if (pcap > 1) path[j++] = '/'; }	// ensure a leading '/'
	while (*p) { if (j + 1 < pcap) path[j++] = *p; p++; }
	path[j] = '\0';

	return host[0] != '\0';
}
NK_FN int http_request (const char *method, const char *url,
				const char *xheaders, const char *body, unsigned body_len,
				char *buf, unsigned cap, http_response *resp)
{
	if (resp) { resp->status = 0; resp->ok = 0; resp->body = buf; resp->body_len = 0;
		    resp->total_len = 0; resp->truncated = 0; }
	if (cap < 2) return -1;

	char host[128], path[512]; unsigned port;
	if (!hc__parse_url (url, host, sizeof host, &port, path, sizeof path)) return -1;

	int sock = kapi_tcp_connect (host, port);
	if (sock < 0) return -2;

	// Build + send the request line and headers (then the optional body).
	char req[1024]; unsigned o = 0; req[0] = '\0';
	hc__cat (req, sizeof req, &o, method);    hc__cat (req, sizeof req, &o, " ");
	hc__cat (req, sizeof req, &o, path);      hc__cat (req, sizeof req, &o, " HTTP/1.0\r\nHost: ");
	hc__cat (req, sizeof req, &o, host);      hc__cat (req, sizeof req, &o,
		 "\r\nUser-Agent: Onyx\r\nConnection: close\r\n");
	if (xheaders) hc__cat (req, sizeof req, &o, xheaders);
	if (body && body_len)
	{
		hc__cat (req, sizeof req, &o, "Content-Length: ");
		hc__cat_uint (req, sizeof req, &o, body_len);
		hc__cat (req, sizeof req, &o, "\r\n");
	}
	hc__cat (req, sizeof req, &o, "\r\n");

	kapi_tcp_send (sock, req, o);
	if (body && body_len) kapi_tcp_send (sock, body, body_len);

	// Read until the server closes the connection (HTTP/1.0), the buffer fills, or
	// we idle too long. kapi_tcp_recv is non-blocking: 0 = nothing yet, <0 = closed.
	unsigned total = 0; int idle = 0;
	for (;;)
	{
		if (total >= cap - 1) { if (resp) resp->truncated = 1; break; }
		int n = kapi_tcp_recv (sock, buf + total, cap - 1 - total);
		if (n > 0) { total += (unsigned) n; idle = 0; }
		else if (n == 0) { if (++idle > 1000) break; msleep (10); }	// ~10 s timeout
		else break;							// connection closed
	}
	buf[total] = '\0';
	kapi_tcp_close (sock);

	if (resp)
	{
		resp->total_len = (int) total;
		const char *sp = buf;					// status line: "HTTP/1.x NNN ..."
		while (*sp && *sp != ' ') sp++;
		if (*sp == ' ')
		{
			sp++; int code = 0;
			while (*sp >= '0' && *sp <= '9') { code = code * 10 + (*sp - '0'); sp++; }
			resp->status = code; resp->ok = (code >= 200 && code < 300);
		}
		for (unsigned k = 0; k + 3 < total; k++)		// split at the blank line
			if (buf[k]=='\r' && buf[k+1]=='\n' && buf[k+2]=='\r' && buf[k+3]=='\n')
			{ resp->body = buf + k + 4; resp->body_len = (int) total - (int) (k + 4); break; }
	}
	return resp ? resp->body_len : (int) total;
}
NK_FN int http_get (const char *url, char *buf, unsigned cap, http_response *r)
{
	return http_request ("GET", url, 0, 0, 0, buf, cap, r);
}
NK_FN int http_post (const char *url, const char *body, unsigned len,
			     char *buf, unsigned cap, http_response *r)
{
	return http_request ("POST", url,
			     "Content-Type: application/x-www-form-urlencoded\r\n",
			     body, len, buf, cap, r);
}

#undef NK_FN
