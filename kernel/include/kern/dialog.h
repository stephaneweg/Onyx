//
// dialog.h -- kernel-managed modal dialogs (MessageBox now; file dialogs later).
//
// A dialog is drawn + handled entirely by the kernel into its own borderless CWindow
// (the owning app is blocked in the kapi, so it can't pump). The window manager
// routes input to the active dialog and keeps it glued above its owner (app-modal).
//
#ifndef _kern_dialog_h
#define _kern_dialog_h

#include <kern/gui/gimage.h>
#include <circle/types.h>

#define DLG_MSGBOX	0
#define DLG_FOPEN	1
#define DLG_FSAVE	2

// MessageBox button sets (kept identical to user/kapi.h).
#define MB_OK		0
#define MB_OKCANCEL	1
#define MB_YESNO	2

class CWindow;

class CDialog
{
public:
	CDialog (int nType, const char *pTitle, const char *pText, int nButtons);

	void Attach (CWindow *pWin)	{ m_pWin = pWin; }
	CWindow *Window (void)		{ return m_pWin; }

	void Draw (void);			// render into the window canvas
	void OnClick (int cx, int cy);		// client-relative
	void OnKey (int nCode);

	boolean IsDone (void) const	{ return m_bDone; }
	int Result (void) const		{ return m_nResult; }

	static int Width (int nType)	{ return nType == DLG_MSGBOX ? 300 : 360; }
	static int Height (int nType)	{ return nType == DLG_MSGBOX ? 120 : 300; }

private:
	void LayoutButtons (void);

	int	 m_nType;
	char	 m_Title[64];
	char	 m_Text[256];
	int	 m_nButtons;
	int	 m_nBtn;			// number of buttons (1..3)
	char	 m_BtnLabel[3][12];
	int	 m_BtnVal[3];			// result each button yields
	int	 m_BtnX[3], m_BtnY, m_BtnW, m_BtnH;
	boolean	 m_bDone;
	int	 m_nResult;
	CWindow	*m_pWin;
};

#endif // _kern_dialog_h
