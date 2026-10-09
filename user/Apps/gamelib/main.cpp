//
// gamelib -- the Game Library: every GameCube, Nintendo 64, Super Nintendo, Game Boy Advance / Color,
// Game Boy and NES game of the watched folders (and their sub-folders), a card each -- a picture of
// its title screen and its name. Laid out as the File Viewer:
//   * on the left the SIDEBAR, in three groups a click on their title folds: Library (All Games),
//     Systems -- the emulators: each with its icon and its number of games; a click shows its
//     games only -- and Folders (the watched folders, their games; Add Folder...; a right click
//     on one: Show / Remove Folder);
//   * above, the path bar ("Game Library > Super Nintendo", the number of games; a click on
//     TR ("Game Library") shows them all); below, the status bar (the game chosen; the picture being
//     made);
//   * the cards, one section per system -- the systems of the installed emulators, each from its
//     app.txt (`games = Super Nintendo: sfc smc`, `order =`): an emulator's package installed, its
//     games show. A click selects one, a double-click (or Enter) plays it: the ROM opens with its
//     emulator (launch.h finds it by the same app.txt), in a window or, with View > Play Full
//     Screen, on the whole display. A right
//     click: Play, Play Full Screen, Show in File Viewer. Keys: the arrows, Enter, PgUp / PgDn,
//     Tab (the next system). A USB gamepad: the d-pad moves, Start (or A) plays, L / R the
//     previous / next system, L2 / R2 turn a page.
//
//   * The folders: SD:/roms by default; Folders > Add Folder... / Remove <folder> (any volume: SD:, SD1: ..,
//     e.g. an exFAT partition of the card), kept in config.ini, one `folder = SD1:/games` line each.
//   * The pictures: each game is run a few seconds without being shown (the emulator core,
//     user/Emulators/gb, user/Emulators/gba, user/Emulators/nes, user/Emulators/snes) and its screen kept -- made in the background, a little every
//     frame, the games shown first, and cached in SD:/apps/gamelib.app/thumbs/ (Library > Refresh finds
//     new ROMs). A Nintendo 64 game gets a label with the name from its header, a GameCube disc its banner.
//   * The emulators are in no menu of the desktop: they are reached from here (one started
//     without a ROM opens the Game Library).
//
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "appkit/appkit.h"
#include "gamepad.h"
#include "gamekit/gamekit.h"
#include "uikit/bmp.h"
#include "uikit/uikit.h"
#include "fontkit/uikitface.h"		// FreeType's text (DejaVu Sans) for every widget
#include "gb/gb.h"
#include "gba/gba.h"
#include "nes/nes.h"
#include "snes/snes.h"

using namespace uikit;

#define WIN_W	980			// (four columns of cards beside the sidebar)
#define WIN_H	580
#define SIDE0	220			// the sidebar: the systems, the folders
static int g_sideW = SIDE0;		// what it takes at the window's left now (pocket, console: its SidePanel's rail, 0 a drawer)
#define SIDE_W	g_sideW
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

// The systems: those the installed emulators play, each from its app.txt (`games = Game Boy Color: gbc;
// Game Boy: gb`, `order = 50`: the sections' order) -- an emulator's package installed, its games show.
// The pictures of the games: the cores carried here (GB, GBA, NES, SNES), an N64 label, a GameCube
// banner; another emulator's games: its icon.
#define MAXSYS	12
enum { CORE_NONE, CORE_GB, CORE_GBA, CORE_SNES, CORE_NES, CORE_N64, CORE_GC, CORE_NDS };
struct Sys { char name[40]; char ext[64]; char extText[64]; char emu[24]; int order, core; };
static Sys g_sys[MAXSYS]; static int NSYS = 0;
#define SYS_NAME(k)	g_sys[k].name
static int slen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scpy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static void load_systems (void)		// (GameKit: the consoles of the installed emulators)
{
	static struct game_system ks[MAXSYS];
	NSYS = games_systems (ks, MAXSYS);
	for (int i = 0; i < NSYS; i++)
	{
		Sys &y = g_sys[i];
		scpy (y.name, ks[i].name, sizeof y.name); scpy (y.ext, ks[i].ext, sizeof y.ext); scpy (y.extText, ks[i].ext_text, sizeof y.extText);
		scpy (y.emu, ks[i].emu, sizeof y.emu); y.order = ks[i].order;
		const char *emu = y.emu;
		y.core = !strcmp (emu, "gbemu") ? CORE_GB : !strcmp (emu, "gbaemu") ? CORE_GBA : !strcmp (emu, "snesemu") ? CORE_SNES :
			 !strcmp (emu, "nesemu") ? CORE_NES : !strcmp (emu, "n64emu") ? CORE_N64 : !strcmp (emu, "gcemu") ? CORE_GC : !strcmp (emu, "ndsemu") ? CORE_NDS : CORE_NONE;
	}
}
struct Game { char path[200]; char name[64]; char key[64]; int sys; unsigned *thumb; bool tried; };
static Game g_games[MAXG]; static int g_ng = 0;
enum { MAXF = 8 };
static char g_folder[MAXF][GAMES_PATH] = { "SD:/roms" }; static int g_nf = 1;	// the watched folders
static bool g_full = false;
static int g_scroll = 0, g_sel = 0, g_hover = -1;		// (g_sel, g_hover: indexes in g_vis)
static Root *g_root = 0;
static int g_vis[MAXG], g_nvis;			// the games shown (g_games indexes), in their order
static void refilter (void);

