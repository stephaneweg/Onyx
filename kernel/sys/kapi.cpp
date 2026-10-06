//
// kapi.cpp
//
// Kernel API: the kapi_* functions behind the kapi table (kern/kapi_abi.h). Apps run at EL0
// and reach them by system calls (kern/el0.h: the EL0 table's stub for slot n does "svc #0" with
// n in x8, El0SyncHandler calls the kernel table's entry n). They run in the calling task --
// at EL1, on its kernel stack, with the app's address space active -- so the arguments are plain
// pointers in that space, checked on entry and copied fault-safe (kern/uaccess.h; see "the
// app's pointers" below). The kernel's own tasks call some of them directly.
//
// extern "C": stable, unmangled names (kapitable.cpp fills the table with them).
//
#include <kern/crashlog.h>
#include <kern/vfs.h>
#include <kern/wsrv.h>		// (v89) the graphics server: the display, the input may be its own
#include <kern/ramfs.h>		// RAM:, the RAM volume
#include <kern/sound.h>
#include <kern/addrspace.h>
#include <kern/applaunch.h>
#include <kern/stream.h>		// CStream / CPipeStream / CFileStream / CProcess
#include <kern/handle.h>		// the per-process opaque handles (files, dirs, streams, procs)
#include <kern/kapi_abi.h>		// struct kapi_dirent
#include <kern/layout.h>
#include <kern/gui/window.h>
#include <kern/gui/surface.h>		// CSurface / CSurfaceManager (shell surfaces)
#include <kern/appcore.h>		// AppCoreBusyUs (v80 cpu_stats)
#include <kern/net.h>		// NetTcpConnect/Send/Recv/Close/Status (socket backend)
#include <kern/debugcon.h>
#include <kern/gui/gimage.h>
#include <kern/thread.h>		// (v67) threads, posts: ThreadsEndProcess
#include <kern/uaccess.h>		// the app's pointers: checked, copied fault-safe
#include <kern/ofile.h>		// (v75) ResolvePath & co. shared with sys/ofile.cpp, OFileNoteDir
#include <kern/image.h>		// (v77) program images: the image kapis, ImageFileChanged
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/timer.h>
#include <circle/time.h>
#include <circle/logger.h>
#include <circle/string.h>
#include <circle/new.h>
#include <circle/util.h>
#include <circle/startup.h>		// reboot() (kapi_reboot)
#include <circle/memory.h>		// CMemorySystem (meminfo)
#include <circle/machineinfo.h>		// CMachineInfo::GetRAMSize (firmware board RAM)
#include <circle/actled.h>		// kapi_shutdown: LED off
#include <circle/2dgraphics.h>		// kapi_present_fb
#include <circle/bcmframebuffer.h>		// kapi_fullscreen_direct
#include <circle/bcmpropertytags.h>	// (v69) kapi_screen_native: the EDID
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

// The calling task's current working directory (FatFs absolute path), or root.
const char *CurCwd (void)					// (kern/ofile.h: sys/procx.cpp uses it too)
{
	CAddressSpace *pAS = CurrentAS ();
	return (pAS != 0) ? pAS->GetCwd () : "SD:/";
}

