//
// Apps/media/main.cpp -- Media Player, Onyx's music library (docs/media/README.md: the mock-ups and the user's
// decisions), in the way of Windows Media Player / iTunes: the songs of the folders it watches (SD:/Music),
// MP3, OGG, FLAC, WAV and MIDI, by artists, albums, songs, genres, folders and playlists; a now-playing
// view (a MIDI file: its notes scrolling as coloured lines); a mini player at the bottom right of the screen.
// Closing the window stops the music. Tags are read, never written; covers come from the files and their
// folders, nothing is downloaded. The videos (films, clips, episodes: MP4, MKV, WebM, AVI, MPEG, WMV...) are
// played by Onyx's media library (user/av, with FFmpeg's decoders and demuxers), in the window or full screen,
// each resumed where it was left.
//
// This file is MIT (Onyx's own code); the Media Player as a program links FFmpeg (GPL-2.0-or-later) and is
// distributed under the GPL-2.0 (docs/LICENSING.md).
//
// A newlib uikit app with FreeType's text (user/Makefile's media.elf rule): the decoders (codecs.c,
// vorbis.c), MeltySynth (Apps/koton/synth) for MIDI, user/av and its codecs for the videos. Its parts:
// decode.h, midi.h, player.h (the music's playback thread), tags.h, lib.h (the library, its scan thread),
// covers.h (their loader thread), videos.h (the videos' facts), thumbs.h (their frames' thread), watch.h
// (a video playing), ui.h. Its files: SD:/etc/media/settings.ini, library.tsv, stats.tsv, videos.tsv,
// thumbs/*.jpg; SD:/Music/Playlists/*.m3u.
//
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include "appkit/appkit.h"
#include "player.h"
#include "ui.h"
#include "thumbs.h"
#include "watch.h"
#include "fileassoc.h"
#include "notify.h"

using namespace uikit;
using namespace media;

#define SETTINGS "SD:/etc/media/settings.ini"

// ---- the state -------------------------------------------------------------------------------------------
static Library *L;				// the library shown
static Covers g_covers;
static Player g_player;
static VideoLib *VL;				// the videos
static Thumbs g_thumbs;
static VideoPlay g_vp;				// the video open (in the window or full screen)
static int g_vidx = -1;				// its index in V, -1 none
static unsigned g_vpMoveT, g_vpSaveT;		// (the pointer moved last: the controls shown; the position saved)
static bool g_vpEnded;
static ScanState *g_scan; static int g_scanTid = -1;
static char g_folders[8][200]; static int g_nfolders;
static int g_volume = 80; static bool g_muted, g_shuffle; static int g_repeat;	// 0 off, 1 all, 2 one
static int g_albumSort = 3; static bool g_albumList;

// songs that are not in the library (a file opened from the File Viewer): ids -1, -2...
static Song g_ext[16]; static int g_next;
static const Song *song (int id) { if (id >= 0) return L && id < L->n ? &L->s[id] : 0; int k = -id - 1; return k < g_next ? &g_ext[k] : 0; }

// the queue: what plays, in its order (shuffled or not); g_qorig: the order it was made in
static IntList g_queue, g_qorig; static int g_qpos = -1;
static int g_playing = -1;			// the song playing / paused (an id), -1 none
static unsigned g_startT; static bool g_counted;	// (a play counted once it has been heard enough)

// the playlists (SD:/Music/Playlists/*.m3u): their songs as paths (the library can be scanned again)
struct Plist { char name[96]; char path[300]; char **paths; int n; };
static Plist *g_pl; static int g_npl;

// ---- the pages -------------------------------------------------------------------------------------------
enum { P_HOME, P_ALBUMS, P_ALBUM, P_ARTISTS, P_ARTIST, P_SONGS, P_GENRES, P_GENRE, P_FOLDERS, P_FOLDER, P_PLAYLIST, P_NOW, P_SEARCH, P_WELCOME,
       P_VIDEOS, P_WATCH };				// P_VIDEOS: arg a kind (VK_*, -1 all); P_WATCH: arg the video
enum { PL_FAV = -1, PL_RECENT = -2 };
struct Page { int kind, arg, scroll, sortCol; bool sortDesc; };
static Page g_hist[64]; static int g_nhist, g_hpos;	// back / forward
static Page &page () { return g_hist[g_hpos]; }
static char g_search[128];

class Sidebar; class TopBar; class Content; class NowBar; class MiniView; class WatchView;
static Root *g_root;
static Sidebar *g_side; static TopBar *g_top; static Content *g_content; static NowBar *g_now; static MiniView *g_mini; static WatchView *g_watch;
static bool g_miniMode; static int g_restore[4];	// the window before the mini player (x, y, w, h)
static unsigned g_tick;

static void navigate (int kind, int arg = 0);
static void play_list (const IntList &ids, int start, bool shuffle);
static void refresh_all ();
static void save_settings ();
static MidiSong *roll_for (int id);
static void video_close ();
static void go_back_page ();

// ---- settings -------------------------------------------------------------------------------------------
static void load_settings ()
{
	g_nfolders = 0;
	void *f = kapi_open (SETTINGS);
	if (f)
	{
		char b[2048]; int n = kapi_read (f, b, sizeof b - 1); kapi_close (f);
		b[n > 0 ? n : 0] = 0;
		for (char *p = b; *p; )
		{
			char *e = strchr (p, '\n'); if (e) *e = 0;
			char *eq = strchr (p, '=');
			if (eq && *p != '#')
			{
				char *k = p, *v = eq + 1; *eq = 0;
				while (*v == ' ') v++;
				int kl = (int) strlen (k); while (kl && k[kl - 1] == ' ') k[--kl] = 0;
				int vl = (int) strlen (v); while (vl && (v[vl - 1] == ' ' || v[vl - 1] == '\r')) v[--vl] = 0;
				if (!strcmp (k, "folder") && g_nfolders < 8 && v[0]) scopy (g_folders[g_nfolders++], v, 200);
				else if (!strcmp (k, "volume")) g_volume = atoi (v);
				else if (!strcmp (k, "shuffle")) g_shuffle = atoi (v) != 0;
				else if (!strcmp (k, "repeat")) g_repeat = atoi (v) % 3;
				else if (!strcmp (k, "soundfont")) scopy (g_sfPath, v, sizeof g_sfPath);
				else if (!strcmp (k, "album_sort")) g_albumSort = atoi (v) & 3;
				else if (!strcmp (k, "album_list")) g_albumList = atoi (v) != 0;
			}
			if (!e) break;
			p = e + 1;
		}
	}
	if (!f) { scopy (g_folders[0], "SD:/Music", 200); g_nfolders = 1; }
	if (g_volume < 0 || g_volume > 100) g_volume = 80;
}
static void save_settings ()
{
	kapi_mkdir (LIB_DIR);
	char b[2048]; int n = snprintf (b, sizeof b, "# Media Player's settings (the app writes them)\n");
	for (int i = 0; i < g_nfolders; i++) n += snprintf (b + n, sizeof b - n, "folder = %s\n", g_folders[i]);
	n += snprintf (b + n, sizeof b - n, "volume = %d\nshuffle = %d\nrepeat = %d\nsoundfont = %s\nalbum_sort = %d\nalbum_list = %d\n",
		       g_volume, g_shuffle ? 1 : 0, g_repeat, g_sfPath, g_albumSort, g_albumList ? 1 : 0);
	kapi_save_file (SETTINGS, b, (unsigned) n);
}

// ---- playlists --------------------------------------------------------------------------------------------
static void playlists_free () { for (int i = 0; i < g_npl; i++) { for (int k = 0; k < g_pl[i].n; k++) free (g_pl[i].paths[k]); free (g_pl[i].paths); } free (g_pl); g_pl = 0; g_npl = 0; }
static void playlist_read (Plist &p)
{
	p.paths = 0; p.n = 0;
	void *f = kapi_open (p.path); if (!f) return;
	u64 sz = kapi_fsize64 (f); if (sz > (2u << 20)) { kapi_close (f); return; }
	char *b = (char *) malloc ((size_t) sz + 1); int got = kapi_read (f, b, (unsigned) sz); kapi_close (f);
	b[got > 0 ? got : 0] = 0;
	int cap = 0;
	for (char *q = b; *q; )
	{
		char *e = strchr (q, '\n'); if (e) *e = 0;
		int l = (int) strlen (q); while (l && (q[l - 1] == '\r' || q[l - 1] == ' ')) q[--l] = 0;
		if (q[0] && q[0] != '#')
		{
			char full[300];
			if (strchr (q, ':')) scopy (full, q, sizeof full); else snprintf (full, sizeof full, "%s/%s", PLAYLIST_DIR, q);
			for (char *c = full; *c; c++) if (*c == '\\') *c = '/';
			if (p.n == cap) { cap = cap ? cap * 2 : 32; p.paths = (char **) realloc (p.paths, sizeof (char *) * cap); }
			p.paths[p.n++] = sdup (full);
		}
		if (!e) break;
		q = e + 1;
	}
	free (b);
}
static void playlists_load ()
{
	playlists_free ();
	void *d = kapi_opendir (PLAYLIST_DIR);
	if (!d) return;
	struct kapi_dirent e; int cap = 0;
	while (kapi_readdir (d, &e) > 0)
	{
		if (e.is_dir || strcasecmp (ext_of (e.name), "m3u") && strcasecmp (ext_of (e.name), "m3u8")) continue;
		if (g_npl == cap) { cap = cap ? cap * 2 : 8; g_pl = (Plist *) realloc (g_pl, sizeof (Plist) * cap); }
		Plist &p = g_pl[g_npl++];
		scopy (p.name, e.name, sizeof p.name); char *dot = strrchr (p.name, '.'); if (dot) *dot = 0;
		snprintf (p.path, sizeof p.path, "%s/%s", PLAYLIST_DIR, e.name);
		playlist_read (p);
	}
	kapi_closedir (d);
}
static void playlist_save (Plist &p)
{
	kapi_mkdir ("SD:/Music"); kapi_mkdir (PLAYLIST_DIR);
	size_t cap = 64; for (int i = 0; i < p.n; i++) cap += strlen (p.paths[i]) + 160;
	char *b = (char *) malloc (cap); size_t n = (size_t) snprintf (b, cap, "#EXTM3U\n");
	for (int i = 0; i < p.n; i++)
	{
		int id = L ? L->find (p.paths[i]) : -1;
		if (id >= 0) n += (size_t) snprintf (b + n, cap - n, "#EXTINF:%d,%s - %s\n", L->s[id].durMs / 1000, L->artist_of (L->s[id]), L->s[id].title);
		n += (size_t) snprintf (b + n, cap - n, "%s\n", p.paths[i]);
	}
	kapi_save_file (p.path, b, (unsigned) n);
	free (b);
}
static int playlist_new (const char *name)
{
	g_pl = (Plist *) realloc (g_pl, sizeof (Plist) * (g_npl + 1));
	Plist &p = g_pl[g_npl];
	scopy (p.name, name, sizeof p.name);
	char safe[96]; int k = 0; for (const char *c = name; *c && k < 90; c++) safe[k++] = strchr ("\\/:*?\"<>|", *c) ? '_' : *c; safe[k] = 0;
	snprintf (p.path, sizeof p.path, "%s/%s.m3u", PLAYLIST_DIR, safe);
	p.paths = 0; p.n = 0;
	playlist_save (p);
	return g_npl++;
}
static void playlist_add (int pl, const IntList &ids)
{
	Plist &p = g_pl[pl];
	p.paths = (char **) realloc (p.paths, sizeof (char *) * (p.n + ids.n + 1));
	for (int i = 0; i < ids.n; i++) { const Song *s = song (ids.v[i]); if (s) p.paths[p.n++] = sdup (s->path); }
	playlist_save (p);
}

// ---- the songs of a page --------------------------------------------------------------------------------
static bool matches (const char *s, const char *q)
{
	if (!q[0]) return true;
	int ql = (int) strlen (q);
	for (const char *p = s; *p; p++) if (!strncasecmp (p, q, (size_t) ql)) return true;
	return false;
}
static void page_songs (const Page &pg, IntList &out)
{
	out.clear ();
	if (!L) return;
	switch (pg.kind)
	{
	case P_ALBUM: if (pg.arg >= 0 && pg.arg < L->nal) for (int i = 0; i < L->al[pg.arg].n; i++) out.push (L->al[pg.arg].songs[i]); return;
	case P_ARTIST: if (pg.arg >= 0 && pg.arg < L->nar) for (int i = 0; i < L->ar[pg.arg].n; i++) out.push (L->ar[pg.arg].songs[i]); return;
	case P_GENRE: if (pg.arg >= 0 && pg.arg < L->nge) for (int i = 0; i < L->ge[pg.arg].n; i++) out.push (L->ge[pg.arg].songs[i]); return;
	case P_FOLDER: if (pg.arg >= 0 && pg.arg < L->nfo) for (int i = 0; i < L->fo[pg.arg].n; i++) out.push (L->fo[pg.arg].songs[i]); return;
	case P_SONGS: for (int i = 0; i < L->nar; i++) for (int k = 0; k < L->ar[i].n; k++) out.push (L->ar[i].songs[k]); return;
	case P_SEARCH:
		for (int i = 0; i < L->nar; i++) for (int k = 0; k < L->ar[i].n; k++)
		{ const Song &x = L->s[L->ar[i].songs[k]]; if (matches (x.title, g_search) || matches (x.artist, g_search) || matches (x.album, g_search)) out.push (L->ar[i].songs[k]); }
		return;
	case P_PLAYLIST:
		if (pg.arg == PL_FAV) { for (int i = 0; i < L->nar; i++) for (int k = 0; k < L->ar[i].n; k++) if (L->s[L->ar[i].songs[k]].fav) out.push (L->ar[i].songs[k]); }
		else if (pg.arg == PL_RECENT)
		{
			for (int i = 0; i < L->n; i++) out.push (i);
			sort_idx (out.v, out.n, [] (const void *, int a, int b) -> int { const Song &x = L->s[a], &y = L->s[b];
				if (x.added != y.added) return y.added > x.added ? 1 : -1; return strcmp (x.path, y.path); }, 0);
			if (out.n > 100) out.n = 100;
		}
		else if (pg.arg >= 0 && pg.arg < g_npl) for (int i = 0; i < g_pl[pg.arg].n; i++) { int id = L->find (g_pl[pg.arg].paths[i]); if (id >= 0) out.push (id); }
		return;
	}
}
// sorted by a column: 1 title, 2 artist, 3 album, 4 time, 5 format
static void sort_songs (IntList &l, int col, bool desc)
{
	if (!col) return;
	struct C { int col; bool desc; } c = { col, desc };
	sort_idx (l.v, l.n, [] (const void *c_, int a, int b) -> int {
		const C *cc = (const C *) c_; const Song &x = L->s[a], &y = L->s[b]; int r = 0;
		switch (cc->col)
		{
		case 1: r = name_cmp (x.title, y.title); break;
		case 2: r = name_cmp (L->artist_of (x), L->artist_of (y)); if (!r) r = name_cmp (x.album, y.album); if (!r) r = x.track - y.track; break;
		case 3: r = name_cmp (x.album, y.album); if (!r) r = x.track - y.track; break;
		case 4: r = x.durMs - y.durMs; break;
		case 5: r = x.fmt - y.fmt; break;
		}
		return cc->desc ? -r : r; }, &c);
}

// ---- playing -------------------------------------------------------------------------------------------------
static void count_play ()
{
	if (g_counted || g_playing < 0) return;
	g_counted = true;
	if (g_playing >= 0 && L) { L->s[g_playing].plays++; L->s[g_playing].lastPlayed = now_stamp (); L->save_stats (); }
}
static void start_song (int qpos)
{
	if (page ().kind == P_WATCH) go_back_page ();		// (the music takes the sound back from the video)
	video_close ();
	if (qpos < 0 || qpos >= g_queue.n) { g_player.stop (); g_playing = -1; g_qpos = -1; refresh_all (); return; }
	g_qpos = qpos; g_playing = g_queue[qpos];
	const Song *s = song (g_playing);
	if (!s) { g_player.stop (); g_playing = -1; refresh_all (); return; }
	g_player.volume = g_muted ? 0 : g_volume;
	g_player.play (s->path);
	g_startT = kapi_get_ticks (); g_counted = false;
	refresh_all ();
}
static void shuffle_from (int keepFirst)
{	// the queue after its first `keepFirst` songs shuffled
	unsigned r = kapi_get_ticks () * 2654435761u + 7;
	for (int i = g_queue.n - 1; i > keepFirst; i--)
	{
		r = r * 1103515245u + 12345u;
		int j = keepFirst + (int) ((r >> 8) % (unsigned) (i - keepFirst + 1));
		int t = g_queue[i]; g_queue[i] = g_queue[j]; g_queue[j] = t;
	}
}
static void play_list (const IntList &ids, int start, bool shuffle)
{
	if (!ids.n) return;
	count_play ();
	g_qorig.copy (ids);
	g_queue.copy (ids);
	if (shuffle) g_shuffle = true;
	int first = start >= 0 && start < ids.n ? start : 0;
	if (g_shuffle)
	{
		if (start < 0) { unsigned r = kapi_get_ticks () * 2654435761u; first = (int) (r % (unsigned) ids.n); }
		int t = g_queue[0]; g_queue[0] = g_queue[first]; g_queue[first] = t;
		shuffle_from (0);
		first = 0;
	}
	start_song (first);
}
static void play_next (bool user)
{
	count_play ();
	if (!g_queue.n) return;
	if (!user && g_repeat == 2) { start_song (g_qpos); return; }
	int n = g_qpos + 1;
	if (n >= g_queue.n) { if (g_repeat == 1 || user) n = 0; else { g_player.stop (); g_playing = -1; refresh_all (); return; } }
	start_song (n);
}
static void play_prev ()
{
	if (g_player.posMs > 3000 || g_qpos <= 0) { g_player.seek (0); return; }
	count_play ();
	start_song (g_qpos - 1);
}
static void video_toggle ();
static void toggle_play ()
{
	if (page ().kind == P_WATCH && g_vp.active ()) { video_toggle (); return; }
	if (g_player.state == PS_PLAYING) g_player.pause ();
	else if (g_player.state == PS_PAUSED) g_player.resume ();
	else if (g_queue.n) start_song (g_qpos >= 0 ? g_qpos : 0);
	else if (L && L->n) { IntList all; page_songs (Page { P_SONGS, 0, 0, 0, false }, all); play_list (all, -1, g_shuffle); }
}
static void set_shuffle (bool on)
{
	g_shuffle = on;
	if (g_queue.n)
	{
		int cur = g_playing;
		if (on) { int p = g_queue.find (cur); if (p > 0) { int t = g_queue[0]; g_queue[0] = g_queue[p]; g_queue[p] = t; } shuffle_from (0); g_qpos = 0; }
		else { g_queue.copy (g_qorig); g_qpos = g_queue.find (cur); }
	}
	save_settings ();
}
static void play_next_ids (const IntList &ids, bool next)
{
	if (!g_queue.n) { play_list (ids, 0, false); return; }
	int at = next ? g_qpos + 1 : g_queue.n;
	for (int i = 0; i < ids.n; i++) { g_queue.insert (at + i, ids.v[i]); g_qorig.push (ids.v[i]); }
}

// ---- the window's parts ------------------------------------------------------------------------------------
static const int SIDE_W = 208, TOP_H = 52, NOW_H = 80;
static unsigned col_side () { return uk_mix (C_BG, C_FIELD, 70); }
static unsigned col_dim () { return uk_mix (C_FIELD, C_FIELD_TEXT, 150); }
static unsigned col_dim_bg () { return uk_mix (C_BG, C_TEXT, 150); }

struct Hit { int x, y, w, h, kind, a, b; };
struct HitList
{
	Hit h[512]; int n;
	void clear () { n = 0; }
	void add (int x, int y, int w, int hh, int kind, int a = 0, int b = 0) { if (n < 512) h[n++] = Hit { x, y, w, hh, kind, a, b }; }
	const Hit *at (int x, int y) const { for (int i = n - 1; i >= 0; i--) if (x >= h[i].x && y >= h[i].y && x < h[i].x + h[i].w && y < h[i].y + h[i].h) return &h[i]; return 0; }
};

