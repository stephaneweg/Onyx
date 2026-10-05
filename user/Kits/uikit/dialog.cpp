//
// uikit/dialog.cpp -- modal dialogs (Modal / MessageBox / FileDialog), compiled into
// libuikit.a. A dialog is a normal uikit Widget (its canvas IS the dialog box) added as the
// topmost child of the Root and flagged `modal`; Root then routes ALL input to it (see
// Widget::handleMouse/handleKey) until run() removes it. Built from uikit controls
// (Button/Textbox/Scrollbar) so dialogs match the rest of the UI.
//
#include "uikit/dialog.h"
#include "uikit/root.h"
#include "uikit/button.h"
#include "uikit/slider.h"
#include "uikit/lang.h"
#include "appkit/appkit.h"		// MB_*, KEY_*, kapi_present, kapi_opendir/readdir
// operator new[]/delete[] resolve at link from the app's onyxpp.hpp (see canvas.cpp).

namespace uikit {

// A dialog control's parent IS the dialog (a Modal): route its callback there.
static void dlg_btn (Widget &w) { if (w.parent) ((Modal *) w.parent)->onButton (w.tag); }
static void dlg_enter (Widget &w) { if (w.parent) ((Modal *) w.parent)->onButton (1); }	// a box's Enter: OK
static void dlg_scr (Widget &w) { if (w.parent) ((Modal *) w.parent)->onScroll (((Scrollbar &) w).value); }

// ---- Modal -------------------------------------------------------------------
Modal::Modal (int w, int h) : Widget (0, 0, w, h), done (false), result (0) { modal = true; transparent = true; }

unsigned Modal::bgColor () { return C_FACE; }
int Modal::titleH () { return uk_fh () + 10; }

void Modal::drawBox (const char *title)
{
	unsigned ol = UK_OUTLINE == 2 ? 0x00000000 : uk_tone (C_FRAME_ACTIVE, 44);
	canvas.clear (ol);
	uk_rbox (canvas, 0, 0, width, height, 8, C_FACE, C_FACE);
	uk_title_strip (canvas, 1, 1, width - 2, titleH (), title, 7);
	uk_rline (canvas, 0, 0, width, height, 8, ol, 255);
	uk_corner_key (canvas, 0, 0, width, height, 8);
}

int Modal::run ()
{
	Root *r = Root::current ();
	if (r == 0) return 0;
	r->addChild (this);			// becomes the topmost (modal) child of the window
	done = false; hasFocus = true; invalidate (true);
	bool focused = false;			// (nothing focused yet: its first text field, ready to type in)
	for (Widget *c = firstChild; c; c = c->nextSib) if (c->hasFocus) focused = true;
	if (!focused)
		for (Widget *c = firstChild; c; c = c->nextSib)
			if (c->isField () && c->canFocus && !c->hidden && !c->disabled) { c->setFocus (); c->onTabFocus (); break; }
	while (!done && !uk_quit ())
	{
		uk_pump ();
		if (!r->valid) { r->draw (); uk_present (); }
		msleep (16);
	}
	r->removeChild (this);
	r->invalidate (true);			// repaint the app, erasing the dialog
	return result;
}

// ---- MessageBox --------------------------------------------------------------
// The box: 320 wide, up to 460 for a long text (its longest line; the text wraps at the words past that),
// as high as its lines need; never more than the window allows.
enum { MB_PAD = 16, MB_MAXW = 460 };
// The next line of p that fits w pixels (cut at a space; a '\n' ends it) into buf (cap bytes) -> past it.
static const char *mb_line (const char *p, int w, char *buf, int cap)
{
	int n = 0, lastSp = -1;
	while (p[n] && p[n] != '\n' && n < cap - 1)
	{
		buf[n] = p[n]; buf[n + 1] = '\0';
		if (p[n] == ' ') lastSp = n;
		if (n > 0 && uk_text_w (buf) > w) { if (lastSp > 0) n = lastSp; break; }
		n++;
	}
	buf[n] = '\0';
	p += n;
	while (*p == ' ') p++;
	if (*p == '\n') p++;
	return p;
}
static int mb_w (const char *text)
{
	Root *r = Root::current (); int W = r ? r->width : 320;
	int w = 320, widest = 0;
	char line[160];
	for (const char *p = text ? text : ""; *p; ) { const char *q = mb_line (p, 100000, line, sizeof line); int t = uk_text_w (line); if (t > widest) widest = t; if (q == p) break; p = q; }
	if (widest + 2 * MB_PAD > w) w = widest + 2 * MB_PAD;
	if (w > MB_MAXW) w = MB_MAXW;
	if (w > W - 20) w = W - 20;
	return w;
}
static int mb_h (const char *text)
{
	Root *r = Root::current (); int H = r ? r->height : 130;
	int w = mb_w (text) - 2 * MB_PAD, lines = 0;
	char line[160];
	for (const char *p = text ? text : ""; *p; lines++) { const char *q = mb_line (p, w, line, sizeof line); if (q == p) break; p = q; }
	if (lines < 1) lines = 1;
	int h = Modal::titleH () + 14 + lines * (uk_fh () + 3) + 14 + 28 + 12;
	if (h < 130) h = 130;
	if (h > H - 20) h = H - 20;
	return h;
}

MessageBox::MessageBox (const char *title, const char *text, int buttons)
  : Modal (mb_w (text), mb_h (text)), m_title (title), m_text (text), m_def (1)
{
	Root *r = Root::current ();
	int W = r ? r->width : width, H = r ? r->height : height;
	left = (W - width) / 2; top = (H - height) / 2;		// centre in the window
	int by = height - 38;
	Button *b;
	if (buttons == MB_YESNOCANCEL)
	{
		b = new Button (width - 268, by, 82, 28, TR ("Yes"),    dlg_btn); b->tag = 1; addChild (b);
		b = new Button (width - 180, by, 82, 28, TR ("No"),     dlg_btn); b->tag = 2; addChild (b);
		b = new Button (width - 92,  by, 82, 28, TR ("Cancel"), dlg_btn); b->tag = 0; addChild (b);
	}
	else if (buttons == MB_YESNO)
	{
		b = new Button (width - 180, by, 82, 28, TR ("Yes"), dlg_btn); b->tag = 1; addChild (b);
		b = new Button (width - 92,  by, 82, 28, TR ("No"),  dlg_btn); b->tag = 0; addChild (b);
	}
	else if (buttons == MB_OKCANCEL)
	{
		b = new Button (width - 180, by, 82, 28, TR ("OK"),     dlg_btn); b->tag = 1; addChild (b);
		b = new Button (width - 92,  by, 82, 28, TR ("Cancel"), dlg_btn); b->tag = 0; addChild (b);
	}
	else
	{
		b = new Button (width - 92, by, 82, 28, TR ("OK"), dlg_btn); b->tag = 1; addChild (b);
	}
}

bool MessageBox::onKey (long k)
{
	if (k == KEY_ENTER) { close (m_def); return true; }
	if (k == 27)        { close (0);     return true; }	// Esc cancels
	return false;
}

void MessageBox::onDraw ()
{
	int fh = uk_fh ();
	drawBox (m_title);
	char buf[160]; int line = 0;
	for (const char *p = m_text ? m_text : ""; *p; line++)
	{
		const char *q = mb_line (p, width - 2 * MB_PAD, buf, sizeof buf);
		canvas.text (MB_PAD, titleH () + 14 + line * (fh + 3), buf, C_TEXT);
		if (q == p) break;
		p = q;
	}
}

// ---- FileDialog --------------------------------------------------------------
//
// (2026-10-05) Redone: an Up button and the folder's path; the volumes at the left (when the box
// is wide enough); the folder's content with an icon each, the folders first, sorted by name, the
// files' sizes at the right; a click selects, a double click (or Enter, or the button) opens a
// folder / takes a file; the arrows move the selection, Backspace goes up. A file's path given as
// the start (a program's last document) opens its folder.
//
// THE CLASS'S SIZE AND ITS FUNCTIONS' NAMES ARE THE LIBRARY'S ABI (layout_lock.cpp, uikit.abi): a
// program that builds a FileDialog itself keeps working, and gets this dialog. So the fields are
// the ones it always had, used as follows: an entry is m_ent[i] = its name (FD_NAME bytes) then
// its size (4 bytes); the last two entries' room holds what the dialog needs more (FdExtra).
static void fd_scopy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }

