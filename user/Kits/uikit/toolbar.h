//
// uikit/toolbar.h -- ToolBar + ToolButton: a strip of small buttons (generalised from Letters'): an
// icon (one of the WKT_* set below, drawn from its geometry at any size, or the app's own drawer),
// a text label beside it or alone, a toggle state (lit: the accent's tint, or filled -- a play
// button), a split part (an arrow opening a palette or a menu), a tooltip; separators and gaps.
//
//   #include "uikit/toolbar.h"       (NOT in uikit/uikit.h: Letters, the Spreadsheet and Cardfile have
//                                   their own ToolBar / ToolButton and `using namespace uikit`)
//   ToolBar *tb = new ToolBar (0, 0, W, 38);
//   tb->add ((new ToolButton (30, 28, "Save (Ctrl+S)", onSave))->setGlyph (WKT_SAVE));
//   tb->sep ();
//   ToolButton *play = (new ToolButton (34, 28, "Play (Space)", onPlay))->setGlyph (WKT_PLAY)->setToggle (true);
//   play->filled = true;  tb->add (play);
//   tb->add ((new ToolButton (0, 28, "Loop"))->setGlyph (WKT_LOOP)->setText ("Loop")->fitWidth ());
//
// onClick gets the button (a toggle's `on` already flipped). A button is flat until pointed
// (`raised`: always a face, as a transport's); it does not take the keyboard focus.
//
#ifndef _uikit_toolbar_h
#define _uikit_toolbar_h

#include "uikit/widget.h"

namespace uikit {

// The toolbar icons, drawn in a size x size box at (x, y) in `ink` (anti-aliased, uikit/vpaint.h).
enum { WKT_NONE = -1, WKT_NEW = 0, WKT_OPEN, WKT_SAVE, WKT_UNDO, WKT_REDO, WKT_CUT, WKT_COPY, WKT_PASTE,
       WKT_PLAY, WKT_PAUSE, WKT_STOP, WKT_RECORD, WKT_TO_START, WKT_TO_END, WKT_REWIND, WKT_FORWARD,
       WKT_LOOP, WKT_METRONOME, WKT_PLUS, WKT_MINUS, WKT_SEARCH, WKT_MIXER, WKT_SPARK, WKT_GEAR,
       WKT_COUNT };
void uk_tool_glyph (Canvas &cv, int kind, int x, int y, int size, unsigned ink);

// An app's icon: drawn in the size x size box at (x, y), `ink` the text's colour (greyed when off).
typedef void (*ToolIconFn) (Canvas &cv, int id, int x, int y, int size, unsigned ink, bool off);

class ToolButton : public Widget
{
public:
	int	   glyph;			// a WKT_* icon (WKT_NONE: none)
	ToolIconFn iconFn; int iconId;		// ... or the app's drawer
	int	   iconSize;			// the icon's box, px (18)
	char	   text[32];			// a label (right of the icon; alone: a text button)
	bool	   toggle, on;			// a toggle: a click flips `on`; on: lit
	bool	   filled;			// on: filled with onColor (a play button) rather than tinted
	bool	   raised;			// a face even when not pointed (a transport's buttons)
	unsigned   onColor;			// UK_AUTO: the accent
	unsigned   iconColor;			// the icon's own colour (UK_AUTO: the text's): a record's red
	Action	   onClick;
	void	   (*arrow) (ToolButton &);	// a split button's arrow part (0: none)

	ToolButton (int w, int h, const char *tip = 0, Action cb = 0);
	ToolButton *setGlyph (int kind);
	ToolButton *setIcon (ToolIconFn fn, int id);
	ToolButton *setText (const char *s);
	ToolButton *setToggle (bool isToggle, bool isOn = false);
	ToolButton *setSplit (void (*arrowCb) (ToolButton &));	// (adds the arrow part's width)
	ToolButton *fitWidth ();		// as wide as its icon and label need
	void setOn (bool v);
	void setDisabled (bool d);

	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	static const int SPLIT_W = 13;
private:
	int m_part, m_down;			// the part under the pointer, the one pressed (-1 none; 1 the arrow)
	unsigned face ();
};

class ToolBar : public Widget
{
public:
	unsigned bg;				// its face (UK_AUTO: the window's, C_BG)
	bool	 line;			// an etched line along its bottom
	ToolBar (int l, int t, int w, int h = 34);
	void add (Widget *w, int gap = 1);	// at the next place (after a gap), centred vertically
	void addRight (Widget *w, int gap = 4);	// from the right end leftward (they follow a resize)
	void sep ();				// an etched separator
	void space (int px) { m_x += px; }	// a gap
	int  next () const { return m_x; }	// where the next one goes
	unsigned bgColor () override { return bg == UK_AUTO ? C_BG : bg; }
	void onDraw () override;
private:
	int m_x, m_rx, m_nsep, m_sepX[24];
};

} // namespace uikit

#endif
