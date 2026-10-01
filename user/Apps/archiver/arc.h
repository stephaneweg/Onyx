//
// archiver/arc.h -- the Archiver's engine, its common part: an archive's entries, the Archive
// interface every format implements (zip.h, and later 7z, tar, rar), files read at random and
// written as a stream through the kapi, names in UTF-8, DOS dates. Plain C++ over newlib (malloc,
// string.h) and the kapi file calls: the same code runs on the PC (the desktop simulator's kapi)
// for the tests. No wtk here.
//
#ifndef _archiver_arc_h
#define _archiver_arc_h

#include <stdlib.h>
#include <string.h>
#include "kapi.h"

namespace arc {

typedef unsigned long long u64;
typedef unsigned u32;
typedef unsigned short u16;
typedef unsigned char u8;

// ---- strings ---------------------------------------------------------------------------------------
static inline char *sdup (const char *s, int n = -1)
{
	if (n < 0) n = (int) strlen (s);
	char *d = (char *) malloc ((size_t) n + 1);
	if (d) { memcpy (d, s, (size_t) n); d[n] = 0; }
	return d;
}
static inline void scopy (char *d, const char *s, int cap)
{
	int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0;
}
static inline void scat (char *d, const char *s, int cap)
{
	int n = (int) strlen (d); scopy (d + n, s, cap - n);
}
static inline char lower (char c) { return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c; }
static inline int ci_cmp (const char *a, const char *b)
{
	for (;; a++, b++)
	{
		char x = lower (*a), y = lower (*b);
		if (x != y) return (unsigned char) x < (unsigned char) y ? -1 : 1;
		if (!x) return 0;
	}
}
// The extension of a name (after its last '.', lower case, without it) into out.
static inline void ext_of (const char *name, char *out, int cap)
{
	const char *slash = strrchr (name, '/'), *dot = strrchr (name, '.');
	out[0] = 0;
	if (!dot || (slash && dot < slash)) return;
	int i = 0; for (dot++; *dot && i < cap - 1; dot++) out[i++] = lower (*dot); out[i] = 0;
}
static inline const char *base_of (const char *path)
{
	const char *s = strrchr (path, '/');
	if (!s) s = strrchr (path, ':');
	return s ? s + 1 : path;
}

// Human sizes: "512 B", "12.4 KB", "3.1 MB", "1.2 GB" (one decimal under 100).
static inline void human_size (u64 n, char *out, int cap)
{
	static const char *u[] = { "B", "KB", "MB", "GB", "TB" };
	int k = 0; u64 whole = n, tenth = 0;
	while (whole >= 1024 && k < 4) { tenth = (whole % 1024) * 10 / 1024; whole /= 1024; k++; }
	char b[32]; int i = 0; char t[24]; int j = 0; u64 v = whole;
	do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v);
	while (j) b[i++] = t[--j];
	if (k && whole < 100) { b[i++] = '.'; b[i++] = (char) ('0' + tenth); }
	b[i++] = ' '; for (const char *s = u[k]; *s; s++) b[i++] = *s; b[i] = 0;
	scopy (out, b, cap);
}
static inline void u64_str (u64 v, char *out, int cap)
{
	char t[24]; int j = 0; do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v);
	int i = 0; while (j && i < cap - 1) out[i++] = t[--j]; out[i] = 0;
}

// ---- DOS dates (what ZIP keeps: 2-second steps from 1980) --------------------------------------------
static inline u32 dos_now ()
{
	int y = 2026, mo = 1, d = 1, h = 0, mi = 0, s = 0;
	kapi_get_datetime (&y, &mo, &d, &h, &mi, &s);
	if (y < 1980) y = 1980;
	return (u32) (((y - 1980) << 25) | (mo << 21) | (d << 16) | (h << 11) | (mi << 5) | (s / 2));
}
// "28/09/2026 14:12"
static inline void dos_str (u32 t, char *out, int cap)
{
	if (!t) { scopy (out, "-", cap); return; }
	int y = (int) (t >> 25) + 1980, mo = (int) (t >> 21) & 15, d = (int) (t >> 16) & 31;
	int h = (int) (t >> 11) & 31, mi = (int) (t >> 5) & 63;
	char b[20] = "00/00/0000 00:00";
	b[0] += (char) (d / 10); b[1] += (char) (d % 10); b[3] += (char) (mo / 10); b[4] += (char) (mo % 10);
	b[6] += (char) (y / 1000); b[7] += (char) (y / 100 % 10); b[8] += (char) (y / 10 % 10); b[9] += (char) (y % 10);
	b[11] += (char) (h / 10); b[12] += (char) (h % 10); b[14] += (char) (mi / 10); b[15] += (char) (mi % 10);
	scopy (out, b, cap);
}

