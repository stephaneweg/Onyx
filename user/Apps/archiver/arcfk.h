//
// archiver/arcfk.h -- the Archiver's view of an archive, over FileKit (SD:/lib/filekit.so: the engine
// that reads and writes the formats is there since 2026-10-05; it was compiled into this program
// before). The same names the Archiver always used -- arc::Archive, its entries, Extract and
// run_extract, op_add / op_delete / op_rename / op_new_folder, Progress -- now thin calls of the
// library's C interface (filekit/filekit.h: fk_arc_*). The formats are ASKED from the library
// (arc::formats): one it learns to read is one the Archiver opens, with no change here.
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
#ifndef _archiver_arcfk_h
#define _archiver_arcfk_h

#include "filekit/arcpath.h"		// the strings, the sizes, the dates, the paths (inline: no engine)
#include "filekit/filekit.h"

namespace arc {

// ---- the formats the library knows ---------------------------------------------------------------
#define ARC_MAX_FORMATS 16
static struct fk_format g_formats[ARC_MAX_FORMATS];
static int g_nformats = -1;
static inline int formats ()
{
	if (g_nformats < 0)
	{
		g_nformats = fk_arc_formats (g_formats, ARC_MAX_FORMATS);
		if (g_nformats > ARC_MAX_FORMATS) g_nformats = ARC_MAX_FORMATS;
		if (g_nformats < 0) g_nformats = 0;
	}
	return g_nformats;
}
// The format that is written (a new archive's): the first of them. 0: none.
static inline const struct fk_format *format_written ()
{
	for (int i = 0; i < formats (); i++) if (g_formats[i].can_write) return &g_formats[i];
	return 0;
}
// A format's first extension ("zip") into out.
static inline void format_ext (const struct fk_format *f, char *out, int cap)
{
	int k = 0;
	for (const char *s = f ? f->extensions : ""; *s && *s != ' ' && k < cap - 1; s++) out[k++] = *s;
	out[k] = 0;
}

// ---- an archive ------------------------------------------------------------------------------------
struct Entry
{
	char *name;				// its path in the archive (UTF-8, '/'-separated)
	u64   size, packed;
	u32   crc, dostime;
	int   method;
	bool  dir, encrypted;
	char  methodName[28];
};

// The progress of a job: bytes done; cancelled when the user said so. Called from the job's thread.
struct Progress
{
	virtual bool step (u64 bytes) = 0;		// bytes more done -> false: stop (cancelled)
	virtual void file (const char *name) { (void) name; }
	virtual ~Progress () {}
};
// FileKit says "done of total"; the Archiver's jobs count steps.
struct Relay { Progress *pr; u64 last; };
static inline int relay_cb (void *user, unsigned long long done, unsigned long long total, const char *name)
{
	(void) total;
	Relay *r = (Relay *) user;
	if (!r->pr) return 0;
	if (name) r->pr->file (name);
	u64 d = done >= r->last ? done - r->last : 0;
	r->last = done;
	return r->pr->step (d) ? 0 : 1;
}

class Archive
{
public:
	fk_arc *h;
	char  path[300];
	char  error[200];
	Entry *e; int n;
	char *comment;
	char  password[128];