// The volume prefix of a path ("SD:", "SD1:", "USB:"... letters then letters / digits, then
// ':'): its length with the ':', 0 if none.
static unsigned VolumePrefix (const char *p)
{
	auto alpha = [] (char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
	if (!alpha (p[0])) return 0;
	unsigned i = 1;
	while (alpha (p[i]) || (p[i] >= '0' && p[i] <= '9')) i++;
	return p[i] == ':' ? i + 1 : 0;
}

// Resolve an app-supplied path to a clean absolute FatFs path in pOut. A path that
// starts with a volume ("SD:", "SD1:"...: the SD card's partitions; "SD0:" is "SD:", the
// first) is taken as absolute; "/x" is relative to the current volume's root; any other
// form (incl. "./x", "../x", "x") is relative to the current working dir. ".", ".." and
// redundant slashes are normalised away. Lets ls/cat/redirection/etc. use relative or
// absolute paths transparently.
void ResolvePath (const char *pIn, char *pOut, unsigned nCap)		// (kern/ofile.h: v75 uses it too)
{
	if (pIn == 0 || nCap < 8) { if (nCap) pOut[0] = '\0'; return; }

	char raw[512];
	unsigned r = 0;
	auto put = [&] (const char *p, unsigned n) { for (unsigned i = 0; i < n && p[i] != '\0' && r < sizeof (raw) - 1; i++) raw[r++] = p[i]; };
	auto putVolume = [&] (const char *p, unsigned n)	// (upper case; SD0: -> SD:)
	{
		if (n == 4 && (p[0] == 'S' || p[0] == 's') && (p[1] == 'D' || p[1] == 'd') && p[2] == '0') { put ("SD:", 3); return; }
		for (unsigned i = 0; i < n && r < sizeof (raw) - 1; i++) raw[r++] = p[i] >= 'a' && p[i] <= 'z' ? (char) (p[i] - 32) : p[i];
	};
	const char *cwd = CurCwd ();
	unsigned nIn = VolumePrefix (pIn);
	if (nIn)							// already absolute
	{
		putVolume (pIn, nIn);
		put (pIn + nIn, ~0u);
	}
	else if (pIn[0] == '/')						// the current volume's root
	{
		unsigned n = VolumePrefix (cwd);
		if (n) put (cwd, n); else put ("SD:", 3);
		put (pIn, ~0u);
	}
	else								// relative to cwd
	{
		put (cwd, ~0u);
		if (r < sizeof (raw) - 1) raw[r++] = '/';
		put (pIn, ~0u);
	}
	raw[r] = '\0';

	// Normalise the part after the volume: process '.', '..' and collapse '/' runs.
	unsigned nVol = VolumePrefix (raw);
	unsigned starts[64]; int depth = 0;
	unsigned o = 0;
	for (unsigned k = 0; k < nVol && o < nCap - 1; k++) pOut[o++] = raw[k];
	unsigned i = nVol;
	while (raw[i] != '\0')
	{
		while (raw[i] == '/') i++;
		if (raw[i] == '\0') break;
		unsigned j = i;
		while (raw[j] != '\0' && raw[j] != '/') j++;
		unsigned len = j - i;
		if (len == 1 && raw[i] == '.')
		{
			// "." : stay
		}
		else if (len == 2 && raw[i] == '.' && raw[i + 1] == '.')
		{
			if (depth > 0) o = starts[--depth];	// pop one component
		}
		else
		{
			if (depth < 64) starts[depth++] = o;	// remember this component's start
			if (o < nCap - 1) pOut[o++] = '/';
			for (unsigned k = i; k < j && o < nCap - 1; k++) pOut[o++] = raw[k];
		}
		i = j;
	}
	if (o == nVol && o < nCap - 1) pOut[o++] = '/';	// nothing left => the volume's root "SD1:/"
	pOut[o] = '\0';
}

// ---- the app's pointers (kern/uaccess.h) --------------------------------------------------------
//
// Every pointer an app passes is checked on entry -- in the user VA range, mapped -- and a kapi given a bad one fails with its usual error
// value instead of faulting in the kernel. Strings and small structures are copied into the
// kernel once (CUserStr, UserGet / UserPut: fault-safe, what is checked is what is used), the
// buffers the kernel reads or fills in place are probed first (UserReadable / UserWritable).
// The checks are in the kapi_* entry points only: the helpers below them (CreateWindow,
// ResolvePath...) take kernel memory, and the kernel calls them with its own.

// An optional out-parameter (0: not wanted): FALSE if it is given but not the caller's.
template <class T> static inline boolean OutOK (T *p)
{
	return p == 0 || UserRange (p, sizeof (T));
}

// Store an optional out-parameter (checked on entry with OutOK; a fault: not stored).
template <class T> static inline void OutPut (T *p, const T &Value)
{
	if (p != 0) UserPut (p, Value);
}

extern "C" {

// --- windowing ---------------------------------------------------------------

// The title: a copy (a window's holds 47 characters; a longer one is cut, as before).
#define TITLE_MAX	64

// Draw text into the calling app's window canvas using the kernel bitmap font
// (transparent background -- only glyph pixels are written). Apps have no font of
// their own, so this is how an app-drawn UI (e.g. the editor) renders text.
// (a text drawn: one line, what is past 64 K characters cut)
#define DRAW_TEXT_MAX	0x10000

// Draw kernel-font text (transparent background) into an arbitrary app-mapped
// 0x00RRGGBB buffer (dst, dstW x dstH) at (x,y). Lets a user-side toolkit render text
// into surfaces other than the main canvas -- e.g. the window-chrome buffers (the
// kernel bitmap font is the only font apps have). dst must be the caller's, mapped writable.
void kapi_draw_text_buf (unsigned *dst, int dstW, int dstH, int x, int y,
			 const char *pStr, unsigned nColor)
{
	if (dst == 0 || pStr == 0 || dstW <= 0 || dstH <= 0)
	{
		return;
	}
	u64 nBytes = (u64) dstW * dstH * sizeof (u32);
	if (!UserWritable (dst, nBytes))
	{
		return;
	}
	CUserStr Text (pStr, DRAW_TEXT_MAX, TRUE);
	if (!Text.OK ())
	{
		return;
	}
	GImage Img ((u32 *) dst, dstW, dstH);
	Img.DrawText (x, y, Text.Get (), (u32) nColor);
}

int kapi_font_width  (void) { return GImage::FontWidth (); }	// glyph cell width
int kapi_font_height (void) { return GImage::FontHeight (); }	// glyph cell height

// Launch another app by folder name (apps/<name>.app/main) as a new process.
// Used by the shell (panel / app-list popup). Returns 1 on success, 0 on failure.
int kapi_launch (const char *pName)
{
	CUserStr Name (pName);
	return Name.OK () && LaunchAppByName (Name.Get ()) ? 1 : 0;
}

// An argv string: the child keeps 1023 characters (CAddressSpace::SetArgs), a longer one is cut.
#define ARGS_MAX	1024

// Run an ELF by absolute path with an argv string (e.g. the file manager opening a
// document in an editor, or launching a program). Fire-and-forget.
int kapi_exec (const char *pPath, const char *pArgs)
{
	CUserStr Path (pPath, UPATH_MAX), Args (pArgs, ARGS_MAX, TRUE);
	if (!Path.OK () || (!Args.OK () && !Args.IsNull ())) return 0;
	return ExecPath (Path.Get (), Args.OK () ? Args.Get () : "") ? 1 : 0;
}
// v49: the same, the process named pName (a runner running an app: named after the app).
int kapi_exec_as (const char *pPath, const char *pArgs, const char *pName)
{
	CUserStr Path (pPath, UPATH_MAX), Args (pArgs, ARGS_MAX, TRUE), Name (pName, 64, TRUE);
	if (!Path.OK () || (!Args.OK () && !Args.IsNull ()) || (!Name.OK () && !Name.IsNull ())) return 0;
	const char *p = Name.OK () && Name.Get ()[0] ? Name.Get () : 0;
	return ExecPath (Path.Get (), Args.OK () ? Args.Get () : "", p) ? 1 : 0;
}

// --- v77: program images (kern/image.h) ---------------------------------------
// A program file is loaded once and its read-only segments shared by its processes; the key is the
// program's canonical path (ImageCanonPath: relative to the caller's working directory, lower
// case). These three calls are /bin/preload's, /bin/unload's and pkg's.

// The program loaded ahead and kept: a kernel task reads it, the call returns at once.
int kapi_image_preload (const char *pPath)
{
	CUserStr Path (pPath, UPATH_MAX);
	if (!Path.OK ()) return -KAPI_EFAULT;
	char Canon[IMG_PATH_MAX];
	if (!ImageCanonPath (Path.Get (), CurCwd (), Canon)) return Path.Get ()[0] ? -KAPI_ENAMETOOLONG : -KAPI_ENOENT;
	return ProgramPreload (Canon);
}

// Its image loses its pin and its name: freed with its last process.
int kapi_image_unload (const char *pPath)
{
	CUserStr Path (pPath, UPATH_MAX);
	if (!Path.OK ()) return -KAPI_EFAULT;
	return ImageUnload (Path.Get (), CurCwd ());
}

// The live images (pPath 0), or the one a run of pPath would map.
int kapi_image_list (const char *pPath, struct kapi_image_info *pOut, unsigned nCap)
{
	if (pPath != 0)
	{
		CUserStr Path (pPath, UPATH_MAX);
		if (!Path.OK ()) return -KAPI_EFAULT;
		struct kapi_image_info Info;
		if (ImageList (Path.Get (), CurCwd (), &Info, 1) == 0) return 0;
		if (nCap > 0 && !UserPut (pOut, Info)) return -KAPI_EFAULT;
		return 1;
	}
	unsigned n = ImageList (0, 0, 0, 0);
	if (n == 0 || nCap == 0) return (int) n;
	if (nCap > n) nCap = n;
	struct kapi_image_info *pList = new struct kapi_image_info[nCap];	// (made here, copied out whole)
	if (pList == 0) return -KAPI_ENOMEM;
	n = ImageList (0, 0, pList, nCap);
	boolean bOK = UserCopyOut (pOut, pList, (u64) (n < nCap ? n : nCap) * sizeof (struct kapi_image_info));
	delete [] pList;
	return bOK ? (int) n : -KAPI_EFAULT;
}

// --- v83: shared libraries (kern/image.h, docs/SHARED-LIBS-PLAN.md) -------------
// The library mapped into the caller -> its export table (0: *pErr says why). A bare name is
// SD:/lib/<name>.so; anything with a '/', a '\\' or a ':' is a path.
const void *kapi_lib_open (const char *pName, unsigned nMinVersion, int *pErr)
{
	int nErr = 0;
	u64 ulTable = 0;
	CUserStr Name (pName, UPATH_MAX);
	CAddressSpace *pAS = CurrentAS ();
	if (!Name.OK ()) nErr = -KAPI_EFAULT;
	else if (pAS == 0 || Name.Get ()[0] == '\0') nErr = -KAPI_EINVAL;
	else
	{
		const char *p = Name.Get ();
		boolean bPath = FALSE;
		for (const char *q = p; *q != '\0'; q++) if (*q == '/' || *q == '\\' || *q == ':') bPath = TRUE;
		CString Path;
		if (bPath) Path = p; else Path.Format ("SD:/lib/%s.so", p);
		char Canon[IMG_PATH_MAX];
		if (!ImageCanonPath (Path, CurCwd (), Canon)) nErr = -KAPI_ENAMETOOLONG;
		else nErr = LibraryOpen (Canon, nMinVersion, pAS, &ulTable);
	}
	if (pErr != 0 && !UserPut (pErr, nErr)) return 0;
	return nErr < 0 ? 0 : (const void *) (uintptr) ulTable;
}

// --- v79: what the kernel is (/bin/uname) --------------------------------------
// "key value" lines. The build's date and revision are buildstamp.cpp's: compiled again at every
// link (kernel/Makefile), so they are this image's, whichever file changed.
extern const char g_BuildStamp[], g_BuildRev[];
int kapi_kernel_info (char *pBuf, unsigned nCap)
{
	CString Text;
	Text.Format ("name Onyx\nabi %u\nbuilt %s\nrev %s\nmachine aarch64\nmodel %s\nram %u\n",
		     (unsigned) KAPI_ABI_VERSION, g_BuildStamp, g_BuildRev,
		     CMachineInfo::Get ()->GetMachineName (), (unsigned) CMachineInfo::Get ()->GetRAMSize ());
	unsigned n = Text.GetLength ();
	if (nCap == 0) return (int) n;
	unsigned k = n < nCap - 1 ? n : nCap - 1;
	char cEnd = '\0';
	if (!UserCopyOut (pBuf, (const char *) Text, k) || !UserCopyOut (pBuf + k, &cEnd, 1)) return -KAPI_EFAULT;
	return (int) n;
}

// (v80) The cores: what each does and how long it was busy. Core 0 (and the network's, netcore=1)
// have a scheduler: the time its tasks ran, the idle task apart -- the network core's tasks wait by
// yielding (it polls the Wi-Fi chip): busy while the network works, asleep between two questions to
// the chip once it is quiet (sys/net.cpp, NetCoreMain: that sleep is taken off). Core 1: the time it rendered sound.
// An app core: the time its jobs ran.
#ifdef ARM_ALLOW_MULTI_CORE
extern "C++" { u64 SoundCoreBusyUs (void); }			// (sys/sound.cpp: core 1's rendering time)
#endif
int kapi_cpu_stats (struct kapi_cpu_stats *pOut)
{
	struct kapi_cpu_stats Out;
	memset (&Out, 0, sizeof Out);
	u64 c, f;
	asm volatile ("mrs %0, cntpct_el0" : "=r" (c));
	asm volatile ("mrs %0, cntfrq_el0" : "=r" (f));
	Out.now_us = f != 0 ? c / f * 1000000 + c % f * 1000000 / f : 0;
#ifdef ARM_ALLOW_MULTI_CORE
	Out.cores = CORES < KAPI_CPU_CORES ? CORES : KAPI_CPU_CORES;
#else
	Out.cores = 1;
#endif
	for (unsigned n = 0; n < Out.cores; n++)
	{
		struct kapi_cpu_core &C = Out.core[n];
		CScheduler *pSched = CScheduler::OfCore (n);
		if (n == 0) { C.role = KAPI_CORE_SYSTEM; C.busy_us = pSched != 0 ? pSched->GetBusyUs () : 0; }
#ifdef ARM_ALLOW_MULTI_CORE
		else if (n == 1) { C.role = KAPI_CORE_SOUND; C.busy_us = SoundCoreBusyUs (); }
		else if (pSched != 0) { C.role = KAPI_CORE_NETWORK; C.busy_us = pSched->GetBusyUs (); }
		else { C.role = KAPI_CORE_APP; C.busy_us = AppCoreBusyUs (n, &C.pid); }
#endif
	}
	return UserCopyOut (pOut, &Out, sizeof Out) ? 0 : -KAPI_EFAULT;
}

int kapi_net_stats (int nPid, struct kapi_net_stats *pOut)
{
	struct kapi_net_stats Out;
	memset (&Out, 0, sizeof Out);
	u64 ulRx = 0, ulTx = 0;
	NetStats (nPid > 0 ? (unsigned) nPid : 0, &ulRx, &ulTx, &Out.sockets);
	Out.rx_bytes = ulRx; Out.tx_bytes = ulTx;
	return UserCopyOut (pOut, &Out, sizeof Out) ? 0 : -KAPI_EFAULT;
}

// Framebuffer size, for edge-pinned borderless windows (the dock, the menu bar).
void kapi_screen_size (int *pW, int *pH)
{
	if (OutOK (pW)) OutPut (pW, g_nScreenWidth);
	if (OutOK (pH)) OutPut (pH, g_nScreenHeight);
}

extern C2DGraphics *g_pGraphics;
// v55: the full-screen window drawing straight into the displayed framebuffer (none: 0)
static CWindow *s_pDirectWin = 0;
static boolean FsDirect (CWindowManager *pWM)
{
	return s_pDirectWin != 0 && pWM->FullscreenWindow () == s_pDirectWin;
}

// Map the displayed framebuffer into pAS at USER_FULLSCREEN_SCREEN (normal uncached: the
// kernel's own map of it is Device memory, slow to read); its first pixel's VA, 0 if none.
static unsigned *MapScreen (CAddressSpace *pAS, unsigned *pPitch)
{
	CBcmFrameBuffer *pFB = g_pGraphics != 0 && pAS != 0 ? (CBcmFrameBuffer *) g_pGraphics->GetDisplay () : 0;
	if (pFB == 0 || pFB->GetDepth () != 32 || pFB->GetBuffer () == 0)
	{
		return 0;
	}
	u64 ulPhys = pFB->GetBuffer (), ulBase = ulPhys & ~(u64) (KPAGE_SIZE - 1);
	u64 ulEnd = ulPhys + (u64) pFB->GetPitch () * pFB->GetHeight ();
	unsigned nPages = (unsigned) ((ulEnd - ulBase + KPAGE_SIZE - 1) / KPAGE_SIZE);
	TKPageAttr Attr = KPAGE_ATTR_APP_SCREEN;
	pAS->MapContig (USER_FULLSCREEN_SCREEN, ulBase, nPages, Attr);
	DataSyncBarrier ();
	if (pPitch != 0) *pPitch = pFB->GetPitch ();
	return (unsigned *) (USER_FULLSCREEN_SCREEN + (ulPhys - ulBase));
}

// --- v38: remote screen (vncd) ----------------------------------------------
// Composite the current screen (windows + wallpaper + cursor, exactly what the
// compositor shows) straight into the caller's buffer of w*h 0x00RRGGBB pixels. w/h
// must equal the screen size (kapi_screen_size). Returns 1, or 0 on a size mismatch.
int kapi_screen_grab (unsigned *pDst, int nW, int nH)
{
	CWindowManager *pWM = CWindowManager::Get ();
	if (   pWM == 0 || pDst == 0 || nW != g_nScreenWidth || nH != g_nScreenHeight
	    || !UserWritable (pDst, (u64) nW * nH * 4))
	{
		return 0;
	}
	// Nothing changed since the previous grab into this same buffer: say so (2) and
	// leave it as is -- vncd then skips the diff / encode entirely.
	static unsigned s_nGen = 0; static unsigned *s_pLast = 0;
	unsigned nGen = g_nScreenGen;
	if (nGen == s_nGen && pDst == s_pLast)
	{
		return 2;
	}
	s_nGen = nGen; s_pLast = pDst;
	if (WsDisplayOwned () && pWM->FullscreenWindow () == 0 && g_pGraphics != 0
	    && (int) g_pGraphics->GetWidth () == nW && (int) g_pGraphics->GetHeight () == nH)
	{							// the graphics server's screen: the off-screen buffer
		memcpy (pDst, g_pGraphics->GetBuffer (), (size_t) nW * nH * 4);
		return 1;
	}
	unsigned nPitch = 0; const u8 *pSrc;
	if (FsDirect (pWM) && (pSrc = (const u8 *) MapScreen (CurrentAS (), &nPitch)) != 0)	// the screen itself
	{								// (mapped in the grabber, uncached)
		for (int y = 0; y < nH; y++) memcpy (pDst + (size_t) y * nW, pSrc + (size_t) y * nPitch, (size_t) nW * 4);
		return 1;
	}
	if (pWM->FullscreenWindow () != 0 && pWM->FullscreenBuffer () != 0)
	{
		memcpy (pDst, pWM->FullscreenBuffer (), (size_t) nW * nH * 4);	// what is shown
		return 1;
	}
	return 0;				// (no graphics server has the display: nothing is shown)
}

// Inject pointer input as if it came from a USB mouse: absolute screen position,
// buttons bit0 left / bit1 right / bit2 middle, wheel = signed notches (0 = none).
void kapi_inject_pointer (int x, int y, unsigned nButtons, int nWheel)
{
	CWindowManager *pWM = CWindowManager::Get ();
	if (pWM == 0)
	{
		return;
	}
	if (WsInputPointer (x, y, nButtons, nWheel))		// (the graphics server's: kern/wsrv.h)
	{
		// the server now, not after the injector's time slice: it moves the pointer, the window
		// dragged, and shows them before the remote desktop grabs the screen again
		if (CScheduler::IsActive ()) CScheduler::Get ()->Yield ();
	}
}

// Inject keyboard input as if typed: a key string in the keyboard's cooked format
// (characters, '\n' = Enter, '\b' = Backspace, VT100 escapes for arrows/Home/...).
void kapi_inject_key (const char *pKeys)
{
	CUserStr Keys (pKeys, 4096, TRUE);
	if (Keys.OK ()) WsInputKey (Keys.Get ());		// (the graphics server's: kern/wsrv.h)
}

// ---- shell surfaces (ABI v35) ----------------------------------------------
// Shared pixel surfaces for the activity-shell compositor: the shell creates one
// (sized to a viewport), passes its id to an app, both map it (same frames, own VA),
// the app draws + presents, the shell composites. See kern/gui/surface.h.

int kapi_surface_create (int w, int h)
{
	CAddressSpace *pAS = CurrentAS ();
	if (pAS == 0 || CSurfaceManager::Get () == 0)
	{
		return 0;
	}
	return CSurfaceManager::Get ()->Create (w, h, pAS->GetPid ());
}

unsigned *kapi_surface_map (int id)
{
	CAddressSpace *pAS = CurrentAS ();
	if (pAS == 0 || CSurfaceManager::Get () == 0)
	{
		return 0;
	}
	CSurface *pS = CSurfaceManager::Get ()->Find (id);
	if (pS == 0)
	{
		return 0;
	}
	pS->AddUser (pAS->GetPid ());		// (its frames kept while this process lives: v65)
	return (unsigned *) pAS->MapSurface (pS->Phys (), pS->Pages ());
}

int kapi_surface_size (int id, int *pW, int *pH)
{
	if (CSurfaceManager::Get () == 0 || !OutOK (pW) || !OutOK (pH))
	{
		return 0;
	}
	CSurface *pS = CSurfaceManager::Get ()->Find (id);
	if (pS == 0)
	{
		return 0;
	}
	OutPut (pW, (int) pS->Width ());
	OutPut (pH, (int) pS->Height ());
	return 1;
}

void kapi_surface_present (int id)
{
	(void) id;
	// Phase 1: the shell composites surfaces from its own loop, so present just yields
	// (like kapi_present) to hand the CPU to the shell promptly. A targeted "surface
	// ready" notify rides the IPC layer (an app may also send a present message).
	if (CScheduler::IsActive ())
	{
		CScheduler::Get ()->Yield ();
	}
}

int kapi_surface_destroy (int id)
{
	if (CSurfaceManager::Get () == 0)
	{
		return 0;
	}
	CSurface *pS = CSurfaceManager::Get ()->Find (id);
	if (pS == 0)
	{
		return 0;
	}
	CAddressSpace *pAS = CurrentAS ();		// only the owner may destroy it
	if (pAS != 0 && pS->OwnerPid () != pAS->GetPid ())
	{
		return 0;
	}
	CSurfaceManager::Get ()->Destroy (id);
	return 1;
}

// (v73) The kernel half of the event pump: the next event, its handler NOT called -- the
// user-side pump_events (the EL0 table's, kern/el0.h) calls it, at EL0.
int kapi_pop_event (struct kapi_event *pEv)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin == 0 || pEv == 0 || !UserRange (pEv, sizeof *pEv))	// (checked before an event
	{								// is taken off the queue)
		return 0;
	}
	GUIEvent Ev;
	if (!pWin->PopEvent (&Ev))
	{
		return 0;
	}
	struct kapi_event E;
	memset (&E, 0, sizeof E);
	E.handler = Ev.ulHandler;
	E.sender = Ev.ulSender;
	E.value = Ev.lValue;
	E.event = Ev.nEvent;
	E.mods = Ev.nMods;
	return UserPut (pEv, E) ? 1 : 0;
}

// (v73) What kapi_get_modifiers reports while a key handler runs: set by the user-side pump
// around the call; the previous value back.
unsigned kapi_event_mods (unsigned nMods)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin == 0)
	{
		return 0xFFFFFFFF;
	}
	unsigned nPrev = pWin->m_nKeyEventMods;
	pWin->m_nKeyEventMods = nMods;
	return nPrev;
}

