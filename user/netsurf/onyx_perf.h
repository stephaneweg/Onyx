/*
 * onyx_perf.h -- timing probes for NetSurf on Onyx (the cost of a rebox, a layout, a redraw).
 *
 * Off unless the file ONYX_NS_DATAPATH "perf" exists (SD:/apps/netsurf.app/perf on the Pi,
 * $(OUT)/data/perf on the PC bench) or NS_PERF is set: then each probe longer than
 * ONYX_PERF_MIN_US prints "ONYX-PERF <what> <us> us" on stderr (the kernel log on the Pi).
 *
 *   uint64_t t0 = onyx_perf_now ();  ...  onyx_perf_log ("layout", t0);
 */
#ifndef ONYX_PERF_H
#define ONYX_PERF_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#define ONYX_PERF_MIN_US 1000

static inline int onyx_perf_on (void)
{
	static int on = -1;
	if (on < 0) {
		FILE *f = NULL;
#ifdef ONYX_NS_DATAPATH
		f = fopen (ONYX_NS_DATAPATH "perf", "r");
#endif
		on = (f != NULL || getenv ("NS_PERF") != NULL) ? 1 : 0;
		if (f != NULL)
			fclose (f);
	}
	return on;
}

static inline uint64_t onyx_perf_now (void)
{
	struct timeval tv;
	if (!onyx_perf_on ())
		return 0;
	gettimeofday (&tv, NULL);
	return (uint64_t) tv.tv_sec * 1000000u + (uint64_t) tv.tv_usec;
}

static inline void onyx_perf_log (const char *what, uint64_t t0)
{
	uint64_t d;
	if (!onyx_perf_on ())
		return;
	d = onyx_perf_now () - t0;
	if (d >= ONYX_PERF_MIN_US)
		fprintf (stderr, "ONYX-PERF %s %lu us\n", what, (unsigned long) d);
}

#endif
