//
// uidialog.hpp -- user-side modal dialogs for the uikit toolkit, as classes:
//   ui::Modal      -- base: owns a Ui of widgets drawn over the app canvas + the modal
//                     loop (swaps the kernel pointer/key handlers to itself, restores
//                     them to the app on close). No kernel CDialog call.
//   ui::MessageBox -- title + message + OK/Yes-No/OK-Cancel buttons.
//   ui::FileDialog -- navigable file browser (open/save).
//
// Convenience wrappers keep the old call sites unchanged:
//   ui::messagebox(ui, title, text, MB_*) / ui::confirm(...)
//   ui::file_open(ui, out, cap, startDir) / ui::file_save(ui, out, cap, startDir, def)
//
#ifndef ONYX_UIDIALOG_HPP
#define ONYX_UIDIALOG_HPP

#include "uikit.hpp"

namespace ui {

class Modal;
// The kernel handlers are C function pointers, so a tiny static bridge routes events to
// the active modal instance (saved/restored across run() so nested dialogs work).
static Modal *g_modal = 0;
static Ui    *g_restore = 0;
static void modal_ev   (unsigned long s, int e, long v);
static void restore_ev (unsigned long s, int e, long v) { if (g_restore) g_restore->onEvent (s, e, v); }
static void noop_ev    (unsigned long, int, long) {}
static void modal_btn  (Widget &w);		// a dialog button -> Modal::onButton(tag)
static void modal_scr  (Widget &w);		// a dialog scrollbar -> Modal::onScroll(value)

// ============================================================================
class Modal
{
public:
	Ui   ui;			// the dialog's own widgets (over the app canvas)
	bool done;
	int  result;
	Modal (unsigned *fb, int W, int H) : ui (fb, W, H), done (false), result (0) {}
	virtual ~Modal () {}

	void close (int r) { result = r; done = true; }

	virtual void paint () {}					// box + static content
	virtual void onEvent (unsigned long s, int e, long v) { ui.onEvent (s, e, v); }
	virtual void onButton (int tag) { (void) tag; }			// from modal_btn
	virtual void onScroll (int value) { (void) value; }		// from modal_scr

	// Run the modal loop; on close hand input back to `app` (0 = none). Returns result.
	int run (Ui *app)
	{
		Modal *prev = g_modal; g_modal = this; done = false;
		kapi_set_pointer_handler (modal_ev);
		kapi_set_key_handler (modal_ev);
		while (!done && !should_exit ())
		{
			pump_events ();
			paint ();
			ui.drawAll ();
			present ();
			msleep (16);
		}
		g_modal = prev;
		if (prev != 0)			// nested: return input to the parent modal
		{
			kapi_set_pointer_handler (modal_ev);
			kapi_set_key_handler (modal_ev);
		}
		else if (app != 0)		// top level: back to the app
		{
			g_restore = app;
			kapi_set_pointer_handler (restore_ev);
			kapi_set_key_handler (restore_ev);
			app->markDirty ();
		}
		else { kapi_set_pointer_handler (noop_ev); kapi_set_key_handler (noop_ev); }
		return result;
	}
};

inline void modal_ev  (unsigned long s, int e, long v) { if (g_modal) g_modal->onEvent (s, e, v); }
inline void modal_btn (Widget &w) { if (g_modal) g_modal->onButton (w.tag); }
inline void modal_scr (Widget &w) { if (g_modal) g_modal->onScroll (((Scrollbar &) w).value); }

// ============================================================================
class MessageBox : public Modal
{
	const char *m_title, *m_text;
	int m_x, m_y, m_w, m_h;
public:
	MessageBox (Ui &app, const char *title, const char *text, int buttons)
	  : Modal (app.fb, app.W, app.H), m_title (title), m_text (text)
	{
		m_w = 320; if (m_w > ui.W - 20) m_w = ui.W - 20;
		m_h = 130; if (m_h > ui.H - 20) m_h = ui.H - 20;
		m_x = (ui.W - m_w) / 2; m_y = (ui.H - m_h) / 2;
		int by = m_y + m_h - 38;
		if (buttons == MB_YESNO)
		{ ui.button (m_x + m_w - 180, by, 82, 28, "Yes", modal_btn).tag = 1;
		  ui.button (m_x + m_w - 92,  by, 82, 28, "No",  modal_btn).tag = 0; }
		else if (buttons == MB_OKCANCEL)
		{ ui.button (m_x + m_w - 180, by, 82, 28, "OK",     modal_btn).tag = 1;
		  ui.button (m_x + m_w - 92,  by, 82, 28, "Cancel", modal_btn).tag = 0; }
		else ui.button (m_x + m_w - 92, by, 82, 28, "OK", modal_btn).tag = 1;
		ui.focus = 0;				// first button focused -> Enter confirms
	}
	void onButton (int tag) override { close (tag); }
	void paint () override
	{
		ui.fill  (m_x, m_y, m_w, m_h, ui.col_face_dn);
		ui.frame (m_x, m_y, m_w, m_h, ui.col_accent);
		ui.fill  (m_x, m_y, m_w, ui.fh + 8, ui.col_face);
		kapi_draw_text (m_x + 8, m_y + 4, m_title, ui.col_text);
		const char *p = m_text; int line = 0; char buf[96];
		while (*p)
		{
			int n = 0; while (p[n] && p[n] != '\n' && n < 95) n++;
			for (int i = 0; i < n; i++) buf[i] = p[i];
			buf[n] = '\0';
			kapi_draw_text (m_x + 10, m_y + ui.fh + 14 + line * (ui.fh + 2), buf, ui.col_text);
			p += n; if (*p == '\n') p++;
			line++;
		}
	}
};

// ============================================================================
class FileDialog : public Modal
{
	enum { MAXENT = 128 };
	char  m_dir[256];
	char  m_ent[MAXENT][96];
	char  m_isdir[MAXENT];
	int   m_count, m_sel, m_top, m_rows, m_rowH;
	int   m_lx, m_ly, m_lw, m_lh, m_bx, m_by, m_bw, m_bh;
	bool  m_save;
	Textbox  *m_nameBox;
	Scrollbar *m_sb;

