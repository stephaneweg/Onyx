//
// window.cpp
//
#include <kern/gui/window.h>
#include <kern/gui/gimage.h>		// GImage (window canvas + chrome buffers)
#include <kern/layout.h>		// KPAGE_SIZE / KPAGE_MASK
#include <circle/util.h>		// memset
#include <circle/new.h>
#include <assert.h>

CWindow::CWindow (int x, int y, int nClientW, int nClientH, const char *pTitle,
		  unsigned nFlags)
:	m_nX (x), m_nY (y), m_nFlags (nFlags),
	m_nLogicalW (nClientW), m_nLogicalH (nClientH),
	m_pRawAlloc (0), m_ulCanvasPhys (0), m_nCanvasPages (0),
	m_nOuterW (0), m_nOuterH (0),
	m_ulKeyHandler (0), m_ulClickHandler (0), m_ulPointerHandler (0),
	m_nEvHead (0), m_nEvTail (0), m_bExitRequested (FALSE)
{
	// Copy the title (the caller's string may live in a transient address space).
	m_Title[0] = '\0';
	if (pTitle != 0)
	{
		unsigned i;
		for (i = 0; i < sizeof (m_Title) - 1 && pTitle[i] != '\0'; i++)
		{
			m_Title[i] = pTitle[i];
		}
		m_Title[i] = '\0';
	}

	// Allocate the canvas as a page-aligned, contiguous block: over-allocate from
	// the (identity-mapped) heap and align the start up to 64 KB, so PA == the
	// aligned VA and the region can be mapped into a process address space.
	unsigned nBytes = (unsigned) (nClientW * nClientH) * sizeof (u32);
	m_nCanvasPages = (nBytes + KPAGE_MASK) / KPAGE_SIZE;
	if (m_nCanvasPages == 0)
	{
		m_nCanvasPages = 1;
	}

	m_pRawAlloc = new u8[m_nCanvasPages * KPAGE_SIZE + KPAGE_SIZE];
	if (m_pRawAlloc == 0)
	{
		return;
	}

	uintptr ulAligned = ((uintptr) m_pRawAlloc + KPAGE_MASK) & ~((uintptr) KPAGE_MASK);
	m_ulCanvasPhys = ulAligned;		// identity region: PA == kernel VA
	memset ((void *) ulAligned, 0, m_nCanvasPages * KPAGE_SIZE);

	m_Canvas.Wrap ((u32 *) ulAligned, nClientW, nClientH);

	// Chrome buffers (normal windows only): two SEPARATE page-aligned, contiguous
	// copies (active, inactive), each OuterW x OuterH. The app draws its decorations
	// into them (via USER_WINDOW_CHROME / _INACTIVE); the compositor blits the focused
	// copy. Separate allocations keep each copy small enough for the heap to reuse on
	// free. Borderless windows have no chrome, so we skip it.
	m_pChromeRaw[0] = m_pChromeRaw[1] = 0;
	m_ulChromePhys[0] = m_ulChromePhys[1] = 0;
	m_nChromePages[0] = m_nChromePages[1] = 0;
	if ((nFlags & WIN_FLAG_BORDERLESS) == 0)
	{
		m_nOuterW = nClientW + 2 * WIN_BORDER;
		m_nOuterH = nClientH + WIN_TITLEBAR_H + WIN_BORDER;
		unsigned nCopyBytes = (unsigned) (m_nOuterW * m_nOuterH) * sizeof (u32);
		unsigned nPages = (nCopyBytes + KPAGE_MASK) / KPAGE_SIZE;
		if (nPages == 0)
		{
			nPages = 1;
		}
		for (int i = 0; i < 2; i++)
		{
			m_pChromeRaw[i] = new u8[nPages * KPAGE_SIZE + KPAGE_SIZE];
			if (m_pChromeRaw[i] == 0)
			{
				break;				// HasChrome() stays false if [0] failed
			}
			uintptr ulC = ((uintptr) m_pChromeRaw[i] + KPAGE_MASK) & ~((uintptr) KPAGE_MASK);
			m_ulChromePhys[i] = ulC;
			m_nChromePages[i] = nPages;
			memset ((void *) ulC, 0, nPages * KPAGE_SIZE);
		}
	}
}

