//
// uidialog.hpp -- user-side modal dialogs for the uikit toolkit: message box / confirm
// (and the file selector, below). No kernel call: the dialog is drawn as an overlay on
// the app's own canvas and runs its own blocking event loop until dismissed, returning
// the result synchronously (like the old kapi_message_box / kapi_file_*). This is what
// lets the kernel CDialog code be removed.
//
// How it works: while modal, the kernel pointer/key handlers are swapped to a dispatcher
// that feeds the dialog's Ui; on dismissal they are swapped to forward to the app's Ui
// (passed in), and the app is marked dirty so it repaints over the overlay.
//
#ifndef ONYX_UIDIALOG_HPP
#define ONYX_UIDIALOG_HPP

#include "uikit.hpp"

namespace ui {

// ---- modal plumbing ----------------------------------------------------------
static Ui *g_modalUi = 0;		// receives events while a dialog is up
static Ui *g_restoreUi = 0;		// the app Ui input returns to afterwards
static volatile int g_dlgDone = 0;	// set by a button to end the modal loop
static int g_dlgResult = 0;

static void dlg_modal_ev   (unsigned long s, int e, long v) { if (g_modalUi)   g_modalUi->onEvent (s, e, v); }
static void dlg_restore_ev (unsigned long s, int e, long v) { if (g_restoreUi) g_restoreUi->onEvent (s, e, v); }

// Run `dlg` modal over `app`'s canvas until g_dlgDone; box[] is the dialog frame to paint
// each frame (so the app content behind stays put). `paint` draws the box + static text.
static inline void run_modal (Ui &app, Ui &dlg, void (*paint) (Ui &))
{
	g_dlgDone = 0; g_modalUi = &dlg;
	kapi_set_pointer_handler (dlg_modal_ev);
	kapi_set_key_handler (dlg_modal_ev);
	while (!g_dlgDone && !should_exit ())
	{
		pump_events ();
		paint (dlg);		// dialog box + labels
		dlg.drawAll ();		// interactive widgets on top
		present ();
		msleep (16);
	}
	g_restoreUi = &app;		// hand input back to the app
	kapi_set_pointer_handler (dlg_restore_ev);
	kapi_set_key_handler (dlg_restore_ev);
	app.markDirty ();
}

// ---- message box / confirm ---------------------------------------------------
static void dlg_yes (Widget &) { g_dlgResult = 1; g_dlgDone = 1; }
static void dlg_no  (Widget &) { g_dlgResult = 0; g_dlgDone = 1; }

// box geometry (shared between the layout + the paint callback) for the message box
static int g_mbX, g_mbY, g_mbW, g_mbH;
static const char *g_mbTitle, *g_mbText;

static void dlg_paint_box (Ui &d)
{
	d.fill  (g_mbX, g_mbY, g_mbW, g_mbH, d.col_face_dn);
	d.frame (g_mbX, g_mbY, g_mbW, g_mbH, d.col_accent);
	d.fill  (g_mbX, g_mbY, g_mbW, d.fh + 8, d.col_face);		// title bar
	kapi_draw_text (g_mbX + 8, g_mbY + 4, g_mbTitle, d.col_text);
	// message: one kapi_draw_text per '\n'-separated line
	const char *p = g_mbText; int line = 0;
	char buf[96];
	while (*p)
	{
		int n = 0; while (p[n] && p[n] != '\n' && n < 95) n++;
		for (int i = 0; i < n; i++) buf[i] = p[i];
		buf[n] = '\0';
		kapi_draw_text (g_mbX + 10, g_mbY + d.fh + 14 + line * (d.fh + 2), buf, d.col_text);
		p += n; if (*p == '\n') p++;
		line++;
	}
}

// Modal message box. buttons = MB_OK / MB_OKCANCEL / MB_YESNO (from kapi.h). Returns
// 1 (OK / Yes) or 0 (Cancel / No).
static inline int messagebox (Ui &app, const char *title, const char *text, int buttons)
{
	int W = app.W, H = app.H;
	g_mbW = 320; if (g_mbW > W - 20) g_mbW = W - 20;
	g_mbH = 130; if (g_mbH > H - 20) g_mbH = H - 20;
	g_mbX = (W - g_mbW) / 2; g_mbY = (H - g_mbH) / 2;
	g_mbTitle = title; g_mbText = text;

	Ui dlg (app.fb, W, H);
	int by = g_mbY + g_mbH - 38;
	if (buttons == MB_YESNO)
	{
		dlg.button (g_mbX + g_mbW - 180, by, 82, 28, "Yes", dlg_yes);
		dlg.button (g_mbX + g_mbW - 92,  by, 82, 28, "No",  dlg_no);
	}
	else if (buttons == MB_OKCANCEL)
	{
		dlg.button (g_mbX + g_mbW - 180, by, 82, 28, "OK",     dlg_yes);
		dlg.button (g_mbX + g_mbW - 92,  by, 82, 28, "Cancel", dlg_no);
	}
	else
	{
		dlg.button (g_mbX + g_mbW - 92, by, 82, 28, "OK", dlg_yes);
	}
	dlg.focus = 0;				// first button focused -> Enter confirms

	g_dlgResult = 0;
	run_modal (app, dlg, dlg_paint_box);
	return g_dlgResult;
}

static inline int confirm (Ui &app, const char *title, const char *text)
{ return messagebox (app, title, text, MB_YESNO); }

// ---- file selector -----------------------------------------------------------
#define UI_FD_MAX 128		// max directory entries listed

static char  g_fdDir[256];
static char  g_fdEnt[UI_FD_MAX][96];
static char  g_fdIsdir[UI_FD_MAX];
static int   g_fdCount, g_fdSel, g_fdTop, g_fdRows, g_fdRowH;
static int   g_fdLX, g_fdLY, g_fdLW, g_fdLH;		// list rect
static int   g_fdBX, g_fdBY, g_fdBW, g_fdBH;		// dialog box rect
static const char *g_fdTitle;
static char  g_fdResult[256];
static Ui       *g_fdDlg;
static Textbox  *g_fdNameBox;
static Scrollbar *g_fdSb;

static void fd_scopy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }
static int  fd_isDotDot (const char *s) { return s[0] == '.' && s[1] == '.' && s[2] == '\0'; }

