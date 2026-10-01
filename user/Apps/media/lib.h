//
// Apps/media/lib.h -- Media Player's library: the songs of the folders it watches (SD:/Music by default),
// their tags (tags.h) kept in SD:/etc/media/library.tsv so the next start is at once; grouped into
// albums (the album artist -- else the artist -- and the album's name), artists, genres and folders;
// what the user does with them in SD:/etc/media/stats.tsv (plays, the last time, favourites); the
// playlists as .m3u files in SD:/Music/Playlists (other players read them).
//
// A scan runs in a thread (Scanner): the folders walked, the files known (same size) taken from the
// previous library, the others read; the new library is handed to the window's thread (kapi_post),
// which swaps it in. Strings are malloc'd (strdup) and freed with their Library.
//
#ifndef _media_lib_h
#define _media_lib_h

#include "tags.h"

namespace media {

#define LIB_DIR		"SD:/etc/media"
#define LIB_FILE	"SD:/etc/media/library.tsv"
#define STATS_FILE	"SD:/etc/media/stats.tsv"
#define PLAYLIST_DIR	"SD:/Music/Playlists"

static inline char *sdup (const char *s) { size_t n = strlen (s ? s : ""); char *d = (char *) malloc (n + 1); memcpy (d, s ? s : "", n + 1); return d; }
static inline unsigned long long now_stamp ()
{	// yyyymmddhhmm (sorts as time)
	int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0; kapi_get_datetime (&y, &mo, &d, &h, &mi, &s);
	return (((((unsigned long long) y * 100 + mo) * 100 + d) * 100 + h) * 100 + mi);
}
// a case- and accent-blind comparison of UTF-8 names (sorting): the ASCII letters folded
static inline int name_cmp (const char *a, const char *b)
{
	// "The Paper Boats" sorts under P
	if (!strncasecmp (a, "The ", 4) && a[4]) a += 4;
	if (!strncasecmp (b, "The ", 4) && b[4]) b += 4;
	return strcasecmp (a, b);
}

// A stable merge sort of indexes, with a context (no global: the scan's thread sorts too).
typedef int (*IdxCmp) (const void *ctx, int a, int b);
static void sort_idx (int *a, int n, IdxCmp cmp, const void *ctx)
{
	if (n < 2) return;
	int *t = (int *) malloc (sizeof (int) * n);
	for (int w = 1; w < n; w *= 2)
	{
		for (int lo = 0; lo < n; lo += 2 * w)
		{
			int mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n, i = lo, j = mid, k = lo;
			while (i < mid && j < hi) t[k++] = cmp (ctx, a[i], a[j]) <= 0 ? a[i++] : a[j++];
			while (i < mid) t[k++] = a[i++];
			while (j < hi) t[k++] = a[j++];
		}
		memcpy (a, t, sizeof (int) * n);
	}
	free (t);
}

struct Song
{
	char *path, *title, *artist, *albumArtist, *album, *genre, *folderCover;
	int year, track, disc, durMs, fmt;
	unsigned size; u64 coverOff; unsigned coverLen;
	unsigned long long added, lastPlayed;
	int plays; bool fav;
	int alb, art, gen, fold;			// indexes (the library's groups)
};
struct Album { char *title, *artist; int year; int *songs, n; int durMs; int cover; unsigned long long added; char *genre; };
struct Group { char *name; int *songs, n; int *albums, na; int durMs; };	// an artist, a genre, a folder
struct Playlist { char *name, *path; int *songs, n, cap; bool builtin; };

class Library
{
public:
	Song *s; int n, cap;
	Album *al; int nal;
	Group *ar; int nar;			// artists (by album artist, else artist)
	Group *ge; int nge;			// genres
	Group *fo; int nfo;			// folders
	Library () : s (0), n (0), cap (0), al (0), nal (0), ar (0), nar (0), ge (0), nge (0), fo (0), nfo (0) {}
	~Library ()
	{
		for (int i = 0; i < n; i++) { Song &x = s[i]; free (x.path); free (x.title); free (x.artist); free (x.albumArtist); free (x.album); free (x.genre); free (x.folderCover); }
		free (s); free_groups ();
	}
	Song &add ()
	{
		if (n == cap) { cap = cap ? cap * 2 : 256; s = (Song *) realloc (s, sizeof (Song) * cap); }
		Song &x = s[n++]; memset (&x, 0, sizeof x); x.alb = x.art = x.gen = x.fold = -1; return x;
	}
	int find (const char *path) const
	{	// (the scan: a sorted copy is kept for the lookups)
		int lo = 0, hi = n - 1;
		while (lo <= hi) { int m = (lo + hi) / 2, c = strcmp (s[order[m]].path, path); if (!c) return order[m]; if (c < 0) lo = m + 1; else hi = m - 1; }
		return -1;
	}
	void index_paths ()
	{
		free (order); order = (int *) malloc (sizeof (int) * (n ? n : 1));
		for (int i = 0; i < n; i++) order[i] = i;
		sort_idx (order, n, [] (const void *c, int a, int b) -> int { const Library *L = (const Library *) c; return strcmp (L->s[a].path, L->s[b].path); }, this);
	}
	const char *artist_of (const Song &x) const { return x.albumArtist && x.albumArtist[0] ? x.albumArtist : x.artist && x.artist[0] ? x.artist : "Unknown artist"; }

