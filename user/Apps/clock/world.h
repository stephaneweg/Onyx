//
// world.h -- the Clock's World tab (04 D4-D8; 03 step 6). One translation unit with main.cpp.
//
//   HereCard   here, big: HH:MM:SS, the date, the zone's city, UTC+h, Summer time (no zone chosen: a warning and a
//              button that opens Language & Region)
//   CityList   the cities followed (config.ini's cities =, 12 at most): each its time, its day when not today, its
//              difference to here; Up / Down / Home / End select, Ctrl+Up / Ctrl+Down move, Delete removes
//   CityVeil   Add a City: the zones not chosen yet (SystemKit's table), a filter, Enter / a double click adds
// The time of a city: UTC + SystemKit's locale_zone_offset_at (city, UTC) -- the summer time judged at the instant.
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

// The cities' names are SystemKit's (English, stored so in config.ini); shown through TR (). Those with a French name:
// TR: Brussels
// TR: London
// TR: Vienna
// TR: Warsaw
// TR: Lisbon
// TR: Athens
static const char *city_name (int z) { return TR (locale_zone_city (z)); }

// A name made comparable: lower case, the accents of Latin-1 dropped ("Athènes" -> "athenes")
static void fold (const char *s, char *o, int cap)
{
	static const char LAT[] = "aaaaaaaceeeeiiiidnooooo/ouuuuyty";	// U+00E0..U+00FF (U+00C0.. folded to them)
	int n = 0;
	while (*s && n < cap - 1)
	{
		unsigned char c = (unsigned char) *s;
		if (c == 0xC3 && s[1]) { unsigned char d = (unsigned char) s[1] | 0x20; o[n++] = d >= 0xA0 && d <= 0xBF ? LAT[d - 0xA0] : '?'; s += 2; continue; }
		if (c >= 0x80) { s++; while ((*s & 0xC0) == 0x80) s++; o[n++] = '?'; continue; }
		o[n++] = (char) (c >= 'A' && c <= 'Z' ? c + 32 : c);
		s++;
	}
	o[n] = 0;
}

// A zone's standard offset (its smaller of January's and July's)
static int zone_std (int z, int year)
{
	int a = locale_zone_offset_at (z, (long long) clk_days (year, 1, 15) * 1440), b = locale_zone_offset_at (z, (long long) clk_days (year, 7, 15) * 1440);
	return a < b ? a : b;
}
static int city_offset (int z) { return locale_zone_offset_at (z, g_now.utc >= 0 ? g_now.utc / 60 : -((-g_now.utc + 59) / 60)); }

static void world_changed ();
static void world_add_open ();
static void world_lr (Widget &) { say ("Language & Region opened"); lx_launch ("control", "langconf"); }

// ---- here, big ---------------------------------------------------------------------------------------------------------
// The analogue face (S3, View > Analogue Clock; config.ini face = analogue): a dial of radius r centred at (cx, cy) --
// the field's colour, an accent rim, 60 minute ticks and 12 hour ones, the hour and minute hands, a red-free accent
// second hand with its tail, the pin in the middle. Angles clockwise from 12 o'clock.
static void draw_face (Canvas &cv, double cx, double cy, double r, int h, int m, int s)
{
	unsigned ink = C_FIELD_TEXT, faint = uk_mix (C_FIELD, C_FIELD_TEXT, 110);
	VPath dial; dial.circle (VV (cx), VV (cy), VV (r)); dial.fill (cv, C_FIELD);
	VPath rim; rim.arc (VV (cx), VV (cy), VV (r - 1.5), 0, 360, VV (3)); rim.fill (cv, C_ACCENT);
	VPath minor, major;
	for (int i = 0; i < 60; i++)
	{
		double t = M_PI * i / 30, sn = sin (t), cs = cos (t);
		bool hour = i % 5 == 0;
		double r0 = r - (hour ? (i % 15 == 0 ? 13 : 11) : 7.5), r1 = r - 5;
		(hour ? major : minor).line (VV (cx + r0 * sn), VV (cy - r0 * cs), VV (cx + r1 * sn), VV (cy - r1 * cs), VV (hour ? 2.4 : 1));
	}
	minor.fill (cv, faint);
	major.fill (cv, ink);
	auto hand = [&] (double turn, double len, double tail, double w, unsigned c)
	{
		double t = 2 * M_PI * turn, sn = sin (t), cs = cos (t);
		VPath p; p.line (VV (cx - tail * sn), VV (cy + tail * cs), VV (cx + len * sn), VV (cy - len * cs), VV (w)); p.fill (cv, c);
	};
	hand (((h % 12) + m / 60.0) / 12, r * 0.50, r * 0.08, 5.5, ink);
	hand ((m + s / 60.0) / 60, r * 0.76, r * 0.08, 3.6, ink);
	hand (s / 60.0, r * 0.84, r * 0.20, 1.6, C_ACCENT);
	VPath pin; pin.circle (VV (cx), VV (cy), VV (4.5)); pin.fill (cv, C_ACCENT);
	VPath dot; dot.circle (VV (cx), VV (cy), VV (1.8)); dot.fill (cv, C_FIELD);
}

