//
// httpget -- minimal REST/HTTP client demo for the reusable HttpClient (../http.hpp).
//
// Usage:  httpget <url>          e.g.  httpget http://example.com/
// Prints the status line, the Content-Type, and the response body. Freestanding C++
// (no libc) -- proves http.hpp works in an integer-only app; console I/O via kapi.
//
#include "kapi.h"
#include "http.hpp"

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
	int s = 0; while (url[s] == ' ' || url[s] == '\t') s++;	// trim leading space
	int e = s; while (url[e] && url[e] != ' ' && url[e] != '\r' && url[e] != '\n') e++;
	url[e] = '\0';
	const char *u = url + s;

	if (!*u) { outs ("usage: httpget <url>\n"); return 1; }

	static char buf[64 * 1024];
	HttpClient http;
	http.user_agent ("Onyx-httpget/1.0").accept ("*/*");

	HttpResponse r = http.get (u, buf, sizeof buf);

	if (r.is_error ())
	{
		outs ("error: ");
		switch (r.status)
		{
		case HTTP_ERR_BAD_URL: outs ("bad URL"); break;
		case HTTP_ERR_HTTPS:   outs ("https not supported yet (no TLS)"); break;
		case HTTP_ERR_NO_NET:  outs ("network down"); break;
		case HTTP_ERR_CONNECT: outs ("connect failed (kapi="); outi (r.net_err); outs (")"); break;
		case HTTP_ERR_SEND:    outs ("send failed"); break;
		case HTTP_ERR_TIMEOUT: outs ("timeout"); break;
		default:               outs ("no response"); break;
		}
		outs ("\n");
		return 1;
	}

	outs ("HTTP "); outi (r.status); outs ("\n");
	char ct[128];
	if (r.header ("Content-Type", ct, sizeof ct) >= 0) { outs ("Content-Type: "); outs (ct); outs ("\n"); }
	outs ("--- body ("); outi (r.body_len); outs (" bytes");
	if (r.truncated) outs (", truncated");
	outs (") ---\n");
	kapi_stdout_write (r.body, (unsigned) r.body_len);
	outs ("\n");
	return r.ok () ? 0 : 2;
}
