//
// vm.h -- an app's virtual memory: lazy regions (VMAs), demand paging, vm_map & co. (kapi v75,
// docs/POSIX-PLAN.md §3.1; docs/02 §4 "Demand paging").
//
// Every address space has a TVmSpace (made with the space: CAddressSpace's constructor maps the
// EL0 kapi pages, which are noted here). Its REGIONS (TVma: [start, end), 64 KB-aligned, sorted,
// never overlapping) say what each part of the user range is:
//
//   ANON   vm_map'ed memory, in the mmap arena [USER_MMAP_BASE, USER_MMAP_END): lazy
//   HEAP   [USER_HEAP_BASE, the page-aligned break): lazy (eager once the app holds an app core)
//   STACK  the main stack below USER_STACK_TOP, each thread's at the top of its slot: lazy
//   IMAGE  the ELF's segments: eager (loaded at start)
//   FIXED  canvases, surfaces, the sound ring, the kapi pages, the code arena...: eager, frames
//          owned elsewhere or mapped once; never changed by the vm_* calls
//
// A LAZY region's pages are filled on first touch (VmFaultIn): a zeroed 64 KB frame from the
// app pool (palloc_high), mapped with the region's protection; the access is retried. An EL0
// access (sys/el0.cpp), a kernel probe of an app buffer (UserReadable / UserWritable, which also
// PIN the range for the call) or a failed fault-safe copy (kern/uaccess.h: filled, then retried
// in C, never in the exception) and an app core's job (sys/appcore.cpp: a page-in request served
// by the pager task on core 0) all go through it. Nothing here yields but the POPULATE / WILLNEED
// fills, and nothing allocates in an exception handler except the EL1 safety net (VmKernelFault).
//
// Frames leave a region (vm_unmap, MADV_DONTNEED, a heap shrink, an ended thread's stack) or have
// their protection lowered at once, unless another task of the process has a kapi in progress
// on them (a PIN): those pages are marked in their PTE (VM_PTE_SW_ZAP / _SYNC) and dealt with
// when the last pin over them ends (the "deferred zap": the kernel never touches a freed frame,
// and nobody waits). A range mapped again before that adopts the pages left (zeroed).
//
// TLB rules (ARMv8-A, inner shareable: cores 2-3 run the same ASID): invalid -> valid needs only
// a DSB (an invalid entry is never cached); valid -> invalid and a protection change: the PTE
// written, DSB ISHST, TLBI VAE1IS per page (ASIDE1IS above 64 pages), DSB ISH, ISB -- and only
// then the frames freed.
//
// Core 0 only (the kapis, the EL0 faults, the pager): the kernel is not preempted, so no lock.
//
// ---------------------------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// ---------------------------------------------------------------------------------------------
//
#ifndef _kern_vm_h
#define _kern_vm_h

#include <circle/types.h>

class CAddressSpace;
class CTask;
struct TTrapFrame;

// ---- the regions --------------------------------------------------------------------------------

#define VM_MAX_VMAS		4096		// regions a process may have (vm_map -> -ENOMEM)
#define VM_PINS			40		// tasks with a kapi in progress (main + 32 threads)
#define VM_RESERVE		(16ULL << 20)	// the app pool kept free: a fill under it is refused
#define VM_POPULATE_BATCH	64		// pages filled between two yields (POPULATE, WILLNEED)
#define VM_TLBI_ASID_ABOVE	64		// more pages than that: one TLBI ASIDE1IS
#define VM_STACK_GUARD_HINT	0x100000	// a fault this far below a stack: "stack overflow"

// The PTE's software bits (the L3 descriptor's Ignored field, bits 55..58).
#define VM_PTE_SW_OWNED		(1ULL << 55)	// a frame of this space (freed with it)
#define VM_PTE_SW_ZAP		(1ULL << 56)	// to drop when no pin covers it any more
#define VM_PTE_SW_SYNC		(1ULL << 57)	// to re-protect (or drop) when no pin covers it

struct TVma
{
	u64	ulStart, ulEnd;			// [start, end), 64 KB-aligned
	u16	nProt;				// KAPI_PROT_*
	u16	nKind;				// KAPI_VMK_*
	u32	nFlags;				// (reserved: 0)
};

struct TVmPin					// a kapi of pTask works in [ulStart, ulEnd) in place
{
	CTask	*pTask;				// 0: a free slot
	u64	 ulStart, ulEnd;		// (the union of what that call probed)
};

struct TVmZap					// a range with ZAP / SYNC pages, still pinned
{
	u64	 ulStart, ulEnd;
	TVmZap	*pNext;
};

