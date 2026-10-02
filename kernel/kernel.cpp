//
// kernel.cpp
//
#include "kernel.h"
#include <kern/crashlog.h>
#include <circle/machineinfo.h>
#include <circle/memory.h>
#include <circle/sched/task.h>
#include <circle/synchronize.h>
#include <circle/string.h>
#include <circle/util.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/input/keymap.h>		// CKeyMap, PHY_MAX_CODE, K_CTRLTAB (SetKeyMapData)
#include <circle/input/mouse.h>
#include <circle/usb/usbgamepad.h>
#include <circle/usb/usbmidi.h>		// USB MIDI input (ABI v68)
#include <kern/kapi_abi.h>		// struct kapi_pad (KernelPadState)
#include <kern/trapframe.h>
#include <kern/addrspace.h>
#include <kern/thread.h>		// ThreadsEndProcess (an app ends with its threads)
#include <kern/appcore.h>
#include <kern/el0.h>			// apps at EL0: El0Enter, the stacks, the per-core setup
#include <fatfs/diskio.h>		// disk_cache_enable (the sector cache: sdcache=)
#include <kern/applaunch.h>
#include <kern/stream.h>
#include <kern/handle.h>		// ProcessRelease, HandlesRunDeferred (kern/handle.h)
#include <kern/kapitable.h>
#include <kern/layout.h>
#include <kern/elf.h>
#include <kern/image.h>		// (v77) program images: the streaming loader, the shared image, preload
#include <kern/gui/gimage.h>
#include <kern/net.h>
#include <kern/ipc.h>		// IpcNotify ("Network up")
#include <kern/sound.h>		// SoundCoreMain (core 1)
#include <kern/ramfs.h>		// RAM:, the RAM volume (system.ini ramfs=)
#include <kern/procx.h>		// (v75) a process's argv / environment blocks
#include <kern/ofile.h>		// (v75) OFileBootCleanup
#ifdef ARM_ALLOW_MULTI_CORE
#include <circle/multicore.h>
#endif
#include <circle/net/ipaddress.h>
#include <circle/net/ntpdaemon.h>

static const char FromKernel[] = "kernel";

// WLAN firmware (brcmfmac43455-sdio.*) lives here; the user's SSID/PSK go in the
// config file. Both must be staged on the SD card -- see docs/NETWORK.md.
#define WLAN_FIRMWARE_PATH	"SD:/firmware/"
#define WLAN_CONFIG_FILE	"SD:/etc/wpa_supplicant.conf"

// Network globals (declared in kern/net.h). g_pNet is published once the kernel
// object exists; g_bNetUp flips TRUE when the link is DHCP-bound.
CNetSubSystem	 *g_pNet   = 0;
volatile boolean  g_bNetUp = FALSE;

// Clock: timezone offset from UTC in minutes (system.ini "timezone="; default CET
// +60, set "120" for CEST summer time) + the NTP server to sync against once the
// link is up (system.ini "ntp="; "off" or "none": no sync). The NTP daemon updates CTimer's
// wall clock. The name DHCP announces: system.ini "hostname=" (default Circle's "raspberrypi").
static int  g_nTimeZoneMin = 60;
static unsigned g_nHeartbeatSec = 5;	// cmdline.txt heartbeat= (0 = off): watchdog summary period
static char g_szNtpServer[64] = "pool.ntp.org";
static char g_szHostname[64] = "";
// The RAM volume's size (system.ini "ramfs=": MB, "N%" of the free page memory, 0 = none;
// empty: 128 MB, at most a quarter of that memory). kern/ramfs.h.
static char g_szRamFs[16] = "";

// Defined in arch/aarch64/exception.cpp: route kernel panics to this displayed
// framebuffer so an exception is visible after the compositor takes the screen.
void SetPanicGraphics (C2DGraphics *p2D);
C2DGraphics *g_pGraphics = 0;		// the displayed framebuffer (kapi_present_fb)

// Verbose logging flag: when on, the kernel logs app lifecycle events (spawn / exit
// / orphan kill). Set at boot from SD:system.ini (verbose=1) and toggled at runtime
// via kapi_set_verbose / the `verbose` command. VLOG(...) is a gated CLogger::Write.
boolean g_bVerbose = FALSE;
#define VLOG(...)	do { if (g_bVerbose) CLogger::Get ()->Write (__VA_ARGS__); } while (0)

//
// An app's user stack: mapped below USER_STACK_TOP in its address space -- 1 MB, or what its
// folder's app.txt asks for, "stack = 8M" (a number of bytes, K or M), rounded up to 64 KB, at
// most 64 MB: NetSurf's JavaScript engine recurses deep. Its task's own (CTask) stack is only its
// kernel stack (EL0_KSTACK_SIZE: the trap frames, the kapis it calls). Read by the process's own
// task, before the ELF: one small file, only for an x.app/main. (A "mode" line, from the time an
// app could run at EL1, is ignored: every process runs at EL0.)
//
static unsigned AppUserStack (const char *pPath)
{
	if (pPath == 0)
		return EL0_USTACK_MIN;

	// "...<name>.app/main[.ext]": the folder's app.txt
	unsigned nLen = 0, nSlash = 0;
	boolean bSlash = FALSE;
	while (pPath[nLen] != '\0')
	{
		if (pPath[nLen] == '/') { nSlash = nLen; bSlash = TRUE; }
		nLen++;
	}
	if (!bSlash || nSlash < 4 || nSlash + 1 + 9 > 256
	    || pPath[nSlash - 4] != '.' || pPath[nSlash - 3] != 'a'
	    || pPath[nSlash - 2] != 'p' || pPath[nSlash - 1] != 'p')
		return EL0_USTACK_MIN;
	char Txt[256];
	unsigned i;
	for (i = 0; i <= nSlash; i++) Txt[i] = pPath[i];
	const char *pName = "app.txt";
	for (unsigned k = 0; pName[k] != '\0'; k++) Txt[i++] = pName[k];
	Txt[i] = '\0';

	FIL File;
	if (f_open (&File, Txt, FA_READ) != FR_OK)
		return EL0_USTACK_MIN;
	char Buf[1024];
	UINT nRead = 0;
	if (f_read (&File, Buf, sizeof Buf - 1, &nRead) != FR_OK)
		nRead = 0;
	f_close (&File);
	Buf[nRead] = '\0';

	// a line "stack = <n>[K|M]" (spaces, '=' or ':'; the key in any case)
	auto lower = [] (char c) { return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c; };
	auto key = [&] (const char *q, const char *k)	// q starts with the key k, then a separator
	{
		unsigned n = 0;
		for (; k[n] != '\0'; n++) if (lower (q[n]) != k[n]) return 0u;
		return q[n] == ' ' || q[n] == '\t' || q[n] == '=' || q[n] == ':' ? n : 0u;
	};
	u64 nStack = 0;
	for (const char *p = Buf; *p != '\0'; )
	{
		while (*p == ' ' || *p == '\t') p++;
		unsigned n;
		if ((n = key (p, "stack")) != 0)
		{
			p += n;
			while (*p == ' ' || *p == '\t' || *p == '=' || *p == ':') p++;
			u64 v = 0;
			while (*p >= '0' && *p <= '9' && v < EL0_USTACK_MAX)
				v = v * 10 + (u64) (*p++ - '0');
			if (*p == 'k' || *p == 'K') v <<= 10;
			else if (*p == 'm' || *p == 'M') v <<= 20;
			nStack = v;
		}
		while (*p != '\0' && *p != '\n') p++;	// next line
		if (*p == '\n') p++;
	}
	if (nStack <= EL0_USTACK_MIN)
		return EL0_USTACK_MIN;
	if (nStack > EL0_USTACK_MAX)
		nStack = EL0_USTACK_MAX;
	return (unsigned) ((nStack + 0xFFFF) & ~(u64) 0xFFFF);
}

//
// (v77) A program's image (kern/image.h) from its file. The image's source over a FatFs file:
// read where the loader asks (it reads forward: the headers, then each segment).
//
static int ProgramFileRead (void *pCtx, u64 ulOffset, void *pBuffer, unsigned nBytes)
{
	FIL *pFile = (FIL *) pCtx;
	if (f_tell (pFile) != ulOffset && f_lseek (pFile, ulOffset) != FR_OK) return -1;
	UINT nRead = 0;
	if (f_read (pFile, pBuffer, nBytes, &nRead) != FR_OK) return -1;
	return (int) nRead;
}

// The image of the program at pPath, a reference taken (ImageRelease). In memory already -- a
// process of that path runs, or it is preloaded -- : nothing is read from the card, not even its
// directory. Else the file is opened and streamed into a new image (the calling task reads,
// yielding). nFlags: IMG_OPEN_PIN (a preload). -> 0 / -KAPI_E* (*ppWhy: for the log).
static int ProgramImage (const char *pPath, unsigned nFlags, TImage **ppImage, unsigned *pHow,
			 const char **ppWhy)
{
	int nErr = ImageOpen (pPath, 0, 0, nFlags, ppImage, pHow, ppWhy);
	if (nErr != -KAPI_ENOENT) return nErr;		// (found, or its load by another task failed)

	FIL File;
	if (f_open (&File, pPath, FA_READ) != FR_OK)
	{
		*ppWhy = "cannot open the file";
		return -KAPI_ENOENT;
	}
	TImgSource Src = { ProgramFileRead, &File, (u64) f_size (&File) };
	// (the open yielded: another task may have made the image meanwhile -- then that one)
	nErr = ImageOpen (pPath, 0, &Src, nFlags, ppImage, pHow, ppWhy);
	f_close (&File);
	return nErr;
}

// Is there a program at pPath: in memory (no card access), else on the card.
static boolean SdFileExists (const char *pPath);
static boolean ProgramExists (const char *pPath)
{
	return ImageList (pPath, 0, 0, 0) != 0 || SdFileExists (pPath);
}

//
// A preload (kapi image_preload, /bin/preload): a kernel task loads the program's image and pins
// it; the caller returned at once. A process started meanwhile waits for this load and shares it.
//
class CPreloadTask : public CTask
{
public:
	CPreloadTask (const char *pPath)
	:	CTask (EL0_KSTACK_SIZE)		// (the stack a process's own load runs on)
	{
		SetName ("preload");
		unsigned n = 0;
		for (; pPath[n] != '\0' && n < sizeof (m_Path) - 1; n++) m_Path[n] = pPath[n];
		m_Path[n] = '\0';
	}

	void Run (void) override
	{
		unsigned nT0 = CTimer::GetClockTicks ();
		TImage *pImage = 0;
		unsigned nHow = 0;
		const char *pWhy = "";
		if (ProgramImage (m_Path, IMG_OPEN_PIN, &pImage, &nHow, &pWhy) < 0)
		{
			CLogger::Get ()->Write ("preload", LogError, "cannot load %s: %s", m_Path, pWhy);
			return;
		}
		u64 nShared = 0, nPrivate = 0;
		ImageSizes (pImage, &nShared, &nPrivate);
		CLogger::Get ()->Write ("preload", LogNotice, "image %s: %s in %u ms, kept (%u KB shared)", m_Path,
					nHow == IMG_HOW_LOADED ? "loaded" : "in memory already",
					(CTimer::GetClockTicks () - nT0) / 1000, (unsigned) (nShared >> 10));
		ImageRelease (pImage);		// (pinned: it stays)
	}

private:
	char m_Path[IMG_PATH_MAX];
};

int ProgramPreload (const char *pCanonPath)		// (kern/applaunch.h: kapi_image_preload)
{
	struct kapi_image_info Info;
	if (ImageList (pCanonPath, 0, &Info, 1) != 0 && (Info.flags & KAPI_IMG_KEPT)) return 0;	// (kept already)
	if (!ProgramExists (pCanonPath)) return -KAPI_ENOENT;
	return new CPreloadTask (pCanonPath) != 0 ? 0 : -KAPI_ENOMEM;
}