class HereCard : public Widget
{
public:
	enum { FACE_R = 55, GAP = 26 };	// the analogue face's radius, the space between it and the words
	Button *lr;			// "Language & Region..." (no zone chosen)
	HereCard (int x, int y, int w, int h) : Widget (x, y, w, h)
	{
		anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
		lr = new Button (0, 92, 10, 28, TR ("Language & Region\xE2\x80\xA6"), world_lr);
		lr->resizeTo (uk_tw (lr->text) + 30, 28);
		addChild (lr);
		place ();
	}
	bool nozone () const { return g_now.zone < 0; }
	int warnW () { UkFaceScope sc (face (13)); return uk_tw (TR ("Time zone not set"), 2) + 26; }
	// the words under the time: the date, and the zone's line (its city, UTC+h, Summer time)
	void dateText (char *b, int cap)
	{
		if (g_now.real) snprintf (b, cap, "%s %d %s %d", TR (DAYNAME[clk_wday (g_now.day)]), g_now.d, TR (MONTHNAME[(g_now.mo + 11) % 12]), g_now.y);
		else snprintf (b, cap, "%s", TR ("Clock not set yet"));
	}
	void zoneText (char *b, int cap)
	{
		char u[16];
		clk_fmt_utc (g_now.here_off, u, sizeof u);
		snprintf (b, cap, "%s" DOT "%s", city_name (g_now.zone), u);
		if (g_now.here_off > zone_std (g_now.zone, g_now.y)) { cat (b, cap, DOT); cat (b, cap, TR ("Summer time")); }
	}
	// Analogue: the face and the words' column, centred together -> the face's left, the column's left
	void analogueCols (int &x0, int &tx)
	{
		char t[32], b[160];
		clk_fmt_hms (g_now.h, g_now.mi, g_now.s, t, sizeof t);
		int cw;
		{ UkFaceScope sc (face (34)); cw = uk_tw ("00:00:00", 2); }
		dateText (b, sizeof b);
		{ UkFaceScope sc (face (16)); int w = uk_tw (b); if (w > cw) cw = w; }
		if (nozone ()) { int w = warnW () + 12 + lr->width; if (w > cw) cw = w; }
		else { zoneText (b, sizeof b); int w = uk_tw (b) + 18; if (w > cw) cw = w; }
		int bw = 2 * FACE_R + GAP + cw;
		x0 = (width - bw) / 2;
		if (x0 < 4) x0 = 4;
		tx = x0 + 2 * FACE_R + GAP;
	}
	void place ()			// the warning and its button, centred together (analogue: in the words' column)
	{
		lr->hidden = !nozone ();
		if (g_cfg.analogue)
		{
			int x0, tx; analogueCols (x0, tx);
			lr->left = tx + warnW () + 12; lr->top = 82;
			return;
		}
		int sw = warnW (), x = (width - sw - 12 - lr->width) / 2;
		lr->left = x + sw + 12; lr->top = 92;
	}
	void layout () override { Widget::layout (); place (); }
	void onDraw () override
	{
		canvas.clear (C_BG);
		place ();
		if (g_cfg.analogue) { drawAnalogue (); return; }
		char t[32], b[160];
		clk_fmt_hms (g_now.h, g_now.mi, g_now.s, t, sizeof t);
		{ UkFaceScope sc (face (54)); uk_text_c (canvas, 0, 0, width, 66, t, C_TEXT, 2); }
		unsigned d = dim_on (C_BG);
		dateText (b, sizeof b);
		{ UkFaceScope sc (face (16)); uk_text_c (canvas, 0, 64, width, 24, b, g_now.real ? C_TEXT : d); }
		if (nozone ())
		{
			int sw = warnW (), x = (width - sw - 12 - lr->width) / 2;
			draw_warn (canvas, x + 9, 92 + 14, 16);
			UkFaceScope sc (face (13));
			uk_text_l (canvas, x + 24, 92, 28, TR ("Time zone not set"), AMBER (), 2);
			return;
		}
		zoneText (b, sizeof b);
		int w = uk_tw (b) + 18, x = (width - w) / 2;
		draw_pin (canvas, x + 6, 104, 14, C_ACCENT);
		uk_text_l (canvas, x + 18, 92, 24, b, d);
	}
	// The analogue face left, the time (smaller), the date and the zone's line in a column right of it
	void drawAnalogue ()
	{
		int x0, tx; analogueCols (x0, tx);
		draw_face (canvas, x0 + FACE_R, height / 2.0, FACE_R, g_now.h, g_now.mi, g_now.s);
		char t[32], b[160];
		clk_fmt_hms (g_now.h, g_now.mi, g_now.s, t, sizeof t);
		{ UkFaceScope sc (face (34)); uk_text_l (canvas, tx, 10, 42, t, C_TEXT, 2); }
		unsigned d = dim_on (C_BG);
		dateText (b, sizeof b);
		{ UkFaceScope sc (face (16)); uk_text_l (canvas, tx, 54, 24, b, g_now.real ? C_TEXT : d); }
		if (nozone ())
		{
			draw_warn (canvas, tx + 9, 82 + 14, 16);
			UkFaceScope sc (face (13));
			uk_text_l (canvas, tx + 24, 82, 28, TR ("Time zone not set"), AMBER (), 2);
			return;
		}
		zoneText (b, sizeof b);
		draw_pin (canvas, tx + 6, 94, 14, C_ACCENT);
		uk_text_l (canvas, tx + 18, 82, 24, b, d);
	}
};