int kapi_should_exit (void)
{
	CAddressSpace *pAS = CurrentAS ();
	if (pAS == 0)
	{
		return 1;
	}
	CWindow *pWin = pAS->GetWindow ();
	return (pWin != 0 && pWin->ShouldExit ()) ? 1 : 0;
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

void kapi_exit (int nStatus)
{
	// Detach the window from the compositor NOW, in the app's own context (IRQs
	// enabled), so the compositor stops drawing it the instant the app exits --
	// well before the janitor reaps the address space. This closes the window
	// immediately and removes any chance of the compositor touching a window that
	// belongs to a task being torn down.
	// ISOLATION TEST (user's idea): on exit, ONLY remove the window from the GUI and
	// stop scheduling this task. Do NOT reap/free anything (no delete pTask, no AS
	// free) and do NOT take over the screen -- the compositor keeps running. If the
	// system stays alive afterwards, the teardown/reap is what breaks the IRQ; if it
	// still hangs, the Terminate/Yield itself is the cause. The AS/window/task leak
	// for now (bounded, one per closed app).
	CAddressSpace *pAS = CurrentAS ();
	if (pAS != 0)
	{
		pAS->SetExitStatus (nStatus);		// surfaced to a waiter via kapi_wait
		CWindow *pWin = pAS->GetWindow ();
		if (pWin != 0 && CWindowManager::Get () != 0)
		{
			CWindowManager::Get ()->Remove (pWin);	// vanish from the compositor
		}
	}

	ThreadsEndProcess ();				// (v67) its other threads end with it

	// Its TCP sockets closed now, not when the janitor reaps it (after all its tasks
	// are gone): the kernel has MAX_SOCKETS in all, and a browser relaunched at once
	// found them still held by the one closing -- its pages waited for sockets (10 s).
	// NetCloseByPid waits for nothing; the reaper's call later finds none left.
	if (pAS != 0)
	{
		NetCloseByPid (pAS->GetPid ());
	}

	// Leave the app's page table before terminating (kernel code on the kernel
	// stack from here on).
	ActivateKernelAddressSpace ();

	if (CScheduler::IsActive ())
	{
		// Terminated => GetNextTask() skips it forever (never scheduled again).
		// Nothing reaps it -> it just sits there. No teardown at all.
		CScheduler::Get ()->GetCurrentTask ()->Terminate ();
	}
	for (;;) { }						// not reached
}

// --- app enumeration + clock -------------------------------------------------

// List installed apps: write each app's folder basename (the "xxx" of "xxx.app")
// under /apps into pBuf, one per line ('\n'-separated, NUL-terminated). Returns the
// number of apps found (some may be omitted if pBuf is too small). The shell uses
// this for the app-list popup.
int kapi_list_apps (char *pBuf, unsigned nBufSize)
{
	if (pBuf == 0 || nBufSize == 0 || !UserWritable (pBuf, nBufSize))
	{
		return 0;
	}
	pBuf[0] = '\0';

	DIR Dir;
	if (f_opendir (&Dir, "SD:apps") != FR_OK)
	{
		return 0;
	}

	unsigned nPos = 0, nCount = 0;
	for (;;)
	{
		FILINFO Info;
		if (f_readdir (&Dir, &Info) != FR_OK || Info.fname[0] == '\0')
		{
			break;
		}
		if (!(Info.fattrib & AM_DIR))
		{
			continue;
		}

		// Copy the name and strip a trailing ".app"; skip non-".app" dirs.
		char Name[64];
		unsigned k = 0;
		for (; Info.fname[k] != '\0' && k < sizeof (Name) - 1; k++)
		{
			Name[k] = Info.fname[k];
		}
		Name[k] = '\0';
		if (k < 4 || Name[k-4] != '.' || Name[k-3] != 'a'
		    || Name[k-2] != 'p' || Name[k-1] != 'p')
		{
			continue;
		}
		Name[k-4] = '\0';

		for (unsigned j = 0; Name[j] != '\0'; j++)
		{
			if (nPos + 2 < nBufSize)
			{
				pBuf[nPos++] = Name[j];
			}
		}
		if (nPos + 1 < nBufSize)
		{
			pBuf[nPos++] = '\n';
		}
		nCount++;
	}

	f_closedir (&Dir);
	pBuf[nPos] = '\0';
	return (int) nCount;
}

// List currently-open apps: the folder name of every non-terminated task that owns
// a window, one per '\n'-separated line in pBuf. Backs the panel's taskbar section.
// Windows created with WIN_FLAG_SYSTEM (menu bar, notifications, panel...) are skipped.
struct WinListCtx { char *pBuf; unsigned nSize; unsigned nPos; int nCount; };

// List ALL tasks for the task manager: one line per task "<state><kind> <name>",
// where state is R/S/B/N, and kind is 'a' (app: has an address space, killable) or
// 'k' (kernel task: protected). Terminated tasks are skipped.
static boolean TaskListCallback (CTask *pTask, const char *pName, TTaskState State,
				 TTaskFlags Flags, void *pParam)
{
	(void) Flags;
	if (State == TaskStateTerminated)
	{
		return TRUE;
	}
	char sc = State == TaskStateReady ? 'R'
		: State == TaskStateSleeping ? 'S'
		: (State == TaskStateBlocked || State == TaskStateBlockedWithTimeout) ? 'B'
		: State == TaskStateNew ? 'N' : '?';
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	if (pAS != 0 && pTask != pAS->GetMainTask ())
	{
		return TRUE;					// (v67) a thread: its app is listed once
	}
	char kc = pAS != 0 ? 'a' : 'k';

	WinListCtx *pCtx = (WinListCtx *) pParam;
	if (pCtx->nPos + 4 < pCtx->nSize)
	{
		pCtx->pBuf[pCtx->nPos++] = sc;
		pCtx->pBuf[pCtx->nPos++] = kc;
		pCtx->pBuf[pCtx->nPos++] = ' ';
	}
	for (unsigned j = 0; pName[j] != '\0'; j++)
		if (pCtx->nPos + 2 < pCtx->nSize) pCtx->pBuf[pCtx->nPos++] = pName[j];
	if (pCtx->nPos + 1 < pCtx->nSize) pCtx->pBuf[pCtx->nPos++] = '\n';
	pCtx->nCount++;
	return TRUE;
}

int kapi_list_tasks (char *pBuf, unsigned nBufSize)
{
	if (pBuf == 0 || nBufSize == 0 || !UserWritable (pBuf, nBufSize))
	{
		return 0;
	}
	pBuf[0] = '\0';
	if (!CScheduler::IsActive ())
	{
		return 0;
	}
	WinListCtx Ctx = { pBuf, nBufSize, 0, 0 };
	CScheduler::Get ()->EnumerateTasks (TaskListCallback, &Ctx);
	pBuf[Ctx.nPos] = '\0';
	return Ctx.nCount;
}

// Kill an app by name (refuses kernel tasks -- those have no address space -- and
// the caller itself). Returns 1 if killed, 0 otherwise.
int kapi_kill (const char *pUserName)
{
	CUserStr Name (pUserName);
	const char *pName = Name.Get ();
	if (pName == 0 || !CScheduler::IsActive ())
	{
		return 0;
	}
	CTask *pTask = CScheduler::Get ()->GetRunningTask (pName);
	if (pTask == 0 || pTask == CScheduler::Get ()->GetCurrentTask ())
	{
		return 0;
	}
	if (pTask->GetUserData (TASK_USER_DATA_USER) == 0)
	{
		return 0;			// kernel task (compositor/reaper/input): protected
	}
	if (pTask->GetUserData (TASK_USER_DATA_USER) == CurrentAS ())
	{
		return 0;			// its own process (a thread of it)
	}
	((CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER))->SetTermReason (KAPI_PROC_KILLED, -9);	// (v75)
	CScheduler::Get ()->TerminateTask (pTask);
	return 1;
}

// --- ps / kill by PID --------------------------------------------------------

static void ProcAppStr (WinListCtx *c, const char *s)
{
	for (unsigned j = 0; s[j] != '\0'; j++)
		if (c->nPos + 2 < c->nSize) c->pBuf[c->nPos++] = s[j];
}
static void ProcAppUInt (WinListCtx *c, unsigned v)
{
	char t[12]; int n = 0;
	if (v == 0) t[n++] = '0';
	while (v) { t[n++] = (char) ('0' + v % 10); v /= 10; }
	while (n > 0) { n--; if (c->nPos + 2 < c->nSize) c->pBuf[c->nPos++] = t[n]; }
}

// One line per task: "<pid> <a|k> <R|S|B|N> <pages> <name>". pid is the address-space
// id for apps, 0 for kernel tasks (which have no address space and are unkillable);
// <pages> is the count of 64 KB physical frames the app owns (0 for kernel tasks).
static boolean ProcListCallback (CTask *pTask, const char *pName, TTaskState State,
				 TTaskFlags Flags, void *pParam)
{
	(void) Flags;
	if (State == TaskStateTerminated) return TRUE;
	WinListCtx *c = (WinListCtx *) pParam;
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	if (pAS != 0 && pTask != pAS->GetMainTask ()) return TRUE;	// (v67) a thread: listed once

	char st = State == TaskStateReady ? 'R'
		: State == TaskStateSleeping ? 'S'
		: (State == TaskStateBlocked || State == TaskStateBlockedWithTimeout) ? 'B'
		: State == TaskStateNew ? 'N' : '?';

	ProcAppUInt (c, pAS != 0 ? pAS->GetPid () : 0);
	ProcAppStr (c, pAS != 0 ? " a " : " k ");
	if (c->nPos + 2 < c->nSize) c->pBuf[c->nPos++] = st;
	if (c->nPos + 2 < c->nSize) c->pBuf[c->nPos++] = ' ';
	ProcAppUInt (c, pAS != 0 ? pAS->GetPages () : 0);	// owned 64 KB pages
	if (c->nPos + 2 < c->nSize) c->pBuf[c->nPos++] = ' ';
	ProcAppStr (c, pName);
	if (c->nPos + 1 < c->nSize) c->pBuf[c->nPos++] = '\n';
	c->nCount++;
	return TRUE;
}

int kapi_list_procs (char *pBuf, unsigned nBufSize)
{
	if (pBuf == 0 || nBufSize == 0 || !UserWritable (pBuf, nBufSize)) return 0;
	pBuf[0] = '\0';
	if (!CScheduler::IsActive ()) return 0;
	WinListCtx Ctx = { pBuf, nBufSize, 0, 0 };
	CScheduler::Get ()->EnumerateTasks (ProcListCallback, &Ctx);
	pBuf[Ctx.nPos] = '\0';
	return Ctx.nCount;
}

struct KillByPidCtx { unsigned nPid; CTask *pFound; };
static boolean FindByPid (CTask *pTask, const char *pName, TTaskState State,
			  TTaskFlags Flags, void *pParam)
{
	(void) pName; (void) Flags;
	if (State == TaskStateTerminated) return TRUE;
	KillByPidCtx *c = (KillByPidCtx *) pParam;
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	if (pAS != 0 && pAS->GetPid () == c->nPid) c->pFound = pTask;	// pids unique
	return TRUE;
}

// A live process's parent pid (0: none, or no such process).
static unsigned ParentPidOf (unsigned nPid)
{
	if (nPid == 0 || !CScheduler::IsActive ()) return 0;
	KillByPidCtx Ctx = { nPid, 0 };
	CScheduler::Get ()->EnumerateTasks (FindByPid, &Ctx);
	CAddressSpace *pAS = Ctx.pFound != 0 ? (CAddressSpace *) Ctx.pFound->GetUserData (TASK_USER_DATA_USER) : 0;
	return pAS != 0 ? pAS->GetParentPid () : 0;
}

// Kill an app by PID. nForce == 0: ask it to close cleanly (raise its window's exit
// flag, so its pump loop ends and main() returns -- the app gets to clean up); a
// windowless app with nothing to signal falls through to a hard terminate. nForce:
// terminate immediately. Returns 1 (signalled/killed), 0 (no such pid), -1 (kernel
// task or the caller itself -- protected).
int kapi_kill_pid (int nPid, int nForce)
{
	if (nPid <= 0 || !CScheduler::IsActive ()) return 0;
	KillByPidCtx Ctx = { (unsigned) nPid, 0 };
	CScheduler::Get ()->EnumerateTasks (FindByPid, &Ctx);
	CTask *pTask = Ctx.pFound;
	if (pTask == 0) return 0;
	if (pTask == CScheduler::Get ()->GetCurrentTask ()) return -1;	// self
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	if (pAS == 0) return -1;					// kernel task
	if (pAS == CurrentAS ()) return -1;				// its own process (a thread)
	if (!nForce)
	{
		CWindow *pWin = pAS->GetWindow ();
		if (pWin != 0) { pWin->RequestExit (); return 1; }	// clean close
		// else: no window to signal -> hard terminate below
	}
	pAS->SetTermReason (KAPI_PROC_KILLED, -9);			// (v75: proc_wait)
	CScheduler::Get ()->TerminateTask (pTask);
	return 1;
}

// --- v91: a process's tree (KAPI_TREE_*) ----------------------------------------
// The tree is made of the parent pids the processes recorded at their spawn: the root, then
// every live process whose parent is in the set, pass after pass (its children, then theirs...).
// A kapi runs on core 0 and is not preempted: nobody spawns or ends while the set is made and
// killed. A child whose start is still deferred (SpawnProcess) is not a task yet: it starts
// with a dead parent and the reaper's orphan scan ends it (kernel.cpp, TerminateOrphans).
#define TREE_MAX	256
struct TreeCtx { unsigned *pPids; unsigned n; boolean bGrew; };
static boolean TreeHas (const TreeCtx *c, unsigned nPid)
{
	for (unsigned i = 0; i < c->n; i++) if (c->pPids[i] == nPid) return TRUE;
	return FALSE;
}
static boolean TreeGrowCb (CTask *pTask, const char *, TTaskState State, TTaskFlags, void *pParam)
{
	if (State == TaskStateTerminated) return TRUE;
	TreeCtx *c = (TreeCtx *) pParam;
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	if (pAS == 0 || pAS->GetParentPid () == 0 || c->n >= TREE_MAX) return TRUE;
	if (TreeHas (c, pAS->GetPid ()) || !TreeHas (c, pAS->GetParentPid ())) return TRUE;
	c->pPids[c->n++] = pAS->GetPid ();		// (a thread of a process already in: TreeHas)
	c->bGrew = TRUE;
	return TRUE;
}

int kapi_proc_tree (int nPid, int nOp, int *pOut, unsigned nCap)
{
	if (nPid <= 0 || nOp < KAPI_TREE_LIST || nOp > KAPI_TREE_KILL_CHILDREN) return -KAPI_EINVAL;
	if (!CScheduler::IsActive ()) return -KAPI_ESRCH;
	KillByPidCtx Root = { (unsigned) nPid, 0 };
	CScheduler::Get ()->EnumerateTasks (FindByPid, &Root);
	if (Root.pFound == 0 || Root.pFound->GetUserData (TASK_USER_DATA_USER) == 0) return -KAPI_ESRCH;

	unsigned Pids[TREE_MAX];				// [0] the root, then its descendants
	TreeCtx Ctx = { Pids, 1, TRUE };
	Pids[0] = (unsigned) nPid;
	for (unsigned nPass = 0; Ctx.bGrew && nPass < TREE_MAX; nPass++)
	{
		Ctx.bGrew = FALSE;
		CScheduler::Get ()->EnumerateTasks (TreeGrowCb, &Ctx);
	}

	if (nOp == KAPI_TREE_LIST)
	{
		unsigned nDesc = Ctx.n - 1;
		for (unsigned i = 0; i < nDesc && i < nCap; i++)
			if (pOut == 0 || !UserPut (&pOut[i], (int) Pids[i + 1])) return -KAPI_EFAULT;
		return (int) nDesc;
	}

	// A kill: never the caller's own process (it is the root, or one of its descendants).
	CAddressSpace *pMe = CurrentAS ();
	if (pMe != 0 && TreeHas (&Ctx, pMe->GetPid ())) return -KAPI_EPERM;
	unsigned nFirst = nOp == KAPI_TREE_KILL ? 0 : 1, nKilled = 0;
	for (unsigned i = Ctx.n; i-- > nFirst; )		// the leaves first
	{
		KillByPidCtx K = { Pids[i], 0 };
		CScheduler::Get ()->EnumerateTasks (FindByPid, &K);
		CAddressSpace *pAS = K.pFound != 0 ? (CAddressSpace *) K.pFound->GetUserData (TASK_USER_DATA_USER) : 0;
		if (pAS == 0) continue;
		pAS->SetTermReason (KAPI_PROC_KILLED, -9);		// (proc_wait)
		CScheduler::Get ()->TerminateTask (K.pFound);	// (its whole process: TerminateGroup)
		nKilled++;
	}
	if (nKilled > 0)
		CLogger::Get ()->Write ("proc", LogNotice, "proc_tree: pid %d's tree, %u process(es) terminated", nPid, nKilled);
	return (int) nKilled;
}

// --- keyboard layout (kernel.cpp drives the Circle CKeyMap; decls in applaunch.h) --
// Switch the keyboard layout to a compiled-in country map ("FR","US","DE","UK",
// "ES","IT","DV"). Returns 1 on success, 0 if unknown / no keyboard.
int kapi_set_keymap (const char *pName)
{
	CUserStr Name (pName);
	return Name.OK () && KernelSetKeyMap (Name.Get ()) ? 1 : 0;
}

// 1 if a USB keyboard is attached & ready, else 0 (ABI v26). The `keyb` tool polls
// this at boot before applying a layout, since it may run before USB enumeration
// completes -- the kernel no longer applies any layout from cmdline.
int kapi_kbd_ready (void)
{
	return KernelKeyboardReady () ? 1 : 0;
}

// Load a keymap from a SD:/etc/keymaps/<X>.kmap blob (ABI v27): header "OKM1" + u16
// rows(128) + u16 cols(5) + rows*cols u16 table. The kernel validates + copies it into
// a persistent layout snapshot and (if a keyboard is attached) onto the live keyboard;
// the caller (keyb) frees its buffer. name is recorded for get_keymap/ps. Returns 1
// once the blob is accepted -- the snapshot is then applied to the keyboard whenever it
// attaches, so this no longer requires a keyboard to be present (it used to return 0
// "no keyboard", which lost the layout at boot if USB enumeration was slow). Returns 0
// only on a malformed blob. Lets layouts be added as files without recompiling the kernel.
int kapi_set_keymap_data (const char *pName, const void *pData, unsigned nLen)
{
	const unsigned char *pUser = (const unsigned char *) pData;
	unsigned char p[8];					// the header, copied
	if (pUser == 0 || nLen < 8 || !UserCopyIn (p, pUser, sizeof p)) return 0;
	if (!(p[0] == 'O' && p[1] == 'K' && p[2] == 'M' && p[3] == '1')) return 0;
	unsigned nRows = (unsigned) p[4] | ((unsigned) p[5] << 8);
	unsigned nCols = (unsigned) p[6] | ((unsigned) p[7] << 8);
	if (nRows != 128 || nCols != 5) return 0;
	unsigned nTable = nRows * nCols * 2;
	if (nLen < 8 + nTable || !UserReadable (pUser + 8, nTable)) return 0;
	CUserStr Name (pName, 64, TRUE);			// (the kernel keeps 7 characters)
	if (!Name.OK () && !Name.IsNull ()) return 0;
	return KernelSetKeyMapData (Name.Get (), pUser + 8, nTable) ? 1 : 0;
}

// kapi_random: random bytes for cryptographic seeding (the TLS entropy source in
// user/Libs/tls/onyx_tls.hpp feeds mbedTLS's CTR_DRBG from here).
//
// This is a SOFTWARE PRNG (splitmix64) seeded from the high-resolution timer. It
// deliberately does NOT touch the Pi 4 hardware RNG: the BCM2711 RNG200 (at
// ARM_HW_RNG_BASE) is not enabled/clocked in our setup, so ANY MMIO access to it stalls
// the AXI bus and HARD-FREEZES the cooperative system -- a bounded poll cannot help
// because the CPU hangs inside the read itself. (Circle's CBcmRandomNumberGenerator is
// the legacy BCM2835 driver, also non-functional here.) So we avoid the RNG entirely.
//
// STOPGAP: this is NOT cryptographically strong (timer-seeded). It is enough to run TLS
// (cert verification is also off for now). TODO before trusting HTTPS: bring the RNG200
// up properly (enable via the VC mailbox / verify on real hardware) for real entropy.
int kapi_random (void *pBuf, unsigned nLen)
{
	if (pBuf == 0 || !UserWritable (pBuf, nLen)) return 0;

	static u64 s = 0;
	if (s == 0)
		s = ((u64) CTimer::Get ()->GetClockTicks () << 16) ^ 0x9E3779B97F4A7C15ULL;

	unsigned char *p = (unsigned char *) pBuf;
	for (unsigned i = 0; i < nLen; i++)
	{
		s += 0x9E3779B97F4A7C15ULL ^ (u64) CTimer::Get ()->GetClockTicks ();
		u64 z = s;				// splitmix64 finalizer
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
		z =  z ^ (z >> 31);
		p[i] = (unsigned char) z;
	}
	return (int) nLen;
}

// Verbose kernel logging: toggle (KernelSetVerbose, defined in kernel.cpp) + read.
// The `verbose` command persists the choice to SD:system.ini itself.
int kapi_set_verbose (int bOn) { KernelSetVerbose (bOn ? TRUE : FALSE); return 1; }
int kapi_get_verbose (void) { return KernelGetVerbose () ? 1 : 0; }

// Current layout name into pBuf (empty = boot default). Returns the length.
int kapi_get_keymap (char *pBuf, unsigned nMax)
{
	if (pBuf == 0 || nMax == 0) return 0;
	int n = UserStrOut (pBuf, nMax, KernelGetKeyMap ());
	return n > 0 ? n : 0;
}

// The calling app's local folder ("SD:apps/<name>.app/") into pBuf -- the task name
// is the app's folder basename. Returns the string length. Used by the shared app
// lib to find an app's config.ini.
int kapi_app_dir (char *pBuf, unsigned nMax)
{
	if (pBuf == 0 || nMax == 0)
	{
		return 0;
	}
	const char *pName = "app";
	if (CScheduler::IsActive ())
	{
		CTask *pTask = CScheduler::Get ()->GetCurrentTask ();
		if (pTask != 0)
		{
			pName = pTask->GetName ();
		}
	}

	char Dir[300];						// (made here, then copied out)
	unsigned p = 0;
	const char *pPre = "SD:apps/";
	const char *pSuf = ".app/";
	for (unsigned i = 0; pPre[i] != '\0' && p + 1 < sizeof Dir; i++) Dir[p++] = pPre[i];
	for (unsigned i = 0; pName[i] != '\0' && p + 1 < sizeof Dir; i++) Dir[p++] = pName[i];
	for (unsigned i = 0; pSuf[i] != '\0' && p + 1 < sizeof Dir; i++) Dir[p++] = pSuf[i];
	Dir[p] = '\0';
	int n = UserStrOut (pBuf, nMax, Dir);
	return n > 0 ? n : 0;
}

// Current local date/time, broken down. Any pointer may be 0. Returns 1 if the
// clock holds a real wall-clock time, 0 if it is still only uptime (no RTC/NTP yet,
// so the fields then reflect seconds-since-boot mapped onto 1970).
int kapi_get_datetime (int *pYear, int *pMonth, int *pDay,
		       int *pHour, int *pMinute, int *pSecond)
{
	unsigned nSeconds = CTimer::Get ()->GetLocalTime ();
	CTime Time;
	Time.Set ((time_t) nSeconds);

	OutPut (pYear,   (int) Time.GetYear ());		// (a bad pointer: that field skipped)
	OutPut (pMonth,  (int) Time.GetMonth ());
	OutPut (pDay,    (int) Time.GetMonthDay ());
	OutPut (pHour,   (int) Time.GetHours ());
	OutPut (pMinute, (int) Time.GetMinutes ());
	OutPut (pSecond, (int) Time.GetSeconds ());

	return nSeconds > 60u * 60 * 24 * 365 ? 1 : 0;	// > ~1 year => a real date
}

// --- console -----------------------------------------------------------------

int kapi_write (int /*fd*/, const void *pBuf, unsigned nLen)
{
	char Tmp[129];
	unsigned n = nLen < sizeof (Tmp) - 1 ? nLen : sizeof (Tmp) - 1;
	if (!UserCopyIn (Tmp, pBuf, n))		// (the app's buffer: copied fault-safe)
	{
		return -1;
	}
	Tmp[n] = '\0';
	CLogger::Get ()->Write ("app", LogNotice, "%s", Tmp);
	return (int) nLen;
}

// --- handles (kern/handle.h) ----------------------------------------------------
//
// The objects an app holds -- an open file (a FatFs FIL, a RAM: or a provider's file), a
// directory listing, a stream, a spawned process -- are named by an opaque handle of the
// calling process's table (a small number in the ABI's void *, never 0), not by their kernel
// address: a kapi looks it up by type and fails cleanly on a bad one, and the process's
// handles still open when it ends are closed by its teardown.

// Hand pObj (its reference) to the caller as a new handle; 0 (and pObj closed) if the
// table is full or out of memory.
static void *HandleNew (void *pObj, unsigned nType, unsigned nKind = HKIND_FATFS)
{
	if (pObj == 0)
	{
		return 0;
	}
	CHandleTable *pTable = HandlesCurrent ();
	void *h = pTable != 0 ? pTable->Add (pObj, nType, nKind) : 0;
	if (h == 0)
	{
		HandleObjectClose (pObj, nType, nKind, FALSE);
	}
	return h;
}

// A handle's object for the length of one kapi call, pinned: the call may yield (an SD read,
// a pipe's wait), and another thread of the process closing the handle meanwhile only marks
// it -- the object is closed when this call is over (CHandleTable::Unpin).
class CHandleUse
{
public:
	CHandleUse (void *h, unsigned nType)
	:	m_pTable (HandlesCurrent ()), m_pObj (0), m_nIdx (0), m_nKind (0)
	{
		if (m_pTable != 0)
		{
			m_pObj = m_pTable->Pin (h, nType, &m_nIdx, &m_nKind);
		}
	}
	~CHandleUse (void)
	{
		if (m_pObj != 0)
		{
			m_pTable->Unpin (m_nIdx);
		}
	}
	void *Obj (void) const		{ return m_pObj; }
	unsigned Kind (void) const	{ return m_nKind; }

private:
	CHandleTable *m_pTable;
	void	     *m_pObj;
	unsigned      m_nIdx;
	unsigned      m_nKind;
};

static void HandleClose (void *h, unsigned nType)
{
	CHandleTable *pTable = HandlesCurrent ();
	if (pTable != 0)
	{
		pTable->Close (h, nType);
	}
}

// --- files (the motivation for direct calls) ---------------------------------
//
// A file handle's object: a FatFs FIL (read-only), a RAM: file or a provider's file -- the
// handle's entry says which (HKIND_*).

void *kapi_open (const char *pUserPath)
{
	CUserStr Path (pUserPath, UPATH_MAX);			// (read once: the kernel's copy from here)
	const char *pPath = Path.Get ();
	if (pPath == 0) return 0;
	char abs[300]; ResolvePath (pPath, abs, sizeof abs);
	if (RamFsHandles (abs)) return HandleNew (RamFsOpen (abs), HANDLE_FILE, HKIND_RAMFS);	// RAM: (kern/ramfs.h)
	if (VfsHandles (pPath)) return HandleNew (VfsOpen (pPath), HANDLE_FILE, HKIND_VFS);	// a provider path (FTP:...)
	FIL *pFile = new FIL;
	if (pFile == 0)
	{
		return 0;
	}
	if (f_open (pFile, abs, FA_READ) != FR_OK)
	{
		delete pFile;
		return 0;
	}
	return HandleNew (pFile, HANDLE_FILE, HKIND_FATFS);
}

// Big file transfers go in pieces with a Yield between them (a voluntary preemption point):
// kernel code is not preempted, so one f_read of a 28 MB file (a Doom WAD, a GBA ROM) would
// stop every other task -- the compositor, the cursor, the sound feeders -- until it ended.
// Between two pieces the FatFs volume lock is free, so other tasks' file calls get through
// too. The caller's buffer stays valid: its address space is active again when it resumes.
#define IO_CHUNK	(64 * 1024)

// (Circle fork, addon/SDCard/emmc.cpp: where the SD data commands spend their time)
extern unsigned long long g_ullEMMCWaitUs, g_ullEMMCCopyUs;
extern unsigned g_nEMMCDataCmds;

FRESULT ChunkedRead (FIL *pFile, void *pBuf, unsigned nLen, UINT *pDone)	// (kern/ofile.h)
{
	u8 *p = (u8 *) pBuf;
	*pDone = 0;
	// A big read (>= 1 MB) logs where its time went: FatFs + the SD driver (waiting for
	// the card / moving the data through the port) or the other tasks between the pieces.
	CTimer *pTimer = CTimer::Get ();
	unsigned nT0 = pTimer->GetClockTicks (), nReadUs = 0, nYieldUs = 0;
	unsigned long long ullWait0 = g_ullEMMCWaitUs, ullCopy0 = g_ullEMMCCopyUs;
	unsigned nCmds0 = g_nEMMCDataCmds, nTotal = nLen;
	FRESULT Res = FR_OK;
	while (nLen > 0)
	{
		unsigned k = nLen > IO_CHUNK ? IO_CHUNK : nLen;
		UINT n = 0;
		unsigned t = pTimer->GetClockTicks ();
		Res = f_read (pFile, p, k, &n);
		nReadUs += pTimer->GetClockTicks () - t;
		if (Res != FR_OK) break;
		*pDone += n; p += n; nLen -= n;
		if (n < k) break;					// the end of the file
		t = pTimer->GetClockTicks ();
		if (nLen > 0 && CScheduler::IsActive ()) CScheduler::Get ()->Yield ();
		nYieldUs += pTimer->GetClockTicks () - t;
	}
	if (nTotal >= 1024 * 1024)
	{
		unsigned nAllUs = pTimer->GetClockTicks () - nT0;
		CLogger::Get ()->Write ("fs", LogNotice,
			"read %u KB in %u ms: f_read %u ms (SD: %u commands, waiting %u ms, data port %u ms), other tasks %u ms",
			*pDone / 1024, nAllUs / 1000, nReadUs / 1000, g_nEMMCDataCmds - nCmds0,
			(unsigned) ((g_ullEMMCWaitUs - ullWait0) / 1000), (unsigned) ((g_ullEMMCCopyUs - ullCopy0) / 1000),
			nYieldUs / 1000);
	}
	return Res;
}

FRESULT ChunkedWrite (FIL *pFile, const void *pBuf, unsigned nLen, UINT *pDone)	// (kern/ofile.h)
{
	const u8 *p = (const u8 *) pBuf;
	*pDone = 0;
	while (nLen > 0)
	{
		unsigned k = nLen > IO_CHUNK ? IO_CHUNK : nLen;
		UINT n = 0;
		FRESULT Res = f_write (pFile, p, k, &n);
		if (Res != FR_OK) return Res;
		*pDone += n; p += n; nLen -= n;
		if (n < k) break;					// (the card is full)
		if (nLen > 0 && CScheduler::IsActive ()) CScheduler::Get ()->Yield ();
	}
	return FR_OK;
}

int kapi_read (void *pHandle, void *pBuf, unsigned nLen)
{
	CHandleUse Use (pHandle, HANDLE_FILE);
	void *pObj = Use.Obj ();
	if (pObj == 0)
	{
		return -1;
	}
	// The bytes it will fill, checked first: a card file's are known (what is left of it: an app
	// passing a buffer's capacity for a short file keeps working), the others' are nLen.
	u64 nFill = nLen;
	if (Use.Kind () == HKIND_FATFS)
	{
		FIL *pFile = (FIL *) pObj;
		u64 nLeft = f_size (pFile) > f_tell (pFile) ? (u64) (f_size (pFile) - f_tell (pFile)) : 0;
		if (nFill > nLeft) nFill = nLeft;
	}
	if (!UserWritable (pBuf, nFill))
	{
		return -1;
	}
	if (Use.Kind () == HKIND_RAMFS) return RamFsRead (pObj, pBuf, nLen);
	if (Use.Kind () == HKIND_VFS) return VfsRead (pObj, pBuf, nLen);
	UINT nRead = 0;
	if (ChunkedRead ((FIL *) pObj, pBuf, nLen, &nRead) != FR_OK)
	{
		return -1;
	}
	return (int) nRead;
}

// The whole size; 0 for a bad handle.
static u64 FileSize (void *pHandle)
{
	CHandleUse Use (pHandle, HANDLE_FILE);		// (RAM:'s lock may yield)
	void *pObj = Use.Obj ();
	if (pObj == 0) return 0;
	if (Use.Kind () == HKIND_RAMFS) return RamFsSize (pObj);
	if (Use.Kind () == HKIND_VFS) return VfsSize (pObj);
	return (u64) f_size ((FIL *) pObj);
}

unsigned kapi_fsize (void *pHandle)
{
	u64 n = FileSize (pHandle);
	return n > 0xFFFFFFFFu ? 0xFFFFFFFFu : (unsigned) n;	// (an exFAT file over 4 GB: fsize64)
}

// v59: the whole size (exFAT: files over 4 GB)
unsigned long long kapi_fsize64 (void *pHandle)
{
	return FileSize (pHandle);
}

// v57: the read position (FatFs fast seek: a big file's cluster map made at its first seek)
int kapi_seek (void *pHandle, unsigned long long ullPos)
{
	CHandleUse Use (pHandle, HANDLE_FILE);
	if (Use.Obj () == 0 || Use.Kind () == HKIND_VFS) return -1;
	if (Use.Kind () == HKIND_RAMFS) return RamFsSeek (Use.Obj (), ullPos);
	FIL *pFile = (FIL *) Use.Obj ();
	if (pFile->cltbl == 0 && f_size (pFile) > 4 * 1024 * 1024)
	{
		DWORD *pTbl = new DWORD[64];
		if (pTbl != 0)
		{
			pTbl[0] = 64; pFile->cltbl = pTbl;
			FRESULT r = f_lseek (pFile, CREATE_LINKMAP);
			if (r == FR_NOT_ENOUGH_CORE)			// (a fragmented file: the size it needs)
			{
				DWORD nNeed = pTbl[0];
				delete [] pTbl;
				pTbl = new DWORD[nNeed];
				pFile->cltbl = pTbl;
				if (pTbl != 0) { pTbl[0] = nNeed; r = f_lseek (pFile, CREATE_LINKMAP); }
			}
			if (r != FR_OK) { delete [] pFile->cltbl; pFile->cltbl = 0; }
		}
	}
	return f_lseek (pFile, (FSIZE_t) ullPos) == FR_OK ? 0 : -1;
}

// v58: executable memory for generated code (a JIT), in the process's code arena
void *kapi_code_alloc (unsigned long ulSize)
{
	CAddressSpace *pAS = CurrentAS ();
	return pAS != 0 ? pAS->CodeAlloc (ulSize) : 0;
}

void kapi_close (void *pHandle)
{
	HandleClose (pHandle, HANDLE_FILE);		// (kern/handle.h: HandleObjectClose)
}

// --- streams / stdio / processes ---------------------------------------------
//
// A stream handle holds one reference to its CStream (a pipe, a file stream); a spawned
// child takes its own on its stdin / stdout, so either side may close first.

void *kapi_pipe (void)
{
	return HandleNew (new CPipeStream, HANDLE_STREAM);
}

void *kapi_file_in (const char *pUserPath)
{
	CUserStr Path (pUserPath, UPATH_MAX);
	if (!Path.OK ()) return 0;
	char abs[300]; ResolvePath (Path.Get (), abs, sizeof abs);
	if (RamFsHandles (abs))
	{
		CRamStream *pRam = new CRamStream (abs, 0);
		if (pRam != 0 && !pRam->IsValid ()) { delete pRam; pRam = 0; }
		return HandleNew (pRam, HANDLE_STREAM);
	}
	CFileStream *pFile = new CFileStream (abs, 0);
	if (pFile == 0) return 0;
	if (!pFile->IsValid ()) { delete pFile; return 0; }
	return HandleNew (pFile, HANDLE_STREAM);
}

void *kapi_file_out (const char *pUserPath, int bAppend)
{
	CUserStr Path (pUserPath, UPATH_MAX);
	if (!Path.OK ()) return 0;
	char abs[300]; ResolvePath (Path.Get (), abs, sizeof abs);
	if (RamFsHandles (abs))
	{
		CRamStream *pRam = new CRamStream (abs, bAppend ? 2 : 1);
		if (pRam != 0 && !pRam->IsValid ()) { delete pRam; pRam = 0; }
		return HandleNew (pRam, HANDLE_STREAM);
	}
	CFileStream *pFile = new CFileStream (abs, bAppend ? 2 : 1);
	if (pFile == 0) return 0;
	if (!pFile->IsValid ()) { delete pFile; return 0; }
	return HandleNew (pFile, HANDLE_STREAM);
}

int kapi_stream_read (void *pHandle, void *pBuf, unsigned nLen)
{
	CHandleUse Use (pHandle, HANDLE_STREAM);	// (a pipe's read waits for its writer)
	if (!UserWritable (pBuf, nLen)) return 0;	// (as a bad handle)
	return Use.Obj () != 0 ? ((CStream *) Use.Obj ())->Read (pBuf, nLen) : 0;
}

// Non-blocking read: >0 bytes, 0 = EOF, -1 = would block. For the terminal, which
// drains a child's stdout without freezing its own UI loop.
int kapi_stream_read_nb (void *pHandle, void *pBuf, unsigned nLen)
{
	CHandleUse Use (pHandle, HANDLE_STREAM);	// (a file stream's read may yield)
	if (!UserWritable (pBuf, nLen)) return 0;	// (as a bad handle)
	return Use.Obj () != 0 ? ((CStream *) Use.Obj ())->ReadNonBlocking (pBuf, nLen) : 0;
}

int kapi_stream_write (void *pHandle, const void *pBuf, unsigned nLen)
{
	CHandleUse Use (pHandle, HANDLE_STREAM);	// (a full pipe waits for its reader)
	if (!UserReadable (pBuf, nLen)) return -1;
	return Use.Obj () != 0 ? ((CStream *) Use.Obj ())->Write (pBuf, nLen) : -1;
}

void kapi_stream_close (void *pHandle)
{
	HandleClose (pHandle, HANDLE_STREAM);		// (this handle's ref dropped)
}

// Signal EOF to readers of this stream (the writer is done). The terminal uses it
// on its keyboard pipe so a stdin-reading child (e.g. cat) ends on Ctrl-D.
void kapi_stream_eof (void *pHandle)
{
	CHandleTable *pTable = HandlesCurrent ();
	unsigned nKind = 0;
	CStream *pStream = pTable != 0 ? (CStream *) pTable->Get (pHandle, HANDLE_STREAM, &nKind) : 0;
	if (pStream == 0 || (nKind & HKIND_STREAM_EOF_DONE)) return;
	if (nKind & HKIND_STREAM_WRITER)		// (v76: a carried write end: once)
	{
		pTable->SetKind (pHandle, HANDLE_STREAM, nKind | HKIND_STREAM_EOF_DONE);
	}
	pStream->CloseWrite ();
}

// Read from this task's stdin (0 = EOF / no stdin).
int kapi_stdin_read (void *pBuf, unsigned nLen)
{
	CAddressSpace *pAS = CurrentAS ();
	CStream *pStream = pAS != 0 ? pAS->GetStdin () : 0;
	if (!UserWritable (pBuf, nLen)) return 0;
	return pStream != 0 ? pStream->Read (pBuf, nLen) : 0;
}

// Write to this task's stdout; if none (e.g. launched from the panel), log it.
int kapi_stdout_write (const void *pBuf, unsigned nLen)
{
	CAddressSpace *pAS = CurrentAS ();
	CStream *pStream = pAS != 0 ? pAS->GetStdout () : 0;
	if (pStream != 0)
	{
		if (!UserReadable (pBuf, nLen)) return -1;
		return pStream->Write (pBuf, nLen);
	}
	char Tmp[129];
	unsigned n = nLen < sizeof (Tmp) - 1 ? nLen : sizeof (Tmp) - 1;
	if (!UserCopyIn (Tmp, pBuf, n)) return -1;
	Tmp[n] = '\0';
	CLogger::Get ()->Write ("app", LogNotice, "%s", Tmp);
	return (int) nLen;
}

// Read the next kernel log event from CLogger's ring (a tee: the logs still go to
// their normal target, this just also exposes them). Returns 1 + fills severity
// (0=panic..4=debug) / source / message, or 0 if the queue is empty. A real-time
// kernel log viewer (kmsg) polls this; nothing needs redirecting or restoring.
int kapi_klog_read (int *pSeverity, char *pSrc, unsigned nSrcCap, char *pMsg, unsigned nMsgCap)
{
	TLogSeverity Sev; char Src[LOG_MAX_SOURCE]; char Msg[LOG_MAX_MESSAGE];
	time_t t; unsigned ht; int tz;
	// (the outputs checked before an event is taken off the queue)
	if (   !OutOK (pSeverity)
	    || (pSrc != 0 && nSrcCap > 0 && !UserRange (pSrc, nSrcCap < sizeof Src ? nSrcCap : sizeof Src))
	    || (pMsg != 0 && nMsgCap > 0 && !UserRange (pMsg, nMsgCap < sizeof Msg ? nMsgCap : sizeof Msg)))
	{
		return 0;
	}
	if (!CLogger::Get ()->ReadEvent (&Sev, Src, Msg, &t, &ht, &tz))
	{
		return 0;
	}
	OutPut (pSeverity, (int) Sev);
	if (pSrc != 0 && nSrcCap > 0) UserStrOut (pSrc, nSrcCap, Src);
	if (pMsg != 0 && nMsgCap > 0) UserStrOut (pMsg, nMsgCap, Msg);
	return 1;
}

// This task's own stdin / stdout stream handles, so a shell (cmd) can wire them into
// the children it spawns (first stage reads the shell's stdin, last stage's output is
// drained by the shell). 0 if none. The handle has its own ref on the stream (the process
// keeps its stdio ref): made at the first call, the same one returned while it is open --
// closing it (stream_close) is allowed and no longer drops the process's own ref.
static void *StdioHandle (CStream *pStream)
{
	CHandleTable *pTable = HandlesCurrent ();
	if (pStream == 0 || pTable == 0)
	{
		return 0;
	}
	void *h = pTable->Find (pStream, HANDLE_STREAM);
	if (h != 0)
	{
		return h;
	}
	pStream->AddRef ();
	h = pTable->Add (pStream, HANDLE_STREAM);
	if (h == 0)
	{
		pStream->Release ();			// (not the last: the process holds one)
	}
	return h;
}
void *kapi_stdin (void)
{
	CAddressSpace *pAS = CurrentAS ();
	return pAS != 0 ? StdioHandle (pAS->GetStdin ()) : 0;
}
void *kapi_stdout (void)
{
	CAddressSpace *pAS = CurrentAS ();
	return pAS != 0 ? StdioHandle (pAS->GetStdout ()) : 0;
}

// Spawn a console program (ELF at pPath) with stdin/stdout streams (stream handles of the
// caller, or 0) + argv. Returns a process handle for kapi_wait, or 0 on failure (also when
// pStdin / pStdout is not a stream handle of the caller).
void *kapi_spawn (const char *pUserPath, const char *pUserArgs, void *pStdin, void *pStdout)
{
	CUserStr Path (pUserPath, UPATH_MAX), Args (pUserArgs, ARGS_MAX, TRUE);
	if (!Path.OK () || (!Args.OK () && !Args.IsNull ()))
	{
		return 0;
	}
	const char *pPath = Path.Get (), *pArgs = Args.Get ();
	// (pinned: SpawnProcess may yield -- it looks for the file -- before the child takes
	// its refs on them)
	CHandleUse In (pStdin, HANDLE_STREAM), Out (pStdout, HANDLE_STREAM);
	if ((pStdin != 0 && In.Obj () == 0) || (pStdout != 0 && Out.Obj () == 0))
	{
		return 0;
	}
	// Its handle first (the table full: no child started that nobody could wait for).
	CHandleTable *pTable = HandlesCurrent ();
	void *h = pTable != 0 ? pTable->Reserve () : 0;
	if (h == 0)
	{
		return 0;
	}

	// Resolve the program path against the caller's cwd, pass that cwd to the child,
	// and record the spawner as the child's parent (so killing the parent cascades).
	char abs[300]; ResolvePath (pPath, abs, sizeof abs);
	CAddressSpace *pAS = CurrentAS ();
	unsigned nParent = pAS != 0 ? pAS->GetPid () : 0;
	CProcess *pProc = SpawnProcess (abs, pArgs, (CStream *) In.Obj (), (CStream *) Out.Obj (),
					CurCwd (), nParent);
	if (pProc == 0)
	{
		pTable->Close (h, HANDLE_RESERVED);
		return 0;
	}
	if (!pTable->Fill (h, pProc, HANDLE_PROCESS))	// (the record's refs: this handle's + the child's)
	{
		ProcessRelease (pProc);			// (not possible: nothing else closes a reserved one)
		return 0;
	}
	return h;
}

// Change the calling task's working directory: resolve pPath, verify it is a real
// directory (f_opendir), and store it. Returns 1 on success, 0 otherwise.
int kapi_chdir (const char *pUserPath)
{
	CAddressSpace *pAS = CurrentAS ();
	CUserStr Path (pUserPath, UPATH_MAX);
	const char *pPath = Path.Get ();
	if (pAS == 0 || pPath == 0) return 0;
	char abs[300]; ResolvePath (pPath, abs, sizeof abs);
	if (RamFsHandles (abs))
	{
		if (!RamFsIsDirPath (abs)) return 0;
		pAS->SetCwd (abs);
		return 1;
	}
	DIR Dir;
	if (f_opendir (&Dir, abs) != FR_OK) return 0;	// not a directory
	f_closedir (&Dir);
	pAS->SetCwd (abs);
	return 1;
}

// Current working directory into pBuf. Returns the length.
int kapi_getcwd (char *pBuf, unsigned nMax)
{
	if (pBuf == 0 || nMax == 0) return 0;
	int n = UserStrOut (pBuf, nMax, CurCwd ());
	return n > 0 ? n : 0;
}

// Wait (cooperatively) for a spawned process to finish; returns its exit status and
// closes the handle. -1: not a process handle of the caller.
int kapi_wait (void *pProc)
{
	int nStatus;
	{
		CHandleUse Use (pProc, HANDLE_PROCESS);	// (pinned while it sleeps)
		CProcess *p = (CProcess *) Use.Obj ();
		if (p == 0) return -1;
		while (!p->bDone)
		{
			if (!CScheduler::IsActive ()) break;
			CScheduler::Get ()->MsSleep (5);
		}
		nStatus = p->nStatus;
	}
	HandleClose (pProc, HANDLE_PROCESS);		// (nothing if another thread closed it meanwhile)
	return nStatus;
}

// Non-blocking poll: 1 if the spawned process has finished (else 0; 1 too for a bad
// handle, as for 0 before). Does NOT close the handle (kapi_wait does). Lets the
// terminal detect completion without blocking.
int kapi_proc_done (void *pProc)
{
	CHandleTable *pTable = HandlesCurrent ();
	CProcess *p = pTable != 0 ? (CProcess *) pTable->Get (pProc, HANDLE_PROCESS) : 0;
	return (p == 0 || p->bDone) ? 1 : 0;
}

// Copy this task's argv string (set at spawn) into pBuf. Returns length.
int kapi_get_args (char *pBuf, unsigned nMax)
{
	if (pBuf == 0 || nMax == 0) return 0;
	CAddressSpace *pAS = CurrentAS ();
	int n = UserStrOut (pBuf, nMax, pAS != 0 ? pAS->GetArgs () : "");
	return n > 0 ? n : 0;
}

// --- directory listing -------------------------------------------------------

void *kapi_opendir (const char *pUserPath)
{
	CUserStr Path (pUserPath, UPATH_MAX);
	const char *pPath = Path.Get ();
	if (pPath == 0)
	{
		return 0;
	}
	char abs[300]; ResolvePath (pPath, abs, sizeof abs);
	if (RamFsHandles (abs)) return HandleNew (RamFsOpenDir (abs), HANDLE_DIR, HKIND_RAMFS);
	if (VfsHandles (pPath)) return HandleNew (VfsOpenDir (pPath), HANDLE_DIR, HKIND_VFS);
	DIR *pDir = new DIR;
	if (pDir == 0)
	{
		return 0;
	}
	if (f_opendir (pDir, abs) != FR_OK)
	{
		delete pDir;
		return 0;
	}
	OFileNoteDir (pDir, abs);			// (v75: dir_read's ino)
	return HandleNew (pDir, HANDLE_DIR, HKIND_FATFS);
}

static int ReadDir (CHandleUse &Use, struct kapi_dirent *pEnt)	// (pEnt: kernel memory)
{
	void *pObj = Use.Obj ();
	if (Use.Kind () == HKIND_RAMFS) return RamFsReadDir (pObj, pEnt);
	if (Use.Kind () == HKIND_VFS) return VfsReadDir (pObj, pEnt);
	FILINFO Info;
	if (f_readdir ((DIR *) pObj, &Info) != FR_OK || Info.fname[0] == '\0')
	{
		return 0;			// error or end of directory
	}
	unsigned i = 0;
	for (; Info.fname[i] != '\0' && i < sizeof (pEnt->name) - 1; i++)
	{
		pEnt->name[i] = Info.fname[i];
	}
	pEnt->name[i] = '\0';
	pEnt->size = Info.fsize > 0xFFFFFFFFu ? 0xFFFFFFFFu : (unsigned) Info.fsize;	// (over 4 GB: fsize64 once opened)
	pEnt->is_dir = (Info.fattrib & AM_DIR) ? 1 : 0;
	return 1;
}

int kapi_readdir (void *pHandle, struct kapi_dirent *pEnt)
{
	if (pEnt == 0 || !UserRange (pEnt, sizeof *pEnt))	// (checked before an entry is read)
	{
		return 0;
	}
	CHandleUse Use (pHandle, HANDLE_DIR);
	if (Use.Obj () == 0)
	{
		return 0;
	}
	struct kapi_dirent Ent;
	memset (&Ent, 0, sizeof Ent);
	int r = ReadDir (Use, &Ent);
	if (r <= 0)
	{
		return r;
	}
	return UserPut (pEnt, Ent) ? r : 0;
}

void kapi_closedir (void *pHandle)
{
	HandleClose (pHandle, HANDLE_DIR);
}

// --- file operations ---------------------------------------------------------

int kapi_mkdir (const char *pUserPath)
{
	CUserStr Path (pUserPath, UPATH_MAX);
	const char *pPath = Path.Get ();
	if (pPath == 0) return -1;
	char abs[300]; ResolvePath (pPath, abs, sizeof abs);
	if (RamFsHandles (abs)) return RamFsMkdir (abs);
	if (VfsHandles (pPath)) return VfsCall (VFS_OP_MKDIR, pPath, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0 ? 0 : -1;
	return (f_mkdir (abs) == FR_OK) ? 0 : -1;
}

int kapi_remove (const char *pUserPath)		// file or empty directory
{
	CUserStr Path (pUserPath, UPATH_MAX);
	const char *pPath = Path.Get ();
	if (pPath == 0) return -1;
	char abs[300]; ResolvePath (pPath, abs, sizeof abs);
	if (RamFsHandles (abs)) return RamFsRemove (abs);
	if (VfsHandles (pPath)) return VfsCall (VFS_OP_REMOVE, pPath, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0 ? 0 : -1;
	ImageFileChanged (abs);				// (v77: a program's image loses its name)
	return (f_unlink (abs) == FR_OK) ? 0 : -1;
}

int kapi_rename (const char *pUserFrom, const char *pUserTo)
{
	CUserStr From (pUserFrom, UPATH_MAX), To (pUserTo, UPATH_MAX);
	const char *pFrom = From.Get (), *pTo = To.Get ();
	if (pFrom == 0 || pTo == 0) return -1;
	{
		char ramF[300], ramT[300];				// RAM: -> RAM: only
		ResolvePath (pFrom, ramF, sizeof ramF);
		ResolvePath (pTo, ramT, sizeof ramT);
		if (RamFsHandles (ramF) || RamFsHandles (ramT))
			return RamFsHandles (ramF) && RamFsHandles (ramT) ? RamFsRename (ramF, ramT) : -1;
	}
	if (VfsHandles (pFrom) || VfsHandles (pTo))		// both on the same provider only
		return (VfsHandles (pFrom) && VfsHandles (pTo)
			&& VfsCall (VFS_OP_RENAME, pFrom, pTo, 0, 0, 0, 0, 0, 0, 0, 0) == 0) ? 0 : -1;
	char absF[300], absT[300];
	ResolvePath (pFrom, absF, sizeof absF);
	ResolvePath (pTo, absT, sizeof absT);
	// f_rename ignores the new name's volume (it renames within the old one): across
	// volumes (SD: -> SD1:) fail, the caller copies then deletes.
	int vf = VolumePrefix (absF), vt = VolumePrefix (absT);
	if (vf != vt) return -1;
	for (int i = 0; i < vf; i++) if (absF[i] != absT[i]) return -1;
	ImageFileChanged (absF);			// (v77: the images of both names, and of the
	ImageFileChanged (absT);			//  programs under a renamed folder)
	return (f_rename (absF, absT) == FR_OK) ? 0 : -1;
}

// --- modal dialogs -----------------------------------------------------------

// Write a whole file (create/truncate). Returns bytes written, or -1 on error.
int kapi_save_file (const char *pUserPath, const void *pBuf, unsigned nLen)
{
	CUserStr Path (pUserPath, UPATH_MAX);
	const char *pPath = Path.Get ();
	if (pPath == 0 || !UserReadable (pBuf, nLen))
	{
		return -1;
	}
	{
		char ram[300]; ResolvePath (pPath, ram, sizeof ram);
		if (RamFsHandles (ram)) return RamFsSave (ram, pBuf, nLen);	// RAM:
	}
	if (VfsHandles (pPath))
	{
		int n = VfsCall (VFS_OP_SAVE, pPath, 0, 0, 0, 0, pBuf, nLen, 0, 0, 0);
		return n >= 0 ? n : -1;
	}
	char abs[300]; ResolvePath (pPath, abs, sizeof abs);
	ImageFileChanged (abs);				// (v77: a program's image loses its name)
	FIL File;
	if (f_open (&File, abs, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return -1;
	}
	UINT nWritten = 0;
	FRESULT Res = ChunkedWrite (&File, pBuf, nLen, &nWritten);
	f_close (&File);
	ImageFileChanged (abs);				// (one made from the half-written file meanwhile)
	return (Res == FR_OK) ? (int) nWritten : -1;
}

// --- networking (TCP sockets over WLAN) --------------------------------------
//
// Thin shims over the socket backend in sys/net.cpp. These run on the app's task,
// so Connect/DNS/Send block cooperatively (the rest of the system keeps running);
// Recv is non-blocking so a GUI app can poll it from its event loop.

// An address as text ("192.168.1.10", an IPv6 one): the net layer writes it into kernel memory,
// copied out to the app.
#define IP_TEXT_MAX	64

int kapi_net_status (char *pIP, unsigned nCap)
{
	boolean bOut = pIP != 0 && nCap > 0;
	if (bOut && !UserRange (pIP, nCap < IP_TEXT_MAX ? nCap : IP_TEXT_MAX)) return 0;
	char IP[IP_TEXT_MAX]; IP[0] = '\0';
	int n = NetStatus (IP, sizeof IP);
	if (bOut) UserStrOut (pIP, nCap, IP);
	return n;
}

// A host name: DNS allows 253 characters.
#define HOST_MAX	256

int kapi_tcp_connect (const char *pUserHost, unsigned nPort)
{
	CUserStr Host (pUserHost, HOST_MAX);
	if (!Host.OK ()) return -1;
	CAddressSpace *pAS = CurrentAS ();
	unsigned nPid = (pAS != 0) ? pAS->GetPid () : 0;	// owner -> auto-close on death
	return NetTcpConnect (Host.Get (), nPort, nPid);
}

// Is the calling process (pid nMe) a descendant of process nAncestor? (Its parent chain,
// as long as the parents live: a few levels at most.)
static unsigned ParentPidOf (unsigned nPid);
static boolean IsDescendantOf (unsigned nMe, unsigned nAncestor)
{
	CAddressSpace *pAS = CurrentAS ();
	unsigned nPid = pAS != 0 && pAS->GetPid () == nMe ? pAS->GetParentPid () : 0;
	for (unsigned nDepth = 0; nPid != 0 && nDepth < 16; nDepth++)
	{
		if (nPid == nAncestor) return TRUE;
		nPid = ParentPidOf (nPid);
	}
	return FALSE;
}

// May the caller use socket h? Its own; or one of an ancestor's, which it adopts (it is
// closed when the caller dies, no longer when that ancestor does): ftpd hands a client's
// socket number to the session process it spawns. Another process's socket: a bad handle
// (the net layer checks the owner again for each request).
void SocketAdopt (int hSock)
{
	CAddressSpace *pAS = CurrentAS ();
	unsigned nMe = pAS != 0 ? pAS->GetPid () : 0;
	unsigned nOwner = NetSocketOwner (hSock);
	if (nMe == 0 || nOwner == 0 || nOwner == nMe) return;
	if (IsDescendantOf (nMe, nOwner))
	{
		NetSocketAdopt (hSock, nOwner, nMe);
	}
}

int kapi_tcp_send (int hSock, const void *pBuf, unsigned nLen)
{
	if (!UserReadable (pBuf, nLen)) return -1;
	SocketAdopt (hSock);
	return NetTcpSend (hSock, pBuf, nLen);
}
int kapi_tcp_recv (int hSock, void *pBuf, unsigned nLen)
{
	if (!UserWritable (pBuf, nLen)) return -1;
	SocketAdopt (hSock);
	return NetTcpRecv (hSock, pBuf, nLen);
}
void kapi_tcp_close (int hSock)                                  { SocketAdopt (hSock); NetTcpClose (hSock); }

// --- v37: TCP server side ----------------------------------------------------
int kapi_tcp_listen (unsigned nPort)
{
	CAddressSpace *pAS = CurrentAS ();
	return NetTcpListen (nPort, (pAS != 0) ? pAS->GetPid () : 0);
}

int kapi_tcp_accept (int hListen, char *pIP, unsigned nCap)
{
	boolean bOut = pIP != 0 && nCap > 0;			// (checked before a client is taken)
	if (bOut && !UserRange (pIP, nCap < IP_TEXT_MAX ? nCap : IP_TEXT_MAX)) return -1;
	SocketAdopt (hListen);
	CAddressSpace *pAS = CurrentAS ();
	char IP[IP_TEXT_MAX]; IP[0] = '\0';
	int n = NetTcpAccept (hListen, IP, sizeof IP, (pAS != 0) ? pAS->GetPid () : 0);
	if (n >= 0 && bOut) UserStrOut (pIP, nCap, IP);
	return n;
}

// --- memory info -------------------------------------------------------------
// System memory snapshot (all sizes in KB): total RAM, free (page-allocator region
// not yet handed out + free heap), memory owned by user apps (g_nUserPages frames),
// and the page size. For the memory monitor + a future user allocator.
int kapi_meminfo (unsigned long *pTotalKB, unsigned long *pFreeKB,
		  unsigned long *pAppKB, unsigned *pPageKB)
{
	CMemorySystem *pMem = CMemorySystem::Get ();
	// Total managed RAM = low region + the FULL high zone (all pager segments, including
	// the [3-4GB] and >4GB RAM the zone allocator reclaimed). GetMemSize() only counts
	// low + seg0, so it under-reports on boards where we reclaimed more -- use the sum.
	unsigned long nTotal = pMem != 0
		? (unsigned long) (CMemorySystem::GetLowMemSize () + CMemorySystem::GetHighZoneTotal ())
		: 0;
	// Free = never-allocated region + freed-and-reusable blocks/pages, for both the
	// heap and the page allocator -- so it rises again when memory is freed.
	unsigned long nHeap  = pMem != 0 ? (unsigned long) (pMem->GetHeapFreeSpace (HEAP_ANY)
						 + pMem->GetHeapFreeListSpace ()) : 0;
	// Pager free = low pager (page tables, DMA-critical) + HIGH pager (app frames/heaps).
	unsigned long nPager = (unsigned long) (CMemorySystem::GetPagerFreeSpace ()
						 + CMemorySystem::GetPagerFreeListSpace ()
						 + CMemorySystem::GetPagerHighFreeSpace ()
						 + CMemorySystem::GetPagerHighFreeListSpace ());
	// (v77: + the program images' shared frames, held once whatever the number of processes)
	unsigned long nApp   = (unsigned long) (g_nUserPages + ImagePagesTotal ()) * (unsigned long) KPAGE_SIZE;

	if (!OutOK (pTotalKB) || !OutOK (pFreeKB) || !OutOK (pAppKB) || !OutOK (pPageKB)) return 0;
	OutPut (pTotalKB, nTotal / 1024);
	OutPut (pFreeKB,  (nHeap + nPager) / 1024);
	OutPut (pAppKB,   nApp / 1024);
	OutPut (pPageKB,  (unsigned) (KPAGE_SIZE / 1024));
	return 1;
}

// Detail beyond meminfo (ABI v33): the firmware-detected physical RAM (e.g. 4096 MB even
// though meminfo's managed total is ~3 GB), plus the size and free space of the HIGH page
// zone that backs app frames (palloc_high). All sizes in KB; any out-ptr may be 0.
int kapi_ram_detail (unsigned long *pDetectedKB, unsigned long *pAppPoolKB,
		     unsigned long *pAppPoolFreeKB, unsigned long *pAbove4GKB,
		     unsigned *pNSegments)
{
	CMachineInfo *pInfo = CMachineInfo::Get ();
	unsigned long nDetected = pInfo != 0 ? (unsigned long) pInfo->GetRAMSize () * 1024UL : 0;
	unsigned long nPool = (unsigned long) (CMemorySystem::GetHighZoneTotal () / 1024);
	unsigned long nFree = (unsigned long) ((CMemorySystem::GetPagerHighFreeSpace ()
						 + CMemorySystem::GetPagerHighFreeListSpace ()) / 1024);
	unsigned long nAbove4G = (unsigned long) (CMemorySystem::GetHighMem4GSize () / 1024);

	if (   !OutOK (pDetectedKB) || !OutOK (pAppPoolKB) || !OutOK (pAppPoolFreeKB)
	    || !OutOK (pAbove4GKB) || !OutOK (pNSegments)) return 0;
	OutPut (pDetectedKB,    nDetected);
	OutPut (pAppPoolKB,     nPool);
	OutPut (pAppPoolFreeKB, nFree);
	OutPut (pAbove4GKB,     nAbove4G);
	OutPut (pNSegments,     (unsigned) CMemorySystem::GetHighSegCount ());
	return 1;
}

// Grow/shrink the calling process's heap by nIncrement bytes (Unix sbrk). Returns
// the previous break, or (void*)-1 on failure. The user-space allocator (user/Runtime/umm.h)
// builds malloc/free + operator new/delete on top of this.
void *kapi_sbrk (long nIncrement)
{
	CAddressSpace *pAS = CurrentAS ();
	if (pAS == 0) return (void *) -1;
	return pAS->Sbrk (nIncrement);
}

// --- v25: reboot --------------------------------------------------------------
// Apply boot-time-only settings (e.g. a freshly written wpa_supplicant.conf) by
// restarting the machine. Circle's reboot() is NORETURN.
void kapi_reboot (void)
{
	CrashLogCleanEnd ();
	CrashLogClockSave ();
	reboot ();
}

// --- v40: clipboard / window opacity / shutdown ------------------------------
// System clipboard: one typed blob (1 = text, 2 = file path(s), '\n'-separated) kept
// by the kernel, so it survives the app that copied. clipboard_get copies up to nCap
// bytes, returns the full length (0 = empty), fills *pType; the serial changes on
// every set (to refresh a Paste menu cheaply).
#define CLIPBOARD_MAX	(64 * 1024)
static u8	s_Clip[CLIPBOARD_MAX];
static unsigned	s_nClipLen = 0, s_nClipSerial = 0;
static int	s_nClipType = 0;

int kapi_clipboard_set (int nType, const void *pData, unsigned nLen)
{
	if (nLen > CLIPBOARD_MAX) nLen = CLIPBOARD_MAX;
	if (pData == 0) nLen = 0;
	if (!UserReadable (pData, nLen)) return 0;		// (the clipboard left as it was)
	if (nLen) memcpy (s_Clip, pData, nLen);
	s_nClipLen = nLen;
	s_nClipType = nLen ? nType : 0;
	s_nClipSerial++;
	return (int) nLen;
}

int kapi_clipboard_get (int *pType, void *pBuf, unsigned nCap, unsigned *pSerial)
{
	unsigned n = s_nClipLen < nCap ? s_nClipLen : nCap;
	if (!OutOK (pType) || !OutOK (pSerial) || (pBuf != 0 && !UserRange (pBuf, n))) return 0;
	OutPut (pType, s_nClipType);
	OutPut (pSerial, s_nClipSerial);
	if (pBuf != 0 && n && !UserCopyOut (pBuf, s_Clip, n)) return 0;
	return (int) s_nClipLen;
}

// --- v41: full-screen apps --------------------------------------------------------
extern C2DGraphics *g_pGraphics;

// Take the whole screen: returns the VA of a screen-sized 0x00RRGGBB back buffer (+ its
// size); the compositor stops drawing and all input goes to the caller's window (made
// if it has none; moved to 0,0, so pointer coordinates are screen coordinates). Draw,
// then kapi_present_fb. 0 on failure.
// The window's place before the full screen, given back by kapi_fullscreen_end (one full-
// screen window at a time).
static CWindow *s_pFsWin = 0; static int s_nFsX, s_nFsY;

unsigned *kapi_fullscreen_begin (int *pW, int *pH)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindowManager *pWM = CWindowManager::Get ();
	if (pAS == 0 || pWM == 0 || g_pGraphics == 0 || !OutOK (pW) || !OutOK (pH))
	{
		return 0;
	}
	// (the caller's window is the graphics server's -- AppKit asks it for one first -- and what the
	// kernel has of it is the program's queue of events: the server sends that program all the
	// input while it has the full screen)
	if (pAS->GetWindow () == 0)
	{
		return 0;
	}
	u64 ulPhys = 0; unsigned nPages = 0;
	if (pWM->EnsureFullscreenBuffer (g_nScreenWidth, g_nScreenHeight, &ulPhys, &nPages) == 0)
	{
		return 0;
	}
	pAS->MapContig (USER_FULLSCREEN_CANVAS, ulPhys, nPages, KPAGE_ATTR_APP_DATA);
	pAS->FlushTLB ();					// (a new buffer after a change of resolution)
	CWindow *pWin = pAS->GetWindow ();
	memset ((void *) ulPhys, 0, (size_t) g_nScreenWidth * g_nScreenHeight * 4);
	pWM->SetFullscreen (pWin);
	s_pDirectWin = 0;
	OutPut (pW, g_nScreenWidth);
	OutPut (pH, g_nScreenHeight);
	WsFullscreen (pAS->GetPid (), TRUE);		// (the graphics server: all the input is this program's)
	return (unsigned *) USER_FULLSCREEN_CANVAS;
}

// The displayed framebuffer, mapped in the full-screen app: it draws (and the GPU renders)
// there; present_fb then copies nothing. Circle's C2DGraphics here is on the firmware's
// framebuffer (CBcmFrameBuffer: its display), 32 bits a pixel, 0x00RRGGBB as the back buffer.
unsigned *kapi_fullscreen_direct (int *pW, int *pH, int *pStride)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindowManager *pWM = CWindowManager::Get ();
	if (pAS == 0 || pWM == 0 || g_pGraphics == 0 || pWM->FullscreenWindow () == 0
	    || pWM->FullscreenWindow () != pAS->GetWindow ()
	    || !OutOK (pW) || !OutOK (pH) || !OutOK (pStride))
	{
		return 0;
	}
	CBcmFrameBuffer *pFB = (CBcmFrameBuffer *) g_pGraphics->GetDisplay ();
	if (pFB == 0 || (int) pFB->GetWidth () != g_nScreenWidth || (int) pFB->GetHeight () != g_nScreenHeight)
	{
		return 0;
	}
	unsigned nPitch = 0;
	unsigned *pScreen = MapScreen (pAS, &nPitch);
	if (pScreen == 0)
	{
		return 0;
	}
	s_pDirectWin = pAS->GetWindow ();
	OutPut (pW, g_nScreenWidth);
	OutPut (pH, g_nScreenHeight);
	OutPut (pStride, (int) (nPitch / 4));
	return pScreen;
}

// Show the full-screen back buffer (copy to the framebuffer + present), then yield.
void kapi_present_fb (void)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindowManager *pWM = CWindowManager::Get ();
	if (pAS != 0 && pWM != 0 && g_pGraphics != 0 && pWM->FullscreenWindow () != 0
	    && pWM->FullscreenWindow () == pAS->GetWindow () && FsDirect (pWM))
	{
		ScreenDirty ();				// (already on the screen)
	}
	else if (pAS != 0 && pWM != 0 && g_pGraphics != 0 && pWM->FullscreenWindow () != 0
	    && pWM->FullscreenWindow () == pAS->GetWindow () && pWM->FullscreenBuffer () != 0)
	{
		// The desktop's last frame may still be on its way (the compositor yields during its
		// display DMA; the full screen was taken meanwhile): wait for it, then check again --
		// the wait yields (the window closed, the full screen given back by another thread).
		DisplayPresentIdle ();
		if (pWM->FullscreenWindow () != 0 && pWM->FullscreenWindow () == pAS->GetWindow ()
		    && pWM->FullscreenBuffer () != 0)
		{
			unsigned nW = g_pGraphics->GetWidth (), nH = g_pGraphics->GetHeight ();
			if ((int) nW == g_nScreenWidth && (int) nH == g_nScreenHeight)
			{
				memcpy (g_pGraphics->GetBuffer (), pWM->FullscreenBuffer (), (size_t) nW * nH * 4);
			}
			g_pGraphics->UpdateDisplay ();
			ScreenDirty ();				// a new frame: kapi_screen_grab (vncd) must see it
		}
	}
	if (CScheduler::IsActive ())
	{
		CScheduler::Get ()->Yield ();
	}
}

// Give the screen back to the desktop (also automatic when the app exits).
void kapi_fullscreen_end (void)
{
	{ CAddressSpace *pWsAS = CurrentAS (); if (pWsAS != 0) WsFullscreen (pWsAS->GetPid (), FALSE); }
	CAddressSpace *pAS = CurrentAS ();
	CWindowManager *pWM = CWindowManager::Get ();
	if (pAS != 0 && pWM != 0 && pWM->FullscreenWindow () != 0 && pWM->FullscreenWindow () == pAS->GetWindow ())
	{
		s_pDirectWin = 0;
		pWM->SetFullscreen (0);			// (the graphics server puts the window back where it was)
	}
}

// End the session: unmount the SD card (flushes FatFs), then restart (mode 1) or
// halt (mode 0: the CPU stops, the screen keeps the last frame -- "safe to power
// off"; the ACT LED goes dark).
void kapi_shutdown (int nMode)
{
	CLogger::Get ()->Write ("kernel", LogNotice, "session end: %s", nMode ? "restart" : "halt");
	CScheduler::Get ()->MsSleep (300);		// let the last frame / log line out
	CrashLogCleanEnd ();				// (a clean end: no crash report, no watchdog)
	CrashLogClockSave ();				// (the time for the next boot)
	f_mount (0, "SD:", 0);				// unmount: flush + release the volumes
	f_mount (0, "SD1:", 0); f_mount (0, "SD2:", 0); f_mount (0, "SD3:", 0);
	if (nMode == 1)
	{
		reboot ();
	}
	CActLED::Get ()->Off ();
	halt ();
}


// --- v42: drag & drop + keyboard modifiers ----------------------------------------
// The payload of the current / last drag session (kept until the next drag_begin, so
// the drop target can read it after GUI_EVENT_DROP).
#define DND_MAX		4096
static u8	s_Dnd[DND_MAX];
static unsigned	s_nDndLen = 0;
static int	s_nDndType = 0;

// Current keyboard modifiers (MOD_CTRL / MOD_SHIFT / MOD_ALT).
// While a key handler runs, these are the modifiers held when that key was typed.
unsigned kapi_get_modifiers (void)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin != 0 && pWin->m_nKeyEventMods != 0xFFFFFFFF) return pWin->m_nKeyEventMods;
	CWindowManager *pWM = CWindowManager::Get ();
	return pWM != 0 ? pWM->Modifiers () : 0;
}

