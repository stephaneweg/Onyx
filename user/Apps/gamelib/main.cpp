//
// gamelib -- the Game Library: every Game Boy / Game Boy Color ROM of a folder (and its
// sub-folders), a tile each -- a picture of its title screen and its name -- in a dark
// grid, one section per system. Click a tile (or arrows + Enter) to play: the ROM opens
// with its runner (SD:/etc/runners.ini: gbemu), in a window or, with View > Play Full
// Screen, on the whole display.
//
//   * The folder: SD:/roms by default; Library > Choose Folder... (kept in config.ini).
//   * The pictures: each game is run a few seconds without being shown (the emulator core,
//     user/gb) and its screen kept -- made in the background, a little every frame, and
//     cached in SD:/apps/gamelib.app/thumbs/ (Library > Refresh finds new ROMs).
//
#include "kapi.h"
#include "applib.h"
#include "launch.h"
#include "wtk/wtk.h"
#include "gb/gb.h"

using namespace wtk;

#define WIN_W	760
#define WIN_H	560
#define TW	160			// a tile's picture: the Game Boy screen at 1x
#define TH	144
#define CELLW	(TW + 24)
#define CELLH	(TH + 44)
#define HEAD_H	34			// a section's title
#define MAXG	256
#define THUMB_FRAMES	420		// ~7 s of game time: past the logos, on the title screen