// ---- the cities --------------------------------------------------------------------------------------------------------
class CityList : public Widget
{
public:
	enum { ROW = 50 };
	int sel, top;			// the selected row (-1 none), the px scrolled
	UkBarDrag bar;
	CityList (int x, int y, int w, int h) : Widget (x, y, w, h), sel (-1), top (0) { canFocus = true; anchor = ANCHOR_FILL; }
	unsigned bgColor () override { return C_BG; }
	int contentH () const { return g_cfg.ncity * ROW + 8; }
	void clampTop () { int most = contentH () - height; if (top > most) top = most; if (top < 0) top = 0; }
	void showSel ()
	{
		if (sel < 0) return;
		int y = 4 + sel * ROW;
		if (y < top) top = y - 4;
		if (y + ROW > top + height) top = y + ROW - height + 4;
		clampTop ();
	}
	void select (int i)
	{
		if (g_cfg.ncity == 0) i = -1;
		else if (i < 0) i = 0;
		else if (i >= g_cfg.ncity) i = g_cfg.ncity - 1;
		sel = i; showSel (); invalidate (true);
		world_changed ();
	}
	void onDraw () override
	{
		canvas.clear (C_BG);
		uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD, hasFocus);
		unsigned ink = C_FIELD_TEXT, d = uk_mix (C_FIELD, C_FIELD_TEXT, 140), faint = uk_mix (C_FIELD, C_FIELD_TEXT, 30);
		if (g_cfg.ncity == 0)
		{
			draw_globe (canvas, width / 2, height / 2 - 30, 26, uk_mix (C_FIELD, C_FIELD_TEXT, 70));
			{ UkFaceScope sc (face (15)); uk_text_c (canvas, 0, height / 2 + 4, width, 22, TR ("No cities yet"), ink, 2); }
			uk_text_c (canvas, 0, height / 2 + 28, width, 20, TR ("Add the cities you want to follow with + Add City (Ctrl+N)."), d);
			return;
		}
		clampTop ();
		UkThumb th = uk_thumb (contentH (), height, top, height - 4);
		int rw = width - (th.show ? UK_SBW + 2 : 0);
		bool nz = g_now.zone < 0;
		for (int i = 0; i < g_cfg.ncity; i++)
		{
			int z = g_cfg.city[i], y = 4 + i * ROW - top;
			if (y + ROW < 0 || y > height) continue;
			if (i == sel) { unsigned t = uk_mix (C_FIELD, C_ACCENT, hasFocus ? 90 : 60); uk_rbox (canvas, 5, y, rw - 10, ROW - 3, 7, t, t); }
			else if (i + 1 < g_cfg.ncity && i + 1 != sel) canvas.fillRect (44, y + ROW - 2, rw - 58, 1, faint);
			int off = city_offset (z);
			ClkCity c = clk_city (g_now.utc, off, g_now.day, g_now.here_off);
			int cy = y + (ROW - 3) / 2;
			if (c.hh >= 7 && c.hh < 19) draw_sun (canvas, 24, cy, 10); else draw_moon (canvas, 24, cy, 10);
			{ UkFaceScope sc (face (15)); uk_text (canvas, 44, y + 5, city_name (z), ink, 2); }
			char df[32], u[16], l2[96];
			clk_fmt_utc (off, u, sizeof u);
			if (nz) snprintf (l2, sizeof l2, "%s", u);
			else
			{
				if (c.diff == 0) snprintf (df, sizeof df, "%s", TR ("Same time"));
				else
				{
					clk_fmt_diff (c.diff, df, sizeof df);
					if (df[0] == '-') { memmove (df + 3, df + 1, strlen (df)); memcpy (df, "\xE2\x88\x92", 3); }	// (a true minus sign)
				}
				snprintf (l2, sizeof l2, "%s" DOT "%s", df, u);
			}
			int x = 44;
			if (c.day && !nz)
			{
				const char *w = c.day > 0 ? TR ("Tomorrow") : TR ("Yesterday");
				uk_text (canvas, x, y + 27, w, C_ACCENT, 2); x += uk_tw (w, 2);
				uk_text (canvas, x, y + 27, DOT, d); x += uk_tw (DOT);
			}
			uk_text (canvas, x, y + 27, l2, d);
			char t[8]; clk_fmt_hm (c.hh, c.mm, t, sizeof t);
			UkFaceScope sc (face (26)); uk_text (canvas, rw - 18 - uk_tw (t, 2), y + (ROW - 3 - uk_fh ()) / 2, t, ink, 2);
		}
		if (th.show) uk_draw_vscroll (canvas, width - UK_SBW - 2, 2, UK_SBW, height - 4, th, C_FIELD);
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		long pos = top;
		if (bar.mouse (mx, my, bl, width - UK_SBW - 2, UK_SBW, 2, height - 4, contentH (), height, &pos)) { top = (int) pos; invalidate (true); return true; }
		if (mx < 0) return false;
		if (wheel) { top -= wheel * ROW; clampTop (); invalidate (true); return true; }
		if (bl && !pressed)
		{
			pressed = true; setFocus ();
			int i = (my + top - 4) / ROW;
			if (my + top >= 4 && i < g_cfg.ncity) select (i);
			invalidate (true);
		}
		else if (!bl) pressed = false;
		return true;
	}
	bool onKey (long k) override
	{
		if (kapi_get_modifiers () & MOD_CTRL) return false;	// (Ctrl+Up / Down: the window's -- moved)
		int page = height / ROW > 1 ? height / ROW - 1 : 1;
		switch (k)
		{
		case KEY_UP:   select (sel - 1); return true;
		case KEY_DOWN: select (sel + 1); return true;
		case KEY_HOME: select (0); return true;
		case KEY_END:  select (g_cfg.ncity - 1); return true;
		case KEY_PGUP: select (sel - page); return true;
		case KEY_PGDN: select (sel + page); return true;
		}
		return false;
	}
};

