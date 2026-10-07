//
// clocktime.h -- the Clock's time arithmetic, UI-free and without the kernel (the Clock and clockd link it; the PC
// tests run it alone with a fake "now"): wall dates and minutes, the World tab's rows, the timer and the stopwatch
// counted from the ticks (100 a second: kapi_get_ticks), and the texts they are shown as.
//
// "Wall" time = the local time the menu bar shows (kapi_get_datetime), counted as if it were UTC: a wall minute is
// clk_days () * 1440 + h * 60 + mi -- no time zone in it, calendar days for the alarms. A tick is a hundredth of a
// second (kapi_get_ticks); the ticks are given as long, the hundredths ("cs") too.
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
#ifndef CLOCK_CLOCKTIME_H
#define CLOCK_CLOCKTIME_H

// ---- wall dates and minutes ---------------------------------------------------------------------------------
long clk_days (int y, int mo, int d);			// the days from 1970-01-01 (negative before)
void clk_civil (long days, int *y, int *mo, int *d);	// back to a date (any pointer may be 0)
int  clk_wday (long days);				// 0 Monday ... 6 Sunday (the alarms' AL_* bit: 1 << clk_wday)
long clk_minute (int y, int mo, int d, int h, int mi);	// the wall minute: clk_days * 1440 + h * 60 + mi
long clk_day_of (long minute);				// its day (floored)
int  clk_valid_date (int y, int mo, int d);		// 1: a real date (1970..9999)

// The stored stamps (alarms.txt): a day "YYYYMMDD", a minute "YYYYMMDDHHMM" -> -1 when not one (empty included).
long clk_parse_day (const char *s);
long clk_parse_minute (const char *s);
void clk_fmt_day (long day, char *out, int cap);	// "20260929" ("" for -1)
void clk_fmt_minute (long minute, char *out, int cap);	// "202609290710" ("" for -1)
// "HH:MM" (also "H:MM"), 00:00..23:59 -> 1 and *hh / *mm, 0 otherwise ("25:99", "7", "")
int  clk_parse_hm (const char *s, int *hh, int *mm);

// ---- texts -------------------------------------------------------------------------------------------------
void clk_fmt_hm (int h, int m, char *out, int cap);		// "07:00"
void clk_fmt_hms (int h, int m, int s, char *out, int cap);	// "12:34:00"
// An offset from here (minutes) -> "+7 h", "-6 h", "+5 h 30", "-3 h 30"; 0 -> "" (the UI says "Same time")
void clk_fmt_diff (int diff, char *out, int cap);
void clk_fmt_utc (int offset, char *out, int cap);		// "UTC+2", "UTC-3:30", "UTC" (as SystemKit's locale_zone_utc)
void clk_fmt_timer (long seconds, char *out, int cap);		// "05:00", "59:59", "1:00:00"
void clk_fmt_sw (long cs, char *out, int cap);			// "00:12.34", "59:59.99", "1:00:00.00"

// ---- the World tab: a city's row ---------------------------------------------------------------------------
struct ClkCity
{
	int hh, mm, ss;		// the city's wall time
	int day;		// its day against here's: -1 yesterday, 0 today, 1 tomorrow (more: a far zone, never with real ones)
	int diff;		// its offset minus here's, minutes (0: the same time)
};
// utc_s: UTC now (seconds since 1970); city_off: the city's offset then (SystemKit's locale_zone_offset_at);
// here_day: here's wall day (clk_days of kapi_get_datetime's date).
ClkCity clk_city (long long utc_s, int city_off, long here_day, int here_off);

// ---- the timer (02 #27-30): counted from the ticks, never from frames --------------------------------------
enum { TM_IDLE, TM_RUNNING, TM_PAUSED };
struct Timer
{
	int  state;		// TM_*
	long total_cs;		// the time set (hundredths)
	long start_tick;	// RUNNING: the tick it (re)started at
	long left_cs;		// RUNNING: what was left at start_tick; PAUSED: what is left
};
void timer_set (Timer &t, long seconds);			// idle, that time set (0..86 399 s; outside: clamped)
void timer_start (Timer &t, long now_tick);			// idle: from the time set; paused: resumed; running: nothing
void timer_pause (Timer &t, long now_tick);			// running -> paused (the time left kept)
void timer_reset (Timer &t);					// idle, back to the time set
long timer_left_cs (const Timer &t, long now_tick);		// what is left, >= 0 (idle: the time set)
long timer_shown (const Timer &t, long now_tick);		// what is shown, seconds, rounded UP (00:01 until it is 0)
bool timer_due (const Timer &t, long now_tick);			// running and nothing left: it rings
long timer_end_tick (const Timer &t);				// running: the tick it reaches 0 (the hand-over's end =)
// A timer taken back (the [timer] of alarms.txt): running, set_s the time set, reaching 0 at end_tick (>= now).
void timer_resume (Timer &t, long set_s, long end_tick, long now_tick);

// ---- the stopwatch (02 #31-33) -----------------------------------------------------------------------------
#define SW_MAX_LAPS	999
struct Stopwatch
{
	bool running;
	long start_tick;	// running: the tick of the last Start
	long base_cs;		// the time before the last Start
	int  nlaps;
	long laps[SW_MAX_LAPS];	// the laps' TOTALS, in order (lap i's time: sw_lap_time)
};
void sw_reset (Stopwatch &w);					// stopped at zero, no laps
void sw_start (Stopwatch &w, long now_tick);			// (running: nothing)
void sw_stop (Stopwatch &w, long now_tick);			// (stopped: nothing)
long sw_total_cs (const Stopwatch &w, long now_tick);		// the time shown
int  sw_lap (Stopwatch &w, long now_tick);			// a lap at now -> its number (1-based), 0 (not running / 999 laps)
long sw_lap_time (const Stopwatch &w, int i);			// lap i's (0-based) own time: its total - the previous one
long sw_running_lap_cs (const Stopwatch &w, long now_tick);	// the lap running: the time since the last lap
int  sw_fastest (const Stopwatch &w);				// the fastest lap (0-based; the first of equals), -1 under 3 laps
int  sw_slowest (const Stopwatch &w);				// the slowest, -1 under 3 laps
// The laps as text for the clipboard (02 #33): the header line "<lap>\t<lap time>\t<total>" with the words given
// (the UI's, translated), then one line a lap, oldest first: "1\t00:12.34\t00:12.34" -> the length (cut at cap - 1).
int  sw_laps_text (const Stopwatch &w, const char *h_lap, const char *h_time, const char *h_total, char *out, int cap);

#endif