// ---- the sidebar ------------------------------------------------------------------------------------------------
enum { SB_PAGE = 1, SB_PLAYLIST, SB_NEWPL, SB_FOLDERS, SB_VIDEOS };
class Sidebar : public Widget
{
public:
	HitList hits; int hot;
	Sidebar (int l, int t, int w, int h) : Widget (l, t, w, h), hot (-1) {}
	unsigned bgColor () override { return col_side (); }
	bool selected (int kind, int arg)
	{
		int k = page ().kind, a = page ().arg;
		if (kind == SB_PLAYLIST) return k == P_PLAYLIST && a == arg;
		if (kind == SB_VIDEOS) return k == P_VIDEOS && a == arg;
		if (arg == P_ALBUMS) return k == P_ALBUMS || k == P_ALBUM;
		if (arg == P_ARTISTS) return k == P_ARTISTS || k == P_ARTIST;
		if (arg == P_GENRES) return k == P_GENRES || k == P_GENRE;
		if (arg == P_FOLDERS) return k == P_FOLDERS || k == P_FOLDER;
		return k == arg;
	}
	void item (int &y, const char *label, int ic, int kind, int arg)
	{
		bool on = selected (kind, arg), h = hits.n == hot;
		if (on) uk_rbox (canvas, 8, y, width - 16, 28, 6, C_ACCENT, C_ACCENT);
		else if (h) uk_rbox (canvas, 8, y, width - 16, 28, 6, uk_mix (col_side (), C_ACCENT, 40), uk_mix (col_side (), C_ACCENT, 40));
		unsigned ink = on ? C_SEL_TEXT : C_TEXT;
		icon (canvas, ic, 18, y + 6, 16, ic == I_HEART ? (on ? C_SEL_TEXT : 0xD8484E) : ink);
		text_v (canvas, 44, y, 28, label, ink, F_UI, on ? 2 : 0, width - 56);
		hits.add (8, y, width - 16, 28, kind, arg);
		y += 30;
	}
	void head (int &y, const char *s) { y += 8; text (canvas, 16, y, s, col_dim_bg (), F_SMALL, 2); y += 20; }
	void onDraw () override
	{
		canvas.clear (col_side ());
		canvas.fillRect (width - 1, 0, 1, height, uk_tone (C_BG, 100));
		hits.clear ();
		int y = 10;
		item (y, "Home", I_HOME, SB_PAGE, P_HOME);
		head (y, "LIBRARY");
		item (y, "Artists", I_PERSON, SB_PAGE, P_ARTISTS);
		item (y, "Albums", I_DISC, SB_PAGE, P_ALBUMS);
		item (y, "Songs", I_NOTE, SB_PAGE, P_SONGS);
		item (y, "Genres", I_TAG, SB_PAGE, P_GENRES);
		item (y, "Folders", I_FOLDER, SB_PAGE, P_FOLDERS);
		head (y, "VIDEOS");
		item (y, "Films", I_FILM, SB_VIDEOS, VK_FILM);
		item (y, "Clips and series", I_TV, SB_VIDEOS, VK_CLIP);
		head (y, "PLAYLISTS");
		item (y, "Favourites", I_HEART, SB_PLAYLIST, PL_FAV);
		item (y, "Recently added", I_CLOCK, SB_PLAYLIST, PL_RECENT);
		int bottom = height - 64;
		for (int i = 0; i < g_npl && y + 30 < bottom; i++) item (y, g_pl[i].name, I_LIST, SB_PLAYLIST, i);
		// at the bottom: a new playlist; the scan
		int by = height - 58;
		bool h = hits.n == hot;
		text_v (canvas, 18, by, 26, "+  New playlist", h ? C_ACCENT : uk_mix (C_TEXT, C_ACCENT, 200), F_UI);
		hits.add (8, by, width - 16, 26, SB_NEWPL);
		char st[96];
		if (g_scan && !g_scan->done) snprintf (st, sizeof st, "Looking for songs...  %d", g_scan->found + g_scan->vfound);
		else if (VL && VL->n) snprintf (st, sizeof st, "%d songs  \xC2\xB7  %d videos", L ? L->n : 0, VL->n);
		else snprintf (st, sizeof st, "%d songs", L ? L->n : 0);
		h = hits.n == hot;
		text_v (canvas, 18, by + 28, 22, st, h ? C_ACCENT : col_dim_bg (), F_SMALL, 0, width - 30);
		hits.add (8, by + 28, width - 16, 22, SB_FOLDERS);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		const Hit *ht = hits.at (mx, my);
		int nh = ht ? (int) (ht - hits.h) : -1;
		if (nh != hot) { hot = nh; invalidate (true); }
		if (bl && !pressed) pressed = true;
		else if (!bl && pressed)
		{
			pressed = false;
			if (!ht) return true;
			if (ht->kind == SB_PAGE) navigate (ht->a);
			else if (ht->kind == SB_PLAYLIST) navigate (P_PLAYLIST, ht->a);
			else if (ht->kind == SB_VIDEOS) navigate (P_VIDEOS, ht->a);
			else if (ht->kind == SB_NEWPL) { extern void new_playlist_dialog (const IntList *); new_playlist_dialog (0); }
			else if (ht->kind == SB_FOLDERS) navigate (P_WELCOME);
		}
		return mx >= 0 && my >= 0 && mx < width && my < height;
	}
};

// ---- the bar on top: back / forward, where we are, the search, grid or list ----------------------------------------
enum { TB_BACK = 1, TB_FWD, TB_CRUMB, TB_GRID, TB_LIST };
class TopBar : public Widget
{
public:
	HitList hits; int hot; Textbox *search;
	TopBar (int l, int t, int w, int h) : Widget (l, t, w, h), hot (-1)
	{
		search = new Textbox (w - 300, 12, 230, 28, "");
		search->anchor = ANCHOR_TOP | ANCHOR_RIGHT; search->maxLen = 100; search->padR = 4;
		addChild (search);
	}
	void round (int x, int y, int ic, bool on, bool h)
	{
		unsigned f = h && on ? uk_mix (C_BG, 0xFFFFFF, 140) : uk_mix (C_BG, 0xFFFFFF, 90);
		VPath c; c.circle (V (x + 15), V (y + 15), V (15)); c.fill (canvas, f);
		VPath o; o.arc (V (x + 15), V (y + 15), V (15), 0, 360, 14); o.fill (canvas, uk_tone (C_BG, 96));
		icon (canvas, ic, x + 8, y + 8, 14, on ? C_TEXT : uk_mix (C_BG, C_TEXT, 90));
	}
	// Where we are, as the File Viewer's path bar: an entry-like field, the levels in it as links -- the page
	// shown in the accent, underlined; the one pointed at underlined.
	void crumbs (int bx, int by, int bw, int bh)
	{
		const char *a = 0, *b = 0; int ak = -1;
		const Page &p = page ();
		static char t[160];
		switch (p.kind)
		{
		case P_HOME: a = "Home"; break;
		case P_ALBUMS: a = "Music"; b = "Albums"; break;
		case P_ALBUM: a = "Albums"; ak = P_ALBUMS; b = L && p.arg < L->nal ? L->al[p.arg].title : ""; break;
		case P_ARTISTS: a = "Music"; b = "Artists"; break;
		case P_ARTIST: a = "Artists"; ak = P_ARTISTS; b = L && p.arg < L->nar ? L->ar[p.arg].name : ""; break;
		case P_SONGS: a = "Music"; b = "Songs"; break;
		case P_GENRES: a = "Music"; b = "Genres"; break;
		case P_GENRE: a = "Genres"; ak = P_GENRES; b = L && p.arg < L->nge ? L->ge[p.arg].name : ""; break;
		case P_FOLDERS: a = "Music"; b = "Folders"; break;
		case P_FOLDER: a = "Folders"; ak = P_FOLDERS; b = L && p.arg < L->nfo ? L->fo[p.arg].name : ""; break;
		case P_PLAYLIST: a = "Playlists"; b = p.arg == PL_FAV ? "Favourites" : p.arg == PL_RECENT ? "Recently added" : p.arg < g_npl ? g_pl[p.arg].name : ""; break;
		case P_NOW: a = "Now playing"; break;
		case P_SEARCH: snprintf (t, sizeof t, "Search: \xE2\x80\x9C%s\xE2\x80\x9D", g_search); a = t; break;
		case P_WELCOME: a = "Music"; b = "Folders to watch"; break;
		case P_VIDEOS: a = "Videos"; b = p.arg == VK_FILM ? "Films" : p.arg == VK_CLIP ? "Clips and series" : 0; if (b) ak = P_VIDEOS; break;
		case P_WATCH: a = "Videos"; b = VL && p.arg >= 0 && p.arg < VL->n ? VL->v[p.arg].title : ""; break;
		}
		unsigned field = uk_mix (C_BG, C_FIELD, 170), ink = uk_ink_on (field), dim = uk_mix (field, ink, 120);
		uk_rbox (canvas, bx, by, bw, bh, 8, uk_tone (field, 136), field);
		uk_rline (canvas, bx, by, bw, bh, 8, uk_tone (C_BG, 88), 190);
		const char *seg[2] = { a, b };
		int n = b ? 2 : 1, x = bx + 14, y = by + (bh - fh ()) / 2, right = bx + bw - 14;
		for (int i = 0; i < n; i++)
		{
			if (i) { uk_glyph (canvas, WKG_CHEV_RIGHT, x + 3, by + bh / 2, 9, dim); x += 18; }
			bool cur = i == n - 1, link = !cur && ak >= 0, h = link && hits.n == hot;
			int w = tw (seg[i], F_UI, cur ? 2 : 0);
			if (w > right - x) w = right - x;
			if (w <= 0) break;
			text (canvas, x, y, seg[i], cur ? uk_tone (C_ACCENT, 84) : ink, F_UI, cur ? 2 : 0, w);
			if (cur) canvas.fillRect (x, y + fh () + 1, w, 2, C_ACCENT);
			else if (h) canvas.fillRect (x, y + fh () + 1, w, 1, ink);
			if (link) hits.add (x, by, w, bh, TB_CRUMB, ak);
			x += w + 10;
		}
	}
	void onDraw () override
	{
		canvas.clear (C_BG);
		canvas.fillRect (0, height - 1, width, 1, uk_tone (C_BG, 100));
		hits.clear ();
		bool canB = g_hpos > 0, canF = g_hpos < g_nhist - 1;
		round (12, 11, I_BACK, canB, hot == hits.n); hits.add (12, 11, 30, 30, TB_BACK);
		round (48, 11, I_FWD, canF, hot == hits.n); hits.add (48, 11, 30, 30, TB_FWD);
		crumbs (92, 10, search->left - 12 - 92, 32);
		// the search's magnifier, inside the field's left (the field draws over the rest)
		int vx = width - 58;
		bool grid = page ().kind == P_ALBUMS;
		if (grid)
		{
			uk_rbox (canvas, vx, 12, 50, 28, 6, C_FIELD, C_FIELD); uk_rline (canvas, vx, 12, 50, 28, 6, uk_tone (C_BG, 96));
			if (!g_albumList) uk_rbox (canvas, vx + 2, 14, 23, 24, 5, C_ACCENT, C_ACCENT); else uk_rbox (canvas, vx + 25, 14, 23, 24, 5, C_ACCENT, C_ACCENT);
			icon (canvas, I_GRID, vx + 6, 18, 15, !g_albumList ? C_SEL_TEXT : C_FIELD_TEXT); hits.add (vx, 12, 25, 28, TB_GRID);
			icon (canvas, I_ROWS, vx + 29, 18, 15, g_albumList ? C_SEL_TEXT : C_FIELD_TEXT); hits.add (vx + 25, 12, 25, 28, TB_LIST);
		}
		if (!search->text[0] && !search->hasFocus)
		{	// the placeholder (drawn by the field's parent: the field is see-through when empty? no: drawn after)
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		const Hit *ht = hits.at (mx, my);
		int nh = ht ? (int) (ht - hits.h) : -1;
		if (nh != hot) { hot = nh; invalidate (true); }
		if (bl && !pressed) pressed = true;
		else if (!bl && pressed)
		{
			pressed = false;
			if (!ht) return false;
			extern void go_back (); extern void go_fwd ();
			if (ht->kind == TB_BACK) go_back ();
			else if (ht->kind == TB_FWD) go_fwd ();
			else if (ht->kind == TB_CRUMB) navigate (ht->a, ht->a == P_VIDEOS ? -1 : 0);
			else if (ht->kind == TB_GRID) { g_albumList = false; save_settings (); refresh_all (); }
			else if (ht->kind == TB_LIST) { g_albumList = true; save_settings (); refresh_all (); }
		}
		return ht != 0;
	}
};
// the search field with its placeholder and magnifier
class SearchBox : public Textbox
{
public:
	SearchBox (int l, int t, int w, int h) : Textbox (l, t, w, h, "", 0) { maxLen = 100; padR = 22; }
	void onDraw () override
	{
		Textbox::onDraw ();
		if (!text[0] && !hasFocus) text_v (canvas, 10, 0, height, "Search the library", uk_mix (C_FIELD, C_FIELD_TEXT, 110));
		icon (canvas, I_SEARCH, width - 22, (height - 14) / 2, 14, uk_mix (C_FIELD, C_FIELD_TEXT, 120));
	}
};

// ---- the content ----------------------------------------------------------------------------------------------
enum { H_ALBUM = 1, H_ALBUM_PLAY, H_ARTIST, H_GENRE, H_FOLDER, H_ROW, H_HEAD, H_PLAY, H_SHUFFLE, H_FAVALL, H_MORE, H_SORT, H_QUEUE,
       H_ADDFOLDER, H_DELFOLDER, H_DONE, H_SEEK, H_TRANSPORT, H_RESUME, H_SEEALL, H_NOWCLOSE, H_SCROLL, H_VIDEO, H_VRESUME, H_OPENVID };
class Content : public Widget
{
public:
	HitList hits; int hot; int contentH; IntList rows; bool rowsValid;
	char *sel; int selCap; int anchorRow, lastClickRow; unsigned lastClickT;
	bool dragSb; int dragOff;
	Content (int l, int t, int w, int h) : Widget (l, t, w, h), hot (-1), contentH (0), rowsValid (false), sel (0), selCap (0), anchorRow (-1),
		lastClickRow (-1), lastClickT (0), dragSb (false), dragOff (0) {}
	unsigned bgColor () override { return C_FIELD; }
	int &sy () { return page ().scroll; }
	void reset_selection () { if (sel) memset (sel, 0, (size_t) selCap); anchorRow = -1; }
	void ensure_sel () { int need = L ? L->n + 1 : 1; if (selCap < need) { sel = (char *) realloc (sel, (size_t) need); memset (sel + selCap, 0, (size_t) (need - selCap)); selCap = need; } }
	void selected_ids (IntList &out) { out.clear (); ensure_sel (); for (int i = 0; i < rows.n; i++) if (rows.v[i] >= 0 && sel[rows.v[i]]) out.push (rows.v[i]); }

	// ---- pieces ----
	void band_button (int x, int y, int w, int ic, const char *label, bool accent, int kind)
	{
		bool h = hits.n == hot;
		if (accent) uk_rbox (canvas, x, y, w, 34, 6, uk_tone (C_ACCENT, h ? 150 : 140), uk_tone (C_ACCENT, h ? 128 : 118));
		else { uk_rbox (canvas, x, y, w, 34, 6, h ? 0xFFFFFF : 0xF4F1EE, h ? 0xF4F2F0 : 0xE8E3DE); uk_rline (canvas, x, y, w, 34, 6, 0x000000, 40); }
		unsigned ink = accent ? 0xFFFFFF : 0x202020;
		if (label) { icon (canvas, ic, x + 12, y + 9, 16, ink); text_v (canvas, x + 34, y, 34, label, ink, F_UI, accent ? 2 : 0); }
		else icon (canvas, ic, x + (w - 16) / 2, y + 9, 16, ic == I_HEART ? 0xD8484E : ink);
		hits.add (x, y, w, 34, kind);
	}
	void section_head (int x, int y, int w, const char *title, const char *sub, const char *right, int rightKind)
	{
		text (canvas, x, y, title, C_FIELD_TEXT, F_H1, 2);
		if (sub) text (canvas, x + tw (title, F_H1, 2) + 14, y + 10, sub, col_dim ());
		if (right)
		{
			int rw = tw (right) + 40; bool h = hits.n == hot;
			uk_rbox (canvas, x + w - rw, y + 2, rw, 30, 6, h ? 0xFFFFFF : uk_mix (C_FIELD, C_BG, 60), uk_mix (C_FIELD, C_BG, 110));
			uk_rline (canvas, x + w - rw, y + 2, rw, 30, 6, uk_tone (C_BG, 96));
			text_v (canvas, x + w - rw + 12, y + 2, 30, right, C_FIELD_TEXT);
			uk_glyph (canvas, WKG_CHEV_DOWN, x + w - 16, y + 18, 8, C_FIELD_TEXT);
			hits.add (x + w - rw, y + 2, rw, 30, rightKind);
		}
	}
	void album_tile (int x, int y, int S, int a, bool showArtist = true)
	{
		const Album &al = L->al[a];
		bool h = hot >= 0 && hot < hits.n + 2 && hits.n == hot;
		uk_rbox (canvas, x + 2, y + 3, S, S, 7, 0, 0, 40);
		g_covers.draw (canvas, a, x, y, S, 7, C_FIELD);
		bool playingHere = g_playing >= 0 && song (g_playing) && song (g_playing)->alb == a && g_playing < (L ? L->n : 0);
		if (playingHere && !h) { uk_rbox (canvas, x + S - 34, y + S - 34, 28, 28, 14, 0x000000, 0x000000, 150); eq_bars (canvas, x + S - 27, y + S - 27, 14, 0xFFFFFF, g_tick); }
		int titleY = y + S + 7;
		hits.add (x, y, S, S + 44, H_ALBUM, a);
		if (h)
		{
			uk_rbox (canvas, x, y, S, S, 7, 0, 0, 60);
			VPath c; c.circle (V (x + 28), V (y + S - 28), V (18)); c.fill (canvas, C_ACCENT);
			icon (canvas, I_PLAY, x + 18, y + S - 38, 20, 0xFFFFFF);
			hits.add (x + 8, y + S - 48, 40, 40, H_ALBUM_PLAY, a);
		}
		text (canvas, x, titleY, al.title, C_FIELD_TEXT, F_UI, 2, S);
		if (showArtist) text (canvas, x, titleY + 19, al.artist, col_dim (), F_SMALL, 0, S);
		else { char t[16]; snprintf (t, sizeof t, "%d", al.year); if (al.year) text (canvas, x, titleY + 19, t, col_dim (), F_SMALL); }
	}
	// a table of songs: columns by width; the playing one marked; sel, hot
	int song_table (int x, int y, int w, IntList &ids, bool showAlbum, bool showNum)
	{
		ensure_sel ();
		int colTitle = x + (showNum ? 44 : 12), colArtist = x + (int) (w * (showAlbum ? 0.40 : 0.55)), colAlbum = x + (int) (w * 0.66);
		int colTime = x + w - 74, colFmt = x + w - 62;
		// the head
		uk_rbox (canvas, x, y, w, 28, 5, uk_mix (C_FIELD, C_BG, 90), uk_mix (C_FIELD, C_BG, 90));
		const Page &pg = page ();
		struct { const char *s; int x; int col; bool right; } heads[] = { { "#", x + 30, 0, true }, { "Title", colTitle, 1, false }, { "Artist", colArtist, 2, false },
			{ "Album", colAlbum, 3, false }, { "Time", colTime, 4, true }, { "", colFmt, 5, false } };
		for (auto &hd : heads)
		{
			if (hd.col == 0 && !showNum) continue;
			if (hd.col == 3 && !showAlbum) continue;
			unsigned ink = pg.sortCol == hd.col && hd.col ? C_ACCENT : col_dim ();
			if (hd.right) text_r (canvas, hd.x, y, 28, hd.s, ink, F_SMALL, 2); else text_v (canvas, hd.x, y, 28, hd.s, ink, F_SMALL, 2);
			if (hd.col) hits.add (hd.right ? hd.x - 40 : hd.x, y, 120, 28, H_HEAD, hd.col);
		}
		y += 30;
		int top = scrollY, bottom = scrollY + height;
		for (int i = 0; i < ids.n; i++)
		{
			int ry = y + i * 32;
			if (ry + 32 < top || ry > bottom) { continue; }
			int id = ids.v[i]; const Song &s = L->s[id];
			bool on = sel[id], pl = id == g_playing, h = hits.n == hot;
			if (on) uk_rbox (canvas, x + 4, ry, w - 8, 30, 5, uk_mix (C_FIELD, C_ACCENT, 70), uk_mix (C_FIELD, C_ACCENT, 70));
			else if (h) uk_rbox (canvas, x + 4, ry, w - 8, 30, 5, uk_mix (C_FIELD, C_BG, 70), uk_mix (C_FIELD, C_BG, 70));
			else if (i % 2) uk_rbox (canvas, x + 4, ry, w - 8, 30, 5, uk_mix (C_FIELD, C_BG, 30), uk_mix (C_FIELD, C_BG, 30));
			unsigned ink = pl ? C_ACCENT : C_FIELD_TEXT, dim = col_dim ();
			char t[16];
			if (showNum)
			{
				if (pl && g_player.state == PS_PLAYING) eq_bars (canvas, x + 16, ry + 9, 12, C_ACCENT, g_tick);
				else if (pl) icon (canvas, I_PAUSE, x + 16, ry + 8, 14, C_ACCENT);
				else { snprintf (t, sizeof t, "%d", s.track ? s.track : i + 1); text_r (canvas, x + 30, ry, 30, t, dim, F_SMALL); }
			}
			else if (pl) eq_bars (canvas, x + 12 - 10, ry + 9, 10, C_ACCENT, g_tick);
			int tx = colTitle + (!showNum && pl ? 8 : 0);
			text_v (canvas, tx, ry, 30, s.title, ink, F_UI, pl ? 2 : 0, colArtist - tx - 12);
			if (s.fav) icon (canvas, I_HEART, colArtist - 26, ry + 8, 13, 0xD8484E);
			text_v (canvas, colArtist, ry, 30, s.artist[0] ? s.artist : L->artist_of (s), dim, F_UI, 0, (showAlbum ? colAlbum : colTime - 50) - colArtist - 12);
			if (showAlbum) text_v (canvas, colAlbum, ry, 30, s.album, dim, F_UI, 0, colTime - 50 - colAlbum - 10);
			fmt_time (t, sizeof t, s.durMs); text_r (canvas, colTime, ry, 30, t, dim, F_SMALL);
			const char *f = s.fmt >= 0 && s.fmt < FMT_N ? FMT_NAME[s.fmt] : "?";
			int fw = tw (f, F_SMALL, 2) + 12;
			unsigned fc = s.fmt == FMT_MIDI ? 0x7860C4 : uk_mix (C_FIELD, C_FIELD_TEXT, 100);
			uk_rbox (canvas, colFmt + 6, ry + 7, fw, 17, 8, fc, fc);
			text_c (canvas, colFmt + 6, ry + 7, fw, 17, f, 0xFFFFFF, F_SMALL, 2);
			hits.add (x, ry, w, 30, H_ROW, id, i);
		}
		return y + ids.n * 32;
	}

