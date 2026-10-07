//
// timerview.h -- the Clock's Timer tab, Time's up, the timer handed to clockd (02 #27-30, 04 D17, D18, D20; 03 step 9,
// §3.6). One translation unit with main.cpp.
//
//   TimerRing    the time left as an arc shrinking from 12 o'clock (VPath::arc), the time big inside, "of 05:00",
//                "Ends at 12:37" (paused: the arc dimmed, "Paused"; idle: full, the time set)
//   the column   Duration: three Spins (hours, minutes, seconds; locked while it runs or is paused), five presets
//                (a click sets, two clicks within 0.4 s set and start), the start button (Start / Pause / Resume),
//                Reset (not while it runs) -- Space and R from the keyboard
//   TimesUpVeil  "Time's up": the sound looped, the window raised, a notification ("Timer -- 05:00 done", its click:
//                "clock timer"); Stop (Enter, Esc) back to the time set, +1 min (+) a fresh minute; unanswered two
//                minutes: the sound stops, the card goes
// The time comes from the ticks (clocktime.h's Timer), never from counted frames. The duration is config.ini's timer =.
// Closing with the timer running asks nothing (03 §10 G1): its end is handed to clockd ([timer] in alarms.txt, RELOAD),
// clockd rings it ("clock --ring timer": ring_timer () below, the block removed then); a Clock started while that
// [timer] is still to come takes it back (running again here, the block removed). A paused timer is kept in
// config.ini (timer_left, timer_of) and comes back paused.
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
static int  sound_play (const char *token, bool loop);	// (sounds.h, ring.h) -> 1 playing, 0 busy, -1 none
static void sound_stop ();
static bool ring_busy ();				// (ring.h) an alarm's ring card shows

#define TIMESUP_UNANSWERED	12000			// ticks: 2 minutes, as an alarm
#define TIMER_BLOCK_W		534			// the ring and the column, centred in the tab

static Timer g_tm;					// the timer (clocktime.h)
static bool g_tmUp;					// the Time's up card shows (the ring empty)

// A container that keeps its children together as one block of a given width, centred across it (the window resized)
class Centred : public Widget
{
public:
	int bw, ox;
	Centred (int x, int y, int w, int h, int blockW) : Widget (x, y, w, h), bw (blockW), ox ((w - blockW) / 2) {}
	int at (int x) const { return ox + x; }		// a child's place, from the block's left edge
	void layout () override
	{
		int nx = (width - bw) / 2;
		if (nx < 0) nx = 0;
		for (Widget *c = firstChild; c; c = c->nextSib) c->left += nx - ox;
		ox = nx;
		lytW = width; lytH = height;
		invalidate (true);
	}
	void onDraw () override { canvas.clear (C_BG); }
};

// "05:00" / "1:00:00" of a time in hundredths, rounded up to the second (as the timer shows it)
static void timer_text (long cs, char *o, int cap) { clk_fmt_timer ((cs + 99) / 100, o, cap); }
// The wall time (HH:MM) dt seconds from now
static void wall_after (long dt, char *o, int cap)
{
	long s = (long) g_now.h * 3600 + g_now.mi * 60 + g_now.s + dt;
	minute_hm (s >= 0 ? s / 60 : (s - 59) / 60, o, cap);
}

