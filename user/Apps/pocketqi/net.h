//
// pocketqi/net.h -- PocketQI: one HTTP(S) request, for the LLM, Wikipedia and the skills' web (OPEN "https://..."):
// NetKit's HttpClient (netkit/http.hpp) with mbedTLS (Libs/tls/onyx_tls.hpp) for https://, as Courier. It blocks:
// only the brain's thread calls it. The server's certificate is not verified (as /bin/llm and Courier: Onyx's
// entropy is not strong yet, docs/03). POCKETQI_NO_TLS (the desktop simulator): http:// only.
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
#ifndef POCKETQI_NET_H
#define POCKETQI_NET_H

#ifndef POCKETQI_NO_TLS
#define ONYX_HTTP_TLS
#endif
#include "netkit/http.hpp"
#include <string>

namespace pq {

enum { NET_BUF = 512 * 1024 };			// the largest answer kept (a Wikipedia summary, a weather forecast)

// HttpClient's progress: the answer complete once its Content-Length is in -- without it, the client waits for the
// server to close (or for its idle timeout, when it does not)
static bool http_complete (void *ctx, long got)
{
	const char *b = (const char *) ctx;
	long he = -1;
	for (long i = 0; i + 3 < got; i++)
		if (b[i] == '\r' && b[i + 1] == '\n' && b[i + 2] == '\r' && b[i + 3] == '\n') { he = i; break; }
	if (he < 0) return true;
	static const char key[] = "\ncontent-length:";
	for (long i = 0; i < he; i++)
	{
		long k = 0;
		while (key[k] && i + k < he && (b[i + k] | 0x20) == (key[k] | 0x20)) k++;
		if (key[k]) continue;
		long n = 0, j = i + k;
		while (j < he && b[j] == ' ') j++;
		while (j < he && b[j] >= '0' && b[j] <= '9') n = n * 10 + (b[j++] - '0');
		return got < he + 4 + n;			// (false: stop here, the answer is whole)
	}
	return true;
}

// method ("GET", "POST"...), url, extra header lines ("Name: value\r\n" each), body (0: none) -> the HTTP status
// (< 0: HttpError, the network's failure) and the body in out
static int http_request (const char *method, const std::string &url, const std::string &headers, const std::string *body,
			 std::string &out, unsigned timeoutMs = 60000)
{
	static char *buf;
	if (!buf) buf = new char[NET_BUF];
	out.clear ();
	HttpClient c;
	c.user_agent ("Onyx-PocketQI/1.0").timeout_ms (timeoutMs).progress (http_complete, buf);
	std::string ctype;
	size_t a = 0;
	while (a < headers.size ())
	{
		size_t e = headers.find ("\r\n", a);
		if (e == std::string::npos) e = headers.size ();
		std::string l = headers.substr (a, e - a);
		size_t colon = l.find (':');
		if (colon != std::string::npos)
		{
			std::string n = trim (l.substr (0, colon)), v = trim (l.substr (colon + 1));
			if (lower (n) == "content-type") ctype = v;		// (HttpClient writes it from its argument)
			else if (!n.empty ()) c.header (n.c_str (), v.c_str ());
		}
		a = e + 2;
	}
	HttpResponse r = c.send (method, url.c_str (), body && !body->empty () ? (ctype.empty () ? "application/json" : ctype.c_str ()) : 0,
				 body ? body->data () : 0, body ? (int) body->size () : 0, buf, NET_BUF);
	if (r.status > 0) out.assign (r.body, (size_t) r.body_len);
	return r.status;
}

} // namespace pq

#endif
