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
#include <circle/device.h>
#include <circle/devicenameservice.h>
#include <circle/multicore.h>
#include <circle/bcm2835.h>
#include <circle/memio.h>
#include <circle/actled.h>
#include <circle/bcmpropertytags.h>

#define CRASH_MAGIC	0x4F4E5843u		// "ONXC"
#define CRASH_VERSION	2
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
	u32	nDumpStep;			// core 1's report: 1 started, 2 text ready, 3 written, 4 the SD write failed
	u32	Crumb[CRUMB_COUNT];
	TCrashSample Sample[SAMPLES];
	char	szPanic[256];
};

#define LOG_OFFSET	((sizeof (TCrashHeader) + 63) & ~63ul)
#define LOG_SIZE	(ONYX_CRASH_AREA_SIZE - LOG_OFFSET)

static TCrashHeader *s_pRec = 0;		// the live record
static boolean s_bKept = FALSE;			// in the RAM kept out of the heap (else s_Fallback)
static u32 s_nPrevMagic = 0, s_nPrevState = 0;	// (what the kept RAM held at boot, for kmsg)
static u8 s_Fallback[ONYX_CRASH_AREA_SIZE] __attribute__ ((aligned (64)));
static char *s_pLog = 0;
static u8 *s_pPrev = 0;				// the previous session's record, when it did not end cleanly
static CBcmWatchdog s_Watchdog;
static unsigned s_nWatchdog = 0;
static unsigned s_nFeed = 0;

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
static u64 s_DumpLBA[DUMP_SECTORS];
static unsigned s_nDumpSectors = 0;
static CDevice *s_pDumpDev = 0;
static char s_DumpBuf[DUMP_SIZE] __attribute__ ((aligned (64)));
static volatile boolean s_bArmed = FALSE;
static volatile u64 s_ulAliveCnt = 0;			// CNTPCT at the reaper's last pass
volatile boolean g_bCrashDumping = FALSE;
static const char *volatile s_pReason = 0;		// a report asked for by core 0 (CrashLogRequest)		// (OnyxDriverWait: never yield then)

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
	TCrashHeader *pRec = (TCrashHeader *) (s_bKept ? (uintptr) g_ulOnyxCrashArea : (uintptr) s_Fallback);
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
	s_ulAliveCnt = Cntpct ();
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
	TCrashHeader *pRec = s_pRec;
	if (pRec == 0) return;
	pRec->nState = STATE_CLEAN;
	Clean (&pRec->nState, 4);
}

static const char *ThrottleText (u32 v, char *pBuf);

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

static void ReportFromRAM (void);
static void ArmDump (boolean *pbDumped);

void CrashLogReport (void)
{
	boolean bDumped = FALSE;
	ArmDump (&bDumped);			// (a report core 1 wrote: into lastcrash.txt first)
	if (bDumped) { if (s_pPrev != 0) { delete [] s_pPrev; s_pPrev = 0; } return; }
	ReportFromRAM ();
}

static void ReportFromRAM (void)
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
	static const char *Step[] = { "not started (core 1 did not see the hang, or was stuck too)", "started", "text ready, the SD write never ended",
				      "written", "the SD write failed" };
	s.Format ("Core 1's report into SD:/etc/crashdump.txt: %s\r\n", Step[pRec->nDumpStep < 5 ? pRec->nDumpStep : 0]);
	Put (File, s);
	{
		char Buf[128];
		s.Format ("Power: SoC %u C (max %u C this session), throttling: %s\r\n", pRec->Crumb[CRUMB_TEMP],
			  pRec->Crumb[CRUMB_TEMP_MAX], ThrottleText (pRec->Crumb[CRUMB_THROTTLED], Buf));
		Put (File, s);
	}
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

