//
// wtk/dialog.cpp -- modal dialogs (Modal / MessageBox / FileDialog), compiled into
// libwtk.a. A dialog is a normal wtk Widget (its canvas IS the dialog box) added as the
// topmost child of the Root and flagged `modal`; Root then routes ALL input to it (see
// Widget::handleMouse/handleKey) until run() removes it. Built from wtk controls
// (Button/Textbox/Scrollbar) so dialogs match the rest of the UI.
//
#include "wtk/dialog.h"
#include "wtk/root.h"
#include "wtk/button.h"
#include "kapi.h"		// MB_*, KEY_*, kapi_present, kapi_opendir/readdir
#include "applib.h"		// should_exit, pump_events, msleep
// operator new[]/delete[] resolve at link from the app's onyxpp.hpp (see canvas.cpp).

namespace wtk {

// A dialog control's parent IS the dialog (a Modal): route its callback there.
static void dlg_btn (Widget &w) { if (w.parent) ((Modal *) w.parent)->onButton (w.tag); }
static void dlg_scr (Widget &w) { if (w.parent) ((Modal *) w.parent)->onScroll (((Scrollbar &) w).value); }

// ---- Modal -------------------------------------------------------------------
Modal::Modal (int w, int h) : Widget (0, 0, w, h), done (false), result (0) { modal = true; }

int Modal::run ()
{
	Root *r = Root::current ();
	if (r == 0) return 0;
	r->addChild (this);			// becomes the topmost (modal) child of the window
	done = false; hasFocus = true; invalidate (true);
	while (!done && !should_exit ())
	{
		pump_events ();
		if (!r->valid) { r->draw (); kapi_present (); }
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
	if (buttons == MB_YESNO)
	{
		b = new Button (width - 180, by, 82, 28, "Yes", dlg_btn); b->tag = 1; addChild (b);
		b = new Button (width - 92,  by, 82, 28, "No",  dlg_btn); b->tag = 0; addChild (b);
	}
	else if (buttons == MB_OKCANCEL)
	{
		b = new Button (width - 180, by, 82, 28, "OK",     dlg_btn); b->tag = 1; addChild (b);
		b = new Button (width - 92,  by, 82, 28, "Cancel", dlg_btn); b->tag = 0; addChild (b);
	}
	else
	{
		b = new Button (width - 92, by, 82, 28, "OK", dlg_btn); b->tag = 1; addChild (b);
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
	int fh = wk_fh ();
	canvas.clear (C_FACE_DN);
	canvas.frameRect (0, 0, width, height, C_ACCENT);
	canvas.fillRect (0, 0, width, fh + 8, C_FACE);
	canvas.text (8, 4, m_title, C_TEXT);
	const char *p = m_text; int line = 0; char buf[96];
	while (*p)
	{
		int n = 0; while (p[n] && p[n] != '\n' && n < 95) n++;
		for (int i = 0; i < n; i++) buf[i] = p[i];
		buf[n] = '\0';
		canvas.text (10, fh + 14 + line * (fh + 2), buf, C_TEXT);
		p += n; if (*p == '\n') p++;
		line++;
	}
}

// ---- FileDialog --------------------------------------------------------------
static void fd_scopy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }
static bool fd_dotdot (const char *s) { return s[0] == '.' && s[1] == '.' && s[2] == '\0'; }

static int fd_w () { Root *r = Root::current (); int W = r ? r->width  : 360; int w = 360; if (w > W - 20) w = W - 20; return w; }
static int fd_h () { Root *r = Root::current (); int H = r ? r->height : 300; int h = 300; if (h > H - 20) h = H - 20; return h; }

FileDialog::FileDialog (const char *startDir, const char *defName, bool save)
  : Modal (fd_w (), fd_h ()), m_save (save)
{
	fd_scopy (m_dir, (startDir && startDir[0]) ? startDir : "SD:/", sizeof m_dir);
	Root *r = Root::current ();
	int W = r ? r->width : width, H = r ? r->height : height;
	left = (W - width) / 2; top = (H - height) / 2;
	int fh = wk_fh ();
	m_rowH = fh + 2;
	m_lx = 10; m_ly = fh + 24;
	m_lw = width - 20 - 14; m_lh = height - m_ly - 78;
	m_rows = m_lh / m_rowH; if (m_rows < 1) m_rows = 1;
	m_count = 0; m_sel = -1; m_top = 0;

	m_sb = new Scrollbar (m_lx + m_lw + 2, m_ly, 12, m_lh, true, 1, 0, dlg_scr); addChild (m_sb);
	m_nameBox = new Textbox (10, m_ly + m_lh + 8, width - 20, fh + 8, defName ? defName : ""); addChild (m_nameBox);
	int by = height - 36;
	Button *b;
	b = new Button (width - 180, by, 82, 28, save ? "Save" : "Open", dlg_btn); b->tag = 1; addChild (b);
	b = new Button (width - 92,  by, 82, 28, "Cancel", dlg_btn);               b->tag = 0; addChild (b);
	read ();
}

void FileDialog::read ()
{
	m_count = 0; m_top = 0; m_sel = -1;
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
	while (n > 0 && m_dir[n - 1] != '/') n--;
	if (n > 0) n--;
	m_dir[n] = '\0';
	if (n < 4) fd_scopy (m_dir, "SD:/", sizeof m_dir);
}
void FileDialog::enter (const char *name)
{
	int n = 0; while (m_dir[n]) n++;
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
	if (m_isdir[row]) { if (fd_dotdot (m_ent[row])) goUp (); else enter (m_ent[row]); read (); }
	else { m_sel = row; m_nameBox->setText (m_ent[row]); }
	invalidate (true);
}

void FileDialog::onButton (int tag)
{
	if (tag == 0) { close (0); return; }			// Cancel
	if (m_nameBox->text[0] != '\0') close (1);		// OK (needs a filename)
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
		if (mx >= m_lx && mx < m_lx + m_lw && my >= m_ly && my < m_ly + m_lh)
			click (m_top + (my - m_ly - 1) / m_rowH);
	}
	else if (!bl) pressed = false;
	return true;						// modal: consume everything
}

void FileDialog::onDraw ()
{
	int fh = wk_fh ();
	canvas.clear (C_FACE_DN);
	canvas.frameRect (0, 0, width, height, C_ACCENT);
	canvas.fillRect (0, 0, width, fh + 8, C_FACE);
	canvas.text (8, 4, m_save ? "Save file" : "Open file", C_TEXT);
	canvas.text (10, fh + 12, m_dir, C_DIS);

	canvas.fillRect (m_lx, m_ly, m_lw, m_lh, C_FIELD);
	canvas.frameRect (m_lx, m_ly, m_lw, m_lh, C_BORDER);
	for (int r = 0; r < m_rows; r++)
	{
		int idx = m_top + r; if (idx >= m_count) break;
		int ry = m_ly + 1 + r * m_rowH;
		if (idx == m_sel) canvas.fillRect (m_lx + 1, ry, m_lw - 2, m_rowH, C_FACE_HI);
		char row[100]; int k = 0;
		for (; m_ent[idx][k] && k < 96; k++) row[k] = m_ent[idx][k];
		if (m_isdir[idx] && !fd_dotdot (m_ent[idx]) && k < 97) row[k++] = '/';
		row[k] = '\0';
		canvas.text (m_lx + 4, ry, row, m_isdir[idx] ? C_ACCENT : C_TEXT);
	}
}

void FileDialog::getResult (char *out, unsigned cap)
{
	const char *nm = m_nameBox->text;
	unsigned n = 0;
	for (; m_dir[n] && n < cap - 1; n++) out[n] = m_dir[n];
	if (n > 0 && out[n - 1] != '/' && n < cap - 1) out[n++] = '/';
	for (int k = 0; nm[k] && n < cap - 1; k++) out[n++] = nm[k];
	out[n] = '\0';
}

// ---- convenience -------------------------------------------------------------
int wk_messagebox (const char *title, const char *text, int buttons)
{ MessageBox m (title, text, buttons); return m.run (); }

bool wk_file_open (char *out, unsigned cap, const char *startDir)
{ FileDialog d (startDir, 0, false); if (d.run () == 1) { d.getResult (out, cap); return true; } return false; }

bool wk_file_save (char *out, unsigned cap, const char *startDir, const char *defName)
{ FileDialog d (startDir, defName, true); if (d.run () == 1) { d.getResult (out, cap); return true; } return false; }

} // namespace wtk