struct Game { char path[200]; char name[64]; char key[64]; bool color; unsigned *thumb; bool tried; };
static Game g_games[MAXG]; static int g_ng = 0;
static char g_folder[200] = "SD:/roms";
static bool g_full = false;
static int g_scroll = 0, g_sel = 0, g_hover = -1;
static Root *g_root = 0;

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
		bool gbc = ends (e.name, ".gbc"), gbf = ends (e.name, ".gb");
		if (!gbc && !gbf) continue;
		Game &g = g_games[g_ng++];
		scpy (g.path, p, sizeof g.path);
		nice_name (e.name, g.name, sizeof g.name);
		scpy (g.key, e.name, sizeof g.key);
		g.color = gbc; g.thumb = 0; g.tried = false;
	}
	kapi_closedir (d);
}
static void rescan (void)
{
	for (int i = 0; i < g_ng; i++) delete [] g_games[i].thumb;
	g_ng = 0;
	scan (g_folder, 0);
	// Game Boy Color first, then by name
	for (int i = 1; i < g_ng; i++)
	{
		Game t = g_games[i]; int j = i;
		while (j > 0 && (g_games[j - 1].color < t.color || (g_games[j - 1].color == t.color && ci_less (t.name, g_games[j - 1].name)))) { g_games[j] = g_games[j - 1]; j--; }
		g_games[j] = t;
	}
	g_sel = 0; g_scroll = 0;
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

// The picture being made: one game at a time, some frames per call.
static gb::Machine *g_tm = 0; static unsigned char *g_trom = 0; static int g_tgame = -1, g_tframe = 0;
static void thumb_work (void)
{
	if (g_tgame < 0)
	{
		for (int i = 0; i < g_ng; i++)
			if (!g_games[i].thumb && !g_games[i].tried)
			{
				g_games[i].tried = true;
				if (thumb_load (g_games[i])) { g_root->invalidate (true); return; }
				void *f = kapi_open (g_games[i].path); if (!f) return;
				unsigned n = kapi_fsize (f);
				delete [] g_trom; g_trom = new unsigned char[n + 1];
				int r = kapi_read (f, g_trom, n); kapi_close (f);
				if (!g_tm) { g_tm = new gb::Machine; g_tm->setAudioRate (8000); }
				if (r <= 0 || !g_tm->load (g_trom, r)) return;
				g_tgame = i; g_tframe = 0;
				return;
			}
		return;
	}
	static short pcm[4096];
	for (int k = 0; k < 12 && g_tframe < THUMB_FRAMES; k++, g_tframe++) { g_tm->runFrame (); g_tm->audioRead (pcm, 2048); }
	if (g_tframe >= THUMB_FRAMES)
	{
		Game &g = g_games[g_tgame];
		g.thumb = new unsigned[TW * TH];
		for (int i = 0; i < TW * TH; i++) g.thumb[i] = g_tm->fb[i];
		thumb_save (g);
		g_tgame = -1;
		g_root->invalidate (true);
	}
}

// ---- the grid ---------------------------------------------------------------------------------------
static int cols (void) { int c = (g_root->width - 20) / CELLW; return c < 1 ? 1 : c; }
// Layout: sections (Color, then Game Boy), each a title and rows of tiles. Tile i -> x, y.
static void tile_pos (int i, int *x, int *y)
{
	int c = cols (), yy = 8, k = 0;
	for (int sec = 0; sec < 2; sec++)
	{
		bool color = sec == 0;
		int first = k, n = 0; while (k + n < g_ng && g_games[k + n].color == color) n++;
		if (!n) continue;
		yy += HEAD_H;
		if (i >= first && i < first + n)
		{
			int j = i - first;
			*x = 10 + (j % c) * CELLW; *y = yy + (j / c) * CELLH - g_scroll;
			return;
		}
		yy += ((n + c - 1) / c) * CELLH + 10;
		k += n;
	}
	*x = 0; *y = yy - g_scroll;
}
static int content_h (void) { int x, y; tile_pos (g_ng, &x, &y); return y + g_scroll + 20; }
static int tile_at (int mx, int my)
{
	for (int i = 0; i < g_ng; i++)
	{
		int x, y; tile_pos (i, &x, &y);
		if (mx >= x && mx < x + CELLW - 8 && my >= y && my < y + CELLH - 8) return i;
	}
	return -1;
}
static void ensure_visible (int i)
{
	int x, y; tile_pos (i, &x, &y);
	if (y < 8) g_scroll += y - 8 - HEAD_H;
	else if (y + CELLH > g_root->height) g_scroll += y + CELLH - g_root->height + 8;
	if (g_scroll < 0) g_scroll = 0;
}
static void play (int i)
{
	if (i < 0 || i >= g_ng) return;
	lx_open (g_games[i].path, g_full ? "--fullscreen" : "");
}

class LibRoot : public Root
{
public:
	LibRoot () : Root (WIN_W, WIN_H, "Game Library") {}
	void onDraw () override
	{
		canvas.clear (0x00141414);
		if (g_ng == 0)
		{
			canvas.text (24, 30, "No Game Boy ROM found in", 0x00C8C8C8);
			canvas.text (24, 52, g_folder, 0x00FFFFFF);
			canvas.text (24, 84, "Put .gb / .gbc files there (sub-folders too), or Library > Choose Folder...", 0x00909090);
			return;
		}
		// section titles
		int c = cols (), yy = 8 - g_scroll, k = 0;
		for (int sec = 0; sec < 2; sec++)
		{
			bool color = sec == 0;
			int n = 0; while (k + n < g_ng && g_games[k + n].color == color) n++;
			if (!n) continue;
			canvas.text (12, yy + 8, color ? "Game Boy Color" : "Game Boy", 0x00FFFFFF);
			canvas.text (13, yy + 8, color ? "Game Boy Color" : "Game Boy", 0x00FFFFFF);
			yy += HEAD_H + ((n + c - 1) / c) * CELLH + 10;
			k += n;
		}
		for (int i = 0; i < g_ng; i++)
		{
			int x, y; tile_pos (i, &x, &y);
			if (y + CELLH < 0 || y > height) continue;
			const Game &g = g_games[i];
			bool hi = i == g_hover || i == g_sel;
			int px = x + 6, py = y + 4;
			canvas.fillRect (x, y, CELLW - 8, CELLH - 8, hi ? 0x00303848 : 0x001E1E1E);
			if (hi) canvas.frameRect (x, y, CELLW - 8, CELLH - 8, i == g_sel ? 0x00E0E0E0 : 0x00707888);
			if (g.thumb)
				for (int r = 0; r < TH; r++)
				{
					int yy2 = py + r; if (yy2 < 0 || yy2 >= height) continue;
					unsigned *d = canvas.px + (long) yy2 * canvas.stride + px;
					const unsigned *s = g.thumb + r * TW;
					for (int q = 0; q < TW && px + q < width; q++) d[q] = s[q];
				}
			else
			{
				canvas.fillRect (px, py, TW, TH, 0x00282828);
				canvas.text (px + 40, py + TH / 2 - 8, g_tgame == i ? "(loading)" : "", 0x00808080);
			}
			char nm[24]; scpy (nm, g.name, sizeof nm);
			if (slen (g.name) > 20) { nm[19] = '.'; nm[20] = '.'; nm[21] = 0; }
			canvas.text (px, py + TH + 8, nm, hi ? 0x00FFFFFF : 0x00C8C8C8);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel) { g_scroll -= wheel * 40; clamp (); invalidate (true); return true; }
		int t = mx >= 0 ? tile_at (mx, my) : -1;
		if (t != g_hover) { g_hover = t; invalidate (true); }
		static bool down = false;
		if (bl && !down && t >= 0) { g_sel = t; invalidate (true); }
		if (!bl && down && t >= 0 && t == g_sel) play (t);
		down = bl != 0;
		return true;
	}
	bool onKey (long k) override
	{
		int c = cols (), old = g_sel;
		if (k == KEY_RIGHT) g_sel++;
		else if (k == KEY_LEFT) g_sel--;
		else if (k == KEY_DOWN) g_sel += c;
		else if (k == KEY_UP) g_sel -= c;
		else if (k == KEY_ENTER || k == ' ') { play (g_sel); return true; }
		else if (k == KEY_PGDN) g_scroll += height - 60;
		else if (k == KEY_PGUP) g_scroll -= height - 60;
		else return Root::onKey (k);
		if (g_sel >= g_ng) g_sel = g_ng - 1;
		if (g_sel < 0) g_sel = 0;
		if (g_sel != old) ensure_visible (g_sel);
		clamp (); invalidate (true);
		return true;
	}
	void clamp () { int mx = content_h () - height; if (g_scroll > mx) g_scroll = mx; if (g_scroll < 0) g_scroll = 0; }
};

static void on_refresh () { rescan (); g_root->invalidate (true); }
static void on_full () { g_full = !g_full; g_root->invalidate (true); }
static void on_folder ()
{
	// pick any file of the folder (a ROM): its folder becomes the library's
	char p[256];
	if (!wk_file_open (p, sizeof p, g_folder)) return;
	int e = slen (p); while (e > 0 && p[e - 1] != '/') e--;
	if (e > 1 && p[e - 2] != ':') e--;			// "SD:/roms/x.gbc" -> "SD:/roms" ("SD:/x" -> "SD:/")
	p[e] = 0;
	if (!p[0]) return;
	scpy (g_folder, p, sizeof g_folder);
	char ini[300]; int n = 0;
	lx_cat (ini, sizeof ini, &n, "; Game Library settings\nfolder = "); lx_cat (ini, sizeof ini, &n, g_folder); lx_cat (ini, sizeof ini, &n, "\n");
	kapi_save_file ("SD:/apps/gamelib.app/config.ini", ini, (unsigned) n);
	on_refresh ();
}
static void on_quit () { kapi_exit (0); }
static void on_play () { play (g_sel); }

int main (void)
{
	if (app_ini_load_path ("SD:/apps/gamelib.app/config.ini") >= 0) scpy (g_folder, app_ini_get (0, "folder", "SD:/roms"), sizeof g_folder);
	LibRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	static Menu menu;
	menu.menu ("Library");
	menu.item ("Play",              "Enter", 0, on_play);
	menu.item ("Refresh",           "^R", WK_CTRL ('R'), on_refresh);
	menu.item ("Choose Folder...",  "",   0, on_folder);
	menu.separator ();
	menu.item ("Quit",              "^Q", WK_CTRL ('Q'), on_quit);
	menu.menu ("View");
	menu.item ("Play Full Screen On / Off", "", 0, on_full);
	menu.publish ();
	rescan ();
	root.attach ();
	while (!should_exit ())
	{
		pump_events ();
		thumb_work ();
		if (!root.valid) { root.draw (); kapi_present (); }
		kapi_msleep (g_tgame >= 0 ? 1 : 16);
	}
	return 0;
}