// vncd: set the modifier state (RFB key events for Control / Shift / Alt).
void kapi_inject_modifiers (unsigned nMods)
{
	CWindowManager *pWM = CWindowManager::Get ();
	WsInputMods (nMods & (MOD_CTRL | MOD_SHIFT | MOD_ALT));	// (and kept here: kapi_get_modifiers)
	if (pWM != 0) pWM->SetModifiers (nMods & (MOD_CTRL | MOD_SHIFT | MOD_ALT));
}

// --- v43: network tools --------------------------------------------------------------
int kapi_net_ping (const char *pUserHost, unsigned nSeq, unsigned nTimeoutMs, char *pIP, unsigned nCap)
{
	CUserStr Host (pUserHost, HOST_MAX);
	boolean bOut = pIP != 0 && nCap > 0;
	if (!Host.OK () || (bOut && !UserRange (pIP, nCap < IP_TEXT_MAX ? nCap : IP_TEXT_MAX))) return -1;
	char IP[IP_TEXT_MAX]; IP[0] = '\0';
	int n = NetPing (Host.Get (), nSeq, nTimeoutMs, IP, sizeof IP);
	if (bOut) UserStrOut (pIP, nCap, IP);
	return n;
}
int kapi_net_resolve (const char *pUserHost, char *pIP, unsigned nCap)
{
	CUserStr Host (pUserHost, HOST_MAX);
	boolean bOut = pIP != 0 && nCap > 0;
	if (!Host.OK () || (bOut && !UserRange (pIP, nCap < IP_TEXT_MAX ? nCap : IP_TEXT_MAX))) return 0;
	char IP[IP_TEXT_MAX]; IP[0] = '\0';
	int n = NetResolve (Host.Get (), IP, sizeof IP);
	if (n > 0 && bOut) UserStrOut (pIP, nCap, IP);
	return n;
}
int kapi_net_info (char *pBuf, unsigned nCap)
{
	if (pBuf != 0 && !UserWritable (pBuf, nCap)) return 0;
	return NetInfo (pBuf, nCap);
}
int kapi_wlan_scan (struct kapi_wlan_ap *pOut, int nMax)
{
	if (pOut != 0 && nMax > 0 && !UserWritable (pOut, (u64) nMax * sizeof *pOut)) return 0;
	return NetWlanScan (pOut, nMax);
}
int kapi_wlan_reconnect (void) { return NetWlanReconnect (); }