static int fd_w () { Root *r = Root::current (); int W = r ? r->width  : 560; int w = 560; if (w > W - 20) w = W - 20; return w; }
static int fd_h () { Root *r = Root::current (); int H = r ? r->height : 430; int h = 430; if (h > H - 20) h = H - 20; return h; }

// The volumes shown at the left (the SD card's FAT / exFAT partitions, the USB sticks).
#ifdef _WIN32		// (the Windows build of the apps, pc/Koton: SD: is the program's folder, then the PC's drives)
static const char *const FD_VOLS[] = { "SD:", "C:", "D:", "E:", "F:", "G:", "H:", "I:", "J:", "K:" };
#elif defined(__APPLE__)	// (the macOS build, pc/macOS: SD: is ~/Documents/Onyx Ledger, HOME: the user's folder, MAC: the Mac's /)
static const char *const FD_VOLS[] = { "SD:", "HOME:", "MAC:" };
#else
static const char *const FD_VOLS[] = { "SD:", "SD1:", "SD2:", "SD3:", "USB:", "USB2:", "USB3:" };
#endif

enum { FD_MAX = 126, FD_NAME = 92, FD_VOLMAX = 8 };	// (128 entries of 96 bytes: 126 files, 2 for FdExtra)
struct FdExtra
{
	const char *filters;			// the kinds of files (setFilters; 0: all)
	int  filter;				// ... the one chosen
	int  fx, fy, fw, fh;			// ... its drop-down (fw 0: none)
	int  hot, hotVol, nvol;			// the row, the volume under the pointer; the volumes shown
	int  ux, uy, uw, uh;			// the Up button
	int  px, pw;				// the path field
	int  vx, vw;				// the volumes' column (vw 0: none)
	int  ny;				// the name row (0: none)
	char upHot, upDown;
	char vol[FD_VOLMAX][8];
};
static_assert (sizeof (FdExtra) <= 2 * 96, "FdExtra lives in two entries of FileDialog::m_ent");
static inline FdExtra &fd_x (char (*ent)[96]) { return *(FdExtra *) (void *) ent[FD_MAX]; }
static inline unsigned fd_size (char (*ent)[96], int i) { unsigned v; __builtin_memcpy (&v, ent[i] + FD_NAME, 4); return v; }
static inline void fd_set_size (char (*ent)[96], int i, unsigned v) { __builtin_memcpy (ent[i] + FD_NAME, &v, 4); }

static bool fd_isdir (const char *p) { void *d = kapi_opendir (p); if (d == 0) return false; kapi_closedir (d); return true; }
static bool fd_root (const char *dir) { int n = 0; while (dir[n]) n++; return n == 0 || dir[n - 1] == ':' || (n > 1 && dir[n - 1] == '/' && dir[n - 2] == ':'); }
static void fd_parent (char *dir)
{
	int n = 0; while (dir[n]) n++;
	if (n > 0 && dir[n - 1] == '/') n--;
	if (n > 0 && dir[n - 1] == ':') { dir[0] = '\0'; return; }	// (a volume's root: nothing above)
	while (n > 0 && dir[n - 1] != '/' && dir[n - 1] != ':') n--;
	if (n > 0 && dir[n - 1] == '/') n--;
	if (n > 0 && dir[n - 1] == ':') { dir[n] = '/'; n++; }		// keep the root's '/': "SD1:/"
	dir[n] = '\0';
}
// A path that names a file (a program gives its last document) or a folder that is gone: the
// nearest folder above it that exists; a file's name left in `name` (ncap bytes; 0: not wanted).
static void fd_start (char *dir, int cap, const char *start, char *name, int ncap)
{
	if (name && ncap > 0) name[0] = '\0';
	fd_scopy (dir, (start && start[0]) ? start : "SD:/", cap);
	if (fd_isdir (dir)) return;
	if (name && ncap > 0)						// its last component: a file's name
	{
		int n = 0; while (dir[n]) n++;
		int k = n; while (k > 0 && dir[k - 1] != '/' && dir[k - 1] != ':') k--;
		fd_scopy (name, dir + k, ncap);
	}
	for (int i = 0; i < 16 && dir[0] && !fd_isdir (dir); i++) fd_parent (dir);
	if (dir[0] == '\0') fd_scopy (dir, "SD:/", cap);
}

