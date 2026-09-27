//
// crashlog.cpp -- the crash record kept across a reboot (see kern/crashlog.h).
//
#include <kern/crashlog.h>
#include <kern/trapframe.h>
#include <kern/layout.h>		// IS_USER_VA
#include <circle/memory.h>		// g_ulOnyxCrashArea
#include <circle/synchronize.h>		// CleanDataCacheRange
#include <circle/sched/scheduler.h>
#include <circle/bcmwatchdog.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/string.h>
#include <circle/util.h>
#include <fatfs/ff.h>

#define CRASH_MAGIC	0x4F4E5843u		// "ONXC"
#define CRASH_VERSION	1
#define STATE_RUNNING	1
#define STATE_CLEAN	2
#define STATE_PANIC	3
#define SAMPLES		16
#define SAMPLE_TASK	16

struct TCrashSample
{
	u64	ulPC, ulLR, ulSP;
	u32	nSPSR, nTick;
	char	szTask[SAMPLE_TASK];
};

struct TCrashHeader
{
	u32	nMagic, nVersion, nState, nBoot;
	u32	nAliveTick;			// CTimer ticks (1/100 s) at the reaper's last pass
	u32	nLogHead;			// bytes of log written (ring: % LOG_SIZE)
	u32	nSampleHead;			// samples written (ring: % SAMPLES)
	u32	nWatchdog;			// seconds (0: off)
	u32	Crumb[CRUMB_COUNT];
	TCrashSample Sample[SAMPLES];
	char	szPanic[256];
};

#define LOG_OFFSET	((sizeof (TCrashHeader) + 63) & ~63ul)
#define LOG_SIZE	(ONYX_CRASH_AREA_SIZE - LOG_OFFSET)

static TCrashHeader *s_pRec = 0;		// the live record (0: no crash area)
static char *s_pLog = 0;
static u8 *s_pPrev = 0;				// the previous session's record, when it did not end cleanly
static CBcmWatchdog s_Watchdog;
static unsigned s_nWatchdog = 0;
static unsigned s_nFeed = 0;

static inline void Clean (const volatile void *p, size_t n)
{
	CleanDataCacheRange ((uintptr) p, n);
}

