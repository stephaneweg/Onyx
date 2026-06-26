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
#include <kern/applaunch.h>
#include <kern/stream.h>		// CStream / CPipeStream / CFileStream / CProcess
#include <kern/kapi_abi.h>		// struct kapi_dirent
#include <kern/layout.h>
#include <kern/gui/window.h>
#include <kern/net.h>		// NetTcpConnect/Send/Recv/Close/Status (socket backend)
#include <kern/debugcon.h>
#include <kern/gui/gimage.h>
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
static const char *CurCwd (void)
{
	CAddressSpace *pAS = CurrentAS ();
	return (pAS != 0) ? pAS->GetCwd () : "SD:/";
}

// Resolve an app-supplied path to a clean absolute FatFs path in pOut. A path that
// already starts with "SD:" is taken as absolute; "/x" is volume-root-relative; any
// other form (incl. "./x", "../x", "x") is relative to the current working dir.
// ".", ".." and redundant slashes are normalised away. Lets ls/cat/redirection/etc.
// use relative or absolute paths transparently.
static void ResolvePath (const char *pIn, char *pOut, unsigned nCap)
{
	if (pIn == 0) { pOut[0] = '\0'; return; }

	char raw[512];
	unsigned r = 0;
	if (pIn[0] == 'S' && pIn[1] == 'D' && pIn[2] == ':')		// already absolute
	{
		for (unsigned i = 0; pIn[i] != '\0' && r < sizeof (raw) - 1; i++) raw[r++] = pIn[i];
	}
	else if (pIn[0] == '/')						// volume-root-relative
	{
		const char *p = "SD:";
		while (*p && r < sizeof (raw) - 1) raw[r++] = *p++;
		for (unsigned i = 0; pIn[i] != '\0' && r < sizeof (raw) - 1; i++) raw[r++] = pIn[i];
	}
	else								// relative to cwd
	{
		const char *cwd = CurCwd ();
		for (unsigned i = 0; cwd[i] != '\0' && r < sizeof (raw) - 1; i++) raw[r++] = cwd[i];
		if (r < sizeof (raw) - 1) raw[r++] = '/';
		for (unsigned i = 0; pIn[i] != '\0' && r < sizeof (raw) - 1; i++) raw[r++] = pIn[i];
	}
	raw[r] = '\0';

	// Normalise the part after "SD:": process '.', '..' and collapse '/' runs.
	unsigned starts[64]; int depth = 0;
	unsigned o = 0;
	if (nCap >= 4) { pOut[o++] = 'S'; pOut[o++] = 'D'; pOut[o++] = ':'; }
	unsigned i = 3;					// skip "SD:"
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
	if (o == 3 && nCap > 4) pOut[o++] = '/';		// nothing left => root "SD:/"
	pOut[o] = '\0';
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
	if (pAS == 0 || w <= 0 || h <= 0 || w > 1024 || h > 768)
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
		int nXRange = g_nScreenWidth  - nOuterW - nXMin;
		int nYRange = g_nScreenHeight - nOuterH;
		s_nRng = s_nRng * 1103515245u + 12345u;
		x = nXRange > 0 ? nXMin + (int) (s_nRng % (unsigned) nXRange) : 0;
		s_nRng = s_nRng * 1103515245u + 12345u;
		y = nYRange > 0 ? (int) (s_nRng % (unsigned) nYRange) : 0;
	}

	// pTitle is a pointer in the calling app's address space, which is active here
	// (EL1) -- CWindow copies it. Fall back to a default if null.
	CWindow *pWin = new CWindow (x, y, w, h, pTitle != 0 ? pTitle : "app", nFlags);
	if (pWin == 0 || !pWin->IsValid ())
	{
		return 0;
	}

	TKPageAttr Attr = KPAGE_ATTR_APP_DATA;			// EL1 RW, ASID-tagged
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

unsigned *kapi_create_window (int w, int h, const char *pTitle)
{
	return CreateWindow (-1, -1, w, h, pTitle, 0);		// auto-placed, normal chrome
}

