/* mutest.c -- the PDF Viewer's MuPDF build checked on the PC (tools/tests/run_pdf_test.sh): opens a PDF, counts
 * its pages, renders one to a PPM, reads its outline, its text, finds a word, its links. Exit 0 when all good. */
#include <mupdf/fitz.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf ("FAIL: " __VA_ARGS__); printf ("\n"); fails++; } } while (0)

int main (int argc, char **argv)
{
	if (argc < 4) { printf ("usage: mutest file.pdf page out.ppm [word]\n"); return 2; }
	fz_context *ctx = fz_new_context (NULL, NULL, FZ_STORE_DEFAULT);
	fz_register_document_handlers (ctx);
	fz_document *doc = NULL;
	fz_try (ctx) doc = fz_open_document (ctx, argv[1]);
	fz_catch (ctx) { printf ("FAIL: open: %s\n", fz_caught_message (ctx)); return 1; }
	int n = fz_count_pages (ctx, doc), pn = atoi (argv[2]) - 1;
	printf ("pages: %d\n", n);
	CHECK (n > 0, "no pages");
	char buf[256];
	if (fz_lookup_metadata (ctx, doc, FZ_META_INFO_TITLE, buf, sizeof buf) > 0) printf ("title: %s\n", buf);
	if (fz_lookup_metadata (ctx, doc, FZ_META_FORMAT, buf, sizeof buf) > 0) printf ("format: %s\n", buf);
	fz_outline *ol = fz_load_outline (ctx, doc);
	int nol = 0;
	for (fz_outline *o = ol; o; o = o->next) { if (nol < 4) printf ("outline: %s -> %d\n", o->title, fz_page_number_from_location (ctx, doc, o->page) + 1); nol++; }
	printf ("outline entries (top level): %d\n", nol);
	fz_drop_outline (ctx, ol);

	fz_page *page = fz_load_page (ctx, doc, pn);
	fz_rect b = fz_bound_page (ctx, page);
	printf ("page %d: %.1f x %.1f pt\n", pn + 1, b.x1 - b.x0, b.y1 - b.y0);
	fz_matrix m = fz_scale (1.5f, 1.5f);
	fz_pixmap *pix = fz_new_pixmap_from_page (ctx, page, m, fz_device_rgb (ctx), 0);
	fz_save_pixmap_as_pnm (ctx, pix, argv[3]);
	printf ("rendered %d x %d\n", pix->w, pix->h);
	/* not all white */
	int dark = 0;
	for (int i = 0; i < pix->w * pix->h * pix->n; i++) if (pix->samples[i] < 128) dark++;
	CHECK (dark > 1000, "the page is blank");
	fz_drop_pixmap (ctx, pix);

	fz_stext_page *st = fz_new_stext_page_from_page (ctx, page, NULL);
	fz_buffer *tb = fz_new_buffer_from_stext_page (ctx, st);
	const char *txt = fz_string_from_buffer (ctx, tb);
	printf ("text: %.60s...\n", txt);
	CHECK (strlen (txt) > 50, "no text");
	if (argc > 4) {
		fz_quad q[64]; int hits = fz_search_stext_page (ctx, st, argv[4], NULL, q, 64);
		printf ("hits of \"%s\": %d\n", argv[4], hits);
		CHECK (hits > 0, "no hit");
	}
	fz_drop_buffer (ctx, tb); fz_drop_stext_page (ctx, st);
	fz_link *ln = fz_load_links (ctx, page); int nl = 0;
	for (fz_link *l = ln; l; l = l->next) nl++;
	printf ("links: %d\n", nl);
	fz_drop_link (ctx, ln);
	fz_drop_page (ctx, page);
	fz_drop_document (ctx, doc);
	fz_drop_context (ctx);
	printf (fails ? "mutest: %d failures\n" : "mutest: all good\n", fails);
	return fails != 0;
}