	// a video's frame w wide (16:9), its length, how much was watched; under the pointer: play
	void video_tile (int x, int y, int w, int vi, bool texts = true)
	{
		const Video &v = VL->v[vi];
		int h = w * 9 / 16;
		bool hv = hits.n == hot;
		hits.add (x, y, w, h + (texts ? 44 : 0), H_VIDEO, vi);
		if (y + h + 44 < 0 || y > height) return;
		uk_rbox (canvas, x + 2, y + 3, w, h, 7, 0, 0, 40);
		g_thumbs.draw (canvas, vi, x, y, w, h, 7, C_FIELD);
		video_badges (x, y, w, h, v);
		if (hv)
		{
			uk_rbox (canvas, x, y, w, h, 7, 0, 0, 50);
			VPath c; c.circle (V (x + w / 2), V (y + h / 2), V (22)); c.fill (canvas, 0xFFFFFF, 230);
			icon (canvas, I_PLAY, x + w / 2 - 10, y + h / 2 - 11, 22, 0x202028);
		}
		if (!texts) return;
		text (canvas, x, y + h + 7, v.title, C_FIELD_TEXT, F_UI, 2, w);
		char k[96]; video_kind_line (v, k, sizeof k);
		if (!v.playable) { char t[140]; snprintf (t, sizeof t, "%s  \xC2\xB7  %s: not played here", k, codec_label (v.vcodec)); text (canvas, x, y + h + 26, t, col_dim (), F_SMALL, 0, w); }
		else text (canvas, x, y + h + 26, k, col_dim (), F_SMALL, 0, w);
	}
	// over a video's frame: its length, the part watched (a red line), seen to the end, a codec not built in
	void video_badges (int x, int y, int w, int h, const Video &v)
	{
		char t[24]; fmt_time (t, sizeof t, v.durMs);
		if (v.durMs > 0)
		{
			int bw = tw (t, F_SMALL, 2) + 12;
			uk_rbox (canvas, x + w - bw - 6, y + h - 25, bw, 18, 4, 0x101014, 0x101014, 200);
			text_c (canvas, x + w - bw - 6, y + h - 25, bw, 18, t, 0xFFFFFF, F_SMALL, 2);
		}
		if (v.posMs > 0 && v.durMs > 0)
		{
			int bw = w - 14, f = (int) ((long long) v.posMs * bw / v.durMs); if (f > bw) f = bw; if (f < 3) f = 3;
			uk_rbox (canvas, x + 7, y + h - 6, bw, 3, 1, 0x000000, 0x000000, 120);
			uk_rbox (canvas, x + 7, y + h - 6, f, 3, 1, 0xE0383C, 0xE0383C);
		}
		else if (v.watched)
		{
			VPath c; c.circle (V (x + w - 15), V (y + 15), V (10)); c.fill (canvas, 0x101014, 190);
			icon (canvas, I_CHECK, x + w - 22, y + 8, 14, 0xFFFFFF);
		}
		if (!v.playable)
		{
			const char *c = codec_label (v.vcodec);
			int bw = tw (c, F_SMALL, 2) + 12;
			uk_rbox (canvas, x + 6, y + 6, bw, 18, 4, 0x707078, 0x707078, 220);
			text_c (canvas, x + 6, y + 6, bw, 18, c, 0xFFFFFF, F_SMALL, 2);
		}
	}
	void small_button (int x, int y, int w, int ic, const char *label, int kind, int a)
	{
		bool h = hits.n == hot;
		uk_rbox (canvas, x, y, w, 26, 5, uk_tone (C_ACCENT, h ? 150 : 140), uk_tone (C_ACCENT, h ? 128 : 118));
		icon (canvas, ic, x + 9, y + 6, 14, 0xFFFFFF);
		text_v (canvas, x + 28, y, 26, label, 0xFFFFFF, F_UI, 2);
		hits.add (x, y, w, 26, kind, a);
	}
	// what to go on with: the video left half way -> its card at (x, y), cw wide
	void resume_video_card (int x, int y, int cw, int vi)
	{
		const Video &v = VL->v[vi];
		uk_rbox (canvas, x, y, cw, 78, 9, 0xFFFFFF, 0xFFFFFF); uk_rline (canvas, x, y, cw, 78, 9, uk_tone (C_BG, 110));
		hits.add (x, y, cw, 78, H_VRESUME, vi);
		int tw_ = 110, th = 62;
		g_thumbs.draw (canvas, vi, x + 8, y + 8, tw_, th, 5, 0xFFFFFF);
		video_badges (x + 8, y + 8, tw_, th, v);
		int tx = x + 8 + tw_ + 12, tmax = cw - (tx - x) - 10;
		text (canvas, tx, y + 10, v.title, C_FIELD_TEXT, F_BIG, 2, tmax);
		char k[64], line[140], left[24]; video_kind_line (v, k, sizeof k);
		int rest = v.durMs - v.posMs;
		if (rest < 60000) snprintf (left, sizeof left, "%d s", rest > 0 ? rest / 1000 : 0); else fmt_long (left, sizeof left, rest);
		snprintf (line, sizeof line, "%s  \xC2\xB7  %s left", k, left);
		text (canvas, tx, y + 31, line, col_dim (), F_SMALL, 0, tmax);
		small_button (tx, y + 46, 96, I_PLAY, "Resume", H_VRESUME, vi);
	}
	// the song playing (else the last played) -> its card
	void resume_song_card (int x, int y, int cw, int cur, bool half)
	{
		const Song &s = *song (cur);
		uk_rbox (canvas, x, y, cw, 78, 9, 0xFFFFFF, 0xFFFFFF); uk_rline (canvas, x, y, cw, 78, 9, uk_tone (C_BG, 110));
		if (s.alb >= 0 && cur >= 0) g_covers.draw (canvas, s.alb, x + 9, y + 9, 60, 5, 0xFFFFFF);
		bool playing = cur == g_playing && g_player.state == PS_PLAYING;
		int tmax = cw - 82 - (half ? 56 : 160);
		text (canvas, x + 82, y + 12, s.title, C_FIELD_TEXT, F_BIG, 2, tmax);
		char line[200];
		int pos = -1; if (s.alb >= 0 && cur >= 0) for (int i = 0; i < L->al[s.alb].n; i++) if (L->al[s.alb].songs[i] == cur) pos = i;
		if (half && pos >= 0) snprintf (line, sizeof line, "%s  \xC2\xB7  song %d of %d", L->artist_of (s), pos + 1, L->al[s.alb].n);
		else snprintf (line, sizeof line, "%s  \xC2\xB7  %s", L->artist_of (s), s.album);
		text (canvas, x + 82, y + 34, line, col_dim (), F_SMALL, 0, tmax);
		if (playing) { eq_bars (canvas, x + 82, y + 55, 12, C_ACCENT, g_tick); text (canvas, x + 100, y + 54, "Playing", C_ACCENT, F_SMALL, 2); }
		else text (canvas, x + 82, y + 54, cur == g_playing ? "Paused" : "Played last", col_dim (), F_SMALL);
		if (half)
		{
			bool h = hits.n == hot;
			VPath c; c.circle (V (x + cw - 34), V (y + 39), V (18)); c.fill (canvas, h ? uk_tone (C_ACCENT, 150) : C_ACCENT);
			icon (canvas, playing ? I_PAUSE : I_PLAY, x + cw - 43, y + 30, 18, 0xFFFFFF);
			hits.add (x + cw - 52, y + 21, 36, 36, H_RESUME, cur);
		}
		else { band_button (x + cw - 130, y + 22, 118, playing ? I_PAUSE : I_PLAY, playing ? "Pause" : cur == g_playing ? "Resume" : "Play", true, H_RESUME); hits.h[hits.n - 1].a = cur; }
	}

