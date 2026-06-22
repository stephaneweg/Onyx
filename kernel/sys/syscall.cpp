//
// syscall.cpp
//
// Syscall dispatch. SyscallEntry() is called from the EL0 (and, for testing, EL1)
// synchronous exception path with a pointer to the trap frame: arguments are in
// x0..x5, the number in x8, and the return value goes back into x0.
//
#include <kern/syscall.h>
#include <kern/trapframe.h>
#include <circle/sched/scheduler.h>
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

static long sys_write (unsigned nFD, const void *pBuf, size_t nLen)
{
	// NOTE: #4 only ever calls this from an EL1 self-test, so pBuf is a kernel
	// pointer and is read directly. Once real EL0 processes exist (#6), this must
	// branch on the caller's EL and use copy_from_user() for user buffers.
	char Tmp[129];
	size_t n = nLen < sizeof (Tmp) - 1 ? nLen : sizeof (Tmp) - 1;
	memcpy (Tmp, pBuf, n);
	Tmp[n] = '\0';

	CLogger::Get ()->Write ("syscall", LogNotice,
				"write(fd=%u, \"%s\", len=%u)", nFD, Tmp, (unsigned) nLen);
	return (long) nLen;
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

	switch (nNum)
	{
	case SYS_write:
		nResult = sys_write ((unsigned) pFrame->x[0],
				     (const void *) pFrame->x[1],
				     (size_t) pFrame->x[2]);
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

	default:
		CLogger::Get ()->Write ("syscall", LogWarning, "unknown syscall %lu", nNum);
		nResult = SYS_ENOSYS;
		break;
	}

	pFrame->x[0] = (u64) nResult;
}
