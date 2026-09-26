//
// groq -- ask a large language model through the Groq chat API (OpenAI-compatible,
// https://api.groq.com/openai/v1/chat/completions). The HTTPS helper behind Lisa (the
// chat app), usable on its own from the shell. A newlib app (mbedTLS), like httpsget.
//
// Usage:
//   groq <question...>        one question (+ the configured role); prints the answer
//   groq -j                   stdin = a JSON array of {"role","content"} messages (the chat
//                             history); the configured role is sent first as "system"
//   -c <path>                 another config file (default SD:/apps/lisa.app/config.ini)
//
// The config (key = value lines, '#' comments; NEVER committed, it holds the API key):
//   key         = gsk_...                   Groq API key (console.groq.com)
//   model       = llama-3.3-70b-versatile
//   role        = You are Lisa, ...         the system prompt; repeated lines are joined
//   temperature = 0.7
//   max_tokens  = 1024
//
// Output: the answer text (Latin-1, the Onyx charset) and exit code 0, or a line
// "error: ..." and exit code 1. Text going out is Latin-1 -> JSON \u00XX; the answer's
// UTF-8 / \uXXXX comes back as Latin-1 (typographic quotes / dashes folded to ASCII).
//
#define ONYX_HTTP_TLS
#include "http.hpp"
#include "kapi.h"

#include <stdlib.h>
#include <string.h>

#define GROQ_URL	"https://api.groq.com/openai/v1/chat/completions"
#define DEF_CONFIG	"SD:/apps/lisa.app/config.ini"
#define REQ_CAP		(96 * 1024)
#define RSP_CAP		(96 * 1024)

static void outs (const char *s) { kapi_stdout_write (s, (unsigned) strlen (s)); }
static int fail (const char *a, const char *b = "")
{
	outs ("error: "); outs (a); outs (b); outs ("\n");
	return 1;
}

// ---- config -------------------------------------------------------------------------
static char g_key[256], g_model[96] = "llama-3.3-70b-versatile";
static char g_temp[16] = "0.7", g_maxtok[16] = "1024";
static char g_role[4096];

