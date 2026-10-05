//
// fkcore.cpp -- FileKit (filekit.h): compression (zlib), ZIP archives on the card (the Archiver's
// engine: Apps/archiver/arc.h, zip.h, ops.h -- compiled here once for everybody) and in memory, whole
// files, trees copied / moved / removed, paths. Integer only. Built into SD:/lib/filekit.so
// (user/Makefile) and, as it is, into the PC builds that want it (the kapi's file calls only).
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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "kapi.h"
#include "Apps/archiver/ops.h"		// arc::Archive, ZipArchive, Plan, the paths' helpers (zlib.h through zip.h)
#include "filekit.h"

using namespace arc;

extern "C" void fk_free (void *p)		{ free (p); }

// ---- compression ---------------------------------------------------------------------------------

extern "C" unsigned fk_crc32 (unsigned crc, const void *d, unsigned n)		{ return (unsigned) crc32 (crc, (const Bytef *) d, n); }
extern "C" unsigned fk_adler32 (unsigned a, const void *d, unsigned n)		{ return (unsigned) adler32 (a, (const Bytef *) d, n); }

static int window_bits (int wrap)
{
	return wrap == FK_RAW ? -15 : wrap == FK_GZIP ? 15 + 16 : wrap == FK_AUTO ? 15 + 32 : 15;
}

extern "C" int fk_deflate (const void *data, unsigned n, int wrap, int level, void **out, unsigned *out_n)
{
	if (out == 0 || out_n == 0 || (data == 0 && n != 0) || wrap < FK_RAW || wrap > FK_GZIP) return -1;
	*out = 0; *out_n = 0;
	z_stream z; memset (&z, 0, sizeof z);
	if (level < 0 || level > 9) level = 6;
	if (deflateInit2 (&z, level, Z_DEFLATED, window_bits (wrap), 8, Z_DEFAULT_STRATEGY) != Z_OK) return -1;
	uLong cap = deflateBound (&z, n) + 32;
	u8 *b = (u8 *) malloc (cap);
	if (b == 0) { deflateEnd (&z); return -1; }
	z.next_in = (Bytef *) data; z.avail_in = n;
	z.next_out = b; z.avail_out = (uInt) cap;
	int r = deflate (&z, Z_FINISH);
	unsigned got = (unsigned) z.total_out;
	deflateEnd (&z);
	if (r != Z_STREAM_END) { free (b); return -1; }
	*out = b; *out_n = got;
	return 0;
}

extern "C" int fk_inflate (const void *data, unsigned n, int wrap, unsigned hint, void **out, unsigned *out_n)
{
	if (out == 0 || out_n == 0 || data == 0 || wrap < FK_RAW || wrap > FK_AUTO) return -1;
	*out = 0; *out_n = 0;
	z_stream z; memset (&z, 0, sizeof z);
	if (inflateInit2 (&z, window_bits (wrap)) != Z_OK) return -1;
	size_t cap = hint != 0 ? (size_t) hint + 1 : (size_t) n * 4 + 256;
	u8 *b = (u8 *) malloc (cap + 1);
	if (b == 0) { inflateEnd (&z); return -1; }
	z.next_in = (Bytef *) data; z.avail_in = n;
	size_t got = 0;
	int r;
	for (;;)
	{
		z.next_out = b + got; z.avail_out = (uInt) (cap - got);
		r = inflate (&z, Z_NO_FLUSH);
		got = cap - z.avail_out;
		if (r == Z_STREAM_END) break;
		if (r != Z_OK && r != Z_BUF_ERROR) break;
		if (z.avail_out == 0)				// more room
		{
			if (cap > (512u << 20)) { r = Z_MEM_ERROR; break; }
			size_t nc = cap * 2;
			u8 *nb = (u8 *) realloc (b, nc + 1);
			if (nb == 0) { r = Z_MEM_ERROR; break; }
			b = nb; cap = nc;
		}
		else if (z.avail_in == 0) break;		// (the input ended before the stream did)
	}
	inflateEnd (&z);
	if (r != Z_STREAM_END) { free (b); return -1; }
	b[got] = 0;
	*out = b; *out_n = (unsigned) got;
	return 0;
}

