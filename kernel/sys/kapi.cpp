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
#include <kern/debugcon.h>
#include <kern/gui/gimage.h>
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/timer.h>
#include <circle/time.h>
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
		int nXMin   = SCREEN_WIDTH / 5;				// skip the leftmost fifth
		int nXRange = SCREEN_WIDTH  - nOuterW - nXMin;
		int nYRange = SCREEN_HEIGHT - nOuterH;
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

// Launch another app by folder name (apps/<name>.app/main.elf) as a new process.
// Used by the shell (panel / app-list popup). Returns 1 on success, 0 on failure.
int kapi_launch (const char *pName)
{
	return LaunchAppByName (pName) ? 1 : 0;
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

// --- widgets + events --------------------------------------------------------
//
// A widget stores the app's callback ADDRESS. On a click the window manager (the
// input thread) enqueues an event to this app's window; the app's pump
// (kapi_pump_events / kapi_wait_for_exit) dispatches it HERE, in the app's own
// context (its page table + stack are active) -- so the callback runs as if the
// app called it. handler signature: void (sender, int event, long value).

unsigned long kapi_add_button (int x, int y, int w, int h, const char *pLabel,
			       void *pHandler)
{
	CAddressSpace *pAS = CurrentAS ();
	if (pAS == 0)
	{
		return 0;
	}
	CWindow *pWin = pAS->GetWindow ();
	if (pWin == 0)
	{
		return 0;
	}
	GWidget *pW = pWin->AddWidget (GW_BUTTON, x, y, w, h, pLabel, (u64) pHandler);
	return (unsigned long) pW;
}

// Helper: add a widget of nType to the calling app's window. Returns its handle.
static unsigned long AddWidgetToCurrent (int nType, int x, int y, int w, int h,
					 const char *pLabel, void *pHandler)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin == 0)
	{
		return 0;
	}
	return (unsigned long) pWin->AddWidget (nType, x, y, w, h, pLabel, (u64) pHandler);
}

unsigned long kapi_add_label (int x, int y, int w, int h, const char *pText)
{
	return AddWidgetToCurrent (GW_LABEL, x, y, w, h, pText, 0);
}

unsigned long kapi_add_checkbox (int x, int y, int w, int h, const char *pLabel,
				 void *pHandler)
{
	return AddWidgetToCurrent (GW_CHECKBOX, x, y, w, h, pLabel, pHandler);
}

unsigned long kapi_add_textbox (int x, int y, int w, int h, void *pHandler)
{
	return AddWidgetToCurrent (GW_TEXTBOX, x, y, w, h, "", pHandler);
}

// Read a widget's text (label / textbox / textarea content) into pBuf. Returns len.
int kapi_widget_get_text (unsigned long hWidget, char *pBuf, unsigned nMax)
{
	GWidget *pW = (GWidget *) hWidget;
	if (pW == 0 || pBuf == 0 || nMax == 0)
	{
		return 0;
	}
	const char *pSrc = pW->pText != 0 ? pW->pText : pW->Label;	// textarea uses pText
	unsigned i = 0;
	for (; i + 1 < nMax && pSrc[i] != '\0'; i++)
	{
		pBuf[i] = pSrc[i];
	}
	pBuf[i] = '\0';
	return (int) i;
}

// Set a widget's text (label / textbox / textarea content).
void kapi_widget_set_text (unsigned long hWidget, const char *pText)
{
	GWidget *pW = (GWidget *) hWidget;
	if (pW == 0)
	{
		return;
	}
	char    *pDst = pW->pText != 0 ? pW->pText : pW->Label;
	unsigned nCap = pW->pText != 0 ? GW_AREA_CAP : GW_TEXT_MAX;
	unsigned i = 0;
	if (pText != 0)
	{
		for (; i < nCap - 1 && pText[i] != '\0'; i++)
		{
			pDst[i] = pText[i];
		}
	}
	pDst[i] = '\0';
}

unsigned long kapi_add_textarea (int x, int y, int w, int h, void *pHandler)
{
	return AddWidgetToCurrent (GW_TEXTAREA, x, y, w, h, "", pHandler);
}

