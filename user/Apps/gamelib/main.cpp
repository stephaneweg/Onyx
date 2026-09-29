//
// gamelib -- the Game Library: every GameCube, Nintendo 64, Super Nintendo, Game Boy Advance / Color,
// Game Boy and NES game of the watched folders (and their sub-folders), a card each -- a picture of
// its title screen and its name. Laid out as the File Viewer:
//   * on the left the SIDEBAR, in three groups a click on their title folds: Library (All Games),
//     Systems -- the emulators: each with its icon and its number of games; a click shows its
//     games only -- and Folders (the watched folders, their games; Add Folder...; a right click
//     on one: Show / Remove Folder);
//   * above, the path bar ("Game Library > Super Nintendo", the number of games; a click on
//     "Game Library" shows them all); below, the status bar (the game chosen; the picture being
//     made);
//   * the cards, one section per system. A click selects one, a double-click (or Enter) plays it:
//     the ROM opens with its emulator (SD:/etc/runners.ini: gbemu, gbaemu, nesemu, snesemu,
//     n64emu, gcemu), in a window or, with View > Play Full Screen, on the whole display. A right
//     click: Play, Play Full Screen, Show in File Viewer. Keys: the arrows, Enter, PgUp / PgDn,
//     Tab (the next system). A USB gamepad: the d-pad moves, Start (or A) plays, L / R the
//     previous / next system, L2 / R2 turn a page.
//
//   * The folders: SD:/roms by default; Folders > Add Folder... / Remove <folder> (any volume: SD:, SD1: ..,
//     e.g. an exFAT partition of the card), kept in config.ini, one `folder = SD1:/games` line each.
//   * The pictures: each game is run a few seconds without being shown (the emulator core,
//     user/gb, user/gba, user/nes, user/snes) and its screen kept -- made in the background, a little every
//     frame, the games shown first, and cached in SD:/apps/gamelib.app/thumbs/ (Library > Refresh finds
//     new ROMs). A Nintendo 64 game gets a label with the name from its header, a GameCube disc its banner.
//   * The emulators are in no menu of the desktop: they are reached from here (one started
//     without a ROM opens the Game Library).
//
#include "kapi.h"
#include "applib.h"
#include "launch.h"
#include "gamepad.h"
#include "bmp.hpp"
#include "wtk/wtk.h"
#include "gb/gb.h"
#include "gba/gba.h"
#include "nes/nes.h"
#include "snes/snes.h"

using namespace wtk;

#define WIN_W	980			// (four columns of cards beside the sidebar)
#define WIN_H	580
#define SIDE_W	220			// the sidebar: the systems, the folders
#define BAR_H	46			// the path bar
#define ST_H	22			// the status bar
#define SIDE_Y	(BAR_H + 4)
#define SIDE_RH	27
#define SICON	22			// a system's icon (its emulator's, small)
#define TW	160			// a tile's picture: the Game Boy screen at 1x
#define TH	144
#define CELLW	(TW + 24)
#define CELLH	(TH + 44)
#define HEAD_H	34			// a section's title
#define MAXG	256
#define THUMB_FRAMES	420		// ~7 s of game time: past the logos, on the title screen

enum { SYS_GC, SYS_N64, SYS_SNES, SYS_GBA, SYS_GBC, SYS_GB, SYS_NES, NSYS };		// the sections, in this order
static const char *const SYS_NAME[NSYS] = { "GameCube", "Nintendo 64", "Super Nintendo", "Game Boy Advance", "Game Boy Color", "Game Boy", "NES" };
static const char *const SYS_EXT[NSYS] = { ".iso / .gcm", ".z64 / .n64 / .v64", ".sfc / .smc", ".gba", ".gbc", ".gb", ".nes" };
static const char *const SYS_EMU[NSYS] = { "gcemu", "n64emu", "snesemu", "gbaemu", "gbemu", "gbemu", "nesemu" };
struct Game { char path[200]; char name[64]; char key[64]; int sys; unsigned *thumb; bool tried; };
static Game g_games[MAXG]; static int g_ng = 0;
enum { MAXF = 8 };
static char g_folder[MAXF][200] = { "SD:/roms" }; static int g_nf = 1;	// the watched folders
static bool g_full = false;
static int g_scroll = 0, g_sel = 0, g_hover = -1;		// (g_sel, g_hover: indexes in g_vis)
static Root *g_root = 0;
static int g_vis[MAXG], g_nvis;			// the games shown (g_games indexes), in their order
static void refilter (void);

static int slen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scpy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static char low (char c) { return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c; }
static bool ends (const char *s, const char *e) { int n = slen (s), m = slen (e); if (n < m) return false; for (int i = 0; i < m; i++) if (low (s[n - m + i]) != e[i]) return false; return true; }
static bool ci_less (const char *a, const char *b) { for (;; a++, b++) { char x = low (*a), y = low (*b); if (x != y || !x) return x < y; } }

// "ZeldaOracleOfSeason.gbc" -> "Zelda Oracle Of Season"; "super_mario_land" -> "Super mario land"
static void nice_name (const char *file, char *out, int cap)
{
	int n = 0; const char *p = file;
	for (const char *q = file; *q; q++) if (*q == '/' || *q == ':') p = q + 1;
	int e = slen (p); while (e > 0 && p[e - 1] != '.') e--;
	if (e > 0) e--; else e = slen (p);
	for (int i = 0; i < e && n < cap - 2; i++)
	{
		char c = p[i];
		if (c == '_' || c == '-') c = ' ';
		if (i > 0 && c >= 'A' && c <= 'Z' && p[i - 1] >= 'a' && p[i - 1] <= 'z') out[n++] = ' ';
		if (n == 0 && c >= 'a' && c <= 'z') c = (char) (c - 32);
		out[n++] = c;
	}
	out[n] = 0;
}

