//
// window.cpp
//
#include <kern/gui/window.h>
#include <kern/layout.h>		// KPAGE_SIZE / KPAGE_MASK
#include <circle/util.h>		// memset
#include <circle/new.h>
#include <assert.h>

CWindow::CWindow (int x, int y, int nClientW, int nClientH, const char *pTitle)
:	m_nX (x), m_nY (y),
	m_pRawAlloc (0), m_ulCanvasPhys (0), m_nCanvasPages (0)
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

	// Client area = the owner's canvas, blitted opaque just below the title bar.
	pScreen->PutOther (&m_Canvas, x0 + WIN_BORDER, y0 + WIN_TITLEBAR_H, FALSE);

	// Outline.
	pScreen->DrawRectangle (x0, y0, x1, y1, 0x00000000);
}

CWindowManager *CWindowManager::s_pThis = 0;

CWindowManager::CWindowManager (void)
:	m_nWindows (0)
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
}
