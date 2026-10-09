//
// window.cpp
//
#include <kern/gui/window.h>
#include <kern/gui/gimage.h>		// GImage (window canvas + chrome buffers)
#include <kern/layout.h>		// KPAGE_SIZE / KPAGE_MASK
#include <circle/util.h>		// memset
#include <circle/timer.h>		// GetTicks (last-pump stamp for the watchdog)
#include <circle/new.h>
#include <circle/sched/synchronizationevent.h>	// m_pWake (the owner's pump)
#include <assert.h>

// A window's pixels (kern/gui/window.h): the heap's here; the graphics server (WIN_PIXELS_HOOK) gives
// its own -- memory shared with the window's program.
#ifndef WIN_PIXELS_HOOK
void *WinPixelsAlloc (int, unsigned nBytes)	{ u8 *p = new u8[nBytes]; if (p != 0) memset (p, 0, nBytes); return p; }
void WinPixelsFree (void *pRaw)			{ delete [] (u8 *) pRaw; }
#endif

CWindow::CWindow (int x, int y, int nClientW, int nClientH, const char *pTitle,
		  unsigned nFlags)
:	m_nGen (0), m_nChromeGen (0), m_nX (x), m_nY (y), m_nFlags (nFlags),
	m_nLogicalW (nClientW), m_nLogicalH (nClientH),
	m_pRawAlloc (0), m_ulCanvasPhys (0), m_nCanvasPages (0),
	m_nOuterW (0), m_nOuterH (0),
	m_ulKeyHandler (0), m_ulClickHandler (0), m_ulPointerHandler (0),
	m_nMinLogicalH (nClientH), m_nAlpha (255), m_ulMenuHandler (0), m_nMenuGen (0),
	m_nEvHead (0), m_nEvTail (0), m_nEvDropped (0), m_pWake (0), m_nLastPump (0),
	m_bExitRequested (FALSE), m_bMinimised (FALSE), m_nDesk (0), m_bOffDesk (FALSE), m_bAside (FALSE), m_bPinned (FALSE), m_bNoInset (FALSE),
	m_nChromeGenShown (0), m_nRetireFrame (0)
{
	m_nCursorShape = 0;				// (the arrow)
	m_bResizable = FALSE; m_nMinW = 64; m_nMinH = 32;
	m_pRetired[0] = m_pRetired[1] = m_pRetired[2] = 0;
	static unsigned s_nNextId = 0;
	m_nId = ++s_nNextId;
	m_nOwnerPid = 0;
	m_Menu[0] = '\0';

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

	m_pRawAlloc = WinPixelsAlloc (0, m_nCanvasPages * KPAGE_SIZE + KPAGE_SIZE);
	if (m_pRawAlloc == 0)
	{
		return;
	}

	uintptr ulAligned = ((uintptr) m_pRawAlloc + KPAGE_MASK) & ~((uintptr) KPAGE_MASK);
	m_ulCanvasPhys = ulAligned;		// identity region: PA == kernel VA

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
		AllocChrome (m_nOuterW, m_nOuterH, m_pChromeRaw, m_ulChromePhys, m_nChromePages);
	}
}

// The two chrome copies for an nOuterW x nOuterH frame (zeroed): both or none (FALSE).
boolean CWindow::AllocChrome (int nOuterW, int nOuterH, void *pRaw[2], u64 ulPhys[2], unsigned nPages[2])
{
	unsigned nCopyBytes = (unsigned) (nOuterW * nOuterH) * sizeof (u32);
	unsigned n = (nCopyBytes + KPAGE_MASK) / KPAGE_SIZE;
	if (n == 0)
	{
		n = 1;
	}
	pRaw[0] = pRaw[1] = 0; ulPhys[0] = ulPhys[1] = 0; nPages[0] = nPages[1] = 0;
	for (int i = 0; i < 2; i++)
	{
		pRaw[i] = WinPixelsAlloc (1 + i, n * KPAGE_SIZE + KPAGE_SIZE);
		if (pRaw[i] == 0)
		{
			break;
		}
		uintptr ulC = ((uintptr) pRaw[i] + KPAGE_MASK) & ~((uintptr) KPAGE_MASK);
		ulPhys[i] = ulC;
		nPages[i] = n;
	}
	if (ulPhys[1] == 0)				// (the inactive copy is mapped too: never half)
	{
		if (pRaw[0] != 0) WinPixelsFree (pRaw[0]);
		pRaw[0] = 0; ulPhys[0] = 0; nPages[0] = 0;
		return FALSE;
	}
	return TRUE;
}

CWindow::~CWindow (void)
{
	for (int i = 0; i < 3; i++)
	{
		if (m_pRetired[i] != 0) { WinPixelsFree (m_pRetired[i]); m_pRetired[i] = 0; }
	}
	if (m_pRawAlloc != 0)
	{
		WinPixelsFree (m_pRawAlloc);
		m_pRawAlloc = 0;
	}
	for (int i = 0; i < 2; i++)
	{
		if (m_pChromeRaw[i] != 0)
		{
			WinPixelsFree (m_pChromeRaw[i]);
			m_pChromeRaw[i] = 0;
		}
	}
}

void CWindow::SetLogicalSize (int w, int h)
{
	Damage ();					// the old size (and the new one below)
	int maxW = m_Canvas.Width ();
	int maxH = m_Canvas.Height ();
	if (w < 1) w = 1; else if (w > maxW) w = maxW;
	if (h < 1) h = 1; else if (h > maxH) h = maxH;
	m_nLogicalW = w;
	m_nLogicalH = h;
	if (HasChrome ())				// the frame follows (its copies were made for the
	{						// canvas's full size: always big enough); the app
		m_nOuterW = ChromeL () + w + ChromeR ();	// redraws it (uikit: uk_decorate_window)
		m_nOuterH = ChromeT () + h + ChromeB ();
		m_nChromeGen++;
	}
	if (h < m_nMinLogicalH)
	{
		m_nMinLogicalH = h;
	}
	Damage ();
}

void CWindow::SetMenu (const char *pSpec, u64 ulHandler)
{
	unsigned i = 0;
	if (pSpec != 0)		// the spec lives in the caller's address space (active here)
	{
		for (; pSpec[i] != '\0' && i < sizeof (m_Menu) - 1; i++) m_Menu[i] = pSpec[i];
	}
	m_Menu[i] = '\0';
	m_ulMenuHandler = ulHandler;
	m_nMenuGen++;
}


// Blend (sw x sh) src pixels onto the screen at (dx,dy) with opacity nAlpha (1..254);
// bKey skips magenta pixels. Clipped to the screen.
static void BlendRect (GImage *pScreen, const u32 *pSrc, int nStride, int dx, int dy,
		       int sw, int sh, int nAlpha, boolean bKey)
{
	int W = pScreen->Width ();
	int cx0 = pScreen->ClipX0 (), cy0 = pScreen->ClipY0 (), cx1 = pScreen->ClipX1 (), cy1 = pScreen->ClipY1 ();
	u32 *pDst = pScreen->Buffer ();
	if (pDst == 0 || pSrc == 0) return;
	unsigned a = (unsigned) nAlpha, ia = 255 - a;
	for (int y = 0; y < sh; y++)
	{
		int ty = dy + y;
		if (ty < cy0 || ty >= cy1) continue;
		const u32 *s = pSrc + y * nStride;
		u32 *d = pDst + ty * W;
		for (int x = 0; x < sw; x++)
		{
			int tx = dx + x;
			if (tx < cx0 || tx >= cx1) continue;
			u32 c = s[x];
			if (bKey && (c & 0xFFFFFF) == 0xFF00FF) continue;
			u32 b = d[tx];
			u32 r  = (((c >> 16) & 0xFF) * a + ((b >> 16) & 0xFF) * ia) / 255;
			u32 g  = (((c >> 8)  & 0xFF) * a + ((b >> 8)  & 0xFF) * ia) / 255;
			u32 bl = (( c        & 0xFF) * a + ( b        & 0xFF) * ia) / 255;
			d[tx] = (r << 16) | (g << 8) | bl;
		}
	}
}

// Blend (sw x sh) pixels whose top byte is a transparency (0 = opaque, 255 = see-through) onto
// the screen at (dx,dy), times the window's opacity nAlpha (1..255). Clipped to the screen's clip
// rectangle. (WIN_FLAG_ALPHA canvases; the frames' rounded corners.)
static void BlendAlphaRect (GImage *pScreen, const u32 *pSrc, int nStride, int dx, int dy,
			    int sw, int sh, int nAlpha)
{
	u32 *pDst = pScreen->Buffer ();
	if (pDst == 0 || pSrc == 0) return;
	int W = pScreen->Width ();
	int x0 = dx > pScreen->ClipX0 () ? dx : pScreen->ClipX0 (), x1 = dx + sw < pScreen->ClipX1 () ? dx + sw : pScreen->ClipX1 ();
	int y0 = dy > pScreen->ClipY0 () ? dy : pScreen->ClipY0 (), y1 = dy + sh < pScreen->ClipY1 () ? dy + sh : pScreen->ClipY1 ();
	for (int ty = y0; ty < y1; ty++)
	{
		const u32 *s = pSrc + (ty - dy) * nStride + (x0 - dx);
		u32 *d = pDst + ty * W + x0;
		for (int n = x1 - x0; n > 0; n--, s++, d++)
		{
			u32 c = *s;
			unsigned a = 255 - (c >> 24);
			if (nAlpha < 255) a = a * (unsigned) nAlpha / 255;
			if (a == 0) continue;
			if (a == 255) { *d = c & 0x00FFFFFF; continue; }
			u32 b = *d;
			unsigned ia = 255 - a;
			u32 r  = (((c >> 16) & 0xFF) * a + ((b >> 16) & 0xFF) * ia) / 255;
			u32 g  = (((c >> 8)  & 0xFF) * a + ((b >> 8)  & 0xFF) * ia) / 255;
			u32 bl = (( c        & 0xFF) * a + ( b        & 0xFF) * ia) / 255;
			*d = (r << 16) | (g << 8) | bl;
		}
	}
}

// A frame's rounded corners: on the corner's row k (0 = the outer edge's), the first
// CornerSpan (k) pixels from the side may be see-through; the others are wholly inside the
// shape of radius KAPI_FRAME_RADIUS (their farthest point from the arc's centre within it), so
// the app draws them opaque.
static int CornerSpan (int k)
{
	static int s_Span[KAPI_FRAME_RADIUS];
	static volatile boolean s_bDone = FALSE;
	if (!s_bDone)
	{
		const int R = KAPI_FRAME_RADIUS;
		for (int j = 0; j < R; j++)
		{
			int i = 0;
			while (i < R && (R - i) * (R - i) + (R - j) * (R - j) > R * R) i++;
			s_Span[j] = i;
		}
		s_bDone = TRUE;
	}
	return k >= 0 && k < KAPI_FRAME_RADIUS ? s_Span[k] : 0;
}

