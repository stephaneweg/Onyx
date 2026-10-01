// writetest.cpp -- user/pdf/pdfwrite.h checked on the PC (tools/tests/run_pdf_test.sh): a document of two pages written with
// DejaVu Sans (and its bold) as subsets, a rectangle, an image with transparency, a link, bookmarks; then read back by the
// PDF Viewer's MuPDF: its pages, its text found again, its outline, its fonts embedded.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pdf/pdfwrite.h"
#include <ft2build.h>
#include FT_FREETYPE_H

static unsigned char *load (const char *p, unsigned *n)
{
	FILE *f = fopen (p, "rb"); if (!f) return 0;
	fseek (f, 0, SEEK_END); long l = ftell (f); fseek (f, 0, SEEK_SET);
	unsigned char *b = new unsigned char[l]; if (fread (b, 1, l, f) != (size_t) l) { fclose (f); return 0; } fclose (f); *n = (unsigned) l; return b;
}
int main (int argc, char **argv)
{
	if (argc < 2) { printf ("usage: writetest out.pdf\n"); return 2; }
	unsigned n1, n2;
	unsigned char *reg = load ("sdcard/res/fonts/DejaVuSans.ttf", &n1), *bold = load ("sdcard/res/fonts/DejaVuSans-Bold.ttf", &n2);
	if (!reg || !bold) { printf ("FAIL: fonts\n"); return 1; }
	FT_Library lib; FT_Init_FreeType (&lib);
	FT_Face fr, fb; FT_New_Memory_Face (lib, reg, n1, 0, &fr); FT_New_Memory_Face (lib, bold, n2, 0, &fb);
	pdfw::Writer w;
	int R = w.add_font (reg, n1), B = w.add_font (bold, n2);
	if (R < 0 || B < 0) { printf ("FAIL: add_font\n"); return 1; }
	w.info ("A test of pdfwrite", "Onyx", 0, "writetest");
	auto line = [&] (int f, FT_Face face, float size, float x, float y, const char *s, unsigned col) {
		for (const unsigned char *p = (const unsigned char *) s; *p; )
		{
			unsigned c = *p++;
			if (c >= 0xC0) { int k = c >= 0xE0 ? 2 : 1; c &= 0x3F >> k; while (k--) c = c << 6 | (*p++ & 0x3F); }
			unsigned g = FT_Get_Char_Index (face, c);
			w.glyph (f, size, x, y, g, c, col);
			FT_Load_Glyph (face, g, FT_LOAD_NO_SCALE);
			x += face->glyph->advance.x * size / face->units_per_EM;
		}
	};
	w.begin_page (595.28f, 841.89f);
	w.fill_rect (50, 50, 495, 4, 0x4992A7);
	line (B, fb, 24, 50, 100, "Onyx — a PDF written here", 0x202020);
	line (R, fr, 12, 50, 130, "Héllo wörld: invoice 12,50 € — the text can be found again.", 0x000000);
	unsigned px[64 * 64]; for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) px[y * 64 + x] = (unsigned) ((x * 4) << 24 | (y * 4) << 16 | 0x80FF);
	w.image (px, 64, 64, 50, 160, 128, 128);
	w.link_uri (50, 300, 200, 20, "https://example.org/");
	line (R, fr, 12, 50, 315, "a link to the web", 0x2060C0);
	w.link_page (50, 340, 200, 20, 1, 50);
	line (R, fr, 12, 50, 355, "a link to page 2", 0x2060C0);
	w.end_page ();
	w.begin_page (841.89f, 595.28f);		// (landscape)
	line (B, fb, 18, 50, 80, "Second page", 0x000000);
	line (R, fr, 11, 50, 110, "Bold and italic are made when the family lacks them:", 0x000000);
	{ float x = 50; for (const char *p = "made bold"; *p; p++) { unsigned g = FT_Get_Char_Index (fr, *p); w.glyph (R, 11, x, 130, g, *p, 0, true, false); FT_Load_Glyph (fr, g, FT_LOAD_NO_SCALE); x += fr->glyph->advance.x * 11.0f / fr->units_per_EM; } }
	{ float x = 150; for (const char *p = "made italic"; *p; p++) { unsigned g = FT_Get_Char_Index (fr, *p); w.glyph (R, 11, x, 130, g, *p, 0, false, true); FT_Load_Glyph (fr, g, FT_LOAD_NO_SCALE); x += fr->glyph->advance.x * 11.0f / fr->units_per_EM; } }
	w.end_page ();
	w.outline (0, "First page", 0, 90); w.outline (1, "The image", 0, 160); w.outline (0, "Second page", 1, 60);
	unsigned len; unsigned char *pdf = w.finish (&len);
	FILE *o = fopen (argv[1], "wb"); fwrite (pdf, 1, len, o); fclose (o);
	printf ("wrote %s: %u bytes (fonts: %u + %u bytes as files)\n", argv[1], len, n1, n2);
	delete[] pdf;
	return 0;
}
