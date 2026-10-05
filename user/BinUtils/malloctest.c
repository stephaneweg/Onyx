/*
 * malloctest.c -- /bin/malloctest: is every byte malloc_usable_size() promises really the block's?
 * (docs/03 §5.4; docs/08-WEBKIT-PORT.md "Step 3": Web's heap corruption on the Pi.)
 *
 *   malloctest [phase...] [-s seed] [-n rounds] [-t threads]
 *       phases: sizes align realloc top sbrk mix threads (default: all)
 *
 * Every block is filled up to malloc_usable_size() with a pattern of its own (its slot and the
 * byte's offset) and its usable size is remembered; the checks, after every phase and every few
 * hundred operations of the random ones: each live block still has its pattern (nothing wrote into
 * it: a neighbour filled to its usable size, the allocator's own chunk headers) and still reports
 * the same usable size (its header is whole). The phases:
 *   sizes    malloc / calloc of every size 0..1024 and around the powers of two up to 4 MB
 *   align    memalign / posix_memalign / aligned_alloc, alignments 16..1 MB, between small blocks
 *   realloc  growing and shrinking in place and by moving, next to free and to used neighbours
 *   top      blocks next to the top of the heap while it grows and is given back (newlib trims the
 *            top above 128 KB: sbrk with a negative increment, the kernel drops the pages)
 *   sbrk     sbrk() called by the program between allocations (newlib's fenceposts), odd increments
 *   mix      all of it at random (-n rounds, default 200000)
 *   threads  the random mix in -t threads at once (default 4), each with its own blocks
 * A finding prints the block (address, the size asked, the usable size, what made it), the first
 * byte changed and the 16 bytes there, and the last operations; the exit status is the number of
 * findings (0: the usable size can be trusted).
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <malloc.h>
#include <pthread.h>

enum { K_MALLOC, K_CALLOC, K_MEMALIGN, K_POSIX_MEMALIGN, K_ALIGNED_ALLOC, K_REALLOC };
static const char *const s_kind[] = { "malloc", "calloc", "memalign", "posix_memalign", "aligned_alloc", "realloc" };

struct block
{
	unsigned char *p;
	size_t asked, usable, align;
	int kind;
};

#define NHIST	8
struct pool
{
	struct block *b;
	int n;				/* slots */
	unsigned id;			/* (the pattern's salt: the thread) */
	uint64_t rng;
	size_t live;			/* bytes in use */
	char hist[NHIST][96];		/* the last operations */
	unsigned nhist;
	int bad;
};

static int s_bad;
static pthread_mutex_t s_out = PTHREAD_MUTEX_INITIALIZER;

static uint64_t rnd (struct pool *q)
{
	q->rng ^= q->rng << 13; q->rng ^= q->rng >> 7; q->rng ^= q->rng << 17;
	return q->rng;
}

static inline unsigned char pat (const struct pool *q, int slot, size_t i)
{
	return (unsigned char) (0xA5 ^ (slot * 31u) ^ (q->id * 97u) ^ (i * 7u) ^ (i >> 8));
}

static void note (struct pool *q, const char *op, int slot, size_t a, size_t b, void *p)
{
	snprintf (q->hist[q->nhist++ % NHIST], sizeof q->hist[0], "%s slot %d (%zu, %zu) -> %p", op, slot, a, b, p);
}

static void finding (struct pool *q, int slot, const char *what, size_t off)
{
	struct block *b = &q->b[slot];
	pthread_mutex_lock (&s_out);
	printf ("FAIL  %s: block %p (%s, asked %zu, align %zu, usable %zu, now %zu), pool %u slot %d",
		what, (void *) b->p, s_kind[b->kind], b->asked, b->align, b->usable, malloc_usable_size (b->p), q->id, slot);
	if (off != (size_t) -1)
	{
		size_t from = off & ~(size_t) 7;
		printf (", byte %zu (%s the size asked) is %02x, not %02x; from %zu:", off,
			off < b->asked ? "within" : "beyond", b->p[off], pat (q, slot, off), from);
		for (size_t i = from; i < from + 16 && i < b->usable; i++)
			printf (" %02x", b->p[i]);
	}
	printf ("\n");
	for (unsigned i = 0; i < NHIST && i < q->nhist; i++)
		printf ("      %s\n", q->hist[(q->nhist - 1 - i) % NHIST]);
	pthread_mutex_unlock (&s_out);
	q->bad++;
}

