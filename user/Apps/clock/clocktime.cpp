//
// clocktime.cpp -- the Clock's time arithmetic (clocktime.h): no UI, no kernel call.
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
#include "clocktime.h"

// ---- small text helpers (no C library needed) ----
namespace {
struct Out
{
	char *d; int cap, n;
	Out (char *o, int c) : d (o), cap (c), n (0) { if (cap > 0) d[0] = 0; }
	void ch (char c) { if (n < cap - 1) { d[n++] = c; d[n] = 0; } }
	void str (const char *s) { while (s && *s) ch (*s++); }
	void num (long v, int width = 1)		// v >= 0, at least width digits
	{
		char t[24]; int k = 0;
		do { t[k++] = (char) ('0' + v % 10); v /= 10; } while (v > 0 && k < 23);
		while (k < width && k < 23) t[k++] = '0';
		while (k > 0) ch (t[--k]);
	}
};
inline long floordiv (long a, long b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
inline long floormod (long a, long b) { return a - floordiv (a, b) * b; }
// n digits of s from position 0 -> the number, -1 when not all digits
long digits (const char *s, int n)
{
	long v = 0;
	for (int i = 0; i < n; i++) { if (s[i] < '0' || s[i] > '9') return -1; v = v * 10 + (s[i] - '0'); }
	return v;
}
int slen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
}

// ---- wall dates and minutes ----
long clk_days (int y, int mo, int d)			// H. Hinnant's days_from_civil
{
	long yy = y - (mo <= 2), era = (yy >= 0 ? yy : yy - 399) / 400, yoe = yy - era * 400;
	long doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}
void clk_civil (long days, int *y, int *mo, int *d)	// civil_from_days
{
	long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
	long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	long mp = (5 * doy + 2) / 153, dd = doy - (153 * mp + 2) / 5 + 1, mm = mp < 10 ? mp + 3 : mp - 9;
	if (y) *y = (int) (yoe + era * 400 + (mm <= 2));
	if (mo) *mo = (int) mm;
	if (d) *d = (int) dd;
}
int clk_wday (long days) { return (int) floormod (days + 3, 7); }	// 1970-01-01 was a Thursday
long clk_minute (int y, int mo, int d, int h, int mi) { return clk_days (y, mo, d) * 1440 + h * 60 + mi; }
long clk_day_of (long minute) { return floordiv (minute, 1440); }
int clk_valid_date (int y, int mo, int d)
{
	if (y < 1970 || y > 9999 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
	int yy, mm, dd;
	clk_civil (clk_days (y, mo, d), &yy, &mm, &dd);
	return yy == y && mm == mo && dd == d;
}
long clk_parse_day (const char *s)
{
	if (slen (s) != 8) return -1;
	long v = digits (s, 8);
	if (v < 0) return -1;
	int y = (int) (v / 10000), mo = (int) (v / 100 % 100), d = (int) (v % 100);
	return clk_valid_date (y, mo, d) ? clk_days (y, mo, d) : -1;
}
long clk_parse_minute (const char *s)
{
	if (slen (s) != 12) return -1;
	long hm = digits (s + 8, 4);
	char day[9];
	for (int i = 0; i < 8; i++) day[i] = s[i];
	day[8] = 0;
	long d = clk_parse_day (day);
	if (d < 0 || hm < 0 || hm / 100 > 23 || hm % 100 > 59) return -1;
	return d * 1440 + hm / 100 * 60 + hm % 100;
}
void clk_fmt_day (long day, char *out, int cap)
{
	Out o (out, cap);
	if (day < 0) return;
	int y, mo, d;
	clk_civil (day, &y, &mo, &d);
	o.num (y, 4); o.num (mo, 2); o.num (d, 2);
}
void clk_fmt_minute (long minute, char *out, int cap)
{
	Out o (out, cap);
	if (minute < 0) return;
	int y, mo, d;
	clk_civil (minute / 1440, &y, &mo, &d);
	o.num (y, 4); o.num (mo, 2); o.num (d, 2); o.num (minute % 1440 / 60, 2); o.num (minute % 60, 2);
}
int clk_parse_hm (const char *s, int *hh, int *mm)
{
	int n = slen (s), c = n == 5 ? 2 : n == 4 ? 1 : -1;
	if (c < 0 || s[c] != ':') return 0;
	long h = digits (s, c), m = digits (s + c + 1, 2);
	if (h < 0 || m < 0 || h > 23 || m > 59) return 0;
	if (hh) *hh = (int) h;
	if (mm) *mm = (int) m;
	return 1;
}

// ---- texts ----
void clk_fmt_hm (int h, int m, char *out, int cap) { Out o (out, cap); o.num (h, 2); o.ch (':'); o.num (m, 2); }
void clk_fmt_hms (int h, int m, int s, char *out, int cap)
{
	Out o (out, cap);
	o.num (h, 2); o.ch (':'); o.num (m, 2); o.ch (':'); o.num (s, 2);
}
void clk_fmt_diff (int diff, char *out, int cap)
{
	Out o (out, cap);
	if (!diff) return;
	int a = diff < 0 ? -diff : diff;
	o.ch (diff < 0 ? '-' : '+'); o.num (a / 60); o.str (" h");
	if (a % 60) { o.ch (' '); o.num (a % 60, 2); }
}
void clk_fmt_utc (int offset, char *out, int cap)
{
	Out o (out, cap);
	int a = offset < 0 ? -offset : offset;
	o.str ("UTC");
	if (!offset) return;
	o.ch (offset < 0 ? '-' : '+'); o.num (a / 60);
	if (a % 60) { o.ch (':'); o.num (a % 60, 2); }
}
void clk_fmt_timer (long seconds, char *out, int cap)
{
	Out o (out, cap);
	if (seconds < 0) seconds = 0;
	if (seconds >= 3600) { o.num (seconds / 3600); o.ch (':'); }
	o.num (seconds / 60 % 60, 2); o.ch (':'); o.num (seconds % 60, 2);
}
void clk_fmt_sw (long cs, char *out, int cap)
{
	Out o (out, cap);
	if (cs < 0) cs = 0;
	long s = cs / 100;
	if (s >= 3600) { o.num (s / 3600); o.ch (':'); }
	o.num (s / 60 % 60, 2); o.ch (':'); o.num (s % 60, 2); o.ch ('.'); o.num (cs % 100, 2);
}

// ---- the World tab ----
ClkCity clk_city (long long utc_s, int city_off, long here_day, int here_off)
{
	ClkCity c;
	long long w = utc_s + city_off * 60LL;			// the city's wall second
	long long day = w >= 0 ? w / 86400 : -((-w + 86399) / 86400), sec = w - day * 86400;
	c.hh = (int) (sec / 3600); c.mm = (int) (sec / 60 % 60); c.ss = (int) (sec % 60);
	c.day = (int) (day - here_day);
	c.diff = city_off - here_off;
	return c;
}

// ---- the timer ----
void timer_set (Timer &t, long seconds)
{
	if (seconds < 0) seconds = 0;
	if (seconds > 86399) seconds = 86399;
	t.state = TM_IDLE; t.total_cs = seconds * 100; t.start_tick = 0; t.left_cs = t.total_cs;
}
void timer_start (Timer &t, long now_tick)
{
	if (t.state == TM_RUNNING) return;
	if (t.state == TM_IDLE) t.left_cs = t.total_cs;
	if (t.left_cs <= 0) return;				// (nothing set)
	t.state = TM_RUNNING; t.start_tick = now_tick;
}
long timer_left_cs (const Timer &t, long now_tick)
{
	if (t.state == TM_IDLE) return t.total_cs;
	if (t.state == TM_PAUSED) return t.left_cs;
	long l = t.left_cs - (now_tick - t.start_tick);
	return l > 0 ? l : 0;
}
void timer_pause (Timer &t, long now_tick)
{
	if (t.state != TM_RUNNING) return;
	t.left_cs = timer_left_cs (t, now_tick); t.state = TM_PAUSED;
}
void timer_reset (Timer &t) { t.state = TM_IDLE; t.left_cs = t.total_cs; t.start_tick = 0; }
long timer_shown (const Timer &t, long now_tick) { return (timer_left_cs (t, now_tick) + 99) / 100; }
bool timer_due (const Timer &t, long now_tick) { return t.state == TM_RUNNING && now_tick - t.start_tick >= t.left_cs; }
long timer_end_tick (const Timer &t) { return t.state == TM_RUNNING ? t.start_tick + t.left_cs : -1; }
void timer_resume (Timer &t, long set_s, long end_tick, long now_tick)
{
	timer_set (t, set_s);
	long left = end_tick - now_tick;
	if (left < 0) left = 0;
	if (left > t.total_cs) t.total_cs = left;		// (never more left than the time set)
	t.state = TM_RUNNING; t.start_tick = now_tick; t.left_cs = left;
}

// ---- the stopwatch ----
void sw_reset (Stopwatch &w) { w.running = false; w.start_tick = 0; w.base_cs = 0; w.nlaps = 0; }
void sw_start (Stopwatch &w, long now_tick) { if (!w.running) { w.running = true; w.start_tick = now_tick; } }
long sw_total_cs (const Stopwatch &w, long now_tick)
{
	long t = w.base_cs + (w.running ? now_tick - w.start_tick : 0);
	return t > 0 ? t : 0;
}
void sw_stop (Stopwatch &w, long now_tick) { if (w.running) { w.base_cs = sw_total_cs (w, now_tick); w.running = false; } }
int sw_lap (Stopwatch &w, long now_tick)
{
	if (!w.running || w.nlaps >= SW_MAX_LAPS) return 0;
	w.laps[w.nlaps++] = sw_total_cs (w, now_tick);
	return w.nlaps;
}
long sw_lap_time (const Stopwatch &w, int i)
{
	if (i < 0 || i >= w.nlaps) return 0;
	return w.laps[i] - (i > 0 ? w.laps[i - 1] : 0);
}
long sw_running_lap_cs (const Stopwatch &w, long now_tick)
{
	return sw_total_cs (w, now_tick) - (w.nlaps > 0 ? w.laps[w.nlaps - 1] : 0);
}
static int sw_pick (const Stopwatch &w, int sign)
{
	if (w.nlaps < 3) return -1;
	int b = 0;
	for (int i = 1; i < w.nlaps; i++) if ((sw_lap_time (w, i) - sw_lap_time (w, b)) * sign < 0) b = i;
	return b;
}
int sw_fastest (const Stopwatch &w) { return sw_pick (w, 1); }
int sw_slowest (const Stopwatch &w) { return sw_pick (w, -1); }
int sw_laps_text (const Stopwatch &w, const char *h_lap, const char *h_time, const char *h_total, char *out, int cap)
{
	Out o (out, cap);
	char t[24];
	o.str (h_lap); o.ch ('\t'); o.str (h_time); o.ch ('\t'); o.str (h_total); o.ch ('\n');
	for (int i = 0; i < w.nlaps; i++)
	{
		o.num (i + 1); o.ch ('\t');
		clk_fmt_sw (sw_lap_time (w, i), t, sizeof t); o.str (t); o.ch ('\t');
		clk_fmt_sw (w.laps[i], t, sizeof t); o.str (t); o.ch ('\n');
	}
	return o.n;
}
