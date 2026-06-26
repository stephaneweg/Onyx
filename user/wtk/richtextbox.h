//
// wtk/richtextbox.h -- multi-line *styled* text editor (word processor body). Unlike
// Textarea (one colour, one size), every character carries its own style: bold /
// italic / underline / strikethrough / highlight, a 16-colour foreground + highlight
// palette, and an integer size multiplier (x1..x8, smoothed). Paragraphs carry a
// heading "level" (Normal / Titre 1-3) applied as a preset across the paragraph.
//
// All styling is synthesised user-side from Onyx's single monospace bitmap font: the
// glyph mask is fetched via kapi_draw_text_buf, then emboldened / sheared / bilinearly
// upscaled and blended -- no kernel/ABI change, no FreeType. Default page is white,
// text black. The widget owns its text buffer + a parallel per-char attribute buffer.
//
#ifndef _wtk_richtextbox_h
#define _wtk_richtextbox_h

#include "wtk/widget.h"

namespace wtk {

// ---- character style flags (bits 0..7 of the packed attribute) ---------------
enum {
	RT_BOLD   = 0x01,
	RT_ITALIC = 0x02,
	RT_UNDER  = 0x04,
	RT_STRIKE = 0x08,
	RT_HILITE = 0x10,	// draw the highlight (background) band
};

// ---- 16-colour palette indices (see RT_PAL[] in the .cpp) --------------------
enum {
	RT_BLACK = 0, RT_WHITE, RT_GRAY,  RT_SILVER,
	RT_RED,       RT_GREEN, RT_BLUE,  RT_YELLOW,
	RT_CYAN,      RT_MAGENTA, RT_ORANGE, RT_PURPLE,
	RT_DKRED,     RT_DKGREEN, RT_DKBLUE, RT_TEAL,
};

// ---- paragraph heading levels (presets applied by setLevel) ------------------
enum { RT_NORMAL = 0, RT_TITLE1, RT_TITLE2, RT_TITLE3 };

// Unpacked character style (public API + the "current typing style"). Packs into one
// u32 stored per character, parallel to the text buffer.
struct RtStyle
{
	unsigned char flags;	// RT_BOLD | RT_ITALIC | ...
	unsigned char fg;	// foreground palette index
	unsigned char bg;	// highlight palette index (used when RT_HILITE set)
	unsigned char size;	// 1..8 (font cell multiplier)
	RtStyle () : flags (0), fg (RT_BLACK), bg (RT_YELLOW), size (1) {}
};

unsigned rt_pack   (const RtStyle &s);		// RtStyle  -> packed u32
RtStyle  rt_unpack (unsigned a);		// packed u32 -> RtStyle
unsigned rt_color  (int paletteIndex);		// palette index -> 0x00RRGGBB

class RichTextBox : public Widget
{
public:
	char     *buf;		// text ('\n' between paragraphs), NUL-terminated
	unsigned *attr;		// packed RtStyle per character (parallel to buf)
	int       cap, len, caret, sel;	// sel = selection anchor (-1 = none)
	int       top;		// first visible VISUAL (wrapped) row
	int       goalX;	// desired caret x for vertical moves (pixels, row-relative)
	bool      readonly;
	RtStyle   cur;		// style stamped onto newly typed characters

	RichTextBox (int l, int t, int w, int h, int capacity);
	~RichTextBox () override;

	void        setContent (const char *s);	// load plain text in the default style
	const char *content () const { return buf; }
	int         length  () const { return len; }

	// ---- styling: applies to the selection, else to `cur` (next typed text) ----
	void toggleFlag  (unsigned flag);	// bold/italic/underline/strike/hilite
	void setFg       (int paletteIndex);
	void setHilite   (int paletteIndex);	// set RT_HILITE + highlight colour
	void clearHilite ();
	void setSize     (int mult);		// 1..8
	void setLevel    (int level);		// RT_NORMAL / RT_TITLE1.. on caret paragraph(s)
	void selectAll   ();
	RtStyle caretStyle () const;		// style at the caret (for toolbar state)

	// ---- scrolling (a host Scrollbar wires value<->topRow, vmax<->rowCount-1) ----
	int  rowCount ();			// total visual rows
	int  topRow () const { return top; }
	void setTopRow (int r);

	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;

private:
	// text buffer
	bool grow (int need);
	void insertChar (char ch);
	void deleteRange (int a, int b);	// remove [a,b)
	bool hasSel () const { return sel >= 0 && sel != caret; }
	void selRange (int &a, int &b) const;
	void deleteSelection ();

	// paragraph / glyph metrics
	int  paraStart (int i) const { while (i > 0 && buf[i - 1] != '\n') i--; return i; }
	int  paraEnd   (int i) const { while (i < len && buf[i] != '\n') i++; return i; }
	int  glyphAdvance (int i) const;

	// word-wrap layout cache (rebuilt lazily on edit / resize)
	int *rowStart, *rowH, *rowY;	// rowY has rowN+1 entries (prefix tops + total)
	int  rowN, rowCap, lastWrapW;
	bool layoutDirty;
	bool barDrag;				// dragging the integrated scrollbar thumb
	void ensureLayout ();
	void relayout (int wrapW);
	void rowAt (int s, int wrapW, int &end, int &h, int &nextStart, bool &hard) const;
	bool growRows (int need);
	int  rowOfChar (int c);
	int  xInRow (int pos);
	void ensureVisible ();
	void moveVert (int dir);
	int  hitTest (int mx, int my);

	// styled glyph rasteriser
	void drawGlyph (int px, int py, char ch, const RtStyle &st);
};

} // namespace wtk

#endif
