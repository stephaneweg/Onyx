//
// gbemu -- the Onyx Game Boy / Game Boy Color emulator (the core: user/gb).
//
//   gbemu <rom.gb | rom.gbc> [--fullscreen]   (without a ROM: opens the Game Library)
//                               (runners.ini: opening a .gb / .gbc file starts it; the Game
//                               Library app lists the ROMs of a folder)
//   * Keys: arrows = the D-pad, X = A, Z = B, Enter = Start, Backspace = Select (held keys,
//     kapi_key_held); F11 or View > Full Screen: the whole display, stretched with the
//     proportions kept and centred (Esc / F11 back).
//   * View > Zoom 2x / 3x / 4x, Palette (the DMG games' 4 shades), Sound on / off.
//   * The cartridge's battery save is <rom>.sav beside the ROM: read at start, written
//     every few seconds after a change and when the emulator closes.
//   * The pace: the sound output (the frames are made as the audio queue drains), or the
//     clock when there is no sound; 59.73 frames a second.
//
#include "kapi.h"
#include "launch.h"
#include "wtk/wtk.h"
#include "gb/gb.h"

using namespace wtk;

static gb::Machine *g_m = 0;
static unsigned char *g_rom = 0;
static char g_rom_path[256], g_sav_path[260];
static int g_zoom = 3;
static bool g_sound = true, g_paused = false;
static unsigned *g_fs = 0; static int g_fsw, g_fsh;		// full screen back buffer
static Root *g_root = 0;
static int g_audio = 0;						// 0 not tried, 1 ours, -1 none

static int slen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scpy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }

static const unsigned PAL_GREEN[4] = { 0xE0F8D0, 0x88C070, 0x346856, 0x081820 };
static const unsigned PAL_GREY[4] = { 0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000 };
static const unsigned PAL_POCKET[4] = { 0xC4CFA1, 0x8B956D, 0x4D533C, 0x1F1F1F };

// ---- the battery save ----------------------------------------------------------------------
static void save_ram (void)
{
	if (!g_m || !g_m->battery || !g_m->sram || !g_m->sramDirty) return;
	if (kapi_save_file (g_sav_path, (const char *) g_m->sram, (unsigned) g_m->sramSize) >= 0) g_m->sramDirty = false;
}
static void load_ram (void)
{
	if (!g_m->battery || !g_m->sram) return;
	void *f = kapi_open (g_sav_path);
	if (!f) return;
	unsigned n = kapi_fsize (f);
	unsigned char *b = new unsigned char[n + 1];
	int r = kapi_read (f, b, n); kapi_close (f);
	if (r > 0) g_m->setSaveRam (b, r);
	delete [] b;
}

// ---- the window ------------------------------------------------------------------------------
class EmuRoot : public Root
{
public:
	EmuRoot (int w, int h, const char *t) : Root (w, h, t) {}
	void onDraw () override
	{
		// the frame, zoomed (whole-number zoom, centred in the client area)
		canvas.clear (0);
		int z = g_zoom, ox = (width - gb::W * z) / 2, oy = (height - gb::H * z) / 2;
		ox = ox < 0 ? 0 : ox; oy = oy < 0 ? 0 : oy;
		for (int y = 0; y < gb::H; y++)
		{
			const unsigned *s = g_m->fb + y * gb::W;
			for (int k = 0; k < z; k++)
			{
				int yy = oy + y * z + k; if (yy >= height) break;
				unsigned *d = canvas.px + (long) yy * canvas.stride + ox;
				for (int x = 0; x < gb::W && ox + x * z < width; x++)
					for (int j = 0; j < z; j++) d[x * z + j] = s[x];
			}
		}
		if (g_paused) canvas.text (8, 8, "Paused", 0xFFFFFF);
	}
	bool onKey (long k) override;
};