void CrashLogInit (void)
{
	if (g_ulOnyxCrashArea == 0 || s_pRec != 0) return;
	TCrashHeader *pRec = (TCrashHeader *) (uintptr) g_ulOnyxCrashArea;
	u32 nBoot = 0;
	if (pRec->nMagic == CRASH_MAGIC && pRec->nVersion == CRASH_VERSION)
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

void CrashLogSample (const TTrapFrame *pFrame)
{
	TCrashHeader *pRec = s_pRec;
	if (pRec == 0) return;
	TCrashSample &S = pRec->Sample[pRec->nSampleHead % SAMPLES];
	S.ulPC = pFrame->elr_el1; S.ulLR = pFrame->x[30]; S.ulSP = pFrame->sp_el0;
	S.nSPSR = (u32) pFrame->spsr_el1; S.nTick = (u32) CTimer::Get ()->GetTicks ();
	const char *pName = 0;
	CTask *pTask = CScheduler::IsActive () ? CScheduler::Get ()->GetCurrentTask () : 0;
	if (pTask != 0) pName = pTask->GetName ();
	unsigned i = 0;
	if (pName != 0) for (; pName[i] && i + 1 < SAMPLE_TASK; i++) S.szTask[i] = pName[i];
	S.szTask[i] = 0;
	pRec->nSampleHead++;
	Clean (&S, sizeof S);
	Clean (&pRec->nSampleHead, 4);
}

void CrashLogAlive (void)
{
	TCrashHeader *pRec = s_pRec;
	if (pRec != 0)
	{
		pRec->nAliveTick = (u32) CTimer::Get ()->GetTicks ();
		Clean (&pRec->nAliveTick, 4);
	}
	if (s_nWatchdog != 0 && ++s_nFeed >= 20)		// (once a second)
	{
		s_nFeed = 0;
		s_Watchdog.Start (s_nWatchdog);
	}
}

void CrashLogStartWatchdog (unsigned nSeconds)
{
	if (nSeconds > CBcmWatchdog::MaxTimeoutSeconds) nSeconds = CBcmWatchdog::MaxTimeoutSeconds;
	s_nWatchdog = nSeconds;
	if (s_pRec != 0) { s_pRec->nWatchdog = nSeconds; Clean (&s_pRec->nWatchdog, 4); }
	if (nSeconds != 0) s_Watchdog.Start (nSeconds);
	CLogger::Get ()->Write ("crashlog", LogNotice, "hang watchdog: %s (%u s), crash record: %s",
				nSeconds ? "on" : "off", nSeconds, s_pRec ? "kept across a reboot" : "none (no RAM above 3 GB)");
}

void CrashLogCrumb (unsigned nIndex, u32 nValue)
{
	TCrashHeader *pRec = s_pRec;
	if (pRec == 0 || nIndex >= CRUMB_COUNT) return;
	pRec->Crumb[nIndex] = nValue;
	Clean (&pRec->Crumb[nIndex], 4);
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
	if (s_nWatchdog != 0) { s_nWatchdog = 0; s_Watchdog.Stop (); }
	TCrashHeader *pRec = s_pRec;
	if (pRec == 0) return;
	pRec->nState = STATE_CLEAN;
	Clean (&pRec->nState, 4);
}

// ---- the report ---------------------------------------------------------------------------------
static const char *CrumbName (unsigned nIndex, u32 v)
{
	static const char *V3D[] = { "idle", "clipping", "binning", "rendering", "texture upload" };
	static const char *Present[] = { "idle", "composing", "display DMA", "waiting for the display DMA" };
	if (nIndex == CRUMB_V3D) return v < 5 ? V3D[v] : "?";
	if (nIndex == CRUMB_PRESENT) return v < 4 ? Present[v] : "?";
	return "";
}

static void Put (FIL &File, const char *p)
{
	UINT n; f_write (&File, p, strlen (p), &n);
}

void CrashLogReport (void)
{
	if (s_pPrev == 0) return;
	const TCrashHeader *pRec = (const TCrashHeader *) s_pPrev;
	const char *pLog = (const char *) s_pPrev + LOG_OFFSET;
	FIL File;
	if (f_open (&File, "SD:/etc/lastcrash.txt", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		delete [] s_pPrev; s_pPrev = 0;
		return;
	}
	CString s;
	s.Format ("Onyx crash record -- session #%u did not end with shutdown / reboot\r\n", pRec->nBoot);
	Put (File, s);
	if (pRec->nState == STATE_PANIC) { s.Format ("Ended by a kernel panic: %s\r\n", pRec->szPanic); Put (File, s); }
	else Put (File, "Ended without a panic: a hang (the watchdog restarted the Pi), or the power was cut.\r\n");
	s.Format ("Last sign of life of the scheduler (the reaper): %u.%02u s after boot; hang watchdog %u s\r\n",
		  pRec->nAliveTick / 100, pRec->nAliveTick % 100, pRec->nWatchdog);
	Put (File, s);
	s.Format ("GPU: %s, display: %s\r\n", CrumbName (CRUMB_V3D, pRec->Crumb[CRUMB_V3D]),
		  CrumbName (CRUMB_PRESENT, pRec->Crumb[CRUMB_PRESENT]));
	Put (File, s);
	Put (File, "\r\nThe primary core, last interrupted at (newest first; t = s after boot):\r\n");
	u32 nS = pRec->nSampleHead;
	for (unsigned k = 0; k < SAMPLES && k < nS; k++)
	{
		const TCrashSample &S = pRec->Sample[(nS - 1 - k) % SAMPLES];
		char szTask[SAMPLE_TASK + 1];
		memcpy (szTask, S.szTask, SAMPLE_TASK); szTask[SAMPLE_TASK] = 0;
		s.Format ("  t %u.%02u  pc %016lX  lr %016lX  sp %016lX  %s  %s%s\r\n", S.nTick / 100, S.nTick % 100,
			  (unsigned long) S.ulPC, (unsigned long) S.ulLR, (unsigned long) S.ulSP,
			  IS_USER_VA (S.ulPC) ? "app code " : "kernel   ", szTask,
			  (S.nSPSR & 0x80) ? "  (IRQs were masked)" : "");
		Put (File, s);
	}
	Put (File, "\r\nThe last kernel log lines:\r\n");
	u32 nHead = pRec->nLogHead, nLen = nHead < LOG_SIZE ? nHead : LOG_SIZE;
	u32 nStart = (nHead - nLen) % LOG_SIZE;
	UINT n;
	if (nStart + nLen <= LOG_SIZE) f_write (&File, pLog + nStart, nLen, &n);
	else { f_write (&File, pLog + nStart, LOG_SIZE - nStart, &n); f_write (&File, pLog, nLen - (LOG_SIZE - nStart), &n); }
	f_close (&File);
	CLogger::Get ()->Write ("crashlog", LogWarning, "the previous session (#%u) did not end cleanly (%s): see SD:/etc/lastcrash.txt",
				pRec->nBoot, pRec->nState == STATE_PANIC ? "a kernel panic" : "a hang or a power cut");
	delete [] s_pPrev; s_pPrev = 0;
}