// ---- a ZIP archive on the card ----------------------------------------------------------------------

struct fk_zip { Archive *a; };

// The library's callback as the engine's Progress.
struct CbProgress : Progress
{
	fk_progress cb; void *user; u64 done, total; bool stopped;
	CbProgress (fk_progress c, void *u, u64 t) : cb (c), user (u), done (0), total (t), stopped (false) {}
	bool step (u64 bytes) override
	{
		done += bytes;
		if (cb != 0 && cb (user, done, total, 0) != 0) stopped = true;
		return !stopped;
	}
	void file (const char *name) override
	{
		if (cb != 0 && cb (user, done, total, name) != 0) stopped = true;
	}
};
struct MemSink : Sink
{
	u8 *b; size_t n, cap; bool ok;
	MemSink (size_t hint) : n (0), cap (hint + 1), ok (true) { b = (u8 *) malloc (cap + 1); if (b == 0) ok = false; }
	bool write (const void *p, u32 k) override
	{
		if (!ok) return false;
		if (n + k > cap)
		{
			size_t nc = (n + k) * 2;
			u8 *nb = (u8 *) realloc (b, nc + 1);
			if (nb == 0) { ok = false; return false; }
			b = nb; cap = nc;
		}
		memcpy (b + n, p, k); n += k;
		return true;
	}
};

extern "C" fk_zip *fk_zip_open (const char *path, char *err, int cap)
{
	char e0[200];
	if (err == 0 || cap <= 0) { err = e0; cap = (int) sizeof e0; }
	err[0] = 0;
	if (path == 0) { scopy (err, "No file.", cap); return 0; }
	Archive *a = archive_open (path, err, cap);
	if (a == 0) return 0;
	fk_zip *z = (fk_zip *) malloc (sizeof (fk_zip));
	if (z == 0) { delete a; scopy (err, "Not enough memory.", cap); return 0; }
	z->a = a;
	return z;
}
extern "C" void fk_zip_close (fk_zip *z)			{ if (z != 0) { delete z->a; free (z); } }
extern "C" const char *fk_zip_error (fk_zip *z)			{ return z != 0 ? z->a->error : ""; }
extern "C" void fk_zip_password (fk_zip *z, const char *pw)
{
	if (z != 0 && !strcmp (z->a->format (), "ZIP")) scopy (((ZipArchive *) z->a)->password, pw != 0 ? pw : "", 128);
}
extern "C" int fk_zip_count (fk_zip *z)				{ return z != 0 ? z->a->n : 0; }
extern "C" int fk_zip_entry (fk_zip *z, int i, struct fk_zip_entry *o)
{
	if (z == 0 || o == 0 || i < 0 || i >= z->a->n) return 0;
	const Entry &e = z->a->e[i];
	memset (o, 0, sizeof *o);
	scopy (o->name, e.name, sizeof o->name);
	o->size = e.size; o->packed = e.packed; o->crc = e.crc; o->dos_time = e.dostime;
	o->dir = e.dir ? 1 : 0; o->encrypted = e.encrypted ? 1 : 0; o->method = e.method;
	return 1;
}
extern "C" int fk_zip_find (fk_zip *z, const char *name)	{ return z != 0 && name != 0 ? z->a->find (name) : -1; }
extern "C" int fk_zip_read (fk_zip *z, int i, void **out, unsigned *out_n)
{
	if (z == 0 || out == 0 || out_n == 0 || i < 0 || i >= z->a->n || z->a->e[i].dir) return -1;
	*out = 0; *out_n = 0;
	if (z->a->e[i].size > (512u << 20)) { z->a->fail ("Too large to read into memory."); return -1; }
	MemSink m ((size_t) z->a->e[i].size);
	if (!m.ok) { z->a->fail ("Not enough memory."); return -1; }
	if (!z->a->extract (i, m, 0) || !m.ok) { free (m.b); return -1; }
	m.b[m.n] = 0;
	*out = m.b; *out_n = (unsigned) m.n;
	return 0;
}
extern "C" int fk_zip_extract (fk_zip *z, int i, const char *dest)
{
	if (z == 0 || dest == 0 || i < 0 || i >= z->a->n) return -1;
	if (z->a->e[i].dir) { mkdirs (dest); return 0; }
	char parent[300]; scopy (parent, dest, sizeof parent);
	char *sl = strrchr (parent, '/'); if (sl != 0) { *sl = 0; mkdirs (parent); }
	FileSink out;
	if (!out.open (dest)) { z->a->fail ("Cannot write ", dest); return -1; }
	bool ok = z->a->extract (i, out, 0);
	out.close ();
	if (!ok || !out.ok) { kapi_remove (dest); if (ok) z->a->fail ("Cannot write ", dest); return -1; }
	return 0;
}
extern "C" int fk_zip_extract_all (fk_zip *z, const char *prefix, const char *dest_dir, fk_progress cb, void *user)
{
	if (z == 0 || dest_dir == 0) return -1;
	Archive &a = *z->a;
	char *sel = 0;
	if (prefix != 0 && prefix[0])
	{
		sel = (char *) calloc ((size_t) (a.n ? a.n : 1), 1);
		if (sel == 0) return -1;
		for (int i = 0; i < a.n; i++) sel[i] = under (a.e[i].name, prefix) ? 1 : 0;
	}
	Extract x;
	x.a = &a; x.sel = sel; x.layout = LAY_FULL; x.overwrite = OW_REPLACE;
	scopy (x.dest, dest_dir, sizeof x.dest);
	CbProgress pr (cb, user, extract_total (x));
	bool ok = run_extract (x, &pr);
	free (sel);
	if (!ok) { if (x.error[0]) a.fail (x.error); else if (pr.stopped) a.fail ("Stopped."); return -1; }
	return x.files;
}

