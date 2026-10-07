//
// llm -- ask a large language model for ONE answer, over HTTPS: the network half of Koton's AI
// composition (user/Apps/koton/engine/ai.h), usable by any app or from the shell. A newlib app
// (mbedTLS), like groq; the TLS work stays out of the app, which spawns this with pipes. It also
// downloads a file (Koton fetches its SoundFont with it).
//
// Usage:
//   llm [request.json] [-o result.json]
//     the request is read from the file, else from stdin; the result goes to the file (then only the
//     progress lines are printed), else to stdout
//
// An AI request (one JSON document, UTF-8):
//   { "provider": "gemini" | "groq" | "mistral" | "claude" | "openai-compatible"
//                 (+ "deepseek", "grok", "openai": OpenAI-compatible endpoints),
//     "model": "...", "key": "...", "system": "...", "user": "...",
//     "json": true,               ask for a JSON answer (Gemini's responseMimeType, OpenAI's json_object)
//     "temperature": 0.7,         (not sent to Claude: its recent models refuse it)
//     "thinking": -1,             Gemini's thinking budget in tokens (-1: the model's default, 0: off)
//     "url": "...",               optional: another endpoint (a local OpenAI-compatible server...)
//     "maxTokens": 32000,         optional: Claude's max_tokens (default 32000), OpenAI's max_tokens
//     "timeout": 600 }            optional: seconds without a byte before giving up (default 600)
//   -> { "ok": true, "text": "..." }  or  { "ok": false, "error": "..." }
//
// A download:
//   { "fetch": "https://...", "out": "SD:/res/soundfonts/GeneralUser-GS.sf2", "timeout": 120 }
//   -> { "ok": true, "bytes": N }  or  { "ok": false, "error": "..." }
//   (redirects followed, the file's folders made, the file written only when complete)
//
// The result is one line of JSON, the last line of stdout; exit code 0 / 1. Progress lines ("llm:
// connecting to ...", "llm: sending 12345 bytes", "llm: waiting for the answer (15 s)", "llm:
// receiving 4096 bytes" -- "llm: receiving 4096 bytes of 31000000" when the size is known) go to
// stderr -- on Onyx the same stream as stdout, so a reader takes the last line starting with '{' as
// the result (kt::aiParseLlmOutput does).
//
// The replies can be big (a 64-bar piece: 60+ KB of JSON inside the provider's JSON; a SoundFont: 30
// MB): the response buffer grows as needed (to the Content-Length at once when there is one; at most 16
// MB for an answer, 512 MB for a download). The TLS certificate is NOT verified (Onyx has no CA
// bundle; the user's decision). A thinking model can stay silent for a minute: the recv BIO is made
// non-blocking after the handshake (onyx_tls's own gives up after 20 s of silence) and this loop waits.
//
// The protocol half (request -> URL + headers + body, provider answer -> text) is testable on the PC:
// #define LLM_PROTO_ONLY and include this file (tools/tests/koton/ai_test.cpp).
//
#include "json.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