static void fill (struct pool *q, int slot)
{
	struct block *b = &q->b[slot];
	for (size_t i = 0; i < b->usable; i++)
		b->p[i] = pat (q, slot, i);
}

/* One block: its pattern and its usable size. 1 when whole. */
static int check_one (struct pool *q, int slot)
{
	struct block *b = &q->b[slot];
	if (b->p == 0)
		return 1;
	if (malloc_usable_size (b->p) != b->usable)
	{
		finding (q, slot, "the usable size changed (the block's header was written)", (size_t) -1);
		b->usable = b->asked;				/* (go on with what is sure) */
		return 0;
	}
	for (size_t i = 0; i < b->usable; i++)
		if (b->p[i] != pat (q, slot, i))
		{
			finding (q, slot, "a block's bytes changed", i);
			fill (q, slot);
			return 0;
		}
	return 1;
}

static void check_all (struct pool *q)
{
	for (int i = 0; i < q->n; i++)
		check_one (q, i);
}

/* A new block in the slot (freed first). */
static void *get (struct pool *q, int slot, int kind, size_t size, size_t align)
{
	struct block *b = &q->b[slot];
	void *p = 0;
	if (b->p)
	{
		check_one (q, slot);
		q->live -= b->usable;
		note (q, "free", slot, b->asked, b->usable, b->p);
		free (b->p);
		b->p = 0;
	}
	switch (kind)
	{
	case K_MALLOC:		p = malloc (size); break;
	case K_CALLOC:		p = calloc (1, size); break;
	case K_MEMALIGN:	p = memalign (align, size); break;
	case K_POSIX_MEMALIGN:	if (posix_memalign (&p, align, size) != 0) p = 0; break;
	case K_ALIGNED_ALLOC:	p = aligned_alloc (align, size); break;
	}
	note (q, s_kind[kind], slot, size, align, p);
	if (p == 0)
		return 0;
	b->p = p; b->asked = size; b->align = align; b->kind = kind;
	b->usable = malloc_usable_size (p);
	if (b->usable < size)
	{
		finding (q, slot, "the usable size is below the size asked", (size_t) -1);
		b->usable = size;
	}
	if (align > 1 && ((uintptr_t) p & (align - 1)) != 0)
		finding (q, slot, "not aligned", (size_t) -1);
	if (kind == K_CALLOC)
		for (size_t i = 0; i < size; i++)
			if (b->p[i] != 0)
			{
				finding (q, slot, "calloc: not zero", (size_t) -1);
				break;
			}
	fill (q, slot);
	q->live += b->usable;
	return p;
}

static void drop (struct pool *q, int slot)
{
	struct block *b = &q->b[slot];
	if (b->p == 0)
		return;
	check_one (q, slot);
	q->live -= b->usable;
	note (q, "free", slot, b->asked, b->usable, b->p);
	free (b->p);
	b->p = 0;
}

/* realloc: the first min(old asked, new) bytes are kept; then the block is filled anew. */
static void resize (struct pool *q, int slot, size_t size)
{
	struct block *b = &q->b[slot];
	if (b->p == 0)
	{
		get (q, slot, K_MALLOC, size, 0);
		return;
	}
	check_one (q, slot);
	if (size == 0)
		size = 1;					/* (realloc (p, 0): frees or not, as the library likes) */
	size_t keep = b->asked < size ? b->asked : size;
	unsigned char *p = realloc (b->p, size);
	note (q, "realloc", slot, b->asked, size, p);
	if (p == 0)
		return;						/* (the old block stays) */
	q->live -= b->usable;
	b->p = p; b->kind = K_REALLOC; b->align = 0;
	size_t u = malloc_usable_size (p);
	for (size_t i = 0; i < keep; i++)
		if (p[i] != pat (q, slot, i))
		{
			b->usable = u;
			finding (q, slot, "realloc lost the contents", i);
			break;
		}
	b->asked = size; b->usable = u;
	if (u < size)
	{
		finding (q, slot, "the usable size is below the size asked", (size_t) -1);
		b->usable = size;
	}
	fill (q, slot);
	q->live += b->usable;
}

