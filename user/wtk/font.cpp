//
// wtk/font.cpp -- Font family loader + Canvas::drawFont + the global default font
// (compiled into libwtk.a). Reads an Onyx .fnt family (or a legacy single-style .fon)
// from the SD card and blits its glyphs into a Canvas, scaled by an integer factor.
// operator new[]/delete[] resolve at link from the app's onyxpp.hpp (see canvas.cpp);
// do NOT include onyxpp.hpp here.
//
#include "wtk/font.h"
#include "wtk/canvas.h"
#include "kapi.h"		// kapi_open / kapi_fsize / kapi_read / kapi_close

namespace wtk {

Font::Font () : data (0), gw (0), gh (0), gcount (0), nstyles (0)
{ for (int i = 0; i < MAXSTYLE; i++) off[i] = 0; }
Font::~Font () { delete [] data; }

static unsigned rd_u16 (const unsigned char *p) { return (unsigned) p[0] | ((unsigned) p[1] << 8); }
static unsigned rd_u32 (const unsigned char *p)
{ return (unsigned) p[0] | ((unsigned) p[1] << 8) | ((unsigned) p[2] << 16) | ((unsigned) p[3] << 24); }

bool Font::load (const char *path)
{
	delete [] data; data = 0; gw = gh = gcount = nstyles = 0;
	for (int i = 0; i < MAXSTYLE; i++) off[i] = 0;
	if (path == 0 || path[0] == '\0') return false;
	void *f = kapi_open (path);
	if (f == 0) return false;
	unsigned sz = kapi_fsize (f);
	if (sz < 4 + 16u || sz > 4u * 1024 * 1024) { kapi_close (f); return false; }
	unsigned char *raw = new unsigned char[sz];
	if (raw == 0) { kapi_close (f); return false; }
	int got = kapi_read (f, raw, sz);
	kapi_close (f);
	if (got < 4) { delete [] raw; return false; }

	if (raw[0] == 'O' && raw[1] == 'N' && raw[2] == 'Y' && raw[3] == 'F' && sz >= 28)
	{							// Onyx font family
		int w = raw[5], h = raw[6], ns = raw[7];
		unsigned ng = rd_u16 (raw + 8);
		if (w < 1 || w > 32 || h < 1 || h > 64 || ns < 1) { delete [] raw; return false; }
		if (ns > MAXSTYLE) ns = MAXSTYLE;
		gw = w; gh = h; gcount = (int) ng; nstyles = ns;
		for (int i = 0; i < ns; i++)
		{
			off[i] = rd_u32 (raw + 12 + i * 4);
			if (off[i] + (unsigned) (gcount * gh) > sz) { delete [] raw; return false; }
		}
	}
	else							// legacy single-style .fon (w,h header)
	{
		int w = raw[0], h = raw[1];
		if (w < 1 || w > 32 || h < 1 || h > 64) { delete [] raw; return false; }
		gw = w; gh = h; gcount = (int) ((sz - 2) / (unsigned) h); nstyles = 1; off[0] = 2;
	}
	data = raw;
	return nstyles > 0 && gcount > 0;
}

const unsigned char *Font::glyph (unsigned ch, int style) const
{
	if (data == 0 || (int) ch >= gcount) return 0;
	if (style < 0 || style >= nstyles) style = 0;		// fall back to regular
	return data + off[style] + (int) ch * gh;
}

// Blit a string in `style` from `f` at (x,y), top-left origin, scaled by `scale` (>=1).
// Only the ink pixels are written (the background shows through), clipped by fillRect.
void Canvas::drawFont (int x, int y, const char *s, const Font &f, unsigned color, int scale, int style)
{
	if (s == 0 || !f.valid ()) return;
	if (scale < 1) scale = 1;
	int gw = f.width (), gh = f.height ();
	for (; *s; ++s)
	{
		const unsigned char *gl = f.glyph ((unsigned char) *s, style);
		if (gl)
			for (int ry = 0; ry < gh; ry++)
			{
				unsigned char b = gl[ry];
				for (int rx = 0; rx < gw; rx++)
					if (b & (0x80 >> rx))
						fillRect (x + rx * scale, y + ry * scale, scale, scale, color);
			}
		x += gw * scale;
	}
}

// ---- global named font registry ----------------------------------------------
// One Font slot per FONT_* id. Lazily-constructed static array (no global-ctor ordering
// concerns). font(id) hands out the slot, falling back to Sans for an unloaded family.
static Font *registry ()
{
	static Font slots[FONT_COUNT];
	return slots;
}

Font &font (int id)
{
	Font *r = registry ();
	if (id < 0 || id >= FONT_COUNT) id = FONT_SANS;
	if (id != FONT_SANS && !r[id].valid ()) id = FONT_SANS;	// fall back to Sans
	return r[id];
}

void init ()
{
	static bool tried = false;
	if (tried) return;
	tried = true;
	Font *r = registry ();
	static const char *const path[FONT_COUNT] = {
		"SD:/fonts/ns-sans.fnt",	// FONT_SANS    (shipped)
		"SD:/fonts/ns-serif.fnt",	// FONT_SERIF   (optional; absent -> falls back to Sans)
		"SD:/fonts/ns-cursive.fnt",	// FONT_CURSIVE (optional)
		"SD:/fonts/ns-mono.fnt",	// FONT_MONO    (optional)
	};
	for (int i = 0; i < FONT_COUNT; i++) r[i].load (path[i]);
}

Font &Font::Sans ()    { return font (FONT_SANS); }
Font &Font::Serif ()   { return font (FONT_SERIF); }
Font &Font::Cursive () { return font (FONT_CURSIVE); }
Font &Font::Mono ()    { return font (FONT_MONO); }

// Draw into a raw framebuffer (app-drawn windows): borrow it with a transient Canvas and
// reuse drawFont, or fall back to the kernel font if the family isn't loaded.
void draw_text (unsigned *fb, int W, int H, int x, int y, const char *s, unsigned color,
		int scale, int style, int id)
{
	if (fb == 0 || s == 0) return;
	Font &f = font (id);
	if (!f.valid ()) { kapi_draw_text_buf (fb, W, H, x, y, s, color); return; }
	Canvas c; c.adopt (fb, W, H); c.drawFont (x, y, s, f, color, scale, style);
}

} // namespace wtk
