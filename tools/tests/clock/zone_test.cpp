//
// zone_test.cpp -- SystemKit's time zones at an instant (AutoDev round 6, steps 1a / 1b): locale_zone_offset_at
// (the summer time judged from UTC, the hour of the change counted) and locale_zone_sync (the zone of system.ini's
// zone= applied to the clock, never a zone guessed from timezone=). Linked with the desktop simulator's fakekapi.o;
// run by tools/tests/run_clock_test.sh, one case an invocation (SIM_CLOCK, SIM_TZ and the writes' system.ini set
// there). Prints "zone <case>: ok" when every check of the case passed; the shell checks the log besides.
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
#include "appkit/appkit.h"
#include "systemkit/systemkit.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static int g_fail, g_checks;
#define CHECK(c) do { g_checks++; if (!(c)) { fprintf (stderr, "zone: FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define EQI(a, b) do { g_checks++; long long a_ = (a), b_ = (b); if (a_ != b_) { fprintf (stderr, "zone: FAIL %s:%d: %s = %lld, expected %lld\n", __FILE__, __LINE__, #a, a_, b_); g_fail++; } } while (0)

// minutes since 1970 of a UTC date (the host's timegm: an independent reckoning)
static long long U (int y, int mo, int d, int h, int mi)
{
	struct tm tm; memset (&tm, 0, sizeof tm);
	tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = mi;
	return (long long) timegm (&tm) / 60;
}
static int zone_of (const char *city)
{
	for (int z = 0; z < locale_zone_count (); z++) if (!strcmp (locale_zone_city (z), city)) return z;
	fprintf (stderr, "zone: no zone %s\n", city);
	return -1;
}
static void ini_is (const char *key, const char *want)
{
	char v[64];
	int r = locale_ini_get (key, v, sizeof v);
	CHECK (want ? r == 1 && !strcmp (v, want) : r == 0);
	if (want && strcmp (v, want)) fprintf (stderr, "zone: %s=%s, %s expected\n", key, v, want);
}

static void test_at (void)
{
	int bx = zone_of ("Brussels"), ny = zone_of ("New York"), tk = zone_of ("Tokyo"), ut = zone_of ("UTC");
	int ld = zone_of ("London"), la = zone_of ("Los Angeles");
	// the EU: the last Sunday of March / October, 01:00 UTC
	EQI (locale_zone_offset_at (bx, U (2026, 3, 29, 0, 59)), 60);
	EQI (locale_zone_offset_at (bx, U (2026, 3, 29, 1, 0)), 120);
	EQI (locale_zone_offset_at (bx, U (2026, 10, 25, 0, 59)), 120);
	EQI (locale_zone_offset_at (bx, U (2026, 10, 25, 1, 0)), 60);
	EQI (locale_zone_offset_at (bx, U (2027, 3, 28, 0, 59)), 60);
	EQI (locale_zone_offset_at (bx, U (2027, 3, 28, 1, 0)), 120);
	EQI (locale_zone_offset_at (bx, U (2027, 10, 31, 0, 59)), 120);
	EQI (locale_zone_offset_at (bx, U (2027, 10, 31, 1, 0)), 60);
	EQI (locale_zone_offset_at (bx, U (2026, 9, 28, 10, 34)), 120);		// the simulator's day
	EQI (locale_zone_offset_at (bx, U (2026, 1, 1, 0, 0)), 60);
	EQI (locale_zone_offset_at (bx, U (2026, 12, 31, 23, 59)), 60);
	EQI (locale_zone_offset_at (ld, U (2026, 10, 25, 0, 59)), 60);		// (London changes at the same instant)
	EQI (locale_zone_offset_at (ld, U (2026, 10, 25, 1, 0)), 0);
	// the US: the 2nd Sunday of March 02:00 standard, the 1st Sunday of November 02:00 summer (local)
	EQI (locale_zone_offset_at (ny, U (2026, 3, 8, 6, 59)), -300);
	EQI (locale_zone_offset_at (ny, U (2026, 3, 8, 7, 0)), -240);
	EQI (locale_zone_offset_at (ny, U (2026, 11, 1, 5, 59)), -240);
	EQI (locale_zone_offset_at (ny, U (2026, 11, 1, 6, 0)), -300);
	EQI (locale_zone_offset_at (ny, U (2027, 3, 14, 6, 59)), -300);
	EQI (locale_zone_offset_at (ny, U (2027, 3, 14, 7, 0)), -240);
	EQI (locale_zone_offset_at (ny, U (2026, 9, 28, 10, 34)), -240);
	EQI (locale_zone_offset_at (la, U (2026, 3, 8, 9, 59)), -480);		// (02:00 PST = 10:00 UTC)
	EQI (locale_zone_offset_at (la, U (2026, 3, 8, 10, 0)), -420);
	EQI (locale_zone_offset_at (la, U (2026, 11, 1, 8, 59)), -420);		// (02:00 PDT = 09:00 UTC)
	EQI (locale_zone_offset_at (la, U (2026, 11, 1, 9, 0)), -480);
	// no summer time
	EQI (locale_zone_offset_at (tk, U (2026, 3, 29, 1, 0)), 540);
	EQI (locale_zone_offset_at (tk, U (2026, 7, 1, 0, 0)), 540);
	EQI (locale_zone_offset_at (tk, U (2026, 12, 1, 0, 0)), 540);
	EQI (locale_zone_offset_at (ut, U (2026, 7, 1, 0, 0)), 0);
	// out of range: 0
	EQI (locale_zone_offset_at (-1, U (2026, 7, 1, 0, 0)), 0);
	EQI (locale_zone_offset_at (locale_zone_count (), U (2026, 7, 1, 0, 0)), 0);
	// every zone, every hour of 2026-2027: its winter offset or its summer one, nothing else; and the change
	// happens twice a year for a zone with summer time (never for Tokyo / UTC)
	for (int z = 0; z < locale_zone_count (); z++)
	{
		int w = locale_zone_offset_at (z, U (2026, 1, 15, 12, 0)), s = locale_zone_offset_at (z, U (2026, 7, 15, 12, 0));
		int changes = 0, last = w, bad = 0;
		for (long long m = U (2026, 1, 1, 0, 0); m < U (2028, 1, 1, 0, 0); m += 60)
		{
			int o = locale_zone_offset_at (z, m);
			if (o != w && o != s) bad++;
			if (o != last) { changes++; last = o; }
		}
		CHECK (!bad);
		EQI (changes, w == s ? 0 : 4);
	}
	// an instant before 1970 (floored days): Brussels in winter 1969
	EQI (locale_zone_offset_at (bx, -60), 60);
}

