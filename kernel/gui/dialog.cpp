//
// dialog.cpp -- kernel modal dialogs (see dialog.h): MessageBox + file Open/Save.
//
#include <kern/dialog.h>
#include <kern/gui/window.h>
#include <fatfs/ff.h>

static void copystr (char *d, const char *s, unsigned cap)
{
	unsigned i = 0;
	if (s != 0) for (; s[i] != '\0' && i < cap - 1; i++) d[i] = s[i];
	d[i] = '\0';
}
static int dlen (const char *s) { int n = 0; while (s[n]) n++; return n; }

CDialog::CDialog (int nType, const char *pTitle, const char *pText, int nButtons)
:	m_nType (nType), m_nButtons (nButtons), m_nBtn (0),
	m_BtnY (0), m_BtnW (72), m_BtnH (24), m_bDone (FALSE), m_nResult (0), m_pWin (0),
	m_nEnt (0), m_Sel (0), m_Top (0), m_VisRows (1), m_ListTop (38), m_RowH (18), m_FnLen (0)
{
	copystr (m_Title, pTitle, sizeof m_Title);
	copystr (m_Text, pText, sizeof m_Text);
	m_Dir[0] = '\0'; m_Fname[0] = '\0'; m_Path[0] = '\0';

	if (nType == DLG_MSGBOX)
	{
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
		else { m_nBtn = 1; copystr (m_BtnLabel[0], "OK", 12); m_BtnVal[0] = 1; }
	}
}

void CDialog::InitFile (const char *pStartDir, const char *pDefName)
{
	copystr (m_Dir, (pStartDir != 0 && pStartDir[0]) ? pStartDir : "SD:/", sizeof m_Dir);
	copystr (m_Fname, pDefName != 0 ? pDefName : "", sizeof m_Fname);
	m_FnLen = dlen (m_Fname);
	m_nBtn = 2;
	copystr (m_BtnLabel[0], m_nType == DLG_FSAVE ? "Save" : "Open", 12); m_BtnVal[0] = 1;
	copystr (m_BtnLabel[1], "Cancel", 12); m_BtnVal[1] = 0;
	ReadDir ();
}

void CDialog::ReadDir (void)
{
	m_nEnt = 0; m_Sel = 0; m_Top = 0;
	int root = (m_Dir[0] == 'S' && m_Dir[1] == 'D' && m_Dir[2] == ':'
		    && (m_Dir[3] == '\0' || (m_Dir[3] == '/' && m_Dir[4] == '\0')));
	if (!root) { copystr (m_Ent[m_nEnt], "..", 48); m_EntDir[m_nEnt] = 1; m_nEnt++; }

	for (int pass = 0; pass < 2; pass++)		// directories first, then files
	{
		DIR Dir;
		if (f_opendir (&Dir, m_Dir) != FR_OK) break;
		FILINFO Info;
		while (m_nEnt < 64 && f_readdir (&Dir, &Info) == FR_OK && Info.fname[0] != '\0')
		{
			int isdir = (Info.fattrib & AM_DIR) ? 1 : 0;
			if ((pass == 0) != (isdir != 0)) continue;
			copystr (m_Ent[m_nEnt], Info.fname, 48);
			m_EntDir[m_nEnt] = (char) isdir;
			m_nEnt++;
		}
		f_closedir (&Dir);
	}
}

void CDialog::EnterDir (const char *pName)
{
	if (pName[0] == '.' && pName[1] == '.')		// up
	{
		int n = dlen (m_Dir);
		if (n > 0 && m_Dir[n - 1] == '/') n--;
		while (n > 0 && m_Dir[n - 1] != '/') n--;
		if (n > 0) n--;
		m_Dir[n] = '\0';
		if (dlen (m_Dir) <= 3) copystr (m_Dir, "SD:/", sizeof m_Dir);
	}
	else
	{
		int n = dlen (m_Dir);
		if (!(n > 0 && m_Dir[n - 1] == '/')) { if (n < 255) m_Dir[n++] = '/'; }
		for (int k = 0; pName[k] && n < 255; k++) m_Dir[n++] = pName[k];
		m_Dir[n] = '\0';
	}
	ReadDir ();
}

void CDialog::BuildPath (const char *pName, char *pOut, int nCap)
{
	int p = 0;
	for (; m_Dir[p] && p < nCap - 1; p++) pOut[p] = m_Dir[p];
	if (p > 0 && pOut[p - 1] != '/' && p < nCap - 1) pOut[p++] = '/';
	for (int k = 0; pName[k] && p < nCap - 1; k++) pOut[p++] = pName[k];
	pOut[p] = '\0';
}

