//
// gen_title_font.cpp -- the windows' title font: DejaVu Sans Bold at 13 px rendered by the apps' own
// FreeType (user/ft/fonts.h: the auto-hinter, the gamma the apps draw with) into SD:/res/fonts/title.aaf,
// an anti-aliased bitmap font every app's frame reads (uikit/skin.cpp: no FreeType needed in the app).
//
//   sh tools/title_font/build.sh        -> sdcard/res/fonts/title.aaf
//
// The file (little-endian): "AAF1", u16 line height, u16 ascent (a line's top to its baseline), u16 the
// characters' count (from U+0020: 32 .. 255, Latin-1), u16 kerning pairs; then a character each:
// u16 advance (1/64 px), i8 left, i8 top (the bitmap's, from the pen on the baseline), u8 w, u8 h,
// u32 its coverage's offset in the data; the pairs: u8 first, u8 second, i16 (1/64 px); the data (w x h
// bytes a character: the opacity, gamma applied).
//
// MIT licence (Onyx).
//
#include <stdio.h>
#include <string.h>
#include <vector>
#include "ft/fonts.h"

static void u16 (std::vector<unsigned char> &o, int v) { o.push_back ((unsigned char) (v & 255)); o.push_back ((unsigned char) ((v >> 8) & 255)); }
static void u32 (std::vector<unsigned char> &o, unsigned v) { for (int i = 0; i < 4; i++) o.push_back ((unsigned char) (v >> (8 * i))); }

int main (int argc, char **argv)
{
	const char *out = argc > 1 ? argv[1] : "sdcard/res/fonts/title.aaf";
	const int PX = 13, FIRST = 32, N = 224;
	if (!fnt::init ()) { fprintf (stderr, "gen_title_font: no TrueType font on the card\n"); return 1; }
	int fam = fnt::find ("DejaVu Sans");
	if (fam < 0) { fprintf (stderr, "gen_title_font: DejaVu Sans not found\n"); return 1; }
	fnt::Font *f = fnt::get (fam, fnt::BOLD, PX * 64);
	if (!f) return 1;
	// the line as FtTextFace::open lays it out (uikit/text.h: the glyphs centred in the line)
	int glyphs = f->ascent + f->descent, h = (f->height + 63) >> 6;
	if (h * 64 < glyphs) h = (glyphs + 63) >> 6;
	int asc = ((h * 64 - glyphs) / 2 + f->ascent + 32) >> 6;
	std::vector<unsigned char> head, tab, kern, data;
	int nk = 0;
	for (int a = 33; a < 127; a++)
		for (int b = 33; b < 127; b++)
		{
			int k = fnt::kern (f, (unsigned) a, (unsigned) b);
			if (k) { kern.push_back ((unsigned char) a); kern.push_back ((unsigned char) b); u16 (kern, k); nk++; }
		}
	for (int i = 0; i < N; i++)
	{
		unsigned cp = (unsigned) (FIRST + i);
		fnt::Glyph *g = fnt::glyph (f, cp);
		int adv = g->adv, l = 0, t = 0, w = 0, hh = 0;
		unsigned off = (unsigned) data.size ();
		if (cp > 32 && cp != 0xA0 && g->gi)
		{
			if (!g->bmp[0]) fnt::render (f, g, 0);
			if (g->bmp[0])
			{
				l = g->bl[0]; t = g->bt[0]; w = g->bw[0]; hh = g->bh[0];
				for (int k = 0; k < w * hh; k++) data.push_back (fnt::g_gamma[g->bmp[0][k]]);
			}
		}
		u16 (tab, adv); tab.push_back ((unsigned char) (signed char) l); tab.push_back ((unsigned char) (signed char) t);
		tab.push_back ((unsigned char) w); tab.push_back ((unsigned char) hh); u32 (tab, off);
	}
	head.push_back ('A'); head.push_back ('A'); head.push_back ('F'); head.push_back ('1');
	u16 (head, h); u16 (head, asc); u16 (head, N); u16 (head, nk);
	FILE *o = fopen (out, "wb");
	if (!o) { perror (out); return 1; }
	fwrite (head.data (), 1, head.size (), o); fwrite (tab.data (), 1, tab.size (), o);
	fwrite (kern.data (), 1, kern.size (), o); fwrite (data.data (), 1, data.size (), o);
	fclose (o);
	printf ("gen_title_font: %s: line %d px, ascent %d, %d characters, %d kerning pairs, %u bytes\n", out, h, asc, N, nk,
		(unsigned) (head.size () + tab.size () + kern.size () + data.size ()));
	return 0;
}