// kapi_clock_info's offset now (-9999: no answer)
static int clock_tz (void)
{
	struct kapi_clock_info ci;
	return kapi_clock_info (&ci) == 0 && (ci.flags & KAPI_CLOCK_REALTIME_VALID) ? ci.tz_minutes : -9999;
}
static void minute (void) { kapi_msleep (59990); }		// (6 000 ticks: 60 s of the simulator's clock)

// A change night: before it, nothing; a minute later the clock and timezone= changed once; then never again
// (no oscillation, the wall time gone back included). from / to: the offsets; tzw: timezone= written after.
static void test_change (int from, int to, const char *tzw)
{
	EQI (clock_tz (), from);
	EQI (locale_zone_sync (), 0);			// (a minute before the change: already right)
	EQI (clock_tz (), from);
	minute ();
	EQI (locale_zone_sync (), 1);			// the change: the clock's offset and system.ini's timezone=
	EQI (clock_tz (), to);
	ini_is ("timezone", tzw);
	EQI (locale_zone_sync (), 0);			// called again: nothing (no oscillation)
	for (int i = 0; i < 90; i++) { minute (); if (locale_zone_sync () != 0) { CHECK (!"changed again"); break; } }
	EQI (clock_tz (), to);
}

int main (int argc, char **argv)
{
	const char *c = argc > 1 ? argv[1] : "";
	if (!strcmp (c, "at")) test_at ();
	// SIM_CLOCK=20261025025930 SIM_TZ=120, zone=Brussels: summer time ends at 01:00 UTC (03:00 CEST -> 02:00 CET)
	else if (!strcmp (c, "autumn")) { test_change (120, 60, "60"); ini_is ("zone", "Brussels"); }
	// SIM_CLOCK=20270328015930 SIM_TZ=60, zone=Paris: it begins at 01:00 UTC (02:00 CET -> 03:00 CEST)
	else if (!strcmp (c, "spring")) { test_change (60, 120, "120"); ini_is ("zone", "Paris"); }
	// SIM_CLOCK=20261101015930 SIM_TZ=-240, zone=New York: 02:00 EDT -> 01:00 EST at 06:00 UTC
	else if (!strcmp (c, "newyork")) { test_change (-240, -300, "-300"); ini_is ("zone", "New York"); }
	// SIM_CLOCK=20260928123400 SIM_TZ=60, zone=Brussels, timezone=60: a clock left on the winter offset, put right
	else if (!strcmp (c, "wrong")) { EQI (locale_zone_sync (), 1); EQI (clock_tz (), 120); ini_is ("timezone", "120"); EQI (locale_zone_sync (), 0); }
	// already right / no zone= / an unknown city / no real date: 0, the clock untouched (the shell: nothing logged,
	// system.ini not written)
	else if (!strcmp (c, "right") || !strcmp (c, "nozone") || !strcmp (c, "guess") || !strcmp (c, "card") || !strcmp (c, "nowhere") || !strcmp (c, "noclock"))
	{
		int tz = clock_tz ();
		for (int i = 0; i < 5; i++) { EQI (locale_zone_sync (), 0); minute (); }
		EQI (clock_tz (), tz);
	}
	else { fprintf (stderr, "zone: no case %s\n", c); return 2; }
	if (g_fail) { fprintf (stderr, "zone %s: %d of %d checks failed\n", c, g_fail, g_checks); return 1; }
	fprintf (stderr, "zone %s: ok (%d checks)\n", c, g_checks);
	return 0;
}
