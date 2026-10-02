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
#include <kern/net.h>		// NetTcpConnect/Send/Recv/Close/Status (socket backend)
#include <kern/debugcon.h>
#include <kern/gui/gimage.h>
#include <kern/thread.h>		// (v67) threads, posts: ThreadsEndProcess
#include <kern/uaccess.h>		// the app's pointers: checked, copied fault-safe
#include <kern/ofile.h>		// (v75) ResolvePath & co. shared with sys/ofile.cpp, OFileNoteDir
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/timer.h>
#include <circle/time.h>
#include <circle/logger.h>
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

// Create the calling app's window. (x,y) is the outer top-left; pass x<0 or y<0 to
// auto-place at a pseudo-random on-screen position (normal apps). nFlags is a mask
// of WIN_FLAG_* (e.g. WIN_FLAG_BORDERLESS for the shell's panel/popup). Returns the
// canvas VA (USER_WINDOW_CANVAS) or 0 on failure. One window per process.
static unsigned *CreateWindow (int x, int y, int w, int h, const char *pTitle,
			       unsigned nFlags)
{
	CAddressSpace *pAS = CurrentAS ();
	if (pAS == 0 || w <= 0 || h <= 0 || w > g_nScreenWidth || h > g_nScreenHeight)	// (no bigger than the screen)
	{
		return 0;
	}
	if (pAS->GetWindow () != 0)
	{
		return (unsigned *) USER_WINDOW_CANVAS;		// one window per process
	}

	boolean bBorderless = (nFlags & WIN_FLAG_BORDERLESS) != 0;
	int nOuterW = w + (bBorderless ? 0 : 2 * WIN_BORDER);
	int nOuterH = h + (bBorderless ? 0 : WIN_TITLEBAR_H + WIN_BORDER);

	// Auto-placement (only when the caller didn't pin a position). The LCG state
	// persists across calls (so successive windows differ) and is seeded from the
	// timer (so it varies run to run). We keep a left margin to dodge the defective
	// left edge of the display.
	if (x < 0 || y < 0)
	{
		static unsigned s_nRng = 0;
		if (s_nRng == 0)
		{
			s_nRng = CTimer::Get ()->GetTicks () | 1u;	// seed once, never 0
		}
		int nXMin   = g_nScreenWidth / 5;			// skip the leftmost fifth
		int ax = 0, ay = 0, aw = g_nScreenWidth, ah = g_nScreenHeight;	// the work area: below
		if (CWindowManager::Get () != 0) CWindowManager::Get ()->WorkArea (&ax, &ay, &aw, &ah);	// the menu
		int nYMin   = ay;					// bar, above the dock
		int nXRange = g_nScreenWidth  - nOuterW - nXMin;
		int nYRange = ay + ah - nOuterH - nYMin;
		s_nRng = s_nRng * 1103515245u + 12345u;
		x = nXRange > 0 ? nXMin + (int) (s_nRng % (unsigned) nXRange) : 0;
		s_nRng = s_nRng * 1103515245u + 12345u;
		y = nYMin + (nYRange > 0 ? (int) (s_nRng % (unsigned) nYRange) : 0);
	}

	// pTitle: kernel memory (the kapi copied the app's) -- CWindow copies it. Fall back to a
	// default if null.
	CWindow *pWin = new CWindow (x, y, w, h, pTitle != 0 ? pTitle : "app", nFlags);
	if (pWin == 0 || !pWin->IsValid ())
	{
		return 0;
	}
	pWin->SetOwnerPid (pAS->GetPid ());			// drag & drop results name it

	TKPageAttr Attr = KPAGE_ATTR_APP_DATA;			// EL0 RW, ASID-tagged
	pAS->MapContig (USER_WINDOW_CANVAS, pWin->CanvasPhys (), pWin->CanvasPages (), Attr);
	if (pWin->HasChrome ())					// user-drawn window chrome buffers
	{
		pAS->MapContig (USER_WINDOW_CHROME,          pWin->ChromePhys (0), pWin->ChromePages (0), Attr);
		pAS->MapContig (USER_WINDOW_CHROME_INACTIVE, pWin->ChromePhys (1), pWin->ChromePages (1), Attr);
	}
	pAS->SetWindow (pWin);
	if (CWindowManager::Get () != 0)
	{
		CWindowManager::Get ()->Add (pWin);
	}

	return (unsigned *) USER_WINDOW_CANVAS;
}

// The title: a copy (a window's holds 47 characters; a longer one is cut, as before).
#define TITLE_MAX	64

unsigned *kapi_create_window (int w, int h, const char *pTitle)
{
	CUserStr Title (pTitle, TITLE_MAX, TRUE);
	if (!Title.OK () && !Title.IsNull ()) return 0;
	return CreateWindow (-1, -1, w, h, Title.Get (), 0);	// auto-placed, normal chrome
}

