//
// calendar/views.h -- the planner's widgets: the time grid (a day or a week: the hours down the
// side, the events as coloured blocks -- click, double-click, drag to move, drag the bottom edge
// to resize; the all-day events in a row on top; now as a red line), the month grid, the
// categories (a coloured check box each) and the tasks, plus a few small pieces (a text field
// with a hint, the accent's button, a bold title).
//
#ifndef _calendar_views_h
#define _calendar_views_h

// What main.cpp does for them.
static int  g_today, g_nowMin;			// today (a day number), now (minutes into the day)
static int  g_sel = -1, g_selStart = 0;		// the event selected (its index, the occurrence's start)
static void open_event (int ev, int occStart);	// the editor on an event (-1: a new one at occStart)
static void new_event_at (int start, bool allDay, int end = -1);
static void goto_day (int dn, int view);	// show that day in that view (-1: the view kept)
static void event_moved (int ev, int oldOccStart, int newStart, int newEnd);
static void data_changed (bool save);
static void task_edit (int i);
static void sel_changed (void);

static int g_fh = 16, g_cw = 8;
#define DBL_TICKS	45

// ---- colours ------------------------------------------------------------------------------------------

static unsigned soft_ink (void) { return wk_mix (C_FIELD_TEXT, C_FIELD, 130); }
static unsigned grid_line (void) { return wk_mix (C_FIELD, C_FIELD_TEXT, 30); }
static unsigned face_soft (void) { return wk_mix (C_TEXT, C_BG, 110); }
static unsigned ev_fill (unsigned c, bool past) { return wk_mix (C_FIELD, c, past ? 34 : 58); }
static unsigned ev_ink (unsigned c, bool past) { unsigned k = wk_mix (c, 0x00000000, 120); return past ? wk_mix (k, C_FIELD, 90) : k; }
static const unsigned RED = 0x00E0453A;

// A disc of colour c (s px across), anti-aliased.
static int isqrt (int v) { int r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }
static void disc (Canvas &cv, int x, int y, int s, unsigned c)
{
	int r16 = s * 8;
	for (int j = 0; j < s; j++)
		for (int i = 0; i < s; i++)
		{
			int dx = i * 16 + 8 - r16, dy = j * 16 + 8 - r16;
			int a = (r16 - isqrt (dx * dx + dy * dy)) * 16 + 128;
			if (a > 0) wk_blend_px (cv, x + i, y + j, c, a > 255 ? 255 : a);
		}
}
// Text cut to w px ("..." when it does not fit).
static void fit (const char *s, int w, char *out, int cap, int style = 0)
{
	scpy (out, cap, s);
	for (int i = 0; out[i]; i++) if (out[i] == '\n') { out[i] = '\0'; break; }
	if (wk_text_w (out, style) <= w) return;
	int n = slen (out);
	while (n > 0)
	{
		out[--n] = '\0';
		char t[300]; scpy (t, sizeof t, out); scat (t, sizeof t, "...");
		if (wk_text_w (t, style) <= w) { scpy (out, cap, t); return; }
	}
}
// Word wrap: the next row of s (maxc characters at most): its length; *next: where the next starts.
static int wrap_row (const char *s, int maxc, const char **next)
{
	if (maxc < 3) maxc = 3;
	int n = 0; while (s[n] && s[n] != '\n' && n <= maxc) n++;
	if (n <= maxc) { *next = s[n] == '\n' ? s + n + 1 : s + n; return n; }
	int cut = maxc; while (cut > 0 && s[cut] != ' ') cut--;
	if (cut < maxc / 3) cut = maxc;
	const char *p = s + cut; while (*p == ' ') p++;
	*next = p;
	return cut;
}
static int iso_week (int dn)
{
	int th = dn - wday (dn) + 3;				// (the week's Thursday)
	int j1 = days_from_civil (dn_y (th), 1, 1);
	return (th - j1) / 7 + 1;
}
static void text_fit (Canvas &cv, int x, int y, int w, int h, const char *s, unsigned c, int style = 0)
{
	if (w < 8) return;
	char b[300]; fit (s, w, b, sizeof b, style);
	wk_text_l (cv, x, y, h, b, c, style);
}

// ---- small pieces -------------------------------------------------------------------------------------

// A Textbox showing a grey hint while it is empty and not focused.
class HintBox : public Textbox
{
public:
	const char *hint;
	HintBox (int l, int t, int w, int h, const char *hint_, Action cb = 0) : Textbox (l, t, w, h, "", cb), hint (hint_) {}
	void onDraw () override
	{
		Textbox::onDraw ();
		if (!text[0] && !hasFocus && hint) wk_text_l (canvas, 7, 0, height, hint, wk_mix (C_FIELD_TEXT, C_FIELD, 110));
	}
};

