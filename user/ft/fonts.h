//
// ft/fonts.h -- the apps' TrueType text (Writer's pages): the fonts on the card by family, at
// any size, their glyphs rendered by FreeType (user/ft/: TrueType only, anti-aliased, auto-
// hinted vertically) and cached. Header-only: include it once, in the app's one translation unit;
// link ft/libft.a (a newlib app: FreeType wants a libc).
//
//   fnt::init ();                       the families of SD:/res/fonts and SD:/fonts (sorted)
//   fnt::count (), fnt::name (i)        ... their names ("Liberation Serif", "DejaVu Sans Mono"...)
//   fnt::find ("Liberation Sans")       a family by name (-1: none)
//   fnt::Font *f = fnt::get (fam, fnt::BOLD | fnt::ITALIC, size64)
//                                       a family's style at a size in 1/64 px (a style the family
//                                       lacks is made: bold thickened, italic slanted)
//   f->ascent, f->descent, f->height    its metrics, 1/64 px (height: from a line to the next)
//   fnt::advance (f, cp)                a character's advance, 1/64 px (the design's: exact at any
//                                       size, so a line breaks the same at every zoom)
//   fnt::kern (f, a, b)                 the pair's kerning, 1/64 px
//   fnt::draw (cv, f, x64, y, cp, c, clip...)  a character at x (1/64 px: 4 positions a pixel)
//                                       on the baseline y, in colour c, blended over the canvas
//   fnt::trim ()                        drop the least used sizes when the cache is big (at a
//                                       moment no Font pointer is held: after a redraw)
//
// A character a font lacks is taken from DejaVu Sans (the card's widest one), same style.
//
#ifndef _ft_fonts_h
#define _ft_fonts_h

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_SIZES_H
#include FT_ADVANCES_H
#include "kapi.h"
#include "wtk/canvas.h"

