//
// swview.h -- the Clock's Stopwatch tab (02 #31-33, 04 D19; 03 step 10). One translation unit with main.cpp.
//
//   StopwatchFace  the time big, MM:SS and the hundredths smaller on one baseline (H:MM:SS from an hour), refreshed
//                  every turn while it runs; under it the lap running ("Lap 4 . 00:04.45"), at zero the keys
//   the buttons    Lap (L) while it runs / Reset (R) when stopped (not at zero); Start / Stop (Space)
//   the laps       a DataGrid, newest first: the lap, its time, the total; from 3 laps the fastest lap time in green
//                  with "Fastest" and an up arrow, the slowest in red with "Slowest" and a down arrow (not colour alone)
//   the footer     "3 laps", Copy Laps (Ctrl+C): the laps as tab-separated text on the clipboard (SystemKit), the
//                  header in the system's language, then "Laps copied"
// The time comes from the ticks (clocktime.h's Stopwatch). Closing never loses it (03 R-1, §10 G1): its start (ticks),
// the time before it and the laps are kept in config.ini (sw_*) and it goes on at the next start -- across a reboot
// too when the real time was known both times (sw_utc), else it comes back stopped at the time it had.
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
static Stopwatch g_sw;					// (~8 KB: static)

// The real time now, UTC seconds, when the kernel knows it (else -1): what tells a reboot apart
static long long utc_known ()
{
	struct kapi_clock_info ci;
	if (kapi_clock_info (&ci) == 0 && (ci.flags & KAPI_CLOCK_REALTIME_VALID)) return (long long) (ci.utc_us / 1000000);
	return -1;
}

// ---- the face ----------------------------------------------------------------------------------------------------------
class StopwatchFace : public Widget
{
public:
	long shown;					// the hundredths drawn (-1: to draw)
	StopwatchFace (int x, int y, int w, int h) : Widget (x, y, w, h), shown (-1) {}
	void onDraw () override
	{
		canvas.clear (C_BG);
		long cs = sw_total_cs (g_sw, g_now.tick);
		shown = cs;
		char all[24], a[24], b[8], s[96], l[24];
		clk_fmt_sw (cs, all, sizeof all);		// "00:41.07", "1:00:00.00": the big part and ".07"
		char *dot = strrchr (all, '.');
		snprintf (b, sizeof b, "%s", dot ? dot : "");
		if (dot) *dot = 0;
		snprintf (a, sizeof a, "%s", all);
		int wa, wb, asc_a = 48, asc_b = 29;
		TextFace *fa = face (60), *fb = face (36);
		{ UkFaceScope sc (fa); wa = uk_tw (a, 2); }
		{ UkFaceScope sc (fb); wb = uk_tw (b, 2); }
		if (fa) asc_a = fa->ascent ();
		if (fb) asc_b = fb->ascent ();
		int x = (width - wa - wb) / 2, base = 8 + asc_a;
		{ UkFaceScope sc (fa); uk_text (canvas, x, base - asc_a, a, C_TEXT, 2); }
		{ UkFaceScope sc (fb); uk_text (canvas, x + wa, base - asc_b, b, uk_mix (C_BG, C_TEXT, 190), 2); }
		if (g_sw.nlaps)
		{
			clk_fmt_sw (sw_running_lap_cs (g_sw, g_now.tick), l, sizeof l);
			snprintf (s, sizeof s, TR ("Lap %d"), g_sw.nlaps + 1);
			cat (s, sizeof s, DOT);
			cat (s, sizeof s, l);
		}
		else snprintf (s, sizeof s, "%s", cs ? "" : TR ("Space: start \xC2\xB7 L: lap \xC2\xB7 R: reset"));
		{ UkFaceScope sc (face (14)); uk_text_c (canvas, 0, height - 24, width, 22, s, dim_on (C_BG)); }
	}
};