static void full_screen (bool on)
{
	if (on && !g_fs)
	{
		g_fs = kapi_fullscreen_begin (&g_fsw, &g_fsh);
		if (g_fs) for (long i = 0; i < (long) g_fsw * g_fsh; i++) g_fs[i] = 0;
	}
	else if (!on && g_fs) { kapi_fullscreen_end (); g_fs = 0; g_root->invalidate (true); }
}
static void blit_full (void)				// stretched to the display, proportions kept, centred
{
	int ow = g_fsw, oh = (int) ((long) g_fsw * gb::H / gb::W);
	if (oh > g_fsh) { oh = g_fsh; ow = (int) ((long) g_fsh * gb::W / gb::H); }
	int ox = (g_fsw - ow) / 2, oy = (g_fsh - oh) / 2;
	static int xmap[4096];
	for (int x = 0; x < ow && x < 4096; x++) xmap[x] = x * gb::W / ow;
	for (int y = 0; y < oh; y++)
	{
		const unsigned *s = g_m->fb + (y * gb::H / oh) * gb::W;
		unsigned *d = g_fs + (long) (oy + y) * g_fsw + ox;
		for (int x = 0; x < ow && x < 4096; x++) d[x] = s[xmap[x]];
	}
}

static void set_zoom (int z)
{
	g_zoom = z;
	g_root->canvas.adopt (kapi_resize_window (gb::W * z, gb::H * z), gb::W * z, gb::H * z);
	g_root->width = gb::W * z; g_root->height = gb::H * z;
	g_root->invalidate (true);
}
static void on_zoom2 () { set_zoom (2); }
static void on_zoom3 () { set_zoom (3); }
static void on_zoom4 () { set_zoom (4); }
static void on_full () { full_screen (!g_fs); }
static void on_green () { g_m->setDmgPalette (PAL_GREEN); }
static void on_grey () { g_m->setDmgPalette (PAL_GREY); }
static void on_pocket () { g_m->setDmgPalette (PAL_POCKET); }
static void on_sound ()
{
	g_sound = !g_sound;
	if (!g_sound && g_audio == 1) { kapi_sound_release (); g_audio = 0; }
}
static void on_pause () { g_paused = !g_paused; g_root->invalidate (true); }
static void on_reset () { save_ram (); g_m->reset (); load_ram (); }
static void on_quit () { kapi_exit (0); }

bool EmuRoot::onKey (long k)
{
	if (k == KEY_F1 + 10) { on_full (); return true; }			// F11
	if (k == 27 && g_fs) { full_screen (false); return true; }
	if (k == 'p' || k == 'P') { on_pause (); return true; }
	return Root::onKey (k);
}

static int buttons (void)
{
	int b = 0;
	if (kapi_key_held (KEY_RIGHT)) b |= gb::BTN_RIGHT;
	if (kapi_key_held (KEY_LEFT)) b |= gb::BTN_LEFT;
	if (kapi_key_held (KEY_UP)) b |= gb::BTN_UP;
	if (kapi_key_held (KEY_DOWN)) b |= gb::BTN_DOWN;
	if (kapi_key_held ('x')) b |= gb::BTN_A;
	if (kapi_key_held ('z')) b |= gb::BTN_B;
	if (kapi_key_held (KEY_ENTER)) b |= gb::BTN_START;
	if (kapi_key_held (KEY_BACKSPACE)) b |= gb::BTN_SELECT;
	if ((b & gb::BTN_LEFT) && (b & gb::BTN_RIGHT)) b &= ~(gb::BTN_LEFT | gb::BTN_RIGHT);	// (not both)
	if ((b & gb::BTN_UP) && (b & gb::BTN_DOWN)) b &= ~(gb::BTN_UP | gb::BTN_DOWN);
	return b;
}

static void show_frame (void)
{
	if (g_fs) { blit_full (); kapi_present_fb (); }
	else { g_root->invalidate (true); g_root->draw (); kapi_present (); }
}

