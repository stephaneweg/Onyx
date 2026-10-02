//
// uaccess.h -- the pointers an app hands the kernel: checked, and read / written without letting
// a bad one fault in the kernel (step 2 of protected mode, docs/EL0-PROTECTED-MODE.md §4.1, §4.7).
//
// Every kapi that takes a pointer from an app checks it on entry and fails with its documented
// error value (the ABI and the apps' failure conventions unchanged). Three tools:
//
//  - The RANGE check: [p, p + n) must lie where the calling process may point. For every process
//    that is the user VA range (kern/layout.h IS_USER_VA: its image, heap, canvases, surfaces,
//    the sound ring, GPU buffers, the kapi table page...). A LEGACY process (EL1t, the model since
//    the beginning) also runs on stacks the kernel heap gives it -- its main task's and its
//    threads' CTask stacks, in the identity region -- so for it a range inside one of ITS
//    tasks' stacks is accepted too (a buffer on the caller's stack, or on another thread's of the
//    same process: a C++ thread handed a pointer to its parent's local). A PROTECTED process
//    (EL0, kern/el0.h: CAddressSpace::IsProtected) has its stacks in the user VA range, and its
//    CTask stack is its KERNEL stack: the user range only. The check never wraps (no p + n).
//
//  - The PROBE (UserReadable / UserWritable): the range check, then every 64 KB page of the
//    range translated by the MMU (AT S1E1R / S1E1W, PAR_EL1) -- mapped, and writable for a
//    write. Used where the kernel works in place in the app's memory (a file read straight into
//    its buffer, a frame rendered into its pixels). An app's user pages are never unmapped
//    while it lives (sbrk only lowers the break, surfaces / the code arena / canvases stay
//    mapped -- a canvas that grows is remapped, never unmapped), so a probed range stays
//    accessible for the whole call, yields included.
//
//  - The fault-safe COPIES (UserCopyIn / UserCopyOut, CUserStr, UserStrOut): the range check,
//    then a copy by routines (arch/aarch64/uaccess.S) listed in an exception FIXUP table: a fault
//    there (a page that is not mapped, read-only, a misaligned access to Device memory) resumes at
//    the routine's recovery label (SyncHandlerEL1 -> UAccessFixup) and the copy returns a failure
//    instead of halting the machine. Word-sized when both pointers are 8-aligned. Strings and
//    small structures are copied into the kernel ONCE: what the kernel checks is what it uses,
//    whatever another thread (or an app core) writes there meanwhile (closes the TOCTOU of a
//    path that ResolvePath reads while the app's other threads run).
//
// Why plain LDR / STR, not LDTR / STTR (the unprivileged loads / stores): executed at EL1, those
// access memory with EL0's permissions -- unless PSTATE.UAO is set (ARMv8.2 FEAT_UAO; the
// Cortex-A72 is ARMv8.0: no UAO, no PAN), when they act as ordinary EL1 accesses. A legacy app's
// pages are EL1-only (KPAGE_ATTR_APP_*: AP = *_EL1) and its stacks are kernel memory: LDTR would
// fault on every pointer it passes. A protected app's pages are EL0- and EL1-accessible (AP =
// *_ALL, no PAN): plain loads work for both, and the range check above is what keeps a protected
// app's pointer off the kernel's memory.
//
// KERNEL callers: a kapi called by kernel code on its own behalf -- no scheduler yet (boot), or the
// current task has no address space (TASK_USER_DATA_USER == 0: the compositor, the reaper...)
// -- passes kernel pointers: the range check accepts anything (the probe and the copies still
// run: they are just loads / stores). On a core without a scheduler other than core 0 (the app
// cores 2-3: their jobs make no kapi call) only the user range is accepted.
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
#ifndef _kern_uaccess_h
#define _kern_uaccess_h

#include <circle/types.h>

struct TTrapFrame;

// ---- the range ------------------------------------------------------------------------------

// The caller is the kernel itself (see above): its pointers are not checked.
boolean UserIsKernelCaller (void);