namespace llm {

enum { API_GEMINI = 0, API_OPENAI, API_CLAUDE };

struct Request
{
	const char *provider, *model, *key, *system, *user, *url;	// into the parsed document
	bool json;
	double temperature;
	int thinking, maxTokens, timeoutSec;
};

struct Call
{
	int api;
	const char *label;		// "Gemini", "Groq"...
	char url[1024];
	char headers[1024];		// extra header lines, each ending in CR LF
	json::Writer body;
	Call () : api (API_OPENAI), label (""), body (false) { url[0] = 0; headers[0] = 0; }
};

static void seterr (char *err, int cap, const char *fmt, ...)
{
	if (!err || cap <= 0) return;
	va_list ap; va_start (ap, fmt); vsnprintf (err, (size_t) cap, fmt, ap); va_end (ap);
}

// The request document -> Request (its strings point into d)
inline bool parseRequest (json::Doc &d, const char *text, unsigned long len, Request &r, char *err, int cap)
{
	if (!d.parse (text, len, json::TOLERANT)) { seterr (err, cap, "bad request JSON (%s, line %d)", d.error (), d.errLine ()); return false; }
	const json::Value &o = d.root ();
	if (!o.isObj ()) { seterr (err, cap, "the request is not a JSON object"); return false; }
	r.provider = o["provider"].asStr ("gemini");
	r.model = o["model"].asStr ("");
	r.key = o["key"].asStr ("");
	r.system = o["system"].asStr ("");
	r.user = o["user"].asStr ("");
	r.url = o["url"].asStr ("");
	r.json = o["json"].asBool (true);
	r.temperature = o["temperature"].asDouble (0.7);
	r.thinking = o["thinking"].asInt (-1);
	r.maxTokens = o["maxTokens"].asInt (0);
	r.timeoutSec = o["timeout"].asInt (600);
	if (r.timeoutSec < 5) r.timeoutSec = 5;
	if (!r.user[0]) { seterr (err, cap, "the request has no \"user\" text"); return false; }
	return true;
}

static void cat (char *d, int cap, const char *s) { int n = (int) strlen (d); while (*s && n + 1 < cap) d[n++] = *s++; d[n] = 0; }
// Uri.EscapeDataString
static void catEscaped (char *d, int cap, const char *s)
{
	static const char hx[] = "0123456789ABCDEF";
	int n = (int) strlen (d);
	for (; *s && n + 4 < cap; s++)
	{
		unsigned char c = (unsigned char) *s;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~') d[n++] = (char) c;
		else { d[n++] = '%'; d[n++] = hx[c >> 4]; d[n++] = hx[c & 15]; }
	}
	d[n] = 0;
}

// Request -> the provider's URL, headers and body
inline bool buildCall (const Request &r, Call &c, char *err, int cap)
{
	const char *p = r.provider;
	const char *endpoint = 0;
	if (json::seqi (p, "gemini")) { c.api = API_GEMINI; c.label = "Gemini"; }
	else if (json::seqi (p, "claude") || json::seqi (p, "anthropic")) { c.api = API_CLAUDE; c.label = "Claude"; endpoint = "https://api.anthropic.com/v1/messages"; }
	else if (json::seqi (p, "groq")) { c.label = "Groq"; endpoint = "https://api.groq.com/openai/v1/chat/completions"; }
	else if (json::seqi (p, "mistral")) { c.label = "Mistral"; endpoint = "https://api.mistral.ai/v1/chat/completions"; }
	else if (json::seqi (p, "deepseek")) { c.label = "DeepSeek"; endpoint = "https://api.deepseek.com/chat/completions"; }
	else if (json::seqi (p, "grok")) { c.label = "Grok"; endpoint = "https://api.x.ai/v1/chat/completions"; }
	else if (json::seqi (p, "openai")) { c.label = "OpenAI"; endpoint = "https://api.openai.com/v1/chat/completions"; }
	else if (json::seqi (p, "openai-compatible")) { c.label = "the server"; endpoint = 0; }
	else { seterr (err, cap, "unknown provider \"%s\"", p); return false; }
	bool keyless = json::seqi (p, "openai-compatible");
	if (!r.key[0] && !keyless) { seterr (err, cap, "no API key for %s", c.label); return false; }
	const char *model = r.model;
	if (!strncmp (model, "models/", 7)) model += 7;
	if (!model[0]) { seterr (err, cap, "no model given for %s", c.label); return false; }

	c.url[0] = 0; c.headers[0] = 0;
	if (r.url[0]) cat (c.url, sizeof c.url, r.url);
	else if (c.api == API_GEMINI)
	{
		cat (c.url, sizeof c.url, "https://generativelanguage.googleapis.com/v1beta/models/");
		catEscaped (c.url, sizeof c.url, model);
		cat (c.url, sizeof c.url, ":generateContent");
	}
	else if (endpoint) cat (c.url, sizeof c.url, endpoint);
	else { seterr (err, cap, "an OpenAI-compatible provider needs a \"url\""); return false; }

	cat (c.headers, sizeof c.headers, "Accept: application/json\r\n");
	json::Writer &b = c.body;
	b.reset ();
	if (c.api == API_GEMINI)
	{
		cat (c.headers, sizeof c.headers, "x-goog-api-key: "); cat (c.headers, sizeof c.headers, r.key); cat (c.headers, sizeof c.headers, "\r\n");
		b.beginObj ();
		if (r.system[0])
		{
			b.key ("systemInstruction"); b.beginObj (); b.key ("parts"); b.beginArr (); b.beginObj (); b.key ("text"); b.str (r.system); b.endObj (); b.endArr (); b.endObj ();
		}
		b.key ("contents"); b.beginArr (); b.beginObj ();
		b.key ("role"); b.str ("user");
		b.key ("parts"); b.beginArr (); b.beginObj (); b.key ("text"); b.str (r.user); b.endObj (); b.endArr ();
		b.endObj (); b.endArr ();
		// no maxOutputTokens: a thinking model spends tokens on it, a low cap would cut the answer
		b.key ("generationConfig"); b.beginObj ();
		if (r.json) { b.key ("responseMimeType"); b.str ("application/json"); }
		b.key ("temperature"); b.num (r.temperature);
		if (r.thinking >= 0) { b.key ("thinkingConfig"); b.beginObj (); b.key ("thinkingBudget"); b.num (r.thinking < 24576 ? r.thinking : 24576); b.endObj (); }
		b.endObj ();
		b.endObj ();
	}
	else if (c.api == API_CLAUDE)
	{
		cat (c.headers, sizeof c.headers, "x-api-key: "); cat (c.headers, sizeof c.headers, r.key); cat (c.headers, sizeof c.headers, "\r\n");
		cat (c.headers, sizeof c.headers, "anthropic-version: 2023-06-01\r\n");
		b.beginObj ();
		b.key ("model"); b.str (model);
		b.key ("max_tokens"); b.num (r.maxTokens > 0 ? r.maxTokens : 32000);	// room for a whole 48-64 bar piece
		if (r.system[0]) { b.key ("system"); b.str (r.system); }
		b.key ("messages"); b.beginArr (); b.beginObj (); b.key ("role"); b.str ("user"); b.key ("content"); b.str (r.user); b.endObj (); b.endArr ();
		b.endObj ();
	}
	else
	{
		if (r.key[0]) { cat (c.headers, sizeof c.headers, "Authorization: Bearer "); cat (c.headers, sizeof c.headers, r.key); cat (c.headers, sizeof c.headers, "\r\n"); }
		b.beginObj ();
		b.key ("model"); b.str (model);
		b.key ("messages"); b.beginArr ();
		if (r.system[0]) { b.beginObj (); b.key ("role"); b.str ("system"); b.key ("content"); b.str (r.system); b.endObj (); }
		b.beginObj (); b.key ("role"); b.str ("user"); b.key ("content"); b.str (r.user); b.endObj ();
		b.endArr ();
		if (r.json) { b.key ("response_format"); b.beginObj (); b.key ("type"); b.str ("json_object"); b.endObj (); }
		b.key ("temperature"); b.num (r.temperature);
		if (r.maxTokens > 0) { b.key ("max_tokens"); b.num (r.maxTokens); }
		b.endObj ();
	}
	if (!b.ok ()) { seterr (err, cap, "out of memory"); return false; }
	return true;
}

// AIClient.ShortError: {"message"}, {"error": "..."}, {"error": {"message"}}, else the raw text (300 bytes)
inline void shortError (const char *body, unsigned long len, char *out, int cap)
{
	json::Doc d;
	if (d.parse (body, len, json::TOLERANT))
	{
		const json::Value &r = d.root ();
		if (r["message"].isStr ()) { r["message"].copyStr (out, (unsigned) cap); return; }
		const json::Value &e = r["error"];
		if (e.isStr ()) { e.copyStr (out, (unsigned) cap); return; }
		if (e.isObj () && e["message"].isStr ()) { e["message"].copyStr (out, (unsigned) cap); return; }
		if (r.isArr () && r[0]["error"]["message"].isStr ()) { r[0]["error"]["message"].copyStr (out, (unsigned) cap); return; }
	}
	unsigned long n = len < 300 ? len : 300;
	if (n >= (unsigned long) cap) n = (unsigned long) cap - 4;
	unsigned long o = 0;
	for (unsigned long i = 0; i < n; i++) out[o++] = (body[i] == '\r' || body[i] == '\n') ? ' ' : body[i];
	if (len > n) { out[o++] = '.'; out[o++] = '.'; out[o++] = '.'; }
	out[o] = 0;
}

static const char *kTruncated = "the answer was cut (the model's token limit): ask for fewer bars, or melodic lines instead of riffs";

// The provider's HTTP answer -> the model's text (appended to `text`). False with a message.
inline bool extractText (int api, const char *label, int status, const char *body, unsigned long len, json::Writer &text, char *err, int cap)
{
	if (status < 200 || status >= 300)
	{
		char m[512]; shortError (body, len, m, sizeof m);
		if (!m[0] && status == 401) snprintf (m, sizeof m, "invalid API key");
		seterr (err, cap, "%s answered %d: %s", label, status, m);
		return false;
	}
	json::Doc d;
	if (!d.parse (body, len, json::STRICT) && !d.parse (body, len, json::TOLERANT))
	{ seterr (err, cap, "%s's answer is unreadable (%s)", label, d.error ()); return false; }
	const json::Value &r = d.root ();
	unsigned long before = text.size ();
	if (api == API_GEMINI)
	{
		const json::Value &cand = r["candidates"][0];
		if (cand.isNull ())
		{
			const char *why = r["promptFeedback"]["blockReason"].asStr (0);
			if (why) seterr (err, cap, "Gemini blocked the request (%s)", why);
			else seterr (err, cap, "Gemini's answer has no candidate");
			return false;
		}
		const char *fr = cand["finishReason"].asStr ("");
		if (!strcmp (fr, "MAX_TOKENS")) { seterr (err, cap, "%s", kTruncated); return false; }
		for (const json::Value *p = cand["content"]["parts"].first (); p; p = p->next)
		{
			if ((*p)["thought"].asBool (false)) continue;		// a thought summary is not the answer
			const json::Value &t = (*p)["text"];
			if (t.isStr ()) text.raw (t.s, t.slen_);
		}
		if (text.size () == before)
		{
			if (fr[0] && strcmp (fr, "STOP")) seterr (err, cap, "empty answer from Gemini (finish reason %s)", fr);
			else seterr (err, cap, "empty answer from Gemini (blocked by a safety filter?)");
			return false;
		}
	}
	else if (api == API_CLAUDE)
	{
		const char *sr = r["stop_reason"].asStr ("");
		if (!strcmp (sr, "refusal")) { seterr (err, cap, "Claude refused the request (safety filter)"); return false; }
		if (!strcmp (sr, "max_tokens")) { seterr (err, cap, "%s", kTruncated); return false; }
		for (const json::Value *b = r["content"].first (); b; b = b->next)
			if (json::seq ((*b)["type"].asStr (""), "text") && (*b)["text"].isStr ()) text.raw ((*b)["text"].s, (*b)["text"].slen_);
		if (text.size () == before) { seterr (err, cap, "empty answer from Claude"); return false; }
	}
	else
	{
		const json::Value &ch = r["choices"][0];
		if (ch.isNull ()) { seterr (err, cap, "%s's answer has no choice", label); return false; }
		if (json::seq (ch["finish_reason"].asStr (""), "length")) { seterr (err, cap, "%s", kTruncated); return false; }
		const json::Value &c = ch["message"]["content"];
		if (c.isStr ()) text.raw (c.s, c.slen_);
		else for (const json::Value *p = c.first (); p; p = p->next) if ((*p)["text"].isStr ()) text.raw ((*p)["text"].s, (*p)["text"].slen_);
		if (text.size () == before) { seterr (err, cap, "empty answer from %s", label); return false; }
	}
	if (!text.ok ()) { seterr (err, cap, "out of memory"); return false; }
	return true;
}

// the result line
inline void writeResult (json::Writer &w, bool ok, const char *s, unsigned long n)
{
	w.reset ();
	w.beginObj ();
	w.key ("ok"); w.boolean (ok);
	w.key (ok ? "text" : "error"); w.str (s, (unsigned) n);
	w.endObj ();
	w.raw ("\n");
}

// the download's result line
inline void writeFetchResult (json::Writer &w, bool ok, unsigned long bytes, const char *error)
{
	w.reset ();
	w.beginObj ();
	w.key ("ok"); w.boolean (ok);
	if (ok) { w.key ("bytes"); w.num ((long long) bytes); }
	else { w.key ("error"); w.str (error ? error : ""); }
	w.endObj ();
	w.raw ("\n");
}

} // namespace llm


