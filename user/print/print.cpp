//
// print/print.cpp -- the print library (SD:/lib/print.so; print/print.h): a job's pages recorded into the
// spool (print/job.h), the fonts found by their family, the printers' list, the requests to printd.
// The Print dialog is print/dialog.cpp.
//
// The library uses two others, opened when first needed (kapi_lib_open: the same tables the program has,
// or the libraries loaded for us): FreeType (ft.so) for print_text's glyphs and advances, wtk (wtk.so) for
// the dialog. Their import stubs are linked in; they go through onyx_ft_table / onyx_wtk_table, set here.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include <ft2build.h>
#include FT_FREETYPE_H
#include "lib.h"
#include "print/print.h"
#include "print/job.h"
#include "print/printers.h"
#include "print/priv.h"

using namespace pprt;

extern "C" {
int onyx_lib_init (const TLibImports *imp);		// (librt.cpp)
const void *onyx_ft_table;				// the import stubs' tables (lib/ft_stubs.o, lib/wtk_stubs.o)
const void *onyx_wtk_table;
}

static TLibImports s_imp;

extern "C" int print_lib_init (const TLibImports *imp)
{
	int r = onyx_lib_init (imp);
	if (r < 0) return -1;
	if (r > 0) { s_imp = *imp; s_imp.size = sizeof s_imp; if (imp->size < sizeof s_imp) { s_imp.ndata = 0; s_imp.data = 0; } }
	return 0;
}
static const void *open_lib (const char *name, unsigned version)
{
	int err = 0;
	const TLibHeader *t = (const TLibHeader *) kapi_lib_open (name, version, &err);
	return t != 0 && t->init (&s_imp) >= 0 ? t : 0;
}
bool print__need_ft ()	{ if (!onyx_ft_table) onyx_ft_table = open_lib ("ft", 116); return onyx_ft_table != 0; }
bool print__need_wtk ()	{ if (!onyx_wtk_table) onyx_wtk_table = open_lib ("wtk", 683); return onyx_wtk_table != 0; }

// ---- the fonts of the card: each file's family and style, read from its tables (not the whole file) ----
struct FontFile { char path[112], family[48]; int style; unsigned char *data; unsigned len; FT_Face face; float size; };
enum { MAXFILES = 64 };
static FontFile s_file[MAXFILES]; static int s_nfile = -1;
static FT_Library s_ft;