// ---- the ROMs ------------------------------------------------------------------------------------
static void scan (const char *dir, int depth)
{
	if (depth > 6) return;
	void *d = kapi_opendir (dir);
	if (!d) return;
	struct kapi_dirent e;
	while (kapi_readdir (d, &e) && g_ng < MAXG)
	{
		if (e.name[0] == '.') continue;
		char p[200]; int n = 0; lx_cat (p, sizeof p, &n, dir); if (n && p[n - 1] != '/') lx_cat (p, sizeof p, &n, "/"); lx_cat (p, sizeof p, &n, e.name);
		if (e.is_dir) { scan (p, depth + 1); continue; }
		bool gbc = ends (e.name, ".gbc"), gbf = ends (e.name, ".gb"), agb = ends (e.name, ".gba"), nes = ends (e.name, ".nes");
		bool sfc = ends (e.name, ".sfc") || ends (e.name, ".smc");
		bool n64 = ends (e.name, ".z64") || ends (e.name, ".n64") || ends (e.name, ".v64");
		bool gcn = ends (e.name, ".iso") || ends (e.name, ".gcm");
		if (!gbc && !gbf && !agb && !nes && !sfc && !n64 && !gcn) continue;
		Game &g = g_games[g_ng++];
		scpy (g.path, p, sizeof g.path);
		nice_name (e.name, g.name, sizeof g.name);
		scpy (g.key, e.name, sizeof g.key);
		g.sys = gcn ? SYS_GC : n64 ? SYS_N64 : sfc ? SYS_SNES : agb ? SYS_GBA : gbc ? SYS_GBC : nes ? SYS_NES : SYS_GB; g.thumb = 0; g.tried = false;
	}
	kapi_closedir (d);
}
static void rescan (void)
{
	for (int i = 0; i < g_ng; i++) delete [] g_games[i].thumb;
	g_ng = 0;
	for (int f = 0; f < g_nf; f++) scan (g_folder[f], 0);
	// by system (Super Nintendo, Advance, Color, Game Boy, NES), then by name
	for (int i = 1; i < g_ng; i++)
	{
		Game t = g_games[i]; int j = i;
		while (j > 0 && (g_games[j - 1].sys > t.sys || (g_games[j - 1].sys == t.sys && ci_less (t.name, g_games[j - 1].name)))) { g_games[j] = g_games[j - 1]; j--; }
		g_games[j] = t;
	}
	g_sel = 0; g_scroll = 0;
	refilter ();
}

// ---- the pictures: cached raw 160 x 144 x RGB ------------------------------------------------------
static void thumb_path (const Game &g, char *out, int cap)
{
	int n = 0; lx_cat (out, cap, &n, "SD:/apps/gamelib.app/thumbs/"); lx_cat (out, cap, &n, g.key); lx_cat (out, cap, &n, ".thm");
}
static bool thumb_load (Game &g)
{
	char p[260]; thumb_path (g, p, sizeof p);
	void *f = kapi_open (p); if (!f) return false;
	static unsigned char b[TW * TH * 3];
	int r = kapi_read (f, b, sizeof b); kapi_close (f);
	if (r != (int) sizeof b) return false;
	g.thumb = new unsigned[TW * TH];
	for (int i = 0; i < TW * TH; i++) g.thumb[i] = ((unsigned) b[i * 3] << 16) | ((unsigned) b[i * 3 + 1] << 8) | b[i * 3 + 2];
	return true;
}
static void thumb_save (const Game &g)
{
	kapi_mkdir ("SD:/apps/gamelib.app/thumbs");
	static unsigned char b[TW * TH * 3];
	for (int i = 0; i < TW * TH; i++) { b[i * 3] = (unsigned char) (g.thumb[i] >> 16); b[i * 3 + 1] = (unsigned char) (g.thumb[i] >> 8); b[i * 3 + 2] = (unsigned char) g.thumb[i]; }
	char p[260]; thumb_path (g, p, sizeof p);
	kapi_save_file (p, (const char *) b, sizeof b);
}

// The picture being made: one game at a time -- its ROM read a piece per call (a GBA ROM is up
// to 32 MB), then some frames run per call. A GBA screen (240 x 160) is shrunk to 160 x 107, a
// NES one (256 x 240) to 154 x 144, a Super Nintendo one (256 x 224) to 160 x 140.
static gb::Machine *g_tm = 0; static gba::Machine *g_tma = 0; static nes::Machine *g_tmn = 0; static snes::Machine *g_tms = 0;
static unsigned char *g_trom = 0; static unsigned g_tsize = 0, g_tread = 0; static void *g_tfile = 0;
static int g_tgame = -1, g_tframe = 0; static bool g_tloaded = false;
// A Nintendo 64 game (too big to run here): a cartridge label -- the name in its header.
static void n64_thumb (Game &g)
{
	unsigned char h[64];
	void *f = kapi_open (g.path); if (!f) return;
	int r = kapi_read (f, h, sizeof h); kapi_close (f);
	if (r != (int) sizeof h) return;
	char name[21];
	for (int i = 0; i < 20; i++)
	{
		int k = 0x20 + i;
		if (h[0] == 0x37) k ^= 1;					// .v64: 16-bit swapped
		else if (h[0] == 0x40) k ^= 3;					// .n64: 32-bit swapped
		name[i] = (char) h[k] >= ' ' && (char) h[k] < 127 ? (char) h[k] : ' ';
	}
	name[20] = 0;
	for (int i = 19; i >= 0 && name[i] == ' '; i--) name[i] = 0;
	g.thumb = new unsigned[TW * TH];
	Canvas c; c.adopt (g.thumb, TW, TH);
	c.clear (0x00202438);
	c.fillRect (8, 8, TW - 16, TH - 16, 0x00383C58);
	c.fillRect (8, 8, TW - 16, 22, 0x00C02020);
	c.text (14, 11, "NINTENDO 64", 0x00FFFFFF);
	int n = slen (name), y = 50;
	for (int s0 = 0; s0 < n && y < TH - 20; y += 18)			// the name, a word wrap at 17 characters
	{
		int e = s0 + 17 < n ? s0 + 17 : n;
		if (e < n) { int b = e; while (b > s0 && name[b] != ' ') b--; if (b > s0) e = b; }
		char line[24]; int k = 0; for (int i = s0; i < e && k < 23; i++) line[k++] = name[i]; line[k] = 0;
		c.text (14, y, line, 0x00F0E8C0);
		s0 = e; while (s0 < n && name[s0] == ' ') s0++;
	}
	thumb_save (g);
}