unsigned *kapi_create_window_ex (int x, int y, int w, int h, const char *pTitle,
				 unsigned nFlags)
{
	CUserStr Title (pTitle, TITLE_MAX, TRUE);
	if (!Title.OK () && !Title.IsNull ()) return 0;
	return CreateWindow (x, y, w, h, Title.Get (), nFlags);
}

// Resize the calling app's window to w x h (logical; clamped to the canvas it was
// created with). The canvas buffer/VA is unchanged -- create the window at the
// MAX size you'll need, then shrink/grow with this (e.g. a taskbar panel). Returns
// the canvas VA, or 0 on failure.
unsigned *kapi_resize_window (int w, int h)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin == 0)
	{
		return 0;
	}
	pWin->SetLogicalSize (w, h);
	return (unsigned *) USER_WINDOW_CANVAS;
}

// Move the calling app's window (outer top-left, screen coords). Used by borderless
// windows that re-position themselves (e.g. the panel keeping itself centered).
void kapi_move_window (int x, int y)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin != 0)
	{
		pWin->Move (x, y);
	}
}

// Draw text into the calling app's window canvas using the kernel bitmap font
// (transparent background -- only glyph pixels are written). Apps have no font of
// their own, so this is how an app-drawn UI (e.g. the editor) renders text.
// (a text drawn: one line, what is past 64 K characters cut)
#define DRAW_TEXT_MAX	0x10000

void kapi_draw_text (int x, int y, const char *pStr, unsigned nColor)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin == 0 || pStr == 0)
	{
		return;
	}
	CUserStr Text (pStr, DRAW_TEXT_MAX, TRUE);
	if (!Text.OK ())
	{
		return;
	}
	pWin->Canvas ()->DrawText (x, y, Text.Get (), (u32) nColor);
}

// Report the calling app's window surfaces so a user-side toolkit can draw the window
// chrome (decorations). Fills *out with the content canvas and, for a normal window,
// the active + inactive chrome copies (in the chrome buffer mapped at
// USER_WINDOW_CHROME), the chrome insets, and the title. For a borderless window the
// chrome pointers are 0. Returns 1, or 0 if the app has no window. The kernel keeps
// chrome BEHAVIOUR (title-bar drag, close-box hit-test) -- only the drawing moves here.
int kapi_get_chrome (struct kapi_chrome *out)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin != 0) { pWin->Damage (); pWin->ChromeTouch (); }	// the caller is about to (re)draw its chrome
	if (pWin == 0 || out == 0)
	{
		return 0;
	}
	struct kapi_chrome C;					// (made here, copied out whole)
	memset (&C, 0, sizeof C);
	C.content   = (unsigned *) USER_WINDOW_CANVAS;
	C.content_w = pWin->ClientWidth ();
	C.content_h = pWin->ClientHeight ();
	if (pWin->HasChrome ())
	{
		C.active   = (unsigned *) USER_WINDOW_CHROME;
		C.inactive = (unsigned *) USER_WINDOW_CHROME_INACTIVE;
		C.chrome_w = pWin->OuterW ();
		C.chrome_h = pWin->OuterH ();
		C.inset_l  = pWin->ChromeL ();
		C.inset_r  = pWin->ChromeR ();
		C.inset_t  = pWin->ChromeT ();
		C.inset_b  = pWin->ChromeB ();
	}
	const char *pTitle = pWin->Title ();
	unsigned i;
	for (i = 0; i + 1 < sizeof C.title && pTitle[i] != '\0'; i++)
	{
		C.title[i] = pTitle[i];
	}
	C.title[i] = '\0';
	return UserPut (out, C) ? 1 : 0;
}

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

// Register an app-level key handler for THIS window (void (sender=0, GUI_EVENT_KEY,
// keycode)). Keys reach it when the window is topmost and no textbox/textarea is
// focused. Pass 0 to clear.
void kapi_set_key_handler (void *pHandler)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin != 0)
	{
		pWin->SetKeyHandler ((u64) pHandler);
	}
}

// Register a canvas-click handler for THIS window: void (sender=0,
// GUI_EVENT_CANVAS_CLICK, (clientX<<16)|clientY) when a press hits no widget.
void kapi_set_click_handler (void *pHandler)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin != 0)
	{
		pWin->SetClickHandler ((u64) pHandler);
	}
}

// Register a full pointer-event handler for THIS window (GUI_EVENT_PTR_* stream).
// For app-side widget toolkits (uikit.h). See kapi_abi.h v22 for the value layout.
void kapi_set_pointer_handler (void *pHandler)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin != 0)
	{
		pWin->SetPointerHandler ((u64) pHandler);
	}
}

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