// A button filled with the accent (the main action), or outlined in red (danger).
class AccentButton : public Widget
{
public:
	char text[40]; Action cb; bool plus, danger;
	AccentButton (int l, int t, int w, int h, const char *s, Action cb_, bool plus_ = false, bool danger_ = false)
		: Widget (l, t, w, h), cb (cb_), plus (plus_), danger (danger_) { scpy (text, sizeof text, s); }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		if (danger)
		{
			unsigned f = pressed ? wk_mix (bgColor (), RED, 70) : hover ? wk_mix (bgColor (), RED, 30) : bgColor ();
			wk_rbox (canvas, 0, 0, width, height, 6, f, f);
			wk_rline (canvas, 0, 0, width, height, 6, RED);
			wk_text_c (canvas, 0, 0, width, height, text, RED, 2);
			return;
		}
		unsigned a = pressed ? wk_tone (C_ACCENT, 100) : hover ? wk_tone (C_ACCENT, 150) : C_ACCENT;
		wk_rbox (canvas, 0, 0, width, height, 6, wk_tone (a, 145), a);
		unsigned ink = wk_ink_on (C_ACCENT);
		int tw = wk_text_w (text, 2), x = (width - tw - (plus ? 18 : 0)) / 2;
		if (plus) { wk_glyph (canvas, WKG_PLUS, x + 6, height / 2, 11, ink); x += 18; }
		wk_text_l (canvas, x, 0, height, text, ink, 2);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0) { if (hover || pressed) { hover = pressed = false; invalidate (true); } return false; }
		if (!hover) { hover = true; invalidate (true); }
		if (bl && !pressed) { pressed = true; invalidate (true); }
		else if (!bl && pressed)
		{
			pressed = false; invalidate (true);
			if (mx >= 0 && my >= 0 && mx < width && my < height && cb) cb (*this);
		}
		return true;
	}
};

// Bold text on the face (the period shown).
class Title : public Widget
{
public:
	char text[96];
	Title (int l, int t, int w, int h) : Widget (l, t, w, h) { text[0] = '\0'; }
	void set (const char *s) { scpy (text, sizeof text, s); invalidate (true); }
	void onDraw () override { canvas.clear (bgColor ()); text_fit (canvas, 0, 0, width, height, text, C_TEXT, 2); }
};

// A heading in the side bar: small grey capitals and a line.
class Heading : public Widget
{
public:
	char text[40]; char right[24];
	Heading (int l, int t, int w, int h, const char *s) : Widget (l, t, w, h) { scpy (text, sizeof text, s); right[0] = '\0'; }
	void setRight (const char *s) { scpy (right, sizeof right, s); invalidate (true); }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		wk_text_l (canvas, 2, 0, height - 2, text, face_soft (), 2);
		if (right[0]) wk_text_l (canvas, width - 2 - wk_text_w (right), 0, height - 2, right, face_soft ());
		canvas.fillRect (0, height - 1, width, 1, wk_mix (C_BG, C_TEXT, 40));
	}
};

// ---- the time grid (a day, a week) ----------------------------------------------------------------------

#define GUTTER	58		// the hours' column
#define HDR_H	58		// the days' names
#define ADROW	22		// an all-day row

class TimeGrid : public Widget
{
public:
	int ndays = 7, day0 = 0;
	int hourH = 46, scrollY = 7 * 46;
	struct Hit { int x, y, w, h, ev, start, end; bool allDay; };
	Hit hits[400]; int nhits = 0;
	int adH = ADROW + 8, y0 = HDR_H + ADROW + 8;
	// the drag: 0 none, 1 pressed on an event, 2 moving it, 3 resizing it, 4 the scroll bar, 5 a new range
	int drag = 0, dx0, dy0, dHit = -1, pvStart, pvEnd, dayAtPress, minAtPress;
	unsigned lastClick = 0; int lastEv = -2, lastStart = 0;

