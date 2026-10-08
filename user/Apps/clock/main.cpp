//
// Apps/clock/main.cpp -- Clock (AutoDev round 6): the time here and around the world, alarms that ring even when the
// app is closed (clockd, user/Apps/clockd), a kitchen timer and a stopwatch -- in English and French. The plan:
// autodev/rounds/06-clock/ (02 the product, 03 the technical plan and its §10 GUI plan, 04 the design and its mock-ups).
//
// One window (560 x 440 at least, resizable), four tabs -- World, Alarms, Timer, Stopwatch -- under a SegmentedControl;
// each tab a Page of widgets shown or hidden (world.h, alarmsview.h, timerview.h, swview.h); the dialogs are overlay
// cards (ui.h's Veil: the window's loop and its onTick go on under them). The model is the core beside the app, shared
// with clockd: alarms.{h,cpp} (SD:/apps/clock.app/alarms.txt -- the Clock is its only writer), clocktime.{h,cpp} (the
// wall time, the world rows, the timer and the stopwatch from the ticks).
//
//   clock                          the tab of last time
//   clock world|alarms|timer|stopwatch
//   clock --ring <id>              alarm <id> rings (started by clockd): the window on the Alarms tab, the ring card,
//                                  the sound, a notification; started only for it, the Clock closes after the answer
//   clock --ring timer             the timer handed to clockd has ended: the Time's up card
//   clock --missed <id> <YYYYMMDDHHMM> [...]   (clockd at its start) once alarms missed while the Pi was off: each
//                                  "Missed alarm: 07:00 School" notified and written on its row -- no sound, no window
// One Clock at a time: a second one sends the running one its arguments (CLOCK_MSG_OPEN), raises it and ends.
// SD:/apps/clock.app/config.ini ([clock]: tab, cities, snooze, timer, face, width, height; a paused timer: timer_left,
// timer_of; the stopwatch: sw_run, sw_start, sw_base, sw_total, sw_tick, sw_utc, sw_laps): AppKit's .ini, through FileKit.
// Keys (04 §7): Ctrl+1..4 and Ctrl+Tab the tabs; Ctrl+N a city / an alarm; Space toggles the alarm, starts / pauses
// the timer, starts / stops the stopwatch; R resets them (not while they run); L a lap; Ctrl+C copies the laps; in a
// card Enter / Esc its default / Cancel, + one more minute on Time's up. Closing asks nothing (03 §10 G1): Ctrl+Q,
// Clock > Quit and the close box all hand over -- a running timer to clockd, the stopwatch kept in config.ini.
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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include "fontkit/uikitface.h"
#include "systemkit/systemkit.h"
#include "filekit/filekit.h"
#include "audiokit/audiokit.h"
#include "alarms.h"
#include "clocktime.h"
#include "clock_proto.h"
#include "ui.h"

#define CONFIG_PATH	"SD:/apps/clock.app/config.ini"
#define CITIES_MAX	12