CWindow::~CWindow (void)
{
	if (m_pRawAlloc != 0)
	{
		delete [] (u8 *) m_pRawAlloc;
		m_pRawAlloc = 0;
	}
	for (int i = 0; i < 2; i++)
	{
		if (m_pChromeRaw[i] != 0)
		{
			delete [] (u8 *) m_pChromeRaw[i];
			m_pChromeRaw[i] = 0;
		}
	}
}

void CWindow::SetLogicalSize (int w, int h)
{
	int maxW = m_Canvas.Width ();
	int maxH = m_Canvas.Height ();
	if (w < 1) w = 1; else if (w > maxW) w = maxW;
	if (h < 1) h = 1; else if (h > maxH) h = maxH;
	m_nLogicalW = w;
	m_nLogicalH = h;
}


void CWindow::DrawTo (GImage *pScreen, boolean bActive)
{
	int cw = ClientWidth ();		// logical size (may be < allocated canvas)
	int ch = ClientHeight ();

	// Outer top-left (chrome + client). A borderless window has zero chrome, so the
	// client area fills the whole window.
	int x0 = m_nX;
	int y0 = m_nY;

	// Window chrome is now drawn USER-SIDE: the app renders its title bar / borders /
	// close box into two pre-composited copies (active + inactive). The compositor just
	// picks the copy matching focus and blits it -- OPAQUE: the chrome is a plain
	// rectangle (no rounded/transparent corners), so we skip the per-pixel transparency
	// test. The kernel keeps only the chrome BEHAVIOUR (title-bar drag, close-box hit).
	if (!Borderless () && m_ulChromePhys[0] != 0)
	{
		u32 *pCopy = (u32 *) m_ulChromePhys[bActive ? 0 : 1];
		GImage Chrome (pCopy, m_nOuterW, m_nOuterH);
		pScreen->PutOther (&Chrome, x0, y0, FALSE);
	}

	// Client area = the owner's canvas, blitted opaque inside the chrome. Only the
	// logical sub-rect is shown (the canvas may be over-allocated for resizing).
	int clientX = x0 + ChromeL ();
	int clientY = y0 + ChromeT ();
	pScreen->PutOtherPart (&m_Canvas, clientX, clientY, 0, 0, cw, ch, FALSE);

}

void CWindow::CloseBoxRect (int *px0, int *py0, int *px1, int *py1) const
{
	int nSize = WIN_TITLEBAR_H - 10;		// square inset in the title bar
	int x1 = m_nX + m_Canvas.Width () + 2 * WIN_BORDER - 1;
	*px1 = x1 - 4;
	*px0 = *px1 - nSize;
	*py0 = m_nY + 5;
	*py1 = *py0 + nSize;
}

boolean CWindow::HitCloseBox (int sx, int sy) const
{
	if (Borderless ())
	{
		return FALSE;			// no close box on a borderless window
	}
	int x0, y0, x1, y1;
	CloseBoxRect (&x0, &y0, &x1, &y1);
	return sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1;
}

void CWindow::PushEvent (const GUIEvent &Event)
{
	m_EvLock.Acquire ();
	unsigned nNext = (m_nEvHead + 1) % WIN_EVENT_QUEUE;
	if (nNext != m_nEvTail)			// drop if the ring is full
	{
		m_Events[m_nEvHead] = Event;
		m_nEvHead = nNext;
	}
	m_EvLock.Release ();
}

boolean CWindow::PopEvent (GUIEvent *pEvent)
{
	boolean bGot = FALSE;
	m_EvLock.Acquire ();
	if (m_nEvTail != m_nEvHead)
	{
		*pEvent = m_Events[m_nEvTail];
		m_nEvTail = (m_nEvTail + 1) % WIN_EVENT_QUEUE;
		bGot = TRUE;
	}
	m_EvLock.Release ();
	return bGot;
}

CWindowManager *CWindowManager::s_pThis = 0;

// Actual framebuffer size in use; initialised to the compile-time defaults and
// overwritten at boot (CKernel) once the chosen resolution is known.
int g_nScreenWidth  = SCREEN_WIDTH;
int g_nScreenHeight = SCREEN_HEIGHT;

// Title-bar text colour (overridable at boot from SD:skins/theme.txt).
u32 g_WinTitleTextColor = 0x00FFFFFF;

