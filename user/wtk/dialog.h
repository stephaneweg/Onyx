//
// wtk/dialog.h -- modal dialogs as wtk objects. A dialog is just a normal wtk Widget
// added as the TOP child of the Root and flagged `modal`, so Root routes all input to it
// (see Widget::handleMouse/handleKey) until it closes and removes itself. It is built
// from ordinary wtk controls (Button / Textbox / Scrollbar) for a uniform look.
//   wtk::Modal      -- base: box widget + the nested run() loop.
//   wtk::MessageBox -- title + message + OK / Yes-No / OK-Cancel.
//   wtk::FileDialog -- navigable file browser (open / save).
// Convenience: wk_messagebox / wk_file_open / wk_file_save.
//
#ifndef _wtk_dialog_h
#define _wtk_dialog_h

#include "wtk/widget.h"
#include "wtk/textbox.h"
#include "wtk/scrollbar.h"

namespace wtk {

class Modal : public Widget
{
public:
	bool done; int result;
	Modal (int w, int h);			// box-sized widget, centred + flagged modal by run()
	int  run ();				// add to Root, pump until closed, remove; returns result
	void close (int r) { result = r; done = true; }
	virtual void onButton (int tag)   { (void) tag; }	// from a dialog button
	virtual void onScroll (int value) { (void) value; }	// from a dialog scrollbar
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

class FileDialog : public Modal
{
	enum { MAXENT = 128 };
	char  m_dir[256], m_ent[MAXENT][96], m_isdir[MAXENT];
	int   m_count, m_sel, m_top, m_rows, m_rowH;
	int   m_lx, m_ly, m_lw, m_lh;		// file-list rect (box-local)
	bool  m_save;
	Textbox   *m_nameBox;
	Scrollbar *m_sb;
	void read (); void goUp (); void enter (const char *name); void click (int row); void syncSb ();
public:
	FileDialog (const char *startDir, const char *defName, bool save);
	void onScroll (int v) override { m_top = v; invalidate (true); }
	void onButton (int tag) override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;	// list area
	void onDraw () override;
	void getResult (char *out, unsigned cap);		// dir + "/" + filename
};

// Convenience: spin a one-shot dialog and return its outcome.
int  wk_messagebox (const char *title, const char *text, int buttons);
bool wk_file_open (char *out, unsigned cap, const char *startDir);
bool wk_file_save (char *out, unsigned cap, const char *startDir, const char *defName);

} // namespace wtk

#endif
