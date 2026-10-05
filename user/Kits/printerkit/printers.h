//
// printerkit/printers.h -- the printers Onyx knows (SD:/etc/printers.ini) and the papers' names; printd's
// protocol (the service "print"). Shared by the library (printerkit/printerkit.cpp), the daemon (Apps/printd) and the
// host tests; no libc.
//
// printers.ini -- written by printd when a printer is added (what the printer said it can do is kept, so
// the Print dialog shows its options without asking it again):
//   default = HP1469F0
//   [HP1469F0]
//   kind = ipp                      ("pdf": the PDF printer, always there; "ipp": a network printer)
//   uri = ipp://192.168.0.14:631/ipp/print
//   model = HP DeskJet 2700 series
//   format = image/pwg-raster       (what printd sends it)
//   color = 1   dpi = 300   copies = 99   quality = 3,4,5
//   media = iso_a4_210x297mm,...    media-default = iso_a4_210x297mm
//   margins = 296,296,296,1270      (left, top, right, bottom: 1/100 mm the printer cannot print on)
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
#ifndef ONYX_PRINT_PRINTERS_H
#define ONYX_PRINT_PRINTERS_H

#include "printerkit/pio.h"
#include "img/pngsave.hpp"

#ifdef PRINT_HOST
#define PRINT_INI	"printers.ini"
#define PRINT_SPOOL	"spool"
#define PRINT_REPLY	"reply"
#else
#define PRINT_INI	"SD:/etc/printers.ini"
#define PRINT_SPOOL	"SD:/var/spool/print"		// <id>.opj (the pages, printerkit/job.h), <id>.job (the ticket)
#define PRINT_REPLY	"RAM:/print"			// printd's answers (a file each: an app's mailbox is its own)
#endif
#define PRINT_PDF_NAME	"PDF"				// the PDF printer's name

// ---- printd's protocol: a mailbox message to the service; the answer is a file the request names --------
#define PRINTD_SERVICE	"print"
enum
{
	PD_SUBMIT = 1,		// PdReq: id = the job (its .opj and .job are in the spool) -> PdAnswer
	PD_LIST,		// PdReq -> PdJob[]
	PD_CANCEL,		// PdReq: id -> PdAnswer
	PD_ADD,			// PdReq: name, uri -> PdAnswer (the printer asked, added)
	PD_REMOVE,		// PdReq: name -> PdAnswer
	PD_DEFAULT,		// PdReq: name -> PdAnswer
	PD_FORGET,		// PdReq: the finished jobs dropped from the list -> PdAnswer
	PD_REFRESH,		// PdReq: name -> PdAnswer (the printer asked again; text: its state)
	PD_SCAN,		// PdReq -> PdFound[] (the local network searched for printers)
};
struct PdReq { unsigned token; unsigned id; char name[64]; char uri[160]; };
struct PdAnswer { int ok; char text[160]; };
struct PdFound { char address[32], name[64], model[64]; int usable; };	// usable: Onyx can print on it
enum { PJ_QUEUED = 0, PJ_PREPARING, PJ_SENDING, PJ_PRINTING, PJ_DONE, PJ_FAILED, PJ_CANCELED };
struct PdJob { unsigned id; int state, pages, page; char printer[64], title[80], message[96]; };

