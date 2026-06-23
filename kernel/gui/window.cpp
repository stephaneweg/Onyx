//
// window.cpp
//
#include <kern/gui/window.h>
#include <kern/gui/skin.h>		// 9-slice widget skins (button/close/window)
#include <kern/layout.h>		// KPAGE_SIZE / KPAGE_MASK
#include <circle/util.h>		// memset
#include <circle/new.h>
#include <assert.h>

CWindow::CWindow (int x, int y, int nClientW, int nClientH, const char *pTitle)
:	m_nX (x), m_nY (y),
	m_pRawAlloc (0), m_ulCanvasPhys (0), m_nCanvasPages (0),
	m_nWidgets (0), m_nEvHead (0), m_nEvTail (0), m_bExitRequested (FALSE)
{
	for (unsigned i = 0; i < WIN_MAX_WIDGETS; i++)
	{
		m_Widgets[i].bUsed = FALSE;
	}

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
}

CWindow::~CWindow (void)
{
	if (m_pRawAlloc != 0)
	{
		delete [] (u8 *) m_pRawAlloc;
		m_pRawAlloc = 0;
	}
}

void CWindow::DrawTo (GImage *pScreen, boolean bActive)
{
	int cw = m_Canvas.Width ();
	int ch = m_Canvas.Height ();

	// Outer rectangle: border + title bar + client area.
	int x0 = m_nX;
	int y0 = m_nY;
	int x1 = m_nX + cw + 2 * WIN_BORDER - 1;
	int y1 = m_nY + WIN_TITLEBAR_H + ch + WIN_BORDER - 1;

	// Frame.
	pScreen->FillRectangle (x0, y0, x1, y1, WIN_COLOR_FRAME);

	// Title bar.
	pScreen->FillRectangle (x0, y0, x1, y0 + WIN_TITLEBAR_H - 1,
				bActive ? WIN_COLOR_TITLE_ACT : WIN_COLOR_TITLE);

	// Title text, vertically centred in the title bar.
	pScreen->DrawText (x0 + 6, y0 + (WIN_TITLEBAR_H - GImage::FontHeight ()) / 2,
			   m_Title, 0x00FFFFFF);

	// Close box [x] at the right of the title bar (skinned if available).
	int cbx0, cby0, cbx1, cby1;
	CloseBoxRect (&cbx0, &cby0, &cbx1, &cby1);
	if (g_pCloseSkin != 0)
	{
		g_pCloseSkin->DrawOn (pScreen, 0, cbx0, cby0,
				      cbx1 - cbx0 + 1, cby1 - cby0 + 1, TRUE);
	}
	else
	{
		pScreen->FillRectangle (cbx0, cby0, cbx1, cby1, 0x00C03020);
		pScreen->DrawRectangle (cbx0, cby0, cbx1, cby1, 0x00000000);
	}
	// X glyph on top so the close box is always recognizable.
	pScreen->DrawLine (cbx0 + 3, cby0 + 3, cbx1 - 3, cby1 - 3, 0x00FFFFFF);
	pScreen->DrawLine (cbx1 - 3, cby0 + 3, cbx0 + 3, cby1 - 3, 0x00FFFFFF);

	// Client area = the owner's canvas, blitted opaque just below the title bar.
	int clientX = x0 + WIN_BORDER;
	int clientY = y0 + WIN_TITLEBAR_H;
	pScreen->PutOther (&m_Canvas, clientX, clientY, FALSE);

	// Widgets, drawn over the canvas (client-relative coords + client origin).
	for (unsigned i = 0; i < m_nWidgets; i++)
	{
		const GWidget *pW = &m_Widgets[i];
		if (!pW->bUsed)
		{
			continue;
		}
		int bx0 = clientX + pW->nX;
		int by0 = clientY + pW->nY;
		int bx1 = bx0 + pW->nW - 1;
		int by1 = by0 + pW->nH - 1;
		if (pW->nType == GW_BUTTON)
		{
			u32 nTextColor;
			if (g_pButtonSkin != 0)
			{
				// nState selects the skin row: 0 normal / 1 hover / 2 pressed.
				g_pButtonSkin->DrawOn (pScreen, (unsigned) pW->nState,
						       bx0, by0, pW->nW, pW->nH, TRUE);
				nTextColor = 0x00000000;	// skin bg is light
			}
			else
			{
				pScreen->FillRectangle (bx0, by0, bx1, by1, 0x00606878);
				pScreen->DrawRectangle (bx0, by0, bx1, by1, 0x00000000);
				nTextColor = 0x00FFFFFF;
			}
			int tx = bx0 + (pW->nW - GImage::TextWidth (pW->Label)) / 2;
			int ty = by0 + (pW->nH - GImage::FontHeight ()) / 2;
			pScreen->DrawText (tx, ty, pW->Label, nTextColor);
		}
	}

	// Outline.
	pScreen->DrawRectangle (x0, y0, x1, y1, 0x00000000);
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
	int x0, y0, x1, y1;
	CloseBoxRect (&x0, &y0, &x1, &y1);
	return sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1;
}

GWidget *CWindow::AddWidget (int nType, int x, int y, int w, int h,
			     const char *pLabel, u64 ulHandler)
{
	if (m_nWidgets >= WIN_MAX_WIDGETS)
	{
		return 0;
	}
	GWidget *pW = &m_Widgets[m_nWidgets];
	pW->nType = nType;
	pW->nX = x; pW->nY = y; pW->nW = w; pW->nH = h;
	pW->ulHandler = ulHandler;
	pW->nState = 0;
	pW->bUsed = TRUE;

	pW->Label[0] = '\0';
	if (pLabel != 0)
	{
		unsigned i;
		for (i = 0; i < sizeof (pW->Label) - 1 && pLabel[i] != '\0'; i++)
		{
			pW->Label[i] = pLabel[i];
		}
		pW->Label[i] = '\0';
	}

	m_nWidgets++;
	return pW;
}