// Framebuffer size, for edge-pinned borderless windows (the shell panel/applist).
void kapi_screen_size (int *pW, int *pH)
{
	if (OutOK (pW)) OutPut (pW, g_nScreenWidth);
	if (OutOK (pH)) OutPut (pH, g_nScreenHeight);
}

// --- v39: system menu bar ----------------------------------------------------
// An app declares its menus on its window (spec: see kapi_abi.h) + the callback that
// receives GUI_EVENT_MENU; the menu-bar app reads the ACTIVE window's menu and sends
// the chosen command back to it.
int kapi_set_menu (const char *pSpec, void *pHandler)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin == 0)
	{
		return 0;
	}
	CUserStr Spec (pSpec, WIN_MENU_MAX, TRUE);		// (cut as before: the window's copy)
	if (!Spec.OK () && !Spec.IsNull ())
	{
		return 0;
	}
	pWin->SetMenu (Spec.Get (), (u64) pHandler);
	return 1;
}

// (filled in kernel memory -- the window manager's lock is held meanwhile --, then copied out)
unsigned kapi_get_menu (char *pBuf, unsigned nCap, char *pTitle, unsigned nTitleCap)
{
	CWindowManager *pWM = CWindowManager::Get ();
	if (pWM == 0)
	{
		return 0;
	}
	char Title[64];						// (a window's title: 47 characters)
	boolean bMenu = pBuf != 0 && nCap > 0, bTitle = pTitle != 0 && nTitleCap > 0;
	if (   (bMenu && !UserRange (pBuf, nCap < WIN_MENU_MAX ? nCap : WIN_MENU_MAX))
	    || (bTitle && !UserRange (pTitle, nTitleCap < sizeof Title ? nTitleCap : sizeof Title)))
	{
		return 0;
	}
	char *pMenu = bMenu ? new char[WIN_MENU_MAX] : 0;
	if (bMenu && pMenu == 0)
	{
		return 0;
	}
	unsigned nSerial = pWM->GetActiveMenu (pMenu, bMenu ? WIN_MENU_MAX : 0, Title, bTitle ? sizeof Title : 0);
	if (bMenu) UserStrOut (pBuf, nCap, pMenu);
	if (bTitle) UserStrOut (pTitle, nTitleCap, Title);
	delete [] pMenu;
	return nSerial;
}

int kapi_menu_command (int nID)
{
	CWindowManager *pWM = CWindowManager::Get ();
	return pWM != 0 && pWM->SendMenuCommand (nID) ? 1 : 0;
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
	GImage Screen ((u32 *) pDst, nW, nH);
	pWM->Composite (&Screen, FALSE);
	return 1;
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
	pWM->OnMouse (x, y, nButtons);
	if (nWheel != 0)
	{
		pWM->OnMouseWheel (x, y, nWheel);
	}
}

// Inject keyboard input as if typed: a key string in the keyboard's cooked format
// (characters, '\n' = Enter, '\b' = Backspace, VT100 escapes for arrows/Home/...).
void kapi_inject_key (const char *pKeys)
{
	CWindowManager *pWM = CWindowManager::Get ();
	CUserStr Keys (pKeys, 4096, TRUE);			// (read under the window manager's lock)
	if (pWM != 0 && Keys.OK ())
	{
		pWM->OnKey (Keys.Get ());
	}
}

// Toggle a named app: if an app with this folder name is already running, ask it to
// close (set its window's exit flag) and return 0; otherwise launch it and return 1
// (-1 on error). The shell's "apps" button uses this so a second click closes the
// popup -- no IPC needed.
int kapi_toggle_app (const char *pUserName)
{
	CUserStr Name (pUserName);
	const char *pName = Name.Get ();
	if (pName == 0 || pName[0] == '\0' || !CScheduler::IsActive ())
	{
		return -1;
	}

	CTask *pTask = CScheduler::Get ()->GetRunningTask (pName);
	if (pTask != 0)
	{
		// Running: close it via its window's exit flag (its pump loop then ends).
		CAddressSpace *pAS =
			(CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
		CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
		if (pWin != 0)
		{
			pWin->RequestExit ();
		}
		return 0;			// toggled OFF
	}

	return LaunchAppByName (pName) ? 1 : -1;	// toggled ON
}

// The first window of a running task named pName that is not on another workspace.
struct RaiseCtx { const char *pName; CWindow *pWin; };
static boolean RaiseCallback (CTask *pTask, const char *pTaskName, TTaskState State, TTaskFlags, void *pParam)
{
	RaiseCtx *pCtx = (RaiseCtx *) pParam;
	if (State == TaskStateTerminated || pCtx->pWin != 0 || pTaskName == 0) return TRUE;
	unsigned i = 0;
	for (; pCtx->pName[i] != '\0' && pTaskName[i] == pCtx->pName[i]; i++) {}
	if (pCtx->pName[i] != '\0' || pTaskName[i] != '\0') return TRUE;
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin != 0 && !pWin->OffDesk ()) pCtx->pWin = pWin;
	return TRUE;
}

