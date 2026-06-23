//
// debugcon.cpp -- see debugcon.h.
//
#include <kern/debugcon.h>
#include <kern/gui/gimage.h>
#include <circle/2dgraphics.h>
#include <circle/util.h>		// memcpy

static const u32 FB_BG	= 0x00000000;	// black
static const u32 FB_FG	= 0x0000FF00;	// green text

// ---- CFbConsole -------------------------------------------------------------

CFbConsole::CFbConsole (C2DGraphics *p2D)
:	m_p2D (p2D), m_nX (0), m_nY (0)
{
}

void CFbConsole::Clear (void)
{
	int nW = (int) m_p2D->GetWidth ();
	int nH = (int) m_p2D->GetHeight ();
	GImage Img ((u32 *) m_p2D->GetBuffer (), nW, nH);
	Img.Clear (FB_BG);
	m_nX = 0;
	m_nY = 0;
	m_p2D->UpdateDisplay ();
}

int CFbConsole::Write (const void *pBuffer, size_t nCount)
{
	int nW  = (int) m_p2D->GetWidth ();
	int nH  = (int) m_p2D->GetHeight ();
	int nFW = GImage::FontWidth ();
	int nFH = GImage::FontHeight ();
	u32 *pFb = (u32 *) m_p2D->GetBuffer ();
	GImage Img (pFb, nW, nH);

	const char *p = (const char *) pBuffer;
	for (size_t i = 0; i < nCount; i++)
	{
		char c = p[i];
		if (c == '\n')
		{
			m_nX = 0;
			m_nY += nFH;
		}
		else if (c == '\r')
		{
			m_nX = 0;
		}
		else
		{
			if (m_nX + nFW > nW)		// wrap
			{
				m_nX = 0;
				m_nY += nFH;
			}
			Img.DrawChar (m_nX, m_nY, c, FB_FG);
			m_nX += nFW;
		}

		if (m_nY + nFH > nH)			// scroll up one line
		{
			int nShift = nFH * nW;
			memcpy (pFb, pFb + nShift, (size_t) (nH - nFH) * nW * sizeof (u32));
			for (int y = nH - nFH; y < nH; y++)
			{
				for (int x = 0; x < nW; x++)
				{
					pFb[y * nW + x] = FB_BG;
				}
			}
			m_nY -= nFH;
		}
	}

	m_p2D->UpdateDisplay ();			// present immediately (no hidden buffer)
	return (int) nCount;
}

// ---- CLogSwitch -------------------------------------------------------------

CLogSwitch::CLogSwitch (void)
:	m_pNormal (0), m_pDebug (0), m_bDebug (FALSE)
{
}

int CLogSwitch::Write (const void *pBuffer, size_t nCount)
{
	CDevice *pTarget = (m_bDebug && m_pDebug != 0) ? m_pDebug : m_pNormal;
	if (pTarget != 0)
	{
		return pTarget->Write (pBuffer, nCount);
	}
	return (int) nCount;
}

// ---- module hooks -----------------------------------------------------------

static CLogSwitch	*s_pSwitch  = 0;
static CFbConsole	*s_pConsole = 0;
static volatile boolean	 s_bActive  = FALSE;

void DebugConsoleRegister (CLogSwitch *pSwitch, CFbConsole *pConsole)
{
	s_pSwitch  = pSwitch;
	s_pConsole = pConsole;
}

void DebugConsoleTakeover (void)
{
	if (s_bActive)
	{
		return;					// already in debug mode
	}
	s_bActive = TRUE;				// compositor stops presenting

	if (s_pConsole != 0)
	{
		s_pConsole->Clear ();
	}
	if (s_pSwitch != 0)
	{
		s_pSwitch->SwitchToDebug (s_pConsole);	// all logger output -> screen
	}
}

boolean DebugConsoleActive (void)
{
	return s_bActive;
}
