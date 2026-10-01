/*
 * Copyright 2008 Vincent Sanders <vince@simtec.co.uk>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Onyx: the scheduled callbacks in a binary heap by due time (then by the order they were
 * scheduled), found by (callback, context) in a hash table. It was a linked list, newest
 * first: each schedule searched the whole list (its "one per callback and context" rule),
 * each run walked it again from its head after every callback fired -- n pending timers of
 * a page, n^2 (8000 setTimeout, 0.7 s on the PC) -- and callbacks due together ran newest
 * first (setTimeout(a, 0); setTimeout(b, 0) ran b, then a). Docs: docs/06-JET-BROWSER.md §35.
 */

#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "utils/sys_time.h"
#include "utils/log.h"

#include "framebuffer/schedule.h"

/**
 * scheduled callback.
 */
struct nscallback
{
	struct timeval tv;		/**< when it is due */
	uint64_t seq;			/**< the order it was scheduled in */
	void (*callback)(void *p);
	void *p;
	unsigned int at;		/**< its index in the heap */
	struct nscallback *hnext;	/**< in its hash bucket */
};

static struct nscallback **heap;	/* heap[0]: the soonest */
static unsigned int heap_n, heap_cap;
static struct nscallback **htab;	/* buckets, a power of 2 */
static unsigned int htab_n;
static uint64_t sched_seq;

static unsigned int sched_hash(void (*callback)(void *p), void *p)
{
	uintptr_t h = (uintptr_t) p * 0x9E3779B1u ^ (uintptr_t) callback * 0x85EBCA77u;

	return (unsigned int) (h ^ (h >> 15)) & (htab_n - 1);
}

static bool sched_before(const struct nscallback *a, const struct nscallback *b)
{
	if (timercmp(&a->tv, &b->tv, !=))
		return timercmp(&a->tv, &b->tv, <);
	return a->seq < b->seq;
}

static void heap_set(unsigned int i, struct nscallback *e)
{
	heap[i] = e;
	e->at = i;
}

static void heap_up(unsigned int i)
{
	struct nscallback *e = heap[i];

	while (i > 0) {
		unsigned int parent = (i - 1) / 2;
		if (!sched_before(e, heap[parent]))
			break;
		heap_set(i, heap[parent]);
		i = parent;
	}
	heap_set(i, e);
}

static void heap_down(unsigned int i)
{
	struct nscallback *e = heap[i];

	for (;;) {
		unsigned int c = 2 * i + 1;
		if (c >= heap_n)
			break;
		if (c + 1 < heap_n && sched_before(heap[c + 1], heap[c]))
			c++;
		if (!sched_before(heap[c], e))
			break;
		heap_set(i, heap[c]);
		i = c;
	}
	heap_set(i, e);
}

/* e out of the heap and of the hash table (not freed) */
static void sched_take(struct nscallback *e)
{
	struct nscallback **b = &htab[sched_hash(e->callback, e->p)];
	unsigned int i = e->at;

	while (*b != e)
		b = &(*b)->hnext;
	*b = e->hnext;

	heap_n--;
	if (i != heap_n) {
		heap_set(i, heap[heap_n]);
		if (i > 0 && sched_before(heap[i], heap[(i - 1) / 2]))
			heap_up(i);
		else
			heap_down(i);
	}
}

static bool sched_grow(void)
{
	if (heap_n == heap_cap) {
		unsigned int cap = heap_cap ? heap_cap * 2 : 64;
		struct nscallback **h = realloc(heap, cap * sizeof(*h));
		if (h == NULL)
			return false;
		heap = h;
		heap_cap = cap;
	}
	if (heap_n + 1 > htab_n) {
		unsigned int n = htab_n ? htab_n * 2 : 64, i;
		struct nscallback **t = calloc(n, sizeof(*t)), **old = htab;
		unsigned int oldn = htab_n;
		if (t == NULL)
			return htab_n != 0;	/* (the old table: longer chains) */
		htab = t;
		htab_n = n;
		for (i = 0; i < oldn; i++) {
			struct nscallback *e = old[i], *next;
			for (; e != NULL; e = next) {
				unsigned int h = sched_hash(e->callback, e->p);
				next = e->hnext;
				e->hnext = htab[h];
				htab[h] = e;
			}
		}
		free(old);
	}
	return true;
}

/**
 * Unschedule a callback.
 *
 * \param  callback  callback function
 * \param  p         user parameter, passed to callback function
 *
 * All scheduled callbacks matching both callback and p are removed.
 */