// ---- the kinds of files: "Name|*.a;*.b|Name|*" ----
static const char *fd_bar (const char *p) { while (*p && *p != '|') p++; return p; }
static int fd_filters (const char *f)
{
	int n = 0;
	if (f == 0 || f[0] == '\0') return 0;
	for (const char *p = f; *p; )
	{
		const char *e = fd_bar (p); if (*e == '\0') break;	// (a name without patterns: not one)
		const char *e2 = fd_bar (e + 1);
		n++;
		p = *e2 ? e2 + 1 : e2;
	}
	return n;
}
// The i-th kind: its name and its patterns copied (ncap, pcap bytes) -> false: no such kind.
static bool fd_filter (const char *f, int i, char *name, int ncap, char *pats, int pcap)
{
	if (f == 0) return false;
	for (const char *p = f; *p; i--)
	{
		const char *e = fd_bar (p); if (*e == '\0') return false;
		const char *e2 = fd_bar (e + 1);
		if (i == 0)
		{
			int k = 0; for (const char *q = p; q < e && k < ncap - 1; q++) name[k++] = *q; name[k] = '\0';
			k = 0; for (const char *q = e + 1; q < e2 && k < pcap - 1; q++) pats[k++] = *q; pats[k] = '\0';
			return true;
		}
		p = *e2 ? e2 + 1 : e2;
	}
	return false;
}
static char fd_low (char c);
// Does a file's name match the patterns ("*.txt;*.md", "*")?
static bool fd_match (const char *name, const char *pats)
{
	int nl = 0; while (name[nl]) nl++;
	for (const char *p = pats; *p; )
	{
		const char *e = p; while (*e && *e != ';') e++;
		int pl = (int) (e - p);
		if (pl == 1 && p[0] == '*') return true;
		if (pl == 3 && p[0] == '*' && p[1] == '.' && p[2] == '*') return true;
		if (pl >= 2 && p[0] == '*')				// "*.ext": the name's end
		{
			int sl = pl - 1;
			if (nl >= sl)
			{
				bool same = true;
				for (int k = 0; k < sl && same; k++) if (fd_low (name[nl - sl + k]) != fd_low (p[1 + k])) same = false;
				if (same) return true;
			}
		}
		p = *e ? e + 1 : e;
	}
	return pats[0] == '\0';
}

static char fd_low (char c) { return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c; }
static int fd_cmp (const char *a, const char *b)
{
	for (; *a && fd_low (*a) == fd_low (*b); a++, b++) {}
	return (int) (unsigned char) fd_low (*a) - (int) (unsigned char) fd_low (*b);
}

// The size as a person reads it: "812 B", "14 KB", "3.2 MB".
static void fd_size_text (unsigned n, char *out)
{
	const char *unit = "B"; unsigned v = n, frac = 0; bool dec = false;
	if (n >= 1024u * 1024u * 1024u) { unit = "GB"; v = n >> 30; frac = ((n >> 20) & 1023) * 10 / 1024; dec = v < 10; }
	else if (n >= 1024u * 1024u) { unit = "MB"; v = n >> 20; frac = ((n >> 10) & 1023) * 10 / 1024; dec = v < 10; }
	else if (n >= 1024u) { unit = "KB"; v = (n + 512) >> 10; }
	char d[12]; int i = 0, k = 0;
	do { d[i++] = (char) ('0' + v % 10); v /= 10; } while (v);
	while (i > 0) out[k++] = d[--i];
	if (dec) { out[k++] = '.'; out[k++] = (char) ('0' + frac); }
	out[k++] = ' ';
	for (; *unit; unit++) out[k++] = *unit;
	out[k] = '\0';
}

// s drawn at (x, y) in at most w pixels: cut with ".." when it does not fit.
static void fd_text_fit (Canvas &cv, int x, int y, const char *s, int w, unsigned c)
{
	char b[100]; int n = 0;
	for (; s[n] && n < 96; n++) b[n] = s[n];
	b[n] = '\0';
	if (uk_text_w (b) > w)
		while (n > 1) { n--; b[n] = '.'; b[n + 1] = '.'; b[n + 2] = '\0'; if (uk_text_w (b) <= w) break; }
	cv.text (x, y, b, c);
}

static void fd_icon_folder (Canvas &cv, int x, int y)
{
	cv.fillRect (x, y + 1, 7, 3, 0x00C8922C);
	cv.fillRect (x, y + 3, 16, 11, 0x00C8922C);
	cv.fillRect (x + 1, y + 5, 14, 8, 0x00F2C458);
	cv.fillRect (x + 1, y + 5, 14, 1, 0x00FBDD8C);
}
static void fd_icon_file (Canvas &cv, int x, int y)
{
	cv.fillRect (x + 2, y, 12, 15, 0x00848C98);
	cv.fillRect (x + 3, y + 1, 10, 13, 0x00FFFFFF);
	cv.fillRect (x + 10, y + 1, 3, 3, 0x00C4CAD4);
	cv.fillRect (x + 5, y + 6, 6, 1, 0x00AEB6C2);
	cv.fillRect (x + 5, y + 8, 6, 1, 0x00AEB6C2);
	cv.fillRect (x + 5, y + 10, 4, 1, 0x00AEB6C2);
}
static void fd_icon_drive (Canvas &cv, int x, int y)
{
	cv.fillRect (x, y + 3, 16, 10, 0x00606874);
	cv.fillRect (x + 1, y + 4, 14, 8, 0x00A9B2BE);
	cv.fillRect (x + 1, y + 4, 14, 1, 0x00CDD4DC);
	cv.fillRect (x + 11, y + 8, 3, 2, 0x0042B866);
}