#ifndef LLM_PROTO_ONLY
// ==== the helper: the network ===============================================================================
#define ONYX_HTTP_TLS
#include "netkit/http.hpp"
#include "appkit/appkit.h"
#include <unistd.h>

static void progress (const char *fmt, ...)
{
	char b[256];
	int n = snprintf (b, sizeof b, "llm: ");
	va_list ap; va_start (ap, fmt); n += vsnprintf (b + n, sizeof b - (size_t) n - 2, fmt, ap); va_end (ap);
	if (n > (int) sizeof b - 2) n = (int) sizeof b - 2;
	b[n++] = '\n';
	write (2, b, (size_t) n);
}

static const char *g_outPath = 0;

// the result line: to the -o file, else to stdout; the exit code
static int emit (const json::Writer &w, bool ok)
{
	if (g_outPath)
	{
		if (kapi_save_file (g_outPath, w.data (), (unsigned) w.size ()) < 0) progress ("cannot write %s", g_outPath);
	}
	else
	{
		const char *p = w.data (); unsigned long left = w.size ();
		while (left > 0)
		{
			int k = (int) write (1, p, left > 16384 ? 16384 : (size_t) left);
			if (k <= 0) break;
			p += k; left -= (unsigned long) k;
		}
	}
	return ok ? 0 : 1;
}
static bool g_fetch = false;
static int finish (bool ok, const char *s, unsigned long n)
{
	json::Writer w (false);
	if (g_fetch) llm::writeFetchResult (w, ok, ok ? n : 0, ok ? 0 : s);
	else llm::writeResult (w, ok, s, n);
	if (!ok && g_outPath) progress ("error: %.*s", (int) (n < 200 ? n : 200), s);
	return emit (w, ok);
}
static int fail (const char *msg) { return finish (false, msg, strlen (msg)); }

