//
// window.cpp
//
#include <kern/gui/window.h>
#include <kern/gui/gimage.h>		// GImage (icon widgets own a decoded BMP)
#include <kern/dialog.h>		// CDialog (kernel modal dialogs)
#include <kern/gui/skin.h>		// 9-slice widget skins (button/close/window)
#include <kern/layout.h>		// KPAGE_SIZE / KPAGE_MASK
#include <circle/util.h>		// memset
#include <circle/new.h>
#include <assert.h>

CWindow::CWindow (int x, int y, int nClientW, int nClientH, const char *pTitle,
		  unsigned nFlags)
:	m_nX (x), m_nY (y), m_nFlags (nFlags),
	m_nLogicalW (nClientW), m_nLogicalH (nClientH),
	m_pRawAlloc (0), m_ulCanvasPhys (0), m_nCanvasPages (0),
	m_ulKeyHandler (0), m_ulClickHandler (0),
	m_pDialog (0), m_pModalChild (0),
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
	for (unsigned i = 0; i < m_nWidgets; i++)
	{
		if (m_Widgets[i].pText != 0)		// textarea heap buffers
		{
			delete [] m_Widgets[i].pText;
			m_Widgets[i].pText = 0;
		}
		if (m_Widgets[i].pIcon != 0)		// GW_ICON decoded image
		{
			delete (GImage *) m_Widgets[i].pIcon;
			m_Widgets[i].pIcon = 0;
		}
	}
	if (m_pRawAlloc != 0)
	{
		delete [] (u8 *) m_pRawAlloc;
		m_pRawAlloc = 0;
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

// Render a multi-line editable text area: white field, char-wrapped lines clipped
// to the box, auto-scrolled to the bottom (cursor end), with a caret when focused.
static void DrawTextArea (GImage *pScreen, const GWidget *pW,
			  int bx0, int by0, int bx1, int by1)
{
	pScreen->FillRectangle (bx0, by0, bx1, by1, 0x00FFFFFF);
	pScreen->DrawRectangle (bx0, by0, bx1, by1,
				pW->bFocused ? 0x000000FF : 0x00808080);

	const char *t = pW->pText != 0 ? pW->pText : "";
	int nFW = GImage::FontWidth ();
	int nFH = GImage::FontHeight ();
	int nCols = (pW->nW - 8) / nFW;
	int nVis  = (pW->nH - 6) / nFH;
	if (nCols < 1) nCols = 1;
	if (nVis  < 1) nVis  = 1;

	// Pass 1: count wrapped lines to auto-scroll to the bottom.
	int nTotal = 1, nCol = 0;
	for (const char *p = t; *p != '\0'; p++)
	{
		if (*p == '\n') { nTotal++; nCol = 0; }
		else { if (++nCol >= nCols) { nTotal++; nCol = 0; } }
	}
	int nTop = nTotal > nVis ? nTotal - nVis : 0;

	// Pass 2: draw only the visible lines.
	int nLine = 0; nCol = 0;
	for (const char *p = t; *p != '\0'; p++)
	{
		char c = *p;
		if (c == '\n') { nLine++; nCol = 0; continue; }
		if (nLine >= nTop && nLine < nTop + nVis)
		{
			pScreen->DrawChar (bx0 + 4 + nCol * nFW,
					   by0 + 3 + (nLine - nTop) * nFH, c, 0x00000000);
		}
		if (++nCol >= nCols) { nLine++; nCol = 0; }
	}

	if (pW->bFocused && nLine >= nTop && nLine < nTop + nVis)	// caret at end
	{
		int cx = bx0 + 4 + nCol * nFW;
		int cy = by0 + 3 + (nLine - nTop) * nFH;
		pScreen->DrawLine (cx, cy, cx, cy + nFH - 1, 0x00000000);
	}
}

void CWindow::DrawTo (GImage *pScreen, boolean bActive)
{
	int cw = ClientWidth ();		// logical size (may be < allocated canvas)
	int ch = ClientHeight ();

	// Outer rectangle: chrome (border + title bar) + client area. A borderless
	// window has zero chrome, so the client area fills the whole window.
	int x0 = m_nX;
	int y0 = m_nY;
	int x1 = m_nX + cw + ChromeL () + ChromeR () - 1;
	int y1 = m_nY + ChromeT () + ch + ChromeB () - 1;

	if (!Borderless ())
	{
		// Frame + title bar: the window skin (wings.bmp, 7/7/32/7) draws the whole
		// chrome; otherwise a flat frame + title bar. The skin was pre-tinted at load
		// (Colorize) into an active + a dimmer inactive copy -- pick by focus.
		CSkin *pChrome = bActive ? g_pWindowSkin
				 : (g_pWindowSkinInactive ? g_pWindowSkinInactive : g_pWindowSkin);
		if (pChrome != 0)
		{
			pChrome->DrawOn (pScreen, 0, x0, y0, x1 - x0 + 1, y1 - y0 + 1, TRUE);
		}
		else
		{
			pScreen->FillRectangle (x0, y0, x1, y1, WIN_COLOR_FRAME);
			pScreen->FillRectangle (x0, y0, x1, y0 + WIN_TITLEBAR_H - 1,
						bActive ? WIN_COLOR_TITLE_ACT : WIN_COLOR_TITLE);
		}

		// Title text, vertically centred in the title bar (inside the left border).
		pScreen->DrawText (x0 + WIN_BORDER + 2,
				   y0 + (WIN_TITLEBAR_H - GImage::FontHeight ()) / 2,
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
	}

	// Client area = the owner's canvas, blitted opaque inside the chrome. Only the
	// logical sub-rect is shown (the canvas may be over-allocated for resizing).
	int clientX = x0 + ChromeL ();
	int clientY = y0 + ChromeT ();
	pScreen->PutOtherPart (&m_Canvas, clientX, clientY, 0, 0, cw, ch, FALSE);

	// Widgets, drawn over the canvas (client-relative coords + client origin).
	for (unsigned i = 0; i < m_nWidgets; i++)
	{
		const GWidget *pW = &m_Widgets[i];
		if (!pW->bUsed || pW->nW <= 0 || pW->nH <= 0)
		{
			continue;			// w=h=0 hides a widget (taskbar slot)
		}
		int bx0 = clientX + pW->nX;
		int by0 = clientY + pW->nY;
		int bx1 = bx0 + pW->nW - 1;
		int by1 = by0 + pW->nH - 1;
		int fh  = GImage::FontHeight ();

		switch (pW->nType)
		{
		case GW_BUTTON:
		{
			// Skin row: 0 normal / 1 hover / 2 pressed.
			unsigned nSt = pW->bMousePressed ? 2 : (pW->bMouseOver ? 1 : 0);
			u32 nTextColor;
			if (g_pButtonSkin != 0)
			{
				g_pButtonSkin->DrawOn (pScreen, nSt, bx0, by0, pW->nW, pW->nH, TRUE);
				nTextColor = 0x00000000;
			}
			else
			{
				u32 bg = pW->bMousePressed ? 0x00404850
				       : (pW->bMouseOver  ? 0x00707888 : 0x00606878);
				pScreen->FillRectangle (bx0, by0, bx1, by1, bg);
				pScreen->DrawRectangle (bx0, by0, bx1, by1, 0x00000000);
				nTextColor = 0x00FFFFFF;
			}
			int tx = bx0 + (pW->nW - GImage::TextWidth (pW->Label)) / 2;
			int ty = by0 + (pW->nH - fh) / 2;
			pScreen->DrawText (tx, ty, pW->Label, nTextColor);
			break;
		}

		case GW_LABEL:
			pScreen->DrawText (bx0, by0 + (pW->nH - fh) / 2, pW->Label, 0x00FFFFFF);
			break;

		case GW_CHECKBOX:
		{
			int nBox = pW->nH < 16 ? pW->nH : 16;
			int nBoxY = by0 + (pW->nH - nBox) / 2;
			pScreen->FillRectangle (bx0, nBoxY, bx0 + nBox - 1, nBoxY + nBox - 1,
						pW->bMouseOver ? 0x00FFFFFF : 0x00DDDDDD);
			pScreen->DrawRectangle (bx0, nBoxY, bx0 + nBox - 1, nBoxY + nBox - 1,
						0x00000000);
			if (pW->nState)		// check mark
			{
				pScreen->DrawLine (bx0 + 3, nBoxY + nBox / 2,
						   bx0 + nBox / 2, nBoxY + nBox - 3, 0x00007000);
				pScreen->DrawLine (bx0 + nBox / 2, nBoxY + nBox - 3,
						   bx0 + nBox - 3, nBoxY + 3, 0x00007000);
			}
			pScreen->DrawText (bx0 + nBox + 6, by0 + (pW->nH - fh) / 2,
					   pW->Label, 0x00FFFFFF);
			break;
		}

		case GW_TEXTBOX:
		{
			pScreen->FillRectangle (bx0, by0, bx1, by1, 0x00FFFFFF);
			pScreen->DrawRectangle (bx0, by0, bx1, by1,
						pW->bFocused ? 0x000000FF : 0x00808080);
			int ty = by0 + (pW->nH - fh) / 2;
			pScreen->DrawText (bx0 + 4, ty, pW->Label, 0x00000000);
			if (pW->bFocused)	// caret after the text
			{
				int cx = bx0 + 4 + GImage::TextWidth (pW->Label);
				pScreen->DrawLine (cx, by0 + 3, cx, by1 - 3, 0x00000000);
			}
			break;
		}

		case GW_PROGRESS:
		{
			pScreen->FillRectangle (bx0, by0, bx1, by1, 0x00303030);
			int nFill = pW->nState;
			if (nFill < 0) nFill = 0; else if (nFill > 100) nFill = 100;
			int nFW = (pW->nW - 2) * nFill / 100;
			if (nFW > 0)
			{
				pScreen->FillRectangle (bx0 + 1, by0 + 1, bx0 + nFW, by1 - 1,
							0x0030C030);
			}
			pScreen->DrawRectangle (bx0, by0, bx1, by1, 0x00000000);
			break;
		}

		case GW_SLIDER:
		{
			int nMidY = by0 + pW->nH / 2;
			pScreen->DrawLine (bx0 + 2, nMidY, bx1 - 2, nMidY, 0x00C0C0C0);   // groove
			int nVal = pW->nState;
			if (nVal < 0) nVal = 0; else if (nVal > 100) nVal = 100;
			int nThumb = bx0 + nVal * (pW->nW - 8) / 100;	// thumb left
			u32 nThumbCol = pW->bMousePressed ? 0x00FFFFFF
				      : (pW->bMouseOver  ? 0x00E0E0E0 : 0x00A0A0C0);
			pScreen->FillRectangle (nThumb, by0, nThumb + 7, by1, nThumbCol);
			pScreen->DrawRectangle (nThumb, by0, nThumb + 7, by1, 0x00000000);
			break;
		}

		case GW_TEXTAREA:
			DrawTextArea (pScreen, pW, bx0, by0, bx1, by1);
			break;

		case GW_SCROLLV:
		case GW_SCROLLH:
		{
			pScreen->FillRectangle (bx0, by0, bx1, by1, 0x00303840);   // track
			pScreen->DrawRectangle (bx0, by0, bx1, by1, 0x00000000);
			int nVal = pW->nState;
			if (nVal < 0) nVal = 0; else if (nVal > 100) nVal = 100;
			u32 nThumbCol = pW->bMousePressed ? 0x00FFFFFF
				      : (pW->bMouseOver  ? 0x00E0E0E0 : 0x00A0A0C0);
			if (pW->nType == GW_SCROLLV)
			{
				int nTH = pW->nH / 4; if (nTH < 16) nTH = 16;
				if (nTH > pW->nH) nTH = pW->nH;
				int ty = by0 + nVal * (pW->nH - nTH) / 100;
				pScreen->FillRectangle (bx0 + 1, ty, bx1 - 1, ty + nTH - 1, nThumbCol);
				pScreen->DrawRectangle (bx0 + 1, ty, bx1 - 1, ty + nTH - 1, 0x00000000);
			}
			else
			{
				int nTW = pW->nW / 4; if (nTW < 16) nTW = 16;
				if (nTW > pW->nW) nTW = pW->nW;
				int tx = bx0 + nVal * (pW->nW - nTW) / 100;
				pScreen->FillRectangle (tx, by0 + 1, tx + nTW - 1, by1 - 1, nThumbCol);
				pScreen->DrawRectangle (tx, by0 + 1, tx + nTW - 1, by1 - 1, 0x00000000);
			}
			break;
		}

		case GW_ICON:
		{
			// Hover / press highlight behind the icon.
			if (pW->bMousePressed)
			{
				pScreen->FillRectangle (bx0, by0, bx1, by1, 0x00405468);
			}
			else if (pW->bMouseOver)
			{
				pScreen->FillRectangle (bx0, by0, bx1, by1, 0x00303f50);
			}

			// Reserve a line at the bottom for the label (if any). The icon image
			// (magenta-keyed) is centred in the remaining area.
			int nLabelH = (pW->Label[0] != '\0') ? fh + 2 : 0;
			const GImage *pImg = (const GImage *) pW->pIcon;
			if (pImg != 0 && pImg->IsValid ())
			{
				int iw = pImg->Width ();
				int ih = pImg->Height ();
				int ix = bx0 + (pW->nW - iw) / 2;
				int iy = by0 + ((pW->nH - nLabelH) - ih) / 2;
				pScreen->PutOther (pImg, ix, iy, TRUE);
			}
			else
			{
				int m = 6;			// placeholder square when no image
				pScreen->FillRectangle (bx0 + m, by0 + m, bx1 - m,
							by1 - m - nLabelH, 0x00808890);
				pScreen->DrawRectangle (bx0 + m, by0 + m, bx1 - m,
							by1 - m - nLabelH, 0x00000000);
			}

			if (pW->Label[0] != '\0')
			{
				int tx = bx0 + (pW->nW - GImage::TextWidth (pW->Label)) / 2;
				pScreen->DrawText (tx, by1 - fh, pW->Label, 0x00FFFFFF);
			}

			// "open/running" badge: a small green triangle in the bottom-left.
			if (pW->nState != 0)
			{
				for (int t = 0; t < 9; t++)
				{
					pScreen->DrawLine (bx0 + 2, by1 - 2 - t,
							   bx0 + 2 + (8 - t), by1 - 2 - t, 0x0040E060);
				}
			}
			break;
		}
		}
	}

	// Outline (only for the flat frame; the skin draws its own border).
	if (g_pWindowSkin == 0)
	{
		pScreen->DrawRectangle (x0, y0, x1, y1, 0x00000000);
	}
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
	pW->bMouseOver = FALSE;
	pW->bMousePressed = FALSE;
	pW->bFocused = FALSE;
	pW->bUsed = TRUE;

	// A textarea owns a heap text buffer (the inline Label is too small).
	pW->pText = 0;
	pW->pIcon = 0;				// GW_ICON image attached by the caller (kapi)
	if (nType == GW_TEXTAREA)
	{
		pW->pText = new char[GW_AREA_CAP];
		if (pW->pText != 0)
		{
			pW->pText[0] = '\0';
		}
	}

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

// Actual framebuffer size in use; initialised to the compile-time defaults and
// overwritten at boot (CKernel) once the chosen resolution is known.
int g_nScreenWidth  = SCREEN_WIDTH;
int g_nScreenHeight = SCREEN_HEIGHT;

CWindowManager::CWindowManager (void)
:	m_nWindows (0), m_pWallpaper (0), m_pCursor (0),
	m_nCursorX (SCREEN_WIDTH / 2), m_nCursorY (SCREEN_HEIGHT / 2),
	m_nPrevX (0), m_nPrevY (0),
	m_bCursorShown (FALSE), m_nLastButtons (0),
	m_pDragWindow (0), m_nDragDX (0), m_nDragDY (0),
	m_pHoverWidget (0), m_pPressedWidget (0), m_pPressedWindow (0),
	m_pFocusWidget (0), m_pFocusWindow (0)
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
	// Drop any references into this window's widgets (it may be freed after).
	if (m_pDragWindow == pWindow)		{ m_pDragWindow = 0; }
	if (m_pPressedWindow == pWindow)	{ m_pPressedWindow = 0; m_pPressedWidget = 0; }
	if (m_pFocusWindow == pWindow)		{ m_pFocusWindow = 0; m_pFocusWidget = 0; }
	m_pHoverWidget = 0;			// recomputed on the next mouse move
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
	if (pWindow->ModalChild () != 0)
	{
		RaiseLocked (pWindow->ModalChild ());	// keep the dialog glued above its owner
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
	GImage  *pWall;

	m_SpinLock.Acquire ();
	nCount = m_nWindows;
	for (unsigned i = 0; i < nCount; i++)
	{
		pSnapshot[i] = m_pWindows[i];
	}
	pWall = m_pWallpaper;
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

// Caller holds m_SpinLock. Topmost window's widget under the cursor (client area
// only, not the title bar or close box); returns the widget + its window, or 0.
GWidget *CWindowManager::WidgetUnderCursor (int x, int y, CWindow **ppWindow)
{
	*ppWindow = 0;
	boolean bOnTitle = FALSE;
	unsigned nHit = HitTest (x, y, &bOnTitle);
	if (nHit == ~0u || bOnTitle)
	{
		return 0;
	}
	CWindow *pWin = m_pWindows[nHit];
	if (pWin->HitCloseBox (x, y))
	{
		return 0;
	}
	int cx = x - (pWin->X () + pWin->ChromeL ());
	int cy = y - (pWin->Y () + pWin->ChromeT ());
	GWidget *pW = pWin->HitWidget (cx, cy);
	if (pW != 0)
	{
		*ppWindow = pWin;
	}
	return pW;
}

// Push an event for a widget to its window (handler/sender/event/value).
static void PostWidgetEvent (CWindow *pWin, GWidget *pW, int nEvent, long lValue)
{
	if (pWin == 0 || pW == 0 || pW->ulHandler == 0)
	{
		return;
	}
	GUIEvent Ev;
	Ev.ulHandler = pW->ulHandler;
	Ev.ulSender  = (u64) pW;
	Ev.nEvent    = nEvent;
	Ev.lValue    = lValue;
	pWin->PushEvent (Ev);
}

// Set a value widget's value (0..100) from the cursor; fire on change. Vertical
// scrollbars track the cursor Y; sliders + horizontal scrollbars track X.
static void ValueFromCursor (CWindow *pWin, GWidget *pW, int x, int y)
{
	if (pWin == 0 || pW == 0)
	{
		return;
	}
	int nPos, nStart, nLen;
	if (pW->nType == GW_SCROLLV)
	{
		nStart = pWin->Y () + pWin->ChromeT () + pW->nY;
		nPos = y; nLen = pW->nH;
	}
	else	// GW_SLIDER, GW_SCROLLH
	{
		nStart = pWin->X () + pWin->ChromeL () + pW->nX;
		nPos = x; nLen = pW->nW;
	}
	if (nLen <= 1)
	{
		return;
	}
	int v = (nPos - nStart) * 100 / nLen;
	if (v < 0) v = 0; else if (v > 100) v = 100;
	if (v != pW->nState)
	{
		pW->nState = v;
		PostWidgetEvent (pWin, pW, GUI_EVENT_VALUE_CHANGED, v);
	}
}

static boolean IsValueWidget (const GWidget *pW)
{
	return pW->nType == GW_SLIDER || pW->nType == GW_SCROLLV
	    || pW->nType == GW_SCROLLH;
}

void CWindowManager::OnMouse (int x, int y, unsigned nButtons)
{
	// Clamp to the screen.
	if (x < 0) x = 0; else if (x >= g_nScreenWidth)  x = g_nScreenWidth - 1;
	if (y < 0) y = 0; else if (y >= g_nScreenHeight) y = g_nScreenHeight - 1;

	m_SpinLock.Acquire ();

	m_nCursorX = x;
	m_nCursorY = y;
	m_bCursorShown = TRUE;

	boolean bLeftNow = (nButtons & 1) != 0;
	boolean bLeftWas = (m_nLastButtons & 1) != 0;

	// --- hover tracking (interactive widgets only) -----------------------
	CWindow *pHoverWin = 0;
	GWidget *pHover = WidgetUnderCursor (x, y, &pHoverWin);
	if (pHover != 0 && (pHover->nType == GW_LABEL || pHover->nType == GW_PROGRESS))
	{
		pHover = 0;			// static widgets don't react
	}
	if (pHover != m_pHoverWidget)
	{
		if (m_pHoverWidget != 0) m_pHoverWidget->bMouseOver = FALSE;
		m_pHoverWidget = pHover;
		if (m_pHoverWidget != 0) m_pHoverWidget->bMouseOver = TRUE;
	}

	if (bLeftNow && !bLeftWas)
	{
		// Press edge: raise the window under the cursor, then dispatch.
		boolean bOnTitle = FALSE;
		unsigned nHit = HitTest (x, y, &bOnTitle);
		if (nHit == ~0u)
		{
			SetFocusWidget (0, 0);		// clicked the desktop -> unfocus
		}
		else
		{
			CWindow *pWin = m_pWindows[nHit];

			if (pWin->Dialog () != 0)
			{
				// Click inside a modal dialog: hand it to the kernel dialog.
				RaiseLocked (pWin);
				pWin->Dialog ()->OnClick (x - (pWin->X () + pWin->ChromeL ()),
							  y - (pWin->Y () + pWin->ChromeT ()));
				pWin->Dialog ()->Draw ();
			}
			else if (pWin->ModalChild () != 0)
			{
				// Owner of an open dialog: it is blocked -- don't deliver input,
				// just surface its dialog in front of it.
				RaiseLocked (pWin);
				RaiseLocked (pWin->ModalChild ());
			}
			else if (RaiseLocked (pWin), pWin->HitCloseBox (x, y))
			{
				pWin->RequestExit ();
			}
			else if (bOnTitle)
			{
				m_pDragWindow = pWin;
				m_nDragDX = x - pWin->X ();
				m_nDragDY = y - pWin->Y ();
			}
			else
			{
				int cx = x - (pWin->X () + pWin->ChromeL ());
				int cy = y - (pWin->Y () + pWin->ChromeT ());
				GWidget *pW = pWin->HitWidget (cx, cy);

				// Focus follows clicks: a textbox/textarea gains focus,
				// anything else (incl. empty area) clears it.
				boolean bEditable = pW != 0 && (pW->nType == GW_TEXTBOX
								|| pW->nType == GW_TEXTAREA);
				SetFocusWidget (bEditable ? pW : 0, bEditable ? pWin : 0);

				if (pW != 0 && (pW->nType == GW_BUTTON
						|| pW->nType == GW_CHECKBOX
						|| pW->nType == GW_ICON
						|| IsValueWidget (pW)))
				{
					pW->bMousePressed = TRUE;	// armed; fires on release
					m_pPressedWidget = pW;
					m_pPressedWindow = pWin;
					if (IsValueWidget (pW))
					{
						ValueFromCursor (pWin, pW, x, y);  // jump to click
					}
				}
				else if (pW == 0 && pWin->ClickHandler () != 0)
				{
					// No widget under the cursor: deliver a canvas click (client
					// coords) to the app for app-drawn mouse UIs (e.g. SameGame).
					GUIEvent Ev;
					Ev.ulHandler = pWin->ClickHandler ();
					Ev.ulSender  = 0;
					Ev.nEvent    = GUI_EVENT_CANVAS_CLICK;
					Ev.lValue    = ((long) 1 << 32) | ((long) cx << 16) | (cy & 0xFFFF);
					pWin->PushEvent (Ev);
				}
			}
		}
	}
	else if (bLeftNow && bLeftWas)
	{
		// Held: drag a window by its title bar, or drag a slider thumb.
		if (m_pDragWindow != 0)
		{
			m_pDragWindow->Move (x - m_nDragDX, y - m_nDragDY);
		}
		else if (m_pPressedWidget != 0 && IsValueWidget (m_pPressedWidget))
		{
			ValueFromCursor (m_pPressedWindow, m_pPressedWidget, x, y);
		}
	}
	else if (!bLeftNow && bLeftWas)
	{
		// Release edge: fire the pressed widget only if still over it.
		if (m_pPressedWidget != 0)
		{
			GWidget *pW = m_pPressedWidget;
			pW->bMousePressed = FALSE;
			if (pW->bMouseOver && pW->nType == GW_CHECKBOX)
			{
				pW->nState ^= 1;
				PostWidgetEvent (m_pPressedWindow, pW,
						 GUI_EVENT_CHECK_CHANGED, pW->nState);
			}
			else if (pW->bMouseOver && (pW->nType == GW_BUTTON
						    || pW->nType == GW_ICON))
			{
				PostWidgetEvent (m_pPressedWindow, pW, GUI_EVENT_CLICK, 0);
			}
			// slider already fired during press/drag
			m_pPressedWidget = 0;
			m_pPressedWindow = 0;
		}
		m_pDragWindow = 0;
	}

	// --- canvas mouse for app-drawn UIs: right-click + drag motion -----------
	// Delivered to the topmost window's click handler (no widget under the cursor),
	// alongside the left-press canvas click above. The app branches on the event.
	boolean bRightNow = (nButtons & 2) != 0;
	boolean bRightWas = (m_nLastButtons & 2) != 0;
	if (!m_pDragWindow && !m_pPressedWidget)
	{
		boolean bOnTitle = FALSE;
		unsigned nHit = HitTest (x, y, &bOnTitle);
		if (nHit != ~0u && !bOnTitle)
		{
			CWindow *pWin = m_pWindows[nHit];
			u64 ulCH = pWin->ClickHandler ();
			if (pWin->ModalChild () != 0 || pWin->Dialog () != 0) ulCH = 0;	// blocked owner / dialog
			int cx = x - (pWin->X () + pWin->ChromeL ());
			int cy = y - (pWin->Y () + pWin->ChromeT ());
			if (ulCH != 0 && !pWin->HitCloseBox (x, y) && pWin->HitWidget (cx, cy) == 0)
			{
				unsigned nBtn = (bLeftNow ? 1 : 0) | (bRightNow ? 2 : 0);
				long lValue = ((long) nBtn << 32) | ((long) cx << 16) | (cy & 0xFFFF);
				GUIEvent Ev;
				Ev.ulHandler = ulCH;
				Ev.ulSender  = 0;
				Ev.lValue    = lValue;
				if (bRightNow && !bRightWas)		// right-button press edge
				{
					Ev.nEvent = GUI_EVENT_CANVAS_CLICK;
					pWin->PushEvent (Ev);
				}
				else if ((bLeftNow || bRightNow)	// drag: a button held + moved
					 && (x != m_nPrevX || y != m_nPrevY))
				{
					Ev.nEvent = GUI_EVENT_CANVAS_MOTION;
					pWin->PushEvent (Ev);
				}
			}
		}
	}

	m_nPrevX = x;
	m_nPrevY = y;
	m_nLastButtons = nButtons;

	m_SpinLock.Release ();
}

// Caller holds m_SpinLock. Move keyboard focus to pW (a textbox) in pWin.
void CWindowManager::SetFocusWidget (GWidget *pW, CWindow *pWin)
{
	if (m_pFocusWidget == pW)
	{
		return;
	}
	if (m_pFocusWidget != 0)
	{
		m_pFocusWidget->bFocused = FALSE;
	}
	m_pFocusWidget = pW;
	m_pFocusWindow = pWin;
	if (m_pFocusWidget != 0)
	{
		m_pFocusWidget->bFocused = TRUE;
	}
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

void CWindowManager::OnKey (const char *pString)
{
	if (pString == 0)
	{
		return;
	}

	m_SpinLock.Acquire ();

	// A modal dialog (topmost window) takes all keys: Enter/Esc + (later) typing.
	CWindow *pTopWin = m_nWindows > 0 ? m_pWindows[m_nWindows - 1] : 0;
	if (pTopWin != 0 && pTopWin->Dialog () != 0)
	{
		const char *pk = pString; int kc;
		while ((kc = NextKey (&pk)) != 0) pTopWin->Dialog ()->OnKey (kc);
		pTopWin->Dialog ()->Draw ();
		m_SpinLock.Release ();
		return;
	}

	GWidget *pW = m_pFocusWidget;
	CWindow *pFWin = m_pFocusWindow;
	const char *p = pString;
	int code;

	if (pW != 0 && (pW->nType == GW_TEXTBOX || pW->nType == GW_TEXTAREA))
	{
		// Textbox edits its inline Label (single line); textarea edits its heap
		// buffer (multi-line: Enter inserts a newline). Special keys are ignored.
		boolean bMulti = (pW->nType == GW_TEXTAREA && pW->pText != 0);
		char    *pBuf  = bMulti ? pW->pText : pW->Label;
		unsigned nCap  = bMulti ? GW_AREA_CAP : GW_TEXT_MAX;

		boolean bChanged = FALSE;
		while ((code = NextKey (&p)) != 0)
		{
			unsigned nLen = 0;
			while (pBuf[nLen] != '\0') nLen++;

			if (code == KEY_BACKSPACE || code == 0x7F)
			{
				if (nLen > 0) { pBuf[nLen - 1] = '\0'; bChanged = TRUE; }
			}
			else if ((code == '\n' || code == KEY_ENTER) && bMulti)
			{
				if (nLen + 1 < nCap)
				{
					pBuf[nLen] = '\n'; pBuf[nLen + 1] = '\0'; bChanged = TRUE;
				}
			}
			else if (code >= ' ' && code < 0x7F)
			{
				if (nLen + 1 < nCap)
				{
					pBuf[nLen] = (char) code; pBuf[nLen + 1] = '\0'; bChanged = TRUE;
				}
			}
		}
		if (bChanged)
		{
			PostWidgetEvent (pFWin, pW, GUI_EVENT_TEXT_CHANGED, 0);
		}
	}
	else
	{
		// No focused editable widget: deliver keys to the topmost window's app-level
		// key handler (e.g. the text editor, which draws + edits its own text).
		CWindow *pTop = m_nWindows > 0 ? m_pWindows[m_nWindows - 1] : 0;
		u64 ulKeyHandler = pTop != 0 ? pTop->KeyHandler () : 0;
		if (pTop != 0 && ulKeyHandler != 0)
		{
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
	}

	m_SpinLock.Release ();
}
