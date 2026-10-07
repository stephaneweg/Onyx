//
// alarm_test.cpp -- the Clock's alarm logic on the PC (AutoDev round 6, step 3; 03 §8.1): user/Apps/clock/alarms.cpp
// and clocktime.cpp with a fake "now" -- the ring once on its minute, across reloads, weekends, summer-time nights,
// jumps of the clock and the boot guard; the file read and written (unknown keys kept, the exact block of an alarm);
// the [timer] handed over rung once. Linked with the desktop simulator's fakekapi.o (alarms_save's file goes to
// SIM_WRITES); run by tools/tests/run_clock_test.sh. Prints "alarm: N checks passed".
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
#include "clock/alarms.h"
#include "clock/clocktime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_fails;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fails++; printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define EQI(a, b) do { g_checks++; long long a_ = (long long) (a), b_ = (long long) (b); if (a_ != b_) { g_fails++; printf ("FAIL %s:%d: %s = %lld, expected %lld\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)
#define EQS(a, b) do { g_checks++; const char *a_ = (a), *b_ = (b); if (strcmp (a_, b_)) { g_fails++; printf ("FAIL %s:%d: %s =\n\"%s\"\nexpected\n\"%s\"\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)

static long W (int y, int mo, int d, int h, int mi) { return clk_minute (y, mo, d, h, mi); }
static char g_txt[16384];
// the file alarms_save wrote (SIM_WRITES/apps/clock.app/alarms.txt), as text
static const char *saved (AlarmSet &s)
{
	CHECK (alarms_save (s, ALARMS_PATH) == 0);
	char p[1024];
	snprintf (p, sizeof p, "%s/apps/clock.app/alarms.txt", getenv ("SIM_WRITES") ? getenv ("SIM_WRITES") : "/tmp/onyx_sim_writes");
	FILE *f = fopen (p, "rb");
	size_t n = f ? fread (g_txt, 1, sizeof g_txt - 1, f) : 0;
	if (f) fclose (f);
	g_txt[n] = 0;
	return g_txt;
}

// The rings of a run of the Ringer: every second from (wall second) a to b, the ticks 100 a second
struct Log { int n; Due d[64]; long at[64]; };
static void step (Ringer &r, const AlarmSet &s, long ws, Log &L, bool trusted = true)
{
	Due d[8];
	int k = ringer_step (r, s, ws / 60 - (ws < 0 && ws % 60 ? 1 : 0), trusted, ws * 100, -1, d, 8);
	for (int i = 0; i < k && L.n < 64; i++) { L.d[L.n] = d[i]; L.at[L.n] = ws; L.n++; }
}
static void run (Ringer &r, const AlarmSet &s, long from_s, long to_s, Log &L, long every = 1)
{
	for (long t = from_s; t <= to_s; t += every) step (r, s, t, L);
}
static long S (int y, int mo, int d, int h, int mi, int sec) { return W (y, mo, d, h, mi) * 60 + sec; }

static const char *WEEKDAYS_0700 =
	"[alarm]\nid = 1\ntime = 07:00\nlabel = School\non = 1\ndays = mon tue wed thu fri\ndate =\nsound = chimes\nsnooze =\n";

static void t1_t2_once_on_its_minute ()
{
	AlarmSet s; alarms_init (s);
	EQI (alarms_parse (s, WEEKDAYS_0700), 1);
	Ringer r = {}; Log L = {};
	run (r, s, S (2026, 9, 28, 6, 58, 0), S (2026, 9, 28, 7, 3, 0), L);
	EQI (L.n, 1);							// 1: once
	EQI (L.d[0].id, 1); EQI (L.d[0].minute, W (2026, 9, 28, 7, 0)); CHECK (!L.d[0].snooze);
	CHECK (L.at[0] >= S (2026, 9, 28, 7, 0, 0) && L.at[0] <= S (2026, 9, 28, 7, 0, 2));	// within 2 s (0 s here)
	// 2: the file read again at 07:00:30 (the Ringer kept) -> still once
	Ringer r2 = {}; Log L2 = {};
	run (r2, s, S (2026, 9, 28, 6, 58, 0), S (2026, 9, 28, 7, 0, 30), L2);
	alarms_parse (s, WEEKDAYS_0700);
	run (r2, s, S (2026, 9, 28, 7, 0, 31), S (2026, 9, 28, 7, 5, 0), L2);
	EQI (L2.n, 1);
	alarms_free (s);
}

