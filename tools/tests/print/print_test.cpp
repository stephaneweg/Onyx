//
// print_test.cpp -- the print system's core on the PC (tools/tests/run_print_test.sh): a job recorded
// (user/Kits/printerkit/job.h), replayed as a PDF (printerkit/pdfsink.h) and as a 300 dpi raster page (printerkit/raster.h),
// the PWG Raster stream written, read back and compared with the page, pixel for pixel.
//
//   print_test <a TrueType font> <output folder>
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define IMGLOAD_IMPLEMENTATION
#include "img/imgload.hpp"
#include "printerkit/job.h"
#include "printerkit/pdfsink.h"
#include "printerkit/raster.h"

static int g_fail;
#define CHECK(c, what) do { if (!(c)) { printf ("FAIL: %s\n", what); g_fail++; } } while (0)

static unsigned char *g_ttf; static unsigned g_ttfLen;
static FT_Library g_lib; static FT_Face g_face;

static void text (pjob::Writer &w, int f, float size, float x, float y, const char *s, unsigned rgb, unsigned style)
{
	FT_Set_Char_Size (g_face, 0, (FT_F26Dot6) (size * 64), 72, 72);
	for (; *s; s++)
	{
		unsigned gi = FT_Get_Char_Index (g_face, (unsigned char) *s);
		FT_Load_Glyph (g_face, gi, FT_LOAD_NO_HINTING);
		w.glyph (f, size, x, y, gi, (unsigned char) *s, rgb, style);
		x += g_face->glyph->advance.x / 64.0f;
	}
}

static void record (const char *path, bool landscape)
{
	pjob::Writer w;
	CHECK (w.open (path), "job created");
	int f = w.font (g_ttf, g_ttfLen);
	CHECK (f == 0 && w.font (g_ttf, g_ttfLen) == 0, "one font for the same bytes");
	float pw = landscape ? 841.89f : 595.28f, ph = landscape ? 595.28f : 841.89f;
	for (int page = 0; page < 2; page++)
	{
		w.begin_page (pw, ph);
		w.rect (36, 36, pw - 72, 2, 0x204080);
		text (w, f, 24, 36, 80, page ? "Second page" : "Onyx print test", 0x000000, 0);
		text (w, f, 12, 36, 110, "The quick brown fox jumps over the lazy dog 0123456789", 0x202020, 0);
		text (w, f, 12, 36, 130, "bold and slanted, made from the regular face", 0xA02020, pjob::ST_BOLD | pjob::ST_ITALIC);
		// an image: a gradient with a transparent corner
		unsigned *px = new unsigned[64 * 48];
		for (int y = 0; y < 48; y++) for (int x = 0; x < 64; x++)
			px[y * 64 + x] = (x + y < 20 ? 0u : 0xFF000000u) | (unsigned) (x * 4) << 16 | (unsigned) (y * 5) << 8 | 0x80;
		unsigned n = 0;
		unsigned char *z = pngsave::deflate ((unsigned char *) px, 64 * 48 * 4, true, &n);
		w.image (36, 160, 192, 144, 64, 48, pjob::IMG_DEFLATE, z, n);
		delete[] z;
		for (int i = 0; i < 64 * 48; i++) px[i] |= 0xFF000000u;
		z = pngsave::jpeg_encode (px, 64, 48, 90, &n);
		w.image (260, 160, 192, 144, 64, 48, pjob::IMG_JPEG, z, n);
		delete[] z; delete[] px;
		// paths: a filled triangle, a stroked box, a curve
		pjob::PathPt tri[4] = { { pjob::V_MOVE, 60, 420 }, { pjob::V_LINE, 160, 420 }, { pjob::V_LINE, 110, 340 }, { pjob::V_CLOSE, 0, 0 } };
		w.path (tri, 4, 0x20A040, 0);
		pjob::PathPt box[5] = { { pjob::V_MOVE, 200, 340 }, { pjob::V_LINE, 300, 340 }, { pjob::V_LINE, 300, 420 }, { pjob::V_LINE, 200, 420 }, { pjob::V_CLOSE, 0, 0 } };
		w.path (box, 5, 0x000000, 3);
		pjob::PathPt cur[4] = { { pjob::V_MOVE, 340, 420 }, { pjob::V_CURVE, 360, 300 }, { pjob::V_CURVE, 440, 300 }, { pjob::V_CURVE, 460, 420 } };
		w.path (cur, 4, 0x8020C0, 1.5f);
		w.end_page ();
	}
	CHECK (w.pages () == 2 && w.close (), "job closed with two pages");
}

struct Out : praster::Raster
{
	FILE *f; const char *dir; int n; unsigned char *keep;
	Out (float w, float h, int dpi, bool gray, FILE *o, const char *d) : Raster (w, h, dpi, gray), f (o), dir (d), n (0), keep (0) {}
	static bool wr (void *c, const void *b, unsigned k) { return fwrite (b, 1, k, (FILE *) c) == k; }
	bool page_ready ()
	{
		if (n == 0)
		{	// the first page kept (the PWG stream is read back against it) and saved as a picture to look at
			int w = width (), h = height (), bpp = bytes_per_pixel ();
			keep = new unsigned char[(size_t) w * h * bpp];
			unsigned *px = new unsigned[(size_t) w * h];
			for (int y = 0; y < h; y++)
			{
				unsigned char *r = keep + (size_t) y * w * bpp;
				row (y, r);
				for (int x = 0; x < w; x++) px[(size_t) y * w + x] = 0xFF000000u | (bpp == 1 ? r[x] * 0x010101u : (unsigned) r[x * 3] << 16 | r[x * 3 + 1] << 8 | r[x * 3 + 2]);
			}
			char p[512]; snprintf (p, sizeof p, "%s/page-%s-%s.png", dir, tag, gray () ? "gray" : "color");
			unsigned len = 0; unsigned char *png = pngsave::png_encode (px, w, h, false, &len);
			FILE *o = fopen (p, "wb"); fwrite (png, 1, len, o); fclose (o);
			delete[] png; delete[] px;
		}
		n++;
		return praster::pwg_page (*this, "iso_a4_210x297mm", 4, 2, wr, f);
	}
	const char *tag;
};

