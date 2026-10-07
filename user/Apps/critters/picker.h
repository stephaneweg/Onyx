//
// picker.h -- Critters' level picker (the window's, included by main.cpp only; 04-ux-design.md §3, D16-D18): the list
// of the levels in their groups (Training, Expedition, My levels: a number badge, the name, a tick and the best, a
// "new" pill, a lock; a refused file with its reason in red), the chosen level's preview (its terrain, dimmed with a lock
// when locked; the card saying why it cannot be played when refused), its facts (save, time, rate, the roles it gives,
// the hint, the best result) and the keys' legend.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _critters_picker_h
#define _critters_picker_h

static const char *const GROUP_NAME[3] = { TRN ("Training"), TRN ("Expedition"), TRN ("My levels") };

// The keys' legend: keycaps ("↑|↓": several) then what they do, in a row
class Legend : public Widget
{
public:
	enum { MAXK = 6 };
	const char *key[MAXK], *act[MAXK]; int n;
	Legend (int l, int t, int w, int h) : Widget (l, t, w, h), n (0) {}
	void add (const char *k, const char *a) { if (n < MAXK) { key[n] = k; act[n] = a; n++; } }
	static int cap (Canvas &cv, int x, int y, const char *k)
	{
		UkFaceScope sc (face (11)); int w = uk_tw (k, 2) + 12; if (w < 22) w = 22;
		uk_raised (cv, x, y, w, 20, 4, uk_tone (C_BG, 150)); uk_text_c (cv, x, y, w, 19, k, C_TEXT, 2); return w;
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		int x = 0;
		for (int i = 0; i < n; i++)
		{
			int cx = x;
			for (const char *k = key[i]; *k; )
			{ char one[24]; int j = 0; while (k[j] && k[j] != '|' && j < 23) { one[j] = k[j]; j++; } one[j] = 0; cx += cap (canvas, cx, 1, one) + 3; k += j; if (*k == '|') k++; }
			uk_text_l (canvas, cx + 3, 0, 22, act[i], C_DIS); x = cx + 3 + uk_text_w (act[i]) + 12;
		}
	}
};