//
// CUserProcessTask: one process = one task (plus its threads, kern/thread.h). The task builds the
// process's address space, loads the ELF into it, maps the user stack, then enters the entry
// point at EL0 (kern/el0.h) -- for good: the process calls the kernel by system calls through
// the EL0 kapi table, and ends in the kernel (exit, a fault, a kill) on this task's stack, which
// is its kernel stack from then on. Processes are isolated from each other (their own page
// tables, ASIDs) and from the kernel (EL0). Preempted at the end of its time slice.
//
class CUserProcessTask : public CTask
{
public:
	// The task's stack is the process's kernel stack (EL0_KSTACK_SIZE).
	// We take the ELF PATH (not a preloaded buffer): the SD read happens in Run(), on
	// THIS task's thread the first time the scheduler switches to us -- so the launcher /
	// shell that spawned us is never blocked by the (slow, ~MB) read.
	// pStdin/pStdout/pProcess/pArgs are for spawned processes (default 0 for plain
	// launches): the streams + spawn handle are installed into the address space so
	// the app's stdio + exit status work; pArgs becomes kapi_get_args.
	CUserProcessTask (const char *pPath, const char *pName, CLogger *pLogger,
			  CStream *pStdin = 0, CStream *pStdout = 0, CProcess *pProcess = 0,
			  const char *pArgs = 0, const char *pCwd = 0, unsigned nParentPid = 0,
			  TProcInfo *pInfo = 0)
	:	CTask (EL0_KSTACK_SIZE),	// the kernel stack (the user stack: Run)
		m_pLogger (pLogger),
		m_pStdin (pStdin), m_pStdout (pStdout), m_pProcess (pProcess),
		m_nParentPid (nParentPid),
		m_pInfo (pInfo)			// (v75) its argv / environment (kern/procx.h), 0: none
	{
		SetName (pName);	// copies into CTask::m_Name (caller's may be transient)
		unsigned p = 0;		// copy the path (the caller's string may be transient)
		if (pPath != 0)
			for (; pPath[p] != '\0' && p < sizeof (m_Path) - 1; p++) m_Path[p] = pPath[p];
		m_Path[p] = '\0';
		unsigned i = 0;
		if (pArgs != 0)
			for (; pArgs[i] != '\0' && i < sizeof (m_Args) - 1; i++) m_Args[i] = pArgs[i];
		m_Args[i] = '\0';
		unsigned k = 0;		// inherited working directory (empty => child defaults to root)
		if (pCwd != 0)
			for (; pCwd[k] != '\0' && k < sizeof (m_Cwd) - 1; k++) m_Cwd[k] = pCwd[k];
		m_Cwd[k] = '\0';
	}

	void Run (void) override
	{
		CAddressSpace *pAS = new CAddressSpace;
		if (pAS == 0 || !pAS->IsValid ())
		{
			m_pLogger->Write (GetName (), LogError, "address space creation failed");
			// The AS never took the streams here, so release the caller's refs ourselves.
			if (m_pStdin  != 0) m_pStdin->Release ();
			if (m_pStdout != 0) m_pStdout->Release ();
			if (m_pProcess != 0) { m_pProcess->nStatus = -1; m_pProcess->bDone = TRUE; ProcessRelease (m_pProcess); }
			ProcInfoFree (m_pInfo);
			delete pAS;
			return;
		}

		// Install stdio + spawn handle + argv before the app runs (the AS owns the
		// stream refs from here, and releases them / marks the process on teardown -- so
		// every failure path below just `delete pAS`).
		pAS->SetStdin (m_pStdin);
		pAS->SetStdout (m_pStdout);
		pAS->SetProcess (m_pProcess);
		pAS->SetArgs (m_Args);
		if (m_Cwd[0] != '\0') pAS->SetCwd (m_Cwd);	// else keep the default root
		pAS->SetParentPid (m_nParentPid);		// 0 = no parent (drawer launch)
		ProcInfoInstall (pAS, m_pInfo);			// (v75: its own now; its record learns its pid)
		m_pInfo = 0;

		// Its program's image (v77, kern/image.h) HERE -- on our own thread, deferred to our first
		// schedule ("when we are switched to") -- so the launcher/shell wasn't blocked by the
		// read. In memory already (another process of the program runs, or it is preloaded): the
		// card is not touched. Else the file is streamed into a new image, in chunks, yielding
		// between them, so the compositor + the rest of the UI keep running while we load (the SD
		// read is the slow part; we're single-core); a start meanwhile waits for this load.
		unsigned nT0 = CTimer::GetClockTicks ();
		TImage *pImage = 0;
		unsigned nHow = 0;
		const char *pWhy = "";
		int nErr = ProgramImage (m_Path, 0, &pImage, &nHow, &pWhy);
		if (nErr < 0)
		{
			m_pLogger->Write (GetName (), LogError, "cannot load %s: %s", m_Path, pWhy);
			delete pAS;
			return;
		}
		unsigned nT1 = CTimer::GetClockTicks ();

		// Mapped: the image's read-only pages (shared, not owned), our own writable ones.
		u64 ulEntry = 0;
		boolean bLoaded = ImageMap (pImage, pAS, &ulEntry);
		if (!bLoaded)
		{
			ImageRelease (pImage);			// (ours; pAS drops its own)
			m_pLogger->Write (GetName (), LogError, "out of memory mapping %s", m_Path);
			delete pAS;
			return;
		}
		// Code was written via the identity mapping: make it executable at the user VA.
		SyncDataAndInstructionCache ();
		unsigned nT2 = CTimer::GetClockTicks ();

		// The user stack's size: app.txt's, read once per image (a start from an image in memory
		// reads nothing; a changed app.txt takes the image's name away: ImageFileChanged).
		unsigned nUserStack = ImageStack (pImage);
		if (nUserStack == 0)
		{
			nUserStack = AppUserStack (m_Path);
			ImageSetStack (pImage, nUserStack);
		}

		// One line per start (the plan's "measure first"; `kmsg` shows it): where the time went.
		{
			u64 nShared = 0, nPrivate = 0;
			ImageSizes (pImage, &nShared, &nPrivate);
			m_pLogger->Write (GetName (), LogNotice,
					  "image %s: %s in %u ms, mapped in %u ms (%u KB shared, %u KB private)",
					  m_Path,
					  nHow == IMG_HOW_LOADED ? "loaded" : nHow == IMG_HOW_WAITED ? "shared after a wait" : "shared",
					  (nT1 - nT0) / 1000, (nT2 - nT1) / 1000,
					  (unsigned) (nShared >> 10), (unsigned) (nPrivate >> 10));
		}
		ImageRelease (pImage);				// (ours: pAS holds the image from now on)

		// Become this address space (kernel stays mapped + EL1-accessible).
		SetUserData (pAS, TASK_USER_DATA_USER);
		pAS->AddTask (this);				// (its main task: the first)
		pAS->Activate ();

		// Its user stack in its own space, then EL0 -- for good: the process ends in the
		// kernel (exit, a fault, a kill), on this task's stack, now only its kernel stack.
		if (!pAS->MapStack (USER_STACK_TOP, nUserStack))
		{
			m_pLogger->Write (GetName (), LogError, "out of memory for the stack");
			ThreadsEndProcess ();
			CScheduler::Get ()->GetCurrentTask ()->Terminate ();
		}
		m_pLogger->Write (GetName (), LogNotice,
				  "running entry %lp ASID %u, stack %u KB, kernel stack %u KB",
				  (void *) ulEntry, (unsigned) pAS->GetASID (), nUserStack >> 10,
				  (unsigned) (GetStack ().Size >> 10));
		El0Enter (ulEntry, USER_STACK_TOP, 0, El0MainReturnVA ());
	}

private:
	char	    m_Path[256];	// ELF path; loaded in Run() on our own thread
	CLogger	   *m_pLogger;
	CStream	   *m_pStdin;
	CStream	   *m_pStdout;
	CProcess   *m_pProcess;
	char	    m_Args[1024];
	char	    m_Cwd[256];
	unsigned    m_nParentPid;
	TProcInfo  *m_pInfo;
};

//
// Compositor (#10): a kernel thread that composites all windows onto the screen
// and presents at ~60 fps. It is the single owner of UpdateDisplay(); window
// owners only draw into their own canvas.
//
boolean g_bDisplayDma = TRUE;			// cmdline.txt dispdma=0: no asynchronous display DMA
extern boolean g_bGpuDirect;			// (sys/v3d.cpp) gpudirect=0: the GPU never writes the window itself

class CCompositorTask : public CTask
{
	// A present: the rectangle (w = 0: the screen) sent by the frame buffer's 2D DMA, read in
	// place in the off-screen buffer; meanwhile the other tasks run (core 0 not held by a busy
	// wait), and nothing draws into that buffer until it is done (only this task does).
	void Present (unsigned x, unsigned y, unsigned w, unsigned h)
	{
		if (!g_bDisplayDma)				// (cmdline.txt dispdma=0: the synchronous copy, as before)
		{
			CrashLogCrumb (CRUMB_PRESENT, 2);
			if (w == 0) m_p2D->UpdateDisplay (); else m_p2D->UpdateDisplay (x, y, w, h);
			CrashLogCrumb (CRUMB_PRESENT, 0);
			return;
		}
		// the DMA started, then polled between yields -- not its completion interrupt: under a
		// heavy GPU load (Ocarina of Time, n64emu) one got lost now and then, and the compositor
		// waited for ever (the screen, the apps presenting, the sound feeder behind them: all stuck)
		CrashLogCrumb (CRUMB_PRESENT, 2);
		if (m_p2D->UpdateDisplayStart (x, y, w, h))
		{
			CrashLogCrumb (CRUMB_PRESENT, 3);
			while (!m_p2D->UpdateDisplayPoll ()) CScheduler::Get ()->Yield ();
		}
		CrashLogCrumb (CRUMB_PRESENT, 0);
	}

public:
	CCompositorTask (C2DGraphics *p2D, CWindowManager *pWM)
	:	m_p2D (p2D), m_pWM (pWM), m_bFirst (TRUE)
	{
		SetName ("compositor");
	}

	// A new screen size asked for (ScreenResizeRequest): the frame buffer made again at that size
	// (the display DMA is idle: each Present waits for its end), the old one back if the firmware
	// refuses it; then the window manager told (windows kept on the screen, the apps' event).
	void Resize (int &nW, int &nH)
	{
		int w = s_nResizeW, h = s_nResizeH, nResult = 0;
		if (w != nW || h != nH)
		{
			CLogger::Get ()->Write ("screen", LogNotice, "resolution %dx%d -> %dx%d", nW, nH, w, h);
			if (!m_p2D->Resize ((unsigned) w, (unsigned) h))
			{
				nResult = -3;
				if (!m_p2D->Resize ((unsigned) nW, (unsigned) nH))
					CLogger::Get ()->Write ("screen", LogError, "the old resolution could not be set again");
			}
			nW = (int) m_p2D->GetWidth ();
			nH = (int) m_p2D->GetHeight ();
			g_nScreenWidth = nW; g_nScreenHeight = nH;
			if (nResult != 0)
				CLogger::Get ()->Write ("screen", LogWarning, "the firmware refused %dx%d: %dx%d", w, h, nW, nH);
			m_pWM->OnScreenResized (nW, nH);
			m_bFirst = TRUE;			// (the whole screen at the next frame)
		}
		s_nResizeResult = nResult;
		DataMemBarrier ();
		s_nResizeDone = s_nResizeSeq;
	}

public:
	static volatile int s_nResizeW, s_nResizeH, s_nResizeResult;
	static volatile unsigned s_nResizeSeq, s_nResizeDone;

	void Run (void) override
	{
		int nW = (int) m_p2D->GetWidth ();
		int nH = (int) m_p2D->GetHeight ();
		unsigned nLastGen = g_nScreenGen - 1, nLastTicks = 0;	// (first frame: always)
		for (;;)
		{
			if (DebugConsoleActive ())
			{
				// An app exited: the debug console owns the display now. Stop
				// presenting so we don't fight it for the framebuffer.
				CScheduler::Get ()->MsSleep (100);
				continue;
			}
			if (m_pWM->FullscreenWindow () != 0)
			{
				// A full-screen app owns the display (kapi_present_fb): pause.
				CScheduler::Get ()->MsSleep (16);
				continue;
			}
			if (s_nResizeSeq != s_nResizeDone) Resize (nW, nH);
			// Recomposite only when something changed (g_nScreenGen), and only the damaged
			// rectangles (ScreenDirtyRect): each is redrawn with the screen clipped to it and
			// sent to the display alone. The whole screen when ScreenDirty said so, plus a
			// safety refresh every 2 s (a missed damage source, the watchdog's frame count).
			unsigned nGen = g_nScreenGen, nTicks = CTimer::Get ()->GetTicks ();
			boolean bSafety = nTicks - nLastTicks >= 2 * HZ;
			if (nGen != nLastGen || bSafety)
			{
				nLastGen = nGen;
				TScreenDamage Damage;
				ScreenTakeDamage (&Damage);
				GImage Screen ((u32 *) m_p2D->GetBuffer (), nW, nH);
				if (Damage.bFull || bSafety || m_bFirst)
				{
					nLastTicks = nTicks; m_bFirst = FALSE;
					m_pWM->Composite (&Screen);
					Present (0, 0, 0, 0);
				}
				else
				{
					for (int i = 0; i < Damage.n; i++)
					{
						Screen.SetClip (Damage.x0[i], Damage.y0[i], Damage.x1[i], Damage.y1[i]);
						m_pWM->Composite (&Screen, i == 0);
						Present ((unsigned) Damage.x0[i], (unsigned) Damage.y0[i],
							 (unsigned) (Damage.x1[i] - Damage.x0[i]),
							 (unsigned) (Damage.y1[i] - Damage.y0[i]));
					}
				}
			}
			CScheduler::Get ()->MsSleep (16);
		}
	}

private:
	C2DGraphics    *m_p2D;
	CWindowManager *m_pWM;
	boolean		m_bFirst;
};

volatile int CCompositorTask::s_nResizeW = 0, CCompositorTask::s_nResizeH = 0, CCompositorTask::s_nResizeResult = 0;
volatile unsigned CCompositorTask::s_nResizeSeq = 0, CCompositorTask::s_nResizeDone = 0;
static boolean s_bCompositor = FALSE;		// (the compositor runs: a resize can be asked for)