// ---- Add a City --------------------------------------------------------------------------------------------------------
static void world_add_pick (Widget &);
static void world_add_do ();
static void world_add_cancel (Widget &);
static void world_add_filter (Widget &);
class CityVeil : public Veil
{
public:
	Textbox *filter;
	DataGrid *grid;
	int zones[32], n;
	CityVeil () : Veil (380, 364, TR ("Add a City")), n (0)
	{
		int y = th () + 10;
		filter = new Textbox (0, 0, cw - 20 - 44, 28, "");
		filter->changed = world_add_filter; filter->maxLen = 40;
		place (filter, 44, y);
		grid = new DataGrid (0, 0, cw - 40, 200);
		grid->setColumns (3);
		grid->setColumn (0, TR ("City"), 170);
		grid->setColumn (1, TR ("Time"), 70, GRID_RIGHT);
		grid->setColumn (2, "UTC", cw - 40 - 2 - 240 - 12, GRID_RIGHT);
		grid->stripes = true;
		grid->cellText = cell;
		grid->onActivate = world_add_pick;
		grid->emptyText = TR ("No city matches");
		grid->user = this;
		place (grid, 20, y + 38);
		Button *ad = new Button (0, 0, 96, 32, TR ("Add"), world_add_pick);
		place (ad, cw - 20 - 96, ch - 46);
		place (new Button (0, 0, 104, 32, TR ("Cancel"), world_add_cancel), cw - 20 - 96 - 10 - 104, ch - 46);
	}
	static const char *cell (DataGrid &g, int row, int col, char *buf, int cap)
	{
		CityVeil *v = (CityVeil *) g.user;
		if (row < 0 || row >= v->n) return "";
		int z = v->zones[row];
		if (col == 0) return city_name (z);
		int off = city_offset (z);
		if (col == 1) { ClkCity c = clk_city (g_now.utc, off, g_now.day, g_now.here_off); clk_fmt_hm (c.hh, c.mm, buf, cap); return buf; }
		clk_fmt_utc (off, buf, cap); return buf;
	}
	// the zones not chosen, matching the filter, sorted by their shown name
	void refill ()
	{
		char f[64], a[64], b[64];
		fold (filter->text, f, sizeof f);
		n = 0;
		for (int z = 0; z < locale_zone_count () && n < 32; z++)
		{
			bool chosen = false;
			for (int i = 0; i < g_cfg.ncity; i++) chosen |= g_cfg.city[i] == z;
			if (chosen) continue;
			fold (city_name (z), a, sizeof a); fold (locale_zone_city (z), b, sizeof b);
			if (f[0] && !strstr (a, f) && !strstr (b, f)) continue;
			zones[n++] = z;
		}
		for (int i = 0; i < n; i++)
			for (int j = i + 1; j < n; j++)
			{
				fold (city_name (zones[i]), a, sizeof a); fold (city_name (zones[j]), b, sizeof b);
				if (strcmp (b, a) < 0) { int t = zones[i]; zones[i] = zones[j]; zones[j] = t; }
			}
		grid->setRows (n);
		grid->setSel (n ? 0 : -1);
		grid->invalidate (true);
	}
	void drawCard () override
	{
		unsigned d = dim_on (C_BG);
		uk_tool_glyph (canvas, WKT_SEARCH, cx + 20, cy + th () + 13, 16, d);
		uk_text_l (canvas, cx + 20, cy + ch - 84, 20, TR ("Type to filter \xC2\xB7 Enter: add \xC2\xB7 Esc: cancel"), d);
	}
	bool onKey (long k) override
	{
		if (k == KEY_ENTER) { world_add_do (); return true; }
		if (k == 27) { hide (); return true; }
		if (k == KEY_UP || k == KEY_DOWN || k == KEY_PGUP || k == KEY_PGDN || k == KEY_HOME || k == KEY_END) return grid->onKey (k);
		return false;
	}
	void onHidden () override;
};