// Raise the named running app's window to the front (taskbar / quicklaunch click on
// an already-open app). Returns 1 if raised, 0 if not running / no window.
int kapi_raise_app (const char *pUserName)
{
	CUserStr Name (pUserName);
	const char *pName = Name.Get ();
	if (pName == 0 || !CScheduler::IsActive () || CWindowManager::Get () == 0)
	{
		return 0;
	}
	// (v65) an instance with a window on the current workspace (or on every one): one on
	// another workspace is not raised -- the caller starts another one here (the dock)
	RaiseCtx Ctx = { pName, 0 };
	CScheduler::Get ()->EnumerateTasks (RaiseCallback, &Ctx);
	if (Ctx.pWin == 0)
	{
		return 0;
	}
	CWindowManager::Get ()->Raise (Ctx.pWin);
	return 1;
}

void kapi_present (void)
{
	// the app's canvas changed: its window's area is to be redrawn
	CAddressSpace *pPresAS = CurrentAS ();
	CWindow *pPresWin = pPresAS != 0 ? pPresAS->GetWindow () : 0;
	if (pPresWin != 0) pPresWin->PresentDamage (); else ScreenDirty ();
	// The compositor reads the shared canvas continuously; yield so it and the
	// other app get the CPU promptly.
	if (CScheduler::IsActive ())
	{
		CScheduler::Get ()->Yield ();
	}
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

// Generate the desktop wallpaper at runtime: a toroidal-Voronoi cellular pattern
// tinted onto base_color, with `points` seeds. seed 0 => seed from the timer (varies
// per boot). Replaces loading a wallpaper BMP from a file. Returns 1 on success.
int kapi_wallpaper_generate (unsigned nBaseColor, int nPoints, unsigned nSeed)
{
	if (CWindowManager::Get () == 0)
	{
		return 0;
	}
	if (nSeed == 0)
	{
		nSeed = CTimer::Get ()->GetTicks () | 1u;
	}
	CWindowManager::Get ()->GenerateWallpaper (nBaseColor, nPoints, nSeed);
	return 1;
}

// Map the shared desktop-wallpaper buffer (screen-sized, 0x00RRGGBB) into the calling
// app at USER_WALLPAPER_CANVAS and return that VA (+ dims). The app draws into it,
// then calls kapi_wallpaper_commit to make it the live background. The frames are
// kernel-owned, so the wallpaper persists after the writer app exits. Returns 0 on
// failure.
unsigned *kapi_wallpaper_buffer (int *pW, int *pH)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindowManager *pWM = CWindowManager::Get ();
	if (pAS == 0 || pWM == 0 || !OutOK (pW) || !OutOK (pH))
	{
		return 0;
	}
	u64 ulPhys = 0; unsigned nPages = 0;
	if (pWM->EnsureWallpaperBuffer (g_nScreenWidth, g_nScreenHeight, &ulPhys, &nPages) == 0)
	{
		return 0;
	}
	TKPageAttr Attr = KPAGE_ATTR_APP_DATA;			// EL0 RW, ASID-tagged
	pAS->MapContig (USER_WALLPAPER_CANVAS, ulPhys, nPages, Attr);
	OutPut (pW, g_nScreenWidth);
	OutPut (pH, g_nScreenHeight);
	return (unsigned *) USER_WALLPAPER_CANVAS;
}

// Make the (app-written) wallpaper buffer the live desktop background.
void kapi_wallpaper_commit (void)
{
	if (CWindowManager::Get () != 0)
	{
		CWindowManager::Get ()->CommitWallpaper ();
	}
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

static boolean WinListCallback (CTask *pTask, const char *pName, TTaskState State,
				TTaskFlags Flags, void *pParam)
{
	(void) Flags;
	if (State == TaskStateTerminated)
	{
		return TRUE;					// keep going
	}
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	if (pAS == 0 || pAS->GetWindow () == 0 || pAS->GetWindow ()->System ())
	{
		return TRUE;					// not a windowed app / a system component
	}
	if (pTask != pAS->GetMainTask ())
	{
		return TRUE;					// (v67) a thread: its app is listed once
	}
	if (pAS->GetWindow ()->OffDesk ())
	{
		return TRUE;					// (v65) on another workspace
	}

	WinListCtx *pCtx = (WinListCtx *) pParam;
	for (unsigned j = 0; pName[j] != '\0'; j++)
	{
		if (pCtx->nPos + 2 < pCtx->nSize) pCtx->pBuf[pCtx->nPos++] = pName[j];
	}
	if (pCtx->nPos + 1 < pCtx->nSize) pCtx->pBuf[pCtx->nPos++] = '\n';
	pCtx->nCount++;
	return TRUE;
}

int kapi_list_windows (char *pBuf, unsigned nBufSize)
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
	CScheduler::Get ()->EnumerateTasks (WinListCallback, &Ctx);
	pBuf[Ctx.nPos] = '\0';
	return Ctx.nCount;
}

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
// user/tls/onyx_tls.hpp feeds mbedTLS's CTR_DRBG from here).
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
	CStream *pStream = pTable != 0 ? (CStream *) pTable->Get (pHandle, HANDLE_STREAM) : 0;
	if (pStream != 0) pStream->CloseWrite ();
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
	return (f_rename (absF, absT) == FR_OK) ? 0 : -1;
}