boolean CWindow::CornersIn (int x0, int y0, int x1, int y1) const
{
	if (Borderless ()) return FALSE;
	int W = OuterWidth (), H = OuterHeight ();
	for (int k = 0; k < KAPI_FRAME_RADIUS && 2 * k < H; k++)
	{
		int n = CornerSpan (k), ya = m_nY + k, yb = m_nY + H - 1 - k;
		if (!((ya >= y0 && ya < y1) || (yb >= y0 && yb < y1))) continue;
		if ((m_nX < x1 && m_nX + n > x0) || (m_nX + W - n < x1 && m_nX + W > x0)) return TRUE;
	}
	return FALSE;
}

// Rows [r0, r1) of a frame's chrome copy (W x H) at (x0, y0 + r0): the corner squares blended
// by their pixels' transparency, the rest opaque.
static void ChromeRows (GImage *pScreen, const GImage *pChrome, int x0, int y0, int r0, int r1)
{
	const int R = KAPI_FRAME_RADIUS;
	int W = pChrome->Width (), H = pChrome->Height ();
	const u32 *pCopy = pChrome->Buffer ();
	for (int r = r0; r < r1; )
	{
		boolean bCorner = r < R || r >= H - R;
		int e = r < R ? R : r >= H - R ? H : H - R;		// this kind of row ends there
		if (e > r1) e = r1;
		if (bCorner && W > 2 * R)
		{
			BlendAlphaRect (pScreen, pCopy + r * W, W, x0, y0 + r, R, e - r, 255);
			pScreen->PutOtherPart (pChrome, x0 + R, y0 + r, R, r, W - 2 * R, e - r, FALSE);
			BlendAlphaRect (pScreen, pCopy + r * W + W - R, W, x0 + W - R, y0 + r, R, e - r, 255);
		}
		else pScreen->PutOtherPart (pChrome, x0, y0 + r, 0, r, W, e - r, FALSE);
		r = e;
	}
}

void CWindow::DrawTo (GImage *pScreen, boolean bActive)
{
	int nAlpha = m_nAlpha;
	if (nAlpha <= 0 || Hidden ())
	{
		return;					// fully faded out / minimised / off-desk: invisible
	}
	if (AlphaCanvas ())				// per-pixel transparency (the dock, the agenda)
	{
		BlendAlphaRect (pScreen, m_Canvas.Buffer (), m_Canvas.Width (), m_nX, m_nY,
				ClientWidth (), ClientHeight (), nAlpha);
		return;
	}
	if (nAlpha < 255)
	{
		// Translucent (a fade in / out): blend chrome + client over what is below (the
		// frame's corners by their own transparency too).
		if (!Borderless () && m_ulChromePhys[0] != 0)
		{
			BlendAlphaRect (pScreen, (const u32 *) m_ulChromePhys[bActive ? 0 : 1], m_nOuterW,
					m_nX, m_nY, m_nOuterW, m_nOuterH, nAlpha);
		}
		BlendRect (pScreen, m_Canvas.Buffer (), m_Canvas.Width (), m_nX + ChromeL (),
			   m_nY + ChromeT (), ClientWidth (), ClientHeight (), nAlpha, Transparent ());
		return;
	}

	int cw = ClientWidth ();		// logical size (may be < allocated canvas)
	int ch = ClientHeight ();

	// Outer top-left (chrome + client). A borderless window has zero chrome, so the
	// client area fills the whole window.
	int x0 = m_nX;
	int y0 = m_nY;

	// The frame is drawn USER-SIDE: the app renders its title bar / borders / buttons into two
	// copies (active + inactive); the compositor picks the one matching focus and blits its
	// bands round the client area (opaque but for the rounded corners' squares, blended by their
	// pixels' transparency). The kernel keeps the frame's BEHAVIOUR (drag, the title buttons).
	if (!Borderless () && m_ulChromePhys[0] != 0)
	{
		GImage Chrome ((u32 *) m_ulChromePhys[bActive ? 0 : 1], m_nOuterW, m_nOuterH);
		int T = ChromeT (), B = ChromeB (), H = m_nOuterH;
		if (Transparent () || T + ch + B > H)
		{
			ChromeRows (pScreen, &Chrome, x0, y0, 0, H);	// (a see-through client: all of it)
		}
		else
		{
			ChromeRows (pScreen, &Chrome, x0, y0, 0, T);			// the title bar
			ChromeRows (pScreen, &Chrome, x0, y0, T + ch, H);		// the bottom border
			int L = ChromeL (), Rt = m_nOuterW - L - cw;			// the sides
			for (int r = T; r < T + ch; )
			{
				int e = r < KAPI_FRAME_RADIUS ? KAPI_FRAME_RADIUS : r >= H - KAPI_FRAME_RADIUS ? T + ch : H - KAPI_FRAME_RADIUS;
				if (e > T + ch) e = T + ch;
				if (r < KAPI_FRAME_RADIUS || r >= H - KAPI_FRAME_RADIUS)	// (beside a corner)
				{
					BlendAlphaRect (pScreen, Chrome.Buffer () + r * m_nOuterW, m_nOuterW, x0, y0 + r, L, e - r, 255);
					BlendAlphaRect (pScreen, Chrome.Buffer () + r * m_nOuterW + L + cw, m_nOuterW,
							x0 + L + cw, y0 + r, Rt, e - r, 255);
				}
				else
				{
					pScreen->PutOtherPart (&Chrome, x0, y0 + r, 0, r, L, e - r, FALSE);
					pScreen->PutOtherPart (&Chrome, x0 + L + cw, y0 + r, L + cw, r, Rt, e - r, FALSE);
				}
				r = e;
			}
		}
	}

	// Client area = the owner's canvas, blitted opaque inside the chrome. Only the
	// logical sub-rect is shown (the canvas may be over-allocated for resizing).
	int clientX = x0 + ChromeL ();
	int clientY = y0 + ChromeT ();
	pScreen->PutOtherPart (&m_Canvas, clientX, clientY, 0, 0, cw, ch, Transparent ());

}

void CWindow::CloseBoxRect (int *px0, int *py0, int *px1, int *py1) const
{
	*px1 = m_nX + OuterWidth () - KAPI_FRAME_BTN_EDGE - 1;		// (the frame as it is now)
	*px0 = *px1 - KAPI_FRAME_BTN_W + 1;
	*py0 = m_nY + KAPI_FRAME_BTN_Y;
	*py1 = *py0 + KAPI_FRAME_BTN_H - 1;
}

boolean CWindow::HitCloseBox (int sx, int sy) const
{
	return HitTitleButton (sx, sy) == KAPI_FRAME_CLOSE;
}

// The title buttons (kapi_abi.h KAPI_FRAME_*): the window menu at the left; close, maximise,
// minimise from the right.
int CWindow::HitTitleButton (int sx, int sy) const
{
	if (Borderless () || Hidden () || Fixed ())		// (a fixed window has no buttons)
	{
		return -1;
	}
	int y = sy - (m_nY + KAPI_FRAME_BTN_Y);
	if (y < 0 || y >= KAPI_FRAME_BTN_H)
	{
		return -1;
	}
	int xl = sx - (m_nX + KAPI_FRAME_BTN_EDGE);
	if (xl >= 0 && xl < KAPI_FRAME_BTN_W)
	{
		return KAPI_FRAME_MENU;
	}
	int xr = (m_nX + OuterWidth () - KAPI_FRAME_BTN_EDGE) - 1 - sx;	// from the right, 0 = its last pixel
	if (xr < 0)
	{
		return -1;
	}
	static const int Order[3] = { KAPI_FRAME_CLOSE, KAPI_FRAME_MAXIMISE, KAPI_FRAME_MINIMISE };
	for (int i = 0; i < 3; i++)
	{
		int d = xr - i * KAPI_FRAME_BTN_STEP;
		if (d >= 0 && d < KAPI_FRAME_BTN_W)
		{
			return Order[i];
		}
	}
	return -1;
}

boolean CWindow::OpaqueAt (int sx, int sy) const
{
	int x = sx - m_nX, y = sy - m_nY;
	if (x < 0 || y < 0 || x >= ClientWidth () || y >= ClientHeight () || m_Canvas.Buffer () == 0)
	{
		return FALSE;
	}
	return (m_Canvas.Buffer ()[y * m_Canvas.Width () + x] >> 24) != 0xFF;
}

boolean CWindow::Grow (int w, int h)
{
	int cw = m_Canvas.Width (), ch = m_Canvas.Height ();
	if (w <= cw && h <= ch)
	{
		return TRUE;					// it fits already
	}
	if (w < cw) w = cw;
	if (h < ch) h = ch;
	if (m_pRetired[0] != 0 || m_pRetired[1] != 0 || m_pRetired[2] != 0)
	{
		return FALSE;					// (the last growth not freed yet)
	}
	unsigned nBytes = (unsigned) (w * h) * sizeof (u32);
	unsigned nPages = (nBytes + KPAGE_MASK) / KPAGE_SIZE;
	void *pRaw = WinPixelsAlloc (0, nPages * KPAGE_SIZE + KPAGE_SIZE);
	if (pRaw == 0)
	{
		return FALSE;
	}
	void *pChRaw[2] = { 0, 0 }; u64 ulChPhys[2] = { 0, 0 }; unsigned nChPages[2] = { 0, 0 };
	if (!Borderless ()
	    && !AllocChrome (w + 2 * WIN_BORDER, h + WIN_TITLEBAR_H + WIN_BORDER, pChRaw, ulChPhys, nChPages))
	{
		if (pChRaw[1] != 0) WinPixelsFree (pChRaw[1]);
		if (pChRaw[0] != 0) WinPixelsFree (pChRaw[0]);
		WinPixelsFree (pRaw);
		return FALSE;
	}
	uintptr ulAligned = ((uintptr) pRaw + KPAGE_MASK) & ~((uintptr) KPAGE_MASK);
	Damage ();
	// the old memory is retired (the compositor may be blitting it right now), the new one in
	m_pRetired[0] = m_pRawAlloc;
	m_pRawAlloc = pRaw; m_ulCanvasPhys = ulAligned; m_nCanvasPages = nPages;
	m_Canvas.Wrap ((u32 *) ulAligned, w, h);
	if (!Borderless ())
	{
		m_pRetired[1] = m_pChromeRaw[0]; m_pRetired[2] = m_pChromeRaw[1];
		for (int i = 0; i < 2; i++)
		{
			m_pChromeRaw[i] = pChRaw[i]; m_ulChromePhys[i] = ulChPhys[i]; m_nChromePages[i] = nChPages[i];
		}
	}
	m_nRetireFrame = CWindowManager::Get () != 0 ? CWindowManager::Get ()->FrameCount () : 0;
	m_nChromeGen++;
	return TRUE;
}