// The list (D16): headings, rows of 25 px (a refused file 40: its reason on a second line); scrolls (wheel, keys, bar)
class LevelList : public Widget
{
public:
	enum { HEAD = 20, SEP = 6, ROW = 25, ROWB = 40 };
	long scroll; UkBarDrag bar; unsigned lastClick; int lastRow;
	LevelList (int l, int t, int w, int h) : Widget (l, t, w, h), scroll (0), lastClick (0), lastRow (-1) { canFocus = true; }
	static bool headBefore (int i) { return i == 0 || g_ent[i]->group != g_ent[i - 1]->group; }
	static int rowH (int i) { return g_ent[i]->ok ? ROW : ROWB; }
	int rowY (int i) const
	{
		int y = 4;
		for (int k = 0; k <= i; k++)
		{
			if (headBefore (k)) y += (k ? SEP : 0) + HEAD;
			if (k < i) y += rowH (k) + 1;
		}
		return y;
	}
	int contentH () const { return g_nent ? rowY (g_nent - 1) + rowH (g_nent - 1) + 6 : 0; }
	void clampScroll () { long most = contentH () - height; if (most < 0) most = 0; if (scroll > most) scroll = most; if (scroll < 0) scroll = 0; }
	void ensureVisible (int i)
	{
		if (i < 0 || i >= g_nent) return;
		int y = rowY (i), top = (int) scroll;
		if (headBefore (i) && y - HEAD - 4 < top) scroll = y - HEAD - 4;
		else if (y - 4 < top) scroll = y - 4;
		else if (y + rowH (i) + 4 > top + height) scroll = y + rowH (i) + 4 - height;
		clampScroll ();
	}
	void heading (int y, int g)
	{
		UkFaceScope sc (face (11));
		const char *s = TR (GROUP_NAME[g < 3 ? g : 2]);
		uk_text_l (canvas, 12, y, HEAD, s, C_DIS, 2);
		if (g == 2) uk_text_l (canvas, 16 + uk_tw (s, 2), y, HEAD, USERDIR, uk_mix (C_FIELD, C_DIS, 170));
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD, hasFocus);
		bool sb = contentH () > height;
		int rw = width - (sb ? 22 : 8);
		for (int i = 0; i < g_nent; i++)
		{
			int y = rowY (i) - (int) scroll;
			if (headBefore (i))
			{
				int hy = y - HEAD;
				if (i) uk_etch_h (canvas, 10, hy - 3, rw - 6, C_FIELD);
				if (hy + HEAD > 0 && hy < height) heading (hy, g_ent[i]->group);
			}
			if (y + rowH (i) < 0 || y > height) continue;
			drawRow (i, y, rw);
		}
		if (sb) { int th = height - 8; UkThumb t = uk_thumb (contentH (), height, scroll, th); uk_draw_vscroll (canvas, width - UK_SBW - 4, 4, UK_SBW, th, t, C_FIELD); }
	}
	void drawRow (int i, int y, int rw)
	{
		Entry &e = *g_ent[i];
		int rh = rowH (i); bool sel = i == g_sel, lk = e.ok && locked (i);
		if (sel) uk_hilite (canvas, 4, y, rw, rh, 6, hasFocus);
		unsigned ink = sel ? uk_hilite_ink (hasFocus) : C_FIELD_TEXT, dim = sel ? uk_mix (uk_hilite_ink (hasFocus), C_ACCENT, 70) : C_DIS;
		if (lk || !e.ok) ink = sel ? uk_mix (C_ACCENT, ink, 200) : uk_mix (C_FIELD, C_FIELD_TEXT, 130);
		char b[96], fit[200];
		if (e.ok && e.group < 2)			// the number badge (a player's level: a dot; refused: a warning sign)
		{
			snprintf (b, sizeof b, "%d", e.num);
			uk_rbox (canvas, 10, y + 4, 20, 17, 5, sel ? uk_tone (C_ACCENT, 100) : uk_tone (C_FIELD, 112), sel ? uk_tone (C_ACCENT, 90) : uk_tone (C_FIELD, 104));
			UkFaceScope sc (face (11)); uk_text_c (canvas, 10, y + 4, 20, 17, b, sel ? 0xFFFFFF : C_DIS, 2);
		}
		else if (e.ok) uk_glyph (canvas, WKG_DOT, 20, y + 12, 8, dim);
		else { VPath p; int t[6] = { V (20), V (y + 4), V (29), V (y + 20), V (11), V (y + 20) }; p.poly (t, 3); p.fill (canvas, 0xE0A020); uk_text_c (canvas, 11, y + 6, 18, 14, "!", 0x000000, 2); }
		int right = 4 + rw - 8;				// the state at the right
		int nameRoom = right - 38 - 8;
		if (!e.ok) { }
		else if (lk) { uk_glyph (canvas, WKG_LOCK, right - 10, y + 12, 12, dim); nameRoom -= 20; }
		else
		{
			critters::Best bst = critters::progress_get (g_prog, e.section);
			if (bst.solved)
			{
				snprintf (b, sizeof b, "%d/%d", bst.saved, e.count);
				UkFaceScope sc (face (11)); int w = uk_tw (b);
				uk_text_l (canvas, right - 22 - w, y, ROW, b, dim);
				uk_glyph (canvas, WKG_CHECK, right - 8, y + 12, 12, sel ? ink : 0x2E9A44);
				nameRoom -= w + 26;
			}
			else
			{
				UkFaceScope sc (face (10)); const char *nw = TR ("new"); int w = uk_tw (nw, 2) + 10;
				uk_rbox (canvas, right - w, y + 6, w, 14, 7, 0xFF9A30, 0xF07A10); uk_text_c (canvas, right - w, y + 6, w, 14, nw, 0xFFFFFF, 2);
				nameRoom -= w + 6;
			}
		}
		uk_text_fit (e.ok ? e.name[g_lang] : e.file, nameRoom, fit, sizeof fit, sel && e.ok ? 2 : 0);
		uk_text_l (canvas, 38, y + (e.ok ? 0 : 1), ROW, fit, ink, sel && e.ok ? 2 : 0);
		if (!e.ok) { uk_text_fit (e.err, right - 38, fit, sizeof fit); uk_text_l (canvas, 38, y + 18, 20, fit, sel ? ink : RED); }
	}
	int rowAt (int my) const
	{
		int cy = my + (int) scroll;
		for (int i = 0; i < g_nent; i++) { int y = rowY (i); if (cy >= y && cy < y + rowH (i)) return i; }
		return -1;
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel) { scroll -= wheel * 40; clampScroll (); invalidate (true); return true; }
		if (bar.mouse (mx, my, bl, width - UK_SBW - 4, UK_SBW, 4, height - 8, contentH (), height, &scroll)) { clampScroll (); invalidate (true); return true; }
		if (mx < 0) return false;
		if (bl && !pressed)
		{
			pressed = true; setFocus ();
			int r = rowAt (my);
			if (r >= 0)
			{
				unsigned now = gms ();
				bool dbl = r == lastRow && now - lastClick < 450;
				lastClick = now; lastRow = r;
				select_entry (r);
				if (dbl) play_selected ();
			}
			return true;
		}
		if (!bl) pressed = false;
		return true;
	}
	bool onKey (long k) override
	{
		int n = g_sel;
		switch (k)
		{
		case KEY_UP: n = g_sel - 1; break;
		case KEY_DOWN: n = g_sel + 1; break;
		case KEY_HOME: n = 0; break;
		case KEY_END: n = g_nent - 1; break;
		case KEY_PGUP: n = g_sel - 6; break;
		case KEY_PGDN: n = g_sel + 6; break;
		case KEY_ENTER: case '\n': case ' ': play_selected (); return true;
		default: return false;
		}
		if (n < 0) n = 0;
		if (n >= g_nent) n = g_nent - 1;
		if (n != g_sel && n >= 0) select_entry (n);
		return true;
	}
};