// ---- the ring ----------------------------------------------------------------------------------------------------------
class TimerRing : public Widget
{
public:
	long key;						// what was drawn (redrawn when it changes)
	TimerRing (int x, int y, int w, int h) : Widget (x, y, w, h), key (-1) {}
	// what the picture depends on now: the second shown, the arc's degree, the state
	long state_key () const
	{
		long left = g_tmUp ? 0 : timer_left_cs (g_tm, g_now.tick), tot = g_tm.total_cs;
		long deg = tot > 0 ? left * 720 / tot : 0;		// (half degrees)
		return ((timer_shown (g_tm, g_now.tick) * 721 + deg) * 4 + g_tm.state) * 2 + (g_tmUp ? 1 : 0);
	}
	void onDraw () override
	{
		canvas.clear (C_BG);
		key = state_key ();
		int cx = width / 2, cy = height / 2, r = width / 2 - 14, rw = 14;
		VPath tr; tr.arc (VV (cx), VV (cy), VV (r), 0, 360, VV (rw)); tr.fill (canvas, uk_tone (C_BG, 108));
		long left = g_tmUp ? 0 : timer_left_cs (g_tm, g_now.tick), tot = g_tm.total_cs;
		double deg = tot > 0 ? 360.0 * left / tot : 0;
		bool paused = g_tm.state == TM_PAUSED;
		unsigned arc = paused ? uk_mix (C_BG, C_ACCENT, 140) : C_ACCENT;
		if (deg >= 0.5)
		{
			int d = (int) lround (deg);
			VPath a; a.arc (VV (cx), VV (cy), VV (r), 90 - d, 90, VV (rw)); a.fill (canvas, arc);
			if (g_tm.state != TM_IDLE)
			{
				double t = (90 - d) * M_PI / 180;
				VPath k; k.circle (VV (cx + r * cos (t)), VV (cy - r * sin (t)), VV (4)); k.fill (canvas, 0xFFFFFF);
			}
		}
		char b[16], s[64], of[16];
		timer_text (left, b, sizeof b);
		unsigned dm = dim_on (C_BG);
		if (g_tm.state != TM_IDLE || g_tmUp)
		{
			timer_text (tot, of, sizeof of);
			snprintf (s, sizeof s, TR ("of %s"), of);
			uk_text_c (canvas, 0, cy - 50, width, 20, s, dm);
		}
		{ UkFaceScope sc (face (46)); uk_text_c (canvas, 0, cy - 30, width, 56, b, C_TEXT, 2); }
		if (paused) { UkFaceScope sc (face (14)); uk_text_c (canvas, 0, cy + 28, width, 22, TR ("Paused"), C_ACCENT, 2); }
		else if (g_tm.state == TM_RUNNING && !g_tmUp)
		{
			char hm[8];
			wall_after ((left + 99) / 100, hm, sizeof hm);
			snprintf (s, sizeof s, TR ("Ends at %s"), hm);
			int w = uk_tw (s) + 18, x = (width - w) / 2;
			draw_bell (canvas, x + 6, cy + 39, 13, dm);
			uk_text_l (canvas, x + 18, cy + 28, 22, s, dm);
		}
	}
};

// ---- Time's up ---------------------------------------------------------------------------------------------------------
static void tu_stop (Widget &);
static void tu_more (Widget &);
class TimesUpVeil : public Veil
{
public:
	long start;				// the tick the card came (unanswered: 2 minutes)
	char line[128];
	Button *more, *stop;
	TimesUpVeil () : Veil (380, 236, TR ("Timer")), start (0)
	{
		line[0] = 0;
		more = new Button (0, 0, 120, 36, "+1 min", tu_more);
		stop = new Button (0, 0, 120, 36, TR ("Stop"), tu_stop);
		int x = (cw - 2 * 120 - 12) / 2;
		place (more, x, ch - 76); place (stop, x + 132, ch - 76);
	}
	void drawCard () override
	{
		int y = cy + th ();
		unsigned d = dim_on (C_BG);
		draw_hourglass (canvas, width / 2, y + 36, 40, C_ACCENT);
		{ UkFaceScope sc (face (28)); uk_text_c (canvas, cx, y + 62, cw, 36, TR ("Time's up"), C_TEXT, 2); }
		uk_text_c (canvas, cx, y + 100, cw, 20, line, d);
		uk_text_c (canvas, cx, cy + ch - 28, cw, 20, TR ("Enter or Esc: stop \xC2\xB7 +: one more minute"), d);
	}
	bool onKey (long k) override
	{
		if (k == KEY_ENTER || k == 27) { tu_stop (*this); return true; }
		if (k == '+') { tu_more (*this); return true; }
		return false;
	}
	void onHidden () override;
};
static TimesUpVeil *g_tu;

