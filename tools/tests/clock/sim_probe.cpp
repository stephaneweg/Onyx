//
// sim_probe.cpp -- checks the desktop simulator's stand-in kernel (tools/tests/desktop_sim/fakekapi.cpp) gives what
// the Clock / clockd tests rely on (AutoDev round 6, step 0): SIM_CLOCK's advancing date, SIM_TZ, kapi_clock_info's
// UTC and kapi_set_timezone moving the wall time -- and that nothing of it changes while SIM_CLOCK is unset. Linked
// with fakekapi.o; run by tools/tests/run_clock_test.sh, one case an invocation (the environment and the script set
// there), from the repository's root. Prints "probe <case>: ok" when every check of the case passed.
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
#include <stdio.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { fprintf (stderr, "probe: FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static bool is_time (int Y, int MO, int D, int H, int MI, int S)
{
	int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
	int r = kapi_get_datetime (&y, &mo, &d, &h, &mi, &s);
	if (r != 1 || y != Y || mo != MO || d != D || h != H || mi != MI || s != S)
	{
		fprintf (stderr, "probe: the date reads %04d-%02d-%02d %02d:%02d:%02d (%d)\n", y, mo, d, h, mi, s, r);
		return false;
	}
	return true;
}
// kapi_clock_info's UTC as seconds since 1970 (-1: no answer)
static long long utc_s (int *tz)
{
	struct kapi_clock_info ci;
	if (kapi_clock_info (&ci) != 0 || !(ci.flags & KAPI_CLOCK_REALTIME_VALID)) return -1;
	if (tz) *tz = ci.tz_minutes;
	return ci.utc_us / 1000000;
}

int main (int argc, char **argv)
{
	const char *c = argc > 1 ? argv[1] : "";
	if (!strcmp (c, "clock"))			// SIM_CLOCK=20260928123400 (SIM_TZ unset: 120), 120 script steps
	{
		CHECK (is_time (2026, 9, 28, 12, 34, 0));
		int tz = 0;
		CHECK (utc_s (&tz) == 1790598840LL - 7200 && tz == 120);	// 10:34:00 UTC
		for (int i = 0; i < 100; i++) kapi_msleep (1000);		// 101 ticks a call: 10 100 ticks = 101 s
		CHECK (kapi_get_ticks () == 1000 + 10100);
		CHECK (is_time (2026, 9, 28, 12, 35, 41));
		CHECK (utc_s (&tz) == 1790598840LL - 7200 + 101 && tz == 120);	// 10:35:41 UTC
	}
	else if (!strcmp (c, "tz"))			// SIM_CLOCK=20261025025930 SIM_TZ=120: kapi_set_timezone moves the wall time
	{
		CHECK (is_time (2026, 10, 25, 2, 59, 30));
		int tz = 0;
		long long u = utc_s (&tz);
		CHECK (u == 1792889970LL && tz == 120);				// 2026-10-25 00:59:30 UTC
		CHECK (kapi_set_timezone (60) == 1);
		CHECK (is_time (2026, 10, 25, 1, 59, 30));			// the UTC goes on, the wall time an hour back
		CHECK (utc_s (&tz) == u && tz == 60);
		CHECK (kapi_set_timezone (900) == 0);				// (out of range: refused, nothing changed)
		CHECK (utc_s (&tz) == u && tz == 60);
		kapi_msleep (5990);						// 600 ticks: 6 s
		CHECK (is_time (2026, 10, 25, 1, 59, 36));
	}
	else if (!strcmp (c, "sim_tz"))			// SIM_CLOCK=20260101000000 SIM_TZ=-300 (New York in winter)
	{
		CHECK (is_time (2026, 1, 1, 0, 0, 0));
		int tz = 0;
		CHECK (utc_s (&tz) == 1767243600LL && tz == -300);		// 2026-01-01 05:00:00 UTC
	}
	else if (!strcmp (c, "noclock"))		// SIM_CLOCK unset: frozen 12:34:00, no clock_info, set_timezone moves nothing
	{
		CHECK (is_time (2026, 9, 28, 12, 34, 0));
		CHECK (utc_s (0) == -1);
		for (int i = 0; i < 10; i++) kapi_msleep (1000);
		CHECK (is_time (2026, 9, 28, 12, 34, 0));
		CHECK (kapi_set_timezone (60) == 1);
		CHECK (is_time (2026, 9, 28, 12, 34, 0));
	}
	else if (!strcmp (c, "stat"))			// SIM_STAT=1, SIM_CLOCK unset: the fixed 12:34 UTC, tz 0, as before
	{
		int tz = -1;
		CHECK (utc_s (&tz) == 1790598840LL && tz == 0);
		CHECK (kapi_set_timezone (60) == 1);
		CHECK (utc_s (&tz) == 1790598840LL && tz == 0);
	}
	else { fprintf (stderr, "probe: no case %s\n", c); return 2; }
	if (g_fail) return 1;
	fprintf (stderr, "probe %s: ok\n", c);
	return 0;
}
