//
// crashlog.cpp -- the crash record kept across a reboot (see kern/crashlog.h).
//
#include <kern/crashlog.h>
#include <kern/trapframe.h>
#include <kern/layout.h>		// IS_USER_VA, USER_VA_BASE, USER_HEAP_BASE
#include <kern/addrspace.h>		// (the watched app's task: its pid)
#include <circle/memory.h>		// g_ulOnyxCrashArea
#include <circle/synchronize.h>		// CleanDataCacheRange
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/bcmwatchdog.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/string.h>
#include <circle/util.h>
#include <fatfs/ff.h>
#include <circle/device.h>
#include <circle/devicenameservice.h>
#include <circle/multicore.h>
#include <circle/bcm2835.h>
#include <circle/memio.h>
#include <circle/actled.h>
#include <circle/bcmpropertytags.h>

#define CRASH_MAGIC	0x4F4E5843u		// "ONXC"
#define CRASH_VERSION	3
#define STATE_RUNNING	1
#define STATE_CLEAN	2
#define STATE_PANIC	3
#define SAMPLES		16
#define SAMPLE_TASK	16
#define STACK_ADDRS	24			// return addresses kept from a stack scan
#define STACK_SCAN	0x4000			// bytes of stack scanned for them

struct TCrashSample
{
	u64	ulPC, ulLR, ulSP;
	u32	nSPSR, nTick;
	char	szTask[SAMPLE_TASK];
};

struct TCrashStack				// the return addresses found on a stack (newest first)
{
	u64	ulPC, ulLR, ulSP;
	u32	nTick, nCount;
	u64	Addr[STACK_ADDRS];
};

struct TCrashHeader
{
	u32	nMagic, nVersion, nState, nBoot;
	u32	nAliveTick;			// CTimer ticks (1/100 s) at the reaper's last pass
	u32	nLogHead;			// bytes of log written (ring: % LOG_SIZE)
	u32	nSampleHead;			// samples written (ring: % SAMPLES)
	u32	nWatchdog;			// seconds (0: off)
	u32	nDumpStep;			// core 1's report: 1 started, 2 text ready, 3 written, 4 the SD write failed
	u32	Crumb[CRUMB_COUNT];
	TCrashSample Sample[SAMPLES];
	char	szPanic[256];
	u32	nWatchPid;			// the app the GUI watchdog watches (0: none)
	u32	nWatchHead;			// its samples written (ring: % SAMPLES)
	TCrashSample Watch[SAMPLES];		// where its task was, when core 0 was interrupted in it
	TCrashStack WatchStack;			// its stack, scanned now and then
	TCrashStack FaultStack;			// the stack of an exception's context (DumpAndHalt)
};

#define LOG_OFFSET	((sizeof (TCrashHeader) + 63) & ~63ul)
#define LOG_SIZE	(ONYX_CRASH_AREA_SIZE - LOG_OFFSET)

static TCrashHeader *s_pRec = 0;		// the live record
static boolean s_bKept = FALSE;			// in the RAM kept out of the heap (else on the heap: not kept)
static u32 s_nPrevMagic = 0, s_nPrevState = 0;	// (what the kept RAM held at boot, for kmsg)
static char *s_pLog = 0;
static u8 *s_pPrev = 0;				// the previous session's record, when it did not end cleanly
static CBcmWatchdog s_Watchdog;
static unsigned s_nWatchdog = 0;
static unsigned s_nFeed = 0;
static volatile unsigned s_nWatchPid = 0;	// (CrashLogWatchPid)

// ---- the dump by core 1 -------------------------------------------------------------------------
// When core 0 stops (the reaper's stamp not moving for DUMP_AFTER_S), core 1 (the sound core,
// woken every ~1 ms by the timer's event stream) writes the report into the sectors of
// SD:/etc/crashdump.txt -- found at boot through FatFs, written raw through the SD device: no
// FatFs, no heap, no logger, no scheduler (core 0 may hold any of their locks) -- then restarts
// the Pi. Kept at the next boot as SD:/etc/lastcrash.txt. Works whether or not the RAM survives.
#define DUMP_SIZE	0x10000
#define DUMP_SECTORS	(DUMP_SIZE / 512)
#define DUMP_AFTER_S	10
#define DUMP_PATH	"SD:/etc/crashdump.txt"
#define DUMP_MARK	"ONYX CRASH REPORT"
#define DUMP_END	"\r\n--- end of the report ---\r\n"
#define LASTCRASH_PATH	"SD:/etc/lastcrash.txt"
static u64 s_DumpLBA[DUMP_SECTORS];
static unsigned s_nDumpSectors = 0;
static CDevice *s_pDumpDev = 0;
static char *s_DumpBuf = 0;				// (DUMP_SIZE, on the heap: see CrashLogInit)
static volatile boolean s_bArmed = FALSE;
static volatile u64 s_ulAliveCnt = 0;			// CNTPCT at the reaper's last pass
volatile boolean g_bCrashDumping = FALSE;		// (OnyxDriverWait: never yield then)
static const char *volatile s_pReason = 0;		// a report asked for (CrashLogDumpNow)

// ---- the app watchdog's report (SD:/etc/apphang.txt) -------------------------------------------------
#define APPHANG_PATH	"SD:/etc/apphang.txt"
#define LASTHANG_PATH	"SD:/etc/lasthang.txt"
#define APPHANG_SIZE	0x8000

static inline u64 Cntpct (void) { u64 v; asm volatile ("isb; mrs %0, cntpct_el0" : "=r" (v)); return v; }
static inline u64 Cntfrq (void) { u64 v; asm volatile ("mrs %0, cntfrq_el0" : "=r" (v)); return v; }

static inline void Clean (const volatile void *p, size_t n)
{
	CleanDataCacheRange ((uintptr) p, n);
}

