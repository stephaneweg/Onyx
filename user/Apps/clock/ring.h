//
// ring.h -- an alarm ringing (02 #17-20, 04 D13-D15; 03 step 8). One translation unit with main.cpp.
//
// clockd decides when (its Ringer); the Clock rings: "clock --ring <id>" (started by clockd) or CLOCK_MSG_OPEN
// "--ring <id>" (the running Clock). alarms.txt is read again, the notification sent first ("Clock" -- "07:00 School",
// its click: "clock alarms"), the window on the Alarms tab with the ring card over it, raised, the sound looped (when
// one can be heard: else the card says why). Snooze (Enter): rings again in config.ini's snooze minutes; Stop (Esc):
// a once alarm is off, a repeating one waits for its next day; unanswered 2 minutes: the sound stops, the card goes,
// "Missed alarm: 07:00 School" is notified and the row says "Missed at 07:00" (missed = in alarms.txt). Started only
// to ring, the Clock then closes by itself (g_ringOnly).
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
#include "sounds.h"

#define RING_UNANSWERED	12000			// ticks: 2 minutes

static void ring_snooze (Widget &);
static void ring_stop (Widget &);
class RingVeil : public Veil
{
public:
	int id;				// the alarm ringing (0: none)
	long minute;			// the minute it rang at (the wall minute)
	long start;			// the tick the card came
	int sound;			// sound_play's: 1, 0 busy, -1 none
	char hm[8], label[180], line[160];
	Button *snooze, *stop;
	RingVeil () : Veil (380, 280, TR ("Alarm")), id (0), minute (0), start (0), sound (1)
	{
		hm[0] = label[0] = line[0] = 0;
		snooze = new Button (0, 0, 168, 36, "", ring_snooze);
		stop = new Button (0, 0, 120, 36, TR ("Stop"), ring_stop);
		place (snooze, 0, 0); place (stop, 0, 0);
	}
	// the card for that alarm: 28 px taller with the no-sound line; the buttons at its foot
	void set (const Alarm &a)
	{
		clk_fmt_hm (a.hh, a.mm, hm, sizeof hm);
		snprintf (label, sizeof label, "%s", label_of (a));
		char rp[96]; repeat_text (a.days, rp, sizeof rp);
		snprintf (line, sizeof line, "%s" DOT "%s", rp, TR (SOUNDNAME[sound_index (a.sound)]));
		snprintf (snooze->text, sizeof snooze->text, TR ("Snooze %d min"), g_cfg.snooze);
		snooze->invalidate (true);
		ch = sound == 1 ? 280 : 308;
		layout ();
		int x = (cw - 168 - 12 - 120) / 2;
		snooze->left = cx + x; snooze->top = cy + ch - 76;
		stop->left = cx + x + 168 + 12; stop->top = cy + ch - 76;
	}
	void drawCard () override
	{
		int y = cy + th ();
		unsigned d = dim_on (C_BG);
		draw_bell (canvas, width / 2, y + 36, 38, C_ACCENT, true);
		{ UkFaceScope sc (face (50)); uk_text_c (canvas, cx, y + 60, cw, 60, hm, C_TEXT, 2); }
		{ UkFaceScope sc (face (18)); uk_text_c (canvas, cx, y + 120, cw, 26, label, C_TEXT, 2); }
		uk_text_c (canvas, cx, y + 146, cw, 20, line, d);
		if (sound != 1)
		{
			const char *s = sound == 0 ? TR ("Sound unavailable: the sound output is busy") : TR ("Sound unavailable");
			int w = uk_tw (s) + 22, x = (width - w) / 2;
			draw_speaker_off (canvas, x + 8, y + 180, 16, AMBER ());
			uk_text_l (canvas, x + 22, y + 170, 20, s, AMBER ());
		}
		uk_text_c (canvas, cx, cy + ch - 28, cw, 20, TR ("Enter: snooze \xC2\xB7 Esc: stop"), d);
	}
	bool onKey (long k) override
	{
		if (k == KEY_ENTER) { ring_snooze (*this); return true; }
		if (k == 27) { ring_stop (*this); return true; }
		return false;
	}
	void onHidden () override { g_list->setFocus (); }
};
static RingVeil *g_ring;
static bool ring_busy () { return g_ring && g_ring->id; }	// (timerview.h: Time's up ending leaves an alarm's sound alone)

// "07:00 School": the notifications' words
static void ring_words (const Alarm &a, char *o, int cap) { char t[8]; clk_fmt_hm (a.hh, a.mm, t, sizeof t); snprintf (o, cap, "%s %s", t, label_of (a)); }