// ---- the tab -----------------------------------------------------------------------------------------------------------
static HereCard *g_here;
static CityList *g_cities;
static FootText *g_worldFoot;
static ToolButton *g_cAdd, *g_cRemove, *g_cUp, *g_cDown;
static CityVeil *g_cityVeil;
static long g_worldLogged = -1;			// the minute the rows were last printed

void CityVeil::onHidden () { g_cities->setFocus (); }

// the footer, the buttons, the rows printed (the PC tests read them)
static void world_changed ()
{
	char s[96];
	if (g_cfg.ncity == 1) snprintf (s, sizeof s, "%s", TR ("1 city \xC2\xB7 up to 12"));
	else if (g_cfg.ncity) snprintf (s, sizeof s, TR ("%d cities \xC2\xB7 up to 12"), g_cfg.ncity);
	else snprintf (s, sizeof s, "%s", TR ("No cities"));
	g_worldFoot->set (s);
	g_cAdd->setDisabled (g_cfg.ncity >= CITIES_MAX);
	g_cRemove->setDisabled (g_cities->sel < 0);
	g_cUp->setDisabled (g_cities->sel <= 0);
	g_cDown->setDisabled (g_cities->sel < 0 || g_cities->sel >= g_cfg.ncity - 1);
	g_cities->invalidate (true);
	g_worldLogged = -1;
}
static void world_print ()
{
	char t[16], u[16];
	clk_fmt_hms (g_now.h, g_now.mi, g_now.s, t, sizeof t);
	if (g_now.zone < 0)						// (where its button is, for the PC tests' click)
		say ("here %s: time zone not set (Language & Region at %d,%d)", t, g_here->left + g_here->lr->left + g_here->lr->width / 2, TOP + g_here->top + g_here->lr->top + 14);
	else
	{
		clk_fmt_utc (g_now.here_off, u, sizeof u);
		say ("here %s %s%s (%s)", t, u, g_now.here_off > zone_std (g_now.zone, g_now.y) ? " summer" : "", locale_zone_city (g_now.zone));
	}
	for (int i = 0; i < g_cfg.ncity; i++)
	{
		int z = g_cfg.city[i];
		ClkCity c = clk_city (g_now.utc, city_offset (z), g_now.day, g_now.here_off);
		char hm[8], df[16];
		clk_fmt_hm (c.hh, c.mm, hm, sizeof hm);
		clk_fmt_diff (c.diff, df, sizeof df);
		say ("city %s %s %s%s", locale_zone_city (z), hm, g_now.zone < 0 ? "" : c.diff ? df : "same time",
		     g_now.zone < 0 || !c.day ? "" : c.day > 0 ? " tomorrow" : " yesterday");
	}
}
static void cities_saved ()
{
	char t[400];
	cfg_cities_text (t, sizeof t);
	say ("cities %s", t);
	if (!cfg_save ()) say ("config.ini not saved");
	world_changed ();
}