// --- v46: sound (kern/sound.h) ---
static unsigned CallerPid (void) { CAddressSpace *pAS = CurrentAS (); return pAS != 0 ? pAS->GetPid () : 0; }
// (v85: a channel of the mixer, named after the program -- its main task's name without the folder)
int  kapi_sound_acquire (void)
{
	CAddressSpace *pAS = CurrentAS ();
	const char *pName = "app";
	if (pAS != 0 && pAS->GetMainTask () != 0) pName = pAS->GetMainTask ()->GetName ();
	const char *pBase = pName;
	for (const char *p = pName; *p != '\0'; p++) if (*p == '/' || *p == ':') pBase = p + 1;
	return SoundAcquire (CallerPid (), pBase);
}
void kapi_sound_release (void) { SoundRelease (CallerPid ()); }
// (retired 2026-10-05: the synthesizer left the kernel -- AudioKit: ak_fm_start / ak_fm_stop /
// ak_fm_instrument, user/Kits/audiokit --; the three slots stay in the table and answer -1)
int  kapi_sound_start (int, unsigned, int, int) { return -1; }
int  kapi_sound_stop (int) { return -1; }
int kapi_sound_write (const short *pFrames, unsigned nFrames)
{
	// (the ring holds half a second: no call takes more frames than that -- what is read)
	if (nFrames > SND_RATE / 2) nFrames = SND_RATE / 2;
	if (pFrames == 0 || !UserReadable (pFrames, (u64) nFrames * 2 * sizeof (short))) return -1;
	return SoundWrite (CallerPid (), (const s16 *) pFrames, nFrames);	// (read under its lock)
}
int kapi_sound_status (unsigned *pRate, unsigned *pFree, unsigned *pOwner)
{
	unsigned nRate = 0, nFree = 0, nOwner = 0;		// (written under its lock: kernel memory)
	int r = SoundStatus (CallerPid (), &nRate, &nFree, &nOwner);
	if (OutOK (pRate)) OutPut (pRate, nRate);
	if (OutOK (pFree)) OutPut (pFree, nFree);
	if (OutOK (pOwner)) OutPut (pOwner, nOwner);
	return r;
}
int  kapi_sound_volume (int nVolume, int nMute) { return SoundVolume (nVolume, nMute); }
int  kapi_sound_output (int nOut) { return SoundOutput (nOut); }	// (v84) which output plays
// (v85) the mixer: its channels, a channel's volume
int kapi_sound_clients (struct kapi_sound_client *pOut, int nMax)
{
	if (pOut == 0 || nMax <= 0) return SoundClients (0, 0);
	if (nMax > 16) nMax = 16;
	struct kapi_sound_client List[16];
	int n = SoundClients (List, nMax);
	int k = n < nMax ? n : nMax;
	if (!UserWritable (pOut, (u64) k * sizeof (struct kapi_sound_client))) return -1;
	memcpy (pOut, List, (size_t) k * sizeof (struct kapi_sound_client));
	return n;
}
int kapi_sound_client_volume (unsigned nPid, int nVolume, int nMute) { return SoundClientVolume (nPid, nVolume, nMute); }
int kapi_sound_instrument (int, const struct kapi_fm_instrument *) { return -1; }	// (retired: see kapi_sound_start)