// ---- the tab -------------------------------------------------------------------------------------------------------------
static const int PRESET_S[5] = { 60, 180, 300, 600, 900 };
static TimerRing *g_ring_w;
static Spin *g_tmSpin[3];
static ToolButton *g_tmPre[5], *g_tmGo, *g_tmReset;
static FootText *g_tmFoot;
static bool g_tmSetting;				// (the spins set by the program: their callbacks ignored)
static int g_preLast = -1; static long g_preTick;	// (a preset's double click)

void TimesUpVeil::onHidden () { g_page[TAB_TIMER]->setFocus (); }

// The buttons, the spins, the presets after a change of state or duration
static void timer_ui ()
{
	bool idle = g_tm.state == TM_IDLE, run = g_tm.state == TM_RUNNING;
	long set_s = g_tm.state == TM_IDLE ? g_tm.total_cs / 100 : g_cfg.timer;
	g_tmSetting = true;
	g_tmSpin[0]->setValue ((int) (set_s / 3600)); g_tmSpin[1]->setValue ((int) (set_s / 60 % 60)); g_tmSpin[2]->setValue ((int) (set_s % 60));
	g_tmSetting = false;
	for (int i = 0; i < 3; i++) { g_tmSpin[i]->disabled = !idle; g_tmSpin[i]->invalidate (true); }
	for (int i = 0; i < 5; i++) g_tmPre[i]->setOn (g_cfg.timer == PRESET_S[i]);
	if (run) { g_tmGo->setGlyph (WKT_PAUSE); g_tmGo->setText (TR ("Pause")); }
	else { g_tmGo->setGlyph (WKT_PLAY); g_tmGo->setText (g_tm.state == TM_PAUSED ? TR ("Resume") : TR ("Start")); }
	g_tmGo->setOn (true);
	g_tmGo->setDisabled (idle && g_tm.total_cs <= 0);
	g_tmReset->setDisabled (run || (idle && g_tm.total_cs == (long) g_cfg.timer * 100));
	g_tmGo->invalidate (true);
	g_ring_w->invalidate (true);
}
// The duration chosen (spins, presets): idle at it, kept for next time (config.ini's timer =)
static void timer_duration (long s)
{
	if (s < 0) s = 0;
	if (s > 86399) s = 86399;
	if (s > 0) g_cfg.timer = (int) s;
	timer_set (g_tm, s);
	timer_ui ();
}
static void tm_spin (Widget &)
{
	if (g_tmSetting || g_tm.state != TM_IDLE) return;
	timer_duration ((long) g_tmSpin[0]->value * 3600 + g_tmSpin[1]->value * 60 + g_tmSpin[2]->value);
}
static void timer_commit_spins () { for (int i = 0; i < 3; i++) g_tmSpin[i]->NumericUpDown::onKey (KEY_ENTER); }	// (typed digits taken)