// Current cursor position relative to the calling window's client origin (so a
// gadget can make its eyes follow the mouse even when it's outside the window).
void kapi_cursor_pos (int *pX, int *pY)
{
	int cx = 0, cy = 0;
	if (CWindowManager::Get () != 0)
	{
		cx = CWindowManager::Get ()->CursorX ();
		cy = CWindowManager::Get ()->CursorY ();
	}
	CAddressSpace *pAS = CurrentAS ();
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin != 0)
	{
		cx -= pWin->X () + pWin->ChromeL ();
		cy -= pWin->Y () + pWin->ChromeT ();
	}
	OutPut (pX, cx);
	OutPut (pY, cy);
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
	FIL File;
	if (f_open (&File, abs, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return -1;
	}
	UINT nWritten = 0;
	FRESULT Res = ChunkedWrite (&File, pBuf, nLen, &nWritten);
	f_close (&File);
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
static void SocketAdopt (int hSock)
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
	unsigned long nApp   = (unsigned long) g_nUserPages * (unsigned long) KPAGE_SIZE;

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

// --- v34: scroll-wheel speed --------------------------------------------------
// Lines scrolled per wheel notch, applied system-wide (the WM scales the raw notch
// before delivering GUI_EVENT_PTR_WHEEL). The theme editor persists it in theme.txt.
void kapi_set_wheel_speed (int nLinesPerNotch)
{
	if (CWindowManager::Get () != 0)
		CWindowManager::Get ()->SetWheelSpeed (nLinesPerNotch);
}

int kapi_get_wheel_speed (void)
{
	return CWindowManager::Get () != 0 ? CWindowManager::Get ()->GetWheelSpeed () : 1;
}

// Grow/shrink the calling process's heap by nIncrement bytes (Unix sbrk). Returns
// the previous break, or (void*)-1 on failure. The user-space allocator (user/umm.h)
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

// Whole-window opacity of the caller's window (0 = invisible .. 255 = opaque), for
// fades (the notification bubbles).
void kapi_set_window_alpha (int nAlpha)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin != 0) pWin->SetAlpha (nAlpha);
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
	if (pAS->GetWindow () == 0 && CreateWindow (0, 0, 64, 64, "fullscreen", WIN_FLAG_BORDERLESS) == 0)
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
	if (pWM->FullscreenWindow () != pWin) { s_pFsWin = pWin; s_nFsX = pWin->X (); s_nFsY = pWin->Y (); }
	pWin->Move (0, 0);
	memset ((void *) ulPhys, 0, (size_t) g_nScreenWidth * g_nScreenHeight * 4);
	pWM->SetFullscreen (pWin);
	s_pDirectWin = 0;
	OutPut (pW, g_nScreenWidth);
	OutPut (pH, g_nScreenHeight);
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
		unsigned nW = g_pGraphics->GetWidth (), nH = g_pGraphics->GetHeight ();
		if ((int) nW == g_nScreenWidth && (int) nH == g_nScreenHeight)
		{
			memcpy (g_pGraphics->GetBuffer (), pWM->FullscreenBuffer (), (size_t) nW * nH * 4);
		}
		g_pGraphics->UpdateDisplay ();
		ScreenDirty ();				// a new frame: kapi_screen_grab (vncd) must see it
		pAS->GetWindow ()->Touch ();		// (rdpd)
	}
	if (CScheduler::IsActive ())
	{
		CScheduler::Get ()->Yield ();
	}
}

