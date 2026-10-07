//
// clocktime_test.cpp -- the Clock's time arithmetic on the PC (AutoDev round 6, step 3; 03 §8.2):
// user/Apps/clock/clocktime.cpp -- the dates, the World tab's rows (with SystemKit's locale_zone_offset_at, inline on
// the PC), the texts, the timer from the ticks (AC-34), the stopwatch's laps and their text (AC-38, AC-39). No kernel
// call; run by tools/tests/run_clock_test.sh. Prints "clocktime: N checks passed".
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
#include "clock/clocktime.h"
#include "systemkit/systemkit.h"
#include <stdio.h>
#include <string.h>

static int g_checks, g_fails;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fails++; printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define EQI(a, b) do { g_checks++; long long a_ = (long long) (a), b_ = (long long) (b); if (a_ != b_) { g_fails++; printf ("FAIL %s:%d: %s = %lld, expected %lld\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)
#define EQS(a, b) do { g_checks++; const char *a_ = (a), *b_ = (b); if (strcmp (a_, b_)) { g_fails++; printf ("FAIL %s:%d: %s = \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)

static char g_b[256];
static const char *hm (int h, int m) { clk_fmt_hm (h, m, g_b, sizeof g_b); return g_b; }
static const char *diff (int d) { clk_fmt_diff (d, g_b, sizeof g_b); return g_b; }
static const char *utc (int o) { clk_fmt_utc (o, g_b, sizeof g_b); return g_b; }
static const char *tmr (long s) { clk_fmt_timer (s, g_b, sizeof g_b); return g_b; }
static const char *sw (long cs) { clk_fmt_sw (cs, g_b, sizeof g_b); return g_b; }
static int zone (const char *city)
{
	for (int z = 0; z < locale_zone_count (); z++) if (!strcmp (locale_zone_city (z), city)) return z;
	printf ("FAIL no zone %s\n", city);
	return -1;
}

static void test_dates ()
{
	EQI (clk_days (1970, 1, 1), 0);
	EQI (clk_days (2026, 9, 28), 20724);
	EQI (clk_wday (clk_days (2026, 9, 28)), 0);			// a Monday
	EQI (clk_wday (clk_days (2026, 10, 4)), 6);			// a Sunday
	EQI (clk_wday (clk_days (1969, 12, 31)), 2);			// (a Wednesday: before 1970 too)
	int y, mo, d, bad = 0;
	for (long k = -800; k < 40000; k += 7)				// round trip, and against a plain day count
	{
		clk_civil (k, &y, &mo, &d);
		if (clk_days (y, mo, d) != k) bad++;
	}
	EQI (bad, 0);
	clk_civil (clk_days (2028, 2, 29) + 1, &y, &mo, &d); CHECK (y == 2028 && mo == 3 && d == 1);
	EQI (clk_minute (2026, 9, 28, 12, 34), 20724L * 1440 + 754);
	EQI (clk_day_of (-1), -1);
	CHECK (clk_valid_date (2028, 2, 29) && !clk_valid_date (2027, 2, 29) && !clk_valid_date (2026, 13, 1) && !clk_valid_date (1969, 1, 1));
	EQI (clk_parse_day ("20260929"), clk_days (2026, 9, 29));
	EQI (clk_parse_day ("20260931"), -1); EQI (clk_parse_day (""), -1); EQI (clk_parse_day ("2026-09-29"), -1); EQI (clk_parse_day (0), -1);
	EQI (clk_parse_minute ("202609290710"), clk_minute (2026, 9, 29, 7, 10));
	EQI (clk_parse_minute ("202609292460"), -1); EQI (clk_parse_minute ("20260929071"), -1);
	clk_fmt_day (clk_days (2026, 9, 29), g_b, sizeof g_b); EQS (g_b, "20260929");
	clk_fmt_day (-1, g_b, sizeof g_b); EQS (g_b, "");
	clk_fmt_minute (clk_minute (2026, 9, 29, 7, 10), g_b, sizeof g_b); EQS (g_b, "202609290710");
	int h = -1, m = -1;
	CHECK (clk_parse_hm ("07:00", &h, &m) && h == 7 && m == 0);
	CHECK (clk_parse_hm ("7:05", &h, &m) && h == 7 && m == 5);
	CHECK (clk_parse_hm ("23:59", &h, &m) && h == 23 && m == 59);
	CHECK (!clk_parse_hm ("25:99", &h, &m) && !clk_parse_hm ("24:00", 0, 0) && !clk_parse_hm ("7", 0, 0) && !clk_parse_hm ("", 0, 0) && !clk_parse_hm ("07:0a", 0, 0));
}

static void test_texts ()
{
	EQS (hm (7, 0), "07:00");
	clk_fmt_hms (12, 34, 0, g_b, sizeof g_b); EQS (g_b, "12:34:00");
	EQS (diff (420), "+7 h"); EQS (diff (-360), "-6 h"); EQS (diff (-60), "-1 h"); EQS (diff (330), "+5 h 30");
	EQS (diff (-210), "-3 h 30"); EQS (diff (0), "");
	EQS (utc (120), "UTC+2"); EQS (utc (-210), "UTC-3:30"); EQS (utc (0), "UTC"); EQS (utc (-300), "UTC-5");
	EQS (tmr (300), "05:00"); EQS (tmr (3599), "59:59"); EQS (tmr (3600), "1:00:00"); EQS (tmr (0), "00:00"); EQS (tmr (86399), "23:59:59");
	EQS (sw (1234), "00:12.34"); EQS (sw (359999), "59:59.99"); EQS (sw (360000), "1:00:00.00"); EQS (sw (0), "00:00.00");
	clk_fmt_sw (360000, g_b, 6); EQS (g_b, "1:00:");				// (cut at cap)
}

// The World tab's rows (AC-6, AC-7, AC-8): the city's offset from SystemKit at that instant
static void test_world ()
{
	long long u = (long long) clk_minute (2026, 9, 28, 10, 34) * 60;	// 10:34 UTC = 12:34 in Brussels
	long here = clk_days (2026, 9, 28);
	long long um = u / 60;
	int bx = zone ("Brussels"), tk = zone ("Tokyo"), ny = zone ("New York"), ld = zone ("London"), pa = zone ("Paris");
	int here_off = locale_zone_offset_at (bx, um);
	EQI (here_off, 120); EQS (utc (here_off), "UTC+2");
	ClkCity c = clk_city (u, locale_zone_offset_at (tk, um), here, here_off);
	EQS (hm (c.hh, c.mm), "19:34"); EQS (diff (c.diff), "+7 h"); EQI (c.day, 0);
	c = clk_city (u, locale_zone_offset_at (ny, um), here, here_off);
	EQS (hm (c.hh, c.mm), "06:34"); EQS (diff (c.diff), "-6 h"); EQI (c.day, 0);
	c = clk_city (u, locale_zone_offset_at (ld, um), here, here_off);
	EQS (hm (c.hh, c.mm), "11:34"); EQS (diff (c.diff), "-1 h"); EQI (c.day, 0); EQI (c.ss, 0);
	c = clk_city (u + 41, locale_zone_offset_at (pa, um), here, here_off);
	EQS (hm (c.hh, c.mm), "12:34"); EQI (c.ss, 41); EQI (c.diff, 0); EQS (diff (c.diff), "");	// "Same time"
	// wall 23:30 in Brussels (21:30 UTC): Tokyo 06:30 tomorrow
	u = (long long) clk_minute (2026, 9, 28, 21, 30) * 60;
	c = clk_city (u, locale_zone_offset_at (tk, u / 60), here, 120);
	EQS (hm (c.hh, c.mm), "06:30"); EQI (c.day, 1);
	// wall 01:00 (23:00 UTC the day before): Los Angeles 16:00 yesterday
	u = (long long) clk_minute (2026, 9, 27, 23, 0) * 60;
	c = clk_city (u, locale_zone_offset_at (zone ("Los Angeles"), u / 60), here, 120);
	EQS (hm (c.hh, c.mm), "16:00"); EQI (c.day, -1); EQS (diff (c.diff), "-9 h");
	// a half-hour zone (synthetic: none in SystemKit's table)
	c = clk_city ((long long) clk_minute (2026, 9, 28, 10, 34) * 60, 330, here, 0);
	EQS (hm (c.hh, c.mm), "16:04"); EQS (diff (c.diff), "+5 h 30");
	// New York on 2026-11-01, whatever Brussels' day: at 05:30 UTC still EDT (01:30), at 06:30 EST (01:30 again)
	u = (long long) clk_minute (2026, 11, 1, 5, 30) * 60;
	c = clk_city (u, locale_zone_offset_at (ny, u / 60), clk_days (2026, 11, 1), 60);
	EQS (hm (c.hh, c.mm), "01:30"); EQS (diff (c.diff), "-5 h");
	u += 3600;
	c = clk_city (u, locale_zone_offset_at (ny, u / 60), clk_days (2026, 11, 1), 60);
	EQS (hm (c.hh, c.mm), "01:30"); EQS (diff (c.diff), "-6 h");
}

// The timer (AC-34): from the start tick, never from the frames
static void test_timer ()
{
	const long T = 123456;
	Timer t; timer_set (t, 600);
	EQI (t.state, TM_IDLE); EQI (timer_left_cs (t, T), 60000); EQS (tmr (timer_shown (t, T)), "10:00");
	timer_start (t, T);
	EQI (t.state, TM_RUNNING); EQI (timer_end_tick (t), T + 60000);
	EQS (tmr (timer_shown (t, T)), "10:00");
	EQS (tmr (timer_shown (t, T + 1)), "10:00");				// (rounded up)
	EQS (tmr (timer_shown (t, T + 100)), "09:59");
	EQS (tmr (timer_shown (t, T + 59940)), "00:01");			// T + 599 400 ms
	EQS (tmr (timer_shown (t, T + 59999)), "00:01");
	CHECK (!timer_due (t, T + 59999));
	CHECK (timer_due (t, T + 60000));					// T + 600 000 ms: it rings
	EQS (tmr (timer_shown (t, T + 60000)), "00:00");
	EQI (timer_left_cs (t, T + 70000), 0);
	// frames at irregular steps (1, 7, 33 ticks): the same time left as computed at once
	long now = T; int bad = 0, k = 0;
	const int stepv[3] = { 1, 7, 33 };
	while (now < T + 60000) { now += stepv[k++ % 3]; if (timer_left_cs (t, now) != (now >= T + 60000 ? 0 : T + 60000 - now)) bad++; }
	EQI (bad, 0);
	// pause, resume, reset
	timer_set (t, 300); timer_start (t, 1000);
	timer_pause (t, 1000 + 4550);						// 45.5 s gone
	EQI (t.state, TM_PAUSED); EQS (tmr (timer_shown (t, 99999)), "04:15");	// (held, whatever the ticks)
	timer_start (t, 20000);
	EQS (tmr (timer_shown (t, 20000 + 1450)), "04:00");
	EQI (timer_end_tick (t), 20000 + 25450);
	timer_reset (t); EQI (t.state, TM_IDLE); EQS (tmr (timer_shown (t, 0)), "05:00"); EQI (timer_end_tick (t), -1);
	timer_set (t, 0); timer_start (t, 5); EQI (t.state, TM_IDLE);		// (nothing set: does not start)
	timer_set (t, 999999); EQI (t.total_cs, 8639900);			// (clamped: 23:59:59)
	// taken back from a [timer] (end 12 000 ticks ahead, set 300): running, 02:00 shown
	timer_resume (t, 300, 1000 + 12000, 1000);
	EQI (t.state, TM_RUNNING); EQS (tmr (timer_shown (t, 1000)), "02:00"); EQI (timer_end_tick (t), 13000);
	EQI (t.total_cs, 30000);
	CHECK (timer_due (t, 13000) && !timer_due (t, 12999));
}

// The stopwatch (AC-37, AC-38, AC-39)
static Stopwatch g_w;
static void test_stopwatch ()
{
	Stopwatch &w = g_w;
	sw_reset (w);
	EQI (sw_total_cs (w, 500), 0); EQI (sw_lap (w, 500), 0);		// (stopped: no lap)
	sw_start (w, 0);
	EQI (sw_lap (w, 1234), 1); EQI (sw_lap (w, 2424), 2);
	EQI (sw_fastest (w), -1); EQI (sw_slowest (w), -1);			// (under 3 laps)
	EQI (sw_running_lap_cs (w, 3000), 576);
	EQI (sw_lap (w, 3700), 3);
	sw_stop (w, 3700);
	EQS (sw (sw_lap_time (w, 0)), "00:12.34"); EQS (sw (sw_lap_time (w, 1)), "00:11.90"); EQS (sw (sw_lap_time (w, 2)), "00:12.76");
	EQI (sw_lap_time (w, 0) + sw_lap_time (w, 1) + sw_lap_time (w, 2), sw_total_cs (w, 99999));	// the sum = the total
	EQI (sw_fastest (w), 1); EQI (sw_slowest (w), 2);			// lap 2 fastest, lap 3 slowest
	static char txt[4096];
	int n = sw_laps_text (w, "Lap", "Lap time", "Total", txt, sizeof txt);
	EQS (txt, "Lap\tLap time\tTotal\n1\t00:12.34\t00:12.34\n2\t00:11.90\t00:24.24\n3\t00:12.76\t00:37.00\n");
	EQI (n, (long) strlen (txt));
	sw_laps_text (w, "Tour", "Temps du tour", "Total", txt, sizeof txt);
	CHECK (!strncmp (txt, "Tour\tTemps du tour\tTotal\n1\t", 27));
	// stopped then started again: the time goes on from where it was
	sw_start (w, 10000); EQI (sw_total_cs (w, 10100), 3800);
	sw_stop (w, 10100); sw_stop (w, 20000); EQI (sw_total_cs (w, 30000), 3800);
	// 999 laps at most; the first of equals is the fastest
	sw_reset (w); sw_start (w, 0);
	for (int i = 1; i <= SW_MAX_LAPS; i++) sw_lap (w, i * 100L);
	EQI (w.nlaps, SW_MAX_LAPS); EQI (sw_lap (w, 999999), 0);
	EQI (sw_fastest (w), 0); EQI (sw_slowest (w), 0);
	EQS (sw (sw_total_cs (w, 360000)), "1:00:00.00");
	sw_reset (w); EQI (w.nlaps, 0); CHECK (!w.running);
}

int main ()
{
	test_dates ();
	test_texts ();
	test_world ();
	test_timer ();
	test_stopwatch ();
	if (g_fails) { printf ("clocktime: FAIL (%d of %d checks)\n", g_fails, g_checks); return 1; }
	printf ("clocktime: %d checks passed\n", g_checks);
	return 0;
}