static unsigned be16 (const unsigned char *p) { return (unsigned) p[0] << 8 | p[1]; }
static unsigned be32 (const unsigned char *p) { return (unsigned) p[0] << 24 | (unsigned) p[1] << 16 | (unsigned) p[2] << 8 | p[3]; }
static bool ieq (const char *a, const char *b)
{
	for (; *a && *b; a++, b++) { char x = *a, y = *b; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return false; }
	return *a == *b;
}
static void scan_file (const char *path)
{
	if (s_nfile >= MAXFILES) return;
	long long h = kapi_file_open (path, KAPI_O_RDONLY, 0);
	if (h <= 0) return;
	unsigned char d[12 + 64 * 16];
	FontFile &f = s_file[s_nfile];
	f.family[0] = 0; f.style = 0; f.data = 0; f.len = 0; f.face = 0; f.size = 0;
	if (kapi_file_read (h, d, 12, 0) == 12 && (be32 (d) == 0x00010000 || be32 (d) == 0x74727565))
	{
		unsigned nt = be16 (d + 4); if (nt > 64) nt = 64;
		if (kapi_file_read (h, d + 12, nt * 16, 12) == (long long) nt * 16)
			for (unsigned i = 0; i < nt; i++)
			{
				const unsigned char *e = d + 12 + i * 16; unsigned off = be32 (e + 8), len = be32 (e + 12);
				if (e[0] == 'h' && e[1] == 'e' && e[2] == 'a' && e[3] == 'd')
				{
					unsigned char m[2];
					if (kapi_file_read (h, m, 2, off + 44) == 2) f.style = (m[1] & 1 ? PRINT_BOLD : 0) | (m[1] & 2 ? PRINT_ITALIC : 0);
				}
				else if (e[0] == 'n' && e[1] == 'a' && e[2] == 'm' && e[3] == 'e' && len >= 6)
				{
					if (len > 32768) len = 32768;
					unsigned char *nm = new unsigned char[len];
					if (kapi_file_read (h, nm, len, off) == (long long) len)
					{
						unsigned cnt = be16 (nm + 2), so = be16 (nm + 4); int best = 0;
						for (unsigned k = 0; k < cnt && 6 + (k + 1) * 12 <= len; k++)
						{
							const unsigned char *r = nm + 6 + k * 12;
							unsigned plat = be16 (r), id = be16 (r + 6), l = be16 (r + 8), o = so + be16 (r + 10);
							if (id != 1 || o + l > len) continue;
							int rank = plat == 3 ? 2 : 1;		// (Windows' names: UTF-16; the Mac's: bytes)
							if (rank <= best) continue;
							int n = 0;
							if (plat == 3) { for (unsigned c = 0; c + 1 < l && n < 47; c += 2) f.family[n++] = (char) (nm[o + c] ? '?' : nm[o + c + 1]); }
							else for (unsigned c = 0; c < l && n < 47; c++) f.family[n++] = (char) nm[o + c];
							f.family[n] = 0; best = rank;
						}
					}
					delete[] nm;
				}
			}
	}
	kapi_handle_close (h);
	if (f.family[0]) { scpy (f.path, sizeof f.path, path); s_nfile++; }
}
static void scan_fonts ()
{
	if (s_nfile >= 0) return;
	s_nfile = 0;
	static const char *const DIR[2] = { "SD:/res/fonts", "SD:/fonts" };
	for (int k = 0; k < 2; k++)
	{
		void *d = kapi_opendir (DIR[k]);
		if (!d) continue;
		struct kapi_dirent e;
		while (kapi_readdir (d, &e))
		{
			int n = slen (e.name);
			if (n < 5 || e.name[n - 4] != '.' || (e.name[n - 3] | 32) != 't' || (e.name[n - 2] | 32) != 't' || (e.name[n - 1] | 32) != 'f') continue;
			char p[160]; scpy (p, sizeof p, DIR[k]); scat (p, sizeof p, "/"); scat (p, sizeof p, e.name);
			scan_file (p);
		}
		kapi_closedir (d);
	}
}
static FontFile *find_font (const char *family, int style, unsigned *synth)
{
	scan_fonts ();
	for (int pass = 0; pass < 2; pass++)
	{
		FontFile *any = 0, *plain = 0;
		for (int i = 0; i < s_nfile; i++)
		{
			if (!ieq (s_file[i].family, family)) continue;
			if (s_file[i].style == style) { *synth = 0; return &s_file[i]; }
			if (s_file[i].style == 0) plain = &s_file[i];
			else if ((s_file[i].style & ~style) == 0) any = &s_file[i];
		}
		// the family without that face: the nearest one, the rest made (thickened, slanted)
		if (any) { *synth = (unsigned) (style & ~any->style); return any; }
		if (plain) { *synth = (unsigned) style; return plain; }
		family = "DejaVu Sans";
	}
	return 0;
}
static bool load_font (FontFile *f)
{
	if (f->face) return true;
	if (!print__need_ft ()) return false;
	if (!s_ft && FT_Init_FreeType (&s_ft)) { s_ft = 0; return false; }
	if (!f->data) f->data = (unsigned char *) pio_load (f->path, &f->len);
	if (!f->data || FT_New_Memory_Face (s_ft, f->data, (FT_Long) f->len, 0, &f->face)) { f->face = 0; return false; }
	f->size = 0;
	return true;
}

// ---- a job ----
struct JFont { FontFile *ff; int id; unsigned synth; };
struct PrintJob
{
	pjob::Writer w;
	PrintSetup s; char title[80];
	unsigned id; int seen; bool on;
	JFont font[32]; int nfont;
	pngsave::Buf path; int npath;
};

static void spool_path (char *out, int cap, unsigned id, const char *ext)
{
	scpy (out, cap, PRINT_SPOOL "/");
	pngsave::Buf b; putint (b, (long) id); b.put ((unsigned char) 0);
	scat (out, cap, (const char *) b.b); scat (out, cap, ext);
}