void CrashLogInit (void)
{
	if (s_pRec != 0) return;
	s_bKept = g_ulOnyxCrashArea != 0;
	// (the buffers on the heap, not in the BSS: the kernel image + BSS must stay under Circle's
	// KERNEL_MAX_SIZE, 2 MB -- past it the BSS runs over the kernel's stacks: no boot at all)
	TCrashHeader *pRec = (TCrashHeader *) (s_bKept ? (uintptr) g_ulOnyxCrashArea : (uintptr) new u8[ONYX_CRASH_AREA_SIZE]);
	if (pRec == 0) return;
	u32 nBoot = 0;
	if (s_bKept) { s_nPrevMagic = pRec->nMagic; s_nPrevState = pRec->nState; }
	if (s_bKept && pRec->nMagic == CRASH_MAGIC && pRec->nVersion == CRASH_VERSION)
	{
		nBoot = pRec->nBoot;
		if (pRec->nState == STATE_RUNNING || pRec->nState == STATE_PANIC)
		{
			s_pPrev = new u8[ONYX_CRASH_AREA_SIZE];
			if (s_pPrev != 0) memcpy (s_pPrev, pRec, ONYX_CRASH_AREA_SIZE);
		}
	}
	memset (pRec, 0, sizeof *pRec);
	pRec->nMagic = CRASH_MAGIC; pRec->nVersion = CRASH_VERSION;
	pRec->nState = STATE_RUNNING; pRec->nBoot = nBoot + 1;
	Clean (pRec, sizeof *pRec);
	s_pLog = (char *) pRec + LOG_OFFSET;
	s_pRec = pRec;
}

void CrashLogText (const void *pText, size_t nCount)
{
	TCrashHeader *pRec = s_pRec;
	if (pRec == 0 || nCount == 0) return;
	if (nCount > LOG_SIZE / 2) { pText = (const char *) pText + nCount - LOG_SIZE / 2; nCount = LOG_SIZE / 2; }
	u32 nPos = __atomic_fetch_add (&pRec->nLogHead, (u32) nCount, __ATOMIC_RELAXED);
	const char *p = (const char *) pText;
	u32 i = nPos % LOG_SIZE;
	size_t nFirst = LOG_SIZE - i < nCount ? LOG_SIZE - i : nCount;
	memcpy (s_pLog + i, p, nFirst);
	Clean (s_pLog + i, nFirst);
	if (nFirst < nCount) { memcpy (s_pLog, p + nFirst, nCount - nFirst); Clean (s_pLog, nCount - nFirst); }
	Clean (&pRec->nLogHead, 4);
}

// ---- the return addresses on a stack --------------------------------------------------------------
// No frame pointers to follow (the apps are built -O2): the words of the stack that point just
// after a BL / BLR, in an app's code ([8 GB, 10 GB)) or the kernel's ([0x80000, _etext)), newest
// first. Every page is probed with AT S1E1R before it is read (a wild SP, an unmapped page:
// no fault). Only from core 0's IRQ or exception path, with the context's own TTBR0.
extern "C" u8 _etext;

static inline boolean Readable (u64 ulVA)
{
	u64 ulPAR;
	asm volatile ("at s1e1r, %1; isb; mrs %0, par_el1" : "=r" (ulPAR) : "r" (ulVA));
	return !(ulPAR & 1);
}

static inline boolean IsCode (u64 v)
{
	return (v >= USER_VA_BASE && v < USER_HEAP_BASE) || (v >= 0x80000 && v < (u64) (uintptr) &_etext);
}

static boolean AfterCall (u64 v)
{
	if ((v & 3) != 0 || !IsCode (v) || !Readable (v - 4)) return FALSE;
	u32 nInsn = *(const volatile u32 *) (uintptr) (v - 4);
	return (nInsn & 0xFC000000u) == 0x94000000u			// BL
	    || (nInsn & 0xFFFFFC1Fu) == 0xD63F0000u;			// BLR
}

static void ScanStack (TCrashStack *pStack, const TTrapFrame *pFrame)
{
	u64 ulSP = (pFrame->spsr_el1 & 0xF) == 0x5 ? (u64) (uintptr) pFrame + TF_SIZE : pFrame->sp_el0;
	pStack->ulPC = pFrame->elr_el1; pStack->ulLR = pFrame->x[30]; pStack->ulSP = ulSP;
	pStack->nTick = (u32) CTimer::Get ()->GetTicks ();
	unsigned n = 0;
	u64 ulPage = ~0ul;
	for (u64 p = ulSP & ~7ul; p < (ulSP & ~7ul) + STACK_SCAN && n < STACK_ADDRS; p += 8)
	{
		if ((p & ~0xFFFul) != ulPage)
		{
			ulPage = p & ~0xFFFul;
			if (!Readable (p)) break;
		}
		u64 v = *(const volatile u64 *) (uintptr) p;
		if (AfterCall (v)) pStack->Addr[n++] = v;
	}
	pStack->nCount = n;
	Clean (pStack, sizeof *pStack);
}

static void FillSample (TCrashSample &S, const TTrapFrame *pFrame, const char *pName)
{
	S.ulPC = pFrame->elr_el1; S.ulLR = pFrame->x[30]; S.ulSP = pFrame->sp_el0;
	S.nSPSR = (u32) pFrame->spsr_el1; S.nTick = (u32) CTimer::Get ()->GetTicks ();
	unsigned i = 0;
	if (pName != 0) for (; pName[i] && i + 1 < SAMPLE_TASK; i++) S.szTask[i] = pName[i];
	S.szTask[i] = 0;
	Clean (&S, sizeof S);
}

void CrashLogSample (const TTrapFrame *pFrame)
{
	TCrashHeader *pRec = s_pRec;
	if (pRec == 0) return;
	CTask *pTask = CScheduler::IsActive () ? CScheduler::Get ()->GetCurrentTask () : 0;
	const char *pName = pTask != 0 ? pTask->GetName () : 0;
	FillSample (pRec->Sample[pRec->nSampleHead % SAMPLES], pFrame, pName);
	pRec->nSampleHead++;
	Clean (&pRec->nSampleHead, 4);

	unsigned nPid = s_nWatchPid;			// the app the GUI watchdog finds frozen
	if (nPid == 0 || pTask == 0) return;
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	if (pAS == 0 || pAS->GetPid () != nPid) return;
	if (pRec->nWatchHead % 8 == 0) ScanStack (&pRec->WatchStack, pFrame);
	FillSample (pRec->Watch[pRec->nWatchHead % SAMPLES], pFrame, pName);
	pRec->nWatchHead++;
	Clean (&pRec->nWatchHead, 4);
}