static void world_add_open ()
{
	tab_show (TAB_WORLD);
	if (g_cfg.ncity >= CITIES_MAX) { say ("city refused (12 already)"); return; }
	g_cityVeil->filter->setText ("");
	g_cityVeil->refill ();
	g_cityVeil->show (g_root, g_cityVeil->filter);
	say ("add a city (%d zones)", g_cityVeil->n);
}
static void world_add_pick (Widget &) { world_add_do (); }
static void world_add_do ()
{
	CityVeil *v = g_cityVeil;
	int r = v->grid->sel;
	if (r < 0 || r >= v->n) return;
	if (g_cfg.ncity >= CITIES_MAX) { say ("city refused (12 already)"); v->hide (); return; }
	g_cfg.city[g_cfg.ncity++] = v->zones[r];
	v->hide ();
	g_cities->select (g_cfg.ncity - 1);
	cities_saved ();
}
static void world_add_cancel (Widget &) { g_cityVeil->hide (); }
static void world_add_filter (Widget &) { g_cityVeil->refill (); }
static void world_remove ()
{
	tab_show (TAB_WORLD);
	int i = g_cities->sel;
	if (i < 0 || i >= g_cfg.ncity) return;
	for (int k = i; k + 1 < g_cfg.ncity; k++) g_cfg.city[k] = g_cfg.city[k + 1];
	g_cfg.ncity--;
	g_cities->select (i < g_cfg.ncity ? i : g_cfg.ncity - 1);
	cities_saved ();
}
static void world_move (int dir)
{
	tab_show (TAB_WORLD);
	int i = g_cities->sel, j = i + dir;
	if (i < 0 || j < 0 || j >= g_cfg.ncity) return;
	int t = g_cfg.city[i]; g_cfg.city[i] = g_cfg.city[j]; g_cfg.city[j] = t;
	g_cities->select (j);
	cities_saved ();
}
static void tb_add (Widget &) { world_add_open (); }
static void tb_remove (Widget &) { world_remove (); }
static void tb_up (Widget &) { world_move (-1); }
static void tb_down (Widget &) { world_move (1); }