static char low (char c) { return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c; }
static bool ends (const char *s, const char *e) { int n = slen (s), m = slen (e); if (n < m) return false; for (int i = 0; i < m; i++) if (low (s[n - m + i]) != e[i]) return false; return true; }
static bool ci_less (const char *a, const char *b) { for (;; a++, b++) { char x = low (*a), y = low (*b); if (x != y || !x) return x < y; } }

// ---- the ROMs ------------------------------------------------------------------------------------
static void rescan (void)			// (GameKit: the ROMs of the watched folders, by system then by name)
{
	for (int i = 0; i < g_ng; i++) delete [] g_games[i].thumb;
	static struct game kg[MAXG];
	static struct game_system ks[MAXSYS];
	for (int k = 0; k < NSYS; k++)
	{
		scpy (ks[k].name, g_sys[k].name, sizeof ks[k].name); scpy (ks[k].ext, g_sys[k].ext, sizeof ks[k].ext);
		scpy (ks[k].ext_text, g_sys[k].extText, sizeof ks[k].ext_text); scpy (ks[k].emu, g_sys[k].emu, sizeof ks[k].emu); ks[k].order = g_sys[k].order;
	}
	g_ng = games_scan (g_folder, g_nf, ks, NSYS, kg, MAXG);
	for (int i = 0; i < g_ng; i++)
	{
		Game &g = g_games[i];
		scpy (g.path, kg[i].path, sizeof g.path); scpy (g.name, kg[i].name, sizeof g.name); scpy (g.key, kg[i].key, sizeof g.key);
		g.sys = kg[i].sys; g.thumb = 0; g.tried = false;
	}
	g_sel = 0; g_scroll = 0;
	refilter ();
}

// ---- the pictures: cached raw 160 x 144 x RGB ------------------------------------------------------
static struct game kgame (const Game &g)	// (GameKit's view of a game: its picture's name)
{
	struct game k;
	scpy (k.path, g.path, sizeof k.path); scpy (k.name, g.name, sizeof k.name); scpy (k.key, g.key, sizeof k.key); k.sys = g.sys;
	return k;
}
static bool thumb_load (Game &g)
{
	struct game k = kgame (g);
	unsigned *px = new unsigned[TW * TH];
	if (!games_thumb_load (&k, px)) { delete [] px; return false; }
	g.thumb = px;
	return true;
}
static void thumb_save (const Game &g) { struct game k = kgame (g); games_thumb_save (&k, g.thumb); }

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