	// ---- the groups ---------------------------------------------------------------------------------
	void build ()
	{
		free_groups ();
		// albums: sort the songs by (artist, album, disc, track, title)
		int *o = (int *) malloc (sizeof (int) * (n ? n : 1));
		for (int i = 0; i < n; i++) o[i] = i;
		sort_idx (o, n, [] (const void *L_, int a, int b) -> int {
			const Library *L = (const Library *) L_;
			const Song &x = L->s[a], &y = L->s[b];
			int c = strcasecmp (x.album, y.album); if (c) return c;
			c = strcasecmp (L->artist_of (x), L->artist_of (y)); if (c) return c;
			if (x.disc != y.disc) return x.disc - y.disc;
			if (x.track != y.track) return x.track - y.track;
			return strcasecmp (x.title, y.title); }, this);
		al = (Album *) calloc ((size_t) (n ? n : 1), sizeof (Album)); nal = 0;
		for (int i = 0; i < n; )
		{
			int j = i;
			const Song &f = s[o[i]];
			while (j < n && !strcasecmp (s[o[j]].album, f.album) && !strcasecmp (artist_of (s[o[j]]), artist_of (f))) j++;
			Album &a = al[nal];
			a.title = sdup (f.album[0] ? f.album : "Unknown album"); a.artist = sdup (artist_of (f));
			a.songs = (int *) malloc (sizeof (int) * (j - i)); a.n = 0; a.cover = -1; a.genre = sdup (f.genre);
			for (int k = i; k < j; k++)
			{
				Song &x = s[o[k]]; x.alb = nal; a.songs[a.n++] = o[k]; a.durMs += x.durMs;
				if (x.year > a.year) a.year = x.year;
				if (x.added > a.added) a.added = x.added;
				if (a.cover < 0 && (x.coverLen || (x.folderCover && x.folderCover[0]))) a.cover = o[k];
			}
			nal++; i = j;
		}
		free (o);
		group (&ar, &nar, 0); group (&ge, &nge, 1); group (&fo, &nfo, 2);
	}
	// the albums in an order: 0 name, 1 artist, 2 year (newest), 3 recently added
	void album_order (int *out, int how) const
	{
		for (int i = 0; i < nal; i++) out[i] = i;
		struct C { const Library *L; int how; } c = { this, how };
		sort_idx (out, nal, [] (const void *c_, int a, int b) -> int {
			const C *cc = (const C *) c_;
			const Album &x = cc->L->al[a], &y = cc->L->al[b];
			int r = 0;
			if (cc->how == 1) r = name_cmp (x.artist, y.artist);
			else if (cc->how == 2) r = y.year - x.year;
			else if (cc->how == 3) r = y.added > x.added ? 1 : y.added < x.added ? -1 : 0;
			return r ? r : name_cmp (x.title, y.title); }, &c);
	}