extern "C" {

PrintJob *print_begin (const PrintSetup *s, const char *title)
{
	if (!s) return 0;
	kapi_mkdir ("SD:/var"); kapi_mkdir ("SD:/var/spool"); kapi_mkdir (PRINT_SPOOL);
	PrintJob *j = new PrintJob;
	for (unsigned i = 0; i < sizeof j->s; i++) ((char *) &j->s)[i] = 0;
	unsigned n = s->size < sizeof j->s ? s->size : sizeof j->s;
	for (unsigned i = 0; i < n; i++) ((char *) &j->s)[i] = ((const char *) s)[i];
	scpy (j->title, sizeof j->title, title && title[0] ? title : "Document");
	j->seen = 0; j->on = false; j->nfont = 0; j->npath = 0;
	// a number no job of the spool has
	static unsigned seq_;
	char p[160];
	for (unsigned id = (kapi_get_ticks () & 0xFFFFF) * 16 + (++seq_ & 15) + 1; ; id++)
	{
		spool_path (p, sizeof p, id, ".job");
		void *f = kapi_open (p);
		if (f) { kapi_close (f); continue; }
		j->id = id; break;
	}
	spool_path (p, sizeof p, j->id, ".opj");
	if (!j->w.open (p)) { delete j; return 0; }
	return j;
}

int print_page (PrintJob *j, float w, float h)
{
	if (!j) return 0;
	if (j->on) j->w.end_page ();
	j->npath = 0; j->path.n = 0;
	j->seen++;
	j->on = j->s.from <= 0 || (j->seen >= j->s.from && (j->s.to <= 0 || j->seen <= j->s.to));
	if (j->on) j->w.begin_page (w > 0 ? w : j->s.paper_w, h > 0 ? h : j->s.paper_h);
	return j->on ? 1 : 0;
}

void print_rect (PrintJob *j, float x, float y, float w, float h, unsigned rgb) { if (j && j->on) j->w.rect (x, y, w, h, rgb); }

static void path_add (PrintJob *j, unsigned verb, float x, float y)
{
	pjob::PathPt p; p.verb = verb; p.x = x; p.y = y;
	j->path.put (&p, sizeof p); j->npath++;
}
void print_path_move (PrintJob *j, float x, float y) { if (j && j->on) path_add (j, pjob::V_MOVE, x, y); }
void print_path_line (PrintJob *j, float x, float y) { if (j && j->on) path_add (j, pjob::V_LINE, x, y); }
void print_path_curve (PrintJob *j, float x1, float y1, float x2, float y2, float x3, float y3)
{
	if (j && j->on) { path_add (j, pjob::V_CURVE, x1, y1); path_add (j, pjob::V_CURVE, x2, y2); path_add (j, pjob::V_CURVE, x3, y3); }
}
void print_path_close (PrintJob *j) { if (j && j->on) path_add (j, pjob::V_CLOSE, 0, 0); }
void print_path_fill (PrintJob *j, unsigned rgb)
{
	if (!j) return;
	if (j->on && j->npath) j->w.path ((const pjob::PathPt *) j->path.b, j->npath, rgb, 0);
	j->npath = 0; j->path.n = 0;
}
void print_path_stroke (PrintJob *j, float width, unsigned rgb)
{
	if (!j) return;
	if (j->on && j->npath) j->w.path ((const pjob::PathPt *) j->path.b, j->npath, rgb, width > 0 ? width : 0.25f);
	j->npath = 0; j->path.n = 0;
}
void print_line (PrintJob *j, float x0, float y0, float x1, float y1, float width, unsigned rgb)
{
	if (!j || !j->on) return;
	// (upright and level lines as rectangles: they stay sharp on the printer's grid)
	if (x0 == x1) { j->w.rect (x0 - width / 2, y0 < y1 ? y0 : y1, width, y0 < y1 ? y1 - y0 : y0 - y1, rgb); return; }
	if (y0 == y1) { j->w.rect (x0 < x1 ? x0 : x1, y0 - width / 2, x0 < x1 ? x1 - x0 : x0 - x1, width, rgb); return; }
	pjob::PathPt p[2] = { { pjob::V_MOVE, x0, y0 }, { pjob::V_LINE, x1, y1 } };
	j->w.path (p, 2, rgb, width > 0 ? width : 0.25f);
}
void print_frame (PrintJob *j, float x, float y, float w, float h, float width, unsigned rgb)
{
	if (!j || !j->on) return;
	j->w.rect (x, y, w, width, rgb); j->w.rect (x, y + h - width, w, width, rgb);
	j->w.rect (x, y, width, h, rgb); j->w.rect (x + w - width, y, width, h, rgb);
}

void print_image (PrintJob *j, const unsigned *px, int pw, int ph, float x, float y, float w, float h, int flags)
{
	if (!j || !j->on || !px || pw <= 0 || ph <= 0 || w <= 0 || h <= 0) return;
	// more pixels than a printer shows (300 an inch): averaged down first -- a smaller job, a faster print
	int k = 1;
	while (pw / (k + 1) >= w / 72.0f * 300.0f && ph / (k + 1) >= h / 72.0f * 300.0f) k++;
	int ow = pw / k, oh = ph / k;
	unsigned *o = new unsigned[(size_t) ow * oh];
	bool alpha = (flags & PRINT_IMG_ALPHA) != 0, opaque = true;
	for (int yy = 0; yy < oh; yy++) for (int xx = 0; xx < ow; xx++)
	{
		unsigned a = 0, r = 0, g = 0, b = 0;
		for (int v = 0; v < k; v++) for (int u = 0; u < k; u++)
		{
			unsigned c = px[(size_t) (yy * k + v) * pw + xx * k + u], ca = alpha ? c >> 24 : 255;
			a += ca; r += ((c >> 16) & 255) * ca; g += ((c >> 8) & 255) * ca; b += (c & 255) * ca;
		}
		unsigned n = (unsigned) (k * k);
		if (a) { r /= a; g /= a; b /= a; }
		a /= n;
		if (a < 255) opaque = false;
		o[(size_t) yy * ow + xx] = a << 24 | r << 16 | g << 8 | b;
	}
	unsigned len = 0; unsigned char *d = 0; int kind = pjob::IMG_DEFLATE;
	if ((flags & PRINT_IMG_PHOTO) && opaque) { d = pngsave::jpeg_encode (o, ow, oh, 90, &len); kind = pjob::IMG_JPEG; }
	if (!d) { d = pngsave::deflate ((const unsigned char *) o, (unsigned) ((size_t) ow * oh * 4), true, &len); kind = pjob::IMG_DEFLATE; }
	if (d) j->w.image (x, y, w, h, ow, oh, kind, d, len);
	delete[] d; delete[] o;
}

int print_font (PrintJob *j, const char *family, int style)
{
	if (!j || j->nfont >= 32) return -1;
	unsigned synth = 0;
	FontFile *f = find_font (family ? family : "DejaVu Sans", style & 3, &synth);
	if (!f || !load_font (f)) return -1;
	for (int i = 0; i < j->nfont; i++) if (j->font[i].ff == f && j->font[i].synth == synth) return i;
	int id = j->w.font (f->data, f->len);
	if (id < 0) return -1;
	JFont &jf = j->font[j->nfont]; jf.ff = f; jf.id = id; jf.synth = synth;
	return j->nfont++;
}
int print_font_data (PrintJob *j, const unsigned char *ttf, unsigned len)
{
	if (!j || j->nfont >= 32) return -1;
	int id = j->w.font (ttf, len);
	if (id < 0) return -1;
	for (int i = 0; i < j->nfont; i++) if (!j->font[i].ff && j->font[i].id == id) return i;
	JFont &jf = j->font[j->nfont]; jf.ff = 0; jf.id = id; jf.synth = 0;
	return j->nfont++;
}
void print_glyph (PrintJob *j, int font, float size, float x, float baseline, unsigned glyph, unsigned unicode, unsigned rgb, int style)
{
	if (!j || !j->on || font < 0 || font >= j->nfont) return;
	j->w.glyph (j->font[font].id, size, x, baseline, glyph, unicode, rgb, (unsigned) (style & 3) | j->font[font].synth);
}

static unsigned utf8 (const unsigned char *&s)
{
	unsigned c = *s++;
	if (c >= 0xC0) { int k = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1; c &= 0x3Fu >> k; while (k-- && (*s & 0xC0) == 0x80) c = c << 6 | (*s++ & 0x3F); }
	return c;
}
// the text's glyphs walked: drawn (j's page) or only measured -> the advance
static float run_text (PrintJob *j, int font, float size, float x, float y, const char *text, unsigned rgb, bool draw)
{
	if (!j || font < 0 || font >= j->nfont || !j->font[font].ff || !text || size <= 0) return 0;
	FontFile *f = j->font[font].ff;
	if (f->size != size) { if (FT_Set_Char_Size (f->face, 0, (FT_F26Dot6) (size * 64 + 0.5f), 72, 72)) return 0; f->size = size; }
	const unsigned char *s = (const unsigned char *) text;
	float pen = 0; unsigned prev = 0;
	bool kern = FT_HAS_KERNING (f->face);
	while (*s)
	{
		unsigned cp = utf8 (s);
		unsigned gi = FT_Get_Char_Index (f->face, cp);
		if (kern && prev && gi) { FT_Vector d; if (!FT_Get_Kerning (f->face, prev, gi, FT_KERNING_UNFITTED, &d)) pen += d.x / 64.0f; }
		if (FT_Load_Glyph (f->face, gi, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP)) continue;
		if (draw && j->on) j->w.glyph (j->font[font].id, size, x + pen, y, gi, cp, rgb, j->font[font].synth);
		pen += f->face->glyph->linearHoriAdvance / 65536.0f;
		prev = gi;
	}
	return pen;
}
float print_text (PrintJob *j, int font, float size, float x, float baseline, const char *text, unsigned rgb) { return run_text (j, font, size, x, baseline, text, rgb, true); }
float print_text_width (PrintJob *j, int font, float size, const char *text) { return run_text (j, font, size, 0, 0, text, 0, false); }
void print_font_metrics (PrintJob *j, int font, float size, float *ascent, float *descent, float *line)
{
	float a = size * 0.93f, d = size * 0.24f, l = size * 1.2f;
	if (j && font >= 0 && font < j->nfont && j->font[font].ff)
	{
		FT_Face f = j->font[font].ff->face;
		if (f->units_per_EM) { float k = size / f->units_per_EM; a = f->ascender * k; d = -f->descender * k; l = f->height * k; }
	}
	if (ascent) *ascent = a;
	if (descent) *descent = d;
	if (line) *line = l;
}

void print_abort (PrintJob *j)
{
	if (!j) return;
	j->w.close ();
	char p[160]; spool_path (p, sizeof p, j->id, ".opj"); kapi_remove (p);
	delete j;
}

int print_end (PrintJob *j)
{
	if (!j) return -1;
	if (j->on) j->w.end_page ();
	int pages = j->w.pages ();
	if (!j->w.close () || pages == 0) { print_abort (j); return -1; }
	// the ticket: what printd needs to print it
	pngsave::Buf b; char app[48]; print__app_name (app, sizeof app);
	put_kv (b, "printer", j->s.printer); put_kv (b, "title", j->title); put_kv (b, "media", j->s.media); put_kv (b, "output", j->s.output);
	put_kv (b, "app", app);
	put_ki (b, "copies", j->s.copies < 1 ? 1 : j->s.copies); put_ki (b, "mono", j->s.mono); put_ki (b, "quality", j->s.quality);
	put_ki (b, "pages", pages); put_ki (b, "landscape", j->s.orientation == PRINT_LANDSCAPE);
	char p[160]; spool_path (p, sizeof p, j->id, ".job");
	unsigned id = j->id;
	bool ok = kapi_save_file (p, b.b, b.n) == (int) b.n;
	delete j;
	PdReq rq; PdAnswer an;
	for (unsigned i = 0; i < sizeof rq; i++) ((char *) &rq)[i] = 0;
	rq.id = id;
	if (!ok || !print__request (PD_SUBMIT, rq, &an, sizeof an, 0, 5000) || !an.ok)
	{
		kapi_remove (p); spool_path (p, sizeof p, id, ".opj"); kapi_remove (p);
		return -1;
	}
	return (int) id;
}

// ---- the setup ----
static void setup_printer (PrintSetup *s, const Printer &p, const char *media, int orientation)
{
	scpy (s->printer, sizeof s->printer, p.name);
	char m[64]; bool have = false;
	for (int i = 0; media && media[0] && list_item (p.media, i, m, sizeof m); i++) if (seq (m, media)) have = true;
	scpy (s->media, sizeof s->media, have ? media : p.media_default[0] ? p.media_default : "iso_a4_210x297mm");
	float w = 595.28f, h = 841.89f;
	media_size (s->media, &w, &h);
	const float k = 72.0f / 2540.0f;			// (1/100 mm -> points)
	float ml = p.margin[0] * k, mt = p.margin[1] * k, mr = p.margin[2] * k, mb = p.margin[3] * k;
	s->orientation = orientation == PRINT_LANDSCAPE ? PRINT_LANDSCAPE : PRINT_PORTRAIT;
	if (s->orientation == PRINT_LANDSCAPE)
	{	// the page lies on the paper: its left edge is the paper's top (print/raster.h turns it that way)
		s->paper_w = h; s->paper_h = w;
		s->margin_l = mt; s->margin_t = mr; s->margin_r = mb; s->margin_b = ml;
	}
	else { s->paper_w = w; s->paper_h = h; s->margin_l = ml; s->margin_t = mt; s->margin_r = mr; s->margin_b = mb; }
	if (s->copies < 1) s->copies = 1;
	if (s->copies > p.copies) s->copies = p.copies < 1 ? 1 : p.copies;
	if (!p.color) s->mono = 1;
	if (!(p.quality & (1u << s->quality))) s->quality = p.quality & (1u << 4) ? 4 : p.quality & (1u << 5) ? 5 : 3;
}
void print_setup_default (PrintSetup *s)
{
	if (!s) return;
	for (unsigned i = 0; i < sizeof *s; i++) ((char *) s)[i] = 0;
	s->size = sizeof *s; s->copies = 1; s->quality = PRINT_NORMAL;
	List *l = new List; load (*l);
	const Printer *p = find (*l, l->def);
	setup_printer (s, p ? *p : l->p[0], 0, PRINT_PORTRAIT);
	delete l;
}
int print_setup_paper (PrintSetup *s, const char *media, int orientation)
{
	if (!s) return 0;
	List *l = new List; load (*l);
	const Printer *p = find (*l, s->printer);
	if (!p) p = &l->p[0];
	setup_printer (s, *p, media, orientation);
	int ok = !media || seq (s->media, media);
	delete l;
	return ok;
}

// ---- the printers, the queue ----
int print_printers (PrintPrinter *out, int max)
{
	List *l = new List; load (*l);
	int n = 0;
	for (int i = 0; i < l->n && n < max; i++, n++)
	{
		const Printer &p = l->p[i]; PrintPrinter &o = out[n];
		scpy (o.name, sizeof o.name, p.name); scpy (o.kind, sizeof o.kind, p.kind); scpy (o.uri, sizeof o.uri, p.uri); scpy (o.model, sizeof o.model, p.model);
		o.color = p.color; o.copies = p.copies; o.dpi = p.dpi; o.quality = p.quality; o.is_default = seq (p.name, l->def);
		scpy (o.media_default, sizeof o.media_default, p.media_default);
	}
	delete l;
	return n;
}
int print_printer_media (const char *printer, int i, char *name, int ncap, char *label, int lcap)
{
	List *l = new List; load (*l);
	const Printer *p = find (*l, printer);
	char m[64]; int ok = p && list_item (p->media, i, m, sizeof m);
	if (ok) { if (name) scpy (name, ncap, m); if (label) media_label (m, label, lcap); }
	delete l;
	return ok;
}
static int simple (int type, const char *name, const char *uri, unsigned id, char *text, int cap, unsigned wait)
{
	PdReq rq; PdAnswer an;
	for (unsigned i = 0; i < sizeof rq; i++) ((char *) &rq)[i] = 0;
	rq.id = id; scpy (rq.name, sizeof rq.name, name); scpy (rq.uri, sizeof rq.uri, uri);
	an.ok = 0; an.text[0] = 0;
	if (!print__request (type, rq, &an, sizeof an, 0, wait)) scpy (an.text, sizeof an.text, "the print service does not answer");
	if (text) scpy (text, cap, an.text);
	return an.ok;
}
int print_printer_add (const char *name, const char *address, char *err, int cap) { return simple (PD_ADD, name, address, 0, err, cap, 30000); }
int print_printer_remove (const char *name) { return simple (PD_REMOVE, name, 0, 0, 0, 0, 5000); }
int print_printer_default (const char *name) { return simple (PD_DEFAULT, name, 0, 0, 0, 0, 5000); }
int print_printer_status (const char *name, char *text, int cap) { return simple (PD_REFRESH, name, 0, 0, text, cap, 30000); }
int print_printers_find (PrintFound *out, int max)
{
	PdReq rq;
	for (unsigned i = 0; i < sizeof rq; i++) ((char *) &rq)[i] = 0;
	PdFound *f = new PdFound[16]; unsigned got = 0;
	int n = 0;
	if (print__request (PD_SCAN, rq, f, 16 * sizeof (PdFound), &got, 30000))
		for (unsigned i = 0; i < got / sizeof (PdFound) && n < max; i++, n++)
		{
			scpy (out[n].address, sizeof out[n].address, f[i].address); scpy (out[n].name, sizeof out[n].name, f[i].name);
			scpy (out[n].model, sizeof out[n].model, f[i].model); out[n].usable = f[i].usable;
		}
	delete[] f;
	return n;
}
int print_job_cancel (unsigned id) { return simple (PD_CANCEL, 0, 0, id, 0, 0, 5000); }
void print_jobs_forget (void) { simple (PD_FORGET, 0, 0, 0, 0, 0, 5000); }
int print_jobs (PrintJobInfo *out, int max)
{
	if (!kapi_ipc_lookup (PRINTD_SERVICE)) return 0;			// (no service: no job; not started for a look)
	PdReq rq;
	for (unsigned i = 0; i < sizeof rq; i++) ((char *) &rq)[i] = 0;
	PdJob *jb = new PdJob[32]; unsigned got = 0;
	int n = 0;
	if (print__request (PD_LIST, rq, jb, 32 * sizeof (PdJob), &got, 3000))
		for (unsigned i = 0; i < got / sizeof (PdJob) && n < max; i++, n++)
		{
			PrintJobInfo &o = out[n]; const PdJob &s = jb[i];
			o.id = s.id; o.state = s.state; o.pages = s.pages; o.page = s.page;
			scpy (o.printer, sizeof o.printer, s.printer); scpy (o.title, sizeof o.title, s.title); scpy (o.message, sizeof o.message, s.message);
		}
	delete[] jb;
	return n;
}

} // extern "C"