// ---- what the program prints (the PC tests read it; on Onyx: the console) --------------------------------------------
static void say (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
static void say (const char *fmt, ...)
{
	va_list ap; va_start (ap, fmt);
	printf ("clock: "); vprintf (fmt, ap); printf ("\n");
	va_end (ap);
	fflush (stdout);
}

// ---- the words of the dates (in the system's language where shown; the stored tokens stay English) -------------------
static const char *const DAYNAME[7] = { TRN ("Monday"), TRN ("Tuesday"), TRN ("Wednesday"), TRN ("Thursday"), TRN ("Friday"), TRN ("Saturday"), TRN ("Sunday") };
static const char *const MONTHNAME[12] = { TRN ("January"), TRN ("February"), TRN ("March"), TRN ("April"), TRN ("May"), TRN ("June"), TRN ("July"),
					   TRN ("August"), TRN ("September"), TRN ("October"), TRN ("November"), TRN ("December") };
static const char *const SHORTDAY[7] = { TRN ("Mon"), TRN ("Tue"), TRN ("Wed"), TRN ("Thu"), TRN ("Fri"), TRN ("Sat"), TRN ("Sun") };
enum { TAB_WORLD, TAB_ALARMS, TAB_TIMER, TAB_SW, TABS };
static const char *const TABKEY[TABS] = { "world", "alarms", "timer", "stopwatch" };	// (config.ini, the arguments)
static const char *const TABNAME[TABS] = { TRN ("World"), TRN ("Alarms"), TRN ("Timer"), TRN ("Stopwatch") };

// ---- now (read once a turn of the loop) --------------------------------------------------------------------------------
struct Now
{
	bool real;		// kapi_get_datetime: a real date (else the time since the boot)
	int  y, mo, d, h, mi, s;
	long day, minute;	// the wall day, the wall minute (clocktime.h)
	long long utc;		// UTC, seconds since 1970
	int  here_off;		// here's offset, minutes: kapi_clock_info's tz_minutes, else system.ini's timezone=
	int  zone;		// SystemKit's locale_zone () (-1: none chosen) -- read again every minute
	bool zone_set;		// a zone or a timezone= (else "Time zone not set")
	long tick;		// kapi_get_ticks
};
static Now g_now;
static void now_read ()
{
	static long s_zoneMinute = -2; static int s_tz = 0; static bool s_tzSet = false;
	Now &n = g_now;
	n.y = 2026; n.mo = 1; n.d = 1; n.h = n.mi = n.s = 0;
	n.real = kapi_get_datetime (&n.y, &n.mo, &n.d, &n.h, &n.mi, &n.s) == 1;
	n.day = clk_days (n.y, n.mo, n.d);
	n.minute = clk_minute (n.y, n.mo, n.d, n.h, n.mi);
	n.tick = (long) kapi_get_ticks ();
	if (n.minute != s_zoneMinute)				// (the zone chosen in Language & Region: seen within a minute)
	{
		s_zoneMinute = n.minute;
		char t[16] = "";
		s_tzSet = locale_ini_get ("timezone", t, sizeof t) > 0 && t[0];
		s_tz = s_tzSet ? atoi (t) : 0;
		n.zone = locale_zone ();
	}
	struct kapi_clock_info ci;
	if (kapi_clock_info (&ci) == 0 && (ci.flags & KAPI_CLOCK_REALTIME_VALID))
	{
		n.utc = (long long) (ci.utc_us / 1000000);
		n.here_off = ci.tz_minutes;
	}
	else
	{
		n.here_off = s_tz;
		n.utc = (long long) n.minute * 60 + n.s - (long long) s_tz * 60;
	}
	n.zone_set = n.zone >= 0 || s_tzSet;
}
// "today", "tomorrow", else the weekday -- of a wall day, seen from today
static inline const char *day_word (long day)
{
	long k = day - g_now.day;
	if (k == 0) return TR ("today");
	if (k == 1) return TR ("tomorrow");
	return TR (DAYNAME[clk_wday (day)]);
}

// ---- config.ini ([clock]) --------------------------------------------------------------------------------------------
struct Config { int tab; int city[CITIES_MAX]; int ncity; int snooze; int timer; int width, height; bool analogue; };
static Config g_cfg;
static fk_kv *g_cfgDoc;					// the file read (its other keys written back as they were)
static const char *cfg_get (const char *key, const char *def) { return fk_kv_get (g_cfgDoc, "clock", key, def); }
static void cfg_set (const char *key, const char *value) { if (g_cfgDoc) fk_kv_set (g_cfgDoc, "clock", key, value); }
static void cfg_set_int (const char *key, long v) { char t[24]; snprintf (t, sizeof t, "%ld", v); cfg_set (key, t); }
static int zone_by_name (const char *s)
{
	for (int z = 0; z < locale_zone_count (); z++) if (!strcmp (locale_zone_city (z), s)) return z;
	return -1;
}
static void cfg_load ()
{
	g_cfgDoc = fk_kv_load (CONFIG_PATH, 0);
	if (!g_cfgDoc) g_cfgDoc = fk_kv_new (0);
	g_cfg.tab = TAB_WORLD;
	const char *t = cfg_get ("tab", "world");
	for (int i = 0; i < TABS; i++) if (!strcmp (t, TABKEY[i])) g_cfg.tab = i;
	g_cfg.ncity = 0;
	const char *c = cfg_get ("cities", "");
	while (*c && g_cfg.ncity < CITIES_MAX)			// "Tokyo,New York,London": an unknown city skipped
	{
		char name[48]; int k = 0;
		while (*c == ',' || *c == ' ') c++;
		while (*c && *c != ',') { if (k < 47) name[k++] = *c; c++; }
		while (k > 0 && name[k - 1] == ' ') k--;
		name[k] = 0;
		int z = zone_by_name (name);
		bool dup = false;
		for (int i = 0; i < g_cfg.ncity; i++) dup |= g_cfg.city[i] == z;
		if (z >= 0 && !dup) g_cfg.city[g_cfg.ncity++] = z;
	}
	g_cfg.snooze = atoi (cfg_get ("snooze", "10"));
	if (g_cfg.snooze < 1 || g_cfg.snooze > 30) g_cfg.snooze = 10;
	g_cfg.timer = atoi (cfg_get ("timer", "300"));
	if (g_cfg.timer < 1 || g_cfg.timer > 86399) g_cfg.timer = 300;
	g_cfg.analogue = !strcmp (cfg_get ("face", "digital"), "analogue");
	g_cfg.width = atoi (cfg_get ("width", "560"));
	g_cfg.height = atoi (cfg_get ("height", "440"));
}
static void cfg_cities_text (char *out, int cap)
{
	out[0] = 0;
	for (int i = 0; i < g_cfg.ncity; i++) { if (i) cat (out, cap, ","); cat (out, cap, locale_zone_city (g_cfg.city[i])); }
}
static bool cfg_save ()
{
	char t[400];
	cfg_set ("tab", TABKEY[g_cfg.tab]);
	cfg_cities_text (t, sizeof t); cfg_set ("cities", t);
	cfg_set_int ("snooze", g_cfg.snooze);
	cfg_set_int ("timer", g_cfg.timer);
	cfg_set ("face", g_cfg.analogue ? "analogue" : "digital");
	cfg_set_int ("width", g_cfg.width);
	cfg_set_int ("height", g_cfg.height);
	return fk_kv_save (g_cfgDoc, CONFIG_PATH, "Onyx Clock settings (tab: world | alarms | timer | stopwatch; cities: SystemKit's zones; snooze: minutes, 1..30; timer: seconds; face: digital | analogue).") == 0;
}

// ---- the alarms (alarms.txt: the Clock writes it, clockd rings it) ----------------------------------------------------
static AlarmSet g_al;
static bool g_alSaveFailed;
static void clockd_reload ()
{
	int pid = kapi_ipc_lookup (CLOCKD_SERVICE);
	if (pid > 0) kapi_mailbox_send (pid, CLOCKD_MSG_RELOAD, "", 0);
}
// The set written (a once alarm whose minute is past written off), clockd told -> true; false: the card refused.
static inline bool alarms_write ()
{
	for (int i = 0; i < g_al.n; i++) if (g_al.a[i].on && alarm_passed (g_al.a[i], g_now.minute)) g_al.a[i].on = false;
	if (g_al.timer && alarms_timer_state (g_al, g_now.tick, g_now.real ? g_now.utc : -1) == TMR_STALE) alarms_timer_clear (g_al);
	g_alSaveFailed = alarms_save (g_al, ALARMS_PATH) != 0;
	if (g_alSaveFailed) { say ("alarms.txt not saved"); return false; }
	say ("alarms saved (%d)", g_al.n);
	clockd_reload ();
	return true;
}

// ---- the window ------------------------------------------------------------------------------------------------------
static Root *g_root;
static SegmentedControl *g_tabs;
static Page *g_page[TABS];
static int g_tab = -1;
static bool g_ringOnly;					// started only to ring: closes itself after the answer (04 D15)
static bool g_closeNow;					// ... time to close
static void tab_show (int t);

#include "world.h"
#include "alarmsview.h"
#include "timerview.h"
#include "swview.h"
#include "ring.h"

static void tab_show (int t)
{
	if (t < 0 || t >= TABS) return;
	bool changed = t != g_tab;
	g_tab = t;
	for (int i = 0; i < TABS; i++) g_page[i]->hidden = i != t;
	if (g_tabs->selected != t) g_tabs->select (t);
	g_root->invalidate (true);
	switch (t)
	{
	case TAB_WORLD:  world_shown (); break;
	case TAB_ALARMS: alarms_shown (); break;
	case TAB_TIMER:  timer_shown (); break;
	default:         sw_shown (); break;
	}
	if (changed) say ("tab %s", TABKEY[t]);
}
static void tabs_changed (Widget &) { tab_show (g_tabs->selected); }

// ---- the menus (04 §6): the keys are the window's (onKey), not the menu's -- UK_CTRL ('1') is ^Q, Ctrl+N means two
// things, Space / L / R / Delete belong to the fields; only Copy Laps is bound (G4) --------------------------------------
static Menu g_menu;
static void m_world () { tab_show (TAB_WORLD); }
static void m_alarms () { tab_show (TAB_ALARMS); }
static void m_timer () { tab_show (TAB_TIMER); }
static void m_sw () { tab_show (TAB_SW); }
static void m_next () { tab_show ((g_tab + 1) % TABS); }
static void menu_build ()
{
	g_menu.menu (TR ("View"));
	g_menu.item (TR ("World"),     "^1", 0, m_world);
	g_menu.item (TR ("Alarms"),    "^2", 0, m_alarms);
	g_menu.item (TR ("Timer"),     "^3", 0, m_timer);
	g_menu.item (TR ("Stopwatch"), "^4", 0, m_sw);
	g_menu.separator ();
	g_menu.item (TR ("Next Tab"),  "Ctrl+Tab", 0, m_next);
	world_view_menu (g_menu);
	alarms_menu (g_menu);
	world_menu (g_menu);
	timer_menu (g_menu);
	sw_menu (g_menu);
	g_menu.publish ();
}

class ClockRoot : public Root
{
public:
	ClockRoot (int w, int h) : Root (w, h, TR ("Clock")) {}
	void onDraw () override { Root::onDraw (); canvas.fillRect (0, TOP - 1, width, 1, uk_tone (bg, 100)); }
	void onResized () override
	{
		g_tabs->left = (width - g_tabs->width) / 2;
		invalidate (true);
	}
	bool onKey (long k) override
	{
		unsigned mods = kapi_get_modifiers ();
		bool ctrl = (mods & MOD_CTRL) != 0;
		if (ctrl && k >= '1' && k <= '4') { tab_show ((int) (k - '1')); return true; }
		if (ctrl && k == KEY_TAB) { tab_show ((g_tab + ((mods & MOD_SHIFT) ? TABS - 1 : 1)) % TABS); return true; }
		switch (g_tab)
		{
		case TAB_WORLD:  return world_key (k, ctrl);
		case TAB_ALARMS: return alarms_key (k, ctrl);
		case TAB_TIMER:  return timer_key (k, ctrl);
		default:         return sw_key (k, ctrl);
		}
	}
	void onTick () override
	{
		now_read ();
		// the messages to the "clock" service: a second Clock's arguments, clockd's rings
		int from = 0, type = 0, n;
		char m[520];
		while ((n = kapi_mailbox_recv (&from, &type, m, sizeof m - 1, 0)) >= 0)
		{
			m[n] = 0;
			if (type == CLOCK_MSG_OPEN) open_args (m, false);
		}
		world_tick ();
		alarms_tick ();
		timer_tick ();
		sw_tick ();
		ring_tick ();
	}
	// The arguments, at the start or from a second Clock / clockd: a tab, a ring.
	static void open_args (const char *a, bool atStart)
	{
		while (*a == ' ') a++;
		if (!strncmp (a, "--missed", 8)) { ring_missed (a + 8, true); return; }	// (clockd: no window raised for it)
		char w[64]; int k;
		bool ring = false;
		while (*a)
		{
			while (*a == ' ') a++;
			for (k = 0; *a && *a != ' '; a++) if (k < 63) w[k++] = *a;
			w[k] = 0;
			if (!k) break;
			if (!strcmp (w, "--ring"))
			{
				while (*a == ' ') a++;
				for (k = 0; *a && *a != ' '; a++) if (k < 63) w[k++] = *a;
				w[k] = 0;
				ring = true;
				if (atStart) g_ringOnly = true;
				if (!strcmp (w, "timer")) ring_timer ();
				else ring_alarm (atoi (w));
				continue;
			}
			for (int t = 0; t < TABS; t++) if (!strcmp (w, TABKEY[t])) { tab_show (t); if (!atStart) g_ringOnly = false; }
		}
		if (!atStart && !ring) { g_ringOnly = false; uk_win_app_raise (CLOCK_SERVICE); }
	}
};

int main (void)
{
	// One Clock at a time: one already running (the "clock" service) is sent this one's arguments (empty: only come
	// forward) and raised, and this one ends here.
	char args[400] = "";
	kapi_get_args (args, sizeof args);
	bool missedOnly = !strncmp (args, "--missed", 8);	// (clockd at its start: the alarms missed while the Pi was off)
	int other = kapi_ipc_lookup (CLOCK_SERVICE);
	if (other > 0)
	{
		kapi_mailbox_send (other, CLOCK_MSG_OPEN, args, (unsigned) strlen (args) + 1);
		if (!missedOnly) uk_win_app_raise (CLOCK_SERVICE);
		return 0;
	}
	kapi_ipc_register (CLOCK_SERVICE);			// (failed: the Clock runs alone, nothing forwarded to it)
	ft_uikit_install ("DejaVu Sans", 13);			// (before the widgets; false: the bitmap font)
	uk_lang_init ();					// the words in the system's language (before the widgets)
	cfg_load ();
	alarms_init (g_al);
	alarms_load (g_al, ALARMS_PATH);
	now_read ();
	if (missedOnly)						// told, written, and gone: no window
	{
		ring_missed (args + 8, false);
		alarms_free (g_al);
		return 0;
	}
	// clockd rings the alarms with the Clock closed: started when it does not run, its boot line made sure of (a card
	// updated by the package manager keeps its own SD:/etc/autostart)
	autostart_ensure ("run clockd", "run notifyd", "# The Clock's alarms (rung with the app closed): clockd, the alarm service.");
	if (kapi_ipc_lookup (CLOCKD_SERVICE) <= 0 && lx_launch ("clockd", 0) > 0) say ("clockd started");

	int w = g_cfg.width < MIN_W ? MIN_W : g_cfg.width > 2000 ? 2000 : g_cfg.width;
	int h = g_cfg.height < MIN_H ? MIN_H : g_cfg.height > 1400 ? 1400 : g_cfg.height;
	ClockRoot root (w, h);
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	root.setResizable (true);
	root.setMinSize (MIN_W, MIN_H);

	static const char *tabs[TABS];
	for (int i = 0; i < TABS; i++) tabs[i] = TR (TABNAME[i]);
	g_tabs = new SegmentedControl ((w - 440) / 2, 10, 440, 28, tabs, TABS, 0, tabs_changed);
	root.addChild (g_tabs);
	for (int i = 0; i < TABS; i++) { g_page[i] = new Page (w, h); g_page[i]->hidden = true; root.addChild (g_page[i]); }
	world_build (g_page[TAB_WORLD]);
	alarms_build (g_page[TAB_ALARMS]);
	timer_build (g_page[TAB_TIMER]);
	sw_build (g_page[TAB_SW]);
	menu_build ();

	tab_show (g_cfg.tab);
	timer_resume_handed ();				// (a timer handed to clockd at the last close, still to come: taken back)
	ClockRoot::open_args (args, true);
	if (g_ringOnly && g_tab != TAB_ALARMS && g_tab != TAB_TIMER) tab_show (TAB_ALARMS);
	root.fitWorkArea ();

	root.attach ();
	while (root.step ())
	{
		if (g_closeNow) { say ("ring-only, closing"); break; }
		kapi_msleep (16);
	}

	// The window closed: what runs is handed over (the timer to clockd, the stopwatch kept), the settings kept.
	sound_stop ();
	timer_at_exit ();
	sw_at_exit ();
	if (!g_ringOnly)
	{
		g_cfg.tab = g_tab;
		if (!root.maximised ()) { g_cfg.width = root.width; g_cfg.height = root.height; }
	}
	cfg_save ();
	alarms_free (g_al);
	return 0;
}
