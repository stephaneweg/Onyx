//
// gbaemu -- the Onyx Game Boy Advance emulator (the core: user/gba).
//
//   gbaemu <rom.gba> [--fullscreen]   (without a ROM: opens the Game Library)
//                               (runners.ini: opening a .gba file starts it; the Game Library
//                               app lists the ROMs of a folder)
//   * Keys: arrows = the D-pad, X = A, Z = B, A = L, S = R, Enter = Start, Backspace = Select
//     (held keys, kapi_key_held); a USB gamepad too (user/gamepad.h: right / top button = A,
//     bottom / left = B, the shoulders L / R, Start, Select); F11 or View > Full Screen: the
//     whole display, stretched with the proportions kept and centred (Esc / F11 back).
//   * View > Zoom 2x / 3x / 4x, Sound on / off.
//   * The cartridge's save (SRAM, Flash or EEPROM, found in the ROM) is <rom>.sav beside the
//     ROM: read at start, written every few seconds after a change and when the emulator closes.
//   * The pace: the sound output (the frames are made as the audio queue drains), or the
//     clock when there is no sound; 59.73 frames a second.
//
#include "kapi.h"
#include "launch.h"
#include "gamepad.h"
#include "wtk/wtk.h"
#include "gba/gba.h"

using namespace wtk;

static gba::Machine *g_m = 0;
static unsigned char *g_rom = 0;
static char g_rom_path[256], g_sav_path[260];
static int g_zoom = 3;
static bool g_sound = true, g_paused = false;
static unsigned *g_fs = 0; static int g_fsw, g_fsh;		// full screen back buffer
static Root *g_root = 0;
static int g_audio = 0;						// 0 not tried, 1 ours, -1 none
static bool g_stats = false;					// View > Show Speed
static char g_statText[96] = "";

// A microsecond clock: the ARM generic timer (apps run at EL1).
static inline unsigned long long now_us (void)
{
	unsigned long long c, f;
	asm volatile ("mrs %0, cntpct_el0" : "=r" (c));
	asm volatile ("mrs %0, cntfrq_el0" : "=r" (f));
	return f ? c * 1000000ull / f : 0;
}
static void fmt_num (char *d, int *n, unsigned v, int decimals)	// v in 1/10^decimals
{
	char t[16]; int k = 0;
	unsigned ip = v, fp = 0, div = 1;
	for (int i = 0; i < decimals; i++) div *= 10;
	ip = v / div; fp = v % div;
	do { t[k++] = (char) ('0' + ip % 10); ip /= 10; } while (ip);
	while (k) d[(*n)++] = t[--k];
	if (decimals) { d[(*n)++] = '.'; for (unsigned m = div / 10; m; m /= 10) { d[(*n)++] = (char) ('0' + (fp / m) % 10); } }
	d[*n] = 0;
}
static void cat (char *d, int *n, const char *s) { while (*s) d[(*n)++] = *s++; d[*n] = 0; }

static int slen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scpy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }


// ---- the cartridge save -------------------------------------------------------------------
static void save_ram (void)
{
	if (!g_m || g_m->saveType == gba::SAVE_NONE || !g_m->saveSize || !g_m->saveDirty) return;
	if (kapi_save_file (g_sav_path, (const char *) g_m->save, (unsigned) g_m->saveSize) >= 0) g_m->saveDirty = false;
}
static void load_ram (void)
{
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
		int z = g_zoom;
		if (gba::W * z != width || gba::H * z != height) canvas.clear (0);
		int ox = (width - gba::W * z) / 2, oy = (height - gba::H * z) / 2;
		ox = ox < 0 ? 0 : ox; oy = oy < 0 ? 0 : oy;
		for (int y = 0; y < gba::H; y++)
		{
			const unsigned *s = g_m->fb + y * gba::W;
			for (int k = 0; k < z; k++)
			{
				int yy = oy + y * z + k; if (yy >= height) break;
				unsigned *d = canvas.px + (long) yy * canvas.stride + ox;
				for (int x = 0; x < gba::W && ox + x * z < width; x++)
					for (int j = 0; j < z; j++) d[x * z + j] = s[x];
			}
		}
		if (g_paused) canvas.text (8, 8, "Paused", 0xFFFFFF);
		if (g_stats) { canvas.fillRect (0, height - 20, width, 20, 0); canvas.text (4, height - 18, g_statText, 0x00FFFF60); }
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
	int ow = g_fsw, oh = (int) ((long) g_fsw * gba::H / gba::W);
	if (oh > g_fsh) { oh = g_fsh; ow = (int) ((long) g_fsh * gba::W / gba::H); }
	int ox = (g_fsw - ow) / 2, oy = (g_fsh - oh) / 2;
	static int xmap[4096];
	for (int x = 0; x < ow && x < 4096; x++) xmap[x] = x * gba::W / ow;
	for (int y = 0; y < oh; y++)
	{
		int sy = y * gba::H / oh;
		unsigned *d = g_fs + (long) (oy + y) * g_fsw + ox;
		if (y > 0 && sy == (y - 1) * gba::H / oh)		// the same source row: copy the line above
		{
			const unsigned *u = d - g_fsw;
			for (int x = 0; x < ow && x < 4096; x++) d[x] = u[x];
			continue;
		}
		const unsigned *s = g_m->fb + sy * gba::W;
		for (int x = 0; x < ow && x < 4096; x++) d[x] = s[xmap[x]];
	}
	if (g_stats)
	{
		Canvas c; c.adopt (g_fs, g_fsw, g_fsh);
		c.fillRect (0, g_fsh - 20, g_fsw, 20, 0); c.text (4, g_fsh - 18, g_statText, 0x00FFFF60);
	}
}

static void set_zoom (int z)
{
	g_zoom = z;
	g_root->canvas.adopt (kapi_resize_window (gba::W * z, gba::H * z), gba::W * z, gba::H * z);
	g_root->width = gba::W * z; g_root->height = gba::H * z;
	g_root->invalidate (true);
}
static void on_zoom2 () { set_zoom (2); }
static void on_zoom3 () { set_zoom (3); }
static void on_zoom4 () { set_zoom (4); }
static void on_full () { full_screen (!g_fs); }
static void on_sound ()
{
	g_sound = !g_sound;
	if (!g_sound && g_audio == 1) { kapi_sound_release (); g_audio = 0; }
}
static void on_pause () { g_paused = !g_paused; g_root->invalidate (true); }
static void on_stats () { g_stats = !g_stats; g_root->invalidate (true); }
static void on_reset () { save_ram (); g_m->reset (); load_ram (); }
static void on_quit () { kapi_exit (0); }

bool EmuRoot::onKey (long k)
{
	if (k == KEY_F1 + 10) { on_full (); return true; }			// F11
	if (k == 27 && g_fs) { full_screen (false); return true; }
	if (k == 'p' || k == 'P') { on_pause (); return true; }
	if (k == KEY_F1 + 11) { on_stats (); return true; }		// F12
	return Root::onKey (k);
}