int ScreenResizeRequest (int nW, int nH)
{
	if (nW < SCREEN_MIN_W || nH < SCREEN_MIN_H || nW > SCREEN_MAX_W || nH > SCREEN_MAX_H || (nW & 1) != 0)
		return -1;
	CWindowManager *pWM = CWindowManager::Get ();
	if (!s_bCompositor || pWM == 0 || pWM->FullscreenWindow () != 0 || DebugConsoleActive ())
		return -2;
	static volatile boolean s_bBusy = FALSE;	// (one at a time)
	if (s_bBusy) return -2;
	s_bBusy = TRUE;
	CCompositorTask::s_nResizeW = nW; CCompositorTask::s_nResizeH = nH;
	DataMemBarrier ();
	unsigned nSeq = ++CCompositorTask::s_nResizeSeq;
	int nResult = -2;
	for (unsigned t = 0; t < 300; t++)		// (3 s: a full-screen app started meanwhile...)
	{
		if (CCompositorTask::s_nResizeDone == nSeq) { nResult = CCompositorTask::s_nResizeResult; break; }
		CScheduler::Get ()->MsSleep (10);
	}
	if (nResult == -2) CCompositorTask::s_nResizeDone = nSeq;	// (given up: not done later)
	s_bBusy = FALSE;
	return nResult;
}


// Cascade kill: when a process dies, its still-running children must die too (e.g.
// killing the terminal also kills its shell + whatever the shell spawned). We track
// each app's parent pid; here we terminate any app whose parent pid is no longer a
// live task. Run from the reaper (a normal task context), so over a few passes a
// dead parent's whole subtree is torn down. Parent pid 0 = no parent (never orphaned).
// (No table of tasks -- the scheduler has no limit: one orphan per pass, each pass a scan of
// every app for each app, which is cheap at this size.)
struct OrphanScan { unsigned nPid; boolean bFound; CTask *pOrphan; unsigned nParent; };
static boolean PidAliveCb (CTask *pTask, const char *, TTaskState State, TTaskFlags, void *pParam)
{
	OrphanScan *s = (OrphanScan *) pParam;
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	if (State != TaskStateTerminated && pAS != 0 && pAS->GetPid () == s->nPid) s->bFound = TRUE;
	return !s->bFound;
}
static boolean OrphanCollect (CTask *pTask, const char *, TTaskState State,
			      TTaskFlags, void *pParam)
{
	if (State == TaskStateTerminated) return TRUE;
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	if (pAS == 0 || pAS->GetParentPid () == 0) return TRUE;	// kernel task / no parent
	OrphanScan Parent = { pAS->GetParentPid (), FALSE, 0, 0 };
	CScheduler::Get ()->EnumerateTasks (PidAliveCb, &Parent);
	if (Parent.bFound) return TRUE;
	OrphanScan *s = (OrphanScan *) pParam;
	s->pOrphan = pTask; s->nParent = Parent.nPid;
	return FALSE;						// (one per pass)
}
static void TerminateOrphans (void)
{
	if (!CScheduler::IsActive ()) return;
	for (unsigned n = 0; n < 64; n++)			// (the rest at the next pass)
	{
		OrphanScan s = { 0, FALSE, 0, 0 };
		CScheduler::Get ()->EnumerateTasks (OrphanCollect, &s);
		if (s.pOrphan == 0) return;
		VLOG ("proc", LogNotice, "orphan %s (parent pid %u gone) terminated",
		      s.pOrphan->GetName (), s.nParent);
		CAddressSpace *pAS = (CAddressSpace *) s.pOrphan->GetUserData (TASK_USER_DATA_USER);
		if (pAS != 0) pAS->SetTermReason (KAPI_PROC_KILLED, -9);	// (v75: proc_wait)
		CScheduler::Get ()->TerminateTask (s.pOrphan);	// (its whole process: terminated now)
	}
}

//
// Reaper: a dedicated kernel thread that reclaims tasks which have ended (closed
// apps) -- freeing their address space, window, and the CTask + stack. Kept as a
// proper scheduler task (like the compositor), separate from CKernel::Run (the
// special "main" task), so the teardown always runs in a normal task context.
//
// Stall watchdog reports (see CScheduler::StallSample) -> the kernel log (kmsg). The PCs
// are kernel addresses (kernel8-rpi4.map / the ELF) or, in an app, its user-VA code.
static void LogStalls (void)
{
	TStallReport R; unsigned nLost = 0;
	static unsigned s_nLostLogged = 0;
	while (CScheduler::Get ()->TakeStallReport (&R, &nLost))
	{
		CString Where;
		for (unsigned i = 0; i < R.nSamples; i++)
		{
			CString One;
			One.Format (" %lx/%lx", (unsigned long) R.PC[i], (unsigned long) R.LR[i]);
			Where.Append (One);
		}
		CLogger::Get ()->Write ("stall", LogWarning, "%s ran %u ms without yielding; pc/lr:%s",
					R.Name, R.nMs, R.nSamples ? (const char *) Where : " (IRQs masked)");
	}
	if (nLost != s_nLostLogged)
	{
		CLogger::Get ()->Write ("stall", LogWarning, "%u stall reports lost", nLost - s_nLostLogged);
		s_nLostLogged = nLost;
	}
}

class CReaperTask : public CTask
{
public:
	CReaperTask (void)
	{
		SetName ("reaper");
	}

	// Also drives the green ACT LED as a headless sign of life (no screen needed):
	// slow blink (1 s) = kernel alive, network not up yet; fast blink (0.2 s) = network
	// up; LED frozen = the kernel no longer schedules (hang). SD accesses flash it too.
	void Run (void) override
	{
		unsigned nTick = 0;
		boolean bOn = FALSE;
		for (;;)
		{
			TerminateOrphans ();			// kill children of dead parents
			CScheduler::Get ()->ReapTerminatedTasks ();
			HandlesRunDeferred ();			// the streams those teardowns left (kern/handle.h)
			LogStalls ();
			CrashLogAlive ();			// (the crash record's uptime + the hang watchdog)

			unsigned nPeriod = NetIsUp () ? 4 : 20;	// x 50 ms
			if (++nTick >= nPeriod)
			{
				nTick = 0;
				bOn = !bOn;
				if (bOn) CActLED::Get ()->On (); else CActLED::Get ()->Off ();
			}
			CScheduler::Get ()->MsSleep (50);
		}
	}
};

//
// GUI watchdog + heartbeat: a kernel thread that checks once a second that the GUI is
// alive and logs to the kernel log (read it remotely with `kmsg`, e.g. over telnetd):
//   * a heartbeat every g_nHeartbeatSec seconds (cmdline.txt heartbeat=, 0 = off):
//     uptime, frames/s, mouse/key events, tasks by state, and each window with its
//     queued / dropped events;
//   * a WARNING when the compositor has produced no frame for 2 s (+ the state of every
//     task, to see who holds the CPU or what the compositor waits on), and when an app
//     stops pumping its window's events for 2 s while some are queued (a frozen app);
//   * the app watchdog: that frozen app watched (where its task is, the return addresses on
//     its stack) and a report rewritten every 2 s into SD:/etc/apphang.txt -- the compositor
//     stalled too -- kept in SD:/etc/lastcrash.txt if the Pi restarts meanwhile (crashlog.h).
// Each warning fires once per episode, with a matching "recovered" line.
//
struct TaskStateScan
{
	unsigned nByState[6];			// TTaskState counts (new ready block blockT sleep term)
	char    *pBuf; unsigned nCap, nLen;	// optional "name:state " list
};
static boolean TaskStateCollect (CTask *pTask, const char *pName, TTaskState State,
				 TTaskFlags Flags, void *pParam)
{
	static const char *Names[] = { "new", "ready", "block", "blockT", "sleep", "term" };
	TaskStateScan *s = (TaskStateScan *) pParam;
	unsigned st = (unsigned) State < 6 ? (unsigned) State : 5;
	s->nByState[st]++;
	if (s->pBuf != 0)
	{
		const char *pSt = (Flags == TaskFlagRunning) ? "RUN" : Names[st];
		for (const char *p = pName; *p && s->nLen + 1 < s->nCap; p++) s->pBuf[s->nLen++] = *p;
		if (s->nLen + 1 < s->nCap) s->pBuf[s->nLen++] = ':';
		for (const char *p = pSt; *p && s->nLen + 1 < s->nCap; p++) s->pBuf[s->nLen++] = *p;
		if (s->nLen + 1 < s->nCap) s->pBuf[s->nLen++] = ' ';
		s->pBuf[s->nLen] = '\0';
	}
	return TRUE;
}

class CGuiWatchdogTask : public CTask
{
public:
	CGuiWatchdogTask (CWindowManager *pWM) : m_pWM (pWM) { SetName ("watchdog"); }

	void Run (void) override
	{
		static const char From[] = "gui";
		unsigned nLastFrames = m_pWM->FrameCount (), nStallSec = 0;
		unsigned nBeatFrames = nLastFrames, nBeatMouse = m_pWM->MouseCount ();
		unsigned nBeatKeys = m_pWM->KeyCount (), nSec = 0;
		boolean bStalled = FALSE;
		CWindow *pWatched = 0;			// (the app watchdog's: CrashLogWatchPid)
		boolean bFrozen[WM_MAX_WINDOWS];
		CWindow *pFrozenWin[WM_MAX_WINDOWS];
		for (unsigned i = 0; i < WM_MAX_WINDOWS; i++) { bFrozen[i] = FALSE; pFrozenWin[i] = 0; }
		static char Tasks[512];

		for (;;)
		{
			CScheduler::Get ()->MsSleep (1000);
			nSec++;
			unsigned nNow = CTimer::Get ()->GetTicks ();

			CrashLogPower ();			// (under-voltage / heat: kmsg + the crash record)
			CrashLogMemory ();			// (the free memory: the crash record)
			if (nSec % 600 == 60) CrashLogClockSave ();	// (SD:/etc/clock: the time at the next boot)

			// 1. Compositor liveness.
			unsigned nFrames = m_pWM->FrameCount ();
			nStallSec = (nFrames == nLastFrames) ? nStallSec + 1 : 0;
			nLastFrames = nFrames;
			if (nStallSec >= 2 && !bStalled)
			{
				bStalled = TRUE;
				TaskStateScan s = {{0}, Tasks, sizeof Tasks, 0}; Tasks[0] = '\0';
				CScheduler::Get ()->EnumerateTasks (TaskStateCollect, &s);
				CLogger::Get ()->Write (From, LogWarning,
					"compositor STALLED: no frame for %u s; tasks: %s", nStallSec, Tasks);
			}
			if (bStalled && nStallSec % 2 == 0 && nStallSec < 12 && !DebugConsoleActive ())
			{
				CrashLogAppHang ("the compositor produced no frame", 0, nStallSec, 0);	// (SD:/etc/apphang.txt)
			}
			if (nStallSec == 12 && !DebugConsoleActive ())	// (its tasks' states logged at 2 s)
			{
				CrashLogRequest ("the compositor produced no frame for 12 s");
			}
			else if (nStallSec == 0 && bStalled)
			{
				bStalled = FALSE;
				CLogger::Get ()->Write (From, LogWarning, "compositor recovered");
				if (pWatched == 0) CrashLogAppRecovered ();
			}

			// 2. Frozen apps: events queued but not pumped for 2 s.
			CWindow *pWins[WM_MAX_WINDOWS];
			unsigned nWins = m_pWM->Snapshot (pWins, WM_MAX_WINDOWS);
			for (unsigned i = 0; i < WM_MAX_WINDOWS; i++)
			{
				if (!bFrozen[i]) continue;
				boolean bStill = FALSE;			// forget windows that went away
				for (unsigned j = 0; j < nWins; j++) if (pWins[j] == pFrozenWin[i]) bStill = TRUE;
				if (!bStill) { bFrozen[i] = FALSE; pFrozenWin[i] = 0; }
			}
			if (pWatched != 0)				// the watched app gone (closed, killed)
			{
				boolean bStill = FALSE;
				for (unsigned j = 0; j < nWins; j++) if (pWins[j] == pWatched) bStill = TRUE;
				if (!bStill) { pWatched = 0; CrashLogAppRecovered (); }
			}
			for (unsigned j = 0; j < nWins; j++)
			{
				CWindow *pW = pWins[j];
				unsigned nIdle = (nNow - pW->LastPumpTicks ()) / HZ;
				boolean bNow = pW->QueuedEvents () > 0 && nIdle >= 2;
				int k = -1;
				for (unsigned i = 0; i < WM_MAX_WINDOWS; i++) if (bFrozen[i] && pFrozenWin[i] == pW) k = (int) i;
				if (bNow && k < 0)
				{
					for (unsigned i = 0; i < WM_MAX_WINDOWS; i++)
						if (!bFrozen[i]) { bFrozen[i] = TRUE; pFrozenWin[i] = pW; break; }
					CLogger::Get ()->Write (From, LogWarning,
						"app '%s' NOT PUMPING events for %u s (%u queued, %u dropped)%s",
						pW->Title (), pW->LastPumpTicks () ? nIdle : nSec,
						pW->QueuedEvents (), pW->DroppedEvents (),
						pWatched == 0 ? "; watched: a report in SD:/etc/apphang.txt every 2 s" : "");
				}
				else if (!bNow && k >= 0)
				{
					bFrozen[k] = FALSE; pFrozenWin[k] = 0;
					CLogger::Get ()->Write (From, LogWarning, "app '%s' pumping again", pW->Title ());
					if (pW == pWatched) { pWatched = 0; CrashLogAppRecovered (); }
				}
				// The app watchdog: the first frozen app watched (where its task is, its
				// stack), its report rewritten every 2 s (SD:/etc/apphang.txt).
				if (bNow && (pWatched == 0 || pWatched == pW))
				{
					if (pWatched == 0) { pWatched = pW; CrashLogWatchPid (pW->OwnerPid ()); }
					if (nIdle % 2 == 0)
						CrashLogAppHang (pW->Title (), pW->OwnerPid (),
								 pW->LastPumpTicks () ? nIdle : nSec, pW->QueuedEvents ());
				}
			}

			// 3. Heartbeat.
			if (g_nHeartbeatSec == 0 || nSec % g_nHeartbeatSec != 0) continue;
			unsigned nMouse = m_pWM->MouseCount (), nKeys = m_pWM->KeyCount ();
			TaskStateScan s = {{0}, 0, 0, 0};
			CScheduler::Get ()->EnumerateTasks (TaskStateCollect, &s);
			char Wins[256]; unsigned n = 0; Wins[0] = '\0';
			for (unsigned j = 0; j < nWins && n + 48 < sizeof Wins; j++)
			{
				CString W;
				W.Format ("%s%s[q%u d%u]", j ? " " : "", pWins[j]->Title (),
					  pWins[j]->QueuedEvents (), pWins[j]->DroppedEvents ());
				for (const char *p = (const char *) W; *p && n + 1 < sizeof Wins; p++) Wins[n++] = *p;
				Wins[n] = '\0';
			}
			CLogger::Get ()->Write (From, LogNotice,
				"heartbeat up %us: %u fps, mouse +%u, keys +%u, tasks %u (ready %u, sleep %u, "
				"block %u), windows %u: %s",
				CTimer::Get ()->GetUptime (),
				(nFrames - nBeatFrames) / g_nHeartbeatSec,
				nMouse - nBeatMouse, nKeys - nBeatKeys,
				s.nByState[0] + s.nByState[1] + s.nByState[2] + s.nByState[3] + s.nByState[4],
				s.nByState[1], s.nByState[4], s.nByState[2] + s.nByState[3],
				nWins, Wins);
			nBeatFrames = nFrames; nBeatMouse = nMouse; nBeatKeys = nKeys;
		}
	}

private:
	CWindowManager *m_pWM;
};

