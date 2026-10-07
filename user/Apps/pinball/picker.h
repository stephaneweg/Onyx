//
// picker.h -- Pinball's table picker (included by main.cpp only): the list of the tables (the shipped ones, then the
// player's from SD:/docs/pinball, then one opened from elsewhere) -- a small picture, the name, the best score or,
// for a table the reader refused, its error in red --, the chosen table's preview (its playfield at rest, or the
// card saying why it cannot be played) and its top 5. 04-ux-design.md §3, D10, D11.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _pinball_picker_h
#define _pinball_picker_h

// The list: two lines a row, the player's tables after an etched line and a caption; scrolls (wheel, keys, its bar)
class TableList : public Widget
{
public:
	enum { RH = 52, GAP = 2, CAPH = 28 };
	long scroll; UkBarDrag bar; unsigned lastClick; int lastRow;
	TableList (int l, int t, int w, int h) : Widget (l, t, w, h), scroll (0), lastClick (0), lastRow (-1) { canFocus = true; }
	// the row of entry i: its top in the content; the caption's place before the first player's table
	int rowY (int i) const
	{
		int y = 6;
		for (int k = 0; k < i; k++) { if (captionBefore (k)) y += CAPH; y += RH + GAP; }
		if (captionBefore (i)) y += CAPH;
		return y;
	}
	static bool captionBefore (int i) { return i > 0 && !g_ent[i]->shipped && g_ent[i - 1]->shipped; }
	int contentH () const { return g_nent ? rowY (g_nent - 1) + RH + 8 : 0; }
	void ensureVisible (int i)
	{
		int y = rowY (i), top = (int) scroll;
		if (y - 4 < top) scroll = y - 4 < 0 ? 0 : y - 4;
		else if (y + RH + 4 > top + height) scroll = y + RH + 4 - height;
		clampScroll ();
	}
	void clampScroll () { long most = contentH () - height; if (most < 0) most = 0; if (scroll > most) scroll = most; if (scroll < 0) scroll = 0; }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD, hasFocus);
		int sb = contentH () > height ? UK_SBW : 0;
		for (int i = 0; i < g_nent; i++)
		{
			int y = rowY (i) - (int) scroll;
			if (captionBefore (i))
			{
				int cy = y - CAPH;
				if (cy + CAPH > 0 && cy < height)
				{
					uk_etch_h (canvas, 10, cy + 4, width - 20 - sb, C_FIELD);
					bool user = g_ent[i]->user;
					UkFaceScope sc (face (11)); uk_text_l (canvas, 12, cy + 8, 18, user ? TR ("Your tables (SD:/docs/pinball)") : TR ("Other tables"), C_DIS, 2);
				}
			}
			if (y + RH < 0 || y > height) continue;
			drawRow (i, y, width - sb);
		}
		if (sb) { UkThumb th = uk_thumb (contentH (), height - 8, scroll, height - 8); uk_draw_vscroll (canvas, width - UK_SBW - 2, 4, UK_SBW, height - 8, th, C_FIELD); }
	}
	void drawRow (int i, int y, int w)
	{
		TableEntry &e = *g_ent[i];
		bool sel = i == g_sel;
		if (sel) uk_hilite (canvas, 4, y, w - 8, RH, 6, hasFocus);
		unsigned ink = sel ? uk_hilite_ink (hasFocus) : C_FIELD_TEXT, dim = sel ? uk_mix (uk_hilite_ink (hasFocus), C_ACCENT, 70) : C_DIS;
		int tx = 12, ty = y + 4;
		if (e.t)						// a small picture of the table (its static layer, 44 px tall)
		{
			if (!e.mini)
			{
				double s = 44.0 / e.t->h; int mw = (int) lround (e.t->w * s); if (mw > 30) mw = 30; if (mw < 4) mw = 4;
				e.mini = new Canvas;
				if (e.mini->alloc (mw, 44)) { View v = { s, (mw - e.t->w * s) / 2, 0 }; e.mini->clear (e.t->background); draw_static (*e.mini, *e.t, v, true); }
			}
			if (e.mini && e.mini->px) { canvas.putOther (*e.mini, tx + (22 - e.mini->w) / 2, ty, false); uk_rline (canvas, tx + (22 - e.mini->w) / 2 - 1, ty - 1, e.mini->w + 2, 46, 2, uk_tone (C_FIELD, 80), 120); }
		}
		else							// refused: a warning sign
		{
			uk_rbox (canvas, tx, ty, 22, 44, 3, uk_tone (C_FIELD, 118), uk_tone (C_FIELD, 108));
			VPath p; int tri[6] = { V (tx + 11), V (ty + 13), V (tx + 20), V (ty + 30), V (tx + 2), V (ty + 30) }; p.poly (tri, 3); p.fill (canvas, 0xE0A020);
			uk_text_c (canvas, tx, ty + 15, 22, 16, "!", 0x000000, 2);
		}
		int x = 46; char fit[200];
		uk_text_fit (e.t ? e.t->name.get (g_lang) : e.file, w - x - 8, fit, sizeof fit, 2);
		uk_text_l (canvas, x, y + 4, 22, fit, e.t ? ink : sel ? uk_mix (C_ACCENT, ink, 190) : uk_mix (C_FIELD, C_FIELD_TEXT, 140), 2);
		if (!e.t) { uk_text_fit (e.err, w - x - 8, fit, sizeof fit); uk_text_l (canvas, x, y + 26, 20, fit, sel ? ink : 0xC0302A); }
		else
		{
			char b[96], n[32];
			if (e.best > 0) { fmt (e.best, n, sizeof n); snprintf (b, sizeof b, "%s  %s", TR ("Best"), n); }
			else snprintf (b, sizeof b, "%s", TR ("No score yet"));
			uk_text_l (canvas, x, y + 26, 20, b, dim);
		}
	}
	int rowAt (int my) const
	{
		int cy = my + (int) scroll;
		for (int i = 0; i < g_nent; i++) { int y = rowY (i); if (cy >= y && cy < y + RH) return i; }
		return -1;
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel) { scroll -= wheel * 40; clampScroll (); invalidate (true); return true; }
		if (bar.mouse (mx, my, bl, width - UK_SBW - 2, UK_SBW, 4, height - 8, contentH (), height - 8, &scroll)) { clampScroll (); invalidate (true); return true; }
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
		case KEY_PGUP: n = g_sel - 5; break;
		case KEY_PGDN: n = g_sel + 5; break;
		case KEY_ENTER: case '\n': case ' ': play_selected (); return true;
		default: return false;
		}
		if (n < 0) n = 0;
		if (n >= g_nent) n = g_nent - 1;
		if (n != g_sel && n >= 0) select_entry (n);
		return true;
	}
};