int main (void)
{
	char args[256] = "";
	kapi_get_args (args, sizeof args);
	int i = 0; while (args[i] == ' ') i++;
	int n = 0;
	bool wantFull = false;
	if (args[i] == '"') { i++; while (args[i] && args[i] != '"' && n < 255) g_rom_path[n++] = args[i++]; if (args[i]) i++; }
	else while (args[i] && n < 255 && !(args[i] == ' ' && args[i + 1] == '-' && args[i + 2] == '-')) g_rom_path[n++] = args[i++];
	while (n > 0 && g_rom_path[n - 1] == ' ') n--;
	g_rom_path[n] = 0;
	if (!g_rom_path[0]) { lx_launch ("gamelib", ""); return 0; }	// no ROM: the Game Library picks one
	// options after the path: --fullscreen (the Game Library's View > Play Full Screen)
	for (; args[i]; i++)
		if (args[i] == '-' && args[i + 1] == '-' && args[i + 2] == 'f' && args[i + 3] == 'u' && args[i + 4] == 'l' && args[i + 5] == 'l') wantFull = true;

	void *f = kapi_open (g_rom_path);
	if (!f) return 1;
	unsigned sz = kapi_fsize (f);
	g_rom = new unsigned char[sz + 1];
	int r = kapi_read (f, g_rom, sz); kapi_close (f);
	g_m = new gb::Machine;
	if (r <= 0 || !g_m->load (g_rom, r)) return 1;
	// <rom>.sav
	scpy (g_sav_path, g_rom_path, sizeof g_sav_path);
	int e = slen (g_sav_path), d = e; while (d > 0 && g_sav_path[d - 1] != '.' && g_sav_path[d - 1] != '/') d--;
	if (d > 0 && g_sav_path[d - 1] == '.') e = d - 1;
	scpy (g_sav_path + e, ".sav", sizeof g_sav_path - e);
	load_ram ();

	char title[48]; scpy (title, g_m->title[0] ? g_m->title : "Game Boy", sizeof title);
	EmuRoot root (gb::W * g_zoom, gb::H * g_zoom, title);
	if (root.canvas.px == 0) return 1;
	g_root = &root;

	static Menu menu;
	menu.menu ("Game");
	menu.item ("Pause",        "P",   0, on_pause);
	menu.item ("Reset",        "",    0, on_reset);
	menu.separator ();
	menu.item ("Quit",         "^Q",  WK_CTRL ('Q'), on_quit);
	menu.menu ("View");
	menu.item ("Full Screen",  "F11", 0, on_full);
	menu.item ("Zoom 2x",      "",    0, on_zoom2);
	menu.item ("Zoom 3x",      "",    0, on_zoom3);
	menu.item ("Zoom 4x",      "",    0, on_zoom4);
	menu.separator ();
	menu.item ("Palette: Green",  "", 0, on_green);
	menu.item ("Palette: Grey",   "", 0, on_grey);
	menu.item ("Palette: Pocket", "", 0, on_pocket);
	menu.menu ("Sound");
	menu.item ("Sound On / Off", "", 0, on_sound);
	menu.publish ();
	root.attach ();
	if (wantFull) full_screen (true);

	static short pcm[4096 * 2];
	unsigned rate = SOUND_RATE, freeFrames = 0, owner = 0;
	g_m->setAudioRate (SOUND_RATE);
	unsigned t0 = kapi_get_ticks (); long long done = 0;		// frames made (clock pacing)
	unsigned lastSave = kapi_get_ticks ();
	while (!should_exit ())
	{
		pump_events ();
		if (g_paused) { root.invalidate (true); show_frame (); kapi_msleep (20); continue; }
		if (g_sound && g_audio == 0) g_audio = kapi_sound_acquire () == 1 ? 1 : -1;
		bool audio = g_sound && g_audio == 1;
		int made = 0;
		if (audio)
		{
			// keep ~3 frames of sound queued: the audio clock paces the game
			kapi_sound_status (&rate, &freeFrames, &owner);
			static unsigned cap = 0; if (freeFrames > cap) cap = freeFrames;
			unsigned queued = cap - freeFrames;
			while (queued < 2400 && made < 3)
			{
				g_m->setButtons (buttons ());
				g_m->runFrame ();
				int k = g_m->audioRead (pcm, 4096);
				if (k > 0) kapi_sound_write (pcm, (unsigned) k);
				queued += (unsigned) k; made++;
			}
			t0 = kapi_get_ticks (); done = 0;
		}
		else
		{
			// the clock: 59.73 frames a second (ticks are 1/100 s)
			long long due = (long long) (kapi_get_ticks () - t0) * 5973 / 10000;
			if (due - done > 6) done = due - 1;			// far behind: skip, do not race
			while (done < due && made < 3)
			{
				g_m->setButtons (buttons ());
				g_m->runFrame ();
				g_m->audioRead (pcm, 4096);			// (dropped)
				done++; made++;
			}
		}
		if (made) show_frame ();
		else kapi_msleep (2);
		if (kapi_get_ticks () - lastSave > 500) { save_ram (); lastSave = kapi_get_ticks (); }	// every 5 s
	}
	save_ram ();
	if (g_fs) kapi_fullscreen_end ();
	return 0;
}