void CrashLogStack (const TTrapFrame *pFrame)
{
	if (s_pRec != 0) ScanStack (&s_pRec->FaultStack, pFrame);
}

void CrashLogWatchPid (unsigned nPid)
{
	TCrashHeader *pRec = s_pRec;
	if (pRec == 0 || s_nWatchPid == nPid) return;
	s_nWatchPid = 0;
	DataMemBarrier ();
	pRec->nWatchHead = 0;
	memset (&pRec->WatchStack, 0, sizeof pRec->WatchStack);
	pRec->nWatchPid = nPid;
	Clean (pRec, sizeof *pRec);
	DataMemBarrier ();
	s_nWatchPid = nPid;
}

void CrashLogAlive (void)
{
	TCrashHeader *pRec = s_pRec;
	if (pRec != 0)
	{
		pRec->nAliveTick = (u32) CTimer::Get ()->GetTicks ();
		Clean (&pRec->nAliveTick, 4);
	}
	s_ulAliveCnt = Cntpct ();
	if (s_nWatchdog != 0 && ++s_nFeed >= 20)		// (once a second)
	{
		s_nFeed = 0;
		s_Watchdog.Start (s_nWatchdog);
	}
}

static void StartAppHangTask (void);

void CrashLogStartWatchdog (unsigned nSeconds)
{
	if (nSeconds > CBcmWatchdog::MaxTimeoutSeconds) nSeconds = CBcmWatchdog::MaxTimeoutSeconds;
	s_nWatchdog = nSeconds;
	if (s_pRec != 0) { s_pRec->nWatchdog = nSeconds; Clean (&s_pRec->nWatchdog, 4); }
	if (nSeconds != 0) s_Watchdog.Start (nSeconds);
	CLogger::Get ()->RegisterPanicHandler (CrashLogPanicHandler);
	StartAppHangTask ();
	CLogger::Get ()->Write ("crashlog", LogNotice, "hang watchdog: %s (%u s); crash record at %lX (%s; at boot: magic %08X state %u); "
				"core 1 dump into SD:/etc/crashdump.txt: %s", nSeconds ? "on" : "off", nSeconds,
				(unsigned long) (uintptr) s_pRec, s_bKept ? "RAM kept out of the heap" : "no RAM above 3 GB: not kept",
				s_nPrevMagic, s_nPrevState, s_nDumpSectors ? "armed" : "not armed");
}

void CrashLogCrumb (unsigned nIndex, u32 nValue)
{
	TCrashHeader *pRec = s_pRec;
	if (pRec == 0 || nIndex >= CRUMB_COUNT) return;
	pRec->Crumb[nIndex] = nValue;
	Clean (&pRec->Crumb[nIndex], 4);
}

void CrashLogMemory (void)
{
	CMemorySystem *pMem = CMemorySystem::Get ();
	if (pMem == 0) return;
	CrashLogCrumb (CRUMB_HEAP_KB, (u32) ((pMem->GetHeapFreeSpace (HEAP_ANY) + pMem->GetHeapFreeListSpace ()) / 1024));
	CrashLogCrumb (CRUMB_PAGES_KB, (u32) ((CMemorySystem::GetPagerFreeSpace () + CMemorySystem::GetPagerFreeListSpace ()) / 1024));
	CrashLogCrumb (CRUMB_APPMEM_KB, (u32) ((CMemorySystem::GetPagerHighFreeSpace ()
						 + CMemorySystem::GetPagerHighFreeListSpace ()) / 1024));
}

void CrashLogPanic (const char *pLine)
{
	TCrashHeader *pRec = s_pRec;
	if (pRec == 0) return;
	unsigned i = 0;
	for (; pLine[i] && i + 1 < sizeof pRec->szPanic; i++) pRec->szPanic[i] = pLine[i];
	pRec->szPanic[i] = 0;
	pRec->nState = STATE_PANIC;
	Clean (pRec, sizeof *pRec);
}

void CrashLogCleanEnd (void)
{
	s_bArmed = FALSE;
	if (s_nWatchdog != 0) { s_nWatchdog = 0; s_Watchdog.Stop (); }
	f_unlink (LASTHANG_PATH);			// (an app still frozen at the end: not a crash)
	f_rename (APPHANG_PATH, LASTHANG_PATH);
	TCrashHeader *pRec = s_pRec;
	if (pRec == 0) return;
	pRec->nState = STATE_CLEAN;
	Clean (&pRec->nState, 4);
}

static const char *ThrottleText (u32 v, char *pBuf);

// ---- the report's text, without the heap (core 1 formats it too) -----------------------------------
struct TOut
{
	char	*pBuf;
	unsigned nLen, nCap;
};

static void Out (TOut &o, const char *p) { while (*p && o.nLen < o.nCap) o.pBuf[o.nLen++] = *p++; }
static void OutN (TOut &o, const char *p, unsigned n) { for (unsigned i = 0; i < n && o.nLen < o.nCap; i++) o.pBuf[o.nLen++] = p[i]; }
static void OutHex (TOut &o, u64 v, unsigned nDigits)
{
	char b[17]; for (int i = (int) nDigits - 1; i >= 0; i--) { b[i] = "0123456789ABCDEF"[v & 15]; v >>= 4; }
	b[nDigits] = 0; Out (o, b);
}
static void OutDec (TOut &o, u64 v)
{
	char b[24]; int i = 23; b[i] = 0;
	do { b[--i] = (char) ('0' + v % 10); v /= 10; } while (v != 0);
	Out (o, b + i);
}
static void OutTicks (TOut &o, u32 t) { OutDec (o, t / 100); Out (o, "."); OutDec (o, (t % 100) / 10); OutDec (o, t % 10); Out (o, " s"); }