//
// Input (#13): a kernel thread that pumps USB plug-and-play and, once a mouse /
// keyboard appears, wires its events into the window manager. Mouse moves/clicks
// drive the cursor + window raise/drag in CWindowManager; key presses are logged
// for now (widget routing arrives with the widget toolkit, #14/#15).
//
// Current keyboard layout name (e.g. "FR", "BE"); empty => none loaded yet. Set by the
// keyb command / theme editor via kapi_set_keymap_data; reported by kapi_get_keymap.
static char g_szKeyMap[8] = "";

// The live keyboard layout, kept as a plain RAM table -- the kernel compiles in NO
// country map. Zero (all KeyNone) at boot; kapi_set_keymap_data copies a .kmap payload
// here, then onto the attached keyboard. Re-applied on every keyboard (re-)attach so a
// hot re-plug keeps the layout (a fresh CKeyMap starts empty). Row-major [phyCode][table].
static u16 g_KeyMap[PHY_MAX_CODE + 1][K_CTRLTAB + 1];

// Copy a raw row-major [phyCode][table] map onto a CKeyMap through the public SetEntry
// (phyCode 0 is skipped -- SetEntry rejects it). Used to (re-)apply g_KeyMap to a keyboard.
static void LoadKeyMapTable (CKeyMap *pKeyMap, const u16 *pMap)
{
	for (u8 nTable = 0; nTable <= K_CTRLTAB; nTable++)
		for (u8 nPhy = 1; nPhy <= PHY_MAX_CODE; nPhy++)
			pKeyMap->SetEntry (nTable, nPhy, pMap[nPhy * (K_CTRLTAB + 1) + nTable]);
}

// USB gamepads (ABI v50): Circle's drivers name them upad1..upad4; each slot keeps the last
// report, written by the driver's status handler (USB completion, interrupt time) under a
// sequence count (odd while writing) so kapi_pad_state reads a whole state.
struct TPadSlot
{
	CUSBGamePadDevice * volatile pDev;
	volatile unsigned nSeq;
	struct kapi_pad State;
};
static TPadSlot s_Pads[KAPI_PAD_MAX];

static void PadCopyState (struct kapi_pad *pOut, const TGamePadState *pIn)
{
	pOut->nbuttons = pIn->nbuttons;
	pOut->buttons = pIn->buttons;
	pOut->naxes = pIn->naxes < KAPI_PAD_AXES ? pIn->naxes : KAPI_PAD_AXES;
	for (int i = 0; i < pOut->naxes; i++)
	{
		pOut->axes[i].value = pIn->axes[i].value;
		pOut->axes[i].minimum = pIn->axes[i].minimum;
		pOut->axes[i].maximum = pIn->axes[i].maximum;
	}
	pOut->nhats = pIn->nhats < KAPI_PAD_HATS ? pIn->nhats : KAPI_PAD_HATS;
	for (int i = 0; i < pOut->nhats; i++) pOut->hats[i] = pIn->hats[i];
}

boolean KernelPadState (int nIndex, struct kapi_pad *pOut)
{
	if (nIndex < 0 || nIndex >= KAPI_PAD_MAX || pOut == 0) return FALSE;
	TPadSlot &Slot = s_Pads[nIndex];
	for (int nTry = 0; nTry < 1000; nTry++)
	{
		if (Slot.pDev == 0) return FALSE;
		unsigned nSeq = Slot.nSeq;
		if (nSeq & 1) continue;				// being written
		DataMemBarrier ();
		memcpy (pOut, &Slot.State, sizeof *pOut);
		DataMemBarrier ();
		if (Slot.nSeq == nSeq) { pOut->seq = nSeq >> 1; return TRUE; }
	}
	return FALSE;
}

// USB MIDI input (ABI v68): Circle's USB MIDI class driver names each device umidiN; its
// packet handler runs at the USB completion (interrupt time) and queues the packet here with
// the clock's time; kapi_midi_read takes them out. One queue for the whole system (a DAW reads
// it); a full queue drops the newest events (counted).
#define MIDI_QUEUE	256				// a power of two
static CUSBMIDIDevice * volatile s_pMidi[KAPI_MIDI_DEVICES];
static struct kapi_midi_event s_MidiQueue[MIDI_QUEUE];
static unsigned s_nMidiIn = 0, s_nMidiOut = 0;		// event counters (in - out = queued)
static unsigned s_nMidiLost = 0;
static CSpinLock s_MidiLock (IRQ_LEVEL);

static void MidiPacket (unsigned nCable, u8 *pPacket, unsigned nLength, unsigned nDevice, void *)
{
	if (nLength == 0 || nLength > 3) return;	// (a reserved code index number)
	unsigned nTime = CTimer::Get ()->GetClockTicks ();
	s_MidiLock.Acquire ();
	if (s_nMidiIn - s_nMidiOut >= MIDI_QUEUE)
	{
		s_nMidiLost++;				// (nobody reads: the newest are dropped)
	}
	else
	{
		struct kapi_midi_event &E = s_MidiQueue[s_nMidiIn % MIDI_QUEUE];
		E.time_us = nTime;
		E.cable = (unsigned char) nCable;
		E.status = pPacket[0];
		E.data1 = nLength > 1 ? pPacket[1] : 0;
		E.data2 = nLength > 2 ? pPacket[2] : 0;
		E.device = (unsigned char) nDevice;
		E.length = (unsigned char) nLength;
		E.reserved[0] = E.reserved[1] = 0;
		s_nMidiIn++;
	}
	s_MidiLock.Release ();
}

int KernelMidiRead (struct kapi_midi_event *pEv, int nMax)
{
	if (pEv == 0 || nMax < 0) return -1;
	int n = 0;
	while (n < nMax)
	{
		struct kapi_midi_event E;
		s_MidiLock.Acquire ();
		boolean bGot = s_nMidiIn != s_nMidiOut;
		if (bGot) E = s_MidiQueue[s_nMidiOut++ % MIDI_QUEUE];
		s_MidiLock.Release ();
		if (!bGot) break;
		pEv[n++] = E;				// (the app's memory: written outside the lock)
	}
	return n;
}

int KernelMidiDevices (void)
{
	int n = 0;
	for (unsigned i = 0; i < KAPI_MIDI_DEVICES; i++) if (s_pMidi[i] != 0) n++;
	return n;
}

// Print Screen (USB usage 0x46): set by the input task when the key goes down (1; 2 with Alt), handled
// by the main task's loop (PrintScreenPoll: the "screenshot" service told, else the app started).
static volatile unsigned s_nPrintScreen;

class CInputTask : public CTask
{
public:
	CInputTask (CUSBHCIDevice *pUSB, CDeviceNameService *pDNS, CDisplay *pDisplay,
		    CLogger *pLogger)
	:	m_pUSB (pUSB), m_pDNS (pDNS), m_pDisplay (pDisplay), m_pLogger (pLogger),
		m_pMouse (0)
	{
		for (unsigned i = 0; i < KBD_MAX; i++) m_pKeyboards[i] = 0;
		SetName ("input");
		s_pThis = this;
	}

	// Load a raw keymap table onto the live keyboard AND into the persistent g_KeyMap
	// snapshot: (PHY_MAX_CODE+1) x (K_CTRLTAB+1) u16 entries, row-major
	// (m_KeyMap[phyCode][table]). The bytes come from a SD:/etc/keymaps/<X>.kmap file
	// (via kapi_set_keymap_data); we keep a copy (so a later keyboard re-plug restores
	// the layout) and push it onto the live keyboard through the public SetEntry, so
	// layouts need no kernel rebuild. Returns FALSE only if the blob is malformed
	// (null / wrong size). When valid the snapshot is ALWAYS updated and TRUE returned;
	// it is pushed onto the live keyboard at once if one is attached, otherwise applied
	// on the next attach (Detect() re-applies g_KeyMap). So a layout set before the
	// keyboard enumerates is never lost -- this is what kills the boot-time `keyb` race
	// (slow USB enumeration used to make keyb give up, leaving the keyboard map-less).
	static boolean SetKeyMapData (const void *pData, unsigned nLen)
	{
		if (pData == 0)
		{
			return FALSE;
		}
		if (nLen != (unsigned) (PHY_MAX_CODE + 1) * (K_CTRLTAB + 1) * sizeof (u16))
		{
			return FALSE;
		}
		const u16 *pMap = (const u16 *) pData;
		for (u8 nTable = 0; nTable <= K_CTRLTAB; nTable++)
			for (u8 nPhy = 0; nPhy <= PHY_MAX_CODE; nPhy++)
				g_KeyMap[nPhy][nTable] = pMap[nPhy * (K_CTRLTAB + 1) + nTable];
		// Ctrl with the keys of - = + 0 (a browser's zoom: Jet Browser, docs/06 §38): Circle's
		// keymap uses the Ctrl column for any key but a letter, and the layouts leave it empty
		// there -- nothing came. The empty Ctrl entry of the key whose own character (or, for
		// '0', its Shift one: AZERTY) is one of them gets the keypad's key of that character,
		// whose string ("-", "+", "0") Circle sends with or without Ctrl: the app sees the
		// character, kapi_get_modifiers () says Ctrl. Letters and the keypad (0x53..0x63) keep
		// theirs; Ctrl+Shift+= (US '+') still gives nothing (Shift's column wins in Circle).
		for (unsigned nPhy = 1; nPhy <= PHY_MAX_CODE; nPhy++)
		{
			if ((nPhy >= 0x04 && nPhy <= 0x1D) || (nPhy >= 0x53 && nPhy <= 0x63)
			    || g_KeyMap[nPhy][K_CTRLTAB] != KeyNone)
				continue;
			u16 nNorm = g_KeyMap[nPhy][K_NORMTAB], nShift = g_KeyMap[nPhy][K_SHIFTTAB];
			if (nNorm == '0' || (nShift == '0' && nNorm != '-' && nNorm != '='))
				g_KeyMap[nPhy][K_CTRLTAB] = KeyKP_0;
			else if (nNorm == '-')
				g_KeyMap[nPhy][K_CTRLTAB] = KeyKP_Subtract;
			else if (nNorm == '=' || nNorm == '+')
				g_KeyMap[nPhy][K_CTRLTAB] = KeyKP_Add;
		}
		// Apply live if a keyboard is already up; otherwise the snapshot is enough --
		// Detect() loads g_KeyMap onto the keyboard the moment it attaches.
		if (s_pThis != 0)
		{
			for (unsigned i = 0; i < KBD_MAX; i++)
			{
				CUSBKeyboardDevice *pKbd = s_pThis->m_pKeyboards[i];
				if (pKbd != 0) LoadKeyMapTable (pKbd->GetKeyMap (), (const u16 *) g_KeyMap);
			}
		}
		return TRUE;
	}

