//
// uaccess.cpp -- the pointers an app hands the kernel: the range check, the probe, the fault-safe
// copies and the exception fixup (see kern/uaccess.h for the model and the rules).
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
#include <kern/uaccess.h>
#include <kern/addrspace.h>
#include <kern/trapframe.h>
#include <kern/layout.h>
#include <kern/vm.h>			// (v75) demand paging: the probes fill and pin, the copies retry
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/types.h>

// ---- who calls --------------------------------------------------------------------------------

// The kernel itself (kern/uaccess.h): no scheduler yet on core 0 (boot), or a task without an
// address space (a kernel task). An app core (2-3, no scheduler: its jobs make no kapi call) or a
// process's task is not.
boolean UserIsKernelCaller (void)
{
	if (!CScheduler::IsActive ())
	{
		return CScheduler::ThisCore () == 0;
	}
	CTask *pTask = CScheduler::Get ()->GetCurrentTask ();
	return pTask == 0 || pTask->GetUserData (TASK_USER_DATA_USER) == 0;
}

// ---- the range ----------------------------------------------------------------------------------

#define AVAIL_ANY	(~(u64) 0)

u64 UserRangeAvail (const void *p)
{
	if (UserIsKernelCaller ())
	{
		return AVAIL_ANY;
	}
	return UAccessAvailIn ((u64) (uintptr) p, USER_VA_BASE, USER_VA_END);
}

boolean UserRange (const void *p, u64 n)
{
	return n == 0 || n <= UserRangeAvail (p);
}

static CAddressSpace *CallerAS (void)
{
	CTask *pTask = CScheduler::IsActive () ? CScheduler::Get ()->GetCurrentTask () : 0;
	return pTask != 0 ? (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER) : 0;
}

// Is the 64 KB page holding ulVA mapped, and readable (writable) BY THE APP -- AT S1E0R / S1E0W,
// the EL0 permissions (v75: a PROT_NONE page is EL1-readable, never the app's to hand over)?
static boolean PageAccessible (u64 ulVA, boolean bWrite)
{
	u64 nFlags, nPAR;
	asm volatile ("mrs %0, daif; msr daifset, #3" : "=r" (nFlags) :: "memory");	// (PAR_EL1 ours)
	if (bWrite)
	{
		asm volatile ("at s1e0w, %1\n\tisb\n\tmrs %0, par_el1" : "=r" (nPAR) : "r" (ulVA) : "memory");
	}
	else
	{
		asm volatile ("at s1e0r, %1\n\tisb\n\tmrs %0, par_el1" : "=r" (nPAR) : "r" (ulVA) : "memory");
	}
	asm volatile ("msr daif, %0" :: "r" (nFlags) : "memory");
	return (nPAR & 1) == 0;					// PAR_EL1.F: the walk faulted
}

// (v75) Every page of the range present (a lazy one filled: kern/vm.h) and accessible, then the
// range PINNED for the rest of the call: no other thread's vm_unmap / DONTNEED / mprotect takes
// it away under the kernel (its zap is deferred). Never yields.
static boolean Probe (const void *p, u64 n, boolean bWrite)
{
	if (n == 0)
	{
		return TRUE;
	}
	if (UserRangeAvail (p) < n)
	{
		return FALSE;
	}
	if (UserIsKernelCaller ())
	{
		return TRUE;				// (kernel memory: mapped by Circle)
	}
	CAddressSpace *pAS = CallerAS ();
	u64 ulFirst = (u64) (uintptr) p & ~(u64) KPAGE_MASK;
	u64 ulLast = ((u64) (uintptr) p + n - 1) & ~(u64) KPAGE_MASK;	// (no wrap: inside the range)
	for (u64 ulPage = ulFirst; ; ulPage += KPAGE_SIZE)
	{
		if (   !PageAccessible (ulPage, bWrite)
		    && (VmFaultIn (pAS, ulPage, bWrite) < 0 || !PageAccessible (ulPage, bWrite)))
		{
			return FALSE;
		}
		if (ulPage == ulLast)
		{
			break;
		}
	}
	VmPin (pAS, ulFirst, ulLast + KPAGE_SIZE);
	return TRUE;
}

boolean UserReadable (const void *p, u64 n)
{
	return Probe (p, n, FALSE);
}

boolean UserWritable (void *p, u64 n)
{
	return Probe (p, n, TRUE);
}

// ---- fault-safe copies ----------------------------------------------------------------------------

// The last fault a copy routine took (UAccessFixup): its address and syndrome.
static volatile u64 s_ulFixupFAR, s_ulFixupESR;

// A copy routine failed: if it faulted on a page of a lazy region of the caller (kern/vm.h) that
// allows the access and was not there, the page is filled now -- in C, out of the exception --
// and TRUE: run the routine again. Each TRUE fills one more page of the range, so a retry loop
// ends.
static boolean UAccessRetry (void)
{
	u64 ulFAR = s_ulFixupFAR, ulESR = s_ulFixupESR;
	s_ulFixupFAR = 0;
	if (   ulFAR == 0 || (ulESR & 0x3C) != 0x04	// (a translation fault: nothing there)
	    || UserIsKernelCaller ())
	{
		return FALSE;
	}
	return VmFaultIn (CallerAS (), ulFAR, (ulESR & (1u << 6)) != 0) > 0;	// (WnR)
}

