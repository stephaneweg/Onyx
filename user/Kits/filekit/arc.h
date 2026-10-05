//
// filekit/arc.h -- the archives' engine (FileKit's; it was the Archiver's until 2026-10-05), its common part: an archive's entries, the Archive
// interface every format implements (zip.h, and later 7z, tar, rar), files read at random and
// written as a stream through the kapi, names in UTF-8, DOS dates. Plain C++ over newlib (malloc,
// string.h) and the kapi file calls: the same code runs on the PC (the desktop simulator's kapi)
// for the tests. No uikit here.
//
#ifndef _archiver_arc_h
#define _archiver_arc_h

#include <stdlib.h>
#include <string.h>
#include "appkit/appkit.h"
#include "arcbase.h"		// strings, sizes, DOS dates, names

namespace arc {

// ---- files: read at random, written as a stream ---------------------------------------------------------
class Reader
{
public:
	void *h; u64 size, pos;
	Reader () : h (0), size (0), pos (0) {}
	~Reader () { close (); }
	bool open (const char *path)
	{
		close ();
		h = kapi_open (path);
		if (!h) return false;
		size = kapi_fsize64 (h); pos = 0;
		return true;
	}
	void close () { if (h) kapi_close (h); h = 0; }
	bool seek (u64 p)
	{
		if (p == pos) return true;
		if (kapi_seek (h, p) != 0) return false;
		pos = p; return true;
	}
	// n bytes (all of them, or false)
	bool read (void *b, u32 n)
	{
		u8 *d = (u8 *) b;
		while (n)
		{
			int r = kapi_read (h, d, n);
			if (r <= 0) return false;
			d += r; n -= (u32) r; pos += (u64) r;
		}
		return true;
	}
	int read_some (void *b, u32 n)
	{
		int r = kapi_read (h, b, n);
		if (r > 0) pos += (u64) r;
		return r;
	}
	bool read_at (u64 p, void *b, u32 n) { return seek (p) && read (b, n); }
};

// Where extracted bytes go: a file (FileSink), memory (the caller's), a checksum only (a test).
struct Sink
{
	virtual bool write (const void *b, u32 n) = 0;
	virtual ~Sink () {}
};
class FileSink : public Sink
{
public:
	void *h; u64 written; bool ok;
	FileSink () : h (0), written (0), ok (false) {}
	~FileSink () { close (); }
	bool open (const char *path, bool append = false)
	{
		h = kapi_file_out (path, append ? 1 : 0); written = 0; ok = h != 0;
		return ok;
	}
	bool write (const void *b, u32 n) override
	{
		const u8 *s = (const u8 *) b;
		while (ok && n)
		{
			int w = kapi_stream_write (h, s, n);
			if (w <= 0) { ok = false; break; }
			s += w; n -= (u32) w; written += (u64) w;
		}
		return ok;
	}
	void close () { if (h) kapi_stream_close (h); h = 0; }
};
struct NullSink : Sink { bool write (const void *, u32) override { return true; } };

// The progress of a job: bytes done of a total; cancelled when the user said so. Called from the
// job's thread.
struct Progress
{
	virtual bool step (u64 bytes) = 0;		// bytes more done -> false: stop (cancelled)
	virtual void file (const char *name) { (void) name; }	// working on this one
	virtual ~Progress () {}
};

// ---- an archive ---------------------------------------------------------------------------------------
struct Entry
{
	char *name;				// its path in the archive (UTF-8, '/'-separated, no '/' at the ends)
	u64   size, packed;			// the original size, what it takes in the archive
	u64   offset;			// the format's (ZIP: its local header)
	u32   crc, dostime;			// CRC-32 (0: none), the DOS date and time
	u32   flags;			// the format's (ZIP: the general-purpose bits)
	int   method;			// the format's (ZIP: 0 stored, 8 deflate...)
	bool  dir, encrypted;
	// ZIP: what a rewrite copies as is
	u16   made, ver, intAttr; u32 extAttr;
	u8   *cext; u16 cextLen;		// the central header's extra field (without its zip64 part)
	u8   *comment; u16 commentLen;
};

// What a rewrite does (an add, a delete, a rename, a new folder): the entries kept (each its index,
// a new name or 0), the new ones (a file of the disk -- or a folder when `disk` is 0 --, its name
// in the archive).
struct NewItem { char *disk; char *name; };
struct Plan
{
	int  *keep; char **rename; int nkeep;
	NewItem *add; int nadd;
	int  level;				// 0 store, 1 fast, 6 normal, 9 best
	bool storePacked;			// already packed files (png, jpg, zip...) stored
	Plan () : keep (0), rename (0), nkeep (0), add (0), nadd (0), level (6), storePacked (true) {}
	~Plan ()
	{
		for (int i = 0; i < nkeep; i++) free (rename[i]);
		for (int i = 0; i < nadd; i++) { free (add[i].disk); free (add[i].name); }
		free (keep); free (rename); free (add);
	}
	void keepEntry (int i, const char *newName = 0)
	{
		keep = (int *) realloc (keep, sizeof (int) * (nkeep + 1));
		rename = (char **) realloc (rename, sizeof (char *) * (nkeep + 1));
		keep[nkeep] = i; rename[nkeep] = newName ? sdup (newName) : 0; nkeep++;
	}
	void addItem (const char *disk, const char *name)
	{
		add = (NewItem *) realloc (add, sizeof (NewItem) * (nadd + 1));
		add[nadd].disk = disk ? sdup (disk) : 0; add[nadd].name = sdup (name); nadd++;
	}
};

// Files that do not shrink: stored as they are.
static inline bool packed_ext (const char *name)
{
	static const char *e[] = { "zip", "7z", "rar", "gz", "tgz", "bz2", "xz", "zst", "lz", "lzma", "jar", "apk",
				   "png", "jpg", "jpeg", "gif", "webp", "mp3", "ogg", "opus", "m4a", "aac", "flac",
				   "mp4", "mkv", "webm", "avi", "mov", "docx", "xlsx", "pptx", "odt", "ods", "epub", 0 };
	char x[12]; ext_of (name, x, sizeof x);
	for (int i = 0; e[i]; i++) if (!strcmp (x, e[i])) return true;
	return false;
}

class Archive
{
public:
	char  path[300];
	char  error[200];
	Entry *e; int n, cap;
	char *comment;