	// ---- the pages ----
	int draw_home (int x, int y, int w)
	{
		int hr = 0, mi = 0; kapi_get_datetime (0, 0, 0, &hr, &mi, 0);
		const char *hello = hr < 5 ? "Good night" : hr < 12 ? "Good morning" : hr < 18 ? "Good afternoon" : "Good evening";
		char sub[120];
		if (VL->n) snprintf (sub, sizeof sub, "%d songs  \xC2\xB7  %d videos  \xC2\xB7  %d playlists", L->n, VL->n, g_npl);
		else snprintf (sub, sizeof sub, "%d songs  \xC2\xB7  %d albums  \xC2\xB7  %d playlists", L->n, L->nal, g_npl);
		section_head (x, y, w, hello, sub, 0, 0);
		y += 50;
		// go on with: the video left half way; the song playing, else the last played
		int cur = g_playing >= 0 ? g_playing : -1;
		if (cur < 0) { unsigned long long best = 0; for (int i = 0; i < L->n; i++) if (L->s[i].lastPlayed > best) { best = L->s[i].lastPlayed; cur = i; } }
		bool songCard = cur >= 0 && song (cur);
		int vres = VL->resume_candidate ();
		if (vres >= 0 && songCard)
		{
			int cw = (w - 16) / 2;
			resume_video_card (x, y, cw, vres);
			resume_song_card (x + cw + 16, y, w - cw - 16, cur, true);
			y += 96;
		}
		else if (vres >= 0) { resume_video_card (x, y, w, vres); y += 96; }
		else if (songCard) { resume_song_card (x, y, w, cur, false); y += 96; }
		// rows: the albums played lately, the videos, the albums added lately
		const int S = 128, G = 18;
		int cols = (w + G) / (S + G); if (cols < 1) cols = 1;
		for (int row = 0; row < 3; row++)
		{
			if (row == 1)
			{
				IntList vl; VL->recent (vl);
				if (!vl.n) continue;
				text (canvas, x, y, "Videos", C_FIELD_TEXT, F_BIG, 2);
				bool h = hits.n == hot;
				text_r (canvas, x + w, y, 22, "See all  \xE2\x80\xBA", h ? C_ACCENT : uk_mix (C_FIELD_TEXT, C_ACCENT, 200));
				hits.add (x + w - 80, y, 80, 22, H_SEEALL, 2);
				y += 32;
				const int VW = 166, VG = 16;
				int vc = (w + VG) / (VW + VG); if (vc < 1) vc = 1;
				for (int i = 0; i < vl.n && i < vc; i++) video_tile (x + i * (VW + VG), y, VW, vl.v[i]);
				y += VW * 9 / 16 + 62;
				continue;
			}
			IntList al;
			int *o = (int *) malloc (sizeof (int) * (L->nal ? L->nal : 1));
			if (row == 0)
			{
				for (int i = 0; i < L->nal; i++) { unsigned long long lp = 0; for (int k = 0; k < L->al[i].n; k++) if (L->s[L->al[i].songs[k]].lastPlayed > lp) lp = L->s[L->al[i].songs[k]].lastPlayed; if (lp) al.push (i); }
				sort_idx (al.v, al.n, [] (const void *, int a, int b) -> int {
					unsigned long long x = 0, y = 0;
					for (int k = 0; k < L->al[a].n; k++) if (L->s[L->al[a].songs[k]].lastPlayed > x) x = L->s[L->al[a].songs[k]].lastPlayed;
					for (int k = 0; k < L->al[b].n; k++) if (L->s[L->al[b].songs[k]].lastPlayed > y) y = L->s[L->al[b].songs[k]].lastPlayed;
					return y > x ? 1 : y < x ? -1 : 0; }, 0);
			}
			else { L->album_order (o, 3); for (int i = 0; i < L->nal; i++) al.push (o[i]); }
			free (o);
			if (!al.n) continue;
			text (canvas, x, y, row == 0 ? "Recently played" : "Recently added", C_FIELD_TEXT, F_BIG, 2);
			bool h = hits.n == hot;
			text_r (canvas, x + w, y, 22, "See all  \xE2\x80\xBA", h ? C_ACCENT : uk_mix (C_FIELD_TEXT, C_ACCENT, 200));
			hits.add (x + w - 80, y, 80, 22, H_SEEALL, row == 0 ? 0 : 1);
			y += 32;
			for (int i = 0; i < al.n && i < cols; i++) album_tile (x + i * (S + G), y, S, al.v[i]);
			y += S + 62;
		}
		return y;
	}
	// the videos of a kind: a grid of their frames
	int draw_videos (int x, int y, int w)
	{
		int kind = page ().arg;
		IntList vl; VL->list (vl, kind);
		long long ms = 0; for (int i = 0; i < vl.n; i++) ms += VL->v[vl.v[i]].durMs;
		char sub[96], d[24]; fmt_long (d, sizeof d, ms);
		snprintf (sub, sizeof sub, vl.n == 1 ? "%d video  \xC2\xB7  %s" : "%d videos  \xC2\xB7  %s", vl.n, d);
		section_head (x, y, w, kind == VK_FILM ? "Films" : kind == VK_CLIP ? "Clips and series" : "Videos", sub, 0, 0);
		y += 54;
		if (!vl.n)
		{
			if (g_scan && !g_scan->done) { char t[64]; snprintf (t, sizeof t, "Looking for videos...  %d", g_scan->vfound); text_c (canvas, x, y + 80, w, 24, t, C_FIELD_TEXT, F_BIG, 2); return y + 120; }
			VPath c; c.circle (V (x + w / 2), V (y + 70), V (40)); c.fill (canvas, C_ACCENT); icon (canvas, I_FILM, x + w / 2 - 20, y + 50, 40, 0xFFFFFF);
			text_c (canvas, x, y + 124, w, 24, kind == VK_FILM ? "No films yet." : kind == VK_CLIP ? "No clips or episodes yet." : "No videos yet.", C_FIELD_TEXT, F_BIG, 2);
			text_c (canvas, x, y + 154, w, 20, "Put them in SD:/Videos (Films, Series, Clips...) or in a folder watched: Media Player finds them at each start.", col_dim ());
			text_c (canvas, x, y + 174, w, 20, "MP4, MKV, WebM, AVI, MPEG, TS, WMV, FLV, OGV... -- H.264, H.265, VP9, AV1, MPEG-4, AAC, AC-3...", col_dim ());
			band_button (x + w / 2 - 85, y + 210, 170, I_PLAY, "Open a video...", true, H_OPENVID);
			return y + 260;
		}
		const int S = 206, G = 22;
		int cols = (w + G) / (S + G); if (cols < 1) cols = 1;
		int gx = x + (w - (cols * S + (cols - 1) * G)) / 2, rowH = S * 9 / 16 + 64;
		for (int i = 0; i < vl.n; i++) video_tile (gx + (i % cols) * (S + G), y + (i / cols) * rowH, S, vl.v[i]);
		return y + ((vl.n + cols - 1) / cols) * rowH;
	}
	int draw_albums (int x, int y, int w)
	{
		char sub[64]; long long ms = 0; for (int i = 0; i < L->nal; i++) ms += L->al[i].durMs;
		char d[24]; fmt_long (d, sizeof d, ms);
		snprintf (sub, sizeof sub, "%d albums  \xC2\xB7  %d songs  \xC2\xB7  %s", L->nal, L->n, d);
		static const char *const SORTS[4] = { "Sort: Name", "Sort: Artist", "Sort: Year", "Sort: Recently added" };
		section_head (x, y, w, "Albums", sub, SORTS[g_albumSort & 3], H_SORT);
		y += 54;
		int *o = (int *) malloc (sizeof (int) * (L->nal ? L->nal : 1));
		L->album_order (o, g_albumSort);
		if (g_albumList)
		{
			for (int i = 0; i < L->nal; i++)
			{
				int ry = y + i * 56; const Album &a = L->al[o[i]];
				if (ry + 56 >= scrollY && ry <= scrollY + height)
				{
					bool h = hits.n == hot;
					if (h) uk_rbox (canvas, x, ry, w, 52, 6, uk_mix (C_FIELD, C_BG, 70), uk_mix (C_FIELD, C_BG, 70));
					g_covers.draw (canvas, o[i], x + 6, ry + 4, 44, 4, h ? uk_mix (C_FIELD, C_BG, 70) : C_FIELD);
					text (canvas, x + 62, ry + 8, a.title, C_FIELD_TEXT, F_UI, 2, w / 2 - 70);
					text (canvas, x + 62, ry + 27, a.artist, col_dim (), F_SMALL, 0, w / 2 - 70);
					char t[64], dd[24]; fmt_long (dd, sizeof dd, a.durMs);
					if (a.year) snprintf (t, sizeof t, "%d", a.year); else t[0] = 0;
					text_v (canvas, x + w / 2, ry, 52, t, col_dim ());
					snprintf (t, sizeof t, "%d songs  \xC2\xB7  %s", a.n, dd); text_r (canvas, x + w - 10, ry, 52, t, col_dim (), F_SMALL);
				}
				hits.add (x, ry, w, 52, H_ALBUM, o[i]);
			}
			y += L->nal * 56;
		}
		else
		{
			const int S = 140, G = 22;
			int cols = (w + G) / (S + G); if (cols < 1) cols = 1;
			int gx = x + (w - (cols * S + (cols - 1) * G)) / 2;
			for (int i = 0; i < L->nal; i++)
			{
				int tx = gx + (i % cols) * (S + G), ty = y + (i / cols) * (S + 64);
				if (ty + S + 64 < scrollY || ty > scrollY + height) { hits.add (tx, ty, S, S + 44, H_ALBUM, o[i]); continue; }
				album_tile (tx, ty, S, o[i]);
			}
			y += ((L->nal + cols - 1) / cols) * (S + 64);
		}
		free (o);
		return y;
	}
	int draw_header_band (int x, int y, int w, int album, const char *kind, const char *title, const char *by, const char *meta, bool circle)
	{
		unsigned tone = album >= 0 ? g_covers.tone (album) : 0x384858;
		unsigned top = uk_mix (tone, 0x000000, 60), bot = uk_mix (tone, 0x000000, 120);
		uk_rbox (canvas, x, y, w, 200, 10, top, bot);
		if (album >= 0) g_covers.draw (canvas, album, x + 20, y + 20, 160, circle ? 80 : 8, uk_mix (top, bot, 100));
		else { VPath c; c.circle (V (x + 100), V (y + 100), V (80)); c.fill (canvas, uk_mix (tone, 0xFFFFFF, 60)); icon (canvas, I_PERSON, x + 60, y + 60, 80, 0xFFFFFF); }
		int tx = x + 204;
		unsigned lite = uk_mix (tone, 0xFFFFFF, 200);
		text (canvas, tx, y + 26, kind, lite, F_SMALL, 2);
		text (canvas, tx, y + 44, title, 0xFFFFFF, F_H1, 2, w - 224);
		if (by) text (canvas, tx, y + 82, by, 0xF0F0F0, F_MID, 0, w - 224);
		text (canvas, tx, y + 106, meta, lite, F_SMALL, 0, w - 224);
		band_button (tx, y + 140, 100, I_PLAY, "Play", true, H_PLAY);
		band_button (tx + 110, y + 140, 112, I_SHUFFLE, "Shuffle", false, H_SHUFFLE);
		band_button (tx + 232, y + 140, 40, I_MORE, 0, false, H_MORE);
		return y + 216;
	}
	int draw_album (int x, int y, int w)
	{
		const Page &pg = page ();
		if (!L || pg.arg < 0 || pg.arg >= L->nal) return y;
		const Album &a = L->al[pg.arg];
		if (midi_here (pg.arg))
		{
			int hRoll = height - 122 - 22 - 60; if (hRoll < 160) hRoll = 160;
			y = draw_midi (x, y, w, hRoll);
			return song_table (x, y + 8, w, rows, false, true);
		}
		char meta[160], d[24]; fmt_long (d, sizeof d, a.durMs);
		const Song &f = L->s[a.songs[0]];
		char yr[16] = ""; if (a.year) snprintf (yr, sizeof yr, "%d  \xC2\xB7  ", a.year);
		snprintf (meta, sizeof meta, "%s%s%s%d songs  \xC2\xB7  %s  \xC2\xB7  %s", yr, a.genre[0] ? a.genre : "", a.genre[0] ? "  \xC2\xB7  " : "", a.n, d, FMT_NAME[f.fmt >= 0 ? f.fmt : 0]);
		y = draw_header_band (x, y, w, pg.arg, "ALBUM", a.title, a.artist, meta, false);
		hits.add (x + 204, y - 216 + 82, tw (a.artist, F_MID), 22, H_ARTIST, f.art);
		return song_table (x, y, w, rows, false, true);
	}
	int draw_artists (int x, int y, int w)
	{
		char sub[48]; snprintf (sub, sizeof sub, "%d artists", L->nar);
		section_head (x, y, w, "Artists", sub, 0, 0);
		y += 54;
		const int S = 128, G = 26;
		int cols = (w + G) / (S + G); if (cols < 1) cols = 1;
		int gx = x + (w - (cols * S + (cols - 1) * G)) / 2;
		for (int i = 0; i < L->nar; i++)
		{
			int tx = gx + (i % cols) * (S + G), ty = y + (i / cols) * (S + 60);
			hits.add (tx, ty, S, S + 44, H_ARTIST, i);
			if (ty + S + 60 < scrollY || ty > scrollY + height) continue;
			const Group &g = L->ar[i];
			bool h = hits.n - 1 == hot;
			int al = g.na ? g.albums[0] : -1;
			if (al >= 0) g_covers.draw (canvas, al, tx, ty, S, S / 2, C_FIELD);
			if (h) { VPath o; o.arc (V (tx + S / 2), V (ty + S / 2), V (S / 2 + 3), 0, 360, V (3)); o.fill (canvas, C_ACCENT); }
			text_c (canvas, tx - 10, ty + S + 6, S + 20, 20, g.name, C_FIELD_TEXT, F_UI, 2);
			char t[48]; snprintf (t, sizeof t, g.na == 1 ? "%d album" : "%d albums", g.na);
			text_c (canvas, tx, ty + S + 26, S, 18, t, col_dim (), F_SMALL);
		}
		return y + ((L->nar + cols - 1) / cols) * (S + 60);
	}
	int draw_artist (int x, int y, int w)
	{
		const Page &pg = page ();
		if (pg.arg < 0 || pg.arg >= L->nar) return y;
		const Group &g = L->ar[pg.arg];
		char meta[96], d[24]; fmt_long (d, sizeof d, g.durMs);
		snprintf (meta, sizeof meta, "%d albums  \xC2\xB7  %d songs  \xC2\xB7  %s", g.na, g.n, d);
		y = draw_header_band (x, y, w, g.na ? g.albums[0] : -1, "ARTIST", g.name, 0, meta, true);
		text (canvas, x, y + 4, "Albums", C_FIELD_TEXT, F_BIG, 2); y += 34;
		const int S = 120, G = 18;
		int cols = (w + G) / (S + G); if (cols < 1) cols = 1;
		for (int i = 0; i < g.na; i++) album_tile (x + (i % cols) * (S + G), y + (i / cols) * (S + 62), S, g.albums[i], false);
		y += ((g.na + cols - 1) / cols) * (S + 62) + 8;
		text (canvas, x, y, "Songs", C_FIELD_TEXT, F_BIG, 2); y += 34;
		return song_table (x, y, w, rows, true, false);
	}
	// the library has no song yet: where to add a folder, in sight
	int empty_library (int x, int y, int w)
	{
		if (g_scan && !g_scan->done)
		{
			char t[64]; snprintf (t, sizeof t, "Looking for songs...  %d", g_scan->found);
			text_c (canvas, x, y + 110, w, 24, t, C_FIELD_TEXT, F_BIG, 2);
			return y + 150;
		}
		text_c (canvas, x, y + 110, w, 24, "No songs yet.", C_FIELD_TEXT, F_BIG, 2);
		text_c (canvas, x, y + 140, w, 20, "Add the folders where your music is (SD:/Music, another partition, a USB drive):", col_dim ());
		text_c (canvas, x, y + 160, w, 20, "Media Player finds the songs in them, and looks again at each start.", col_dim ());
		band_button (x + w / 2 - 85, y + 196, 170, I_PLUS, "Add a folder...", true, H_ADDFOLDER);
		return y + 250;
	}
	int draw_groups (int x, int y, int w, Group *gs, int n, const char *title, int kind, bool paths)
	{
		char sub[48]; snprintf (sub, sizeof sub, "%d", n);
		section_head (x, y, w, title, sub, 0, 0);
		if (paths)
		{	// the folders: where to add one, in sight (the menu's Folders to Watch... too)
			band_button (x + w - 170, y, 170, I_PLUS, "Add a folder...", true, H_ADDFOLDER);
			if (!n) return empty_library (x, y, w);
		}
		y += 54;
		if (!paths)
		{	// tiles of colour
			const int TW = 200, TH = 92, G = 16;
			int cols = (w + G) / (TW + G); if (cols < 1) cols = 1;
			for (int i = 0; i < n; i++)
			{
				int tx = x + (i % cols) * (TW + G), ty = y + (i / cols) * (TH + G);
				hits.add (tx, ty, TW, TH, kind, i);
				unsigned c = hash_str (gs[i].name); c = uk_mix (0x30405A, (c & 0x7F7F7F) + 0x303030, 160);
				bool h = hits.n - 1 == hot;
				uk_rbox (canvas, tx, ty, TW, TH, 10, uk_tone (c, h ? 150 : 138), uk_tone (c, h ? 120 : 108));
				text (canvas, tx + 16, ty + 16, gs[i].name, 0xFFFFFF, F_BIG, 2, TW - 32);
				char t[48]; snprintf (t, sizeof t, gs[i].na == 1 ? "%d songs  \xC2\xB7  %d album" : "%d songs  \xC2\xB7  %d albums", gs[i].n, gs[i].na);
				text (canvas, tx + 16, ty + TH - 30, t, 0xF0F0F0, F_SMALL);
			}
			return y + ((n + cols - 1) / cols) * (TH + G);
		}
		for (int i = 0; i < n; i++)
		{
			int ry = y + i * 44;
			hits.add (x, ry, w, 40, kind, i);
			bool h = hits.n - 1 == hot;
			if (h) uk_rbox (canvas, x, ry, w, 40, 6, uk_mix (C_FIELD, C_BG, 70), uk_mix (C_FIELD, C_BG, 70));
			icon (canvas, I_FOLDER, x + 10, ry + 10, 20, 0);
			text_v (canvas, x + 42, ry, 40, gs[i].name, C_FIELD_TEXT, F_UI, 2, w - 200);
			char t[48]; snprintf (t, sizeof t, "%d songs", gs[i].n); text_r (canvas, x + w - 12, ry, 40, t, col_dim (), F_SMALL);
		}
		return y + n * 44;
	}
	int draw_list_page (int x, int y, int w, const char *kind, const char *title, int coverAlbum)
	{
		long long ms = 0; for (int i = 0; i < rows.n; i++) ms += L->s[rows.v[i]].durMs;
		char meta[96], d[24]; fmt_long (d, sizeof d, ms); snprintf (meta, sizeof meta, "%d songs  \xC2\xB7  %s", rows.n, d);
		y = draw_header_band (x, y, w, coverAlbum, kind, title, 0, meta, false);
		if (!rows.n)
		{
			const char *m = page ().kind == P_PLAYLIST && page ().arg == PL_FAV ? "No favourites yet: the \xE2\x99\xA5 of a song, of the bar below, adds it here."
				: page ().kind == P_PLAYLIST ? "This playlist is empty: right click songs, then \xE2\x80\x9C" "Add to a playlist\xE2\x80\x9D." : "Nothing here.";
			text (canvas, x, y + 20, m, col_dim ());
			return y + 60;
		}
		return song_table (x, y, w, rows, true, false);
	}
	int draw_search (int x, int y, int w)
	{
		char sub[64]; snprintf (sub, sizeof sub, "%d songs", rows.n);
		section_head (x, y, w, "Search", sub, 0, 0);
		y += 54;
		// the albums and artists that match
		int na = 0;
		const int S = 110, G = 16; int cols = (w + G) / (S + G);
		for (int i = 0; i < L->nal && na < cols; i++)
			if (matches (L->al[i].title, g_search) || matches (L->al[i].artist, g_search)) { album_tile (x + na * (S + G), y, S, i); na++; }
		if (na) y += S + 62;
		// the videos that match
		int nv = 0;
		const int VW = 166, VG = 16; int vc = (w + VG) / (VW + VG); if (vc < 1) vc = 1;
		for (int i = 0; i < VL->n && nv < vc; i++)
			if (!VL->v[i].ext && matches (VL->v[i].title, g_search)) { video_tile (x + nv * (VW + VG), y, VW, i); nv++; }
		if (nv) y += VW * 9 / 16 + 62;
		if (!rows.n && !na && !nv) { text (canvas, x, y, "Nothing found.", col_dim ()); return y + 40; }
		return song_table (x, y, w, rows, true, false);
	}
	int draw_welcome (int x, int y, int w)
	{
		int mx = x + w / 2;
		y += 30;
		static const int ORDER[3] = { 0, 2, 1 };				// (the middle one on top)
		for (int j = 0; j < 3 && L && L->nal; j++) { int k = ORDER[j]; g_covers.draw (canvas, k % L->nal, mx - 110 + k * 70 - 45, y + (k == 1 ? 0 : 10), 90, 8, C_FIELD); }
		if (!L || !L->nal) { VPath c; c.circle (V (mx), V (y + 45), V (45)); c.fill (canvas, C_ACCENT); icon (canvas, I_NOTE, mx - 24, y + 21, 48, 0xFFFFFF); }
		y += 120;
		text_c (canvas, x, y, w, 34, "Your music, all in one place", C_FIELD_TEXT, F_H1, 2);
		text_c (canvas, x, y + 40, w, 20, "Media Player finds the songs in the folders you give it, and looks again at each start.", col_dim ());
		text_c (canvas, x, y + 60, w, 20, "MP3, OGG, FLAC, WAV and MIDI (played through a SoundFont); videos: MP4, MKV, WebM, AVI, WMV and more.", col_dim ());
		int fw = 460, fx = mx - fw / 2, fy = y + 100;
		int fh_ = 10 + (g_nfolders ? g_nfolders : 1) * 36;
		uk_rbox (canvas, fx, fy, fw, fh_, 8, 0xFFFFFF, 0xFFFFFF); uk_rline (canvas, fx, fy, fw, fh_, 8, uk_tone (C_BG, 110));
		for (int i = 0; i < g_nfolders; i++)
		{
			int ry = fy + 5 + i * 36;
			icon (canvas, I_FOLDER, fx + 14, ry + 8, 20, 0);
			text_v (canvas, fx + 44, ry, 36, g_folders[i], 0x202020, F_UI, 2, fw - 200);
			int cnt = 0, vc = 0; int pl = (int) strlen (g_folders[i]);
			for (int k = 0; L && k < L->n; k++) if (!strncasecmp (L->s[k].path, g_folders[i], (size_t) pl)) cnt++;
			for (int k = 0; VL && k < VL->n; k++) if (!strncasecmp (VL->v[k].path, g_folders[i], (size_t) pl)) vc++;
			char t[64];
			if (g_scan && !g_scan->done) snprintf (t, sizeof t, "looking...  %d", g_scan->found + g_scan->vfound);
			else if (vc) snprintf (t, sizeof t, "%d songs, %d videos", cnt, vc); else snprintf (t, sizeof t, "%d songs found", cnt);
			text_r (canvas, fx + fw - 44, ry, 36, t, 0x707070, F_SMALL);
			bool h = hits.n == hot;
			icon (canvas, I_CLOSE, fx + fw - 30, ry + 11, 14, h ? 0xC04040 : 0x909090);
			hits.add (fx + fw - 36, ry + 4, 28, 28, H_DELFOLDER, i);
		}
		if (!g_nfolders) text_c (canvas, fx, fy + 5, fw, 36, "No folder: add one.", 0x909090);
		fy += fh_ + 18;
		band_button (mx - 160, fy, 150, I_PLUS, "Add a folder...", false, H_ADDFOLDER);
		band_button (mx + 10, fy, 150, I_DISC, "Done", true, H_DONE);
		return fy + 60;
	}
	// ---- now playing: the cover large (a MIDI file: its notes), up next ----
	static unsigned roll_colour (int ch)
	{
		static const unsigned CH[16] = { 0xF06E5A, 0xFABE46, 0x78D26E, 0x50BEE6, 0xAA82FA, 0xFA78C8, 0x5ADCC8, 0xE6E66E, 0xF09650, 0x9696A0,
						 0x6E96FA, 0xDC6EDC, 0x96DC50, 0x50C8A0, 0xC8A078, 0xA0B4DC };
		return CH[ch & 15];
	}
	// a MIDI file's notes, scrolling under the playhead: a line a note, a colour a channel, the bars numbered
	void piano_roll (int x, int y, int w, int h, MidiSong *m, long long posMs)
	{
		const unsigned BG = 0x18161F;
		uk_rbox (canvas, x, y, w, h, 10, BG, BG);
		if (!m) return;
		int lo = m->lowKey - 1, hi = m->highKey + 1; if (hi - lo < 24) { int c = (lo + hi) / 2; lo = c - 12; hi = c + 12; }
		int keys = hi - lo + 1, kx = x + 50, kw = w - 60, top = y + 26, kh = h - 34;
		int rowH = kh / keys; if (rowH < 3) rowH = 3;
		int rows = kh / rowH; if (rows < keys) { hi = lo + rows - 1; keys = rows; }
		top = y + 26 + (kh - keys * rowH) / 2;
		for (int k = 0; k < keys; k++)					// the keyboard
		{
			int key = hi - k, n = key % 12; bool black = n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
			canvas.fillRect (x + 10, top + k * rowH, black ? 22 : 32, rowH - 1, black ? 0x3C3A46 : 0xE6E4EC);
		}
		i64 now = posMs * SOUND_RATE / 1000;
		// the span: about 8 bars (else 10 s), the playhead at 30 %
		i64 span = SOUND_RATE * 10;
		if (m->nbars > 2) { int b = 0; while (b + 1 < m->nbars && m->bars[b + 1] <= now) b++; int e = b + 8 < m->nbars ? b + 8 : m->nbars - 1; if (e > b) span = (m->bars[e] - m->bars[b]) * 8 / (e - b); }
		if (span < SOUND_RATE * 3) span = SOUND_RATE * 3;
		i64 t0 = now - span * 3 / 10;
		auto X = [&] (i64 t) { return kx + (int) ((t - t0) * kw / span); };
		for (int b = 0; b < m->nbars; b++)				// the bars
		{
			int bx = X (m->bars[b]);
			if (bx < kx || bx > kx + kw) continue;
			canvas.fillRect (bx, y + 8, 1, h - 16, uk_mix (BG, 0xFFFFFF, 34));
			char t[12]; snprintf (t, sizeof t, "%d", b + 1); text (canvas, bx + 4, y + 7, t, 0x8A889A, F_SMALL);
		}
		for (int i = 0; i < m->nnotes; i++)
		{
			const MidiNote &n = m->notes[i];
			if (n.off < t0 || n.on > t0 + span || n.ch == 9 || n.key > hi || n.key < lo) continue;
			int nx0 = X (n.on), nx1 = X (n.off);
			if (nx0 < kx) nx0 = kx; if (nx1 > kx + kw) nx1 = kx + kw; if (nx1 - nx0 < 4) nx1 = nx0 + 4;
			int ny = top + (hi - n.key) * rowH;
			bool sounding = n.on <= now && n.off > now, past = n.off <= now;
			unsigned c = roll_colour (n.ch); if (past) c = uk_mix (c, BG, 110);
			int nh = rowH > 4 ? rowH - 1 : rowH;
			uk_rbox (canvas, nx0, ny, nx1 - nx0 - 1, nh, nh / 2 < 3 ? nh / 2 : 3, c, c);
			if (sounding) uk_rline (canvas, nx0 - 1, ny - 1, nx1 - nx0 + 1, nh + 2, 3, 0xFFFFFF);
		}
		int ph = kx + kw * 3 / 10;					// the playhead
		canvas.fillRect (ph - 1, y + 6, 2, h - 12, 0xFFFFFF);
		VPath tri; int tp[] = { V (ph - 6), V (y + 6), V (ph + 6), V (y + 6), V (ph), V (y + 13) }; tri.poly (tp, 3); tri.fill (canvas, 0xFFFFFF);
	}
	// the instruments of the file: a dot of their colour, their name -> the height used
	int roll_legend (int x, int y, int w, MidiSong *m, unsigned ink)
	{
		if (!m) return 0;
		int lx = x, ly = y;
		for (int c = 0; c < 16; c++)
		{
			if (!m->used[c] || c == 9) continue;
			const char *nm = m->instrument (c);
			int ww = tw (nm, F_SMALL) + 30;
			if (lx + ww > x + w) { lx = x; ly += 20; }
			VPath d; d.circle (V (lx + 6), V (ly + 9), V (5)); d.fill (canvas, roll_colour (c));
			text (canvas, lx + 16, ly + 2, nm, ink, F_SMALL);
			lx += ww;
		}
		return ly - y + 20;
	}
	// a MIDI file playing: its header (cover, title, what plays it), its notes large, its instruments
	int draw_midi (int x, int y, int w, int hRoll)
	{
		const Song *s = song (g_playing);
		MidiSong *m = roll_for (g_playing);
		if (!s) return y;
		if (s->alb >= 0 && g_playing >= 0) g_covers.draw (canvas, s->alb, x, y, 104, 6, C_FIELD);
		int tx = x + 124;
		text (canvas, tx, y + 2, g_player.state == PS_PLAYING ? "MIDI  \xC2\xB7  NOW PLAYING" : "MIDI  \xC2\xB7  PAUSED", 0x7860C4, F_SMALL, 2);
		text (canvas, tx, y + 19, s->title, C_FIELD_TEXT, F_H2, 2, w - 124);
		int ninst = 0; if (m) for (int c = 0; c < 16; c++) if (m->used[c] && c != 9) ninst++;
		char line[200];
		snprintf (line, sizeof line, "%s  \xC2\xB7  %d instrument%s%s  \xC2\xB7  \xE2\x99\xA9 = %d", g_playing >= 0 ? L->artist_of (*s) : s->artist, ninst, ninst == 1 ? "" : "s",
			  m && m->used[9] ? " and drums" : "", m ? m->bpm : 120);
		text (canvas, tx, y + 48, line, col_dim (), F_SMALL, 0, w - 124);
		text_v (canvas, tx, y + 70, 28, "Played by", col_dim (), F_SMALL);
		int fx = tx + 64, fw = tw (ak_soundfont_name ()[0] ? ak_soundfont_name () : "a SoundFont") + 24;
		uk_rbox (canvas, fx, y + 70, fw, 28, 6, uk_mix (C_FIELD, C_BG, 60), uk_mix (C_FIELD, C_BG, 110)); uk_rline (canvas, fx, y + 70, fw, 28, 6, uk_tone (C_BG, 96));
		text_v (canvas, fx + 12, y + 70, 28, ak_soundfont_name ()[0] ? ak_soundfont_name () : "a SoundFont", C_FIELD_TEXT);
		y += 122;
		piano_roll (x, y, w, hRoll, m, g_player.posMs);
		y += hRoll + 12;
		y += roll_legend (x, y, w, m, col_dim ());
		return y + 8;
	}
	bool midi_here (int album) { const Song *s = song (g_playing); return s && g_playing >= 0 && s->fmt == FMT_MIDI && s->alb == album && album >= 0; }
	int draw_now (int x, int y, int w);
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
};

// the MIDI song shown by now playing (parsed by the window: the player's own is its thread's)
static MidiSong *g_roll; static int g_rollFor = -2;
static MidiSong *roll_for (int id)
{
	if (id == g_rollFor) return g_roll;
	delete g_roll; g_roll = 0; g_rollFor = id;
	const Song *s = song (id);
	if (s && s->fmt == FMT_MIDI) { g_roll = new MidiSong; char e[64]; if (!g_roll->load (s->path, e, sizeof e)) { delete g_roll; g_roll = 0; } }
	return g_roll;
}

int Content::draw_now (int x, int y, int w)
{
	const Song *s = song (g_playing);
	if (s && s->fmt == FMT_MIDI)
	{
		int hRoll = height - 122 - 22 - 60; if (hRoll < 160) hRoll = 160;
		return draw_midi (x, y, w, hRoll);
	}
	unsigned tone = s && s->alb >= 0 && g_playing >= 0 ? g_covers.tone (s->alb) : 0x2A3448;
	unsigned top = uk_mix (tone, 0x0A0E18, 150), bot = uk_mix (tone, 0x0A0E18, 215);
	for (int yy = 0; yy < height; yy++) canvas.fillRect (0, scrollY + yy, width, 1, uk_mix (top, bot, yy * 256 / (height ? height : 1)));
	int x0 = 40, y0 = scrollY + 24;
	VPath c; c.circle (V (x0 + 16), V (y0 + 16), V (16)); c.fill (canvas, 0xFFFFFF, 40);
	icon (canvas, I_DOWN, x0 + 8, y0 + 8, 16, 0xFFFFFF);
	hits.add (x0, y0, 32, 32, H_NOWCLOSE);
	text_v (canvas, x0 + 44, y0, 32, "Now playing", 0xFFFFFF, F_UI, 2);
	if (!s) { text (canvas, x0, y0 + 80, "Nothing is playing. Choose a song, an album...", 0xE0E0E0, F_MID); return y + height; }
	int qw = 330, avail = width - 80 - qw - 40;
	bool midi = s->fmt == FMT_MIDI;
	int S = avail < 320 ? avail : 320; if (S > height - 260) S = height - 260; if (S < 120) S = 120;
	int cx = x0, cy = y0 + 54;
	if (midi) { int rw = avail; piano_roll (cx, cy, rw, S, roll_for (g_playing), g_player.posMs); }
	else if (s->alb >= 0 && g_playing >= 0) g_covers.draw (canvas, s->alb, cx, cy, S, 10, uk_mix (top, bot, 100));
	int ty = cy + S + (midi ? 56 : 20);
	text (canvas, cx, ty, s->title, 0xFFFFFF, F_H1, 2, avail);
	char line[220]; snprintf (line, sizeof line, "%s  \xC2\xB7  %s%s", g_playing >= 0 ? L->artist_of (*s) : s->artist, s->album, "");
	text (canvas, cx, ty + 38, line, 0xD8E0E8, F_MID, 0, avail);
	char fmtl[120];
	if (midi) snprintf (fmtl, sizeof fmtl, "MIDI  \xC2\xB7  played by %s", ak_soundfont_name ()[0] ? ak_soundfont_name () : "a SoundFont");
	else if (g_player.rate) snprintf (fmtl, sizeof fmtl, "%s  \xC2\xB7  %d.%d kHz  \xC2\xB7  %d bit  \xC2\xB7  %d kbit/s", g_player.fmt, g_player.rate / 1000, g_player.rate % 1000 / 100,
					  g_player.bits ? g_player.bits : 16, g_player.kbps);
	else fmtl[0] = 0;
	text (canvas, cx, ty + 62, fmtl, 0xA8B4C0, F_SMALL);
	// up next
	int qx = width - 40 - qw, qy = y0 + 54, qh = height - (qy - scrollY) - 30;
	uk_rbox (canvas, qx, qy, qw, qh, 12, 0xFFFFFF, 0xFFFFFF, 26);
	text (canvas, qx + 18, qy + 14, "Up next", 0xFFFFFF, F_BIG, 2);
	int rowsMax = (qh - 60) / 56;
	int shown = 0;
	for (int i = g_qpos + 1; i < g_queue.n && shown < rowsMax; i++, shown++)
	{
		const Song *q = song (g_queue[i]); if (!q) continue;
		int ry = qy + 50 + shown * 56;
		bool h = hits.n == hot;
		if (h) uk_rbox (canvas, qx + 8, ry - 4, qw - 16, 52, 8, 0xFFFFFF, 0xFFFFFF, 40);
		if (q->alb >= 0 && g_queue[i] >= 0) g_covers.draw (canvas, q->alb, qx + 18, ry, 44, 4, uk_mix (top, bot, 100));
		text (canvas, qx + 74, ry + 4, q->title, 0xFFFFFF, F_UI, 2, qw - 150);
		text (canvas, qx + 74, ry + 24, q->artist[0] ? q->artist : q->albumArtist, 0xC0CCD8, F_SMALL, 0, qw - 150);
		char t[16]; fmt_time (t, sizeof t, q->durMs); text_r (canvas, qx + qw - 18, ry + 2, 40, t, 0xC0CCD8, F_SMALL);
		hits.add (qx + 8, ry - 4, qw - 16, 52, H_QUEUE, i);
	}
	if (!shown) text (canvas, qx + 18, qy + 56, g_repeat == 1 ? "The queue starts again." : "Nothing after this song.", 0xB0BCC8);
	else if (g_queue.n - g_qpos - 1 > shown) { char t[48]; snprintf (t, sizeof t, "%d more", g_queue.n - g_qpos - 1 - shown); text (canvas, qx + 18, qy + qh - 26, t, 0xB0BCC8, F_SMALL); }
	return y + height;
}

void Content::onDraw ()
{
	canvas.clear (C_FIELD);
	hits.clear ();
	if (!L) return;
	if (!rowsValid) { page_songs (page (), rows); sort_songs (rows, page ().sortCol, page ().sortDesc); rowsValid = true; }
	// the pages draw in the view's coordinates: their top at 22 - the page's scroll (Widget::scrollY stays 0:
	// the pieces cull against 0 .. height)
	int sy_ = sy (), x = 28, w = width - 56 - UK_SBW, y0 = 22 - sy_, yEnd = y0;
	switch (page ().kind)
	{
	case P_ALBUMS: case P_ARTISTS: case P_SONGS: case P_GENRES:
		if (!L->n)
		{
			static const char *T[] = { "Albums", "Artists", "Songs", "Genres" };
			int k = page ().kind == P_ALBUMS ? 0 : page ().kind == P_ARTISTS ? 1 : page ().kind == P_SONGS ? 2 : 3;
			section_head (x, y0, w, T[k], "0", 0, 0);
			yEnd = empty_library (x, y0, w);
			break;
		}
		if (page ().kind == P_ALBUMS) yEnd = draw_albums (x, y0, w);
		else if (page ().kind == P_ARTISTS) yEnd = draw_artists (x, y0, w);
		else if (page ().kind == P_GENRES) yEnd = draw_groups (x, y0, w, L->ge, L->nge, "Genres", H_GENRE, false);
		else { char sub[32]; snprintf (sub, sizeof sub, "%d songs", rows.n); section_head (x, y0, w, "Songs", sub, 0, 0); yEnd = song_table (x, y0 + 54, w, rows, true, false); }
		break;
	case P_HOME: yEnd = draw_home (x, y0, w); break;
	case P_ALBUM: yEnd = draw_album (x, y0, w); break;
	case P_ARTIST: yEnd = draw_artist (x, y0, w); break;
	case P_GENRE: yEnd = draw_list_page (x, y0, w, "GENRE", L->ge[page ().arg].name, L->ge[page ().arg].na ? L->ge[page ().arg].albums[0] : -1); break;
	case P_FOLDERS: yEnd = draw_groups (x, y0, w, L->fo, L->nfo, "Folders", H_FOLDER, true); break;
	case P_FOLDER: yEnd = draw_list_page (x, y0, w, "FOLDER", L->fo[page ().arg].name, L->fo[page ().arg].na ? L->fo[page ().arg].albums[0] : -1); break;
	case P_PLAYLIST:
	{
		const char *nm = page ().arg == PL_FAV ? "Favourites" : page ().arg == PL_RECENT ? "Recently added" : page ().arg < g_npl ? g_pl[page ().arg].name : "";
		int cov = rows.n ? L->s[rows.v[0]].alb : -1;
		yEnd = draw_list_page (x, y0, w, "PLAYLIST", nm, cov);
		break;
	}
	case P_NOW: yEnd = draw_now (x, y0, w); contentH = height; return;
	case P_SEARCH: yEnd = draw_search (x, y0, w); break;
	case P_WELCOME: yEnd = draw_welcome (x, y0, w); break;
	case P_VIDEOS: yEnd = draw_videos (x, y0, w); break;
	case P_WATCH: yEnd = y0; break;
	}
	contentH = yEnd + sy_ + 30;
	// the scroll bar
	if (contentH > height)
	{
		UkThumb t = uk_thumb (contentH, height, sy_, height - 8);
		uk_draw_vscroll (canvas, width - UK_SBW - 2, 4, UK_SBW, height - 8, t, C_FIELD, dragSb);
	}
}

// ---- dialogs -------------------------------------------------------------------------------------------------
static void dlg_btn (Widget &w) { if (w.parent) ((Modal *) w.parent)->onButton (w.tag); }
static void dlg_enter (Widget &w) { if (w.parent) ((Modal *) w.parent)->onButton (1); }
static void centre (Modal *m)
{
	Root *r = Root::current (); if (!r) return;
	m->left = (r->width - m->width) / 2; m->top = (r->height - m->height) / 2;
}
class InputBox : public Modal
{
	const char *m_title, *m_msg;
public:
	Textbox *tb;
	InputBox (const char *title, const char *msg, const char *init) : Modal (420, 150), m_title (title), m_msg (msg)
	{
		centre (this);
		tb = new Textbox (16, titleH () + 40, width - 32, 28, init, dlg_enter);
		tb->maxLen = 90; tb->setText (init); tb->caret = (int) strlen (init); tb->hasFocus = true; addChild (tb);
		Button *b = new Button (width - 196, height - 44, 86, 30, "OK", dlg_btn); b->tag = 1; addChild (b);
		b = new Button (width - 102, height - 44, 86, 30, "Cancel", dlg_btn); b->tag = 0; addChild (b);
	}
	void onButton (int tag) override { close (tag); }
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }
	void onDraw () override { drawBox (m_title); text_v (canvas, 16, titleH () + 10, 22, m_msg, C_TEXT); }
};
static bool ask_text (const char *title, const char *msg, const char *init, char *out, int cap)
{
	InputBox b (title, msg, init);
	if (!b.run ()) return false;
	scopy (out, b.tb->text, cap); tidy (out);
	return out[0] != 0;
}
// a song's facts (read only)
class PropsBox : public Modal
{
	char m_lines[12][2][160]; int m_n;
public:
	PropsBox (const Song &s) : Modal (520, 360), m_n (0)
	{
		centre (this);
		char t[64];
		add ("Title", s.title); add ("Artist", s.artist); add ("Album artist", s.albumArtist); add ("Album", s.album); add ("Genre", s.genre);
		if (s.year) { snprintf (t, sizeof t, "%d", s.year); add ("Year", t); }
		if (s.track) { snprintf (t, sizeof t, s.disc ? "%d (disc %d)" : "%d", s.track, s.disc); add ("Track", t); }
		fmt_time (t, sizeof t, s.durMs); add ("Length", t);
		snprintf (t, sizeof t, "%s  \xC2\xB7  %.1f MB", s.fmt >= 0 ? FMT_NAME[s.fmt] : "?", s.size / 1048576.0); add ("File", t);
		add ("Where", s.path);
		snprintf (t, sizeof t, "%d", s.plays); add ("Played", t);
		Button *b = new Button (width - 102, height - 44, 86, 30, "Close", dlg_btn); b->tag = 1; addChild (b);
	}
	void add (const char *k, const char *v) { if (!v || !v[0] || m_n >= 12) return; scopy (m_lines[m_n][0], k, 160); scopy (m_lines[m_n][1], v, 160); m_n++; }
	void onButton (int tag) override { close (tag); }
	bool onKey (long k) override { if (k == 27 || k == KEY_ENTER) { close (1); return true; } return false; }
	void onDraw () override
	{
		drawBox ("Properties");
		for (int i = 0; i < m_n; i++)
		{
			int y = titleH () + 14 + i * 22;
			text (canvas, 16, y, m_lines[i][0], uk_mix (C_FACE, C_TEXT, 150), F_UI);
			text (canvas, 130, y, m_lines[i][1], C_TEXT, F_UI, 2, width - 146);
		}
	}
};
void new_playlist_dialog (const IntList *ids)
{
	char name[96];
	if (!ask_text ("New playlist", "Its name:", "", name, sizeof name)) return;
	int k = playlist_new (name);
	if (ids && ids->n) playlist_add (k, *ids);
	refresh_all ();
}