// A GameCube disc (too big to run here): its banner (opening.bnr, found in the disc's file
// table: 96 x 32 pixels RGB5A3) and its full name.
static unsigned be32 (const unsigned char *p) { return (unsigned) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static bool disc_read (void *f, unsigned off, void *dst, unsigned n) { return kapi_seek (f, off) == 0 && kapi_read (f, dst, n) == (int) n; }
static void gc_thumb (Game &g)
{
	void *f = kapi_open (g.path); if (!f) return;
	static unsigned char hdr[0x440];
	unsigned char *fst = 0, *bnr = 0;
	if (!disc_read (f, 0, hdr, sizeof hdr) || be32 (hdr + 0x1C) != 0xC2339F3D) { kapi_close (f); return; }
	unsigned fstOff = be32 (hdr + 0x424), fstSize = be32 (hdr + 0x428);
	if (fstSize > 0 && fstSize < 0x100000)
	{
		fst = new unsigned char[fstSize];
		if (!disc_read (f, fstOff, fst, fstSize)) { delete [] fst; fst = 0; }
	}
	if (fst)
	{
		unsigned n = be32 (fst + 8);
		const char *names = (const char *) fst + n * 12;
		for (unsigned i = 1; i < n && (i + 1) * 12 <= fstSize; i++)
		{
			const unsigned char *e = fst + i * 12;
			if (e[0] != 0) continue;					// (a folder)
			unsigned no = be32 (e) & 0xFFFFFF;
			if ((const unsigned char *) names + no + 12 > fst + fstSize) continue;
			const char *nm = names + no; const char *want = "opening.bnr"; int k = 0;
			while (want[k] && low (nm[k]) == want[k]) k++;
			if (want[k] || nm[k]) continue;
			bnr = new unsigned char[0x1960];
			if (!disc_read (f, be32 (e + 4), bnr, 0x1960)) { delete [] bnr; bnr = 0; }
			break;
		}
		delete [] fst;
	}
	kapi_close (f);
	g.thumb = new unsigned[TW * TH];
	Canvas c; c.adopt (g.thumb, TW, TH);
	c.clear (0x00302848);
	c.fillRect (8, 8, TW - 16, 20, 0x006A5ACD);
	c.text (14, 10, "GAMECUBE", 0x00FFFFFF);
	char name[65]; int nl = 0;
	if (bnr)
	{
		// the banner: 4 x 4 tiles of RGB5A3, 1.5 times its size, over the dark background
		for (int ty = 0; ty < 32; ty += 4)
			for (int tx = 0; tx < 96; tx += 4)
				for (int y = 0; y < 4; y++)
					for (int x = 0; x < 4; x++)
					{
						const unsigned char *q = bnr + 0x20 + ((ty / 4) * 24 + tx / 4) * 32 + (y * 4 + x) * 2;
						unsigned v = (unsigned) q[0] << 8 | q[1], col;
						if (v & 0x8000) col = ((v >> 10) & 31) << 19 | ((v >> 5) & 31) << 11 | (v & 31) << 3;
						else
						{
							unsigned a = (v >> 12) & 7;
							if (a < 3) continue;
							col = ((v >> 8) & 15) * 17 << 16 | ((v >> 4) & 15) * 17 << 8 | (v & 15) * 17;
						}
						int px = 8 + (tx + x) * 3 / 2, py = 34 + (ty + y) * 3 / 2;
						for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++)
							if (px + dx < TW && py + dy < TH) g.thumb[(py + dy) * TW + px + dx] = col;
					}
		for (; nl < 64 && bnr[0x1860 + nl] >= ' '; nl++) name[nl] = (char) bnr[0x1860 + nl];
		if (!nl) for (; nl < 32 && bnr[0x1820 + nl] >= ' '; nl++) name[nl] = (char) bnr[0x1820 + nl];
		delete [] bnr;
	}
	if (!nl) for (; nl < 64 && hdr[0x20 + nl] >= ' '; nl++) name[nl] = (char) hdr[0x20 + nl];
	name[nl] = 0;
	int y = 88;
	for (int s0 = 0; s0 < nl && y < TH - 14; y += 16)			// the name, a word wrap at 17 characters
	{
		int e = s0 + 17 < nl ? s0 + 17 : nl;
		if (e < nl) { int b = e; while (b > s0 && name[b] != ' ') b--; if (b > s0) e = b; }
		char line[24]; int k = 0; for (int i = s0; i < e && k < 23; i++) line[k++] = name[i]; line[k] = 0;
		c.text (10, y, line, 0x00F0E8C0);
		s0 = e; while (s0 < nl && name[s0] == ' ') s0++;
	}
	thumb_save (g);
}

static void thumb_work (void)
{
	if (g_tgame < 0)
	{
		for (int pass = 0; pass < 2; pass++)			// the games shown first, then the others
		for (int j = 0; j < (pass ? g_ng : g_nvis); j++)
		{
			int i = pass ? j : g_vis[j];
			if (!g_games[i].thumb && !g_games[i].tried)
			{
				g_games[i].tried = true;
				if (thumb_load (g_games[i])) { g_root->invalidate (true); return; }
				if (g_games[i].sys == SYS_N64) { n64_thumb (g_games[i]); g_root->invalidate (true); return; }
				if (g_games[i].sys == SYS_GC) { gc_thumb (g_games[i]); g_root->invalidate (true); return; }
				g_tfile = kapi_open (g_games[i].path); if (!g_tfile) return;
				g_tsize = kapi_fsize (g_tfile);
				if (g_tsize > 0x2000000) g_tsize = 0x2000000;
				delete [] g_trom; g_trom = new unsigned char[g_tsize + 1];
				g_tread = 0; g_tloaded = false;
				g_tgame = i; g_tframe = 0;
				g_root->invalidate (true);
				return;
			}
		}
		return;
	}
	Game &g = g_games[g_tgame];
	if (!g_tloaded)						// a piece of the ROM
	{
		unsigned k = g_tsize - g_tread > 0x80000 ? 0x80000 : g_tsize - g_tread;
		int got = k ? kapi_read (g_tfile, g_trom + g_tread, k) : 0;
		if (got > 0) g_tread += (unsigned) got;
		if (got > 0 && g_tread < g_tsize) return;
		kapi_close (g_tfile); g_tfile = 0;
		bool ok;
		if (g.sys == SYS_GBA) { if (!g_tma) { g_tma = new gba::Machine; g_tma->setAudioRate (8000); } ok = g_tma->load (g_trom, (int) g_tread); }
		else if (g.sys == SYS_SNES) { if (!g_tms) { g_tms = new snes::Machine; g_tms->setAudioRate (8000); } ok = g_tms->load (g_trom, (int) g_tread); }
		else if (g.sys == SYS_NES) { if (!g_tmn) { g_tmn = new nes::Machine; g_tmn->setAudioRate (8000); } ok = g_tmn->load (g_trom, (int) g_tread); }
		else { if (!g_tm) { g_tm = new gb::Machine; g_tm->setAudioRate (8000); } ok = g_tm->load (g_trom, (int) g_tread); }
		if (!ok) { g_tgame = -1; return; }
		g_tloaded = true;
		return;
	}
	static short pcm[4096];
	int per = g.sys == SYS_GBA ? 4 : g.sys == SYS_SNES ? 3 : g.sys == SYS_NES ? 6 : 12;
	for (int k = 0; k < per && g_tframe < THUMB_FRAMES; k++, g_tframe++)
	{
		if (g.sys == SYS_GBA) { g_tma->runFrame (); g_tma->audioRead (pcm, 2048); }
		else if (g.sys == SYS_SNES) { g_tms->runFrame (); g_tms->audioRead (pcm, 2048); }
		else if (g.sys == SYS_NES) { g_tmn->runFrame (); g_tmn->audioRead (pcm, 2048); }
		else { g_tm->runFrame (); g_tm->audioRead (pcm, 2048); }
	}
	if (g_tframe >= THUMB_FRAMES)
	{
		g.thumb = new unsigned[TW * TH];
		if (g.sys == SYS_GBA)
		{
			// 240 x 160 -> 160 x 107, centred in the 160 x 144 picture on black
			int oy = (TH - 107) / 2;
			for (int i = 0; i < TW * TH; i++) g.thumb[i] = 0;
			for (int y = 0; y < 107; y++)
				for (int x = 0; x < TW; x++) g.thumb[(oy + y) * TW + x] = g_tma->fb[(y * 160 / 107) * gba::W + x * 3 / 2];
		}
		else if (g.sys == SYS_SNES)
		{
			// 256 x 224 -> 160 x 140, centred on black
			int oy = (TH - 140) / 2;
			for (int i = 0; i < TW * TH; i++) g.thumb[i] = 0;
			for (int y = 0; y < 140; y++)
				for (int x = 0; x < TW; x++) g.thumb[(oy + y) * TW + x] = g_tms->fb[(y * 224 / 140) * snes::W + x * 256 / TW];
		}
		else if (g.sys == SYS_NES)
		{
			// 256 x 240 -> 154 x 144, centred on black
			int ox = (TW - 154) / 2;
			for (int i = 0; i < TW * TH; i++) g.thumb[i] = 0;
			for (int y = 0; y < TH; y++)
				for (int x = 0; x < 154; x++) g.thumb[y * TW + ox + x] = g_tmn->fb[(y * 240 / TH) * nes::W + x * 256 / 154];
		}
		else for (int i = 0; i < TW * TH; i++) g.thumb[i] = g_tm->fb[i];
		thumb_save (g);
		delete [] g_trom; g_trom = 0;				// (a 16 MB ROM: give it back)
		g_tgame = -1;
		g_root->invalidate (true);
	}
}

