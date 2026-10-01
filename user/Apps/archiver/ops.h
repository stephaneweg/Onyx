//
// archiver/ops.h -- what the Archiver does with an archive, whatever its format: open one (the
// format from its first bytes), extract entries into a folder (the archive's folders kept, from
// the current folder down, or flat; what to do when a file exists), and change it -- add files and
// folders of the disk into one of its folders, delete, rename, make a folder -- by rewriting it into
// a new file next to it, swapped in at the end (the old one stays whole if anything fails).
// Plain C++ (no wtk): the UI runs these in a thread; the tests run them on the PC.
//
#ifndef _archiver_ops_h
#define _archiver_ops_h

#include "arc.h"
#include "zip.h"

namespace arc {

// ---- paths --------------------------------------------------------------------------------------------
static inline void join (char *out, int cap, const char *a, const char *b)
{
	scopy (out, a, cap);
	int n = (int) strlen (out);
	if (n && out[n - 1] != '/' && out[n - 1] != ':' && b[0]) scat (out, "/", cap);
	scat (out, b, cap);
}
static inline bool path_exists (const char *p) { void *h = kapi_open (p); if (h) { kapi_close (h); return true; } void *d = kapi_opendir (p); if (d) { kapi_closedir (d); return true; } return false; }
static inline bool path_is_dir (const char *p) { void *d = kapi_opendir (p); if (d) { kapi_closedir (d); return true; } return false; }
// Every folder of `path` made (kapi_mkdir makes one level; -1 when it exists).
static inline void mkdirs (const char *path)
{
	char b[300]; scopy (b, path, sizeof b);
	int start = 0;
	for (int i = 0; b[i]; i++) if (b[i] == ':') { start = i + 1; break; }
	while (b[start] == '/') start++;
	for (int i = start; ; i++)
	{
		if (b[i] == '/' || b[i] == 0)
		{
			char c = b[i]; b[i] = 0;
			if (b[start]) kapi_mkdir (b);
			b[i] = c;
			if (!c) break;
		}
	}
}
// An archive name made a safe name on the card: no "..", no absolute path, no drive, the characters
// FAT refuses replaced by '_', no trailing dots or spaces in a part.
static inline void safe_rel (const char *name, char *out, int cap)
{
	int k = 0;
	const char *s = name;
	if (s[0] && s[1] == ':') s += 2;
	while (*s && k < cap - 1)
	{
		while (*s == '/') s++;
		const char *e = s; while (*e && *e != '/') e++;
		int len = (int) (e - s);
		bool dots = (len == 1 && s[0] == '.') || (len == 2 && s[0] == '.' && s[1] == '.');
		if (len && !dots)
		{
			if (k && k < cap - 1) out[k++] = '/';
			int st = k;
			for (int i = 0; i < len && k < cap - 1; i++)
			{
				unsigned char c = (unsigned char) s[i];
				out[k++] = (c < 32 || strchr ("<>:\"|?*\\", c)) ? '_' : (char) c;
			}
			while (k > st && (out[k - 1] == '.' || out[k - 1] == ' ')) k--;
			if (k == st) out[k++] = '_';
		}
		s = e;
	}
	out[k] = 0;
}
// "name.txt" -> "name (2).txt" ... the first that does not exist
static inline void unique_path (char *path, int cap)
{
	if (!path_exists (path)) return;
	char base[300], ext[40] = "";
	scopy (base, path, sizeof base);
	char *dot = strrchr (base, '.'), *sl = strrchr (base, '/');
	if (dot && (!sl || dot > sl) && dot != base) { scopy (ext, dot, sizeof ext); *dot = 0; }
	for (int i = 2; i < 1000; i++)
	{
		char num[12]; u64_str ((u64) i, num, sizeof num);
		scopy (path, base, cap); scat (path, " (", cap); scat (path, num, cap); scat (path, ")", cap); scat (path, ext, cap);
		if (!path_exists (path)) return;
	}
}

// ---- opening ---------------------------------------------------------------------------------------------
// The format from the first bytes (then the name): an Archive to open, or 0 (`why` says it).
static inline Archive *archive_for (const char *path, char *why, int cap)
{
	u8 m[264]; memset (m, 0, sizeof m);
	Reader r;
	if (!r.open (path)) { scopy (why, "Cannot open the file.", cap); return 0; }
	u32 k = r.size < sizeof m ? (u32) r.size : (u32) sizeof m;
	if (k && !r.read (m, k)) { scopy (why, "Cannot read the file.", cap); return 0; }
	r.close ();
	char x[12]; ext_of (path, x, sizeof x);
	if ((m[0] == 'P' && m[1] == 'K') || !strcmp (x, "zip") || !strcmp (x, "jar") || !strcmp (x, "docx") ||
	    !strcmp (x, "xlsx") || !strcmp (x, "odt") || !strcmp (x, "epub") || !strcmp (x, "apk"))
		return new ZipArchive;
	if (m[0] == '7' && m[1] == 'z' && m[2] == 0xBC && m[3] == 0xAF) { scopy (why, "7z archives are not supported yet.", cap); return 0; }
	if (m[0] == 'R' && m[1] == 'a' && m[2] == 'r' && m[3] == '!') { scopy (why, "RAR archives are not supported yet.", cap); return 0; }
	if (m[0] == 0x1F && m[1] == 0x8B) { scopy (why, "gzip / tar.gz archives are not supported yet.", cap); return 0; }
	if (!memcmp (m + 257, "ustar", 5)) { scopy (why, "tar archives are not supported yet.", cap); return 0; }
	if (r.size == 0 && !strcmp (x, "zip")) return new ZipArchive;
	// a self-extracting ZIP (an .exe with the archive at its end): try ZIP
	return new ZipArchive;
}
static inline Archive *archive_open (const char *path, char *why, int cap)
{
	Archive *a = archive_for (path, why, cap);
	if (a && !a->open (path)) { scopy (why, a->error, cap); delete a; return 0; }
	return a;
}
// A new, empty archive at `path` (the format from its extension: ZIP only, for now).
static inline Archive *archive_new (const char *path)
{
	Archive *a = new ZipArchive;
	scopy (a->path, path, sizeof a->path);
	return a;
}

// ---- which entries: a folder selected is everything under it -----------------------------------------------
// prefix "kernel/sys" -> the entries "kernel/sys" and "kernel/sys/..."
static inline bool under (const char *name, const char *prefix)
{
	int n = (int) strlen (prefix);
	if (!n) return true;
	return !strncmp (name, prefix, (size_t) n) && (name[n] == 0 || name[n] == '/');
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
enum { LAY_FULL = 0, LAY_FROM_CURRENT = 1, LAY_FLAT = 2 };
enum { OW_ASK = 0, OW_REPLACE = 1, OW_SKIP = 2, OW_KEEP_BOTH = 3 };
// The answers to "it exists": this one or all of them
enum { ANS_REPLACE = 1, ANS_SKIP = 2, ANS_KEEP_BOTH = 3, ANS_REPLACE_ALL = 4, ANS_SKIP_ALL = 5, ANS_CANCEL = -1 };

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

static inline u64 extract_total (const Extract &x)
{
	u64 t = 0;
	for (int i = 0; i < x.a->n; i++) if (!x.sel || x.sel[i]) t += x.a->e[i].size;
	return t;
}

// The path entry i goes to (false: nothing -- a folder flattened away)
static inline bool extract_target (const Extract &x, int i, char *out, int cap)
{
	const Entry &en = x.a->e[i];
	const char *rel = en.name;
	if (x.layout == LAY_FLAT) { if (en.dir) return false; rel = base_of (en.name); }
	else if (x.layout == LAY_FROM_CURRENT && x.current[0] && under (en.name, x.current))
	{
		int n = (int) strlen (x.current);
		rel = en.name + n + (en.name[n] == '/' ? 1 : 0);
		if (!rel[0]) return false;
	}
	char safe[300]; safe_rel (rel, safe, sizeof safe);
	if (!safe[0]) return false;
	join (out, cap, x.dest, safe);
	return true;
}

static inline bool run_extract (Extract &x, Progress *pr)
{
	Archive &a = *x.a;
	mkdirs (x.dest);
	int ow = x.overwrite;
	for (int i = 0; i < a.n; i++)
	{
		if (x.sel && !x.sel[i]) continue;
		const Entry &en = a.e[i];
		char to[300];
		if (!extract_target (x, i, to, sizeof to)) continue;
		if (en.dir) { mkdirs (to); continue; }
		char parent[300]; scopy (parent, to, sizeof parent);
		char *sl = strrchr (parent, '/'); if (sl) { *sl = 0; mkdirs (parent); }
		if (path_exists (to))
		{
			int what = ow;
			if (ow == OW_ASK)
			{
				int ans = x.ask ? x.ask (x.askCtx, to) : ANS_SKIP;
				if (ans == ANS_CANCEL) { scopy (x.error, "Cancelled.", sizeof x.error); return false; }
				if (ans == ANS_REPLACE_ALL) { ow = OW_REPLACE; ans = ANS_REPLACE; }
				if (ans == ANS_SKIP_ALL) { ow = OW_SKIP; ans = ANS_SKIP; }
				what = ans == ANS_REPLACE ? OW_REPLACE : ans == ANS_KEEP_BOTH ? OW_KEEP_BOTH : OW_SKIP;
			}
			if (what == OW_SKIP) { x.skipped++; if (pr) pr->step (en.size); continue; }
			if (what == OW_KEEP_BOTH) unique_path (to, sizeof to);
			else kapi_remove (to);
		}
		if (pr) pr->file (en.name);
		FileSink out;
		if (!out.open (to)) { scopy (x.error, "Cannot write ", sizeof x.error); scat (x.error, to, sizeof x.error); return false; }
		bool ok = a.extract (i, out, pr);
		out.close ();
		if (!ok || !out.ok)
		{
			kapi_remove (to);			// (no half file left)
			scopy (x.error, ok ? "Cannot write " : a.error, sizeof x.error);
			if (ok) scat (x.error, to, sizeof x.error);
			return false;
		}
		x.files++;
	}
	return true;
}

// ---- changing ---------------------------------------------------------------------------------------------
// Rewrite `a` by `plan` into a new file next to it, then swap it in; a is opened again (its entries
// are the new ones). An archive not yet on the disk (a new one) is written in place.
static inline bool apply_plan (Archive &a, const Plan &plan, Progress *pr)
{
	bool exists = path_exists (a.path);
	char tmp[300];
	scopy (tmp, a.path, sizeof tmp);
	if (exists) { scat (tmp, ".part", sizeof tmp); unique_path (tmp, sizeof tmp); }
	if (!a.rewrite (plan, tmp, pr)) { kapi_remove (tmp); return false; }
	if (exists)
	{
		if (kapi_remove (a.path) != 0) { kapi_remove (tmp); a.fail ("Cannot replace ", a.path); return false; }
		if (kapi_rename (tmp, a.path) != 0) { a.fail ("The new archive is ", tmp); return false; }
	}
	char p[300]; scopy (p, a.path, sizeof p);
	return a.open (p);
}

// Everything kept but the flags in `drop` (0: nothing dropped)
static inline void plan_keep_all (const Archive &a, Plan &plan, const char *drop)
{
	for (int i = 0; i < a.n; i++) if (!drop || !drop[i]) plan.keepEntry (i);
}

// Delete the entries flagged
static inline bool op_delete (Archive &a, const char *sel, Progress *pr)
{
	Plan plan; plan_keep_all (a, plan, sel);
	return apply_plan (a, plan, pr);
}

// Rename `from` (a file or a folder: what is under it follows) to `to` (a full archive path)
static inline bool op_rename (Archive &a, const char *from, const char *to, Progress *pr)
{
	Plan plan;
	int fl = (int) strlen (from);
	bool any = false;
	for (int i = 0; i < a.n; i++)
	{
		if (under (a.e[i].name, from))
		{
			char nn[600]; scopy (nn, to, sizeof nn); scat (nn, a.e[i].name + fl, sizeof nn);
			plan.keepEntry (i, nn); any = true;
		}
		else plan.keepEntry (i);
	}
	if (!any) { a.fail ("Nothing to rename."); return false; }
	return apply_plan (a, plan, pr);
}

// A new (empty) folder `name` (a full archive path)
static inline bool op_new_folder (Archive &a, const char *name, Progress *pr)
{
	Plan plan; plan_keep_all (a, plan, 0);
	plan.addItem (0, name);
	return apply_plan (a, plan, pr);
}

// Adding files of the disk: each path (a file or a folder, walked) into the archive's folder `into`
// ("" = the top). keepFolders: a folder added keeps its name and its tree; false: only its files,
// flat. A name that exists already: replaced (replace) or the new one skipped.
struct AddSet
{
	Plan  plan;
	char *drop;				// the old entries replaced
	int   files, folders, replaced, skipped;
	u64   bytes;
	AddSet () : drop (0), files (0), folders (0), replaced (0), skipped (0), bytes (0) {}
	~AddSet () { free (drop); }
};
static inline void add_name (const Archive &a, AddSet &s, const char *disk, const char *name, bool replace)
{
	for (int k = 0; k < s.plan.nadd; k++) if (!ci_cmp (s.plan.add[k].name, name)) return;	// (twice)
	int old = a.find (name);
	if (old >= 0)
	{
		if (!disk && a.e[old].dir) return;			// (the folder is there)
		if (!replace) { s.skipped++; return; }
		s.drop[old] = 1; s.replaced++;
	}
	s.plan.addItem (disk, name);
	if (disk) s.files++; else s.folders++;
}
static inline void add_walk (const Archive &a, AddSet &s, const char *disk, const char *name, bool keepFolders,
			     bool replace, int depth)
{
	if (keepFolders) add_name (a, s, 0, name, replace);
	if (depth > 24) return;
	void *d = kapi_opendir (disk);
	if (!d) return;
	struct kapi_dirent de;
	char **sub = 0; int ns = 0;			// (the folder read first: kapi's dir handle is not reentrant)
	while (kapi_readdir (d, &de))
	{
		if (!strcmp (de.name, ".") || !strcmp (de.name, "..")) continue;
		sub = (char **) realloc (sub, sizeof (char *) * (ns + 1));
		char *t = (char *) malloc (strlen (de.name) + 2); t[0] = de.is_dir ? 'd' : 'f'; strcpy (t + 1, de.name);
		sub[ns++] = t;
	}
	kapi_closedir (d);
	for (int i = 0; i < ns; i++)
	{
		char dp[300]; join (dp, sizeof dp, disk, sub[i] + 1);
		char an[600];
		if (keepFolders) { scopy (an, name, sizeof an); scat (an, "/", sizeof an); scat (an, sub[i] + 1, sizeof an); }
		else
		{
			const char *sl = strrchr (name, '/');
			if (sl) { scopy (an, name, sizeof an); an[sl - name + 1] = 0; } else an[0] = 0;
			scat (an, sub[i] + 1, sizeof an);
		}
		if (sub[i][0] == 'd') add_walk (a, s, dp, an, keepFolders, replace, depth + 1);
		else
		{
			add_name (a, s, dp, an, replace);
			void *h = kapi_open (dp); if (h) { s.bytes += kapi_fsize64 (h); kapi_close (h); }
		}
		free (sub[i]);
	}
	free (sub);
}
static inline void plan_add (const Archive &a, AddSet &s, const char *const *paths, int np, const char *into,
			     bool keepFolders, bool replace)
{
	free (s.drop); s.drop = (char *) calloc ((size_t) (a.n ? a.n : 1), 1);
	for (int i = 0; i < np; i++)
	{
		char p[300]; scopy (p, paths[i], sizeof p);
		int pl = (int) strlen (p); while (pl > 1 && p[pl - 1] == '/' && p[pl - 2] != ':') p[--pl] = 0;
		char an[600]; scopy (an, into, sizeof an);
		if (an[0]) scat (an, "/", sizeof an);
		scat (an, base_of (p), sizeof an);
		if (path_is_dir (p)) add_walk (a, s, p, an, keepFolders, replace, 0);
		else
		{
			add_name (a, s, p, an, replace);
			void *h = kapi_open (p); if (h) { s.bytes += kapi_fsize64 (h); kapi_close (h); }
		}
	}
	// the kept entries go first (the old order), the new ones after
	Plan &pl = s.plan;
	int *keep = 0; int nk = 0;
	for (int i = 0; i < a.n; i++) if (!s.drop[i]) { keep = (int *) realloc (keep, sizeof (int) * (nk + 1)); keep[nk++] = i; }
	pl.keep = keep; pl.nkeep = nk;
	pl.rename = (char **) calloc ((size_t) (nk ? nk : 1), sizeof (char *));
}
// How many of the names given exist in the archive (to ask before adding)
static inline int add_conflicts (const Archive &a, const char *const *paths, int np, const char *into, bool keepFolders)
{
	AddSet s; plan_add (a, s, paths, np, into, keepFolders, true);
	return s.replaced;
}
static inline bool op_add (Archive &a, const char *const *paths, int np, const char *into, bool keepFolders,
			   bool replace, int level, Progress *pr, int *added = 0)
{
	AddSet s; plan_add (a, s, paths, np, into, keepFolders, replace);
	s.plan.level = level;
	if (added) *added = s.files + s.folders;
	if (!s.files && !s.folders) { a.fail (s.skipped ? "These names exist in the archive already." : "Nothing to add."); return false; }
	return apply_plan (a, s.plan, pr);
}

} // namespace arc

#endif