// ---- navigation --------------------------------------------------------------------------------------------
static bool video_start (int vi, bool fromStart);
// the page changed: leaving a video closes it (where it was left kept); coming back to one opens it again
static void page_changed (int oldKind)
{
	if (oldKind == P_WATCH && page ().kind != P_WATCH) video_close ();
	if (page ().kind == P_WATCH && !g_vp.active ())
	{
		int vi = page ().arg;
		if (vi < 0 || vi >= (VL ? VL->n : 0) || !VL->v[vi].playable || !video_start (vi, false)) { page ().kind = P_HOME; page ().arg = 0; }
	}
}
static void navigate (int kind, int arg)
{
	Page &cur = page ();
	if (cur.kind == kind && cur.arg == arg && kind != P_SEARCH) return;
	int old = cur.kind;
	if (g_hpos < 63) g_hpos++; else memmove (g_hist, g_hist + 1, sizeof (Page) * 63);
	g_hist[g_hpos] = Page { kind, arg, 0, kind == P_SONGS ? 2 : 0, false };
	g_nhist = g_hpos + 1;
	g_content->rowsValid = false; g_content->reset_selection ();
	page_changed (old);
	refresh_all ();
}
void go_back () { if (g_hpos > 0) { int old = page ().kind; g_hpos--; g_content->rowsValid = false; g_content->reset_selection (); page_changed (old); refresh_all (); } }
void go_fwd () { if (g_hpos < g_nhist - 1) { int old = page ().kind; g_hpos++; g_content->rowsValid = false; g_content->reset_selection (); page_changed (old); refresh_all (); } }
static void go_back_page ()
{	// (out of a video: back, else home)
	if (g_hpos > 0) go_back ();
	else { int old = page ().kind; g_hist[0] = Page { P_HOME, 0, 0, 0, false }; page_changed (old); refresh_all (); }
}

// ---- the content's events ---------------------------------------------------------------------------------------
static void context_menu (int mx, int my, IntList &ids, bool inPlaylist)
{
	if (!ids.n) return;
	const Song &f = L->s[ids.v[0]];
	PopupMenu m (mx, my);
	m.add ("Play", 1, true, "Enter");
	m.add ("Play next", 2);
	m.add ("Add to the queue", 3);
	m.separator ();
	m.add ("Add to a playlist...", 4);
	m.add (f.fav ? "Remove from Favourites" : "Add to Favourites", 5);
	if (inPlaylist) m.add ("Remove from this playlist", 10);
	m.separator ();
	m.add ("Go to the album", 6, ids.n == 1);
	m.add ("Go to the artist", 7, ids.n == 1);
	m.separator ();
	m.add ("Properties...", 8, ids.n == 1);
	m.add ("Show in the File Viewer", 9, ids.n == 1);
	switch (m.run ())
	{
	case 1: play_list (ids, 0, false); break;
	case 2: play_next_ids (ids, true); break;
	case 3: play_next_ids (ids, false); break;
	case 4:
	{
		PopupMenu p (mx + 20, my + 20);
		p.add ("New playlist...", 1000); if (g_npl) p.separator ();
		for (int i = 0; i < g_npl && i < 12; i++) p.add (g_pl[i].name, i);
		int r = p.run ();
		if (r == 1000) new_playlist_dialog (&ids); else if (r >= 0) playlist_add (r, ids);
		break;
	}
	case 5: { bool on = !f.fav; for (int i = 0; i < ids.n; i++) L->s[ids.v[i]].fav = on; L->save_stats (); break; }
	case 6: navigate (P_ALBUM, f.alb); break;
	case 7: navigate (P_ARTIST, f.art); break;
	case 8: { PropsBox b (f); b.run (); break; }
	case 9: { char d[300]; scopy (d, f.path, sizeof d); char *sl = strrchr (d, '/'); if (sl) *sl = 0; fa_open (d); break; }
	case 10:
	{
		Plist &p = g_pl[page ().arg];
		for (int i = 0; i < ids.n; i++)
			for (int k = 0; k < p.n; k++) if (!strcmp (p.paths[k], L->s[ids.v[i]].path)) { free (p.paths[k]); memmove (p.paths + k, p.paths + k + 1, sizeof (char *) * (p.n - k - 1)); p.n--; break; }
		playlist_save (p);
		g_content->rowsValid = false;
		break;
	}
	}
	refresh_all ();
}
static void more_menu (int mx, int my)
{
	IntList &rows = g_content->rows;
	bool userPl = page ().kind == P_PLAYLIST && page ().arg >= 0;
	PopupMenu m (mx, my);
	m.add ("Play next", 1, rows.n > 0);
	m.add ("Add to the queue", 2, rows.n > 0);
	m.add ("Add to a playlist...", 3, rows.n > 0);
	m.add ("Add to Favourites", 4, rows.n > 0);
	if (page ().kind == P_ALBUM || page ().kind == P_FOLDER) { m.separator (); m.add ("Show in the File Viewer", 5); }
	if (userPl) { m.separator (); m.add ("Rename the playlist...", 6); m.add ("Delete the playlist", 7); }
	switch (m.run ())
	{
	case 1: play_next_ids (rows, true); break;
	case 2: play_next_ids (rows, false); break;
	case 3: { PopupMenu p (mx + 20, my + 20); p.add ("New playlist...", 1000); for (int i = 0; i < g_npl && i < 12; i++) p.add (g_pl[i].name, i);
		  int r = p.run (); if (r == 1000) new_playlist_dialog (&rows); else if (r >= 0) playlist_add (r, rows); break; }
	case 4: for (int i = 0; i < rows.n; i++) L->s[rows.v[i]].fav = true; L->save_stats (); break;
	case 5: if (rows.n) { char d[300]; scopy (d, L->s[rows.v[0]].path, sizeof d); char *sl = strrchr (d, '/'); if (sl) *sl = 0; fa_open (d); } break;
	case 6:
	{
		Plist &p = g_pl[page ().arg]; char nm[96];
		if (ask_text ("Rename the playlist", "Its new name:", p.name, nm, sizeof nm))
		{
			char np[300]; snprintf (np, sizeof np, "%s/%s.m3u", PLAYLIST_DIR, nm);
			if (kapi_rename (p.path, np) == 0) { scopy (p.path, np, sizeof p.path); scopy (p.name, nm, sizeof p.name); }
		}
		break;
	}
	case 7:
		if (uk_messagebox ("Delete the playlist", "Delete this playlist? (Its songs stay in the library.)", MB_YESNO) == 1)
		{ kapi_remove (g_pl[page ().arg].path); playlists_load (); go_back (); }
		break;
	}
	refresh_all ();
}
bool Content::onMouse (int mx, int my, int bl, int br, int, int wheel)
{
	bool in = mx >= 0 && my >= 0 && mx < width && my < height;
	int maxS = contentH - height; if (maxS < 0) maxS = 0;
	if (wheel && in) { int s = sy () - wheel * 60; sy () = s < 0 ? 0 : s > maxS ? maxS : s; invalidate (true); return true; }
	// the scroll bar
	if (bl && !pressed && in && contentH > height && mx >= width - UK_SBW - 4) { dragSb = true; pressed = true; }
	if (dragSb)
	{
		if (!bl) { dragSb = false; pressed = false; invalidate (true); return true; }
		UkThumb t = uk_thumb (contentH, height, sy (), height - 8);
		sy () = (int) uk_thumb_pos (my - 4, height - 8, contentH, height, t.h); invalidate (true); return true;
	}
	const Hit *ht = in ? hits.at (mx, my) : 0;
	int nh = ht ? (int) (ht - hits.h) : -1;
	if (nh != hot) { hot = nh; invalidate (true); }
	if (br && in && !pressed)
	{	// a right click: the row's menu (the selection, if it is in it)
		pressed = true;
		if (ht && ht->kind == H_VIDEO)
		{
			int ax = 0, ay = 0; for (Widget *p = this; p && p != g_root; p = p->parent) { ax += p->left; ay += p->top; }
			extern void video_menu (int, int, int); video_menu (ax + mx, ay + my, ht->a);
		}
		if (ht && ht->kind == H_ROW)
		{
			ensure_sel ();
			if (!sel[ht->a]) { reset_selection (); sel[ht->a] = 1; anchorRow = ht->b; invalidate (true); }
			IntList ids; selected_ids (ids);
			int ax = 0, ay = 0; for (Widget *p = this; p && p != g_root; p = p->parent) { ax += p->left; ay += p->top; }
			context_menu (ax + mx, ay + my, ids, page ().kind == P_PLAYLIST && page ().arg >= 0);
		}
		return true;
	}
	if (bl && !pressed) { pressed = true; return in; }
	if (!bl && !br && pressed)
	{
		pressed = false;
		if (!ht) return in;
		int ax = 0, ay = 0; for (Widget *p = this; p && p != g_root; p = p->parent) { ax += p->left; ay += p->top; }
		switch (ht->kind)
		{
		case H_ALBUM: navigate (P_ALBUM, ht->a); break;
		case H_ALBUM_PLAY: { IntList l; for (int i = 0; i < L->al[ht->a].n; i++) l.push (L->al[ht->a].songs[i]); play_list (l, 0, false); break; }
		case H_ARTIST: if (ht->a >= 0) navigate (P_ARTIST, ht->a); break;
		case H_GENRE: navigate (P_GENRE, ht->a); break;
		case H_FOLDER: navigate (P_FOLDER, ht->a); break;
		case H_HEAD:
			if (page ().sortCol == ht->a) page ().sortDesc = !page ().sortDesc; else { page ().sortCol = ht->a; page ().sortDesc = false; }
			rowsValid = false; invalidate (true); break;
		case H_ROW:
		{
			ensure_sel ();
			unsigned now = kapi_get_ticks (), mods = kapi_get_modifiers ();
			if (lastClickRow == ht->b && now - lastClickT < 45 && !(mods & (MOD_CTRL | MOD_SHIFT))) { play_list (rows, ht->b, false); lastClickRow = -1; break; }
			lastClickRow = ht->b; lastClickT = now;
			if (mods & MOD_CTRL) { sel[ht->a] = !sel[ht->a]; anchorRow = ht->b; }
			else if ((mods & MOD_SHIFT) && anchorRow >= 0)
			{
				int anc = anchorRow; reset_selection (); anchorRow = anc;		// (reset_selection forgets it)
				int a = anc < ht->b ? anc : ht->b, b = anc < ht->b ? ht->b : anc;
				for (int i = a; i <= b && i < rows.n; i++) sel[rows.v[i]] = 1;
			}
			else { reset_selection (); sel[ht->a] = 1; anchorRow = ht->b; }
			invalidate (true);
			break;
		}
		case H_PLAY: play_list (rows, 0, false); break;
		case H_SHUFFLE: play_list (rows, -1, true); break;
		case H_MORE: more_menu (ax + ht->x, ay + ht->y + ht->h + 2); break;
		case H_SORT:
		{
			PopupMenu m (ax + ht->x, ay + ht->y + ht->h + 2);
			m.add ("Name", 0); m.add ("Artist", 1); m.add ("Year", 2); m.add ("Recently added", 3);
			int r = m.run (); if (r >= 0) { g_albumSort = r; save_settings (); }
			invalidate (true); break;
		}
		case H_QUEUE: start_song (ht->a); break;
		case H_RESUME:
			if (ht->a == g_playing) toggle_play ();
			else { const Song &s = L->s[ht->a]; IntList l; int st = 0; for (int i = 0; s.alb >= 0 && i < L->al[s.alb].n; i++) { if (L->al[s.alb].songs[i] == ht->a) st = i; l.push (L->al[s.alb].songs[i]); } play_list (l, st, false); }
			break;
		case H_SEEALL: if (ht->a == 0) navigate (P_SONGS); else if (ht->a == 2) navigate (P_VIDEOS, -1); else navigate (P_PLAYLIST, PL_RECENT); break;
		case H_VIDEO: case H_VRESUME: { extern void play_video (int, bool); play_video (ht->a, false); break; }
		case H_OPENVID: { extern void m_open_video (); m_open_video (); break; }
		case H_NOWCLOSE: go_back (); break;
		case H_ADDFOLDER:
		{
			char d[200];
			if (g_nfolders < 8 && uk_folder_open (d, sizeof d, "SD:/"))
			{
				int l = (int) strlen (d); while (l > 4 && d[l - 1] == '/') d[--l] = 0;
				bool have = false; for (int i = 0; i < g_nfolders; i++) if (!strcmp (g_folders[i], d)) have = true;
				if (!have) { scopy (g_folders[g_nfolders++], d, 200); save_settings (); extern void start_scan (); start_scan (); }
			}
			invalidate (true); break;
		}
		case H_DELFOLDER:
			for (int i = ht->a; i < g_nfolders - 1; i++) memcpy (g_folders[i], g_folders[i + 1], 200);
			g_nfolders--; save_settings (); { extern void start_scan (); start_scan (); } invalidate (true); break;
		case H_DONE: navigate (P_HOME); break;
		}
		return true;
	}
	return in;
}
bool Content::onKey (long k)
{
	if (k == UK_CTRL ('A')) { ensure_sel (); for (int i = 0; i < rows.n; i++) sel[rows.v[i]] = 1; invalidate (true); return true; }
	if (k == KEY_ENTER) { IntList ids; selected_ids (ids); if (ids.n == 1) { int r = rows.find (ids.v[0]); play_list (rows, r, false); } else if (ids.n) play_list (ids, 0, false); return true; }
	if (k == KEY_PGDN || k == KEY_PGUP || k == KEY_HOME || k == KEY_END)
	{
		int maxS = contentH - height; if (maxS < 0) maxS = 0;
		int s = k == KEY_HOME ? 0 : k == KEY_END ? maxS : sy () + (k == KEY_PGDN ? height - 60 : -(height - 60));
		sy () = s < 0 ? 0 : s > maxS ? maxS : s; invalidate (true); return true;
	}
	return false;
}