static long Copy (void *pDst, const void *pSrc, u64 n)
{
	long r;
	while ((r = UAccessCopy (pDst, pSrc, n)) != 0 && UAccessRetry ()) { }
	return r;
}

static long StrCopy (char *pDst, const char *pSrc, u64 nCap)
{
	long r;
	while ((r = UAccessStrCopy (pDst, pSrc, nCap)) < 0 && UAccessRetry ()) { }
	return r;
}

static long StrLen (const char *pSrc, u64 nMax)
{
	long r;
	while ((r = UAccessStrLen (pSrc, nMax)) < 0 && UAccessRetry ()) { }
	return r;
}

boolean UserCopyIn (void *pKernel, const void *pUser, u64 n)
{
	if (n == 0)
	{
		return TRUE;
	}
	return UserRangeAvail (pUser) >= n && Copy (pKernel, pUser, n) == 0;
}

boolean UserCopyOut (void *pUser, const void *pKernel, u64 n)
{
	if (n == 0)
	{
		return TRUE;
	}
	return UserRangeAvail (pUser) >= n && Copy (pUser, pKernel, n) == 0;
}

int UserStrOut (char *pBuf, unsigned nCap, const char *pStr)
{
	if (nCap == 0)
	{
		return 0;
	}
	if (pBuf == 0)
	{
		return -1;
	}
	unsigned n = 0;
	if (pStr != 0)
	{
		while (n < nCap - 1 && pStr[n] != '\0') n++;
	}
	if (   UserRangeAvail (pBuf) < (u64) n + 1
	    || Copy (pBuf, pStr, n) != 0
	    || Copy (pBuf + n, "", 1) != 0)
	{
		return -1;
	}
	return (int) n;
}

CUserStr::CUserStr (const char *pUser, u64 nMax, boolean bTruncate)
:	m_pStr (0),
	m_pHeap (0),
	m_nLen (0),
	m_bNull (pUser == 0)
{
	m_Inline[0] = '\0';
	if (pUser == 0 || nMax == 0)
	{
		return;
	}
	u64 nAvail = UserRangeAvail (pUser);		// (it must end -- its NUL -- inside that)

	// Most strings fit the inline buffer: one pass.
	u64 nCap = nMax < sizeof m_Inline ? nMax : sizeof m_Inline;
	if (nCap > nAvail) nCap = nAvail;
	if (nCap == 0)
	{
		return;					// not the caller's
	}
	long n = StrCopy (m_Inline, pUser, nCap);
	if (n < 0)
	{
		return;					// a fault
	}
	if ((u64) n < nCap)
	{
		m_pStr = m_Inline;			// its NUL found
		m_nLen = (u64) n;
		return;
	}
	if (nCap == nMax)				// nMax - 1 characters and more
	{
		if (bTruncate)
		{
			m_Inline[nMax - 1] = '\0';
			m_pStr = m_Inline;
			m_nLen = nMax - 1;
		}
		return;
	}
	if (nCap == nAvail)
	{
		return;					// it runs out of the caller's range
	}

	// Longer than the inline buffer: its length, then a heap copy of exactly that.
	u64 nCap2 = nMax < nAvail ? nMax : nAvail;
	long nLen = StrLen (pUser, nCap2);
	if (nLen < 0)
	{
		return;
	}
	u64 nCopy = (u64) nLen;
	if (nCopy >= nCap2)				// no NUL in nCap2 bytes
	{
		if (nCap2 != nMax || !bTruncate)
		{
			return;
		}
		nCopy = nMax - 1;
	}
	m_pHeap = new char[nCopy + 1];
	if (m_pHeap == 0 || Copy (m_pHeap, pUser, nCopy) != 0)
	{
		return;
	}
	m_pHeap[nCopy] = '\0';				// (whatever changed there meanwhile: a string)
	m_pStr = m_pHeap;
	m_nLen = nCopy;
}

CUserStr::~CUserStr (void)
{
	delete [] m_pHeap;
	m_pHeap = 0;
	m_pStr = 0;
}

// ---- the exception side ---------------------------------------------------------------------------

struct TUAccessFixup
{
	u64	ulStart;
	u64	ulEnd;
	u64	ulFixup;
};

extern "C" const TUAccessFixup g_UAccessFixups[];	// (arch/aarch64/uaccess.S)

#define EC_DABORT_SAME		0x25

boolean UAccessFixup (TTrapFrame *pFrame, u64 ulESR)
{
	if (((ulESR >> 26) & 0x3F) != EC_DABORT_SAME)
	{
		return FALSE;
	}
	for (const TUAccessFixup *p = g_UAccessFixups; p->ulStart != 0; p++)
	{
		if (pFrame->elr_el1 >= p->ulStart && pFrame->elr_el1 < p->ulEnd)
		{
			u64 ulFAR;
			asm volatile ("mrs %0, far_el1" : "=r" (ulFAR));
			s_ulFixupESR = ulESR;			// (v75: UAccessRetry fills that page)
			s_ulFixupFAR = ulFAR;
			pFrame->elr_el1 = p->ulFixup;	// the routine returns -1
			return TRUE;
		}
	}
	return FALSE;
}
