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
#include "applib.h"		// should_exit, pump_events, msleep
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
static int mb_w () { Root *r = Root::current (); int W = r ? r->width  : 320; int w = 320; if (w > W - 20) w = W - 20; return w; }
static int mb_h () { Root *r = Root::current (); int H = r ? r->height : 130; int h = 130; if (h > H - 20) h = H - 20; return h; }

MessageBox::MessageBox (const char *title, const char *text, int buttons)
  : Modal (mb_w (), mb_h ()), m_title (title), m_text (text), m_def (1)
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
	const char *p = m_text; int line = 0; char buf[96];
	while (*p)
	{
		int n = 0; while (p[n] && p[n] != '\n' && n < 95) n++;
		for (int i = 0; i < n; i++) buf[i] = p[i];
		buf[n] = '\0';
		canvas.text (14, titleH () + 10 + line * (fh + 2), buf, C_TEXT);
		p += n; if (*p == '\n') p++;
		line++;
	}
}

// ---- FileDialog --------------------------------------------------------------
static void fd_scopy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }
static bool fd_dotdot (const char *s) { return s[0] == '.' && s[1] == '.' && s[2] == '\0'; }

static int fd_w () { Root *r = Root::current (); int W = r ? r->width  : 360; int w = 360; if (w > W - 20) w = W - 20; return w; }
static int fd_h () { Root *r = Root::current (); int H = r ? r->height : 300; int h = 300; if (h > H - 20) h = H - 20; return h; }

// The volumes the list shows above a volume root ("..": the SD card's FAT/exFAT partitions).
#ifdef _WIN32		// (the Windows build of the apps, pc/Koton: SD: is the program's folder, then the PC's drives)
static const char *const FD_VOLS[] = { "SD:", "C:", "D:", "E:", "F:", "G:", "H:", "I:", "J:", "K:" };
#elif defined(__APPLE__)	// (the macOS build, pc/macOS: SD: is ~/Documents/Onyx Ledger, HOME: the user's folder, MAC: the Mac's /)
static const char *const FD_VOLS[] = { "SD:", "HOME:", "MAC:" };
#else
static const char *const FD_VOLS[] = { "SD:", "SD1:", "SD2:", "SD3:", "USB:", "USB2:", "USB3:" };
#endif

FileDialog::FileDialog (const char *startDir, const char *defName, bool save, bool folder)
  : Modal (fd_w (), fd_h ()), m_save (save), m_folder (folder)
{
	fd_scopy (m_dir, (startDir && startDir[0]) ? startDir : "SD:/", sizeof m_dir);
	Root *r = Root::current ();
	int W = r ? r->width : width, H = r ? r->height : height;
	left = (W - width) / 2; top = (H - height) / 2;
	int fh = uk_fh ();
	m_rowH = fh + 4;
	m_lx = 10; m_ly = titleH () + fh + 12;
	m_lw = width - 20 - 14; m_lh = height - m_ly - 78;
	m_rows = (m_lh - 4) / m_rowH; if (m_rows < 1) m_rows = 1;
	m_count = 0; m_sel = -1; m_top = 0;
	m_lastRow = -1; m_lastTick = 0;

	m_sb = new Scrollbar (m_lx + m_lw + 2, m_ly, 12, m_lh, true, 1, 0, dlg_scr); addChild (m_sb);
	m_nameBox = 0;
	if (!folder) { m_nameBox = new Textbox (10, m_ly + m_lh + 8, width - 20, fh + 8, defName ? defName : "", dlg_enter); addChild (m_nameBox); }
	if (m_nameBox && save) m_nameBox->hasFocus = true;	// (a save: the name typed at once; run () focuses the box)
	int by = height - 36;
	Button *b;
	b = new Button (width - 180, by, 82, 28, TR (folder ? "Choose" : save ? "Save" : "Open"), dlg_btn); b->tag = 1; addChild (b);
	b = new Button (width - 92,  by, 82, 28, TR ("Cancel"), dlg_btn);               b->tag = 0; addChild (b);
	read ();
}