// the PWG stream read back: its first page's rows
static bool pwg_read (const char *path, int w, int h, int bpp, const unsigned char *want)
{
	FILE *f = fopen (path, "rb");
	unsigned char hd[1800];
	if (fread (hd, 1, 4, f) != 4 || memcmp (hd, "RaS2", 4)) return false;
	if (fread (hd, 1, 1796, f) != 1796 || memcmp (hd, "PwgRaster", 10)) return false;
	unsigned W = (unsigned) hd[372] << 24 | hd[373] << 16 | hd[374] << 8 | hd[375], H = (unsigned) hd[376] << 24 | hd[377] << 16 | hd[378] << 8 | hd[379];
	if ((int) W != w || (int) H != h || hd[391] != bpp * 8) return false;
	unsigned char *line = new unsigned char[(size_t) w * bpp];
	bool ok = true;
	for (int y = 0; y < h && ok; )
	{
		int rep = fgetc (f) + 1;
		for (int x = 0; x < w && ok; )
		{
			int c = fgetc (f);
			if (c < 0) { ok = false; break; }
			if (c < 128) { unsigned char p[4]; if (fread (p, 1, bpp, f) != (size_t) bpp) ok = false; for (int k = 0; k <= c && x < w; k++, x++) memcpy (line + (size_t) x * bpp, p, bpp); }
			else { int k = 257 - c; if (x + k > w || fread (line + (size_t) x * bpp, 1, (size_t) k * bpp, f) != (size_t) k * bpp) ok = false; x += k; }
		}
		for (int k = 0; k < rep && y < h && ok; k++, y++) if (memcmp (line, want + (size_t) y * w * bpp, (size_t) w * bpp)) ok = false;
	}
	delete[] line; fclose (f);
	return ok;
}

int main (int argc, char **argv)
{
	if (argc < 3) return 2;
	FILE *f = fopen (argv[1], "rb"); if (!f) { printf ("no font %s\n", argv[1]); return 2; }
	fseek (f, 0, SEEK_END); g_ttfLen = (unsigned) ftell (f); fseek (f, 0, SEEK_SET);
	g_ttf = new unsigned char[g_ttfLen]; if (fread (g_ttf, 1, g_ttfLen, f) != g_ttfLen) return 2;
	fclose (f);
	FT_Init_FreeType (&g_lib); FT_New_Memory_Face (g_lib, g_ttf, g_ttfLen, 0, &g_face);
	char job[512], p[512];

	for (int land = 0; land < 2; land++)
	{
		snprintf (job, sizeof job, "%s/test%d.opj", argv[2], land);
		record (job, land != 0);
		// as a PDF
		{
			pjob::Reader r; pjob::PdfSink s;
			CHECK (r.open (job), "job opened");
			int n = 0, k; while ((k = r.page (s)) == 1) n++;
			CHECK (k == 0 && n == 2, "two pages replayed into the PDF");
			unsigned len = 0; unsigned char *pdf = s.w.finish (&len);
			CHECK (pdf && len > 2000 && !memcmp (pdf, "%PDF-1.7", 8), "a PDF");
			snprintf (p, sizeof p, "%s/test%d.pdf", argv[2], land);
			FILE *o = fopen (p, "wb"); fwrite (pdf, 1, len, o); fclose (o); delete[] pdf;
		}
		// as a raster, colour then grey
		for (int gray = 0; gray < 2; gray++)
		{
			snprintf (p, sizeof p, "%s/test%d-%d.pwg", argv[2], land, gray);
			FILE *o = fopen (p, "wb");
			CHECK (praster::pwg_begin (Out::wr, o), "PWG begun");
			Out s (595.28f, 841.89f, 300, gray != 0, o, argv[2]);
			s.tag = land ? "landscape" : "portrait";
			pjob::Reader r; r.open (job);
			int n = 0; while (r.page (s) == 1) n++;
			fclose (o);
			CHECK (n == 2 && s.n == 2 && !s.failed, "two raster pages");
			CHECK (s.width () == 2480 && s.height () == 3508, "A4 at 300 dpi: 2480 x 3508");
			CHECK (pwg_read (p, s.width (), s.height (), s.bytes_per_pixel (), s.keep), "the PWG stream gives the page back");
			// ink where it must be, paper elsewhere
			int w = s.width (), bpp = s.bytes_per_pixel (); long dark = 0;
			for (long i = 0; i < (long) w * s.height () * bpp; i++) if (s.keep[i] < 128) dark++;
			CHECK (dark > 20000 && dark < (long) w * s.height () * bpp / 4, "ink on the page, and not everywhere");
			FILE *q = fopen (p, "rb"); fseek (q, 0, SEEK_END); long sz = ftell (q); fclose (q);
			printf ("%s %s: %ld bytes for 2 pages\n", land ? "landscape" : "portrait", gray ? "grey" : "colour", sz);
			CHECK (sz < 6000000, "the rows are packed");
		}
	}
	if (g_fail) { printf ("print: %d FAILED\n", g_fail); return 1; }
	printf ("print: all good\n");
	return 0;
}