void CDialog::Confirm (void)
{
	if (m_nType == DLG_FSAVE)
	{
		if (m_FnLen > 0) { BuildPath (m_Fname, m_Path, sizeof m_Path); m_nResult = 1; m_bDone = TRUE; }
	}
	else	// DLG_FOPEN: a file must be selected
	{
		if (m_Sel >= 0 && m_Sel < m_nEnt && !m_EntDir[m_Sel])
		{ BuildPath (m_Ent[m_Sel], m_Path, sizeof m_Path); m_nResult = 1; m_bDone = TRUE; }
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

static void DrawButton (GImage *g, int x, int y, int w, int h, const char *label)
{
	g->FillRectangle (x, y, x + w - 1, y + h - 1, 0x00586478);
	g->DrawRectangle (x, y, x + w - 1, y + h - 1, 0x00141820);
	int tx = x + (w - GImage::TextWidth (label)) / 2;
	g->DrawText (tx, y + (h - GImage::FontHeight ()) / 2, label, 0x00ffffff);
}

void CDialog::DrawMsgBox (void)
{
	GImage *g = m_pWin->Canvas ();
	int W = g->Width (), H = g->Height ();
	g->FillRectangle (0, 0, W - 1, H - 1, 0x00303a48);
	g->FillRectangle (0, 0, W - 1, 18, 0x00415068);
	g->DrawText (8, 3, m_Title, 0x00ffffff);
	g->DrawRectangle (0, 0, W - 1, H - 1, 0x00141820);

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
	for (int i = 0; i < m_nBtn; i++) DrawButton (g, m_BtnX[i], m_BtnY, m_BtnW, m_BtnH, m_BtnLabel[i]);
}

void CDialog::DrawFile (void)
{
	GImage *g = m_pWin->Canvas ();
	int W = g->Width (), H = g->Height ();
	g->FillRectangle (0, 0, W - 1, H - 1, 0x00303a48);
	g->FillRectangle (0, 0, W - 1, 18, 0x00415068);
	g->DrawText (8, 3, m_Title, 0x00ffffff);
	g->DrawRectangle (0, 0, W - 1, H - 1, 0x00141820);
	g->DrawText (8, 22, m_Dir, 0x0090b0d0);

	m_RowH = GImage::FontHeight () + 2; m_ListTop = 38;
	int listH = H - m_ListTop - 56;
	m_VisRows = listH / m_RowH; if (m_VisRows < 1) m_VisRows = 1;
	g->FillRectangle (8, m_ListTop, W - 9, m_ListTop + listH - 1, 0x00202830);

	if (m_Sel < m_Top) m_Top = m_Sel;
	if (m_Sel >= m_Top + m_VisRows) m_Top = m_Sel - m_VisRows + 1;
	if (m_Top < 0) m_Top = 0;
	for (int r = 0; r < m_VisRows; r++)
	{
		int idx = m_Top + r; if (idx >= m_nEnt) break;
		int y = m_ListTop + r * m_RowH;
		if (idx == m_Sel) g->FillRectangle (8, y, W - 9, y + m_RowH - 1, 0x00355070);
		char ln[52]; int p = 0;
		if (m_EntDir[idx]) ln[p++] = '[';
		for (int k = 0; m_Ent[idx][k] && p < 50; k++) ln[p++] = m_Ent[idx][k];
		if (m_EntDir[idx]) ln[p++] = ']';
		ln[p] = '\0';
		g->DrawText (12, y + 1, ln, m_EntDir[idx] ? 0x0080c8ff : 0x00d8d8d8);
	}

	if (m_nType == DLG_FSAVE)			// editable filename
	{
		int fy = H - 52;
		g->DrawText (8, fy, "name:", 0x0090a0b0);
		g->FillRectangle (50, fy - 2, W - 12, fy + 14, 0x00101418);
		g->DrawText (54, fy, m_Fname, 0x00ffffff);
		int cx = 54 + m_FnLen * GImage::FontWidth ();
		g->DrawLine (cx, fy, cx, fy + GImage::FontHeight (), 0x0060ff90);
	}

	LayoutButtons ();
	for (int i = 0; i < m_nBtn; i++) DrawButton (g, m_BtnX[i], m_BtnY, m_BtnW, m_BtnH, m_BtnLabel[i]);
}

void CDialog::Draw (void)
{
	if (m_nType == DLG_MSGBOX) DrawMsgBox (); else DrawFile ();
}

void CDialog::OnClick (int cx, int cy)
{
	// Buttons (both kinds).
	for (int i = 0; i < m_nBtn; i++)
		if (cx >= m_BtnX[i] && cx < m_BtnX[i] + m_BtnW && cy >= m_BtnY && cy < m_BtnY + m_BtnH)
		{
			if (m_nType == DLG_MSGBOX || m_BtnVal[i] == 0)	// any msgbox button, or Cancel
			{ m_nResult = m_BtnVal[i]; m_bDone = TRUE; }
			else Confirm ();				// Open / Save
			return;
		}

	if (m_nType == DLG_MSGBOX) return;

	// File list.
	if (cy >= m_ListTop && cy < m_ListTop + m_VisRows * m_RowH)
	{
		int idx = m_Top + (cy - m_ListTop) / m_RowH;
		if (idx >= 0 && idx < m_nEnt)
		{
			if (m_EntDir[idx]) EnterDir (m_Ent[idx]);
			else
			{
				m_Sel = idx;
				if (m_nType == DLG_FSAVE) { copystr (m_Fname, m_Ent[idx], sizeof m_Fname); m_FnLen = dlen (m_Fname); }
			}
		}
	}
}

void CDialog::OnKey (int nCode)
{
	if (m_nType == DLG_MSGBOX)
	{
		if (nCode == 13) { m_nResult = m_nBtn ? m_BtnVal[0] : 1; m_bDone = TRUE; }
		else if (nCode == 27) { m_nResult = 0; m_bDone = TRUE; }
		return;
	}

	if (nCode == 27) { m_nResult = 0; m_bDone = TRUE; }		// Esc
	else if (nCode == 13) Confirm ();				// Enter
	else if (nCode == KEY_UP) { if (m_Sel > 0) m_Sel--; }
	else if (nCode == KEY_DOWN) { if (m_Sel < m_nEnt - 1) m_Sel++; }
	else if (m_nType == DLG_FSAVE)
	{
		if (nCode == 8 && m_FnLen > 0) m_Fname[--m_FnLen] = '\0';
		else if (nCode >= ' ' && nCode < 0x7f && m_FnLen < (int) sizeof m_Fname - 1)
		{ m_Fname[m_FnLen++] = (char) nCode; m_Fname[m_FnLen] = '\0'; }
	}
}