// Give the screen back to the desktop (also automatic when the app exits).
void kapi_fullscreen_end (void)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindowManager *pWM = CWindowManager::Get ();
	if (pAS != 0 && pWM != 0 && pWM->FullscreenWindow () != 0 && pWM->FullscreenWindow () == pAS->GetWindow ())
	{
		CWindow *pWin = pAS->GetWindow ();
		s_pDirectWin = 0;
		pWM->SetFullscreen (0);
		// back where it was; else (made full screen at once) centred, below the menu bar
		int x = s_nFsX, y = s_nFsY;
		if (s_pFsWin != pWin || y < 32)			// (32: the menu bar, user/Apps/menubar)
		{
			x = (g_nScreenWidth - pWin->OuterW ()) / 2; y = (g_nScreenHeight - pWin->OuterH ()) / 2;
			if (y < 32) y = 32;
			if (x < 0) x = 0;
		}
		pWin->Move (x, y);
		s_pFsWin = 0;
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

// Start dragging (type, data) from the caller's window, with `label` on the cursor
// badge. The left button must be held (call it from a pointer-move handler once the
// cursor moved a few pixels with the button down). 1 = started, 0 = not.
int kapi_drag_begin (int nType, const void *pData, unsigned nLen, const char *pLabel)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindowManager *pWM = CWindowManager::Get ();
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin == 0 || pWM == 0)
	{
		return 0;
	}
	CUserStr Label (pLabel, DND_LABEL_MAX, TRUE);		// copy out of the app's memory first
	if (!Label.OK () && !Label.IsNull ())
	{
		return 0;
	}
	if (nLen > DND_MAX) nLen = DND_MAX;
	if (pData == 0) nLen = 0;
	if (!UserReadable (pData, nLen))
	{
		return 0;
	}
	if (!pWM->DragBegin (pWin, Label.OK () ? Label.Get () : ""))
	{
		return 0;
	}
	if (nLen) memcpy (s_Dnd, pData, nLen);
	s_nDndLen = nLen;
	s_nDndType = nType;
	return 1;
}

// The dropped payload: copies <= cap bytes, returns the full length (+ its type).
int kapi_drag_data (int *pType, void *pBuf, unsigned nCap)
{
	unsigned n = s_nDndLen < nCap ? s_nDndLen : nCap;
	if (!OutOK (pType) || (pBuf != 0 && !UserRange (pBuf, n))) return 0;
	OutPut (pType, s_nDndType);
	if (pBuf != 0 && n && !UserCopyOut (pBuf, s_Dnd, n)) return 0;
	return (int) s_nDndLen;
}

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
int  kapi_sound_acquire (void) { return SoundAcquire (CallerPid ()); }
void kapi_sound_release (void) { SoundRelease (CallerPid ()); }
int  kapi_sound_start (int nVoice, unsigned nMilliHz, int nWave, int nVolume) { return SoundStart (CallerPid (), nVoice, nMilliHz, nWave, nVolume); }
int  kapi_sound_stop (int nVoice) { return SoundStop (CallerPid (), nVoice); }
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
	int r = SoundStatus (&nRate, &nFree, &nOwner);
	if (OutOK (pRate)) OutPut (pRate, nRate);
	if (OutOK (pFree)) OutPut (pFree, nFree);
	if (OutOK (pOwner)) OutPut (pOwner, nOwner);
	return r;
}
int  kapi_sound_volume (int nVolume, int nMute) { return SoundVolume (nVolume, nMute); }
int kapi_sound_instrument (int nVoice, const struct kapi_fm_instrument *pIns)
{
	struct kapi_fm_instrument In;
	if (pIns == 0 || !UserGet (&In, pIns)) return -1;
	return SoundInstrument (CallerPid (), nVoice, &In);
}

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
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	return pWM != 0 && pWM->KeyHeld (nKey, pWin) ? 1 : 0;
}
void kapi_inject_key_held (int nKey, int bDown)
{
	CWindowManager *pWM = CWindowManager::Get ();
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
	CWindowManager *pWM = CWindowManager::Get ();
	CAddressSpace *pAS = CurrentAS ();
	Pad.focus = pWM != 0 && pAS != 0 && pWM->HasKeyFocus (pAS->GetWindow ()) ? 1 : 0;
	return UserPut (pOut, Pad) ? 1 : 0;
}

}  // extern "C"

// ---- v56: the windows as objects (the window-level remote desktop, rdpd) --------------------
static CWindow *WinById (CWindowManager *pWM, unsigned nId)
{
	CWindow *List[WM_MAX_WINDOWS];
	unsigned n = pWM->Snapshot (List, WM_MAX_WINDOWS);
	for (unsigned i = 0; i < n; i++) if (List[i]->Id () == nId) return List[i];
	return 0;				// (a CWindow is never freed: see Composite)
}

