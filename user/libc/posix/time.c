/*
 * time.c -- clocks and sleeps (libonyxposix, docs/POSIX-PLAN.md §3.4).
 *
 * No system call per read: the ARM generic timer (CNTPCT_EL0 / CNTFRQ_EL0, readable at EL0) is
 * scaled with one sample of the kernel's clock (kapi clock_info, v75: CNTPCT at boot, a UTC
 * time at a CNTPCT value, the time zone). CLOCK_MONOTONIC counts from boot; CLOCK_REALTIME is the
 * sample's UTC plus the counter since, the sample taken again every 60 s (it follows NTP).
 * On a kernel without clock_info the wall clock comes once from kapi get_datetime (the local
 * time, taken as UTC with TZ "UTC0": localtime then shows the same hour) and the monotonic clock
 * counts from power-on.
 *
 * Sleeps: kapi sleep_us (v75; under 1 ms the kernel yields in a loop), else msleep / yield.
 * The CPU-time clocks are the monotonic one (no per-task accounting yet).
 * Time zones: crt0posix sets TZ from clock_info's offset when the environment has none (a fixed
 * offset, no daylight saving rules); newlib's localtime_r / mktime / strftime then use it.
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions: The above copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
 * IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#include <time.h>
#include <sys/time.h>
#include <sys/times.h>
#include <unistd.h>
#include <errno.h>
#include "posix_internal.h"

static inline unsigned long long cntpct (void)
{
#if defined(__aarch64__)
	unsigned long long c;
	__asm__ volatile ("isb\n\tmrs %0, cntpct_el0" : "=r" (c));
	return c;
#else
	return 0;
#endif
}

static inline unsigned long long cntfrq (void)
{
#if defined(__aarch64__)
	unsigned long long f;
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (f));
	return f;
#else
	return 1;
#endif
}

static struct
{
	volatile int valid;
	unsigned long long cnt, freq, boot;
	long long utc_us;
	int tz;
	int from_kernel;
} s_clk;
static volatile unsigned s_clkLock;

/* ns in t counter ticks (no overflow: the quotient and the rest apart) */
static inline unsigned long long ticks_ns (unsigned long long t, unsigned long long f)
{
	return (t / f) * 1000000000ULL + (t % f) * 1000000000ULL / f;
}