// A non-blocking receive for mbedTLS (onyx_tls's blocks and gives up after 20 s of silence): a whole
// TCP segment is kept (Circle drops the rest of a segment read partially), WANT_READ when none yet.
static int nb_bio_recv (void *ctx, unsigned char *buf, size_t len)
{
	onyx_tls::Session *s = (onyx_tls::Session *) ctx;
	if (s->rxpos >= s->rxlen)
	{
		int n = kapi_tcp_recv (s->sock, s->rxbuf, (unsigned) sizeof s->rxbuf);
		if (n < 0) return MBEDTLS_ERR_NET_CONN_RESET;
		if (n == 0) return MBEDTLS_ERR_SSL_WANT_READ;
		s->rxlen = n; s->rxpos = 0;
	}
	int avail = s->rxlen - s->rxpos;
	int give = (int) len < avail ? (int) len : avail;
	memcpy (buf, s->rxbuf + s->rxpos, (size_t) give);
	s->rxpos += give;
	return give;
}

static int headerEnd (const char *b, int total)
{
	for (int k = 0; k + 3 < total; k++) if (b[k] == '\r' && b[k + 1] == '\n' && b[k + 2] == '\r' && b[k + 3] == '\n') return k;
	return -1;
}
// the body's announced length (-1: none, chunked or until the close)
static long contentLength (const char *b, int he)
{
	int vl = 0;
	const char *te = http_detail::find (b, he, "Transfer-Encoding", &vl);
	if (te && http_detail::contains_ci (te, vl, "chunked")) return -1;
	const char *cl = http_detail::find (b, he, "Content-Length", &vl);
	if (!cl) return -1;
	long v = 0; for (int i = 0; i < vl && cl[i] >= '0' && cl[i] <= '9'; i++) v = v * 10 + (cl[i] - '0');
	return v;
}
// has the whole response arrived? (Content-Length reached, or the last chunk seen)
static bool complete (const char *b, int total, int he)
{
	int bs = he + 4, vl = 0;
	const char *te = http_detail::find (b, he, "Transfer-Encoding", &vl);
	if (te && http_detail::contains_ci (te, vl, "chunked"))
		return total - bs >= 5 && !memcmp (b + total - 5, "0\r\n\r\n", 5) && (total - bs == 5 || b[total - 6] == '\n');
	long cl = contentLength (b, he);
	return cl >= 0 && total - bs >= cl;
}