static void t3_weekend ()
{
	AlarmSet s; alarms_init (s);
	alarms_parse (s, WEEKDAYS_0700);
	EQI (alarm_next (s.a[0], W (2026, 10, 2, 8, 0)), W (2026, 10, 5, 7, 0));	// Fri 08:00 -> Mon 07:00
	EQI (alarm_next (s.a[0], W (2026, 10, 4, 23, 59)), W (2026, 10, 5, 7, 0));	// Sun 23:59 -> Mon 07:00
	EQI (alarm_next (s.a[0], W (2026, 10, 5, 7, 0)), W (2026, 10, 5, 7, 0));	// (>= from)
	EQI (alarm_next (s.a[0], W (2026, 12, 31, 7, 1)), W (2027, 1, 1, 7, 0));	// (a Friday, across the year)
	Ringer r = {}; Log L = {};
	run (r, s, S (2026, 10, 2, 8, 0, 0), S (2026, 10, 5, 6, 59, 0), L, 60);	// the weekend, minute by minute
	EQI (L.n, 0);
	run (r, s, S (2026, 10, 5, 7, 0, 0), S (2026, 10, 5, 7, 0, 0), L);
	EQI (L.n, 1);
	alarms_free (s);
}

static void t4_disabled_deleted ()
{
	AlarmSet s; alarms_init (s);
	alarms_parse (s, "[alarm]\nid = 1\ntime = 07:00\non = 1\ndays = mon tue wed thu fri sat sun\n"
	                 "[alarm]\nid = 2\ntime = 08:00\non = 1\ndays = mon tue wed thu fri sat sun\n");
	Ringer r = {}; Log L = {};
	run (r, s, S (2026, 9, 28, 0, 0, 0), S (2026, 9, 28, 0, 1, 0), L);
	alarms_parse (s, "[alarm]\nid = 1\ntime = 07:00\non = 0\ndays = mon tue wed thu fri sat sun\n");	// 1 off, 2 deleted
	run (r, s, S (2026, 9, 28, 0, 2, 0), S (2026, 10, 5, 23, 59, 0), L, 60);
	EQI (L.n, 0);
	alarms_free (s);
}

static void t5_once ()
{
	AlarmSet s; alarms_init (s);
	alarms_parse (s, "[alarm]\nid = 4\ntime = 07:00\non = 1\ndays =\ndate = 20260929\n");
	Ringer r = {}; Log L = {};
	run (r, s, S (2026, 9, 28, 0, 0, 0), S (2026, 10, 6, 0, 0, 0), L, 60);
	EQI (L.n, 1);
	EQI (L.d[0].id, 4); EQI (L.d[0].minute, W (2026, 9, 29, 7, 0));
	CHECK (!alarm_passed (s.a[0], W (2026, 9, 29, 7, 0)));
	CHECK (alarm_passed (s.a[0], W (2026, 9, 29, 7, 1)));
	CHECK (!alarm_passed (s.a[0], W (2026, 9, 28, 12, 0)));
	alarms_free (s);
}