static void fd_read (void)		// list g_fdDir into g_fdEnt[] (".." first), reset selection
{
	g_fdCount = 0; g_fdTop = 0; g_fdSel = -1;
	fd_scopy (g_fdEnt[0], "..", 96); g_fdIsdir[0] = 1; g_fdCount = 1;
	void *d = kapi_opendir (g_fdDir);
	if (d != 0)
	{
		struct kapi_dirent e;
		while (g_fdCount < UI_FD_MAX && kapi_readdir (d, &e))
		{
			fd_scopy (g_fdEnt[g_fdCount], e.name, 96);
			g_fdIsdir[g_fdCount] = e.is_dir ? 1 : 0;
			g_fdCount++;
		}
		kapi_closedir (d);
	}
}
static void fd_goUp (void)
{
	int n = 0; while (g_fdDir[n]) n++;
	if (n > 0 && g_fdDir[n - 1] == '/') n--;
	while (n > 0 && g_fdDir[n - 1] != '/') n--;
	if (n > 0) n--;
	g_fdDir[n] = '\0';
	if (n < 4) fd_scopy (g_fdDir, "SD:/", sizeof g_fdDir);		// clamp to root
}
static void fd_enter (const char *name)
{
	int n = 0; while (g_fdDir[n]) n++;
	if (!(n > 0 && g_fdDir[n - 1] == '/') && n < 255) g_fdDir[n++] = '/';
	for (int k = 0; name[k] && n < 255; k++) g_fdDir[n++] = name[k];
	g_fdDir[n] = '\0';
}
static void fd_click (int row)
{
	if (row < 0 || row >= g_fdCount) return;
	if (g_fdIsdir[row]) { if (fd_isDotDot (g_fdEnt[row])) fd_goUp (); else fd_enter (g_fdEnt[row]); fd_read (); }
	else { g_fdSel = row; g_fdNameBox->setText (g_fdEnt[row]); }
	g_fdDlg->markDirty ();
}
static void fd_ok (Widget &)
{
	const char *nm = g_fdNameBox->getText ();
	if (nm[0] == '\0') return;					// need a filename
	int n = 0; for (; g_fdDir[n] && n < 254; n++) g_fdResult[n] = g_fdDir[n];
	if (n > 0 && g_fdResult[n - 1] != '/' && n < 255) g_fdResult[n++] = '/';
	for (int k = 0; nm[k] && n < 255; k++) g_fdResult[n++] = nm[k];
	g_fdResult[n] = '\0';
	g_dlgResult = 1; g_dlgDone = 1;
}
static void fd_cancel (Widget &) { g_dlgResult = 0; g_dlgDone = 1; }
static void fd_scroll (Widget &w) { g_fdTop = ((Scrollbar &) w).value; g_fdDlg->markDirty (); }