	TimeGrid (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	unsigned bgColor () override { return C_FIELD; }
	int colW () const { int w = (width - GUTTER - WK_SBW - 2) / ndays; return w < 20 ? 20 : w; }
	int colX (int i) const { return GUTTER + i * colW (); }
	int bodyH () const { return height - y0; }
	void clampScroll () { int m = 24 * hourH - bodyH (); if (m < 0) m = 0; if (scrollY > m) scrollY = m; if (scrollY < 0) scrollY = 0; }
	void scrollToHour (int h) { scrollY = h * hourH; clampScroll (); invalidate (true); }

	// The events' blocks in one day's column: overlapping ones side by side.
	void lay_day (Occ *occ, int n, int di)
	{
		int dn = day0 + di, x0 = colX (di) + 1, w0 = colW () - 3;
		struct B { int k, s, e, col; } b[96]; int nb = 0;
		for (int k = 0; k < n && nb < 96; k++)
		{
			const Event &e = g_ev[occ[k].ev];
			if (e.allDay || occ[k].end - occ[k].start >= 1440) continue;
			int s = occ[k].start, en = occ[k].end;
			if (drag >= 2 && drag <= 3 && dHit >= 0 && occ[k].ev == hits[dHit].ev && occ[k].start == hits[dHit].start) { s = pvStart; en = pvEnd; }
			if (s >= (dn + 1) * 1440 || en <= dn * 1440) continue;
			if (s < dn * 1440) s = dn * 1440;
			if (en > (dn + 1) * 1440) en = (dn + 1) * 1440;
			if (en - s < 20) en = s + 20;
			b[nb].k = k; b[nb].s = s; b[nb].e = en; b[nb].col = 0; nb++;
		}
		for (int i = 1; i < nb; i++) { B t = b[i]; int j = i; while (j > 0 && b[j - 1].s > t.s) { b[j] = b[j - 1]; j--; } b[j] = t; }
		int i = 0;
		while (i < nb)					// a cluster: the ones that overlap one another
		{
			int j = i, cend = b[i].e, colEnd[16], ncol = 0;
			while (j < nb && (j == i || b[j].s < cend))
			{
				int c = 0; while (c < ncol && colEnd[c] > b[j].s) c++;
				if (c == ncol && ncol < 16) ncol++;
				if (c >= 16) c = 15;
				colEnd[c] = b[j].e; b[j].col = c;
				if (b[j].e > cend) cend = b[j].e;
				j++;
			}
			for (int k = i; k < j; k++)
			{
				int cw = w0 / ncol;
				int y = y0 + (b[k].s - dn * 1440) * hourH / 60 - scrollY;
				int h = (b[k].e - b[k].s) * hourH / 60 - 2;
				draw_block (occ[b[k].k], x0 + b[k].col * cw, y, b[k].col == ncol - 1 ? w0 - b[k].col * cw : cw - 2, h, b[k].s, b[k].e);
			}
			i = j;
		}
	}
	void draw_block (const Occ &o, int x, int y, int w, int h, int s, int en)
	{
		const Event &e = g_ev[o.ev];
		unsigned c = cat_color (e.cat);
		bool past = o.end <= g_today * 1440 + g_nowMin;
		bool sel = o.ev == g_sel && o.start == g_selStart;
		if (y + h < y0 || y > height) { add_hit (x, y, w, h, o, false); return; }
		unsigned fill = sel ? wk_mix (c, 0x00FFFFFF, 40) : ev_fill (c, past), ink = sel ? wk_ink_on (c) : ev_ink (c, past);
		if (sel) fill = c;
		wk_rbox (canvas, x, y, w, h, 5, fill, fill);
		wk_rbox (canvas, x, y, 4, h, 2, c, c, 255, WK_TL | WK_BL);
		if (h >= 16)
		{
			// The title (wrapped over the rows it may take), the time, the place -- what fits.
			const char *ti = e.title[0] ? e.title : "(No title)";
			int rows = (h - 4) / (g_fh + 1), tw = w - 11, ty = y + 2, maxc = tw / (g_cw > 0 ? g_cw : 8);
			char a[8], b[8], t[40]; fmt_hm (s, a, sizeof a); fmt_hm (en, b, sizeof b);
			scpy (t, sizeof t, a); scat (t, sizeof t, " - "); scat (t, sizeof t, b);
			if (wk_text_w (t) > tw) scpy (t, sizeof t, a);
			int trows = rows <= 1 ? 1 : rows - 1 - (e.place[0] && rows >= 4 ? 1 : 0);
			if (trows > 3) trows = 3;
			const char *p = ti;
			for (int r = 0; r < trows && *p; r++)
			{
				const char *nx; int n = wrap_row (p, maxc, &nx);
				char row[120]; int k = 0; for (; k < n && k < 119; k++) row[k] = p[k]; row[k] = '\0';
				if (r == trows - 1 && *nx) { scpy (row, sizeof row, p); }	// (the last row: cut with "...")
				text_fit (canvas, x + 8, ty, tw, g_fh, row, ink, 2); ty += g_fh + 1;
				p = nx;
			}
			if (rows >= 2) { text_fit (canvas, x + 8, ty, tw, g_fh, t, ink); ty += g_fh + 1; }
			if (e.place[0] && ty + g_fh <= y + h) text_fit (canvas, x + 8, ty, tw, g_fh, e.place, ink);
		}
		add_hit (x, y, w, h, o, false);
	}
	void add_hit (int x, int y, int w, int h, const Occ &o, bool ad)
	{
		if (nhits >= 400) return;
		Hit &t = hits[nhits++];
		t.x = x; t.y = y; t.w = w; t.h = h; t.ev = o.ev; t.start = o.start; t.end = o.end; t.allDay = ad;
	}

	void onDraw () override
	{
		canvas.clear (C_FIELD);
		nhits = 0;
		static Occ occ[600];
		int n = occurrences (day0, day0 + ndays, occ, 600);
		int cw = colW ();

		// The all-day row(s): each item on the first free row along its days.
		struct A { int k, a, b, row; } ad[64]; int nad = 0, rows = 0;
		unsigned char used[8][7]; for (int r = 0; r < 8; r++) for (int d = 0; d < 7; d++) used[r][d] = 0;
		for (int k = 0; k < n && nad < 64; k++)
		{
			const Event &e = g_ev[occ[k].ev];
			if (!e.allDay && occ[k].end - occ[k].start < 1440) continue;
			int a = occ[k].start / 1440 - day0, b = (occ[k].end - 1) / 1440 - day0;
			if (a < 0) a = 0;
			if (b >= ndays) b = ndays - 1;
			if (b < a) b = a;
			int r = 0;
			for (; r < 8; r++) { bool free = true; for (int d = a; d <= b; d++) if (used[r][d]) free = false; if (free) break; }
			if (r >= 8) continue;
			for (int d = a; d <= b; d++) used[r][d] = 1;
			ad[nad].k = k; ad[nad].a = a; ad[nad].b = b; ad[nad].row = r; nad++;
			if (r + 1 > rows) rows = r + 1;
		}
		adH = (rows < 1 ? 1 : rows) * ADROW + 8;
		y0 = HDR_H + adH;
		clampScroll ();

		// The body: the hours.
		int bw = cw * ndays;
		for (int di = 0; di < ndays; di++)
		{
			int dn = day0 + di, x = colX (di);
			unsigned bg = dn == g_today ? wk_mix (C_FIELD, C_ACCENT, 16) : wday (dn) >= 5 ? wk_mix (C_FIELD, C_FIELD_TEXT, 7) : C_FIELD;
			canvas.fillRect (x, y0, cw, height - y0, bg);
			// (the evening and the night a shade darker)
			int ya = y0 - scrollY, yb = y0 + 8 * hourH - scrollY, yc = y0 + 19 * hourH - scrollY;
			unsigned night = wk_mix (bg, C_FIELD_TEXT, 8);
			if (yb > y0) canvas.fillRect (x, ya < y0 ? y0 : ya, cw, yb - (ya < y0 ? y0 : ya), night);
			if (yc < height) canvas.fillRect (x, yc < y0 ? y0 : yc, cw, height - (yc < y0 ? y0 : yc), night);
		}
		for (int h = 0; h <= 24; h++)
		{
			int y = y0 + h * hourH - scrollY;
			if (y >= y0 - g_fh && y < height)
			{
				if (y >= y0) canvas.fillRect (GUTTER - 6, y, bw + 6, 1, grid_line ());
				if (h > 0 && h < 24 && y - g_fh / 2 >= y0)
				{
					char t[8]; fmt_hm (h * 60, t, sizeof t);
					wk_text_l (canvas, GUTTER - 10 - wk_text_w (t), y - g_fh / 2 - 1, g_fh, t, soft_ink ());
				}
			}
			int yh = y + hourH / 2;					// (the half hour: dotted)
			if (h < 24 && yh >= y0 && yh < height)
				for (int x = GUTTER; x < GUTTER + bw; x += 3) canvas.pixel (x, yh, wk_mix (C_FIELD, C_FIELD_TEXT, 18));
		}
		for (int di = 0; di <= ndays; di++) canvas.fillRect (colX (di) - (di == ndays ? 1 : 0), y0, 1, height - y0, grid_line ());

		// A range being drawn (a new event).
		if (drag == 5)
		{
			int di = pvStart / 1440 - day0;
			int ya = y0 + (pvStart % 1440) * hourH / 60 - scrollY, yb = y0 + (pvEnd - (pvStart / 1440) * 1440) * hourH / 60 - scrollY;
			wk_rbox (canvas, colX (di) + 1, ya, cw - 3, yb - ya - 1, 5, wk_mix (C_FIELD, C_ACCENT, 90), wk_mix (C_FIELD, C_ACCENT, 90));
			char a[8], b[8], t[24]; fmt_hm (pvStart, a, sizeof a); fmt_hm (pvEnd, b, sizeof b);
			scpy (t, sizeof t, a); scat (t, sizeof t, " - "); scat (t, sizeof t, b);
			text_fit (canvas, colX (di) + 8, ya + 2, cw - 12, g_fh, t, wk_ink_on (wk_mix (C_FIELD, C_ACCENT, 90)), 2);
		}
		for (int di = 0; di < ndays; di++) lay_day (occ, n, di);

		// Now: a red line across today.
		if (g_today >= day0 && g_today < day0 + ndays)
		{
			int y = y0 + g_nowMin * hourH / 60 - scrollY;
			if (y >= y0 && y < height)
			{
				int x = colX (g_today - day0);
				canvas.fillRect (x, y - 1, cw, 2, RED);
				disc (canvas, x - 5, y - 5, 10, RED);
			}
		}

		// The header over the body: the days' names and numbers, the all-day row.
		canvas.fillRect (0, 0, width, y0, C_FIELD);
		for (int di = 0; di < ndays; di++)
		{
			int dn = day0 + di, x = colX (di);
			bool today = dn == g_today;
			unsigned ink = today ? C_ACCENT : wday (dn) >= 5 ? soft_ink () : C_FIELD_TEXT;
			if (ndays == 1)
			{
				char t[40]; scpy (t, sizeof t, WDAY[wday (dn)]);
				wk_text_l (canvas, x + 12, 6, g_fh, t, today ? C_ACCENT : soft_ink (), 2);
			}
			else
			{
				char t[8]; scpy (t, sizeof t, WD3[wday (dn)]); for (int i = 0; t[i]; i++) if (t[i] >= 'a') t[i] = (char) (t[i] - 32);
				wk_text_c (canvas, x, 6, cw, g_fh, t, today ? C_ACCENT : soft_ink (), 2);
			}
			char num[4] = ""; scatn (num, sizeof num, dn_d (dn));
			int s = 30, cx = ndays == 1 ? x + 12 : x + (cw - s) / 2, cy = 6 + g_fh + 2;
			if (today) { disc (canvas, cx, cy, s, C_ACCENT); wk_text_c (canvas, cx, cy, s, s, num, wk_ink_on (C_ACCENT), 2); }
			else wk_text_c (canvas, cx, cy, s, s, num, ink, 2);
			if (di > 0) canvas.fillRect (x, HDR_H - 8, 1, y0 - HDR_H + 8, grid_line ());
		}
		wk_text_l (canvas, 6, HDR_H + 3, ADROW - 4, "all-day", soft_ink ());
		{ char wk[12] = "W"; scatn (wk, sizeof wk, iso_week (day0)); wk_text_c (canvas, 0, 6, GUTTER, g_fh, wk, soft_ink ()); }
		for (int i = 0; i < nad; i++)
		{
			const Occ &o = occ[ad[i].k]; const Event &e = g_ev[o.ev];
			unsigned c = cat_color (e.cat);
			int x = colX (ad[i].a) + 2, w = (ad[i].b - ad[i].a + 1) * cw - 5, y = HDR_H + 2 + ad[i].row * ADROW;
			bool sel = o.ev == g_sel && o.start == g_selStart;
			unsigned fill = sel ? c : wk_mix (C_FIELD, c, 170);
			wk_rbox (canvas, x, y, w, ADROW - 3, 5, fill, fill);
			text_fit (canvas, x + 7, y, w - 10, ADROW - 3, e.title[0] ? e.title : "(No title)", wk_ink_on (fill), 2);
			add_hit (x, y, w, ADROW - 3, o, true);
		}
		canvas.fillRect (0, y0 - 1, width, 1, wk_mix (C_FIELD, C_FIELD_TEXT, 60));

		// The scroll bar.
		WkThumb t = wk_thumb (24 * hourH, bodyH (), scrollY, bodyH () - 4);
		if (t.show) wk_draw_vscroll (canvas, width - WK_SBW - 2, y0 + 2, WK_SBW, bodyH () - 4, t, C_FIELD, drag == 4);
	}

	int hit_at (int mx, int my) const
	{
		for (int i = nhits - 1; i >= 0; i--)
		{
			const Hit &h = hits[i];
			if (!h.allDay && (my < y0)) continue;
			if (mx >= h.x && mx < h.x + h.w && my >= h.y && my < h.y + h.h) return i;
		}
		return -1;
	}
	int day_at (int mx) const { int d = (mx - GUTTER) / colW (); if (mx < GUTTER) d = 0; if (d >= ndays) d = ndays - 1; return day0 + d; }
	int min_at (int my) const { int m = (my - y0 + scrollY) * 60 / hourH; if (m < 0) m = 0; if (m > 1439) m = 1439; return m; }

	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx < 0 && !bl) { if (drag) { drag = 0; invalidate (true); } return false; }
		if (wheel) { scrollY -= wheel * hourH; clampScroll (); invalidate (true); return true; }
		if (drag == 4)
		{
			if (!bl) { drag = 0; pressed = false; invalidate (true); return true; }
			WkThumb t = wk_thumb (24 * hourH, bodyH (), scrollY, bodyH () - 4);
			scrollY = (int) wk_thumb_pos (my - y0 - 2, bodyH () - 4, 24 * hourH, bodyH (), t.h); clampScroll (); invalidate (true);
			return true;
		}
		if (drag == 5)					// drawing a new event's range
		{
			int m = (min_at (my) + 7) / 15 * 15;
			int a = dayAtPress * 1440 + minAtPress, b = dayAtPress * 1440 + m;
			if (b < a) { int t = a; a = b; b = t; }
			if (b - a < 15) b = a + 15;
			pvStart = a; pvEnd = b; invalidate (true);
			if (!bl) { drag = 0; pressed = false; invalidate (true); new_event_at (pvStart, false, pvEnd); }
			return true;
		}
		if (drag == 1 || drag == 2 || drag == 3)
		{
			const Hit &h = hits[dHit];
			if (bl)
			{
				if (drag == 1 && (mx - dx0) * (mx - dx0) + (my - dy0) * (my - dy0) > 25 && !h.allDay)
					drag = (dy0 >= h.y + h.h - 6) ? 3 : 2;
				if (drag == 2)
				{
					int dm = (my - dy0) * 60 / hourH; dm = (dm >= 0 ? dm + 7 : dm - 7) / 15 * 15;
					int dd = day_at (mx) - dayAtPress;
					pvStart = h.start + dd * 1440 + dm; pvEnd = h.end + dd * 1440 + dm;
					invalidate (true);
				}
				else if (drag == 3)
				{
					int e = (day_at (dx0) * 1440) + (min_at (my) + 7) / 15 * 15;
					if (e < h.start + 15) e = h.start + 15;
					pvStart = h.start; pvEnd = e;
					invalidate (true);
				}
				return true;
			}
			int d = drag; drag = 0; pressed = false;
			if ((d == 2 || d == 3) && (pvStart != h.start || pvEnd != h.end)) event_moved (h.ev, h.start, pvStart, pvEnd);
			invalidate (true);
			return true;
		}
		if (bl && !pressed)
		{
			pressed = true;
			WkThumb t = wk_thumb (24 * hourH, bodyH (), scrollY, bodyH () - 4);
			if (t.show && mx >= width - WK_SBW - 4 && my >= y0) { drag = 4; return onMouse (mx, my, bl, 0, 0, 0); }
			unsigned now = kapi_get_ticks ();
			int i = hit_at (mx, my);
			if (i >= 0)
			{
				const Hit &h = hits[i];
				bool dbl = lastEv == h.ev && lastStart == h.start && now - lastClick < DBL_TICKS;
				g_sel = h.ev; g_selStart = h.start; sel_changed ();
				lastEv = h.ev; lastStart = h.start; lastClick = now;
				if (dbl) { lastEv = -2; pressed = false; open_event (h.ev, h.start); return true; }
				drag = 1; dHit = i; dx0 = mx; dy0 = my; dayAtPress = day_at (mx); pvStart = h.start; pvEnd = h.end;
				invalidate (true);
				return true;
			}
			if (my < HDR_H && mx >= GUTTER) { if (ndays > 1) goto_day (day_at (mx), 0); return true; }
			bool dbl = lastEv == -1 && now - lastClick < DBL_TICKS;
			if (g_sel >= 0) { g_sel = -1; sel_changed (); }
			lastEv = -1; lastClick = now;
			if (my < y0 && mx >= GUTTER) { if (dbl) new_event_at (day_at (mx) * 1440, true); invalidate (true); return true; }
			if (mx >= GUTTER && my >= y0)
			{
				if (dbl) { lastEv = -2; new_event_at (day_at (mx) * 1440 + min_at (my) / 30 * 30, false); return true; }
				dayAtPress = day_at (mx); minAtPress = min_at (my) / 15 * 15; dx0 = mx; dy0 = my;
				drag = 6;				// (a press on the empty grid: a drag draws a new event)
			}
			invalidate (true);
			return true;
		}
		if (drag == 6)
		{
			if (!bl) { drag = 0; pressed = false; return true; }
			if ((my - dy0) * (my - dy0) > 64) { drag = 5; return onMouse (mx, my, bl, 0, 0, 0); }
			return true;
		}
		if (!bl) pressed = false;
		return true;
	}
	bool onKey (long k) override
	{
		if (k == KEY_PGUP) { scrollY -= bodyH () * 3 / 4; clampScroll (); invalidate (true); return true; }
		if (k == KEY_PGDN) { scrollY += bodyH () * 3 / 4; clampScroll (); invalidate (true); return true; }
		if (k == KEY_ENTER && g_sel >= 0) { open_event (g_sel, g_selStart); return true; }
		return false;
	}
};

