//
// Apps/mail/ui.h -- Mail's drawing helpers: the faces (DejaVu Sans at a few sizes), text cut to fit, the icons
// (drawn from their geometry: wtk/vpaint.h), the round avatars with their initials, the hit lists of the parts that
// are drawn by hand (the folders, the conversations, the message's header).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _mail_ui_h
#define _mail_ui_h

#include "ft/wtkface.h"
#include "wtk/wtk.h"
#include "mail/util.h"

namespace mailapp {

using namespace wtk;

// (style 1 here is bold: wtk's 2 -- Mail draws no italics in its own parts)
static inline int B_ (int style) { return style == 1 ? 2 : style; }

// ---- the faces --------------------------------------------------------------------------------------------------------
enum { F_UI, F_SMALL, F_MID, F_H2, F_H1, F_TINY, F_N };
static FtTextFace *g_face[F_N];
static void faces_open ()
{
	static const int SZ[F_N] = { 13, 11, 14, 17, 24, 9 };
	for (int i = 1; i < F_N; i++) { g_face[i] = new FtTextFace; if (!g_face[i]->open ("DejaVu Sans", SZ[i])) { delete g_face[i]; g_face[i] = 0; } }
}
static inline int tw (const char *s, int f = F_UI, int style = 0) { WkFaceScope sc (f == F_UI ? 0 : g_face[f]); return wk_tw (s, B_ (style)); }
static inline int fh (int f = F_UI) { WkFaceScope sc (f == F_UI ? 0 : g_face[f]); return wk_fh (); }
// s at x, the line's top y; cut with "..." past w (0: not cut)
static inline void text (Canvas &cv, int x, int y, const char *s, unsigned c, int f = F_UI, int style = 0, int w = 0)
{
	WkFaceScope sc (f == F_UI ? 0 : g_face[f]);
	style = B_ (style);
	if (w > 0 && wk_tw (s, style) > w) { char b[600]; wk_text_fit (s, w, b, sizeof b, style); wk_text (cv, x, y, b, c, style); }
	else wk_text (cv, x, y, s, c, style);
}
static inline void text_v (Canvas &cv, int x, int y, int h, const char *s, unsigned c, int f = F_UI, int style = 0, int w = 0) { text (cv, x, y + (h - fh (f)) / 2, s, c, f, style, w); }
static inline void text_r (Canvas &cv, int xr, int y, int h, const char *s, unsigned c, int f = F_UI, int style = 0) { text (cv, xr - tw (s, f, style), y + (h - fh (f)) / 2, s, c, f, style); }
static inline void text_c (Canvas &cv, int x, int y, int w, int h, const char *s, unsigned c, int f = F_UI, int style = 0) { text (cv, x + (w - tw (s, f, style)) / 2, y + (h - fh (f)) / 2, s, c, f, style); }

static inline void wk_fill_round (Canvas &cv, int x, int y, int w, int h, int r, unsigned c) { wk_rbox (cv, x, y, w, h, r, c, c); }

// a text field that says what goes in it while empty
class HintBox : public Textbox
{
public:
	char hint[200];
	HintBox (int l, int t, int w, int h, const char *hn) : Textbox (l, t, w, h, "") { mail::scpy (hint, hn, sizeof hint); }
	void setHint (const char *h) { if (strcmp (h, hint)) { mail::scpy (hint, h, sizeof hint); invalidate (true); } }
	void onDraw () override
	{
		Textbox::onDraw ();
		if (!text[0] && !hasFocus && hint[0]) text_v (canvas, 9, 0, height, hint, wk_mix (C_FIELD, C_FIELD_TEXT, 110), F_UI, 0, width - 16);
	}
};

// ---- the hit lists ---------------------------------------------------------------------------------------------------------
struct Hit { int x, y, w, h, kind, a, b; };
struct HitList
{
	Hit h[600]; int n;
	HitList () : n (0) {}
	void clear () { n = 0; }
	void add (int x, int y, int w, int hh, int kind, int a = 0, int b = 0) { if (n < 600) h[n++] = Hit { x, y, w, hh, kind, a, b }; }
	const Hit *at (int x, int y) const { for (int i = n - 1; i >= 0; i--) if (x >= h[i].x && y >= h[i].y && x < h[i].x + h[i].w && y < h[i].y + h[i].h) return &h[i]; return 0; }
};

// ---- the icons -----------------------------------------------------------------------------------------------------------------
enum { I_PEN, I_REPLY, I_REPLYALL, I_FORWARD, I_ARCHIVE, I_TRASH, I_JUNK, I_STAR, I_STAR_O, I_REFRESH, I_SEARCH, I_INBOX, I_SENT,
       I_DRAFTS, I_FOLDER, I_PERSON, I_GEAR, I_CLIP, I_SEND, I_CLOSE, I_CHEV_R, I_CHEV_D, I_PLUS, I_UNIFIED, I_DOT, I_PICTURE, I_FILE, I_CHECK,
       I_WARN, I_MORE, I_BACK };
static void icon (Canvas &cv, int id, int x, int y, int s, unsigned c)
{
	VPath p;
	int X = V (x), Y = V (y), u = V (s) / 24, w2 = 2 * u + u / 2;
#define PX(a) (X + (a) * u)
#define PY(b) (Y + (b) * u)
	switch (id)
	{
	case I_PEN:
		{ int t[] = { PX (4), PY (20), PX (5), PY (15), PX (16), PY (4), PX (20), PY (8), PX (9), PY (19) }; p.poly (t, 5); } break;
	case I_REPLY:
		{ int t[] = { PX (21), PY (19), PX (21), PY (15), PX (18), PY (12), PX (5), PY (12) }; p.polyline (t, 4, w2);
		  int h[] = { PX (10), PY (6), PX (4), PY (12), PX (10), PY (18) }; p.polyline (h, 3, w2); } break;
	case I_REPLYALL:
		{ int t[] = { PX (23), PY (19), PX (23), PY (15), PX (20), PY (12), PX (9), PY (12) }; p.polyline (t, 4, 2 * u);
		  int h[] = { PX (13), PY (6), PX (7), PY (12), PX (13), PY (18) }; p.polyline (h, 3, 2 * u);
		  int g[] = { PX (8), PY (6), PX (2), PY (12), PX (8), PY (18) }; p.polyline (g, 3, 2 * u); } break;
	case I_FORWARD:
		{ int t[] = { PX (3), PY (19), PX (3), PY (15), PX (6), PY (12), PX (19), PY (12) }; p.polyline (t, 4, w2);
		  int h[] = { PX (14), PY (6), PX (20), PY (12), PX (14), PY (18) }; p.polyline (h, 3, w2); } break;
	case I_ARCHIVE: p.rrect (PX (2), PY (4), 20 * u, 5 * u, u); p.rrect (PX (4), PY (10), 16 * u, 11 * u, u); p.fill (cv, c); { VPath h; h.rect (PX (9), PY (13), 6 * u, 2 * u); h.fill (cv, 0xFFFFFF); } return;
	case I_TRASH: p.rect (PX (3), PY (5), 18 * u, 2 * u); p.rect (PX (9), PY (2), 6 * u, 3 * u); p.rrect (PX (5), PY (8), 14 * u, 14 * u, u); p.fill (cv, c); { VPath h; h.rect (PX (9), PY (11), 2 * u, 8 * u); h.rect (PX (13), PY (11), 2 * u, 8 * u); h.fill (cv, 0xFFFFFF); } return;
	case I_JUNK: p.arc (PX (12), PY (12), 9 * u, 0, 360, w2); p.line (PX (6), PY (18), PX (18), PY (6), w2); break;
	case I_STAR: case I_STAR_O:
	{
		int t[20]; static const int R[2] = { 11, 5 };
		static const int CS[10][2] = { { 0, -100 }, { 59, -81 }, { 95, -31 }, { 95, 31 }, { 59, 81 }, { 0, 100 }, { -59, 81 }, { -95, 31 }, { -95, -31 }, { -59, -81 } };
		for (int k = 0; k < 10; k++) { int r = R[k & 1]; int ci = (k * 1) % 10; t[2 * k] = PX (12) + CS[ci][0] * r * u / 100; t[2 * k + 1] = PY (13) + CS[ci][1] * r * u / 100; }
		if (id == I_STAR) p.poly (t, 10); else p.polyline (t, 10, 2 * u, true);
		break;
	}
	case I_REFRESH: p.arc (PX (12), PY (12), 8 * u, 30, 320, w2); { int t[] = { PX (16), PY (2), PX (22), PY (7), PX (15), PY (10) }; p.poly (t, 3); } break;
	case I_SEARCH: p.arc (PX (10), PY (10), 6 * u, 0, 360, 2 * u); p.line (PX (15), PY (15), PX (21), PY (21), 3 * u); break;
	case I_INBOX:
		{ int t[] = { PX (2), PY (13), PX (7), PY (13), PX (9), PY (16), PX (15), PY (16), PX (17), PY (13), PX (22), PY (13) }; p.polyline (t, 6, 2 * u);
		  int o[] = { PX (2), PY (13), PX (5), PY (4), PX (19), PY (4), PX (22), PY (13), PX (22), PY (20), PX (2), PY (20) }; p.polyline (o, 6, 2 * u, true); } break;
	case I_SENT: { int t[] = { PX (2), PY (11), PX (22), PY (3), PX (15), PY (21), PX (11), PY (13) }; p.poly (t, 4); } break;
	case I_SEND: { int t[] = { PX (3), PY (4), PX (22), PY (12), PX (3), PY (20), PX (6), PY (12) }; p.poly (t, 4); } break;
	case I_DRAFTS:
		{ int t[] = { PX (4), PY (2), PX (14), PY (2), PX (20), PY (8), PX (20), PY (22), PX (4), PY (22) }; p.polyline (t, 5, 2 * u, true);
		  p.line (PX (8), PY (12), PX (16), PY (12), 2 * u); p.line (PX (8), PY (17), PX (14), PY (17), 2 * u); } break;
	case I_FOLDER: p.rrect (PX (1), PY (6), 22 * u, 15 * u, 2 * u); p.rrect (PX (1), PY (3), 9 * u, 5 * u, 2 * u); break;
	case I_PERSON: p.circle (PX (12), PY (8), 5 * u); p.arc (PX (12), PY (24), 9 * u, 180, 360, 0); { int t[] = { PX (3), PY (22), PX (5), PY (16), PX (9), PY (14), PX (15), PY (14), PX (19), PY (16), PX (21), PY (22) }; p.poly (t, 6); } break;
	case I_GEAR:
		for (int k = 0; k < 8; k++) { static const int D[8][2] = { { 0, -9 }, { 6, -6 }, { 9, 0 }, { 6, 6 }, { 0, 9 }, { -6, 6 }, { -9, 0 }, { -6, -6 } }; p.circle (PX (12 + D[k][0]), PY (12 + D[k][1]), 2 * u + u / 2); }
		p.circle (PX (12), PY (12), 8 * u); p.fill (cv, c); { VPath h; h.circle (PX (12), PY (12), 3 * u); h.fill (cv, 0xFFFFFF); } return;
	case I_CLIP: p.line (PX (9), PY (8), PX (9), PY (17), 2 * u); p.arc (PX (12), PY (17), 3 * u, 0, 180, 2 * u); p.line (PX (15), PY (17), PX (15), PY (5), 2 * u); p.arc (PX (11), PY (5), 4 * u, 180, 360, 2 * u); p.line (PX (7), PY (5), PX (7), PY (19), 2 * u); p.arc (PX (11), PY (19), 4 * u, 0, 180, 2 * u); break;
	case I_CLOSE: p.line (PX (6), PY (6), PX (18), PY (18), 2 * u); p.line (PX (18), PY (6), PX (6), PY (18), 2 * u); break;
	case I_CHEV_R: { int t[] = { PX (9), PY (5), PX (16), PY (12), PX (9), PY (19) }; p.polyline (t, 3, w2); } break;
	case I_CHEV_D: { int t[] = { PX (5), PY (9), PX (12), PY (16), PX (19), PY (9) }; p.polyline (t, 3, w2); } break;
	case I_BACK: { int t[] = { PX (15), PY (5), PX (8), PY (12), PX (15), PY (19) }; p.polyline (t, 3, w2); } break;
	case I_PLUS: p.rect (PX (5), PY (11), 14 * u, w2); p.rect (PX (11), PY (5), w2, 14 * u); break;
	case I_UNIFIED:
		{ int o[] = { PX (2), PY (13), PX (5), PY (4), PX (19), PY (4), PX (22), PY (13), PX (22), PY (20), PX (2), PY (20) }; p.poly (o, 6); p.fill (cv, c);
		  VPath h; int t[] = { PX (4), PY (13), PX (8), PY (13), PX (10), PY (16), PX (14), PY (16), PX (16), PY (13), PX (20), PY (13) }; h.polyline (t, 6, 2 * u); h.fill (cv, 0xFFFFFF); } return;
	case I_DOT: p.circle (PX (12), PY (12), 5 * u); break;
	case I_PICTURE: p.rrect (PX (2), PY (4), 20 * u, 16 * u, 2 * u); p.fill (cv, c); { VPath h; int t[] = { PX (4), PY (18), PX (10), PY (10), PX (14), PY (15), PX (16), PY (13), PX (20), PY (18) }; h.poly (t, 5); h.circle (PX (16), PY (8), 2 * u); h.fill (cv, 0xFFFFFF); } return;
	case I_FILE: { int t[] = { PX (5), PY (2), PX (14), PY (2), PX (19), PY (7), PX (19), PY (22), PX (5), PY (22) }; p.poly (t, 5); } break;
	case I_CHECK: { int t[] = { PX (4), PY (12), PX (10), PY (18), PX (20), PY (6) }; p.polyline (t, 3, 3 * u); } break;
	case I_WARN: { int t[] = { PX (12), PY (2), PX (23), PY (21), PX (1), PY (21) }; p.poly (t, 3); p.fill (cv, c); VPath h; h.rect (PX (11), PY (8), 2 * u, 7 * u); h.rect (PX (11), PY (17), 2 * u, 2 * u); h.fill (cv, 0xFFFFFF); return; }
	case I_MORE: for (int k = 0; k < 3; k++) p.circle (PX (5 + k * 7), PY (12), 2 * u); break;
	}
#undef PX
#undef PY
	p.fill (cv, c);
}

// ---- the avatars: a coloured disc, the initials -----------------------------------------------------------------------------
static unsigned avatar_colour (const char *key)
{
	static const unsigned C[10] = { 0xC5504B, 0x3C8DA8, 0x3D7A4B, 0x8E5BB5, 0xC77C2E, 0x2F6FB0, 0x8A6D3B, 0xB0466E, 0x4B7F86, 0x5E6B7A };
	unsigned h = 2166136261u; for (const char *p = key; p && *p; p++) h = (h ^ (unsigned char) (*p | 32)) * 16777619u;
	return C[h % 10];
}
// "Marie Dubois" -> "MD", "jonas.peeters@x" -> "JP"
static void initials (const char *name, char *out)
{
	int k = 0; bool start = true;
	for (const char *p = name; *p && k < 6; p++)
	{
		unsigned char c = (unsigned char) *p;
		if (c == '@') break;
		if (c == ' ' || c == '.' || c == '_' || c == '-' || c == '"' || c == '\'') { start = true; continue; }
		if (start)
		{
			if (c >= 'a' && c <= 'z') c -= 32;
			if (c >= 0xC0) { int l = c >= 0xE0 ? 3 : 2; for (int i = 0; i < l && p[i]; i++) out[k++] = p[i]; p += l - 1; if (l == 2 && (unsigned char) out[k - 2] == 0xC3 && (unsigned char) out[k - 1] >= 0xA0) out[k - 1] -= 0x20; }
			else out[k++] = (char) c;
			start = false;
			if ((k >= 2 && (unsigned char) out[0] < 0x80) || k >= 4) break;
		}
	}
	out[k] = 0;
}
static void avatar (Canvas &cv, int cx, int cy, int r, const char *name, const char *key)
{
	VPath p; p.circle (V (cx), V (cy), V (r)); p.fill (cv, avatar_colour (key && *key ? key : name));
	char in[8]; initials (name, in);
	int f = r >= 18 ? F_MID : F_SMALL;
	text_c (cv, cx - r, cy - r, 2 * r, 2 * r, in, 0xFFFFFF, f, 1);
}

} // namespace mailapp

#endif