	static void scopy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }
	static bool isDotDot (const char *s) { return s[0] == '.' && s[1] == '.' && s[2] == '\0'; }

	void read ()
	{
		m_count = 0; m_top = 0; m_sel = -1;
		scopy (m_ent[0], "..", 96); m_isdir[0] = 1; m_count = 1;
		void *d = kapi_opendir (m_dir);
		if (d != 0)
		{
			struct kapi_dirent e;
			while (m_count < MAXENT && kapi_readdir (d, &e))
			{ scopy (m_ent[m_count], e.name, 96); m_isdir[m_count] = e.is_dir ? 1 : 0; m_count++; }
			kapi_closedir (d);
		}
	}
	void goUp ()
	{
		int n = 0; while (m_dir[n]) n++;
		if (n > 0 && m_dir[n - 1] == '/') n--;
		while (n > 0 && m_dir[n - 1] != '/') n--;
		if (n > 0) n--;
		m_dir[n] = '\0';
		if (n < 4) scopy (m_dir, "SD:/", sizeof m_dir);
	}
	void enter (const char *name)
	{
		int n = 0; while (m_dir[n]) n++;
		if (!(n > 0 && m_dir[n - 1] == '/') && n < 255) m_dir[n++] = '/';
		for (int k = 0; name[k] && n < 255; k++) m_dir[n++] = name[k];
		m_dir[n] = '\0';
	}
	void click (int row)
	{
		if (row < 0 || row >= m_count) return;
		if (m_isdir[row]) { if (isDotDot (m_ent[row])) goUp (); else enter (m_ent[row]); read (); }
		else { m_sel = row; m_nameBox->setText (m_ent[row]); }
		ui.markDirty ();
	}
public:
	FileDialog (Ui &app, const char *startDir, const char *defName, bool save)
	  : Modal (app.fb, app.W, app.H), m_save (save)
	{
		scopy (m_dir, (startDir && startDir[0]) ? startDir : "SD:/", sizeof m_dir);
		m_bw = 360; if (m_bw > ui.W - 20) m_bw = ui.W - 20;
		m_bh = 300; if (m_bh > ui.H - 20) m_bh = ui.H - 20;
		m_bx = (ui.W - m_bw) / 2; m_by = (ui.H - m_bh) / 2;
		m_rowH = ui.fh + 2;
		m_lx = m_bx + 10; m_ly = m_by + ui.fh + 24;
		m_lw = m_bw - 20 - 14; m_lh = m_bh - (m_ly - m_by) - 78;
		m_rows = m_lh / m_rowH; if (m_rows < 1) m_rows = 1;
		m_sb = &ui.scrollbar (m_lx + m_lw + 2, m_ly, 12, m_lh, true, 1, 0, modal_scr);
		m_nameBox = &ui.textbox (m_bx + 10, m_ly + m_lh + 8, m_bw - 20, ui.fh + 8, defName ? defName : "");
		int by = m_by + m_bh - 36;
		ui.button (m_bx + m_bw - 180, by, 82, 28, save ? "Save" : "Open", modal_btn).tag = 1;
		ui.button (m_bx + m_bw - 92,  by, 82, 28, "Cancel", modal_btn).tag = 0;
		read ();
	}
	void onScroll (int v) override { m_top = v; ui.markDirty (); }
	void onButton (int tag) override
	{
		if (tag == 0) { close (0); return; }			// Cancel
		if (m_nameBox->getText ()[0] != '\0') close (1);	// OK (need a filename)
	}
	void onEvent (unsigned long s, int e, long v) override
	{
		ui.onEvent (s, e, v);					// filename box / buttons / scrollbar
		if (e == GUI_EVENT_PTR_DOWN && (GUI_PTR_CHANGED (v) & 1))
		{
			int x = GUI_PTR_X (v), y = GUI_PTR_Y (v);
			if (x >= m_lx && x < m_lx + m_lw && y >= m_ly && y < m_ly + m_lh)
				click (m_top + (y - m_ly - 1) / m_rowH);
		}
	}
	void paint () override
	{
		int mt = m_count - m_rows; if (mt < 1) mt = 1;		// reflect list scroll
		m_sb->vmax = mt; m_sb->value = m_top;

		ui.fill  (m_bx, m_by, m_bw, m_bh, ui.col_face_dn);
		ui.frame (m_bx, m_by, m_bw, m_bh, ui.col_accent);
		ui.fill  (m_bx, m_by, m_bw, ui.fh + 8, ui.col_face);
		kapi_draw_text (m_bx + 8, m_by + 4, m_save ? "Save file" : "Open file", ui.col_text);
		kapi_draw_text (m_bx + 10, m_by + ui.fh + 12, m_dir, ui.col_dis);

		ui.fill  (m_lx, m_ly, m_lw, m_lh, ui.col_field);
		ui.frame (m_lx, m_ly, m_lw, m_lh, ui.col_border);
		for (int r = 0; r < m_rows; r++)
		{
			int idx = m_top + r; if (idx >= m_count) break;
			int ry = m_ly + 1 + r * m_rowH;
			if (idx == m_sel) ui.fill (m_lx + 1, ry, m_lw - 2, m_rowH, ui.col_face_hi);
			char row[100]; int k = 0;
			for (; m_ent[idx][k] && k < 96; k++) row[k] = m_ent[idx][k];
			if (m_isdir[idx] && !isDotDot (m_ent[idx]) && k < 97) row[k++] = '/';
			row[k] = '\0';
			kapi_draw_text (m_lx + 4, ry, row, m_isdir[idx] ? ui.col_accent : ui.col_text);
		}
	}
	void getResult (char *out, unsigned cap)			// dir + "/" + filename
	{
		const char *nm = m_nameBox->getText ();
		unsigned n = 0;
		for (; m_dir[n] && n < cap - 1; n++) out[n] = m_dir[n];
		if (n > 0 && out[n - 1] != '/' && n < cap - 1) out[n++] = '/';
		for (int k = 0; nm[k] && n < cap - 1; k++) out[n++] = nm[k];
		out[n] = '\0';
	}
};

// ---- convenience wrappers (unchanged call sites) -----------------------------
static inline int messagebox (Ui &app, const char *title, const char *text, int buttons)
{ MessageBox d (app, title, text, buttons); return d.run (&app); }
static inline int confirm (Ui &app, const char *title, const char *text)
{ MessageBox d (app, title, text, MB_YESNO); return d.run (&app); }
static inline int file_open (Ui &app, char *out, unsigned cap, const char *startDir)
{ FileDialog d (app, startDir, 0, false); int r = d.run (&app); if (r) d.getResult (out, cap); return r; }
static inline int file_save (Ui &app, char *out, unsigned cap, const char *startDir, const char *defName)
{ FileDialog d (app, startDir, defName, true); int r = d.run (&app); if (r) d.getResult (out, cap); return r; }

} // namespace ui

#endif // ONYX_UIDIALOG_HPP
