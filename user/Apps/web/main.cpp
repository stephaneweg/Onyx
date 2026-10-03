//
// web/main.cpp -- Web, the WebKit browser's window (roadmap step 1 of docs/08-WEBKIT-PORT.md): the
// toolbar (back, forward, reload / stop, home, the address), the page, the status bar; the menus in the
// Onyx menu bar. One page per window -- no tabs (the user's choice): a link that asks for a new window
// (target=_blank, window.open) starts this program again on it (the kernel shares the program's image).
//
//   web [url]        (no url: the start page, SD:/apps/web.app/start.html)
//
// The page is the engine's (engine.h): WebKit (engine_webkit.cpp) on the Pi, a picture in the desktop
// simulator (engine_mock.cpp). What happens is written to the kernel log (`kmsg`): "web: ..." lines.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see LICENSE).
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "wtk/toolbar.h"
#include "wtk/paint.h"
#include "wtk/skin.h"
#include "clipboard.h"
#include "engine.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

int downloads_main ();				// downloads.cpp: the downloads' window (its own process)

using namespace wtk;

#define W0	1000		// the first size (the window fits the work area)
#define H0	700
#define TB_H	40		// the toolbar
#define SB_H	22		// the status bar
#define HOME	"file:///apps/web.app/start.html"

static void say (const char *s)		// (one write: one line of the kernel log)
{
	char b[600];
	int n = snprintf (b, sizeof b, "web: %s\n", s);
	if (n > (int) sizeof b - 1) n = (int) sizeof b - 1;
	fwrite (b, 1, (size_t) n, stderr);
	fflush (stderr);
}

// ---- the page -------------------------------------------------------------------------------------------

static void context_menu (int x, int y);

// The page area: the engine paints into its canvas; the pointer and the keys go to the engine.
class PageView : public Widget
{
public:
	int dx, dy, dw, dh;			// what changed since the last paint (dw 0: nothing)
	int buttons;				// the pointer's buttons held, as last seen

	PageView (int l, int t, int w, int h) : Widget (l, t, w, h), dx (0), dy (0), dw (w), dh (h), buttons (0)
	{
		canFocus = true;
		anchor = ANCHOR_FILL;
	}
	void damage (int x, int y, int w, int h)
	{
		if (dw == 0) { dx = x; dy = y; dw = w; dh = h; }
		else
		{
			int x1 = dx + dw > x + w ? dx + dw : x + w, y1 = dy + dh > y + h ? dy + dh : y + h;
			dx = dx < x ? dx : x; dy = dy < y ? dy : y;
			dw = x1 - dx; dh = y1 - dy;
		}
		invalidate (true);
	}
	void onDraw () override
	{
		if (canvas.px == 0) return;
		if (dw == 0) { dx = 0; dy = 0; dw = width; dh = height; }	// (a recomposite: all of it)
		int x = dx < 0 ? 0 : dx, y = dy < 0 ? 0 : dy;
		int w = dx + dw > width ? width - x : dx + dw - x, h = dy + dh > height ? height - y : dy + dh - y;
		if (w > 0 && h > 0) engine_paint (canvas.px, canvas.stride, x, y, w, h);
		dw = 0;
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		int b = (bl ? 1 : 0) | (br ? 2 : 0) | (bm ? 4 : 0);
		unsigned mods = kapi_get_modifiers ();
		if (wheel) engine_wheel (mx, my, wheel, mods);
		int changed = b ^ buttons;
		if ((changed & 2) && (b & 2))				// a right click: the browser's menu
		{
			buttons = b;
			setFocus ();
			context_menu (mx, my);
			return true;
		}
		if (changed & 2) { buttons = b; return true; }		// (its release)
		if (changed)
		{
			if (b & ~buttons) setFocus ();
			for (int bit = 1; bit <= 4; bit <<= 1)		// one event a button
				if (changed & bit) engine_mouse (mx, my, (buttons ^ bit) & 7, bit, mods), buttons ^= bit;
		}
		else if (!wheel) engine_mouse (mx, my, b, 0, mods);
		buttons = b;
		return true;
	}
	bool onKey (long k) override
	{
		engine_key (k, kapi_get_modifiers ());
		return true;
	}
};

// ---- the window ------------------------------------------------------------------------------------------

