//
// hostmem.cpp -- the host's memory for the guests (onyxrun.h): each process's user range is one host reservation
// (56 GB of address space, nothing committed), its regions committed there at the same offset -- so two regions next
// to each other in the guest are next to each other in the host too, and a buffer may span both. A shared buffer (a
// window's pixels, a shm object) is host memory of its own, mapped into every process that has it.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

// ---- the host ----------------------------------------------------------------------------------------------------
u8 *host_reserve (u64 len)
{
#ifdef _WIN32
	return (u8 *) VirtualAlloc (0, len, MEM_RESERVE, PAGE_NOACCESS);
#else
	void *p = mmap (0, len, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	return p == MAP_FAILED ? 0 : (u8 *) p;
#endif
}

void host_release_all (u8 *p, u64 len)
{
#ifdef _WIN32
	(void) len;
	VirtualFree (p, 0, MEM_RELEASE);
#else
	munmap (p, len);
#endif
}

bool host_commit (u8 *p, u64 len)
{
#ifdef _WIN32
	return VirtualAlloc (p, len, MEM_COMMIT, PAGE_READWRITE) == p;
#else
	return mmap (p, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == p;
#endif
}

void host_decommit (u8 *p, u64 len)
{
#ifdef _WIN32
	VirtualFree (p, len, MEM_DECOMMIT);
#else
	mmap (p, len, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
#endif
}

u8 *host_shared_alloc (u64 len)
{
#ifdef _WIN32
	return (u8 *) VirtualAlloc (0, len, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
	void *p = mmap (0, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? 0 : (u8 *) p;
#endif
}

void host_shared_free (u8 *p, u64 len)
{
#ifdef _WIN32
	(void) len;
	VirtualFree (p, 0, MEM_RELEASE);
#else
	munmap (p, len);
#endif
}

// ---- a process's memory -------------------------------------------------------------------------------------------
static const u64 RANGE_LO = USER_VA_BASE, RANGE_HI = USER_VA_CEILING;

bool Mem::init ()
{
	base = host_reserve (RANGE_HI - RANGE_LO);
	if (!base) rlog ("cannot reserve %llu GB of host address space for a process", (unsigned long long) ((RANGE_HI - RANGE_LO) >> 30));
	return base != 0;
}

void Mem::fini ()
{
	if (base) host_release_all (base, RANGE_HI - RANGE_LO);
	base = 0;
	regions.clear ();
}

bool Mem::overlaps (u64 va, u64 len) const
{
	auto it = regions.upper_bound (va);
	if (it != regions.end () && it->first < va + len) return true;
	if (it != regions.begin ())
	{
		--it;
		if (it->first + it->second.len > va) return true;
	}
	return false;
}

bool Mem::map (u64 va, u64 len, unsigned prot, const char *what, int kind)
{
	if (len == 0 || (va & (HM_GRAIN - 1)) || (len & (HM_GRAIN - 1)) || va < RANGE_LO || va + len > RANGE_HI || va + len < va)
		return false;
	if (overlaps (va, len)) return false;
	u8 *h = base + (va - RANGE_LO);
	if (!host_commit (h, len)) { rlog ("out of host memory (%llu KB for %s)", (unsigned long long) (len >> 10), what); return false; }
	regions[va] = Region { va, len, prot, what, h, false, kind };
	log.push_back (MemOp { 0, va, len, prot, h });
	return true;
}

bool Mem::map_shared (u64 va, u64 len, unsigned prot, const char *what, u8 *host, int kind)
{
	if (len == 0 || (va & (HM_GRAIN - 1)) || (len & (HM_GRAIN - 1)) || va < RANGE_LO || va + len > RANGE_HI || va + len < va)
		return false;
	if (overlaps (va, len)) return false;
	regions[va] = Region { va, len, prot, what, host, true, kind };
	log.push_back (MemOp { 0, va, len, prot, host });
	return true;
}

void Mem::split_at (u64 a)
{
	auto it = regions.upper_bound (a);
	if (it == regions.begin ()) return;
	--it;
	Region r = it->second;
	if (a <= r.va || a >= r.va + r.len) return;
	it->second.len = a - r.va;
	Region n = r;
	n.va = a; n.len = r.va + r.len - a; n.host = r.host + (a - r.va);
	regions[a] = n;
}

bool Mem::unmap (u64 va, u64 len)
{
	if ((va & (HM_GRAIN - 1)) || len == 0) return false;
	len = ALIGN_UP (len);
	split_at (va); split_at (va + len);
	for (auto it = regions.lower_bound (va); it != regions.end () && it->first < va + len; )
	{
		log.push_back (MemOp { 1, it->first, it->second.len, 0, 0 });
		if (!it->second.shared) host_decommit (it->second.host, it->second.len);
		it = regions.erase (it);
	}
	return true;
}

bool Mem::protect (u64 va, u64 len, unsigned prot)
{
	if ((va & (HM_GRAIN - 1)) || len == 0) return false;
	len = ALIGN_UP (len);
	split_at (va); split_at (va + len);
	for (auto it = regions.lower_bound (va); it != regions.end () && it->first < va + len; ++it)
	{
		it->second.prot = prot;
		log.push_back (MemOp { 2, it->first, it->second.len, prot, 0 });
	}
	return true;
}

const Region *Mem::find (u64 va) const
{
	auto it = regions.upper_bound (va);
	if (it == regions.begin ()) return 0;
	--it;
	return va < it->first + it->second.len ? &it->second : 0;
}

const Region *Mem::next (u64 va) const
{
	const Region *r = find (va);
	if (r) return r;
	auto it = regions.upper_bound (va);
	return it == regions.end () ? 0 : &it->second;
}

u64 Mem::find_free (u64 lo, u64 hi, u64 len) const
{
	len = ALIGN_UP (len);
	u64 a = ALIGN_UP (lo);
	for (auto it = regions.begin (); it != regions.end (); ++it)
	{
		u64 rs = it->first, re = rs + it->second.len;
		if (re <= a) continue;
		if (rs >= a + len) break;
		a = ALIGN_UP (re);
	}
	return a + len <= hi ? a : 0;
}

u8 *Mem::g2h (u64 va, u64 n, unsigned prot) const
{
	const Region *r = find (va);
	if (!r || (r->prot & prot) != prot) return 0;
	u8 *h = r->host + (va - r->va);
	u64 end = va + n;
	if (end < va) return 0;
	while (end > r->va + r->len)		// (spans the next region: it must follow in the host too)
	{
		u8 *expect = r->host + r->len;
		const Region *q = find (r->va + r->len);
		if (!q || (q->prot & prot) != prot || q->host != expect) return 0;
		r = q;
	}
	return h;
}
