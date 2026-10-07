//
// alarms.cpp -- the Clock's alarms (alarms.h): alarms.txt read and written through FileKit's fk_kv, the next ring,
// the [timer] handed over, the Ringer. No UI; the kernel only through FileKit (the file).
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
#include "alarms.h"
#include "clocktime.h"

namespace {
bool seq (const char *a, const char *b)
{
	if (!a) a = "";
	if (!b) b = "";
	while (*a && *a == *b) { a++; b++; }
	return *a == *b;
}
void scopy (char *d, int cap, const char *s)
{
	int n = 0;
	if (cap <= 0) return;
	while (s && s[n] && n < cap - 1) { d[n] = s[n]; n++; }
	d[n] = 0;
}
// a whole decimal number, an optional '-' first -> 1 and *v; 0 otherwise (empty, letters, spaces)
bool parse_ll (const char *s, long long *v)
{
	bool neg = s && *s == '-';
	long long r = 0;
	if (neg) s++;
	if (!s || !*s) return false;
	for (; *s; s++)
	{
		if (*s < '0' || *s > '9' || r > 100000000000000000LL) return false;
		r = r * 10 + (*s - '0');
	}
	*v = neg ? -r : r;
	return true;
}
void fmt_ll (long long v, char *out, int cap)
{
	char t[24]; int k = 0, n = 0;
	bool neg = v < 0;
	unsigned long long u = neg ? 0ULL - (unsigned long long) v : (unsigned long long) v;
	do { t[k++] = (char) ('0' + u % 10); u /= 10; } while (u && k < 23);
	if (neg && n < cap - 1) out[n++] = '-';
	while (k > 0 && n < cap - 1) out[n++] = t[--k];
	if (cap > 0) out[n] = 0;
}
const char *const DAY[7] = { "mon", "tue", "wed", "thu", "fri", "sat", "sun" };
// the keys alarms.cpp writes itself (the others of a block are copied)
const char *const ALARM_KEYS[] = { "id", "time", "label", "on", "days", "date", "sound", "snooze", "missed", 0 };
const char *const TIMER_KEYS[] = { "end", "set", "label", "utc", 0 };
const char *const NO_KEYS[] = { 0 };
bool known (const char *const *keys, const char *k) { for (; *keys; keys++) if (seq (*keys, k)) return true; return false; }
// block b of the document read: its entries whose keys are not ours, set in block nb of the new document
void copy_unknown (const fk_kv *from, int b, fk_kv *to, int nb, const char *const *keys)
{
	if (!from || b <= 0) return;
	for (int i = 0; i < fk_kv_count (from); i++)
		if (fk_kv_block (from, i) == b && !known (keys, fk_kv_key (from, i)))
			fk_kv_block_set (to, nb, fk_kv_key (from, i), fk_kv_value (from, i));
}
void set_num (fk_kv *kv, int b, const char *key, long long v)
{
	char t[24];
	fmt_ll (v, t, sizeof t);
	fk_kv_block_set (kv, b, key, t);
}
}

// ---- reading ----
void alarms_init (AlarmSet &s)
{
	s.n = 0; s.next_id = 1; s.doc = 0;
	alarms_timer_clear (s);
	s.timer_block = 0;
}
void alarms_free (AlarmSet &s) { fk_kv_free (s.doc); alarms_init (s); }