// The address field: a click into it (when it has not the keyboard yet) empties it for a new address, as
// a browser selects it all; Esc gives the page's address back and the keyboard back to the page.
static char g_pageUrl[Textbox::TEXT_CAP];
static void focus_page ();
class UrlField : public Textbox
{
public:
	UrlField (int l, int t, int w, int h, Action enter) : Textbox (l, t, w, h, "", enter) {}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		if (bl && !hasFocus) { setText (""); caret = 0; }
		return Textbox::onMouse (mx, my, bl, br, bm, wheel);
	}
	bool onKey (long k) override
	{
		if (k == 27) { setText (g_pageUrl); focus_page (); return true; }
		return Textbox::onKey (k);
	}
};

static PageView   *g_page;
static UrlField   *g_url;
static Label      *g_status;
static ToolButton *g_back, *g_fwd, *g_reload;
static char        g_title[256];
static char        g_statusText[512];
static bool        g_loading;
static double      g_progress;
static char        g_hover[512];

static void glyph (Canvas &cv, int id, int x, int y, int s, unsigned ink, bool off)
{
	wk_glyph (cv, id, x + s / 2, y + s / 2, s, off ? wk_mix (ink, C_BG, 170) : ink);
}

static void draw_title ();
static void show_pending_popup ();
static void downloads_tick ();

class WebRoot : public Root
{
public:
	WebRoot () : Root (W0, H0, "Web") {}
	void onTick () override
	{
		engine_cycle ();
		show_pending_popup ();					// (a <select>: shown outside the engine's call)
		downloads_tick ();
	}
	void onResized () override
	{
		engine_resize (g_page->width, g_page->height);
		g_page->damage (0, 0, g_page->width, g_page->height);
		draw_title ();
	}
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		if (type == DND_TEXT) engine_load (data);
		else if (type == DND_FILES)
		{
			char p[512]; int n = 0;
			while (data[n] && data[n] != '\n' && n < (int) sizeof p - 1) { p[n] = data[n]; n++; }
			p[n] = 0;
			engine_load (p);
		}
	}
};
static WebRoot *g_root;

// The window's title: the page's (the frame is drawn by the app: wtk/skin.cpp -- drawn again here with
// the page's title over the kernel's, which stays the program's).
static void draw_title ()
{
	struct kapi_chrome c;
	if (!kapi_get_chrome (&c) || !c.active) return;
	const char *t = g_title[0] ? g_title : "Web";
	wk_draw_frame (c.active, c.chrome_w, c.chrome_h, c.inset_t, t, C_FRAME_ACTIVE, true);
	if (c.inactive) wk_draw_frame (c.inactive, c.chrome_w, c.chrome_h, c.inset_t, t, C_FRAME_INACTIVE, false);
	g_root->invalidate (false);
}

static char g_dlText[256];		// the downloads' line (as Jet: their progress in the status bar)

static void show_status ()
{
	char b[512];
	if (g_hover[0]) snprintf (b, sizeof b, "%s", g_hover);
	else if (g_loading) snprintf (b, sizeof b, "Loading... %d %%", (int) (g_progress * 100));
	else if (g_dlText[0]) snprintf (b, sizeof b, "%s", g_dlText);
	else snprintf (b, sizeof b, "%s", g_statusText);
	g_status->setText (b);
}

// ---- the <select> lists: a scrolling list under the box (wtk's PopupMenu holds 16 items) -------------------

enum { POP_MAX = 512, POP_ROWS = 14 };
static char          g_popText[64 * 1024];
static const char   *g_popItem[POP_MAX];
static unsigned char g_popFlag[POP_MAX];
static int           g_popN, g_popSel, g_popX, g_popY, g_popW, g_popH;
static bool          g_popPending;