	// Is a USB keyboard attached and ready right now? Exposed to userspace via
	// kapi_kbd_ready so the `keyb` tool can poll before applying a layout at boot
	// (it may run before USB enumeration finishes).
	static boolean HasKeyboard (void)
	{
		if (s_pThis == 0) return FALSE;
		for (unsigned i = 0; i < KBD_MAX; i++) if (s_pThis->m_pKeyboards[i] != 0) return TRUE;
		return FALSE;
	}

	void Run (void) override
	{
		Detect ();			// devices already present at boot
		for (;;)
		{
			m_pUSB->UpdatePlugAndPlay ();
			Detect ();		// (cheap: a few name lookups; a keyboard plugged later is taken too)
			DetectPads ();
			DetectMidi ();
			CScheduler::Get ()->MsSleep (100);
		}
	}

private:
	void Detect (void)
	{
		// Every USB keyboard, ukbd1..ukbd4 -- not only the first: a wireless mouse's receiver
		// (Logitech's nano receiver: a boot keyboard interface beside the mouse) or a
		// keyboard's extra interface may take ukbd1, and the real keyboard is then ukbd2.
		for (unsigned i = 0; i < KBD_MAX; i++)
		{
			if (m_pKeyboards[i] != 0) continue;
			CString Name;
			Name.Format ("ukbd%u", i + 1);
			CUSBKeyboardDevice *pKbd = (CUSBKeyboardDevice *) m_pDNS->GetDevice (Name, FALSE);
			if (pKbd == 0) continue;
			s_KbdMods[i] = 0;
			for (unsigned k = 0; k < 6; k++) s_KbdKeys[i][k] = 0;
			pKbd->RegisterRemovedHandler (KeyboardRemoved, (void *) (uintptr) i);
			pKbd->RegisterKeyPressedHandler (KeyPressedStub);
			// Mixed mode: the raw report too (cooked keys unaffected), for the
			// modifier state (Ctrl = copy in drag & drop, kapi_get_modifiers).
			pKbd->RegisterKeyStatusHandlerRaw (KeyRawStub, TRUE, (void *) (uintptr) i);
			// Apply the current layout snapshot (empty until keyb loads a .kmap),
			// so a hot re-plug keeps the layout -- a fresh CKeyMap starts empty.
			LoadKeyMapTable (pKbd->GetKeyMap (), (const u16 *) g_KeyMap);
			m_pKeyboards[i] = pKbd;
			m_pLogger->Write ("input", LogNotice, "keyboard attached (%s)", (const char *) Name);
		}

		if (m_pMouse == 0)
		{
			m_pMouse = (CMouseDevice *) m_pDNS->GetDevice ("mouse1", FALSE);
			if (m_pMouse != 0)
			{
				m_pMouse->RegisterRemovedHandler (MouseRemoved);
				// Cooked mode: absolute coords clamped to the display; bCursor
				// FALSE -- our compositor draws the cursor itself.
				if (m_pMouse->Setup (m_pDisplay, FALSE))
				{
					m_pMouse->RegisterEventHandler (MouseEventStub);
					m_pLogger->Write ("input", LogNotice, "mouse attached");
				}
			}
		}
	}

	// Gamepads upad1..upad4 -> slots 0..3 (Circle frees and reuses the numbers on unplug).
	void DetectPads (void)
	{
		for (unsigned i = 0; i < KAPI_PAD_MAX; i++)
		{
			TPadSlot &Slot = s_Pads[i];
			if (Slot.pDev != 0) continue;
			CUSBGamePadDevice *pPad = (CUSBGamePadDevice *) m_pDNS->GetDevice ("upad", i + 1, FALSE);
			if (pPad == 0) continue;
			Slot.nSeq++;					// (odd: being written)
			DataMemBarrier ();
			memset (&Slot.State, 0, sizeof Slot.State);
			const TUSBDeviceDescriptor *pDesc = pPad->GetDevice ()->GetDeviceDescriptor ();
			if (pDesc != 0) { Slot.State.vid = pDesc->idVendor; Slot.State.pid = pDesc->idProduct; }
			Slot.State.props = pPad->GetProperties ();
			PadCopyState (&Slot.State, pPad->GetInitialState ());
			DataMemBarrier ();
			Slot.nSeq++;
			Slot.pDev = pPad;
			pPad->RegisterRemovedHandler (PadRemoved, &Slot);
			pPad->RegisterStatusHandler (PadStatus);
			m_pLogger->Write ("input", LogNotice, "gamepad %u attached (%04x:%04x, %d buttons, %d axes, %d hats%s)",
					  i + 1, Slot.State.vid, Slot.State.pid, Slot.State.nbuttons, Slot.State.naxes,
					  Slot.State.nhats, (Slot.State.props & GamePadPropertyIsKnown) ? ", known" : "");
		}
	}

	// MIDI devices umidi1..umidiN (Circle reuses the numbers after an unplug): a new one gets
	// our packet handler once (Circle allows one) and a removal handler that frees its slot.
	void DetectMidi (void)
	{
		for (unsigned i = 0; i < KAPI_MIDI_DEVICES; i++)
		{
			if (s_pMidi[i] != 0) continue;
			CUSBMIDIDevice *pMidi = (CUSBMIDIDevice *) m_pDNS->GetDevice ("umidi", i + 1, FALSE);
			if (pMidi == 0) continue;
			pMidi->RegisterRemovedHandler (MidiRemoved, (void *) &s_pMidi[i]);
			pMidi->RegisterPacketHandler (MidiPacket, 0);
			s_pMidi[i] = pMidi;
			m_pLogger->Write ("input", LogNotice, "MIDI device %u attached", i + 1);
		}
	}

	static void MidiRemoved (CDevice *, void *pContext)
	{
		if (pContext != 0) *(CUSBMIDIDevice * volatile *) pContext = 0;
	}

	static void PadStatus (unsigned nDeviceIndex, const TGamePadState *pState)
	{
		if (nDeviceIndex >= KAPI_PAD_MAX || pState == 0) return;
		TPadSlot &Slot = s_Pads[nDeviceIndex];
		Slot.nSeq++;
		DataMemBarrier ();
		PadCopyState (&Slot.State, pState);
		DataMemBarrier ();
		Slot.nSeq++;
	}

	static void PadRemoved (CDevice *, void *pContext)
	{
		if (pContext != 0) ((TPadSlot *) pContext)->pDev = 0;
	}

	// Circle's cooked mouse reports the *changed button mask* on MouseDown/MouseUp
	// (not the full state) and the full state only on MouseMove. If we forwarded that
	// raw value, a release (MouseUp, mask=1) would look like "still pressed", so the
	// WM would miss the press edge of a second click -- breaking double-click. So we
	// reconstruct the real button bitmask: Down sets the bit, Up clears it, Move syncs.
	static void MouseEventStub (TMouseEvent Event, unsigned nButtons,
				    unsigned nPosX, unsigned nPosY, int nWheelMove)
	{
		static unsigned s_nButtons = 0;
		switch (Event)
		{
		case MouseEventMouseDown: s_nButtons |= nButtons;  break;	// nButtons = changed mask
		case MouseEventMouseUp:   s_nButtons &= ~nButtons; break;
		case MouseEventMouseMove: s_nButtons = nButtons;   break;	// full state
		case MouseEventMouseWheel:					// scroll notch, no button change
			if (CWindowManager::Get () != 0)
				CWindowManager::Get ()->OnMouseWheel ((int) nPosX, (int) nPosY, nWheelMove);
			return;
		default: break;
		}
		if (CWindowManager::Get () != 0)
		{
			CWindowManager::Get ()->OnMouse ((int) nPosX, (int) nPosY, s_nButtons);
		}
	}

	// USB HID modifier byte: bit0/4 Ctrl, bit1/5 Shift, bit2/6 Alt (left/right). Each
	// keyboard's last report is kept (pArg: its slot); the WM gets them merged, so a
	// keyboard's empty report does not release the keys held on another one.
	static void KeyRawStub (unsigned char ucModifiers, const unsigned char RawKeys[6], void *pArg)
	{
		unsigned nSlot = (unsigned) (uintptr) pArg;
		if (nSlot >= KBD_MAX) return;
		s_KbdMods[nSlot] = ucModifiers;
		for (unsigned k = 0; k < 6; k++) s_KbdKeys[nSlot][k] = RawKeys[k];
		PublishKeys ();
	}

	static void PublishKeys (void)
	{
		unsigned char ucMods = 0, Keys[6] = { 0, 0, 0, 0, 0, 0 };
		unsigned n = 0;
		for (unsigned i = 0; i < KBD_MAX; i++)
		{
			ucMods |= s_KbdMods[i];
			for (unsigned k = 0; k < 6 && n < 6; k++)
				if (s_KbdKeys[i][k] != 0) Keys[n++] = s_KbdKeys[i][k];
		}
		unsigned nMods = ((ucMods & 0x11) ? MOD_CTRL : 0)
			       | ((ucMods & 0x22) ? MOD_SHIFT : 0)
			       | ((ucMods & 0x44) ? MOD_ALT : 0);
		CWindowManager *pWM = CWindowManager::Get ();
		if (pWM != 0 && pWM->Modifiers () != nMods) pWM->SetModifiers (nMods);
		if (pWM != 0) pWM->SetUsbHeld (Keys);	// held keys (games, ABI v48)
		static boolean s_bPrintHeld = FALSE;	// Print Screen: its press (not while it is held)
		boolean bPrint = FALSE;
		for (unsigned k = 0; k < 6; k++) if (Keys[k] == 0x46) bPrint = TRUE;
		if (bPrint && !s_bPrintHeld) s_nPrintScreen = (nMods & MOD_ALT) ? 2 : 1;
		s_bPrintHeld = bPrint;
	}

	static void KeyPressedStub (const char *pString)
	{
		// Route to the focused widget (a textbox); the WM edits its text + posts a
		// TEXT_CHANGED event to the owning app.
		if (CWindowManager::Get () != 0)
		{
			CWindowManager::Get ()->OnKey (pString);
		}
	}

	static void MouseRemoved (CDevice *, void *)
	{
		if (s_pThis != 0) { s_pThis->m_pMouse = 0; }
	}

	static void KeyboardRemoved (CDevice *, void *pContext)
	{
		unsigned nSlot = (unsigned) (uintptr) pContext;
		if (s_pThis == 0 || nSlot >= KBD_MAX) return;
		s_pThis->m_pKeyboards[nSlot] = 0;
		s_KbdMods[nSlot] = 0;				// (its keys no longer held)
		for (unsigned k = 0; k < 6; k++) s_KbdKeys[nSlot][k] = 0;
		PublishKeys ();
	}

	CUSBHCIDevice	   *m_pUSB;
	CDeviceNameService *m_pDNS;
	CDisplay	   *m_pDisplay;
	CLogger		   *m_pLogger;
	CMouseDevice       * volatile m_pMouse;
	static const unsigned KBD_MAX = 4;		// ukbd1..ukbd4
	CUSBKeyboardDevice * volatile m_pKeyboards[KBD_MAX];

	static unsigned char s_KbdMods[KBD_MAX];	// each keyboard's last raw report
	static unsigned char s_KbdKeys[KBD_MAX][6];

	static CInputTask  *s_pThis;
};

CInputTask *CInputTask::s_pThis = 0;
unsigned char CInputTask::s_KbdMods[CInputTask::KBD_MAX];
unsigned char CInputTask::s_KbdKeys[CInputTask::KBD_MAX][6];

// Keyboard-layout control exposed to the kapi layer (sys/kapi.cpp). The kernel no longer
// compiles in any country map, so loading one *by name* (kapi_set_keymap) is unsupported
// and always fails -- layouts ship as SD:/etc/keymaps/*.kmap and are loaded by name+data
// through KernelSetKeyMapData. The ABI slot stays (append-only); the userspace helper
// (ax_load_keymap) treats the failure as "no compiled map" and falls back to the file.
boolean KernelSetKeyMap (const char *pName)
{
	(void) pName;
	return FALSE;
}

// Load a keymap from a raw table (validated SD:/etc/keymaps/<X>.kmap payload) and
// record its name for reporting. Used by kapi_set_keymap_data so layouts can be added
// as files without recompiling the kernel.
boolean KernelSetKeyMapData (const char *pName, const void *pData, unsigned nLen)
{
	if (!CInputTask::SetKeyMapData (pData, nLen))
	{
		return FALSE;
	}
	unsigned i = 0;
	if (pName != 0)
		for (; pName[i] != '\0' && i < sizeof (g_szKeyMap) - 1; i++) g_szKeyMap[i] = pName[i];
	g_szKeyMap[i] = '\0';
	return TRUE;
}

const char *KernelGetKeyMap (void)
{
	return g_szKeyMap;
}

// Keyboard readiness, exposed to userspace (kapi_kbd_ready). The `keyb` tool polls
// this at boot and applies the layout only once the keyboard has enumerated -- the
// kernel no longer applies any layout itself (cmdline keymap= is ignored).
boolean KernelKeyboardReady (void)
{
	return CInputTask::HasKeyboard ();
}

