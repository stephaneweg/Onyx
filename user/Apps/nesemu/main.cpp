//
// nesemu -- the Onyx NES / Famicom emulator (the core: user/Emulators/nes).
//
//   nesemu <rom.nes> [--fullscreen]   (without a ROM: opens the Game Library)
//                               (its app.txt "games": opening a .nes file starts it; the Game Library
//                               app lists the ROMs of a folder)
//   * Keys: the keyboard is pad 0 (gamepad.h's [keyboard], the Gamepad applet: by default the arrows, X = A,
//     Z = B, Enter = Start, Backspace = Select); a USB gamepad too (user/Include/gamepad.h: right / top button = A, bottom /
//     left = B, Start, Select); F11 or View > Full Screen: the whole display, stretched with the
//     proportions kept and centred (Esc / F11 back).
//   * View > Zoom 1x / 2x / 3x, Region: NTSC (60 Hz) / PAL (50 Hz) -- guessed from the ROM's
//     header or its name ("(Europe)", "(E)", "(PAL)"...), Sound on / off.
//   * The cartridge's battery save is <rom>.sav beside the ROM: read at start, written
//     every few seconds after a change and when the emulator closes.
//   * The pace: the sound output (the frames are made as the audio queue drains), or the
//     clock when there is no sound; 60.10 (NTSC) or 50.01 (PAL) frames a second.
//   * The machine runs on an app core (core 2 or 3, user/Emulators/emucore.h) when one is free: the
//     window, the input and the sound stay on this thread. Without a free core it runs here.
//   * Mappers: 0 (NROM), 1 (MMC1), 2 (UxROM), 3 (CNROM), 4 (MMC3), 7 (AxROM), 66 (GxROM).
//
#include "audiokit/audiokit.h"
#include "appkit/appkit.h"
#include "gamepad.h"
#include "uikit/uikit.h"
#include "nes/nes.h"
#include "uikit/dialog.h"
#include "emucore.h"

using namespace uikit;

static nes::Machine *g_m = 0;
static unsigned char *g_rom = 0;
static char g_rom_path[256], g_sav_path[260];
static int g_zoom = 2;
static bool g_sound = true, g_paused = false;
static unsigned *g_fs = 0; static int g_fsw, g_fsh;		// full screen back buffer
static Root *g_root = 0;
static int g_audio = 0;						// 0 not tried, 1 ours, -1 none
static bool g_stats = false;					// View > Show Speed
static char g_statText[96] = "";
static EmuCore g_ec;
static int g_stride;						// the window canvas's row pitch (its 4x width)
static bool g_loading = true;					// the loading screen, until the ROM is in
static unsigned g_loadDone = 0, g_loadSize = 1;
static char g_loadName[64];						// the machine's thread (emucore.h)
static volatile bool g_audioOn = false;				// the machine keeps its sound

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

// the frame rate x 100 (NTSC 60.10, PAL 50.01)
static unsigned fps100 (void) { return g_m && g_m->pal ? 5001 : 6010; }

// "(Europe)", "(E)", "(PAL)", "(Australia)"... in the file name: a PAL game
static bool name_says_pal (const char *p)
{
	static const char *const tags[] = { "(Europe", "(E)", "(PAL", "(Australia", "(Germany", "(France", "(Spain", "(Italy", "(Sweden", "(Netherlands", "(UK" };
	for (int i = 0; p[i]; i++)
		for (unsigned t = 0; t < sizeof tags / sizeof tags[0]; t++)
		{
			const char *q = tags[t]; int k = 0;
			while (q[k] && p[i + k] == q[k]) k++;
			if (!q[k]) return true;
		}
	return false;
}

