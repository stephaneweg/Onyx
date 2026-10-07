//
// panel.h -- Pinball's panel and the drawn widgets the window shares (included by main.cpp only): a heading (a title
// over wrapped words: the table's name and goal), the figures (bonus, multiplier, best, the tilt's dots), the table's
// rules as goals (their counts as dots), the keys' legend (the keyboard's, or the pad's once a pad button is seen).
// 04-ux-design.md §2.1, D5-D9.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _pinball_panel_h
#define _pinball_panel_h

static int wrapped (const char *s, int w) { int st[8], ln[8]; return s && *s ? uk_text_wrap (s, (int) strlen (s), w, 8, st, ln) : 0; }

// A title (titlePx bold, a colour swatch at its left) over a text wrapped to the width
class Heading : public Widget
{
public:
	char title[200]; const char *text; int titlePx; bool swatch; unsigned sw0, sw1;
	Heading (int l, int tp, int w, int h) : Widget (l, tp, w, h), text (""), titlePx (16), swatch (false), sw0 (0), sw1 (0) { title[0] = 0; }
	void set (const char *t, const char *x) { char fit[200]; snprintf (fit, sizeof fit, "%s", t ? t : ""); snprintf (title, sizeof title, "%s", fit); text = x ? x : ""; invalidate (true); }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		int x = 0;
		if (swatch) { uk_rbox (canvas, 0, 3, 8, titlePx + 4, 3, sw0, sw1); x = 14; }
		{
			TextFace *f = face (titlePx); UkFaceScope sc (f);
			char fit[200]; uk_text_fit (title, width - x, fit, sizeof fit, 2);
			uk_text (canvas, x, 0, fit, C_TEXT, 2);
		}
		int st[8], ln[8]; int n = text[0] ? uk_text_wrap (text, (int) strlen (text), width - 2, 6, st, ln) : 0;
		for (int i = 0; i < n; i++) { char b[200]; snprintf (b, sizeof b, "%.*s", ln[i], text + st[i]); uk_text (canvas, 0, titlePx + 10 + i * uk_fh (), b, C_DIS); }
	}
};

// The figures under the score: caption at the left, the value in bold at the right; the Tilt row: three dots
class Stats : public Widget
{
public:
	enum { ROWS = 4 };
	char val[ROWS][64]; int tilt;
	Stats (int l, int t, int w, int h) : Widget (l, t, w, h), tilt (0) { for (int i = 0; i < ROWS; i++) val[i][0] = 0; }
	void set (int i, const char *v) { if (strcmp (val[i], v)) { snprintf (val[i], sizeof val[i], "%s", v); invalidate (true); } }
	void setTilt (int n) { if (n != tilt) { tilt = n; invalidate (true); } }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_sunken (canvas, 0, 0, width, height, 6, uk_tone (C_BG, 140));
		const char *cap[ROWS] = { TR ("Bonus"), TR ("Multiplier"), TR ("Best"), TR ("Tilt") };
		int rh = (height - 8) / ROWS;
		for (int i = 0; i < ROWS; i++)
		{
			int y = 4 + i * rh;
			if (i) uk_etch_h (canvas, 10, y, width - 20, uk_tone (C_BG, 140));
			uk_text_l (canvas, 12, y, rh, cap[i], C_DIS);
			if (i == 3)
				for (int k = 0; k < 3; k++)
				{ VPath p; p.circle (V (width - 18 - k * 16), V (y + rh / 2), V (5)); p.fill (canvas, 2 - k < tilt ? 0xE0453A : uk_tone (C_BG, 110)); }
			else uk_text_l (canvas, width - 12 - uk_text_w (val[i], 2), y, rh, val[i], C_TEXT, 2);
		}
	}
};

// The table's rules as goals: a row per [rule] with a message -- its count as dots (filled: counted this game; more
// than 6: "3/10"), then the message; a "once" rule done: a check, greyed
class RuleList : public Widget
{
public:
	const Table *t; const Looks *k; int done[pinball::MAXRULE]; bool doneOnce[pinball::MAXRULE];
	RuleList (int l, int tp, int w, int h) : Widget (l, tp, w, h), t (0), k (0) { clear (); }
	void clear () { memset (done, 0, sizeof done); memset (doneOnce, 0, sizeof doneOnce); }
	// from the game -> repainted when a count changed
	void update (const Game &g)
	{
		bool ch = false;
		for (int r = 0; r < t->nrule; r++)
			if (done[r] != g.ruleCount[r] || doneOnce[r] != g.ruleDone[r]) { done[r] = g.ruleCount[r]; doneOnce[r] = g.ruleDone[r]; ch = true; }
		if (ch) invalidate (true);
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		if (!t) return;
		int y = 0;
		unsigned dot = k ? k->lampCol : 0xE0A800;
		if (dot == 0xFFD34D) dot = 0xE0A800;			// (a pale yellow reads badly on a light panel)
		for (int i = 0; i < t->nrule && y + 20 <= height; i++)
		{
			const pinball::Rule &r = t->rule[i]; const char *m = r.message.get (g_lang);
			if (!m[0]) continue;
			int x = 2;
			if (doneOnce[i]) { uk_glyph (canvas, WKG_CHECK, x + 6, y + 10, 12, 0x3A9A4A); x += 18; }
			else if (r.count <= 6)
				for (int q = 0; q < r.count; q++) { VPath p; p.circle (V (x + 5), V (y + 10), V (4)); p.fill (canvas, q < done[i] ? dot : uk_tone (C_BG, 105)); x += 11; }
			else { char b[16]; snprintf (b, sizeof b, "%d/%d", done[i], r.count); uk_text_l (canvas, x, y, 20, b, C_TEXT, 2); x += uk_text_w (b, 2); }
			char fit[80]; uk_text_fit (m, width - x - 8, fit, sizeof fit);
			uk_text_l (canvas, x + 6, y, 20, fit, doneOnce[i] ? C_DIS : C_TEXT);
			y += 21;
		}
	}
};

// The keys' legend: keycaps ("Z|←": several) then what they do; a column (the panel) or a row (the picker)
class Legend : public Widget
{
public:
	enum { MAXK = 8 };
	const char *key[MAXK], *act[MAXK]; int n; bool row;
	Legend (int l, int t, int w, int h, bool row_) : Widget (l, t, w, h), n (0), row (row_) {}
	void add (const char *k, const char *a) { if (n < MAXK) { key[n] = k; act[n] = a; n++; } }
	static int cap (Canvas &cv, int x, int y, const char *k)
	{
		UkFaceScope sc (face (11));
		int w = uk_tw (k, 2) + 12; if (w < 22) w = 22;
		uk_raised (cv, x, y, w, 20, 4, uk_tone (C_BG, 150));
		uk_text_c (cv, x, y, w, 19, k, C_TEXT, 2);
		return w;
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		int x = 0, y = 0;
		for (int i = 0; i < n; i++)
		{
			int cx = x;
			for (const char *s = key[i]; *s; )
			{
				char one[24]; int j = 0; while (s[j] && s[j] != '|' && j < 23) { one[j] = s[j]; j++; } one[j] = 0;
				cx += cap (canvas, cx, y, one) + 3; s += j; if (*s == '|') s++;
			}
			if (row) { uk_text_l (canvas, cx + 3, y, 20, act[i], C_DIS); x = cx + 3 + uk_text_w (act[i]) + 18; }
			else { uk_text_l (canvas, 74, y, 20, act[i], C_TEXT); y += 25; }
		}
	}
};

#endif