// How many bytes from p on the caller may hand the kernel: to the end of the user VA range, or
// (legacy) of its process's stack that holds p; 0 if p is in neither. A kernel caller: no limit.
u64 UserRangeAvail (const void *p);

// [p, p + n) is the caller's to hand over (n == 0: always TRUE, p is not touched).
boolean UserRange (const void *p, u64 n);

// ... and mapped page by page, readable / writable (AT): the kernel may then access it in place.
boolean UserReadable (const void *p, u64 n);
boolean UserWritable (void *p, u64 n);

// Bytes from p to the end of [ulBase, ulEnd) if p lies in it, else 0 (never p + n: no wrap).
static inline u64 UAccessAvailIn (u64 p, u64 ulBase, u64 ulEnd)
{
	return p >= ulBase && p < ulEnd ? ulEnd - p : 0;
}

// ---- fault-safe copies ------------------------------------------------------------------------

// The range checked, then copied; FALSE on a bad range or a fault (some bytes may have moved).
boolean UserCopyIn (void *pKernel, const void *pUser, u64 n);
boolean UserCopyOut (void *pUser, const void *pKernel, u64 n);

// One value out / in (an out-parameter, a small structure).
template <class T> static inline boolean UserPut (T *pUser, const T &Value)
{
	return UserCopyOut (pUser, &Value, sizeof (T));
}
template <class T> static inline boolean UserGet (T *pValue, const T *pUser)
{
	return UserCopyIn (pValue, pUser, sizeof (T));
}

// A kernel string out to the app's pBuf[nCap]: cut to nCap - 1 characters, NUL-terminated.
// -> its length (characters, the NUL not counted), -1 on a bad pointer / fault (0 if nCap == 0).
int UserStrOut (char *pBuf, unsigned nCap, const char *pStr);

#define USTR_INLINE	256		// characters held without the heap
#define UPATH_MAX	512		// a path (with its NUL): longer fails (ResolvePath's own buffer)

// A kernel copy of an app's string (read once). At most nMax - 1 characters: a longer one is cut
// there (bTruncate) or rejected. The copy is on the object up to USTR_INLINE bytes, on the heap
// beyond. Not OK: a null pointer, a bad range, a fault, too long, out of memory.
class CUserStr
{
public:
	CUserStr (const char *pUser, u64 nMax = USTR_INLINE, boolean bTruncate = FALSE);
	~CUserStr (void);

	boolean IsNull (void) const	{ return m_bNull; }	// the app passed 0
	boolean OK (void) const		{ return m_pStr != 0; }
	const char *Get (void) const	{ return m_pStr; }	// 0 if not OK
	u64 Length (void) const		{ return m_nLen; }

private:
	CUserStr (const CUserStr &);
	CUserStr &operator= (const CUserStr &);

private:
	const char *m_pStr;
	char	   *m_pHeap;
	u64	    m_nLen;
	boolean	    m_bNull;
	char	    m_Inline[USTR_INLINE];
};

// ---- the exception side (arch/aarch64/exception.cpp) -------------------------------------------

// A data abort at EL1 in one of the copy routines: resume at its recovery label (the copy fails).
// TRUE: pFrame's ELR redirected.
boolean UAccessFixup (TTrapFrame *pFrame, u64 ulESR);

// PC lies in the legacy apps' memcpy / memset / memmove (the kapi table's entries).
boolean UAccessInAppMem (u64 ulPC);

// The routines (arch/aarch64/uaccess.S).
extern "C" {
long UAccessCopy (void *pDst, const void *pSrc, u64 nLen);		// 0 / -1
long UAccessStrCopy (char *pDst, const char *pSrc, u64 nCap);		// length, nCap, -1
long UAccessStrLen (const char *pSrc, u64 nMax);			// length, nMax, -1
void *AppMemcpy (void *pDst, const void *pSrc, size_t nLen);
void *AppMemset (void *pDst, int nValue, size_t nLen);
void *AppMemmove (void *pDst, const void *pSrc, size_t nLen);
}

#endif // _kern_uaccess_h