// One HTTP exchange: the answer's status, headers and (dechunked) body in a malloc'd buffer (the caller
// frees resp.buf). 0, or an error message.
struct Resp { char *buf; int status; const char *hdr; int hdrLen; char *body; long bodyLen; Resp () : buf (0), status (0), hdr (0), hdrLen (0), body (0), bodyLen (0) {} };

static const char *request (const char *method, const char *url, const char *extraHeaders, const char *body, unsigned long bodyLen,
			    int timeoutSec, long maxBytes, bool download, Resp &out, char *err, int cap)
{
	static char host[160], path[2048];
	unsigned port; bool https;
	if (!http_detail::parse_url (url, host, (int) sizeof host, &port, path, (int) sizeof path, &https)) return "bad URL";
	char ip[40];
	if (!kapi_net_status (ip, sizeof ip)) return "the network is down (Wi-Fi not connected?)";
	progress ("connecting to %s", host);
	Transport tp;
	int raw = 0;
	int orc = http_detail::tp_open (tp, host, port, https, &raw);
	if (orc == HTTP_ERR_TLS) return "TLS handshake failed";
	if (orc != 0) { snprintf (err, (size_t) cap, "cannot reach %s (%s)", host, raw == -3 ? "DNS" : raw == -1 ? "no network" : "connect"); return err; }
	if (https) mbedtls_ssl_set_bio (&tp.ssl.ssl, &tp.ssl, onyx_tls::bio_send, nb_bio_recv, 0);

	// the request
	static char hdr[4096]; hdr[0] = 0;
	{
		int o = 0;
		using http_detail::cat; using http_detail::cati;
		cat (hdr, (int) sizeof hdr, &o, method); cat (hdr, (int) sizeof hdr, &o, " "); cat (hdr, (int) sizeof hdr, &o, path);
		cat (hdr, (int) sizeof hdr, &o, " HTTP/1.1\r\nHost: "); cat (hdr, (int) sizeof hdr, &o, host);
		if ((https && port != 443) || (!https && port != 80)) { cat (hdr, (int) sizeof hdr, &o, ":"); cati (hdr, (int) sizeof hdr, &o, (long) port); }
		cat (hdr, (int) sizeof hdr, &o, "\r\nUser-Agent: Onyx-llm/1.0\r\nConnection: close\r\n");
		if (body) { cat (hdr, (int) sizeof hdr, &o, "Content-Type: application/json\r\nContent-Length: "); cati (hdr, (int) sizeof hdr, &o, (long) bodyLen); cat (hdr, (int) sizeof hdr, &o, "\r\n"); }
		cat (hdr, (int) sizeof hdr, &o, extraHeaders ? extraHeaders : "");
		cat (hdr, (int) sizeof hdr, &o, "\r\n");
	}
	if (body) progress ("sending %lu bytes", bodyLen);
	if (http_detail::tp_send (tp, hdr, (int) strlen (hdr)) < 0 || (body && bodyLen && http_detail::tp_send (tp, body, (int) bodyLen) < 0))
	{ http_detail::tp_close (tp); return "sending the request failed"; }

	// the answer
	long bcap = 256 * 1024; int total = 0;
	char *buf = (char *) malloc ((size_t) bcap);
	if (!buf) { http_detail::tp_close (tp); return "out of memory"; }
	unsigned idle0 = kapi_get_ticks (), shown = idle0, waited = 0;
	int reported = 0, he = -1; long announced = -1;
	const char *problem = 0;
	for (;;)
	{
		if (bcap - total < 32 * 1024)
		{
			if (bcap >= maxBytes) { problem = download ? "the file is too big" : "the answer is too big"; break; }
			long nc = bcap * 2; if (nc > maxBytes + 64 * 1024) nc = maxBytes + 64 * 1024;
			char *nb = (char *) realloc (buf, (size_t) nc);
			if (!nb) { problem = "out of memory"; break; }
			buf = nb; bcap = nc;
		}
		int n = http_detail::tp_recv (tp, buf + total, (int) (bcap - 1 - total));
		unsigned now = kapi_get_ticks ();
		if (n > 0)
		{
			total += n; idle0 = now;
			if (he < 0 && (he = headerEnd (buf, total)) >= 0)
			{
				announced = contentLength (buf, he);
				// room for the whole announced body at once (a big download is not copied again and again)
				if (announced > 0 && he + 4 + announced + 1 > bcap && announced <= maxBytes)
				{
					char *nb = (char *) realloc (buf, (size_t) (he + 4 + announced + 64 * 1024));
					if (nb) { buf = nb; bcap = he + 4 + announced + 64 * 1024; }
				}
			}
			int step = download ? 256 * 1024 : 8192;
			if (total - reported >= step || (now - shown) >= 100)
			{
				int got = he >= 0 ? total - he - 4 : total;
				if (announced > 0) progress ("receiving %d bytes of %ld", got, announced); else progress ("receiving %d bytes", got);
				reported = total; shown = now;
			}
			if (he >= 0 && complete (buf, total, he)) break;
		}
		else if (n == 0)
		{
			unsigned idleMs = (now - idle0) * 10;			// (ticks: HZ = 100)
			if (idleMs > (unsigned) timeoutSec * 1000)
			{
				if (total == 0) problem = "no answer (timeout)";
				else if (download) problem = "the download stalled (timeout)";
				break;
			}
			if (total == 0 && idleMs / 5000 > waited) { waited = idleMs / 5000; progress ("waiting for the answer (%u s)", waited * 5); }
			kapi_msleep (download ? 2 : 5);
		}
		else break;							// closed: complete
	}
	http_detail::tp_close (tp);
	if (problem) { free (buf); return problem; }
	if (total == 0) { free (buf); return "empty answer (the connection was closed)"; }
	buf[total] = 0;

	// status line, headers, body (chunked decoded)
	int i = 0, status = 0;
	while (i < total && buf[i] != ' ') i++;
	if (i < total) { i++; while (i < total && buf[i] >= '0' && buf[i] <= '9') { status = status * 10 + (buf[i] - '0'); i++; } }
	he = headerEnd (buf, total);
	if (he < 0) { free (buf); return "a truncated HTTP answer"; }
	int hs = 0; while (hs < he && buf[hs] != '\n') hs++; if (hs < he) hs++;
	long bs = he + 4, blen = total - bs;
	int vl = 0;
	const char *te = http_detail::find (buf, he, "Transfer-Encoding", &vl);
	if (te && http_detail::contains_ci (te, vl, "chunked")) blen = http_detail::dechunk (buf + bs, (int) blen);
	else
	{
		long cl = contentLength (buf, he);
		if (cl >= 0 && cl < blen) blen = cl;
		if (download && cl > blen) { free (buf); snprintf (err, (size_t) cap, "the download was cut (%ld of %ld bytes)", blen, cl); return err; }
	}
	buf[bs + blen] = 0;
	progress ("received %ld bytes", blen);
	out.buf = buf; out.status = status; out.hdr = buf + hs; out.hdrLen = he - hs; out.body = buf + bs; out.bodyLen = blen;
	return 0;
}