// ---- what is shown: all the games, a system's, a folder's ----------------------------------------------
enum { F_ALL = 0, F_SYS = 1, F_FOLDER = 1 + NSYS };	// g_filter: F_ALL, F_SYS + a system, F_FOLDER + a folder
static int g_filter = F_ALL;
static int g_nsys[NSYS], g_nfold[MAXF];			// the games of each system, of each folder

static bool in_folder (const Game &g, const char *f)
{
	int n = slen (f);
	while (n > 0 && f[n - 1] == '/') n--;
	for (int i = 0; i < n; i++) if (low (g.path[i]) != low (f[i])) return false;
	return g.path[n] == '/';
}
static bool shown (const Game &g)
{
	if (g_filter >= F_FOLDER) return in_folder (g, g_folder[g_filter - F_FOLDER]);
	if (g_filter >= F_SYS) return g.sys == g_filter - F_SYS;
	return true;
}
static void refilter (void)
{
	if (g_filter >= F_FOLDER && g_filter - F_FOLDER >= g_nf) g_filter = F_ALL;
	g_nvis = 0;
	for (int i = 0; i < g_ng; i++) if (shown (g_games[i])) g_vis[g_nvis++] = i;
	for (int k = 0; k < NSYS; k++) g_nsys[k] = 0;
	for (int f = 0; f < MAXF; f++) g_nfold[f] = 0;
	for (int i = 0; i < g_ng; i++)
	{
		g_nsys[g_games[i].sys]++;
		for (int f = 0; f < g_nf; f++) if (in_folder (g_games[i], g_folder[f])) g_nfold[f]++;
	}
	if (g_sel >= g_nvis) g_sel = g_nvis - 1;
	if (g_sel < 0) g_sel = 0;
}
static void set_filter (int f)
{
	if (f == g_filter) return;
	g_filter = f; g_sel = 0; g_scroll = 0; g_hover = -1;
	refilter ();
	if (g_root) g_root->invalidate (true);
}
static const char *filter_name (void)
{
	if (g_filter >= F_FOLDER) return g_folder[g_filter - F_FOLDER];
	if (g_filter >= F_SYS) return SYS_NAME[g_filter - F_SYS];
	return "All Games";
}
// Tab, a gamepad's shoulders: the next / previous system that has games (All Games between)
static void next_filter (int dir)
{
	int f = g_filter >= F_FOLDER ? F_ALL : g_filter;
	for (int k = 0; k <= NSYS; k++)
	{
		f = (f + dir + NSYS + 1) % (NSYS + 1);			// F_ALL, F_SYS + 0 .. F_SYS + NSYS - 1
		if (f == F_ALL || g_nsys[f - F_SYS] > 0) break;
	}
	set_filter (f);
}

// ---- the grid ---------------------------------------------------------------------------------------
static int grid_x (void) { return SIDE_W + 10; }
static int grid_bottom (void) { return g_root->height - ST_H; }
static int cols (void) { int c = (g_root->width - SIDE_W - 20) / CELLW; return c < 1 ? 1 : c; }
// Layout: a section per system (a title and rows of cards). Card i (of g_vis) -> x, y.
static void tile_pos (int i, int *x, int *y)
{
	int c = cols (), yy = BAR_H + 8, k = 0;
	for (int sec = 0; sec < NSYS; sec++)
	{
		int first = k, n = 0; while (k + n < g_nvis && g_games[g_vis[k + n]].sys == sec) n++;
		if (!n) continue;
		yy += HEAD_H;
		if (i >= first && i < first + n)
		{
			int j = i - first;
			*x = grid_x () + (j % c) * CELLW; *y = yy + (j / c) * CELLH - g_scroll;
			return;
		}
		yy += ((n + c - 1) / c) * CELLH + 10;
		k += n;
	}
	*x = grid_x (); *y = yy - g_scroll;
}
static int content_end (void) { int x, y; tile_pos (g_nvis, &x, &y); return y + g_scroll + 10; }
static int tile_at (int mx, int my)
{
	if (mx < SIDE_W || my < BAR_H || my >= grid_bottom ()) return -1;
	for (int i = 0; i < g_nvis; i++)
	{
		int x, y; tile_pos (i, &x, &y);
		if (mx >= x && mx < x + CELLW - 8 && my >= y && my < y + CELLH - 8) return i;
	}
	return -1;
}
static void ensure_visible (int i)
{
	int x, y; tile_pos (i, &x, &y);
	if (y < BAR_H + 8) g_scroll += y - (BAR_H + 8) - HEAD_H;
	else if (y + CELLH > grid_bottom ()) g_scroll += y + CELLH - grid_bottom () + 8;
	if (g_scroll < 0) g_scroll = 0;
}

static void play (int i)			// (a g_games index)
{
	if (i < 0 || i >= g_ng) return;
	lx_open (g_games[i].path, g_full ? "--fullscreen" : "");
}
static void play_shown (int v) { if (v >= 0 && v < g_nvis) play (g_vis[v]); }