FileDialog::FileDialog (const char *startDir, const char *defName, bool save, bool folder)
  : Modal (fd_w (), fd_h ()), m_save (save), m_folder (folder)
{
	char fileName[FD_NAME], defDir[256];
	// a default name that is a whole path (a program gives its document's): the name is its last part,
	// and the dialog opens in its folder
	bool defPath = false;
	for (const char *p = defName ? defName : ""; *p; p++) if (*p == '/' || *p == ':') defPath = true;
	if (defPath)
	{
		fd_start (m_dir, sizeof m_dir, defName, fileName, sizeof fileName);
		int n = 0; while (defName[n]) n++;
		int k = n; while (k > 0 && defName[k - 1] != '/' && defName[k - 1] != ':') k--;
		fd_scopy (defDir, defName + k, sizeof defDir);		// (the name, whether the file exists or not)
		fd_scopy (fileName, defDir, sizeof fileName);
		defName = fileName;
	}
	else fd_start (m_dir, sizeof m_dir, startDir, fileName, sizeof fileName);
	FdExtra &x = fd_x (m_ent);
	__builtin_memset (&x, 0, sizeof x);
	x.hot = x.hotVol = -1;
	m_count = 0; m_sel = -1; m_top = 0;
	m_lastRow = -1; m_lastTick = 0;
	Root *r = Root::current ();
	int W = r ? r->width : width, H = r ? r->height : height;
	left = (W - width) / 2; top = (H - height) / 2;

	int fh = uk_fh (), pad = 12;
	int barH = fh + 12, y0 = titleH () + 10;
	x.ux = pad; x.uy = y0; x.uw = barH + 2; x.uh = barH;
	x.px = x.ux + x.uw + 6; x.pw = width - pad - x.px;
	int by = height - 40;					// the buttons' row
	x.ny = folder ? 0 : by - (fh + 10) - 10;
	m_ly = y0 + barH + 8;
	m_lh = (folder ? by : x.ny) - 10 - m_ly;
	x.vw = width >= 440 ? 112 : 0; x.vx = pad;
	m_lx = pad + (x.vw ? x.vw + 8 : 0);
	m_lw = width - pad - 14 - m_lx;
	m_rowH = fh + 8; if (m_rowH < 22) m_rowH = 22;
	m_rows = (m_lh - 4) / m_rowH; if (m_rows < 1) m_rows = 1;

	m_sb = new Scrollbar (m_lx + m_lw + 2, m_ly, 12, m_lh, true, 1, 0, dlg_scr); addChild (m_sb);
	m_nameBox = 0;
	if (!folder)
	{
		int lw = uk_text_w (TR ("Name:")) + 8;
		const char *nm = defName && defName[0] ? defName : save ? fileName : "";
		m_nameBox = new Textbox (m_lx + lw, x.ny, width - pad - m_lx - lw, fh + 10, nm, dlg_enter); addChild (m_nameBox);
		if (save) m_nameBox->hasFocus = true;		// (a save: the name typed at once; run () focuses the box)
	}
	Button *b;
	b = new Button (width - pad - 184, by, 88, 28, TR (folder ? "Choose" : save ? "Save" : "Open"), dlg_btn); b->tag = 1; addChild (b);
	b = new Button (width - pad - 88,  by, 88, 28, TR ("Cancel"), dlg_btn);               b->tag = 0; addChild (b);
	for (unsigned i = 0; i < sizeof FD_VOLS / sizeof FD_VOLS[0] && x.nvol < FD_VOLMAX; i++)	// the volumes mounted
	{
		char root[10]; fd_scopy (root, FD_VOLS[i], 7);
		int n = 0; while (root[n]) n++; root[n] = '/'; root[n + 1] = '\0';
		if (fd_isdir (root)) fd_scopy (x.vol[x.nvol++], FD_VOLS[i], 8);
	}
	read ();
}

// The folder's content: the folders first, then by name.
void FileDialog::read ()
{
	m_count = 0; m_top = 0; m_sel = -1; fd_x (m_ent).hot = -1;
	char kind[64], pats[256]; pats[0] = '\0';
	bool filtered = fd_filter (fd_x (m_ent).filters, fd_x (m_ent).filter, kind, sizeof kind, pats, sizeof pats);
	void *d = kapi_opendir (m_dir);
	if (d != 0)
	{
		struct kapi_dirent e;
		while (m_count < FD_MAX && kapi_readdir (d, &e))
		{
			if (e.name[0] == '.' && (e.name[1] == '\0' || (e.name[1] == '.' && e.name[2] == '\0'))) continue;
			if (filtered && !e.is_dir && !fd_match (e.name, pats)) continue;	// (not of the kind chosen)
			fd_scopy (m_ent[m_count], e.name, FD_NAME); fd_set_size (m_ent, m_count, e.size);
			m_isdir[m_count] = e.is_dir ? 1 : 0; m_count++;
		}
		kapi_closedir (d);
	}
	for (int i = 1; i < m_count; i++)
	{
		char t[96]; __builtin_memcpy (t, m_ent[i], 96); char td = m_isdir[i]; int k = i;
		while (k > 0 && (m_isdir[k - 1] < td || (m_isdir[k - 1] == td && fd_cmp (m_ent[k - 1], t) > 0)))
		{ __builtin_memcpy (m_ent[k], m_ent[k - 1], 96); m_isdir[k] = m_isdir[k - 1]; k--; }
		__builtin_memcpy (m_ent[k], t, 96); m_isdir[k] = td;
	}
	syncSb ();
}

void FileDialog::setFilters (const char *filters)
{
	FdExtra &x = fd_x (m_ent);
	x.filters = fd_filters (filters) > 0 && !m_folder ? filters : 0;
	x.filter = 0;
	x.fw = 0;
	if (x.filters)						// its drop-down: left of the buttons
	{
		x.fx = 12; x.fy = height - 40; x.fh = 28;
		x.fw = width - 12 - 184 - 12 - x.fx;
		if (x.fw > 300) x.fw = 300;
	}
	read (); invalidate (true);
}

void FileDialog::goUp ()			// the folder above (a volume's root: stays)
{
	if (fd_root (m_dir)) return;
	fd_parent (m_dir);
	if (m_dir[0] == '\0') fd_scopy (m_dir, "SD:/", sizeof m_dir);
	read (); m_lastRow = -1; invalidate (true);
}