GWidget *CWindow::HitWidget (int cx, int cy)
{
	for (unsigned i = 0; i < m_nWidgets; i++)
	{
		GWidget *pW = &m_Widgets[i];
		if (pW->bUsed
		    && cx >= pW->nX && cx < pW->nX + pW->nW
		    && cy >= pW->nY && cy < pW->nY + pW->nH)
		{
			return pW;
		}
	}
	return 0;
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

CWindowManager::CWindowManager (void)
:	m_nWindows (0),
	m_nCursorX (SCREEN_WIDTH / 2), m_nCursorY (SCREEN_HEIGHT / 2),
	m_bCursorShown (FALSE), m_nLastButtons (0),
	m_pDragWindow (0), m_nDragDX (0), m_nDragDY (0)
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
	if (m_pDragWindow == pWindow)
	{
		m_pDragWindow = 0;		// don't dereference a removed/freed window
	}
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

void CWindowManager::Composite (GImage *pScreen)
{
	// Snapshot the window list under the lock, then blit without holding it (so we
	// don't keep IRQ masked for the whole frame). Note: a window pointer in the
	// snapshot must stay valid while we blit -- process teardown removes a window
	// from the list but does NOT free the CWindow (see CAddressSpace::~), so the
	// memory remains valid here. (Proper deferred free is future work.)
	CWindow *pSnapshot[WM_MAX_WINDOWS];
	unsigned nCount;

	m_SpinLock.Acquire ();
	nCount = m_nWindows;
	for (unsigned i = 0; i < nCount; i++)
	{
		pSnapshot[i] = m_pWindows[i];
	}
	m_SpinLock.Release ();

	pScreen->Clear (WIN_COLOR_DESKTOP);

	// Draw back-to-front; the last (topmost) window is the active one.
	for (unsigned i = 0; i < nCount; i++)
	{
		if (pSnapshot[i] != 0)
		{
			pSnapshot[i]->DrawTo (pScreen, i == nCount - 1);
		}
	}

	// Cursor, drawn last so it floats above everything. A classic top-left arrow:
	// a black-bordered white triangle whose tip is at the cursor hot-spot (cx,cy).
	if (m_bCursorShown)
	{
		int cx = m_nCursorX;
		int cy = m_nCursorY;
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
		int x1 = x0 + pWin->ClientWidth () + 2 * WIN_BORDER - 1;
		int y1 = y0 + WIN_TITLEBAR_H + pWin->ClientHeight () + WIN_BORDER - 1;
		if (x >= x0 && x <= x1 && y >= y0 && y <= y1)
		{
			*pbOnTitleBar = (y < y0 + WIN_TITLEBAR_H);
			return (unsigned) i;
		}
	}
	return ~0u;
}

void CWindowManager::OnMouse (int x, int y, unsigned nButtons)
{
	// Clamp to the screen.
	if (x < 0) x = 0; else if (x >= SCREEN_WIDTH)  x = SCREEN_WIDTH - 1;
	if (y < 0) y = 0; else if (y >= SCREEN_HEIGHT) y = SCREEN_HEIGHT - 1;

	m_SpinLock.Acquire ();

	m_nCursorX = x;
	m_nCursorY = y;
	m_bCursorShown = TRUE;

	boolean bLeftNow  = (nButtons & 1) != 0;
	boolean bLeftWas  = (m_nLastButtons & 1) != 0;

	if (bLeftNow && !bLeftWas)
	{
		// Press edge: raise the window under the cursor; then close box / title
		// bar drag / widget click depending on where the press landed.
		boolean bOnTitle = FALSE;
		unsigned nHit = HitTest (x, y, &bOnTitle);
		if (nHit != ~0u)
		{
			CWindow *pWin = m_pWindows[nHit];
			// Raise: move to the end of the list (topmost + active).
			for (unsigned j = nHit; j + 1 < m_nWindows; j++)
			{
				m_pWindows[j] = m_pWindows[j + 1];
			}
			m_pWindows[m_nWindows - 1] = pWin;

			if (pWin->HitCloseBox (x, y))
			{
				pWin->RequestExit ();		// app polls / wakes and exits
			}
			else if (bOnTitle)
			{
				m_pDragWindow = pWin;
				m_nDragDX = x - pWin->X ();
				m_nDragDY = y - pWin->Y ();
			}
			else
			{
				// Client area: hit-test widgets in canvas-relative coords.
				int cx = x - (pWin->X () + WIN_BORDER);
				int cy = y - (pWin->Y () + WIN_TITLEBAR_H);
				GWidget *pW = pWin->HitWidget (cx, cy);
				if (pW != 0 && pW->ulHandler != 0)
				{
					GUIEvent Ev;
					Ev.ulHandler = pW->ulHandler;
					Ev.ulSender  = (u64) pW;
					Ev.nEvent    = GUI_EVENT_CLICK;
					Ev.lValue    = 0;
					pWin->PushEvent (Ev);
				}
			}
		}
	}
	else if (bLeftNow && bLeftWas && m_pDragWindow != 0)
	{
		// Drag: move the window so the grab point tracks the cursor.
		m_pDragWindow->Move (x - m_nDragDX, y - m_nDragDY);
	}
	else if (!bLeftNow)
	{
		m_pDragWindow = 0;	// release ends the drag
	}

	m_nLastButtons = nButtons;

	m_SpinLock.Release ();
}