static const char *CrumbName (unsigned nIndex, u32 v)
{
	static const char *V3D[] = { "idle", "clipping", "binning", "rendering", "texture upload" };
	static const char *Present[] = { "idle", "composing", "display DMA", "waiting for the display DMA" };
	if (nIndex == CRUMB_V3D) return v < 5 ? V3D[v] : "?";
	if (nIndex == CRUMB_PRESENT) return v < 4 ? Present[v] : "?";
	return "";
}

static void OutSamples (TOut &o, const TCrashSample *pSample, u32 nHead)
{
	for (unsigned k = 0; k < SAMPLES && k < nHead; k++)
	{
		const TCrashSample &S = pSample[(nHead - 1 - k) % SAMPLES];
		Out (o, "  t "); OutTicks (o, S.nTick);
		Out (o, "  pc "); OutHex (o, S.ulPC, 16); Out (o, "  lr "); OutHex (o, S.ulLR, 16); Out (o, "  sp "); OutHex (o, S.ulSP, 16);
		Out (o, IS_USER_VA (S.ulPC) ? "  app code  " : "  kernel    ");
		OutN (o, S.szTask, strnlen (S.szTask, SAMPLE_TASK));
		if (S.nSPSR & 0x80) Out (o, "  (IRQs were masked)");
		Out (o, "\r\n");
	}
}

static void OutStack (TOut &o, const TCrashStack &S)
{
	Out (o, "  at t "); OutTicks (o, S.nTick);
	Out (o, ": pc "); OutHex (o, S.ulPC, 16); Out (o, "  lr "); OutHex (o, S.ulLR, 16); Out (o, "  sp "); OutHex (o, S.ulSP, 16);
	Out (o, "\r\n  return addresses on its stack (newest first; app code from 200000000, else the kernel's):\r\n");
	for (unsigned i = 0; i < S.nCount && i < STACK_ADDRS; i++)
	{
		Out (o, (i % 6) == 0 ? "   " : " "); OutHex (o, S.Addr[i], 9);
		if (i % 6 == 5 || i + 1 == S.nCount) Out (o, "\r\n");
	}
	if (S.nCount == 0) Out (o, "   (none found)\r\n");
}

// Everything the record holds, then as much of the log's tail as nLogMax (and the room) allows.
static void OutRecord (TOut &o, const TCrashHeader *pRec, const char *pLog, unsigned nLogMax)
{
	if (pRec->nState == STATE_PANIC) { Out (o, "A kernel panic: "); Out (o, pRec->szPanic); Out (o, "\r\n"); }
	Out (o, "Last pass of the reaper (the scheduler alive): "); OutTicks (o, pRec->nAliveTick);
	Out (o, " after boot; hang watchdog "); OutDec (o, pRec->nWatchdog); Out (o, " s\r\n");
	{
		char Buf[128];
		Out (o, "Power: SoC "); OutDec (o, pRec->Crumb[CRUMB_TEMP]); Out (o, " C (max "); OutDec (o, pRec->Crumb[CRUMB_TEMP_MAX]);
		Out (o, " C this session), throttling: "); Out (o, ThrottleText (pRec->Crumb[CRUMB_THROTTLED], Buf)); Out (o, "\r\n");
	}
	Out (o, "Free memory (checked every second): kernel heap "); OutDec (o, pRec->Crumb[CRUMB_HEAP_KB]);
	Out (o, " KB, kernel pages "); OutDec (o, pRec->Crumb[CRUMB_PAGES_KB]);
	Out (o, " KB, app pages "); OutDec (o, pRec->Crumb[CRUMB_APPMEM_KB]); Out (o, " KB\r\n");
	Out (o, "GPU: "); Out (o, CrumbName (CRUMB_V3D, pRec->Crumb[CRUMB_V3D]));
	Out (o, ", display: "); Out (o, CrumbName (CRUMB_PRESENT, pRec->Crumb[CRUMB_PRESENT])); Out (o, "\r\n");

	Out (o, "\r\nCore 0, last interrupted at (newest first; t = time after boot; IRQ timer ticks):\r\n");
	OutSamples (o, pRec->Sample, pRec->nSampleHead);
	if (pRec->FaultStack.ulSP != 0)
	{
		Out (o, "\r\nThe context of the exception:\r\n");
		OutStack (o, pRec->FaultStack);
	}
	if (pRec->nWatchPid != 0)
	{
		Out (o, "\r\nThe app the GUI watchdog found frozen (pid "); OutDec (o, pRec->nWatchPid);
		Out (o, "); its task, when core 0 was interrupted in it (newest first):\r\n");
		if (pRec->nWatchHead == 0) Out (o, "  (never: it did not run since -- blocked, or waiting)\r\n");
		OutSamples (o, pRec->Watch, pRec->nWatchHead);
		if (pRec->WatchStack.ulSP != 0) OutStack (o, pRec->WatchStack);
	}
	Out (o, "(addresses: aarch64-none-elf-addr2line -f -C -e <app>.elf, or kernel8-rpi4.elf)\r\n");

	Out (o, "\r\nThe last kernel log lines:\r\n");
	u32 nHead = pRec->nLogHead, nLen = nHead < LOG_SIZE ? nHead : LOG_SIZE;
	if (nLen > nLogMax) nLen = nLogMax;
	if (nLen > o.nCap - o.nLen) nLen = o.nCap - o.nLen;
	u32 nStart = (nHead - nLen) % LOG_SIZE;
	for (u32 i = 0; i < nLen; i++) o.pBuf[o.nLen++] = pLog[(nStart + i) % LOG_SIZE];
}

// ---- core 0, at boot: the previous session's report ------------------------------------------------
static void ReportFromRAM (boolean *pbWritten);
static void ArmDump (boolean *pbDumped);
static void MergeAppHang (boolean bWritten);

