//
// modeconf -- the Control Panel's Mode applet (applet_proto.h; alone, a window of its own): the interface's mode
// (docs/POCKETUI-TECH-STUDY.md section 8.4). Three cards with a picture: the desktop (Elegant: windows, the menu
// bar, the dock), pocket (PocketUI: one app at a time on a small screen), console (PocketUI's console mode: the
// games, a television, a pad) -- the one in use marked. Apply: "the open programs will be closed" asked, then the
// switch (SystemKit's session_switch_start: /bin/session switch closes the programs -- an unsaved document asked by
// its program --, writes SD:/etc/system.ini "shell =", has the kernel start the other graphics server and runs
// the other mode's session file; this applet and the Control Panel are ended with the old session). A program that
// does not close in time: "... is waiting for an answer" -- Wait, Force (it is ended), Cancel (nothing switched).
// The pictures: SD:/apps/modeconf.app/res/{desktop,pocket,console}.bmp (the design study's mock-ups made small:
// tools/screenshot/modeconf_previews.py).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <stdio.h>
#include <string.h>
#include "appkit/appkit.h"
#include "systemkit/systemkit.h"
#include "uikit/uikit.h"
#include "uikit/bmp.h"
#include "fontkit/uikitface.h"		// FreeType's text (DejaVu Sans) for every widget

using namespace uikit;

#define W	700
#define H	470
#define CARD_W	216
#define CARD_H	262
#define PIC_W	160
#define PIC_H	100
#define RES	"SD:/apps/modeconf.app/res/"

static const char *const PIC[3] = { RES "desktop.bmp", RES "pocket.bmp", RES "console.bmp" };
static const char *const NAME[3] = { TRN ("Desktop"), TRN ("Pocket"), TRN ("Console") };
static const char *const WHAT[3] =
{
	TRN ("Windows side by side, the menu bar and the dock: for a monitor, a keyboard and a mouse."),
	TRN ("One app at a time, full screen, on a small screen: for a handheld or a 7-inch display."),
	TRN ("The games first, on a television, with a gamepad; every app full screen."),
};

static int g_cur;			// the mode running (its server's)
static int g_sel;			// the card chosen
static void *g_proc;			// /bin/session switch, running
static int g_host;			// the Control Panel (an applet's host): kept open while the programs close
static Label *g_status;
static Button *g_apply;
static Widget *g_cards[3];