static nserror schedule_remove(void (*callback)(void *p), void *p)
{
	struct nscallback *e;

	if (heap_n == 0)
		return NSERROR_OK;

	NSLOG(schedule, DEBUG, "removing %p, %p", callback, p);

	/* (one at most: framebuffer_schedule keeps them unique) */
	for (e = htab[sched_hash(callback, p)]; e != NULL; e = e->hnext) {
		if (e->callback == callback && e->p == p) {
			sched_take(e);
			free(e);
			break;
		}
	}

	return NSERROR_OK;
}

/* exported function documented in framebuffer/schedule.h */
nserror framebuffer_schedule(int tival, void (*callback)(void *p), void *p)
{
	struct nscallback *nscb;
	struct timeval tv;
	unsigned int h;
	nserror ret;

	/* ensure uniqueness of the callback and context */
	ret = schedule_remove(callback, p);
	if ((tival < 0) || (ret != NSERROR_OK)) {
		return ret;
	}

	NSLOG(schedule, DEBUG, "Adding %p(%p) in %d", callback, p, tival);

	tv.tv_sec = tival / 1000; /* miliseconds to seconds */
	tv.tv_usec = (tival % 1000) * 1000; /* remainder to microseconds */

	nscb = calloc(1, sizeof(struct nscallback));
	if (nscb == NULL || !sched_grow()) {
		free(nscb);
		return NSERROR_NOMEM;
	}

	gettimeofday(&nscb->tv, NULL);
	timeradd(&nscb->tv, &tv, &nscb->tv);

	nscb->callback = callback;
	nscb->p = p;
	nscb->seq = ++sched_seq;

	h = sched_hash(callback, p);
	nscb->hnext = htab[h];
	htab[h] = nscb;
	heap_set(heap_n, nscb);
	heap_n++;
	heap_up(heap_n - 1);

	return NSERROR_OK;
}

/* Onyx: the callbacks run in one go before the main loop takes its events again (the
 * page's timers: a heavy page's scripts would otherwise keep the window from responding) */
#define SCHEDULE_BUDGET_US 40000

/* Onyx: and sooner when the user clicked, turned the wheel or typed meanwhile (asked every
 * few ms: onyx_chrome.cpp pumps the window's events) -- a click waits for one callback at
 * most, not for the budget's worth */
#define SCHEDULE_INPUT_CHECK_US 4000
extern int onyx_chrome_input_pending(void) __attribute__((weak));

/* exported function documented in framebuffer/schedule.h */
int schedule_run(void)
{
	struct timeval start, now, checked;
	struct timeval tv;
	struct timeval rettime;
	struct nscallback *e;

	if (heap_n == 0)
		return -1;

	gettimeofday(&tv, NULL);
	start = checked = tv;

	/* the callbacks due when the run began, soonest first (one scheduled meanwhile
	 * waits for the next run) */
	while (heap_n > 0 && timercmp(&tv, &heap[0]->tv, >)) {
		e = heap[0];
		sched_take(e);

		e->callback(e->p);

		free(e);

		if (heap_n == 0)
			return -1; /* no more callbacks scheduled */

		/* Onyx: past the budget, the events first (the rest at once after) */
		gettimeofday(&now, NULL);
		if ((now.tv_sec - start.tv_sec) * 1000000L +
		    (now.tv_usec - start.tv_usec) > SCHEDULE_BUDGET_US)
			return 0;
		if (onyx_chrome_input_pending != NULL &&
		    (now.tv_sec - checked.tv_sec) * 1000000L +
		    (now.tv_usec - checked.tv_usec) > SCHEDULE_INPUT_CHECK_US) {
			checked = now;
			if (onyx_chrome_input_pending())
				return 0;
		}
	}

	if (heap_n == 0)
		return -1;

	/* make rettime relative to now */
	timersub(&heap[0]->tv, &tv, &rettime);

	NSLOG(schedule, DEBUG,
	      "returning time to next event as %ldms",
	      (rettime.tv_sec * 1000) + (rettime.tv_usec / 1000));

	/* return next event time in milliseconds (24days max wait) */
	return (rettime.tv_sec * 1000) + (rettime.tv_usec / 1000);
}

void list_schedule(void)
{
	struct timeval tv;
	unsigned int i;

	gettimeofday(&tv, NULL);

	NSLOG(netsurf, INFO, "schedule list at %ld:%ld", tv.tv_sec,
	      tv.tv_usec);

	for (i = 0; i < heap_n; i++) {
		NSLOG(netsurf, INFO, "Schedule %p at %ld:%ld", heap[i],
		      heap[i]->tv.tv_sec, heap[i]->tv.tv_usec);
	}
}


/*
 * Local Variables:
 * c-basic-offset:8
 * End:
 */