// ---- the sidebar ----------------------------------------------------------------------------------------
enum { G_LIBRARY, G_SYSTEMS, G_FOLDERS, NGROUPS };
static const char *const GROUP_NAME[NGROUPS] = { "Library", "Systems", "Folders" };
static bool g_folded[NGROUPS];
enum { SR_GROUP, SR_ALL, SR_SYS, SR_FOLDER, SR_ADD };
struct SideRow { int kind, index; };
static SideRow g_srow[NGROUPS + 2 + NSYS + MAXF + 1]; static int g_nsrow;
static int g_sideHot = -1, g_crumbHot = -1, g_crumbX = 0;
static void side_rows (void)
{
	g_nsrow = 0;
	for (int gr = 0; gr < NGROUPS; gr++)
	{
		g_srow[g_nsrow++] = { SR_GROUP, gr };
		if (g_folded[gr]) continue;
		if (gr == G_LIBRARY) g_srow[g_nsrow++] = { SR_ALL, 0 };
		else if (gr == G_SYSTEMS) for (int k = 0; k < NSYS; k++) g_srow[g_nsrow++] = { SR_SYS, k };
		else { for (int f = 0; f < g_nf; f++) g_srow[g_nsrow++] = { SR_FOLDER, f }; g_srow[g_nsrow++] = { SR_ADD, 0 }; }
	}
}
static int side_at (int mx, int my)
{
	if (mx < 0 || mx >= SIDE_W - 4 || my < SIDE_Y || my >= grid_bottom ()) return -1;
	int r = (my - SIDE_Y) / SIDE_RH;
	return r >= 0 && r < g_nsrow ? r : -1;
}
static int row_filter (const SideRow &r)
{
	return r.kind == SR_ALL ? F_ALL : r.kind == SR_SYS ? F_SYS + r.index : r.kind == SR_FOLDER ? F_FOLDER + r.index : -1;
}

// A system's icon: its emulator's (SD:/apps/<emulator>.app/icon.bmp), small; its top byte the
// transparency (the icon's magenta: see-through).
static unsigned *g_sysIcon[NSYS]; static bool g_sysTried[NSYS];
static const unsigned *sys_icon (int k)
{
	if (g_sysTried[k]) return g_sysIcon[k];
	g_sysTried[k] = true;
	char q[96]; int n = 0;
	lx_cat (q, sizeof q, &n, "SD:/apps/"); lx_cat (q, sizeof q, &n, SYS_EMU[k]); lx_cat (q, sizeof q, &n, ".app/icon.bmp");
	int sw = 0, sh = 0;
	unsigned *src = ui::bmp_decode (q, &sw, &sh);
	if (!src || sw <= 0 || sh <= 0) { delete [] src; return 0; }
	unsigned *d = g_sysIcon[k] = new unsigned[SICON * SICON];
	for (int j = 0; j < SICON; j++)					// (4 x 4 samples a pixel)
		for (int i = 0; i < SICON; i++)
		{
			unsigned r = 0, g = 0, b = 0, m = 0;
			for (int sy = 0; sy < 4; sy++)
				for (int sx = 0; sx < 4; sx++)
				{
					int x = ((i * 4 + sx) * sw + sw / 2) / (SICON * 4), y = ((j * 4 + sy) * sh + sh / 2) / (SICON * 4);
					unsigned c = src[y * sw + x] & 0xFFFFFF;
					if (c == 0xFF00FF) continue;
					r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; m++;
				}
			d[j * SICON + i] = m == 0 ? 0xFF000000u : ((255 - m * 255 / 16) << 24) | ((r / m) << 16) | ((g / m) << 8) | (b / m);
		}
	delete [] src;
	return d;
}
static void blit_icon (Canvas &cv, int x, int y, const unsigned *px)
{
	if (!px) return;
	for (int j = 0; j < SICON; j++)
		for (int i = 0; i < SICON; i++)
		{
			unsigned c = px[j * SICON + i], t = c >> 24;
			int xx = x + i, yy = y + j;
			if (t == 255 || xx < 0 || yy < 0 || xx >= cv.w || yy >= cv.h) continue;
			unsigned *p = cv.px + (long) yy * cv.stride + xx;
			*p = t == 0 ? (c & 0xFFFFFF) : wk_over (*p, c & 0xFFFFFF, 255 - (int) t);
		}
}

static void on_add ();
static void remove_folder (int f);
static void side_click (int r)
{
	const SideRow &sr = g_srow[r];
	if (sr.kind == SR_GROUP) { g_folded[sr.index] = !g_folded[sr.index]; side_rows (); g_sideHot = -1; g_root->invalidate (true); return; }
	if (sr.kind == SR_ADD) { on_add (); return; }
	set_filter (row_filter (sr));
}
static void folder_menu (int f, int mx, int my)
{
	PopupMenu m (mx, my);
	m.add ("Show", 1);
	m.add ("Show in File Viewer", 2);
	m.separator ();
	m.add ("Remove Folder", 3);
	int r = m.run ();
	if (r == 1) set_filter (F_FOLDER + f);
	else if (r == 2) lx_launch ("fileviewer", g_folder[f]);
	else if (r == 3) remove_folder (f);
	g_root->invalidate (true);
}
static void tile_menu (int v, int mx, int my)
{
	PopupMenu m (mx, my);
	m.add ("Play", 1, true, "Enter");
	m.add ("Play Full Screen", 2);
	m.separator ();
	m.add ("Show in File Viewer", 3);
	int r = m.run ();
	const Game &g = g_games[g_vis[v]];
	if (r == 1) play (g_vis[v]);
	else if (r == 2) lx_open (g.path, "--fullscreen");
	else if (r == 3)
	{
		char dir[200]; scpy (dir, g.path, sizeof dir);
		int e = slen (dir); while (e > 0 && dir[e - 1] != '/') e--;
		if (e > 1 && dir[e - 2] != ':') e--;			// "SD:/roms/x.gb" -> "SD:/roms" ("SD:/" kept)
		dir[e] = 0;
		lx_launch ("fileviewer", dir);
	}
	g_root->invalidate (true);
}

// The theme's look (wtk/paint.h), as the File Viewer: the sidebar on the window's face (the one
// shown lit in the accent), the path bar, the cards on the field -- each a raised card of the face
// (the one pointed at lighter, outlined in the accent; the chosen one in the accent), its picture
// as it is.
class LibRoot : public Root
{
public:
	LibRoot () : Root (WIN_W, WIN_H, "Game Library") {}
	void onResized () override { clamp (); invalidate (true); }	// (the grid follows the width)
	void onDraw () override
	{
		canvas.clear (C_BG);
		drawGrid ();					// (first: the bars cover what scrolls past them)
		drawSidebar ();
		drawBar ();
		drawStatus ();
	}