void CrashLogReport (void)
{
	boolean bDumped = FALSE, bWritten = FALSE;
	ArmDump (&bDumped);			// (a report core 1 wrote: into lastcrash.txt first)
	if (bDumped) { if (s_pPrev != 0) { delete [] s_pPrev; s_pPrev = 0; } }
	else ReportFromRAM (&bWritten);
	MergeAppHang (bDumped || bWritten);
}

static void ReportFromRAM (boolean *pbWritten)
{
	*pbWritten = FALSE;
	if (s_pPrev == 0) return;
	const TCrashHeader *pRec = (const TCrashHeader *) s_pPrev;
	const char *pLog = (const char *) s_pPrev + LOG_OFFSET;
	char *pBuf = new char[DUMP_SIZE];
	FIL File;
	if (pBuf == 0 || f_open (&File, LASTCRASH_PATH, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		delete [] pBuf;
		delete [] s_pPrev; s_pPrev = 0;
		return;
	}
	TOut o = { pBuf, 0, DUMP_SIZE };
	Out (o, "Onyx crash record -- session #"); OutDec (o, pRec->nBoot); Out (o, " did not end with shutdown / reboot\r\n");
	if (pRec->nState != STATE_PANIC) Out (o, "Ended without a panic: a hang (the watchdog restarted the Pi), or the power was cut.\r\n");
	static const char *Step[] = { "not started (core 1 did not see the hang, or was stuck too)", "started", "text ready, the SD write never ended",
				      "written", "the SD write failed" };
	Out (o, "Core 1's report into SD:/etc/crashdump.txt: "); Out (o, Step[pRec->nDumpStep < 5 ? pRec->nDumpStep : 0]); Out (o, "\r\n");
	OutRecord (o, pRec, pLog, LOG_SIZE);
	UINT n;
	f_write (&File, pBuf, o.nLen, &n);
	f_close (&File);
	delete [] pBuf;
	*pbWritten = TRUE;
	CLogger::Get ()->Write ("crashlog", LogWarning, "the previous session (#%u) did not end cleanly (%s): see SD:/etc/lastcrash.txt",
				pRec->nBoot, pRec->nState == STATE_PANIC ? "a kernel panic" : "a hang or a power cut");
	delete [] s_pPrev; s_pPrev = 0;
}

// The app watchdog's last report of the previous session (the session did not end cleanly:
// CrashLogCleanEnd renames it): added to lastcrash.txt, or lastcrash.txt when there is no other.
static void MergeAppHang (boolean bWritten)
{
	FIL In, Out;
	if (f_open (&In, APPHANG_PATH, FA_READ) != FR_OK) return;
	if (f_open (&Out, LASTCRASH_PATH, bWritten ? FA_WRITE | FA_OPEN_APPEND : FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
	{
		static const char Sep[] = "\r\n\r\n=== The app watchdog's last report of that session (SD:/etc/apphang.txt) ===\r\n";
		static const char Alone[] = "Onyx crash record -- the previous session did not end with shutdown / reboot, and the kernel\r\n"
					    "kept no record of it (core 1's dump not written, the RAM not kept). The app watchdog's last\r\n"
					    "report of that session (SD:/etc/apphang.txt) follows.\r\n\r\n";
		UINT n, m;
		if (bWritten) f_write (&Out, Sep, sizeof Sep - 1, &n);
		else f_write (&Out, Alone, sizeof Alone - 1, &n);
		static char Buf[4096];
		while (f_read (&In, Buf, sizeof Buf, &n) == FR_OK && n != 0) f_write (&Out, Buf, n, &m);
		f_close (&Out);
		CLogger::Get ()->Write ("crashlog", LogWarning, "the app watchdog reported a frozen app before the end of the previous session: "
					"see SD:/etc/lastcrash.txt");
	}
	f_close (&In);
	f_unlink (LASTHANG_PATH);
	f_rename (APPHANG_PATH, LASTHANG_PATH);
}

// ---- core 0, at boot: the previous dump kept, the file made ready for this session ---------------
static void ArmDump (boolean *pbDumped)
{
	*pbDumped = FALSE;
	if (s_DumpBuf == 0 && (s_DumpBuf = new char[DUMP_SIZE]) == 0) return;
	static FIL File;
	UINT n;
	if (f_open (&File, DUMP_PATH, FA_READ | FA_WRITE) == FR_OK && f_size (&File) == DUMP_SIZE)
	{
		if (f_read (&File, s_DumpBuf, DUMP_SIZE, &n) == FR_OK && n == DUMP_SIZE
		    && memcmp (s_DumpBuf, DUMP_MARK, sizeof DUMP_MARK - 1) == 0)
		{
			unsigned nLen = 0;			// up to the end mark
			const char *pEnd = 0;
			for (unsigned i = 0; i + sizeof DUMP_END - 1 <= DUMP_SIZE && pEnd == 0; i++)
				if (s_DumpBuf[i] == DUMP_END[0] && memcmp (s_DumpBuf + i, DUMP_END, sizeof DUMP_END - 1) == 0)
					pEnd = s_DumpBuf + i;
			nLen = pEnd ? (unsigned) (pEnd - s_DumpBuf) + sizeof DUMP_END - 1 : DUMP_SIZE;
			FIL Out;
			if (f_open (&Out, LASTCRASH_PATH, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
			{
				f_write (&Out, s_DumpBuf, nLen, &n);
				f_close (&Out);
				*pbDumped = TRUE;
				CLogger::Get ()->Write ("crashlog", LogWarning, "the previous session froze: see SD:/etc/lastcrash.txt");
			}
		}
	}
	else
	{
		f_close (&File);
		if (f_open (&File, DUMP_PATH, FA_READ | FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return;
	}
	// this session's: "armed", the sectors of the file noted
	memset (s_DumpBuf, ' ', DUMP_SIZE);
	memcpy (s_DumpBuf, "(armed: no crash report)\r\n", 26);
	f_lseek (&File, 0);
	if (f_write (&File, s_DumpBuf, DUMP_SIZE, &n) != FR_OK || n != DUMP_SIZE || f_sync (&File) != FR_OK) { f_close (&File); return; }
	unsigned k = 0;
	for (; k < DUMP_SECTORS; k++)
	{
		u8 b;
		if (f_lseek (&File, (FSIZE_t) k * 512) != FR_OK || f_read (&File, &b, 1, &n) != FR_OK || n != 1 || File.sect == 0) break;
		s_DumpLBA[k] = File.sect;
	}
	f_close (&File);
	s_pDumpDev = CDeviceNameService::Get ()->GetDevice ("emmc1", TRUE);
	if (k != DUMP_SECTORS || s_pDumpDev == 0) return;
	s_nDumpSectors = k;
	s_ulAliveCnt = Cntpct ();
	s_bArmed = TRUE;
}

// ---- core 1: signs on the green ACT LED (headless: no screen) ------------------------------------
static void FeedWatchdog (void)			// (as CBcmWatchdog::Start, without its lock)
{
	if (s_nWatchdog == 0) return;
	write32 (ARM_PM_WDOG, ARM_PM_PASSWD | ((s_nWatchdog << 16) & ARM_PM_WDOG_TIME));
	write32 (ARM_PM_RSTC, ARM_PM_PASSWD | ARM_PM_RSTC_REBOOT | (read32 (ARM_PM_RSTC) & ARM_PM_RSTC_CLEAR));
}

static void WaitMs (unsigned nMs)
{
	u64 t0 = Cntpct (), n = Cntfrq () * nMs / 1000;
	while (Cntpct () - t0 < n) {}
}

// nCount blinks of nOnMs on / nOffMs off (CActLED: a GPIO set / clear, no lock)
static void Blink (unsigned nCount, unsigned nOnMs, unsigned nOffMs)
{
	CActLED *pLED = CActLED::Get ();
	for (unsigned i = 0; i < nCount; i++)
	{
		if (pLED) pLED->On ();
		WaitMs (nOnMs);
		if (pLED) pLED->Off ();
		WaitMs (nOffMs);
		FeedWatchdog ();
	}
}

static void BuildDump (u64 ulStalled)
{
	const TCrashHeader *pRec = s_pRec;
	TOut o = { s_DumpBuf, 0, DUMP_SIZE - 64 };
	Out (o, DUMP_MARK " -- session #"); OutDec (o, pRec->nBoot);
	if (s_pReason != 0) { Out (o, ": "); Out (o, s_pReason); Out (o, "; written by core 1, then the Pi restarted\r\n"); }
	else { Out (o, ": core 0 stopped for "); OutDec (o, ulStalled); Out (o, " s; written by core 1, then the Pi restarted\r\n"); }
	Out (o, "Now: "); OutTicks (o, (u32) CTimer::Get ()->GetTicks ()); Out (o, " after boot (IRQ timer ticks; frozen too if core 0 no longer takes IRQs)\r\n");
	OutRecord (o, pRec, s_pLog, LOG_SIZE);
	o.nCap = DUMP_SIZE;
	OutN (o, DUMP_END, sizeof DUMP_END - 1);
	while (o.nLen < DUMP_SIZE) s_DumpBuf[o.nLen++] = ' ';
}

void CrashLogCoreCheck (void)
{
	if (!s_bArmed || s_nWatchdog == 0) return;	// (hangreboot=0: no dump, no restart)
	u64 ulNow = Cntpct (), ulFrq = Cntfrq ();
	u64 ulAlive = s_ulAliveCnt;
	if (ulFrq == 0 || (s_pReason == 0 && ulNow - ulAlive < DUMP_AFTER_S * ulFrq)) return;
	s_bArmed = FALSE;
	g_bCrashDumping = TRUE;
	TCrashHeader *pRec = s_pRec;
	pRec->nDumpStep = 1; Clean (&pRec->nDumpStep, 4);
	DataMemBarrier ();
	Blink (30, 50, 50);				// core 1 saw the hang: 3 s of fast blinks
	BuildDump ((ulNow - ulAlive) / ulFrq);
	CleanDataCacheRange ((uintptr) s_DumpBuf, DUMP_SIZE);
	pRec->nDumpStep = 2; Clean (&pRec->nDumpStep, 4);
	FeedWatchdog ();
	boolean bOK = TRUE;
	for (unsigned k = 0; k < s_nDumpSectors; )
	{
		unsigned n = 1;				// (a run of consecutive sectors: one write)
		while (k + n < s_nDumpSectors && s_DumpLBA[k + n] == s_DumpLBA[k] + n) n++;
		if (   s_pDumpDev->Seek (s_DumpLBA[k] * 512) != s_DumpLBA[k] * 512
		    || s_pDumpDev->Write (s_DumpBuf + k * 512, n * 512) != (int) (n * 512))
			bOK = FALSE;
		k += n;
	}
	pRec->nDumpStep = bOK ? 3 : 4; Clean (&pRec->nDumpStep, 4);
	DataSyncBarrier ();
	if (bOK) { CActLED *pLED = CActLED::Get (); if (pLED) pLED->On (); WaitMs (3000); }	// written: 3 s on
	else Blink (3, 1000, 1000);					// failed: 3 slow blinks
	s_Watchdog.Restart ();			// (does not return)
}

void CrashLogCoreInit (void)
{
	// the timer's event stream: a WFE ends at least every 2^16 counter ticks (~1.2 ms)
	u64 v; asm volatile ("mrs %0, cntkctl_el1" : "=r" (v));
	v = (v & ~0xF0ul) | (15ul << 4) | (1ul << 2);
	asm volatile ("msr cntkctl_el1, %0; isb" :: "r" (v));
}

// Core 1 writes the report now and restarts the Pi; this core waits with its IRQs masked (no
// SD access meanwhile). Returns only when there is no dump to write (not armed, hangreboot=0).
void CrashLogDumpNow (const char *pReason)
{
	if (!s_bArmed || s_nWatchdog == 0) return;
	if (CMultiCoreSupport::ThisCore () == 1)	// (core 1 itself: it writes it here)
	{
		s_pReason = pReason;
		CrashLogCoreCheck ();
		return;
	}
	asm volatile ("msr daifset, #3" ::: "memory");
	s_pReason = pReason;
	DataSyncBarrier ();
	asm volatile ("sev");
	for (;;) asm volatile ("wfe");			// core 1 writes the report and restarts the Pi
}

void CrashLogRequest (const char *pReason)
{
	if (!s_bArmed || s_nWatchdog == 0) return;
	CLogger::Get ()->Write ("crashlog", LogError, "%s: crash report, then restart", pReason);
	CrashLogDumpNow (pReason);
}

// Circle's panic (an assertion, the kernel heap "Out of memory", ...): its logger calls this
// right after the panic line, then halts EVERY core -- core 1 too, so it never wrote its report
// and the hardware watchdog restarted the Pi 15 s later with nothing on the card. The panic line
// (the log's last) goes into the record, and core 1 writes the report before the halt.
void CrashLogPanicHandler (void)
{
	TCrashHeader *pRec = s_pRec;
	if (pRec != 0 && pRec->nState != STATE_PANIC)
	{
		char Line[sizeof pRec->szPanic];	// the log's last line, without its escape sequences
		u32 nHead = pRec->nLogHead, nLen = nHead < LOG_SIZE ? nHead : LOG_SIZE, nEnd = 0, nStart;
		while (nEnd < nLen && (s_pLog[(nHead - 1 - nEnd) % LOG_SIZE] == '\n' || s_pLog[(nHead - 1 - nEnd) % LOG_SIZE] == '\r')) nEnd++;
		for (nStart = nEnd; nStart < nLen && nStart - nEnd < 400 && s_pLog[(nHead - 1 - nStart) % LOG_SIZE] != '\n'; nStart++) {}
		unsigned n = 0;
		for (u32 k = nStart; k > nEnd && n + 1 < sizeof Line; k--)
		{
			char c = s_pLog[(nHead - k) % LOG_SIZE];
			if (c == '\x1b') { while (k > nEnd + 1 && s_pLog[(nHead - k) % LOG_SIZE] != 'm') k--; continue; }
			if ((unsigned char) c >= ' ') Line[n++] = c;
		}
		Line[n] = 0;
		CrashLogPanic (n != 0 ? Line : "a kernel panic (Circle)");
	}
	CrashLogDumpNow ("a kernel panic (Circle's: every core halted)");
}

// ---- the app watchdog's report: SD:/etc/apphang.txt, rewritten every 2 s while an app is frozen ----
// Written by a task of its own: the GUI watchdog never waits on the SD card (the frozen app may
// hold the file system's lock).
static char *s_AppBuf = 0;			// (APPHANG_SIZE, on the heap)
static char s_szAppWhat[64];
static volatile unsigned s_nAppPid = 0, s_nAppIdle = 0, s_nAppQueued = 0;
static volatile unsigned s_nAppReq = 0, s_nAppDone = 0, s_nAppRecover = 0, s_nAppRecovered = 0;
static unsigned s_nAppReports = 0;

struct TAppTasks { TOut *pOut; unsigned nPid, nFound; };

static boolean AppTaskCollect (CTask *pTask, const char *pName, TTaskState State, TTaskFlags Flags, void *pParam)
{
	static const char *Names[] = { "new", "ready (wants the CPU)", "blocked (waits for an event)", "blocked with a timeout",
				       "sleeping", "terminated" };
	TAppTasks *p = (TAppTasks *) pParam;
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	if (pAS == 0 || pAS->GetPid () != p->nPid) return TRUE;
	Out (*p->pOut, "  task '"); Out (*p->pOut, pName != 0 ? pName : "?"); Out (*p->pOut, "': ");
	Out (*p->pOut, (Flags & TaskFlagRunning) ? "running" : (unsigned) State < 6 ? Names[State] : "?");
	Out (*p->pOut, ", "); OutDec (*p->pOut, (u64) pAS->GetPages () * KPAGE_SIZE / 1024); Out (*p->pOut, " KB of pages\r\n");
	p->nFound++;
	return TRUE;
}

static void WriteAppHang (void)
{
	const TCrashHeader *pRec = s_pRec;
	if (pRec == 0 || (s_AppBuf == 0 && (s_AppBuf = new char[APPHANG_SIZE]) == 0)) return;
	TOut o = { s_AppBuf, 0, APPHANG_SIZE };
	Out (o, "ONYX APP WATCHDOG -- session #"); OutDec (o, pRec->nBoot); Out (o, ", t = ");
	OutTicks (o, (u32) CTimer::Get ()->GetTicks ()); Out (o, " after boot (report #"); OutDec (o, ++s_nAppReports);
	Out (o, ", rewritten every 2 s while it lasts)\r\n");
	if (s_nAppPid != 0)
	{
		Out (o, "'"); Out (o, s_szAppWhat); Out (o, "' (pid "); OutDec (o, s_nAppPid);
		Out (o, ") has not taken its window's events for "); OutDec (o, s_nAppIdle); Out (o, " s ("); OutDec (o, s_nAppQueued);
		Out (o, " queued)\r\n");
		TAppTasks T = { &o, s_nAppPid, 0 };
		if (CScheduler::IsActive ()) CScheduler::Get ()->EnumerateTasks (AppTaskCollect, &T);
		if (T.nFound == 0) Out (o, "  (no task of that pid)\r\n");
	}
	else { Out (o, s_szAppWhat); Out (o, " for "); OutDec (o, s_nAppIdle); Out (o, " s\r\n"); }
	OutRecord (o, pRec, s_pLog, 12 * 1024);
	Out (o, "\r\n");
	FIL File; UINT n;
	if (f_open (&File, APPHANG_PATH, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return;
	f_write (&File, s_AppBuf, o.nLen, &n);
	f_close (&File);
}

class CAppHangTask : public CTask
{
public:
	CAppHangTask (void) { SetName ("apphang"); }

	void Run (void) override
	{
		for (;;)
		{
			CScheduler::Get ()->MsSleep (200);
			if (s_nAppReq != s_nAppDone)
			{
				s_nAppDone = s_nAppReq;
				WriteAppHang ();
			}
			if (s_nAppRecover != s_nAppRecovered)
			{
				s_nAppRecovered = s_nAppRecover;
				s_nAppReports = 0;
				f_unlink (LASTHANG_PATH);
				f_rename (APPHANG_PATH, LASTHANG_PATH);
			}
		}
	}
};

static void StartAppHangTask (void)
{
	static boolean s_bStarted = FALSE;
	if (s_bStarted) return;
	s_bStarted = TRUE;
	new CAppHangTask;
}

void CrashLogAppHang (const char *pWhat, unsigned nPid, unsigned nIdleSec, unsigned nQueued)
{
	unsigned i = 0;
	for (; pWhat != 0 && pWhat[i] && i + 1 < sizeof s_szAppWhat; i++) s_szAppWhat[i] = pWhat[i];
	s_szAppWhat[i] = 0;
	s_nAppPid = nPid; s_nAppIdle = nIdleSec; s_nAppQueued = nQueued;
	s_nAppReq++;
}

void CrashLogAppRecovered (void)
{
	CrashLogWatchPid (0);
	s_nAppRecover++;
}

// ---- power and temperature (the firmware's view), once a second from the GUI watchdog --------------
// GET_THROTTLED: bit 0 under-voltage now, 1 ARM frequency capped, 2 throttled, 3 soft temperature
// limit; bits 16-19 the same "has occurred since boot". A change is logged; the last values are
// kept in the crash record (a freeze under a weak supply or heat shows there).
static const char *ThrottleText (u32 v, char *pBuf)
{
	static const char *Name[] = { "under-voltage", "ARM freq capped", "throttled", "soft temp limit" };
	unsigned n = 0; pBuf[0] = 0;
	for (int i = 0; i < 4; i++)
		if (v & (1u << i) || v & (1u << (16 + i)))
		{
			const char *a = Name[i];
			if (n) { pBuf[n++] = ','; pBuf[n++] = ' '; }
			while (*a) pBuf[n++] = *a++;
			const char *b = (v & (1u << i)) ? " NOW" : " (earlier)";
			while (*b) pBuf[n++] = *b++;
		}
	if (n == 0) { const char *a = "none"; while (*a) pBuf[n++] = *a++; }
	pBuf[n] = 0;
	return pBuf;
}

void CrashLogPower (void)
{
	CBcmPropertyTags Tags;
	TPropertyTagSimple Thr; Thr.nValue = 0;
	u32 nThr = Tags.GetTag (PROPTAG_GET_THROTTLED, &Thr, sizeof Thr, 4) ? Thr.nValue : 0xFFFFFFFFu;
	TPropertyTagTemperature Temp; Temp.nTemperatureId = TEMPERATURE_ID;
	u32 nTemp = Tags.GetTag (PROPTAG_GET_TEMPERATURE, &Temp, sizeof Temp, 4) ? Temp.nValue / 1000 : 0;
	static u32 s_nLastThr = 0; static u32 s_nLastTempStep = 0; static boolean s_bFirst = TRUE;
	if (s_pRec != 0)
	{
		s_pRec->Crumb[CRUMB_THROTTLED] = nThr; s_pRec->Crumb[CRUMB_TEMP] = nTemp;
		if (nTemp > s_pRec->Crumb[CRUMB_TEMP_MAX]) s_pRec->Crumb[CRUMB_TEMP_MAX] = nTemp;
		Clean (s_pRec->Crumb, sizeof s_pRec->Crumb);
	}
	char Buf[128];
	u32 nStep = nTemp / 5;
	if (s_bFirst || nThr != s_nLastThr || (nStep != s_nLastTempStep && nTemp >= 60))
		CLogger::Get ()->Write ("power", nThr != 0 && nThr != 0xFFFFFFFFu ? LogWarning : LogNotice,
					"SoC %u C, throttling: %s (%08X)", nTemp, ThrottleText (nThr, Buf), nThr);
	s_bFirst = FALSE; s_nLastThr = nThr; s_nLastTempStep = nStep;
}

// ---- the clock kept across boots (no battery-backed clock on the Pi) ------------------------------------
// Until NTP answers (~15 s after boot, if the network comes up at all), the time was 0 and the
// files written meanwhile (crashdump.txt, lastcrash.txt...) had no date. SD:/etc/clock holds the
// last time seen (UTC seconds, text), written every 10 minutes and at shutdown / reboot; at boot
// it is the time until NTP corrects it (behind by how long the Pi was off).
#define CLOCK_PATH	"SD:/etc/clock"
#define CLOCK_VALID	1700000000u			// (2023: a time NTP or the file gave)

void CrashLogClockRestore (void)
{
	if (CTimer::Get ()->GetUniversalTime () >= CLOCK_VALID) return;
	FIL File; UINT n; char Buf[24];
	if (f_open (&File, CLOCK_PATH, FA_READ) != FR_OK) return;
	boolean bOK = f_read (&File, Buf, sizeof Buf - 1, &n) == FR_OK;
	f_close (&File);
	if (!bOK) return;
	Buf[n] = 0;
	unsigned nTime = 0;
	for (unsigned i = 0; Buf[i] >= '0' && Buf[i] <= '9'; i++) nTime = nTime * 10 + (unsigned) (Buf[i] - '0');
	if (nTime < CLOCK_VALID) return;
	CTimer::Get ()->SetTime (nTime, FALSE);
	CLogger::Get ()->Write ("clock", LogNotice, "time restored from %s (until NTP)", CLOCK_PATH);
}

void CrashLogClockSave (void)
{
	unsigned nTime = CTimer::Get ()->GetUniversalTime ();
	if (nTime < CLOCK_VALID) return;
	char Buf[16]; int i = 15; Buf[i] = 0; Buf[--i] = '\n';
	do { Buf[--i] = (char) ('0' + nTime % 10); nTime /= 10; } while (nTime != 0);
	FIL File; UINT n;
	if (f_open (&File, CLOCK_PATH, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return;
	f_write (&File, Buf + i, (UINT) (15 - i), &n);
	f_close (&File);
}
