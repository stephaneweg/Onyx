/*
 * onyx_ws.h -- Onyx: the long-lived connections of NetSurf's scripts, outside the fetch
 * queue and the cache: WebSocket (RFC 6455, ws:// and wss://) and the event streams of
 * EventSource (text/event-stream), each in a thread of its own (kernel v67), as the
 * downloads of onyx_fetch.c; and a blocking GET for importScripts in a worker.
 *
 * Every call here is the UI thread's. The connection's thread posts (kapi_post) when
 * something came; the notify callback then runs on the UI thread, which takes the events
 * (onyx_ws_take). Plain C over the kapis, mbedTLS (onyx_nstls) and zlib: no NetSurf call.
 */
#ifndef ONYX_WS_H
#define ONYX_WS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct onyx_ws onyx_ws;

enum onyx_ws_mode {
	ONYX_WS_SOCKET = 0,	/* a WebSocket: the handshake, then messages */
	ONYX_WS_EVENTS = 1	/* an event stream: a GET, then its body as it comes */
};

enum onyx_ws_evtype {
	OWS_OPEN = 1,		/* the handshake / the head is in: code = the HTTP status,
				 * head = the response's head */
	OWS_TEXT,		/* a text message (UTF-8, checked) */
	OWS_BINARY,		/* a binary message */
	OWS_DATA,		/* (event stream) body bytes, cut after a line's end */
	OWS_CLOSE,		/* the end: code (1000..., 1006 for no close frame), clean,
				 * the reason in data */
	OWS_ERROR		/* a failure (before OWS_CLOSE): code = the HTTP status or 0,
				 * data = what failed */
};

struct onyx_ws_event {
	struct onyx_ws_event *next;
	int type;
	int code;
	int clean;
	char *head;		/* OWS_OPEN: status line and header lines, CRLF-separated */
	size_t len;
	uint8_t data[];		/* the payload, NUL-terminated (len bytes before the NUL) */
};

/* A connection to url (ws:, wss:, http:, https:): its thread started. hdrs: more request
 * header lines ("Name: value\r\n"...: Origin, Cookie, User-Agent, Sec-WebSocket-Protocol,
 * Last-Event-ID...). notify(pw) runs on the UI thread when events wait. NULL: no thread. */
onyx_ws *onyx_ws_open(int mode, const char *url, const char *hdrs,
		void (*notify)(void *pw), void *pw);

/* A message to send (WebSocket): queued, sent by the connection's thread. 0, or -1. */
int onyx_ws_send(onyx_ws *w, int binary, const void *data, size_t len);

/* The closing handshake (WebSocket): a close frame with code (0: none) and reason. */
void onyx_ws_close(onyx_ws *w, int code, const char *reason, size_t rlen);

/* The events come since the last call, oldest first (free each with free()). */
struct onyx_ws_event *onyx_ws_take(onyx_ws *w);

/* The bytes of the messages queued and not yet sent. */
size_t onyx_ws_buffered(onyx_ws *w);

/* The connection dropped (its thread told to stop; it frees what is left). */
void onyx_ws_free(onyx_ws *w);

/* The nth value (0...) of a header in a head (OWS_OPEN's), trimmed; malloc'd, or NULL. */
char *onyx_ws_head_get(const char *head, const char *name, int nth);

/* A GET, blocking (importScripts in a worker): the body (malloc'd, NUL-terminated, *len),
 * *status the HTTP status; redirects followed. NULL: it failed. */
char *onyx_http_get_sync(const char *url, const char *hdrs, size_t *len, int *status);

/* onyx_fetch.c: a connect in the downloads' turn (one at a time) */
int onyx_fetch_connect(const char *host, unsigned port);

/* Onyx: the app ends -- every connection told to stop -> the threads still running */
int onyx_ws_shutdown(void);

/* onyx_fetch.c (Onyx): whether the user accepted this host's bad certificate (any thread) */
int onyx_fetch_insecure_host(const char *host);

#ifdef __cplusplus
}
#endif

#endif