// ---- a new archive ----
struct fk_zipw
{
	char path[300];
	Plan plan;
	char **tmp; int ntmp;				// the buffers added, as files of the moment
	fk_zipw () : tmp (0), ntmp (0) { path[0] = 0; }
};
extern "C" fk_zipw *fk_zipw_create (const char *path)
{
	if (path == 0 || !path[0]) return 0;
	fk_zipw *w = new fk_zipw;
	scopy (w->path, path, sizeof w->path);
	return w;
}
static void zipw_walk (fk_zipw *w, const char *disk, const char *name, int depth)
{
	w->plan.addItem (0, name);
	if (depth > 24) return;
	void *d = kapi_opendir (disk);
	if (d == 0) return;
	struct kapi_dirent de;
	char **sub = 0; unsigned char *isDir = 0; int ns = 0;	// (read first: the folder's handle is not reentrant)
	while (kapi_readdir (d, &de) > 0)
	{
		if (!strcmp (de.name, ".") || !strcmp (de.name, "..")) continue;
		sub = (char **) realloc (sub, sizeof (char *) * (size_t) (ns + 1));
		isDir = (unsigned char *) realloc (isDir, (size_t) (ns + 1));
		sub[ns] = sdup (de.name); isDir[ns] = de.is_dir ? 1 : 0; ns++;
	}
	kapi_closedir (d);
	for (int i = 0; i < ns; i++)
	{
		char dp[300], an[600];
		join (dp, sizeof dp, disk, sub[i]);
		scopy (an, name, sizeof an); scat (an, "/", sizeof an); scat (an, sub[i], sizeof an);
		if (isDir[i]) zipw_walk (w, dp, an, depth + 1); else w->plan.addItem (dp, an);
		free (sub[i]);
	}
	free (sub); free (isDir);
}
extern "C" int fk_zipw_add (fk_zipw *w, const char *disk, const char *name)
{
	if (w == 0 || disk == 0) return -1;
	char p[300]; scopy (p, disk, sizeof p);
	int pl = (int) strlen (p); while (pl > 1 && p[pl - 1] == '/' && p[pl - 2] != ':') p[--pl] = 0;
	if (name == 0 || !name[0]) name = base_of (p);
	if (path_is_dir (p)) { zipw_walk (w, p, name, 0); return 0; }
	if (!path_exists (p)) return -1;
	w->plan.addItem (p, name);
	return 0;
}
extern "C" int fk_zipw_add_data (fk_zipw *w, const char *name, const void *data, unsigned n)
{
	if (w == 0 || name == 0 || (data == 0 && n != 0)) return -1;
	char t[300];
	snprintf (t, sizeof t, "%s.fk%d.tmp", w->path, w->ntmp);
	if (kapi_save_file (t, data, n) < 0) return -1;
	w->tmp = (char **) realloc (w->tmp, sizeof (char *) * (size_t) (w->ntmp + 1));
	w->tmp[w->ntmp++] = sdup (t);
	w->plan.addItem (t, name);
	return 0;
}
extern "C" void fk_zipw_level (fk_zipw *w, int level)		{ if (w != 0) w->plan.level = level < 0 ? 6 : level > 9 ? 9 : level; }
extern "C" int fk_zipw_close (fk_zipw *w, fk_progress cb, void *user, char *err, int cap)
{
	char e0[200];
	if (err == 0 || cap <= 0) { err = e0; cap = (int) sizeof e0; }
	err[0] = 0;
	if (w == 0) return -1;
	char part[300]; scopy (part, w->path, sizeof part); scat (part, ".part", sizeof part);
	u64 total = 0;
	for (int i = 0; i < w->plan.nadd; i++)
		if (w->plan.add[i].disk != 0) { void *h = kapi_open (w->plan.add[i].disk); if (h != 0) { total += kapi_fsize64 (h); kapi_close (h); } }
	Archive *a = archive_new (part);
	CbProgress pr (cb, user, total);
	bool ok = a->rewrite (w->plan, part, &pr);
	if (!ok) { scopy (err, a->error[0] ? a->error : "The archive could not be written.", cap); kapi_remove (part); }
	else
	{
		kapi_remove (w->path);
		if (kapi_rename (part, w->path) != 0) { scopy (err, "Cannot replace ", cap); scat (err, w->path, cap); ok = false; }
	}
	delete a;
	for (int i = 0; i < w->ntmp; i++) { kapi_remove (w->tmp[i]); free (w->tmp[i]); }
	free (w->tmp);
	delete w;
	return ok ? 0 : -1;
}