// A Nintendo DS game (too big to run here): its banner's icon (32 x 32, 16 colours in 8 x 8 tiles), three times
// its size, and its title (the banner's English one: name, subtitle, publisher, a line each).
static void nds_thumb (Game &g)
{
	void *f = kapi_open (g.path); if (!f) return;
	static unsigned char hdr[0x200], ban[0x440];
	bool ok = disc_read (f, 0, hdr, sizeof hdr);
	unsigned bo = ok ? (unsigned) hdr[0x68] | hdr[0x69] << 8 | hdr[0x6A] << 16 | (unsigned) hdr[0x6B] << 24 : 0;
	bool hasBan = ok && bo && disc_read (f, bo, ban, sizeof ban);
	kapi_close (f);
	if (!ok) return;
	g.thumb = new unsigned[TW * TH];
	Canvas c; c.adopt (g.thumb, TW, TH);
	c.clear (0x00282C34);
	c.fillRect (8, 8, TW - 16, 20, 0x00A0A4B0);
	c.text (14, 10, "NINTENDO DS", 0x00202020);
	char name[72]; int nl = 0;
	if (hasBan)
	{
		for (int y = 0; y < 32; y++)
			for (int x = 0; x < 32; x++)
			{
				int tile = (y / 8) * 4 + x / 8;
				unsigned char b = ban[0x20 + tile * 32 + (y & 7) * 4 + (x & 7) / 2];
				int ci = (x & 1) ? b >> 4 : b & 15;
				if (!ci) continue;
				unsigned v = (unsigned) ban[0x220 + ci * 2] | ban[0x221 + ci * 2] << 8;
				unsigned col = ((v & 31) * 255 / 31) << 16 | (((v >> 5) & 31) * 255 / 31) << 8 | ((v >> 10) & 31) * 255 / 31;
				for (int dy = 0; dy < 3; dy++) for (int dx = 0; dx < 3; dx++) g.thumb[(34 + y * 3 + dy) * TW + 32 + x * 3 + dx] = col;
			}
		for (int i = 0; i < 64 && nl < 70; i++)				// (UTF-16: Latin-1 kept, the line breaks as spaces)
		{
			unsigned ch = (unsigned) ban[0x340 + i * 2] | ban[0x341 + i * 2] << 8;
			if (!ch) break;
			name[nl++] = ch == '\n' ? ' ' : ch < 256 && ch >= ' ' ? (char) ch : '?';
		}
	}
	if (!nl) for (; nl < 12 && hdr[nl] >= ' ' && hdr[nl] < 127; nl++) name[nl] = (char) hdr[nl];
	name[nl] = 0;
	int y = hasBan ? 132 - 16 : 50;
	for (int s0 = 0; s0 < nl && y < TH - 12; y += 14)			// the name, a word wrap at 20 characters
	{
		int e = s0 + 20 < nl ? s0 + 20 : nl;
		if (e < nl) { int b = e; while (b > s0 && name[b] != ' ') b--; if (b > s0) e = b; }
		char line[24]; int k = 0; for (int i = s0; i < e && k < 23; i++) line[k++] = name[i]; line[k] = 0;
		c.text (8, y, line, 0x00F0E8C0);
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
				int core = g_sys[g_games[i].sys].core;
				if (core == CORE_N64) { n64_thumb (g_games[i]); g_root->invalidate (true); return; }
				if (core == CORE_GC) { gc_thumb (g_games[i]); g_root->invalidate (true); return; }
				if (core == CORE_NDS) { nds_thumb (g_games[i]); g_root->invalidate (true); return; }
				if (core == CORE_NONE) continue;			// (its emulator's icon instead)
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
		if (g_sys[g.sys].core == CORE_GBA) { if (!g_tma) { g_tma = new gba::Machine; g_tma->setAudioRate (8000); } ok = g_tma->load (g_trom, (int) g_tread); }
		else if (g_sys[g.sys].core == CORE_SNES) { if (!g_tms) { g_tms = new snes::Machine; g_tms->setAudioRate (8000); } ok = g_tms->load (g_trom, (int) g_tread); }
		else if (g_sys[g.sys].core == CORE_NES) { if (!g_tmn) { g_tmn = new nes::Machine; g_tmn->setAudioRate (8000); } ok = g_tmn->load (g_trom, (int) g_tread); }
		else { if (!g_tm) { g_tm = new gb::Machine; g_tm->setAudioRate (8000); } ok = g_tm->load (g_trom, (int) g_tread); }
		if (!ok) { g_tgame = -1; return; }
		g_tloaded = true;
		return;
	}
	static short pcm[4096];
	int per = g_sys[g.sys].core == CORE_GBA ? 4 : g_sys[g.sys].core == CORE_SNES ? 3 : g_sys[g.sys].core == CORE_NES ? 6 : 12;
	for (int k = 0; k < per && g_tframe < THUMB_FRAMES; k++, g_tframe++)
	{
		if (g_sys[g.sys].core == CORE_GBA) { g_tma->runFrame (); g_tma->audioRead (pcm, 2048); }
		else if (g_sys[g.sys].core == CORE_SNES) { g_tms->runFrame (); g_tms->audioRead (pcm, 2048); }
		else if (g_sys[g.sys].core == CORE_NES) { g_tmn->runFrame (); g_tmn->audioRead (pcm, 2048); }
		else { g_tm->runFrame (); g_tm->audioRead (pcm, 2048); }
	}
	if (g_tframe >= THUMB_FRAMES)
	{
		g.thumb = new unsigned[TW * TH];
		if (g_sys[g.sys].core == CORE_GBA)
		{
			// 240 x 160 -> 160 x 107, centred in the 160 x 144 picture on black
			int oy = (TH - 107) / 2;
			for (int i = 0; i < TW * TH; i++) g.thumb[i] = 0;
			for (int y = 0; y < 107; y++)
				for (int x = 0; x < TW; x++) g.thumb[(oy + y) * TW + x] = g_tma->fb[(y * 160 / 107) * gba::W + x * 3 / 2];
		}
		else if (g_sys[g.sys].core == CORE_SNES)
		{
			// 256 x 224 -> 160 x 140, centred on black
			int oy = (TH - 140) / 2;
			for (int i = 0; i < TW * TH; i++) g.thumb[i] = 0;
			for (int y = 0; y < 140; y++)
				for (int x = 0; x < TW; x++) g.thumb[(oy + y) * TW + x] = g_tms->fb[(y * 224 / 140) * snes::W + x * 256 / TW];
		}
		else if (g_sys[g.sys].core == CORE_NES)
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
enum { F_ALL = 0, F_SYS = 1, F_FOLDER = 1 + MAXSYS };	// g_filter: F_ALL, F_SYS + a system, F_FOLDER + a folder
static int g_filter = F_ALL;
static int g_nsys[MAXSYS], g_nfold[MAXF];			// the games of each system, of each folder

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
	if (g_filter >= F_SYS) return SYS_NAME(g_filter - F_SYS);
	return TR ("All Games");
}
// Tab, a gamepad's shoulders: the next / previous system that has games (All Games between)
static void next_filter (int dir)
{
	int f = g_filter >= F_FOLDER ? F_ALL : g_filter;
	for (int k = 0; k <= NSYS; k++)
	{
		f = (f + dir + NSYS + 1) % (NSYS + 1);			// F_ALL, F_SYS + 0 .. F_SYS + NSYS - 1 (F_SYS == 1)
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
static const char *const GROUP_NAME[NGROUPS] = { TRN ("Library"), TRN ("Systems"), TRN ("Folders") };
static bool g_folded[NGROUPS];
enum { SR_GROUP, SR_ALL, SR_SYS, SR_FOLDER, SR_ADD };
struct SideRow { int kind, index; };
static SideRow g_srow[NGROUPS + 2 + MAXSYS + MAXF + 1]; static int g_nsrow;
static int g_sideHot = -1, g_crumbHot = -1, g_crumbX = 0;
static void sp_build (void);
static void side_rows (void)
{
	sp_build ();					// (pocket, console: the SidePanel's items again)
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
// (P7) In pocket and console the sidebar is a navigation SidePanel (uikit/sidepanel.h: a rail of the systems' icons in
// landscape, a drawer in portrait, the d-pad's column in console); 0 on the desktop -- its own sidebar, as always.
static SidePanel *g_sp;
enum { SP_ADD = 9000, SPI_ALL = 0, SPI_SYS = 1, SPI_FOLDER = 500, SPI_PLUS = 501 };	// (an item's id: its filter; its icon)
static void sp_build (void);
static int side_at (int mx, int my)
{
	if (g_sp || mx < 0 || mx >= SIDE_W - 4 || my < SIDE_Y || my >= grid_bottom ()) return -1;
	int r = (my - SIDE_Y) / SIDE_RH;
	return r >= 0 && r < g_nsrow ? r : -1;
}
static int row_filter (const SideRow &r)
{
	return r.kind == SR_ALL ? F_ALL : r.kind == SR_SYS ? F_SYS + r.index : r.kind == SR_FOLDER ? F_FOLDER + r.index : -1;
}

// A system's icon: its emulator's (SD:/apps/<emulator>.app/icon.bmp), small; its top byte the
// transparency (the icon's magenta: see-through).
static unsigned *g_sysIcon[MAXSYS]; static bool g_sysTried[MAXSYS];
static unsigned *g_sysBig[MAXSYS]; static int g_sysBigW[MAXSYS], g_sysBigH[MAXSYS]; static bool g_sysBigTried[MAXSYS];
static const unsigned *sys_big (int k, int *w, int *h)		// its emulator's icon, as it is
{
	if (!g_sysBigTried[k])
	{
		g_sysBigTried[k] = true;
		char q[96]; int n = 0;
		lx_cat (q, sizeof q, &n, "SD:/apps/"); lx_cat (q, sizeof q, &n, g_sys[k].emu); lx_cat (q, sizeof q, &n, ".app/icon.bmp");
		g_sysBig[k] = ui::bmp_decode (q, &g_sysBigW[k], &g_sysBigH[k]);
		if (g_sysBig[k] && (g_sysBigW[k] > 64 || g_sysBigH[k] > 64)) { delete [] g_sysBig[k]; g_sysBig[k] = 0; }
	}
	*w = g_sysBigW[k]; *h = g_sysBigH[k];
	return g_sysBig[k];
}
static const unsigned *sys_icon (int k)
{
	if (g_sysTried[k]) return g_sysIcon[k];
	g_sysTried[k] = true;
	char q[96]; int n = 0;
	lx_cat (q, sizeof q, &n, "SD:/apps/"); lx_cat (q, sizeof q, &n, g_sys[k].emu); lx_cat (q, sizeof q, &n, ".app/icon.bmp");
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
			*p = t == 0 ? (c & 0xFFFFFF) : uk_over (*p, c & 0xFFFFFF, 255 - (int) t);
		}
}

static void on_add ();
static void remove_folder (int f);
static void folder_menu (int f, int mx, int my);
static void sp_icon (Canvas &cv, int id, int x, int y, int size, unsigned ink, bool on)
{
	int ix = x + (size - SICON) / 2, iy = y + (size - SICON) / 2;
	if (id >= SPI_SYS && id < SPI_FOLDER) blit_icon (cv, ix, iy, sys_icon (id - SPI_SYS));
	else if (id == SPI_ALL)						// four cards
		for (int q = 0; q < 4; q++)
			uk_rbox (cv, ix + 2 + (q & 1) * 10, iy + 2 + (q >> 1) * 10, 8, 8, 2, on ? ink : uk_tone (C_ACCENT, 150), on ? ink : uk_tone (C_ACCENT, 120));
	else if (id == SPI_FOLDER)					// a folder
	{
		uk_rbox (cv, ix + 3, iy + 5, 7, 4, 1, 0x00D8AA52, 0x00C89A48);
		uk_rbox (cv, ix + 3, iy + 7, 16, 11, 2, 0x00EEC46C, 0x00D8A850);
		uk_rline (cv, ix + 3, iy + 7, 16, 11, 2, 0x00906A28, 190);
	}
	else uk_glyph (cv, WKG_PLUS, x + size / 2, y + size / 2, 10, ink);
}
static void sp_build (void)			// the groups as headings (they fold), the places with their number of games
{
	if (!g_sp) return;
	g_sp->clear ();
	auto count = [] (int id, int n) { char t[12]; int m = ax_itoa (n, t); t[m] = 0; g_sp->setTrailing (id, t); };
	for (int gr = 0; gr < NGROUPS; gr++)
	{
		int h = g_sp->addHeading (TR (GROUP_NAME[gr]), UK_SPI_FOLDABLE | (g_folded[gr] ? UK_SPI_FOLDED : 0));
		if (gr == G_LIBRARY) { g_sp->addItem (F_ALL, TR ("All Games"), SPI_ALL, 0, h); count (F_ALL, g_ng); }
		else if (gr == G_SYSTEMS)
			for (int k = 0; k < NSYS; k++)
			{
				g_sp->addItem (F_SYS + k, SYS_NAME(k), SPI_SYS + k, 0, h); count (F_SYS + k, g_nsys[k]);
				if (g_nsys[k] == 0) g_sp->setFlags (F_SYS + k, UK_SPI_DISABLED);
			}
		else
		{
			for (int f = 0; f < g_nf; f++) { g_sp->addItem (F_FOLDER + f, g_folder[f], SPI_FOLDER, 0, h); count (F_FOLDER + f, g_nfold[f]); }
			g_sp->addItem (SP_ADD, TR ("Add Folder..."), SPI_PLUS, 0, h);
		}
	}
	g_sp->select (g_filter);
}
static void sp_place (void)			// its rectangle, what it takes of the window's left
{
	if (!g_sp || !g_root) return;
	g_sp->place (0, BAR_H, SIDE0, g_root->height - ST_H - BAR_H);
	g_sideW = g_sp->reservedWidth ();
}
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
	m.add (TR ("Show"), 1);
	m.add (TR ("Show in File Viewer"), 2);
	m.separator ();
	m.add (TR ("Remove Folder"), 3);
	int r = m.run ();
	if (r == 1) set_filter (F_FOLDER + f);
	else if (r == 2) lx_launch ("fileviewer", g_folder[f]);
	else if (r == 3) remove_folder (f);
	g_root->invalidate (true);
}
static void tile_menu (int v, int mx, int my)
{
	PopupMenu m (mx, my);
	m.add (TR ("Play"), 1, true, TR ("Enter"));
	m.add (TR ("Play Full Screen"), 2);
	m.separator ();
	m.add (TR ("Show in File Viewer"), 3);
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

// The theme's look (uikit/paint.h), as the File Viewer: the sidebar on the window's face (the one
// shown lit in the accent), the path bar, the cards on the field -- each a raised card of the face
// (the one pointed at lighter, outlined in the accent; the chosen one in the accent), its picture
// as it is.
class LibRoot : public Root
{
public:
	LibRoot () : Root (WIN_W, WIN_H, TR ("Game Library")) {}
	void onResized () override { sp_place (); clamp (); invalidate (true); }	// (the grid follows the width)
	void onSizeClass (int) override { sp_place (); clamp (); invalidate (true); }
	void onDraw () override
	{
		if (g_sp && g_sp->selected () != g_filter) g_sp->select (g_filter);
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
		unsigned ink = C_FIELD_TEXT, dim = uk_mix (C_FIELD, C_FIELD_TEXT, 130);
		if (g_nvis == 0)
		{
			int y = BAR_H + 30;
			if (g_ng == 0)
			{
				canvas.text (gx + 24, y, TR ("No game found in"), ink);
				for (int f = 0; f < g_nf; f++) uk_text (canvas, gx + 24, y + 22 + f * 20, g_folder[f], ink, 2);
				y += 34 + g_nf * 20;
				char e[160], h[240]; int en = 0; e[0] = 0;
				for (int k = 0; k < NSYS && en < 150; k++) { if (k) lx_cat (e, sizeof e, &en, " / "); lx_cat (e, sizeof e, &en, g_sys[k].extText); }
				if (NSYS) snprintf (h, sizeof h, TR ("Put %s files there"), e); else snprintf (h, sizeof h, "%s", TR ("Put an emulator first (Control Panel > Packages)"));
				canvas.text (gx + 24, y, h, dim);
				canvas.text (gx + 24, y + 20, TR ("(sub-folders too), or Folders > Add Folder..."), dim);
			}
			else if (g_filter >= F_SYS && g_filter < F_FOLDER)
			{
				int k = g_filter - F_SYS;
				char t[96]; snprintf (t, sizeof t, TR ("No %s game yet."), SYS_NAME(k));
				uk_text (canvas, gx + 24, y, t, ink, 2);
				char h[160]; snprintf (h, sizeof h, TR ("Put %s files in a watched folder"), g_sys[k].extText);
				canvas.text (gx + 24, y + 26, h, dim);
				canvas.text (gx + 24, y + 46, TR ("(sub-folders too), then Library > Refresh."), dim);
			}
			else canvas.text (gx + 24, y, TR ("No game in this folder."), dim);
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
				uk_text (canvas, gx + 12, yy + 6, SYS_NAME(sec), ink, 2);
				char num[12]; int m = ax_itoa (n, num); num[m] = 0;
				canvas.text (gx + 12 + uk_text_w (SYS_NAME(sec), 2) + 10, yy + 6, num, dim);
				uk_etch_h (canvas, gx + 10, yy + 26, gw - 20, C_FIELD);
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
			if (sel) uk_hilite (canvas, x, y, CELLW - 8, CELLH - 8, 8, true);
			else
			{
				uk_rbox (canvas, x, y, CELLW - 8, CELLH - 8, 8, uk_tone (C_FACE, hot ? 196 : 170), uk_tone (C_FACE, hot ? 152 : 134));
				uk_rline (canvas, x, y, CELLW - 8, CELLH - 8, 8, hot ? C_ACCENT : uk_tone (C_FACE, 76), hot ? 230 : 160);
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
				uk_sunken (canvas, px, py, TW, TH, 4, uk_tone (C_FACE, 112));
				canvas.text (px + 44, py + TH / 2 - 8, g_tgame == g_vis[i] ? TR ("(loading)") : "", C_TEXT);
				int bw = 0, bh = 0; const unsigned *ic = g_sys[g.sys].core == CORE_NONE ? sys_big (g.sys, &bw, &bh) : 0;
				for (int r = 0; ic && r < bh * 2; r++)		// (no picture made: its emulator's icon, twice its size)
				{
					int yy2 = py + (TH - bh * 2) / 2 + r; if (yy2 < BAR_H || yy2 >= gb) continue;
					for (int q = 0; q < bw * 2; q++)
					{
						unsigned c = ic[(r / 2) * bw + q / 2] & 0xFFFFFF;
						int xx = px + (TW - bw * 2) / 2 + q;
						if (c != 0xFF00FF && xx < width) canvas.px[(long) yy2 * canvas.stride + xx] = c;
					}
				}
			}
			char nm[80]; uk_text_fit (g.name, TW, nm, sizeof nm);	// (the picture's width; cut at a character)
			canvas.text (px, py + TH + 7, nm, sel ? uk_hilite_ink (true) : C_TEXT);
		}
	}

	void drawSidebar ()
	{
		if (g_sp) return;				// (pocket, console: the SidePanel draws itself)
		int gb = grid_bottom ();
		canvas.fillRect (0, BAR_H, SIDE_W, gb - BAR_H, C_BG);
		uk_etch_v (canvas, SIDE_W - 2, BAR_H + 4, gb - BAR_H - 8, C_BG);
		unsigned dim = uk_mix (C_BG, C_TEXT, 150);
		int fh = uk_fh ();
		for (int r = 0; r < g_nsrow; r++)
		{
			int y = SIDE_Y + r * SIDE_RH;
			if (y + SIDE_RH > gb) break;
			const SideRow &sr = g_srow[r];
			bool hot = r == g_sideHot;
			if (sr.kind == SR_GROUP)
			{
				uk_glyph (canvas, g_folded[sr.index] ? WKG_CHEV_RIGHT : WKG_CHEV_DOWN, 14, y + SIDE_RH / 2, 8, hot ? C_TEXT : dim);
				uk_text (canvas, 24, y + (SIDE_RH - fh) / 2, TR (GROUP_NAME[sr.index]), hot ? C_TEXT : dim, 2);
				continue;
			}
			int f = row_filter (sr);
			bool on = f >= 0 && f == g_filter;
			if (on) uk_hilite (canvas, 8, y + 1, SIDE_W - 20, SIDE_RH - 2, 6, true);
			else if (hot) uk_rbox (canvas, 8, y + 1, SIDE_W - 20, SIDE_RH - 2, 6, uk_tone (C_BG, 160), uk_tone (C_BG, 148));
			int n = sr.kind == SR_ALL ? g_ng : sr.kind == SR_SYS ? g_nsys[sr.index] : sr.kind == SR_FOLDER ? g_nfold[sr.index] : -1;
			unsigned ink = on ? C_SEL_TEXT : (sr.kind == SR_SYS && n == 0) || sr.kind == SR_ADD ? dim : C_TEXT;
			int ix = 16, iy = y + (SIDE_RH - SICON) / 2;
			if (sr.kind == SR_SYS) blit_icon (canvas, ix, iy, sys_icon (sr.index));
			else if (sr.kind == SR_ALL)				// four cards
				for (int q = 0; q < 4; q++)
					uk_rbox (canvas, ix + 2 + (q & 1) * 10, iy + 2 + (q >> 1) * 10, 8, 8, 2, uk_tone (C_ACCENT, on ? 200 : 150), uk_tone (C_ACCENT, on ? 170 : 120));
			else if (sr.kind == SR_FOLDER)				// a folder
			{
				uk_rbox (canvas, ix + 3, iy + 5, 7, 4, 1, 0x00D8AA52, 0x00C89A48);
				uk_rbox (canvas, ix + 3, iy + 7, 16, 11, 2, 0x00EEC46C, 0x00D8A850);
				uk_rline (canvas, ix + 3, iy + 7, 16, 11, 2, 0x00906A28, 190);
			}
			else uk_glyph (canvas, WKG_PLUS, ix + SICON / 2, y + SIDE_RH / 2, 10, ink);
			char lab[48];
			scpy (lab, sr.kind == SR_ALL ? TR ("All Games") : sr.kind == SR_SYS ? SYS_NAME(sr.index) : sr.kind == SR_FOLDER ? g_folder[sr.index] : TR ("Add Folder..."), sizeof lab);
			char num[12] = ""; int nw = 0;
			if (n >= 0) { int m = ax_itoa (n, num); num[m] = 0; nw = uk_text_w (num) + 8; }
			char fit[48]; uk_text_fit (lab, SIDE_W - 20 - 44 - nw - 6, fit, sizeof fit);	// (cut at a character, "...")
			canvas.text (44, y + (SIDE_RH - fh) / 2, fit, ink);
			if (n >= 0) canvas.text (SIDE_W - 20 - nw, y + (SIDE_RH - fh) / 2, num, on ? C_SEL_TEXT : dim);
		}
	}

	// The path bar: TR ("Game Library") (a link back to all the games), then what is shown; at the right
	// the number of games.
	void drawBar ()
	{
		canvas.fillRect (0, 0, width, BAR_H, C_BG);
		int fx = 10, fy = 7, fw = width - 20, fh = BAR_H - 14;
		unsigned field = uk_mix (C_BG, C_FIELD, 170), ink = uk_ink_on (field), dim = uk_mix (field, ink, 120);
		uk_rbox (canvas, fx, fy, fw, fh, 8, uk_tone (field, 136), field);
		uk_rline (canvas, fx, fy, fw, fh, 8, uk_tone (C_BG, 88), 190);
		int x = fx + 14, y = fy + (fh - uk_fh ()) / 2;
		bool top = g_filter == F_ALL;
		const char *root = TR ("Game Library");
		int tw = uk_text_w (root, top ? 2 : 0);
		uk_text (canvas, x, y, root, top ? uk_tone (C_ACCENT, 84) : ink, top ? 2 : 0);
		if (top) canvas.fillRect (x, y + uk_fh () + 1, tw, 2, C_ACCENT);
		else if (g_crumbHot == 0) canvas.fillRect (x, y + uk_fh () + 1, tw, 1, ink);
		g_crumbX = x + tw;
		x += tw + 10;
		if (!top)
		{
			uk_glyph (canvas, WKG_CHEV_RIGHT, x + 3, fy + fh / 2, 9, dim);
			x += 18;
			const char *nm = filter_name ();
			uk_text (canvas, x, y, nm, uk_tone (C_ACCENT, 84), 2);
			canvas.fillRect (x, y + uk_fh () + 1, uk_text_w (nm, 2), 2, C_ACCENT);
		}
		char cnt[48]; snprintf (cnt, sizeof cnt, g_nvis == 1 ? TR ("%d game") : TR ("%d games"), g_nvis);
		canvas.text (fx + fw - 14 - uk_text_w (cnt), y, cnt, dim);
	}

	// The status bar: the picture being made, or the game chosen.
	void drawStatus ()
	{
		int y = grid_bottom ();
		uk_rbox (canvas, 0, y, width, ST_H, 0, uk_tone (C_FACE, 160), uk_tone (C_FACE, 124));
		uk_etch_h (canvas, 0, y, width, C_FACE);
		char s[160]; int n = 0; s[0] = 0;
		if (g_tgame >= 0) snprintf (s, sizeof s, TR ("Making the picture of %s..."), g_games[g_tgame].name);
		else if (g_nvis > 0)
		{
			const Game &g = g_games[g_vis[g_sel]];
			lx_cat (s, sizeof s, &n, g.name); lx_cat (s, sizeof s, &n, "   -   "); lx_cat (s, sizeof s, &n, SYS_NAME(g.sys));
			lx_cat (s, sizeof s, &n, g_full ? TR ("   (Enter: play full screen)") : TR ("   (Enter or a double-click: play)"));
		}
		canvas.text (10, y + (ST_H - uk_fh ()) / 2 + 1, s, C_TEXT);
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
static void save_folders () { games_folders_save (g_folder, g_nf); }
static void on_add ()
{
	char p[256];
	if (g_nf >= MAXF) { uk_messagebox (TR ("Game Library"), TR ("Too many folders (8 at most)."), MB_OK); return; }
	if (!uk_folder_open (p, sizeof p, g_folder[g_nf - 1 >= 0 ? g_nf - 1 : 0])) return;
	int e = slen (p); if (e > 1 && p[e - 1] == '/' && p[e - 2] != ':') p[--e] = 0;	// "SD:/roms/" -> "SD:/roms"
	if (e > 63) { uk_messagebox (TR ("Game Library"), TR ("This folder's path is too long (63 characters at most)."), MB_OK); return; }
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
static void on_s6 () { set_filter (F_SYS + 6); } static void on_s7 () { set_filter (F_SYS + 7); }
static void on_s8 () { set_filter (F_SYS + 8); } static void on_s9 () { set_filter (F_SYS + 9); }
static void on_s10 () { set_filter (F_SYS + 10); } static void on_s11 () { set_filter (F_SYS + 11); }
static const MenuAction ON_SYS[MAXSYS] = { on_s0, on_s1, on_s2, on_s3, on_s4, on_s5, on_s6, on_s7, on_s8, on_s9, on_s10, on_s11 };

static Menu g_menu;
static void build_menu ()
{
	g_menu = Menu ();
	g_menu.menu (TR ("Library"));
	g_menu.item (TR ("Play"),              TR ("Enter"), 0, on_play);
	g_menu.item (TR ("Refresh"),           "^R", UK_CTRL ('R'), on_refresh);
	g_menu.separator ();
	g_menu.item (TR ("Quit"),              "^Q", UK_CTRL ('Q'), on_quit);
	g_menu.menu (TR ("Folders"));
	g_menu.item (TR ("Add Folder..."),     "",   0, on_add);
	if (g_nf > 0) g_menu.separator ();
	for (int f = 0; f < g_nf; f++)
	{
		char l[220]; int n = 0; snprintf (l, sizeof l, TR ("Remove %s"), g_folder[f]); (void) n;
		g_menu.item (l, "", 0, ON_RM[f]);
	}
	g_menu.menu (TR ("View"));
	g_menu.item (TR ("All Games"),         "Tab", 0, on_all);
	for (int k = 0; k < NSYS; k++) g_menu.item (SYS_NAME(k), "", 0, ON_SYS[k]);
	g_menu.separator ();
	g_menu.item (TR ("Play Full Screen On / Off"), "", 0, on_full);
	g_menu.publish ();
}

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);		// (before the widgets; false: the bitmap font)
	uk_lang_init ();
	g_nf = games_folders (g_folder, MAXF);		// (GameKit: the watched folders; SD:/roms by default)
	LibRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	build_menu ();
	load_systems ();				// (the installed emulators: their app.txt)
	rescan ();
	if (uk_size_class () != UK_SC_REGULAR)		// pocket, console: the sidebar as a SidePanel
	{
		g_sp = new SidePanel (0, BAR_H, SIDE0, WIN_H - BAR_H - ST_H, UK_SP_LEFT, UK_SP_NAVIGATION);
		g_sp->setIconFn (sp_icon);
		g_sp->onSelect = [] (SidePanel &, int id) { if (id == SP_ADD) { g_sp->select (g_filter); on_add (); } else set_filter (id); if (g_root) g_root->invalidate (true); };
		g_sp->onItemMenu = [] (SidePanel &, int id, int x, int y) { if (id >= F_FOLDER && id < SP_ADD) folder_menu (id - F_FOLDER, x, y); };
		g_sp->onPresentation = [] (SidePanel &, int) { sp_place (); if (g_root) g_root->invalidate (true); };
		root.addChild (g_sp);
	}
	side_rows ();
	sp_place ();
	root.setResizable (true);			// (the grid lays itself out from the width)
	root.attach ();
	while (!should_exit ())
	{
		pump_events ();
		thumb_work ();
		pad_poll ();
		if (!root.valid) { root.draw (); uk_win_present (); }
		kapi_msleep (g_tgame >= 0 ? 1 : 16);
	}
	return 0;
}
