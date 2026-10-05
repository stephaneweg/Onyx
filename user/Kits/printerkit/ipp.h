//
// printerkit/ipp.h -- IPP (the Internet Printing Protocol, RFC 8010 / 8011; what IPP Everywhere and AirPrint
// printers speak: no driver of a make): a printer asked what it can do (Get-Printer-Attributes), a document
// sent (Print-Job, streamed -- its length is not known before it is made: HTTP's chunked encoding), a job
// followed (Get-Job-Attributes) and cancelled (Cancel-Job). Over HTTP on port 631, through a small socket
// interface: the kapi's TCP on Onyx (printerkit/ippnet.h), BSD sockets in the host test.
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
#ifndef ONYX_PRINT_IPP_H
#define ONYX_PRINT_IPP_H

#include "imagekit/img/pngsave.hpp"

namespace ipp {

using pngsave::Buf;

// the sockets: connect -> a socket >= 0 (< 0: failed); send: everything or false; recv: > 0 bytes, 0 nothing
// within the wait, < 0 closed
struct Net
{
	int  (*connect) (const char *host, int port);
	bool (*send) (int s, const void *b, unsigned n);
	int  (*recv) (int s, void *b, unsigned n, unsigned wait_ms);
	void (*close) (int s);
};

enum { OP_PRINT_JOB = 0x02, OP_VALIDATE_JOB = 0x04, OP_CANCEL_JOB = 0x08, OP_GET_JOB_ATTRS = 0x09, OP_GET_PRINTER_ATTRS = 0x0B };
enum { T_OPERATION = 0x01, T_JOB = 0x02, T_END = 0x03, T_PRINTER = 0x04,
       T_INTEGER = 0x21, T_BOOLEAN = 0x22, T_ENUM = 0x23, T_RESOLUTION = 0x32, T_RANGE = 0x33,
       T_TEXT = 0x41, T_NAME = 0x42, T_KEYWORD = 0x44, T_URI = 0x45, T_CHARSET = 0x47, T_LANGUAGE = 0x48, T_MIME = 0x49 };
enum { JOB_PENDING = 3, JOB_HELD, JOB_PROCESSING, JOB_STOPPED, JOB_CANCELED, JOB_ABORTED, JOB_COMPLETED };

static inline int slen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline bool seq (const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static inline void scpy (char *d, int cap, const char *s, int n = -1)
{
	int i = 0;
	for (; s && s[i] && i < cap - 1 && (n < 0 || i < n); i++) d[i] = s[i];
	d[i] = 0;
}
static inline void scat (char *d, int cap, const char *s) { int n = slen (d); scpy (d + n, cap - n, s); }
static inline void sint (char *d, int cap, long v)
{
	char t[24]; int n = 0; bool neg = v < 0;
	if (neg) v = -v;
	do { t[n++] = (char) ('0' + v % 10); v /= 10; } while (v);
	int k = 0;
	if (neg && k < cap - 1) d[k++] = '-';
	while (n && k < cap - 1) d[k++] = t[--n];
	d[k] = 0;
}

// ---- a printer's address: "ipp://host:631/ipp/print", "http://...", or just "192.168.0.14" ----------------
struct Uri { char host[96]; int port; char path[96]; char ipp[224]; };
static bool parse_uri (const char *s, Uri &u)
{
	while (*s == ' ') s++;
	const char *p = s;
	for (const char *q = s; *q; q++) if (q[0] == ':' && q[1] == '/' && q[2] == '/') { p = q + 3; break; }
	int n = 0;
	while (p[n] && p[n] != ':' && p[n] != '/' && p[n] != ' ') n++;
	if (n == 0 || n >= (int) sizeof u.host) return false;
	scpy (u.host, sizeof u.host, p, n);
	p += n; u.port = 631;
	if (*p == ':') { p++; u.port = 0; while (*p >= '0' && *p <= '9') u.port = u.port * 10 + (*p++ - '0'); if (u.port <= 0 || u.port > 65535) return false; }
	if (*p == '/') { n = 0; while (p[n] && p[n] != ' ') n++; scpy (u.path, sizeof u.path, p, n); }
	else scpy (u.path, sizeof u.path, "/ipp/print");
	char num[12]; sint (num, sizeof num, u.port);
	scpy (u.ipp, sizeof u.ipp, "ipp://"); scat (u.ipp, sizeof u.ipp, u.host); scat (u.ipp, sizeof u.ipp, ":"); scat (u.ipp, sizeof u.ipp, num); scat (u.ipp, sizeof u.ipp, u.path);
	return true;
}

// ---- a request: its attributes --------------------------------------------------------------------------------
struct Msg
{
	Buf b;
	void begin (int op, int id)
	{
		b.n = 0;
		b.put ((unsigned char) 2); b.put ((unsigned char) 0);			// IPP 2.0
		b.put ((unsigned char) (op >> 8)); b.put ((unsigned char) op);
		b.be32 ((unsigned) id);
		group (T_OPERATION);
		str (T_CHARSET, "attributes-charset", "utf-8");
		str (T_LANGUAGE, "attributes-natural-language", "en");
	}
	void group (int tag) { b.put ((unsigned char) tag); }
	void raw (int tag, const char *name, const void *v, int n)
	{
		int l = slen (name);
		b.put ((unsigned char) tag); b.put ((unsigned char) (l >> 8)); b.put ((unsigned char) l); b.put (name, (unsigned) l);
		b.put ((unsigned char) (n >> 8)); b.put ((unsigned char) n); b.put (v, (unsigned) n);
	}
	void str (int tag, const char *name, const char *v) { raw (tag, name, v, slen (v)); }
	void num (int tag, const char *name, int v) { unsigned char t[4] = { (unsigned char) (v >> 24), (unsigned char) (v >> 16), (unsigned char) (v >> 8), (unsigned char) v }; raw (tag, name, t, 4); }
	void boolean (const char *name, bool v) { unsigned char t = v ? 1 : 0; raw (T_BOOLEAN, name, &t, 1); }
	void range (const char *name, int lo, int hi)
	{
		unsigned char t[8] = { (unsigned char) (lo >> 24), (unsigned char) (lo >> 16), (unsigned char) (lo >> 8), (unsigned char) lo,
				       (unsigned char) (hi >> 24), (unsigned char) (hi >> 16), (unsigned char) (hi >> 8), (unsigned char) hi };
		raw (T_RANGE, name, t, 8);
	}
	void end () { b.put ((unsigned char) T_END); }
};

// ---- an answer: its status, its attributes one after the other ------------------------------------------------
struct Attr { int group, tag; char name[64]; const unsigned char *v; int len; };
struct Parser
{
	const unsigned char *d; unsigned n, i; int group; char last[64];
	int status;
	bool begin (const unsigned char *data, unsigned len)
	{
		d = data; n = len; i = 8; group = 0; last[0] = 0; status = 0;
		if (len < 9) return false;
		status = d[2] << 8 | d[3];
		return true;
	}
	bool next (Attr &a)
	{
		for (;;)
		{
			if (i >= n) return false;
			int tag = d[i++];
			if (tag == T_END) return false;
			if (tag < 0x10) { group = tag; continue; }
			if (i + 2 > n) return false;
			unsigned nl = (unsigned) d[i] << 8 | d[i + 1]; i += 2;
			if (i + nl + 2 > n) return false;
			if (nl) scpy (last, sizeof last, (const char *) d + i, (int) nl);
			i += nl;
			unsigned vl = (unsigned) d[i] << 8 | d[i + 1]; i += 2;
			if (i + vl > n) return false;
			a.group = group; a.tag = tag; scpy (a.name, sizeof a.name, last); a.v = d + i; a.len = (int) vl;
			i += vl;
			return true;
		}
	}
	static int integer (const Attr &a) { return a.len >= 4 ? (int) ((unsigned) a.v[0] << 24 | (unsigned) a.v[1] << 16 | (unsigned) a.v[2] << 8 | a.v[3]) : 0; }
	static void text (const Attr &a, char *out, int cap) { scpy (out, cap, (const char *) a.v, a.len); }
};

// ---- HTTP: a POST of application/ipp, its body in chunks; the answer's body --------------------------------------
struct Http
{
	const Net *net; int s; char err[96];
	Http () : net (0), s (-1) { err[0] = 0; }
	~Http () { shut (); }
	void shut () { if (s >= 0 && net) net->close (s); s = -1; }

	bool open (const Net *nt, const Uri &u)
	{
		net = nt;
		s = net->connect (u.host, u.port);
		if (s < 0) { scpy (err, sizeof err, "the printer does not answer"); return false; }
		char h[512], num[12]; sint (num, sizeof num, u.port);
		scpy (h, sizeof h, "POST "); scat (h, sizeof h, u.path); scat (h, sizeof h, " HTTP/1.1\r\nHost: "); scat (h, sizeof h, u.host);
		scat (h, sizeof h, ":"); scat (h, sizeof h, num);
		scat (h, sizeof h, "\r\nUser-Agent: Onyx-print\r\nContent-Type: application/ipp\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n");
		return put (h, (unsigned) slen (h));
	}
	bool put (const void *b, unsigned n)
	{
		if (s < 0) return false;
		if (!net->send (s, b, n)) { scpy (err, sizeof err, "the connection to the printer was lost"); shut (); return false; }
		return true;
	}
	bool chunk (const void *b, unsigned n)
	{
		if (!n) return true;
		static const char *H = "0123456789ABCDEF";
		char h[12]; int k = 0; bool on = false;
		for (int sh = 28; sh >= 0; sh -= 4) { unsigned v = (n >> sh) & 15; if (v || on || sh == 0) { h[k++] = H[v]; on = true; } }
		h[k++] = '\r'; h[k++] = '\n';
		return put (h, (unsigned) k) && put (b, n) && put ("\r\n", 2);
	}
	// the request's end, then the answer: its body (the IPP message) into out
	bool finish (Buf &out, unsigned wait_ms = 60000)
	{
		if (!put ("0\r\n\r\n", 5)) return false;
		Buf r; unsigned idle = 0;
		for (;;)
		{
			unsigned char t[4096];
			int k = net->recv (s, t, sizeof t, 200);
			if (k > 0) { r.put (t, (unsigned) k); idle = 0; if (complete (r)) break; continue; }
			if (k < 0) break;
			idle += 200;
			if (idle >= wait_ms) { scpy (err, sizeof err, "the printer did not answer in time"); shut (); return false; }
		}
		shut ();
		return body (r, out);
	}

private:
	// the head's end (after any "100 Continue") -> the body's offset, 0: not there yet
	static unsigned head_end (const Buf &r, unsigned from)
	{
		for (unsigned i = from; i + 3 < r.n; i++) if (r.b[i] == '\r' && r.b[i + 1] == '\n' && r.b[i + 2] == '\r' && r.b[i + 3] == '\n') return i + 4;
		return 0;
	}
	static int status_at (const Buf &r, unsigned at)
	{
		if (r.n < at + 12) return 0;
		return (r.b[at + 9] - '0') * 100 + (r.b[at + 10] - '0') * 10 + (r.b[at + 11] - '0');
	}
	static bool header (const Buf &r, unsigned from, unsigned to, const char *name, char *out, int cap)
	{
		int nl = slen (name);
		for (unsigned i = from; i + nl < to; i++)
		{
			if (i != from && r.b[i - 1] != '\n') continue;
			int k = 0;
			while (k < nl && ((r.b[i + k] | 32) == (name[k] | 32))) k++;
			if (k < nl || r.b[i + nl] != ':') continue;
			unsigned j = i + nl + 1; while (j < to && r.b[j] == ' ') j++;
			int o = 0; while (j < to && r.b[j] != '\r' && o < cap - 1) out[o++] = (char) r.b[j++];
			out[o] = 0;
			return true;
		}
		return false;
	}
	static unsigned start (const Buf &r, unsigned *bodyAt)
	{
		unsigned at = 0;
		for (;;)
		{
			unsigned e = head_end (r, at);
			if (!e) return (unsigned) -1;
			if (status_at (r, at) / 100 != 1) { *bodyAt = e; return at; }
			at = e;
		}
	}
	static bool complete (const Buf &r)
	{
		unsigned b = 0, at = start (r, &b);
		if (at == (unsigned) -1) return false;
		char v[32];
		if (header (r, at, b, "Content-Length", v, sizeof v)) { unsigned n = 0; for (int i = 0; v[i] >= '0' && v[i] <= '9'; i++) n = n * 10 + (unsigned) (v[i] - '0'); return r.n >= b + n; }
		if (header (r, at, b, "Transfer-Encoding", v, sizeof v))
		{	// the chunks walked to the empty one
			unsigned i = b;
			for (;;)
			{
				unsigned n = 0, j = i; bool any = false;
				for (; j < r.n; j++)
				{
					int c = r.b[j], d = c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1;
					if (d < 0) break;
					n = n * 16 + (unsigned) d; any = true;
				}
				while (j < r.n && r.b[j] != '\n') j++;
				if (!any || j >= r.n) return false;
				if (n == 0) return true;
				i = j + 1 + n + 2;
				if (i > r.n) return false;
			}
		}
		return false;					// (neither: the body ends when the printer closes)
	}
	bool body (const Buf &r, Buf &out)
	{
		unsigned b = 0, at = start (r, &b);
		if (at == (unsigned) -1) { scpy (err, sizeof err, "the printer's answer is not HTTP"); return false; }
		int st = status_at (r, at);
		if (st != 200)
		{
			scpy (err, sizeof err, st == 401 || st == 403 ? "the printer asks for a password" : st == 404 ? "no printer at this address (the path)" : st == 426 ? "the printer wants an encrypted connection" : "the printer refused the request (HTTP ");
			if (st != 401 && st != 403 && st != 404 && st != 426) { char num[12]; sint (num, sizeof num, st); scat (err, sizeof err, num); scat (err, sizeof err, ")"); }
			return false;
		}
		char v[32];
		out.n = 0;
		if (header (r, at, b, "Transfer-Encoding", v, sizeof v) && (v[0] | 32) == 'c')
		{
			unsigned i = b;
			while (i < r.n)
			{
				unsigned n = 0, j = i;
				for (; j < r.n; j++)
				{
					int c = r.b[j], d = c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1;
					if (d < 0) break;
					n = n * 16 + (unsigned) d;
				}
				while (j < r.n && r.b[j] != '\n') j++;
				j++;
				if (n == 0 || j + n > r.n) break;
				out.put (r.b + j, n);
				i = j + n + 2;
			}
		}
		else if (r.n > b) out.put (r.b + b, r.n - b);
		if (out.n < 9) { scpy (err, sizeof err, "the printer's answer is empty"); return false; }
		return true;
	}
};

// one request with no document, its answer
static bool roundtrip (const Net *net, const Uri &u, Msg &m, Buf &out, char *err, int cap)
{
	Http h;
	bool ok = h.open (net, u) && h.chunk (m.b.b, m.b.n) && h.finish (out);
	if (!ok) scpy (err, cap, h.err);
	return ok;
}

// what IPP's status says, for a person
static const char *status_text (int st)
{
	switch (st)
	{
	case 0x0400: return "the printer did not understand the request";
	case 0x0401: case 0x0402: case 0x0403: return "the printer does not allow it";
	case 0x0406: return "the printer or the job was not found";
	case 0x040A: return "the printer does not take this kind of document";
	case 0x040B: return "the printer does not take these settings";
	case 0x0506: return "the printer accepts no job for now";
	case 0x0507: return "the printer is busy";
	case 0x0508: return "the job was cancelled";
	default:     return st >= 0x0500 ? "the printer reported an error" : st >= 0x0400 ? "the printer refused the request" : "";
	}
}

// ---- what a printer can do ---------------------------------------------------------------------------------------
struct Caps
{
	char name[64], model[64], formats[256], state_reasons[128];
	bool pwg, urf, pdf, jpeg, color, gray8, rgb8, duplex;
	int dpi, copies, state;
	unsigned quality;				// bits 3 (draft), 4 (normal), 5 (high)
	char media[1024], media_default[64];		// IPP's names, comma separated
	int margin[4];					// left, top, right, bottom: 1/100 mm
	int ink[4]; char ink_name[4][32]; int ninks;	// the cartridges' levels (%)
};
static void list_add (char *list, int cap, const char *v)
{
	if (list[0]) scat (list, cap, ",");
	scat (list, cap, v);
}
static bool get_caps (const Net *net, const char *address, Caps &c, char *err, int cap)
{
	Uri u;
	if (!parse_uri (address, u)) { scpy (err, cap, "this is not a printer's address"); return false; }
	Msg m; m.begin (OP_GET_PRINTER_ATTRS, 1);
	m.str (T_URI, "printer-uri", u.ipp);
	m.str (T_KEYWORD, "requested-attributes", "all");
	m.end ();
	Buf out;
	if (!roundtrip (net, u, m, out, err, cap)) return false;
	Parser p;
	if (!p.begin (out.b, out.n) || p.status >= 0x0400) { scpy (err, cap, p.status ? status_text (p.status) : "the printer's answer cannot be read"); return false; }
	for (unsigned i = 0; i < sizeof c; i++) ((char *) &c)[i] = 0;
	c.copies = 1; c.dpi = 0;
	Attr a; char v[128]; int res300 = 0, resFirst = 0, nlev = 0;
	while (p.next (a))
	{
		Parser::text (a, v, sizeof v);
		if (seq (a.name, "printer-make-and-model")) scpy (c.model, sizeof c.model, v);
		else if (seq (a.name, "printer-name")) scpy (c.name, sizeof c.name, v);
		else if (seq (a.name, "printer-state") && a.tag == T_ENUM) c.state = Parser::integer (a);
		else if (seq (a.name, "printer-state-reasons")) list_add (c.state_reasons, sizeof c.state_reasons, v);
		else if (seq (a.name, "document-format-supported"))
		{
			list_add (c.formats, sizeof c.formats, v);
			if (seq (v, "image/pwg-raster")) c.pwg = true;
			else if (seq (v, "image/urf")) c.urf = true;
			else if (seq (v, "application/pdf")) c.pdf = true;
			else if (seq (v, "image/jpeg")) c.jpeg = true;
		}
		else if (seq (a.name, "color-supported") && a.len >= 1) c.color = a.v[0] != 0;
		else if (seq (a.name, "copies-supported") && a.tag == T_RANGE && a.len >= 8) c.copies = (int) ((unsigned) a.v[4] << 24 | (unsigned) a.v[5] << 16 | (unsigned) a.v[6] << 8 | a.v[7]);
		else if (seq (a.name, "print-quality-supported") && a.tag == T_ENUM) { int q = Parser::integer (a); if (q >= 3 && q <= 5) c.quality |= 1u << q; }
		else if (seq (a.name, "sides-supported")) { if (!seq (v, "one-sided")) c.duplex = true; }
		else if (seq (a.name, "media-supported")) { if (v[0] != 'c' || v[1] != 'u') list_add (c.media, sizeof c.media, v); }	// (not custom_min / custom_max)
		else if (seq (a.name, "media-default")) scpy (c.media_default, sizeof c.media_default, v);
		else if (seq (a.name, "media-left-margin-supported") && !c.margin[0]) c.margin[0] = Parser::integer (a);
		else if (seq (a.name, "media-top-margin-supported") && !c.margin[1]) c.margin[1] = Parser::integer (a);
		else if (seq (a.name, "media-right-margin-supported") && !c.margin[2]) c.margin[2] = Parser::integer (a);
		else if (seq (a.name, "media-bottom-margin-supported") && !c.margin[3]) c.margin[3] = Parser::integer (a);
		else if (seq (a.name, "pwg-raster-document-type-supported")) { if (seq (v, "sgray_8")) c.gray8 = true; else if (seq (v, "srgb_8")) c.rgb8 = true; }
		else if (seq (a.name, "pwg-raster-document-resolution-supported") && a.tag == T_RESOLUTION && a.len >= 9)
		{
			int r = Parser::integer (a);
			if (a.v[8] == 4) r = r * 254 / 100;			// (dots per centimetre)
			if (!resFirst) resFirst = r;
			if (r == 300) res300 = r;
		}
		else if (seq (a.name, "marker-names") && c.ninks < 4) scpy (c.ink_name[c.ninks++], 32, v);
		else if (seq (a.name, "marker-levels") && a.tag == T_INTEGER && nlev < 4) c.ink[nlev++] = Parser::integer (a);
	}
	c.dpi = res300 ? res300 : resFirst;
	if (!c.quality) c.quality = 1u << 4;
	if (!c.model[0]) scpy (c.model, sizeof c.model, c.name[0] ? c.name : "IPP printer");
	return true;
}

// ---- a job sent: begin, the document's bytes as they come, end -> the printer's job number --------------------
struct JobSetup
{
	const char *title, *user, *format, *media;
	int copies, quality; bool mono;
};
struct Send
{
	Http h; char err[96];
	bool begin (const Net *net, const char *address, const JobSetup &js, bool validateOnly = false)
	{
		Uri u; err[0] = 0;
		if (!parse_uri (address, u)) { scpy (err, sizeof err, "this is not a printer's address"); return false; }
		Msg m; m.begin (validateOnly ? OP_VALIDATE_JOB : OP_PRINT_JOB, 2);
		m.str (T_URI, "printer-uri", u.ipp);
		m.str (T_NAME, "requesting-user-name", js.user && js.user[0] ? js.user : "onyx");
		m.str (T_NAME, "job-name", js.title && js.title[0] ? js.title : "Onyx document");
		m.str (T_MIME, "document-format", js.format);
		m.group (T_JOB);
		if (js.copies > 1) m.num (T_INTEGER, "copies", js.copies);
		if (js.media && js.media[0]) m.str (T_KEYWORD, "media", js.media);
		if (js.quality >= 3 && js.quality <= 5) m.num (T_ENUM, "print-quality", js.quality);
		m.str (T_KEYWORD, "print-color-mode", js.mono ? "monochrome" : "color");
		m.end ();
		if (!h.open (net, u) || !h.chunk (m.b.b, m.b.n)) { scpy (err, sizeof err, h.err); return false; }
		return true;
	}
	bool write (const void *b, unsigned n) { if (!h.chunk (b, n)) { scpy (err, sizeof err, h.err); return false; } return true; }
	// -> the job's number at the printer (0 for a validation), -1: refused (err says why)
	int end ()
	{
		Buf out;
		if (!h.finish (out, 120000)) { scpy (err, sizeof err, h.err); return -1; }
		Parser p; Attr a; int id = 0;
		if (!p.begin (out.b, out.n)) { scpy (err, sizeof err, "the printer's answer cannot be read"); return -1; }
		char why[96]; why[0] = 0;
		while (p.next (a))
		{
			if (seq (a.name, "job-id") && a.tag == T_INTEGER) id = Parser::integer (a);
			else if (seq (a.name, "status-message") || seq (a.name, "detailed-status-message")) { if (!why[0]) Parser::text (a, why, sizeof why); }
		}
		if (p.status >= 0x0400) { scpy (err, sizeof err, status_text (p.status)); if (why[0]) { scat (err, sizeof err, ": "); scat (err, sizeof err, why); } return -1; }
		return id;
	}
};

// a job's state at the printer (JOB_*; reasons: why, e.g. "media-empty"); < 0: no answer
static int job_state (const Net *net, const char *address, int id, char *reasons, int cap)
{
	Uri u; char err[96];
	if (reasons) reasons[0] = 0;
	if (!parse_uri (address, u)) return -1;
	Msg m; m.begin (OP_GET_JOB_ATTRS, 3);
	m.str (T_URI, "printer-uri", u.ipp);
	m.num (T_INTEGER, "job-id", id);
	m.str (T_NAME, "requesting-user-name", "onyx");
	m.end ();
	Buf out;
	if (!roundtrip (net, u, m, out, err, sizeof err)) return -1;
	Parser p; Attr a; int st = -1;
	if (!p.begin (out.b, out.n)) return -1;
	if (p.status == 0x0406) return JOB_COMPLETED;				// (the printer forgot it: it is done)
	while (p.next (a))
	{
		if (seq (a.name, "job-state") && a.tag == T_ENUM) st = Parser::integer (a);
		else if (seq (a.name, "job-state-reasons") && reasons) { char v[64]; Parser::text (a, v, sizeof v); if (!seq (v, "none")) list_add (reasons, cap, v); }
	}
	return st;
}
static bool cancel_job (const Net *net, const char *address, int id)
{
	Uri u; char err[96];
	if (!parse_uri (address, u)) return false;
	Msg m; m.begin (OP_CANCEL_JOB, 4);
	m.str (T_URI, "printer-uri", u.ipp);
	m.num (T_INTEGER, "job-id", id);
	m.str (T_NAME, "requesting-user-name", "onyx");
	m.end ();
	Buf out;
	return roundtrip (net, u, m, out, err, sizeof err);
}

} // namespace ipp

#endif
