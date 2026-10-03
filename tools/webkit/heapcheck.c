/*
 * heapcheck.c -- a checking malloc in front of newlib's, for hunting heap corruption in Web
 * (tools/webkit/build-web.sh HEAPCHECK=1; linked with --wrap=malloc,free,...). Each block gets a
 * header (a magic, its size, the callers that allocated and freed it) and a canary after it; a
 * freed block is filled with 0xDD and kept in a quarantine before newlib gets it back. Checked:
 *   - at free: the magic (a pointer that is not a block of ours, a double free), the canary (an
 *     overflow);
 *   - when a block leaves the quarantine: its fill (a write after free).
 * A finding is written to the standard error (the kernel log: kmsg) with the block's size, the
 * offset of the first byte changed, the allocation's and the free's return addresses (frame
 * pointers), then a deliberate fault makes the kernel print the backtrace of where it was found.
 * Pointers newlib allocated itself (strdup, FILE buffers) have no header: freed to newlib as they are.
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see fetch.sh).
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>

void *__real_malloc (size_t);
void __real_free (void *);
void *__real_memalign (size_t, size_t);
size_t __real_malloc_usable_size (void *);

#define MAGIC_LIVE	0x4C495645u		/* "LIVE" */
#define MAGIC_FREE	0x46524545u		/* "FREE" */
#define HDR		64
#define CANARY		16
#define NTRACE		3
#define QUARANTINE	8192
#define QBYTES_MAX	(96u << 20)		/* the quarantine holds at most this much */

struct hdr
{
	uint32_t magic, align;
	size_t size;
	void *base;				/* what newlib returned */
	uintptr_t alloc[NTRACE];		/* the allocating callers */
	uintptr_t freed[NTRACE - 1];	/* the freeing ones (then 0) */
};

static volatile int s_lock;
static void lock (void) { while (__atomic_exchange_n (&s_lock, 1, __ATOMIC_ACQUIRE)) sched_yield (); }
static void unlock (void) { __atomic_store_n (&s_lock, 0, __ATOMIC_RELEASE); }

static void *s_q[QUARANTINE];
static unsigned s_qhead, s_qn;
static size_t s_qbytes;

static void trace (uintptr_t *out, int n)
{
	uintptr_t *fp = (uintptr_t *) __builtin_frame_address (0);
	for (int i = 0; i < n; i++) out[i] = 0;
	/* skip our own frame and the wrapper's */
	for (int i = 0; i < n + 2 && fp; i++)
	{
		uintptr_t next = fp[0], lr = fp[1];
		if (i >= 2) out[i - 2] = lr;
		if (next <= (uintptr_t) fp || (next & 7)) break;
		fp = (uintptr_t *) next;
	}
}

static void report (const char *what, struct hdr *h, void *p, long off)
{
	char b[400];
	int n = snprintf (b, sizeof b,
		"heapcheck: %s: block %p size %zu, byte %ld changed; allocated from %lx %lx %lx; freed from %lx %lx\n",
		what, p, h ? h->size : 0, off, h ? (unsigned long) h->alloc[0] : 0, h ? (unsigned long) h->alloc[1] : 0,
		h ? (unsigned long) h->alloc[2] : 0, h ? (unsigned long) h->freed[0] : 0, h ? (unsigned long) h->freed[1] : 0);
	write (2, b, (size_t) n);
	*(volatile int *) 16 = 1;		/* (the kernel prints the backtrace) */
}

static void *make (size_t align, size_t n)
{
	if (align < 16) align = 16;
	size_t lead = align > HDR ? align : HDR;
	unsigned char *base = align > 16 ? (unsigned char *) __real_memalign (align, lead + n + CANARY)
					 : (unsigned char *) __real_malloc (lead + n + CANARY);
	if (!base) return 0;
	unsigned char *p = base + lead;
	struct hdr *h = (struct hdr *) (p - HDR);
	h->magic = MAGIC_LIVE;
	h->align = (uint32_t) align;
	h->size = n;
	h->base = base;
	trace (h->alloc, NTRACE);
	h->freed[0] = h->freed[1] = 0;
	memset (p + n, 0xC5, CANARY);
	return p;
}