void CWindow::DropChrome (void)
{
	if (Borderless ())
	{
		return;
	}
	Damage ();
	m_nFlags |= WIN_FLAG_BORDERLESS;
	for (int i = 0; i < 2; i++)
	{
		if (m_pChromeRaw[i] != 0) WinPixelsFree (m_pChromeRaw[i]);	// (one thread composes: none reads it now)
		m_pChromeRaw[i] = 0; m_ulChromePhys[i] = 0; m_nChromePages[i] = 0;
	}
	m_nOuterW = m_nLogicalW; m_nOuterH = m_nLogicalH;
	m_nChromeGen++;
	Damage ();
}

void CWindow::FreeRetired (void)
{
	if (m_pRetired[0] == 0 && m_pRetired[1] == 0 && m_pRetired[2] == 0)
	{
		return;
	}
	CWindowManager *pWM = CWindowManager::Get ();
	if (pWM != 0 && pWM->FrameCount () - m_nRetireFrame < 3)
	{
		return;						// (a frame begun before may still read it)
	}
	for (int i = 0; i < 3; i++)
	{
		if (m_pRetired[i] != 0) { WinPixelsFree (m_pRetired[i]); m_pRetired[i] = 0; }
	}
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
	else
	{
		m_nEvDropped++;
	}
	m_EvLock.Release ();
	if (m_pWake != 0) { m_pWake->Set (); m_pWake->Clear (); }	// (wakes the owner's kapi_pump_wait)
}

void CWindow::RequestExit (void)
{
	m_bExitRequested = TRUE;
	if (m_pWake != 0) { m_pWake->Set (); m_pWake->Clear (); }
}