// ---- a ZIP archive in memory ----------------------------------------------------------------------
// (the plain format: no ZIP64, no encryption -- what the office formats and OpenRaster are)

static const u8 *zm_eocd (const u8 *z, unsigned n)
{
	if (z == 0 || n < 22) return 0;
	unsigned lo = n > 22 + 65535 ? n - 22 - 65535 : 0;
	for (unsigned p = n - 22; ; p--)
	{
		if (rd32 (z + p) == 0x06054B50u) return z + p;
		if (p == lo) break;
	}
	return 0;
}
// Central entry number i -> its record (0: none); *next: the one after.
static const u8 *zm_central (const u8 *z, unsigned n, int i, int *count)
{
	const u8 *e = zm_eocd (z, n);
	if (e == 0) return 0;
	int cnt = rd16 (e + 10);
	unsigned off = rd32 (e + 16);
	if (count != 0) *count = cnt;
	if (i < 0 || i >= cnt) return 0;
	const u8 *p = z + off;
	for (int k = 0; ; k++)
	{
		if (p + 46 > z + n || rd32 (p) != 0x02014B50u) return 0;
		if (k == i) return p;
		p += 46 + rd16 (p + 28) + rd16 (p + 30) + rd16 (p + 32);
	}
}
extern "C" int fk_zipmem_count (const void *zip, unsigned n)
{
	const u8 *e = zm_eocd ((const u8 *) zip, n);
	return e != 0 ? rd16 (e + 10) : -1;
}
extern "C" int fk_zipmem_entry (const void *zip, unsigned n, int i, char *name, int cap, unsigned *size)
{
	const u8 *z = (const u8 *) zip;
	const u8 *c = zm_central (z, n, i, 0);
	if (c == 0) return 0;
	unsigned nl = rd16 (c + 28);
	if (c + 46 + nl > z + n) return 0;
	if (name != 0 && cap > 0)
	{
		unsigned k = nl < (unsigned) cap - 1 ? nl : (unsigned) cap - 1;
		memcpy (name, c + 46, k); name[k] = 0;
	}
	if (size != 0) *size = rd32 (c + 24);
	return 1;
}
extern "C" int fk_zipmem_get (const void *zip, unsigned n, const char *name, void **out, unsigned *out_n)
{
	const u8 *z = (const u8 *) zip;
	if (out == 0 || out_n == 0 || name == 0) return -1;
	*out = 0; *out_n = 0;
	int cnt = 0;
	if (zm_central (z, n, 0, &cnt) == 0 && cnt == 0) return -1;
	unsigned want = (unsigned) strlen (name);
	for (int i = 0; i < cnt; i++)
	{
		const u8 *c = zm_central (z, n, i, 0);
		if (c == 0) return -1;
		unsigned nl = rd16 (c + 28);
		if (nl != want || c + 46 + nl > z + n || memcmp (c + 46, name, nl) != 0) continue;
		unsigned method = rd16 (c + 10), packed = rd32 (c + 20), size = rd32 (c + 24), lho = rd32 (c + 42);
		if (lho + 30 > n || rd32 (z + lho) != 0x04034B50u) return -1;
		unsigned data = lho + 30 + rd16 (z + lho + 26) + rd16 (z + lho + 28);
		if (data > n || packed > n - data) return -1;
		if (method == 0)
		{
			u8 *b = (u8 *) malloc ((size_t) packed + 1);
			if (b == 0) return -1;
			memcpy (b, z + data, packed); b[packed] = 0;
			*out = b; *out_n = packed;
			return 0;
		}
		if (method != 8) return -1;
		return fk_inflate (z + data, packed, FK_RAW, size, out, out_n);
	}
	return -1;
}