// ---- core 0, at boot: the previous dump kept, the file made ready for this session ---------------
static void ArmDump (boolean *pbDumped)
{
	*pbDumped = FALSE;
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
			if (f_open (&Out, "SD:/etc/lastcrash.txt", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
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

// ---- core 1: the text, without the heap -------------------------------------------------------------
static unsigned s_nOut;
static void Out (const char *p) { while (*p && s_nOut < DUMP_SIZE - 64) s_DumpBuf[s_nOut++] = *p++; }
static void OutN (const char *p, unsigned n) { for (unsigned i = 0; i < n && s_nOut < DUMP_SIZE - 64; i++) s_DumpBuf[s_nOut++] = p[i]; }
static void OutHex (u64 v, unsigned nDigits)
{
	char b[17]; for (int i = (int) nDigits - 1; i >= 0; i--) { b[i] = "0123456789ABCDEF"[v & 15]; v >>= 4; }
	b[nDigits] = 0; Out (b);
}
static void OutDec (u64 v)
{
	char b[24]; int i = 23; b[i] = 0;
	do { b[--i] = (char) ('0' + v % 10); v /= 10; } while (v != 0);
	Out (b + i);
}
static void OutTicks (u32 t) { OutDec (t / 100); Out ("."); OutDec ((t % 100) / 10); OutDec (t % 10); Out (" s"); }

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
	s_nOut = 0;
	Out (DUMP_MARK " -- session #"); OutDec (pRec->nBoot);
	if (s_pReason != 0) { Out (": "); Out (s_pReason); Out (" (the scheduler still ran); written by core 1, then the Pi restarted\r\n"); }
	else { Out (": core 0 stopped for "); OutDec (ulStalled); Out (" s; written by core 1, then the Pi restarted\r\n"); }
	if (pRec->nState == STATE_PANIC) { Out ("A kernel panic: "); Out (pRec->szPanic); Out ("\r\n"); }
	Out ("Last pass of the reaper (the scheduler alive): "); OutTicks (pRec->nAliveTick); Out (" after boot\r\n");
	{
		char Buf[128];
		Out ("Power: SoC "); OutDec (pRec->Crumb[CRUMB_TEMP]); Out (" C (max "); OutDec (pRec->Crumb[CRUMB_TEMP_MAX]);
		Out (" C this session), throttling: "); Out (ThrottleText (pRec->Crumb[CRUMB_THROTTLED], Buf)); Out ("\r\n");
	}
	Out ("GPU: "); Out (CrumbName (CRUMB_V3D, pRec->Crumb[CRUMB_V3D]));
	Out (", display: "); Out (CrumbName (CRUMB_PRESENT, pRec->Crumb[CRUMB_PRESENT])); Out ("\r\n");
	Out ("\r\nCore 0, last interrupted at (newest first; t = time after boot; IRQ timer ticks):\r\n");
	u32 nS = pRec->nSampleHead;
	for (unsigned k = 0; k < SAMPLES && k < nS; k++)
	{
		const TCrashSample &S = pRec->Sample[(nS - 1 - k) % SAMPLES];
		Out ("  t "); OutTicks (S.nTick);
		Out ("  pc "); OutHex (S.ulPC, 16); Out ("  lr "); OutHex (S.ulLR, 16); Out ("  sp "); OutHex (S.ulSP, 16);
		Out (IS_USER_VA (S.ulPC) ? "  app code  " : "  kernel    ");
		OutN (S.szTask, strnlen (S.szTask, SAMPLE_TASK));
		if (S.nSPSR & 0x80) Out ("  (IRQs were masked)");
		Out ("\r\n");
	}
	Out ("Now: "); OutTicks ((u32) CTimer::Get ()->GetTicks ()); Out (" (IRQ timer ticks; frozen too if core 0 no longer takes IRQs)\r\n");
	Out ("\r\nThe last kernel log lines:\r\n");
	u32 nHead = pRec->nLogHead, nLen = nHead < LOG_SIZE ? nHead : LOG_SIZE;
	u32 nRoom = DUMP_SIZE - 64 - s_nOut - 64;
	if (nLen > nRoom) nLen = nRoom;
	u32 nStart = (nHead - nLen) % LOG_SIZE;
	for (u32 i = 0; i < nLen; i++) s_DumpBuf[s_nOut++] = s_pLog[(nStart + i) % LOG_SIZE];
	OutN (DUMP_END, sizeof DUMP_END - 1);
	while (s_nOut < DUMP_SIZE) s_DumpBuf[s_nOut++] = ' ';
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

void CrashLogRequest (const char *pReason)
{
	if (!s_bArmed || s_nWatchdog == 0) return;
	CLogger::Get ()->Write ("crashlog", LogError, "%s: crash report, then restart", pReason);
	asm volatile ("msr daifset, #3" ::: "memory");	// (core 0 stays out of the way: no SD access)
	s_pReason = pReason;
	DataSyncBarrier ();
	for (;;) asm volatile ("wfe");			// core 1 writes the report and restarts the Pi
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