CWindowManager::CWindowManager (void)
:	m_nWindows (0), m_pWallpaper (0), m_pCursor (0),
	m_pWallRaw (0), m_ulWallPhys (0), m_nWallPages (0), m_bLiveWall (FALSE),
	m_nCursorX (SCREEN_WIDTH / 2), m_nCursorY (SCREEN_HEIGHT / 2),
	m_nPrevX (0), m_nPrevY (0),
	m_bCursorShown (FALSE), m_nLastButtons (0),
	m_pDragWindow (0), m_nDragDX (0), m_nDragDY (0),
	m_pPtrOverWindow (0), m_pPtrCaptureWindow (0)
{
	assert (s_pThis == 0);
	s_pThis = this;

	for (unsigned i = 0; i < WM_MAX_WINDOWS; i++)
	{
		m_pWindows[i] = 0;
	}
}

void CWindowManager::Add (CWindow *pWindow)
{
	assert (pWindow != 0);
	m_SpinLock.Acquire ();
	if (m_nWindows < WM_MAX_WINDOWS)
	{
		m_pWindows[m_nWindows++] = pWindow;
	}
	m_SpinLock.Release ();
}

void CWindowManager::Remove (CWindow *pWindow)
{
	m_SpinLock.Acquire ();
	// Drop any references into this window (it may be freed right after).
	if (m_pDragWindow == pWindow)		{ m_pDragWindow = 0; }
	if (m_pPtrOverWindow == pWindow)	{ m_pPtrOverWindow = 0; }
	if (m_pPtrCaptureWindow == pWindow)	{ m_pPtrCaptureWindow = 0; }
	for (unsigned i = 0; i < m_nWindows; i++)
	{
		if (m_pWindows[i] == pWindow)
		{
			// Shift the rest down to keep draw order.
			for (unsigned j = i; j + 1 < m_nWindows; j++)
			{
				m_pWindows[j] = m_pWindows[j + 1];
			}
			m_pWindows[--m_nWindows] = 0;
			break;
		}
	}
	m_SpinLock.Release ();
}

// Caller holds m_SpinLock.
void CWindowManager::RaiseLocked (CWindow *pWindow)
{
	for (unsigned i = 0; i < m_nWindows; i++)
	{
		if (m_pWindows[i] == pWindow)
		{
			for (unsigned j = i; j + 1 < m_nWindows; j++)
			{
				m_pWindows[j] = m_pWindows[j + 1];
			}
			m_pWindows[m_nWindows - 1] = pWindow;	// move to top (drawn last)
			break;
		}
	}
}

void CWindowManager::Raise (CWindow *pWindow)
{
	m_SpinLock.Acquire ();
	RaiseLocked (pWindow);
	m_SpinLock.Release ();
}

void CWindowManager::Composite (GImage *pScreen)
{
	// Snapshot the window list under the lock, then blit without holding it (so we
	// don't keep IRQ masked for the whole frame). Note: a window pointer in the
	// snapshot must stay valid while we blit -- process teardown removes a window
	// from the list but does NOT free the CWindow (see CAddressSpace::~), so the
	// memory remains valid here. (Proper deferred free is future work.)
	CWindow *pSnapshot[WM_MAX_WINDOWS];
	unsigned nCount;
	GImage  *pWall;

	m_SpinLock.Acquire ();
	nCount = m_nWindows;
	for (unsigned i = 0; i < nCount; i++)
	{
		pSnapshot[i] = m_pWindows[i];
	}
	// A committed app-written wallpaper (m_WallImage) takes priority over the
	// kernel-set one (m_pWallpaper).
	pWall = (m_bLiveWall && m_WallImage.IsValid ()) ? &m_WallImage : m_pWallpaper;
	m_SpinLock.Release ();

	// Desktop background: the wallpaper if set (filled behind it for any margin),
	// otherwise the solid desktop colour.
	pScreen->Clear (WIN_COLOR_DESKTOP);
	if (pWall != 0 && pWall->IsValid ())
	{
		pScreen->PutOther (pWall, 0, 0, FALSE);
	}

	// Draw back-to-front; the last (topmost) window is the active one.
	for (unsigned i = 0; i < nCount; i++)
	{
		if (pSnapshot[i] != 0)
		{
			pSnapshot[i]->DrawTo (pScreen, i == nCount - 1);
		}
	}

	// Cursor, drawn last so it floats above everything. Prefer the loaded cursor
	// bitmap (mousecur.bin); its hot-spot is the top-left corner. Without it, fall
	// back to a drawn black-bordered white arrow whose tip is at (cx,cy).
	if (m_bCursorShown)
	{
		int cx = m_nCursorX;
		int cy = m_nCursorY;
		if (m_pCursor != 0 && m_pCursor->IsValid ())
		{
			pScreen->PutOther (m_pCursor, cx, cy, TRUE);
		}
		else
		{
			for (int r = 0; r < 16; r++)
			{
				for (int c = 0; c <= r; c++)
				{
					// White fill, black on the two edges for contrast.
					u32 col = (c == 0 || c == r) ? 0x00000000 : 0x00FFFFFF;
					pScreen->SetPixel (cx + c, cy + r, col);
				}
				// Close the bottom edge with a black pixel.
				pScreen->SetPixel (cx + r, cy + r, 0x00000000);
			}
		}
	}
}