static void parse_alarm (const fk_kv *kv, int b, Alarm &a)
{
	long long v;
	a.block = b;
	a.id = parse_ll (fk_kv_block_get (kv, b, "id", ""), &v) && v > 0 && v < 1000000000 ? (int) v : 0;
	a.valid = clk_parse_hm (fk_kv_block_get (kv, b, "time", ""), &a.hh, &a.mm) != 0;
	if (!a.valid) a.hh = a.mm = 0;
	a.on = !seq (fk_kv_block_get (kv, b, "on", "1"), "0");
	a.days = alarms_days_parse (fk_kv_block_get (kv, b, "days", ""));
	a.date = clk_parse_day (fk_kv_block_get (kv, b, "date", ""));
	alarm_set_label (a, fk_kv_block_get (kv, b, "label", ""));
	scopy (a.sound, sizeof a.sound, alarm_sound (fk_kv_block_get (kv, b, "sound", "")));
	a.snooze = clk_parse_minute (fk_kv_block_get (kv, b, "snooze", ""));
	a.missed = clk_parse_minute (fk_kv_block_get (kv, b, "missed", ""));
}
static bool parse_timer (AlarmSet &s, const fk_kv *kv, int b)
{
	long long end, set, utc;
	if (!parse_ll (fk_kv_block_get (kv, b, "end", ""), &end) || end < 0 ||
	    !parse_ll (fk_kv_block_get (kv, b, "set", ""), &set) || set <= 0 || set > 86399) return false;
	if (!parse_ll (fk_kv_block_get (kv, b, "utc", ""), &utc) || utc < 0) utc = -1;
	s.timer = true; s.timer_end_tick = (long) end; s.timer_set = (int) set; s.timer_utc = utc; s.timer_block = b;
	scopy (s.timer_label, sizeof s.timer_label, fk_kv_block_get (kv, b, "label", ""));
	return true;
}
// the set from a document (taken: freed by alarms_free)
static int from_doc (AlarmSet &s, fk_kv *kv)
{
	alarms_free (s);
	s.doc = kv;
	if (!kv) return 0;
	for (int b = 1; b <= fk_kv_blocks (kv); b++)
	{
		const char *nm = fk_kv_block_name (kv, b);
		if (seq (nm, "alarm") && s.n < ALARMS_MAX) parse_alarm (kv, b, s.a[s.n++]);
		else if (seq (nm, "timer") && !s.timer) parse_timer (s, kv, b);
	}
	// the ids: unique, the next one above them all; a missing or repeated one given a new number
	for (int i = 0; i < s.n; i++) if (s.a[i].id >= s.next_id) s.next_id = s.a[i].id + 1;
	for (int i = 0; i < s.n; i++)
	{
		bool dup = s.a[i].id <= 0;
		for (int j = 0; j < i && !dup; j++) dup = s.a[j].id == s.a[i].id;
		if (dup) s.a[i].id = s.next_id++;
	}
	return s.n;
}
int alarms_parse (AlarmSet &s, const char *text) { return from_doc (s, fk_kv_parse (text ? text : "", 0)); }
int alarms_load (AlarmSet &s, const char *path) { return from_doc (s, fk_kv_load (path, 0)); }

// ---- writing ----
int alarms_save (AlarmSet &s, const char *path)
{
	fk_kv *kv = fk_kv_new (0), *old = s.doc;
	char t[64];
	int blk[ALARMS_MAX], tb = 0;
	if (!kv) return -1;
	if (old)						// what comes before any [block], kept
		for (int i = 0; i < fk_kv_count (old); i++)
			if (fk_kv_block (old, i) == 0) fk_kv_set (kv, "", fk_kv_key (old, i), fk_kv_value (old, i));
	for (int i = 0; i < s.n; i++)
	{
		const Alarm &a = s.a[i];
		int b = blk[i] = fk_kv_block_new (kv, "alarm");
		if (b < 0) { fk_kv_free (kv); return -1; }
		set_num (kv, b, "id", a.id);
		if (a.valid) clk_fmt_hm (a.hh, a.mm, t, sizeof t);
		fk_kv_block_set (kv, b, "time", a.valid ? t : fk_kv_block_get (old, a.block, "time", ""));	// (an invalid one: as read)
		fk_kv_block_set (kv, b, "label", a.label);
		fk_kv_block_set (kv, b, "on", a.on ? "1" : "0");
		alarms_days_text (a.days, t, sizeof t); fk_kv_block_set (kv, b, "days", t);
		clk_fmt_day (a.days ? -1 : a.date, t, sizeof t); fk_kv_block_set (kv, b, "date", t);
		fk_kv_block_set (kv, b, "sound", alarm_sound (a.sound));
		clk_fmt_minute (a.snooze, t, sizeof t); fk_kv_block_set (kv, b, "snooze", t);
		if (a.missed >= 0) { clk_fmt_minute (a.missed, t, sizeof t); fk_kv_block_set (kv, b, "missed", t); }
		copy_unknown (old, a.block, kv, b, ALARM_KEYS);
	}
	if (s.timer)
	{
		tb = fk_kv_block_new (kv, "timer");
		if (tb < 0) { fk_kv_free (kv); return -1; }
		set_num (kv, tb, "end", s.timer_end_tick);
		set_num (kv, tb, "set", s.timer_set);
		fk_kv_block_set (kv, tb, "label", s.timer_label);
		if (s.timer_utc >= 0) set_num (kv, tb, "utc", s.timer_utc);
		copy_unknown (old, s.timer_block, kv, tb, TIMER_KEYS);
	}
	if (old)						// the blocks of other names, kept as they were
		for (int b = 1; b <= fk_kv_blocks (old); b++)
		{
			const char *nm = fk_kv_block_name (old, b);
			if (seq (nm, "alarm") || seq (nm, "timer")) continue;
			int nb = fk_kv_block_new (kv, nm);
			if (nb < 0) { fk_kv_free (kv); return -1; }
			copy_unknown (old, b, kv, nb, NO_KEYS);		// (no key of ours: all copied)
		}
	if (fk_kv_save (kv, path, "# Clock -- the alarms (written by the Clock app, rung by clockd). One [alarm] block an alarm.") != 0)
	{
		fk_kv_free (kv);
		return -1;
	}
	for (int i = 0; i < s.n; i++) s.a[i].block = blk[i];	// re-based on what was written
	s.timer_block = s.timer ? tb : 0;
	fk_kv_free (old);
	s.doc = kv;
	return 0;
}