unsigned *kapi_create_window_ex (int x, int y, int w, int h, const char *pTitle,
				 unsigned nFlags)
{
	return CreateWindow (x, y, w, h, pTitle, nFlags);
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
void kapi_draw_text (int x, int y, const char *pStr, unsigned nColor)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin == 0 || pStr == 0)
	{
		return;
	}
	pWin->Canvas ()->DrawText (x, y, pStr, (u32) nColor);
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
	if (pWin == 0 || out == 0)
	{
		return 0;
	}
	memset (out, 0, sizeof *out);
	out->content   = (unsigned *) USER_WINDOW_CANVAS;
	out->content_w = pWin->ClientWidth ();
	out->content_h = pWin->ClientHeight ();
	if (pWin->HasChrome ())
	{
		out->active   = (unsigned *) USER_WINDOW_CHROME;
		out->inactive = (unsigned *) USER_WINDOW_CHROME_INACTIVE;
		out->chrome_w = pWin->OuterW ();
		out->chrome_h = pWin->OuterH ();
		out->inset_l  = pWin->ChromeL ();
		out->inset_r  = pWin->ChromeR ();
		out->inset_t  = pWin->ChromeT ();
		out->inset_b  = pWin->ChromeB ();
	}
	const char *pTitle = pWin->Title ();
	unsigned i;
	for (i = 0; i + 1 < sizeof out->title && pTitle[i] != '\0'; i++)
	{
		out->title[i] = pTitle[i];
	}
	out->title[i] = '\0';
	return 1;
}

