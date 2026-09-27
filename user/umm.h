//
// umm.h -- a small user-space memory allocator for Onyx apps, on top of kapi_sbrk.
//
// Strategy: size-class free lists (a simple slab) for small blocks, a first-fit free
// list for large ones, all carved from an arena grown via kapi_sbrk in >=64 KB chunks.
// All memory lives in the app's own heap region (USER_HEAP_BASE, 10 GB) -> it is in
// the app's address space and freed wholesale when the app exits. There is no global
// free-back-to-the-OS (sbrk only grows here), which is fine: a process's whole heap
// is reclaimed on teardown.
//
// Pure C (no libc), header-only, static -- one private heap per app (one TU). C apps
// call umm_malloc/umm_free/...; the C++ support layer (crt0c++ / new) wires
// operator new/delete onto these. Freed heap pages show up in the app's page count
// (ps / memmon), since kapi_sbrk maps owned frames.
//
#ifndef ONYX_UMM_H
#define ONYX_UMM_H

#include "kapi.h"

typedef struct { unsigned long size; unsigned long magic; } umm_hdr;	// 16-byte header
#define UMM_MAGIC	0x4F4E5958554D4DUL				// "ONYXUMM"-ish
#define UMM_ALIGN	16
#define UMM_NCLASS	12

static const unsigned long umm__class[UMM_NCLASS] =
	{ 16, 32, 48, 64, 96, 128, 192, 256, 512, 1024, 2048, 4096 };

static void *umm__free[UMM_NCLASS];	// per-class free lists (next ptr stored in the block)
static void *umm__large;		// large-block free list (first-fit by stored size)
static char *umm__brk;			// arena bump pointer
static char *umm__end;			// end of the sbrk'd arena
static int   umm__started;

static inline unsigned long umm__round (unsigned long n)
{ return (n + (UMM_ALIGN - 1)) & ~(unsigned long) (UMM_ALIGN - 1); }

static inline int umm__classidx (unsigned long n)
{ for (int i = 0; i < UMM_NCLASS; i++) if (n <= umm__class[i]) return i; return -1; }

static inline int umm__grow (unsigned long need)
{
	unsigned long chunk = need < 0x10000UL ? 0x10000UL : umm__round (need);
	void *p = kapi_sbrk ((long) chunk);
	if (p == (void *) -1) return 0;
	if (!umm__started || (char *) p != umm__end)	// first chunk or a gap: (re)base the bump
	{ umm__brk = (char *) p; umm__end = (char *) p + chunk; umm__started = 1; }
	else						// contiguous: just extend
	{ umm__end += chunk; }
	return 1;
}

static inline void *umm__bump (unsigned long total)
{
	if (umm__brk + total > umm__end) { if (!umm__grow (total)) return 0; }
	void *p = umm__brk; umm__brk += total; return p;
}

static inline void *umm_malloc (unsigned long n)
{
	if (n == 0) n = 1;
	int ci = umm__classidx (n);
	unsigned long blk = (ci >= 0) ? umm__class[ci] : umm__round (n);

	if (ci >= 0 && umm__free[ci])			// small: pop the class free list
	{
		void *b = umm__free[ci]; umm__free[ci] = *(void **) b;
		((umm_hdr *) ((char *) b - sizeof (umm_hdr)))->magic = UMM_MAGIC;
		return b;
	}
	if (ci < 0)					// large: first-fit the large list
	{
		void **pp = &umm__large;
		while (*pp)
		{
			void *b = *pp;
			umm_hdr *h = (umm_hdr *) ((char *) b - sizeof (umm_hdr));
			if (h->size >= blk) { *pp = *(void **) b; h->magic = UMM_MAGIC; return b; }
			pp = (void **) b;
		}
	}

	void *raw = umm__bump (sizeof (umm_hdr) + blk);	// carve a fresh block
	if (raw == 0) return 0;
	umm_hdr *h = (umm_hdr *) raw; h->size = blk; h->magic = UMM_MAGIC;
	return (char *) raw + sizeof (umm_hdr);
}

static inline void umm_free (void *p)
{
	if (p == 0) return;
	umm_hdr *h = (umm_hdr *) ((char *) p - sizeof (umm_hdr));
	if (h->magic != UMM_MAGIC) return;		// not ours / double-free guard
	h->magic = 0;
	int ci = umm__classidx (h->size);
	if (ci >= 0) { *(void **) p = umm__free[ci]; umm__free[ci] = p; }
	else         { *(void **) p = umm__large;    umm__large    = p; }
}

static inline void *umm_calloc (unsigned long n, unsigned long sz)
{
	unsigned long t = n * sz;
	void *p = umm_malloc (t);
	if (p) { char *c = (char *) p; for (unsigned long i = 0; i < t; i++) c[i] = 0; }
	return p;
}

static inline void *umm_realloc (void *p, unsigned long n)
{
	if (p == 0) return umm_malloc (n);
	if (n == 0) { umm_free (p); return 0; }
	umm_hdr *h = (umm_hdr *) ((char *) p - sizeof (umm_hdr));
	if (h->size >= n) return p;			// already big enough
	void *np = umm_malloc (n);
	if (np == 0) return 0;
	char *s = (char *) p, *d = (char *) np;
	for (unsigned long i = 0; i < h->size; i++) d[i] = s[i];
	umm_free (p);
	return np;
}

#endif // ONYX_UMM_H