static void t6_snooze ()
{
	AlarmSet s; alarms_init (s);
	alarms_parse (s, "[alarm]\nid = 1\ntime = 07:00\nlabel = School\non = 1\ndays = mon tue wed thu fri\n");
	Ringer r = {}; Log L = {};
	run (r, s, S (2026, 9, 29, 6, 59, 0), S (2026, 9, 29, 7, 0, 20), L);
	EQI (L.n, 1);
	alarm_snooze (s.a[0], S (2026, 9, 29, 7, 0, 20) / 60, 10);			// Snooze at 07:00:20
	EQI (s.a[0].snooze, W (2026, 9, 29, 7, 10));
	const char *t = saved (s);
	CHECK (strstr (t, "\nsnooze = 202609290710\n") != 0);
	alarms_load (s, ALARMS_PATH);						// (the Clock wrote it, clockd reloads)
	EQI (s.a[0].snooze, W (2026, 9, 29, 7, 10));
	run (r, s, S (2026, 9, 29, 7, 0, 21), S (2026, 9, 29, 7, 30, 0), L);
	EQI (L.n, 2);
	EQI (L.d[1].minute, W (2026, 9, 29, 7, 10)); CHECK (L.d[1].snooze); EQI (L.d[1].id, 1);
	EQI (L.at[1], S (2026, 9, 29, 7, 10, 0));
	alarms_next (s, W (2026, 9, 29, 7, 5), 0);
	EQI (alarms_next (s, W (2026, 9, 29, 7, 5), 0), W (2026, 9, 29, 7, 10));	// "Snoozed until 07:10"
	alarm_stop (s.a[0]);
	EQI (s.a[0].snooze, -1); CHECK (s.a[0].on);					// (a weekly alarm stays on)
	alarms_free (s);
}

static void t7_t8_t9_late ()
{
	AlarmSet s; alarms_init (s);
	alarms_parse (s, WEEKDAYS_0700);
	Ringer r = {}; Log L = {};
	run (r, s, S (2026, 9, 28, 9, 0, 0), S (2026, 9, 28, 9, 10, 0), L);		// 7: started at 09:00
	EQI (L.n, 0);
	Ringer r8 = {}; Log L8 = {};
	step (r8, s, S (2026, 9, 28, 6, 59, 40), L8); step (r8, s, S (2026, 9, 28, 6, 59, 50), L8);
	step (r8, s, S (2026, 9, 28, 7, 1, 30), L8);					// 8: 06:59:50 -> 07:01:30
	EQI (L8.n, 1); EQI (L8.d[0].minute, W (2026, 9, 28, 7, 0));
	Ringer r9 = {}; Log L9 = {};
	step (r9, s, S (2026, 9, 28, 6, 58, 0), L9); step (r9, s, S (2026, 9, 28, 6, 59, 0), L9);
	step (r9, s, S (2026, 9, 28, 7, 3, 0), L9);					// 9: 06:59 -> 07:03
	run (r9, s, S (2026, 9, 28, 7, 3, 1), S (2026, 9, 28, 7, 10, 0), L9);
	EQI (L9.n, 0);
	Ringer rb = {}; Log Lb = {};							// (2 minutes late still rung)
	step (rb, s, S (2026, 9, 28, 6, 59, 0), Lb); step (rb, s, S (2026, 9, 28, 7, 2, 0), Lb);
	EQI (Lb.n, 1);
	alarms_free (s);
}

static void t10_back_one_hour ()
{
	AlarmSet s; alarms_init (s);
	alarms_parse (s, "[alarm]\nid = 1\ntime = 02:30\non = 1\ndays = mon tue wed thu fri sat sun\n");
	Ringer r = {}; Log L = {};
	run (r, s, S (2026, 10, 25, 1, 58, 0), S (2026, 10, 25, 3, 0, 0), L, 10);	// CEST, then at 03:00...
	run (r, s, S (2026, 10, 25, 2, 0, 0), S (2026, 10, 25, 4, 0, 0), L, 10);	// ... back to 02:00 CET
	EQI (L.n, 1);
	EQI (L.d[0].minute, W (2026, 10, 25, 2, 30));
	CHECK (L.at[0] < S (2026, 10, 25, 3, 0, 0));					// (the first time)
	run (r, s, S (2026, 10, 25, 4, 0, 10), S (2026, 10, 26, 2, 31, 0), L, 30);	// the next night: again
	EQI (L.n, 2);
	alarms_free (s);
}