static void trim (char *s)
{
	int n = (int) strlen (s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = '\0';
	int i = 0; while (s[i] == ' ' || s[i] == '\t') i++;
	if (i) memmove (s, s + i, (size_t) (n - i + 1));
}
static void setv (char *dst, unsigned cap, const char *v) { strncpy (dst, v, cap - 1); dst[cap - 1] = '\0'; }

static bool load_config (const char *path)
{
	void *f = kapi_open (path);
	if (f == 0) return false;
	static char buf[16384];
	int n = kapi_read (f, buf, sizeof buf - 1);
	kapi_close (f);
	if (n <= 0) return false;
	buf[n] = '\0';
	char *p = buf;
	if ((unsigned char) p[0] == 0xEF && (unsigned char) p[1] == 0xBB && (unsigned char) p[2] == 0xBF) p += 3;	// BOM
	while (*p)
	{
		char *line = p;
		while (*p && *p != '\n') p++;
		if (*p) *p++ = '\0';
		trim (line);
		if (line[0] == '#' || line[0] == ';' || line[0] == '[' || line[0] == '\0') continue;
		char *eq = strchr (line, '=');
		if (eq == 0) continue;
		*eq = '\0';
		char *k = line, *v = eq + 1;
		trim (k); trim (v);
		if (!strcmp (k, "key"))              setv (g_key, sizeof g_key, v);
		else if (!strcmp (k, "model"))       setv (g_model, sizeof g_model, v);
		else if (!strcmp (k, "temperature")) setv (g_temp, sizeof g_temp, v);
		else if (!strcmp (k, "max_tokens"))  setv (g_maxtok, sizeof g_maxtok, v);
		else if (!strcmp (k, "role"))
		{
			if (g_role[0]) strncat (g_role, "\n", sizeof g_role - strlen (g_role) - 1);
			strncat (g_role, v, sizeof g_role - strlen (g_role) - 1);
		}
	}
	return true;
}

// ---- JSON out (Latin-1 -> escaped JSON string) ------------------------------------------
static char *g_req; static int g_rlen;
static void put (const char *s) { while (*s && g_rlen < REQ_CAP - 1) g_req[g_rlen++] = *s++; g_req[g_rlen] = '\0'; }
static void put_json_str (const char *s)
{
	static const char hex[] = "0123456789abcdef";
	put ("\"");
	for (; *s && g_rlen < REQ_CAP - 8; s++)
	{
		unsigned char c = (unsigned char) *s;
		char e[8];
		if (c == '"' || c == '\\') { e[0] = '\\'; e[1] = (char) c; e[2] = 0; }
		else if (c == '\n') strcpy (e, "\\n");
		else if (c == '\t') strcpy (e, "\\t");
		else if (c < 0x20 || c >= 0x7F)
		{ e[0] = '\\'; e[1] = 'u'; e[2] = '0'; e[3] = '0'; e[4] = hex[c >> 4]; e[5] = hex[c & 15]; e[6] = 0; }
		else { e[0] = (char) c; e[1] = 0; }
		put (e);
	}
	put ("\"");
}

// ---- JSON in -----------------------------------------------------------------------------
// Unicode code point -> Latin-1 (appended to out); typographic punctuation folded to ASCII.
static void emit_cp (char *out, int &n, int cap, unsigned cp)
{
	const char *s = 0; char one[2] = { 0, 0 };
	if (cp < 0x100) { one[0] = (char) cp; s = one; }
	else switch (cp)
	{
	case 0x2018: case 0x2019: case 0x201A: case 0x2032: s = "'"; break;
	case 0x201C: case 0x201D: case 0x201E: case 0x2033: s = "\""; break;
	case 0x2013: case 0x2014: case 0x2212: s = "-"; break;
	case 0x2026: s = "..."; break;
	case 0x2022: case 0x2023: case 0x25CF: s = "*"; break;
	case 0x20AC: s = "EUR"; break;
	case 0x2192: s = "->"; break;
	case 0x2190: s = "<-"; break;
	case 0x00A0: case 0x202F: case 0x2009: s = " "; break;
	default:
		if (cp >= 0x1F000) s = "";			// emoji: dropped
		else s = "?";
	}
	if (cp == 0) return;
	while (*s && n < cap - 1) out[n++] = *s++;
}

static unsigned hex4 (const char *p)
{
	unsigned v = 0;
	for (int i = 0; i < 4; i++)
	{
		char c = p[i]; v <<= 4;
		if (c >= '0' && c <= '9') v |= (unsigned) (c - '0');
		else if (c >= 'a' && c <= 'f') v |= (unsigned) (c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') v |= (unsigned) (c - 'A' + 10);
	}
	return v;
}

// Decode the JSON string starting at p (just after the opening quote) into Latin-1.
static void json_str (const char *p, char *out, int cap)
{
	int n = 0;
	while (*p && *p != '"')
	{
		unsigned cp;
		unsigned char c = (unsigned char) *p;
		if (c == '\\')
		{
			p++;
			switch (*p)
			{
			case 'n': cp = '\n'; break; case 't': cp = ' '; break; case 'r': cp = 0; break;
			case 'b': case 'f': cp = 0; break;
			case 'u':
				cp = hex4 (p + 1); p += 4;
				if (cp >= 0xD800 && cp < 0xDC00 && p[1] == '\\' && p[2] == 'u')	// surrogate pair
				{ unsigned lo = hex4 (p + 3); cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p += 6; }
				break;
			default: cp = (unsigned char) *p; break;
			}
			if (*p) p++;
		}
		else if (c < 0x80) { cp = c; p++; }
		else								// raw UTF-8
		{
			int k = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
			cp = c & (k == 3 ? 0x07 : k == 2 ? 0x0F : 0x1F); p++;
			while (k-- > 0 && ((unsigned char) *p & 0xC0) == 0x80) cp = (cp << 6) | ((unsigned char) *p++ & 0x3F);
		}
		emit_cp (out, n, cap, cp);
	}
	out[n] = '\0';
}

// Find "key" : "..." after from; returns the position just after the opening quote, or 0.
static const char *find_str (const char *from, const char *key)
{
	char pat[40]; pat[0] = '"'; strcpy (pat + 1, key); strcat (pat, "\"");
	const char *p = strstr (from, pat);
	while (p)
	{
		const char *q = p + strlen (pat);
		while (*q == ' ' || *q == '\n' || *q == '\r' || *q == '\t') q++;
		if (*q == ':')
		{
			q++; while (*q == ' ' || *q == '\n' || *q == '\r' || *q == '\t') q++;
			if (*q == '"') return q + 1;
		}
		p = strstr (p + 1, pat);
	}
	return 0;
}

int main (void)
{
	static char args[1024];
	kapi_get_args (args, sizeof args);
	const char *cfg = DEF_CONFIG;
	bool json = false;
	char *q = args;
	for (;;)							// options
	{
		while (*q == ' ') q++;
		if (q[0] == '-' && q[1] == 'j' && (q[2] == ' ' || q[2] == '\0')) { json = true; q += 2; continue; }
		if (q[0] == '-' && q[1] == 'c' && q[2] == ' ')
		{
			q += 3; while (*q == ' ') q++;
			cfg = q; while (*q && *q != ' ') q++;
			if (*q) *q++ = '\0';
			continue;
		}
		break;
	}
	if (!json && *q == '\0') { outs ("usage: groq [-c config] <question> | groq -j < messages.json\n"); return 1; }
	if (!load_config (cfg)) return fail ("no config file ", cfg);
	if (g_key[0] == '\0') return fail ("no API key (key = ...) in ", cfg);

	g_req = (char *) malloc (REQ_CAP);
	char *rsp = (char *) malloc (RSP_CAP);
	if (!g_req || !rsp) return fail ("out of memory");
	g_rlen = 0; g_req[0] = '\0';

	put ("{\"model\":"); put_json_str (g_model);
	put (",\"temperature\":"); put (g_temp);
	put (",\"max_tokens\":"); put (g_maxtok);
	put (",\"messages\":[");
	bool first = true;
	if (g_role[0]) { put ("{\"role\":\"system\",\"content\":"); put_json_str (g_role); put ("}"); first = false; }
	if (json)
	{
		// The caller's array, as-is (already JSON): strip its brackets and splice it in.
		static char in[REQ_CAP / 2];
		int n = 0, r;
		while (n < (int) sizeof in - 1 && (r = kapi_stdin_read (in + n, (unsigned) (sizeof in - 1 - n))) > 0) n += r;
		in[n] = '\0';
		char *a = strchr (in, '['), *b = strrchr (in, ']');
		if (!a || !b || b < a) return fail ("stdin: expected a JSON array of messages");
		*b = '\0'; a++;
		while (*a == ' ' || *a == '\n' || *a == '\r' || *a == '\t') a++;
		if (*a) { if (!first) put (","); put (a); }
	}
	else
	{
		if (!first) put (",");
		put ("{\"role\":\"user\",\"content\":"); put_json_str (q); put ("}");
	}
	put ("]}");
	if (g_rlen >= REQ_CAP - 2) return fail ("request too long");

	HttpClient http;
	http.user_agent ("Onyx-groq/1.0").accept ("application/json").bearer (g_key).timeout_ms (90000);
	HttpResponse r = http.post_json (GROQ_URL, g_req, rsp, RSP_CAP);
	if (r.is_error ())
	{
		switch (r.status)
		{
		case HTTP_ERR_NO_NET:  return fail ("the network is down (Wi-Fi not connected?)");
		case HTTP_ERR_CONNECT: return fail ("cannot reach api.groq.com (DNS / connect)");
		case HTTP_ERR_TLS:     return fail ("TLS handshake failed");
		case HTTP_ERR_TIMEOUT: return fail ("no answer (timeout)");
		default:               return fail ("request failed");
		}
	}
	static char text[RSP_CAP];
	if (!r.ok ())
	{
		const char *m = find_str (r.body, "message");
		char code[16]; int c = r.status, k = 0; char t[8]; int tn = 0;
		while (c) { t[tn++] = (char) ('0' + c % 10); c /= 10; }
		code[k++] = 'H'; code[k++] = 'T'; code[k++] = 'T'; code[k++] = 'P'; code[k++] = ' ';
		while (tn) code[k++] = t[--tn];
		code[k++] = ':'; code[k++] = ' '; code[k] = '\0';
		if (m) { json_str (m, text, 1024); return fail (code, text); }
		return fail (code, r.status == 401 ? "invalid API key" : "");
	}
	const char *ch = strstr (r.body, "\"choices\"");
	const char *c = ch ? find_str (ch, "content") : 0;
	if (c == 0) return fail ("unexpected answer from the server");
	json_str (c, text, sizeof text);
	outs (text);
	outs ("\n");
	return 0;
}