// Verbose flag control, exposed to the kapi layer (kapi_set_verbose / get_verbose).
void KernelSetVerbose (boolean bOn)
{
	g_bVerbose = bOn;
	CLogger::Get ()->Write ("verbose", LogNotice, "verbose logging %s", bOn ? "ON" : "OFF");
}
boolean KernelGetVerbose (void) { return g_bVerbose; }

//
// CNetBringupTask -- bring up WLAN + the TCP/IP stack on the primary core in the
// background, so a missing firmware / wpa_supplicant.conf / access point never
// blocks (or slows) the GUI boot. The order is mandatory: the WLAN device must be
// up before the net subsystem, and wpa_supplicant associates after the stack is
// initialized. Once Initialize() returns, Circle's own CNetTask / CPHYTask keep
// the stack running on our scheduler; this task just waits for the link, logs the
// address and exits (it has no user address space, so returning is clean).
//
class CNetBringupTask : public CTask
{
public:
	CNetBringupTask (CBcm4343Device *pWLAN, CNetSubSystem *pNet,
			 CWPASupplicant *pWPA, CLogger *pLogger)
	:	m_pWLAN (pWLAN), m_pNet (pNet), m_pWPA (pWPA), m_pLogger (pLogger)
	{
		SetName ("net");
	}

	void Run (void) override
	{
		g_pNet = m_pNet;		// publish (still down until associated)

		m_pLogger->Write (FromKernel, LogNotice,
				  "net: bringing up WLAN (firmware " WLAN_FIRMWARE_PATH ")");
		if (!m_pWLAN->Initialize ())
		{
			m_pLogger->Write (FromKernel, LogWarning,
				"net: WLAN init failed -- is " WLAN_FIRMWARE_PATH " present?");
			return;
		}

		if (!m_pNet->Initialize (FALSE))	// FALSE: don't block here for activate
		{
			m_pLogger->Write (FromKernel, LogWarning, "net: TCP/IP init failed");
			return;
		}

		m_pLogger->Write (FromKernel, LogNotice,
				  "net: associating (" WLAN_CONFIG_FILE ") ...");
		if (!m_pWPA->Initialize ())
		{
			m_pLogger->Write (FromKernel, LogWarning,
				"net: wpa_supplicant init failed -- is " WLAN_CONFIG_FILE " present?");
			return;
		}

		// Wait for the link to come up (DHCP bind). Log progress occasionally so a
		// stuck association is visible, but never give up -- the AP may appear later.
		unsigned nWaited = 0;
		while (!m_pNet->IsRunning ())
		{
			CScheduler::Get ()->MsSleep (250);
			if ((nWaited += 250) % 10000 == 0)
				m_pLogger->Write (FromKernel, LogNotice,
						  "net: still associating (%u s) ...", nWaited / 1000);
		}

		CString IPString;
		m_pNet->GetConfig ()->GetIPAddress ()->Format (&IPString);
		m_pLogger->Write (FromKernel, LogNotice, "net: up, IP %s",
				  (const char *) IPString);
		g_bNetUp = TRUE;
		CString Msg;
		Msg.Format ("Connected. IP address %s", (const char *) IPString);
		if (g_bNetCore) NetCoreNotify ("Network", (const char *) Msg);	// (IPC is core 0's)
		else IpcNotify ("Network", (const char *) Msg);

		// Sync the wall clock over NTP (its own background task; updates CTimer so
		// kapi_get_datetime / the agenda / log timestamps show real local time).
		if (g_szNtpServer[0])
		{
			new CNTPDaemon (g_szNtpServer, m_pNet);
			m_pLogger->Write (FromKernel, LogNotice, "net: NTP started (%s)", g_szNtpServer);
		}
	}

private:
	CBcm4343Device *m_pWLAN;
	CNetSubSystem  *m_pNet;
	CWPASupplicant *m_pWPA;
	CLogger	       *m_pLogger;
};

// netcore=1: the bring-up task is created on core 3 (by NetCoreMain), so it -- and every task
// it starts: Circle's net tasks, the WLAN kprocs, wpa_supplicant, NTP -- runs there.
static CBcm4343Device *s_pNetWLAN; static CNetSubSystem *s_pNetNet;
static CWPASupplicant *s_pNetWPA; static CLogger *s_pNetLogger;
static CTask *NewNetBringupTask (void)
{
	return new CNetBringupTask (s_pNetWLAN, s_pNetNet, s_pNetWPA, s_pNetLogger);
}

// Resolution: cmdline.txt "width="/"height=" override the defaults (1024x768).
// m_Options is constructed before m_Screen/m_2DGraphics, so it is safe to query here.
#define OPT_W(opt)	((opt).GetWidth ()  != 0 ? (int) (opt).GetWidth ()  : SCREEN_WIDTH)
#define OPT_H(opt)	((opt).GetHeight () != 0 ? (int) (opt).GetHeight () : SCREEN_HEIGHT)

CKernel::CKernel (void)
:	m_CPUThrottle (CPUSpeedMaximum),	// 1.5 GHz from boot, not the ~600 MHz idle default
	m_Screen (OPT_W (m_Options), OPT_H (m_Options)),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_2DGraphics (OPT_W (m_Options), OPT_H (m_Options), FALSE),	// VSync off: avoid page-flip present
	m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
	m_USB (&m_Interrupt, &m_Timer, TRUE),			// TRUE: enable plug-and-play
	m_WLAN (WLAN_FIRMWARE_PATH),				// WLAN firmware dir on the SD card
	m_Net (0, 0, 0, 0, DEFAULT_HOSTNAME, NetDeviceTypeWLAN),// DHCP over WLAN
	m_WPASupplicant (WLAN_CONFIG_FILE),			// SSID/PSK supplied by the user
	m_FbConsole (&m_2DGraphics),
	m_bGraphics (FALSE),
	m_bSDMounted (FALSE),
	m_bUSB (FALSE)
{
	m_ActLED.Blink (5);		// visible sign of life before the console is up

	// Publish the chosen resolution so the GUI (compositor, wallpaper, cursor clamp,
	// dialog centering, kapi_screen_size) uses the real size, not the defaults.
	g_nScreenWidth  = OPT_W (m_Options);
	g_nScreenHeight = OPT_H (m_Options);

	// Keyboard layout is NOT applied here: cmdline keymap= is ignored. Userspace owns
	// it -- the autostart `keyb <XX>` tool polls kapi_kbd_ready then kapi_set_keymap.
}

// Read a whole file from the mounted SD card into a freshly allocated buffer.
// Returns the buffer (caller may leak it for a one-shot process load) + its size,
// or 0 on any error.
static u8 *LoadFileFromSD (const char *pPath, unsigned *pSize)
{
	FIL File;
	if (f_open (&File, pPath, FA_READ) != FR_OK)
	{
		return 0;
	}

	unsigned nSize = (unsigned) f_size (&File);
	u8 *pBuffer = new u8[nSize];
	if (pBuffer == 0)
	{
		f_close (&File);
		return 0;
	}

	UINT nRead = 0;
	if (f_read (&File, pBuffer, nSize, &nRead) != FR_OK || nRead != nSize)
	{
		delete [] pBuffer;
		f_close (&File);
		return 0;
	}

	f_close (&File);
	*pSize = nSize;
	return pBuffer;
}

// Scan a theme.txt buffer for a "wheelspeed=N" line; returns N, or 0 if absent/invalid.
static int ParseWheelSpeed (const u8 *p, unsigned n)
{
	static const char key[] = "wheelspeed=";
	const unsigned klen = sizeof key - 1;
	for (unsigned i = 0; i + klen <= n; i++)
	{
		unsigned k = 0;
		while (k < klen && p[i + k] == (u8) key[k]) k++;
		if (k != klen) continue;
		int v = 0; unsigned j = i + klen;
		while (j < n && p[j] >= '0' && p[j] <= '9') v = v * 10 + (p[j++] - '0');
		return v;
	}
	return 0;
}

// The mouse cursor, built in (it was SD:skins/mousecur.bin, SimpleOS's 12x19 arrow; the skins folder
// is gone -- window chrome is drawn user-side): W white, B black, . transparent.
static const char s_Cursor[19][13] = {
	"W...........",
	"WW..........",
	"WBW.........",
	"WBBW........",
	"WBBBW.......",
	"WBBBBW......",
	"WBBBBBW.....",
	"WBBBBBBW....",
	"WBBBBBBBW...",
	"WBBBBBBBBW..",
	"WBBBBBBBBBW.",
	"WBBBBBBBBBBW",
	"WBBBBBBWWWWW",
	"WBBBWBBW....",
	"WBBW.WBBW...",
	"WBW..WBBW...",
	"WW....WBBW..",
	"......WBBW..",
	".......WW..."
};

static GImage *BuiltinCursor (void)
{
	const int nW = 12, nH = 19;
	GImage *pImg = new GImage;
	if (pImg == 0) return 0;
	pImg->SetSize (nW, nH);
	if (!pImg->IsValid ()) { delete pImg; return 0; }
	for (int y = 0; y < nH; y++)
	{
		for (int x = 0; x < nW; x++)
		{
			char c = s_Cursor[y][x];
			pImg->SetPixel (x, y, c == 'W' ? 0x00FFFFFF : c == 'B' ? 0x00000000 : GIMAGE_TRANSPARENT);
		}
	}
	return pImg;
}

static boolean KeyEq (const char *s, const char *e, const char *pLit)
{
	while (s < e && *pLit != '\0' && *s == *pLit) { s++; pLit++; }
	return s == e && *pLit == '\0';
}

// Read SD:system.ini at boot for system-wide settings (currently just verbose=0/1).
static void ReadSystemConfig (void)
{
	unsigned nSize = 0;
	u8 *pData = LoadFileFromSD ("SD:etc/system.ini", &nSize);
	if (pData == 0) return;
	const char *p = (const char *) pData, *pEnd = p + nSize;
	while (p < pEnd)
	{
		const char *ls = p;
		while (p < pEnd && *p != '\n' && *p != '\r') p++;
		const char *le = p;
		while (p < pEnd && (*p == '\n' || *p == '\r')) p++;
		while (ls < le && (*ls == ' ' || *ls == '\t')) ls++;
		if (ls >= le || *ls == '#' || *ls == ';') continue;
		const char *eq = ls;
		while (eq < le && *eq != '=') eq++;
		if (eq >= le) continue;
		const char *ke = eq;
		while (ke > ls && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
		const char *vs = eq + 1;
		while (vs < le && (*vs == ' ' || *vs == '\t')) vs++;
		if (KeyEq (ls, ke, "verbose")) g_bVerbose = (vs < le && *vs == '1');
		else if (KeyEq (ls, ke, "timezone"))
		{
			const char *q = vs; int neg = 0; long v = 0; boolean any = FALSE;
			if (q < le && (*q == '-' || *q == '+')) { neg = (*q == '-'); q++; }
			while (q < le && *q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); q++; any = TRUE; }
			if (any) g_nTimeZoneMin = (int) (neg ? -v : v);
		}

		else if (KeyEq (ls, ke, "ntp"))
		{
			unsigned i = 0;
			for (const char *q = vs; q < le && i < sizeof (g_szNtpServer) - 1; q++)
			{
				if (*q == ' ' || *q == '\t') break;
				g_szNtpServer[i++] = *q;
			}
			if (i > 0) g_szNtpServer[i] = '\0';
			if ((i == 3 && KeyEq (g_szNtpServer, g_szNtpServer + 3, "off")) || (i == 4 && KeyEq (g_szNtpServer, g_szNtpServer + 4, "none")))
				g_szNtpServer[0] = '\0';		// (no clock sync)
		}
		else if (KeyEq (ls, ke, "ramfs"))
		{
			unsigned i = 0;
			for (const char *q = vs; q < le && i < sizeof (g_szRamFs) - 1; q++)
			{
				if (*q == ' ' || *q == '\t') break;
				g_szRamFs[i++] = *q;
			}
			g_szRamFs[i] = '\0';
		}
		else if (KeyEq (ls, ke, "hostname"))
		{
			unsigned i = 0;					// (letters, digits, '-': a DNS label)
			for (const char *q = vs; q < le && i < sizeof (g_szHostname) - 1; q++)
			{
				char c = *q;
				if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) break;
				g_szHostname[i++] = c;
			}
			g_szHostname[i] = '\0';
		}
	}
	delete [] pData;
}

// Quick existence check (metadata only -- a directory lookup, NOT the file body). Used
// before deferring an app's load so a missing path fails immediately at the caller (the
// heavy ~MB read still happens later on the new task's own thread).
static boolean SdFileExists (const char *pPath)
{
	FILINFO Info;
	return f_stat (pPath, &Info) == FR_OK && !(Info.fattrib & AM_DIR);
}