static void t11_set_back ()
{
	AlarmSet s; alarms_init (s);
	alarms_parse (s, "[alarm]\nid = 1\ntime = 07:01\non = 1\ndays = mon tue wed thu fri sat sun\n");
	Ringer r = {}; Log L = {};
	run (r, s, S (2026, 9, 28, 11, 58, 0), S (2026, 9, 28, 12, 0, 0), L);
	run (r, s, S (2026, 9, 28, 7, 0, 0), S (2026, 9, 28, 7, 5, 0), L);		// set back 5 h, then on
	EQI (L.n, 1);
	EQI (L.d[0].minute, W (2026, 9, 28, 7, 1)); EQI (L.at[0], S (2026, 9, 28, 7, 1, 0));
	alarms_free (s);
}

static void t12_boot_guard ()
{
	AlarmSet s; alarms_init (s);
	alarms_parse (s, "[alarm]\nid = 1\ntime = 07:00\non = 1\ndays = mon tue wed thu fri sat sun\n"
	                 "[alarm]\nid = 2\ntime = 07:02\non = 1\ndays = mon tue wed thu fri sat sun\n");
	Ringer r = {}; Log L = {};
	long boot = S (2026, 9, 28, 6, 59, 30);
	for (long t = boot; t <= S (2026, 9, 28, 7, 5, 0); t++) step (r, s, t, L, t - boot >= 90);	// trusted from 07:01:00
	EQI (L.n, 1);
	EQI (L.d[0].id, 2); EQI (L.at[0], S (2026, 9, 28, 7, 2, 0));
	alarms_free (s);
}

static void t13_summer_nights ()
{
	AlarmSet s; alarms_init (s);
	alarms_parse (s, "[alarm]\nid = 1\ntime = 07:00\non = 1\ndays = mon tue wed thu fri sat sun\n");
	EQI (alarm_next (s.a[0], W (2026, 10, 24, 8, 0)), W (2026, 10, 25, 7, 0));
	EQI (alarm_next (s.a[0], W (2026, 10, 25, 7, 1)), W (2026, 10, 26, 7, 0));
	EQI (alarm_next (s.a[0], W (2027, 3, 27, 8, 0)), W (2027, 3, 28, 7, 0));
	EQI (alarm_next (s.a[0], W (2027, 3, 28, 7, 1)), W (2027, 3, 29, 7, 0));
	alarms_free (s);
}

static void t14_once_day ()
{
	long now = W (2026, 9, 28, 12, 34);
	char d[16];
	clk_fmt_day (alarm_once_day (10, 0, now), d, sizeof d); EQS (d, "20260929");
	clk_fmt_day (alarm_once_day (13, 0, now), d, sizeof d); EQS (d, "20260928");
	clk_fmt_day (alarm_once_day (12, 34, now), d, sizeof d); EQS (d, "20260929");
	clk_fmt_day (alarm_once_day (12, 35, now), d, sizeof d); EQS (d, "20260928");
	clk_fmt_day (alarm_once_day (0, 0, W (2026, 12, 31, 23, 59)), d, sizeof d); EQS (d, "20270101");
}

