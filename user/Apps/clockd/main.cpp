//
// Apps/clockd/main.cpp -- clockd, the Clock's alarm service (AutoDev round 6; the plan: autodev/rounds/06-clock/
// 03-technical-analysis.md §3.5). No window, no words: started at boot by SD:/etc/autostart ("run clockd"), it
// registers the IPC service "clockd", reads SD:/apps/clock.app/alarms.txt (written by the Clock only, through
// Apps/clock/alarms.cpp) and looks at the wall clock every half second. An alarm minute that comes (the Ringer:
// each minute rung once, within 2 minutes, never the past at its start) is handed to the Clock -- the running one
// is sent CLOCK_MSG_OPEN "--ring <id>", else "clock --ring <id>" is started --, which rings it (its window, the
// sound, the notification, in the system's language). The [timer] handed over by a Clock that closed while it ran
// rings the same way ("--ring timer"). Only when the Clock can be neither told nor started does clockd send a
// notification of its own: word-free, the time and the label.
//
//   CLOCKD_MSG_RELOAD   alarms.txt read again now (the Ringer kept: nothing rung twice); the file is also looked
//                       at every 30 s (a hand edit, a message lost)
//   CLOCKD_MSG_QUIT     the service ends
// Once a minute: SystemKit's locale_zone_sync () -- the system's clock follows the summer time of the zone chosen
// in Language & Region (zone= of SD:/etc/system.ini).
//
//   clockd [--grace N]   N: the ticks (hundredths of a second since the boot) before which no alarm rings -- the
//                        "last time seen" restored at boot is not the real time until NTP has answered (default
//                        9000 = 90 s; 0 for the tests)
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
#include "systemkit/systemkit.h"
#include "filekit/filekit.h"
#include "Apps/clock/alarms.h"
#include "Apps/clock/clocktime.h"
#include "Apps/clock/clock_proto.h"

static AlarmSet g_set;
static Ringer   g_ringer;		// (zeroed: not started -- its first step takes "now" as already looked at)

static void say (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
static void say (const char *fmt, ...)
{
	va_list ap; va_start (ap, fmt);
	printf ("clockd: "); vprintf (fmt, ap); printf ("\n");
	va_end (ap);
	fflush (stdout);
}

// A cheap signature of alarms.txt (its size and a sum of its bytes; 0: no file): changed = read again.
static unsigned file_sig (void)
{
	void *f = kapi_open (ALARMS_PATH);
	if (!f) return 0;
	unsigned n = kapi_fsize (f), sum = n * 2654435761u + 1;
	unsigned char b[512];
	for (;;)
	{
		int r = kapi_read (f, b, sizeof b);
		if (r <= 0) break;
		for (int i = 0; i < r; i++) sum = sum * 31 + b[i];
	}
	kapi_close (f);
	return sum ? sum : 1;
}

static unsigned g_sig;
static void reload (const char *why)
{
	alarms_load (g_set, ALARMS_PATH);			// (a missing file: no alarms)
	g_sig = file_sig ();
	say ("reload (%s): %d alarm%s%s", why, g_set.n, g_set.n == 1 ? "" : "s", g_set.timer ? ", a timer" : "");
}

// One ring handed to the Clock: the running one told, else started; neither: clockd's own notification.
static void ring (const Due &d, int now_s)
{
	char args[32], hm[8], label[200];
	int i = d.id == ALARM_TIMER ? -1 : alarm_find (g_set, d.id);
	long m = d.minute < 0 ? 0 : d.minute % 1440;
	clk_fmt_hm ((int) (m / 60), (int) (m % 60), hm, sizeof hm);
	if (d.id == ALARM_TIMER) snprintf (args, sizeof args, "--ring timer");
	else snprintf (args, sizeof args, "--ring %d", d.id);
	snprintf (label, sizeof label, "%s", d.id == ALARM_TIMER ? g_set.timer_label : i >= 0 ? g_set.a[i].label : "");
	int pid = kapi_ipc_lookup (CLOCK_SERVICE);
	bool told = false;
	if (pid > 0) told = kapi_mailbox_send (pid, CLOCK_MSG_OPEN, args, (unsigned) strlen (args) + 1) >= 0;
	if (!told) told = lx_launch ("clock", args) > 0;
	char at[16];
	clk_fmt_hms (now_s / 3600, now_s / 60 % 60, now_s % 60, at, sizeof at);
	if (d.id == ALARM_TIMER) say ("ring timer %s at %s%s", hm, at, told ? "" : " (the Clock cannot start: notified)");
	else say ("ring %d %s%s at %s%s", d.id, hm, d.snooze ? " (snoozed)" : "", at, told ? "" : " (the Clock cannot start: notified)");
	if (!told) notify_action (hm, label, d.id == ALARM_TIMER ? "clock timer" : "clock alarms");
}

int main (void)
{
	if (!kapi_ipc_register (CLOCKD_SERVICE)) return 0;		// another clockd runs
	long grace = 9000;
	char a[64] = "";
	kapi_get_args (a, sizeof a);
	if (const char *g = strstr (a, "--grace")) grace = atol (g + 7);
	alarms_init (g_set);
	reload ("start");
	locale_zone_sync ();
	unsigned lastSig = kapi_get_ticks (), lastSync = lastSig;
	for (;;)
	{
		// the messages: read alarms.txt again, quit
		int from = 0, type = 0, n;
		char m[520];
		while ((n = kapi_mailbox_recv (&from, &type, m, sizeof m, 0)) >= 0)
		{
			if (type == CLOCKD_MSG_RELOAD) reload ("message");
			else if (type == CLOCKD_MSG_QUIT) { say ("quit"); alarms_free (g_set); return 0; }
		}
		unsigned now_t = kapi_get_ticks ();
		if (now_t - lastSig >= 3000)				// every 30 s: changed by hand?
		{
			lastSig = now_t;
			if (file_sig () != g_sig) reload ("changed");
		}
		if (now_t - lastSync >= 6000)				// once a minute: the summer time
		{
			lastSync = now_t;
			if (locale_zone_sync ()) say ("the clock follows the zone's summer time");
		}
		// what rings now
		int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
		bool real = kapi_get_datetime (&y, &mo, &d, &h, &mi, &s) == 1;
		long now_min = clk_minute (y, mo, d, h, mi);
		struct kapi_clock_info ci;
		long long utc = -1;
		if (kapi_clock_info (&ci) == 0 && (ci.flags & KAPI_CLOCK_REALTIME_VALID)) utc = (long long) (ci.utc_us / 1000000);
		Due due[8];
		int k = ringer_step (g_ringer, g_set, now_min, real && (long) now_t >= grace, (long) now_t, utc, due, 8);
		for (int i = 0; i < k; i++) ring (due[i], h * 3600 + mi * 60 + s);
		kapi_msleep (500);
	}
}