void CWindowManager::SetWallpaper (GImage *pImage)
{
	m_SpinLock.Acquire ();
	GImage *pOld = m_pWallpaper;
	m_pWallpaper = pImage;
	m_SpinLock.Release ();

	if (pOld != 0)				// free the previous wallpaper outside the lock
	{
		delete pOld;
	}
}

// Allocate (once) a page-aligned, contiguous, screen-sized wallpaper buffer the same
// way a window canvas is allocated, so the kapi layer can map it into a writer app.
// Returns the buffer's physical (== kernel VA) address + page count.
u32 *CWindowManager::EnsureWallpaperBuffer (int nW, int nH, u64 *pPhys, unsigned *pnPages)
{
	if (m_pWallRaw == 0)
	{
		unsigned nBytes = (unsigned) (nW * nH) * sizeof (u32);
		m_nWallPages = (nBytes + KPAGE_MASK) / KPAGE_SIZE;
		if (m_nWallPages == 0) m_nWallPages = 1;

		m_pWallRaw = new u8[m_nWallPages * KPAGE_SIZE + KPAGE_SIZE];
		if (m_pWallRaw == 0) { m_nWallPages = 0; return 0; }

		uintptr ulAligned = ((uintptr) m_pWallRaw + KPAGE_MASK) & ~((uintptr) KPAGE_MASK);
		m_ulWallPhys = ulAligned;		// identity region: PA == kernel VA
		memset ((void *) ulAligned, 0, m_nWallPages * KPAGE_SIZE);
		m_WallImage.Wrap ((u32 *) ulAligned, nW, nH);
	}
	if (pPhys)   *pPhys   = m_ulWallPhys;
	if (pnPages) *pnPages = m_nWallPages;
	return (u32 *) m_ulWallPhys;
}

// Integer square root (no FP: apps/kernel build with -mgeneral-regs-only).
static unsigned IntSqrt (unsigned n)
{
	if (n == 0)
	{
		return 0;
	}
	unsigned x = n, y = (x + 1) / 2;
	while (y < x)
	{
		x = y;
		y = (x + n / x) / 2;
	}
	return x;
}

// Tint nBase by a brightness derived from the distance (near a seed = dark cell
// centre, far = bright edge), with a floor so cells never go fully black. Ported
// from SimpleOS ComputeColor() (temp/Background.bas).
static u32 VoronoiTint (u32 nBase, unsigned nDist)
{
	const unsigned nMin = 96;
	unsigned cc = nMin + nDist * (255 - nMin) / 255;	// 96..255
	if (cc > 255) cc = 255;
	unsigned r = (((nBase >> 16) & 0xFF) * cc) >> 8;
	unsigned g = (((nBase >> 8)  & 0xFF) * cc) >> 8;
	unsigned b = (( nBase        & 0xFF) * cc) >> 8;
	return (r << 16) | (g << 8) | b;			// 0x00RRGGBB
}