namespace fnt {

enum { BOLD = 1, ITALIC = 2 };
enum { MAXFACE = 96, MAXFAM = 48, MAXFONT = 512 };

struct Glyph
{
	unsigned cp;			// the character (0: an empty slot)
	int gi;				// its glyph in src's face (0: none, a box drawn)
	int adv;			// advance, 1/64 px
	struct Font *src;		// the font it comes from (this one or the fallback)
	unsigned char *bmp[4];		// coverage bitmaps, one per quarter-pixel position (lazily)
	short bl[4], bt[4], bw[4], bh[4];	// their left / top (from the pen) / size
};

struct Font
{
	int fam, style, size64;
	int face;			// g_face[] index used
	bool fakeBold, fakeItalic;	// the style is made
	FT_Size size;
	int ascent, descent, height;	// 1/64 px (descent positive, below the baseline)
	int ulPos, ulThick;		// underline: its top below the baseline, its thickness (1/64 px)
	int xHeight;			// the x-height (strike-through line: at its half)
	int boldX;			// a made bold's extra advance
	Glyph *tab; int cap, n;		// the glyphs (open addressing on cp)
	Font *fallback;			// DejaVu Sans at the same style and size (lazily; 0: none)
	bool fbTried;
	unsigned used;			// last get () (the least used go first)
	unsigned bytes;			// its bitmaps' size
};

struct FaceFile
{
	char path[112];
	char family[48];
	int style;			// BOLD / ITALIC from the file's own tables
	int weight;			// OS/2 usWeightClass (400 regular, 700 bold)
	unsigned char *data; unsigned len;
	FT_Face face;
	bool failed;
};

struct Family { char name[48]; int file[4]; };	// file[style]: g_face[] index, -1 none

// ---- state -------------------------------------------------------------------------------------------
static FT_Library g_lib;
static bool g_init, g_ok;
static FaceFile g_face[MAXFACE]; static int g_nface;
static Family g_fam[MAXFAM]; static int g_nfam;
static Font *g_font[MAXFONT]; static int g_nfont;
static unsigned g_tick, g_bytes;
static int g_fallbackFam = -1;
static unsigned char g_gamma[256];	// coverage -> opacity (a slightly heavier stroke on screen)

// ---- small helpers -----------------------------------------------------------------------------------
static inline int s_len (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline void s_cpy (char *d, const char *s, int cap) { int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static inline int lc (int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int s_icmp (const char *a, const char *b)
{
	for (;; a++, b++)
	{
		int x = lc ((unsigned char) *a), y = lc ((unsigned char) *b);
		if (x != y || !x) return x - y;
	}
}
static unsigned be16 (const unsigned char *p) { return (unsigned) p[0] << 8 | p[1]; }
static unsigned be32 (const unsigned char *p) { return (unsigned) p[0] << 24 | (unsigned) p[1] << 16 | (unsigned) p[2] << 8 | p[3]; }

static bool read_at (void *f, unsigned pos, void *buf, unsigned n)
{
	if (kapi_seek (f, pos) < 0) return false;
	return kapi_read (f, buf, n) == (int) n;
}

// A font file's family name and style from its own tables (no FreeType: 3 small reads). False: not
// a TrueType font.
static bool scan_file (const char *path, FaceFile &ff)
{
	void *f = kapi_open (path);
	if (!f) return false;
	bool ok = false;
	unsigned char hd[12], dir[16 * 40];
	if (read_at (f, 0, hd, 12))
	{
		unsigned ver = be32 (hd), nt = be16 (hd + 4);
		if ((ver == 0x00010000u || ver == 0x74727565u) && nt > 0 && nt <= 40 && read_at (f, 12, dir, nt * 16))
		{
			unsigned nameOff = 0, nameLen = 0, headOff = 0, os2Off = 0;
			for (unsigned i = 0; i < nt; i++)
			{
				const unsigned char *r = dir + i * 16;
				unsigned tag = be32 (r), off = be32 (r + 8), len = be32 (r + 12);
				if (tag == 0x6E616D65u) { nameOff = off; nameLen = len; }	// 'name'
				else if (tag == 0x68656164u) headOff = off;			// 'head'
				else if (tag == 0x4F532F32u) os2Off = off;			// 'OS/2'
			}
			ff.style = 0; ff.weight = 400;
			unsigned char b[64];
			if (headOff && read_at (f, headOff + 44, b, 2)) ff.style = (int) (be16 (b) & 3);	// macStyle
			if (os2Off && read_at (f, os2Off, b, 64))
			{
				ff.weight = (int) be16 (b + 4);
				unsigned sel = be16 (b + 62);
				if (sel & 1) ff.style |= ITALIC;
				if (sel & 0x20) ff.style |= BOLD;
			}
			if (nameOff && nameLen > 6 && nameLen < 200000)
			{
				unsigned char *nm = new unsigned char[nameLen];
				if (read_at (f, nameOff, nm, nameLen))
				{
					unsigned cnt = be16 (nm + 2), strOff = be16 (nm + 4);
					int best = -1, bestScore = 0;
					for (unsigned i = 0; i < cnt && 6 + i * 12 + 12 <= nameLen; i++)
					{
						const unsigned char *r = nm + 6 + i * 12;
						unsigned plat = be16 (r), enc = be16 (r + 2), lang = be16 (r + 4), id = be16 (r + 6);
						if (id != 1) continue;			// the (legacy) family name
						int sc = plat == 3 && enc == 1 && lang == 0x409 ? 3 : plat == 3 && enc == 1 ? 2 : plat == 1 && enc == 0 ? 1 : 0;
						if (sc > bestScore) { bestScore = sc; best = (int) i; }
					}
					if (best >= 0)
					{
						const unsigned char *r = nm + 6 + best * 12;
						unsigned plat = be16 (r), len = be16 (r + 8), off = strOff + be16 (r + 10);
						int k = 0;
						if (off + len <= nameLen)
						{
							if (plat == 3) for (unsigned j = 0; j + 1 < len && k < 47; j += 2) { unsigned c = be16 (nm + off + j); ff.family[k++] = c < 256 ? (char) c : '?'; }
							else for (unsigned j = 0; j < len && k < 47; j++) ff.family[k++] = (char) nm[off + j];
						}
						ff.family[k] = 0;
						ok = k > 0;
					}
				}
				delete[] nm;
			}
		}
	}
	kapi_close (f);
	return ok;
}

static bool is_ttf (const char *n)
{
	int l = s_len (n);
	return l > 4 && n[l - 4] == '.' && lc (n[l - 3]) == 't' && lc (n[l - 2]) == 't' && lc (n[l - 1]) == 'f';
}

static void scan_dir (const char *dir)
{
	void *d = kapi_opendir (dir);
	if (!d) return;
	kapi_dirent e;
	while (g_nface < MAXFACE && kapi_readdir (d, &e) > 0)
	{
		if (e.is_dir || !is_ttf (e.name)) continue;
		FaceFile &ff = g_face[g_nface];
		int n = 0;
		for (const char *p = dir; *p && n < 100; p++) ff.path[n++] = *p;
		if (n && ff.path[n - 1] != '/') ff.path[n++] = '/';
		for (const char *p = e.name; *p && n < (int) sizeof ff.path - 1; p++) ff.path[n++] = *p;
		ff.path[n] = 0;
		ff.data = 0; ff.face = 0; ff.failed = false;
		if (scan_file (ff.path, ff)) g_nface++;
	}
	kapi_closedir (d);
}

// The style slot a file fills in its family: the one it says; a family with two files for one slot
// (a light and a regular...) keeps the one nearest the slot's weight.
static void add_to_family (int fi)
{
	FaceFile &ff = g_face[fi];
	int f = -1;
	for (int i = 0; i < g_nfam; i++) if (s_icmp (g_fam[i].name, ff.family) == 0) { f = i; break; }
	if (f < 0)
	{
		if (g_nfam >= MAXFAM) return;
		f = g_nfam++;
		s_cpy (g_fam[f].name, ff.family, sizeof g_fam[f].name);
		for (int s = 0; s < 4; s++) g_fam[f].file[s] = -1;
	}
	int s = ff.style & 3, old = g_fam[f].file[s];
	if (old >= 0)
	{
		int want = s & BOLD ? 700 : 400;
		int dOld = g_face[old].weight - want, dNew = ff.weight - want;
		if ((dOld < 0 ? -dOld : dOld) <= (dNew < 0 ? -dNew : dNew)) return;
	}
	g_fam[f].file[s] = fi;
}

// ---- the families -------------------------------------------------------------------------------------
static bool init ()
{
	if (g_init) return g_ok;
	g_init = true;
	for (int i = 0; i < 256; i++)				// opacity = coverage ^ (1 / 1.25)
	{
		// (integer pow: a table of (i/255)^0.8 by bisection on x^5 = (i/255)^4)
		unsigned lo = 0, hi = 255;
		unsigned long long t = (unsigned long long) i * i * i * i;	// i^4 (/ 255^4)
		while (lo < hi)
		{
			unsigned m = (lo + hi + 1) / 2;
			unsigned long long m5 = (unsigned long long) m * m * m * m * m;	// m^5 (/ 255^5)
			if (m5 <= t * 255ull) lo = m; else hi = m - 1;
		}
		g_gamma[i] = (unsigned char) lo;
	}
	if (FT_Init_FreeType (&g_lib)) return false;
	scan_dir ("SD:/res/fonts");
	scan_dir ("SD:/fonts");
	for (int i = 0; i < g_nface; i++) add_to_family (i);
	for (int i = 1; i < g_nfam; i++)			// sorted by name
		for (int j = i; j > 0 && s_icmp (g_fam[j - 1].name, g_fam[j].name) > 0; j--)
		{ Family t = g_fam[j]; g_fam[j] = g_fam[j - 1]; g_fam[j - 1] = t; }
	g_fallbackFam = -1;
	for (int i = 0; i < g_nfam; i++) if (s_icmp (g_fam[i].name, "DejaVu Sans") == 0) g_fallbackFam = i;
	g_ok = g_nfam > 0;
	return g_ok;
}

static int count () { return g_nfam; }
static const char *name (int i) { return i >= 0 && i < g_nfam ? g_fam[i].name : ""; }
static int find (const char *n)
{
	for (int i = 0; i < g_nfam; i++) if (s_icmp (g_fam[i].name, n) == 0) return i;
	return -1;
}
// The styles a family has (a mask of 1 << style).
static int styles (int fam)
{
	int m = 0;
	if (fam >= 0 && fam < g_nfam) for (int s = 0; s < 4; s++) if (g_fam[fam].file[s] >= 0) m |= 1 << s;
	return m;
}

// A face file's FreeType face, the file read at its first use.
static FT_Face open_face (int fi)
{
	FaceFile &ff = g_face[fi];
	if (ff.face || ff.failed) return ff.face;
	ff.failed = true;
	void *f = kapi_open (ff.path);
	if (!f) return 0;
	unsigned n = kapi_fsize (f);
	ff.data = n ? new unsigned char[n] : 0;
	if (ff.data && kapi_read (f, ff.data, n) == (int) n && FT_New_Memory_Face (g_lib, ff.data, (FT_Long) n, 0, &ff.face) == 0)
	{ ff.len = n; ff.failed = false; }
	else { delete[] ff.data; ff.data = 0; ff.face = 0; }
	kapi_close (f);
	return ff.face;
}

// ---- sized fonts --------------------------------------------------------------------------------------
static void free_glyphs (Font *f)
{
	for (int i = 0; i < f->cap; i++)
		for (int p = 0; p < 4; p++) if (f->tab[i].bmp[p]) delete[] f->tab[i].bmp[p];
	delete[] f->tab;
	g_bytes -= f->bytes;
	f->tab = 0; f->cap = f->n = 0; f->bytes = 0;
}

static Font *make (int fam, int style, int size64)
{
	if (fam < 0 || fam >= g_nfam) return 0;
	const Family &F = g_fam[fam];
	int file = F.file[style], made = 0;		// the file, and what is made of it
	if (file < 0 && style == (BOLD | ITALIC))
	{
		if (F.file[BOLD] >= 0) { file = F.file[BOLD]; made = ITALIC; }
		else if (F.file[ITALIC] >= 0) { file = F.file[ITALIC]; made = BOLD; }
	}
	if (file < 0 && F.file[0] >= 0) { file = F.file[0]; made = style; }
	for (int s = 0; file < 0 && s < 4; s++) if (F.file[s] >= 0) { file = F.file[s]; made = style & ~s; }
	if (file < 0) return 0;
	FT_Face face = open_face (file);
	if (!face) return 0;
	Font *f = new Font;
	f->fam = fam; f->style = style; f->size64 = size64; f->face = file;
	f->fakeBold = (made & BOLD) != 0; f->fakeItalic = (made & ITALIC) != 0;
	f->size = 0;
	if (FT_New_Size (face, &f->size) || FT_Activate_Size (f->size) || FT_Set_Char_Size (face, 0, size64 < 64 ? 64 : size64, 72, 72))
	{ delete f; return 0; }
	FT_Fixed ys = f->size->metrics.y_scale;
	f->ascent = (int) FT_MulFix (face->ascender, ys);
	f->descent = (int) -FT_MulFix (face->descender, ys);
	f->height = (int) FT_MulFix (face->height, ys);
	if (f->height < f->ascent + f->descent) f->height = f->ascent + f->descent;
	f->ulThick = (int) FT_MulFix (face->underline_thickness, ys);
	if (f->ulThick < 64) f->ulThick = 64;
	f->ulPos = (int) -FT_MulFix (face->underline_position, ys) - f->ulThick / 2;
	if (f->ulPos < 64) f->ulPos = 64;
	f->xHeight = f->ascent * 53 / 100;		// (the fonts' OS/2 sxHeight, near enough)
	f->boldX = f->fakeBold ? size64 / 24 : 0;
	f->cap = 128; f->n = 0; f->bytes = 0;
	f->tab = new Glyph[f->cap];
	for (int i = 0; i < f->cap; i++) { f->tab[i].cp = 0; for (int p = 0; p < 4; p++) f->tab[i].bmp[p] = 0; }
	f->fallback = 0; f->fbTried = false;
	f->used = g_tick;
	return f;
}

// A family's style at a size (1/64 px).
static Font *get (int fam, int style, int size64)
{
	if (!g_ok) return 0;
	style &= 3;
	if (fam < 0 || fam >= g_nfam) fam = g_fallbackFam >= 0 ? g_fallbackFam : 0;
	g_tick++;
	for (int i = 0; i < g_nfont; i++)
	{
		Font *f = g_font[i];
		if (f->fam == fam && f->style == style && f->size64 == size64)
		{
			f->used = g_tick;
			if (i > 0) { g_font[i] = g_font[i - 1]; g_font[i - 1] = f; }	// (the busiest first)
			return f;
		}
	}
	if (g_nfont >= MAXFONT)						// (full: the family's nearest size)
	{
		Font *best = 0;
		for (int i = 0; i < g_nfont; i++)
		{
			Font *f = g_font[i];
			if (f->fam != fam || f->style != style) continue;
			int d = f->size64 - size64, bd = best ? best->size64 - size64 : 0;
			if (!best || (d < 0 ? -d : d) < (bd < 0 ? -bd : bd)) best = f;
		}
		return best ? best : g_font[0];
	}
	Font *f = make (fam, style, size64);
	if (f) g_font[g_nfont++] = f;
	return f;
}

// Forget the fonts not used lately when the cache holds too much (a Font from get () is then stale:
// only at a moment no Font pointer is held -- after a redraw; the app forgets its own, g_onTrim).
static void (*g_onTrim) ();
static void trim (unsigned maxBytes = 6u << 20, int maxFonts = 96)
{
	if (g_bytes <= maxBytes && g_nfont <= maxFonts) return;
	if (g_onTrim) g_onTrim ();
	while ((g_bytes > maxBytes || g_nfont > maxFonts) && g_nfont > 1)
	{
		int lru = 0;
		for (int i = 1; i < g_nfont; i++) if (g_font[i]->used < g_font[lru]->used) lru = i;
		Font *f = g_font[lru];
		if (f->used + 2 >= g_tick && g_bytes <= maxBytes * 2) break;	// (all in use: keep them)
		for (int i = 0; i < g_nfont; i++)				// (its borrowers' glyphs pointed at it)
			if (g_font[i]->fallback == f)
			{
				Font *b = g_font[i];
				free_glyphs (b);
				b->cap = 128; b->tab = new Glyph[b->cap];
				for (int k = 0; k < b->cap; k++) { b->tab[k].cp = 0; for (int p = 0; p < 4; p++) b->tab[k].bmp[p] = 0; }
				b->fallback = 0; b->fbTried = false;
			}
		free_glyphs (f);
		FT_Done_Size (f->size);
		delete f;
		g_font[lru] = g_font[--g_nfont];
	}
}

// ---- glyphs -------------------------------------------------------------------------------------------
static Glyph *slot (Font *f, unsigned cp)
{
	unsigned m = (unsigned) f->cap - 1, h = (cp * 2654435761u) & m;
	while (f->tab[h].cp && f->tab[h].cp != cp) h = (h + 1) & m;
	return &f->tab[h];
}

static void grow (Font *f)
{
	Glyph *old = f->tab; int oc = f->cap;
	f->cap *= 2;
	f->tab = new Glyph[f->cap];
	for (int i = 0; i < f->cap; i++) { f->tab[i].cp = 0; for (int p = 0; p < 4; p++) f->tab[i].bmp[p] = 0; }
	for (int i = 0; i < oc; i++) if (old[i].cp) *slot (f, old[i].cp) = old[i];
	delete[] old;
}

static Glyph *glyph (Font *f, unsigned cp)
{
	if (cp == 0) cp = ' ';
	Glyph *g = slot (f, cp);
	if (g->cp == cp) return g;
	if ((f->n + 1) * 2 > f->cap) { grow (f); g = slot (f, cp); }
	FT_Face face = g_face[f->face].face;
	Font *src = f;
	FT_UInt gi = FT_Get_Char_Index (face, cp);
	if (gi == 0 && cp >= 32)					// not in this font: DejaVu Sans's
	{
		if (!f->fbTried)
		{
			f->fbTried = true;
			if (g_fallbackFam >= 0 && g_fallbackFam != f->fam) f->fallback = get (g_fallbackFam, f->style, f->size64);
		}
		Font *fb = f->fallback;
		if (fb)
		{
			FT_UInt g2 = FT_Get_Char_Index (g_face[fb->face].face, cp);
			if (g2) { src = fb; gi = g2; face = g_face[fb->face].face; }
		}
	}
	g->cp = cp; g->gi = (int) gi; g->src = src;
	for (int p = 0; p < 4; p++) g->bmp[p] = 0;
	FT_Fixed a = 0;
	FT_Activate_Size (src->size);
	if (FT_Get_Advance (face, gi, FT_LOAD_NO_HINTING, &a) != 0) a = 0;
	g->adv = (int) (a >> 10) + src->boldX;
	if (gi == 0 && cp >= 32) g->adv = f->size64 / 2;		// (an empty box)
	f->n++;
	return g;
}

static int advance (Font *f, unsigned cp) { return f ? glyph (f, cp)->adv : 0; }

static int kern (Font *f, unsigned a, unsigned b)
{
	if (!f) return 0;
	FT_Face face = g_face[f->face].face;
	if (!FT_HAS_KERNING (face)) return 0;
	// (each read at once: fetching b's glyph may grow the cache and move a's)
	Glyph *ga = glyph (f, a);
	bool okA = ga->src == f && ga->gi; unsigned giA = ga->gi;
	Glyph *gb = glyph (f, b);
	if (!okA || gb->src != f || !gb->gi) return 0;
	unsigned giB = gb->gi;
	FT_Vector v;
	FT_Activate_Size (f->size);
	if (FT_Get_Kerning (face, (FT_UInt) giA, (FT_UInt) giB, FT_KERNING_UNFITTED, &v)) return 0;
	return (int) v.x;
}

// The coverage bitmap of a glyph at a quarter-pixel position.
static void render (Font *f, Glyph *g, int ph)
{
	Font *src = g->src;
	FT_Face face = g_face[src->face].face;
	g->bl[ph] = g->bt[ph] = g->bw[ph] = g->bh[ph] = 0;
	FT_Activate_Size (src->size);
	if (FT_Load_Glyph (face, (FT_UInt) g->gi, FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP) != 0) return;
	FT_GlyphSlot sl = face->glyph;
	if (sl->format != FT_GLYPH_FORMAT_OUTLINE) return;
	if (src->fakeBold) FT_Outline_EmboldenXY (&sl->outline, src->boldX, src->boldX);
	if (src->fakeItalic)
	{
		FT_Matrix sh = { 0x10000, 0x0366A, 0, 0x10000 };	// (a 12 degree slant)
		FT_Outline_Transform (&sl->outline, &sh);
	}
	if (ph) FT_Outline_Translate (&sl->outline, ph * 16, 0);
	if (FT_Render_Glyph (sl, FT_RENDER_MODE_NORMAL) != 0) return;
	const FT_Bitmap &b = sl->bitmap;
	int w = (int) b.width, h = (int) b.rows;
	if (w <= 0 || h <= 0) { g->bmp[ph] = new unsigned char[1]; return; }
	unsigned char *px = new unsigned char[w * h];
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) px[y * w + x] = b.buffer[y * b.pitch + x];
	g->bmp[ph] = px;
	g->bl[ph] = (short) sl->bitmap_left; g->bt[ph] = (short) sl->bitmap_top;
	g->bw[ph] = (short) w; g->bh[ph] = (short) h;
	f->bytes += (unsigned) (w * h); g_bytes += (unsigned) (w * h);
	(void) f;
}

static inline void blend (unsigned &d, unsigned c, int a)
{
	if (a >= 255) { d = c; return; }
	unsigned rb = d & 0xFF00FF, gg = d & 0x00FF00;
	rb += (((c & 0xFF00FF) - rb) * (unsigned) a >> 8) & 0xFF00FF;
	gg += (((c & 0x00FF00) - gg) * (unsigned) a >> 8) & 0x00FF00;
	d = (d & 0xFF000000u) | rb | gg;
}

// A character at x64 (1/64 px) on the baseline y, in colour c, clipped to [cx0, cx1) x [cy0, cy1).
static void draw (wtk::Canvas &cv, Font *f, int x64, int y, unsigned cp, unsigned c,
		  int cx0, int cy0, int cx1, int cy1)
{
	if (!f || cp <= 32 || cp == 0xA0) return;
	Glyph *g = glyph (f, cp);
	int ph = (x64 & 63) >> 4, x = x64 >> 6;
	if (cx0 < 0) cx0 = 0;
	if (cy0 < 0) cy0 = 0;
	if (cx1 > cv.w) cx1 = cv.w;
	if (cy1 > cv.h) cy1 = cv.h;
	if (g->gi == 0)							// no such character anywhere: a box
	{
		int bw = g->adv >> 6, bh = f->ascent * 7 / 10 >> 6;
		for (int i = 1; i < bw - 1; i++)
			for (int j = 0; j <= bh; j += bh)
			{ int px = x + i, py = y - j; if (px >= cx0 && px < cx1 && py >= cy0 && py < cy1) blend (cv.px[py * cv.stride + px], c, 160); }
		for (int j = 0; j <= bh; j++)
			for (int i = 1; i < bw - 1; i += bw - 3 > 0 ? bw - 3 : 1)
			{ int px = x + i, py = y - j; if (px >= cx0 && px < cx1 && py >= cy0 && py < cy1) blend (cv.px[py * cv.stride + px], c, 160); }
		return;
	}
	if (!g->bmp[ph]) render (f, g, ph);
	const unsigned char *b = g->bmp[ph];
	if (!b) return;
	int gx = x + g->bl[ph], gy = y - g->bt[ph], w = g->bw[ph], h = g->bh[ph];
	int i0 = cx0 - gx > 0 ? cx0 - gx : 0, i1 = cx1 - gx < w ? cx1 - gx : w;
	int j0 = cy0 - gy > 0 ? cy0 - gy : 0, j1 = cy1 - gy < h ? cy1 - gy : h;
	for (int j = j0; j < j1; j++)
	{
		unsigned *d = cv.px + (gy + j) * cv.stride + gx;
		const unsigned char *s = b + j * w;
		for (int i = i0; i < i1; i++) if (s[i]) blend (d[i], c, g_gamma[s[i]]);
	}
}

// A string (Latin-1 / ASCII) at x, baseline y, the pen's end returned (1/64 px). For labels.
static int draw_str (wtk::Canvas &cv, Font *f, int x64, int y, const char *s, unsigned c)
{
	unsigned prev = 0;
	for (; s && *s; s++)
	{
		unsigned cp = (unsigned char) *s;
		if (prev) x64 += kern (f, prev, cp);
		draw (cv, f, x64, y, cp, c, 0, 0, cv.w, cv.h);
		x64 += advance (f, cp);
		prev = cp;
	}
	return x64;
}
static int str_w (Font *f, const char *s)
{
	int x = 0; unsigned prev = 0;
	for (; s && *s; s++) { unsigned cp = (unsigned char) *s; if (prev) x += kern (f, prev, cp); x += advance (f, cp); prev = cp; }
	return x;
}

} // namespace fnt

#endif