// --- v68: low-latency sound ---
int kapi_sound_config (int nChunkFrames, int nAhead) { return SoundConfig (CallerPid (), nChunkFrames, nAhead); }

// The ring's page mapped into the owner, like a shared surface (its frames are the kernel's:
// the space's teardown drops the mapping only). Mapped once per process: the surface arena
// is a bump allocator, so the address is kept for the next call of the same process.
struct kapi_sound_ring *kapi_sound_map (void)
{
	static unsigned s_nMapPid = 0;
	static void *s_pMapVA = 0;
	CAddressSpace *pAS = CurrentAS ();
	if (pAS == 0)
	{
		return 0;
	}
	struct kapi_sound_ring *pRing = SoundRing (pAS->GetPid ());
	if (pRing == 0)
	{
		return 0;
	}
	if (s_nMapPid != pAS->GetPid () || s_pMapVA == 0)
	{
		void *pVA = pAS->MapSurface ((u64) (uintptr) pRing, SND_RING_PAGE / KPAGE_SIZE);
		if (pVA == 0)
		{
			return 0;			// (its surface arena is full)
		}
		s_nMapPid = pAS->GetPid ();
		s_pMapVA = pVA;
	}
	return (struct kapi_sound_ring *) s_pMapVA;
}

// --- v48: held keys ---
int kapi_key_held (int nKey)
{
	CWindowManager *pWM = CWindowManager::Get ();
	CAddressSpace *pAS = CurrentAS ();
	// (the graphics server says who has the keyboard: kern/wsrv.h)
	return pWM != 0 && pAS != 0 && WsFocusPid () == pAS->GetPid () && pWM->KeyHeldAny (nKey) ? 1 : 0;
}
void kapi_inject_key_held (int nKey, int bDown)
{
	CWindowManager *pWM = CWindowManager::Get ();
	WsInputHeld (nKey, bDown ? TRUE : FALSE);		// (and kept here: kapi_key_held)
	if (pWM != 0) pWM->SetInjectedHeld (nKey, bDown ? TRUE : FALSE);
}