	void drawGrid ()
	{
		int gx = SIDE_W, gw = width - SIDE_W, gb = grid_bottom ();
		canvas.fillRect (gx, BAR_H, gw, gb - BAR_H, C_FIELD);
		unsigned ink = C_FIELD_TEXT, dim = wk_mix (C_FIELD, C_FIELD_TEXT, 130);
		if (g_nvis == 0)
		{
			int y = BAR_H + 30;
			if (g_ng == 0)
			{
				canvas.text (gx + 24, y, "No game found in", ink);
				for (int f = 0; f < g_nf; f++) canvas.drawFont (gx + 24, y + 22 + f * 20, g_folder[f], font (), ink, 1, 2);
				y += 34 + g_nf * 20;
				canvas.text (gx + 24, y, "Put .gb / .gbc / .gba / .nes / .sfc / .z64 / .iso files there", dim);
				canvas.text (gx + 24, y + 20, "(sub-folders too), or Folders > Add Folder...", dim);
			}
			else if (g_filter >= F_SYS && g_filter < F_FOLDER)
			{
				int k = g_filter - F_SYS;
				char t[64]; int n = 0; lx_cat (t, sizeof t, &n, "No "); lx_cat (t, sizeof t, &n, SYS_NAME[k]); lx_cat (t, sizeof t, &n, " game yet.");
				canvas.drawFont (gx + 24, y, t, font (), ink, 1, 2);
				char h[96]; n = 0; lx_cat (h, sizeof h, &n, "Put "); lx_cat (h, sizeof h, &n, SYS_EXT[k]); lx_cat (h, sizeof h, &n, " files in a watched folder");
				canvas.text (gx + 24, y + 26, h, dim);
				canvas.text (gx + 24, y + 46, "(sub-folders too), then Library > Refresh.", dim);
			}
			else canvas.text (gx + 24, y, "No game in this folder.", dim);
			return;
		}
		// the sections' titles: the system and its number of games
		int c = cols (), yy = BAR_H + 8 - g_scroll, k = 0;
		for (int sec = 0; sec < NSYS; sec++)
		{
			int n = 0; while (k + n < g_nvis && g_games[g_vis[k + n]].sys == sec) n++;
			if (!n) continue;
			if (yy + HEAD_H > BAR_H && yy < gb)
			{
				canvas.drawFont (gx + 12, yy + 6, SYS_NAME[sec], font (), ink, 1, 2);
				char num[12]; int m = ax_itoa (n, num); num[m] = 0;
				canvas.text (gx + 12 + wk_text_w (SYS_NAME[sec], 2) + 10, yy + 6, num, dim);
				wk_etch_h (canvas, gx + 10, yy + 26, gw - 20, C_FIELD);
			}
			yy += HEAD_H + ((n + c - 1) / c) * CELLH + 10;
			k += n;
		}
		for (int i = 0; i < g_nvis; i++)
		{
			int x, y; tile_pos (i, &x, &y);
			if (y + CELLH < BAR_H || y > gb) continue;
			const Game &g = g_games[g_vis[i]];
			bool sel = i == g_sel, hot = i == g_hover && !sel;
			int px = x + 8, py = y + 6;
			if (sel) wk_hilite (canvas, x, y, CELLW - 8, CELLH - 8, 8, true);
			else
			{
				wk_rbox (canvas, x, y, CELLW - 8, CELLH - 8, 8, wk_tone (C_FACE, hot ? 196 : 170), wk_tone (C_FACE, hot ? 152 : 134));
				wk_rline (canvas, x, y, CELLW - 8, CELLH - 8, 8, hot ? C_ACCENT : wk_tone (C_FACE, 76), hot ? 230 : 160);
			}
			if (g.thumb)
				for (int r = 0; r < TH; r++)
				{
					int yy2 = py + r; if (yy2 < BAR_H || yy2 >= gb) continue;
					unsigned *d = canvas.px + (long) yy2 * canvas.stride + px;
					const unsigned *s = g.thumb + r * TW;
					for (int q = 0; q < TW && px + q < width; q++) d[q] = s[q];
				}
			else
			{
				wk_sunken (canvas, px, py, TW, TH, 4, wk_tone (C_FACE, 112));
				canvas.text (px + 44, py + TH / 2 - 8, g_tgame == g_vis[i] ? "(loading)" : "", C_TEXT);
			}
			char nm[24]; scpy (nm, g.name, sizeof nm);
			if (slen (g.name) > 20) { nm[19] = '.'; nm[20] = '.'; nm[21] = 0; }
			canvas.text (px, py + TH + 7, nm, sel ? wk_hilite_ink (true) : C_TEXT);
		}
	}

	void drawSidebar ()
	{
		int gb = grid_bottom ();
		canvas.fillRect (0, BAR_H, SIDE_W, gb - BAR_H, C_BG);
		wk_etch_v (canvas, SIDE_W - 2, BAR_H + 4, gb - BAR_H - 8, C_BG);
		unsigned dim = wk_mix (C_BG, C_TEXT, 150);
		int fh = wk_fh (), fw = wk_text_w ("M");
		if (fw < 1) fw = 8;
		for (int r = 0; r < g_nsrow; r++)
		{
			int y = SIDE_Y + r * SIDE_RH;
			if (y + SIDE_RH > gb) break;
			const SideRow &sr = g_srow[r];
			bool hot = r == g_sideHot;
			if (sr.kind == SR_GROUP)
			{
				wk_glyph (canvas, g_folded[sr.index] ? WKG_CHEV_RIGHT : WKG_CHEV_DOWN, 14, y + SIDE_RH / 2, 8, hot ? C_TEXT : dim);
				canvas.drawFont (24, y + (SIDE_RH - fh) / 2, GROUP_NAME[sr.index], font (), hot ? C_TEXT : dim, 1, 2);
				continue;
			}
			int f = row_filter (sr);
			bool on = f >= 0 && f == g_filter;
			if (on) wk_hilite (canvas, 8, y + 1, SIDE_W - 20, SIDE_RH - 2, 6, true);
			else if (hot) wk_rbox (canvas, 8, y + 1, SIDE_W - 20, SIDE_RH - 2, 6, wk_tone (C_BG, 160), wk_tone (C_BG, 148));
			int n = sr.kind == SR_ALL ? g_ng : sr.kind == SR_SYS ? g_nsys[sr.index] : sr.kind == SR_FOLDER ? g_nfold[sr.index] : -1;
			unsigned ink = on ? C_SEL_TEXT : (sr.kind == SR_SYS && n == 0) || sr.kind == SR_ADD ? dim : C_TEXT;
			int ix = 16, iy = y + (SIDE_RH - SICON) / 2;
			if (sr.kind == SR_SYS) blit_icon (canvas, ix, iy, sys_icon (sr.index));
			else if (sr.kind == SR_ALL)				// four cards
				for (int q = 0; q < 4; q++)
					wk_rbox (canvas, ix + 2 + (q & 1) * 10, iy + 2 + (q >> 1) * 10, 8, 8, 2, wk_tone (C_ACCENT, on ? 200 : 150), wk_tone (C_ACCENT, on ? 170 : 120));
			else if (sr.kind == SR_FOLDER)				// a folder
			{
				wk_rbox (canvas, ix + 3, iy + 5, 7, 4, 1, 0x00D8AA52, 0x00C89A48);
				wk_rbox (canvas, ix + 3, iy + 7, 16, 11, 2, 0x00EEC46C, 0x00D8A850);
				wk_rline (canvas, ix + 3, iy + 7, 16, 11, 2, 0x00906A28, 190);
			}
			else wk_glyph (canvas, WKG_PLUS, ix + SICON / 2, y + SIDE_RH / 2, 10, ink);
			char lab[48];
			scpy (lab, sr.kind == SR_ALL ? "All Games" : sr.kind == SR_SYS ? SYS_NAME[sr.index] : sr.kind == SR_FOLDER ? g_folder[sr.index] : "Add Folder...", sizeof lab);
			char num[12] = ""; int nw = 0;
			if (n >= 0) { int m = ax_itoa (n, num); num[m] = 0; nw = wk_text_w (num) + 8; }
			int maxc = (SIDE_W - 20 - 44 - nw - 6) / fw;
			if (slen (lab) > maxc && maxc > 2) { lab[maxc - 2] = '.'; lab[maxc - 1] = '.'; lab[maxc] = 0; }
			canvas.text (44, y + (SIDE_RH - fh) / 2, lab, ink);
			if (n >= 0) canvas.text (SIDE_W - 20 - nw, y + (SIDE_RH - fh) / 2, num, on ? C_SEL_TEXT : dim);
		}
	}