// ---- the month grid ---------------------------------------------------------------------------------------

#define MHDR	30

class MonthGrid : public Widget
{
public:
	int year = 2026, month = 1;
	int sel = 0;					// the day selected (a day number)
	struct Hit { int x, y, w, h, ev, start; }; Hit hits[300]; int nhits = 0;
	unsigned lastClick = 0; int lastKey = -99, lastStart = 0;
	MonthGrid (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	unsigned bgColor () override { return C_FIELD; }
	int first () const { int d = days_from_civil (year, month, 1); return d - wday (d); }
	int cellW () const { return (width - 2) / 7; }
	int cellH () const { return (height - MHDR - 1) / 6; }
	static bool touches (const Occ &o, int dn)
	{ return o.start < (dn + 1) * 1440 && (o.end > dn * 1440 || (o.end == o.start && o.start >= dn * 1440)); }
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		nhits = 0;
		int cw = cellW (), ch = cellH (), d0 = first ();
		for (int i = 0; i < 7; i++)
		{
			char t[8]; scpy (t, sizeof t, WD3[i]); for (int k = 0; t[k]; k++) if (t[k] >= 'a') t[k] = (char) (t[k] - 32);
			wk_text_l (canvas, 1 + i * cw + 10, 0, MHDR, t, soft_ink (), 2);
		}
		static Occ occ[800];
		int n = occurrences (d0, d0 + 42, occ, 800);
		int rows = (ch - 30) / (g_fh + 3);
		for (int r = 0; r < 6; r++)
			for (int c = 0; c < 7; c++)
			{
				int dn = d0 + r * 7 + c, x = 1 + c * cw, y = MHDR + r * ch;
				bool in = dn_m (dn) == month, today = dn == g_today;
				unsigned bg = !in ? wk_mix (C_FIELD, C_FIELD_TEXT, 12) : c >= 5 ? wk_mix (C_FIELD, C_FIELD_TEXT, 6) : C_FIELD;
				if (dn == sel) bg = wk_mix (bg, C_ACCENT, 22);
				canvas.fillRect (x, y, cw, ch, bg);
				canvas.fillRect (x, y, cw, 1, grid_line ());
				canvas.fillRect (x, y, 1, ch, grid_line ());
				char num[12] = "";
				if (dn_d (dn) == 1) { scat (num, sizeof num, MON3[dn_m (dn) - 1]); scat (num, sizeof num, " "); }
				scatn (num, sizeof num, dn_d (dn));
				if (today)
				{
					int w = wk_text_w (num, 2) + 14; if (w < 24) w = 24;
					wk_rbox (canvas, x + 5, y + 4, w, 22, 11, C_ACCENT, C_ACCENT);
					wk_text_c (canvas, x + 5, y + 4, w, 22, num, wk_ink_on (C_ACCENT), 2);
				}
				else wk_text_l (canvas, x + 10, y + 4, 22, num, in ? C_FIELD_TEXT : soft_ink (), 2);
				// The day's events.
				int shown = 0, total = 0, yy = y + 29;
				for (int k = 0; k < n; k++) if (touches (occ[k], dn)) total++;
				int room = total > rows ? rows - 1 : rows;	// (a row for "+N more")
				for (int k = 0; k < n; k++)
				{
					if (!touches (occ[k], dn) || shown >= room) continue;
					const Event &e = g_ev[occ[k].ev];
					unsigned col = cat_color (e.cat);
					bool selEv = occ[k].ev == g_sel && occ[k].start == g_selStart;
					int ex = x + 4, ew = cw - 8, eh = g_fh + 1;
					if (e.allDay || occ[k].end - occ[k].start >= 1440)
					{
						unsigned f = selEv ? col : wk_mix (C_FIELD, col, 170);
						wk_rbox (canvas, ex, yy, ew, eh, 4, f, f);
						text_fit (canvas, ex + 6, yy, ew - 9, eh, e.title, wk_ink_on (f), 2);
					}
					else
					{
						if (selEv) wk_rbox (canvas, ex, yy, ew, eh, 4, wk_mix (C_FIELD, col, 60), wk_mix (C_FIELD, col, 60));
						disc (canvas, ex + 3, yy + (eh - 7) / 2, 7, col);
						char t[140]; fmt_hm (occ[k].start, t, sizeof t); scat (t, sizeof t, " ");
						int tw = ew >= 150 ? wk_text_w (t) : 0;	// (a narrow cell: the title alone)
						if (tw) wk_text_l (canvas, ex + 14, yy, eh, t, soft_ink ());
						text_fit (canvas, ex + 14 + tw, yy, ew - 16 - tw, eh, e.title, C_FIELD_TEXT);
					}
					if (nhits < 300) { Hit &h = hits[nhits++]; h.x = ex; h.y = yy; h.w = ew; h.h = eh; h.ev = occ[k].ev; h.start = occ[k].start; }
					yy += eh + 2; shown++;
				}
				if (total > shown)
				{
					char t[24] = "+"; scatn (t, sizeof t, total - shown); scat (t, sizeof t, " more");
					wk_text_l (canvas, x + 8, yy, g_fh + 1, t, soft_ink (), 2);
				}
			}
		canvas.fillRect (1 + 7 * cw, MHDR, 1, 6 * ch, grid_line ());
		canvas.fillRect (1, MHDR + 6 * ch, 7 * cw, 1, grid_line ());
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx < 0) { pressed = false; return false; }
		if (wheel) { goto_day (add_months (days_from_civil (year, month, 1), wheel > 0 ? -1 : 1), -1); return true; }
		if (bl && !pressed)
		{
			pressed = true;
			unsigned now = kapi_get_ticks ();
			for (int i = nhits - 1; i >= 0; i--)
			{
				const Hit &h = hits[i];
				if (mx >= h.x && mx < h.x + h.w && my >= h.y && my < h.y + h.h)
				{
					bool dbl = lastKey == h.ev && lastStart == h.start && now - lastClick < DBL_TICKS;
					g_sel = h.ev; g_selStart = h.start; sel_changed ();
					lastKey = h.ev; lastStart = h.start; lastClick = now;
					if (dbl) { lastKey = -99; open_event (h.ev, h.start); }
					invalidate (true);
					return true;
				}
			}
			if (my < MHDR) return true;
			int c = (mx - 1) / cellW (), r = (my - MHDR) / cellH ();
			if (c > 6) c = 6;
			if (r > 5) r = 5;
			int dn = first () + r * 7 + c;
			bool dbl = lastKey == -1 - dn && now - lastClick < DBL_TICKS;
			lastKey = -1 - dn; lastClick = now;
			sel = dn; if (g_sel >= 0) { g_sel = -1; sel_changed (); }
			if (dbl) { lastKey = -99; goto_day (dn, 0); }
			invalidate (true);
			return true;
		}
		if (!bl) pressed = false;
		return true;
	}
};