extern "C" {

int kapi_win_list (struct kapi_win_info *pOut, int nMax)
{
	CWindowManager *pWM = CWindowManager::Get ();
	if (pWM == 0 || pOut == 0 || nMax <= 0 || !UserWritable (pOut, (u64) nMax * sizeof *pOut)) return 0;
	CWindow *List[WM_MAX_WINDOWS];
	unsigned n = pWM->Snapshot (List, WM_MAX_WINDOWS);
	CWindow *pFs = pWM->FullscreenWindow ();
	int k = 0;
	{	// the desktop first (it is not a window: the wallpaper + the backmost windows)
		struct kapi_win_info &I = pOut[k++];
		memset (&I, 0, sizeof I);
		I.id = KAPI_WIN_DESKTOP; I.w = g_nScreenWidth; I.h = g_nScreenHeight;
		I.flags = WIN_FLAG_BACKMOST | WIN_FLAG_BORDERLESS; I.alpha = 255; I.gen = pWM->DesktopGen ();
		memcpy (I.title, "Onyx Desktop", 13);
	}
	for (unsigned i = 0; i < n && k < nMax; i++)
	{
		CWindow *pW = List[i];
		struct kapi_win_info &I = pOut[k++];
		I.id = pW->Id (); I.pid = pW->OwnerPid ();
		I.x = pW->X () + pW->ChromeL (); I.y = pW->Y () + pW->ChromeT ();
		I.w = pW->ClientWidth (); I.h = pW->ClientHeight ();
		I.flags = pW->Flags (); I.alpha = pW->Alpha (); I.gen = pW->Gen ();
		I.state = (pWM->HasKeyFocus (pW) ? KAPI_WIN_KEYS : 0) | (pW->Minimised () ? KAPI_WIN_MINIMISED : 0)
			| (pW->OffDesk () ? KAPI_WIN_OFFDESK : 0) | (unsigned) ((pW->Desk () + 1) & 0xFF) << 8;
		if (pW == pFs) { I.x = I.y = 0; I.w = g_nScreenWidth; I.h = g_nScreenHeight; I.state |= KAPI_WIN_FULLSCREEN; }
		I.ow = I.oh = I.il = I.it = 0; I.chromeGen = pW->ChromeGen ();
		if (pW->HasChrome () && pW != pFs) { I.ow = pW->OuterW (); I.oh = pW->OuterH (); I.il = pW->ChromeL (); I.it = pW->ChromeT (); }
		const char *t = pW->Title ();
		unsigned j = 0;
		for (; j + 1 < sizeof I.title && t[j]; j++) I.title[j] = t[j];
		I.title[j] = 0;
	}
	return k;
}

int kapi_win_read (unsigned nId, int nPart, int x, int y, int w, int h, unsigned *pDst, int nStride)
{
	CWindowManager *pWM = CWindowManager::Get ();
	if (pWM != 0 && nId == KAPI_WIN_DESKTOP)		// the whole desktop, composited straight in
	{
		if (nPart != 0 || x != 0 || y != 0 || w != g_nScreenWidth || h != g_nScreenHeight || nStride != w
		    || pDst == 0 || !UserWritable (pDst, (u64) w * h * 4)) return -1;
		GImage Img ((u32 *) pDst, w, h);
		pWM->CompositeDesktop (&Img);
		return 0;
	}
	CWindow *pW = pWM != 0 ? WinById (pWM, nId) : 0;
	if (pW == 0 || pDst == 0) return -1;
	const u8 *pSrc; unsigned nPitch; int W, H;
	if (nPart == 1 || nPart == 2)
	{
		if (!pW->HasChrome ()) return -1;
		W = pW->OuterW (); H = pW->OuterH ();
		pSrc = (const u8 *) pW->ChromePhys (nPart - 1); nPitch = (unsigned) W * 4;
	}
	else if (nPart != 0) return -1;
	else if (pW == pWM->FullscreenWindow ())
	{
		W = g_nScreenWidth; H = g_nScreenHeight;
		if (FsDirect (pWM) && (pSrc = (const u8 *) MapScreen (CurrentAS (), &nPitch)) != 0) {}
		else if (pWM->FullscreenBuffer () != 0) { pSrc = (const u8 *) pWM->FullscreenBuffer (); nPitch = (unsigned) W * 4; }
		else return -1;
	}
	else
	{
		W = pW->ClientWidth (); H = pW->ClientHeight ();
		pSrc = (const u8 *) pW->CanvasBuffer (); nPitch = (unsigned) pW->Canvas ()->Width () * 4;
		if (pSrc == 0) return -1;
	}
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > W) w = W - x;
	if (y + h > H) h = H - y;
	if (w <= 0 || h <= 0) return 0;
	if (nStride < w) return -1;
	if (!UserWritable (pDst, ((u64) (h - 1) * (u64) nStride + (u64) w) * 4)) return -1;
	for (int r = 0; r < h; r++)
		memcpy (pDst + (size_t) r * nStride, pSrc + (size_t) (y + r) * nPitch + (size_t) x * 4, (size_t) w * 4);
	return 0;
}