	// The path bar: "Game Library" (a link back to all the games), then what is shown; at the right
	// the number of games.
	void drawBar ()
	{
		canvas.fillRect (0, 0, width, BAR_H, C_BG);
		int fx = 10, fy = 7, fw = width - 20, fh = BAR_H - 14;
		unsigned field = wk_mix (C_BG, C_FIELD, 170), ink = wk_ink_on (field), dim = wk_mix (field, ink, 120);
		wk_rbox (canvas, fx, fy, fw, fh, 8, wk_tone (field, 136), field);
		wk_rline (canvas, fx, fy, fw, fh, 8, wk_tone (C_BG, 88), 190);
		int x = fx + 14, y = fy + (fh - wk_fh ()) / 2;
		bool top = g_filter == F_ALL;
		const char *root = "Game Library";
		int tw = wk_text_w (root, top ? 2 : 0);
		canvas.drawFont (x, y, root, font (), top ? wk_tone (C_ACCENT, 84) : ink, 1, top ? 2 : 0);
		if (top) canvas.fillRect (x, y + wk_fh () + 1, tw, 2, C_ACCENT);
		else if (g_crumbHot == 0) canvas.fillRect (x, y + wk_fh () + 1, tw, 1, ink);
		g_crumbX = x + tw;
		x += tw + 10;
		if (!top)
		{
			wk_glyph (canvas, WKG_CHEV_RIGHT, x + 3, fy + fh / 2, 9, dim);
			x += 18;
			const char *nm = filter_name ();
			canvas.drawFont (x, y, nm, font (), wk_tone (C_ACCENT, 84), 1, 2);
			canvas.fillRect (x, y + wk_fh () + 1, wk_text_w (nm, 2), 2, C_ACCENT);
		}
		char cnt[24]; int n = ax_itoa (g_nvis, cnt); cnt[n] = 0;
		lx_cat (cnt, sizeof cnt, &n, g_nvis == 1 ? " game" : " games");
		canvas.text (fx + fw - 14 - wk_text_w (cnt), y, cnt, dim);
	}

	// The status bar: the picture being made, or the game chosen.
	void drawStatus ()
	{
		int y = grid_bottom ();
		wk_rbox (canvas, 0, y, width, ST_H, 0, wk_tone (C_FACE, 160), wk_tone (C_FACE, 124));
		wk_etch_h (canvas, 0, y, width, C_FACE);
		char s[160]; int n = 0; s[0] = 0;
		if (g_tgame >= 0) { lx_cat (s, sizeof s, &n, "Making the picture of "); lx_cat (s, sizeof s, &n, g_games[g_tgame].name); lx_cat (s, sizeof s, &n, "..."); }
		else if (g_nvis > 0)
		{
			const Game &g = g_games[g_vis[g_sel]];
			lx_cat (s, sizeof s, &n, g.name); lx_cat (s, sizeof s, &n, "   -   "); lx_cat (s, sizeof s, &n, SYS_NAME[g.sys]);
			lx_cat (s, sizeof s, &n, g_full ? "   (Enter: play full screen)" : "   (Enter or a double-click: play)");
		}
		canvas.text (10, y + (ST_H - wk_fh ()) / 2 + 1, s, C_TEXT);
	}

	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		static bool down = false, rdown = false;
		static unsigned lastClick = 0; static int lastTile = -1;
		int sh = side_at (mx, my);
		int ch = g_filter != F_ALL && my >= 7 && my < BAR_H - 7 && mx >= 24 && mx < g_crumbX ? 0 : -1;
		int t = mx >= 0 ? tile_at (mx, my) : -1;
		bool redraw = false;
		if (sh != g_sideHot) { g_sideHot = sh; redraw = true; }
		if (ch != g_crumbHot) { g_crumbHot = ch; redraw = true; }
		if (t != g_hover) { g_hover = t; redraw = true; }
		if (wheel)
		{
			if (mx >= SIDE_W) { g_scroll -= wheel * 40; clamp (); }
			invalidate (true);
			return true;
		}
		bool press = bl && !down, rpress = br && !rdown;
		down = bl != 0; rdown = br != 0;
		if (press)
		{
			if (ch == 0) set_filter (F_ALL);
			else if (sh >= 0) side_click (sh);
			else if (t >= 0)				// a click selects, a double-click (0.4 s) plays
			{
				unsigned now = kapi_get_ticks ();
				bool dbl = t == lastTile && now - lastClick < 40;
				g_sel = t; redraw = true;
				lastClick = now; lastTile = dbl ? -1 : t;
				if (dbl) play_shown (t);
			}
		}
		else if (rpress)
		{
			if (sh >= 0 && g_srow[sh].kind == SR_FOLDER) folder_menu (g_srow[sh].index, mx, my);
			else if (t >= 0) { g_sel = t; draw (); tile_menu (t, mx, my); }
		}
		if (redraw) invalidate (true);
		return true;
	}
	bool onKey (long k) override
	{
		int c = cols (), old = g_sel;
		if (k == KEY_RIGHT) g_sel++;
		else if (k == KEY_LEFT) g_sel--;
		else if (k == KEY_DOWN) g_sel += c;
		else if (k == KEY_UP) g_sel -= c;
		else if (k == KEY_ENTER || k == ' ') { play_shown (g_sel); return true; }
		else if (k == KEY_PGDN) g_scroll += grid_bottom () - BAR_H - 60;
		else if (k == KEY_PGUP) g_scroll -= grid_bottom () - BAR_H - 60;
		else if (k == '\t') { next_filter (1); return true; }
		else return Root::onKey (k);
		if (g_sel >= g_nvis) g_sel = g_nvis - 1;
		if (g_sel < 0) g_sel = 0;
		if (g_sel != old) ensure_visible (g_sel);
		clamp (); invalidate (true);
		return true;
	}
	void clamp () { int mx = content_end () - grid_bottom (); if (g_scroll > mx) g_scroll = mx; if (g_scroll < 0) g_scroll = 0; }
};