// ---- the laps' grid: newest first, the fastest and the slowest marked ---------------------------------------------------
static const char *lap_cell (DataGrid &, int row, int col, char *buf, int cap)
{
	int i = g_sw.nlaps - 1 - row;
	if (i < 0) return "";
	if (col == 0) { snprintf (buf, cap, "%d", i + 1); return buf; }
	if (col == 1) { clk_fmt_sw (sw_lap_time (g_sw, i), buf, cap); return buf; }
	if (col == 2) { clk_fmt_sw (g_sw.laps[i], buf, cap); return buf; }
	return "";
}
static bool lap_draw (DataGrid &, Canvas &cv, int row, int col, int x, int y, int w, int h, unsigned, bool)
{
	int i = g_sw.nlaps - 1 - row;
	bool f = i >= 0 && i == sw_fastest (g_sw), s = i >= 0 && i == sw_slowest (g_sw);
	if (!f && !s) return false;
	unsigned c = f ? GREEN () : RED ();
	if (col == 1)
	{
		char b[24]; clk_fmt_sw (sw_lap_time (g_sw, i), b, sizeof b);
		uk_text (cv, x + w - 10 - uk_tw (b, 2), y + (h - uk_fh ()) / 2, b, c, 2);
		return true;
	}
	if (col != 3) return false;
	const char *t = f ? TR ("Fastest") : TR ("Slowest");
	unsigned bg = uk_mix (C_FIELD, c, 40);
	int tw = uk_tw (t, 2) + 26, py = y + 3, ph = h - 6;
	uk_rbox (cv, x + 8, py, tw, ph, ph / 2, bg, bg);
	uk_glyph (cv, f ? WKG_UP : WKG_DOWN, x + 18, py + ph / 2, 8, c);
	uk_text_l (cv, x + 26, py, ph, t, c, 2);
	return true;
}

// ---- the tab -------------------------------------------------------------------------------------------------------------
static StopwatchFace *g_swFace;
static ToolButton *g_swLap, *g_swGo, *g_swCopy;
static DataGrid *g_swGrid;
static FootText *g_swFoot;

// Lap / Reset, Start / Stop, Copy Laps, the footer, the grid after a change
static void sw_ui ()
{
	bool run = g_sw.running, zero = !run && sw_total_cs (g_sw, g_now.tick) == 0 && g_sw.nlaps == 0;
	if (run) { g_swLap->setGlyph (WKT_NONE); g_swLap->setText (TR ("Lap")); g_swLap->tip = TR ("Lap (L)"); }
	else { g_swLap->setGlyph (WKT_TO_START); g_swLap->setText (TR ("Reset")); g_swLap->tip = TR ("Reset (R)"); }
	g_swLap->setDisabled (zero || (run && g_sw.nlaps >= SW_MAX_LAPS));
	g_swLap->invalidate (true);
	g_swGo->setGlyph (run ? WKT_STOP : WKT_PLAY);
	g_swGo->setText (run ? TR ("Stop") : TR ("Start"));
	g_swGo->setOn (true);
	g_swGo->invalidate (true);
	g_swCopy->setDisabled (g_sw.nlaps == 0);
	char s[64];
	if (g_sw.nlaps == 0) snprintf (s, sizeof s, "%s", TR ("No laps"));
	else if (g_sw.nlaps == 1) snprintf (s, sizeof s, "%s", TR ("1 lap"));
	else snprintf (s, sizeof s, TR ("%d laps"), g_sw.nlaps);
	g_swFoot->set (s);
	g_swGrid->setRows (g_sw.nlaps);
	g_swGrid->invalidate (true);
	g_swFace->invalidate (true);
}
// The rows as shown (newest first, the marks), printed for the PC tests -- the first ten
static void sw_print ()
{
	char t[24], tot[24];
	for (int r = 0; r < g_sw.nlaps && r < 10; r++)
	{
		int i = g_sw.nlaps - 1 - r;
		clk_fmt_sw (sw_lap_time (g_sw, i), t, sizeof t);
		clk_fmt_sw (g_sw.laps[i], tot, sizeof tot);
		say ("lap %d %s %s%s", i + 1, t, tot, i == sw_fastest (g_sw) ? " fastest" : i == sw_slowest (g_sw) ? " slowest" : "");
	}
}
static void sw_go ()					// Start / Stop (Space)
{
	char t[24];
	g_swFoot->say ("");
	if (g_sw.running)
	{
		sw_stop (g_sw, g_now.tick);
		clk_fmt_sw (sw_total_cs (g_sw, g_now.tick), t, sizeof t);
		say ("stopwatch stopped %s (%d laps)", t, g_sw.nlaps);
		sw_print ();
	}
	else
	{
		sw_start (g_sw, g_now.tick);
		clk_fmt_sw (sw_total_cs (g_sw, g_now.tick), t, sizeof t);
		say ("stopwatch started %s", t);
	}
	sw_ui ();
}
static void sw_do_lap ()				// Lap (L): while it runs
{
	if (!g_sw.running) return;
	int n = sw_lap (g_sw, g_now.tick);
	if (!n) return;
	char t[24], tot[24];
	clk_fmt_sw (sw_lap_time (g_sw, n - 1), t, sizeof t);
	clk_fmt_sw (g_sw.laps[n - 1], tot, sizeof tot);
	say ("lap %d %s %s", n, t, tot);
	g_swFoot->say ("");
	sw_ui ();
	g_swGrid->ensureVisible (0);
}
static void sw_do_reset ()				// Reset (R): when stopped
{
	if (g_sw.running) { say ("stopwatch reset refused (running)"); return; }
	sw_reset (g_sw);
	say ("stopwatch reset");
	g_swFoot->say ("");
	sw_ui ();
}
// Copy Laps (Ctrl+C): the header line in the system's language, then a lap a line, tab-separated, oldest first
static void sw_copy ()
{
	if (g_tab != TAB_SW || g_sw.nlaps == 0) return;
	int cap = 64 + (g_sw.nlaps + 1) * 48;
	char *t = new char[cap];
	int n = sw_laps_text (g_sw, TR ("Lap"), TR ("Lap time"), TR ("Total"), t, cap);
	clip_set_text_n (t, n);
	delete [] t;
	say ("laps copied (%d)", g_sw.nlaps);
	g_swFoot->say (TR ("Laps copied"));
}
static void tb_sw_lap (Widget &) { if (g_sw.running) sw_do_lap (); else sw_do_reset (); }
static void tb_sw_go (Widget &) { sw_go (); }
static void tb_sw_copy (Widget &) { sw_copy (); }