// The preview: the chosen table at rest (cached), or, refused, why it cannot be played
class Thumb : public Widget
{
public:
	Canvas cache; int cachedFor, cw, ch;
	Thumb (int l, int t, int w, int h) : Widget (l, t, w, h), cachedFor (-1), cw (0), ch (0) {}
	void forget () { cachedFor = -1; }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		if (g_sel < 0 || g_sel >= g_nent) return;
		TableEntry &e = *g_ent[g_sel];
		if (e.t)
		{
			if (cachedFor != g_sel || cw != width || ch != height || !cache.px)
			{
				if (cache.alloc (width, height))
				{
					cache.clear (bgColor ());
					View v = fit_view (*e.t, width, height, 4);
					uk_rbox (cache, (int) v.ox - 3, (int) v.oy - 3, (int) (e.t->w * v.s) + 6, (int) (e.t->h * v.s) + 6, 6, uk_tone (C_BG, 90), uk_tone (C_BG, 70));
					draw_static (cache, *e.t, v, false);
					draw_dynamic (cache, *e.t, e.look, v, 0);
					cachedFor = g_sel; cw = width; ch = height;
				}
			}
			if (cache.px) canvas.putOther (cache, 0, 0, false);
			return;
		}
		uk_sunken (canvas, 0, 0, width, height, 8, uk_tone (C_BG, 120));
		VPath p; int cx = width / 2, ty = 70; int tri[6] = { V (cx), V (ty), V (cx + 30), V (ty + 52), V (cx - 30), V (ty + 52) }; p.poly (tri, 3); p.fill (canvas, 0xE0A020);
		{ UkFaceScope sc (face (30)); uk_text_c (canvas, cx - 20, ty + 14, 40, 40, "!", 0x000000, 2); }
		const char *lines[3] = { TR ("This table cannot be played."), e.err, TR ("Correct the file in a text editor (Tinypad): the list is read again when you come back to it.") };
		int y = ty + 72;
		for (int k = 0; k < 3; k++)
		{
			int st[8], ln[8]; int n = uk_text_wrap (lines[k], (int) strlen (lines[k]), width - 28, 8, st, ln, 0, k == 0 ? 2 : 0);
			for (int i = 0; i < n; i++)
			{
				char b[240]; snprintf (b, sizeof b, "%.*s", ln[i], lines[k] + st[i]);
				uk_text (canvas, 14, y, b, k == 1 ? 0xC0302A : k == 0 ? C_TEXT : C_DIS, k == 0 ? 2 : 0); y += uk_fh ();
			}
			y += 8;
		}
	}
};

// The chosen table's top 5 ("—" for an empty rank)
class ScoreList : public Widget
{
public:
	ScoreList (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_text_l (canvas, 0, 0, 20, TR ("Best scores"), C_TEXT, 2);
		uk_etch_h (canvas, 0, 22, width, C_BG);
		pinball::ScoreLine sl[pinball::TOPN]; int n = 0;
		if (g_sel >= 0 && g_sel < g_nent && g_ent[g_sel]->t) n = pinball::scores_read (g_scores, g_ent[g_sel]->section, sl);
		for (int i = 0; i < pinball::TOPN; i++)
		{
			int y = 26 + i * 21; char b[8], s[32];
			snprintf (b, sizeof b, "%d", i + 1); uk_text_l (canvas, 2, y, 20, b, C_DIS, 2);
			if (i >= n) { uk_text_l (canvas, 22, y, 20, "\xE2\x80\x94", uk_mix (C_BG, C_DIS, 150)); continue; }
			fmt (sl[i].score, s, sizeof s); int sw = uk_text_w (s, 2);
			char fit[80]; uk_text_fit (sl[i].name, width - 30 - sw - 8, fit, sizeof fit);
			uk_text_l (canvas, 22, y, 20, fit, C_TEXT);
			uk_text_l (canvas, width - 2 - sw, y, 20, s, C_TEXT, 2);
		}
	}
};

#endif