// ---- the bar of what plays, at the bottom ---------------------------------------------------------------------
enum { N_COVER = 1, N_FAV, N_SHUFFLE, N_PREV, N_PLAY, N_NEXT, N_REPEAT, N_SEEK, N_QUEUE, N_MINI, N_MUTE, N_VOL, N_TITLE, N_ARTIST };
static void enter_mini (bool on);
class NowBar : public Widget
{
public:
	HitList hits; int hot, drag; long long dragMs;
	NowBar (int l, int t, int w, int h) : Widget (l, t, w, h), hot (-1), drag (0), dragMs (0) {}
	unsigned face () { return uk_mix (C_FIELD, C_BG, 90); }
	unsigned bgColor () override { return face (); }
	int seekX () { return width / 2 - 200; }
	int seekW () { return 400; }
	void onDraw () override
	{
		unsigned f = face ();
		canvas.clear (f);
		canvas.fillRect (0, 0, width, 1, uk_tone (C_BG, 100));
		hits.clear ();
		const Song *s = song (g_playing);
		unsigned dim = uk_mix (f, C_FIELD_TEXT, 150);
		if (s)
		{
			if (s->alb >= 0 && g_playing >= 0) g_covers.draw (canvas, s->alb, 12, 12, 56, 5, f);
			else { uk_rbox (canvas, 12, 12, 56, 56, 5, C_ACCENT, C_ACCENT); icon (canvas, I_NOTE, 24, 24, 32, 0xFFFFFF); }
			hits.add (12, 12, 56, 56, N_COVER);
			int maxw = seekX () - 80 - 40;
			if (maxw < 120) maxw = 120;
			text (canvas, 80, 18, s->title, C_FIELD_TEXT, F_UI, 2, maxw);
			char line[200]; snprintf (line, sizeof line, "%s  \xC2\xB7  %s", s->artist[0] ? s->artist : L ? L->artist_of (*s) : "", s->album);
			text (canvas, 80, 40, line, dim, F_SMALL, 0, maxw + 30);
			int hx = 80 + (tw (s->title, F_UI, 2) < maxw ? tw (s->title, F_UI, 2) : maxw) + 10;
			if (g_playing >= 0) { icon (canvas, s->fav ? I_HEART : I_HEART_O, hx, 18, 16, s->fav || hits.n == hot ? 0xD8484E : dim); hits.add (hx - 3, 15, 22, 22, N_FAV); }
		}
		else text_v (canvas, 16, 0, height, "Choose a song, an album, then \xE2\x96\xB6", dim);
		// the transport
		int mx = width / 2;
		auto btn = [&] (int x, int y, int sz, int ic, unsigned c, int kind) { bool h = hits.n == hot; icon (canvas, ic, x, y, sz, h ? C_ACCENT : c); hits.add (x - 4, y - 4, sz + 8, sz + 8, kind); };
		btn (mx - 122, 15, 18, I_SHUFFLE, g_shuffle ? C_ACCENT : dim, N_SHUFFLE);
		btn (mx - 72, 12, 22, I_PREV, C_FIELD_TEXT, N_PREV);
		{ bool h = hits.n == hot; VPath c; c.circle (V (mx), V (24), V (19)); c.fill (canvas, h ? uk_tone (C_ACCENT, 150) : C_ACCENT);
		  icon (canvas, g_player.state == PS_PLAYING || g_player.state == PS_LOADING ? I_PAUSE : I_PLAY, mx - 10, 14, 20, 0xFFFFFF); hits.add (mx - 19, 5, 38, 38, N_PLAY); }
		btn (mx + 50, 12, 22, I_NEXT, C_FIELD_TEXT, N_NEXT);
		btn (mx + 104, 15, 18, g_repeat == 2 ? I_REPEAT1 : I_REPEAT, g_repeat ? C_ACCENT : dim, N_REPEAT);
		// the position
		long long pos = drag == N_SEEK ? dragMs : g_player.posMs, len = g_player.lenMs > 0 ? g_player.lenMs : s ? s->durMs : 0;
		int bx = seekX (), bw = seekW ();
		char t[16];
		fmt_time (t, sizeof t, pos); text_r (canvas, bx - 10, 50, 18, t, dim, F_SMALL);
		fmt_time (t, sizeof t, len); text (canvas, bx + bw + 10, 52, t, dim, F_SMALL);
		int fill = len > 0 ? (int) (pos * bw / len) : 0; if (fill > bw) fill = bw;
		uk_rbox (canvas, bx, 57, bw, 5, 2, uk_tone (f, 112), uk_tone (f, 112));
		if (fill > 0) uk_rbox (canvas, bx, 57, fill, 5, 2, C_ACCENT, C_ACCENT);
		if (s) { VPath k; k.circle (V (bx + fill), V (59) + 8, V (7)); k.fill (canvas, 0xFFFFFF); VPath o; o.arc (V (bx + fill), V (59) + 8, V (7), 0, 360, 24); o.fill (canvas, C_ACCENT); }
		hits.add (bx - 6, 48, bw + 12, 22, N_SEEK);
		// the right: the queue, the mini player, the volume
		int rx = width - 16, vw = 90;
		int vf = g_muted ? 0 : g_volume * vw / 100;
		uk_rbox (canvas, rx - vw, 38, vw, 4, 2, uk_tone (f, 112), uk_tone (f, 112));
		if (vf) uk_rbox (canvas, rx - vw, 38, vf, 4, 2, uk_mix (f, C_FIELD_TEXT, 170), uk_mix (f, C_FIELD_TEXT, 170));
		VPath k; k.circle (V (rx - vw + vf), V (40), V (6)); k.fill (canvas, 0xFFFFFF); VPath o; o.arc (V (rx - vw + vf), V (40), V (6), 0, 360, 20); o.fill (canvas, uk_mix (f, C_FIELD_TEXT, 170));
		hits.add (rx - vw - 6, 30, vw + 12, 20, N_VOL);
		btn (rx - vw - 30, 31, 18, g_muted || !g_volume ? I_MUTE : I_VOLUME, C_FIELD_TEXT, N_MUTE);
		btn (rx - vw - 62, 31, 18, I_MINI, C_FIELD_TEXT, N_MINI);
		btn (rx - vw - 94, 31, 18, I_QUEUE, page ().kind == P_NOW ? C_ACCENT : C_FIELD_TEXT, N_QUEUE);
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (drag)
		{
			if (drag == N_SEEK)
			{
				long long len = g_player.lenMs; int bx = seekX (), bw = seekW ();
				int p = mx - bx; if (p < 0) p = 0; if (p > bw) p = bw;
				dragMs = len * p / bw;
				if (!bl) { g_player.seek (dragMs); drag = 0; }
			}
			else
			{
				int rx = width - 16, vw = 90, p = mx - (rx - vw); if (p < 0) p = 0; if (p > vw) p = vw;
				g_volume = p * 100 / vw; g_muted = false; g_player.volume = g_volume;
				if (!bl) { drag = 0; save_settings (); }
			}
			invalidate (true);
			return true;
		}
		if (wheel && in) { g_volume += wheel * 5; if (g_volume < 0) g_volume = 0; if (g_volume > 100) g_volume = 100; g_player.volume = g_muted ? 0 : g_volume; invalidate (true); return true; }
		const Hit *ht = in ? hits.at (mx, my) : 0;
		int nh = ht ? (int) (ht - hits.h) : -1;
		if (nh != hot) { hot = nh; invalidate (true); }
		if (bl && !pressed)
		{
			pressed = true;
			if (ht && ht->kind == N_SEEK && g_playing != -1) { drag = N_SEEK; return onMouse (mx, my, bl, 0, 0, 0); }
			if (ht && ht->kind == N_VOL) { drag = N_VOL; return onMouse (mx, my, bl, 0, 0, 0); }
			return in;
		}
		if (!bl && pressed)
		{
			pressed = false;
			if (!ht) return in;
			switch (ht->kind)
			{
			case N_COVER: case N_QUEUE: if (page ().kind == P_NOW) go_back (); else navigate (P_NOW); break;
			case N_FAV: if (g_playing >= 0) { L->s[g_playing].fav = !L->s[g_playing].fav; L->save_stats (); refresh_all (); } break;
			case N_SHUFFLE: set_shuffle (!g_shuffle); break;
			case N_PREV: play_prev (); break;
			case N_PLAY: toggle_play (); break;
			case N_NEXT: play_next (true); break;
			case N_REPEAT: g_repeat = (g_repeat + 1) % 3; save_settings (); break;
			case N_MUTE: g_muted = !g_muted; g_player.volume = g_muted ? 0 : g_volume; break;
			case N_MINI: enter_mini (true); break;
			}
			invalidate (true);
			return true;
		}
		return in;
	}
};

// ---- the mini player: the window reduced to a card at the bottom right of the screen -----------------------------
enum { M_PREV = 1, M_PLAY, M_NEXT, M_BACK, M_SEEK };
class MiniView : public Widget
{
public:
	HitList hits; int hot;
	MiniView (int l, int t, int w, int h) : Widget (l, t, w, h), hot (-1) {}
	void onDraw () override
	{
		const Song *s = song (g_playing);
		unsigned tone = s && s->alb >= 0 && g_playing >= 0 ? g_covers.tone (s->alb) : 0x2A3448;
		unsigned top = uk_mix (tone, 0x101018, 140), bot = uk_mix (tone, 0x101018, 200);
		for (int y = 0; y < height; y++) canvas.fillRect (0, y, width, 1, uk_mix (top, bot, y * 256 / height));
		hits.clear ();
		if (s && s->alb >= 0 && g_playing >= 0) g_covers.draw (canvas, s->alb, 10, 10, height - 20, 6, uk_mix (top, bot, 128));
		else { uk_rbox (canvas, 10, 10, height - 20, height - 20, 6, C_ACCENT, C_ACCENT); icon (canvas, I_NOTE, 24, 24, height - 48, 0xFFFFFF); }
		int x = height + 4, w = width - x - 12;
		text (canvas, x, 10, s ? s->title : "Nothing playing", 0xFFFFFF, F_UI, 2, w - 30);
		if (s) text (canvas, x, 29, s->artist[0] ? s->artist : s->albumArtist, 0xD0D4DC, F_SMALL, 0, w - 30);
		long long len = g_player.lenMs > 0 ? g_player.lenMs : s ? s->durMs : 0, pos = g_player.posMs;
		int fill = len > 0 ? (int) (pos * w / len) : 0; if (fill > w) fill = w;
		canvas.fillRect (x, 50, w, 3, uk_mix (bot, 0xFFFFFF, 70)); canvas.fillRect (x, 50, fill, 3, 0xFFFFFF);
		hits.add (x, 44, w, 14, M_SEEK);
		int my = 60, cx = x + 46;
		auto btn = [&] (int bx, int ic, int kind, int sz) { bool h = hits.n == hot; icon (canvas, ic, bx, my + (22 - sz) / 2, sz, h ? uk_mix (0xFFFFFF, C_ACCENT, 150) : 0xFFFFFF); hits.add (bx - 4, my - 4, sz + 8, 30, kind); };
		btn (cx - 42, I_PREV, M_PREV, 18);
		{ bool h = hits.n == hot; VPath c; c.circle (V (cx + 11), V (my + 11), V (13)); c.fill (canvas, h ? 0xE8E8F0 : 0xFFFFFF);
		  icon (canvas, g_player.state == PS_PLAYING ? I_PAUSE : I_PLAY, cx + 3, my + 3, 16, 0x202030); hits.add (cx - 2, my - 2, 26, 26, M_PLAY); }
		btn (cx + 34, I_NEXT, M_NEXT, 18);
		char t[32], a[16], b[16]; fmt_time (a, sizeof a, pos); fmt_time (b, sizeof b, len); snprintf (t, sizeof t, "%s / %s", a, b);
		text_r (canvas, width - 12, my, 22, t, 0xD0D4DC, F_SMALL);
		{ bool h = hits.n == hot; VPath c; c.circle (V (width - 19), V (19), V (10)); c.fill (canvas, 0xFFFFFF, h ? 90 : 45); icon (canvas, I_EXPAND, width - 26, 12, 14, 0xFFFFFF); hits.add (width - 30, 8, 22, 22, M_BACK); }
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		const Hit *ht = hits.at (mx, my);
		int nh = ht ? (int) (ht - hits.h) : -1;
		if (nh != hot) { hot = nh; invalidate (true); }
		if (bl && !pressed) { pressed = true; return true; }
		if (!bl && pressed)
		{
			pressed = false;
			if (!ht) return true;
			switch (ht->kind)
			{
			case M_PREV: play_prev (); break;
			case M_PLAY: toggle_play (); break;
			case M_NEXT: play_next (true); break;
			case M_BACK: enter_mini (false); break;
			case M_SEEK: if (g_player.lenMs > 0) g_player.seek (g_player.lenMs * (mx - ht->x) / (ht->w ? ht->w : 1)); break;
			}
			invalidate (true);
		}
		return true;
	}
};

// ---- the videos: playing one ----------------------------------------------------------------------------------------
// where it was left (seen to the end: from the start next time, marked watched)
static void video_save_pos ()
{
	if (g_vidx < 0 || g_vidx >= VL->n || !g_vp.active ()) return;
	Video &x = VL->v[g_vidx];
	long long pos = g_vp.pos_ms (), len = g_vp.len_ms () > 0 ? g_vp.len_ms () : x.durMs;
	if (g_vp.st.ended || (len > 0 && pos >= len - (len > 600000 ? 60000 : len / 20))) { x.posMs = 0; if (g_vp.st.ended || pos > 0) x.watched = true; }
	else x.posMs = pos < 5000 ? 0 : (int) pos;
	if (len > 0 && !x.durMs) x.durMs = (int) len;
	VL->save ();
}
static void video_close ()
{
	if (!g_vp.active ()) { g_vidx = -1; g_thumbs.busy = false; return; }
	video_save_pos ();
	g_vp.close ();
	g_vidx = -1; g_thumbs.busy = false;
}
// the video vi opened (the music stopped: it gives the sound output back) -> false: it cannot be
static bool video_start (int vi, bool fromStart)
{
	if (vi < 0 || vi >= VL->n) return false;
	if (g_player.state != PS_STOPPED || g_player.sound == 1)
	{	// the music's thread lets the sound go (a video's sound takes it)
		int r = g_player.released; g_player.release ();
		for (int i = 0; i < 100 && g_player.released == r; i++) kapi_msleep (5);
		g_now->invalidate (true);
	}
	video_close ();
	Video &x = VL->v[vi];
	int r = g_vp.open (x.path, fromStart || x.watched && !x.posMs ? 0 : x.posMs, g_volume, g_muted);
	if (r != AV_OK)
	{
		uk_messagebox ("Media Player", r == AV_EUNSUP ? "This file is not a video Media Player\ncan read." : "This video cannot be read.", MB_OK);
		return false;
	}
	g_vidx = vi; g_vpEnded = false;
	x.lastPlayed = now_stamp ();
	VL->save ();
	g_vpMoveT = kapi_get_ticks (); g_vpSaveT = g_vpMoveT;
	g_thumbs.busy = true;
	return true;
}
void play_video (int vi, bool fromStart)
{
	if (vi < 0 || vi >= VL->n) return;
	const Video &x = VL->v[vi];
	if (!x.playable)
	{
		char m[300], t[28]; scopy (t, x.title, sizeof t);
		snprintf (m, sizeof m, "\xE2\x80\x9C%s\xE2\x80\x9D cannot be played:\nits picture (%s) or its sound (%s)\nis in a format Onyx cannot decode.", t, codec_label (x.vcodec), x.acodec[0] ? codec_label (x.acodec) : "none");
		uk_messagebox ("Media Player", m, MB_OK);
		return;
	}
	if (!video_start (vi, fromStart)) return;
	if (page ().kind == P_WATCH) { page ().arg = vi; refresh_all (); }
	else navigate (P_WATCH, vi);
}
static void video_toggle ()
{
	if (!g_vp.active ()) return;
	if (g_vp.st.ended) { g_vp.seek (0); g_vp.play (); g_vpEnded = false; }
	else if (g_vp.st.paused) g_vp.play ();
	else { g_vp.pause (); video_save_pos (); }
	g_vpMoveT = kapi_get_ticks ();
}
static void video_seek_by (long long ms) { if (g_vp.active ()) { long long t = g_vp.pos_ms () + ms; g_vp.seek (t < 0 ? 0 : t); g_vpEnded = false; g_vpMoveT = kapi_get_ticks (); } }
static void video_volume (int v) { g_volume = v < 0 ? 0 : v > 100 ? 100 : v; g_muted = false; g_player.volume = g_volume; g_vp.set_volume (g_volume, g_muted); }
// the next episode of the series playing, -1 none
static int next_episode (int vi)
{
	if (vi < 0 || vi >= VL->n || !VL->v[vi].episode) return -1;
	IntList l; VL->list (l, VK_CLIP);
	int k = l.find (vi);
	if (k >= 0 && k + 1 < l.n) { const Video &a = VL->v[vi], &b = VL->v[l.v[k + 1]]; if (!strcmp (a.title, b.title) && b.episode) return l.v[k + 1]; }
	return -1;
}