int kapi_win_raise (unsigned nId)
{
	CWindowManager *pWM = CWindowManager::Get ();
	CWindow *pW = pWM != 0 ? WinById (pWM, nId) : 0;
	if (pW == 0) return -1;
	pWM->Raise (pW);
	ScreenDirty ();
	return 0;
}

int kapi_win_close (unsigned nId)
{
	CWindowManager *pWM = CWindowManager::Get ();
	CWindow *pW = pWM != 0 ? WinById (pWM, nId) : 0;
	if (pW == 0) return -1;
	pW->RequestExit ();
	return 0;
}

// ---- v64: the modernised CDE desktop's windows ------------------------------------------
int kapi_win_minimise (unsigned nId)
{
	CWindowManager *pWM = CWindowManager::Get ();
	if (pWM == 0) return -1;
	CWindow *pW;
	if (nId == 0) { CAddressSpace *pAS = CurrentAS (); pW = pAS != 0 ? pAS->GetWindow () : 0; }
	else pW = WinById (pWM, nId);
	if (pW == 0) return -1;
	pWM->Minimise (pW);
	return 0;
}

int kapi_win_geometry (struct kapi_win_geom *pOut)
{
	CWindowManager *pWM = CWindowManager::Get ();
	CAddressSpace *pAS = CurrentAS ();
	CWindow *pW = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWM == 0 || pW == 0 || pOut == 0) return -1;
	struct kapi_win_geom G;					// (made here, copied out whole)
	memset (&G, 0, sizeof G);
	G.x = pW->X (); G.y = pW->Y ();
	G.w = pW->OuterWidth (); G.h = pW->OuterHeight ();
	G.cw = pW->ClientWidth (); G.ch = pW->ClientHeight ();
	pWM->WorkArea (&G.ax, &G.ay, &G.aw, &G.ah);
	G.state = (pWM->HasKeyFocus (pW) ? KAPI_WIN_KEYS : 0) | (pW->Minimised () ? KAPI_WIN_MINIMISED : 0)
		| (pW->OffDesk () ? KAPI_WIN_OFFDESK : 0) | (unsigned) ((pW->Desk () + 1) & 0xFF) << 8;
	return UserPut (pOut, G) ? 0 : -1;
}

// ---- v65: the workspaces (virtual desktops) -------------------------------------------------
int kapi_desk (int nSet, int nCount)
{
	CWindowManager *pWM = CWindowManager::Get ();
	if (pWM == 0) return 1 << 8;
	return nSet < 0 && nCount <= 0 ? pWM->DeskInfo () : pWM->SetDesk (nSet, nCount);
}

int kapi_win_desk (unsigned nId, int n)
{
	CWindowManager *pWM = CWindowManager::Get ();
	if (pWM == 0) return -3;
	CWindow *pW;
	if (nId == 0) { CAddressSpace *pAS = CurrentAS (); pW = pAS != 0 ? pAS->GetWindow () : 0; }
	else pW = WinById (pWM, nId);
	if (pW == 0) return -3;
	return n < -1 ? pW->Desk () : pWM->MoveToDesk (pW, n);
}

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

// Resize the caller's window, its canvas (and frame copies) growing when needed: new memory
// mapped at the same addresses (the old kept a few frames for the compositor, then freed).
unsigned *kapi_resize_window2 (int w, int h, int *pStride)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin == 0 || w <= 0 || h <= 0 || !OutOK (pStride))
	{
		return 0;
	}
	if (w > g_nScreenWidth) w = g_nScreenWidth;		// (no bigger than the screen)
	if (h > g_nScreenHeight) h = g_nScreenHeight;
	if (w > pWin->Canvas ()->Width () || h > pWin->Canvas ()->Height ())
	{
		if (!pWin->Grow (w, h))
		{
			return 0;
		}
		TKPageAttr Attr = KPAGE_ATTR_APP_DATA;
		pAS->MapContig (USER_WINDOW_CANVAS, pWin->CanvasPhys (), pWin->CanvasPages (), Attr);
		if (pWin->HasChrome ())
		{
			pAS->MapContig (USER_WINDOW_CHROME,          pWin->ChromePhys (0), pWin->ChromePages (0), Attr);
			pAS->MapContig (USER_WINDOW_CHROME_INACTIVE, pWin->ChromePhys (1), pWin->ChromePages (1), Attr);
		}
		pAS->FlushTLB ();
	}
	pWin->SetLogicalSize (w, h);
	OutPut (pStride, (int) pWin->Canvas ()->Width ());
	return (unsigned *) USER_WINDOW_CANVAS;
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