#ifdef ARM_ALLOW_MULTI_CORE
// The secondary cores (Circle's CMultiCoreSupport, started at boot). The scheduler, the
// interrupts and every process stay on core 0; core 1 is the sound producer (it sleeps
// in WFE until the audio is first used, see sys/sound.cpp); cores 2 and 3 are app cores
// that an app can acquire to run a function of its own (sys/appcore.cpp) -- core 2 only
// with netcore=1: core 3 then runs the network stack (its own scheduler, sys/net.cpp).
class COnyxCores : public CMultiCoreSupport
{
public:
	COnyxCores (void) : CMultiCoreSupport (CMemorySystem::Get ()) {}
	void Run (unsigned nCore) override
	{
		El0CoreInit (nCore);			// (the EL0 controls of this core, kern/el0.h)
		if (nCore == 1) SoundCoreMain ();
		else if (nCore == 3 && g_bNetCore) NetCoreMain ();	// the network core
		else AppCoreMain (nCore);		// cores 2-3: app cores (kern/appcore.h)
		for (;;) asm volatile ("wfe");
	}
};
static COnyxCores *s_pCores = 0;
#endif

// Launch an app by folder name: SD:apps/<name>.app/main -> a new process.
// Safe to call from any task context (cooperative); the new task runs when scheduled.
static boolean LaunchApp (const char *pName, CLogger *pLogger)
{
	CString Path;
	Path.Format ("SD:apps/%s.app/main", pName);

	// Verify the program exists NOW (cheap; no card access when its image is in memory) so a bad
	// name fails here, not asynchronously.
	if (!ProgramExists ((const char *) Path))
	{
		pLogger->Write (FromKernel, LogError, "launch: not found %s", (const char *) Path);
		return FALSE;
	}

	// Deferred load: hand the PATH to the task; it reads the ELF on its own thread when
	// the scheduler first switches to it, so this caller (often the UI) returns at once.
	// (v75: a desktop launch gets the system's default environment, kern/procx.h)
	TProcInfo *pInfo = ProcInfoNew ((const char *) Path, 0, FALSE);
	CUserProcessTask *pTask = new CUserProcessTask ((const char *) Path, pName, pLogger,
							0, 0, 0, 0, 0, 0, pInfo);
	if (pTask == 0)
	{
		ProcInfoFree (pInfo);
		pLogger->Write (FromKernel, LogError, "launch: out of memory for %s", pName);
		return FALSE;
	}
	pLogger->Write (FromKernel, LogNotice, "launch: %s (deferred)", pName);
	return TRUE;
}

// Non-static wrapper exported to the rest of the kernel (kapi_launch). Spawns an
// app by folder name from any task context; logs via the global logger.
boolean LaunchAppByName (const char *pName)
{
	if (pName == 0 || pName[0] == '\0')
	{
		return FALSE;
	}
	return LaunchApp (pName, CLogger::Get ());
}

// Spawn a console process: load the ELF at pElfPath and run it with the given
// stdin/stdout streams + argv. Returns a CProcess record holding two refs -- the caller's
// (kapi_spawn puts it in a handle) and the child's -- or 0 on failure. The child address
// space takes a ref on each stream (the caller keeps its own); the record's done/status
// are set when the child exits.
CProcess *SpawnProcess (const char *pElfPath, const char *pArgs,
			CStream *pStdin, CStream *pStdout, const char *pCwd,
			unsigned nParentPid, TProcInfo *pInfo)
{
	if (pElfPath == 0 || !ProgramExists (pElfPath))	// missing -> immediate failure (shell prints "not found")
	{
		ProcInfoFree (pInfo);
		return 0;
	}
	// (v75) its argv / environment (kern/procx.h): spawn_ex's, else the spawner's environment
	if (pInfo == 0)
	{
		pInfo = ProcInfoNew (pElfPath, pArgs, TRUE);
	}

	CProcess *pProc = new CProcess;
	if (pProc == 0)
	{
		ProcInfoFree (pInfo);
		return 0;
	}
	pProc->bDone = FALSE;
	pProc->nStatus = 0;
	pProc->nRef = 2;			// the caller's handle + the child (kern/handle.h)
	pProc->nReason = KAPI_PROC_EXITED;	// (v75: the teardown sets both)
	pProc->nPid = 0;

	if (pStdin  != 0) pStdin->AddRef ();		// the child AS will release these
	if (pStdout != 0) pStdout->AddRef ();

	// Deferred load: the task reads pElfPath on its own thread. If the file is missing,
	// it marks pProc done (status -1) so a waiter unblocks -- the failure surfaces async.
	CUserProcessTask *pTask = new CUserProcessTask (pElfPath, pElfPath, CLogger::Get (),
				      pStdin, pStdout, pProc, pArgs, pCwd, nParentPid, pInfo);
	if (pTask == 0)
	{
		if (pStdin  != 0) pStdin->Release ();
		if (pStdout != 0) pStdout->Release ();
		ProcInfoFree (pInfo);
		delete pProc;
		return 0;
	}
	VLOG ("proc", LogNotice, "spawn %s (parent pid %u, deferred)", pElfPath, nParentPid);
	return pProc;
}

// Derive a short task name from an ELF path. "SD:apps/tinypad.app/main" -> the
// parent folder minus ".app" ("tinypad"); "SD:/bin/ls" -> the basename ("ls"; any
// ".ext" suffix is dropped). Falls back to "app". Writes up to nCap-1 chars into pOut.
static void NameFromPath (const char *pPath, char *pOut, unsigned nCap)
{
	if (nCap == 0) return;
	unsigned nLen = 0;
	while (pPath[nLen] != '\0') nLen++;

	// Find the last '/' and the segment after it.
	int nSlash = -1;
	for (unsigned i = 0; i < nLen; i++) if (pPath[i] == '/') nSlash = (int) i;
	const char *pSeg = pPath + nSlash + 1;	// basename ("main" or "ls")

	// If the basename is "main" (or "main.<ext>"), use the parent dir name instead.
	boolean bMain = pSeg[0] == 'm' && pSeg[1] == 'a' && pSeg[2] == 'i' && pSeg[3] == 'n'
			&& (pSeg[4] == '\0' || pSeg[4] == '.');
	const char *pStart; int nSegLen;
	if (bMain && nSlash > 0)
	{
		int nPrev = -1;
		for (int i = nSlash - 1; i >= 0; i--) if (pPath[i] == '/') { nPrev = i; break; }
		pStart = pPath + nPrev + 1;
		nSegLen = nSlash - (nPrev + 1);
		// Strip a trailing ".app".
		if (nSegLen >= 4 && pStart[nSegLen - 4] == '.' && pStart[nSegLen - 3] == 'a'
		    && pStart[nSegLen - 2] == 'p' && pStart[nSegLen - 1] == 'p')
			nSegLen -= 4;
	}
	else
	{
		pStart = pSeg;
		nSegLen = 0;
		while (pStart[nSegLen] != '\0' && pStart[nSegLen] != '.') nSegLen++;	// drop any ".ext"
	}

	if (nSegLen <= 0) { pStart = "app"; nSegLen = 3; }
	unsigned j = 0;
	for (int i = 0; i < nSegLen && j < nCap - 1; i++) pOut[j++] = pStart[i];
	pOut[j] = '\0';
}

// Run an arbitrary ELF by absolute path with an argv string. Fire-and-forget: no
// stdio streams and no CProcess handle (nothing to wait on / free), so the task
// just terminates and the reaper reclaims it. Returns TRUE if the ELF loaded.
// (Only ELFs: the formats a runner executes -- .bas, .bax... -- are resolved in user space,
// SD:/etc/runners.ini + user/launch.h, which starts the runner under the app's name.)
boolean ExecPath (const char *pElfPath, const char *pArgs, const char *pName)
{
	if (pElfPath == 0 || pElfPath[0] == '\0')
	{
		return FALSE;
	}
	char Name[40];
	if (pName != 0) { unsigned k = 0; for (; pName[k] && k < sizeof Name - 1; k++) Name[k] = pName[k]; Name[k] = '\0'; }
	else NameFromPath (pElfPath, Name, sizeof (Name));	// ("x.app/main" -> "x")
	if (!ProgramExists (pElfPath))		// fail now if missing; body read is still deferred
	{
		return FALSE;
	}
	// Deferred load (see CUserProcessTask): the task reads pElfPath on its own thread. (v75: the
	// caller's environment -- the system default when the kernel starts it, kern/procx.h.)
	TProcInfo *pInfo = ProcInfoNew (pElfPath, pArgs, TRUE);
	if (new CUserProcessTask (pElfPath, Name, CLogger::Get (), 0, 0, 0, pArgs, 0, 0, pInfo) == 0)
	{
		ProcInfoFree (pInfo);
		return FALSE;
	}
	return TRUE;
}

// Log the .app subdirectories of /apps (validates FatFs directory enumeration;
// the basis for the shell's app list later).
static void EnumerateApps (CLogger *pLogger)
{
	DIR Dir;
	if (f_opendir (&Dir, "SD:apps") != FR_OK)
	{
		pLogger->Write (FromKernel, LogWarning, "no /apps directory");
		return;
	}
	unsigned nCount = 0;
	for (;;)
	{
		FILINFO Info;
		if (f_readdir (&Dir, &Info) != FR_OK || Info.fname[0] == '\0')
		{
			break;
		}
		if (Info.fattrib & AM_DIR)
		{
			pLogger->Write (FromKernel, LogNotice, "  app dir: %s", Info.fname);
			nCount++;
		}
	}
	f_closedir (&Dir);
	pLogger->Write (FromKernel, LogNotice, "/apps: %u entries", nCount);
}