struct ZbEntry { char *name; unsigned crc, packed, size, offset; int method; };
struct fk_zipbuf { u8 *b; size_t n, cap; ZbEntry *e; int ne; bool bad; };
static bool zb_put (fk_zipbuf *z, const void *p, size_t k)
{
	if (z->n + k > z->cap)
	{
		size_t nc = (z->n + k) * 2 + 4096;
		u8 *nb = (u8 *) realloc (z->b, nc);
		if (nb == 0) { z->bad = true; return false; }
		z->b = nb; z->cap = nc;
	}
	memcpy (z->b + z->n, p, k); z->n += k;
	return true;
}
extern "C" fk_zipbuf *fk_zipbuf_new (void)
{
	fk_zipbuf *z = (fk_zipbuf *) calloc (1, sizeof (fk_zipbuf));
	return z;
}
extern "C" void fk_zipbuf_free (fk_zipbuf *z)
{
	if (z == 0) return;
	for (int i = 0; i < z->ne; i++) free (z->e[i].name);
	free (z->e); free (z->b); free (z);
}
extern "C" int fk_zipbuf_add (fk_zipbuf *z, const char *name, const void *data, unsigned n, int level)
{
	if (z == 0 || name == 0 || (data == 0 && n != 0) || z->bad) return -1;
	void *pk = 0; unsigned pn = 0; int method = 0;
	if (level != 0 && n > 0 && fk_deflate (data, n, FK_RAW, level, &pk, &pn) == 0 && pn < n) method = 8;
	else { free (pk); pk = 0; pn = n; }
	ZbEntry *ne = (ZbEntry *) realloc (z->e, sizeof (ZbEntry) * (size_t) (z->ne + 1));
	if (ne == 0) { free (pk); z->bad = true; return -1; }
	z->e = ne;
	ZbEntry &x = z->e[z->ne];
	x.name = sdup (name); x.crc = fk_crc32 (0, data, n); x.packed = pn; x.size = n; x.offset = (unsigned) z->n; x.method = method;
	unsigned nl = (unsigned) strlen (name);
	u8 h[30]; u8 *p = h;
	p = wr32 (p, 0x04034B50u); p = wr16 (p, 20); p = wr16 (p, 0x0800); p = wr16 (p, (u32) method);
	p = wr32 (p, dos_now ()); p = wr32 (p, x.crc); p = wr32 (p, pn); p = wr32 (p, n); p = wr16 (p, nl); p = wr16 (p, 0);
	bool ok = x.name != 0 && zb_put (z, h, 30) && zb_put (z, name, nl) && zb_put (z, method == 8 ? pk : data, pn);
	free (pk);
	if (!ok) { free (x.name); z->bad = true; return -1; }
	z->ne++;
	return 0;
}
extern "C" int fk_zipbuf_finish (fk_zipbuf *z, void **out, unsigned *out_n)
{
	if (z == 0 || out == 0 || out_n == 0) { fk_zipbuf_free (z); return -1; }
	*out = 0; *out_n = 0;
	unsigned cd = (unsigned) z->n, when = dos_now ();
	for (int i = 0; i < z->ne && !z->bad; i++)
	{
		const ZbEntry &x = z->e[i];
		unsigned nl = (unsigned) strlen (x.name);
		u8 h[46]; u8 *p = h;
		p = wr32 (p, 0x02014B50u); p = wr16 (p, 20); p = wr16 (p, 20); p = wr16 (p, 0x0800); p = wr16 (p, (u32) x.method);
		p = wr32 (p, when); p = wr32 (p, x.crc); p = wr32 (p, x.packed); p = wr32 (p, x.size);
		p = wr16 (p, nl); p = wr16 (p, 0); p = wr16 (p, 0); p = wr16 (p, 0); p = wr16 (p, 0); p = wr32 (p, 0); p = wr32 (p, x.offset);
		zb_put (z, h, 46); zb_put (z, x.name, nl);
	}
	u8 e[22]; u8 *p = e;
	p = wr32 (p, 0x06054B50u); p = wr16 (p, 0); p = wr16 (p, 0); p = wr16 (p, (u32) z->ne); p = wr16 (p, (u32) z->ne);
	p = wr32 (p, (u32) (z->n - cd)); p = wr32 (p, cd); p = wr16 (p, 0);
	zb_put (z, e, 22);
	if (z->bad) { fk_zipbuf_free (z); return -1; }
	*out = z->b; *out_n = (unsigned) z->n;
	z->b = 0;
	fk_zipbuf_free (z);
	return 0;
}