static void t15_next_line ()
{
	AlarmSet s; alarms_init (s);
	alarms_parse (s, WEEKDAYS_0700);
	long now = W (2026, 9, 28, 12, 34);
	int idx = -2;
	long at = alarms_next (s, now, &idx);
	EQI (at - now, 1106); EQI (idx, 0);						// tomorrow 07:00, in 18 h 26 min
	EQI (clk_day_of (at) - clk_day_of (now), 1);
	Alarm a; alarm_new (s, a);
	a.hh = 14; a.mm = 30; a.date = alarm_once_day (14, 30, now); alarm_set_label (a, "Medicine");
	EQI (alarm_add (s, a), 1);
	at = alarms_next (s, now, &idx);
	EQI (at - now, 116); EQI (idx, 1); EQI (clk_day_of (at) - clk_day_of (now), 0);	// today 14:30, in 1 h 56 min
	s.a[0].on = s.a[1].on = false;
	EQI (alarms_next (s, now, &idx), -1); EQI (idx, -1);				// "No alarm set"
	alarms_free (s);
}

static void t16_repeat_kind ()
{
	EQI (alarm_repeat_kind (alarms_days_parse ("mon tue wed thu fri")), REP_WEEKDAYS);
	EQI (alarm_repeat_kind (alarms_days_parse ("mon tue wed thu fri sat sun")), REP_DAILY);
	EQI (alarm_repeat_kind (alarms_days_parse ("sat sun")), REP_WEEKENDS);
	EQI (alarm_repeat_kind (alarms_days_parse ("mon wed fri")), REP_CUSTOM);
	EQI (alarm_repeat_kind (alarms_days_parse ("")), REP_ONCE);
	EQI (alarms_days_parse ("Sun, MON  xyz fri"), AL_MON | AL_FRI | AL_SUN);
	char t[64];
	alarms_days_text (AL_SUN | AL_MON | AL_WED, t, sizeof t); EQS (t, "mon wed sun");
	alarms_days_text (0, t, sizeof t); EQS (t, "");
	alarms_days_text (AL_EVERYDAY, t, 8); EQS (t, "mon tue");			// (cut at cap)
}

static void t17_hand_written ()
{
	char text[8192]; int n = 0;
	n += snprintf (text + n, sizeof text - n, "# my alarms\n[alarm]\nid = 1\ntime = 07:00\ncolour = red\nlabel = School\non = 1\n"
	               "days = mon tue wed thu fri\ndate =\nsound = chimes\nsnooze =\n");
	n += snprintf (text + n, sizeof text - n, "\n[alarm]\nid = 2\ntime = 25:99\nlabel = Broken\non = 1\ndays = mon tue wed thu fri sat sun\n");
	for (int i = 3; i <= 22; i++) n += snprintf (text + n, sizeof text - n, "\n[alarm]\nid = %d\ntime = 06:%02d\non = 0\n", i, i);
	n += snprintf (text + n, sizeof text - n, "\n[notes]\nwho = me\n");
	AlarmSet s; alarms_init (s);
	EQI (alarms_parse (s, text), 20);						// 22 blocks: the first 20
	CHECK (s.a[0].valid && !s.a[1].valid);
	EQS (s.a[1].label, "Broken");
	EQI (s.next_id, 21);
	Ringer r = {}; Log L = {};
	run (r, s, S (2026, 9, 28, 0, 0, 0), S (2026, 9, 29, 0, 0, 0), L, 60);	// 25:99 never rung (07:00 is)
	EQI (L.n, 1); EQI (L.d[0].id, 1);
	const char *t = saved (s);
	CHECK (strstr (t, "[alarm]\nid = 1\ntime = 07:00\nlabel = School\non = 1\ndays = mon tue wed thu fri\ndate =\n"
	                  "sound = chimes\nsnooze =\ncolour = red\n") != 0);		// the unknown key kept
	CHECK (strstr (t, "[alarm]\nid = 2\ntime = 25:99\nlabel = Broken\n") != 0);	// the invalid time as read
	CHECK (strstr (t, "[notes]\nwho = me\n") != 0);					// another block kept
	CHECK (strstr (t, "id = 20\n") != 0 && !strstr (t, "id = 21\n") && !strstr (t, "id = 22\n"));
	CHECK (!strncmp (t, "# Clock -- the alarms", 21));
	// saved again (re-based on what was written): the same text
	char first[16384]; strcpy (first, t);
	EQS (saved (s), first);
	// read back: the same alarms
	AlarmSet b; alarms_init (b);
	EQI (alarms_load (b, ALARMS_PATH), 20);
	EQS (fk_kv_block_get (b.doc, 1, "colour", "-"), "red");
	CHECK (!b.a[1].valid);
	alarms_free (b);
	// ids: a missing or repeated one renumbered, above them all
	alarms_parse (s, "[alarm]\nid = 5\ntime = 07:00\n[alarm]\nid = 5\ntime = 08:00\n[alarm]\ntime = 09:00\n");
	EQI (s.a[0].id, 5); EQI (s.a[1].id, 6); EQI (s.a[2].id, 7); EQI (s.next_id, 8);
	CHECK (s.a[2].on);								// (no "on": on)
	alarms_free (s);
}