unsigned long kapi_add_scrollbar_v (int x, int y, int w, int h, void *pHandler)
{
	return AddWidgetToCurrent (GW_SCROLLV, x, y, w, h, "", pHandler);
}

unsigned long kapi_add_scrollbar_h (int x, int y, int w, int h, void *pHandler)
{
	return AddWidgetToCurrent (GW_SCROLLH, x, y, w, h, "", pHandler);
}

// Read an entire SD file into a freshly new[]'d buffer; caller delete[]s. 0 on error.
static u8 *ReadWholeFile (const char *pPath, unsigned *pSize)
{
	FIL File;
	if (pPath == 0 || f_open (&File, pPath, FA_READ) != FR_OK)
	{
		return 0;
	}
	unsigned nSize = (unsigned) f_size (&File);
	u8 *pBuf = new u8[nSize];
	UINT nRead = 0;
	if (pBuf == 0 || f_read (&File, pBuf, nSize, &nRead) != FR_OK || nRead != nSize)
	{
		delete [] pBuf;
		f_close (&File);
		return 0;
	}
	f_close (&File);
	*pSize = nSize;
	return pBuf;
}

// Add a clickable icon: a (magenta-keyed) 24-bpp BMP loaded from pBmpPath, with an
// optional label below. Fires GUI_EVENT_CLICK like a button. pBmpPath may be 0 (a
// placeholder square is drawn). The decoded image is owned by the widget.
unsigned long kapi_add_icon (int x, int y, int w, int h, const char *pBmpPath,
			     const char *pLabel, void *pHandler)
{
	CAddressSpace *pAS = CurrentAS ();
	CWindow *pWin = pAS != 0 ? pAS->GetWindow () : 0;
	if (pWin == 0)
	{
		return 0;
	}

	GImage *pImg = 0;
	if (pBmpPath != 0)
	{
		unsigned nSize = 0;
		u8 *pData = ReadWholeFile (pBmpPath, &nSize);
		if (pData != 0)
		{
			pImg = new GImage;
			if (pImg != 0 && !pImg->LoadBMP (pData, nSize))
			{
				delete pImg;
				pImg = 0;
			}
			delete [] pData;
		}
	}

	GWidget *pW = pWin->AddWidget (GW_ICON, x, y, w, h, pLabel, (u64) pHandler);
	if (pW == 0)
	{
		delete pImg;			// window full: don't leak the image
		return 0;
	}
	pW->pIcon = pImg;
	return (unsigned long) pW;
}

// Replace an icon widget's image (reloads the BMP). pBmpPath may be 0 to clear it.
// Lets the panel repurpose a taskbar slot for a different app. Cheap enough when
// called only on changes (not every frame).
void kapi_widget_set_icon (unsigned long hWidget, const char *pBmpPath)
{
	GWidget *pW = (GWidget *) hWidget;
	if (pW == 0)
	{
		return;
	}

	GImage *pImg = 0;
	if (pBmpPath != 0)
	{
		unsigned nSize = 0;
		u8 *pData = ReadWholeFile (pBmpPath, &nSize);
		if (pData != 0)
		{
			pImg = new GImage;
			if (pImg != 0 && !pImg->LoadBMP (pData, nSize))
			{
				delete pImg;
				pImg = 0;
			}
			delete [] pData;
		}
	}

	GImage *pOld = (GImage *) pW->pIcon;
	pW->pIcon = pImg;			// publish before freeing (cooperative: safe)
	delete pOld;
}