// ---- files ---------------------------------------------------------------------------------------

extern "C" int fk_exists (const char *path)
{
	if (path == 0 || !path[0]) return 0;
	void *d = kapi_opendir (path);
	if (d != 0) { kapi_closedir (d); return 2; }
	void *h = kapi_open (path);
	if (h != 0) { kapi_close (h); return 1; }
	return 0;
}
extern "C" long long fk_file_size (const char *path)
{
	void *h = path != 0 ? kapi_open (path) : 0;
	if (h == 0) return -1;
	long long n = (long long) kapi_fsize64 (h);
	kapi_close (h);
	return n;
}
extern "C" int fk_load (const char *path, void **out, unsigned *out_n)
{
	if (out == 0 || out_n == 0 || path == 0) return -1;
	*out = 0; *out_n = 0;
	Reader r;
	if (!r.open (path) || r.size > (1024ull << 20)) return -1;
	u8 *b = (u8 *) malloc ((size_t) r.size + 1);
	if (b == 0) return -1;
	if (r.size != 0 && !r.read (b, (u32) r.size)) { free (b); return -1; }
	b[r.size] = 0;
	*out = b; *out_n = (unsigned) r.size;
	return 0;
}
extern "C" int fk_save (const char *path, const void *data, unsigned n)
{
	if (path == 0 || (data == 0 && n != 0)) return -1;
	return kapi_save_file (path, data, n) < 0 ? -1 : 0;
}
extern "C" int fk_mkdirs (const char *path)
{
	if (path == 0 || !path[0]) return -1;
	mkdirs (path);
	return path_is_dir (path) ? 0 : -1;
}