static void pool_init (struct pool *q, int n, unsigned id, uint64_t seed)
{
	memset (q, 0, sizeof *q);
	q->b = calloc ((size_t) n, sizeof (struct block));
	q->n = n; q->id = id; q->rng = seed * 0x9E3779B97F4A7C15ull + id + 1;
}

static void pool_done (struct pool *q, const char *name)
{
	check_all (q);
	for (int i = 0; i < q->n; i++)
		drop (q, i);
	free (q->b);
	pthread_mutex_lock (&s_out);
	if (name)
		printf ("%s  %s\n", q->bad ? "FAIL" : "PASS", name);
	s_bad += q->bad;
	pthread_mutex_unlock (&s_out);
}

/* ---- the phases ---- */
static void phase_sizes (uint64_t seed)
{
	struct pool q;
	pool_init (&q, 4096, 1, seed);
	int s = 0;
	for (size_t n = 0; n <= 1024; n++)
	{
		get (&q, s++, n & 1 ? K_CALLOC : K_MALLOC, n, 0);
		get (&q, s++, K_MALLOC, n, 0);
	}
	check_all (&q);
	for (int i = 0; i < s; i += 2)				/* every other one: free neighbours */
		drop (&q, i);
	check_all (&q);
	for (int i = 0; i < s; i += 2)				/* and reused, a little smaller or larger */
		get (&q, i, K_MALLOC, (size_t) (i / 2) + rnd (&q) % 17, 0);
	check_all (&q);
	for (int sh = 11; sh <= 22; sh++)
		for (int d = -24; d <= 24; d += 8)
			get (&q, s++, K_MALLOC, ((size_t) 1 << sh) + d, 0);
	pool_done (&q, "sizes: malloc / calloc 0..1024 bytes and around the powers of two, filled to the usable size");
}

static void phase_align (uint64_t seed)
{
	struct pool q;
	pool_init (&q, 4096, 2, seed);
	int s = 0;
	for (int round = 0; round < 3; round++)
		for (size_t al = 16; al <= (1u << 20); al <<= 1)
			for (int k = 0; k < 9; k++)
			{
				static const size_t sz[9] = { 1, 8, 24, 40, 100, 1000, 4096, 65536, 100000 };
				size_t n = sz[k] + (round ? rnd (&q) % 64 : 0);
				int kind = k % 3 == 0 ? K_MEMALIGN : k % 3 == 1 ? K_POSIX_MEMALIGN : K_ALIGNED_ALLOC;
				if (kind == K_ALIGNED_ALLOC)
					n = (n + al - 1) & ~(al - 1);	/* (C11: a multiple of the alignment) */
				if (s >= q.n - 2)
					break;
				get (&q, s++, K_MALLOC, 1 + rnd (&q) % 200, 0);	/* small ones in between */
				get (&q, s++, kind, n, al);
			}
	check_all (&q);
	for (int i = 0; i < s; i += 3)
		drop (&q, i);
	check_all (&q);
	for (int i = 0; i < s; i += 3)
		get (&q, i, K_MEMALIGN, 1 + rnd (&q) % 3000, (size_t) 32 << (rnd (&q) % 8));
	pool_done (&q, "align: memalign / posix_memalign / aligned_alloc, alignments 16..1 MB, filled to the usable size");
}

