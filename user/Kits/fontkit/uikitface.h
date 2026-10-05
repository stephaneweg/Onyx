//
// ft/uikitface.h -- FreeType's anti-aliased text for every uikit widget: a uikit::TextFace (uikit/text.h)
// built on ft/fonts.h (the card's TrueType families, FreeType's glyphs cached per quarter-pixel
// position). Header-only, like ft/fonts.h: include it once, in the app's one translation unit, and
// link ft/libft.a -- a newlib app (Letters' rule in user/Makefile is the model).
//
//   #include "fontkit/uikitface.h"
//   int main () {
//       ft_uikit_install ("DejaVu Sans", 13);     // before building the widgets; false: none (bitmap)
//       Root root (...);  ...
//   }
//
// Text is UTF-8 (a stray byte is read as Latin-1); styles 0 regular, 1 italic, 2 bold, 3 bold italic
// (a style the family lacks is made by fnt::). The line height is the font's (DejaVu Sans at 13 px:
// 16 px, as the bitmap font's), the text centred in it. More faces (a display's large digits):
//
//   FtTextFace *big = new FtTextFace;  big->open ("DejaVu Sans", 24);   lcd->face = big;
//
// fnt::trim () may run at any moment no fnt::Font pointer is held: the face holds none between calls.
//
#ifndef _ft_uikitface_h
#define _ft_uikitface_h

#include "fontkit/fonts.h"
#include "uikit/text.h"

class FtTextFace : public uikit::TextFace
{
public:
	bool kerning;				// the pairs' kerning (on)

	FtTextFace () : kerning (true), m_fam (-1), m_px (0), m_h (16), m_asc (12) { reset (); }

	// The family by name (falls back to DejaVu Sans), px the size (the em, in pixels). fnt::init ()
	// is called here. False: no TrueType font on the card.
	bool open (const char *family, int px)
	{
		if (!fnt::init ()) return false;
		int fam = family ? fnt::find (family) : -1;
		if (fam < 0) fam = fnt::find ("DejaVu Sans");
		if (fam < 0) fam = 0;
		if (px < 4) px = 4;
		m_fam = fam; m_px = px;
		reset ();
		fnt::Font *f = font (0);
		if (!f) { m_fam = -1; return false; }
		int glyphs = f->ascent + f->descent;			// (1/64 px)
		m_h = (f->height + 63) >> 6;
		if (m_h * 64 < glyphs) m_h = (glyphs + 63) >> 6;
		m_asc = ((m_h * 64 - glyphs) / 2 + f->ascent + 32) >> 6;	// the glyphs centred in the line
		return true;
	}
	int family () const { return m_fam; }
	int px () const { return m_px; }

	int height () override { return m_h; }
	int ascent () override { return m_asc; }
	int width (const char *s, int style) override { return widthN (s, 1 << 30, style); }

	// The first n bytes' width (up to a NUL): the advances and kerning in 1/64 px, rounded -- the
	// same pen draw () moves. Remembered in a small cache (a label is measured at each redraw).
	int widthN (const char *s, int n, int style) override
	{
		if (!s || n <= 0 || m_fam < 0) return 0;
		style &= 3;
		unsigned h = 2166136261u; int len = 0;
		for (; len < n && s[len]; len++) h = (h ^ (unsigned char) s[len]) * 16777619u;
		if (len == 0) return 0;
		h ^= (unsigned) style * 0x9E3779B9u;
		WCache &c = m_wc[(h ^ (h >> 16)) & (WC - 1)];
		if (c.len == len && c.hash == h && c.style == style) return c.w;
		fnt::Font *f = font (style);
		if (!f) return 0;
		long x64 = 0; unsigned prev = 0;
		for (int i = 0; i < len; )
		{
			int k; unsigned cp = uikit::uk_u8_get (s + i, len - i, &k); i += k;
			if (kerning && prev) x64 += fnt::kern (f, prev, cp);
			x64 += adv (f, style, cp);
			prev = cp;
		}
		int w = (int) ((x64 + 32) >> 6);
		c.hash = h; c.len = len; c.style = style; c.w = w;
		return w;
	}

	void draw (uikit::Canvas &cv, int x, int yTop, const char *s, unsigned color, int style) override
	{
		if (!s || !*s || m_fam < 0) return;
		style &= 3;
		fnt::Font *f = font (style);
		if (!f) return;
		int base = yTop + m_asc, n = 0;
		while (s[n]) n++;
		long x64 = (long) x * 64; unsigned prev = 0;
		color &= 0x00FFFFFFu;
		for (int i = 0; i < n; )
		{
			int k; unsigned cp = uikit::uk_u8_get (s + i, n - i, &k); i += k;
			if (kerning && prev) x64 += fnt::kern (f, prev, cp);
			if (x64 >= (long) cv.w * 64) break;			// (the rest is past the edge)
			fnt::draw (cv, f, (int) x64, base, cp, color, 0, 0, cv.w, cv.h);
			x64 += adv (f, style, cp);
			prev = cp;
		}
	}

private:
	enum { WC = 128 };
	struct WCache { unsigned hash; int len, style, w; };
	int m_fam, m_px, m_h, m_asc;
	int m_adv[4][128];			// the ASCII advances per style, 1/64 px (-1: not yet asked)
	WCache m_wc[WC];

	void reset ()
	{
		for (int s = 0; s < 4; s++) for (int c = 0; c < 128; c++) m_adv[s][c] = -1;
		for (int i = 0; i < WC; i++) { m_wc[i].len = -1; m_wc[i].hash = 0; m_wc[i].style = 0; m_wc[i].w = 0; }
	}
	// uikit's style (1 italic, 2 bold) -> fnt's (BOLD 1, ITALIC 2); the sized font asked each time
	// (no pointer kept: fnt::trim may drop it between two calls).
	fnt::Font *font (int style)
	{
		int fs = ((style & 2) ? fnt::BOLD : 0) | ((style & 1) ? fnt::ITALIC : 0);
		return fnt::get (m_fam, fs, m_px * 64);
	}
	int adv (fnt::Font *f, int style, unsigned cp)
	{
		if (cp < 128)
		{
			int &a = m_adv[style][cp];
			if (a < 0) a = fnt::advance (f, cp);
			return a;
		}
		return fnt::advance (f, cp);
	}
};

// Install FreeType's text for every uikit widget: the family by name ("DejaVu Sans", "Liberation Sans",
// "Selawik"...; DejaVu Sans when absent) at px pixels. False: no TrueType font (uikit keeps its bitmap
// fonts). Call it before building the widgets.
static FtTextFace *g_ftUIKitFace;		// (the one ft_uikit_install made)
// Installs FreeType's text for every uikit widget with the family by name (DejaVu Sans when absent) at px pixels, false when the card has no TrueType font.
static inline bool ft_uikit_install (const char *family, int px)
{
	if (!g_ftUIKitFace) g_ftUIKitFace = new FtTextFace;
	if (!g_ftUIKitFace || !g_ftUIKitFace->open (family, px)) return false;
	uikit::uk_set_textface (g_ftUIKitFace);
	return true;
}

// The face ft_uikit_install made (0: none yet).
static inline FtTextFace *ft_uikit_face () { return g_ftUIKitFace; }

#endif
