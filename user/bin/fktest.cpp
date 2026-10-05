//
// fktest -- FileKit's self-test (SD:/lib/filekit.so, filekit/filekit.h): compression, a ZIP archive
// made and read on the card and in memory, a tree copied, moved and removed, the paths. Everything
// it writes is under SD:/tmp/fktest, removed at the end.
//   usage: fktest          -> "fktest: N passed, 0 failed", status 0
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
#include "kapi.h"
#include "applib.h"
#include "../onyxpp.hpp"		// (operator new / delete: the library's binding)
#include "../filekit/filekit.h"

static int g_pass, g_fail;
static void check (const char *what, bool ok)
{
	ax_puts (ok ? "PASS " : "FAIL "); ax_putln (what);
	if (ok) g_pass++; else g_fail++;
}
static bool same (const void *a, const void *b, unsigned n)
{
	const unsigned char *x = (const unsigned char *) a, *y = (const unsigned char *) b;
	for (unsigned i = 0; i < n; i++) if (x[i] != y[i]) return false;
	return true;
}
static bool streq (const char *a, const char *b) { return ax_streq (a, b) != 0; }

#define D "SD:/tmp/fktest"
static unsigned char g_data[40000];

int main (void)
{
	for (unsigned i = 0; i < sizeof g_data; i++) g_data[i] = (unsigned char) ("the quick brown fox "[i % 20] + (i / 5000));
	void *p = 0, *q = 0; unsigned pn = 0, qn = 0;

	// compression: the three wrappers there and back
	check ("crc32 of \"123456789\" is CBF43926", fk_crc32 (0, "123456789", 9) == 0xCBF43926u);
	check ("adler32 of \"Wikipedia\" is 11E60398", fk_adler32 (1, "Wikipedia", 9) == 0x11E60398u);
	for (int w = FK_RAW; w <= FK_GZIP; w++)
	{
		bool ok = fk_deflate (g_data, sizeof g_data, w, 6, &p, &pn) == 0 && pn < sizeof g_data / 4
		       && fk_inflate (p, pn, w, 0, &q, &qn) == 0 && qn == sizeof g_data && same (q, g_data, qn);
		check (w == FK_RAW ? "deflate / inflate, raw" : w == FK_ZLIB ? "deflate / inflate, zlib" : "deflate / inflate, gzip", ok);
		if (w == FK_GZIP)
		{
			void *r = 0; unsigned rn = 0;
			check ("inflate tells gzip by itself", fk_inflate (p, pn, FK_AUTO, 0, &r, &rn) == 0 && rn == sizeof g_data);
			fk_free (r);
		}
		fk_free (p); fk_free (q); p = q = 0;
	}
	check ("a damaged stream is refused", fk_inflate ("not a stream at all", 19, FK_ZLIB, 0, &p, &pn) != 0);

	// files and trees
	fk_remove (D);
	check ("folders made", fk_mkdirs (D "/a/b") == 0 && fk_exists (D "/a/b") == 2);
	check ("a file saved, measured", fk_save (D "/a/one.txt", g_data, 12345) == 0 && fk_file_size (D "/a/one.txt") == 12345);
	fk_save (D "/a/b/two.bin", g_data + 100, 30000);
	check ("a file loaded", fk_load (D "/a/one.txt", &p, &pn) == 0 && pn == 12345 && same (p, g_data, pn));
	fk_free (p); p = 0;
	int files = 0, folders = 0;
	check ("a tree measured", fk_tree_size (D "/a", &files, &folders) == 42345 && files == 2 && folders == 2);
	check ("a tree copied", fk_copy (D "/a", D "/c", 0, 0) == 0 && fk_file_size (D "/c/b/two.bin") == 30000);
	check ("a folder is not copied into itself", fk_copy (D "/a", D "/a/b/x", 0, 0) != 0);
	check ("a tree moved", fk_move (D "/c", D "/m", 0, 0) == 0 && fk_exists (D "/c") == 0 && fk_file_size (D "/m/one.txt") == 12345);

	// a ZIP archive on the card
	char err[200];
	fk_zipw *w = fk_zipw_create (D "/t.zip");
	bool ok = w != 0 && fk_zipw_add (w, D "/a", "tree") == 0 && fk_zipw_add_data (w, "hello.txt", "hello, zip", 10) == 0;
	ok = ok && fk_zipw_close (w, 0, 0, err, sizeof err) == 0;
	check ("an archive written", ok && fk_file_size (D "/t.zip") > 100);
	fk_zip *z = fk_zip_open (D "/t.zip", err, sizeof err);
	check ("the archive opened", z != 0);
	if (z != 0)
	{
		int i = fk_zip_find (z, "tree/b/two.bin"), h = fk_zip_find (z, "HELLO.TXT");
		struct fk_zip_entry e;
		check ("its entries found", i >= 0 && h >= 0 && fk_zip_entry (z, i, &e) && e.size == 30000 && !e.dir && e.packed < e.size);
		check ("an entry read to memory", h >= 0 && fk_zip_read (z, h, &p, &pn) == 0 && pn == 10 && same (p, "hello, zip", 10));
		fk_free (p); p = 0;
		check ("an entry extracted", i >= 0 && fk_zip_extract (z, i, D "/x/two.bin") == 0 && fk_file_size (D "/x/two.bin") == 30000);
		check ("everything extracted", fk_zip_extract_all (z, "tree", D "/all", 0, 0) == 2 && fk_load (D "/all/tree/one.txt", &p, &pn) == 0
		       && pn == 12345 && same (p, g_data, pn));
		fk_free (p); p = 0;
		fk_zip_close (z);
	}
	check ("a file that is not an archive is refused", fk_zip_open (D "/a/one.txt", err, sizeof err) == 0 || fk_zip_count (fk_zip_open (D "/a/one.txt", err, sizeof err)) == 0);

	// the formats, asked; tar and gzip read
	static struct fk_format fmt[8];
	int nf = fk_arc_formats (fmt, 8);
	bool zipw = false, tarr = false;
	for (int i = 0; i < nf && i < 8; i++)
	{
		if (streq (fmt[i].name, "ZIP") && fmt[i].can_read && fmt[i].can_write) zipw = true;
		if (streq (fmt[i].name, "TAR") && fmt[i].can_read && !fmt[i].can_write) tarr = true;
	}
	check ("the formats are told: ZIP read and written, TAR read", nf >= 4 && zipw && tarr);
	check ("names told by their extension", fk_arc_is_name ("a/b/Photos.ZIP") == 1 && fk_arc_is_name ("x.tar.gz") == 1 && fk_arc_is_name ("x.txt") == 0);
	{
		// a tar of one folder and one file, made by hand (a header's sum counts its own field as spaces)
		static unsigned char tar[512 * 6];
		for (unsigned i = 0; i < sizeof tar; i++) tar[i] = 0;
		for (int k = 0; k < 2; k++)
		{
			unsigned char *h = tar + (k == 0 ? 0 : 512);
			const char *nm = k == 0 ? "docs/" : "docs/hello.txt";
			for (int i = 0; nm[i]; i++) h[i] = (unsigned char) nm[i];
			const char *mode = "0000644"; for (int i = 0; i < 7; i++) h[100 + i] = (unsigned char) mode[i];
			const char *size = k == 0 ? "00000000000" : "00000000014"; for (int i = 0; i < 11; i++) h[124 + i] = (unsigned char) size[i];	// 12 bytes
			const char *mt = "14000000000"; for (int i = 0; i < 11; i++) h[136 + i] = (unsigned char) mt[i];
			h[156] = k == 0 ? '5' : '0';
			const char *us = "ustar"; for (int i = 0; i < 5; i++) h[257 + i] = (unsigned char) us[i];
			h[263] = '0'; h[264] = '0';
			unsigned sum = 0; for (int i = 0; i < 512; i++) sum += (i >= 148 && i < 156) ? ' ' : h[i];
			for (int i = 5; i >= 0; i--) { h[148 + i] = (unsigned char) ('0' + (sum & 7)); sum >>= 3; }
			h[154] = 0; h[155] = ' ';
		}
		const char *body = "hello, tar!\n"; for (int i = 0; i < 12; i++) tar[1024 + i] = (unsigned char) body[i];
		fk_save (D "/t.tar", tar, sizeof tar);
		char f1[16] = "", why[120] = "";
		check ("a tar is told from its bytes", fk_arc_probe (D "/t.tar", f1, sizeof f1, why, sizeof why) == 1 && streq (f1, "TAR"));
		fk_arc *t = fk_arc_open (D "/t.tar", err, sizeof err);
		int hi = t != 0 ? fk_zip_find (t, "docs/hello.txt") : -1;
		check ("a tar opened: its entries, not writable", t != 0 && fk_zip_count (t) == 2 && hi >= 0 && fk_arc_writable (t) == 0);
		check ("a tar's entry read", hi >= 0 && fk_zip_read (t, hi, &p, &pn) == 0 && pn == 12 && same (p, body, 12));
		fk_free (p); p = 0;
		check ("a tar is not changed", t != 0 && fk_arc_new_folder (t, "x", 0, 0) != 0);
		fk_zip_close (t);
		// ... the same inside a gzip
		ok = fk_deflate (tar, sizeof tar, FK_GZIP, 6, &q, &qn) == 0 && fk_save (D "/t.tgz", q, qn) == 0;
		fk_free (q); q = 0;
		t = ok ? fk_arc_open (D "/t.tgz", err, sizeof err) : 0;
		hi = t != 0 ? fk_zip_find (t, "docs/hello.txt") : -1;
		check ("a tar.gz opened and read", t != 0 && streq (fk_arc_format (t), "TAR.GZ") && hi >= 0 && fk_zip_read (t, hi, &p, &pn) == 0 && pn == 12 && same (p, body, 12));
		fk_free (p); p = 0;
		struct fk_extract x;
		for (unsigned i = 0; i < sizeof x; i++) ((char *) &x)[i] = 0;
		x.size = sizeof x; x.dest = D "/untar"; x.layout = FK_LAYOUT_FLAT; x.exists = FK_EXISTS_REPLACE;
		check ("... extracted flat", t != 0 && fk_arc_extract_with (t, &x) == 0 && x.files == 1 && fk_file_size (D "/untar/hello.txt") == 12);
		fk_zip_close (t);
		// a plain file gzipped: one entry, its name without ".gz"
		ok = fk_deflate (g_data, 5000, FK_GZIP, 6, &q, &qn) == 0 && fk_save (D "/notes.txt.gz", q, qn) == 0;
		fk_free (q); q = 0;
		t = ok ? fk_arc_open (D "/notes.txt.gz", err, sizeof err) : 0;
		struct fk_zip_entry ge;
		check ("a .gz opened as one entry", t != 0 && streq (fk_arc_format (t), "GZIP") && fk_zip_count (t) == 1 && fk_zip_entry (t, 0, &ge)
		       && streq (ge.name, "notes.txt") && ge.size == 5000 && fk_zip_read (t, 0, &p, &pn) == 0 && pn == 5000 && same (p, g_data, 5000));
		fk_free (p); p = 0;
		fk_zip_close (t);
	}
	// an archive changed through the generic calls
	{
		fk_arc *a = fk_arc_open (D "/t.zip", err, sizeof err);
		const char *add[1] = { D "/m" };
		int added = 0;
		check ("files added to an archive", a != 0 && fk_arc_writable (a) == 1 && fk_arc_add (a, add, 1, "extra", 1, 1, 6, 0, 0, &added) == 0
		       && added >= 3 && fk_zip_find (a, "extra/m/one.txt") >= 0);
		check ("an entry renamed", a != 0 && fk_arc_rename (a, "hello.txt", "bonjour.txt", 0, 0) == 0 && fk_zip_find (a, "bonjour.txt") >= 0 && fk_zip_find (a, "hello.txt") < 0);
		int cnt = a != 0 ? fk_zip_count (a) : 0;
		char *sel = (char *) "";
		static char flags[64];
		for (int i = 0; i < 64; i++) flags[i] = 0;
		int bi = a != 0 ? fk_zip_find (a, "bonjour.txt") : -1;
		if (bi >= 0 && bi < 64) flags[bi] = 1;
		sel = flags;
		check ("an entry deleted", a != 0 && cnt <= 64 && fk_arc_delete (a, sel, 0, 0) == 0 && fk_zip_count (a) == cnt - 1 && fk_zip_find (a, "bonjour.txt") < 0);
		char mn[28];
		int ti = a != 0 ? fk_zip_find (a, "tree/b/two.bin") : -1;
		check ("an entry tested, its method named", ti >= 0 && fk_arc_test (a, ti, 0, 0) == 0 && fk_arc_method_name (a, ti, mn, sizeof mn) && streq (mn, "Deflate"));
		fk_zip_close (a);
	}

	// a ZIP archive in memory
	fk_zipbuf *b = fk_zipbuf_new ();
	ok = b != 0 && fk_zipbuf_add (b, "mimetype", "application/x-test", 18, 0) == 0 && fk_zipbuf_add (b, "content.xml", g_data, 20000, 6) == 0;
	ok = ok && fk_zipbuf_finish (b, &p, &pn) == 0;
	check ("an archive built in memory", ok && pn > 100 && pn < 10000);
	char name[64]; unsigned size = 0;
	check ("its entries listed", ok && fk_zipmem_count (p, pn) == 2 && fk_zipmem_entry (p, pn, 1, name, sizeof name, &size) && streq (name, "content.xml") && size == 20000);
	check ("an entry of it inflated", ok && fk_zipmem_get (p, pn, "content.xml", &q, &qn) == 0 && qn == 20000 && same (q, g_data, qn));
	fk_free (q); q = 0;
	check ("its first entry is stored as it is", ok && fk_zipmem_get (p, pn, "mimetype", &q, &qn) == 0 && qn == 18 && same ((char *) p + 30 + 8, "application/x-test", 18));
	fk_free (q); q = 0;
	// ... and the card's reader reads what the memory's writer made
	if (ok) fk_save (D "/mem.zip", p, pn);
	z = fk_zip_open (D "/mem.zip", err, sizeof err);
	check ("the card's reader opens it", z != 0 && fk_zip_count (z) == 2 && fk_zip_read (z, 1, &q, &qn) == 0 && qn == 20000 && same (q, g_data, qn));
	fk_free (q); fk_zip_close (z);
	fk_free (p);

	// paths
	char t[300];
	check ("a path's name", streq (fk_path_name ("SD:/a/b/photo.JPG"), "photo.JPG") && streq (fk_path_name ("SD:x"), "x"));
	fk_path_ext ("SD:/a/b/photo.JPG", t, sizeof t);
	bool pe = streq (t, "jpg");
	fk_path_ext ("SD:/a.b/readme", t, sizeof t);
	check ("a path's extension", pe && t[0] == 0);
	fk_path_folder ("SD:/a/b/photo.JPG", t, sizeof t);
	bool pf = streq (t, "SD:/a/b");
	fk_path_folder ("SD:/a", t, sizeof t);
	check ("a path's folder", pf && streq (t, "SD:/"));
	fk_path_join (t, sizeof t, "SD:/a", "b.txt");
	bool pj = streq (t, "SD:/a/b.txt");
	fk_path_join (t, sizeof t, "SD:/", "b.txt");
	check ("two parts joined", pj && streq (t, "SD:/b.txt"));
	fk_path_join (t, sizeof t, D, "a/one.txt");
	fk_path_unique (t, sizeof t);
	check ("a name not taken", streq (t, D "/a/one (2).txt"));
	fk_human_size (12697, t, sizeof t);
	check ("a size for people", streq (t, "12.3 KB"));

	check ("the tree removed", fk_remove (D) == 0 && fk_exists (D) == 0);
	check ("a volume's root is never removed", fk_remove ("SD:/") != 0 && fk_remove ("SD:") != 0);

	char n1[12], n2[12];
	ax_itoa (g_pass, n1); ax_itoa (g_fail, n2);
	ax_puts ("fktest: "); ax_puts (n1); ax_puts (" passed, "); ax_puts (n2); ax_putln (" failed");
	return g_fail ? 1 : 0;
}