static void fd_paint (Ui &d)
{
	d.fill  (g_fdBX, g_fdBY, g_fdBW, g_fdBH, d.col_face_dn);
	d.frame (g_fdBX, g_fdBY, g_fdBW, g_fdBH, d.col_accent);
	d.fill  (g_fdBX, g_fdBY, g_fdBW, d.fh + 8, d.col_face);
	kapi_draw_text (g_fdBX + 8, g_fdBY + 4, g_fdTitle, d.col_text);
	kapi_draw_text (g_fdBX + 10, g_fdBY + d.fh + 12, g_fdDir, d.col_dis);

	d.fill  (g_fdLX, g_fdLY, g_fdLW, g_fdLH, d.col_field);
	d.frame (g_fdLX, g_fdLY, g_fdLW, g_fdLH, d.col_border);
	for (int r = 0; r < g_fdRows; r++)
	{
		int idx = g_fdTop + r;
		if (idx >= g_fdCount) break;
		int ry = g_fdLY + 1 + r * g_fdRowH;
		if (idx == g_fdSel) d.fill (g_fdLX + 1, ry, g_fdLW - 2, g_fdRowH, d.col_face_hi);
		char row[100]; int k = 0;
		for (; g_fdEnt[idx][k] && k < 96; k++) row[k] = g_fdEnt[idx][k];
		if (g_fdIsdir[idx] && !fd_isDotDot (g_fdEnt[idx]) && k < 97) row[k++] = '/';
		row[k] = '\0';
		kapi_draw_text (g_fdLX + 4, ry, row, g_fdIsdir[idx] ? d.col_accent : d.col_text);
	}
}
static void fd_ev (unsigned long s, int e, long v)
{
	g_fdDlg->onEvent (s, e, v);			// filename box + buttons + scrollbar
	if (e == GUI_EVENT_PTR_DOWN && (GUI_PTR_CHANGED (v) & 1))
	{
		int x = GUI_PTR_X (v), y = GUI_PTR_Y (v);
		if (x >= g_fdLX && x < g_fdLX + g_fdLW && y >= g_fdLY && y < g_fdLY + g_fdLH)
			fd_click (g_fdTop + (y - g_fdLY - 1) / g_fdRowH);
	}
}

static inline int fd_run (Ui &app, char *out, unsigned cap, const char *startDir,
			  const char *defName, int save)
{
	fd_scopy (g_fdDir, (startDir && startDir[0]) ? startDir : "SD:/", sizeof g_fdDir);
	g_fdTitle = save ? "Save file" : "Open file";

	int W = app.W, H = app.H;
	int dw = 360; if (dw > W - 20) dw = W - 20;
	int dh = 300; if (dh > H - 20) dh = H - 20;
	g_fdBX = (W - dw) / 2; g_fdBY = (H - dh) / 2; g_fdBW = dw; g_fdBH = dh;

	Ui dlg (app.fb, W, H); g_fdDlg = &dlg;
	g_fdRowH = dlg.fh + 2;
	g_fdLX = g_fdBX + 10; g_fdLY = g_fdBY + dlg.fh + 24;
	g_fdLW = dw - 20 - 14; g_fdLH = dh - (g_fdLY - g_fdBY) - 78;
	g_fdRows = g_fdLH / g_fdRowH; if (g_fdRows < 1) g_fdRows = 1;

	g_fdSb = &dlg.scrollbar (g_fdLX + g_fdLW + 2, g_fdLY, 12, g_fdLH, true, 1, 0, fd_scroll);
	g_fdNameBox = &dlg.textbox (g_fdBX + 10, g_fdLY + g_fdLH + 8, dw - 20, dlg.fh + 8, defName ? defName : "");
	int by = g_fdBY + dh - 36;
	dlg.button (g_fdBX + dw - 180, by, 82, 28, save ? "Save" : "Open", fd_ok);
	dlg.button (g_fdBX + dw - 92,  by, 82, 28, "Cancel", fd_cancel);

	fd_read ();
	g_dlgDone = 0; g_dlgResult = 0;
	kapi_set_pointer_handler (fd_ev);
	kapi_set_key_handler (fd_ev);
	while (!g_dlgDone && !should_exit ())
	{
		pump_events ();
		int mt = g_fdCount - g_fdRows; if (mt < 1) mt = 1;	// reflect list scroll
		g_fdSb->vmax = mt; g_fdSb->value = g_fdTop;
		fd_paint (dlg);
		dlg.drawAll ();
		present ();
		msleep (16);
	}
	g_restoreUi = &app;
	kapi_set_pointer_handler (dlg_restore_ev);
	kapi_set_key_handler (dlg_restore_ev);
	app.markDirty ();

	if (g_dlgResult) { fd_scopy (out, g_fdResult, (int) cap); return 1; }
	return 0;
}

static inline int file_open (Ui &app, char *out, unsigned cap, const char *startDir)
{ return fd_run (app, out, cap, startDir, 0, 0); }
static inline int file_save (Ui &app, char *out, unsigned cap, const char *startDir, const char *defName)
{ return fd_run (app, out, cap, startDir, defName, 1); }

} // namespace ui

#endif // ONYX_UIDIALOG_HPP
