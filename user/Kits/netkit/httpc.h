//
// httpc.h -- a minimal HTTP/1.0 client for Onyx apps, layered on the ABI v21 TCP
// socket calls (kapi_tcp_connect/send/recv/close + kapi_net_status).
//
// Design choices for the Onyx user model:
//   * Header-only (static inline) -- just #include it.
//   * NO dynamic allocation (there is no user malloc): the CALLER provides the
//     response buffer. Everything else lives on the app stack. So all memory is in
//     the app's own address space (static .bss @ 8 GB / stack @ 16 GB); the TCP
//     kapis copy between that buffer and the kernel socket buffers.
//   * HTTP/1.0 + "Connection: close": the server closes the socket when done, so we
//     just read to EOF -- no chunked-transfer decoding needed (1.0 never chunks).
//   * Plain HTTP only (port 80 default) -- there is no TLS in the stack yet.
//   * Blocking: polls the non-blocking kapi_tcp_recv with a timeout; fine for the
//     occasional fetch (it briefly stalls the calling app, like an IRC connect).
//
#ifndef ONYX_HTTPC_H
#define ONYX_HTTPC_H
#include "appkit/appkit.h"
#include "nk_api.h"


typedef struct
{
	int   status;		// HTTP status code (200, 404, ...), 0 if unparsed
	int   ok;		// 1 if status is 2xx
	char *body;		// points INTO the caller's buffer (start of the body)
	int   body_len;		// number of body bytes in the buffer
	int   total_len;	// total bytes received (headers + body)
	int   truncated;	// 1 if the response did not fit in the buffer
} http_response;

// ---- internals (hc__*) -------------------------------------------------------
NK_API int hc__len (const char *s);
NK_API void hc__cat (char *d, unsigned cap, unsigned *o, const char *s);
// Append a non-negative integer in decimal.
NK_API void hc__cat_uint (char *d, unsigned cap, unsigned *o, unsigned v);
// Parse "[http://]host[:port][/path]" into host / port / path (path defaults "/").
NK_API int hc__parse_url (const char *url, char *host, unsigned hcap,
				 unsigned *port, char *path, unsigned pcap);

// ---- public API --------------------------------------------------------------
// Perform an HTTP request. method = "GET"/"POST"/...; xheaders = extra header lines
// ("Key: Value\r\n" each) or 0; body/body_len = request body or 0. The response
// (headers + body) is read into buf[cap]; resp (if non-0) is filled in, with
// resp->body pointing into buf. Returns the body length (>=0), or <0 on error
// (-1 bad URL, -2 connect failed).
NK_API int http_request (const char *method, const char *url,
				const char *xheaders, const char *body, unsigned body_len,
				char *buf, unsigned cap, http_response *resp);
NK_API int http_get (const char *url, char *buf, unsigned cap, http_response *r);
NK_API int http_post (const char *url, const char *body, unsigned len,
			     char *buf, unsigned cap, http_response *r);

#if defined (NK_BODIES_INLINE) && !defined (NK_IMPL)
#include "httpc.inc"
#endif

#endif