static long days_from_civil (int y, int m, int d)
{
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	long yoe = y - era * 400;
	long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

static void sample (void)
{
	struct kapi_clock_info ci;
	if (kapi__core () == 0 && kapi_clock_info (&ci) == 0 && ci.freq != 0)
	{
		s_clk.cnt = ci.cnt;
		s_clk.freq = ci.freq;
		s_clk.boot = ci.boot_cnt;
		s_clk.utc_us = ci.utc_us;
		s_clk.tz = ci.tz_minutes;
		s_clk.from_kernel = 1;
	}
	else if (!s_clk.valid)
	{
		/* an older kernel (or an app core's first read): the RTC's local time, once */
		s_clk.freq = cntfrq ();
		if (s_clk.freq == 0)
			s_clk.freq = 54000000;
		s_clk.cnt = cntpct ();
		s_clk.boot = 0;
		s_clk.tz = 0;
		s_clk.utc_us = 0;
		int y, mo, d, h, mi, s;
		if (kapi__core () == 0 && kapi_get_datetime (&y, &mo, &d, &h, &mi, &s))
			s_clk.utc_us = ((((long long) days_from_civil (y, mo, d) * 24 + h) * 60 + mi) * 60 + s) * 1000000LL;
		else
			s_clk.utc_us = (long long) (ticks_ns (s_clk.cnt, s_clk.freq) / 1000);	/* since boot */
		s_clk.from_kernel = 0;
	}
	__atomic_store_n (&s_clk.valid, 1, __ATOMIC_RELEASE);
}

static void ensure (int resample)
{
	if (__atomic_load_n (&s_clk.valid, __ATOMIC_ACQUIRE) && !resample)
		return;
	if (kapi__core () != 0)
	{
		if (!s_clk.valid)
			sample ();
		return;
	}
	__onyx_lock (&s_clkLock);
	if (!s_clk.valid || resample)
		sample ();
	__onyx_unlock (&s_clkLock);
}

unsigned long long __onyx_mono_ns (void)
{
	ensure (0);
	unsigned long long c = cntpct ();
	return ticks_ns (c > s_clk.boot ? c - s_clk.boot : 0, s_clk.freq);
}

long long __onyx_real_ns (void)
{
	ensure (0);
	unsigned long long c = cntpct ();
	if (s_clk.from_kernel && c - s_clk.cnt > 60ULL * s_clk.freq && kapi__core () == 0)
	{
		ensure (1);
		c = cntpct ();
	}
	long long base = s_clk.utc_us * 1000LL;
	return c >= s_clk.cnt ? base + (long long) ticks_ns (c - s_clk.cnt, s_clk.freq)
			      : base - (long long) ticks_ns (s_clk.cnt - c, s_clk.freq);
}

int __onyx_tz_minutes (void)
{
	ensure (0);
	return s_clk.tz;
}

void __onyx_sleep_ns (unsigned long long ns)
{
	if (kapi__core () != 0)
	{
		unsigned long long end = __onyx_mono_ns () + ns;
		while (__onyx_mono_ns () < end)
			kapi__pause ();
		return;
	}
	unsigned long long us = (ns + 999) / 1000;
	if (us == 0)
	{
		kapi_yield ();
		return;
	}
	if (kapi_sleep_us (us) != -KAPI_ENOSYS)
		return;
	/* an older kernel: msleep for the milliseconds, yields for the rest */
	unsigned long long end = __onyx_mono_ns () + ns;
	if (us >= 1000)
		kapi_msleep ((unsigned) (us / 1000));
	while (__onyx_mono_ns () < end)
		kapi_yield ();
}

/* ---- POSIX ---- */
static int clock_ok (clockid_t c)
{
	return c == CLOCK_REALTIME || c == CLOCK_MONOTONIC || c == CLOCK_MONOTONIC_RAW ||
	       c == CLOCK_MONOTONIC_COARSE || c == CLOCK_REALTIME_COARSE || c == CLOCK_BOOTTIME ||
	       c == CLOCK_PROCESS_CPUTIME_ID || c == CLOCK_THREAD_CPUTIME_ID;
}

int clock_gettime (clockid_t c, struct timespec *ts)
{
	if (!clock_ok (c) || ts == 0)
		return ONYX_ERR (EINVAL);
	if (c == CLOCK_REALTIME || c == CLOCK_REALTIME_COARSE)
	{
		long long ns = __onyx_real_ns ();
		ts->tv_sec = (time_t) (ns / 1000000000LL);
		ts->tv_nsec = (long) (ns % 1000000000LL);
		if (ts->tv_nsec < 0)
		{
			ts->tv_nsec += 1000000000L;
			ts->tv_sec--;
		}
		return 0;
	}
	unsigned long long ns = __onyx_mono_ns ();
	ts->tv_sec = (time_t) (ns / 1000000000ULL);
	ts->tv_nsec = (long) (ns % 1000000000ULL);
	return 0;
}

int clock_getres (clockid_t c, struct timespec *ts)
{
	if (!clock_ok (c))
		return ONYX_ERR (EINVAL);
	if (ts)
	{
		ensure (0);
		ts->tv_sec = 0;
		ts->tv_nsec = (long) (1000000000ULL / s_clk.freq);
		if (ts->tv_nsec == 0)
			ts->tv_nsec = 1;
	}
	return 0;
}

int clock_settime (clockid_t c, const struct timespec *ts) { (void) c; (void) ts; return ONYX_ERR (EPERM); }
int clock_getcpuclockid (pid_t pid, clockid_t *c) { (void) pid; *c = CLOCK_PROCESS_CPUTIME_ID; return 0; }

int clock_nanosleep (clockid_t c, int flags, const struct timespec *req, struct timespec *rem)
{
	if (!clock_ok (c) || req == 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L)
		return EINVAL;
	if (flags & TIMER_ABSTIME)
	{
		for (;;)
		{
			struct timespec now;
			clock_gettime (c, &now);
			long long d = ((long long) req->tv_sec - now.tv_sec) * 1000000000LL + (req->tv_nsec - now.tv_nsec);
			if (d <= 0)
				return 0;
			__onyx_sleep_ns ((unsigned long long) d);
		}
	}
	unsigned long long ns = (unsigned long long) req->tv_sec * 1000000000ULL + (unsigned long long) req->tv_nsec;
	__onyx_sleep_ns (ns);
	if (rem)
	{
		rem->tv_sec = 0;
		rem->tv_nsec = 0;
	}
	return 0;
}

int nanosleep (const struct timespec *req, struct timespec *rem)
{
	int r = clock_nanosleep (CLOCK_MONOTONIC, 0, req, rem);
	return r ? ONYX_ERR (r) : 0;
}

int usleep (useconds_t us)
{
	__onyx_sleep_ns ((unsigned long long) us * 1000ULL);
	return 0;
}

unsigned sleep (unsigned s)
{
	__onyx_sleep_ns ((unsigned long long) s * 1000000000ULL);
	return 0;
}

static long rpc_gettimeofday (long tv, long tz, long c)
{
	(void) tz; (void) c;
	struct timeval *t = (struct timeval *) tv;
	long long ns = __onyx_real_ns ();
	t->tv_sec = (time_t) (ns / 1000000000LL);
	t->tv_usec = (suseconds_t) ((ns % 1000000000LL) / 1000);
	return 0;
}

int _gettimeofday (struct timeval *tv, void *tz)
{
	if (tv == 0)
		return 0;
	return (int) onyx_rpc3 (rpc_gettimeofday, (long) tv, (long) tz, 0);
}

clock_t _times (struct tms *t)
{
	clock_t ticks = (clock_t) (__onyx_mono_ns () / (1000000000ULL / CLOCKS_PER_SEC));
	if (t)
	{
		t->tms_utime = ticks;
		t->tms_stime = 0;
		t->tms_cutime = 0;
		t->tms_cstime = 0;
	}
	return ticks;
}

/* timegm: UTC broken-down time -> seconds (mktime without the time zone) */
time_t timegm (struct tm *tm)
{
	int y = tm->tm_year + 1900, m = tm->tm_mon;
	y += m / 12;
	m %= 12;
	if (m < 0)
	{
		m += 12;
		y--;
	}
	long long days = days_from_civil (y, m + 1, 1) + tm->tm_mday - 1;
	long long t = ((days * 24 + tm->tm_hour) * 60 + tm->tm_min) * 60LL + tm->tm_sec;
	struct tm out;
	time_t r = (time_t) t;
	if (gmtime_r (&r, &out))
		*tm = out;
	return r;
}

/* timers and alarms: none (STUB-OK) */
unsigned alarm (unsigned s) { (void) s; return 0; }
int timer_create (clockid_t c, struct sigevent *e, timer_t *t) { (void) c; (void) e; (void) t; return ONYX_ERR (ENOSYS); }
int timer_delete (timer_t t) { (void) t; return ONYX_ERR (ENOSYS); }
int timer_settime (timer_t t, int f, const struct itimerspec *v, struct itimerspec *o) { (void) t; (void) f; (void) v; (void) o; return ONYX_ERR (ENOSYS); }
int timer_gettime (timer_t t, struct itimerspec *v) { (void) t; (void) v; return ONYX_ERR (ENOSYS); }
int timer_getoverrun (timer_t t) { (void) t; return ONYX_ERR (ENOSYS); }
int setitimer (int which, const struct itimerval *v, struct itimerval *o) { (void) which; (void) v; (void) o; return ONYX_ERR (ENOSYS); }
int getitimer (int which, struct itimerval *v) { (void) which; (void) v; return ONYX_ERR (ENOSYS); }