	Archive () : h (0), e (0), n (0), comment (0) { path[0] = error[0] = password[0] = 0; }
	~Archive () { clear (); if (h) fk_zip_close (h); }
	const char *format () const { return h ? fk_arc_format (h) : ""; }
	bool writable () const { return h && fk_arc_writable (h); }
	const char *methodName (const Entry &x) const { return x.methodName; }
	void fail (const char *a, const char *b = 0) { scopy (error, a, sizeof error); if (b) scat (error, b, sizeof error); }
	void failed () { scopy (error, fk_zip_error (h), sizeof error); }
	void sync () { if (h) fk_zip_password (h, password); }		// (before any work on its entries)
	void clear ()
	{
		for (int i = 0; i < n; i++) free (e[i].name);
		free (e); e = 0; n = 0;
		free (comment); comment = 0;
	}
	// The entries as the library has them now (after an open, after a change).
	void load ()
	{
		clear ();
		if (!h) return;
		scopy (path, fk_arc_path (h), sizeof path);
		int cnt = fk_zip_count (h);
		e = (Entry *) calloc ((size_t) (cnt > 0 ? cnt : 1), sizeof (Entry));
		if (!e) return;
		for (int i = 0; i < cnt; i++)
		{
			struct fk_zip_entry z;
			if (!fk_zip_entry (h, i, &z)) break;
			Entry &x = e[n++];
			x.name = sdup (z.name); x.size = z.size; x.packed = z.packed; x.crc = z.crc; x.dostime = z.dos_time;
			x.method = z.method; x.dir = z.dir != 0; x.encrypted = z.encrypted != 0;
			fk_arc_method_name (h, i, x.methodName, sizeof x.methodName);
		}
		const char *c = fk_arc_comment (h);
		comment = c && c[0] ? sdup (c) : 0;
	}
	int find (const char *name) const
	{
		for (int i = 0; i < n; i++) if (!ci_cmp (e[i].name, name)) return i;
		return -1;
	}
	u64 totalSize () const { u64 t = 0; for (int i = 0; i < n; i++) t += e[i].size; return t; }
	u64 totalPacked () const { u64 t = 0; for (int i = 0; i < n; i++) t += e[i].packed; return t; }
	// Entry i read and checked, nothing written.
	bool test (int i, Progress *pr)
	{
		sync ();
		Relay r = { pr, 0 };
		if (fk_arc_test (h, i, relay_cb, &r) != 0) { failed (); return false; }
		return true;
	}
	// A new archive put on the card with nothing in it.
	bool writeEmpty ()
	{
		if (fk_arc_write_empty (h) != 0) { failed (); return false; }
		load ();
		return true;
	}
};
typedef Archive ZipArchive;			// (the password is every archive's field here)

static inline Archive *archive_open (const char *path, char *why, int cap)
{
	fk_arc *h = fk_arc_open (path, why, cap);
	if (!h) return 0;
	Archive *a = new Archive;
	a->h = h;
	a->load ();
	return a;
}
// A new, empty archive at `path` (the format the library writes), not on the card yet.
static inline Archive *archive_new (const char *path)
{
	const struct fk_format *f = format_written ();
	fk_arc *h = fk_arc_new (path, f ? f->name : "ZIP");
	Archive *a = new Archive;
	a->h = h;
	scopy (a->path, path, sizeof a->path);
	if (!h) a->fail ("No format can be written.");
	return a;
}

// A selection as a set of flags over the entries: the names given (files or folders, a folder
// covering what is under it, implicit folders too).
static inline char *select_names (const Archive &a, const char *const *names, int nn)
{
	char *sel = (char *) calloc ((size_t) (a.n ? a.n : 1), 1);
	for (int i = 0; i < a.n; i++)
		for (int j = 0; j < nn; j++)
			if (under (a.e[i].name, names[j])) { sel[i] = 1; break; }
	return sel;
}

// ---- extracting -----------------------------------------------------------------------------------------
enum { LAY_FULL = FK_LAYOUT_FULL, LAY_FROM_CURRENT = FK_LAYOUT_FROM, LAY_FLAT = FK_LAYOUT_FLAT };
enum { OW_ASK = FK_EXISTS_ASK, OW_REPLACE = FK_EXISTS_REPLACE, OW_SKIP = FK_EXISTS_SKIP, OW_KEEP_BOTH = FK_EXISTS_KEEP_BOTH };
enum { ANS_REPLACE = FK_ANSWER_REPLACE, ANS_SKIP = FK_ANSWER_SKIP, ANS_KEEP_BOTH = FK_ANSWER_KEEP_BOTH,
       ANS_REPLACE_ALL = FK_ANSWER_REPLACE_ALL, ANS_SKIP_ALL = FK_ANSWER_SKIP_ALL, ANS_CANCEL = FK_ANSWER_CANCEL };

struct Extract
{
	Archive   *a;
	const char *sel;			// flags over a->e (0: every entry)
	char  dest[300];			// the folder to extract into (made)
	char  current[300];			// the archive's folder shown (LAY_FROM_CURRENT strips it)
	int   layout, overwrite;
	int   (*ask) (void *ctx, const char *path);	// OW_ASK: -> an ANS_*
	void *askCtx;
	int   files, skipped;			// done
	char  error[200];
	Extract () : a (0), sel (0), layout (LAY_FULL), overwrite (OW_ASK), ask (0), askCtx (0), files (0), skipped (0)
	{ dest[0] = current[0] = error[0] = 0; }
};
static inline u64 extract_total (const Extract &x) { return x.a && x.a->h ? fk_arc_extract_bytes (x.a->h, x.sel) : 0; }

struct ExtractCtx { Relay r; Extract *x; };		// (Relay first: the progress callback's user is this)
static inline int extract_ask (void *user, const char *path)
{
	Extract *x = ((ExtractCtx *) user)->x;
	return x->ask ? x->ask (x->askCtx, path) : ANS_SKIP;
}
static inline bool run_extract (Extract &x, Progress *pr)
{
	if (!x.a || !x.a->h) { scopy (x.error, "No archive.", sizeof x.error); return false; }
	x.a->sync ();
	ExtractCtx c; c.r.pr = pr; c.r.last = 0; c.x = &x;
	struct fk_extract o;
	memset (&o, 0, sizeof o);
	o.size = sizeof o; o.dest = x.dest; o.selected = x.sel; o.from = x.current;
	o.layout = x.layout; o.exists = x.overwrite; o.ask = extract_ask; o.progress = relay_cb; o.user = &c;
	int r = fk_arc_extract_with (x.a->h, &o);
	x.files = o.files; x.skipped = o.skipped;
	if (r != 0) { scopy (x.error, fk_zip_error (x.a->h), sizeof x.error); return false; }
	return true;
}

// ---- changing (the archive is rewritten; its entries are read again) ------------------------------------------
static inline bool changed (Archive &a, int r)
{
	if (r != 0) { a.failed (); return false; }
	a.load ();
	return true;
}
static inline bool op_delete (Archive &a, const char *sel, Progress *pr)
{
	a.sync (); Relay r = { pr, 0 };
	return changed (a, fk_arc_delete (a.h, sel, relay_cb, &r));
}
static inline bool op_rename (Archive &a, const char *from, const char *to, Progress *pr)
{
	a.sync (); Relay r = { pr, 0 };
	return changed (a, fk_arc_rename (a.h, from, to, relay_cb, &r));
}
static inline bool op_new_folder (Archive &a, const char *name, Progress *pr)
{
	a.sync (); Relay r = { pr, 0 };
	return changed (a, fk_arc_new_folder (a.h, name, relay_cb, &r));
}
static inline int add_conflicts (const Archive &a, const char *const *paths, int np, const char *into, bool keepFolders)
{
	return a.h ? fk_arc_add_conflicts (a.h, paths, np, into, keepFolders ? 1 : 0) : 0;
}
// What an add has to read and copy, in bytes (the progress bar's total).
static inline u64 add_bytes (const Archive &a, const char *const *paths, int np, const char *into, bool keepFolders, bool replace)
{
	return a.h ? fk_arc_add_bytes (a.h, paths, np, into, keepFolders ? 1 : 0, replace ? 1 : 0) : 0;
}
static inline bool op_add (Archive &a, const char *const *paths, int np, const char *into, bool keepFolders,
			   bool replace, int level, Progress *pr, int *added = 0)
{
	if (!a.h) { a.fail ("No format can be written."); return false; }
	a.sync (); Relay r = { pr, 0 };
	return changed (a, fk_arc_add (a.h, paths, np, into, keepFolders ? 1 : 0, replace ? 1 : 0, level, relay_cb, &r, added));
}

} // namespace arc

#endif
