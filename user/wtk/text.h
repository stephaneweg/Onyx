//
// wtk/text.h -- the text-rendering hook: a TextFace an app installs (wk_set_textface) so that
// every text path of wtk draws and measures with it -- Canvas::text, wk_text_l / _c / _w, the line
// height widgets lay out with (wk_fh ()), the widths they compute (a NumericUpDown's right
// alignment, tooltips, icon labels, the grid's cells, the text boxes' carets...). Without a face
// (the default) wtk draws with its bitmap fonts exactly as before, byte for byte.
//
// A face is proportional and its text is UTF-8 (the bitmap path stays Latin-1, one byte a glyph).
// Styles: 0 regular, 1 italic, 2 bold, 3 bold italic (as the bitmap families). Install it before
// building the widgets (some size themselves from wk_fh () when made):
//
//   #include "ft/wtkface.h"              (a newlib app linking ft/libft.a: FreeType's anti-aliased text)
//   int main () { ft_wtk_install ("DejaVu Sans", 13); Root root (...); ... }
//
// The window's frame (its title) keeps the desktop's bitmap font: every window's frame looks the
// same whatever its app draws inside.
//
#ifndef _wtk_text_h
#define _wtk_text_h

#include "wtk/canvas.h"

namespace wtk {

struct TextFace
{
	virtual int  height () = 0;		// a line's height, px (wk_fh () while installed)
	virtual int  ascent () = 0;		// from a line's top to its baseline, px
	virtual int  width (const char *utf8, int style) = 0;			// the advance, px
	// Draw at x, the line's top at yTop (a line of height () px), blended over the canvas.
	virtual void draw (Canvas &cv, int x, int yTop, const char *utf8, unsigned color, int style) = 0;
	// The width of the first n bytes (a prefix: the caret's place). The default copies them and
	// asks width (); a face overrides it to measure in place.
	virtual int  widthN (const char *utf8, int n, int style);
	virtual ~TextFace () {}
};

extern TextFace *wk_face_;			// (the face in use: wk_textface ())
extern int	 wk_face_fw_;			// (its digit's width: wk_fw () while installed)
void wk_set_textface (TextFace *f);		// install a face; 0: back to the bitmap fonts
static inline TextFace *wk_textface () { return wk_face_; }

// Another face for the time of a scope -- a widget's own captions, a display's large digits --, the
// one before back at its end. f = 0: no change.
struct WkFaceScope
{
	TextFace *keep; int keepFw;
	WkFaceScope (TextFace *f) : keep (wk_face_), keepFw (wk_face_fw_)
	{ if (f) { wk_face_ = f; wk_face_fw_ = f->width ("0", 0); if (wk_face_fw_ < 1) wk_face_fw_ = 1; } }
	~WkFaceScope () { wk_face_ = keep; wk_face_fw_ = keepFw; }
};

// ---- measuring and drawing, through the face when one is installed ------------------------------------
// The bitmap fonts' cell (the kernel's font: what wk_fw () / wk_fh () give without a face).
int  wk_bfw ();
int  wk_bfh ();
// The width of s as Canvas::text draws it (no face: its length x wk_fw ()); of its first n bytes.
int  wk_tw (const char *s, int style = 0);
int  wk_tw_n (const char *s, int n, int style = 0);
// The place (a byte index <= n, a character's start) whose caret is the nearest to x px from the
// text's start: a click in a text.
int  wk_tpos (const char *s, int n, int x, int style = 0);
// s at (x, y) -- the line's top-left -- in `style` (the bitmap path: the loaded family's style).
void wk_text (Canvas &cv, int x, int y, const char *s, unsigned c, int style = 0);
// ... clipped to the box (cx, cy, cw, ch): a text that runs past a field's edge.
void wk_text_clip (Canvas &cv, int x, int y, const char *s, unsigned c, int style,
		   int cx, int cy, int cw, int ch);
// Fit s into w px (a copy in out, cap bytes): cut at a character, "..." at its end when it did
// not fit. Returns its width.
int  wk_text_fit (const char *s, int w, char *out, int cap, int style = 0);

// ---- UTF-8 (the face's text) ------------------------------------------------------------------------------
// A sequence's length from its first byte and what follows it (1 for a byte that does not start a
// valid sequence: read as Latin-1, so older text still shows), bounded by n bytes.
static inline int wk_u8_len (const char *s, int n)
{
	unsigned char c = (unsigned char) s[0];
	int k = c < 0xC2 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : c < 0xF5 ? 4 : 1;
	if (k > n) return 1;
	for (int i = 1; i < k; i++) if (((unsigned char) s[i] & 0xC0) != 0x80) return 1;
	return k;
}
// The character at s (its length in *len), a stray byte as its Latin-1 character.
static inline unsigned wk_u8_get (const char *s, int n, int *len)
{
	int k = wk_u8_len (s, n);
	unsigned char c = (unsigned char) s[0];
	unsigned cp = k == 1 ? c : k == 2 ? c & 0x1F : k == 3 ? c & 0x0F : c & 0x07;
	for (int i = 1; i < k; i++) cp = (cp << 6) | ((unsigned char) s[i] & 0x3F);
	if (len) *len = k;
	return cp;
}
// The next / the previous character's start from byte i (of n).
static inline int wk_u8_next (const char *s, int i, int n) { return i >= n ? n : i + wk_u8_len (s + i, n - i); }
static inline int wk_u8_prev (const char *s, int i)
{
	if (i <= 0) return 0;
	int j = i - 1;
	while (j > 0 && i - j < 4 && ((unsigned char) s[j] & 0xC0) == 0x80) j--;
	return wk_u8_len (s + j, i - j) == i - j ? j : i - 1;
}
// cp written as UTF-8 into o (4 bytes room) -> its length.
static inline int wk_u8_put (char *o, unsigned cp)
{
	if (cp < 0x80) { o[0] = (char) cp; return 1; }
	if (cp < 0x800) { o[0] = (char) (0xC0 | cp >> 6); o[1] = (char) (0x80 | (cp & 0x3F)); return 2; }
	if (cp < 0x10000)
	{ o[0] = (char) (0xE0 | cp >> 12); o[1] = (char) (0x80 | ((cp >> 6) & 0x3F)); o[2] = (char) (0x80 | (cp & 0x3F)); return 3; }
	o[0] = (char) (0xF0 | cp >> 18); o[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
	o[2] = (char) (0x80 | ((cp >> 6) & 0x3F)); o[3] = (char) (0x80 | (cp & 0x3F));
	return 4;
}
// A typed key as the text widgets store it with a face: its UTF-8 (Latin-1 0xA0..0xFF, 0x80 the
// euro sign as the keymaps send it) -> the length (0: not a character).
static inline int wk_u8_key (long k, char *o)
{
	if (k >= 32 && k <= 126) { o[0] = (char) k; return 1; }
	if (k >= 0xA0 && k <= 0xFF) return wk_u8_put (o, (unsigned) k);
	if (k == 0x80) return wk_u8_put (o, 0x20AC);
	return 0;
}

} // namespace wtk

#endif