struct TVmSpace
{
	TVma	*pVma;				// sorted by start, binary-searched; grows by doubling
	unsigned nVma, nCap;
	TVmPin	 Pin[VM_PINS];
	unsigned nPins;				// slots in use
	TVmZap	*pZapPending;
	u64	 nFaults;			// pages filled (EL0, kernel probes and copies, app cores)
	boolean	 bEagerHeap;			// an app core was acquired: the heap is filled as it grows
};

// The space's regions (0 if it has none yet: bCreate makes them; 0 then only on out of memory).
TVmSpace *VmOf (CAddressSpace *pAS, boolean bCreate = FALSE);

// The space ends (~CAddressSpace, before the page tables and the frames are freed): its TVmSpace.
void VmTeardown (CAddressSpace *pAS);

// ---- demand paging ------------------------------------------------------------------------------

// Fill the page of ulVA if it lies in a lazy region that allows the access (a software table walk,
// so it works for any space, current or not). Never yields.
// -> 1 filled, 0 already present and allowed, -KAPI_EFAULT no lazy region there, -KAPI_EACCES the
//    protection forbids it, -KAPI_ENOMEM the app pool is under VM_RESERVE.
int VmFaultIn (CAddressSpace *pAS, u64 ulVA, boolean bWrite);

// Fill what is not present of [ulStart, ulEnd) in the lazy regions there (others skipped), a
// yield every VM_POPULATE_BATCH pages when bYield. -> 0, or -KAPI_ENOMEM (stopped there).
int VmPopulate (CAddressSpace *pAS, u64 ulStart, u64 ulEnd, boolean bYield);

// Why an EL0 access at ulFAR failed, for the kill's message: "stack overflow", "PROT_NONE
// access", or 0.
const char *VmFaultWhy (CAddressSpace *pAS, u64 ulFAR);

// The EL1 safety net (SyncHandlerEL1, after the uaccess fixup): kernel code touched an unfilled
// page of a lazy region of the current app outside the helpers -> filled (logged once), TRUE: the
// access is retried. FALSE: a kernel bug, as before.
boolean VmKernelFault (TTrapFrame *pFrame, u64 ulESR, u64 ulFAR);

// Is a growth of nBytes of writable memory acceptable (the heuristic overcommit)?
boolean VmCommitOK (u64 nBytes);

// Bytes free in the app pool (the high zone, or the low pager on a 1 GB Pi).
u64 VmPoolFree (void);

// ---- pins (kern/uaccess.h's probes) ---------------------------------------------------------------

// The current task's kapi works in [ulStart, ulEnd) in place until it returns.
void VmPin (CAddressSpace *pAS, u64 ulStart, u64 ulEnd);

// pTask's kapi returned (sys/el0.cpp, after every system call; an ending thread): its pin dropped,
// and the deferred zaps no pin covers any more done.
void VmUnpinTask (CAddressSpace *pAS, CTask *pTask);

// ---- the regions the rest of the kernel makes ------------------------------------------------------

// A FIXED or IMAGE range mapped eagerly (MapContig, the code arena, the ELF loader): noted for
// vm_query / vm_stats (replacing what that kind had there). Never fails visibly.
void VmNoteRegion (CAddressSpace *pAS, u64 ulStart, u64 ulEnd, unsigned nProt, unsigned nKind);

// A lazy stack [ulTop - nSize, ulTop) replacing whatever stack was in [ulTop - nSlot, ulTop) (its
// pages dropped). -> 0 / -KAPI_ENOMEM.
int VmMapStack (CAddressSpace *pAS, u64 ulTop, u64 nSize, u64 nSlot);

// Drop the pages of [ulStart, ulEnd) (an ended thread's stack): they read as zeros again.
void VmDiscard (CAddressSpace *pAS, u64 ulStart, u64 ulEnd);

// The heap region [USER_HEAP_BASE, ulNewEnd) (64 KB-aligned), from ulOldEnd: grown (the overcommit
// check; filled at once when the heap is eager) or shrunk (its pages dropped). -> 0 / -KAPI_ENOMEM.
int VmHeapResize (CAddressSpace *pAS, u64 ulOldEnd, u64 ulNewEnd);

// An app core was acquired: the heap filled now and as it grows (its jobs touch what the main
// thread allocated). Yields.
void VmEagerHeap (CAddressSpace *pAS);

// The region holding ulVA (0: none) -- a copy.
boolean VmRegionAt (CAddressSpace *pAS, u64 ulVA, TVma *pOut);

// The end of the highest region below ulVA (USER_VA_BASE if none).
u64 VmRegionEndBelow (CAddressSpace *pAS, u64 ulVA);

// ---- the word waits (sys/thread.cpp) --------------------------------------------------------------

// A frame of [ulPhys, ulPhys + nLen) is about to leave its space: its word waiters woken.
void WordWaitsZap (u64 ulPhys, u64 nLen);

#endif // _kern_vm_h
