//
// httpsget -- HTTPS client demo: the reusable HttpClient (../http.hpp) with TLS
// enabled (ONYX_HTTP_TLS -> ../tls/onyx_tls.hpp -> mbedTLS). A newlib app (mbedTLS
// uses libc), so it prints with stdio.
//
// Build needs the cross-built mbedTLS (see user/tls/README); the bin/Makefile builds
// it only when MBEDTLS_DIR is set:  make MBEDTLS_DIR=../tls/mbedtls
//
// Usage:  httpsget https://example.com/
// NOTE: certificate verification is currently OFF and entropy is a placeholder
// (see onyx_tls.hpp) -- functional, not yet secure.
//
#define ONYX_HTTP_TLS
#include "http.hpp"

#include "kapi.h"		// kapi_get_args

// Direct, UNBUFFERED stdout (no newlib stdio buffering), consistent with httpget.
// httpsget is still a newlib app (mbedTLS uses libc), it just doesn't route its own
// output through printf.
static void outs (const char *s) { int n = 0; while (s[n]) n++; kapi_stdout_write (s, (unsigned) n); }
static void outi (int v)
{
	char t[16]; int n = 0;
	if (v < 0) { kapi_stdout_write ("-", 1); v = -v; }
	if (v == 0) t[n++] = '0';
	while (v) { t[n++] = (char) ('0' + v % 10); v /= 10; }
	char o[16]; int m = 0; while (n) o[m++] = t[--n];
	kapi_stdout_write (o, (unsigned) m);
}

int main (void)
{
	char url[512];
	kapi_get_args (url, sizeof url);
	int s = 0; while (url[s] == ' ' || url[s] == '\t') s++;
	int e = s; while (url[e] && url[e] != ' ' && url[e] != '\r' && url[e] != '\n') e++;
	url[e] = '\0';
	const char *u = url + s;
	if (!*u) { outs ("usage: httpsget <url>\n"); return 1; }

	static char buf[128 * 1024];
	HttpClient http;
	http.user_agent ("Onyx-httpsget/1.0").accept ("*/*");

	HttpResponse r = http.get (u, buf, sizeof buf);
	if (r.is_error ())
	{
		outs ("error: status "); outi (r.status);
		if (r.status == HTTP_ERR_TLS) outs (" (TLS handshake failed)");
		else if (r.status == HTTP_ERR_CONNECT)
		{
			outs (" (connect: kapi="); outi (r.net_err); outs (" -- ");
			outs (r.net_err == -1 ? "no net" :
			      r.net_err == -2 ? "too many sockets" :
			      r.net_err == -3 ? "DNS failed" :
			      r.net_err == -5 ? "connect refused" : "?");
			outs (")");
		}
		outs ("\n");
		return 1;
	}

	outs ("HTTP "); outi (r.status); outs ("  (");
	outi (r.body_len); outs (" body bytes");
	if (r.truncated) outs (", truncated");
	outs (")\n");
	char ct[128];
	if (r.header ("Content-Type", ct, sizeof ct) >= 0) { outs ("Content-Type: "); outs (ct); outs ("\n"); }
	kapi_stdout_write (r.body, (unsigned) r.body_len);
	outs ("\n");
	return r.ok () ? 0 : 2;
}