// --- v68: USB MIDI input (kernel.cpp) ---
int kapi_midi_read (struct kapi_midi_event *pEv, int nMax)
{
	if (pEv != 0 && nMax > 0 && !UserWritable (pEv, (u64) nMax * sizeof *pEv)) return -1;
	return KernelMidiRead (pEv, nMax);
}
int kapi_midi_devices (void) { return KernelMidiDevices (); }

// --- v50: USB gamepads ---
int kapi_pad_state (int nIndex, struct kapi_pad *pOut)
{
	struct kapi_pad Pad;
	if (pOut == 0 || !UserRange (pOut, sizeof *pOut) || !KernelPadState (nIndex, &Pad)) return 0;
	CAddressSpace *pAS = CurrentAS ();
	Pad.focus = pAS != 0 && WsFocusPid () == pAS->GetPid () ? 1 : 0;	// (the graphics server says who has the keyboard)
	return UserPut (pOut, Pad) ? 1 : 0;
}

}  // extern "C"

extern "C" {

// --- v66: the screen's resolution, while running (kernel.cpp: the compositor does it) ---------
int kapi_screen_set (int w, int h)
{
	return ScreenResizeRequest (w, h);
}

// --- v69: the monitor's own resolution (its EDID), the time zone while running ---------------------
// The preferred timing is the EDID's first detailed timing descriptor (bytes 54..71): the active
// pixels, 8 low bits and the 4 high ones of a shared byte, horizontally then vertically.
int kapi_screen_native (int *pw, int *ph)
{
	if (pw == 0 || ph == 0 || !OutOK (pw) || !OutOK (ph)) return 0;
	CBcmPropertyTags Tags;
	TPropertyTagEDIDBlock Edid;
	Edid.nBlockNumber = EDID_FIRST_BLOCK;
	if (!Tags.GetTag (PROPTAG_GET_EDID_BLOCK, &Edid, sizeof Edid, 4) || Edid.nStatus != EDID_STATUS_SUCCESS) return 0;
	const u8 *b = Edid.Block;
	static const u8 Magic[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
	for (int i = 0; i < 8; i++) if (b[i] != Magic[i]) return 0;
	const u8 *d = b + 54;
	if (d[0] == 0 && d[1] == 0) return 0;			// (not a timing: a display descriptor)
	int w = d[2] | ((d[4] & 0xF0) << 4), h = d[5] | ((d[7] & 0xF0) << 4);
	if (w < 320 || h < 200) return 0;
	OutPut (pw, w); OutPut (ph, h);
	return 1;
}

int kapi_set_timezone (int nMinutes)
{
	if (nMinutes < -720 || nMinutes > 840) return 0;
	return CTimer::Get ()->SetTimeZone (nMinutes) ? 1 : 0;
}

// (v71) A volume's room: RAM: (kern/ramfs.h) or a FatFs volume ("SD:", "SD1:"...: f_getfree,
// from the FAT's free-cluster count -- FSINFO on FAT32, kept by FatFs once known).
int kapi_vol_info (const char *pUserPath, struct kapi_vol_info *pUserOut)
{
	CUserStr Path (pUserPath, UPATH_MAX);
	if (!Path.OK () || pUserOut == 0 || !UserRange (pUserOut, sizeof *pUserOut)) return -1;
	struct kapi_vol_info Out;				// (made here, copied out whole)
	struct kapi_vol_info *pOut = &Out;
	char abs[300]; ResolvePath (Path.Get (), abs, sizeof abs);
	memset (pOut, 0, sizeof *pOut);
	if (RamFsHandles (abs))
	{
		if (!RamFsMounted ()) return -1;
		u64 nTotal, nUsed, nFree; unsigned nFiles, nDirs;
		RamFsInfo (&nTotal, &nUsed, &nFree, &nFiles, &nDirs);
		pOut->total = nTotal; pOut->used = nUsed; pOut->free = nFree;
		pOut->files = nFiles; pOut->dirs = nDirs; pOut->flags = KAPI_VOL_RAM;
		strcpy (pOut->type, "RAM");
		return UserPut (pUserOut, Out) ? 0 : -1;
	}
	unsigned nVol = VolumePrefix (abs);
	if (nVol == 0) return -1;
	char Vol[16];
	if (nVol >= sizeof Vol - 1) return -1;
	memcpy (Vol, abs, nVol); Vol[nVol] = '\0';
	DWORD nFreeClust = 0; FATFS *pFs = 0;
	if (f_getfree (Vol, &nFreeClust, &pFs) != FR_OK || pFs == 0) return -1;
	u64 nClust = (u64) pFs->csize * FF_MAX_SS;
	pOut->total = (u64) (pFs->n_fatent - 2) * nClust;
	pOut->free = (u64) nFreeClust * nClust;
	pOut->used = pOut->total - pOut->free;
	strcpy (pOut->type, pFs->fs_type == FS_FAT12 ? "FAT12" : pFs->fs_type == FS_FAT16 ? "FAT16"
			  : pFs->fs_type == FS_FAT32 ? "FAT32" : "exFAT");
	return UserPut (pUserOut, Out) ? 0 : -1;
}

}  // extern "C"