static void timer_go ()					// Start / Pause / Resume (Space)
{
	char t[16];
	if (g_tm.state == TM_RUNNING)
	{
		timer_pause (g_tm, g_now.tick);
		timer_text (timer_left_cs (g_tm, g_now.tick), t, sizeof t);
		say ("timer paused %s", t);
	}
	else
	{
		if (g_tm.state == TM_IDLE) timer_commit_spins ();
		if (g_tm.total_cs <= 0) { timer_ui (); return; }
		bool resumed = g_tm.state == TM_PAUSED;
		timer_start (g_tm, g_now.tick);
		timer_text (timer_left_cs (g_tm, g_now.tick), t, sizeof t);
		say ("timer %s %s (the duration locked)", resumed ? "resumed" : "started", t);
	}
	timer_ui ();
}
static void timer_do_reset ()				// Reset (R): not while it runs
{
	if (g_tm.state == TM_RUNNING) { say ("timer reset refused (running)"); return; }
	timer_set (g_tm, g_cfg.timer);
	char t[16]; timer_text (g_tm.total_cs, t, sizeof t);
	say ("timer reset %s", t);
	timer_ui ();
}
static void timer_preset (int i, bool start)
{
	if (g_tm.state != TM_IDLE) { say ("preset refused (the timer runs)"); timer_ui (); return; }
	timer_duration (PRESET_S[i]);
	char t[16]; timer_text (g_tm.total_cs, t, sizeof t);
	say ("timer set %s", t);
	if (start) timer_go ();
}
static void tb_go (Widget &) { timer_go (); }
static void tb_reset (Widget &) { timer_do_reset (); }
static void tb_preset (Widget &w)
{
	int i = 0;
	while (i < 4 && g_tmPre[i] != &w) i++;
	bool twice = g_preLast == i && g_now.tick - g_preTick <= 40;	// (two clicks within 0.4 s: set and start)
	g_preLast = twice ? -1 : i; g_preTick = g_now.tick;
	timer_preset (i, twice);
}

// It rings: the card, the sound, the notification, the window raised (the timer of this Clock, or clockd's)
static void times_up (long set_s, long end_tick)
{
	if (!g_tu) g_tu = new TimesUpVeil ();
	char t[16], hm[8], w[96];
	clk_fmt_timer (set_s, t, sizeof t);
	wall_after (-(g_now.tick - end_tick) / 100, hm, sizeof hm);
	snprintf (g_tu->line, sizeof g_tu->line, TR ("The %s timer ended at %s"), t, hm);
	snprintf (w, sizeof w, TR ("Timer \xE2\x80\x94 %s done"), t);
	notify_action (TR ("Clock"), w, "clock timer");
	say ("time's up (%s, ended at %s)", t, hm);
	g_tmUp = true;
	timer_set (g_tm, set_s);			// (the ring empty under the card; Stop: back to the time set)
	g_tu->start = g_now.tick;
	bool editing = false;				// (a card already shows -- the editor, a city picker: left as it is, R-8)
	for (Widget *c = g_root->firstChild; c; c = c->nextSib) editing |= c->modal && c != g_tu;
	if (!editing) tab_show (TAB_TIMER);
	sound_play ("chimes", true);
	timer_ui ();
	g_tu->show (g_root, g_tu->stop);
	kapi_raise_app (CLOCK_SERVICE);
}
static void tu_end ()
{
	if (!ring_busy ()) sound_stop ();
	g_tmUp = false;
	g_tu->hide ();
}
static void tu_stop (Widget &)
{
	say ("time's up stopped");
	tu_end ();
	timer_set (g_tm, g_cfg.timer);			// back to the time set
	timer_ui ();
	if (g_ringOnly) g_closeNow = true;
}
static void tu_more (Widget &)
{
	tu_end ();
	g_ringOnly = false;				// (a timer runs now: the window stays -- validation 3 note 4)
	timer_set (g_tm, 60);
	timer_start (g_tm, g_now.tick);
	say ("timer started 01:00 (one more minute)");
	timer_ui ();
}