// Boot the userland: the kernel just launches the init program (PID-1 style, no
// arguments). init reads /etc/autostart and starts everything from there (see
// user/bin/init.c), so all the launch policy lives in userland, not the kernel.
//
// Which ELF to run is the cmdline.txt option "init=" (e.g. init=SD:/bin/init);
// it defaults to SD:bin/init when absent, so existing cards keep booting. This
// lets you swap the init program (a recovery shell, a different launcher) without
// rebuilding the kernel.
void CKernel::StartAutostart (void)
{
	ProcInfoBootInit ();			// (v75) the default environment: SD:/etc/environment
	OFileBootCleanup ();			// (v75) files unlinked while open that a crash left

	// cmdline netlog=1: the network's start written to SD:/netlog.txt (bin/netlog) -- for a Pi
	// without a screen; started first, so that it has the kernel log from the bring-up on
	if (m_Options.GetAppOptionDecimal ("netlog", 0) != 0 && !ExecPath ("SD:bin/netlog", ""))
	{
		m_Logger.Write (FromKernel, LogWarning, "netlog=1: cannot start SD:bin/netlog");
	}
	const char *pInit = m_Options.GetAppOptionString ("init", "SD:bin/init");
	if (!ExecPath (pInit, ""))
	{
		m_Logger.Write (FromKernel, LogWarning, "cannot start init '%s'", pInit);
	}
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	// The crash record (kern/crashlog.h): the previous session's kept aside, this one's
	// started -- before the first log line.
	CrashLogInit ();

	// Bring up the HDMI text console FIRST (like VMKernel) so the boot log is
	// visible on screen even without a serial cable.
	if (bOK)
	{
		bOK = m_Screen.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Serial.Initialize (115200);
	}

	if (bOK)
	{
		// Log through a switch: normally the HDMI boot console (m_Screen). When an
		// app exits we flip it to the framebuffer console so messages stay visible
		// after the compositor takes the display.
		m_LogSwitch.SetNormal (&m_Screen);
		DebugConsoleRegister (&m_LogSwitch, &m_FbConsole);
		bOK = m_Logger.Initialize (&m_LogSwitch);
	}

	if (bOK)
	{
		// The CPU was pinned to its max clock at construction (m_CPUThrottle).
		// Report it so a slow boot is easy to spot: it should read ~1500 MHz on a
		// Pi 4, not ~600 MHz.
		m_Logger.Write (FromKernel, LogNotice, "ARM clock: %u MHz (max %u MHz)",
				m_CPUThrottle.GetClockRate () / 1000000,
				m_CPUThrottle.GetMaxClockRate () / 1000000);

		// Firmware-reported board RAM (4096 / 8192 ...): the truth against which the
		// high-zone reclaim below is judged. If this says 8192 but "above 4GB" is 0,
		// the >4GB reclaim fell back (bug); if this says 4096, 0 above 4GB is correct.
		CMachineInfo *pMI = CMachineInfo::Get ();
		m_Logger.Write (FromKernel, LogNotice, "board RAM (firmware): %u MB",
				pMI != 0 ? pMI->GetRAMSize () : 0);

		// High-zone page allocator (app frames). SetupHighMem runs before the logger
		// exists, so report its result here: total high RAM for apps + how much was
		// reclaimed above 4GB from the device tree (0 if the board has none).
		unsigned long nHighMB = (unsigned long)
			((CMemorySystem::GetPagerHighFreeSpace () + (1024*1024 - 1)) / (1024*1024));
		unsigned long n4GMB = (unsigned long)
			((CMemorySystem::GetHighMem4GSize () + (1024*1024 - 1)) / (1024*1024));
		m_Logger.Write (FromKernel, LogNotice,
				"high page zone: %lu MB for apps across %u segment(s), %lu MB above 4GB",
				nHighMB, CMemorySystem::GetHighSegCount (), n4GMB);
	}

	if (bOK)
	{
		bOK = m_Interrupt.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Timer.Initialize ();
	}

	if (bOK)
	{
		// Take over exception handling from Circle: install our VBAR_EL1 (EL1 + EL0
		// vectors: the trap frame, the system calls), and drive the scheduler's time
		// slice from the 100 Hz timer tick -> preemptive multitasking (#4).
		install_vectors ();
		m_Timer.RegisterPeriodicHandler (PeriodicTick);

		// The EL0 controls of this core (kern/el0.h; cmdline.txt el0pmu=1: the performance
		// counters readable by the apps).
		El0ConfigurePmu (m_Options.GetAppOptionDecimal ("el0pmu", 0) != 0);
		El0CoreInit (0);

		// Per-process address spaces (#5): remember the kernel TTBR0 and switch
		// TTBR0/ASID on every task switch based on the task's address space.
		AddrSpaceInit ();
		m_Scheduler.RegisterTaskSwitchHandler (AddressSpaceTaskSwitch);
		m_Scheduler.RegisterTaskTerminationHandler (AddressSpaceTaskTerminate);

		// Publish the kapi ABI table (apps call the kernel through it). Must run
		// before any address space is built (each one maps the table).
		KApiTableInit ();
		El0Init ();			// (the EL0 table + code pages, from the kapi table)

#ifdef ARM_ALLOW_MULTI_CORE
		// Start cores 1..3 (core 1 = the sound producer). Not fatal if it fails:
		// the rest of the system only uses core 0.
		// netcore=1 (cmdline.txt): the network stack on core 3 (then not an app core)
		g_bNetCore = m_Options.GetAppOptionDecimal ("netcore", 0) != 0;
		// (diagnostics) dispdma=0: the compositor's copies to the screen synchronous (the
		// asynchronous 2D DMA off); gpudirect=0: the GPU renders into its own buffer, copied
		g_bDisplayDma = m_Options.GetAppOptionDecimal ("dispdma", 1) != 0;
		g_bGpuDirect = m_Options.GetAppOptionDecimal ("gpudirect", 1) != 0;
		s_pCores = new COnyxCores;
		if (s_pCores == 0 || !s_pCores->Initialize ())
			m_Logger.Write (FromKernel, LogWarning, "secondary cores did not start (no sound producer)");
		else
			m_Logger.Write (FromKernel, LogNotice, g_bNetCore ? "cores 1-3 started (core 1: sound, core 3: network)"
								   : "cores 1-3 started (core 1: sound)");
#endif
	}

	// Framebuffer is optional (needs an attached display). Do not fail boot if it
	// is unavailable -- the console + scheduler still run.
	if (bOK)
	{
		m_bGraphics = m_2DGraphics.Initialize ();
		if (!m_bGraphics)
		{
			m_Logger.Write (FromKernel, LogWarning, "no framebuffer/display");
		}
		else
		{
			// Route kernel panics to the displayed framebuffer (the boot console
			// stops being scanned out once the compositor takes over).
			SetPanicGraphics (&m_2DGraphics);
			g_pGraphics = &m_2DGraphics;
		}
	}

	// SD card is optional too: mount it so apps can be loaded from the card. If it
	// is absent, we fall back to the ELF images embedded in the kernel.
	if (bOK)
	{
		// cmdline.txt: sdhs=0 -> the card at 25 MHz instead of High Speed (50 MHz, the
		// default: for a card that misbehaves), sdcache=0 -> no sector cache (A/B tests).
		CEMMCDevice::SetHighSpeed (m_Options.GetAppOptionDecimal ("sdhs", 1) != 0);
		disk_cache_enable (m_Options.GetAppOptionDecimal ("sdcache", 1) != 0);
		if (m_EMMC.Initialize () && f_mount (&m_FileSystem, "SD:", 1) == FR_OK)
		{
			m_bSDMounted = TRUE;
			m_Logger.Write (FromKernel, LogNotice, "SD card mounted (SD:): %s, sector cache %s",
					CEMMCDevice::IsHighSpeed () ? "High Speed 50 MHz" : "25 MHz",
					m_Options.GetAppOptionDecimal ("sdcache", 1) ? "on" : "off");

			// the card's other partitions: each FAT / exFAT one mounted as SD1: .. SD3:
			for (int i = 1; i <= 3; i++)
			{
				char Vol[8] = { 'S', 'D', (char) ('0' + i), ':', 0 };
				if (f_mount (&m_FileSystemN[i - 1], Vol, 1) == FR_OK)
				{
					m_Logger.Write (FromKernel, LogNotice, "SD card partition %d mounted (%s)", i + 1, Vol);
				}
				else
				{
					f_mount (0, Vol, 0);	// (not FAT, or no such partition)
				}
			}

			CrashLogClockRestore ();	// the last time seen (no clock on the Pi) until NTP
			CrashLogReport ();		// the previous session, if it froze: SD:/etc/lastcrash.txt
			ReadSystemConfig ();		// SD:system.ini -> verbose flag, timezone, etc.
			m_Timer.SetTimeZone (g_nTimeZoneMin);	// local time for the clock/agenda

			// Mouse cursor: built in (BuiltinCursor), a GImage the compositor blits.
			m_WindowManager.SetCursor (BuiltinCursor ());
		}
		else
		{
			m_Logger.Write (FromKernel, LogWarning,
					"no SD card; using embedded apps");
		}
	}

	// RAM:, the RAM volume (after system.ini: its size)
	if (bOK)
	{
		RamFsInit (g_szRamFs);
	}

	// USB host (mouse + keyboard). Optional: if it fails, the GUI still runs, just
	// without input. Initialize() scans for devices already attached at boot; the
	// input thread later pumps UpdatePlugAndPlay() for hot-plug.
	if (bOK)
	{
		m_bUSB = m_USB.Initialize ();
		if (!m_bUSB)
		{
			m_Logger.Write (FromKernel, LogWarning, "no USB host; input disabled");
		}
	}

	return bOK;
}

// Print Screen pressed (s_nPrintScreen): the Screenshot app captures -- the running one is told through
// its "screenshot" service ("now", or "window <id>" with Alt: the window that has the keyboard), else
// it is started ("--now" / "--window <id>"). docs/screenshot/README.md.
static void PrintScreenPoll (void)
{
	unsigned nWhat = s_nPrintScreen;
	if (nWhat == 0) return;
	s_nPrintScreen = 0;
	unsigned nId = 0;
	CWindowManager *pWM = CWindowManager::Get ();
	if (nWhat == 2 && pWM != 0)
	{
		CWindow *List[WM_MAX_WINDOWS];
		unsigned n = pWM->Snapshot (List, WM_MAX_WINDOWS);
		for (unsigned i = 0; i < n; i++) if (pWM->HasKeyFocus (List[i])) nId = List[i]->Id ();
	}
	CString Msg, Args;
	if (nId != 0) { Msg.Format ("window %u", nId); Args.Format ("--window %u", nId); }
	else { Msg = "now"; Args = "--now"; }
	if (!IpcPost ("screenshot", 1, (const char *) Msg, Msg.GetLength () + 1))
	{
		ExecPath ("SD:apps/screenshot.app/main", (const char *) Args, "screenshot");
	}
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice, "Onyx -- a lean OS on Circle (codename Zircon)");
	m_Logger.Write (FromKernel, LogNotice,
			"Multi-process kernel + GUI, apps at EL0 calling the kernel by system calls");
	m_Logger.Write (FromKernel, LogNotice, "Compiled on " __DATE__ " " __TIME__);

	CMachineInfo *pInfo = CMachineInfo::Get ();
	m_Logger.Write (FromKernel, LogNotice, "Running on %s, %lu MB RAM",
			pInfo->GetMachineName (),
			(unsigned long) (CMemorySystem::Get ()->GetMemSize () / 0x100000));

	// Launch the autostart apps (each one a process at EL0, kern/el0.h).
	// They create their windows and draw into the shared canvas; the compositor is
	// started AFTER a readable pause so the boot log stays on screen first.
	if (m_bGraphics)
	{
		if (!m_bSDMounted)
		{
			m_Logger.Write (FromKernel, LogError, "no SD card: cannot launch apps");
		}
		else
		{
			EnumerateApps (&m_Logger);

			// Restore the saved scroll-wheel speed (theme editor persists it).
			unsigned nThemeSize = 0;
			u8 *pTheme = LoadFileFromSD ("SD:/etc/theme.txt", &nThemeSize);
			if (pTheme != 0)
			{
				int nSpeed = ParseWheelSpeed (pTheme, nThemeSize);
				if (nSpeed > 0)
				{
					m_WindowManager.SetWheelSpeed (nSpeed);
					m_Logger.Write (FromKernel, LogNotice,
							"wheel speed: %d lines/notch", nSpeed);
				}
				delete [] pTheme;
			}
		}

		// Reaper: reclaims ended apps; it also blinks the ACT LED (headless sign of
		// life from the very start of the userland).
		new CReaperTask;
		// the hang watchdog (fed by the reaper): cmdline.txt hangreboot= seconds, 0 = off
		CrashLogStartWatchdog (m_Options.GetAppOptionDecimal ("hangreboot", 15));
		m_Logger.Write (FromKernel, LogNotice, "reaper started");

		// Input: pumps USB plug-and-play, so the keyboard/mouse enumerate while the
		// userland starts; the layout `keyb` records is installed when the keyboard
		// attaches.
		if (m_bUSB)
		{
			new CInputTask (&m_USB, &m_DeviceNameService,
					m_2DGraphics.GetDisplay (), &m_Logger);
			m_Logger.Write (FromKernel, LogNotice, "input started");
		}

		// Compositor BEFORE the userland, run at once (YieldTo) so it exists and
		// presents before the first app's window. (There used to be a 6 s pause here
		// to keep the HDMI boot log readable; starting the compositor late, with a
		// single GUI app already running, hung the boot -- the old voronoy-masked race.)
		CCompositorTask *pCompositor = new CCompositorTask (&m_2DGraphics, &m_WindowManager);
		s_bCompositor = TRUE;
		m_Logger.Write (FromKernel, LogNotice, "compositor started");
		m_Scheduler.YieldTo (pCompositor);
		m_LogSwitch.MuteNormal ();		// the boot console is hidden now (see MuteNormal)

		// GUI watchdog + heartbeat (kmsg): compositor stalls, frozen apps, and a
		// periodic summary every cmdline.txt heartbeat= seconds (default 5, 0 = off).
		// cmdline watchdog=0 skips the task entirely (A/B testing).
		// Scheduler tuning (A/B testing): slice= app time slice in 10 ms ticks,
		// hogsched=0 disables the CPU-hog detection / bursts (plain round-robin).
		{
			unsigned nSlice = m_Options.GetAppOptionDecimal ("slice", SCHED_SLICE_TICKS);
			boolean bHog = m_Options.GetAppOptionDecimal ("hogsched", 1) != 0;
			m_Scheduler.Configure (nSlice, bHog);
			m_Logger.Write (FromKernel, LogNotice, "scheduler: slice %u ticks, hog detection %s",
					nSlice, bHog ? "on" : "off");
		}
		unsigned nBeat = m_Options.GetAppOptionDecimal ("heartbeat", 5);
		g_nHeartbeatSec = nBeat == (unsigned) -1 ? 5 : nBeat;
		if (m_Options.GetAppOptionDecimal ("watchdog", 1) != 0)
		{
			new CGuiWatchdogTask (&m_WindowManager);
			m_Logger.Write (FromKernel, LogNotice, "gui watchdog started (heartbeat %u s)",
					g_nHeartbeatSec);
		}
		else
		{
			m_Logger.Write (FromKernel, LogNotice, "gui watchdog disabled (cmdline watchdog=0)");
		}

		if (m_bSDMounted)
		{
			StartAutostart ();		// spawn the init program (cmdline init=)
		}
	}
	else
	{
		m_Logger.Write (FromKernel, LogWarning, "no framebuffer; idling");
	}

	// Network: bring up WLAN + TCP/IP in the background. Needs the SD card (WLAN
	// firmware + wpa_supplicant.conf live there). Fully non-fatal -- the GUI runs
	// whether or not WiFi associates; apps test NetIsUp() before using sockets.
	if (m_bSDMounted)
	{
		if (g_szHostname[0]) m_Net.SetHostname (g_szHostname);	// (system.ini: before DHCP starts)
		if (g_bNetCore)
		{
			s_pNetWLAN = &m_WLAN; s_pNetNet = &m_Net; s_pNetWPA = &m_WPASupplicant; s_pNetLogger = &m_Logger;
			NetCoreStart (NewNetBringupTask);
			m_Logger.Write (FromKernel, LogNotice, "net: bring-up started on core 3");
		}
		else
		{
			new CNetBringupTask (&m_WLAN, &m_Net, &m_WPASupplicant, &m_Logger);
			m_Logger.Write (FromKernel, LogNotice, "net: bring-up task started");
		}
	}

	// The "main" task has nothing left to do; reaping is the dedicated reaper task's
	// job now. Just idle.
	for (unsigned nTick = 0; ; nTick++)
	{
		m_Scheduler.MsSleep (50);
		PrintScreenPoll ();			// (Print Screen: the Screenshot app)
		if (nTick % 5 == 4) NetCorePoll ();	// the network core's notices (IpcNotify), every 250 ms
	}

	return ShutdownHalt;
}
