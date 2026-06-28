//
// wtk/font.h -- Font: a bitmap font *family* loaded from one Onyx ".fnt" file on the SD
// card (e.g. SD:/fonts/ns-sans.fnt, the MIT "ns-sans" face exported by tools/fonts/
// gen_nssans.py from NetSurf's glyph_data). One file embeds all four styles; the header
// carries the glyph size and the file offset of each style block:
//     0   "ONYF"  magic
//     4   version (u8) | width (u8) | height (u8) | styles (u8)
//     8   glyphs  (u16 LE, per style) | reserved (u16)
//     12  offset[styles] (u32 LE each): file offset of each style block
//         style index = (bold?2:0)|(italic?1:0): 0=reg 1=italic 2=bold 3=bold+italic
//     ..  style blocks: glyphs * height bytes, 1 byte/row, bit 0x80 = leftmost px.
// (A legacy 2-byte-header single-style .fon is also accepted, as one regular style.)
//
// Draw with Canvas::drawFont (declared in canvas.h, implemented in font.cpp). A global
// default family is loaded once by wtk::init() and reachable via wtk::font(), so any
// widget can render styled text from a real face, not just the kernel's single font.
//
#ifndef _wtk_font_h
#define _wtk_font_h

namespace wtk {

class Font
{
public:
	static const int MAXSTYLE = 4;
	unsigned char *data;		// whole file image; 0 = not loaded
	int  gw, gh, gcount, nstyles;	// glyph width/height, glyphs per style, style count
	unsigned off[MAXSTYLE];		// byte offset (within data) of each style block

	Font ();
	~Font ();

	bool load (const char *path);				// (re)load an Onyx .fnt/.fon
	bool valid () const { return data != 0 && nstyles > 0; }
	int  width () const { return gw; }
	int  height () const { return gh; }
	int  styles () const { return nstyles; }
	// gh bytes (MSB-left rows), or 0. style 0=reg 1=italic 2=bold 3=bold+italic; a style
	// beyond what this family carries falls back to regular.
	const unsigned char *glyph (unsigned ch, int style = 0) const;

	// Named global families (loaded by wtk::init from SD:/fonts/<name>.fnt). Convenience
	// over font(id); a family whose file is absent falls back to Sans when drawn.
	static Font &Sans ();
	static Font &Serif ();
	static Font &Cursive ();
	static Font &Mono ();
};

// The global named font registry. wtk::init() loads every known family once (idempotent;
// Root calls it at startup, so EVERY wtk app gets the fonts -- an app that builds no Root
// must call wtk::init() itself). A widget chooses a face by passing font(id) to drawFont.
enum { FONT_SANS = 0, FONT_SERIF, FONT_CURSIVE, FONT_MONO, FONT_COUNT };
void  init ();
Font &font (int id = FONT_SANS);		// the family for `id`, or Sans if it isn't loaded

// Draw `s` into a raw 0x00RRGGBB framebuffer (W x H) via the font registry -- for app-drawn
// windows that don't own a wtk Canvas. Scaled by `scale`, in `style`, family `id`. Falls
// back to the kernel font (kapi_draw_text_buf) when the family isn't loaded.
void draw_text (unsigned *fb, int W, int H, int x, int y, const char *s, unsigned color,
		int scale = 1, int style = 0, int id = FONT_SANS);

} // namespace wtk

#endif