// ---- the hooks main.cpp calls ----------------------------------------------------------------------------------------------
static void timer_build (Page *p)
{
	int w = p->width, h = p->height;
	timer_set (g_tm, g_cfg.timer);
	{
		long left = atol (cfg_get ("timer_left", "0")), of = atol (cfg_get ("timer_of", "0"));	// (a paused timer kept)
		if (left > 0 && of >= left && of <= 8640000) { g_tm.state = TM_PAUSED; g_tm.total_cs = of; g_tm.left_cs = left; }
	}
	Centred *c = new Centred (0, 0, w, h - FOOT, TIMER_BLOCK_W);
	c->anchor = ANCHOR_FILL;
	p->addChild (c);
	g_ring_w = new TimerRing (c->at (0), 8, 262, 262);
	c->addChild (g_ring_w);
	int x0 = 282, cw = TIMER_BLOCK_W - x0;
	c->addChild (new Caption (c->at (x0), 16, cw, 22, TR ("Duration"), true));
	static const char *const UNIT[3] = { TRN ("hours"), TRN ("minutes"), TRN ("seconds") };
	static const int HI[3] = { 23, 59, 59 };
	int bw = (cw - 2 * 10) / 3;
	for (int i = 0; i < 3; i++)
	{
		g_tmSpin[i] = new Spin (c->at (x0 + i * (bw + 10)), 42, bw, 30, 0, HI[i], 0, tm_spin);
		c->addChild (g_tmSpin[i]);
		c->addChild (new Caption (c->at (x0 + i * (bw + 10)), 74, bw, 18, TR (UNIT[i]), false, true));
	}
	c->addChild (new Caption (c->at (x0), 104, cw, 22, TR ("Presets"), true));
	static const char *const PRE[5] = { "1 min", "3 min", "5 min", "10 min", "15 min" };
	int pw = (cw - 4 * 5) / 5;
	for (int i = 0; i < 5; i++)
	{
		g_tmPre[i] = tool_button (TR ("Set (a double click: set and start)"), WKT_NONE, PRE[i], tb_preset, pw, 30);
		g_tmPre[i]->setToggle (true, false);
		g_tmPre[i]->left = c->at (x0 + i * (pw + 5)); g_tmPre[i]->top = 130;
		c->addChild (g_tmPre[i]);
	}
	g_tmGo = tool_button (TR ("Start / Pause (Space)"), WKT_PLAY, TR ("Start"), tb_go, cw, 42);
	g_tmGo->filled = true; g_tmGo->setToggle (true, true);
	g_tmGo->left = c->at (x0); g_tmGo->top = 186;
	c->addChild (g_tmGo);
	g_tmReset = tool_button (TR ("Reset (R)"), WKT_TO_START, TR ("Reset"), tb_reset, cw, 36);
	g_tmReset->left = c->at (x0); g_tmReset->top = 238;
	c->addChild (g_tmReset);
	g_tmFoot = new FootText (14, h - FOOT + 8, w - 28, 30);
	g_tmFoot->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	g_tmFoot->set (TR ("Space: start / pause \xC2\xB7 R: reset \xC2\xB7 it rings even with the Clock closed"));
	p->addChild (g_tmFoot);
	timer_ui ();
	if (g_tm.state == TM_PAUSED) { char t[16]; timer_text (g_tm.left_cs, t, sizeof t); say ("timer paused %s (kept)", t); }
}
static void timer_shown () { g_page[TAB_TIMER]->setFocus (); timer_ui (); }
static void timer_tick ()
{
	if (g_tm.state == TM_RUNNING && timer_due (g_tm, g_now.tick) && !g_tmUp)
	{
		long set_s = g_tm.total_cs / 100, end = timer_end_tick (g_tm);
		g_tm.state = TM_IDLE;
		times_up (set_s, end);
	}
	if (g_tmUp && g_now.tick - g_tu->start >= TIMESUP_UNANSWERED)	// unanswered: as an alarm (the sound stops, the card goes)
	{
		say ("time's up unanswered");
		tu_end ();
		timer_set (g_tm, g_cfg.timer);
		timer_ui ();
		if (g_ringOnly) g_closeNow = true;
	}
	if (g_tab == TAB_TIMER && g_ring_w->state_key () != g_ring_w->key) g_ring_w->invalidate (true);
}
static bool timer_key (long k, bool ctrl)
{
	if (ctrl) return false;
	if (k == ' ') { timer_go (); return true; }
	if (k == 'r' || k == 'R') { timer_do_reset (); return true; }
	return false;
}
static void m_tm_go () { tab_show (TAB_TIMER); timer_go (); }
static void m_tm_reset () { tab_show (TAB_TIMER); timer_do_reset (); }
static void m_tm_1 () { tab_show (TAB_TIMER); timer_preset (0, false); }
static void m_tm_3 () { tab_show (TAB_TIMER); timer_preset (1, false); }
static void m_tm_5 () { tab_show (TAB_TIMER); timer_preset (2, false); }
static void m_tm_10 () { tab_show (TAB_TIMER); timer_preset (3, false); }
static void m_tm_15 () { tab_show (TAB_TIMER); timer_preset (4, false); }
static void timer_menu (Menu &m)
{
	static char mins[4][32];
	static const int MN[4] = { 3, 5, 10, 15 };
	for (int i = 0; i < 4; i++) snprintf (mins[i], sizeof mins[i], TR ("%d minutes"), MN[i]);
	m.menu (TR ("Timer"));
	m.item (TR ("Start / Pause"), TR ("Space"), 0, m_tm_go);
	m.item (TR ("Reset"), "R", 0, m_tm_reset);
	m.separator ();
	m.item (TR ("1 minute"), 0, 0, m_tm_1);
	m.item (mins[0], 0, 0, m_tm_3);
	m.item (mins[1], 0, 0, m_tm_5);
	m.item (mins[2], 0, 0, m_tm_10);
	m.item (mins[3], 0, 0, m_tm_15);
}
// At the start: a [timer] handed to clockd at the last close and still to come is taken back -- running here, the block
// removed, clockd told (it rings only once, never twice: 03 §3.6). One already past is left to clockd (it rings it once:
// "clock --ring timer"); one of an earlier boot is dropped at the next save.
static void timer_resume_handed ()
{
	int st = alarms_timer_state (g_al, g_now.tick, g_now.real ? g_now.utc : -1);
	if (st != TMR_PENDING) return;
	timer_resume (g_tm, g_al.timer_set, g_al.timer_end_tick, g_now.tick);
	alarms_timer_clear (g_al);
	alarms_write ();
	char t[16]; timer_text (timer_left_cs (g_tm, g_now.tick), t, sizeof t);
	say ("timer running %s", t);
	timer_ui ();
	tab_show (TAB_TIMER);
}
// At the end: a running timer handed to clockd ([timer]: its end in ticks, its time set, its end in UTC when the real
// time is known), a paused one kept in config.ini.
static void timer_at_exit ()
{
	cfg_set_int ("timer_left", g_tm.state == TM_PAUSED ? g_tm.left_cs : 0);
	cfg_set_int ("timer_of", g_tm.state == TM_PAUSED ? g_tm.total_cs : 0);
	if (g_tm.state != TM_RUNNING) return;
	long end = timer_end_tick (g_tm);
	long long utc = -1;
	struct kapi_clock_info ci;
	if (kapi_clock_info (&ci) == 0 && (ci.flags & KAPI_CLOCK_REALTIME_VALID))
		utc = (long long) (ci.utc_us / 1000000) + (end - (long) kapi_get_ticks ()) / 100;
	alarms_timer_put (g_al, end, (int) (g_tm.total_cs / 100), utc, "");
	char t[16]; timer_text (timer_left_cs (g_tm, g_now.tick), t, sizeof t);
	say ("timer handed to clockd (%s left)", t);
	alarms_write ();
}
// "clock --ring timer" (clockd: the [timer] handed over has ended): Time's up with its time set; the block removed,
// clockd told -- nothing left for it to ring again (03 §3.6, validation 2 gap 1).
static void ring_timer ()
{
	alarms_load (g_al, ALARMS_PATH);
	g_alPrint = true;
	if (!g_al.timer)
	{
		say ("ring timer: no timer");
		alarms_changed ();
		if (g_ringOnly && !(g_tu && g_tu->shown ())) g_closeNow = true;
		return;
	}
	long set_s = g_al.timer_set > 0 ? g_al.timer_set : g_cfg.timer, end = g_al.timer_end_tick;
	alarms_timer_clear (g_al);
	alarms_write ();
	alarms_changed ();
	if (g_tm.state != TM_IDLE) say ("ring timer: the running timer kept");
	else times_up (set_s, end);
}