void FileDialog::read ()
{
	m_count = 0; m_top = 0; m_sel = -1;
	if (m_dir[0] == '\0')					// the volume list: the mounted ones
	{
		for (unsigned i = 0; i < sizeof FD_VOLS / sizeof FD_VOLS[0]; i++)
		{
			char root[8]; fd_scopy (root, FD_VOLS[i], 6);
			int n = 0; while (root[n]) n++; root[n] = '/'; root[n + 1] = '\0';
			void *d = kapi_opendir (root);
			if (d == 0) continue;
			kapi_closedir (d);
			fd_scopy (m_ent[m_count], FD_VOLS[i], 96); m_isdir[m_count] = 1; m_count++;
		}
		syncSb ();
		return;
	}
	fd_scopy (m_ent[0], "..", 96); m_isdir[0] = 1; m_count = 1;
	void *d = kapi_opendir (m_dir);
	if (d != 0)
	{
		struct kapi_dirent e;
		while (m_count < MAXENT && kapi_readdir (d, &e))
		{ fd_scopy (m_ent[m_count], e.name, 96); m_isdir[m_count] = e.is_dir ? 1 : 0; m_count++; }
		kapi_closedir (d);
	}
	syncSb ();
}
void FileDialog::goUp ()
{
	int n = 0; while (m_dir[n]) n++;
	if (n > 0 && m_dir[n - 1] == '/') n--;
	if (n > 0 && m_dir[n - 1] == ':') { m_dir[0] = '\0'; return; }	// volume root -> volume list
	while (n > 0 && m_dir[n - 1] != '/') n--;
	if (n > 0) n--;
	if (n > 0 && m_dir[n - 1] == ':') n++;				// keep the root's '/': "SD1:/"
	m_dir[n] = '\0';
	if (n == 0) fd_scopy (m_dir, "SD:/", sizeof m_dir);
}
void FileDialog::enter (const char *name)
{
	int n = 0; while (m_dir[n]) n++;
	if (n == 0)							// a volume from the volume list
	{ fd_scopy (m_dir, name, sizeof m_dir - 1); n = 0; while (m_dir[n]) n++; m_dir[n++] = '/'; m_dir[n] = '\0'; return; }
	if (!(n > 0 && m_dir[n - 1] == '/') && n < 255) m_dir[n++] = '/';
	for (int k = 0; name[k] && n < 255; k++) m_dir[n++] = name[k];
	m_dir[n] = '\0';
}
void FileDialog::syncSb ()
{
	int mt = m_count - m_rows; if (mt < 1) mt = 1;
	m_sb->vmax = mt; m_sb->value = m_top; m_sb->invalidate (true);
}
void FileDialog::click (int row)
{
	if (row < 0 || row >= m_count) return;
	unsigned now = kapi_get_ticks ();
	bool dbl = row == m_lastRow && now - m_lastTick < 40;		// (0.4 s)
	m_lastRow = row; m_lastTick = now;
	if (m_isdir[row]) { if (fd_dotdot (m_ent[row])) goUp (); else enter (m_ent[row]); read (); m_lastRow = -1; }
	else
	{
		m_sel = row; if (m_nameBox) m_nameBox->setText (m_ent[row]);
		if (dbl) { m_lastRow = -1; onButton (1); }		// a double click: as Open / Save
	}
	invalidate (true);
}

void FileDialog::onButton (int tag)
{
	if (tag == 0) { close (0); return; }			// Cancel
	if (m_folder) { if (m_dir[0] != '\0') close (1); return; }	// Choose (needs a folder)
	if (m_nameBox->text[0] != '\0' && m_dir[0] != '\0') close (1);	// OK (needs a filename)
}

bool FileDialog::onKey (long k)
{
	if (k == KEY_ENTER) { onButton (1); return true; }	// (the name box's: dlg_enter)
	if (k == 27) { onButton (0); return true; }
	return false;
}

bool FileDialog::onMouse (int mx, int my, int bl, int, int, int wheel)
{
	if (mx < 0) { pressed = false; return true; }
	if (wheel)						// wheel scrolls the file list
	{
		int mt = m_count - m_rows; if (mt < 0) mt = 0;
		int nt = m_top - wheel; if (nt < 0) nt = 0; if (nt > mt) nt = mt;
		if (nt != m_top) { m_top = nt; syncSb (); invalidate (true); }
		return true;
	}
	if (bl && !pressed)					// press edge in the list area
	{
		pressed = true;
		if (mx >= m_lx && mx < m_lx + m_lw && my >= m_ly + 2 && my < m_ly + m_lh)
			click (m_top + (my - m_ly - 2) / m_rowH);
	}
	else if (!bl) pressed = false;
	return true;						// modal: consume everything
}

void FileDialog::onDraw ()
{
	int fh = uk_fh ();
	drawBox (TR (m_folder ? "Choose folder" : m_save ? "Save file" : "Open file"));
	canvas.text (12, titleH () + 6, m_dir[0] ? m_dir : TR ("Volumes"), C_DIS);

	uk_sunken (canvas, m_lx, m_ly, m_lw, m_lh, 4, C_FIELD, false);
	for (int r = 0; r < m_rows; r++)
	{
		int idx = m_top + r; if (idx >= m_count) break;
		int ry = m_ly + 2 + r * m_rowH;
		bool s = idx == m_sel;
		if (s) uk_hilite (canvas, m_lx + 3, ry, m_lw - 6, m_rowH, 4, true);
		char row[100]; int k = 0;
		for (; m_ent[idx][k] && k < 96; k++) row[k] = m_ent[idx][k];
		if (m_isdir[idx] && !fd_dotdot (m_ent[idx]) && k < 97) row[k++] = '/';
		row[k] = '\0';
		canvas.text (m_lx + 8, ry + (m_rowH - fh) / 2, row, s ? C_SEL_TEXT : m_isdir[idx] ? uk_tone (C_ACCENT, 84) : C_FIELD_TEXT);
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

bool uk_file_open (char *out, unsigned cap, const char *startDir)
{ FileDialog d (startDir, 0, false); if (d.run () == 1) { d.getResult (out, cap); return true; } return false; }

bool uk_file_save (char *out, unsigned cap, const char *startDir, const char *defName)
{ FileDialog d (startDir, defName, true); if (d.run () == 1) { d.getResult (out, cap); return true; } return false; }

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