static bool copy_file (const char *src, const char *dst, CbProgress &pr)
{
	Reader r;
	if (!r.open (src)) return false;
	FileSink out;
	if (!out.open (dst)) return false;
	static const unsigned CH = 64 * 1024;
	u8 *b = (u8 *) malloc (CH);
	bool ok = b != 0;
	pr.file (src);
	for (u64 left = r.size; ok && left != 0; )
	{
		u32 k = left > CH ? CH : (u32) left;
		ok = r.read (b, k) && out.write (b, k);
		left -= k;
		if (ok && !pr.step (k)) ok = false;
	}
	free (b);
	out.close ();
	if (!ok || !out.ok) { kapi_remove (dst); return false; }
	return true;
}
// The entries of a folder, read before anything is done to them (the handle is not reentrant).
struct DirList { char **name; unsigned char *dir; int n; };
static void dir_read (const char *path, DirList &l)
{
	l.name = 0; l.dir = 0; l.n = 0;
	void *d = kapi_opendir (path);
	if (d == 0) return;
	struct kapi_dirent de;
	while (kapi_readdir (d, &de) > 0)
	{
		if (!strcmp (de.name, ".") || !strcmp (de.name, "..")) continue;
		l.name = (char **) realloc (l.name, sizeof (char *) * (size_t) (l.n + 1));
		l.dir = (unsigned char *) realloc (l.dir, (size_t) (l.n + 1));
		l.name[l.n] = sdup (de.name); l.dir[l.n] = de.is_dir ? 1 : 0; l.n++;
	}
	kapi_closedir (d);
}
static void dir_free (DirList &l)
{
	for (int i = 0; i < l.n; i++) free (l.name[i]);
	free (l.name); free (l.dir);
}
static void tree_size (const char *path, u64 &bytes, int &files, int &folders, int depth)
{
	if (!path_is_dir (path))
	{
		void *h = kapi_open (path);
		if (h != 0) { bytes += kapi_fsize64 (h); kapi_close (h); files++; }
		return;
	}
	folders++;
	if (depth > 32) return;
	DirList l; dir_read (path, l);
	for (int i = 0; i < l.n; i++)
	{
		char p[300]; join (p, sizeof p, path, l.name[i]);
		tree_size (p, bytes, files, folders, depth + 1);
	}
	dir_free (l);
}
extern "C" long long fk_tree_size (const char *path, int *files, int *folders)
{
	u64 b = 0; int f = 0, d = 0;
	if (path == 0 || !fk_exists (path)) return -1;
	tree_size (path, b, f, d, 0);
	if (files != 0) *files = f;
	if (folders != 0) *folders = d;
	return (long long) b;
}
static bool copy_tree (const char *src, const char *dst, CbProgress &pr, int depth)
{
	if (!path_is_dir (src)) return copy_file (src, dst, pr);
	if (depth > 32) return false;
	mkdirs (dst);
	DirList l; dir_read (src, l);
	bool ok = true;
	for (int i = 0; ok && i < l.n; i++)
	{
		char a[300], b[300];
		join (a, sizeof a, src, l.name[i]); join (b, sizeof b, dst, l.name[i]);
		ok = copy_tree (a, b, pr, depth + 1);
	}
	dir_free (l);
	return ok;
}
extern "C" int fk_copy (const char *src, const char *dst, fk_progress cb, void *user)
{
	if (src == 0 || dst == 0 || !fk_exists (src) || !strcmp (src, dst)) return -1;
	// (a folder into itself would never end)
	size_t sl = strlen (src);
	if (path_is_dir (src) && !strncmp (dst, src, sl) && dst[sl] == '/') return -1;
	long long total = cb != 0 ? fk_tree_size (src, 0, 0) : 0;
	CbProgress pr (cb, user, total > 0 ? (u64) total : 0);
	char parent[300]; scopy (parent, dst, sizeof parent);
	char *s = strrchr (parent, '/'); if (s != 0 && s > parent && s[-1] != ':') { *s = 0; mkdirs (parent); }
	return copy_tree (src, dst, pr, 0) ? 0 : -1;
}
static bool remove_tree (const char *path, int depth)
{
	if (!path_is_dir (path)) return kapi_remove (path) == 0;
	if (depth > 32) return false;
	DirList l; dir_read (path, l);
	bool ok = true;
	for (int i = 0; i < l.n; i++)
	{
		char p[300]; join (p, sizeof p, path, l.name[i]);
		if (!remove_tree (p, depth + 1)) ok = false;
	}
	dir_free (l);
	return ok && kapi_remove (path) == 0;
}
extern "C" int fk_remove (const char *path)
{
	if (path == 0 || !path[0]) return -1;
	// never a volume's root ("SD:", "SD:/")
	size_t n = strlen (path);
	if (path[n - 1] == ':' || (n >= 2 && path[n - 1] == '/' && path[n - 2] == ':')) return -1;
	if (!fk_exists (path)) return -1;
	return remove_tree (path, 0) ? 0 : -1;
}
extern "C" int fk_move (const char *src, const char *dst, fk_progress cb, void *user)
{
	if (src == 0 || dst == 0 || !fk_exists (src)) return -1;
	if (kapi_rename (src, dst) == 0) return 0;		// (the same volume: at once)
	if (fk_copy (src, dst, cb, user) != 0) return -1;
	return fk_remove (src);
}