// The preview (D16-D18): the chosen level's terrain scaled to fit (made once, kept), its hatches and exits; locked:
// dimmed with a lock badge; refused: the card saying why
class Preview : public Widget
{
public:
	Preview (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		if (g_sel < 0 || g_sel >= g_nent) return;
		Entry &e = *g_ent[g_sel];
		if (!e.ok)
		{
			uk_sunken (canvas, 0, 0, width, height, 8, uk_tone (C_BG, 120));
			VPath p; int cx = 52, ty = 30; int tri[6] = { V (cx), V (ty), V (cx + 30), V (ty + 52), V (cx - 30), V (ty + 52) }; p.poly (tri, 3); p.fill (canvas, 0xE0A020);
			{ UkFaceScope sc (face (30)); uk_text_c (canvas, cx - 20, ty + 14, 40, 40, "!", 0x000000, 2); }
			const char *lines[3] = { TR ("This level cannot be played."), e.err, TR ("Correct the file in a text editor (Tinypad): the list is read again when you come back to it.") };
			int y = 22;
			for (int k = 0; k < 3; k++)
			{
				int st[6], lnn[6]; int n = uk_text_wrap (lines[k], (int) strlen (lines[k]), width - 120, 5, st, lnn, 0, k == 0 ? 2 : 0);
				for (int i = 0; i < n; i++) { char b[300]; snprintf (b, sizeof b, "%.*s", lnn[i], lines[k] + st[i]); uk_text (canvas, 104, y, b, k == 1 ? RED : k == 0 ? C_TEXT : C_DIS, k == 0 ? 2 : 0); y += uk_fh (); }
				y += 8;
			}
			return;
		}
		double s = fmin ((width - 8.0) / e.w, (height - 8.0) / e.h);
		int pw = (int) (e.w * s), ph = (int) (e.h * s), x0 = (width - pw) / 2, y0 = (height - ph) / 2;
		uk_rbox (canvas, x0 - 4, y0 - 4, pw + 8, ph + 8, 6, uk_tone (C_BG, 90), uk_tone (C_BG, 70));
		if (!e.thumb) make_thumb (e, pw, ph);
		if (e.thumb && e.thumb->px) canvas.putOther (*e.thumb, x0, y0, false);
		else canvas.fillRect (x0, y0, pw, ph, 0x101830);
		double k = s / 2 * 1.4; if (k > 0.9) k = 0.9;
		for (int i = 0; i < e.nexit; i++) portal (canvas, x0 + e.exit[i].x * s, y0 + (e.exit[i].y + 1) * s, 200, k);
		for (int i = 0; i < e.nhatch; i++) hatch (canvas, x0 + e.hatch[i].x * s, y0 + (e.hatch[i].y + 1) * s - 2, false, k);
		if (locked (g_sel))
		{
			VPath p; p.rect (V (x0), V (y0), V (pw), V (ph)); p.fill (canvas, 0x000000, 120);
			uk_rbox (canvas, width / 2 - 22, height / 2 - 22, 44, 44, 22, uk_tone (C_FACE, 150), uk_tone (C_FACE, 110), 230);
			uk_glyph (canvas, WKG_LOCK, width / 2, height / 2, 22, C_TEXT);
		}
	}
};