static void t18_new_alarm_block ()
{
	AlarmSet s; alarms_init (s);
	Alarm a; alarm_new (s, a);
	a.hh = 7; a.mm = 0; alarm_set_label (a, "School"); a.days = AL_WEEKDAYS;
	EQI (alarm_add (s, a), 0);
	EQI (s.a[0].id, 1);
	EQS (saved (s), "# Clock -- the alarms (written by the Clock app, rung by clockd). One [alarm] block an alarm.\n\n"
	                "[alarm]\nid = 1\ntime = 07:00\nlabel = School\non = 1\ndays = mon tue wed thu fri\ndate =\n"
	                "sound = chimes\nsnooze =\n");
	// the 21st refused; removing; a missing file: an empty set
	for (int i = 1; i < ALARMS_MAX; i++) { alarm_new (s, a); CHECK (alarm_add (s, a) == i); }
	alarm_new (s, a);
	EQI (alarm_add (s, a), -1);
	EQI (alarm_remove (s, 3), 1); EQI (alarm_remove (s, 3), 0); EQI (s.n, ALARMS_MAX - 1);
	EQI (alarm_find (s, 4), 2);
	alarm_new (s, a); EQI (a.id, 21);						// (never reused: 3 not given again)
	EQI (alarms_load (s, "SD:/apps/clock.app/none.txt"), 0); EQI (s.n, 0); EQI (s.next_id, 1);
	// labels: 40 characters at most, cut on a character; new lines made spaces; the sound token
	alarm_set_label (a, "0123456789012345678901234567890123456789XYZ"); EQI ((long) strlen (a.label), 40);
	alarm_set_label (a, "\xC3\xA9" "cole\nlundi");			// "école\nlundi"
	EQS (a.label, "\xC3\xA9" "cole lundi");
	char u[200] = ""; for (int i = 0; i < 45; i++) strcat (u, "\xE2\x82\xAC");	// 45 euro signs (3 bytes each)
	alarm_set_label (a, u); EQI ((long) strlen (a.label), 120);
	EQS (alarm_sound ("marimba"), "marimba"); EQS (alarm_sound ("beeps"), "beeps"); EQS (alarm_sound ("gong"), "chimes");
	alarms_free (s);
}