static int buttons (void)
{
	int b = 0;
	if (kapi_key_held (KEY_RIGHT)) b |= gba::BTN_RIGHT;
	if (kapi_key_held (KEY_LEFT)) b |= gba::BTN_LEFT;
	if (kapi_key_held (KEY_UP)) b |= gba::BTN_UP;
	if (kapi_key_held (KEY_DOWN)) b |= gba::BTN_DOWN;
	if (kapi_key_held ('x')) b |= gba::BTN_A;
	if (kapi_key_held ('z')) b |= gba::BTN_B;
	if (kapi_key_held (KEY_ENTER)) b |= gba::BTN_START;
	if (kapi_key_held (KEY_BACKSPACE)) b |= gba::BTN_SELECT;
	if (kapi_key_held ('a')) b |= gba::BTN_L;
	if (kapi_key_held ('s')) b |= gba::BTN_R;
	// USB gamepads (user/gamepad.h): by place, as on Nintendo's pads -- the right face
	// button is A, the bottom one B (and the top / left ones the same), the shoulders L / R
	unsigned p = pad_buttons (-1);
	if (p & PAD_RIGHT) b |= gba::BTN_RIGHT;
	if (p & PAD_LEFT) b |= gba::BTN_LEFT;
	if (p & PAD_UP) b |= gba::BTN_UP;
	if (p & PAD_DOWN) b |= gba::BTN_DOWN;
	if (p & (PAD_B | PAD_Y)) b |= gba::BTN_A;
	if (p & (PAD_A | PAD_X)) b |= gba::BTN_B;
	if (p & PAD_START) b |= gba::BTN_START;
	if (p & PAD_SELECT) b |= gba::BTN_SELECT;
	if (p & (PAD_L | PAD_L2)) b |= gba::BTN_L;
	if (p & (PAD_R | PAD_R2)) b |= gba::BTN_R;
	if ((b & gba::BTN_LEFT) && (b & gba::BTN_RIGHT)) b &= ~(gba::BTN_LEFT | gba::BTN_RIGHT);	// (not both)
	if ((b & gba::BTN_UP) && (b & gba::BTN_DOWN)) b &= ~(gba::BTN_UP | gba::BTN_DOWN);
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
	if (sz > 0x2000000) sz = 0x2000000;				// (32 MB: the largest cartridge)
	g_rom = new unsigned char[sz + 1];
	int r = 0;							// (in 1 MB pieces: a big ROM takes a while)
	while ((unsigned) r < sz) { unsigned k = sz - (unsigned) r > 0x100000 ? 0x100000 : sz - (unsigned) r; int got = kapi_read (f, g_rom + r, k); if (got <= 0) break; r += got; }
	kapi_close (f);
	g_m = new gba::Machine;
	if (r <= 0 || !g_m->load (g_rom, r)) return 1;
	// <rom>.sav
	scpy (g_sav_path, g_rom_path, sizeof g_sav_path);
	int e = slen (g_sav_path), d = e; while (d > 0 && g_sav_path[d - 1] != '.' && g_sav_path[d - 1] != '/') d--;
	if (d > 0 && g_sav_path[d - 1] == '.') e = d - 1;
	scpy (g_sav_path + e, ".sav", sizeof g_sav_path - e);
	load_ram ();

	char title[48]; scpy (title, g_m->title[0] ? g_m->title : "Game Boy Advance", sizeof title);
	EmuRoot root (gba::W * g_zoom, gba::H * g_zoom, title);
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
	menu.item ("Show Speed",   "F12", 0, on_stats);
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
	// View > Show Speed: frames emulated / shown a second, the time of one emulated frame
	// and of one shown frame (drawing + the compositor), the sound queued
	unsigned long long stT = now_us (), emuUs = 0, drawUs = 0; unsigned stEmu = 0, stShown = 0, stQueued = 0;
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
				unsigned long long e0 = now_us ();
				g_m->runFrame ();
				emuUs += now_us () - e0; stEmu++;
				int k = g_m->audioRead (pcm, 4096);
				if (k > 0) kapi_sound_write (pcm, (unsigned) k);
				queued += (unsigned) k; made++;
			}
			stQueued = queued;
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
				unsigned long long e0 = now_us ();
				g_m->runFrame ();
				emuUs += now_us () - e0; stEmu++;
				g_m->audioRead (pcm, 4096);			// (dropped)
				done++; made++;
			}
		}
		if (made) { unsigned long long d0 = now_us (); show_frame (); drawUs += now_us () - d0; stShown++; }
		else kapi_msleep (2);
		unsigned long long tn = now_us ();
		if (tn - stT >= 1000000)
		{
			unsigned long long el = tn - stT;
			int n = 0;
			fmt_num (g_statText, &n, (unsigned) ((unsigned long long) stEmu * 10000000ull / el), 1); cat (g_statText, &n, " fps  shown ");
			fmt_num (g_statText, &n, (unsigned) ((unsigned long long) stShown * 10000000ull / el), 1); cat (g_statText, &n, "  emu ");
			fmt_num (g_statText, &n, stEmu ? (unsigned) (emuUs / stEmu / 100) : 0, 1); cat (g_statText, &n, " ms  draw ");
			fmt_num (g_statText, &n, stShown ? (unsigned) (drawUs / stShown / 100) : 0, 1); cat (g_statText, &n, " ms");
			if (audio) { cat (g_statText, &n, "  sound "); fmt_num (g_statText, &n, stQueued * 1000 / SOUND_RATE, 0); cat (g_statText, &n, " ms"); }
			stT = tn; emuUs = drawUs = 0; stEmu = stShown = 0;
		}
		if (kapi_get_ticks () - lastSave > 500) { save_ram (); lastSave = kapi_get_ticks (); }	// every 5 s
	}
	save_ram ();
	if (g_fs) kapi_fullscreen_end ();
	return 0;
}