void CWindowManager::GenerateWallpaper (u32 nBaseColor, int nPoints, unsigned nSeed)
{
	const int adiv = 2;				// render at half-res, then upscale
	const int nW = g_nScreenWidth;
	const int nH = g_nScreenHeight;
	const int mx = nW / adiv;
	const int my = nH / adiv;

	if (nPoints < 1)  nPoints = 1;
	if (nPoints > 64) nPoints = 64;
	if (nSeed == 0)   nSeed = 1;

	int px[64], py[64];				// seed points (half-res space)
	unsigned nRng = nSeed;
	for (int i = 0; i < nPoints; i++)
	{
		nRng = nRng * 1103515245u + 12345u; px[i] = (int) (nRng % (unsigned) mx);
		nRng = nRng * 1103515245u + 12345u; py[i] = (int) (nRng % (unsigned) my);
	}

	GImage *pImg = new GImage;
	if (pImg == 0)
	{
		return;
	}
	pImg->SetSize (nW, nH);
	if (!pImg->IsValid ())
	{
		delete pImg;
		return;
	}
	u32 *pBuf = pImg->Buffer ();

	for (int y = 0; y < my; y++)
	{
		for (int x = 0; x < mx; x++)
		{
			unsigned nBest = 0xFFFFFFFF;
			for (int i = 0; i < nPoints; i++)
			{
				// Toroidal axis distance, normalised to 0..128 (wraps at 128
				// so the field tiles seamlessly).
				int dx = x > px[i] ? x - px[i] : px[i] - x;
				dx = dx * 256 / mx; if (dx > 128) dx = 256 - dx;
				int dy = y > py[i] ? y - py[i] : py[i] - y;
				dy = dy * 256 / my; if (dy > 128) dy = 256 - dy;

				unsigned d = IntSqrt ((unsigned) (dx * dx + dy * dy));
				if (d > 255) d = 255;
				if (d < nBest) nBest = d;
			}

			u32 nCol = VoronoiTint (nBaseColor, nBest);
			for (int j = 0; j < adiv; j++)		// upscale the half-res cell
			{
				for (int k = 0; k < adiv; k++)
				{
					pBuf[(y * adiv + j) * nW + (x * adiv + k)] = nCol;
				}
			}
		}
	}

	SetWallpaper (pImg);				// WM takes ownership
}

// Caller holds m_SpinLock.
unsigned CWindowManager::HitTest (int x, int y, boolean *pbOnTitleBar)
{
	*pbOnTitleBar = FALSE;
	// Top-down: the last window in the list is topmost.
	for (int i = (int) m_nWindows - 1; i >= 0; i--)
	{
		CWindow *pWin = m_pWindows[i];
		if (pWin == 0)
		{
			continue;
		}
		int x0 = pWin->X ();
		int y0 = pWin->Y ();
		int x1 = x0 + pWin->ClientWidth () + pWin->ChromeL () + pWin->ChromeR () - 1;
		int y1 = y0 + pWin->ChromeT () + pWin->ClientHeight () + pWin->ChromeB () - 1;
		if (x >= x0 && x <= x1 && y >= y0 && y <= y1)
		{
			// Borderless windows have no title bar (ChromeT == 0), so a hit is
			// always in the client area -- never a drag region.
			*pbOnTitleBar = !pWin->Borderless () && (y < y0 + pWin->ChromeT ());
			return (unsigned) i;
		}
	}
	return ~0u;
}

// Push one GUI_EVENT_PTR_* event to a window's pointer handler (client coords,
// clamped non-negative; the toolkit treats out-of-bounds as "over no widget").
void CWindowManager::EmitPointer (CWindow *pWin, int nEvent, int cx, int cy,
				  unsigned nButtons, unsigned nChanged)
{
	if (pWin == 0) return;
	u64 ulH = pWin->PointerHandler ();
	if (ulH == 0) return;
	if (cx < 0) cx = 0; else if (cx > 0xFFFF) cx = 0xFFFF;
	if (cy < 0) cy = 0; else if (cy > 0xFFFF) cy = 0xFFFF;
	GUIEvent Ev;
	Ev.ulHandler = ulH;
	Ev.ulSender  = 0;
	Ev.nEvent    = nEvent;
	Ev.lValue    = ((long) (nChanged & 0xFF) << 40) | ((long) (nButtons & 0xFF) << 32)
		     | ((long) (cx & 0xFFFF) << 16) | (long) (cy & 0xFFFF);
	pWin->PushEvent (Ev);
}

