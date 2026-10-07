//
// uikit/dialog.h -- modal dialogs as uikit objects. A dialog is just a normal uikit Widget
// added as the TOP child of the Root and flagged `modal`, so Root routes all input to it
// (see Widget::handleMouse/handleKey) until it closes and removes itself. It is built
// from ordinary uikit controls (Button / Textbox / Scrollbar) for a uniform look.
//   uikit::Modal      -- base: box widget + the nested run() loop.
//   uikit::MessageBox -- title + message + OK / Yes-No / OK-Cancel.
//   uikit::FileDialog -- navigable file browser (open / save).
// Convenience: uk_messagebox / uk_file_open / uk_file_save.
//
#ifndef _uikit_dialog_h
#define _uikit_dialog_h

#include "uikit/widget.h"
#include "uikit/textbox.h"
#include "uikit/scrollbar.h"

namespace uikit {

class Modal : public Widget
{
public:
	bool done; int result;
	Modal (int w, int h);			// box-sized widget, centred + flagged modal by run()
	int  run ();				// add to Root, pump until closed, remove; returns result
	void close (int r) { result = r; done = true; }
	virtual void onButton (int tag)   { (void) tag; }	// from a dialog button
	virtual void onScroll (int value) { (void) value; }	// from a dialog scrollbar
	unsigned bgColor () override;		// the face (its controls blend into it)
	// The box: a rounded panel of the face, a title strip in the active frame's colour, an
	// outline; its corners see-through. The title strip's height: titleH ().
	void drawBox (const char *title);
	static int titleH ();
};

class MessageBox : public Modal
{
	const char *m_title, *m_text; int m_def;
public:
	MessageBox (const char *title, const char *text, int buttons);
	void onButton (int tag) override { close (tag); }
	bool onKey (long k) override;		// Enter = default, Esc = cancel
	void onDraw () override;
};

// The file dialog (uk_file_open / uk_file_save / uk_folder_open show it): an Up button and the folder's
// path, the volumes at the left, the folder's content (the folders first, sorted, the files' sizes); a click
// selects, a double click / Enter / the button opens a folder or takes the file; the arrows, Backspace (up).
// startDir may be a file's path: its folder (a save dialog proposes its name).
// ITS SIZE AND ITS FUNCTIONS ARE THE LIBRARY'S ABI: the fields below are kept as they always were (dialog.cpp
// says how the dialog uses them), nothing is added.
class FileDialog : public Modal
{
	enum { MAXENT = 128 };
	char  m_dir[256], m_ent[MAXENT][96], m_isdir[MAXENT];
	int   m_count, m_sel, m_top, m_rows, m_rowH;
	int   m_lastRow; unsigned m_lastTick;	// a double click on a file: confirmed
	int   m_lx, m_ly, m_lw, m_lh;		// file-list rect (box-local)
	bool  m_save, m_folder;		// m_folder: pick a directory (no filename box)
	Textbox   *m_nameBox;
	Scrollbar *m_sb;
	void read (); void goUp (); void enter (const char *name); void click (int row); void syncSb ();
public:
	FileDialog (const char *startDir, const char *defName, bool save, bool folder = false);
	void onScroll (int v) override { m_top = v; invalidate (true); }
	void onButton (int tag) override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;	// list area
	bool onKey (long k) override;		// Enter = Open / Save, Esc = Cancel
	void onDraw () override;
	void getResult (char *out, unsigned cap);		// dir + "/" + filename (folder mode: dir)
	// The kinds of files shown: "Text files|*.txt;*.md|All files|*" -- pairs of a name and its patterns
	// (';' between them; "*": every file), '|' between everything. A drop-down beside the buttons chooses
	// one; the folder shows the files that match it (and every folder). A save: a name typed without an
	// extension takes the kind's first one. 0 / "": every file, no drop-down. The text must live as long
	// as the dialog (it is not copied).
	void setFilters (const char *filters);
	const char *fileName () const { return m_nameBox ? m_nameBox->text : ""; }	// the name box's text
};

// Colour dialog (WPF-style ColorPicker dialog): R / G / B sliders, a 16-colour palette,
// a preview (old | new) and the hex value; OK / Enter keeps it, Cancel / Esc does not.
class ColorDialog : public Modal
{
public:
	unsigned color, orig;
	ColorDialog (unsigned initial, const char *title);
	void onButton (int tag) override { close (tag); }
	bool onKey (long k) override;
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	void syncSliders ();
	class Slider *r, *g, *b;
private:
	const char *m_title;
};

// Convenience: spin a one-shot dialog and return its outcome.
// A pop-up menu at (x, y) of the window (a context menu, the window menu): a floating panel of
// items -- the one under the pointer in the accent -- and separators. run () shows it until an
// item is picked (its id), or a click elsewhere / Esc (-1).
class PopupMenu : public Modal
{
	enum { MAXI = 16 };
	const char *m_label[MAXI], *m_hint[MAXI]; int m_id[MAXI]; bool m_on[MAXI], m_sep[MAXI];
	int m_n, m_hot;
	int rowY (int i) const;
	int rowAt (int mx, int my) const;
public:
	PopupMenu (int x, int y);
	void add (const char *label, int id, bool enabled = true, const char *hint = 0);
	void separator ();
	int  run ();				// fits the menu in the window, shows it -> id / -1
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
};

int  uk_messagebox (const char *title, const char *text, int buttons);	// modal; buttons: MB_* -> 1 OK / Yes, 0 Cancel / No / Esc (2: Yes-No-Cancel's No)
// The file dialogs: modal, from startDir (a folder, or a file's path: its folder); true = OK (out: the chosen
// path). filters: the kinds of files offered, "Text files|*.txt;*.md|All files|*" (FileDialog::setFilters); 0: all.
bool uk_file_open (char *out, unsigned cap, const char *startDir, const char *filters = 0);
bool uk_file_save (char *out, unsigned cap, const char *startDir, const char *defName, const char *filters = 0);	// with a name box (defName in it)
bool uk_folder_open (char *out, unsigned cap, const char *startDir);	// pick a directory
bool uk_color_dialog (unsigned *color, const char *title = "Colour");	// true = OK (*color set)

} // namespace uikit

#endif