// ---- the download ----
static void makeFolders (const char *path)
{
	char d[512]; int n = (int) strlen (path);
	if (n >= (int) sizeof d) return;
	for (int i = 0; i < n; i++)
	{
		d[i] = path[i];
		if (path[i] == '/' && i > 0 && path[i - 1] != ':') { d[i] = 0; kapi_mkdir (d); d[i] = '/'; }
	}
}

static int fetch (const char *url0, const char *outPath, int timeoutSec)
{
	g_fetch = true;
	if (!url0[0]) return fail ("no URL to fetch");
	if (!outPath[0]) return fail ("no \"out\" path");
	static char url[2048], next[2048], loc[2048], err[600];
	snprintf (url, sizeof url, "%s", url0);
	Resp r;
	for (int hop = 0; ; hop++)
	{
		const char *e = request ("GET", url, "Accept: */*\r\n", 0, 0, timeoutSec, 512L * 1024 * 1024, true, r, err, sizeof err);
		if (e) return fail (e);
		if (r.status >= 300 && r.status < 400 && r.status != 304)
		{
			HttpResponse h; h.hdr = r.hdr; h.hdr_len = r.hdrLen;
			bool has = h.header ("Location", loc, (int) sizeof loc) > 0;
			free (r.buf); r = Resp ();
			if (!has) return fail ("a redirect without a Location");
			if (hop >= 8) return fail ("too many redirects");
			if (!http_detail::resolve_redirect (url, loc, next, (int) sizeof next)) return fail ("a bad redirect");
			snprintf (url, sizeof url, "%s", next);
			progress ("redirected (%d)", hop + 1);
			continue;
		}
		break;
	}
	if (r.status < 200 || r.status >= 300)
	{
		free (r.buf);
		snprintf (err, sizeof err, "the server answered %d", r.status);
		return fail (err);
	}
	makeFolders (outPath);
	progress ("writing %s", outPath);
	int w = kapi_save_file (outPath, r.body, (unsigned) r.bodyLen);
	unsigned long n = (unsigned long) r.bodyLen;
	free (r.buf);
	if (w < 0 || (unsigned long) w != n) { snprintf (err, sizeof err, "cannot write %s", outPath); return fail (err); }
	return finish (true, "", n);
}