void FileDialog::enter (const char *name)	// into a folder of this one ("SD1:": a volume)
{
	int n = 0; while (m_dir[n]) n++;
	int ln = 0; while (name[ln]) ln++;
	if (ln > 0 && name[ln - 1] == ':') { fd_scopy (m_dir, name, sizeof m_dir - 1); n = ln; m_dir[n++] = '/'; m_dir[n] = '\0'; }
	else
	{
		if (!(n > 0 && m_dir[n - 1] == '/') && n < 255) m_dir[n++] = '/';
		for (int k = 0; name[k] && n < 255; k++) m_dir[n++] = name[k];
		m_dir[n] = '\0';
	}
	read (); m_lastRow = -1; invalidate (true);
}

void FileDialog::syncSb ()
{
	int mt = m_count - m_rows; if (mt < 1) mt = 1;
	m_sb->vmax = mt; m_sb->value = m_top; m_sb->invalidate (true);
}

// A row selected (shown, its name in the box when it is a file); a second click within 0.4 s: as
// the button. row < 0 or past the end: nothing selected.
void FileDialog::click (int row)
{
	if (row < 0 || row >= m_count) { if (m_sel >= 0) { m_sel = -1; invalidate (true); } return; }
	unsigned now = kapi_get_ticks ();
	bool dbl = row == m_lastRow && now - m_lastTick < 40;
	m_lastRow = row; m_lastTick = now;
	m_sel = row;
	if (!m_isdir[row] && m_nameBox) m_nameBox->setText (m_ent[row]);
	if (m_sel < m_top) m_top = m_sel;
	if (m_sel >= m_top + m_rows) m_top = m_sel - m_rows + 1;
	syncSb ();
	invalidate (true);
	if (dbl) { m_lastRow = -1; onButton (1); }
}

void FileDialog::onButton (int tag)
{
	if (tag == 0) { close (0); return; }			// Cancel
	if (m_sel >= 0 && m_isdir[m_sel])			// a folder selected: opened (chosen, in a folder dialog)
	{
		bool typed = m_save && m_nameBox && m_nameBox->text[0] != '\0';
		if (!typed) { char name[FD_NAME]; fd_scopy (name, m_ent[m_sel], FD_NAME); enter (name); if (m_folder) close (1); return; }
	}
	if (m_folder) { if (m_dir[0] != '\0') close (1); return; }
	if (m_nameBox->text[0] == '\0' || m_dir[0] == '\0') return;	// (it needs a name)
	if (m_save)						// a name without an extension: the kind's first one
	{
		char kind[64], pats[256];
		bool dot = false;
		for (const char *p = m_nameBox->text; *p; p++) if (*p == '.') dot = true;
		if (!dot && fd_filter (fd_x (m_ent).filters, fd_x (m_ent).filter, kind, sizeof kind, pats, sizeof pats)
		    && pats[0] == '*' && pats[1] == '.' && pats[2] != '*' && pats[2] != '\0')
		{
			char name[FD_NAME]; int n = 0;
			for (; m_nameBox->text[n] && n < FD_NAME - 12; n++) name[n] = m_nameBox->text[n];
			for (int k = 1; pats[k] && pats[k] != ';' && n < FD_NAME - 1; k++) name[n++] = pats[k];
			name[n] = '\0';
			m_nameBox->setText (name);
		}
	}
	close (1);
}

bool FileDialog::onKey (long k)
{
	if (k == KEY_ENTER) { onButton (1); return true; }	// (the name box's: dlg_enter)
	if (k == 27) { onButton (0); return true; }
	if (k == KEY_UP || k == KEY_DOWN)
	{
		int row = k == KEY_UP ? (m_sel > 0 ? m_sel - 1 : 0) : (m_sel < m_count - 1 ? m_sel + 1 : m_count - 1);
		m_lastRow = -1;					// (not a double click)
		click (row);
		m_lastRow = -1;
		return true;
	}
	if (k == KEY_BACKSPACE && (m_nameBox == 0 || m_nameBox->text[0] == '\0')) { goUp (); return true; }
	return false;
}

bool FileDialog::onMouse (int mx, int my, int bl, int, int, int wheel)
{
	FdExtra &x = fd_x (m_ent);
	if (mx < 0)
	{
		pressed = false;
		if (x.hot >= 0 || x.hotVol >= 0 || x.upHot) { x.hot = x.hotVol = -1; x.upHot = 0; invalidate (true); }
		return true;
	}
	if (wheel)						// the wheel scrolls the list
	{
		int mt = m_count - m_rows; if (mt < 0) mt = 0;
		int nt = m_top - wheel * 3; if (nt < 0) nt = 0; if (nt > mt) nt = mt;
		if (nt != m_top) { m_top = nt; syncSb (); invalidate (true); }
		return true;
	}
	bool inList = mx >= m_lx && mx < m_lx + m_lw && my >= m_ly + 2 && my < m_ly + m_lh;
	bool inVol = x.vw && mx >= x.vx && mx < x.vx + x.vw && my >= m_ly + 4 && my < m_ly + m_lh;
	bool onUp = mx >= x.ux && mx < x.ux + x.uw && my >= x.uy && my < x.uy + x.uh;
	int hot = inList ? m_top + (my - m_ly - 2) / m_rowH : -1; if (hot >= m_count) hot = -1;
	int hv = inVol ? (my - m_ly - 4) / m_rowH : -1; if (hv >= x.nvol) hv = -1;
	if (hot != x.hot || hv != x.hotVol || (char) onUp != x.upHot) { x.hot = hot; x.hotVol = hv; x.upHot = onUp; invalidate (true); }
	bool onKind = x.fw && mx >= x.fx && mx < x.fx + x.fw && my >= x.fy && my < x.fy + x.fh;
	if (bl && !pressed && onKind)				// the kinds' menu, under the drop-down
	{
		static char labels[12][96];
		PopupMenu menu (left + x.fx, top + x.fy + x.fh);
		int n = fd_filters (x.filters); if (n > 12) n = 12;
		for (int i = 0; i < n; i++)
		{
			char pats[256];
			fd_filter (x.filters, i, labels[i], 64, pats, sizeof pats);
			menu.add (labels[i], i + 1);
		}
		int r = menu.run ();
		if (r >= 1 && r - 1 != x.filter) { x.filter = r - 1; read (); }
		invalidate (true);
		return true;
	}
	if (bl && !pressed)					// the press
	{
		pressed = true;
		if (onUp) { x.upDown = 1; invalidate (true); }
		else if (inList) click (m_top + (my - m_ly - 2) / m_rowH);
		else if (hv >= 0) { char v[8]; fd_scopy (v, x.vol[hv], 8); enter (v); }
	}
	else if (!bl)
	{
		if (pressed && x.upDown) { x.upDown = 0; invalidate (true); if (onUp) goUp (); }
		pressed = false;
	}
	return true;						// modal: everything is its own
}

