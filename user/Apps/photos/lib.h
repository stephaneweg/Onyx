//
// Apps/photos/lib.h -- the library: every picture of the folders watched (SD:/Pictures, every volume's DCIM, the
// folders added by hand), what is known of each (exif.h: its size, when it was taken, the camera; a favourite, a
// description), kept in SD:/etc/photos/library.db so the next start shows them at once; the folders walked again by a
// thread of its own (the new pictures read, the gone ones dropped; a volume not there keeps its photos aside). The
// albums: SD:/etc/photos/albums/<name>.txt, one path a line (an album never copies a photo). The folders added:
// SD:/etc/photos/folders.txt.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _photos_lib_h
#define _photos_lib_h

#include "Apps/photos/exif.h"
#include "imagekit/img/imgload.hpp"

namespace photos {

#define PH_DIR		"SD:/etc/photos"
#define PH_DB		PH_DIR "/library.db"
#define PH_ALBUMS	PH_DIR "/albums"
#define PH_FOLDERS	PH_DIR "/folders.txt"

// ---- small helpers ----------------------------------------------------------------------------------------------------------
static inline void scpy (char *d, const char *s, int cap) { if (cap <= 0) return; int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static inline char *sdup (const char *s) { if (!s) s = ""; size_t n = strlen (s); char *r = (char *) malloc (n + 1); memcpy (r, s, n + 1); return r; }
static inline int lc (int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static inline bool ieq (const char *a, const char *b) { while (*a && *b) { if (lc (*a) != lc (*b)) return false; a++; b++; } return *a == *b; }
static inline const char *ifind (const char *s, const char *w) { int n = (int) strlen (w); for (; *s; s++) { int i = 0; while (i < n && lc (s[i]) == lc (w[i])) i++; if (i == n) return s; } return 0; }
static int icmp (const char *a, const char *b) { while (*a && lc (*a) == lc (*b)) { a++; b++; } return lc ((unsigned char) *a) - lc ((unsigned char) *b); }
static inline bool ipfx (const char *s, const char *p) { while (*p) { if (lc (*s) != lc (*p)) return false; s++; p++; } return true; }
static unsigned hash_str (const char *s) { unsigned h = 2166136261u; while (s && *s) h = (h ^ (unsigned char) lc (*s++)) * 16777619u; return h; }
static const char *base_name (const char *p) { const char *b = strrchr (p, '/'); return b ? b + 1 : p; }

static char *file_read (const char *path, int *len)
{
	void *f = kapi_open (path); if (!f) return 0;
	unsigned n = kapi_fsize (f);
	char *b = (char *) malloc (n + 1);
	int got = 0;
	while (b && got < (int) n) { int r = kapi_read (f, b + got, (int) n - got); if (r <= 0) break; got += r; }
	kapi_close (f);
	if (!b) return 0;
	b[got] = 0; if (len) *len = got;
	return b;
}
static void mkdirs (const char *path)
{
	char p[300]; scpy (p, path, sizeof p);
	for (char *s = strchr (p, '/'); s; s = strchr (s + 1, '/')) { *s = 0; if (s > p && s[-1] != ':') kapi_mkdir (p); *s = '/'; }
	kapi_mkdir (p);
}
static bool dir_exists (const char *p) { void *d = kapi_opendir (p); if (!d) return false; kapi_closedir (d); return true; }
// the volume of a path is there ("SD1:/DCIM/x.jpg" -> SD1: mounted)
static bool volume_present (const char *path)
{
	char v[16]; int k = 0; while (path[k] && path[k] != ':' && k < 12) { v[k] = path[k]; k++; }
	if (path[k] != ':') return true;
	v[k++] = ':'; v[k++] = '/'; v[k] = 0;
	return dir_exists (v);
}

// a growing array
template <class T> struct Vec
{
	T *a; int n, cap;
	Vec () : a (0), n (0), cap (0) {}
	void push (const T &v) { if (n == cap) { int c = cap ? cap * 2 : 64; T *t = (T *) realloc (a, sizeof (T) * c); if (!t) return; a = t; cap = c; } a[n++] = v; }
	T &operator[] (int i) { return a[i]; }
	const T &operator[] (int i) const { return a[i]; }
	void clear () { n = 0; }
};

// ---- a photo ----------------------------------------------------------------------------------------------------------------------
struct Photo
{
	char *path;
	unsigned size;
	long long taken;		// local seconds (exif.h), 0 unknown
	long long added;		// when the library first saw it (local seconds)
	short w, h;			// as shown (the orientation applied); 0 unknown
	unsigned char orient;
	bool fav, alive, offline;
	char *camera, *expo, *desc;
	unsigned key () const { return (hash_str (path) ^ size * 2654435761u ^ orient * 0x9E3779B9u) | 1; }
	long long when () const { return taken ? taken : added; }
};
static void photo_free (Photo &p) { free (p.path); free (p.camera); free (p.expo); free (p.desc); p.path = p.camera = p.expo = p.desc = 0; }
static void photo_from (Photo &p, const char *path, unsigned size, const PicInfo &pi, long long added)
{
	memset (&p, 0, sizeof p);
	p.path = sdup (path); p.size = size; p.taken = pi.taken; p.added = added;
	bool turn = pi.orient >= 5;
	p.w = (short) (turn ? pi.h : pi.w); p.h = (short) (turn ? pi.w : pi.h);
	if (pi.w > 32767 || pi.h > 32767) p.w = p.h = 0;
	p.orient = (unsigned char) pi.orient;
	p.camera = sdup (pi.camera); p.expo = sdup (pi.expo); p.desc = sdup ("");
	p.alive = true;
}

static long long now_local ()
{
	int y, mo, d, h, mi, s;
	if (!kapi_get_datetime (&y, &mo, &d, &h, &mi, &s)) return 0;
	return local_secs (y, mo, d, h, mi, s);
}

// ---- an album -------------------------------------------------------------------------------------------------------------------
struct Album { char name[120]; Vec<char *> paths; };

// ---- the scan's thread: the folders walked, what it found posted back -------------------------------------------------------
struct Found { char *path; unsigned size; bool known; PicInfo pi; };
struct ScanBatch { Found *f; int n; bool done; int scanId; char roots[16][200]; int nroots; };

class Library;
static void scan_posted (void *ctx, long);

class Library
{
public:
	Vec<Photo> ph;
	Vec<Album> albums;
	char roots[16][200]; int nroots;	// the folders watched
	char added[16][200]; int nadded;	// (of them, the ones added by hand)
	bool scanning; int scanCount; int scanId;
	bool dirty;
	int gen;				// changes when the photos' indices move (some dropped)
	void (*onChange) ();			// the window's: the library changed

	Library () : nroots (0), nadded (0), scanning (false), scanCount (0), scanId (0), dirty (false), gen (0), onChange (0), m_hash (0), m_hcap (0), m_tid (-1), m_quit (false) {}

	// ---- the index by path
	int find (const char *path) const
	{
		if (!m_hcap) return -1;
		unsigned h = hash_str (path) & (m_hcap - 1);
		for (int k = 0; k < m_hcap; k++) { int i = m_hash[h]; if (i < 0) return -1; if (i < ph.n && ph[i].path && ieq (ph[i].path, path)) return i; h = (h + 1) & (m_hcap - 1); }
		return -1;
	}
	void reindex ()
	{
		int need = 64; while (need < ph.n * 2 + 2) need *= 2;
		if (need != m_hcap) { free (m_hash); m_hash = (int *) malloc (sizeof (int) * need); m_hcap = need; }
		for (int i = 0; i < m_hcap; i++) m_hash[i] = -1;
		for (int i = 0; i < ph.n; i++)
		{
			unsigned h = hash_str (ph[i].path) & (m_hcap - 1);
			while (m_hash[h] >= 0) h = (h + 1) & (m_hcap - 1);
			m_hash[h] = i;
		}
	}

	// ---- the folders watched
	void load_roots ()
	{
		nroots = nadded = 0;
		add_root ("SD:/Pictures", false);
		if (dir_exists ("SD:/Pictures/Camera")) scpy (roots[nroots++], "SD:/Pictures/Camera", sizeof roots[0]);	// (shown apart; walked with Pictures)
		static const char *const V[] = { "SD:/DCIM", "SD1:/DCIM", "SD2:/DCIM", "SD3:/DCIM", "USB1:/DCIM", "USB1P1:/DCIM", "USB1P2:/DCIM",
			"USB2:/DCIM", "USB2P1:/DCIM", "USB2P2:/DCIM", "USB3:/DCIM", "USB3P1:/DCIM", "USB3P2:/DCIM" };	// (a camera's card: P1 / P2 at most)
		for (unsigned i = 0; i < sizeof V / sizeof V[0]; i++) if (dir_exists (V[i])) add_root (V[i], false);
		int len; char *b = file_read (PH_FOLDERS, &len);
		if (b)
		{
			for (char *l = b; l && *l; )
			{
				char *e = strchr (l, '\n'); if (e) *e = 0;
				int k = (int) strlen (l); while (k && (l[k - 1] == '\r' || l[k - 1] == ' ' || l[k - 1] == '/')) l[--k] = 0;
				if (l[0]) add_root (l, true);
				l = e ? e + 1 : 0;
			}
			free (b);
		}
	}
	bool add_root (const char *p, bool byHand)
	{
		for (int i = 0; i < nroots; i++) if (ipfx (p, roots[i]) && (p[strlen (roots[i])] == 0 || p[strlen (roots[i])] == '/')) return false;	// (inside one already)
		if (nroots >= 16) return false;
		scpy (roots[nroots++], p, sizeof roots[0]);
		if (byHand && nadded < 16) scpy (added[nadded++], p, sizeof added[0]);
		return true;
	}
	void save_added ()
	{
		char b[16 * 202]; int k = 0;
		for (int i = 0; i < nadded; i++) k += snprintf (b + k, sizeof b - k, "%s\n", added[i]);
		mkdirs (PH_DIR); kapi_save_file (PH_FOLDERS, b, (unsigned) k);
	}
	bool remove_added (const char *p)
	{
		int j = -1; for (int i = 0; i < nadded; i++) if (ieq (added[i], p)) j = i;
		if (j < 0) return false;
		for (int i = j; i < nadded - 1; i++) scpy (added[i], added[i + 1], sizeof added[0]);
		nadded--; save_added ();
		// its photos leave the library
		for (int i = 0; i < ph.n; i++) if (ipfx (ph[i].path, p) && !under_other_root (ph[i].path, p)) ph[i].alive = false;
		compact (); load_roots ();
		return true;
	}
	bool under_other_root (const char *path, const char *except) const
	{
		for (int i = 0; i < nroots; i++) if (!ieq (roots[i], except) && ipfx (path, roots[i])) return true;
		return false;
	}

	// ---- the database
	void load ()
	{
		load_roots ();
		int len; char *b = file_read (PH_DB, &len);
		if (b)
		{
			for (char *l = b; l && *l; )
			{
				char *e = strchr (l, '\n'); if (e) *e = 0;
				if (l[0] == 'P' && l[1] == '\t')
				{
					char *f[12]; int nf = 0; f[nf++] = l + 2;
					for (char *c = l + 2; *c && nf < 12; c++) if (*c == '\t') { *c = 0; f[nf++] = c + 1; }
					if (nf >= 11)
					{
						Photo p; memset (&p, 0, sizeof p);
						p.path = sdup (f[0]); p.size = (unsigned) strtoul (f[1], 0, 10); p.taken = atoll (f[2]); p.added = atoll (f[3]);
						p.w = (short) atoi (f[4]); p.h = (short) atoi (f[5]); p.orient = (unsigned char) atoi (f[6]); p.fav = f[7][0] == '1';
						p.camera = sdup (f[8]); p.expo = sdup (f[9]); p.desc = unesc (f[10]);
						p.alive = true; p.offline = !volume_present (p.path);
						ph.push (p);
					}
				}
				l = e ? e + 1 : 0;
			}
			free (b);
		}
		reindex ();
		load_albums ();
	}
	void save ()
	{
		dirty = false;
		mkdirs (PH_DIR);
		size_t cap = 4096 + (size_t) ph.n * 200; for (int i = 0; i < ph.n; i++) cap += strlen (ph[i].path) + strlen (ph[i].desc) * 2 + strlen (ph[i].camera) + strlen (ph[i].expo);
		char *b = (char *) malloc (cap); size_t k = 0;
		if (!b) return;
		k += snprintf (b + k, cap - k, "# Photos' library (Onyx): path size taken added w h orient fav camera exposure description\n");
		for (int i = 0; i < ph.n; i++)
		{
			const Photo &p = ph[i]; if (!p.alive) continue;
			char d[1200]; esc (p.desc, d, sizeof d);
			k += snprintf (b + k, cap - k, "P\t%s\t%u\t%lld\t%lld\t%d\t%d\t%d\t%d\t%s\t%s\t%s\n", p.path, p.size, p.taken, p.added, p.w, p.h, p.orient, p.fav ? 1 : 0, p.camera, p.expo, d);
			if (k >= cap) { k = cap - 1; break; }
		}
		kapi_save_file (PH_DB, b, (unsigned) k);
		free (b);
	}
	static void esc (const char *s, char *o, int cap)
	{
		int k = 0;
		for (; *s && k < cap - 3; s++)
		{
			if (*s == '\n') { o[k++] = '\\'; o[k++] = 'n'; } else if (*s == '\t') { o[k++] = '\\'; o[k++] = 't'; } else if (*s == '\\') { o[k++] = '\\'; o[k++] = '\\'; }
			else if (*s != '\r') o[k++] = *s;
		}
		o[k] = 0;
	}
	static char *unesc (const char *s)
	{
		char *o = (char *) malloc (strlen (s) + 1); int k = 0;
		for (; *s; s++) { if (*s == '\\' && s[1]) { s++; o[k++] = *s == 'n' ? '\n' : *s == 't' ? '\t' : *s; } else o[k++] = *s; }
		o[k] = 0; return o;
	}
	// the photos gone dropped (their album lines too)
	void compact ()
	{
		int k = 0;
		for (int i = 0; i < ph.n; i++) { if (ph[i].alive) ph[k++] = ph[i]; else photo_free (ph[i]); }
		if (k != ph.n) gen++;
		ph.n = k;
		reindex ();
		dirty = true;
	}

	// ---- the albums
	void load_albums ()
	{
		for (int i = 0; i < albums.n; i++) { for (int j = 0; j < albums[i].paths.n; j++) free (albums[i].paths[j]); free (albums[i].paths.a); }
		albums.clear ();
		void *d = kapi_opendir (PH_ALBUMS);
		if (!d) return;
		struct kapi_dirent e;
		while (kapi_readdir (d, &e))
		{
			int l = (int) strlen (e.name);
			if (e.is_dir || l < 5 || !ieq (e.name + l - 4, ".txt")) continue;
			Album a; memset (&a, 0, sizeof a);
			scpy (a.name, e.name, sizeof a.name); a.name[l - 4 < (int) sizeof a.name ? l - 4 : sizeof a.name - 1] = 0;
			char p[300]; snprintf (p, sizeof p, PH_ALBUMS "/%s", e.name);
			int len; char *b = file_read (p, &len);
			if (b)
			{
				for (char *s = b; s && *s; ) { char *x = strchr (s, '\n'); if (x) *x = 0; int k = (int) strlen (s); if (k && s[k - 1] == '\r') s[--k] = 0; if (s[0] && s[0] != '#') a.paths.push (sdup (s)); s = x ? x + 1 : 0; }
				free (b);
			}
			albums.push (a);
		}
		kapi_closedir (d);
		// A..Z
		for (int i = 1; i < albums.n; i++) { Album t = albums[i]; int j = i - 1; while (j >= 0 && icmp (albums[j].name, t.name) > 0) { albums[j + 1] = albums[j]; j--; } albums[j + 1] = t; }
	}
	void save_album (int a)
	{
		mkdirs (PH_ALBUMS);
		Album &A = albums[a];
		size_t cap = 64; for (int i = 0; i < A.paths.n; i++) cap += strlen (A.paths[i]) + 1;
		char *b = (char *) malloc (cap); size_t k = 0;
		for (int i = 0; i < A.paths.n; i++) k += snprintf (b + k, cap - k, "%s\n", A.paths[i]);
		char p[300]; snprintf (p, sizeof p, PH_ALBUMS "/%s.txt", A.name);
		kapi_save_file (p, b, (unsigned) k);
		free (b);
	}
	int album_find (const char *name) const { for (int i = 0; i < albums.n; i++) if (ieq (albums[i].name, name)) return i; return -1; }
	int album_new (const char *name)
	{
		int a = album_find (name); if (a >= 0) return a;
		Album x; memset (&x, 0, sizeof x); scpy (x.name, name, sizeof x.name);
		albums.push (x); save_album (albums.n - 1);
		load_albums ();
		return album_find (name);
	}
	bool album_has (int a, const char *path) const { const Album &A = albums[a]; for (int i = 0; i < A.paths.n; i++) if (ieq (A.paths[i], path)) return true; return false; }
	void album_add (int a, const char *path) { if (!album_has (a, path)) { albums[a].paths.push (sdup (path)); save_album (a); } }
	void album_remove (int a, const char *path)
	{
		Album &A = albums[a]; int k = 0;
		for (int i = 0; i < A.paths.n; i++) { if (ieq (A.paths[i], path)) free (A.paths[i]); else A.paths[k++] = A.paths[i]; }
		A.paths.n = k; save_album (a);
	}
	bool album_rename (int a, const char *name)
	{
		if (!name[0] || strchr (name, '/') || strchr (name, ':') || album_find (name) >= 0) return false;
		char o[300], n[300]; snprintf (o, sizeof o, PH_ALBUMS "/%s.txt", albums[a].name); snprintf (n, sizeof n, PH_ALBUMS "/%s.txt", name);
		if (kapi_rename (o, n) != 0) return false;
		load_albums (); return true;
	}
	void album_delete (int a) { char o[300]; snprintf (o, sizeof o, PH_ALBUMS "/%s.txt", albums[a].name); kapi_remove (o); load_albums (); }
	// a path moved / renamed / gone: the albums follow
	void albums_path_changed (const char *from, const char *to)
	{
		for (int a = 0; a < albums.n; a++)
		{
			bool ch = false; Album &A = albums[a];
			for (int i = 0; i < A.paths.n; i++) if (ieq (A.paths[i], from)) { free (A.paths[i]); A.paths[i] = to ? sdup (to) : 0; ch = true; }
			if (!ch) continue;
			int k = 0; for (int i = 0; i < A.paths.n; i++) if (A.paths[i]) A.paths[k++] = A.paths[i];
			A.paths.n = k; save_album (a);
		}
	}

	// ---- the scan
	void start_scan ()
	{
		if (scanning) return;
		scanning = true; scanCount = 0; scanId++;
		for (int i = 0; i < ph.n; i++) ph[i].alive = false;
		Job *j = new Job; j->lib = this; j->id = scanId; j->nroots = nroots; memcpy (j->roots, roots, sizeof roots);
		// what is known (path + size): the thread skips reading them again
		j->nknown = ph.n; j->known = (Known *) malloc (sizeof (Known) * (ph.n ? ph.n : 1));
		for (int i = 0; i < ph.n; i++) { j->known[i].path = sdup (ph[i].path); j->known[i].size = ph[i].size; j->known[i].h = hash_str (ph[i].path); }
		m_tid = kapi_thread_create (scan_main, j, 256 * 1024, "photos-scan");
		if (m_tid <= 0) { scanning = false; for (int i = 0; i < ph.n; i++) ph[i].alive = true; }
	}
	void quit () { m_quit = true; if (m_tid > 0) kapi_thread_join (m_tid, 3000, 0); }

	// the window's thread: a batch came
	void take (ScanBatch *b)
	{
		if (b->scanId != scanId) return;
		bool first = ph.n == 0;
		long long now = now_local ();
		for (int i = 0; i < b->n; i++)
		{
			Found &f = b->f[i];
			int k = find (f.path);
			if (k >= 0 && ph[k].size == f.size) { ph[k].alive = true; ph[k].offline = false; continue; }
			if (k >= 0)
			{	// changed (edited elsewhere): its facts again, its own kept
				Photo &p = ph[k]; bool fav = p.fav; char *desc = p.desc; long long ad = p.added; p.desc = 0; photo_free (p);
				photo_from (p, f.path, f.size, f.pi, ad); free (p.desc); p.desc = desc; p.fav = fav;
			}
			else
			{
				Photo p; photo_from (p, f.path, f.size, f.pi, first || !now ? f.pi.taken : now);
				ph.push (p); add_hash (ph.n - 1);
			}
			dirty = true; scanCount++;
		}
		if (b->done)
		{
			// gone: dropped (a volume not there: kept aside)
			for (int i = 0; i < ph.n; i++)
				if (!ph[i].alive)
				{
					bool watched = false; for (int r = 0; r < nroots; r++) if (ipfx (ph[i].path, roots[r])) watched = true;
					if (watched && !volume_present (ph[i].path)) { ph[i].alive = true; ph[i].offline = true; }
					else dirty = true;
				}
			int before = ph.n;
			compact ();
			if (before == ph.n && !scanCount) dirty = false;
			scanning = false;
			if (m_tid > 0) { kapi_thread_join (m_tid, 1000, 0); m_tid = -1; }
			if (dirty) save ();
		}
		if (onChange) onChange ();
	}

	struct Known { char *path; unsigned size; unsigned h; };
	struct Job { Library *lib; int id; char roots[16][200]; int nroots; Known *known; int nknown; Found *batch; int nb; unsigned lastPost; int *kh; int khcap; };

private:
	int *m_hash; int m_hcap;
	int m_tid; volatile bool m_quit;

	void add_hash (int i)
	{
		if (ph.n * 2 + 2 > m_hcap) { reindex (); return; }
		unsigned h = hash_str (ph[i].path) & (m_hcap - 1);
		while (m_hash[h] >= 0) h = (h + 1) & (m_hcap - 1);
		m_hash[h] = i;
	}
	static int known_find (Job *j, const char *path)
	{
		unsigned h = hash_str (path);
		for (unsigned k = h & (j->khcap - 1), n = 0; n < (unsigned) j->khcap; n++, k = (k + 1) & (j->khcap - 1))
		{
			int i = j->kh[k]; if (i < 0) return -1;
			if (j->known[i].h == h && ieq (j->known[i].path, path)) return i;
		}
		return -1;
	}
	static void post (Job *j, bool done)
	{
		ScanBatch *b = new ScanBatch; b->f = j->batch; b->n = j->nb; b->done = done; b->scanId = j->id; b->nroots = 0;
		j->batch = 0; j->nb = 0; j->lastPost = kapi_get_ticks ();
		kapi_post (scan_posted, b, (long) j->lib);
	}
	static void walk (Job *j, const char *dir, int depth)
	{
		if (depth > 10 || j->lib->m_quit) return;
		void *d = kapi_opendir (dir);
		if (!d) return;
		struct kapi_dirent e;
		// the entries first (a folder's handle kept open while going down would hold FatFs's)
		Vec<char *> subs;
		while (kapi_readdir (d, &e))
		{
			if (e.name[0] == '.' || !strcmp (e.name, "$RECYCLE.BIN") || !strcmp (e.name, "System Volume Information")) continue;
			char p[400]; snprintf (p, sizeof p, "%s/%s", dir, e.name);
			if (e.is_dir) { subs.push (sdup (p)); continue; }
			if (!img_is_image_name (e.name)) continue;
			Found f; memset (&f, 0, sizeof f);
			f.path = sdup (p); f.size = e.size;
			int k = known_find (j, p);
			if (k >= 0 && j->known[k].size == e.size) f.known = true;
			else if (!pic_info (p, f.pi)) { free (f.path); continue; }
			if (!j->batch) j->batch = (Found *) malloc (sizeof (Found) * 256);
			j->batch[j->nb++] = f;
			if (j->nb >= 256 || (j->nb >= 16 && kapi_get_ticks () - j->lastPost > 40)) post (j, false);
		}
		kapi_closedir (d);
		for (int i = 0; i < subs.n; i++) { walk (j, subs[i], depth + 1); free (subs[i]); }
		free (subs.a);
	}
	static int scan_main (void *arg)
	{
		Job *j = (Job *) arg;
		j->khcap = 64; while (j->khcap < j->nknown * 2 + 2) j->khcap *= 2;
		j->kh = (int *) malloc (sizeof (int) * j->khcap); for (int i = 0; i < j->khcap; i++) j->kh[i] = -1;
		for (int i = 0; i < j->nknown; i++) { unsigned k = j->known[i].h & (j->khcap - 1); while (j->kh[k] >= 0) k = (k + 1) & (j->khcap - 1); j->kh[k] = i; }
		j->batch = 0; j->nb = 0; j->lastPost = kapi_get_ticks ();
		for (int r = 0; r < j->nroots && !j->lib->m_quit; r++)
		{
			bool inside = false;		// (a root in another: walked with it)
			for (int o = 0; o < j->nroots; o++) { int l = (int) strlen (j->roots[o]); if (o != r && ipfx (j->roots[r], j->roots[o]) && j->roots[r][l] == '/') inside = true; }
			if (!inside) walk (j, j->roots[r], 0);
		}
		post (j, true);
		for (int i = 0; i < j->nknown; i++) free (j->known[i].path);
		free (j->known); free (j->kh);
		delete j;
		return 0;
	}
};

static void scan_posted (void *ctx, long lib)
{
	ScanBatch *b = (ScanBatch *) ctx;
	((Library *) lib)->take (b);
	for (int i = 0; i < b->n; i++) free (b->f[i].path);
	free (b->f);
	delete b;
}

} // namespace photos

#endif
