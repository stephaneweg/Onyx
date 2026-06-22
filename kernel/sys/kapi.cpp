//
// kapi.cpp
//
// Kernel API (Option C): functions that EL1 apps call DIRECTLY (no syscall trap).
// Their addresses are exported at the kernel link step (see kernel/Makefile ->
// user/kernel_syms.ld) and resolved when the app ELFs are linked. Apps run in EL1
// in their own address space, so these run in the app's context with the kernel
// mapped in -- arguments are plain pointers in the active address space (no
// copy_from_user needed).
//
// extern "C": stable, unmangled names for the symbol-export/link step.
//
#include <kern/addrspace.h>
#include <kern/layout.h>
#include <kern/gui/window.h>
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/new.h>
#include <circle/util.h>
#include <fatfs/ff.h>
#include <circle/types.h>

static CAddressSpace *CurrentAS (void)
{
	if (!CScheduler::IsActive ())
	{
		return 0;
	}
	CTask *pTask = CScheduler::Get ()->GetCurrentTask ();
	return (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
}

extern "C" {

// --- windowing ---------------------------------------------------------------

unsigned *kapi_create_window (int w, int h, const char *pTitle)
{
	CAddressSpace *pAS = CurrentAS ();
	if (pAS == 0 || w <= 0 || h <= 0 || w > 1024 || h > 768)
	{
		return 0;
	}
	if (pAS->GetWindow () != 0)
	{
		return (unsigned *) USER_WINDOW_CANVAS;		// one window per process
	}

	// Place windows down the RIGHT edge, stacked one below the other.
	static int s_nNextY = 10;
	int nOuterW = w + 2 * WIN_BORDER;
	int x = SCREEN_WIDTH - nOuterW - 8;			// right-aligned (8 px margin)
	if (x < 0)
	{
		x = 0;
	}
	int y = s_nNextY;
	s_nNextY += WIN_TITLEBAR_H + h + WIN_BORDER + 12;	// next window below + gap

	// pTitle is a pointer in the calling app's address space, which is active here
	// (EL1) -- CWindow copies it. Fall back to a default if null.
	CWindow *pWin = new CWindow (x, y, w, h, pTitle != 0 ? pTitle : "app");
	if (pWin == 0 || !pWin->IsValid ())
	{
		return 0;
	}

	TKPageAttr Attr = KPAGE_ATTR_APP_DATA;			// EL1 RW, ASID-tagged
	pAS->MapContig (USER_WINDOW_CANVAS, pWin->CanvasPhys (), pWin->CanvasPages (), Attr);
	pAS->SetWindow (pWin);
	if (CWindowManager::Get () != 0)
	{
		CWindowManager::Get ()->Add (pWin);
	}

	return (unsigned *) USER_WINDOW_CANVAS;
}

void kapi_present (void)
{
	// The compositor reads the shared canvas continuously; yield so it and the
	// other app get the CPU promptly.
	if (CScheduler::IsActive ())
	{
		CScheduler::Get ()->Yield ();
	}
}

unsigned kapi_get_ticks (void)
{
	return CTimer::Get ()->GetTicks ();			// HZ ticks since boot
}

void kapi_msleep (unsigned nMillis)
{
	if (CScheduler::IsActive ())
	{
		CScheduler::Get ()->MsSleep (nMillis);
	}
}

void kapi_yield (void)
{
	if (CScheduler::IsActive ())
	{
		CScheduler::Get ()->Yield ();
	}
}

void kapi_exit (int /*nStatus*/)
{
	if (CScheduler::IsActive ())
	{
		CScheduler::Get ()->GetCurrentTask ()->Terminate ();	// frees the AS + window
	}
	for (;;) { }						// not reached
}

// --- console -----------------------------------------------------------------

int kapi_write (int /*fd*/, const void *pBuf, unsigned nLen)
{
	char Tmp[129];
	unsigned n = nLen < sizeof (Tmp) - 1 ? nLen : sizeof (Tmp) - 1;
	memcpy (Tmp, pBuf, n);			// app buffer is in the active AS (EL1)
	Tmp[n] = '\0';
	CLogger::Get ()->Write ("app", LogNotice, "%s", Tmp);
	return (int) nLen;
}

// --- files (the motivation for direct calls) ---------------------------------
//
// Handles are heap-allocated FIL objects; apps treat them as opaque void*.

void *kapi_open (const char *pPath)
{
	FIL *pFile = new FIL;
	if (pFile == 0)
	{
		return 0;
	}
	if (f_open (pFile, pPath, FA_READ) != FR_OK)
	{
		delete pFile;
		return 0;
	}
	return pFile;
}

int kapi_read (void *pHandle, void *pBuf, unsigned nLen)
{
	if (pHandle == 0)
	{
		return -1;
	}
	UINT nRead = 0;
	if (f_read ((FIL *) pHandle, pBuf, nLen, &nRead) != FR_OK)
	{
		return -1;
	}
	return (int) nRead;
}

unsigned kapi_fsize (void *pHandle)
{
	if (pHandle == 0)
	{
		return 0;
	}
	return (unsigned) f_size ((FIL *) pHandle);
}

void kapi_close (void *pHandle)
{
	if (pHandle != 0)
	{
		f_close ((FIL *) pHandle);
		delete (FIL *) pHandle;
	}
}

}  // extern "C"
