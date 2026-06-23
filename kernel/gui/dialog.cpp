//
// dialog.cpp -- kernel modal dialogs (see dialog.h). MessageBox for now.
//
#include <kern/dialog.h>
#include <kern/gui/window.h>

static void copystr (char *d, const char *s, unsigned cap)
{
	unsigned i = 0;
	if (s != 0) for (; s[i] != '\0' && i < cap - 1; i++) d[i] = s[i];
	d[i] = '\0';
}

CDialog::CDialog (int nType, const char *pTitle, const char *pText, int nButtons)
:	m_nType (nType), m_nButtons (nButtons), m_nBtn (0),
	m_BtnY (0), m_BtnW (72), m_BtnH (24), m_bDone (FALSE), m_nResult (0), m_pWin (0)
{
	copystr (m_Title, pTitle, sizeof m_Title);
	copystr (m_Text, pText, sizeof m_Text);

	if (nButtons == MB_YESNO)
	{
		m_nBtn = 2;
		copystr (m_BtnLabel[0], "Yes", 12); m_BtnVal[0] = 1;
		copystr (m_BtnLabel[1], "No", 12);  m_BtnVal[1] = 0;
	}
	else if (nButtons == MB_OKCANCEL)
	{
		m_nBtn = 2;
		copystr (m_BtnLabel[0], "OK", 12);     m_BtnVal[0] = 1;
		copystr (m_BtnLabel[1], "Cancel", 12); m_BtnVal[1] = 0;
	}
	else
	{
		m_nBtn = 1;
		copystr (m_BtnLabel[0], "OK", 12); m_BtnVal[0] = 1;
	}
}

void CDialog::LayoutButtons (void)
{
	GImage *g = m_pWin->Canvas ();
	int W = g->Width (), H = g->Height ();
	m_BtnY = H - m_BtnH - 12;
	int total = m_nBtn * m_BtnW + (m_nBtn - 1) * 12;
	int x0 = (W - total) / 2;
	for (int i = 0; i < m_nBtn; i++) m_BtnX[i] = x0 + i * (m_BtnW + 12);
}

void CDialog::Draw (void)
{
	GImage *g = m_pWin->Canvas ();
	int W = g->Width (), H = g->Height ();

	g->FillRectangle (0, 0, W - 1, H - 1, 0x00303a48);		// body
	g->FillRectangle (0, 0, W - 1, 18, 0x00415068);			// title bar
	g->DrawText (8, 3, m_Title, 0x00ffffff);
	g->DrawRectangle (0, 0, W - 1, H - 1, 0x00141820);		// outline

	int ty = 32; const char *p = m_Text; char line[80]; int li = 0;
	for (;;)
	{
		char c = *p;
		if (c == '\n' || c == '\0')
		{
			line[li] = '\0';
			g->DrawText (14, ty, line, 0x00e0e0e0);
			ty += GImage::FontHeight () + 2; li = 0;
			if (c == '\0') break;
			p++; continue;
		}
		if (li < (int) sizeof line - 1) line[li++] = c;
		p++;
	}

	LayoutButtons ();
	for (int i = 0; i < m_nBtn; i++)
	{
		g->FillRectangle (m_BtnX[i], m_BtnY, m_BtnX[i] + m_BtnW - 1, m_BtnY + m_BtnH - 1, 0x00586478);
		g->DrawRectangle (m_BtnX[i], m_BtnY, m_BtnX[i] + m_BtnW - 1, m_BtnY + m_BtnH - 1, 0x00141820);
		int tx = m_BtnX[i] + (m_BtnW - GImage::TextWidth (m_BtnLabel[i])) / 2;
		g->DrawText (tx, m_BtnY + (m_BtnH - GImage::FontHeight ()) / 2, m_BtnLabel[i], 0x00ffffff);
	}
}

void CDialog::OnClick (int cx, int cy)
{
	for (int i = 0; i < m_nBtn; i++)
		if (cx >= m_BtnX[i] && cx < m_BtnX[i] + m_BtnW && cy >= m_BtnY && cy < m_BtnY + m_BtnH)
		{
			m_nResult = m_BtnVal[i]; m_bDone = TRUE; return;
		}
}

void CDialog::OnKey (int nCode)
{
	if (nCode == 13) { m_nResult = m_nBtn ? m_BtnVal[0] : 1; m_bDone = TRUE; }	// Enter -> default
	else if (nCode == 27) { m_nResult = 0; m_bDone = TRUE; }			// Esc -> cancel
}