// The stopwatch of last time (config.ini's sw_*): going on, from the ticks in the same boot, from the real time across
// a reboot when it was known both times; else stopped at the time it had at the close.
static void sw_restore ()
{
	sw_reset (g_sw);
	const char *lp = cfg_get ("sw_laps", "");
	while (*lp && g_sw.nlaps < SW_MAX_LAPS)
	{
		long v = atol (lp);
		if (v <= 0 || (g_sw.nlaps && v < g_sw.laps[g_sw.nlaps - 1])) break;	// (not a run of totals: the rest dropped)
		g_sw.laps[g_sw.nlaps++] = v;
		while (*lp && *lp != ',') lp++;
		while (*lp == ',' || *lp == ' ') lp++;
	}
	long base = atol (cfg_get ("sw_base", "0")), total = atol (cfg_get ("sw_total", "0"));
	long start = atol (cfg_get ("sw_start", "0")), saved = atol (cfg_get ("sw_tick", "0"));
	long long sutc = atoll (cfg_get ("sw_utc", "-1")), nutc = utc_known ();
	bool run = atoi (cfg_get ("sw_run", "0")) == 1;
	if (base < 0) base = 0;
	if (total < base) total = base;
	if (g_sw.nlaps && g_sw.laps[g_sw.nlaps - 1] > total) total = g_sw.laps[g_sw.nlaps - 1];
	if (!run) { g_sw.base_cs = total; return; }
	// the same boot: the ticks went on since the close (and the real time agrees, when known both times)
	bool same = g_now.tick >= saved && start <= saved;
	if (same && sutc >= 0 && nutc >= 0)
	{
		long long d = (nutc - sutc) - (g_now.tick - saved) / 100;
		same = d >= -5 && d <= 5;
	}
	if (same) { g_sw.running = true; g_sw.start_tick = start; g_sw.base_cs = base; }
	else if (sutc >= 0 && nutc >= sutc)			// another boot, the real time known: it went on meanwhile
	{
		g_sw.running = true; g_sw.start_tick = g_now.tick;
		g_sw.base_cs = total + (long) ((nutc - sutc) * 100);
	}
	else g_sw.base_cs = total;				// (nothing tells how long: stopped at the time it had)
}