static void ring_alarm (int id)
{
	if (!g_ring) g_ring = new RingVeil ();
	alarms_load (g_al, ALARMS_PATH);			// (what clockd rang: the file as it is now)
	g_alPrint = true;
	int i = alarm_find (g_al, id);
	if (i < 0 || !g_al.a[i].valid)
	{
		say ("ring %d: no such alarm", id);
		alarms_changed ();
		if (g_ringOnly && !g_ring->shown ()) g_closeNow = true;
		return;
	}
	Alarm &a = g_al.a[i];
	char w[200];
	ring_words (a, w, sizeof w);
	notify_action (TR ("Clock"), w, "clock alarms");	// (first: it shows even if the window is slow to come)
	say ("ring %d %s", id, w);
	a.snooze = -1; a.missed = -1;				// (this ring is the snooze's; a missed mark goes)
	if (g_ring->shown () && g_ring->id && g_ring->id != id) say ("ring %d replaces ring %d", id, g_ring->id);
	g_ring->id = id; g_ring->minute = g_now.minute; g_ring->start = g_now.tick;
	g_ring->sound = sound_play (a.sound, true);
	g_ring->set (a);
	tab_show (TAB_ALARMS);
	g_alSel = id;
	alarms_changed ();
	g_ring->show (g_root, g_ring->snooze);
	uk_win_app_raise (CLOCK_SERVICE);
}
// the ring answered (or not): the card gone, the sound stopped, the file written, the Clock closed if it came only for it
static void ring_end ()
{
	sound_stop ();
	g_ring->id = 0;
	g_ring->hide ();
	alarms_commit ();
	if (g_ringOnly) g_closeNow = true;
}
static void ring_snooze (Widget &)
{
	int i = alarm_find (g_al, g_ring->id);
	if (i >= 0)
	{
		char t[8];
		alarm_snooze (g_al.a[i], g_now.minute, g_cfg.snooze);
		minute_hm (g_al.a[i].snooze, t, sizeof t);
		say ("snooze %d until %s", g_ring->id, t);
	}
	ring_end ();
}
static void ring_stop (Widget &)
{
	int i = alarm_find (g_al, g_ring->id);
	if (i >= 0) { alarm_stop (g_al.a[i]); say ("stop %d", g_ring->id); }
	ring_end ();
}
static void ring_tick ()
{
	sound_step ();
	if (!g_ring || !g_ring->id || g_now.tick - g_ring->start < RING_UNANSWERED) return;
	// unanswered: as Stop, but the miss kept on the row and notified
	int i = alarm_find (g_al, g_ring->id);
	if (i >= 0)
	{
		char w[200], t[260];
		ring_words (g_al.a[i], w, sizeof w);
		snprintf (t, sizeof t, TR ("Missed alarm: %s"), w);
		notify_action (TR ("Clock"), t, "clock alarms");
		g_al.a[i].missed = g_ring->minute;
		g_al.a[i].snooze = -1;
		say ("missed %d", g_ring->id);
	}
	ring_end ();
}

// ---- the alarms missed while the Pi was off (S2): clockd at its start, "--missed <id> <YYYYMMDDHHMM> [...]" --------------
// Each: "Missed alarm: 07:00 School" notified (no sound, no card), the miss written on the alarm's row (missed =; a once
// alarm gone by is then written off by alarms_write). live: the window is there (its Alarms tab shown again).
static void ring_missed (const char *a, bool live)
{
	alarms_load (g_al, ALARMS_PATH);			// (the file as clockd saw it)
	bool changed = false;
	for (;;)
	{
		char w1[16], w2[16]; int k;
		while (*a == ' ') a++;
		for (k = 0; *a && *a != ' '; a++) if (k < 15) w1[k++] = *a;
		w1[k] = 0;
		while (*a == ' ') a++;
		for (k = 0; *a && *a != ' '; a++) if (k < 15) w2[k++] = *a;
		w2[k] = 0;
		if (!w1[0] || !w2[0]) break;
		int id = atoi (w1), i = alarm_find (g_al, id);
		long minute = clk_parse_minute (w2);
		if (i < 0 || !g_al.a[i].valid || minute < 0 || g_al.a[i].missed >= minute || (ring_busy () && g_ring->id == id))
		{
			say ("missed %d: nothing to say", id);
			continue;
		}
		char w[200], t[260];
		ring_words (g_al.a[i], w, sizeof w);
		snprintf (t, sizeof t, TR ("Missed alarm: %s"), w);
		notify_action (TR ("Clock"), t, "clock alarms");
		g_al.a[i].missed = minute;
		g_al.a[i].snooze = -1;
		say ("missed %d %s (while off)", id, w);
		changed = true;
	}
	if (changed) alarms_write ();
	if (live) { g_alPrint = true; alarms_changed (); }
}
