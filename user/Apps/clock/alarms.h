//
// alarms.h -- the Clock's alarms (SD:/apps/clock.app/alarms.txt), UI-free: the model the Clock edits and clockd
// rings, the next time an alarm rings, and the Ringer -- "each alarm minute rung once, within 2 minutes" -- that
// clockd steps every half second. Linked by the Clock and clockd; the PC tests run it with a fake "now".
//
// alarms.txt (FileKit's fk_kv text: written by the Clock only, clockd reads it):
//   [alarm]              one block an alarm, in their order (at most ALARMS_MAX: more in a hand-edited file are
//   id     = 1             not used)
//   time   = 07:00       HH:MM, the wall time (an invalid one: the alarm "Invalid", never rung, its text kept)
//   label  = School      UTF-8, 40 characters at most
//   on     = 1
//   days   = mon tue wed thu fri    (empty: once, on "date")
//   date   =             once: the day, YYYYMMDD
//   sound  = chimes      chimes | beeps | marimba
//   snooze =             YYYYMMDDHHMM: rings again then
//   missed = ...         (only when set) YYYYMMDDHHMM: a ring nobody answered (the row's "Missed at")
//   [timer]              a running timer handed over by the Clock when it closed: end = its end in ticks, set = its
//   end = 61000            time set (seconds), label, utc = its end in UTC seconds when the clock was real
// Unknown keys (and blocks) are kept when the Clock writes the file again.
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
#ifndef CLOCK_ALARMS_H
#define CLOCK_ALARMS_H
#include "filekit/filekit.h"

#define ALARMS_PATH	"SD:/apps/clock.app/alarms.txt"
#define ALARMS_MAX	20
#define ALARM_LABEL_MAX	40		// characters
#define ALARM_TIMER	(-1)		// a Due's id: the [timer]
enum { AL_MON = 1, AL_TUE = 2, AL_WED = 4, AL_THU = 8, AL_FRI = 16, AL_SAT = 32, AL_SUN = 64 };
#define AL_WEEKDAYS	(AL_MON | AL_TUE | AL_WED | AL_THU | AL_FRI)
#define AL_WEEKENDS	(AL_SAT | AL_SUN)
#define AL_EVERYDAY	(AL_WEEKDAYS | AL_WEEKENDS)

struct Alarm
{
	int  id;		// the file's id (never reused while the file lives: AlarmSet::next_id = max + 1)
	bool valid;		// time = HH:MM read (else shown "Invalid", never rung)
	int  hh, mm;
	bool on;
	unsigned days;		// AL_* mask; 0 = once
	long date;		// once: the day it rings (clk_days); -1 none
	char label[164];	// UTF-8, <= 40 characters
	char sound[12];		// "chimes" | "beeps" | "marimba"
	long snooze;		// the wall minute (clk_minute) it rings again, -1 none
	long missed;		// the wall minute of the last unanswered ring, -1 none (written by the Clock)
	int  block;		// its block in the document read (0: new) -- its unknown keys copied from there
};
struct AlarmSet
{
	Alarm a[ALARMS_MAX];
	int  n;
	int  next_id;		// the id a new alarm gets
	bool timer;		// a [timer] block
	long timer_end_tick;	// its end = (ticks)
	int  timer_set;		// its set = (seconds)
	long long timer_utc;	// its utc = (seconds since 1970), -1 none
	char timer_label[64];
	int  timer_block;	// its block in the document read (0: new)
	fk_kv *doc;		// the file read, for its unknown keys and blocks (0: none)
};

void alarms_init (AlarmSet &s);				// empty (call once before the rest)
void alarms_free (AlarmSet &s);				// the document read freed; empty again
int  alarms_parse (AlarmSet &s, const char *text);	// the first 20 [alarm] blocks, the [timer] -> how many alarms
int  alarms_load (AlarmSet &s, const char *path);	// a missing file = an empty set -> how many
int  alarms_save (AlarmSet &s, const char *path);	// written (unknown keys kept), the set re-based on it -> 0, -1

// One alarm
void alarm_new (AlarmSet &s, Alarm &a);			// defaults: a new id, 07:00, on, once, chimes, no label
int  alarm_add (AlarmSet &s, const Alarm &a);		// appended -> its index, -1 (20 already)
int  alarm_find (const AlarmSet &s, int id);		// its index, -1
int  alarm_remove (AlarmSet &s, int id);		// -> 1, 0 none
void alarm_set_label (Alarm &a, const char *utf8);	// cut at 40 characters (on a character), new lines made spaces
const char *alarm_sound (const char *token);		// "chimes" | "beeps" | "marimba" (unknown: "chimes")
unsigned alarms_days_parse (const char *v);		// "mon tue ..." -> AL_* (unknown words ignored)
void alarms_days_text (unsigned m, char *out, int cap);	// AL_* -> "mon tue wed thu fri" (Monday first)
enum { REP_ONCE, REP_DAILY, REP_WEEKDAYS, REP_WEEKENDS, REP_CUSTOM };
int  alarm_repeat_kind (unsigned days);			// the UI turns it into words (Once, Every day, Weekdays...)

// When
long alarm_once_day (int hh, int mm, long now_min);	// a once alarm's day: today if hh:mm is still to come, else tomorrow
long alarm_next (const Alarm &a, long from_min);	// the first wall minute >= from_min it rings by its schedule
							// (valid; "on" and the snooze not looked at), -1 never
long alarms_next (const AlarmSet &s, long now_min, int *idx);	// the soonest ring after now_min of the on ones,
							// the snoozes counted -> the minute, -1 none (*idx: which)
bool alarm_passed (const Alarm &a, long now_min);	// a once alarm whose minute is past (shown off, written on = 0)
void alarm_snooze (Alarm &a, long now_min, int minutes);	// rings again at now_min + minutes
void alarm_stop (Alarm &a);				// answered: the snooze and the missed mark cleared, a once alarm off

// The [timer] handed over: what it is at now (now_utc: UTC seconds, -1 unknown)
enum { TMR_NONE, TMR_STALE, TMR_PENDING, TMR_DUE };
// NONE: no [timer]. STALE: not of this boot (its end further than its time set + 1 s, or its utc = 5 s off from the
// ticks' reckoning) -- ignored, dropped at the Clock's next save. PENDING: still to come. DUE: its end reached.
int  alarms_timer_state (const AlarmSet &s, long now_tick, long long now_utc);
void alarms_timer_put (AlarmSet &s, long end_tick, int set_s, long long end_utc, const char *label);	// (the hand-over)
void alarms_timer_clear (AlarmSet &s);

// ---- the Ringer (clockd) ------------------------------------------------------------------------------------
struct Due { int id; long minute; bool snooze; };	// id ALARM_TIMER for the [timer]
struct Ringer { long last; bool started; long timer_rung; };	// zeroed = not started
void ringer_start (Ringer &r, long now_min);		// what is past stays past; timer_rung = -1
// One look (clockd: every 0.5 s): what rings now, at most max -> how many. now_min: the wall minute; trusted: the
// date is real and the boot guard past (else nothing of the alarms rings, "now" taken as looked at); now_tick: the
// ticks, now_utc: UTC seconds (-1 unknown) for the [timer].
//   - the [timer] rings once when DUE (its end remembered in timer_rung: not again after a reload);
//   - an alarm minute in (last, now] and at most 2 minutes old rings once; a jump back of up to 2 h (the summer
//     time ending) rings nothing twice, a bigger one (the clock set) starts again from there.
int  ringer_step (Ringer &r, const AlarmSet &s, long now_min, bool trusted, long now_tick, long long now_utc, Due *out, int max);

#endif