// Set the desktop wallpaper from a 24-bpp BMP on the SD card (drawn behind every
// window by the compositor). Pass 0 to clear it. Returns 1 on success.
int kapi_set_wallpaper (const char *pBmpPath)
{
	if (CWindowManager::Get () == 0)
	{
		return 0;
	}
	if (pBmpPath == 0)
	{
		CWindowManager::Get ()->SetWallpaper (0);
		return 1;
	}

	unsigned nSize = 0;
	u8 *pData = ReadWholeFile (pBmpPath, &nSize);
	if (pData == 0)
	{
		return 0;
	}
	GImage *pImg = new GImage;
	boolean bOK = pImg != 0 && pImg->LoadBMP (pData, nSize);
	delete [] pData;
	if (!bOK)
	{
		delete pImg;
		return 0;
	}
	CWindowManager::Get ()->SetWallpaper (pImg);	// WM takes ownership
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

// Checkbox state (1 = checked).
int kapi_widget_get_checked (unsigned long hWidget)
{
	GWidget *pW = (GWidget *) hWidget;
	return pW != 0 ? pW->nState : 0;
}

unsigned long kapi_add_progress (int x, int y, int w, int h)
{
	return AddWidgetToCurrent (GW_PROGRESS, x, y, w, h, "", 0);
}

unsigned long kapi_add_slider (int x, int y, int w, int h, void *pHandler)
{
	return AddWidgetToCurrent (GW_SLIDER, x, y, w, h, "", pHandler);
}

// Slider / progress value (0..100).
int kapi_widget_get_value (unsigned long hWidget)
{
	GWidget *pW = (GWidget *) hWidget;
	return pW != 0 ? pW->nState : 0;
}

void kapi_widget_set_value (unsigned long hWidget, int nValue)
{
	GWidget *pW = (GWidget *) hWidget;
	if (pW == 0)
	{
		return;
	}
	if (nValue < 0) nValue = 0; else if (nValue > 100) nValue = 100;
	pW->nState = nValue;
}

// Move/resize a widget after creation (client-relative). Set w=h=0 to hide it
// (drawn empty, never hit). Used by the panel to lay out its dynamic taskbar.
void kapi_widget_set_rect (unsigned long hWidget, int x, int y, int w, int h)
{
	GWidget *pW = (GWidget *) hWidget;
	if (pW == 0)
	{
		return;
	}
	pW->nX = x; pW->nY = y; pW->nW = w; pW->nH = h;
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

// --- streams / stdio / processes ---------------------------------------------

void *kapi_pipe (void)
{
	return new CPipeStream;
}

void *kapi_file_in (const char *pPath)
{
	CFileStream *pFile = new CFileStream (pPath, 0);
	if (pFile == 0) return 0;
	if (!pFile->IsValid ()) { delete pFile; return 0; }
	return pFile;
}

void *kapi_file_out (const char *pPath, int bAppend)
{
	CFileStream *pFile = new CFileStream (pPath, bAppend ? 2 : 1);
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

// Spawn a console program (ELF at pPath) with stdin/stdout streams + argv. Returns
// a process handle for kapi_wait, or 0 on failure.
void *kapi_spawn (const char *pPath, const char *pArgs, void *pStdin, void *pStdout)
{
	return SpawnProcess (pPath, pArgs, (CStream *) pStdin, (CStream *) pStdout);
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
	DIR *pDir = new DIR;
	if (pDir == 0)
	{
		return 0;
	}
	if (f_opendir (pDir, pPath) != FR_OK)
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
	return (pPath != 0 && f_mkdir (pPath) == FR_OK) ? 0 : -1;
}

int kapi_remove (const char *pPath)		// file or empty directory
{
	return (pPath != 0 && f_unlink (pPath) == FR_OK) ? 0 : -1;
}

int kapi_rename (const char *pFrom, const char *pTo)
{
	return (pFrom != 0 && pTo != 0 && f_rename (pFrom, pTo) == FR_OK) ? 0 : -1;
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

// Write a whole file (create/truncate). Returns bytes written, or -1 on error.
int kapi_save_file (const char *pPath, const void *pBuf, unsigned nLen)
{
	if (pPath == 0)
	{
		return -1;
	}
	FIL File;
	if (f_open (&File, pPath, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return -1;
	}
	UINT nWritten = 0;
	FRESULT Res = f_write (&File, pBuf, nLen, &nWritten);
	f_close (&File);
	return (Res == FR_OK) ? (int) nWritten : -1;
}

}  // extern "C"