// Parse the next logical key from a Circle cooked-mode string, advancing *pp.
// Returns 0 at end of string. Printable/control bytes return their value; VT100
// escape sequences (cursor/home/end/page/del) map to KEY_* codes.
static int NextKey (const char **pp)
{
	const char *p = *pp;
	if (*p == '\0')
	{
		return 0;
	}
	if (p[0] == 0x1b && p[1] == '[')
	{
		int code = 0, adv = 0;
		switch (p[2])
		{
		case 'A': code = KEY_UP;    adv = 3; break;
		case 'B': code = KEY_DOWN;  adv = 3; break;
		case 'C': code = KEY_RIGHT; adv = 3; break;
		case 'D': code = KEY_LEFT;  adv = 3; break;
		case '1': if (p[3] == '~') { code = KEY_HOME; adv = 4; } break;
		case '3': if (p[3] == '~') { code = KEY_DEL;  adv = 4; } break;
		case '4': if (p[3] == '~') { code = KEY_END;  adv = 4; } break;
		case '5': if (p[3] == '~') { code = KEY_PGUP; adv = 4; } break;
		case '6': if (p[3] == '~') { code = KEY_PGDN; adv = 4; } break;
		}
		if (adv != 0)
		{
			*pp = p + adv;
			return code;
		}
		*pp = p + 2;			// unknown escape: skip ESC '[' and continue
		return NextKey (pp);
	}
	*pp = p + 1;
	// Normalise the keys Circle's keymap delivers as raw control bytes: Enter comes
	// as '\n' (and some keyboards send '\r'), Backspace as DEL (0x7F). Map both to the
	// KEY_* codes apps expect, so every app/dialog sees a single consistent value.
	unsigned char ch = (unsigned char) p[0];
	if (ch == '\n' || ch == '\r') return KEY_ENTER;
	if (ch == 0x7F)               return KEY_BACKSPACE;
	return ch;
}