boolean CWindow::PopEvent (GUIEvent *pEvent)
{
	boolean bGot = FALSE;
	m_nLastPump = CTimer::Get ()->GetTicks ();	// the owner is alive and pumping
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
volatile unsigned g_nScreenGen = 1;

// The damage list: up to SCREEN_DAMAGE_MAX rectangles, merged when they overlap or touch;
// when it is full, or covers most of the screen, it becomes "the whole screen".
static CSpinLock s_DamageLock;
static TScreenDamage s_Damage = { TRUE, 0, {}, {}, {}, {} };

void ScreenDirty (void)
{
	s_DamageLock.Acquire ();
	s_Damage.bFull = TRUE; s_Damage.n = 0;
	s_DamageLock.Release ();
	g_nScreenGen++;
}

void ScreenDirtyRect (int x, int y, int w, int h)
{
	int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
	int x1 = x + w > g_nScreenWidth ? g_nScreenWidth : x + w, y1 = y + h > g_nScreenHeight ? g_nScreenHeight : y + h;
	if (x1 <= x0 || y1 <= y0) return;
	s_DamageLock.Acquire ();
	TScreenDamage &D = s_Damage;
	if (!D.bFull)
	{
		// merge with every rectangle it overlaps or touches (repeat: the union may reach more)
		for (boolean bMerged = TRUE; bMerged; )
		{
			bMerged = FALSE;
			for (int i = 0; i < D.n; i++)
				if (x0 <= D.x1[i] && D.x0[i] <= x1 && y0 <= D.y1[i] && D.y0[i] <= y1)
				{
					if (D.x0[i] < x0) x0 = D.x0[i];
					if (D.y0[i] < y0) y0 = D.y0[i];
					if (D.x1[i] > x1) x1 = D.x1[i];
					if (D.y1[i] > y1) y1 = D.y1[i];
					D.n--;
					D.x0[i] = D.x0[D.n]; D.y0[i] = D.y0[D.n]; D.x1[i] = D.x1[D.n]; D.y1[i] = D.y1[D.n];
					bMerged = TRUE;
					break;
				}
		}
		if (D.n == SCREEN_DAMAGE_MAX) { D.bFull = TRUE; D.n = 0; }
		else
		{
			D.x0[D.n] = x0; D.y0[D.n] = y0; D.x1[D.n] = x1; D.y1[D.n] = y1; D.n++;
			long nArea = 0;
			for (int i = 0; i < D.n; i++) nArea += (long) (D.x1[i] - D.x0[i]) * (D.y1[i] - D.y0[i]);
			if (nArea * 3 > (long) g_nScreenWidth * g_nScreenHeight * 2) { D.bFull = TRUE; D.n = 0; }	// (> 2/3: all)
		}
	}
	s_DamageLock.Release ();
	g_nScreenGen++;
}

void ScreenTakeDamage (TScreenDamage *pOut)
{
	s_DamageLock.Acquire ();
	*pOut = s_Damage;
	s_Damage.bFull = FALSE; s_Damage.n = 0;
	s_DamageLock.Release ();
}

// Title-bar text colour (overridable at boot from SD:/etc/theme.txt).
u32 g_WinTitleTextColor = 0x00FFFFFF;

CWindowManager::CWindowManager (void)
:	m_nWindows (0), m_pWallpaper (0), m_pCursor (0),
	m_pWallRaw (0), m_ulWallPhys (0), m_nWallPages (0), m_bLiveWall (FALSE), m_nWallGen (0),
	m_nShape (0),
	m_nCursorX (SCREEN_WIDTH / 2), m_nCursorY (SCREEN_HEIGHT / 2),
	m_nPrevX (0), m_nPrevY (0),
	m_bCursorShown (FALSE), m_nLastButtons (0),
	m_pDragWindow (0), m_nDragDX (0), m_nDragDY (0),
	m_pTitleClick (0), m_nTitleClickTicks (0), m_pBtnDown (0), m_nBtnDown (-1),
	m_pPtrOverWindow (0), m_pPtrCaptureWindow (0), m_nWheelSpeed (2),
	m_nFrames (0), m_nMouseEvents (0), m_nKeyEvents (0),
	m_pFullscreen (0), m_pFsRaw (0), m_ulFsPhys (0), m_nFsPages (0),
	m_pMenuLast (0), m_nMenuLastGen (0), m_nMenuSerial (1),
	m_bDnd (FALSE), m_pDndSrc (0), m_pDndOver (0), m_nModifiers (0),
	m_nDesk (0), m_nDesks (4), m_nDeskGen (0)
{
	for (unsigned i = 0; i < WM_CURSOR_SHAPES; i++) { m_pShape[i] = 0; m_nShapeHotX[i] = m_nShapeHotY[i] = 0; }
	m_pSizeWindow = 0; m_nSizeEdge = 0;
	m_DndLabel[0] = '\0';
	for (unsigned i = 0; i < HELD_WORDS; i++) m_UsbHeld[i] = m_VncHeld[i] = 0;
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
	// (v65) a new window opens on the current desk; the shell's parts are on every one
	pWindow->SetDesk (pWindow->Topmost () || pWindow->Backmost () || pWindow->System () ? -1 : m_nDesk);
	pWindow->SetOffDesk (FALSE);
	m_SpinLock.Release ();
	pWindow->Damage ();
	m_SpinLock.Acquire ();
	if (m_nWindows < WM_MAX_WINDOWS)
	{
		if (pWindow->Backmost ())
		{
			// Keep backmost windows (the shell desktop) at the BOTTOM of the z-order.
			for (unsigned j = m_nWindows; j > 0; j--) m_pWindows[j] = m_pWindows[j - 1];
			m_pWindows[0] = pWindow;
			m_nWindows++;
		}
		else if (pWindow->Topmost ())
		{
			m_pWindows[m_nWindows++] = pWindow;	// the menu bar: above everything
		}
		else
		{
			// Normal windows open on top of the normal band, i.e. below any
			// topmost window (the menu bar stays above them).
			unsigned k = m_nWindows;
			while (k > 0 && m_pWindows[k - 1]->Topmost ()) k--;
			for (unsigned j = m_nWindows; j > k; j--) m_pWindows[j] = m_pWindows[j - 1];
			m_pWindows[k] = pWindow;
			m_nWindows++;
		}
	}
	m_SpinLock.Release ();
}

void CWindowManager::Remove (CWindow *pWindow)
{
	if (pWindow != 0) pWindow->Damage ();
	m_SpinLock.Acquire ();
	// Drop any references into this window (it may be freed right after).
	if (m_pDragWindow == pWindow)		{ m_pDragWindow = 0; }
	if (m_pSizeWindow == pWindow)		{ m_pSizeWindow = 0; ScreenDirty (); }
	if (m_pPtrOverWindow == pWindow)	{ m_pPtrOverWindow = 0; }
	if (m_pPtrCaptureWindow == pWindow)	{ m_pPtrCaptureWindow = 0; }
	if (m_pFullscreen == pWindow)		{ m_pFullscreen = 0; }	// its app quit: desktop back
	if (m_pDndOver == pWindow)		{ m_pDndOver = 0; }
	if (m_pTitleClick == pWindow)		{ m_pTitleClick = 0; }
	if (m_pBtnDown == pWindow)		{ m_pBtnDown = 0; }
	if (m_bDnd && m_pDndSrc == pWindow)	{ m_bDnd = FALSE; m_pDndSrc = 0; }	// source gone
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
	if (pWindow != 0 && pWindow->OffDesk ()) SetDeskLocked (pWindow->Desk ());	// (its desk shown)
	if (pWindow != 0 && pWindow->Minimised ()) pWindow->SetMinimised (FALSE);	// (back from the dock)
	if (pWindow != 0) pWindow->Damage ();
	if (pWindow != 0 && pWindow->Backmost ())
	{
		return;				// the shell desktop never rises above other windows
	}
	for (unsigned i = 0; i < m_nWindows; i++)
	{
		if (m_pWindows[i] == pWindow)
		{
			for (unsigned j = i; j + 1 < m_nWindows; j++)
			{
				m_pWindows[j] = m_pWindows[j + 1];
			}
			// Top of its band: the very top for a topmost window, else just below
			// the topmost ones (the menu bar).
			unsigned k = m_nWindows - 1;
			if (!pWindow->Topmost ())
			{
				while (k > 0 && m_pWindows[k - 1]->Topmost ()) k--;
				for (unsigned j = m_nWindows - 1; j > k; j--) m_pWindows[j] = m_pWindows[j - 1];
			}
			m_pWindows[k] = pWindow;		// drawn after everything below it
			break;
		}
	}
}

CWindow *CWindowManager::ActiveLocked (void)
{
	for (int i = (int) m_nWindows - 1; i >= 0; i--)
	{
		CWindow *p = m_pWindows[i];
		if (p != 0 && !p->Topmost () && !p->Backmost () && !p->Borderless () && !p->Hidden ())
		{
			return p;
		}
	}
	return 0;
}

void CWindowManager::SetFullscreen (CWindow *pWindow)
{
	ScreenDirty ();
	m_SpinLock.Acquire ();
	m_pFullscreen = pWindow;
	m_pPtrCaptureWindow = 0;
	m_pPtrOverWindow = 0;
	m_pDragWindow = 0;
	m_SpinLock.Release ();
}

u32 *CWindowManager::EnsureFullscreenBuffer (int nW, int nH, u64 *pPhys, unsigned *pnPages)
{
	unsigned nBytes = (unsigned) (nW * nH) * sizeof (u32);
	unsigned nNeed = (nBytes + KPAGE_MASK) / KPAGE_SIZE;
	if (nNeed == 0) nNeed = 1;
	if (m_pFsRaw != 0 && nNeed > m_nFsPages)
	{
		// The screen grew since the buffer was made (kapi_screen_set): a new one of the new
		// size, else fullscreen_begin's clear, the app and present_fb run past its end (the
		// kernel heap overwritten). The old one is left allocated, as the wallpaper's: an app
		// may still have it mapped.
		m_pFsRaw = 0; m_ulFsPhys = 0; m_nFsPages = 0;
	}
	if (m_pFsRaw == 0)
	{
		m_nFsPages = nNeed;
		m_pFsRaw = new u8[m_nFsPages * KPAGE_SIZE + KPAGE_SIZE];
		if (m_pFsRaw == 0) { m_nFsPages = 0; return 0; }
		m_ulFsPhys = ((uintptr) m_pFsRaw + KPAGE_MASK) & ~((uintptr) KPAGE_MASK);
		memset ((void *) m_ulFsPhys, 0, m_nFsPages * KPAGE_SIZE);
	}
	if (pPhys)   *pPhys   = m_ulFsPhys;
	if (pnPages) *pnPages = m_nFsPages;
	return (u32 *) m_ulFsPhys;
}

CWindow *CWindowManager::KeyTargetLocked (void)
{
	if (m_pFullscreen != 0)
	{
		return m_pFullscreen;			// a full-screen app gets every key
	}
	for (int i = (int) m_nWindows - 1; i >= 0; i--)
	{
		if (m_pWindows[i] != 0 && !m_pWindows[i]->Topmost () && !m_pWindows[i]->Hidden ())
		{
			return m_pWindows[i];
		}
	}
	return 0;
}

int CWindowManager::TopInsetLocked (void)
{
	int nInset = 0;
	for (unsigned i = 0; i < m_nWindows; i++)
	{
		CWindow *p = m_pWindows[i];
		if (p != 0 && p->Topmost () && !p->NoInset () && p->Y () == 0 && p->MinLogicalHeight () > nInset)
		{
			nInset = p->MinLogicalHeight ();
		}
	}
	return nInset;
}

int CWindowManager::TopInset (void)
{
	m_SpinLock.Acquire ();
	int n = TopInsetLocked ();
	m_SpinLock.Release ();
	return n;
}

// The height kept by the topmost windows standing on the screen's bottom edge (the dock: its
// bar -- the smallest it has been; its drawers grow it upward for a while).
int CWindowManager::BottomInsetLocked (void)
{
	int nInset = 0;
	for (unsigned i = 0; i < m_nWindows; i++)
	{
		CWindow *p = m_pWindows[i];
		if (p != 0 && p->Topmost () && !p->NoInset () && p->Y () > 0 && !p->Hidden ()
		    && p->Y () + p->OuterHeight () >= g_nScreenHeight && p->MinLogicalHeight () > nInset)
		{
			nInset = p->MinLogicalHeight ();
		}
	}
	return nInset;
}

void CWindowManager::WorkArea (int *px, int *py, int *pw, int *ph)
{
	m_SpinLock.Acquire ();
	int t = TopInsetLocked (), b = BottomInsetLocked ();
	m_SpinLock.Release ();
	if (t + b > g_nScreenHeight / 2) b = 0;
	*px = 0; *py = t; *pw = g_nScreenWidth; *ph = g_nScreenHeight - t - b;
}

// Caller holds m_SpinLock.
void CWindowManager::MinimiseLocked (CWindow *pWindow)
{
	if (pWindow == 0 || pWindow->Topmost () || pWindow->Backmost () || pWindow->Minimised ())
	{
		return;
	}
	if (m_pPtrOverWindow == pWindow)
	{
		EmitPointer (pWindow, GUI_EVENT_PTR_LEAVE, -1, -1, 0, 0);
		m_pPtrOverWindow = 0;
	}
	if (m_pPtrCaptureWindow == pWindow)	{ m_pPtrCaptureWindow = 0; }
	if (m_pDragWindow == pWindow)		{ m_pDragWindow = 0; }
	if (m_pSizeWindow == pWindow)		{ m_pSizeWindow = 0; ScreenDirty (); }
	if (m_pFullscreen == pWindow)		{ return; }
	pWindow->SetMinimised (TRUE);
}

void CWindowManager::SetAside (CWindow *pWindow, boolean bOn)
{
	if (pWindow == 0) return;
	m_SpinLock.Acquire ();
	pWindow->SetAside (bOn);
	if (bOn) ForgetHiddenLocked ();
	m_SpinLock.Release ();
}

void CWindowManager::Minimise (CWindow *pWindow)
{
	m_SpinLock.Acquire ();
	MinimiseLocked (pWindow);
	m_SpinLock.Release ();
}

// ---- workspaces (v65) ------------------------------------------------------------------------
// Caller holds m_SpinLock: the pointer's state no longer names a window that went hidden.
void CWindowManager::ForgetHiddenLocked (void)
{
	if (m_pPtrOverWindow != 0 && m_pPtrOverWindow->Hidden ())
	{
		EmitPointer (m_pPtrOverWindow, GUI_EVENT_PTR_LEAVE, -1, -1, 0, 0);
		m_pPtrOverWindow = 0;
	}
	if (m_pPtrCaptureWindow != 0 && m_pPtrCaptureWindow->Hidden ())	{ m_pPtrCaptureWindow = 0; }
	if (m_pSizeWindow != 0 && m_pSizeWindow->Hidden ())		{ m_pSizeWindow = 0; ScreenDirty (); }
	if (m_pDragWindow != 0 && m_pDragWindow->Hidden ())		{ m_pDragWindow = 0; }
	if (m_pBtnDown != 0 && m_pBtnDown->Hidden ())			{ m_pBtnDown = 0; }
	if (m_pTitleClick != 0 && m_pTitleClick->Hidden ())		{ m_pTitleClick = 0; }
}

// Caller holds m_SpinLock.
void CWindowManager::SetDeskLocked (int n)
{
	if (n < 0 || n >= m_nDesks)
	{
		return;
	}
	if (n != m_nDesk)
	{
		m_nDesk = n;
		m_nDeskGen++;
	}
	for (unsigned i = 0; i < m_nWindows; i++)
	{
		CWindow *p = m_pWindows[i];
		if (p != 0) p->SetOffDesk (p->Desk () >= 0 && p->Desk () != n);
	}
	ForgetHiddenLocked ();
}

int CWindowManager::SetDesk (int n, int nCount)
{
	m_SpinLock.Acquire ();
	if (nCount > 0)
	{
		if (nCount > KAPI_DESK_MAX) nCount = KAPI_DESK_MAX;
		if (nCount != m_nDesks)
		{
			m_nDesks = nCount;
			m_nDeskGen++;
			for (unsigned i = 0; i < m_nWindows; i++)		// (the desks dropped: onto the last)
				if (m_pWindows[i] != 0 && m_pWindows[i]->Desk () >= nCount) m_pWindows[i]->SetDesk (nCount - 1);
			if (m_nDesk >= nCount) m_nDesk = nCount - 1;
		}
	}
	unsigned nGen = m_nDeskGen;
	SetDeskLocked (n >= 0 ? n : m_nDesk);
	boolean bChanged = nGen != m_nDeskGen || nCount > 0;
	int nInfo = m_nDesk | (m_nDesks << 8) | (int) ((m_nDeskGen & 0x7FFF) << 16);
	m_SpinLock.Release ();
	if (bChanged) ScreenDirty ();
	return nInfo;
}

int CWindowManager::DeskInfo (void)
{
	return m_nDesk | (m_nDesks << 8) | (int) ((m_nDeskGen & 0x7FFF) << 16);
}

int CWindowManager::MoveToDesk (CWindow *pWindow, int n)
{
	if (pWindow == 0) return -3;
	m_SpinLock.Acquire ();
	if (n >= m_nDesks) n = m_nDesks - 1;
	if (n >= -1 && n != pWindow->Desk () && !pWindow->Topmost () && !pWindow->Backmost ())
	{
		pWindow->SetDesk (n);
		pWindow->SetOffDesk (n >= 0 && n != m_nDesk);
		ForgetHiddenLocked ();
		m_nDeskGen++;
	}
	int nDesk = pWindow->Desk ();
	m_SpinLock.Release ();
	return nDesk;
}

unsigned CWindowManager::GetActiveMenu (char *pBuf, unsigned nCap, char *pTitle, unsigned nTitleCap, CWindow *pFor)
{
	m_SpinLock.Acquire ();
	CWindow *p = pFor != 0 ? pFor : ActiveLocked ();
	unsigned nSerial = 0;
	if (p != 0)
	{
		if (p != m_pMenuLast || p->MenuGen () != m_nMenuLastGen)
		{
			m_pMenuLast = p;
			m_nMenuLastGen = p->MenuGen ();
			m_nMenuSerial++;
		}
		nSerial = m_nMenuSerial;
		unsigned i = 0;
		if (pBuf != 0 && nCap > 0)
		{
			for (const char *s = p->Menu (); s[i] != '\0' && i < nCap - 1; i++) pBuf[i] = s[i];
			pBuf[i] = '\0';
		}
		i = 0;
		if (pTitle != 0 && nTitleCap > 0)
		{
			for (const char *s = p->Title (); s[i] != '\0' && i < nTitleCap - 1; i++) pTitle[i] = s[i];
			pTitle[i] = '\0';
		}
	}
	else
	{
		m_pMenuLast = 0;
		if (pBuf != 0 && nCap > 0) pBuf[0] = '\0';
		if (pTitle != 0 && nTitleCap > 0) pTitle[0] = '\0';
	}
	m_SpinLock.Release ();
	return nSerial;
}

boolean CWindowManager::SendMenuCommand (int nID, CWindow *pFor)
{
	boolean bOK = FALSE;
	m_SpinLock.Acquire ();
	CWindow *p = pFor != 0 ? pFor : ActiveLocked ();
	if (p != 0)
	{
		if (nID == -1)
		{
			p->RequestExit ();			// "Quit": like its close box
			bOK = TRUE;
		}
		else if (p->MenuHandler () != 0)
		{
			GUIEvent Ev;
			Ev.ulHandler = p->MenuHandler ();
			Ev.ulSender  = 0;
			Ev.nEvent    = GUI_EVENT_MENU;
			Ev.lValue    = nID;
			p->PushEvent (Ev);
			bOK = TRUE;
		}
	}
	m_SpinLock.Release ();
	return bOK;
}

void CWindowManager::Raise (CWindow *pWindow)
{
	m_SpinLock.Acquire ();
	RaiseLocked (pWindow);
	m_SpinLock.Release ();
}

void CWindowManager::Composite (GImage *pScreen, boolean bCountFrame)
{
	// Snapshot the window list under the lock, then blit without holding it (so we
	// don't keep IRQ masked for the whole frame). Note: a window pointer in the
	// snapshot must stay valid while we blit -- process teardown removes a window
	// from the list but does NOT free the CWindow (see CAddressSpace::~), so the
	// memory remains valid here. (Proper deferred free is future work.)
	CWindow *pSnapshot[WM_MAX_WINDOWS];
	unsigned nCount;
	GImage  *pWall;
	CWindow *pActive;

	m_SpinLock.Acquire ();
	pActive = KeyTargetLocked ();			// the focused window (active chrome)
	nCount = m_nWindows;
	for (unsigned i = 0; i < nCount; i++)
	{
		pSnapshot[i] = m_pWindows[i];
		if (bCountFrame && pSnapshot[i] != 0) pSnapshot[i]->FreeRetired ();	// (a grown window's old memory)
	}
	// A committed app-written wallpaper (m_WallImage) takes priority over the
	// kernel-set one (m_pWallpaper).
	pWall = (m_bLiveWall && m_WallImage.IsValid ()) ? &m_WallImage : m_pWallpaper;
	boolean bDnd = m_bDnd;
	char DndLabel[DND_LABEL_MAX + 2];
	if (bDnd)
	{
		unsigned i = 0;
		if (m_nModifiers & MOD_CTRL) { DndLabel[i++] = '+'; }		// copy
		for (unsigned j = 0; m_DndLabel[j] != '\0' && i < DND_LABEL_MAX; j++) DndLabel[i++] = m_DndLabel[j];
		DndLabel[i] = '\0';
	}
	m_SpinLock.Release ();

	// The topmost window that paints the whole clip rectangle opaquely: nothing below it
	// (desktop, wallpaper, lower windows) needs drawing -- a window refreshing alone (a
	// game, an emulator) costs its own pixels once.
	unsigned nFirst = 0;
	for (unsigned i = nCount; i-- > 0; )
		if (pSnapshot[i] != 0 && pSnapshot[i]->CoversOpaque (pScreen->ClipX0 (), pScreen->ClipY0 (),
								   pScreen->ClipX1 (), pScreen->ClipY1 ()))
		{
			nFirst = i;
			break;
		}

	// Desktop background: the wallpaper if set (filled behind it for any margin),
	// otherwise the solid desktop colour.
	if (nFirst == 0 && !(nCount > 0 && pSnapshot[0] != 0
			     && pSnapshot[0]->CoversOpaque (pScreen->ClipX0 (), pScreen->ClipY0 (), pScreen->ClipX1 (), pScreen->ClipY1 ())))
	{
		pScreen->Clear (WIN_COLOR_DESKTOP);
		if (pWall != 0 && pWall->IsValid ())
		{
			pScreen->PutOther (pWall, 0, 0, FALSE);
		}
	}

	// Draw back-to-front; the last (topmost) window is the active one.
	for (unsigned i = nFirst; i < nCount; i++)
	{
		if (pSnapshot[i] != 0)
		{
			pSnapshot[i]->DrawTo (pScreen, pSnapshot[i] == pActive);
		}
	}

	// Drag & drop badge: the dragged item's label, just below-right of the cursor.
	if (bDnd && m_bCursorShown)
	{
		int bw = GImage::TextWidth (DndLabel) + 12, bh = GImage::FontHeight () + 6;
		int bx = m_nCursorX + 16, by = m_nCursorY + 18;
		pScreen->FillRectangle (bx + 2, by + 2, bx + bw + 1, by + bh + 1, 0x00101418);	// shadow
		pScreen->FillRectangle (bx, by, bx + bw - 1, by + bh - 1, 0x00303D4D);
		pScreen->DrawRectangle (bx, by, bx + bw - 1, by + bh - 1, 0x0090C0FF);
		pScreen->DrawText (bx + 6, by + 3, DndLabel, 0x00FFFFFF);
	}

	// (v82) A window being resized: the outline of its frame to be (white between two black lines).
	if (m_pSizeWindow != 0)
	{
		int x0 = m_nSizeX, y0 = m_nSizeY, x1 = m_nSizeX + m_nSizeW - 1, y1 = m_nSizeY + m_nSizeH - 1;
		pScreen->DrawRectangle (x0, y0, x1, y1, 0x00000000);
		pScreen->DrawRectangle (x0 + 1, y0 + 1, x1 - 1, y1 - 1, 0x00FFFFFF);
		pScreen->DrawRectangle (x0 + 2, y0 + 2, x1 - 2, y1 - 2, 0x00000000);
	}

	// Cursor, drawn last so it floats above everything. Prefer the cursor
	// image (the kernel's built-in arrow); its hot-spot is the top-left corner. Without it, fall
	// back to a drawn black-bordered white arrow whose tip is at (cx,cy).
	if (m_bCursorShown)
	{
		int cx = m_nCursorX;
		int cy = m_nCursorY;
		GImage *pShape = m_nShape < WM_CURSOR_SHAPES ? m_pShape[m_nShape] : 0;
		if (pShape != 0 && pShape->IsValid ())		// (v81: a hand, the text bar, ...)
		{
			pScreen->PutOther (pShape, cx - m_nShapeHotX[m_nShape], cy - m_nShapeHotY[m_nShape], TRUE);
		}
		else if (m_pCursor != 0 && m_pCursor->IsValid ())
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

	if (bCountFrame)
	{
		m_nFrames++;
	}
}

void CWindowManager::CompositeDesktop (GImage *pScreen)
{
	CWindow *pSnapshot[WM_MAX_WINDOWS];
	m_SpinLock.Acquire ();
	unsigned nCount = m_nWindows;
	for (unsigned i = 0; i < nCount; i++) pSnapshot[i] = m_pWindows[i];
	GImage *pWall = (m_bLiveWall && m_WallImage.IsValid ()) ? &m_WallImage : m_pWallpaper;
	m_SpinLock.Release ();
	pScreen->Clear (WIN_COLOR_DESKTOP);
	if (pWall != 0 && pWall->IsValid ()) pScreen->PutOther (pWall, 0, 0, FALSE);
	for (unsigned i = 0; i < nCount; i++)
		if (pSnapshot[i] != 0 && pSnapshot[i]->Backmost ()) pSnapshot[i]->DrawTo (pScreen, FALSE);
}

unsigned CWindowManager::DesktopGen (void)
{
	m_SpinLock.Acquire ();
	unsigned g = m_nWallGen;
	for (unsigned i = 0; i < m_nWindows; i++) if (m_pWindows[i]->Backmost ()) g += m_pWindows[i]->Gen ();
	m_SpinLock.Release ();
	return g;
}

unsigned CWindowManager::Snapshot (CWindow **ppOut, unsigned nMax)
{
	m_SpinLock.Acquire ();
	unsigned n = m_nWindows < nMax ? m_nWindows : nMax;
	for (unsigned i = 0; i < n; i++)
	{
		ppOut[i] = m_pWindows[i];
	}
	m_SpinLock.Release ();
	return n;
}

void CWindowManager::SetWallpaper (GImage *pImage)
{
	m_nWallGen++;
	ScreenDirty ();
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
	if (m_pWallRaw != 0 && (m_WallImage.Width () != nW || m_WallImage.Height () != nH))
	{
		// The screen's size changed (kapi_screen_set): a new buffer of the new size. The old
		// one is left allocated -- an app may still have it mapped (it writes there, not over
		// memory given back to the heap); a few MB at each change of the resolution.
		m_bLiveWall = FALSE;
		m_pWallRaw = 0; m_ulWallPhys = 0; m_nWallPages = 0;
	}
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
	ScreenDirty ();
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
		if (pWin == 0 || pWin->Hidden () || pWin->Alpha () <= 0)
		{
			continue;
		}
		int x0 = pWin->X ();
		int y0 = pWin->Y ();
		int x1 = x0 + pWin->ClientWidth () + pWin->ChromeL () + pWin->ChromeR () - 1;
		int y1 = y0 + pWin->ChromeT () + pWin->ClientHeight () + pWin->ChromeB () - 1;
		if (x >= x0 && x <= x1 && y >= y0 && y <= y1)
		{
			if (pWin->AlphaCanvas () && !pWin->OpaqueAt (x, y))
			{
				continue;			// a see-through pixel: what lies below
			}
			// Borderless windows have no title bar (ChromeT == 0), so a hit is
			// always in the client area -- never a drag region.
			*pbOnTitleBar = !pWin->Borderless () && (y < y0 + pWin->ChromeT ());
			return (unsigned) i;
		}
	}
	return ~0u;
}

// ---- drag & drop (ABI v42) -------------------------------------------------------------
// Push a drag & drop event to a window's pointer handler: lValue = (flags << 32) |
// (x << 16) | y (client coords, clamped non-negative) -- or, for DRAG_DONE, the caller's
// raw value in lRaw.
static void EmitDnd (CWindow *pWin, int nEvent, int cx, int cy, unsigned nFlags, long lRaw = -1)
{
	if (pWin == 0 || pWin->PointerHandler () == 0) return;
	if (cx < 0) cx = 0; else if (cx > 0xFFFF) cx = 0xFFFF;
	if (cy < 0) cy = 0; else if (cy > 0xFFFF) cy = 0xFFFF;
	GUIEvent Ev;
	Ev.ulHandler = pWin->PointerHandler ();
	Ev.ulSender  = 0;
	Ev.nEvent    = nEvent;
	Ev.lValue    = lRaw >= 0 ? lRaw : ((long) nFlags << 32) | ((long) cx << 16) | (long) cy;
	pWin->PushEvent (Ev);
}

// The screen's size changed (see window.h). Every window stays on the screen (moved in, not
// resized: its app does that, on GUI_EVENT_DISPLAY_RESIZE -- uikit re-maximises a maximised
// window, shrinks one too big for the work area); the cursor too.
void CWindowManager::OnScreenResized (int nW, int nH)
{
	CWindow *pSnapshot[WM_MAX_WINDOWS];
	m_SpinLock.Acquire ();
	unsigned nCount = m_nWindows;
	for (unsigned i = 0; i < nCount; i++) pSnapshot[i] = m_pWindows[i];
	if (m_nCursorX >= nW) m_nCursorX = nW - 1;
	if (m_nCursorY >= nH) m_nCursorY = nH - 1;
	m_bLiveWall = FALSE;				// (the old size: the wallpaper's app paints it again)
	m_nWallGen++;
	m_SpinLock.Release ();
	for (unsigned i = 0; i < nCount; i++)
	{
		CWindow *p = pSnapshot[i];
		if (p == 0 || p->Backmost ()) continue;
		if (p->Fixed ())				// (centred again)
		{
			int cx = (nW - p->OuterWidth ()) / 2, cy = (nH - p->OuterHeight ()) / 2;
			p->Move (cx < 0 ? 0 : cx, cy < 0 ? 0 : cy);
			continue;
		}
		int x = p->X (), y = p->Y ();		// (past the right / bottom edge: moved in; a
		if (x > 0 && x + p->OuterWidth () > nW)		// window parked off the screen, at a
		{						// negative place, is left there)
			x = nW - p->OuterWidth (); if (x < 0) x = 0;
		}
		if (y > 0 && y + p->OuterHeight () > nH)
		{
			y = nH - p->OuterHeight (); if (y < 0) y = 0;
		}
		if (x != p->X () || y != p->Y ()) p->Move (x, y);
	}
	for (unsigned i = 0; i < nCount; i++)
	{
		CWindow *p = pSnapshot[i];
		if (p == 0 || p->PointerHandler () == 0) continue;
		GUIEvent Ev;
		Ev.ulHandler = p->PointerHandler ();
		Ev.ulSender  = 0;
		Ev.nEvent    = GUI_EVENT_DISPLAY_RESIZE;
		Ev.lValue    = ((long) nW << 16) | (long) nH;
		p->PushEvent (Ev);
	}
	ScreenDirty ();
}

// A title button for the app (GUI_EVENT_WINCTL, v64): the window menu, maximise.
static void EmitWinCtl (CWindow *pWin, int nWhat)
{
	if (pWin == 0 || pWin->PointerHandler () == 0) return;
	GUIEvent Ev;
	Ev.ulHandler = pWin->PointerHandler ();
	Ev.ulSender  = 0;
	Ev.nEvent    = GUI_EVENT_WINCTL;
	Ev.lValue    = nWhat;
	pWin->PushEvent (Ev);
}

boolean CWindowManager::DragBegin (CWindow *pSrc, const char *pLabel)
{
	m_SpinLock.Acquire ();
	boolean bOK = pSrc != 0 && (m_nLastButtons & 1) != 0 && m_pFullscreen == 0;
	if (bOK)
	{
		if (m_bDnd) DndFinishLocked (m_nCursorX, m_nCursorY, TRUE);	// (a stale session)
		unsigned i = 0;
		for (; pLabel != 0 && pLabel[i] != '\0' && i < DND_LABEL_MAX - 1; i++) m_DndLabel[i] = pLabel[i];
		m_DndLabel[i] = '\0';
		m_pDndSrc = pSrc; m_pDndOver = 0; m_bDnd = TRUE;
	}
	m_SpinLock.Release ();
	ScreenDirty ();
	return bOK;
}

// Caller holds m_SpinLock. End the session: DROP to the window under (x,y) (unless
// cancelled), DRAG_DONE to the source.
void CWindowManager::DndFinishLocked (int x, int y, boolean bCancel)
{
	CWindow *pSrc = m_pDndSrc;
	m_bDnd = FALSE; m_pDndSrc = 0;
	if (m_pDndOver != 0) { EmitDnd (m_pDndOver, GUI_EVENT_DRAG_OVER, 0, 0, DND_F_LEAVE); m_pDndOver = 0; }
	unsigned nFlags = (m_nModifiers & MOD_CTRL) ? DND_F_COPY : 0, nPid = 0;
	if (bCancel)
	{
		nFlags |= DND_F_CANCEL;
	}
	else
	{
		boolean bTitle = FALSE;
		unsigned nHit = HitTest (x, y, &bTitle);
		CWindow *pT = nHit != ~0u ? m_pWindows[nHit] : 0;
		if (pT == 0 || pT->Backmost ()) nFlags |= DND_F_DESKTOP;
		if (pT != 0 && pT->PointerHandler () != 0)
		{
			EmitDnd (pT, GUI_EVENT_DROP, x - (pT->X () + pT->ChromeL ()),
				 y - (pT->Y () + pT->ChromeT ()), nFlags & DND_F_COPY);
			nPid = pT->OwnerPid ();
		}
	}
	EmitDnd (pSrc, GUI_EVENT_DRAG_DONE, 0, 0, 0, ((long) nFlags << 32) | (long) nPid);
	ScreenDirty ();
}

// Push one GUI_EVENT_PTR_* event to a window's pointer handler (client coords,
// clamped non-negative; the toolkit treats out-of-bounds as "over no widget").
void CWindowManager::EmitPointer (CWindow *pWin, int nEvent, int cx, int cy,
				  unsigned nButtons, unsigned nChanged, int nWheel)
{
	if (pWin == 0) return;
	u64 ulH = pWin->PointerHandler ();
	if (ulH == 0) return;
	if (cx < 0) cx = 0; else if (cx > 0xFFFF) cx = 0xFFFF;
	if (cy < 0) cy = 0; else if (cy > 0xFFFF) cy = 0xFFFF;
	if (nWheel >  127) nWheel =  127;			// fits the signed 8-bit wheel field
	if (nWheel < -128) nWheel = -128;
	GUIEvent Ev;
	Ev.ulHandler = ulH;
	Ev.ulSender  = 0;
	Ev.nEvent    = nEvent;
	Ev.lValue    = ((long) (nWheel & 0xFF) << 48) | ((long) (nChanged & 0xFF) << 40)
		     | ((long) (nButtons & 0xFF) << 32)
		     | ((long) (cx & 0xFFFF) << 16) | (long) (cy & 0xFFFF);
	pWin->PushEvent (Ev);
}

// Route a scroll-wheel notch to the window under the cursor (or the pointer-capture
// window if a drag is in progress) as a GUI_EVENT_PTR_WHEEL pointer event.
void CWindowManager::OnMouseWheel (int x, int y, int nWheel)
{
	if (nWheel == 0) return;
	if (x < 0) x = 0; else if (x >= g_nScreenWidth)  x = g_nScreenWidth - 1;
	if (y < 0) y = 0; else if (y >= g_nScreenHeight) y = g_nScreenHeight - 1;

	m_SpinLock.Acquire ();
	CWindow *pTarget = m_pFullscreen != 0 ? m_pFullscreen : m_pPtrCaptureWindow;
	if (pTarget == m_pFullscreen && pTarget != 0)
	{
		EmitPointer (pTarget, GUI_EVENT_PTR_WHEEL, x, y, m_nLastButtons, 0, nWheel * m_nWheelSpeed);
		m_SpinLock.Release ();
		return;
	}
	if (pTarget == 0)
	{
		boolean bOnTitle = FALSE;
		unsigned nHit = HitTest (x, y, &bOnTitle);
		if (nHit != ~0u)
		{
			CWindow *pW = m_pWindows[nHit];
			if (pW->PointerHandler () != 0 && !bOnTitle && !pW->HitCloseBox (x, y))
				pTarget = pW;
		}
	}
	if (pTarget != 0)
	{
		int cx = x - (pTarget->X () + pTarget->ChromeL ());
		int cy = y - (pTarget->Y () + pTarget->ChromeT ());
		EmitPointer (pTarget, GUI_EVENT_PTR_WHEEL, cx, cy, m_nLastButtons, 0,
			     nWheel * m_nWheelSpeed);		// scale the raw notch to lines
	}
	m_SpinLock.Release ();
}

// System-wide wheel speed (lines per notch); clamped to [1,16].
void CWindowManager::SetWheelSpeed (int nLinesPerNotch)
{
	if (nLinesPerNotch < 1)  nLinesPerNotch = 1;
	if (nLinesPerNotch > 16) nLinesPerNotch = 16;
	m_nWheelSpeed = nLinesPerNotch;
}

// Parse the next logical key from a Circle cooked-mode string, advancing *pp.
// Returns 0 at end of string. Printable/control bytes return their value; VT100
// escape sequences (cursor/home/end/page/del) map to KEY_* codes.
static int NextKey (const char **pp, unsigned *pMods = 0)
{
	const char *p = *pp;
	if (*p == '\0')
	{
		return 0;
	}
	if (p[0] == 0x1b && p[1] == '[')
	{
		// ESC '[' [n [';' m]] final -- the modifier form (Ctrl+Up = ESC[1;5A, Ctrl+PgUp =
		// ESC[5;5~) gives the same KEY_* as the plain key: apps read Ctrl / Shift / Alt with
		// kapi_get_modifiers. (It used to fall through as the characters '1' ';' '5' 'A'.)
		// The xterm modifier parameter m = 1 + Shift(1) + Alt(2) + Ctrl(4) is returned in
		// *pMods and travels with the key event (kapi_get_modifiers reports it while the
		// app handles that key), so Shift+arrow works even when the global modifier state
		// is late or clobbered (VNC, a second keyboard).
		if (p[2] == '[')						// F1-F5: ESC[[A..E
		{
			char c = p[3];
			*pp = p + (c != '\0' ? 4 : 3);
			if (c >= 'A' && c <= 'E') return KEY_F1 + (c - 'A');
			return NextKey (pp, pMods);
		}
		int n = 0, i = 2, code = 0, m = 0;
		while (p[i] >= '0' && p[i] <= '9') n = n * 10 + (p[i++] - '0');
		if (p[i] == ';') { i++; while (p[i] >= '0' && p[i] <= '9') m = m * 10 + (p[i++] - '0'); }
		if (pMods != 0 && m > 1)
		{
			m--;
			*pMods = ((m & 1) ? MOD_SHIFT : 0) | ((m & 2) ? MOD_ALT : 0) | ((m & 4) ? MOD_CTRL : 0);
		}
		switch (p[i])
		{
		case 'A': code = KEY_UP;    break;
		case 'B': code = KEY_DOWN;  break;
		case 'C': code = KEY_RIGHT; break;
		case 'D': code = KEY_LEFT;  break;
		case 'H': code = KEY_HOME;  break;
		case 'F': code = KEY_END;   break;
		case '~':
			switch (n) { case 1: code = KEY_HOME; break; case 3: code = KEY_DEL; break; case 4: code = KEY_END; break;
				     case 5: code = KEY_PGUP; break; case 6: code = KEY_PGDN; break;
				     case 11: case 12: case 13: case 14: case 15: code = KEY_F1 + (n - 11); break;	// xterm F1-F5
				     case 17: case 18: case 19: case 20: case 21: code = KEY_F1 + 5 + (n - 17); break;	// F6-F10
				     case 23: case 24: code = KEY_F1 + 10 + (n - 23); break; }			// F11, F12
			break;
		}
		if (p[i] != '\0') i++;				// the final byte
		*pp = p + i;
		if (code != 0) return code;
		return NextKey (pp, pMods);			// unknown sequence: skipped whole
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

// (2026-10-08) The same parsing for a server's policy (core.h el_core_next_key: PocketUI's shell keys).
int WmNextKey (const char **pp, unsigned *pMods)
{
	return NextKey (pp, pMods);
}

void CWindowManager::OnMouse (int x, int y, unsigned nButtons)
{
	if (x < 0) x = 0; else if (x >= g_nScreenWidth)  x = g_nScreenWidth - 1;
	if (y < 0) y = 0; else if (y >= g_nScreenHeight) y = g_nScreenHeight - 1;

	m_SpinLock.Acquire ();
	m_nMouseEvents++;
	if (x != m_nCursorX || y != m_nCursorY || !m_bCursorShown)
	{
		// the cursor's old and new places (the drag badge, next to it, too)
		// (any shape: its box around the hot spot, v81)
		int cw = WM_CURSOR_BOX, ch = WM_CURSOR_BOX;
		if (m_bDnd)
		{
			int bw = 16 + GImage::TextWidth (m_DndLabel) + GImage::FontWidth () + 16, bh = 18 + GImage::FontHeight () + 10;
			if (bw > cw) cw = bw;
			if (bh > ch) ch = bh;
		}
		const int R = WM_CURSOR_REACH;
		if (m_bCursorShown) ScreenDirtyRect (m_nCursorX - R, m_nCursorY - R, cw + R, ch + R);
		ScreenDirtyRect (x - R, y - R, cw + R, ch + R);
	}
	m_nCursorX = x; m_nCursorY = y; m_bCursorShown = TRUE;

	if (m_pFullscreen != 0)
	{
		// Full-screen app: the whole pointer stream goes to it, in screen coordinates.
		CWindow *pFs = m_pFullscreen;
		for (unsigned b = 1; b <= 4; b <<= 1)
		{
			boolean now = (nButtons & b) != 0, was = (m_nLastButtons & b) != 0;
			if (now && !was)      EmitPointer (pFs, GUI_EVENT_PTR_DOWN, x, y, nButtons, b);
			else if (!now && was) EmitPointer (pFs, GUI_EVENT_PTR_UP, x, y, nButtons, b);
		}
		if (x != m_nPrevX || y != m_nPrevY) EmitPointer (pFs, GUI_EVENT_PTR_MOVE, x, y, nButtons, 0);
		m_nPrevX = x; m_nPrevY = y; m_nLastButtons = nButtons;
		ShowShapeLocked (pFs->CursorShape ());
		m_SpinLock.Release ();
		return;
	}

	if (m_bDnd)
	{
		// Drag session: DRAG_OVER to the window under the cursor, DROP at the release.
		// (The normal stream below still reaches the source -- its capture -- so its
		// widgets see the button go up.)
		boolean bT = FALSE;
		unsigned nHit = HitTest (x, y, &bT);
		CWindow *pOver = nHit != ~0u ? m_pWindows[nHit] : 0;
		if (pOver != 0 && pOver->PointerHandler () == 0) pOver = 0;
		if (pOver != m_pDndOver && m_pDndOver != 0)
			EmitDnd (m_pDndOver, GUI_EVENT_DRAG_OVER, 0, 0, DND_F_LEAVE);
		if (pOver != 0 && (pOver != m_pDndOver || x != m_nPrevX || y != m_nPrevY))
			EmitDnd (pOver, GUI_EVENT_DRAG_OVER, x - (pOver->X () + pOver->ChromeL ()),
				 y - (pOver->Y () + pOver->ChromeT ()),
				 (m_nModifiers & MOD_CTRL) ? DND_F_COPY : 0);
		m_pDndOver = pOver;
		if ((nButtons & 1) == 0) DndFinishLocked (x, y, FALSE);
	}

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
			int nBtn = pWin->HitTitleButton (x, y);
			unsigned nEdge = nBtn < 0 ? pWin->HitResizeEdge (x, y) : 0;
			if (nEdge != 0)					// (v82) an edge, a corner: resized
			{
				m_pSizeWindow = pWin; m_nSizeEdge = nEdge;
				m_nSizePX = x; m_nSizePY = y;
				m_nSizeX = m_nSizeX0 = pWin->X (); m_nSizeY = m_nSizeY0 = pWin->Y ();
				m_nSizeW = m_nSizeW0 = pWin->OuterWidth (); m_nSizeH = m_nSizeH0 = pWin->OuterHeight ();
				SizeOutlineDirty ();
			}
			else if (nBtn == KAPI_FRAME_MENU)
			{
				EmitWinCtl (pWin, KAPI_FRAME_MENU);	// (a menu opens at the press)
			}
			else if (nBtn >= 0)
			{
				m_pBtnDown = pWin;			// acted on at the release, over it
				m_nBtnDown = nBtn;
			}
			else if (bOnTitle && pWin->Pinned ())
			{
				// (PocketUI's card: its title bar neither drags it nor maximises it)
			}
			else if (bOnTitle && !pWin->Fixed ())		// (a fixed window stays put)
			{
				unsigned nNow = CTimer::Get ()->GetTicks ();
				if (m_pTitleClick == pWin && nNow - m_nTitleClickTicks < HZ * 4 / 10)
				{
					m_pTitleClick = 0;			// a double click: maximise / restore
					EmitWinCtl (pWin, KAPI_FRAME_MAXIMISE);
				}
				else
				{
					m_pTitleClick = pWin;
					m_nTitleClickTicks = nNow;
					m_pDragWindow = pWin;
					m_nDragDX = x - pWin->X ();
					m_nDragDY = y - pWin->Y ();
				}
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
		if (m_pSizeWindow != 0) SizeDragLocked (x, y);
		else if (m_pDragWindow != 0)
		{
			int ny = y - m_nDragDY, nInset = TopInsetLocked ();
			if (!m_pDragWindow->Topmost () && ny < nInset) ny = nInset;	// not under the bar
			m_pDragWindow->Move (x - m_nDragDX, ny);
		}
	}
	else if (!bLeftNow && bLeftWas)
	{
		m_pDragWindow = 0;
		if (m_pSizeWindow != 0) SizeEndLocked ();
		if (m_pBtnDown != 0)				// a title button's release: over it, it acts
		{
			CWindow *pWin = m_pBtnDown;
			int nBtn = m_nBtnDown;
			m_pBtnDown = 0;
			if (pWin->HitTitleButton (x, y) == nBtn)
			{
				if (nBtn == KAPI_FRAME_CLOSE)		pWin->RequestExit ();
				else if (nBtn == KAPI_FRAME_MINIMISE)	MinimiseLocked (pWin);
				else					EmitWinCtl (pWin, nBtn);
			}
		}
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
		if (m_pSizeWindow != 0) pOver = 0;		// (the frame is being dragged: not the app's)
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
	PickShapeLocked ();
	m_SpinLock.Release ();
}

// ---- the pointer's shape (kapi v81) ---------------------------------------------------
void CWindowManager::SetCursorImage (unsigned nShape, GImage *pImage, int nHotX, int nHotY)
{
	if (nShape >= WM_CURSOR_SHAPES) return;
	m_pShape[nShape] = pImage;
	m_nShapeHotX[nShape] = nHotX;
	m_nShapeHotY[nShape] = nHotY;
}

void CWindowManager::ShowShapeLocked (unsigned nShape)
{
	if (nShape >= WM_CURSOR_SHAPES || m_pShape[nShape] == 0) nShape = 0;
	if (nShape == m_nShape) return;
	m_nShape = nShape;
	if (m_bCursorShown)
		ScreenDirtyRect (m_nCursorX - WM_CURSOR_REACH, m_nCursorY - WM_CURSOR_REACH,
				 WM_CURSOR_BOX + WM_CURSOR_REACH, WM_CURSOR_BOX + WM_CURSOR_REACH);
}

// ---- a window resized by its frame (kapi v82) --------------------------------------------
// The frame's edges under a point of the screen: within WIN_EDGE_BAND of the outer edge; near a
// corner, the two edges. 0: the window is not resizable, or the point is elsewhere.
unsigned CWindow::HitResizeEdge (int sx, int sy) const
{
	if (!m_bResizable || Borderless () || Fixed () || m_bPinned) return 0;
	int rx = sx - m_nX, ry = sy - m_nY, W = OuterWidth (), H = OuterHeight ();
	if (rx < 0 || ry < 0 || rx >= W || ry >= H) return 0;
	unsigned e = 0;
	if (rx < WIN_EDGE_BAND) e |= WIN_EDGE_L; else if (rx >= W - WIN_EDGE_BAND) e |= WIN_EDGE_R;
	if (ry < WIN_EDGE_BAND) e |= WIN_EDGE_T; else if (ry >= H - WIN_EDGE_BAND) e |= WIN_EDGE_B;
	if (e == 0) return 0;
	if (e & (WIN_EDGE_L | WIN_EDGE_R))
	{
		if (ry < WIN_EDGE_CORNER) e |= WIN_EDGE_T; else if (ry >= H - WIN_EDGE_CORNER) e |= WIN_EDGE_B;
	}
	if (e & (WIN_EDGE_T | WIN_EDGE_B))
	{
		if (rx < WIN_EDGE_CORNER) e |= WIN_EDGE_L; else if (rx >= W - WIN_EDGE_CORNER) e |= WIN_EDGE_R;
	}
	return e;
}

static unsigned EdgeShape (unsigned nEdge)
{
	boolean bH = (nEdge & (WIN_EDGE_L | WIN_EDGE_R)) != 0, bV = (nEdge & (WIN_EDGE_T | WIN_EDGE_B)) != 0;
	if (bH && bV)
	{
		boolean bNWSE = (nEdge & (WIN_EDGE_L | WIN_EDGE_T)) == (WIN_EDGE_L | WIN_EDGE_T)
			     || (nEdge & (WIN_EDGE_R | WIN_EDGE_B)) == (WIN_EDGE_R | WIN_EDGE_B);
		return bNWSE ? KAPI_CURSOR_SIZE_NWSE : KAPI_CURSOR_SIZE_NESW;
	}
	return bH ? KAPI_CURSOR_SIZE_H : bV ? KAPI_CURSOR_SIZE_V : 0;
}

// The outline's four sides (3 pixels thick) made dirty: where it was, where it is.
void CWindowManager::SizeOutlineDirty (void)
{
	int x = m_nSizeX, y = m_nSizeY, w = m_nSizeW, h = m_nSizeH;
	ScreenDirtyRect (x, y, w, 3); ScreenDirtyRect (x, y + h - 3, w, 3);
	ScreenDirtyRect (x, y, 3, h); ScreenDirtyRect (x + w - 3, y, 3, h);
}

// The pointer moved with the button down: the frame as it would be, from where it was at the press
// -- never smaller than the window's smallest client area, its top never under the menu bar.
void CWindowManager::SizeDragLocked (int x, int y)
{
	CWindow *pWin = m_pSizeWindow;
	int dx = x - m_nSizePX, dy = y - m_nSizePY;
	int nX = m_nSizeX0, nY = m_nSizeY0, nW = m_nSizeW0, nH = m_nSizeH0;
	int nMinW = pWin->MinClientW () + pWin->ChromeL () + pWin->ChromeR ();
	int nMinH = pWin->MinClientH () + pWin->ChromeT () + pWin->ChromeB ();
	if (m_nSizeEdge & WIN_EDGE_R) nW += dx;
	if (m_nSizeEdge & WIN_EDGE_B) nH += dy;
	if (m_nSizeEdge & WIN_EDGE_L) { nX += dx; nW -= dx; }
	if (m_nSizeEdge & WIN_EDGE_T)
	{
		int nTop = TopInsetLocked ();
		if (nY + dy < nTop) dy = nTop - nY;
		nY += dy; nH -= dy;
	}
	if (nW > g_nScreenWidth) nW = g_nScreenWidth;
	if (nH > g_nScreenHeight) nH = g_nScreenHeight;
	if (nW < nMinW) { if (m_nSizeEdge & WIN_EDGE_L) nX -= nMinW - nW; nW = nMinW; }
	if (nH < nMinH) { if (m_nSizeEdge & WIN_EDGE_T) nY -= nMinH - nH; nH = nMinH; }
	if (nX == m_nSizeX && nY == m_nSizeY && nW == m_nSizeW && nH == m_nSizeH) return;
	SizeOutlineDirty ();
	m_nSizeX = nX; m_nSizeY = nY; m_nSizeW = nW; m_nSizeH = nH;
	SizeOutlineDirty ();
}

// The release: the outline goes, the window is told its new frame (it resizes and moves itself).
void CWindowManager::SizeEndLocked (void)
{
	CWindow *pWin = m_pSizeWindow;
	SizeOutlineDirty ();
	m_pSizeWindow = 0;
	if (pWin == 0 || pWin->PointerHandler () == 0) return;
	if (m_nSizeX == m_nSizeX0 && m_nSizeY == m_nSizeY0 && m_nSizeW == m_nSizeW0 && m_nSizeH == m_nSizeH0) return;
	int cw = m_nSizeW - pWin->ChromeL () - pWin->ChromeR (), ch = m_nSizeH - pWin->ChromeT () - pWin->ChromeB ();
	GUIEvent Ev;
	Ev.ulHandler = pWin->PointerHandler ();
	Ev.ulSender  = 0;
	Ev.nEvent    = GUI_EVENT_WINRESIZE;
	Ev.lValue    = (long) (((u64) (u16) (s16) m_nSizeX << 48) | ((u64) (u16) (s16) m_nSizeY << 32)
			       | ((u64) (u16) cw << 16) | (u64) (u16) ch);
	pWin->PushEvent (Ev);
}

// What is under the pointer: a window being dragged by its title shows the four arrows; the
// window that holds the pointer (a button down) or the one whose client area it is over, its own
// shape; anything else (a frame, a title bar, the desktop, a drag & drop) the arrow.
void CWindowManager::PickShapeLocked (void)
{
	unsigned nShape = 0;
	if (m_pSizeWindow != 0) nShape = EdgeShape (m_nSizeEdge);
	else if (m_pDragWindow != 0) nShape = KAPI_CURSOR_MOVE;
	else if (!m_bDnd)
	{
		if (m_pPtrCaptureWindow == 0)			// (v82) on a frame's edge: the arrows that size it
		{
			boolean bOnTitle = FALSE;
			unsigned nHit = HitTest (m_nCursorX, m_nCursorY, &bOnTitle);
			if (nHit != ~0u && m_pWindows[nHit]->HitTitleButton (m_nCursorX, m_nCursorY) < 0)
				nShape = EdgeShape (m_pWindows[nHit]->HitResizeEdge (m_nCursorX, m_nCursorY));
		}
		if (nShape == 0)
		{
			CWindow *pWin = m_pPtrCaptureWindow != 0 ? m_pPtrCaptureWindow : m_pPtrOverWindow;
			if (pWin != 0) nShape = pWin->CursorShape ();
		}
	}
	ShowShapeLocked (nShape);
}

void CWindowManager::SetWindowCursor (CWindow *pWindow, unsigned nShape)
{
	m_SpinLock.Acquire ();
	pWindow->SetCursorShape (nShape);
	if (m_pFullscreen == pWindow) ShowShapeLocked (nShape);
	else if (m_pFullscreen == 0) PickShapeLocked ();
	m_SpinLock.Release ();
}

// ---- held keys (ABI v48) ------------------------------------------------------
// USB HID usage -> logical key (letters by their US-layout position).
static int UsageToKey (unsigned char u)
{
	if (u >= 0x04 && u <= 0x1D) return 'a' + (u - 0x04);
	if (u >= 0x1E && u <= 0x26) return '1' + (u - 0x1E);
	switch (u)
	{
	case 0x27: return '0';
	case 0x28: case 0x58: return KEY_ENTER;
	case 0x29: return 27;
	case 0x2C: return ' ';
	case 0x4F: return KEY_RIGHT;
	case 0x50: return KEY_LEFT;
	case 0x51: return KEY_DOWN;
	case 0x52: return KEY_UP;
	}
	return 0;
}

void CWindowManager::SetUsbHeld (const unsigned char RawKeys[6])
{
	u32 Held[HELD_WORDS];
	for (unsigned i = 0; i < HELD_WORDS; i++) Held[i] = 0;
	for (unsigned i = 0; i < 6; i++)
	{
		int k = UsageToKey (RawKeys[i]);
		if (k > 0 && k < HELD_KEYS) Held[k >> 5] |= 1u << (k & 31);
	}
	for (unsigned i = 0; i < HELD_WORDS; i++) m_UsbHeld[i] = Held[i];
}

void CWindowManager::SetInjectedHeld (int nKey, boolean bDown)
{
	if (nKey >= 'A' && nKey <= 'Z') nKey += 'a' - 'A';
	if (nKey <= 0 || nKey >= HELD_KEYS) return;
	if (bDown) m_VncHeld[nKey >> 5] |= 1u << (nKey & 31);
	else       m_VncHeld[nKey >> 5] &= ~(1u << (nKey & 31));
}

boolean CWindowManager::KeyHeld (int nKey, CWindow *pWin)
{
	if (nKey >= 'A' && nKey <= 'Z') nKey += 'a' - 'A';
	if (nKey == '\n' || nKey == '\r') nKey = KEY_ENTER;
	if (nKey <= 0 || nKey >= HELD_KEYS || pWin == 0) return FALSE;
	m_SpinLock.Acquire ();
	boolean bFocus = KeyTargetLocked () == pWin;
	m_SpinLock.Release ();
	if (!bFocus) return FALSE;
	return ((m_UsbHeld[nKey >> 5] | m_VncHeld[nKey >> 5]) >> (nKey & 31)) & 1 ? TRUE : FALSE;
}

boolean CWindowManager::KeyHeldAny (int nKey)
{
	if (nKey >= 'A' && nKey <= 'Z') nKey += 'a' - 'A';
	if (nKey == '\n' || nKey == '\r') nKey = KEY_ENTER;
	if (nKey <= 0 || nKey >= HELD_KEYS) return FALSE;
	return ((m_UsbHeld[nKey >> 5] | m_VncHeld[nKey >> 5]) >> (nKey & 31)) & 1 ? TRUE : FALSE;
}

CWindow *CWindowManager::KeyTarget (void)
{
	m_SpinLock.Acquire ();
	CWindow *pWin = KeyTargetLocked ();
	m_SpinLock.Release ();
	return pWin;
}

boolean CWindowManager::HasKeyFocus (CWindow *pWin)
{
	if (pWin == 0) return FALSE;
	m_SpinLock.Acquire ();
	boolean bFocus = KeyTargetLocked () == pWin;
	m_SpinLock.Release ();
	return bFocus;
}

void CWindowManager::OnKey (const char *pString)
{
	if (pString == 0) return;
	m_SpinLock.Acquire ();
	m_nKeyEvents++;
	if (m_bDnd && pString[0] == 0x1b && pString[1] == '\0')	// Esc cancels a drag
	{
		DndFinishLocked (m_nCursorX, m_nCursorY, TRUE);
		m_SpinLock.Release ();
		return;
	}
	// Deliver keys to the topmost window's app-level key handler. Apps own their text
	// input via the user-side uikit toolkit -- no kernel widgets or dialogs any more.
	boolean bSwitched = FALSE;
	const char *p = pString; int code;
	unsigned nMods;
	while (nMods = 0, (code = NextKey (&p, &nMods)) != 0)
	{
		// (v65) Ctrl+Alt+Left / Right: the previous / next desk; with Shift the active window
		// goes along (not for a full-screen app: every key is its own)
		unsigned m = m_nModifiers | nMods;
		if ((m & (MOD_CTRL | MOD_ALT)) == (MOD_CTRL | MOD_ALT) && (code == KEY_LEFT || code == KEY_RIGHT)
		    && m_pFullscreen == 0 && m_nDesks > 1)
		{
			int n = (m_nDesk + (code == KEY_RIGHT ? 1 : m_nDesks - 1)) % m_nDesks;
			CWindow *pMove = (m & MOD_SHIFT) ? ActiveLocked () : 0;
			if (pMove != 0 && pMove->Desk () >= 0) pMove->SetDesk (n);
			SetDeskLocked (n);
			if (pMove != 0) RaiseLocked (pMove);
			bSwitched = TRUE;
			continue;
		}
		CWindow *pTop = KeyTargetLocked ();		// topmost window except the menu bar
		u64 ulKeyHandler = pTop != 0 ? pTop->KeyHandler () : 0;
		if (pTop == 0 || ulKeyHandler == 0)
		{
			continue;
		}
		GUIEvent Ev;
		Ev.nMods     = m;
		Ev.ulHandler = ulKeyHandler;
		Ev.ulSender  = 0;
		Ev.nEvent    = GUI_EVENT_KEY;
		Ev.lValue    = code;
		pTop->PushEvent (Ev);
	}
	m_SpinLock.Release ();
	if (bSwitched) ScreenDirty ();
}