// ---- the battery save ----------------------------------------------------------------------
static void save_ram (void)
{
	if (!g_m || !g_m->battery || !g_m->sramDirty) return;
	if (kapi_save_file (g_sav_path, (const char *) g_m->sram, (unsigned) sizeof g_m->sram) >= 0) g_m->sramDirty = false;
}
static void load_ram (void)
{
	if (!g_m->battery) return;
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
		if (g_loading)						// the ROM being read: its name and a bar
		{
			canvas.clear (0);
			canvas.text (8, height / 2 - 24, "Loading...", 0xFFFFFF);
			canvas.text (8, height / 2 - 6, g_loadName, 0xA0A0A0);
			int bw = width - 16;
			canvas.fillRect (8, height / 2 + 14, bw, 6, 0x404040);
			canvas.fillRect (8, height / 2 + 14, (int) ((long long) bw * g_loadDone / g_loadSize), 6, 0x40C040);
			return;
		}
		// the frame, zoomed (whole-number zoom, centred in the client area)
		int z = g_zoom;
		if (nes::W * z != width || nes::H * z != height) canvas.clear (0);
		int ox = (width - nes::W * z) / 2, oy = (height - nes::H * z) / 2;
		ox = ox < 0 ? 0 : ox; oy = oy < 0 ? 0 : oy;
		for (int y = 0; y < nes::H; y++)
		{
			const unsigned *s = ec_front (&g_ec) + y * nes::W;
			int yy = oy + y * z; if (yy >= height) break;
			unsigned *d = canvas.px + (long) yy * canvas.stride + ox;
			int n = 0;					// one zoomed row...
			for (int x = 0; x < nes::W && ox + x * z < width; x++)
			{
				unsigned c = s[x];
				for (int j = 0; j < z; j++) d[n++] = c;
			}
			for (int k = 1; k < z && yy + k < height; k++)	// ... copied to the next ones
			{
				unsigned *r = d + (long) k * canvas.stride;
				for (int i = 0; i < n; i++) r[i] = d[i];
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
		g_fs = uk_win_fullscreen_begin (&g_fsw, &g_fsh);
		if (g_fs) for (long i = 0; i < (long) g_fsw * g_fsh; i++) g_fs[i] = 0;
	}
	else if (!on && g_fs) { uk_win_fullscreen_end (); g_fs = 0; g_root->invalidate (true); }
}
static void blit_full (void)				// stretched to the display, proportions kept, centred
{
	int ow = g_fsw, oh = (int) ((long) g_fsw * nes::H / nes::W);
	if (oh > g_fsh) { oh = g_fsh; ow = (int) ((long) g_fsh * nes::W / nes::H); }
	int ox = (g_fsw - ow) / 2, oy = (g_fsh - oh) / 2;
	static int xmap[4096];
	for (int x = 0; x < ow && x < 4096; x++) xmap[x] = x * nes::W / ow;
	for (int y = 0; y < oh; y++)
	{
		int sy = y * nes::H / oh;
		unsigned *d = g_fs + (long) (oy + y) * g_fsw + ox;
		if (y > 0 && sy == (y - 1) * nes::H / oh)		// the same source row: copy the line above
		{
			const unsigned *u = d - g_fsw;
			for (int x = 0; x < ow && x < 4096; x++) d[x] = u[x];
			continue;
		}
		const unsigned *s = ec_front (&g_ec) + sy * nes::W;
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
	// (the window's buffer keeps the pitch it was made with: 3x -- draw with that one)
	g_root->canvas.adopt (uk_win_resize (nes::W * z, nes::H * z), nes::W * z, nes::H * z, g_stride);
	uikit::uk_decorate_window ();					// the frame follows
	g_root->width = nes::W * z; g_root->height = nes::H * z;
	g_root->invalidate (true);
}
static void on_zoom1 () { set_zoom (1); }
static void on_zoom2 () { set_zoom (2); }
static void on_zoom3 () { set_zoom (3); }
static void on_full () { full_screen (!g_fs); }
static void region (bool pal) { ec_hold (&g_ec); g_m->setPal (pal); ec_resume (&g_ec); }
static void on_ntsc () { region (false); }
static void on_pal () { region (true); }
static void on_sound ()
{
	g_sound = !g_sound;
	if (!g_sound && g_audio == 1) { g_audioOn = false; ak_out_close (); g_audio = 0; }
}
static void on_pause () { g_paused = !g_paused; g_root->invalidate (true); }
static void on_stats () { g_stats = !g_stats; g_root->invalidate (true); }
static void on_reset () { ec_hold (&g_ec); save_ram (); g_m->reset (); load_ram (); ec_resume (&g_ec); }
static void on_quit () { kapi_exit (0); }

bool EmuRoot::onKey (long k)
{
	if (g_loading) return Root::onKey (k);
	if (k == KEY_F1 + 10) { on_full (); return true; }			// F11
	if (k == 27 && g_fs) { full_screen (false); return true; }
	if (k == 'p' || k == 'P') { on_pause (); return true; }
	if (k == KEY_F1 + 11) { on_stats (); return true; }		// F12
	return Root::onKey (k);
}

static int buttons (void)
{
	int b = 0;
	// USB gamepads (user/Include/gamepad.h): by place, as on Nintendo's pads -- the right face
	// button is A, the bottom one B (and the top / left ones the same)
	unsigned p = pad_buttons (-1);
	if (p & PAD_RIGHT) b |= nes::BTN_RIGHT;
	if (p & PAD_LEFT) b |= nes::BTN_LEFT;
	if (p & PAD_UP) b |= nes::BTN_UP;
	if (p & PAD_DOWN) b |= nes::BTN_DOWN;
	if (p & (PAD_B | PAD_Y)) b |= nes::BTN_A;
	if (p & (PAD_A | PAD_X)) b |= nes::BTN_B;
	if (p & PAD_START) b |= nes::BTN_START;
	if (p & PAD_SELECT) b |= nes::BTN_SELECT;
	if ((b & nes::BTN_LEFT) && (b & nes::BTN_RIGHT)) b &= ~(nes::BTN_LEFT | nes::BTN_RIGHT);	// (not both)
	if ((b & nes::BTN_UP) && (b & nes::BTN_DOWN)) b &= ~(nes::BTN_UP | nes::BTN_DOWN);
	return b;
}

// One frame of the machine: on the app core (no kapi call, no allocation here).
static void nes_frame (EmuCore *ec)
{
	static short pcm[4096 * 2];
	g_m->setButtons (ec->btn);
	g_m->runFrame ();
	unsigned *d = ec_back (ec);
	const unsigned *s = g_m->fb;
	for (int i = 0; i < nes::W * nes::H; i++) d[i] = s[i];
	ec_publish (ec);
	int k = g_m->audioRead (pcm, 4096);
	if (k > 0 && g_audioOn) ec_audio_push (ec, pcm, k);
}

static void show_frame (void)
{
	if (g_fs) { blit_full (); kapi_present_fb (); }
	else { g_root->invalidate (true); g_root->draw (); uk_win_present (); }
}

int main (void)
{
	pad_keyboard (1);				// (the keyboard is pad 0 too: gamepad.h's [keyboard] keys)
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

	// The window first, with a loading screen, then the ROM in pieces (a big one takes a
	// while): the header gives the title at once.
	void *f = kapi_open (g_rom_path);
	if (!f) return 1;
	unsigned sz = kapi_fsize (f);
	g_rom = new unsigned char[sz + 1];
	int r = kapi_read (f, g_rom, sz < 16 ? sz : 16);
	if (r <= 0) { kapi_close (f); return 1; }
	// the title: the file name without its extension (an iNES header has none)
	char title[64];
	{ int b = slen (g_rom_path); while (b > 0 && g_rom_path[b - 1] != '/' && g_rom_path[b - 1] != ':') b--; scpy (g_loadName, g_rom_path + b, sizeof g_loadName); }
	{
		scpy (title, g_loadName, sizeof title);
		int e = slen (title); while (e > 0 && title[e - 1] != '.') e--;
		if (e > 1) title[e - 1] = 0;
	}
	// made at the 3x size (its buffer keeps that pitch), then shown at the zoom chosen
	g_stride = nes::W * 3;
	EmuRoot root (nes::W * 3, nes::H * 3, title);
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	set_zoom (g_zoom);
	root.attach ();
	g_loadSize = sz ? sz : 1; g_loadDone = (unsigned) r;
	root.invalidate (true); root.draw (); uk_win_present ();
	while ((unsigned) r < sz)
	{
		unsigned k = sz - (unsigned) r > 0x40000 ? 0x40000 : sz - (unsigned) r;
		int got = kapi_read (f, g_rom + r, k);
		if (got <= 0) break;
		r += got; g_loadDone = (unsigned) r;
		pump_events ();
		if (should_exit ()) { kapi_close (f); return 0; }
		root.invalidate (true); root.draw (); uk_win_present ();
	}
	kapi_close (f);
	g_m = new nes::Machine;
	if (!g_m->load (g_rom, r))
	{
		char msg[160]; int n = 0;
		if (r >= 16 && g_rom[0] == 'N' && g_rom[1] == 'E' && g_rom[2] == 'S')
		{
			int mp = (g_rom[6] >> 4) | (g_rom[7] & 0xF0);
			cat (msg, &n, "This game uses mapper "); fmt_num (msg, &n, (unsigned) mp, 0);
			cat (msg, &n, ", not supported yet.\nSupported: 0, 1, 2, 3, 4, 7, 66.");
		}
		else cat (msg, &n, "Not a NES ROM (iNES / NES 2.0).");
		g_loading = false;
		uk_messagebox ("NES", msg, MB_OK);
		return 1;
	}
	if (!g_m->pal && name_says_pal (g_loadName)) g_m->setPal (true);
	// <rom>.sav
	scpy (g_sav_path, g_rom_path, sizeof g_sav_path);
	int e = slen (g_sav_path), d = e; while (d > 0 && g_sav_path[d - 1] != '.' && g_sav_path[d - 1] != '/') d--;
	if (d > 0 && g_sav_path[d - 1] == '.') e = d - 1;
	scpy (g_sav_path + e, ".sav", sizeof g_sav_path - e);
	load_ram ();

	static Menu menu;
	menu.menu ("Game");
	menu.item ("Pause",        "P",   0, on_pause);
	menu.item ("Reset",        "",    0, on_reset);
	menu.separator ();
	menu.item ("Quit",         "^Q",  UK_CTRL ('Q'), on_quit);
	menu.menu ("View");
	menu.item ("Full Screen",  "F11", 0, on_full);
	menu.item ("Zoom 1x",      "",    0, on_zoom1);
	menu.item ("Zoom 2x",      "",    0, on_zoom2);
	menu.item ("Zoom 3x",      "",    0, on_zoom3);
	menu.separator ();
	menu.item ("Region: NTSC (60 Hz)", "", 0, on_ntsc);
	menu.item ("Region: PAL (50 Hz)",  "", 0, on_pal);
	menu.separator ();
	menu.item ("Show Speed",   "F12", 0, on_stats);
	menu.menu ("Sound");
	menu.item ("Sound On / Off", "", 0, on_sound);
	menu.publish ();
	if (wantFull) full_screen (true);

	static short pcm[4096 * 2];
	unsigned freeFrames = 0;
	g_m->setAudioRate (SOUND_RATE);
	if (!ec_init (&g_ec, nes::W, nes::H, nes_frame)) return 1;
	g_loading = false;
	
	unsigned t0 = kapi_get_ticks (); unsigned asked = 0; unsigned lastFps = fps100 ();		// frames asked for (clock pacing)
	unsigned lastSave = kapi_get_ticks ();
	// View > Show Speed: frames emulated / shown a second, the time of one emulated frame
	// and of one shown frame (drawing + the compositor), the sound queued, where it runs
	unsigned long long stT = now_us (), drawUs = 0, stEmuUs = 0; unsigned stDone = 0, stShown = 0, stQueued = 0;
	while (!should_exit ())
	{
		pump_events ();
		g_ec.btn = buttons ();
		if (g_paused) { root.invalidate (true); show_frame (); kapi_msleep (20); continue; }	// (no frame asked: it waits)
		if (g_sound && g_audio == 0) { g_audio = ak_out_open (0, 0) == 1 ? 1 : -1; g_audioOn = g_audio == 1; }
		bool audio = g_sound && g_audio == 1;
		unsigned fps = fps100 ();
		if (fps != lastFps) { lastFps = fps; t0 = kapi_get_ticks (); asked = 0; }	// (the region changed)
		const unsigned perFrame = SOUND_RATE * 100 / fps;		// sound frames per video frame
		if (audio)
		{
			// keep ~3 frames of sound queued: the audio clock paces the game
			freeFrames = (unsigned) ak_out_free ();
			static unsigned cap = 0; if (freeFrames > cap) cap = freeFrames;
			unsigned queued = cap - freeFrames;
			int k = ec_audio_pop (&g_ec, pcm, freeFrames < 4096 ? (int) freeFrames : 4096);
			if (k > 0) { ak_out_write (pcm, k); queued += (unsigned) k; }
			unsigned have = queued + ec_audio_count (&g_ec) + ec_pending (&g_ec) * perFrame;
			while (have < 2400 && ec_pending (&g_ec) < 3) { ec_request (&g_ec, 1); have += perFrame; }
			stQueued = queued;
			t0 = kapi_get_ticks (); asked = 0;
		}
		else
		{
			// the clock: 60.10 / 50.01 frames a second (ticks are 1/100 s)
			unsigned due = (unsigned) ((unsigned long long) (kapi_get_ticks () - t0) * fps / 10000);
			if (due - asked > 6 && due > asked) asked = due - 1;	// far behind: skip, do not race
			while (asked < due && ec_pending (&g_ec) < 3) { ec_request (&g_ec, 1); asked++; }
		}
		ec_pump (&g_ec);						// (no app core: the frames are made here)
		if (ec_take (&g_ec)) { unsigned long long d0 = now_us (); show_frame (); drawUs += now_us () - d0; stShown++; }
		else kapi_msleep (2);
		unsigned long long tn = now_us ();
		if (tn - stT >= 1000000)
		{
			unsigned long long el = tn - stT;
			unsigned doneNow = g_ec.done, stEmu = doneNow - stDone;
			unsigned long long emuNow = g_ec.emuUs, emuUs = emuNow - stEmuUs;
			int n = 0;
			fmt_num (g_statText, &n, (unsigned) ((unsigned long long) stEmu * 10000000ull / el), 1); cat (g_statText, &n, " fps  shown ");
			fmt_num (g_statText, &n, (unsigned) ((unsigned long long) stShown * 10000000ull / el), 1); cat (g_statText, &n, "  emu ");
			fmt_num (g_statText, &n, stEmu ? (unsigned) (emuUs / stEmu / 100) : 0, 1); cat (g_statText, &n, " ms  draw ");
			fmt_num (g_statText, &n, stShown ? (unsigned) (drawUs / stShown / 100) : 0, 1); cat (g_statText, &n, " ms");
			if (audio) { cat (g_statText, &n, "  sound "); fmt_num (g_statText, &n, stQueued * 1000 / SOUND_RATE, 0); cat (g_statText, &n, " ms"); }
			if (ec_on_core (&g_ec)) { cat (g_statText, &n, "  core "); fmt_num (g_statText, &n, (unsigned) g_ec.core, 0); }
			stT = tn; stDone = doneNow; stEmuUs = emuNow; drawUs = 0; stShown = 0;
		}
		if (kapi_get_ticks () - lastSave > 500) { ec_hold (&g_ec); save_ram (); ec_resume (&g_ec); lastSave = kapi_get_ticks (); }	// every 5 s
	}
	ec_shutdown (&g_ec);
	save_ram ();
	if (g_fs) uk_win_fullscreen_end ();
	return 0;
}