// ---- the hooks main.cpp calls ----------------------------------------------------------------------------------------------
static void sw_build (Page *p)
{
	int w = p->width, h = p->height;
	sw_restore ();
	g_swFace = new StopwatchFace (0, 4, w, 104);
	g_swFace->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_TOP;
	p->addChild (g_swFace);
	int bw = 164, gap = 14;
	Centred *c = new Centred (0, 112, w, 38, 2 * bw + gap);
	c->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_TOP;
	p->addChild (c);
	g_swLap = tool_button (TR ("Lap (L)"), WKT_NONE, TR ("Lap"), tb_sw_lap, bw, 38);
	g_swLap->left = c->at (0); g_swLap->top = 0;
	c->addChild (g_swLap);
	g_swGo = tool_button (TR ("Start / Stop (Space)"), WKT_PLAY, TR ("Start"), tb_sw_go, bw, 38);
	g_swGo->filled = true; g_swGo->setToggle (true, true);
	g_swGo->left = c->at (bw + gap); g_swGo->top = 0;
	c->addChild (g_swGo);
	g_swGrid = new DataGrid (12, 162, w - 24, h - FOOT - 162);
	g_swGrid->anchor = ANCHOR_FILL;
	g_swGrid->setColumns (4);
	g_swGrid->setColumn (0, TR ("Lap"), 70);
	g_swGrid->setColumn (1, TR ("Lap time"), 130, GRID_RIGHT);
	g_swGrid->setColumn (2, TR ("Total"), 130, GRID_RIGHT);
	g_swGrid->setColumn (3, "", w - 24 - 2 - 330 - 12);
	g_swGrid->cellText = lap_cell; g_swGrid->cellDraw = lap_draw; g_swGrid->stripes = true;
	g_swGrid->emptyText = TR ("No laps yet: Lap (L) while it runs");
	p->addChild (g_swGrid);
	g_swFoot = new FootText (14, h - FOOT + 8, w - 200, 30);
	g_swFoot->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	p->addChild (g_swFoot);
	g_swCopy = tool_button (TR ("Copy the laps as text (Ctrl+C)"), WKT_COPY, TR ("Copy Laps"), tb_sw_copy);
	g_swCopy->left = w - 12 - g_swCopy->width; g_swCopy->top = h - FOOT + 8;
	g_swCopy->anchor = ANCHOR_RIGHT | ANCHOR_BOTTOM;
	p->addChild (g_swCopy);
	sw_ui ();
	if (g_sw.running || g_sw.base_cs || g_sw.nlaps)
	{
		char t[24]; clk_fmt_sw (sw_total_cs (g_sw, g_now.tick), t, sizeof t);
		say ("stopwatch kept %s%s (%d laps)", t, g_sw.running ? " running" : "", g_sw.nlaps);
	}
}
static void sw_shown () { g_page[TAB_SW]->setFocus (); sw_ui (); }
static void sw_tick ()
{
	if (g_tab != TAB_SW) return;
	if (g_swGrid->width != g_swGrid->column (3).width + 330 + 14)		// (the window resized: the last column fills)
	{
		int w3 = g_swGrid->width - 2 - 330 - 12;
		g_swGrid->column (3).width = w3 < 40 ? 40 : w3;
		g_swGrid->invalidate (true);
	}
	if (sw_total_cs (g_sw, g_now.tick) != g_swFace->shown) g_swFace->invalidate (true);
}
static bool sw_key (long k, bool ctrl)
{
	if (ctrl) return false;
	if (k == ' ') { sw_go (); return true; }
	if (k == 'l' || k == 'L') { sw_do_lap (); return true; }
	if (k == 'r' || k == 'R') { sw_do_reset (); return true; }
	return false;
}
static void m_sw_go () { tab_show (TAB_SW); sw_go (); }
static void m_sw_lap () { tab_show (TAB_SW); sw_do_lap (); }
static void m_sw_reset () { tab_show (TAB_SW); sw_do_reset (); }
static void m_sw_copy () { sw_copy (); }
static void sw_menu (Menu &m)
{
	m.menu (TR ("Stopwatch"));
	m.item (TR ("Start / Stop"), TR ("Space"), 0, m_sw_go);
	m.item (TR ("Lap"), "L", 0, m_sw_lap);
	m.item (TR ("Reset"), "R", 0, m_sw_reset);
	m.separator ();
	m.item (TR ("Copy Laps"), "^C", UK_CTRL ('C'), m_sw_copy);	// (the only key bound in a menu: 04 §6)
}
// At the end: the stopwatch kept (config.ini, written by cfg_save): running or not, its laps
static void sw_at_exit ()
{
	char t[24];
	cfg_set_int ("sw_run", g_sw.running ? 1 : 0);
	cfg_set_int ("sw_start", g_sw.running ? g_sw.start_tick : 0);
	cfg_set_int ("sw_base", g_sw.base_cs);
	cfg_set_int ("sw_total", sw_total_cs (g_sw, g_now.tick));
	cfg_set_int ("sw_tick", g_now.tick);
	long long u = utc_known ();
	snprintf (t, sizeof t, "%lld", u);
	cfg_set ("sw_utc", t);
	int cap = g_sw.nlaps * 12 + 1;
	char *l = new char[cap];
	l[0] = 0;
	for (int i = 0; i < g_sw.nlaps; i++) { snprintf (t, sizeof t, i ? ",%ld" : "%ld", g_sw.laps[i]); cat (l, cap, t); }
	cfg_set ("sw_laps", l);
	delete [] l;
	if (g_sw.running || g_sw.nlaps || g_sw.base_cs)
	{
		clk_fmt_sw (sw_total_cs (g_sw, g_now.tick), t, sizeof t);
		say ("stopwatch kept at the close %s%s", t, g_sw.running ? " running" : "");
	}
}