// Draw kernel-font text (transparent background) into an arbitrary app-mapped
// 0x00RRGGBB buffer (dst, dstW x dstH) at (x,y). Lets a user-side toolkit render text
// into surfaces other than the main canvas -- e.g. the window-chrome buffers (the
// kernel bitmap font is the only font apps have). dst must lie in user space.
void kapi_draw_text_buf (unsigned *dst, int dstW, int dstH, int x, int y,
			 const char *pStr, unsigned nColor)
{
	if (dst == 0 || pStr == 0 || dstW <= 0 || dstH <= 0)
	{
		return;
	}
	u64 nBytes = (u64) dstW * dstH * sizeof (u32);
	if (!IS_USER_VA (dst) || !IS_USER_VA ((u8 *) dst + nBytes - 1))
	{
		return;
	}
	GImage Img ((u32 *) dst, dstW, dstH);
	Img.DrawText (x, y, pStr, (u32) nColor);
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

// Launch another app by folder name (apps/<name>.app/main.elf) as a new process.
// Used by the shell (panel / app-list popup). Returns 1 on success, 0 on failure.
int kapi_launch (const char *pName)
{
	return LaunchAppByName (pName) ? 1 : 0;
}

// Run an ELF by absolute path with an argv string (e.g. the file manager opening a
// document in an editor, or launching a program). Fire-and-forget.
int kapi_exec (const char *pPath, const char *pArgs)
{
	return ExecPath (pPath, pArgs ? pArgs : "") ? 1 : 0;
}

// Framebuffer size, for edge-pinned borderless windows (the shell panel/applist).
void kapi_screen_size (int *pW, int *pH)
{
	if (pW != 0) *pW = g_nScreenWidth;
	if (pH != 0) *pH = g_nScreenHeight;
}

// Toggle a named app: if an app with this folder name is already running, ask it to
// close (set its window's exit flag) and return 0; otherwise launch it and return 1
// (-1 on error). The shell's "apps" button uses this so a second click closes the
// popup -- no IPC needed.
int kapi_toggle_app (const char *pName)
{
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

// Raise the named running app's window to the front (taskbar / quicklaunch click on
// an already-open app). Returns 1 if raised, 0 if not running / no window.
int kapi_raise_app (const char *pName)
{
	if (pName == 0 || !CScheduler::IsActive () || CWindowManager::Get () == 0)
	{
		return 0;
	}
	CTask *pTask = CScheduler::Get ()->GetRunningTask (pName);
	if (pTask == 0)
	{
		return 0;
	}
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin == 0)
	{
		return 0;
	}
	CWindowManager::Get ()->Raise (pWin);
	return 1;
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
	if (pAS == 0 || pWM == 0)
	{
		return 0;
	}
	u64 ulPhys = 0; unsigned nPages = 0;
	if (pWM->EnsureWallpaperBuffer (g_nScreenWidth, g_nScreenHeight, &ulPhys, &nPages) == 0)
	{
		return 0;
	}
	TKPageAttr Attr = KPAGE_ATTR_APP_DATA;			// EL1 RW, ASID-tagged
	pAS->MapContig (USER_WALLPAPER_CANVAS, ulPhys, nPages, Attr);
	if (pW != 0) *pW = g_nScreenWidth;
	if (pH != 0) *pH = g_nScreenHeight;
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

void kapi_pump_events (void)
{
	CAddressSpace *pAS = CurrentAS ();
	if (pAS == 0)
	{
		return;
	}
	CWindow *pWin = pAS->GetWindow ();
	if (pWin == 0)
	{
		return;
	}

	GUIEvent Ev;
	while (pWin->PopEvent (&Ev))
	{
		if (Ev.ulHandler != 0)
		{
			((void (*) (unsigned long, int, long)) Ev.ulHandler)
				(Ev.ulSender, Ev.nEvent, Ev.lValue);
		}
	}
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

void kapi_wait_for_exit (void)
{
	// Blocking message pump for passive apps: dispatch events until the window's
	// close box is clicked (RequestExit), then return so the app can clean up.
	for (;;)
	{
		kapi_pump_events ();
		if (kapi_should_exit ())
		{
			return;
		}
		if (CScheduler::IsActive ())
		{
			CScheduler::Get ()->MsSleep (16);
		}
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
	if (pBuf == 0 || nBufSize == 0)
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
	if (pAS == 0 || pAS->GetWindow () == 0)
	{
		return TRUE;					// not a windowed app
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
	if (pBuf == 0 || nBufSize == 0)
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
	char kc = pTask->GetUserData (TASK_USER_DATA_USER) != 0 ? 'a' : 'k';

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
	if (pBuf == 0 || nBufSize == 0)
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
int kapi_kill (const char *pName)
{
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
	if (pBuf == 0 || nBufSize == 0) return 0;
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
	if (!nForce)
	{
		CWindow *pWin = pAS->GetWindow ();
		if (pWin != 0) { pWin->RequestExit (); return 1; }	// clean close
		// else: no window to signal -> hard terminate below
	}
	CScheduler::Get ()->TerminateTask (pTask);
	return 1;
}

// --- keyboard layout (kernel.cpp drives the Circle CKeyMap; decls in applaunch.h) --
// Switch the keyboard layout to a compiled-in country map ("FR","US","DE","UK",
// "ES","IT","DV"). Returns 1 on success, 0 if unknown / no keyboard.
int kapi_set_keymap (const char *pName)
{
	return KernelSetKeyMap (pName) ? 1 : 0;
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
	const unsigned char *p = (const unsigned char *) pData;
	if (p == 0 || nLen < 8) return 0;
	if (!(p[0] == 'O' && p[1] == 'K' && p[2] == 'M' && p[3] == '1')) return 0;
	unsigned nRows = (unsigned) p[4] | ((unsigned) p[5] << 8);
	unsigned nCols = (unsigned) p[6] | ((unsigned) p[7] << 8);
	if (nRows != 128 || nCols != 5) return 0;
	unsigned nTable = nRows * nCols * 2;
	if (nLen < 8 + nTable) return 0;
	return KernelSetKeyMapData (pName, p + 8, nTable) ? 1 : 0;
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
	if (pBuf == 0) return 0;

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
	const char *p = KernelGetKeyMap ();
	unsigned i = 0;
	for (; p[i] != '\0' && i < nMax - 1; i++) pBuf[i] = p[i];
	pBuf[i] = '\0';
	return (int) i;
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

	unsigned p = 0;
	const char *pPre = "SD:apps/";
	const char *pSuf = ".app/";
	for (unsigned i = 0; pPre[i] != '\0' && p + 1 < nMax; i++) pBuf[p++] = pPre[i];
	for (unsigned i = 0; pName[i] != '\0' && p + 1 < nMax; i++) pBuf[p++] = pName[i];
	for (unsigned i = 0; pSuf[i] != '\0' && p + 1 < nMax; i++) pBuf[p++] = pSuf[i];
	pBuf[p] = '\0';
	return (int) p;
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

	if (pYear)   *pYear   = (int) Time.GetYear ();
	if (pMonth)  *pMonth  = (int) Time.GetMonth ();
	if (pDay)    *pDay    = (int) Time.GetMonthDay ();
	if (pHour)   *pHour   = (int) Time.GetHours ();
	if (pMinute) *pMinute = (int) Time.GetMinutes ();
	if (pSecond) *pSecond = (int) Time.GetSeconds ();

	return nSeconds > 60u * 60 * 24 * 365 ? 1 : 0;	// > ~1 year => a real date
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
	char abs[300]; ResolvePath (pPath, abs, sizeof abs);
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

// --- streams / stdio / processes ---------------------------------------------

void *kapi_pipe (void)
{
	return new CPipeStream;
}

void *kapi_file_in (const char *pPath)
{
	char abs[300]; ResolvePath (pPath, abs, sizeof abs);
	CFileStream *pFile = new CFileStream (abs, 0);
	if (pFile == 0) return 0;
	if (!pFile->IsValid ()) { delete pFile; return 0; }
	return pFile;
}

void *kapi_file_out (const char *pPath, int bAppend)
{
	char abs[300]; ResolvePath (pPath, abs, sizeof abs);
	CFileStream *pFile = new CFileStream (abs, bAppend ? 2 : 1);
	if (pFile == 0) return 0;
	if (!pFile->IsValid ()) { delete pFile; return 0; }
	return pFile;
}

int kapi_stream_read (void *pHandle, void *pBuf, unsigned nLen)
{
	return pHandle != 0 ? ((CStream *) pHandle)->Read (pBuf, nLen) : 0;
}

// Non-blocking read: >0 bytes, 0 = EOF, -1 = would block. For the terminal, which
// drains a child's stdout without freezing its own UI loop.
int kapi_stream_read_nb (void *pHandle, void *pBuf, unsigned nLen)
{
	return pHandle != 0 ? ((CStream *) pHandle)->ReadNonBlocking (pBuf, nLen) : 0;
}

int kapi_stream_write (void *pHandle, const void *pBuf, unsigned nLen)
{
	return pHandle != 0 ? ((CStream *) pHandle)->Write (pBuf, nLen) : -1;
}

void kapi_stream_close (void *pHandle)
{
	if (pHandle != 0) ((CStream *) pHandle)->Release ();
}

// Signal EOF to readers of this stream (the writer is done). The terminal uses it
// on its keyboard pipe so a stdin-reading child (e.g. cat) ends on Ctrl-D.
void kapi_stream_eof (void *pHandle)
{
	if (pHandle != 0) ((CStream *) pHandle)->CloseWrite ();
}

// Read from this task's stdin (0 = EOF / no stdin).
int kapi_stdin_read (void *pBuf, unsigned nLen)
{
	CAddressSpace *pAS = CurrentAS ();
	CStream *pStream = pAS != 0 ? pAS->GetStdin () : 0;
	return pStream != 0 ? pStream->Read (pBuf, nLen) : 0;
}

// Write to this task's stdout; if none (e.g. launched from the panel), log it.
int kapi_stdout_write (const void *pBuf, unsigned nLen)
{
	CAddressSpace *pAS = CurrentAS ();
	CStream *pStream = pAS != 0 ? pAS->GetStdout () : 0;
	if (pStream != 0)
	{
		return pStream->Write (pBuf, nLen);
	}
	char Tmp[129];
	unsigned n = nLen < sizeof (Tmp) - 1 ? nLen : sizeof (Tmp) - 1;
	memcpy (Tmp, pBuf, n);
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
	if (!CLogger::Get ()->ReadEvent (&Sev, Src, Msg, &t, &ht, &tz))
	{
		return 0;
	}
	if (pSeverity != 0) *pSeverity = (int) Sev;
	if (pSrc != 0 && nSrcCap > 0)
	{
		unsigned i = 0; for (; Src[i] != '\0' && i < nSrcCap - 1; i++) pSrc[i] = Src[i]; pSrc[i] = '\0';
	}
	if (pMsg != 0 && nMsgCap > 0)
	{
		unsigned i = 0; for (; Msg[i] != '\0' && i < nMsgCap - 1; i++) pMsg[i] = Msg[i]; pMsg[i] = '\0';
	}
	return 1;
}

// This task's own stdin / stdout stream handles, so a shell (cmd) can wire them into
// the children it spawns (first stage reads the shell's stdin, last stage's output is
// drained by the shell). 0 if none.
void *kapi_stdin (void)
{
	CAddressSpace *pAS = CurrentAS ();
	return pAS != 0 ? (void *) pAS->GetStdin () : 0;
}
void *kapi_stdout (void)
{
	CAddressSpace *pAS = CurrentAS ();
	return pAS != 0 ? (void *) pAS->GetStdout () : 0;
}

// Spawn a console program (ELF at pPath) with stdin/stdout streams + argv. Returns
// a process handle for kapi_wait, or 0 on failure.
void *kapi_spawn (const char *pPath, const char *pArgs, void *pStdin, void *pStdout)
{
	// Resolve the program path against the caller's cwd, pass that cwd to the child,
	// and record the spawner as the child's parent (so killing the parent cascades).
	char abs[300]; ResolvePath (pPath, abs, sizeof abs);
	CAddressSpace *pAS = CurrentAS ();
	unsigned nParent = pAS != 0 ? pAS->GetPid () : 0;
	return SpawnProcess (abs, pArgs, (CStream *) pStdin, (CStream *) pStdout, CurCwd (), nParent);
}

// Change the calling task's working directory: resolve pPath, verify it is a real
// directory (f_opendir), and store it. Returns 1 on success, 0 otherwise.
int kapi_chdir (const char *pPath)
{
	CAddressSpace *pAS = CurrentAS ();
	if (pAS == 0 || pPath == 0) return 0;
	char abs[300]; ResolvePath (pPath, abs, sizeof abs);
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
	const char *p = CurCwd ();
	unsigned i = 0;
	for (; p[i] != '\0' && i < nMax - 1; i++) pBuf[i] = p[i];
	pBuf[i] = '\0';
	return (int) i;
}

// Wait (cooperatively) for a spawned process to finish; returns its exit status and
// frees the handle.
int kapi_wait (void *pProc)
{
	CProcess *p = (CProcess *) pProc;
	if (p == 0) return -1;
	while (!p->bDone)
	{
		if (!CScheduler::IsActive ()) break;
		CScheduler::Get ()->MsSleep (5);
	}
	int nStatus = p->nStatus;
	delete p;
	return nStatus;
}

// Non-blocking poll: 1 if the spawned process has finished (else 0). Does NOT free
// the handle (kapi_wait does). Lets the terminal detect completion without blocking.
int kapi_proc_done (void *pProc)
{
	CProcess *p = (CProcess *) pProc;
	return (p == 0 || p->bDone) ? 1 : 0;
}

// Copy this task's argv string (set at spawn) into pBuf. Returns length.
int kapi_get_args (char *pBuf, unsigned nMax)
{
	if (pBuf == 0 || nMax == 0) return 0;
	CAddressSpace *pAS = CurrentAS ();
	const char *pArgs = pAS != 0 ? pAS->GetArgs () : 0;
	unsigned i = 0;
	if (pArgs != 0)
	{
		for (; pArgs[i] != '\0' && i < nMax - 1; i++) pBuf[i] = pArgs[i];
	}
	pBuf[i] = '\0';
	return (int) i;
}

// --- directory listing -------------------------------------------------------

void *kapi_opendir (const char *pPath)
{
	if (pPath == 0)
	{
		return 0;
	}
	char abs[300]; ResolvePath (pPath, abs, sizeof abs);
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
	return pDir;
}

int kapi_readdir (void *pHandle, struct kapi_dirent *pEnt)
{
	if (pHandle == 0 || pEnt == 0)
	{
		return 0;
	}
	FILINFO Info;
	if (f_readdir ((DIR *) pHandle, &Info) != FR_OK || Info.fname[0] == '\0')
	{
		return 0;			// error or end of directory
	}
	unsigned i = 0;
	for (; Info.fname[i] != '\0' && i < sizeof (pEnt->name) - 1; i++)
	{
		pEnt->name[i] = Info.fname[i];
	}
	pEnt->name[i] = '\0';
	pEnt->size = (unsigned) Info.fsize;
	pEnt->is_dir = (Info.fattrib & AM_DIR) ? 1 : 0;
	return 1;
}

void kapi_closedir (void *pHandle)
{
	if (pHandle != 0)
	{
		f_closedir ((DIR *) pHandle);
		delete (DIR *) pHandle;
	}
}

// --- file operations ---------------------------------------------------------

int kapi_mkdir (const char *pPath)
{
	if (pPath == 0) return -1;
	char abs[300]; ResolvePath (pPath, abs, sizeof abs);
	return (f_mkdir (abs) == FR_OK) ? 0 : -1;
}

int kapi_remove (const char *pPath)		// file or empty directory
{
	if (pPath == 0) return -1;
	char abs[300]; ResolvePath (pPath, abs, sizeof abs);
	return (f_unlink (abs) == FR_OK) ? 0 : -1;
}

int kapi_rename (const char *pFrom, const char *pTo)
{
	if (pFrom == 0 || pTo == 0) return -1;
	char absF[300], absT[300];
	ResolvePath (pFrom, absF, sizeof absF);
	ResolvePath (pTo, absT, sizeof absT);
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
	if (pX) *pX = cx;
	if (pY) *pY = cy;
}

// --- modal dialogs -----------------------------------------------------------

// Write a whole file (create/truncate). Returns bytes written, or -1 on error.
int kapi_save_file (const char *pPath, const void *pBuf, unsigned nLen)
{
	if (pPath == 0)
	{
		return -1;
	}
	char abs[300]; ResolvePath (pPath, abs, sizeof abs);
	FIL File;
	if (f_open (&File, abs, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return -1;
	}
	UINT nWritten = 0;
	FRESULT Res = f_write (&File, pBuf, nLen, &nWritten);
	f_close (&File);
	return (Res == FR_OK) ? (int) nWritten : -1;
}

// --- networking (TCP sockets over WLAN) --------------------------------------
//
// Thin shims over the socket backend in sys/net.cpp. These run on the app's task,
// so Connect/DNS/Send block cooperatively (the rest of the system keeps running);
// Recv is non-blocking so a GUI app can poll it from its event loop.

int kapi_net_status (char *pIP, unsigned nCap) { return NetStatus (pIP, nCap); }

int kapi_tcp_connect (const char *pHost, unsigned nPort)
{
	CAddressSpace *pAS = CurrentAS ();
	unsigned nPid = (pAS != 0) ? pAS->GetPid () : 0;	// owner -> auto-close on death
	return NetTcpConnect (pHost, nPort, nPid);
}

int  kapi_tcp_send  (int hSock, const void *pBuf, unsigned nLen) { return NetTcpSend (hSock, pBuf, nLen); }
int  kapi_tcp_recv  (int hSock, void *pBuf, unsigned nLen)       { return NetTcpRecv (hSock, pBuf, nLen); }
void kapi_tcp_close (int hSock)                                  { NetTcpClose (hSock); }

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

	if (pTotalKB) *pTotalKB = nTotal / 1024;
	if (pFreeKB)  *pFreeKB  = (nHeap + nPager) / 1024;
	if (pAppKB)   *pAppKB   = nApp / 1024;
	if (pPageKB)  *pPageKB  = KPAGE_SIZE / 1024;
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

	if (pDetectedKB)    *pDetectedKB    = nDetected;
	if (pAppPoolKB)     *pAppPoolKB     = nPool;
	if (pAppPoolFreeKB) *pAppPoolFreeKB = nFree;
	if (pAbove4GKB)     *pAbove4GKB     = nAbove4G;
	if (pNSegments)     *pNSegments     = CMemorySystem::GetHighSegCount ();
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
	reboot ();
}

}  // extern "C"
