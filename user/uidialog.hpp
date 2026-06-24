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

} // namespace ui

#endif // ONYX_UIDIALOG_HPP