void FileDialog::onDraw ()
{
	FdExtra &x = fd_x (m_ent);
	int fh = uk_fh ();
	drawBox (TR (m_folder ? "Choose folder" : m_save ? "Save file" : "Open file"));

	// the Up button (an arrow), the path
	bool root = fd_root (m_dir);
	uk_raised (canvas, x.ux, x.uy, x.uw, x.uh, 5, C_FACE, root ? UK_DISABLED : x.upDown ? UK_PRESSED : x.upHot ? UK_HOT : UK_NORMAL);
	{
		unsigned c = root ? C_DIS : C_TEXT;
		int cx = x.ux + x.uw / 2, cy = x.uy + x.uh / 2 - 6 + (x.upDown ? 1 : 0);
		for (int i = 0; i < 6; i++) canvas.fillRect (cx - i, cy + i, 2 * i + 1, 1, c);	// the head
		canvas.fillRect (cx - 1, cy + 6, 3, 6, c);						// the stem
	}
	uk_sunken (canvas, x.px, x.uy, x.pw, x.uh, 5, C_FIELD, false);
	{
		const char *p = m_dir; int avail = x.pw - 40;
		bool cut = false;
		while (*p && uk_text_w (p) > avail - (cut ? uk_text_w ("...") : 0)) { p++; cut = true; }
		fd_icon_folder (canvas, x.px + 8, x.uy + (x.uh - 15) / 2);
		int tx = x.px + 32, ty = x.uy + (x.uh - fh) / 2;
		if (cut) { canvas.text (tx, ty, "...", C_DIS); tx += uk_text_w ("..."); }
		canvas.text (tx, ty, p, C_FIELD_TEXT);
	}

	// the volumes
	if (x.vw)
	{
		uk_sunken (canvas, x.vx, m_ly, x.vw, m_lh, 5, C_FIELD, false);
		for (int i = 0; i < x.nvol; i++)
		{
			int ry = m_ly + 4 + i * m_rowH;
			int n = 0; while (x.vol[i][n]) n++;
			bool cur = true;
			for (int k = 0; k < n; k++) if (fd_low (m_dir[k]) != fd_low (x.vol[i][k])) cur = false;
			if (cur) uk_hilite (canvas, x.vx + 3, ry, x.vw - 6, m_rowH, 4, false);
			else if (i == x.hotVol) canvas.fillRect (x.vx + 3, ry, x.vw - 6, m_rowH, uk_tone (C_FIELD, 118));
			fd_icon_drive (canvas, x.vx + 9, ry + (m_rowH - 15) / 2);
			canvas.text (x.vx + 32, ry + (m_rowH - fh) / 2, x.vol[i], cur ? uk_hilite_ink (false) : C_FIELD_TEXT);
		}
	}

	// the folder's content
	uk_sunken (canvas, m_lx, m_ly, m_lw, m_lh, 5, C_FIELD, false);
	if (m_count == 0)
	{
		const char *t = TR ("This folder is empty");
		canvas.text (m_lx + (m_lw - uk_text_w (t)) / 2, m_ly + m_lh / 2 - fh / 2, t, C_DIS);
	}
	for (int r = 0; r < m_rows; r++)
	{
		int idx = m_top + r; if (idx >= m_count) break;
		int ry = m_ly + 2 + r * m_rowH;
		bool s = idx == m_sel;
		if (s) uk_hilite (canvas, m_lx + 3, ry, m_lw - 6, m_rowH, 4, true);
		else if (idx == x.hot) canvas.fillRect (m_lx + 3, ry, m_lw - 6, m_rowH, uk_tone (C_FIELD, 118));
		int right = m_lx + m_lw - 10;
		if (m_isdir[idx]) fd_icon_folder (canvas, m_lx + 9, ry + (m_rowH - 15) / 2);
		else
		{
			fd_icon_file (canvas, m_lx + 9, ry + (m_rowH - 15) / 2);
			char sz[24]; fd_size_text (fd_size (m_ent, idx), sz);
			int sw = uk_text_w (sz);
			canvas.text (right - sw, ry + (m_rowH - fh) / 2, sz, s ? C_SEL_TEXT : C_DIS);
			right -= sw + 14;
		}
		fd_text_fit (canvas, m_lx + 32, ry + (m_rowH - fh) / 2, m_ent[idx], right - (m_lx + 32), s ? C_SEL_TEXT : C_FIELD_TEXT);
	}

	// the name's label; how many there are
	if (x.ny) canvas.text (m_lx, x.ny + 5, TR ("Name:"), C_TEXT);
	if (x.fw)						// the kind of files: a drop-down
	{
		char kind[64], pats[256], label[330]; int k = 0;
		fd_filter (x.filters, x.filter, kind, sizeof kind, pats, sizeof pats);
		for (int i = 0; kind[i] && k < 64; i++) label[k++] = kind[i];
		if (pats[0] && !(pats[0] == '*' && pats[1] == '\0')) { label[k++] = ' '; label[k++] = '('; for (int i = 0; pats[i] && k < 325; i++) label[k++] = pats[i]; label[k++] = ')'; }
		label[k] = '\0';
		uk_raised (canvas, x.fx, x.fy, x.fw, x.fh, 5, C_FACE, UK_NORMAL);
		fd_text_fit (canvas, x.fx + 10, x.fy + (x.fh - fh) / 2, label, x.fw - 34, C_TEXT);
		int ax = x.fx + x.fw - 16, ay = x.fy + x.fh / 2 - 2;
		for (int i = 0; i < 5; i++) canvas.fillRect (ax - 4 + i, ay + i, 9 - 2 * i, 1, C_TEXT);	// the arrow
	}
	else
	{
		char t[48]; int n = m_count, i = 0, k = 0; char d[8];
		do { d[i++] = (char) ('0' + n % 10); n /= 10; } while (n);
		while (i > 0) t[k++] = d[--i];
		t[k++] = ' ';
		const char *w = TR (m_count == 1 ? "item" : "items");
		for (; *w && k < 46; w++) t[k++] = *w;
		t[k] = '\0';
		canvas.text (12, height - 40 + (28 - fh) / 2, t, C_DIS);
	}
}