// What the view shows (the window's, or the whole screen): the frame, the controls over it while the pointer
// moves (or paused), what loads, the end (again; the next episode), an error.
enum { W_BACK = 1, W_PLAY, W_B10, W_F10, W_SEEK, W_MUTE, W_VOL, W_FULL, W_AGAIN, W_NEXTEP, W_BIGPLAY };
struct WatchUi { HitList hits; int hot, drag; long long dragMs; bool full; };
static bool watch_controls (const WatchUi &u)
{
	return !g_vp.active () || g_vp.st.paused || g_vp.st.ended || g_vp.st.error || u.drag || kapi_get_ticks () - g_vpMoveT < 300;
}
static int seek_x0 () { return 24; }
static void draw_watch (Canvas &cv, int W, int H, WatchUi &u)
{
	u.hits.clear ();
	g_vp.draw (cv, 0, 0, W, H);
	const Video *x = g_vidx >= 0 && g_vidx < VL->n ? &VL->v[g_vidx] : 0;
	const av_player_status &st = g_vp.st;
	int cx = W / 2, cy = H / 2;
	if (!g_vp.frame && x && !st.error)
	{	// (the first frame not there yet: the video's own, from the library, dimmed)
		int tw_ = W * 2 / 3, th = tw_ * 9 / 16; if (th > H * 2 / 3) { th = H * 2 / 3; tw_ = th * 16 / 9; }
		g_thumbs.draw (cv, g_vidx, cx - tw_ / 2, cy - th / 2, tw_, th, 0, 0);
		uk_rbox (cv, cx - tw_ / 2, cy - th / 2, tw_, th, 0, 0, 0, 120);
	}
	bool loading = g_vp.active () && !st.error && !st.ended && (st.seeking || st.waiting || st.ready < AV_HAVE_CURRENT_DATA || !g_vp.frame);
	if (st.error)
	{
		uk_rbox (cv, cx - 260, cy - 60, 520, 120, 12, 0x1C1C22, 0x1C1C22, 230);
		text_c (cv, cx - 260, cy - 44, 520, 26, "This video cannot be played.", 0xFFFFFF, F_BIG, 2);
		const char *why = st.error == AV_EUNSUP ? "Its picture or sound is in a format Onyx does not decode yet." : "The file is damaged, or its data could not be read.";
		text_c (cv, cx - 260, cy - 10, 520, 20, why, 0xC8CCD4);
	}
	else if (st.ended)
	{
		int ne = next_episode (g_vidx);
		int bw = ne >= 0 ? 380 : 200;
		uk_rbox (cv, cx - bw / 2, cy - 34, bw, 68, 12, 0x1C1C22, 0x1C1C22, 210);
		int bx = cx - bw / 2 + 16;
		bool h = u.hits.n == u.hot;
		uk_rbox (cv, bx, cy - 18, 168, 36, 8, h ? 0xFFFFFF : 0xE8E8EE, h ? 0xF0F0F4 : 0xD8D8E0);
		icon (cv, I_REPEAT, bx + 14, cy - 9, 18, 0x202028); text_v (cv, bx + 42, cy - 18, 36, "Watch again", 0x202028, F_UI, 2);
		u.hits.add (bx, cy - 18, 168, 36, W_AGAIN);
		if (ne >= 0)
		{
			bx += 180; h = u.hits.n == u.hot;
			uk_rbox (cv, bx, cy - 18, 168, 36, 8, uk_tone (C_ACCENT, h ? 150 : 140), uk_tone (C_ACCENT, h ? 128 : 118));
			char t[48]; snprintf (t, sizeof t, "Next: S%d E%d", VL->v[ne].season, VL->v[ne].episode);
			icon (cv, I_NEXT, bx + 14, cy - 9, 18, 0xFFFFFF); text_v (cv, bx + 42, cy - 18, 36, t, 0xFFFFFF, F_UI, 2);
			u.hits.add (bx, cy - 18, 168, 36, W_NEXTEP, ne);
		}
	}
	else if (loading)
	{	// a turning arc
		int a0 = (int) (kapi_get_ticks () * 9 % 360);
		VPath r; r.arc (V (cx), V (cy), V (24), a0, a0 + 270, V (4)); r.fill (cv, 0xFFFFFF, 220);
	}
	else if (st.paused && g_vp.frame)
	{
		bool h = u.hits.n == u.hot;
		VPath c; c.circle (V (cx), V (cy), V (36)); c.fill (cv, 0x000000, h ? 170 : 130);
		icon (cv, I_PLAY, cx - 16, cy - 18, 36, 0xFFFFFF);
		u.hits.add (cx - 36, cy - 36, 72, 72, W_BIGPLAY);
	}
	if (!watch_controls (u)) return;
	// the top: back, the title
	for (int k = 0; k < 30; k++) uk_rbox (cv, 0, k * 3, W, 3, 0, 0, 0, (30 - k) * (30 - k) * 160 / 900);
	{
		bool h = u.hits.n == u.hot;
		VPath c; c.circle (V (36), V (36), V (18)); c.fill (cv, 0xFFFFFF, h ? 90 : 45);
		icon (cv, u.full ? I_UNFULL : I_BACK, u.full ? 26 : 25, 26, 20, 0xFFFFFF);
		u.hits.add (18, 18, 36, 36, W_BACK);
		if (x)
		{
			text (cv, 68, 16, x->title, 0xFFFFFF, F_BIG, 2, W - 100);
			char k[64], line[200]; video_kind_line (*x, k, sizeof k);
			if (st.width > 0) snprintf (line, sizeof line, "%s  \xC2\xB7  %d \xC3\x97 %d  \xC2\xB7  %s%s%s", k, st.width, st.height, codec_label (x->vcodec), x->acodec[0] ? " / " : "", x->acodec[0] ? codec_label (x->acodec) : "");
			else snprintf (line, sizeof line, "%s", k);
			text (cv, 68, 38, line, 0xC8CCD4, F_SMALL, 0, W - 100);
		}
	}
	// the bottom: the position, the transport, the volume, full screen
	for (int k = 0; k < 36; k++) uk_rbox (cv, 0, H - 108 + k * 3, W, 3, 0, 0, 0, (k + 1) * (k + 1) * 180 / 1296);
	long long len = g_vp.len_ms () > 0 ? g_vp.len_ms () : x ? x->durMs : 0;
	long long pos = u.drag == W_SEEK ? u.dragMs : g_vp.pos_ms ();
	int sx = seek_x0 (), sw = W - 48, sy = H - 62;
	{
		int f = len > 0 ? (int) (pos * sw / len) : 0; if (f > sw) f = sw; if (f < 0) f = 0;
		bool h = u.hits.n == u.hot || u.drag == W_SEEK;
		int th = h ? 5 : 4;
		uk_rbox (cv, sx, sy - th / 2, sw, th, 2, 0xFFFFFF, 0xFFFFFF, 70);
		if (f > 0) uk_rbox (cv, sx, sy - th / 2, f, th, 2, 0xE0383C, 0xE0383C);
		VPath k; k.circle (V (sx + f), V (sy) + V (1) / 2, V (h ? 8 : 6)); k.fill (cv, 0xE0383C);
		u.hits.add (sx - 8, sy - 12, sw + 16, 24, W_SEEK);
	}
	int by = H - 44;
	auto btn = [&] (int bx, int ic, int sz, int kind) { bool h = u.hits.n == u.hot; icon (cv, ic, bx, by + (28 - sz) / 2, sz, h ? 0xFFFFFF : 0xE4E6EC); u.hits.add (bx - 6, by - 4, sz + 12, 36, kind); };
	btn (sx, g_vp.st.paused || g_vp.st.ended ? I_PLAY : I_PAUSE, 26, W_PLAY);
	btn (sx + 46, I_BACK10, 22, W_B10);
	btn (sx + 82, I_FWD10, 22, W_F10);
	char a[16], b[16], t[40]; fmt_time (a, sizeof a, pos); fmt_time (b, sizeof b, len); snprintf (t, sizeof t, "%s / %s", a, b);
	text_v (cv, sx + 122, by, 28, t, 0xFFFFFF, F_UI, 0);
	int rx = W - 24;
	btn (rx - 22, u.full ? I_UNFULL : I_FULL, 22, W_FULL);
	int vw = 90, vx = rx - 22 - 22 - vw, vf = g_muted ? 0 : g_volume * vw / 100;
	uk_rbox (cv, vx, by + 12, vw, 4, 2, 0xFFFFFF, 0xFFFFFF, 70);
	if (vf) uk_rbox (cv, vx, by + 12, vf, 4, 2, 0xFFFFFF, 0xFFFFFF);
	{ VPath k; k.circle (V (vx + vf), V (by + 14), V (6)); k.fill (cv, 0xFFFFFF); }
	u.hits.add (vx - 6, by, vw + 12, 28, W_VOL);
	btn (vx - 34, g_muted || !g_volume ? I_MUTE : I_VOLUME, 20, W_MUTE);
}
static void video_full_screen ();
// a click (or the end of a drag) on the view's controls; true: handled
static bool watch_press (WatchUi &u, const Hit *ht, int mx)
{
	if (!ht) return false;
	switch (ht->kind)
	{
	case W_BACK: if (u.full) return true; go_back_page (); return true;
	case W_PLAY: case W_BIGPLAY: video_toggle (); return true;
	case W_B10: video_seek_by (-10000); return true;
	case W_F10: video_seek_by (10000); return true;
	case W_MUTE: g_muted = !g_muted; g_player.volume = g_muted ? 0 : g_volume; g_vp.set_volume (g_volume, g_muted); save_settings (); return true;
	case W_FULL: if (!u.full) video_full_screen (); return true;
	case W_AGAIN: g_vp.seek (0); g_vp.play (); g_vpEnded = false; return true;
	case W_NEXTEP: play_video (ht->a, true); return true;
	case W_SEEK: case W_VOL: (void) mx; return true;
	}
	return false;
}
// a drag on the position or the volume: its value from x
static void watch_drag (WatchUi &u, int mx, int W, bool up)
{
	if (u.drag == W_SEEK)
	{
		long long len = g_vp.len_ms () > 0 ? g_vp.len_ms () : g_vidx >= 0 ? VL->v[g_vidx].durMs : 0;
		int sw = W - 48, p = mx - seek_x0 (); if (p < 0) p = 0; if (p > sw) p = sw;
		u.dragMs = len * p / sw;
		if (up) { g_vp.seek (u.dragMs); g_vpEnded = false; u.drag = 0; }
	}
	else if (u.drag == W_VOL)
	{
		int vw = 90, vx = W - 24 - 22 - 22 - vw, p = mx - vx; if (p < 0) p = 0; if (p > vw) p = vw;
		video_volume (p * 100 / vw);
		if (up) { u.drag = 0; save_settings (); }
	}
	g_vpMoveT = kapi_get_ticks ();
}

class WatchView : public Widget
{
public:
	WatchUi u; bool shown; unsigned lastClickT;
	WatchView (int l, int t, int w, int h) : Widget (l, t, w, h), shown (false), lastClickT (0) { u.hot = -1; u.drag = 0; u.dragMs = 0; u.full = false; u.hits.clear (); }
	unsigned bgColor () override { return 0; }
	void onDraw () override { shown = watch_controls (u); draw_watch (canvas, width, height, u); }
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (in && !shown) invalidate (true);
		if (in) g_vpMoveT = kapi_get_ticks ();
		if (u.drag) { watch_drag (u, mx, width, !bl); invalidate (true); return true; }
		if (wheel && in) { video_volume (g_volume + wheel * 5); invalidate (true); return true; }
		const Hit *ht = in ? u.hits.at (mx, my) : 0;
		int nh = ht ? (int) (ht - u.hits.h) : -1;
		if (nh != u.hot) { u.hot = nh; invalidate (true); }
		if (bl && !pressed)
		{
			pressed = true;
			if (ht && (ht->kind == W_SEEK || ht->kind == W_VOL)) { u.drag = ht->kind; watch_drag (u, mx, width, false); invalidate (true); }
			return in;
		}
		if (!bl && pressed)
		{
			pressed = false;
			if (ht) { watch_press (u, ht, mx); invalidate (true); return true; }
			if (!in) return false;
			// on the picture: play / pause; twice: full screen
			unsigned now = kapi_get_ticks ();
			video_toggle ();
			if (now - lastClickT < 45) { lastClickT = 0; video_toggle (); video_full_screen (); }
			else lastClickT = now;
			invalidate (true);
			return true;
		}
		return in;
	}
};

// the keys of the view (the window's and full screen's): true if taken
static bool watch_key (long k, bool full)
{
	if (!g_vp.active ()) return false;
	g_vpMoveT = kapi_get_ticks ();
	switch (k)
	{
	case ' ': case 'k': case 'K': video_toggle (); return true;
	case KEY_LEFT: case 'j': video_seek_by (-10000); return true;
	case KEY_RIGHT: case 'l': video_seek_by (10000); return true;
	case KEY_UP: video_volume (g_volume + 5); return true;
	case KEY_DOWN: video_volume (g_volume - 5); return true;
	case KEY_HOME: g_vp.seek (0); return true;
	case 'm': case 'M': g_muted = !g_muted; g_vp.set_volume (g_volume, g_muted); return true;
	case 'f': case 'F': if (!full) video_full_screen (); return !full;
	}
	if (k >= '0' && k <= '9' && g_vp.len_ms () > 0) { g_vp.seek (g_vp.len_ms () * (k - '0') / 10); return true; }
	return false;
}

// ---- full screen: the video alone, the controls over it while the pointer moves ---------------------------------------
static volatile long g_fsKey; static volatile int g_fsX = -1, g_fsY = -1, g_fsBtn, g_fsDown, g_fsUp, g_fsWheel, g_fsMoved;
static void fs_key (unsigned long, int ev, gui_value v) { if (ev == GUI_EVENT_KEY) g_fsKey = (long) v; }
static void fs_ptr (unsigned long, int ev, gui_value v)
{
	if (ev == GUI_EVENT_PTR_MOVE || ev == GUI_EVENT_PTR_DOWN || ev == GUI_EVENT_PTR_UP) { g_fsX = GUI_PTR_X (v); g_fsY = GUI_PTR_Y (v); g_fsMoved = 1; }
	if (ev == GUI_EVENT_PTR_DOWN && (GUI_PTR_CHANGED (v) & 1)) { g_fsDown = 1; g_fsBtn = 1; }
	if (ev == GUI_EVENT_PTR_UP && (GUI_PTR_CHANGED (v) & 1)) { g_fsUp = 1; g_fsBtn = 0; }
	if (ev == GUI_EVENT_PTR_WHEEL) g_fsWheel += GUI_PTR_WHEEL (v);
}
static void video_full_screen ()
{
	if (!g_vp.active ()) return;
	int W, H;
	unsigned *fb = kapi_fullscreen_begin (&W, &H);
	if (!fb) return;
	kapi_set_key_handler (fs_key); kapi_set_pointer_handler (fs_ptr);
	g_fsKey = 0; g_fsDown = g_fsUp = g_fsWheel = g_fsMoved = 0;
	Canvas cv; cv.adopt (fb, W, H, W);
	static WatchUi u; u.hits.clear (); u.hot = -1; u.drag = 0; u.full = true;
	unsigned serial = ~0u, gen = ~0u, lastClick = 0; bool shown = true, dirty = true;
	g_vpMoveT = kapi_get_ticks ();
	for (;;)
	{
		kapi_pump_wait (8);
		if (uk_quit ()) break;
		bool nf = g_vp.poll ();
		long k = g_fsKey; g_fsKey = 0;
		if (k == 27 || k == KEY_F1 + 10 || ((k == 'f' || k == 'F') && !u.drag)) break;
		if (k && watch_key (k, true)) dirty = true;
		if (g_fsMoved) { g_fsMoved = 0; g_vpMoveT = kapi_get_ticks (); if (!shown) dirty = true; }
		if (g_fsWheel) { video_volume (g_volume + g_fsWheel * 5); g_fsWheel = 0; dirty = true; }
		const Hit *ht = u.hits.at (g_fsX, g_fsY);
		int nh = ht ? (int) (ht - u.hits.h) : -1;
		if (nh != u.hot) { u.hot = nh; dirty = true; }
		if (u.drag) { watch_drag (u, g_fsX, W, g_fsUp != 0); g_fsUp = 0; g_fsDown = 0; dirty = true; }
		if (g_fsDown) { g_fsDown = 0; if (ht && (ht->kind == W_SEEK || ht->kind == W_VOL)) { u.drag = ht->kind; watch_drag (u, g_fsX, W, false); } dirty = true; }
		if (g_fsUp)
		{
			g_fsUp = 0; dirty = true;
			if (ht && ht->kind == W_BACK) break;
			if (ht && ht->kind == W_FULL) break;
			if (!watch_press (u, ht, g_fsX))
			{
				unsigned now = kapi_get_ticks ();
				video_toggle ();
				if (now - lastClick < 45) { video_toggle (); break; }
				lastClick = now;
			}
			if (!g_vp.active ()) break;
		}
		bool sh = watch_controls (u);
		if (sh != shown) { shown = sh; dirty = true; }
		if (nf || g_vp.serial != serial) { serial = g_vp.serial; dirty = true; }
		if (g_vp.statusGen != gen) { gen = g_vp.statusGen; dirty = true; }
		bool loading = !g_vp.st.error && !g_vp.st.ended && (g_vp.st.seeking || g_vp.st.waiting || !g_vp.frame);
		if (loading) dirty = true;
		if (g_vp.st.ended && !g_vpEnded) { g_vpEnded = true; video_save_pos (); }
		if (kapi_get_ticks () - g_vpSaveT > 1500) { g_vpSaveT = kapi_get_ticks (); if (!g_vp.st.paused) video_save_pos (); }
		g_thumbs.busy = !g_vp.st.paused;
		if (dirty) { draw_watch (cv, W, H, u); kapi_present_fb (); dirty = false; }
	}
	kapi_fullscreen_end ();
	g_root->attach ();
	g_vpMoveT = kapi_get_ticks ();
	refresh_all ();
}

// a video's facts
class VPropsBox : public Modal
{
	char m_lines[12][2][160]; int m_n;
public:
	VPropsBox (const Video &x) : Modal (540, 330), m_n (0)
	{
		centre (this);
		char t[160], k[64];
		add ("Title", x.title);
		video_kind_line (x, k, sizeof k); add ("Kind", k);
		fmt_time (t, sizeof t, x.durMs); add ("Length", t);
		if (x.w) { snprintf (t, sizeof t, "%d \xC3\x97 %d", x.w, x.h); add ("Picture", t); }
		snprintf (t, sizeof t, "%s%s", codec_label (x.vcodec), x.playable ? "" : "  (not decoded by Onyx yet)"); add ("Video", t);
		if (x.acodec[0]) add ("Sound", codec_label (x.acodec));
		snprintf (t, sizeof t, "%s  \xC2\xB7  %.1f MB", ext_of (x.path), x.size / 1048576.0); add ("File", t);
		add ("Where", x.path);
		if (x.posMs > 0) { char a[24]; fmt_time (a, sizeof a, x.posMs); snprintf (t, sizeof t, "left at %s", a); add ("Watched", t); }
		else if (x.watched) add ("Watched", "to the end");
		Button *b = new Button (width - 102, height - 44, 86, 30, "Close", dlg_btn); b->tag = 1; addChild (b);
	}
	void add (const char *k, const char *v) { if (!v || !v[0] || m_n >= 12) return; scopy (m_lines[m_n][0], k, 160); scopy (m_lines[m_n][1], v, 160); m_n++; }
	void onButton (int tag) override { close (tag); }
	bool onKey (long k) override { if (k == 27 || k == KEY_ENTER) { close (1); return true; } return false; }
	void onDraw () override
	{
		drawBox ("Properties");
		for (int i = 0; i < m_n; i++)
		{
			int y = titleH () + 14 + i * 22;
			text (canvas, 16, y, m_lines[i][0], uk_mix (C_FACE, C_TEXT, 150), F_UI);
			text (canvas, 130, y, m_lines[i][1], C_TEXT, F_UI, 2, width - 146);
		}
	}
};
void video_menu (int mx, int my, int vi)
{
	if (vi < 0 || vi >= VL->n) return;
	Video &x = VL->v[vi];
	PopupMenu m (mx, my);
	m.add (x.posMs > 0 ? "Resume" : "Play", 1, x.playable, "Enter");
	m.add ("Play from the Start", 2, x.playable && x.posMs > 0);
	m.separator ();
	m.add (x.watched || x.posMs ? "Mark as Not Watched" : "Mark as Watched", 3);
	m.separator ();
	m.add ("Properties...", 4);
	m.add ("Show in the File Viewer", 5);
	switch (m.run ())
	{
	case 1: play_video (vi, false); break;
	case 2: play_video (vi, true); break;
	case 3: if (x.watched || x.posMs) { x.watched = false; x.posMs = 0; } else { x.watched = true; x.posMs = 0; } VL->save (); break;
	case 4: { VPropsBox b (x); b.run (); break; }
	case 5: { char d[300]; scopy (d, x.path, sizeof d); char *sl = strrchr (d, '/'); if (sl) *sl = 0; fa_open (d); break; }
	}
	refresh_all ();
}