namespace pprt {

static inline int slen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline bool seq (const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static inline bool starts (const char *s, const char *p) { while (*p) if (*s++ != *p++) return false; return true; }
static inline void scpy (char *d, int cap, const char *s, int n = -1)
{
	int i = 0;
	for (; s && s[i] && i < cap - 1 && (n < 0 || i < n); i++) d[i] = s[i];
	d[i] = 0;
}
static inline void scat (char *d, int cap, const char *s) { int n = slen (d); scpy (d + n, cap - n, s); }
static inline int sint (const char *s) { int v = 0; bool neg = *s == '-'; if (neg) s++; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return neg ? -v : v; }
static inline void putint (pngsave::Buf &b, long v)
{
	char t[24]; int n = 0;
	if (v < 0) { b.put ((unsigned char) '-'); v = -v; }
	do { t[n++] = (char) ('0' + v % 10); v /= 10; } while (v);
	while (n) b.put ((unsigned char) t[--n]);
}
static inline void puts_ (pngsave::Buf &b, const char *s) { b.put (s, (unsigned) slen (s)); }
static inline void put_kv (pngsave::Buf &b, const char *k, const char *v) { puts_ (b, k); puts_ (b, " = "); puts_ (b, v); b.put ((unsigned char) '\n'); }
static inline void put_ki (pngsave::Buf &b, const char *k, long v) { puts_ (b, k); puts_ (b, " = "); putint (b, v); b.put ((unsigned char) '\n'); }

// ---- an .ini text walked: sections, keys ----
struct Ini
{
	char *t; char *p; char section[64], key[32], val[1100];
	Ini (char *text) : t (text), p (text) { section[0] = 0; }
	// the next "key = value" (section: the "[...]" it is under) -> false at the end
	bool next ()
	{
		while (p && *p)
		{
			while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
			char *e = p; while (*e && *e != '\n') e++;
			char *l = p; p = *e ? e + 1 : e;
			while (e > l && (e[-1] == '\r' || e[-1] == ' ')) e--;
			if (*l == '[') { char *c = l + 1; int n = 0; while (c < e && *c != ']' && n < 63) section[n++] = *c++; section[n] = 0; continue; }
			if (*l == '#' || *l == ';' || l == e) continue;
			char *q = l; while (q < e && *q != '=') q++;
			if (q == e) continue;
			char *ke = q; while (ke > l && ke[-1] == ' ') ke--;
			scpy (key, sizeof key, l, (int) (ke - l));
			q++; while (q < e && *q == ' ') q++;
			scpy (val, sizeof val, q, (int) (e - q));
			return true;
		}
		return false;
	}
};

// ---- the printers ----
struct Printer
{
	char name[64], kind[8], uri[160], model[64], format[32];
	int color, dpi, copies; unsigned quality;	// quality: bits 3 (draft), 4 (normal), 5 (high)
	char media[1024], media_default[64];
	int margin[4];
};
enum { MAXPRINTERS = 12 };
struct List { Printer p[MAXPRINTERS]; int n; char def[64]; };

static inline void pdf_printer (Printer &p)
{
	for (unsigned i = 0; i < sizeof p; i++) ((char *) &p)[i] = 0;
	scpy (p.name, sizeof p.name, PRINT_PDF_NAME); scpy (p.kind, sizeof p.kind, "pdf");
	scpy (p.model, sizeof p.model, "Saves the pages as a PDF file");
	p.color = 1; p.copies = 1; p.quality = 1u << 4;
	scpy (p.media, sizeof p.media, "iso_a4_210x297mm,iso_a5_148x210mm,iso_a3_297x420mm,na_letter_8.5x11in,na_legal_8.5x14in");
	scpy (p.media_default, sizeof p.media_default, "iso_a4_210x297mm");
}
// the list: the PDF printer first (it needs no file), then the file's
static void load (List &l)
{
	l.n = 1; l.def[0] = 0;
	pdf_printer (l.p[0]);
	char *t = pio_load (PRINT_INI, 0);
	if (t)
	{
		Ini ini (t); Printer *cur = 0; char sec[64]; sec[0] = 0;
		while (ini.next ())
		{
			if (!ini.section[0]) { if (seq (ini.key, "default")) scpy (l.def, sizeof l.def, ini.val); continue; }
			if (!seq (sec, ini.section))
			{
				scpy (sec, sizeof sec, ini.section); cur = 0;
				if (seq (sec, PRINT_PDF_NAME) || l.n >= MAXPRINTERS) continue;
				cur = &l.p[l.n++];
				for (unsigned i = 0; i < sizeof *cur; i++) ((char *) cur)[i] = 0;
				scpy (cur->name, sizeof cur->name, sec); scpy (cur->kind, sizeof cur->kind, "ipp");
				cur->copies = 1; cur->dpi = 300; cur->quality = 1u << 4;
			}
			if (!cur) continue;
			const char *k = ini.key, *v = ini.val;
			if (seq (k, "kind")) scpy (cur->kind, sizeof cur->kind, v);
			else if (seq (k, "uri")) scpy (cur->uri, sizeof cur->uri, v);
			else if (seq (k, "model")) scpy (cur->model, sizeof cur->model, v);
			else if (seq (k, "format")) scpy (cur->format, sizeof cur->format, v);
			else if (seq (k, "color")) cur->color = sint (v);
			else if (seq (k, "dpi")) cur->dpi = sint (v);
			else if (seq (k, "copies")) cur->copies = sint (v);
			else if (seq (k, "quality")) { cur->quality = 0; for (const char *q = v; *q; q++) if (*q >= '3' && *q <= '5') cur->quality |= 1u << (*q - '0'); }
			else if (seq (k, "media")) scpy (cur->media, sizeof cur->media, v);
			else if (seq (k, "media-default")) scpy (cur->media_default, sizeof cur->media_default, v);
			else if (seq (k, "margins")) { const char *q = v; for (int i = 0; i < 4; i++) { cur->margin[i] = sint (q); while (*q && *q != ',') q++; if (*q) q++; } }
		}
		delete[] t;
	}
	bool have = false;
	for (int i = 0; i < l.n; i++) if (seq (l.p[i].name, l.def)) have = true;
	if (!have) scpy (l.def, sizeof l.def, l.n > 1 ? l.p[1].name : PRINT_PDF_NAME);
}
static bool save (const List &l)
{
	pngsave::Buf b;
	puts_ (b, "# The printers (Control Panel > Printers; written by printd)\n");
	put_kv (b, "default", l.def);
	for (int i = 0; i < l.n; i++)
	{
		const Printer &p = l.p[i];
		if (seq (p.kind, "pdf")) continue;
		puts_ (b, "\n["); puts_ (b, p.name); puts_ (b, "]\n");
		put_kv (b, "kind", p.kind); put_kv (b, "uri", p.uri); put_kv (b, "model", p.model); put_kv (b, "format", p.format);
		put_ki (b, "color", p.color); put_ki (b, "dpi", p.dpi); put_ki (b, "copies", p.copies);
		puts_ (b, "quality = "); bool any = false;
		for (int q = 3; q <= 5; q++) if (p.quality & (1u << q)) { if (any) b.put ((unsigned char) ','); putint (b, q); any = true; }
		b.put ((unsigned char) '\n');
		put_kv (b, "media", p.media); put_kv (b, "media-default", p.media_default);
		puts_ (b, "margins = "); for (int k = 0; k < 4; k++) { if (k) b.put ((unsigned char) ','); putint (b, p.margin[k]); }
		b.put ((unsigned char) '\n');
	}
	return pio_save (PRINT_INI, b.b ? b.b : (const unsigned char *) "", b.n);
}
static inline const Printer *find (const List &l, const char *name)
{
	for (int i = 0; i < l.n; i++) if (seq (l.p[i].name, name)) return &l.p[i];
	return 0;
}

// ---- the papers: IPP's names ("iso_a4_210x297mm", "na_letter_8.5x11in") ----
// the i-th name of a comma-separated list -> false: no more
static bool list_item (const char *list, int i, char *out, int cap)
{
	const char *p = list;
	for (; i > 0; i--) { while (*p && *p != ',') p++; if (!*p) return false; p++; }
	while (*p == ' ') p++;
	if (!*p) return false;
	int n = 0; while (p[n] && p[n] != ',') n++;
	scpy (out, cap, p, n);
	return true;
}
// a paper's size in points (upright), from its name's end: <width>x<height><mm|in>
static bool media_size (const char *name, float *w, float *h)
{
	int n = slen (name), u = n;
	while (u > 0 && name[u - 1] != '_') u--;
	if (u == 0) return false;
	float v[2] = { 0, 0 }; const char *p = name + u;
	for (int k = 0; k < 2; k++)
	{
		float f = 0, scale = 1; bool frac = false;
		while ((*p >= '0' && *p <= '9') || *p == '.')
		{
			if (*p == '.') frac = true;
			else if (frac) { scale /= 10; f += (*p - '0') * scale; }
			else f = f * 10 + (*p - '0');
			p++;
		}
		v[k] = f;
		if (k == 0) { if (*p != 'x') return false; p++; }
	}
	float per = p[0] == 'i' ? 72.0f : p[0] == 'm' ? 72.0f / 25.4f : 0;
	if (per == 0 || v[0] <= 0 || v[1] <= 0) return false;
	*w = v[0] * per; *h = v[1] * per;
	return true;
}
// a paper's name for a person: "A4", "Letter", or its size
static void media_label (const char *name, char *out, int cap)
{
	static const char *const K[][2] = {
		{ "iso_a3_", "A3" }, { "iso_a4_", "A4" }, { "iso_a5_", "A5" }, { "iso_a6_", "A6" }, { "iso_b5_", "B5" }, { "jis_b5_", "B5 (JIS)" },
		{ "na_letter_", "Letter" }, { "na_legal_", "Legal" }, { "na_executive_", "Executive" }, { "na_govt-letter_", "Government Letter" },
		{ "na_number-10_", "Envelope #10" }, { "iso_dl_", "Envelope DL" }, { "iso_c6_", "Envelope C6" }, { "iso_c5_", "Envelope C5" },
		{ "jpn_hagaki_", "Hagaki" }, { "jpn_chou3_", "Envelope Chou 3" }, { "jpn_chou4_", "Envelope Chou 4" },
		{ "na_index-4x6_", "Photo 4 x 6 in" }, { "na_index-5x8_", "Card 5 x 8 in" }, { "na_5x7_", "Photo 5 x 7 in" },
		{ "om_small-photo_", "Photo 10 x 15 cm" }, { "oe_photo-l_", "Photo 3.5 x 5 in" }, { "jpn_photo-2l_", "Photo 2L" } };
	for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++) if (starts (name, K[i][0])) { scpy (out, cap, K[i][1]); return; }
	int n = slen (name), u = n;
	while (u > 0 && name[u - 1] != '_') u--;
	scpy (out, cap, name + u);
}

} // namespace pprt

#endif