void FileDialog::getResult (char *out, unsigned cap)
{
	const char *nm = m_folder ? "" : m_nameBox->text;
	unsigned n = 0;
	for (; m_dir[n] && n < cap - 1; n++) out[n] = m_dir[n];
	if (nm[0] && n > 0 && out[n - 1] != '/' && n < cap - 1) out[n++] = '/';
	for (int k = 0; nm[k] && n < cap - 1; k++) out[n++] = nm[k];
	out[n] = '\0';
}

// ---- convenience -------------------------------------------------------------
// ---- PopupMenu ------------------------------------------------------------------------------------
enum { PM_PAD = 4, PM_SEP = 9 };
static int pm_row () { return uk_fh () + 8; }

PopupMenu::PopupMenu (int x, int y) : Modal (120, 2 * PM_PAD), m_n (0), m_hot (-1) { left = x; top = y; }

void PopupMenu::add (const char *label, int id, bool enabled, const char *hint)
{
	if (m_n >= MAXI) return;
	m_label[m_n] = label; m_hint[m_n] = hint; m_id[m_n] = id; m_on[m_n] = enabled; m_sep[m_n] = false;
	m_n++;
}

void PopupMenu::separator ()
{
	if (m_n >= MAXI) return;
	m_label[m_n] = 0; m_hint[m_n] = 0; m_id[m_n] = -1; m_on[m_n] = false; m_sep[m_n] = true;
	m_n++;
}

int PopupMenu::rowY (int i) const
{
	int y = PM_PAD;
	for (int k = 0; k < i; k++) y += m_sep[k] ? PM_SEP : pm_row ();
	return y;
}

int PopupMenu::rowAt (int mx, int my) const
{
	if (mx < 0 || mx >= width) return -1;
	for (int i = 0; i < m_n; i++)
	{
		int y = rowY (i), h = m_sep[i] ? PM_SEP : pm_row ();
		if (my >= y && my < y + h) return m_sep[i] || !m_on[i] ? -1 : i;
	}
	return -1;
}

int PopupMenu::run ()
{
	int w = 0, lw = 0, hw = 0;
	for (int i = 0; i < m_n; i++)
		if (!m_sep[i])
		{
			int a = uk_text_w (m_label[i]), b = m_hint[i] ? uk_text_w (m_hint[i]) : 0;
			if (a > lw) lw = a;
			if (b > hw) hw = b;
		}
	w = 14 + lw + (hw ? 24 + hw : 0) + 14;
	if (w < 150) w = 150;
	int h = rowY (m_n) + PM_PAD;
	Root *r = Root::current ();
	if (r)							// (in the window)
	{
		if (left + w > r->width) left = r->width - w;
		if (top + h > r->height) top = r->height - h;
		if (left < 0) left = 0;
		if (top < 0) top = 0;
	}
	resizeTo (w, h);
	int res = Modal::run ();
	return res > 0 ? m_id[res - 1] : -1;
}

void PopupMenu::onDraw ()
{
	int fh = uk_fh ();
	canvas.clear (UK_TRANSPARENT_KEY);
	uk_popup (canvas, 0, 0, width, height, 7, C_FIELD);
	for (int i = 0; i < m_n; i++)
	{
		int y = rowY (i);
		if (m_sep[i]) { uk_etch_h (canvas, 8, y + PM_SEP / 2 - 1, width - 16, C_FIELD); continue; }
		bool hot = i == m_hot;
		if (hot) uk_hilite (canvas, PM_PAD, y, width - 2 * PM_PAD, pm_row (), 5, true);
		unsigned ink = !m_on[i] ? uk_mix (C_FIELD, C_FIELD_TEXT, 110) : hot ? C_SEL_TEXT : C_FIELD_TEXT;
		canvas.text (14, y + (pm_row () - fh) / 2, m_label[i], ink);
		if (m_hint[i])
			canvas.text (width - 14 - uk_text_w (m_hint[i]), y + (pm_row () - fh) / 2, m_hint[i],
				     hot ? C_SEL_TEXT : uk_mix (C_FIELD, C_FIELD_TEXT, 150));
	}
}

bool PopupMenu::onMouse (int mx, int my, int bl, int, int, int)
{
	int hot = (mx >= 0 && my >= 0 && mx < width && my < height) ? rowAt (mx, my) : -1;
	if (hot != m_hot) { m_hot = hot; invalidate (true); }
	if (bl && !pressed)
	{
		pressed = true;
		if (mx < 0 || my < 0 || mx >= width || my >= height) close (0);	// elsewhere: nothing
	}
	else if (!bl && pressed)
	{
		pressed = false;
		if (hot >= 0) close (hot + 1);
	}
	return true;
}

bool PopupMenu::onKey (long k)
{
	if (k == 27) { close (0); return true; }
	if (k == KEY_DOWN || k == KEY_UP)
	{
		int i = m_hot;
		for (int n = 0; n < m_n; n++)
		{
			i = k == KEY_DOWN ? (i + 1) % m_n : (i <= 0 ? m_n - 1 : i - 1);
			if (!m_sep[i] && m_on[i]) break;
		}
		m_hot = i; invalidate (true);
		return true;
	}
	if (k == KEY_ENTER && m_hot >= 0) { close (m_hot + 1); return true; }
	return true;
}

int uk_messagebox (const char *title, const char *text, int buttons)
{ MessageBox m (title, text, buttons); return m.run (); }

bool uk_file_open (char *out, unsigned cap, const char *startDir, const char *filters)
{ FileDialog d (startDir, 0, false); d.setFilters (filters); if (d.run () == 1) { d.getResult (out, cap); return true; } return false; }

bool uk_file_save (char *out, unsigned cap, const char *startDir, const char *defName, const char *filters)
{ FileDialog d (startDir, defName, true); d.setFilters (filters); if (d.run () == 1) { d.getResult (out, cap); return true; } return false; }