static void phase_realloc (uint64_t seed)
{
	struct pool q;
	pool_init (&q, 1024, 3, seed);
	for (int i = 0; i < q.n; i++)
		get (&q, i, K_MALLOC, 1 + rnd (&q) % 600, 0);
	for (int round = 0; round < 40; round++)
	{
		for (int i = round & 1; i < q.n; i += 2)
		{
			size_t n = q.b[i].asked;
			switch (rnd (&q) % 5)
			{
			case 0: n += 1 + rnd (&q) % 16; break;			/* into the slack */
			case 1: n += 16 + rnd (&q) % 4096; break;
			case 2: n = n > 40 ? n - 1 - rnd (&q) % 32 : n + 8; break;
			case 3: n = 1 + rnd (&q) % 20000; break;
			case 4: drop (&q, i); continue;				/* a free neighbour */
			}
			resize (&q, i, n);
		}
		check_all (&q);
	}
	pool_done (&q, "realloc: growing and shrinking, in place and moved, filled to the usable size");
}

/* The top of the heap grown and given back, with live blocks just below it. */
static void phase_top (uint64_t seed)
{
	struct pool q;
	pool_init (&q, 512, 4, seed);
	char *brk0 = sbrk (0), *lo = brk0, *hi = brk0;
	for (int round = 0; round < 24; round++)
	{
		int s = 0;
		get (&q, s++, K_MALLOC, 24 + rnd (&q) % 4000, 0);		/* stays, below the big ones */
		for (int i = 0; i < 40; i++)
		{
			get (&q, s++, K_MALLOC, (200u << 10) + rnd (&q) % (300u << 10), 0);
			get (&q, s++, K_MALLOC, 1 + rnd (&q) % 300, 0);		/* a small one next to the top */
		}
		char *b = sbrk (0);
		if (b > hi) hi = b;
		check_all (&q);
		for (int i = s - 1; i >= 1; i--)				/* from the top down: trimmed */
		{
			drop (&q, i);
			if ((i & 7) == 0)
				check_all (&q);
		}
		b = sbrk (0);
		if (b < lo) lo = b;
		check_all (&q);
		for (int i = 1; i < 60; i++)					/* the top again, pages new */
			get (&q, i, round & 1 ? K_MEMALIGN : K_MALLOC, 1 + rnd (&q) % 70000, round & 1 ? 64 : 0);
		check_all (&q);
		for (int i = 1; i < 60; i++)
			drop (&q, i);
	}
	printf ("      (the break: %p at the start, up to %p, back down to %p)\n", (void *) brk0, (void *) hi, (void *) lo);
	pool_done (&q, "top: blocks next to the top of the heap while it grows and is trimmed (sbrk < 0)");
}

/* sbrk() by the program between allocations: newlib sees a foreign break (the fenceposts). */
static void phase_sbrk (uint64_t seed)
{
	struct pool q;
	pool_init (&q, 2048, 5, seed);
	int s = 0, moved = 0;
	for (int round = 0; round < 60 && s < q.n - 24; round++)
	{
		for (int i = 0; i < 20; i++)					/* use the top up, in pieces of all sizes */
			get (&q, s++, i & 1 ? K_MALLOC : K_MEMALIGN, 1 + rnd (&q) % (i < 16 ? 9000 : 150000), i & 1 ? 0 : 32);
		static const long inc[6] = { 1, 8, 24, 100, 4096, 65536 + 7 };
		char *b = sbrk (inc[round % 6]);				/* ours, not malloc's */
		if (b != (char *) -1)
		{
			memset (b, 0xEE, (size_t) inc[round % 6]);
			moved++;
		}
		get (&q, s++, K_MALLOC, 300000 + rnd (&q) % 100000, 0);	/* more than the top has: extended */
		check_all (&q);
		for (int i = s - 21; i < s; i += 2)
			drop (&q, i);
		check_all (&q);
	}
	printf ("      (%d foreign sbrk calls)\n", moved);
	pool_done (&q, "sbrk: the program's own sbrk() between allocations (fenceposts), filled to the usable size");
}