static void world_build (Page *p)
{
	int w = p->width, h = p->height;
	g_here = new HereCard (12, 6, w - 24, 122);
	p->addChild (g_here);
	g_cities = new CityList (12, 134, w - 24, h - FOOT - 134);
	p->addChild (g_cities);
	g_worldFoot = new FootText (14, h - FOOT + 8, 220, 30);
	g_worldFoot->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM;
	p->addChild (g_worldFoot);
	int x = w - 12;
	ToolButton *b[4];
	b[0] = g_cDown = tool_button (TR ("Move down"), WKT_NONE, 0, tb_down, 30); g_cDown->setIcon (chev_icon, 1);
	b[1] = g_cUp = tool_button (TR ("Move up"), WKT_NONE, 0, tb_up, 30); g_cUp->setIcon (chev_icon, 0);
	b[2] = g_cRemove = tool_button (TR ("Remove the city (Delete)"), WKT_MINUS, TR ("Remove"), tb_remove);
	b[3] = g_cAdd = tool_button (TR ("Add a city (Ctrl+N)"), WKT_PLUS, TR ("Add City"), tb_add);
	for (int i = 0; i < 4; i++)
	{
		x -= b[i]->width;
		b[i]->left = x; b[i]->top = h - FOOT + 8;
		b[i]->anchor = ANCHOR_RIGHT | ANCHOR_BOTTOM;
		p->addChild (b[i]);
		x -= i == 1 ? 12 : 6;
	}
	g_cityVeil = new CityVeil ();
	if (g_cfg.ncity) g_cities->sel = 0;
	world_changed ();
}
static void world_shown ()
{
	g_cities->setFocus ();
	g_here->invalidate (true);
	world_changed ();
}
static void world_tick ()
{
	static int s_sec = -1;
	if (g_tab != TAB_WORLD) return;
	if (g_now.s != s_sec) { s_sec = g_now.s; g_here->invalidate (true); }
	if (g_now.minute != g_worldLogged)
	{
		if (g_worldLogged >= 0) g_cities->invalidate (true);	// (a new minute: the cities' times)
		g_worldLogged = g_now.minute;
		world_print ();
	}
}
static bool world_key (long k, bool ctrl)
{
	if (ctrl && k == UK_CTRL ('N')) { world_add_open (); return true; }
	if (ctrl && k == KEY_UP) { world_move (-1); return true; }
	if (ctrl && k == KEY_DOWN) { world_move (1); return true; }
	if (k == KEY_DEL) { world_remove (); return true; }
	if (!ctrl && (k == KEY_UP || k == KEY_DOWN || k == KEY_HOME || k == KEY_END || k == KEY_PGUP || k == KEY_PGDN))
	{ g_cities->setFocus (); return g_cities->onKey (k); }
	return false;
}
// View > Analogue Clock / Digital Clock (S3): here's face, kept in config.ini (face = analogue | digital)
static void world_face (bool analogue)
{
	tab_show (TAB_WORLD);
	if (g_cfg.analogue == analogue) return;
	g_cfg.analogue = analogue;
	say ("face %s", analogue ? "analogue" : "digital");
	g_here->place ();
	g_here->invalidate (true);
	if (!cfg_save ()) say ("config.ini not saved");
}
static void m_face_analogue () { world_face (true); }
static void m_face_digital () { world_face (false); }
static void world_view_menu (Menu &m)
{
	m.separator ();
	m.item (TR ("Analogue Clock"), 0, 0, m_face_analogue);
	m.item (TR ("Digital Clock"), 0, 0, m_face_digital);
}
static void m_city_add () { world_add_open (); }
static void m_city_remove () { world_remove (); }
static void m_city_up () { world_move (-1); }
static void m_city_down () { world_move (1); }
static void world_menu (Menu &m)
{
	m.menu (TR ("City"));
	m.item (TR ("Add City\xE2\x80\xA6"), "^N", 0, m_city_add);
	m.item (TR ("Remove City"), "Del", 0, m_city_remove);
	m.item (TR ("Move up"), "Ctrl+\xE2\x86\x91", 0, m_city_up);
	m.item (TR ("Move down"), "Ctrl+\xE2\x86\x93", 0, m_city_down);
}