	Archive () : e (0), n (0), cap (0), comment (0) { path[0] = error[0] = 0; }
	virtual ~Archive () { clear (); }
	virtual const char *format () const = 0;	// "ZIP"
	virtual const char *methodName (const Entry &) const { return ""; }
	virtual bool writable () const { return false; }
	virtual bool open (const char *p) = 0;
	// The bytes of entry i into out (checked against its CRC); false: error[] says why.
	virtual bool extract (int i, Sink &out, Progress *pr) = 0;
	// A new archive at `dest` made of plan's entries; false: error[] (dest may be left half written).
	virtual bool rewrite (const Plan &plan, const char *dest, Progress *pr)
	{ (void) plan; (void) dest; (void) pr; fail ("This format is read only."); return false; }

	void fail (const char *a, const char *b = 0)
	{
		scopy (error, a, sizeof error); if (b) scat (error, b, sizeof error);
	}
	Entry &add ()
	{
		if (n == cap)
		{
			int nc = cap ? cap * 2 : 64;
			Entry *ne = (Entry *) realloc (e, sizeof (Entry) * nc);
			if (!ne) { static Entry dummy; return dummy; }
			e = ne; cap = nc;
		}
		Entry &x = e[n++]; memset (&x, 0, sizeof x);
		return x;
	}
	void clear ()
	{
		for (int i = 0; i < n; i++) { free (e[i].name); free (e[i].cext); free (e[i].comment); }
		free (e); e = 0; n = cap = 0;
		free (comment); comment = 0;
	}
	int find (const char *name) const
	{
		for (int i = 0; i < n; i++) if (!ci_cmp (e[i].name, name)) return i;
		return -1;
	}
	u64 totalSize () const { u64 t = 0; for (int i = 0; i < n; i++) t += e[i].size; return t; }
	u64 totalPacked () const { u64 t = 0; for (int i = 0; i < n; i++) t += e[i].packed; return t; }
};

} // namespace arc

#endif