static void mix (struct pool *q, long rounds)
{
	for (long r = 0; r < rounds; r++)
	{
		int slot = (int) (rnd (q) % (unsigned) q->n);
		unsigned op = (unsigned) (rnd (q) % 100);
		size_t n;
		unsigned c = (unsigned) (rnd (q) % 100);
		if (c < 60) n = rnd (q) % 256;					/* small: the bins */
		else if (c < 90) n = rnd (q) % 4096;
		else if (c < 99) n = rnd (q) % 70000;
		else n = rnd (q) % (600u << 10);				/* above the trim threshold */
		if (q->live > (48u << 20) && op < 80)
			op = 99;						/* enough in use: free */
		if (op < 35) get (q, slot, K_MALLOC, n, 0);
		else if (op < 42) get (q, slot, K_CALLOC, n, 0);
		else if (op < 52) get (q, slot, K_MEMALIGN, n, (size_t) 32 << (rnd (q) % 12));
		else if (op < 56) get (q, slot, K_POSIX_MEMALIGN, n, (size_t) 16 << (rnd (q) % 8));
		else if (op < 72) resize (q, slot, n);
		else drop (q, slot);
		if ((r & 1023) == 1023)
			check_all (q);
	}
}

static void phase_mix (uint64_t seed, long rounds)
{
	struct pool q;
	pool_init (&q, 3000, 6, seed);
	mix (&q, rounds);
	pool_done (&q, "mix: malloc / calloc / memalign / realloc / free at random, filled to the usable size");
}

static long s_rounds;
static void *t_mix (void *a)
{
	struct pool *q = a;
	mix (q, s_rounds);
	pool_done (q, 0);
	return (void *) (long) q->bad;
}

static void phase_threads (uint64_t seed, long rounds, int nt)
{
	pthread_t th[16];
	static struct pool q[16];
	int before = s_bad, started = 0;
	if (nt > 16) nt = 16;
	s_rounds = rounds / nt;
	for (int i = 0; i < nt; i++)
	{
		pool_init (&q[i], 1500, 10 + (unsigned) i, seed);
		if (pthread_create (&th[i], 0, t_mix, &q[i]) != 0)
			break;
		started++;
	}
	for (int i = 0; i < started; i++)
		pthread_join (th[i], 0);
	printf ("%s  threads: the random mix in %d threads at once, filled to the usable size\n",
		started == nt && s_bad == before ? "PASS" : "FAIL", started);
	if (started != nt)
		s_bad++;
}

int main (int argc, char **argv)
{
	uint64_t seed = 1;
	long rounds = 200000;
	int nt = 4, any = 0;
	unsigned want = 0;
	static const char *const names[] = { "sizes", "align", "realloc", "top", "sbrk", "mix", "threads" };
	for (int i = 1; i < argc; i++)
	{
		if (argv[i][0] == '-' && i + 1 < argc)
		{
			long v = strtol (argv[++i], 0, 0);
			switch (argv[i - 1][1])
			{
			case 's': seed = (uint64_t) v; break;
			case 'n': rounds = v; break;
			case 't': nt = (int) v; break;
			}
			continue;
		}
		for (unsigned k = 0; k < 7; k++)
			if (strcmp (argv[i], names[k]) == 0)
			{
				want |= 1u << k;
				any = 1;
			}
	}
	if (!any)
		want = 0x7F;
	static char outbuf[1024];
	setvbuf (stdout, outbuf, _IOLBF, sizeof outbuf);		/* (a line at a time: seen as it runs) */
	printf ("malloctest: seed %lu, %ld rounds, %d threads\n", (unsigned long) seed, rounds, nt);
	if (want & 1) phase_sizes (seed);
	if (want & 2) phase_align (seed);
	if (want & 4) phase_realloc (seed);
	if (want & 8) phase_top (seed);
	if (want & 16) phase_sbrk (seed);
	if (want & 32) phase_mix (seed, rounds);
	if (want & 64) phase_threads (seed, rounds, nt);
	printf ("malloctest: %d finding(s)\n", s_bad);
	return s_bad > 255 ? 255 : s_bad;
}
