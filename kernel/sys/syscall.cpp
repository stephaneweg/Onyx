//
// syscall.cpp
//
// Syscall dispatch. SyscallEntry() is called from the EL0 (and, for testing, EL1)
// synchronous exception path with a pointer to the trap frame: arguments are in
// x0..x5, the number in x8, and the return value goes back into x0.
//
#include <kern/syscall.h>
#include <kern/trapframe.h>
#include <kern/addrspace.h>
#include <kern/layout.h>
#include <kern/gui/window.h>
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/util.h>
#include <circle/types.h>

// ---- user-memory access (for #6; LDTR/STTR honor EL0 permissions in EL1) -----

int copy_from_user (void *pDst, const void *pUserSrc, size_t nLen)
{
	u8 *pD = (u8 *) pDst;
	const u8 *pS = (const u8 *) pUserSrc;
	for (size_t i = 0; i < nLen; i++)
	{
		u8 v;
		asm volatile ("ldtrb %w0, [%1]" : "=r" (v) : "r" (pS + i));
		pD[i] = v;
	}
	return SYS_OK;
}

int copy_to_user (void *pUserDst, const void *pSrc, size_t nLen)
{
	u8 *pD = (u8 *) pUserDst;
	const u8 *pS = (const u8 *) pSrc;
	for (size_t i = 0; i < nLen; i++)
	{
		asm volatile ("sttrb %w0, [%1]" : : "r" (pS[i]), "r" (pD + i));
	}
	return SYS_OK;
}

// ---- individual syscalls -----------------------------------------------------

static long sys_write (unsigned nFD, const void *pBuf, size_t nLen, boolean bFromUser)
{
	char Tmp[129];
	size_t n = nLen < sizeof (Tmp) - 1 ? nLen : sizeof (Tmp) - 1;

	if (bFromUser)
	{
		// EL0 buffer: use unprivileged loads (works under PAN, honors EL0 rights).
		if (copy_from_user (Tmp, pBuf, n) != SYS_OK)
		{
			return SYS_EFAULT;
		}
	}
	else
	{
		// EL1 self-test: pBuf is a kernel pointer, read it directly.
		memcpy (Tmp, pBuf, n);
	}
	Tmp[n] = '\0';

	CLogger::Get ()->Write ("syscall", LogNotice, "write(fd=%u, \"%s\", len=%u) [%s]",
				nFD, Tmp, (unsigned) nLen, bFromUser ? "user" : "kernel");
	return (long) nLen;
}

// create_window(w, h, title): give the calling process a window whose canvas is
// mapped into its address space at USER_WINDOW_CANVAS. Returns that address (the
// pixel buffer) or 0 on failure. Title text is not rendered yet (font layer TODO).
static long sys_create_window (int w, int h, const char * /*pUserTitle*/)
{
	if (!CScheduler::IsActive ())
	{
		return 0;
	}
	CTask *pTask = CScheduler::Get ()->GetCurrentTask ();
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	if (pAS == 0 || w <= 0 || h <= 0 || w > 1024 || h > 768)
	{
		return 0;
	}
	if (pAS->GetWindow () != 0)
	{
		return (long) USER_WINDOW_CANVAS;	// one window per process for now
	}

	// Stagger window placement.
	static unsigned s_nPlaced = 0;
	int x = 30 + (int) (s_nPlaced * 70);
	int y = 40 + (int) (s_nPlaced * 55);
	s_nPlaced++;

	CWindow *pWin = new CWindow (x, y, w, h, "app");
	if (pWin == 0 || !pWin->IsValid ())
	{
		return 0;
	}

	TKPageAttr Attr = KPAGE_ATTR_USER_DATA;
	pAS->MapContig (USER_WINDOW_CANVAS, pWin->CanvasPhys (), pWin->CanvasPages (), Attr);
	pAS->SetWindow (pWin);
	if (CWindowManager::Get () != 0)
	{
		CWindowManager::Get ()->Add (pWin);
	}

	return (long) USER_WINDOW_CANVAS;
}

static long sys_exit (int nStatus)
{
	// For an EL0 process this will terminate the process (#6). For the current
	// EL1 self-test there is nothing to tear down; just log and yield away.
	CLogger::Get ()->Write ("syscall", LogNotice, "exit(%d)", nStatus);
	if (CScheduler::IsActive ())
	{
		CScheduler::Get ()->GetCurrentTask ()->Terminate ();	// does not return
	}
	return SYS_OK;
}

// ---- dispatcher --------------------------------------------------------------

void SyscallEntry (TTrapFrame *pFrame)
{
	unsigned long nNum = pFrame->x[8];
	long nResult;

	// Caller's exception level: SPSR_EL1.M[3:0] == 0 (EL0t) means a user process.
	boolean bFromUser = (pFrame->spsr_el1 & 0xF) == 0;

	switch (nNum)
	{
	case SYS_write:
		nResult = sys_write ((unsigned) pFrame->x[0],
				     (const void *) pFrame->x[1],
				     (size_t) pFrame->x[2],
				     bFromUser);
		break;

	case SYS_yield:
		if (CScheduler::IsActive ())
		{
			CScheduler::Get ()->Yield ();
		}
		nResult = SYS_OK;
		break;

	case SYS_exit:
		nResult = sys_exit ((int) pFrame->x[0]);
		break;

	case SYS_getpid:
		nResult = 1;		// placeholder until the process model (#5/#6)
		break;

	case SYS_create_window:
		nResult = sys_create_window ((int) pFrame->x[0], (int) pFrame->x[1],
					     (const char *) pFrame->x[2]);
		break;

	case SYS_present:
		// The compositor reads the shared canvas continuously; just yield so it
		// and the other process get the CPU promptly.
		if (CScheduler::IsActive ())
		{
			CScheduler::Get ()->Yield ();
		}
		nResult = SYS_OK;
		break;

	case SYS_get_ticks:
		nResult = (long) CTimer::Get ()->GetTicks ();		// HZ ticks since boot
		break;

	case SYS_msleep:
		if (CScheduler::IsActive ())
		{
			CScheduler::Get ()->MsSleep ((unsigned) pFrame->x[0]);
		}
		nResult = SYS_OK;
		break;

	default:
		CLogger::Get ()->Write ("syscall", LogWarning, "unknown syscall %lu", nNum);
		nResult = SYS_ENOSYS;
		break;
	}

	pFrame->x[0] = (u64) nResult;
}