// (the functions as the programs built before 2026-10-05 call them, without the kinds: the library's
// table keeps their names)
bool uk_file_open (char *out, unsigned cap, const char *startDir);
bool uk_file_open (char *out, unsigned cap, const char *startDir) { return uk_file_open (out, cap, startDir, 0); }
bool uk_file_save (char *out, unsigned cap, const char *startDir, const char *defName);
bool uk_file_save (char *out, unsigned cap, const char *startDir, const char *defName) { return uk_file_save (out, cap, startDir, defName, 0); }

bool uk_folder_open (char *out, unsigned cap, const char *startDir)
{ FileDialog d (startDir, 0, false, true); if (d.run () == 1) { d.getResult (out, cap); return true; } return false; }

// ---- ColorDialog ---------------------------------------------------------------------------
static const unsigned CD_PAL[16] = {
	0x00000000, 0x00808080, 0x00C0C0C0, 0x00FFFFFF, 0x00800000, 0x00FF0000, 0x00FF8000, 0x00FFFF00,
	0x00008000, 0x0000FF00, 0x00008080, 0x0000FFFF, 0x00000080, 0x000000FF, 0x00800080, 0x00FF00FF };
enum { CD_W = 360, CD_H = 248, CD_SX = 44, CD_SW = 170, CD_PY = 142, CD_PC = 20, CD_Y0 = 40 };

static void cd_slide (Widget &w)
{
	ColorDialog *d = (ColorDialog *) w.parent;
	d->color = ((unsigned) d->r->value << 16) | ((unsigned) d->g->value << 8) | (unsigned) d->b->value;
	d->invalidate (true);
}

ColorDialog::ColorDialog (unsigned initial, const char *title)
  : Modal (CD_W, CD_H), color (initial & 0xFFFFFF), orig (initial & 0xFFFFFF), m_title (title)
{
	Root *rt = Root::current ();
	int W = rt ? rt->width : width, H = rt ? rt->height : height;
	left = (W - width) / 2; top = (H - height) / 2;
	int fh = uk_fh ();
	r = new Slider (CD_SX, CD_Y0,           CD_SW, fh + 6, 0, 255, (color >> 16) & 255, cd_slide, C_FACE); addChild (r);
	g = new Slider (CD_SX, CD_Y0 + fh + 12, CD_SW, fh + 6, 0, 255, (color >> 8) & 255,  cd_slide, C_FACE); addChild (g);
	b = new Slider (CD_SX, CD_Y0 + 2 * (fh + 12), CD_SW, fh + 6, 0, 255, color & 255,  cd_slide, C_FACE); addChild (b);
	Button *bt;
	bt = new Button (width - 180, height - 38, 82, 28, TR ("OK"),     dlg_btn); bt->tag = 1; addChild (bt);
	bt = new Button (width - 92,  height - 38, 82, 28, TR ("Cancel"), dlg_btn); bt->tag = 0; addChild (bt);
}

void ColorDialog::syncSliders ()
{
	r->value = (color >> 16) & 255; g->value = (color >> 8) & 255; b->value = color & 255;
	r->invalidate (true); g->invalidate (true); b->invalidate (true);
}

void ColorDialog::onDraw ()
{
	int fh = uk_fh ();
	drawBox (m_title);
	const char *lab[3] = { "R", "G", "B" };
	for (int i = 0; i < 3; i++)
	{
		int y = CD_Y0 + i * (fh + 12) + 3;
		canvas.text (20, y, lab[i], C_TEXT);
		int v = i == 0 ? (color >> 16) & 255 : i == 1 ? (color >> 8) & 255 : color & 255;
		char n[4] = { (char) ('0' + v / 100), (char) ('0' + v / 10 % 10), (char) ('0' + v % 10), 0 };
		canvas.text (CD_SX + CD_SW + 8, y, n, C_TEXT);
	}
	int px = CD_SX + CD_SW + 44, pw = width - px - 12, ph = 3 * (fh + 12) - 6;	// preview: old | new
	uk_rbox (canvas, px, CD_Y0, pw, ph, 5, orig, orig);
	uk_rbox (canvas, px + pw / 2, CD_Y0, pw - pw / 2, ph, 5, color, color, 255, UK_TR | UK_BR);
	uk_rline (canvas, px, CD_Y0, pw, ph, 5, uk_tone (C_FACE, 60), 220);
	static const char *HX = "0123456789ABCDEF";
	char hex[8] = { '#', HX[(color >> 20) & 15], HX[(color >> 16) & 15], HX[(color >> 12) & 15],
			HX[(color >> 8) & 15], HX[(color >> 4) & 15], HX[color & 15], 0 };
	canvas.text (px, CD_Y0 + ph + 4, hex, C_TEXT);
	for (int i = 0; i < 16; i++)				// the palette
	{
		int x = 20 + (i % 8) * (CD_PC + 4), y = CD_PY + 8 + (i / 8) * (CD_PC + 4);
		uk_rbox (canvas, x, y, CD_PC, CD_PC, 4, CD_PAL[i], CD_PAL[i]);
		uk_rline (canvas, x, y, CD_PC, CD_PC, 4, CD_PAL[i] == color ? C_ACCENT : uk_tone (C_FACE, 70), CD_PAL[i] == color ? 255 : 170);
		if (CD_PAL[i] == color) uk_rline (canvas, x - 1, y - 1, CD_PC + 2, CD_PC + 2, 5, C_ACCENT, 160);
	}
}

bool ColorDialog::onMouse (int mx, int my, int bl, int, int, int)
{
	if (mx < 0 || !bl || pressed) { if (!bl) pressed = false; return true; }
	pressed = true;
	for (int i = 0; i < 16; i++)
	{
		int x = 20 + (i % 8) * (CD_PC + 4), y = CD_PY + 8 + (i / 8) * (CD_PC + 4);
		if (mx >= x && mx < x + CD_PC && my >= y && my < y + CD_PC)
		{ color = CD_PAL[i]; syncSliders (); invalidate (true); break; }
	}
	return true;
}

bool ColorDialog::onKey (long k)
{
	if (k == KEY_ENTER) { close (1); return true; }
	if (k == 27)        { close (0); return true; }
	return false;
}

bool uk_color_dialog (unsigned *color, const char *title)
{
	ColorDialog d (color ? *color : 0, title);
	if (!d.run ()) return false;
	if (color) *color = d.color;
	return true;
}

} // namespace uikit