static void layout_parts ()
{
	int W = g_root->width, H = g_root->height;
	g_watch->hidden = true;
	if (g_miniMode)
	{
		g_side->hidden = g_top->hidden = g_content->hidden = g_now->hidden = true;
		g_mini->hidden = false; g_mini->left = 0; g_mini->top = 0; g_mini->resizeTo (W, H);
		return;
	}
	g_mini->hidden = true;
	if (page ().kind == P_WATCH)
	{	// a video: the whole window
		g_side->hidden = g_top->hidden = g_content->hidden = g_now->hidden = true;
		g_watch->hidden = false; g_watch->left = 0; g_watch->top = 0; g_watch->resizeTo (W, H);
		return;
	}
	g_side->hidden = g_top->hidden = g_content->hidden = g_now->hidden = false;
	g_top->left = 0; g_top->top = 0; g_top->resizeTo (W, TOP_H);			// (the bar across the window, the sidebar under it)
	g_side->left = 0; g_side->top = TOP_H; g_side->resizeTo (SIDE_W, H - TOP_H - NOW_H);
	g_top->search->left = g_top->width - 300 - (page ().kind == P_ALBUMS ? 0 : -60); g_top->search->top = 12;
	g_content->left = SIDE_W; g_content->top = TOP_H; g_content->resizeTo (W - SIDE_W, H - TOP_H - NOW_H);
	g_now->left = 0; g_now->top = H - NOW_H; g_now->resizeTo (W, NOW_H);
}
static void refresh_all ()
{
	if (!g_root) return;
	layout_parts ();
	g_side->invalidate (true); g_top->invalidate (true); g_content->invalidate (true); g_now->invalidate (true); g_mini->invalidate (true); g_watch->invalidate (true);
	g_root->invalidate (false);
}
static void resize_to (int cw, int ch, int x, int y)
{
	int stride = cw;
	unsigned *fb = kapi_resize_window2 (cw, ch, &stride);
	if (!fb) return;
	g_root->canvas.adopt (fb, cw, ch, stride);
	g_root->width = cw; g_root->height = ch;
	uk_decorate_window ();
	kapi_move_window (x, y);
	refresh_all ();
}
static void enter_mini (bool on)
{
	if (on == g_miniMode) return;
	if (on && page ().kind == P_WATCH) go_back_page ();		// (the mini player is the music's)
	struct kapi_win_geom g;
	if (kapi_win_geometry (&g) != 0) return;
	if (on)
	{
		if (g_root->maximised ()) g_root->maximise (false), kapi_win_geometry (&g);
		g_restore[0] = g.x; g_restore[1] = g.y; g_restore[2] = g.cw; g_restore[3] = g.ch;
		int fw = g.w - g.cw, fh_ = g.h - g.ch, cw = 330, ch = 92;
		g_miniMode = true;
		resize_to (cw, ch, g.ax + g.aw - cw - fw - 10, g.ay + g.ah - ch - fh_ - 10);
	}
	else { g_miniMode = false; resize_to (g_restore[2], g_restore[3], g_restore[0], g_restore[1]); }
}

// ---- the library: loaded, scanned, swapped ------------------------------------------------------------------------
static void remap_page (Page &p, Library *o, Library *n)
{
	const char *name = 0;
	switch (p.kind)
	{
	case P_ALBUM: if (p.arg >= 0 && p.arg < o->nal) { const Album &a = o->al[p.arg]; p.arg = -1; for (int i = 0; i < n->nal; i++) if (!strcmp (n->al[i].title, a.title) && !strcmp (n->al[i].artist, a.artist)) p.arg = i; } break;
	case P_ARTIST: name = p.arg >= 0 && p.arg < o->nar ? o->ar[p.arg].name : 0; p.arg = -1; for (int i = 0; name && i < n->nar; i++) if (!strcmp (n->ar[i].name, name)) p.arg = i; break;
	case P_GENRE: name = p.arg >= 0 && p.arg < o->nge ? o->ge[p.arg].name : 0; p.arg = -1; for (int i = 0; name && i < n->nge; i++) if (!strcmp (n->ge[i].name, name)) p.arg = i; break;
	case P_FOLDER: name = p.arg >= 0 && p.arg < o->nfo ? o->fo[p.arg].name : 0; p.arg = -1; for (int i = 0; name && i < n->nfo; i++) if (!strcmp (n->fo[i].name, name)) p.arg = i; break;
	default: return;
	}
	if (p.arg < 0) { p.kind = P_HOME; p.arg = 0; }
}
static void scan_done (void *, long)
{
	if (!g_scan) return;
	kapi_thread_join (g_scanTid, 1000, 0); g_scanTid = -1;
	Library *n = g_scan->fresh, *o = L;
	n->build (); n->index_paths ();
	// what the user did meanwhile (plays, favourites): from the library shown
	for (int i = 0; i < n->n; i++) { int k = o->find (n->s[i].path); if (k >= 0) { n->s[i].plays = o->s[k].plays; n->s[i].fav = o->s[k].fav; n->s[i].lastPlayed = o->s[k].lastPlayed; } }
	if (!o->n) n->load_stats ();				// (a first scan: the plays kept from before, if any)
	auto remap = [&] (int id) { if (id < 0) return id; int k = n->find (o->s[id].path); return k; };
	for (int i = 0; i < g_queue.n; i++) g_queue[i] = remap (g_queue[i]);
	for (int i = 0; i < g_qorig.n; i++) g_qorig[i] = remap (g_qorig[i]);
	if (g_playing >= 0) g_playing = remap (g_playing);
	for (int i = 0; i < g_nhist; i++) remap_page (g_hist[i], o, n);
	L = n;
	// the videos: what was done meanwhile kept (where one was left), those opened from outside kept
	{
		VideoLib *vn = g_scan->vfresh, *vo = VL;
		vn->index_paths ();
		for (int i = 0; i < vn->n; i++) { int k = vo->find (vn->v[i].path); if (k >= 0) { vn->v[i].posMs = vo->v[k].posMs; vn->v[i].watched = vo->v[k].watched; vn->v[i].lastPlayed = vo->v[k].lastPlayed; } }
		for (int i = 0; i < vo->n; i++) if (vo->v[i].ext && vn->find (vo->v[i].path) < 0) { Video &x = vn->add (); vn->copy_from (x, vo->v[i]); }
		vn->index_paths ();
		auto vremap = [&] (int id) { return id >= 0 && id < vo->n ? vn->find (vo->v[id].path) : -1; };
		if (g_vidx >= 0) g_vidx = vremap (g_vidx);
		for (int i = 0; i < g_nhist; i++) if (g_hist[i].kind == P_WATCH) { g_hist[i].arg = vremap (g_hist[i].arg); if (g_hist[i].arg < 0) g_hist[i] = Page { P_HOME, 0, 0, 0, false }; }
		if (g_vidx < 0 && g_vp.active ()) g_vp.close ();
		VL = vn; g_thumbs.set_library (vn);
		vn->save ();
		delete vo; delete g_scan->vold;
	}
	g_covers.set_library (n);
	g_rollFor = -2;
	n->save ();
	delete o;
	delete g_scan; g_scan = 0;
	if ((L->n || VL->n) && page ().kind == P_WELCOME && g_nhist == 1) { g_hist[0] = Page { P_HOME, 0, 0, 0, false }; }
	g_content->rowsValid = false; g_content->reset_selection (); g_content->selCap = 0; free (g_content->sel); g_content->sel = 0;
	refresh_all ();
}
static int scan_main (void *p) { scan_thread (p); kapi_post (scan_done, 0, 0); return 0; }
void start_scan ()
{
	if (g_scan) { g_scan->cancel = true; kapi_thread_join (g_scanTid, 5000, 0); delete g_scan->fresh; delete g_scan->vfresh; delete g_scan->vold; delete g_scan; g_scan = 0; }
	g_scan = new ScanState;
	memset (g_scan, 0, sizeof *g_scan);
	bool videos = false;
	for (int i = 0; i < g_nfolders; i++) { scopy (g_scan->roots[g_scan->nroots++], g_folders[i], 200); if (!strcasecmp (g_folders[i], VIDEO_DIR)) videos = true; }
	if (!videos && g_scan->nroots < 8) scopy (g_scan->roots[g_scan->nroots++], VIDEO_DIR, 200);	// (SD:/Videos: always looked at)
	g_scan->old = L; g_scan->fresh = new Library;
	g_scan->vold = VL->clone (); g_scan->vfresh = new VideoLib;
	L->index_paths (); VL->index_paths ();
	g_scanTid = kapi_thread_create (scan_main, g_scan, 512 * 1024, "scan");
	if (g_scanTid < 0) { scan_thread (g_scan); scan_done (0, 0); }
}
static void on_cover () { if (g_content) { g_content->invalidate (true); g_now->invalidate (true); g_mini->invalidate (true); g_watch->invalidate (true); g_root->invalidate (false); } }

// ---- the window ----------------------------------------------------------------------------------------------
class MediaRoot : public Root
{
public:
	int lastSec, lastState, ended, errs; unsigned lastAnim, lastSide; char lastSearch[128];
	MediaRoot (int w, int h) : Root (w, h, "Media Player"), lastSec (-1), lastState (-1), ended (0), errs (0), lastAnim (0), lastSide (0) { lastSearch[0] = 0; }
	void onResized () override { refresh_all (); }
	void onTick () override
	{
		g_tick = kapi_get_ticks ();
		if (g_vp.active ())
		{	// the video: its frame due, its status; where it is, saved now and then
			static unsigned gen = ~0u;
			bool nf = g_vp.poll ();
			bool watching = page ().kind == P_WATCH && !g_miniMode;
			if (watching && (nf || g_vp.statusGen != gen || g_watch->shown != watch_controls (g_watch->u) || (!g_vp.frame && !g_vp.st.error) || g_vp.st.seeking || g_vp.st.waiting))
				g_watch->invalidate (true);
			gen = g_vp.statusGen;
			if (g_vp.st.ended && !g_vpEnded) { g_vpEnded = true; video_save_pos (); }
			if (g_tick - g_vpSaveT > 1500) { g_vpSaveT = g_tick; if (!g_vp.st.paused) video_save_pos (); }
			g_thumbs.busy = !g_vp.st.paused;
		}
		if (g_player.endedGen != ended) { ended = g_player.endedGen; play_next (false); }
		if (g_player.errGen != errs) { errs = g_player.errGen; notify ("Media Player", g_player.err); g_playing = g_player.state == PS_STOPPED && g_playing >= 0 ? g_playing : g_playing; refresh_all (); }
		int sec = (int) (g_player.posMs / 1000);
		if (sec != lastSec || g_player.state != lastState)
		{
			lastSec = sec; lastState = g_player.state;
			g_now->invalidate (true); g_mini->invalidate (true);
			// a play counted: half the song, or 4 minutes, heard
			if (!g_counted && g_playing >= 0 && g_player.lenMs > 0 && (g_player.posMs * 2 >= g_player.lenMs || g_player.posMs > 240000)) count_play ();
		}
		// what moves: the bars of the song playing (5 a second), the notes of a MIDI file (30 a second)
		bool playing = g_player.state == PS_PLAYING;
		const Song *s = song (g_playing);
		bool roll = s && s->fmt == FMT_MIDI && (page ().kind == P_NOW || (page ().kind == P_ALBUM && page ().arg == s->alb && g_playing >= 0));
		unsigned every = roll ? 3 : 20;
		if (playing && g_tick - lastAnim >= every && !g_miniMode) { lastAnim = g_tick; g_content->invalidate (true); }
		if (g_scan && g_tick - lastSide >= 50) { lastSide = g_tick; g_side->invalidate (true); if (page ().kind == P_WELCOME) g_content->invalidate (true); }
		// the search
		const char *q = g_top->search->text;
		if (strcmp (q, lastSearch))
		{
			scopy (lastSearch, q, sizeof lastSearch); scopy (g_search, q, sizeof g_search);
			if (q[0]) { if (page ().kind == P_SEARCH) { g_content->rowsValid = false; page ().scroll = 0; refresh_all (); } else navigate (P_SEARCH); }
			else if (page ().kind == P_SEARCH) go_back ();
		}
		if (!valid) {}
	}
	bool onKey (long k) override
	{
		if (page ().kind == P_WATCH && !g_miniMode)
		{
			if (k == 27 || k == KEY_BACKSPACE) { go_back_page (); return true; }
			if (watch_key (k, false)) { g_watch->invalidate (true); return true; }
			return false;
		}
		if (k == ' ' && !g_top->search->hasFocus) { toggle_play (); return true; }
		if (k == 27 && page ().kind == P_NOW) { go_back (); return true; }
		if (k == 27 && g_top->search->text[0]) { g_top->search->setText (""); return true; }
		if (!g_top->search->hasFocus && g_content->onKey (k)) return true;
		return false;
	}
};

static void m_play () { toggle_play (); }
static void m_next () { play_next (true); }
static void m_prev () { play_prev (); }
static void m_shuffle () { set_shuffle (!g_shuffle); refresh_all (); }
static void m_repeat () { g_repeat = (g_repeat + 1) % 3; save_settings (); refresh_all (); }
static void m_home () { navigate (P_HOME); }
static void m_albums () { navigate (P_ALBUMS); }
static void m_artists () { navigate (P_ARTISTS); }
static void m_songs () { navigate (P_SONGS); }
static void m_now () { navigate (P_NOW); }
static void m_mini () { enter_mini (!g_miniMode); }
static void m_search () { if (g_miniMode) enter_mini (false); g_top->search->setFocus (); }
static void m_folders () { navigate (P_WELCOME); }
static void m_rescan () { start_scan (); refresh_all (); }
static void m_newpl () { new_playlist_dialog (0); }
static void m_back () { go_back (); }
static void m_films () { navigate (P_VIDEOS, VK_FILM); }
static void m_clips () { navigate (P_VIDEOS, VK_CLIP); }
static void m_full () { if (page ().kind == P_WATCH) video_full_screen (); }
static void m_open ()
{
	char p[300];
	if (!uk_file_open (p, sizeof p, g_nfolders ? g_folders[0] : "SD:/")) return;
	extern void open_file (const char *);
	open_file (p);
}
void m_open_video ()
{
	char p[300];
	if (!uk_file_open (p, sizeof p, VIDEO_DIR)) return;
	extern void open_file (const char *);
	open_file (p);
}
void open_file (const char *p)
{
	if (is_video_path (p))
	{	// a video: from the library, else read now (kept for this run)
		int vi = VL->find (p);
		if (vi < 0)
		{
			Video &x = VL->add ();
			if (!probe_video (p, &x)) { VL->n--; uk_messagebox ("Media Player", "This file is not a video Media Player\ncan read.", MB_OK); return; }
			char t[200]; video_names (p, t, sizeof t, &x.kind, &x.season, &x.episode);
			if (x.kind < 0) x.kind = x.durMs >= 40 * 60000 ? VK_FILM : VK_CLIP;
			x.path = sdup (p); x.title = sdup (t); x.ext = true; x.added = now_stamp ();
			vi = VL->n - 1;
			VL->index_paths ();
		}
		play_video (vi, false);
		return;
	}
	if (!strcasecmp (ext_of (p), "m3u") || !strcasecmp (ext_of (p), "m3u8"))
	{	// a playlist: its songs that are in the library, in its order
		Plist pl; scopy (pl.path, p, sizeof pl.path); playlist_read (pl);
		IntList l; for (int i = 0; i < pl.n; i++) { int id = L->find (pl.paths[i]); if (id >= 0) l.push (id); free (pl.paths[i]); }
		free (pl.paths);
		if (l.n) play_list (l, 0, false);
		else uk_messagebox ("Media Player", "None of this playlist's songs is in the library (Folders to Watch...).", MB_OK);
		return;
	}
	int id = L ? L->find (p) : -1;
	if (id >= 0)
	{
		const Song &s = L->s[id]; IntList l; int st = 0;
		for (int i = 0; s.alb >= 0 && i < L->al[s.alb].n; i++) { if (L->al[s.alb].songs[i] == id) st = i; l.push (L->al[s.alb].songs[i]); }
		play_list (l, st, false);
		return;
	}
	Tags t;
	if (!read_tags (p, &t)) { uk_messagebox ("Media Player", "This file cannot be played (MP3, OGG,\nFLAC, WAV, MIDI and FM songs are; MP4,\nMKV, WebM, AVI, WMV... videos).", MB_OK); return; }
	int k = g_next < 16 ? g_next++ : 15;
	Song &x = g_ext[k];
	if (x.path) { free (x.path); free (x.title); free (x.artist); free (x.albumArtist); free (x.album); free (x.genre); free (x.folderCover); }
	memset (&x, 0, sizeof x);
	x.path = sdup (p); x.title = sdup (t.title); x.artist = sdup (t.artist); x.albumArtist = sdup (t.albumArtist); x.album = sdup (t.album);
	x.genre = sdup (t.genre); x.folderCover = sdup (""); x.durMs = t.durMs; x.fmt = t.fmt; x.alb = x.art = x.gen = x.fold = -1;
	IntList l; l.push (-k - 1);
	play_list (l, 0, false);
}

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);
	faces_open ();
	load_settings ();
	ak_soundfont_prefer (g_sfPath);			// (the MIDI files: AudioKit plays them)
	kapi_mkdir (LIB_DIR);

	L = new Library;
	L->load (); L->load_stats (); L->build (); L->index_paths ();
	VL = new VideoLib;
	VL->load (); VL->index_paths ();
	playlists_load ();
	g_hist[0] = Page { L->n || VL->n ? P_HOME : P_WELCOME, 0, 0, 0, false }; g_nhist = 1; g_hpos = 0;

	MediaRoot root (1000, 640);
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	root.setResizable (true);
	root.setBg (C_BG);

	g_side = new Sidebar (0, TOP_H, SIDE_W, 640 - TOP_H - NOW_H); root.addChild (g_side);
	g_top = new TopBar (0, 0, 1000, TOP_H); root.addChild (g_top);
	{	// the search field: the one with a placeholder
		g_top->removeChild (g_top->search); delete g_top->search;
		g_top->search = new SearchBox (g_top->width - 300, 12, 230, 28); g_top->addChild (g_top->search);
	}
	g_content = new Content (SIDE_W, TOP_H, 1000 - SIDE_W, 640 - TOP_H - NOW_H); root.addChild (g_content);
	g_now = new NowBar (0, 640 - NOW_H, 1000, NOW_H); root.addChild (g_now);
	g_mini = new MiniView (0, 0, 330, 92); g_mini->hidden = true; root.addChild (g_mini);
	g_watch = new WatchView (0, 0, 1000, 640); g_watch->hidden = true; root.addChild (g_watch);

	g_covers.lib = L; g_covers.onLoaded = on_cover; g_covers.start ();
	g_thumbs.lib = VL; g_thumbs.onLoaded = on_cover; g_thumbs.start ();
	g_player.volume = g_volume; g_player.start ();

	static Menu menu;
	menu.menu ("File");
	menu.item ("Open a File...", "^O", UK_CTRL ('O'), m_open);
	menu.item ("Open a Video...", "", 0, m_open_video);
	menu.separator ();
	menu.item ("Folders to Watch...", "", 0, m_folders);
	menu.item ("Look for New Songs", "", 0, m_rescan);
	menu.separator ();
	menu.item ("New Playlist...", "", 0, m_newpl);
	menu.menu ("Play");
	menu.item ("Play / Pause", "^P", UK_CTRL ('P'), m_play);
	menu.item ("Next", "^F", UK_CTRL ('F'), m_next);
	menu.item ("Previous", "^B", UK_CTRL ('B'), m_prev);
	menu.separator ();
	menu.item ("Shuffle", "^S", UK_CTRL ('S'), m_shuffle);
	menu.item ("Repeat (all, one, off)", "^T", UK_CTRL ('T'), m_repeat);
	menu.separator ();
	menu.item ("Full Screen (a video)", "F", 0, m_full);
	menu.menu ("View");
	menu.item ("Home", "", 0, m_home);
	menu.item ("Albums", "", 0, m_albums);
	menu.item ("Artists", "", 0, m_artists);
	menu.item ("Songs", "", 0, m_songs);
	menu.item ("Films", "", 0, m_films);
	menu.item ("Clips and Series", "", 0, m_clips);
	menu.item ("Now Playing", "^L", UK_CTRL ('L'), m_now);
	menu.separator ();
	menu.item ("Search", "^E", UK_CTRL ('E'), m_search);
	menu.item ("Back", "", 0, m_back);
	menu.separator ();
	menu.item ("Mini Player", "^K", UK_CTRL ('K'), m_mini);
	menu.publish ();

	refresh_all ();
	root.fitWorkArea ();

	char args[300]; int na = kapi_get_args (args, sizeof args); args[na > 0 && na < 300 ? na : 0] = 0;
	char *a = args; while (*a == ' ') a++;
	int al = (int) strlen (a); while (al && a[al - 1] == ' ') a[--al] = 0;
	if (a[0] == '"') { a++; char *q = strchr (a, '"'); if (q) *q = 0; }
	if (a[0]) open_file (a);

	start_scan ();
	root.run ();

	count_play ();
	video_close ();
	g_player.quit (); g_covers.quit (); g_thumbs.quit ();
	if (g_scan) { g_scan->cancel = true; kapi_thread_join (g_scanTid, 3000, 0); }
	return 0;
}
