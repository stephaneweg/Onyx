//
// hostmem.cpp -- the guest's memory at its own addresses in the host (onyxrun.h). The whole user range of an Onyx
// process, [USER_VA_BASE, USER_VA_CEILING) = [8 GB, 64 GB), is reserved in the host at the start (no host library
// or heap can land there afterwards); a region the program gets (its ELF, a library, the heap, a stack, a vm_map)
// is then committed there, zero-filled, and mapped into every guest engine at the same address. The host keeps
// every region readable and writable (the kapi functions read and write the program's buffers; the loader writes
// its code): the guest's protection is Unicorn's.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include <map>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

static std::map<u64, HmRegion> s_Regions;	// by start (the big lock held, or the start before any thread)

static const u64 RANGE_LO = USER_VA_BASE, RANGE_HI = USER_VA_CEILING;

bool hm_init (void)
{
#ifdef _WIN32
	void *p = VirtualAlloc ((void *) RANGE_LO, RANGE_HI - RANGE_LO, MEM_RESERVE, PAGE_NOACCESS);
	if (p != (void *) RANGE_LO)
	{
		rlog ("cannot reserve the guest's range %llx..%llx (error %lu)", (unsigned long long) RANGE_LO,
		      (unsigned long long) RANGE_HI, (unsigned long) GetLastError ());
		return false;
	}
#else
	void *p = mmap ((void *) RANGE_LO, RANGE_HI - RANGE_LO, PROT_NONE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
	if (p != (void *) RANGE_LO)
	{
		rlog ("cannot reserve the guest's range %llx..%llx", (unsigned long long) RANGE_LO, (unsigned long long) RANGE_HI);
		return false;
	}
#endif
	return true;
}

static bool host_commit (u64 va, u64 len)
{
#ifdef _WIN32
	return VirtualAlloc ((void *) va, len, MEM_COMMIT, PAGE_READWRITE) == (void *) va;
#else
	return mmap ((void *) va, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == (void *) va;
#endif
}

static void host_release (u64 va, u64 len)
{
#ifdef _WIN32
	VirtualFree ((void *) va, len, MEM_DECOMMIT);
#else
	mmap ((void *) va, len, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
#endif
}

static bool overlaps (u64 va, u64 len)
{
	auto it = s_Regions.upper_bound (va);
	if (it != s_Regions.end () && it->first < va + len) return true;
	if (it != s_Regions.begin ())
	{
		--it;
		if (it->first + it->second.len > va) return true;
	}
	return false;
}

bool hm_map (u64 va, u64 len, unsigned prot, const char *what)
{
	if (len == 0 || (va & (HM_GRAIN - 1)) || (len & (HM_GRAIN - 1)) || va < RANGE_LO || va + len > RANGE_HI || va + len < va)
		return false;
	if (overlaps (va, len)) return false;
	if (!host_commit (va, len)) { rlog ("out of host memory (%llu KB for %s)", (unsigned long long) (len >> 10), what); return false; }
	s_Regions[va] = HmRegion { va, len, prot, what };
	cpu_map_all (va, len, prot, (void *) va);
	return true;
}

// The regions overlapping [va, va + len) split at its ends (a region's pieces keep its protection and name).
static void split_at (u64 a)
{
	auto it = s_Regions.upper_bound (a);
	if (it == s_Regions.begin ()) return;
	--it;
	HmRegion r = it->second;
	if (a <= r.va || a >= r.va + r.len) return;
	it->second.len = a - r.va;
	s_Regions[a] = HmRegion { a, r.va + r.len - a, r.prot, r.what };
}

bool hm_unmap (u64 va, u64 len)
{
	if ((va & (HM_GRAIN - 1)) || len == 0) return false;
	len = (len + HM_GRAIN - 1) & ~(HM_GRAIN - 1);
	split_at (va); split_at (va + len);
	for (auto it = s_Regions.lower_bound (va); it != s_Regions.end () && it->first < va + len; )
	{
		cpu_unmap_all (it->first, it->second.len);
		host_release (it->first, it->second.len);
		it = s_Regions.erase (it);
	}
	return true;
}

bool hm_protect (u64 va, u64 len, unsigned prot)
{
	if ((va & (HM_GRAIN - 1)) || len == 0) return false;
	len = (len + HM_GRAIN - 1) & ~(HM_GRAIN - 1);
	split_at (va); split_at (va + len);
	for (auto it = s_Regions.lower_bound (va); it != s_Regions.end () && it->first < va + len; ++it)
	{
		it->second.prot = prot;
		cpu_protect_all (it->first, it->second.len, prot);
	}
	return true;
}

bool hm_find (u64 va, HmRegion *out)
{
	auto it = s_Regions.upper_bound (va);
	if (it == s_Regions.begin ()) return false;
	--it;
	if (va >= it->first + it->second.len) return false;
	if (out) *out = it->second;
	return true;
}

bool hm_next (u64 va, HmRegion *out)
{
	if (hm_find (va, out)) return true;
	auto it = s_Regions.upper_bound (va);
	if (it == s_Regions.end ()) return false;
	if (out) *out = it->second;
	return true;
}

std::vector<HmRegion> hm_regions (void)
{
	std::vector<HmRegion> v;
	for (auto &r : s_Regions) v.push_back (r.second);
	return v;
}

bool hm_ok (const void *p, u64 n, unsigned prot)
{
	u64 a = (u64) (uintptr_t) p, e = a + n;
	if (e < a) return false;
	while (a < e || (n == 0 && a == e))
	{
		HmRegion r;
		if (!hm_find (a, &r) || (r.prot & prot) != prot) return false;
		if (n == 0) return true;
		a = r.va + r.len;
	}
	return true;
}

u64 hm_find_free (u64 lo, u64 hi, u64 len)
{
	len = (len + HM_GRAIN - 1) & ~(HM_GRAIN - 1);
	u64 a = (lo + HM_GRAIN - 1) & ~(HM_GRAIN - 1);
	for (auto it = s_Regions.begin (); it != s_Regions.end (); ++it)
	{
		u64 rs = it->first, re = rs + it->second.len;
		if (re <= a) continue;
		if (rs >= a + len) break;
		a = (re + HM_GRAIN - 1) & ~(HM_GRAIN - 1);
	}
	return a + len <= hi ? a : 0;
}