// ---- one alarm ----
void alarm_new (AlarmSet &s, Alarm &a)
{
	a.id = s.next_id; a.valid = true; a.hh = 7; a.mm = 0; a.on = true; a.days = 0; a.date = -1;
	a.label[0] = 0; scopy (a.sound, sizeof a.sound, "chimes"); a.snooze = -1; a.missed = -1; a.block = 0;
}
int alarm_add (AlarmSet &s, const Alarm &a)
{
	if (s.n >= ALARMS_MAX) return -1;
	s.a[s.n] = a;
	if (s.a[s.n].id <= 0 || alarm_find (s, s.a[s.n].id) >= 0) s.a[s.n].id = s.next_id;
	if (s.a[s.n].id >= s.next_id) s.next_id = s.a[s.n].id + 1;
	return s.n++;
}
int alarm_find (const AlarmSet &s, int id)
{
	for (int i = 0; i < s.n; i++) if (s.a[i].id == id) return i;
	return -1;
}
int alarm_remove (AlarmSet &s, int id)
{
	int i = alarm_find (s, id);
	if (i < 0) return 0;
	for (; i + 1 < s.n; i++) s.a[i] = s.a[i + 1];
	s.n--;
	return 1;
}
void alarm_set_label (Alarm &a, const char *u)
{
	int n = 0, chars = 0;
	for (; u && *u && n < (int) sizeof a.label - 1; u++)
	{
		unsigned char c = (unsigned char) *u;
		if ((c & 0xC0) != 0x80)				// a character starts
		{
			if (chars == ALARM_LABEL_MAX) break;
			int len = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
			if (n + len > (int) sizeof a.label - 1) break;
			chars++;
		}
		a.label[n++] = c == '\n' || c == '\r' || c == '\t' ? ' ' : (char) c;
	}
	a.label[n] = 0;
}
const char *alarm_sound (const char *t)
{
	if (seq (t, "beeps")) return "beeps";
	if (seq (t, "marimba")) return "marimba";
	return "chimes";
}
unsigned alarms_days_parse (const char *v)
{
	unsigned m = 0;
	while (v && *v)
	{
		while (*v == ' ' || *v == '\t' || *v == ',') v++;
		char w[8]; int k = 0;
		while (*v && *v != ' ' && *v != '\t' && *v != ',')
		{
			char c = *v++;
			if (k < 7) w[k++] = c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
		}
		w[k] = 0;
		for (int d = 0; d < 7; d++) if (seq (w, DAY[d])) m |= 1u << d;
	}
	return m;
}
void alarms_days_text (unsigned m, char *out, int cap)
{
	int n = 0;
	if (cap > 0) out[0] = 0;
	for (int d = 0; d < 7; d++)
		if (m & (1u << d))
		{
			if (n && n < cap - 1) out[n++] = ' ';
			for (const char *p = DAY[d]; *p && n < cap - 1; p++) out[n++] = *p;
			if (cap > 0) out[n] = 0;
		}
}
int alarm_repeat_kind (unsigned days)
{
	days &= AL_EVERYDAY;
	return !days ? REP_ONCE : days == AL_EVERYDAY ? REP_DAILY : days == AL_WEEKDAYS ? REP_WEEKDAYS
	     : days == AL_WEEKENDS ? REP_WEEKENDS : REP_CUSTOM;
}

// ---- when ----
long alarm_once_day (int hh, int mm, long now_min)
{
	long day = clk_day_of (now_min);
	return hh * 60 + mm > now_min - day * 1440 ? day : day + 1;
}
long alarm_next (const Alarm &a, long from_min)
{
	if (!a.valid) return -1;
	int hm = a.hh * 60 + a.mm;
	if (!(a.days & AL_EVERYDAY))
	{
		if (a.date < 0) return -1;
		long m = a.date * 1440 + hm;
		return m >= from_min ? m : -1;
	}
	long day = clk_day_of (from_min);
	for (int k = 0; k <= 7; k++)				// (calendar days: a summer-time night is a day like any other)
		if (a.days & (1u << clk_wday (day + k)))
		{
			long m = (day + k) * 1440 + hm;
			if (m >= from_min) return m;
		}
	return -1;
}
long alarms_next (const AlarmSet &s, long now_min, int *idx)
{
	long best = -1;
	if (idx) *idx = -1;
	for (int i = 0; i < s.n; i++)
	{
		const Alarm &a = s.a[i];
		if (!a.on || !a.valid) continue;
		long m = alarm_next (a, now_min + 1);
		if (a.snooze > now_min && (m < 0 || a.snooze < m)) m = a.snooze;
		if (m >= 0 && (best < 0 || m < best)) { best = m; if (idx) *idx = i; }
	}
	return best;
}
bool alarm_passed (const Alarm &a, long now_min)
{
	if ((a.days & AL_EVERYDAY) || a.date < 0 || !a.valid) return false;
	if (a.snooze >= now_min) return false;			// (snoozed: not over yet)
	return a.date * 1440 + a.hh * 60 + a.mm < now_min;
}
void alarm_snooze (Alarm &a, long now_min, int minutes) { a.snooze = now_min + (minutes > 0 ? minutes : 1); }
void alarm_stop (Alarm &a)
{
	a.snooze = -1; a.missed = -1;
	if (!(a.days & AL_EVERYDAY)) a.on = false;
}