static void t19_timer ()
{
	const long T = 100000;
	char text[256];
	snprintf (text, sizeof text, "[timer]\nend = %ld\nset = 600\nlabel = Tea\n", T);
	AlarmSet s; alarms_init (s);
	alarms_parse (s, text);
	CHECK (s.timer); EQI (s.timer_end_tick, T); EQI (s.timer_set, 600); EQS (s.timer_label, "Tea");
	EQI (alarms_timer_state (s, T - 1, -1), TMR_PENDING);
	EQI (alarms_timer_state (s, T, -1), TMR_DUE);					// due at T, not before
	EQI (alarms_timer_state (s, T - 60100, -1), TMR_PENDING);			// (set * 100 + 100 ahead: still this boot's)
	EQI (alarms_timer_state (s, T - 60101, -1), TMR_STALE);			// (further: an earlier boot's)
	Ringer r = {}; Due d[4]; int rung = 0, at = -1;
	long m = W (2026, 9, 28, 12, 0);
	for (long t = T - 100; t <= T + 1000; t += 50)				// stepped every 50 ticks: once
	{
		int k = ringer_step (r, s, m, true, t, -1, d, 4);
		for (int i = 0; i < k; i++) if (d[i].id == ALARM_TIMER) { rung++; if (at < 0) at = (int) (t - T); }
	}
	EQI (rung, 1); EQI (at, 0);
	alarms_parse (s, text);							// reloaded, the same end: not again
	for (long t = T + 1000; t <= T + 2000; t += 50) rung += ringer_step (r, s, m, true, t, -1, d, 4);
	EQI (rung, 1);
	snprintf (text, sizeof text, "[timer]\nend = %ld\nset = 600\n", T + 3000);	// a new hand-over: once again
	alarms_parse (s, text);
	for (long t = T + 2000; t <= T + 4000; t += 50) rung += ringer_step (r, s, m, false, t, -1, d, 4);	// (untrusted: the ticks still)
	EQI (rung, 2);
	// a stale one never rings
	snprintf (text, sizeof text, "[timer]\nend = %ld\nset = 10\n", T + 5000);
	alarms_parse (s, text);
	Ringer r2 = {}; int k = 0;
	for (long t = T; t < T + 3000; t += 50) k += ringer_step (r2, s, m, true, t, -1, d, 4);
	EQI (k, 0);
	// utc =: both reckonings must agree (validation 1 note 4)
	snprintf (text, sizeof text, "[timer]\nend = 5000\nset = 300\nutc = 1790598900\n");	// 10:35:00 UTC
	alarms_parse (s, text);
	EQI (alarms_timer_state (s, 2000, 1790598870LL), TMR_PENDING);		// 30 s to come by both
	EQI (alarms_timer_state (s, 2000, 1790598000LL), TMR_STALE);		// the ticks of another boot
	EQI (alarms_timer_state (s, 2000, -1), TMR_PENDING);			// (no UTC now: the ticks alone)
	EQI (alarms_timer_state (s, 5000, 1790598903LL), TMR_DUE);		// (3 s off: within 5)
	// written: end, set, label, utc; cleared: gone; the alarms beside it untouched
	alarms_parse (s, WEEKDAYS_0700);
	alarms_timer_put (s, 61000, 300, 1790598900LL, "Eggs\nnow");
	const char *t = saved (s);
	CHECK (strstr (t, "[alarm]\nid = 1\ntime = 07:00\nlabel = School\n") != 0);
	CHECK (strstr (t, "\n[timer]\nend = 61000\nset = 300\nlabel = Eggs now\nutc = 1790598900\n") != 0);
	alarms_load (s, ALARMS_PATH);
	CHECK (s.timer && s.timer_end_tick == 61000 && s.timer_utc == 1790598900LL);
	alarms_timer_clear (s);
	t = saved (s);
	CHECK (!strstr (t, "[timer]") && strstr (t, "[alarm]\nid = 1\n"));
	alarms_free (s);
}

int main ()
{
	t1_t2_once_on_its_minute ();
	t3_weekend ();
	t4_disabled_deleted ();
	t5_once ();
	t6_snooze ();
	t7_t8_t9_late ();
	t10_back_one_hour ();
	t11_set_back ();
	t12_boot_guard ();
	t13_summer_nights ();
	t14_once_day ();
	t15_next_line ();
	t16_repeat_kind ();
	t17_hand_written ();
	t18_new_alarm_block ();
	t19_timer ();
	if (g_fails) { printf ("alarm: FAIL (%d of %d checks)\n", g_fails, g_checks); return 1; }
	printf ("alarm: %d checks passed\n", g_checks);
	return 0;
}