void CWindowManager::OnMouse (int x, int y, unsigned nButtons)
{
	if (x < 0) x = 0; else if (x >= g_nScreenWidth)  x = g_nScreenWidth - 1;
	if (y < 0) y = 0; else if (y >= g_nScreenHeight) y = g_nScreenHeight - 1;

	m_SpinLock.Acquire ();
	m_nCursorX = x; m_nCursorY = y; m_bCursorShown = TRUE;

	boolean bLeftNow = (nButtons & 1) != 0;
	boolean bLeftWas = (m_nLastButtons & 1) != 0;

	if (bLeftNow && !bLeftWas)
	{
		// Press edge: raise the window under the cursor, then close box / title-bar
		// drag / a left canvas-click for an app-drawn UI (e.g. SameGame).
		boolean bOnTitle = FALSE;
		unsigned nHit = HitTest (x, y, &bOnTitle);
		if (nHit != ~0u)
		{
			CWindow *pWin = m_pWindows[nHit];
			RaiseLocked (pWin);
			if (pWin->HitCloseBox (x, y))
			{
				pWin->RequestExit ();
			}
			else if (bOnTitle)
			{
				m_pDragWindow = pWin;
				m_nDragDX = x - pWin->X ();
				m_nDragDY = y - pWin->Y ();
			}
			else if (pWin->ClickHandler () != 0)
			{
				int cx = x - (pWin->X () + pWin->ChromeL ());
				int cy = y - (pWin->Y () + pWin->ChromeT ());
				GUIEvent Ev;
				Ev.ulHandler = pWin->ClickHandler ();
				Ev.ulSender  = 0;
				Ev.nEvent    = GUI_EVENT_CANVAS_CLICK;
				Ev.lValue    = ((long) 1 << 32) | ((long) cx << 16) | (cy & 0xFFFF);
				pWin->PushEvent (Ev);
			}
		}
	}
	else if (bLeftNow && bLeftWas)
	{
		if (m_pDragWindow != 0)
			m_pDragWindow->Move (x - m_nDragDX, y - m_nDragDY);
	}
	else if (!bLeftNow && bLeftWas)
	{
		m_pDragWindow = 0;
	}

	// Right-click press + drag motion for app-drawn UIs (left press handled above).
	boolean bRightNow = (nButtons & 2) != 0;
	boolean bRightWas = (m_nLastButtons & 2) != 0;
	if (m_pDragWindow == 0)
	{
		boolean bOnTitle = FALSE;
		unsigned nHit = HitTest (x, y, &bOnTitle);
		if (nHit != ~0u && !bOnTitle)
		{
			CWindow *pWin = m_pWindows[nHit];
			u64 ulCH = pWin->ClickHandler ();
			int cx = x - (pWin->X () + pWin->ChromeL ());
			int cy = y - (pWin->Y () + pWin->ChromeT ());
			if (ulCH != 0 && !pWin->HitCloseBox (x, y))
			{
				unsigned nBtn = (bLeftNow ? 1 : 0) | (bRightNow ? 2 : 0);
				long lValue = ((long) nBtn << 32) | ((long) cx << 16) | (cy & 0xFFFF);
				GUIEvent Ev;
				Ev.ulHandler = ulCH; Ev.ulSender = 0; Ev.lValue = lValue;
				if (bRightNow && !bRightWas)
				{
					Ev.nEvent = GUI_EVENT_CANVAS_CLICK;
					pWin->PushEvent (Ev);
				}
				else if ((bLeftNow || bRightNow) && (x != m_nPrevX || y != m_nPrevY))
				{
					Ev.nEvent = GUI_EVENT_CANVAS_MOTION;
					pWin->PushEvent (Ev);
				}
			}
		}
	}

	// Pointer stream for user-side widget toolkits (enter/leave/move/down/up; a
	// button-drag captures the pointer so the stream stays with that window).
	{
		boolean bOnTitleP = FALSE;
		unsigned nHitP = HitTest (x, y, &bOnTitleP);
		CWindow *pOver = 0;
		if (nHitP != ~0u)
		{
			CWindow *pW = m_pWindows[nHitP];
			if (pW->PointerHandler () != 0 && !bOnTitleP && !pW->HitCloseBox (x, y))
				pOver = pW;
		}
		if (m_pPtrCaptureWindow == 0 && pOver != m_pPtrOverWindow)
		{
			if (m_pPtrOverWindow != 0)
				EmitPointer (m_pPtrOverWindow, GUI_EVENT_PTR_LEAVE,
					m_nPrevX - (m_pPtrOverWindow->X () + m_pPtrOverWindow->ChromeL ()),
					m_nPrevY - (m_pPtrOverWindow->Y () + m_pPtrOverWindow->ChromeT ()),
					nButtons, 0);
			m_pPtrOverWindow = pOver;
			if (pOver != 0)
				EmitPointer (pOver, GUI_EVENT_PTR_ENTER,
					x - (pOver->X () + pOver->ChromeL ()),
					y - (pOver->Y () + pOver->ChromeT ()), nButtons, 0);
		}
		CWindow *pTarget = m_pPtrCaptureWindow ? m_pPtrCaptureWindow : pOver;
		if (pTarget != 0)
		{
			int cx = x - (pTarget->X () + pTarget->ChromeL ());
			int cy = y - (pTarget->Y () + pTarget->ChromeT ());
			for (unsigned b = 1; b <= 4; b <<= 1)		// left=1 right=2 middle=4
			{
				boolean now = (nButtons & b) != 0, was = (m_nLastButtons & b) != 0;
				if (now && !was)
				{
					if (m_pPtrCaptureWindow == 0 && pOver != 0)
						m_pPtrCaptureWindow = pOver;
					EmitPointer (pTarget, GUI_EVENT_PTR_DOWN, cx, cy, nButtons, b);
				}
				else if (!now && was)
					EmitPointer (pTarget, GUI_EVENT_PTR_UP, cx, cy, nButtons, b);
			}
			if (x != m_nPrevX || y != m_nPrevY)
				EmitPointer (pTarget, GUI_EVENT_PTR_MOVE, cx, cy, nButtons, 0);
		}
		if ((nButtons & 7) == 0)
			m_pPtrCaptureWindow = 0;
	}

	m_nPrevX = x; m_nPrevY = y; m_nLastButtons = nButtons;
	m_SpinLock.Release ();
}

void CWindowManager::OnKey (const char *pString)
{
	if (pString == 0) return;
	m_SpinLock.Acquire ();
	// Deliver keys to the topmost window's app-level key handler. Apps own their text
	// input via the user-side uikit toolkit -- no kernel widgets or dialogs any more.
	CWindow *pTop = m_nWindows > 0 ? m_pWindows[m_nWindows - 1] : 0;
	u64 ulKeyHandler = pTop != 0 ? pTop->KeyHandler () : 0;
	if (pTop != 0 && ulKeyHandler != 0)
	{
		const char *p = pString; int code;
		while ((code = NextKey (&p)) != 0)
		{
			GUIEvent Ev;
			Ev.ulHandler = ulKeyHandler;
			Ev.ulSender  = 0;
			Ev.nEvent    = GUI_EVENT_KEY;
			Ev.lValue    = code;
			pTop->PushEvent (Ev);
		}
	}
	m_SpinLock.Release ();
}