// ---- the [timer] ----
int alarms_timer_state (const AlarmSet &s, long now_tick, long long now_utc)
{
	if (!s.timer) return TMR_NONE;
	long d = s.timer_end_tick - now_tick;			// ticks still to come (negative: past)
	if (d > s.timer_set * 100L + 100) return TMR_STALE;	// (not of this boot)
	if (s.timer_utc >= 0 && now_utc >= 0)
	{
		long long off = s.timer_utc - (now_utc + d / 100);
		if (off > 5 || off < -5) return TMR_STALE;
	}
	return d > 0 ? TMR_PENDING : TMR_DUE;
}
void alarms_timer_put (AlarmSet &s, long end_tick, int set_s, long long end_utc, const char *label)
{
	s.timer = true; s.timer_end_tick = end_tick; s.timer_set = set_s; s.timer_utc = end_utc;
	scopy (s.timer_label, sizeof s.timer_label, label);
	for (char *p = s.timer_label; *p; p++) if (*p == '\n' || *p == '\r') *p = ' ';
}
void alarms_timer_clear (AlarmSet &s)
{
	s.timer = false; s.timer_end_tick = 0; s.timer_set = 0; s.timer_utc = -1; s.timer_label[0] = 0;
}

// ---- the Ringer ----
void ringer_start (Ringer &r, long now_min) { r.last = now_min; r.started = true; r.timer_rung = -1; }
int ringer_step (Ringer &r, const AlarmSet &s, long now_min, bool trusted, long now_tick, long long now_utc, Due *out, int max)
{
	int n = 0;
	bool first = !r.started;
	if (first) ringer_start (r, now_min);			// (what is past stays past: the agenda's rule)
	// the [timer]: on the ticks, which never jump -- neither the start nor the trust gates it; once an end
	if (alarms_timer_state (s, now_tick, now_utc) == TMR_DUE && s.timer_end_tick != r.timer_rung && n < max)
	{
		out[n].id = ALARM_TIMER; out[n].minute = now_min; out[n].snooze = false; n++;
		r.timer_rung = s.timer_end_tick;
	}
	if (first) return n;
	if (!trusted) { r.last = now_min; return n; }		// (no real date yet, the boot guard)
	if (now_min == r.last) return n;
	if (now_min < r.last)					// back: the clock set (> 2 h) or the summer time ending
	{
		if (r.last - now_min > 120) r.last = now_min;
		return n;					// (a small step back: the same minutes not rung twice)
	}
	long from = r.last + 1 > now_min - 2 ? r.last + 1 : now_min - 2;	// (nothing older than 2 minutes)
	for (int i = 0; i < s.n && n < max; i++)
	{
		const Alarm &a = s.a[i];
		if (!a.on || !a.valid) continue;
		long m = alarm_next (a, from);
		if (m >= 0 && m <= now_min) { out[n].id = a.id; out[n].minute = m; out[n].snooze = false; n++; }
		else if (a.snooze >= from && a.snooze <= now_min) { out[n].id = a.id; out[n].minute = a.snooze; out[n].snooze = true; n++; }
	}
	r.last = now_min;
	return n;
}
int alarms_missed_since (const AlarmSet &s, long since_min, long upto_min, Due *out, int max)
{
	int n = 0;
	for (int i = 0; i < s.n && n < max; i++)
	{
		const Alarm &a = s.a[i];
		if (!a.on || !a.valid || (a.days & AL_EVERYDAY) || a.date < 0) continue;
		if (a.snooze > upto_min) continue;			// (snoozed: still to come)
		long m = a.date * 1440 + a.hh * 60 + a.mm;
		bool sn = a.snooze >= 0 && a.snooze > m;		// (snoozed, and that snooze went by too)
		if (sn) m = a.snooze;
		if (m < since_min || m > upto_min || a.missed >= m) continue;
		out[n].id = a.id; out[n].minute = m; out[n].snooze = sn; n++;
	}
	return n;
}