// A USB gamepad moves through the cards too: the d-pad (repeating while held), A / Start to play,
// L / R the previous / next system, L2 / R2 a page.
static void pad_poll (void)
{
	static unsigned last = 0; static unsigned t0 = 0;
	unsigned b = pad_buttons (-1), now = kapi_get_ticks ();
	unsigned press = b & ~last;
	bool rep = (b & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)) && (int) (now - t0) > 18;	// (every 0.18 s)
	last = b;
	if (!press && !rep) return;
	if (press & (PAD_START | PAD_A)) { play_shown (g_sel); return; }
	if (press & PAD_L) { next_filter (-1); return; }
	if (press & PAD_R) { next_filter (1); return; }
	if (press & PAD_L2) { g_root->onKey (KEY_PGUP); return; }
	if (press & PAD_R2) { g_root->onKey (KEY_PGDN); return; }
	unsigned d = press ? press : b;
	long k = (d & PAD_RIGHT) ? KEY_RIGHT : (d & PAD_LEFT) ? KEY_LEFT : (d & PAD_DOWN) ? KEY_DOWN : (d & PAD_UP) ? KEY_UP : 0;
	if (!k) return;
	t0 = now + (press ? 25 : 0);				// (a longer wait before the first repeat)
	g_root->onKey (k);
}

static void on_refresh () { rescan (); side_rows (); g_root->invalidate (true); }
static void on_full () { g_full = !g_full; g_root->invalidate (true); }
static void build_menu ();
static void save_folders ()
{
	char ini[MAXF * 202 + 64]; int n = 0;
	lx_cat (ini, sizeof ini, &n, "; Game Library settings: the watched folders, one line each\n");
	for (int f = 0; f < g_nf; f++) { lx_cat (ini, sizeof ini, &n, "folder = "); lx_cat (ini, sizeof ini, &n, g_folder[f]); lx_cat (ini, sizeof ini, &n, "\n"); }
	if (g_nf == 0) lx_cat (ini, sizeof ini, &n, "folder =\n");			// (none: not the default)
	kapi_save_file ("SD:/apps/gamelib.app/config.ini", ini, (unsigned) n);
}
static void on_add ()
{
	char p[256];
	if (g_nf >= MAXF) { wk_messagebox ("Game Library", "Too many folders (8 at most).", MB_OK); return; }
	if (!wk_folder_open (p, sizeof p, g_folder[g_nf - 1 >= 0 ? g_nf - 1 : 0])) return;
	int e = slen (p); if (e > 1 && p[e - 1] == '/' && p[e - 2] != ':') p[--e] = 0;	// "SD:/roms/" -> "SD:/roms"
	if (e > 63) { wk_messagebox ("Game Library", "This folder's path is too long (63 characters at most).", MB_OK); return; }
	for (int f = 0; f < g_nf; f++) { const char *a = g_folder[f], *b = p; while (*a && low (*a) == low (*b)) a++, b++; if (!*a && !*b) return; }
	scpy (g_folder[g_nf++], p, sizeof g_folder[0]);
	save_folders (); build_menu (); on_refresh ();
}
static void remove_folder (int f)
{
	if (f < 0 || f >= g_nf) return;
	for (int k = f; k < g_nf - 1; k++) scpy (g_folder[k], g_folder[k + 1], sizeof g_folder[0]);
	g_nf--;
	if (g_filter >= F_FOLDER) g_filter = F_ALL;
	save_folders (); build_menu (); on_refresh ();
}
// (a menu action takes no argument: one per slot)
static void on_rm0 () { remove_folder (0); } static void on_rm1 () { remove_folder (1); }
static void on_rm2 () { remove_folder (2); } static void on_rm3 () { remove_folder (3); }
static void on_rm4 () { remove_folder (4); } static void on_rm5 () { remove_folder (5); }
static void on_rm6 () { remove_folder (6); } static void on_rm7 () { remove_folder (7); }
static const MenuAction ON_RM[MAXF] = { on_rm0, on_rm1, on_rm2, on_rm3, on_rm4, on_rm5, on_rm6, on_rm7 };
static void on_quit () { kapi_exit (0); }
static void on_play () { play_shown (g_sel); }
static void on_all () { set_filter (F_ALL); }
static void on_s0 () { set_filter (F_SYS + 0); } static void on_s1 () { set_filter (F_SYS + 1); }
static void on_s2 () { set_filter (F_SYS + 2); } static void on_s3 () { set_filter (F_SYS + 3); }
static void on_s4 () { set_filter (F_SYS + 4); } static void on_s5 () { set_filter (F_SYS + 5); }
static void on_s6 () { set_filter (F_SYS + 6); }
static const MenuAction ON_SYS[NSYS] = { on_s0, on_s1, on_s2, on_s3, on_s4, on_s5, on_s6 };

static Menu g_menu;
static void build_menu ()
{
	g_menu = Menu ();
	g_menu.menu ("Library");
	g_menu.item ("Play",              "Enter", 0, on_play);
	g_menu.item ("Refresh",           "^R", WK_CTRL ('R'), on_refresh);
	g_menu.separator ();
	g_menu.item ("Quit",              "^Q", WK_CTRL ('Q'), on_quit);
	g_menu.menu ("Folders");
	g_menu.item ("Add Folder...",     "",   0, on_add);
	if (g_nf > 0) g_menu.separator ();
	for (int f = 0; f < g_nf; f++)
	{
		char l[220]; int n = 0; lx_cat (l, sizeof l, &n, "Remove "); lx_cat (l, sizeof l, &n, g_folder[f]);
		g_menu.item (l, "", 0, ON_RM[f]);
	}
	g_menu.menu ("View");
	g_menu.item ("All Games",         "Tab", 0, on_all);
	for (int k = 0; k < NSYS; k++) g_menu.item (SYS_NAME[k], "", 0, ON_SYS[k]);
	g_menu.separator ();
	g_menu.item ("Play Full Screen On / Off", "", 0, on_full);
	g_menu.publish ();
}

int main (void)
{
	if (app_ini_load_path ("SD:/apps/gamelib.app/config.ini") >= 0)
	{
		// one `folder = <path>` line per watched folder (an .ini value: 63 characters at most)
		int nf = 0; bool any = false;
		for (int i = 0; i < g_ini_n && nf < MAXF; i++)
			if (g_ini_sec[i][0] == 0 && ax_streq (g_ini_key[i], "folder"))
			{ any = true; if (g_ini_val[i][0]) scpy (g_folder[nf++], g_ini_val[i], sizeof g_folder[0]); }
		if (any) g_nf = nf;
	}
	LibRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	build_menu ();
	rescan ();
	side_rows ();
	root.setResizable (true);			// (the grid lays itself out from the width)
	root.attach ();
	while (!should_exit ())
	{
		pump_events ();
		thumb_work ();
		pad_poll ();
		if (!root.valid) { root.draw (); kapi_present (); }
		kapi_msleep (g_tgame >= 0 ? 1 : 16);
	}
	return 0;
}