// ---- the categories ------------------------------------------------------------------------------------

class CategoryList : public Widget
{
public:
	int hot = -1;
	CategoryList (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	static int rowH () { return g_fh + 9; }
	void onDraw () override
	{
		canvas.clear (C_BG);
		for (int i = 0; i < g_ncat; i++)
		{
			int y = i * rowH ();
			if (i == hot) wk_rbox (canvas, 0, y, width, rowH (), 5, wk_mix (C_BG, C_TEXT, 22), wk_mix (C_BG, C_TEXT, 22));
			unsigned c = g_cat[i].color;
			int bx = 6, by = y + (rowH () - 16) / 2;
			if (g_cat[i].shown) { wk_rbox (canvas, bx, by, 16, 16, 4, c, c); wk_glyph (canvas, WKG_CHECK, bx + 8, by + 8, 10, 0x00FFFFFF); }
			else { wk_rbox (canvas, bx, by, 16, 16, 4, C_BG, C_BG); wk_rline (canvas, bx, by, 16, 16, 4, c); wk_rline (canvas, bx + 1, by + 1, 14, 14, 3, c); }
			text_fit (canvas, 30, y, width - 34, rowH (), g_cat[i].name, C_TEXT);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int r = mx < 0 ? -1 : my / rowH ();
		if (r >= g_ncat) r = -1;
		if (r != hot) { hot = r; invalidate (true); }
		if (mx < 0) { pressed = false; return false; }
		if (bl && !pressed)
		{
			pressed = true;
			if (r >= 0) { g_cat[r].shown = !g_cat[r].shown; invalidate (true); data_changed (true); }
		}
		else if (!bl) pressed = false;
		return true;
	}
};

// ---- the tasks ---------------------------------------------------------------------------------------------

class TaskList : public Widget
{
public:
	int order[512], n = 0, top = 0, hot = -1;
	unsigned lastClick = 0; int lastRow = -1;
	TaskList (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	static int rowH () { return g_fh + 12; }
	void sort ()					// to do first (the earliest due first, no date last), done at the end
	{
		n = 0;
		for (int i = 0; i < g_ntk && n < 512; i++) order[n++] = i;
		for (int i = 1; i < n; i++)
		{
			int t = order[i], j = i;
			while (j > 0 && less (t, order[j - 1])) { order[j] = order[j - 1]; j--; }
			order[j] = t;
		}
	}
	static bool less (int a, int b)
	{
		const Task &x = g_tk[a], &y = g_tk[b];
		if (x.done != y.done) return !x.done;
		int dx = x.due ? x.due : 1 << 30, dy = y.due ? y.due : 1 << 30;
		return dx < dy;
	}
	int rows () const { return height / rowH (); }
	void onDraw () override
	{
		sort ();
		canvas.clear (C_BG);
		if (top > n - rows ()) top = n - rows ();
		if (top < 0) top = 0;
		if (n == 0) { wk_text_c (canvas, 0, 6, width, g_fh, "Nothing to do", face_soft ()); return; }
		for (int r = 0; r < rows () && top + r < n; r++)
		{
			int i = order[top + r], y = r * rowH ();
			const Task &t = g_tk[i];
			if (top + r == hot) wk_rbox (canvas, 0, y, width, rowH (), 5, wk_mix (C_BG, C_TEXT, 22), wk_mix (C_BG, C_TEXT, 22));
			int cy = y + (rowH () - 16) / 2;
			if (t.done) { disc (canvas, 6, cy, 16, C_ACCENT); wk_glyph (canvas, WKG_CHECK, 14, cy + 8, 9, wk_ink_on (C_ACCENT)); }
			else { disc (canvas, 6, cy, 16, t.cat >= 0 ? cat_color (t.cat) : face_soft ()); disc (canvas, 8, cy + 2, 12, C_BG); }
			char due[24] = ""; unsigned dc = face_soft ();
			if (t.due && !t.done)
			{
				if (t.due == g_today) { scpy (due, sizeof due, "Today"); dc = C_ACCENT; }
				else if (t.due == g_today + 1) scpy (due, sizeof due, "Tomorrow");
				else { scpy (due, sizeof due, MON3[dn_m (t.due) - 1]); scat (due, sizeof due, " "); scatn (due, sizeof due, dn_d (t.due)); if (t.due < g_today) dc = RED; }
			}
			int dw = due[0] ? wk_text_w (due) + 8 : 0;
			unsigned ink = t.done ? face_soft () : C_TEXT;
			char b[140]; fit (t.title, width - 32 - dw, b, sizeof b);
			wk_text_l (canvas, 30, y, rowH (), b, ink);
			if (t.done) canvas.fillRect (30, y + rowH () / 2, wk_text_w (b), 1, ink);
			if (due[0]) wk_text_l (canvas, width - dw + 2, y, rowH (), due, dc, t.due <= g_today ? 2 : 0);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		int r = mx < 0 ? -1 : top + my / rowH ();
		if (r >= n) r = -1;
		if (r != hot) { hot = r; invalidate (true); }
		if (mx < 0) { pressed = false; return false; }
		if (wheel) { top -= wheel; invalidate (true); return true; }
		if (bl && !pressed)
		{
			pressed = true;
			if (r < 0) return true;
			int i = order[r];
			unsigned now = kapi_get_ticks ();
			if (mx < 28) { g_tk[i].done = !g_tk[i].done; invalidate (true); data_changed (true); return true; }
			bool dbl = lastRow == r && now - lastClick < DBL_TICKS;
			lastRow = r; lastClick = now;
			if (dbl) { lastRow = -1; task_edit (i); }
		}
		else if (!bl) pressed = false;
		return true;
	}
};

#endif