static int ours (void *p)
{
	/* (the header is read before p: not below the heap's start, 10 GB -- kern/layout.h) */
	if (((uintptr_t) p & 15) != 0 || (uintptr_t) p < 0x280000000ull + 4096) return 0;
	struct hdr *h = (struct hdr *) ((unsigned char *) p - HDR);
	return h->magic == MAGIC_LIVE || h->magic == MAGIC_FREE;
}

static void check_fill (void *p)
{
	struct hdr *h = (struct hdr *) ((unsigned char *) p - HDR);
	if (h->magic != MAGIC_FREE) report ("header changed in quarantine", h, p, -1);
	unsigned char *c = (unsigned char *) p;
	for (size_t i = 0; i < h->size; i++)
		if (c[i] != 0xDD) report ("written after free", h, p, (long) i);
	for (int i = 0; i < CANARY; i++)
		if (c[h->size + i] != 0xC5) report ("canary changed in quarantine", h, p, (long) (h->size + i));
}

void *__wrap_malloc (size_t n) { return make (16, n); }
void *__wrap_memalign (size_t a, size_t n) { return make (a, n); }
void *__wrap_aligned_alloc (size_t a, size_t n) { return make (a, n); }
int __wrap_posix_memalign (void **out, size_t a, size_t n)
{
	void *p = make (a, n ? n : 1);
	if (!p) return 12;
	*out = p;
	return 0;
}
void *__wrap_calloc (size_t a, size_t b)
{
	size_t n = a * b;
	if (b && n / b != a) return 0;
	void *p = make (16, n);
	if (p) memset (p, 0, n);
	return p;
}

void __wrap_free (void *p)
{
	if (!p) return;
	if (!ours (p)) { __real_free (p); return; }	/* (newlib's own block) */
	struct hdr *h = (struct hdr *) ((unsigned char *) p - HDR);
	if (h->magic == MAGIC_FREE) report ("freed twice", h, p, -1);
	unsigned char *c = (unsigned char *) p;
	for (int i = 0; i < CANARY; i++)
		if (c[h->size + i] != 0xC5) report ("overflow (canary)", h, p, (long) (h->size + i));
	h->magic = MAGIC_FREE;
	uintptr_t t[NTRACE];
	trace (t, NTRACE);
	h->freed[0] = t[0]; h->freed[1] = t[1];
	memset (p, 0xDD, h->size);
	lock ();
	if (s_qn == QUARANTINE || s_qbytes + h->size > QBYTES_MAX)
	{
		while (s_qn && (s_qn == QUARANTINE || s_qbytes + h->size > QBYTES_MAX))
		{
			void *old = s_q[s_qhead];
			s_q[s_qhead] = 0;
			s_qhead = (s_qhead + 1) % QUARANTINE;
			s_qn--;
			struct hdr *oh = (struct hdr *) ((unsigned char *) old - HDR);
			s_qbytes -= oh->size;
			check_fill (old);
			oh->magic = 0;
			__real_free (oh->base);
		}
	}
	s_q[(s_qhead + s_qn) % QUARANTINE] = p;
	s_qn++;
	s_qbytes += h->size;
	unlock ();
}

void *__wrap_realloc (void *p, size_t n)
{
	if (!p) return make (16, n);
	if (!n) { __wrap_free (p); return 0; }
	if (!ours (p))					/* (newlib's own block: newlib's realloc) */
	{
		size_t old = __real_malloc_usable_size (p);
		void *q = make (16, n);
		if (q) { memcpy (q, p, old < n ? old : n); __real_free (p); }
		return q;
	}
	struct hdr *h = (struct hdr *) ((unsigned char *) p - HDR);
	void *q = make (h->align, n);
	if (!q) return 0;
	memcpy (q, p, h->size < n ? h->size : n);
	__wrap_free (p);
	return q;
}

size_t __wrap_malloc_usable_size (void *p)
{
	if (!p) return 0;
	if (!ours (p)) return __real_malloc_usable_size (p);
	return ((struct hdr *) ((unsigned char *) p - HDR))->size;
}
