/*
 * sys/features.h -- libonyxposix's overlay of newlib's <sys/features.h> (docs/POSIX-PLAN.md §3.4).
 *
 * newlib takes its POSIX option macros (_POSIX_THREADS, _POSIX_TIMERS, _POSIX_MONOTONIC_CLOCK...)
 * from this header, and only defines them for a few hosted targets (RTEMS, Cygwin): on
 * aarch64-none-elf none is set, so <time.h> hides clock_gettime / nanosleep and <sched.h>
 * sched_yield. libonyxposix implements them, so this overlay (first on the include path:
 * -isystem $ONYX_SYSROOT/include) pulls newlib's header, then states what Onyx provides.
 *
 * ---------------------------------------------------------------------------------------------
 * MIT License
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ---------------------------------------------------------------------------------------------
 */
#ifndef _ONYX_SYS_FEATURES_H
#define _ONYX_SYS_FEATURES_H

#include_next <sys/features.h>

/* The target: a POSIX-ish static environment over the Onyx kapi (v75). */
#ifndef __onyx__
#define __onyx__		1
#endif
#define __ONYX_POSIX__		1		/* libonyxposix's headers are in use */

#ifndef _POSIX_VERSION
#define _POSIX_VERSION			200809L
#endif
#define _POSIX_THREADS			200809L
#define _POSIX_THREAD_ATTR_STACKSIZE	200809L
#define _POSIX_THREAD_ATTR_STACKADDR	200809L
#define _POSIX_THREAD_SAFE_FUNCTIONS	200809L
#define _POSIX_THREAD_PRIORITY_SCHEDULING 200809L
#define _POSIX_THREAD_CPUTIME		200809L
#define _POSIX_TIMEOUTS			200809L
#define _POSIX_READER_WRITER_LOCKS	200809L
#define _POSIX_SPIN_LOCKS		200809L
#define _POSIX_BARRIERS			200809L
#define _POSIX_SEMAPHORES		200809L
#define _POSIX_TIMERS			200809L	/* clock_gettime, nanosleep (timer_create: ENOSYS) */
#define _POSIX_MONOTONIC_CLOCK		200809L
#define _POSIX_CLOCK_SELECTION		200809L
#define _POSIX_CPUTIME			200809L
#define _POSIX_PRIORITY_SCHEDULING	200809L	/* sched_* (one policy: the kernel's) */
#define _POSIX_SPAWN			200809L
#define _POSIX_MAPPED_FILES		200809L
#define _POSIX_MEMORY_PROTECTION	200809L
#define _POSIX_FSYNC			200809L
#define _POSIX_NO_TRUNC			1
#define _UNIX98_THREAD_MUTEX_ATTRIBUTES	1

#endif /* _ONYX_SYS_FEATURES_H */
