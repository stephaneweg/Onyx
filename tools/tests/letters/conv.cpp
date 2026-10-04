// conv.cpp -- Letters' files on the PC: a document read (RTF, DOCX, ODT, text) and written again in a
// format by its name's extension. Linked with the desktop simulator's wtk (the image codecs) and its
// kapi (fakekapi.o).
//
//   conv IN OUT
//
#include <stdio.h>
#include <stdlib.h>
#include "Apps/letters/fileio.h"
#include "Apps/letters/docx.h"
#include "Apps/letters/odt.h"
using namespace wr;

static char *slurp (const char *p, int *n)
{
	FILE *f = fopen (p, "rb"); if (!f) return 0;
	fseek (f, 0, SEEK_END); long l = ftell (f); fseek (f, 0, SEEK_SET);
	char *b = new char[l + 1]; *n = (int) fread (b, 1, l, f); b[*n] = 0; fclose (f);
	return b;
}
static bool ext (const char *p, const char *e) { int n = slen (p), k = slen (e); return n >= k && !sicmp (p + n - k, e); }

int main (int argc, char **argv)
{
	if (argc < 3) { fprintf (stderr, "conv IN OUT\n"); return 2; }
	int n; char *b = slurp (argv[1], &n);
	if (!b) { perror (argv[1]); return 1; }
	Doc d; doc_init (d);
	bool ok = rtf_is (b, n) ? rtf_load (d, b, n) : docx_is (b, n) ? docx_load (d, b, n) : odt_is (b, n) ? odt_load (d, b, n) : (txt_load (d, b, n), true);
	if (!ok) { fprintf (stderr, "%s: not read\n", argv[1]); return 1; }
	int bn; story_p (d, SY_BODY, &bn);
	fprintf (stderr, "%s: %d paragraphs, %d tables, %d fields, %d images\n", argv[1], bn, d.ntbl, d.nfld, d.nimg);
	char *o = 0; unsigned ol = 0;
	if (ext (argv[2], ".docx")) { unsigned char *z; ok = docx_save (d, &z, &ol); o = (char *) z; }
	else if (ext (argv[2], ".odt")) { unsigned char *z; ok = odt_save (d, &z, &ol); o = (char *) z; }
	else { Out w; if (ext (argv[2], ".txt")) txt_save (d, w); else if (ext (argv[2], ".html")) html_save (d, w, "doc"); else rtf_save (d, w); o = w.b; ol = (unsigned) w.n; }
	if (!ok) { fprintf (stderr, "not written\n"); return 1; }
	FILE *f = fopen (argv[2], "wb"); fwrite (o, 1, ol, f); fclose (f);
	doc_clear (d);
	return 0;
}