int main (void)
{
	// ---- arguments ----
	static char args[1024];
	kapi_get_args (args, sizeof args);
	const char *inPath = 0;
	char *q = args;
	for (;;)
	{
		while (*q == ' ') q++;
		if (!*q) break;
		char *tok = q;
		while (*q && *q != ' ') q++;
		if (*q) *q++ = 0;
		if (!strcmp (tok, "-o"))
		{
			while (*q == ' ') q++;
			g_outPath = q;
			while (*q && *q != ' ') q++;
			if (*q) *q++ = 0;
		}
		else if (!strcmp (tok, "-h") || !strcmp (tok, "--help"))
		{
			static const char u[] = "usage: llm [request.json] [-o result.json]   (the request on stdin when no file)\n";
			write (1, u, sizeof u - 1);
			return 0;
		}
		else inPath = tok;
	}

	// ---- the request ----
	char *req = 0; unsigned long rlen = 0, rcap = 0;
	if (inPath)
	{
		void *f = kapi_open (inPath);
		if (!f) { char m[300]; snprintf (m, sizeof m, "cannot open %.200s", inPath); return fail (m); }
		rcap = kapi_fsize (f) + 1;
		req = (char *) malloc (rcap);
		if (!req) { kapi_close (f); return fail ("out of memory"); }
		int n;
		while (rlen + 1 < rcap && (n = kapi_read (f, req + rlen, (unsigned) (rcap - 1 - rlen))) > 0) rlen += (unsigned long) n;
		kapi_close (f);
	}
	else
	{
		for (;;)
		{
			if (rlen + 4096 + 1 > rcap)
			{
				unsigned long nc = rcap ? rcap * 2 : 65536;
				char *nb = (char *) realloc (req, nc);
				if (!nb) { free (req); return fail ("out of memory"); }
				req = nb; rcap = nc;
			}
			int n = kapi_stdin_read (req + rlen, (unsigned) (rcap - 1 - rlen));
			if (n <= 0) break;
			rlen += (unsigned long) n;
		}
	}
	if (!req || rlen == 0) { free (req); return fail ("no request (stdin was empty)"); }
	req[rlen] = 0;

	static char err[600];
	json::Doc rd;
	// a download?
	{
		json::Doc peek;
		if (peek.parse (req, rlen, json::TOLERANT) && peek.root ().has ("fetch"))
		{
			static char url[2048], out[512];
			peek.root ()["fetch"].copyStr (url, sizeof url);
			peek.root ()["out"].copyStr (out, sizeof out);
			int to = peek.root ()["timeout"].asInt (120);
			free (req);
			return fetch (url, out, to < 5 ? 5 : to);
		}
	}
	llm::Request r;
	llm::Call call;
	if (!llm::parseRequest (rd, req, rlen, r, err, sizeof err) || !llm::buildCall (r, call, err, sizeof err)) { free (req); return fail (err); }
	free (req); req = 0;			// (the strings of r point into rd, not into req)

	// A busy model answers 503 ("high demand") or 429 / 500 / 502 / 504 for a moment: asked again after
	// 5, 10, 20, 40 s before its error is given (Gemini's free models do so often, even with a short prompt).
	Resp resp;
	for (int attempt = 0;; attempt++)
	{
		const char *e = request ("POST", call.url, call.headers, call.body.data (), call.body.size (), r.timeoutSec, 16L * 1024 * 1024, false, resp, err, sizeof err);
		if (e) return fail (e);
		int st = resp.status;
		bool busy = st == 429 || st == 500 || st == 502 || st == 503 || st == 504;
		if (!busy || attempt == 4) break;
		free (resp.buf); resp = Resp ();
		unsigned wait = 5u << attempt;
		progress ("%s answered %d (busy): asking again in %u s (%d/4)", call.label, st, wait, attempt + 1);
		kapi_msleep (wait * 1000);
	}
	json::Writer text (false);
	bool ok = llm::extractText (call.api, call.label, resp.status, resp.body, (unsigned long) resp.bodyLen, text, err, sizeof err);
	free (resp.buf);
	if (!ok) return fail (err);
	progress ("done: %lu bytes of text", (unsigned long) text.size ());
	return finish (true, text.data (), text.size ());
}
#endif