// ---- printd: a request, its answer (a file the request names: this program's mailbox stays its own) ----
void print__app_name (char *out, int cap)
{
	char d[128]; int n = kapi_app_dir (d, sizeof d);
	out[0] = 0;
	if (n <= 0 || n >= (int) sizeof d) return;
	d[n] = 0;
	int e = n; while (e > 0 && d[e - 1] == '/') e--;
	int s = e; while (s > 0 && d[s - 1] != '/' && d[s - 1] != ':') s--;
	int k = 0;
	for (int i = s; i < e && k < cap - 1; i++) { if (d[i] == '.' && i + 4 == e) break; out[k++] = d[i]; }
	out[k] = 0;
}
bool print__request (int type, PdReq &rq, void *answer, unsigned cap, unsigned *got, unsigned wait_ms)
{
	int pid = kapi_ipc_lookup (PRINTD_SERVICE);
	if (pid == 0)
	{
		if (!kapi_launch ("printd")) return false;
		for (int i = 0; i < 80 && pid == 0; i++) { kapi_msleep (25); pid = kapi_ipc_lookup (PRINTD_SERVICE); }
		if (pid == 0) return false;
	}
	static unsigned seq_;
	rq.token = (kapi_get_ticks () << 8) ^ ((unsigned) (unsigned long long) &seq_ >> 4) ^ ++seq_;
	char p[64]; scpy (p, sizeof p, PRINT_REPLY "/");
	pngsave::Buf b; putint (b, (long) rq.token); b.put ((unsigned char) 0);
	scat (p, sizeof p, (const char *) b.b);
	kapi_mailbox_send (pid, type, &rq, sizeof rq);
	for (unsigned waited = 0; waited <= wait_ms; waited += 20)
	{
		void *f = kapi_open (p);
		if (f)
		{
			int n = kapi_read (f, answer, cap);
			kapi_close (f); kapi_remove (p);
			if (got) *got = n > 0 ? (unsigned) n : 0;
			return n >= 0;
		}
		kapi_msleep (20);
	}
	return false;
}