class ListPopup : public Modal
{
	int m_rowH, m_top, m_hot, m_rows;
	bool m_wasDown;
public:
	ListPopup (int x, int y, int w, int rows, int rowH)
	  : Modal (w, rows * rowH + 8), m_rowH (rowH), m_top (0), m_hot (g_popSel), m_rows (rows), m_wasDown (true)
	{
		left = x; top = y; transparent = true;
		if (m_hot >= m_rows) m_top = m_hot - m_rows / 2;
		if (m_top > g_popN - m_rows) m_top = g_popN - m_rows;
		if (m_top < 0) m_top = 0;
	}
	bool choosable (int i) const
	{
		return i >= 0 && i < g_popN && (g_popFlag[i] & ENGINE_ITEM_ENABLED) && !(g_popFlag[i] & (ENGINE_ITEM_SEPARATOR | ENGINE_ITEM_LABEL));
	}
	void onDraw () override
	{
		canvas.clear (WK_TRANSPARENT_KEY);
		wk_popup (canvas, 0, 0, width, height, 6, C_FIELD);
		for (int r = 0; r < m_rows && m_top + r < g_popN; r++)
		{
			int i = m_top + r, y = 4 + r * m_rowH;
			if (g_popFlag[i] & ENGINE_ITEM_SEPARATOR) { wk_etch_h (canvas, 8, y + m_rowH / 2, width - 16, C_FIELD); continue; }
			bool label = g_popFlag[i] & ENGINE_ITEM_LABEL;
			if (i == m_hot && choosable (i)) wk_hilite (canvas, 4, y, width - 8, m_rowH, 4);
			unsigned ink = i == m_hot && choosable (i) ? wk_hilite_ink () : choosable (i) || label ? C_FIELD_TEXT : C_DIS;
			wk_text_l (canvas, label ? 8 : 16, y, m_rowH, g_popItem[i], ink, label ? 2 : 0);
		}
		if (g_popN > m_rows)						// (where the view is in the list)
		{
			int th = (height - 8) * m_rows / g_popN, ty = 4 + (height - 8 - th) * m_top / (g_popN - m_rows);
			canvas.fillRect (width - 5, ty, 3, th, C_DIS);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel)
		{
			m_top -= wheel * 3;
			if (m_top > g_popN - m_rows) m_top = g_popN - m_rows;
			if (m_top < 0) m_top = 0;
			invalidate (true);
			return true;
		}
		bool inside = mx >= 0 && my >= 0 && mx < width && my < height;
		int i = inside ? m_top + (my - 4) / m_rowH : -1;
		if (i != m_hot && inside) { m_hot = i; invalidate (true); }
		if (bl && !m_wasDown)
		{
			if (!inside) close (0);
			else if (choosable (i)) close (i + 1);
		}
		m_wasDown = bl;
		return true;
	}
	bool onKey (long k) override
	{
		int i = m_hot;
		if (k == 27) { close (0); return true; }
		if (k == KEY_ENTER) { if (choosable (m_hot)) close (m_hot + 1); return true; }
		int step = k == KEY_DOWN ? 1 : k == KEY_UP ? -1 : k == KEY_PGDN ? m_rows : k == KEY_PGUP ? -m_rows
			 : k == KEY_HOME ? -POP_MAX : k == KEY_END ? POP_MAX : 0;
		if (!step) return true;
		int dir = step > 0 ? 1 : -1;
		i += step;
		if (i < 0) i = 0;
		if (i >= g_popN) i = g_popN - 1;
		while (i >= 0 && i < g_popN && !choosable (i)) i += dir;
		if (!choosable (i)) return true;
		m_hot = i;
		if (m_hot < m_top) m_top = m_hot;
		if (m_hot >= m_top + m_rows) m_top = m_hot - m_rows + 1;
		invalidate (true);
		return true;
	}
};

static void on_show_popup (const char *const *items, const unsigned char *flags, int count, int selected, int x, int y, int w, int h)
{
	int used = 0;
	g_popN = 0;
	for (int i = 0; i < count && i < POP_MAX; i++)
	{
		int l = (int) strlen (items[i]);
		if (used + l + 1 > (int) sizeof g_popText) break;
		memcpy (g_popText + used, items[i], (size_t) l + 1);
		g_popItem[i] = g_popText + used;
		g_popFlag[i] = flags[i];
		used += l + 1;
		g_popN = i + 1;
	}
	g_popSel = selected;
	g_popX = x; g_popY = y; g_popW = w; g_popH = h;
	g_popPending = g_popN > 0;
	if (!g_popPending) engine_popup_select (-1);
}

static void on_hide_popup () { g_popPending = false; }

static void show_pending_popup ()
{
	if (!g_popPending) return;
	g_popPending = false;
	int rowH = wk_fh () + 8, w = g_popW;
	for (int i = 0; i < g_popN; i++) { int tw = wk_text_w (g_popItem[i]) + 34; if (tw > w) w = tw; }
	if (w > g_root->width - 8) w = g_root->width - 8;
	int rows = g_popN < POP_ROWS ? g_popN : POP_ROWS;
	int x = g_page->left + g_popX, y = g_page->top + g_popY + g_popH, h = rows * rowH + 8;
	if (y + h > g_root->height && g_page->top + g_popY - h >= 0) y = g_page->top + g_popY - h;	// (above the box)
	if (y + h > g_root->height) y = g_root->height - h;
	if (x + w > g_root->width) x = g_root->width - w;
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	ListPopup p (x, y, w, rows, rowH);
	int r = p.run ();
	engine_popup_select (r > 0 ? r - 1 : -1);
	g_page->setFocus ();
}