// ---- code page 437 (old ZIP names) -> UTF-8 ------------------------------------------------------------
static const unsigned short CP437[128] = {
	0xC7,0xFC,0xE9,0xE2,0xE4,0xE0,0xE5,0xE7,0xEA,0xEB,0xE8,0xEF,0xEE,0xEC,0xC4,0xC5,
	0xC9,0xE6,0xC6,0xF4,0xF6,0xF2,0xFB,0xF9,0xFF,0xD6,0xDC,0xA2,0xA3,0xA5,0x20A7,0x192,
	0xE1,0xED,0xF3,0xFA,0xF1,0xD1,0xAA,0xBA,0xBF,0x2310,0xAC,0xBD,0xBC,0xA1,0xAB,0xBB,
	0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
	0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
	0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
	0x3B1,0xDF,0x393,0x3C0,0x3A3,0x3C3,0xB5,0x3C4,0x3A6,0x398,0x3A9,0x3B4,0x221E,0x3C6,0x3B5,0x2229,
	0x2261,0xB1,0x2265,0x2264,0x2320,0x2321,0xF7,0x2248,0xB0,0x2219,0xB7,0x221A,0x207F,0xB2,0x25A0,0xA0 };
static inline int u8_put (char *o, unsigned cp)
{
	if (cp < 0x80) { o[0] = (char) cp; return 1; }
	if (cp < 0x800) { o[0] = (char) (0xC0 | cp >> 6); o[1] = (char) (0x80 | (cp & 0x3F)); return 2; }
	o[0] = (char) (0xE0 | cp >> 12); o[1] = (char) (0x80 | ((cp >> 6) & 0x3F)); o[2] = (char) (0x80 | (cp & 0x3F));
	return 3;
}
// A name of n bytes in CP437 (utf8 = false) or UTF-8, made a clean archive path: '\' -> '/', no
// leading '/', no "./", no trailing '/' (the caller knows it was a folder). malloc'ed.
static inline char *clean_name (const char *s, int n, bool utf8)
{
	char *o = (char *) malloc ((size_t) n * 3 + 1);
	if (!o) return 0;
	int k = 0;
	for (int i = 0; i < n; i++)
	{
		unsigned char c = (unsigned char) s[i];
		if (c == '\\') c = '/';
		if (c == 0) break;
		if (c >= 0x80 && !utf8) k += u8_put (o + k, CP437[c - 0x80]);
		else o[k++] = (char) c;
	}
	o[k] = 0;
	// drop "/", "./" at the start, doubled slashes, a trailing slash
	char *r = o, *w = o;
	while (*r == '/' || (r[0] == '.' && r[1] == '/')) r += (*r == '/') ? 1 : 2;
	for (; *r; r++) { if (*r == '/' && (w == o || w[-1] == '/')) continue; *w++ = *r; }
	while (w > o && w[-1] == '/') w--;
	*w = 0;
	return o;
}
static inline bool is_ascii (const char *s) { for (; *s; s++) if ((unsigned char) *s >= 0x80) return false; return true; }
// n bytes that are valid UTF-8 (with at least one character past ASCII: a CP437 name rarely is)
static inline bool valid_utf8 (const char *s, int n)
{
	bool multi = false;
	for (int i = 0; i < n;)
	{
		unsigned char c = (unsigned char) s[i];
		int k = c < 0x80 ? 1 : c >= 0xC2 && c < 0xE0 ? 2 : c >= 0xE0 && c < 0xF0 ? 3 : c >= 0xF0 && c < 0xF5 ? 4 : 0;
		if (!k || i + k > n) return false;
		for (int j = 1; j < k; j++) if (((unsigned char) s[i + j] & 0xC0) != 0x80) return false;
		if (k > 1) multi = true;
		i += k;
	}
	return multi;
}

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