// The chosen level's facts (D16, D17)
class LevelInfo : public Widget
{
public:
	LevelInfo (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		if (g_sel < 0 || g_sel >= g_nent || !g_ent[g_sel]->ok) return;
		Entry &e = *g_ent[g_sel];
		char b[300], t[16], fit[300];
		{ UkFaceScope sc (face (17)); uk_text_fit (e.name[g_lang], width, fit, sizeof fit, 2); uk_text (canvas, 0, 0, fit, C_TEXT, 2); }
		if (e.group < 2) snprintf (b, sizeof b, TR ("%s \xC2\xB7 level %d of %d"), TR (GROUP_NAME[e.group]), e.num, group_size (e.group));
		else snprintf (b, sizeof b, "%s \xC2\xB7 %s", TR (GROUP_NAME[2]), e.file);
		uk_text_fit (b, width, fit, sizeof fit); uk_text (canvas, 0, 24, fit, C_DIS);
		uk_sunken (canvas, 0, 46, width, 30, 6, uk_tone (C_BG, 140));	// save, time, rate in a sunken strip
		mmss (e.timeSec, t, sizeof t);
		const char *cap[3] = { TR ("Save"), TR ("Time"), TR ("Rate") }; char val[3][24];
		snprintf (val[0], 24, TR ("%d of %d"), e.save, e.count); snprintf (val[1], 24, "%s", t); snprintf (val[2], 24, "%d", e.rate);
		for (int i = 0; i < 3; i++)
		{
			int cx = i * width / 3; if (i) uk_etch_v (canvas, cx, 51, 20, uk_tone (C_BG, 140));
			int w = uk_text_w (cap[i]) + 6 + uk_text_w (val[i], 2), x = cx + (width / 3 - w) / 2;
			uk_text_l (canvas, x, 46, 30, cap[i], C_DIS); uk_text_l (canvas, x + uk_text_w (cap[i]) + 6, 46, 30, val[i], C_TEXT, 2);
		}
		int x = 0;						// the roles it gives
		for (int r = 0; r < NBUILT; r++)
		{
			bool off = e.roles[r] == 0;
			role_icon (canvas, r, x + 18, 102, 30, off);
			if (off) snprintf (b, sizeof b, "\xE2\x80\x93"); else snprintf (b, sizeof b, "\xC3\x97%d", e.roles[r]);
			uk_text_l (canvas, x + 36, 88, 30, b, off ? uk_mix (C_BG, C_DIS, 150) : C_TEXT, 2);
			x += width / NBUILT;
		}
		const char *h = e.hint[g_lang]; int st[4], lnn[4]; int nl = *h ? uk_text_wrap (h, (int) strlen (h), width, 3, st, lnn) : 0;
		for (int i = 0; i < nl; i++) { snprintf (b, sizeof b, "%.*s", lnn[i], h + st[i]); uk_text (canvas, 0, 128 + i * uk_fh (), b, C_TEXT); }
		int by = 132 + nl * uk_fh () + 6;
		critters::Best bst = critters::progress_get (g_prog, e.section);
		if (locked (g_sel))
		{
			uk_glyph (canvas, WKG_LOCK, 8, by + 10, 12, C_DIS);
			int pv = prev_in_chain (g_sel);
			snprintf (b, sizeof b, TR ("Solve \xE2\x80\x9C%s\xE2\x80\x9D to open this level."), pv >= 0 ? g_ent[pv]->name[g_lang] : "");
			uk_text_fit (b, width - 22, fit, sizeof fit); uk_text_l (canvas, 22, by, 20, fit, C_DIS);
		}
		else if (bst.solved)
		{
			uk_glyph (canvas, WKG_CHECK, 8, by + 10, 12, 0x2E9A44); mmss (bst.timeSec, t, sizeof t);
			snprintf (b, sizeof b, TR ("Best: %d saved \xC2\xB7 %s"), bst.saved, t); uk_text_l (canvas, 22, by, 20, b, C_TEXT, 2);
		}
		else uk_text_l (canvas, 0, by, 20, TR ("Not solved yet."), C_DIS);
	}
};

#endif