// ---- a card: the picture, the name, a line on it; a click chooses it -------------------------------------------
class ModeCard : public Widget
{
public:
	int mode; unsigned *pic; int pw, ph;
	ModeCard (int x, int y, int m) : Widget (x, y, CARD_W, CARD_H), mode (m), pic (0), pw (0), ph (0)
	{
		pic = ui::icon_load (PIC[m], &pw, &ph);
	}
	void onDraw () override
	{
		unsigned bg = parent ? parent->bgColor () : C_BG;
		canvas.clear (bg);
		bool sel = g_sel == mode;
		uk_rbox (canvas, 0, 0, width, height, 8, C_FACE_HI, C_FACE);
		uk_rline (canvas, 0, 0, width, height, 8, sel ? C_ACCENT : C_DIS, sel ? 255 : 120);
		if (sel) uk_rline (canvas, 1, 1, width - 2, height - 2, 7, C_ACCENT);
		int px = (width - PIC_W) / 2, py = 12;
		canvas.fillRect (px - 1, py - 1, PIC_W + 2, PIC_H + 2, C_DIS);
		if (pic)
			for (int y = 0; y < ph && y < PIC_H; y++)
				for (int x = 0; x < pw && x < PIC_W; x++) canvas.pixel (px + x, py + y, pic[y * pw + x]);
		else canvas.fillRect (px, py, PIC_W, PIC_H, C_BG);
		int y = py + PIC_H + 12;
		uk_text_c (canvas, 0, y, width, 20, TR (NAME[mode]), C_TEXT, 2);
		y += 22;
		if (mode == g_cur) uk_text_c (canvas, 0, y, width, 18, TR ("in use"), C_ACCENT);
		y += 22;
		const char *s = TR (WHAT[mode]);
		int st[6], ln[6];
		int n = uk_text_wrap (s, (int) strlen (s), width - 20, 5, st, ln);
		for (int k = 0; k < n; k++)
		{
			char line[160]; int l = ln[k] < 159 ? ln[k] : 159;
			memcpy (line, s + st[k], l); line[l] = 0;
			uk_text_c (canvas, 0, y + k * 18, width, 18, line, C_DIS);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (bl && mx >= 0 && my >= 0 && mx < width && my < height && g_proc == 0)
		{
			g_sel = mode;
			for (int k = 0; k < 3; k++) g_cards[k]->invalidate (true);
		}
		return true;
	}
};

// ---- a program that does not close: Wait / Force / Cancel ---------------------------------------------------
class WaitBox : public Modal
{
	char m_text[400];
public:
	explicit WaitBox (const char *text) : Modal (460, 170)
	{
		snprintf (m_text, sizeof m_text, "%s", text);
		Root *r = Root::current ();
		left = ((r ? r->width : W) - width) / 2; top = ((r ? r->height : H) - height) / 2;
		const char *lab[3] = { TR ("Wait"), TR ("Force"), TR ("Cancel") };
		for (int k = 0; k < 3; k++)
		{
			Button *b = new Button (width - 92 * (3 - k) - 8, height - 40, 86, 28, lab[k], btn);
			b->tag = k == 2 ? 0 : k + 1;
			addChild (b);
		}
	}
	static void btn (Widget &w) { if (w.parent) ((Modal *) w.parent)->onButton (w.tag); }
	void onButton (int tag) override { close (tag); }
	bool onKey (long k) override
	{
		if (k == 27) { close (0); return true; }
		if (k == KEY_ENTER) { close (1); return true; }
		return false;
	}
	void onDraw () override
	{
		drawBox (TR ("Mode"));
		int st[6], ln[6], n = uk_text_wrap (m_text, (int) strlen (m_text), width - 32, 5, st, ln);
		for (int k = 0; k < n; k++)
		{
			char line[200]; int l = ln[k] < 199 ? ln[k] : 199;
			memcpy (line, m_text + st[k], l); line[l] = 0;
			uk_text (canvas, 16, titleH () + 14 + k * 20, line, C_TEXT);
		}
	}
};

static void start_switch (int flags)
{
	g_proc = session_switch_start (g_sel, flags, g_host);
	if (g_proc == 0) { g_status->setText (TR ("The session's tool (SD:/bin/session) could not be started.")); return; }
	g_apply->disabled = true; g_apply->invalidate (true);
	g_status->setText (TR ("Closing the open programs..."));
}

static void on_apply (Widget &)
{
	if (g_proc) return;
	if (g_sel == g_cur)
	{
		if (session_mode () != g_sel) session_set_mode (g_sel);
		g_status->setText (TR ("This interface is the one in use."));
		return;
	}
	char q[300];
	snprintf (q, sizeof q, "%s: %s\n\n%s", TR ("The interface"), TR (NAME[g_sel]),
		  TR ("The open programs will be closed, then the interface starts again."));
	if (uk_messagebox (TR ("Mode"), q, MB_OKCANCEL) != 1) return;
	start_switch (0);
}

// /bin/session switch's answer (when this applet is still there to see it: else it was ended with the old session)
static void switch_done (int r)
{
	g_apply->disabled = false; g_apply->invalidate (true);
	if (r == SESSION_WAITING_APPS)
	{
		static char list[1024];
		int n = session_waiting (list, sizeof list);
		char who[64] = "";
		if (n > 0)
		{
			const char *t = strchr (list, '\t');
			const char *e = t ? strchr (t + 1, '\n') : 0;
			if (t && t[1] && t[1] != '\n') snprintf (who, sizeof who, "%.*s", (int) ((e ? e : t + 1 + strlen (t + 1)) - t - 1), t + 1);
			else snprintf (who, sizeof who, "%.*s", (int) (t ? t - list : (int) strlen (list)), list);
		}
		char text[400];
		if (n > 1) snprintf (text, sizeof text, "%s %s (+%d).\n%s", who, TR ("is waiting for an answer"), n - 1,
				     TR ("Answer its question, or force it to end (its unsaved work is lost)."));
		else snprintf (text, sizeof text, "%s %s.\n%s", who[0] ? who : TR ("A program"), TR ("is waiting for an answer"),
			       TR ("Answer its question, or force it to end (its unsaved work is lost)."));
		WaitBox box (text);
		int a = box.run ();
		if (a == 1) start_switch (SESSION_NO_ASK);
		else if (a == 2) start_switch (SESSION_NO_ASK | SESSION_FORCE);
		else g_status->setText (TR ("Nothing changed: the interface stays as it is."));
		return;
	}
	if (r == SESSION_SWITCHED) g_status->setText (TR ("The interface was switched."));
	else if (r == SESSION_FELL_BACK) g_status->setText (TR ("That interface could not start: the desktop instead."));
	else if (r == SESSION_REFUSED) g_status->setText (TR ("Not now: a full-screen program has the display."));
	else g_status->setText (TR ("The interface could not be switched."));
}

class ModeRoot : public Root
{
public:
	ModeRoot () : Root (W, H, TR ("Mode")) {}
	void onTick () override
	{
		if (g_proc && kapi_proc_done (g_proc))
		{
			int r = kapi_wait (g_proc);
			g_proc = 0;
			switch_done (r);
		}
	}
};

// The running server's mode (its UIKit asked); none: the one chosen.
static int running_mode (void)
{
	struct uk_win_server_info si;
	memset (&si, 0, sizeof si);
	si.size = sizeof si;
	if (uk_win_server (&si) > 0 && si.mode >= 0 && si.mode < 3) return si.mode;
	return session_mode ();
}

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);		// (before the widgets; false: the bitmap font)
	uk_lang_init ();
	if (uk_applet ())				// "--applet <surface> <host pid>": the host kept open meanwhile
	{
		char a[96]; kapi_get_args (a, sizeof a);
		int s = 0, h = 0;
		if (sscanf (a, "--applet %d %d", &s, &h) == 2) g_host = h;
	}
	ModeRoot root;
	if (root.canvas.px == 0) return 1;
	g_cur = running_mode ();
	g_sel = g_cur;
	int X = root.width > W ? (root.width - W) / 2 : 0;

	GroupBox *gb = new GroupBox (X + 10, 8, W - 20, CARD_H + 44, TR ("The interface"));
	root.addChild (gb);
	int ct = gb->contentTop () + 6;
	int gap = (W - 20 - 3 * CARD_W) / 4;
	for (int m = 0; m < 3; m++)
	{
		g_cards[m] = new ModeCard (gap + m * (CARD_W + gap), ct, m);
		gb->addChild (g_cards[m]);
	}
	int y = 8 + CARD_H + 54;
	g_apply = new Button (X + W - 170, y, 150, 30, TR ("Apply"), on_apply);
	root.addChild (g_apply);
	root.addChild (new Label (X + 12, y, W - 200, 20, TR ("Switching closes the open programs:"), C_TEXT, root.bg));
	root.addChild (new Label (X + 12, y + 20, W - 200, 20, TR ("each one asks about its unsaved work."), C_TEXT, root.bg));
	g_status = new Label (X + 12, y + 48, W - 24, 22, "", C_TEXT, root.bg);
	root.addChild (g_status);
	root.addChild (new Label (X + 12, H - 26, W - 24, 20, TR ("Kept in SD:/etc/system.ini (shell=); the programs of each mode: SD:/etc/session."), C_DIS, root.bg));
	root.run ();
	return 0;
}