// ---- find in the page (Ctrl+F): a bar above the status bar ----------------------------------------------

#define FIND_H	34
static void find_close ();
class FindField : public Textbox
{
public:
	FindField (int l, int t, int w, int h, Action enter) : Textbox (l, t, w, h, "", enter) {}
	bool onKey (long k) override
	{
		if (k == 27) { find_close (); return true; }
		return Textbox::onKey (k);
	}
};
class FindBar : public Widget
{
public:
	FindBar (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override { canvas.clear (C_BG); wk_etch_h (canvas, 0, 0, width, C_BG); }
};
static FindBar   *g_find;
static FindField *g_findField;
static Label     *g_findInfo;

static void find_go (bool back)
{
	if (g_findField->text[0]) engine_find (g_findField->text, back);
	else { g_findInfo->setText (""); engine_find_done (); }
}
static void cb_find_enter (Widget &) { find_go ((kapi_get_modifiers () & MOD_SHIFT) != 0); }
static void cb_find_changed (Widget &) { find_go (false); }
static void cb_find_next (Widget &) { find_go (false); g_findField->setFocus (); }
static void cb_find_prev (Widget &) { find_go (true); g_findField->setFocus (); }
static void cb_find_close (Widget &) { find_close (); }
static void on_find_result (int matches)
{
	char b[64];
	if (matches > 0) snprintf (b, sizeof b, matches == 1 ? "1 match" : "%d matches", matches);
	else snprintf (b, sizeof b, "Not found");
	g_findInfo->setText (b);
}

static void find_layout (bool shown)
{
	int pageH = g_root->height - TB_H - SB_H - (shown ? FIND_H : 0);
	g_page->resizeTo (g_root->width, pageH);
	g_find->hidden = !shown;
	engine_resize (g_page->width, g_page->height);
	g_page->damage (0, 0, g_page->width, g_page->height);
	g_root->invalidate (true);
}
static void op_find ()
{
	if (g_find->hidden)
	{
		g_find->left = 0; g_find->top = g_root->height - SB_H - FIND_H;
		g_find->resizeTo (g_root->width, FIND_H);
		find_layout (true);
	}
	g_findField->setFocus ();
	g_findField->caret = (int) strlen (g_findField->text);
	find_go (false);
}
static void find_close ()
{
	if (g_find->hidden) return;
	engine_find_done ();
	find_layout (false);
	g_page->setFocus ();
}

// ---- downloads: the downloads' window (a process of its own) and the status bar --------------------------

static int g_dlTo = -1, g_dlFrom = -1, g_dlPid;
static char g_dlIn[512];
static int g_dlInLen;

static void dl_close ()
{
	if (g_dlTo >= 0) close (g_dlTo);
	if (g_dlFrom >= 0) close (g_dlFrom);
	g_dlTo = g_dlFrom = -1;
	g_dlPid = 0;
}

// A line to the downloads' window; started (again) when it is not there and `start` (a new download).
static void dl_send (const char *line, bool start)
{
	for (int attempt = 0; attempt < 2; attempt++)
	{
		if (g_dlTo < 0)
		{
			if (!start || !(g_dlPid = engine_spawn_self ("--onyx-downloads", &g_dlTo, &g_dlFrom))) return;
		}
		int n = (int) strlen (line);
		if (write (g_dlTo, line, (size_t) n) == n) return;
		dl_close ();						// (its window was closed)
	}
}

static void downloads_tick ()
{
	if (g_dlFrom < 0) return;
	int st;
	if (g_dlPid > 1 && waitpid (g_dlPid, &st, WNOHANG) == g_dlPid) { dl_close (); return; }	// (its window closed)
	int n = (int) read (g_dlFrom, g_dlIn + g_dlInLen, sizeof g_dlIn - 1 - (size_t) g_dlInLen);
	if (n <= 0) return;
	g_dlInLen += n;
	g_dlIn[g_dlInLen] = 0;
	char *s = g_dlIn, *nl;
	while ((nl = strchr (s, '\n')) != 0)
	{
		*nl = 0;
		char m[96]; int l = snprintf (m, sizeof m, "web: the downloads' window: %.60s\n", s);	// (kmsg)
		(void) !write (2, m, (size_t) l);
		if (!strncmp (s, "cancel ", 7)) engine_download_cancel (atoi (s + 7));
		s = nl + 1;
	}
	g_dlInLen = (int) strlen (s);
	memmove (g_dlIn, s, (size_t) g_dlInLen + 1);
}

static const char *base_name (const char *path)
{
	const char *b = strrchr (path, '/');
	return b ? b + 1 : path;
}

static void on_download (int id, int event, const char *text, long long done, long long total)
{
	char line[700];
	switch (event)
	{
	case ENGINE_DL_STARTED:
		snprintf (line, sizeof line, "add %d %lld %s\n", id, total, text);
		dl_send (line, true);
		snprintf (g_dlText, sizeof g_dlText, "Downloading %s...", base_name (text));
		break;
	case ENGINE_DL_PROGRESS:
		snprintf (line, sizeof line, "prog %d %lld %lld\n", id, done, total);
		dl_send (line, false);
		if (total > 0) snprintf (g_dlText, sizeof g_dlText, "Downloading %s: %d %%", base_name (text), (int) (done * 100 / total));
		else snprintf (g_dlText, sizeof g_dlText, "Downloading %s: %lld KB", base_name (text), done / 1024);
		break;
	case ENGINE_DL_FINISHED:
		snprintf (line, sizeof line, "done %d %lld\n", id, done);
		dl_send (line, false);
		snprintf (g_dlText, sizeof g_dlText, "Downloaded %s (SD:/Downloads)", base_name (text));
		break;
	case ENGINE_DL_FAILED:
		snprintf (line, sizeof line, "fail %d %s\n", id, text);
		dl_send (line, false);
		snprintf (g_dlText, sizeof g_dlText, "Download failed: %s", text);
		break;
	case ENGINE_DL_CANCELLED:
		snprintf (line, sizeof line, "cancelled %d\n", id);
		dl_send (line, false);
		snprintf (g_dlText, sizeof g_dlText, "Download cancelled");
		break;
	}
	show_status ();
}

static void op_downloads () { dl_send ("\n", true); }

// ---- the right click's menu ---------------------------------------------------------------------------------

// A link's or an image's address -> a file name for Save As (the last part of its path).
static void name_of (const char *url, char *o, int cap)
{
	const char *s = url, *q = strpbrk (url, "?#");
	const char *end = q ? q : url + strlen (url);
	for (const char *p = url; p < end; p++) if (*p == '/') s = p + 1;
	int n = (int) (end - s);
	if (n <= 0) { snprintf (o, (size_t) cap, "download"); return; }
	if (n > cap - 1) n = cap - 1;
	memcpy (o, s, (size_t) n);
	o[n] = 0;
}

static void save_as (const char *url)
{
	char name[128], path[300];
	name_of (url, name, sizeof name);
	kapi_mkdir ("SD:/Downloads");
	if (wk_file_save (path, sizeof path, "SD:/Downloads", name)) engine_download_url (url, path);
}

enum { CM_OPEN_LINK = 1, CM_COPY_LINK, CM_SAVE_LINK, CM_OPEN_IMAGE, CM_COPY_IMAGE, CM_SAVE_IMAGE,
       CM_BACK, CM_FORWARD, CM_RELOAD, CM_COPY, CM_SELECT_ALL, CM_SAVE_PAGE };

static void op_back ();
static void op_forward ();
static void op_reload ();

static void context_menu (int x, int y)
{
	char link[1024], image[1024];
	engine_hit (link, sizeof link, image, sizeof image);
	PopupMenu m (g_page->left + x, g_page->top + y);
	if (link[0])
	{
		m.add ("Open Link in New Window", CM_OPEN_LINK);
		m.add ("Copy Link Address", CM_COPY_LINK);
		m.add ("Save Link As...", CM_SAVE_LINK);
		m.separator ();
	}
	if (image[0])
	{
		m.add ("Open Image in New Window", CM_OPEN_IMAGE);
		m.add ("Copy Image Address", CM_COPY_IMAGE);
		m.add ("Save Image As...", CM_SAVE_IMAGE);
		m.separator ();
	}
	m.add ("Back", CM_BACK, !g_back->disabled);
	m.add ("Forward", CM_FORWARD, !g_fwd->disabled);
	m.add ("Reload", CM_RELOAD);
	m.separator ();
	m.add ("Copy", CM_COPY, true, "^C");
	m.add ("Select All", CM_SELECT_ALL, true, "^A");
	m.add ("Save Page As...", CM_SAVE_PAGE);
	switch (m.run ())
	{
	case CM_OPEN_LINK: engine_new_window (link); break;
	case CM_COPY_LINK: clip_set_text (link); break;
	case CM_SAVE_LINK: save_as (link); break;
	case CM_OPEN_IMAGE: engine_new_window (image); break;
	case CM_COPY_IMAGE: clip_set_text (image); break;
	case CM_SAVE_IMAGE: save_as (image); break;
	case CM_BACK: op_back (); break;
	case CM_FORWARD: op_forward (); break;
	case CM_RELOAD: op_reload (); break;
	case CM_COPY: engine_command ("Copy"); break;
	case CM_SELECT_ALL: engine_command ("SelectAll"); break;
	case CM_SAVE_PAGE: save_as (g_pageUrl); break;
	}
	g_page->setFocus ();
}

// ---- the system clipboard (the engine's: copies leave the page, the other apps' come in) ----------------

static void clip_write (const char *s, unsigned long n) { clip_set_text_n (s, (int) n); }
static unsigned long clip_read (char *b, unsigned long cap)
{
	static char t[256 * 1024];
	int n = clip_get_text (t, sizeof t);
	if (b && cap)
	{
		unsigned long m = (unsigned long) n < cap - 1 ? (unsigned long) n : cap - 1;
		memcpy (b, t, m);
		b[m] = 0;
	}
	return (unsigned long) n;
}
static unsigned clip_serial ()
{
	int type = 0;
	unsigned serial = 0;
	kapi_clipboard_get (&type, 0, 0, &serial);
	return serial;
}

// ---- the engine's calls ------------------------------------------------------------------------------------

static void on_needs_display (int x, int y, int w, int h) { g_page->damage (x, y, w, h); }
// The title's face has Latin-1's letters: the typographic punctuation pages use made plain.
static void plain_title (const char *t, char *o, int cap)
{
	static const struct { const char *u8, *ascii; } map[] = {
		{ "\xE2\x80\x94", " - " }, { "\xE2\x80\x93", "-" }, { "\xE2\x80\x98", "'" }, { "\xE2\x80\x99", "'" },
		{ "\xE2\x80\x9C", "\"" }, { "\xE2\x80\x9D", "\"" }, { "\xE2\x80\xA6", "..." }, { "\xC2\xA0", " " },
		{ "\xE2\x80\xA2", "-" }, { "\xC2\xB7", "-" }, { "\xE2\x80\xAF", " " } };
	int n = 0;
	while (*t && n < cap - 4)
	{
		bool done = false;
		for (unsigned i = 0; i < sizeof map / sizeof map[0] && !done; i++)
		{
			int l = (int) strlen (map[i].u8);
			if (!strncmp (t, map[i].u8, l))
			{
				for (const char *a = map[i].ascii; *a && n < cap - 1; a++) o[n++] = *a;
				t += l; done = true;
			}
		}
		if (!done) o[n++] = *t++;
	}
	o[n] = 0;
}
static void on_title (const char *t)
{
	plain_title (t, g_title, sizeof g_title);
	draw_title ();
}
static void on_url (const char *u)
{
	snprintf (g_pageUrl, sizeof g_pageUrl, "%s", u);
	if (!g_url->hasFocus) g_url->setText (u);
}
static void focus_page () { g_page->setFocus (); g_url->invalidate (true); }
static void on_loading (bool loading, double p)
{
	g_loading = loading; g_progress = p;
	if (!loading) snprintf (g_statusText, sizeof g_statusText, "Done");
	g_reload->setGlyph (WKT_NONE)->setIcon (glyph, loading ? WKG_CLOSE : WKG_RELOAD);
	g_reload->tip = loading ? "Stop (Esc)" : "Reload (Ctrl+R)";
	g_reload->invalidate (true);
	show_status ();
}
static void on_history (bool b, bool f)
{
	g_back->setDisabled (!b);
	g_fwd->setDisabled (!f);
}
static void on_status (const char *s)
{
	snprintf (g_hover, sizeof g_hover, "%s", s);
	show_status ();
}
static void on_open_window (const char *url) { engine_new_window (url); }
static void on_load_failed (const char *url, const char *why)
{
	snprintf (g_statusText, sizeof g_statusText, "Could not load %s: %s", url, why);
	show_status ();
}
static void on_process_ended ()
{
	snprintf (g_statusText, sizeof g_statusText, "The page stopped working: Reload to load it again");
	show_status ();
}
static void on_alert (const char *t) { wk_messagebox ("This page says", t, MB_OK); }
static bool on_confirm (const char *t) { return wk_messagebox ("This page asks", t, MB_OKCANCEL) == 1; }

static void op_back ();
static void op_forward ();
static void op_reload ();
static void op_location ();
static void on_key_not_handled (long k, unsigned mods)
{
	if ((mods & MOD_ALT) && k == KEY_LEFT) op_back ();
	else if ((mods & MOD_ALT) && k == KEY_RIGHT) op_forward ();
	else if (k == KEY_BACKSPACE && !(mods & (MOD_CTRL | MOD_ALT))) op_back ();
	else if (k == 27 && g_loading) engine_stop ();
	else if (k == KEY_F1 + 4) op_reload ();				// F5
	else if (k == KEY_F1 + 5) op_location ();			// F6
}

static const EngineClient s_client = {
	on_needs_display, on_title, on_url, on_loading, on_history, on_status, on_open_window,
	on_load_failed, on_process_ended, on_alert, on_confirm, on_key_not_handled,
	on_show_popup, on_hide_popup, on_find_result, on_download,
};

// ---- the commands ------------------------------------------------------------------------------------------

static void op_back () { engine_back (); g_page->setFocus (); }
static void op_forward () { engine_forward (); g_page->setFocus (); }
static void op_reload () { if (g_loading) engine_stop (); else engine_reload (); }
static void op_home () { engine_load (HOME); g_page->setFocus (); }
static void op_location () { g_url->setText (""); g_url->setFocus (); g_url->caret = 0; g_url->invalidate (true); }
static void op_go (Widget &) { engine_load (g_url->text); g_page->setFocus (); }
static void op_new_window () { engine_new_window (HOME); }
static void op_open_file ()
{
	char p[256];
	if (wk_file_open (p, sizeof p, "SD:/")) engine_load (p);
}
static void op_close () { kapi_exit (0); }
static void op_save_page () { save_as (g_pageUrl); }
// Edit: the address field's when it has the keyboard, else the page's.
static void op_cut () { if (g_url->hasFocus) g_url->onKey (WK_CTRL ('X')); else engine_command ("Cut"); }
static void op_copy () { if (g_url->hasFocus) g_url->onKey (WK_CTRL ('C')); else engine_command ("Copy"); }
static void op_paste () { if (g_url->hasFocus) g_url->onKey (WK_CTRL ('V')); else engine_command ("Paste"); }
static void op_select_all () { if (!g_url->hasFocus) engine_command ("SelectAll"); }	// (the field: ^C takes it whole)
static void op_zoom_in () { engine_zoom (engine_zoom_factor () * 1.1); }
static void op_zoom_out () { engine_zoom (engine_zoom_factor () / 1.1); }
static void op_zoom_reset () { engine_zoom (1); }
static void op_about () { wk_messagebox ("About Web", "Web -- the Onyx browser on WebKit.\nWebKit: LGPL-2.1 (see SD:/docs/licences).", MB_OK); }

static void cb_back (Widget &) { op_back (); }
static void cb_forward (Widget &) { op_forward (); }
static void cb_reload (Widget &) { op_reload (); }
static void cb_home (Widget &) { op_home (); }

static ToolButton *tool (int id, const char *tip, Action cb)
{
	ToolButton *b = (new ToolButton (30, 28, tip, cb))->setIcon (glyph, id);
	b->iconSize = 16;
	return b;
}

int main (int argc, char **argv)
{
	if (engine_is_auxiliary (argc, argv))			// WebKit's web or network process
		return engine_auxiliary_main (argc, argv);
	if (argc > 1 && !strcmp (argv[1], "--onyx-downloads"))	// the downloads' window
		return downloads_main ();
	if (argc > 1 && !strcmp (argv[1], "--applet"))		// a web view in another program's window (Mail)
	{
		extern int webview_main (int, char **);
		return webview_main (argc, argv);
	}

	char url[1024] = "";
	if (argc > 1) snprintf (url, sizeof url, "%s", argv[1]);
	else kapi_get_args (url, sizeof url);
	say (url[0] ? url : "(the start page)");

	WebRoot root;
	g_root = &root;
	if (root.canvas.px == 0) return 1;

	ToolBar *tb = new ToolBar (0, 0, W0, TB_H);
	tb->line = true;
	tb->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	tb->add (g_back = tool (WKG_CHEV_LEFT, "Back (Alt+Left)", cb_back), 6);
	tb->add (g_fwd = tool (WKG_CHEV_RIGHT, "Forward (Alt+Right)", cb_forward));
	tb->add (g_reload = tool (WKG_RELOAD, "Reload (Ctrl+R)", cb_reload));
	tb->add (tool (WKG_HOME, "Home", cb_home));
	g_back->setDisabled (true);
	g_fwd->setDisabled (true);
	root.addChild (tb);
	int ux = tb->next () + 8;
	g_url = new UrlField (ux, 7, W0 - ux - 10, 26, op_go);
	g_url->maxLen = Textbox::TEXT_CAP - 1;
	g_url->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	root.addChild (g_url);

	g_page = new PageView (0, TB_H, W0, H0 - TB_H - SB_H);
	root.addChild (g_page);
	g_status = new Label (10, H0 - SB_H + 3, W0 - 20, SB_H - 6, "", C_TEXT, C_BG);
	g_status->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM | ANCHOR_RIGHT;
	root.addChild (g_status);

	g_find = new FindBar (0, H0 - SB_H - FIND_H, W0, FIND_H);	// (hidden until Ctrl+F)
	g_find->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM | ANCHOR_RIGHT;
	g_find->hidden = true;
	g_find->addChild (new Label (10, 9, 40, 16, "Find:", C_TEXT, C_BG));
	g_findField = new FindField (54, 4, 300, 26, cb_find_enter);
	g_findField->changed = cb_find_changed;
	g_find->addChild (g_findField);
	g_find->addChild (tool (WKG_CHEV_UP, "Previous (Shift+Enter)", cb_find_prev));
	g_find->lastChild->left = 360; g_find->lastChild->top = 3;
	g_find->addChild (tool (WKG_CHEV_DOWN, "Next (Enter)", cb_find_next));
	g_find->lastChild->left = 392; g_find->lastChild->top = 3;
	g_findInfo = new Label (432, 9, 200, 16, "", C_TEXT, C_BG);
	g_find->addChild (g_findInfo);
	ToolButton *fc = tool (WKG_CLOSE, "Close (Esc)", cb_find_close);
	fc->left = W0 - 40; fc->top = 3; fc->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
	g_find->addChild (fc);
	root.addChild (g_find);

	static Menu menu;
	menu.menu ("File");
	menu.item ("New Window", "^N", WK_CTRL ('N'), op_new_window);
	menu.item ("Open File...", "^O", WK_CTRL ('O'), op_open_file);
	menu.item ("Open Location", "^L", WK_CTRL ('L'), op_location);
	menu.item ("Save Page As...", "^S", WK_CTRL ('S'), op_save_page);
	menu.item ("Downloads", "^J", WK_CTRL ('J'), op_downloads);
	menu.separator ();
	menu.item ("Close Window", "^W", WK_CTRL ('W'), op_close);
	menu.menu ("Edit");
	menu.item ("Cut", "^X", WK_CTRL ('X'), op_cut);
	menu.item ("Copy", "^C", WK_CTRL ('C'), op_copy);
	menu.item ("Paste", "^V", WK_CTRL ('V'), op_paste);
	menu.item ("Select All", "^A", WK_CTRL ('A'), op_select_all);
	menu.separator ();
	menu.item ("Find...", "^F", WK_CTRL ('F'), op_find);
	menu.menu ("View");
	menu.item ("Reload", "^R", WK_CTRL ('R'), op_reload);
	menu.separator ();
	menu.item ("Zoom In", "", 0, op_zoom_in);
	menu.item ("Zoom Out", "", 0, op_zoom_out);
	menu.item ("Actual Size", "", 0, op_zoom_reset);
	menu.menu ("Go");
	menu.item ("Back", "Alt+Left", 0, op_back);
	menu.item ("Forward", "Alt+Right", 0, op_forward);
	menu.item ("Home", "", 0, op_home);
	menu.menu ("Help");
	menu.item ("About Web", "", 0, op_about);
	menu.publish ();

	root.setResizable (true);
	root.fitWorkArea ();
	engine_set_clipboard (clip_write, clip_read, clip_serial);
	if (!engine_init (&s_client, g_page->width, g_page->height))
	{
		wk_messagebox ("Web", "The web engine could not start (see kmsg).", MB_OK);
		return 1;
	}
	engine_load (url[0] ? url : HOME);
	g_page->setFocus ();
	draw_title ();
	root.run ();
	say ("the window closes");
	return 0;
}