	// ---- the file -------------------------------------------------------------------------------------
	bool load ()
	{
		void *f = kapi_open (LIB_FILE);
		if (!f) return false;
		u64 sz = kapi_fsize64 (f);
		if (sz == 0 || sz > (64u << 20)) { kapi_close (f); return false; }
		char *b = (char *) malloc ((size_t) sz + 1);
		int got = kapi_read (f, b, (unsigned) sz); kapi_close (f);
		if (got <= 0) { free (b); return false; }
		b[got] = 0;
		char *p = b;
		if (strncmp (p, "#onyx-media 1", 13)) { free (b); return false; }
		while (*p)
		{
			char *e = strchr (p, '\n'); if (e) *e = 0;
			if (*p && *p != '#')
			{
				char *fl[16]; int k = 0; char *q = p;
				while (k < 16) { fl[k++] = q; char *t = strchr (q, '\t'); if (!t) break; *t = 0; q = t + 1; }
				if (k == 16)
				{
					Song &x = add ();
					x.path = sdup (fl[0]); x.size = (unsigned) strtoul (fl[1], 0, 10); x.durMs = atoi (fl[2]);
					x.year = atoi (fl[3]); x.track = atoi (fl[4]); x.disc = atoi (fl[5]); x.fmt = atoi (fl[6]);
					x.coverOff = strtoull (fl[7], 0, 10); x.coverLen = (unsigned) strtoul (fl[8], 0, 10);
					x.folderCover = sdup (fl[9]); x.added = strtoull (fl[10], 0, 10);
					x.title = sdup (fl[11]); x.artist = sdup (fl[12]); x.albumArtist = sdup (fl[13]); x.album = sdup (fl[14]); x.genre = sdup (fl[15]);
				}
			}
			if (!e) break;
			p = e + 1;
		}
		free (b);
		return true;
	}
	bool save () const
	{
		kapi_mkdir (LIB_DIR);
		size_t cap_ = 64 + (size_t) n * 160, len = 0;
		for (int i = 0; i < n; i++) { const Song &x = s[i]; cap_ += strlen (x.path) + strlen (x.title) + strlen (x.artist) + strlen (x.albumArtist) + strlen (x.album) + strlen (x.genre) + strlen (x.folderCover); }
		char *b = (char *) malloc (cap_);
		len += (size_t) snprintf (b, cap_, "#onyx-media 1\n# path size dur year track disc fmt coverOff coverLen folderCover added title artist albumArtist album genre\n");
		for (int i = 0; i < n; i++)
		{
			const Song &x = s[i];
			len += (size_t) snprintf (b + len, cap_ - len, "%s\t%u\t%d\t%d\t%d\t%d\t%d\t%llu\t%u\t%s\t%llu\t%s\t%s\t%s\t%s\t%s\n", x.path, x.size, x.durMs, x.year, x.track,
						  x.disc, x.fmt, (unsigned long long) x.coverOff, x.coverLen, x.folderCover, x.added, x.title, x.artist, x.albumArtist, x.album, x.genre);
		}
		bool ok = kapi_save_file (LIB_FILE, b, (unsigned) len) == (int) len;
		free (b);
		return ok;
	}
	// plays, favourites: kept apart (they change often)
	void load_stats ()
	{
		void *f = kapi_open (STATS_FILE); if (!f) return;
		u64 sz = kapi_fsize64 (f); if (sz == 0 || sz > (16u << 20)) { kapi_close (f); return; }
		char *b = (char *) malloc ((size_t) sz + 1); int got = kapi_read (f, b, (unsigned) sz); kapi_close (f);
		if (got <= 0) { free (b); return; }
		b[got] = 0;
		index_paths ();
		for (char *p = b; *p; )
		{
			char *e = strchr (p, '\n'); if (e) *e = 0;
			char *t1 = strchr (p, '\t');
			if (t1 && *p != '#')
			{
				*t1 = 0; int i = find (p);
				if (i >= 0) { char *q = t1 + 1; s[i].plays = (int) strtol (q, &q, 10); if (*q) q++; s[i].lastPlayed = strtoull (q, &q, 10); if (*q) q++; s[i].fav = atoi (q) != 0; }
			}
			if (!e) break;
			p = e + 1;
		}
		free (b);
	}
	void save_stats () const
	{
		kapi_mkdir (LIB_DIR);
		size_t cap_ = 64, len = 0;
		for (int i = 0; i < n; i++) if (s[i].plays || s[i].fav) cap_ += strlen (s[i].path) + 48;
		char *b = (char *) malloc (cap_);
		len += (size_t) snprintf (b, cap_, "# path plays lastPlayed favourite\n");
		for (int i = 0; i < n; i++)
			if (s[i].plays || s[i].fav) len += (size_t) snprintf (b + len, cap_ - len, "%s\t%d\t%llu\t%d\n", s[i].path, s[i].plays, s[i].lastPlayed, s[i].fav ? 1 : 0);
		kapi_save_file (STATS_FILE, b, (unsigned) len);
		free (b);
	}

private:
	int *order = 0;
	void free_groups ()
	{
		for (int i = 0; i < nal; i++) { free (al[i].title); free (al[i].artist); free (al[i].songs); free (al[i].genre); }
		free (al); al = 0; nal = 0;
		Group **gs[3] = { &ar, &ge, &fo }; int *ns[3] = { &nar, &nge, &nfo };
		for (int g = 0; g < 3; g++) { for (int i = 0; i < *ns[g]; i++) { free ((*gs[g])[i].name); free ((*gs[g])[i].songs); free ((*gs[g])[i].albums); } free (*gs[g]); *gs[g] = 0; *ns[g] = 0; }
		free (order); order = 0;
	}
	// kind 0 artists, 1 genres, 2 folders
	static const char *key_of (const Library *L, const Song &x, int kind, char *buf, int cap)
	{
		if (kind == 0) return L->artist_of (x);
		if (kind == 1) return x.genre && x.genre[0] ? x.genre : "Unknown genre";
		scopy (buf, x.path, cap); char *sl = strrchr (buf, '/'); if (sl) *sl = 0; return buf;
	}
	void group (Group **out, int *nout, int kind)
	{
		int *o = (int *) malloc (sizeof (int) * (n ? n : 1));
		for (int i = 0; i < n; i++) o[i] = i;
		struct C { const Library *L; int kind; } cx = { this, kind };
		sort_idx (o, n, [] (const void *c_, int a, int b) -> int {
			const C *cc = (const C *) c_;
			char ba[300], bb[300];
			const Song &x = cc->L->s[a], &y = cc->L->s[b];
			int c = cc->kind == 2 ? strcmp (key_of (cc->L, x, 2, ba, sizeof ba), key_of (cc->L, y, 2, bb, sizeof bb))
					      : name_cmp (key_of (cc->L, x, cc->kind, ba, sizeof ba), key_of (cc->L, y, cc->kind, bb, sizeof bb));
			if (c) return c;
			if (x.alb != y.alb) return x.alb - y.alb;
			return x.track - y.track; }, &cx);
		Group *g = (Group *) calloc ((size_t) (n ? n : 1), sizeof (Group)); int ng = 0;
		char k1[300], k2[300];
		for (int i = 0; i < n; )
		{
			const char *key = key_of (this, s[o[i]], kind, k1, sizeof k1);
			int j = i;
			while (j < n && !(kind == 2 ? strcmp (key_of (this, s[o[j]], kind, k2, sizeof k2), key) : strcasecmp (key_of (this, s[o[j]], kind, k2, sizeof k2), key))) j++;
			Group &G = g[ng];
			G.name = sdup (key); G.songs = (int *) malloc (sizeof (int) * (j - i)); G.albums = (int *) malloc (sizeof (int) * (j - i));
			for (int k = i; k < j; k++)
			{
				Song &x = s[o[k]]; G.songs[G.n++] = o[k]; G.durMs += x.durMs;
				if (kind == 0) x.art = ng; else if (kind == 1) x.gen = ng; else x.fold = ng;
				bool have = false; for (int a = 0; a < G.na; a++) if (G.albums[a] == x.alb) have = true;
				if (!have && x.alb >= 0) G.albums[G.na++] = x.alb;
			}
			ng++; i = j;
		}
		free (o);
		*out = g; *nout = ng;
	}
};

// ---- the scan --------------------------------------------------------------------------------------------
struct ScanState
{
	volatile int found, read, done;		// songs found, read now (not known), the end
	char current[160];			// the folder being walked
	char roots[8][200]; int nroots;
	Library *old, *fresh;			// what is known; what the scan makes
	volatile bool cancel;
};
static bool is_cover_name (const char *n)
{
	static const char *const N[] = { "cover", "folder", "front", "album", "albumart", "albumartsmall", 0 };
	char b[64]; int k = 0;
	for (const char *p = n; *p && *p != '.' && k < 63; p++) b[k++] = (char) (*p >= 'A' && *p <= 'Z' ? *p + 32 : *p);
	b[k] = 0;
	const char *e = ext_of (n);
	if (strcasecmp (e, "jpg") && strcasecmp (e, "jpeg") && strcasecmp (e, "png") && strcasecmp (e, "bmp")) return false;
	for (int i = 0; N[i]; i++) if (!strcmp (b, N[i])) return true;
	return false;
}
static void scan_dir (ScanState *st, const char *dir, int depth)
{
	if (depth > 12 || st->cancel) return;
	scopy (st->current, dir, sizeof st->current);
	void *d = kapi_opendir (dir);
	if (!d) return;
	struct Ent { char name[128]; unsigned size; int dir; };
	int cap = 64, ne = 0; Ent *es = (Ent *) malloc (sizeof (Ent) * cap);
	struct kapi_dirent e; char cover[300] = "";
	while (kapi_readdir (d, &e) > 0)
	{
		if (e.name[0] == '.') continue;
		if (ne == cap) { cap *= 2; es = (Ent *) realloc (es, sizeof (Ent) * cap); }
		scopy (es[ne].name, e.name, sizeof es[ne].name); es[ne].size = e.size; es[ne].dir = e.is_dir; ne++;
		if (!e.is_dir && !cover[0] && is_cover_name (e.name)) snprintf (cover, sizeof cover, "%s/%s", dir, e.name);
	}
	kapi_closedir (d);
	for (int i = 0; i < ne && !st->cancel; i++)
	{
		char p[300]; snprintf (p, sizeof p, "%s/%s", dir, es[i].name);
		if (es[i].dir) { if (strcasecmp (es[i].name, "Playlists")) scan_dir (st, p, depth + 1); continue; }
		if (format_of (p) < 0) continue;
		st->found++;
		int k = st->old ? st->old->find (p) : -1;
		Song &x = st->fresh->add ();
		if (k >= 0 && st->old->s[k].size == es[i].size)
		{
			const Song &o = st->old->s[k];
			x = o;
			x.path = sdup (o.path); x.title = sdup (o.title); x.artist = sdup (o.artist); x.albumArtist = sdup (o.albumArtist);
			x.album = sdup (o.album); x.genre = sdup (o.genre); x.folderCover = sdup (cover);
			x.alb = x.art = x.gen = x.fold = -1;
		}
		else
		{
			Tags t;
			if (!read_tags (p, &t)) { st->fresh->n--; continue; }
			st->read++;
			x.path = sdup (p); x.title = sdup (t.title); x.artist = sdup (t.artist); x.albumArtist = sdup (t.albumArtist);
			x.album = sdup (t.album); x.genre = sdup (t.genre); x.folderCover = sdup (cover);
			x.year = t.year; x.track = t.track; x.disc = t.disc; x.durMs = t.durMs; x.fmt = t.fmt; x.size = es[i].size;
			x.coverOff = t.coverOff; x.coverLen = t.coverLen; x.added = now_stamp ();
		}
	}
	free (es);
}
static int scan_thread (void *p)
{
	ScanState *st = (ScanState *) p;		// (st->old: indexed by the window before -- index_paths -- read only here)
	for (int r = 0; r < st->nroots && !st->cancel; r++) scan_dir (st, st->roots[r], 0);
	st->done = 1;
	return 0;
}

} // namespace media

#endif