// ---- paths ---------------------------------------------------------------------------------------

extern "C" const char *fk_path_name (const char *path)		{ return path != 0 ? base_of (path) : ""; }
extern "C" void fk_path_ext (const char *path, char *out, int cap)
{
	if (out == 0 || cap <= 0) return;
	out[0] = 0;
	if (path != 0) ext_of (base_of (path), out, cap);
}
extern "C" void fk_path_folder (const char *path, char *out, int cap)
{
	if (out == 0 || cap <= 0) return;
	scopy (out, path != 0 ? path : "", cap);
	int n = (int) strlen (out);
	while (n > 1 && out[n - 1] == '/' && out[n - 2] != ':') out[--n] = 0;	// ("a/b/" is "a/b")
	char *s = strrchr (out, '/');
	if (s == 0) { char *c = strrchr (out, ':'); if (c != 0) c[1] = 0; else out[0] = 0; return; }
	if (s > out && s[-1] == ':') s[1] = 0; else *s = 0;			// ("SD:/a" -> "SD:/")
}
extern "C" void fk_path_join (char *out, int cap, const char *folder, const char *name)
{
	if (out == 0 || cap <= 0) return;
	join (out, cap, folder != 0 ? folder : "", name != 0 ? name : "");
}
extern "C" void fk_path_unique (char *path, int cap)		{ if (path != 0 && cap > 0) unique_path (path, cap); }
extern "C" void fk_human_size (unsigned long long n, char *out, int cap)	{ if (out != 0 && cap > 0) human_size (n, out, cap); }
extern "C" void fk_dos_time_str (unsigned t, char *out, int cap)	{ if (out != 0 && cap > 0) dos_str (t, out, cap); }
