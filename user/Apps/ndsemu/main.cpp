//
// ndsemu -- the Onyx Nintendo DS emulator (the core: user/Emulators/nds).
//
//   ndsemu <rom.nds> [--fullscreen] [--interp]   (without a ROM: opens the Game Library)
//   * The two screens one above the other (View > Side by Side: next to each other; View > Swap
//     Screens), zoomed 1x / 2x / 3x; F11 or View > Full Screen: the whole display, the proportions kept.
//   * The touch screen: the mouse's left button on the bottom screen (or a touch screen's finger).
//   * Keys: the keyboard is pad 0 (gamepad.h's [keyboard], the Gamepad applet: by default the arrows,
//     X = A, Z = B, S = X, A = Y, Q / W = L / R, Enter = Start, Backspace = Select); a USB gamepad too,
//     the buttons by their place as on Nintendo's pads (the right one A, the bottom one B, the top one
//     X, the left one Y); F9 closes / opens the lid.
//   * The save (EEPROM, Flash or FRAM, found as the game uses it) is <rom>.sav beside the ROM: read
//     at the start, written a few seconds after the game saved and when the emulator closes.
//   * The machine runs on an app core (user/Emulators/emucore.h), the 3D on another when one is free
//     (else at the VBlank, here); the processors through the JIT (kapi_code_alloc), or the
//     interpreter (Game > Interpreter, --interp).
//   * The clock is Onyx's; the firmware's user is "Onyx", in the system's language. A ROM whose secure
//     area is still encrypted needs the ARM7 BIOS's key: SD:/apps/ndsemu.app/bios7.bin (the user's own).
//   * The pace: the sound output (59.83 frames a second), or the clock when there is no sound.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include "audiokit/audiokit.h"
#include "appkit/appkit.h"
#include "gamepad.h"
#include "uikit/uikit.h"
#include "uikit/lang.h"
#include "fontkit/uikitface.h"		// FreeType's text (DejaVu Sans) for every widget
#include "nds/nds.h"
#include "emucore.h"

using namespace uikit;

static nds::Machine *g_m = 0;
static unsigned char *g_rom = 0;
static char g_rom_path[256], g_sav_path[260];
static int g_zoom = 2;
static bool g_side = false, g_swap = false;
static bool g_sound = true, g_paused = false;
static unsigned *g_fs = 0; static int g_fsw, g_fsh;		// full screen back buffer
static Root *g_root = 0;
static int g_audio = 0;						// 0 not tried, 1 ours, -1 none
static bool g_stats = false;
static char g_statText[128] = "";
static EmuCore g_ec;
static int g_stride;						// the window canvas's row pitch (its largest width)
static bool g_loading = true;
static unsigned g_loadDone = 0, g_loadSize = 1;
static char g_loadName[64];
static volatile bool g_audioOn = false;
static volatile int g_touchDown = 0, g_touchX = 0, g_touchY = 0;
static volatile int g_lid = 0;
static bool g_wantJit = true;
static int g_r3dCore = -1;
static volatile unsigned g_r3dReq = 0, g_r3dStop = 0;

enum { SW = nds::W, SH = nds::H };

static inline unsigned long long now_us (void) { return ec_now_us (); }
static int slen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scpy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static void cat (char *d, int *n, const char *s) { while (*s) d[(*n)++] = *s++; d[*n] = 0; }
static void fmt_num (char *d, int *n, unsigned v, int decimals)	// v in 1/10^decimals
{
	char t[16]; int k = 0;
	unsigned div = 1;
	for (int i = 0; i < decimals; i++) div *= 10;
	unsigned ip = v / div, fp = v % div;
	do { t[k++] = (char) ('0' + ip % 10); ip /= 10; } while (ip);
	while (k) d[(*n)++] = t[--k];
	if (decimals) { d[(*n)++] = '.'; for (unsigned mm = div / 10; mm; mm /= 10) d[(*n)++] = (char) ('0' + (fp / mm) % 10); }
	d[*n] = 0;
}

// ---- the save ---------------------------------------------------------------------------------------
static void save_ram (void)
{
	if (!g_m || !g_m->saveDirty || !g_m->save || !g_m->saveSize) return;
	if (kapi_save_file (g_sav_path, (const char *) g_m->save, g_m->saveSize) >= 0) g_m->saveDirty = false;
}
static void load_ram (void)
{
	void *f = kapi_open (g_sav_path);
	if (!f) return;
	unsigned n = kapi_fsize (f);
	if (n > nds::Cart::SAVE_MAX) n = nds::Cart::SAVE_MAX;
	unsigned char *b = new unsigned char[n + 1];
	int r = kapi_read (f, b, n); kapi_close (f);
	if (r > 0) g_m->setSaveData (b, (unsigned) r);
	delete [] b;
}

// ---- the 3D on another core --------------------------------------------------------------------------
static void r3d_core (void *)
{
	unsigned seen = 0;
	while (!g_r3dStop)
	{
		if (g_r3dReq != seen) { seen = g_r3dReq; ec_fence (); g_m->render3dNow (); ec_sev (); }
		else ec_wfe ();
	}
}
static void r3d_kick (void *)
{
	while (g_m->r3d.linesDone < SH) ec_wfe ();		// (the last frame drawn first: its polygons are reused)
	g_m->r3d.linesDone = 0;
	ec_fence ();
	g_r3dReq++;
	ec_sev ();
}
static void r3d_wait (void *, int line)
{
	while (g_m->r3d.linesDone <= line) ec_wfe ();
	ec_fence ();
}

// ---- the layout ------------------------------------------------------------------------------------------
// the two screens' places in a w x h area at a scale (s / 256 of a DS pixel): top-left corners
static void screens_at (int w, int h, int &s, int &tx, int &ty, int &bx, int &by)
{
	int lw = g_side ? 2 * SW : SW, lh = g_side ? SH : 2 * SH;
	s = w * 256 / lw; if (h * 256 / lh < s) s = h * 256 / lh;
	int ow = lw * s / 256, oh = lh * s / 256;
	int ox = (w - ow) / 2, oy = (h - oh) / 2;
	int ax = ox, ay = oy, cx = g_side ? ox + SW * s / 256 : ox, cy = g_side ? oy : oy + SH * s / 256;
	if (g_swap) { tx = cx; ty = cy; bx = ax; by = ay; }
	else { tx = ax; ty = ay; bx = cx; by = cy; }
}

// a screen scaled into dst (pitch), at (x, y), scale s / 256
static void blit (unsigned *dst, int pitch, int dw, int dh, int x, int y, int s, const unsigned *src)
{
	int ow = SW * s / 256, oh = SH * s / 256;
	static int xmap[4096];
	for (int i = 0; i < ow && i < 4096; i++) xmap[i] = i * 256 / s;
	for (int j = 0; j < oh; j++)
	{
		int yy = y + j; if (yy < 0 || yy >= dh) continue;
		int sy = j * 256 / s;
		unsigned *d = dst + (long) yy * pitch + x;
		if (j > 0 && sy == (j - 1) * 256 / s && yy > 0)	// the same source row: copy the line above
		{
			const unsigned *u = d - pitch;
			for (int i = 0; i < ow && x + i < dw; i++) d[i] = u[i];
			continue;
		}
		const unsigned *r = src + sy * SW;
		for (int i = 0; i < ow && x + i < dw && i < 4096; i++) d[i] = r[xmap[i]];
	}
}

class EmuRoot : public Root
{
public:
	EmuRoot (int w, int h, const char *t) : Root (w, h, t) {}
	void onDraw () override
	{
		if (g_loading)
		{
			canvas.clear (0);
			canvas.text (8, height / 2 - 24, TR ("Loading..."), 0xFFFFFF);
			canvas.text (8, height / 2 - 6, g_loadName, 0xA0A0A0);
			int bw = width - 16;
			canvas.fillRect (8, height / 2 + 14, bw, 6, 0x404040);
			canvas.fillRect (8, height / 2 + 14, (int) ((long long) bw * g_loadDone / g_loadSize), 6, 0x40C040);
			return;
		}
		int s, tx, ty, bx, by;
		screens_at (width, height, s, tx, ty, bx, by);
		const unsigned *f = ec_front (&g_ec);
		blit (canvas.px, canvas.stride, width, height, tx, ty, s, f);
		blit (canvas.px, canvas.stride, width, height, bx, by, s, f + SW * SH);
		if (g_paused) canvas.text (8, 8, TR ("Paused"), 0xFFFFFF);
		if (g_lid) canvas.text (8, 24, TR ("The lid is closed (F9)"), 0xFFFFFF);
		if (g_stats) { canvas.fillRect (0, height - 20, width, 20, 0); canvas.text (4, height - 18, g_statText, 0x00FFFF60); }
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) br; (void) bm; (void) wheel;
		if (g_loading) return false;
		int s, tx, ty, bx, by;
		screens_at (width, height, s, tx, ty, bx, by);
		int x = (mx - bx) * 256 / s, y = (my - by) * 256 / s;
		if (bl && x >= 0 && y >= 0 && x < SW && y < SH) { g_touchX = x; g_touchY = y; g_touchDown = 1; }
		else if (!bl) g_touchDown = 0;
		return true;
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
static void blit_full (void)
{
	int s, tx, ty, bx, by;
	screens_at (g_fsw, g_fsh, s, tx, ty, bx, by);
	const unsigned *f = ec_front (&g_ec);
	blit (g_fs, g_fsw, g_fsw, g_fsh, tx, ty, s, f);
	blit (g_fs, g_fsw, g_fsw, g_fsh, bx, by, s, f + SW * SH);
	if (g_stats)
	{
		Canvas c; c.adopt (g_fs, g_fsw, g_fsh);
		c.fillRect (0, g_fsh - 20, g_fsw, 20, 0); c.text (4, g_fsh - 18, g_statText, 0x00FFFF60);
	}
}

static void set_size (void)
{
	int w = (g_side ? 2 * SW : SW) * g_zoom, h = (g_side ? SH : 2 * SH) * g_zoom;
	g_root->canvas.adopt (uk_win_resize (w, h), w, h, g_stride);
	uikit::uk_decorate_window ();
	g_root->width = w; g_root->height = h;
	g_root->canvas.clear (0);
	g_root->invalidate (true);
}
static void on_zoom1 () { g_zoom = 1; set_size (); }
static void on_zoom2 () { g_zoom = 2; set_size (); }
static void on_zoom3 () { g_zoom = 3; set_size (); }
static void on_side () { g_side = !g_side; set_size (); if (g_fs) { for (long i = 0; i < (long) g_fsw * g_fsh; i++) g_fs[i] = 0; } }
static void on_swap () { g_swap = !g_swap; g_root->invalidate (true); }
static void on_full () { full_screen (!g_fs); }
static void on_sound ()
{
	g_sound = !g_sound;
	if (!g_sound && g_audio == 1) { g_audioOn = false; ak_out_close (); g_audio = 0; }
}
static void on_pause () { g_paused = !g_paused; g_root->invalidate (true); }
static void on_stats () { g_stats = !g_stats; g_root->invalidate (true); }
static void on_lid () { g_lid = !g_lid; g_root->invalidate (true); }
static void on_reset () { ec_hold (&g_ec); save_ram (); g_m->reset (); ec_resume (&g_ec); }
static void on_interp ()
{
	ec_hold (&g_ec);
	g_wantJit = !g_wantJit;
	g_m->useJit = g_wantJit && g_m->jit;
	ec_resume (&g_ec);
}
static void on_quit () { kapi_exit (0); }

bool EmuRoot::onKey (long k)
{
	if (g_loading) return Root::onKey (k);
	if (k == KEY_F1 + 10) { on_full (); return true; }			// F11
	if (k == 27 && g_fs) { full_screen (false); return true; }
	if (k == 'p' || k == 'P') { on_pause (); return true; }
	if (k == KEY_F1 + 11) { on_stats (); return true; }		// F12
	if (k == KEY_F1 + 8) { on_lid (); return true; }			// F9
	return Root::onKey (k);
}

static int buttons (void)
{
	int b = 0;
	unsigned p = pad_buttons (-1);
	if (p & PAD_RIGHT) b |= nds::BTN_RIGHT;
	if (p & PAD_LEFT) b |= nds::BTN_LEFT;
	if (p & PAD_UP) b |= nds::BTN_UP;
	if (p & PAD_DOWN) b |= nds::BTN_DOWN;
	if (p & PAD_B) b |= nds::BTN_A;					// (by place: the right one is A)
	if (p & PAD_A) b |= nds::BTN_B;
	if (p & PAD_Y) b |= nds::BTN_X;
	if (p & PAD_X) b |= nds::BTN_Y;
	if (p & PAD_START) b |= nds::BTN_START;
	if (p & PAD_SELECT) b |= nds::BTN_SELECT;
	if (p & (PAD_L | PAD_L2)) b |= nds::BTN_L;
	if (p & (PAD_R | PAD_R2)) b |= nds::BTN_R;
	if ((b & nds::BTN_LEFT) && (b & nds::BTN_RIGHT)) b &= ~(nds::BTN_LEFT | nds::BTN_RIGHT);
	if ((b & nds::BTN_UP) && (b & nds::BTN_DOWN)) b &= ~(nds::BTN_UP | nds::BTN_DOWN);
	if (g_lid) b |= nds::BTN_LID;
	return b;
}

// One frame of the machine: on the app core (no kapi call, no allocation here).
static void nds_frame (EmuCore *ec)
{
	static short pcm[4096 * 2];
	g_m->setButtons (ec->btn);
	g_m->setTouch (g_touchDown != 0, g_touchX, g_touchY);
	g_m->runFrame ();
	unsigned *d = ec_back (ec);
	for (int i = 0; i < SW * SH; i++) { d[i] = g_m->screen[0][i]; d[SW * SH + i] = g_m->screen[1][i]; }
	ec_publish (ec);
	int k = g_m->audioRead (pcm, 4096);
	if (k > 0 && g_audioOn) ec_audio_push (ec, pcm, k);
}

static void show_frame (void)
{
	if (g_fs) { blit_full (); kapi_present_fb (); }
	else { g_root->invalidate (true); g_root->draw (); uk_win_present (); }
}

static void *code_alloc (unsigned n) { return kapi_code_alloc (n); }

int main (void)
{
	pad_keyboard (1);
	ft_uikit_install ("DejaVu Sans", 13);		// (before the widgets; false: the bitmap font)
	uk_lang_init ();
	char args[256] = "";
	kapi_get_args (args, sizeof args);
	int i = 0; while (args[i] == ' ') i++;
	int n = 0;
	bool wantFull = false;
	if (args[i] == '"') { i++; while (args[i] && args[i] != '"' && n < 255) g_rom_path[n++] = args[i++]; if (args[i]) i++; }
	else while (args[i] && n < 255 && !(args[i] == ' ' && args[i + 1] == '-' && args[i + 2] == '-')) g_rom_path[n++] = args[i++];
	while (n > 0 && g_rom_path[n - 1] == ' ') n--;
	g_rom_path[n] = 0;
	if (!g_rom_path[0]) { lx_launch ("gamelib", ""); return 0; }
	for (; args[i]; i++)
		if (args[i] == '-' && args[i + 1] == '-')
		{
			if (args[i + 2] == 'f' && args[i + 3] == 'u') wantFull = true;
			if (args[i + 2] == 'i' && args[i + 3] == 'n') g_wantJit = false;
		}

	void *f = kapi_open (g_rom_path);
	if (!f) return 1;
	unsigned sz = kapi_fsize (f);
	if (sz > 0x20000000) sz = 0x20000000;				// (512 MB: the largest card)
	g_rom = new unsigned char[sz + 4];
	if (!g_rom) { kapi_close (f); uk_messagebox (TR ("Nintendo DS"), TR ("Not enough memory for this game."), MB_OK); return 1; }
	int r = kapi_read (f, g_rom, sz < 0x200 ? sz : 0x200);
	if (r <= 0) { kapi_close (f); return 1; }
	char title[48] = "Nintendo DS"; int tn = 0;
	if (r >= 0x10) { for (; tn < 12; tn++) { char c = (char) g_rom[tn]; if (c < 32 || c >= 127) break; title[tn] = c; } if (tn) title[tn] = 0; }
	{ int b = slen (g_rom_path); while (b > 0 && g_rom_path[b - 1] != '/' && g_rom_path[b - 1] != ':') b--; scpy (g_loadName, g_rom_path + b, sizeof g_loadName); }
	// the window at its largest (its buffer keeps that pitch), then shown at the size chosen
	g_stride = 2 * SW * 3;
	EmuRoot root (2 * SW * 3, 2 * SH * 3, title);
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	set_size ();
	root.attach ();
	g_loadSize = sz ? sz : 1; g_loadDone = (unsigned) r;
	root.invalidate (true); root.draw (); uk_win_present ();
	while ((unsigned) r < sz)
	{
		unsigned k = sz - (unsigned) r > 0x100000 ? 0x100000 : sz - (unsigned) r;
		int got = kapi_read (f, g_rom + r, k);
		if (got <= 0) break;
		r += got; g_loadDone = (unsigned) r;
		pump_events ();
		if (should_exit ()) { kapi_close (f); return 0; }
		root.invalidate (true); root.draw (); uk_win_present ();
	}
	kapi_close (f);
	g_m = new nds::Machine;
	// the user and the clock before the boot (the firmware is made from them)
	const char *lang = uk_lang ();
	int dsLang = 1;
	if (lang[0] == 'f' && lang[1] == 'r') dsLang = 2;
	else if (lang[0] == 'd' && lang[1] == 'e') dsLang = 3;
	else if (lang[0] == 'i' && lang[1] == 't') dsLang = 4;
	else if (lang[0] == 'e' && lang[1] == 's') dsLang = 5;
	else if (lang[0] == 'j' && lang[1] == 'a') dsLang = 0;
	g_m->setUser ("Onyx", dsLang, 1, 1, 4);
	int y, mo, d, h, mi, se;
	if (kapi_get_datetime (&y, &mo, &d, &h, &mi, &se) == 1 && y >= 2000 && y < 2100) g_m->setTime (y, mo, d, h, mi, se);
	{
		void *bf = kapi_open ("SD:/apps/ndsemu.app/bios7.bin");
		if (bf)
		{
			static unsigned char b7[0x4000];
			int k = kapi_read (bf, b7, sizeof b7); kapi_close (bf);
			if (k > 0) g_m->setBios7 (b7, (unsigned) k);
		}
	}
	if (!g_m->load (g_rom, (unsigned) r))
	{
		g_loading = false;
		uk_messagebox (TR ("Nintendo DS"), TR ("This is not a Nintendo DS game (.nds)."), MB_OK);
		return 1;
	}
	if (g_m->lastError[0]) uk_messagebox (TR ("Nintendo DS"), TR ("This game's secure area is encrypted: it needs a decrypted dump, or the ARM7 BIOS in SD:/apps/ndsemu.app/bios7.bin."), MB_OK);
	scpy (g_sav_path, g_rom_path, sizeof g_sav_path);
	int e = slen (g_sav_path), dd = e; while (dd > 0 && g_sav_path[dd - 1] != '.' && g_sav_path[dd - 1] != '/') dd--;
	if (dd > 0 && g_sav_path[dd - 1] == '.') e = dd - 1;
	scpy (g_sav_path + e, ".sav", sizeof g_sav_path - e);
	load_ram ();
	g_m->setAudioRate (SOUND_RATE);
	if (g_m->jitEnable (code_alloc)) g_m->useJit = g_wantJit;	// (the JIT's code memory: taken here, not on the app core)

	static Menu menu;
	menu.menu (TR ("Game"));
	menu.item (TR ("Pause"),        "P",   0, on_pause);
	menu.item (TR ("Reset"),        "",    0, on_reset);
	if (g_m->jit) menu.item (TR ("Interpreter (no JIT)"), "", 0, on_interp);
	menu.item (TR ("Close / Open the Lid"), "F9", 0, on_lid);
	menu.separator ();
	menu.item (TR ("Quit"),         "^Q",  UK_CTRL ('Q'), on_quit);
	menu.menu (TR ("View"));
	menu.item (TR ("Full Screen"),  "F11", 0, on_full);
	menu.item (TR ("Side by Side"), "",    0, on_side);
	menu.item (TR ("Swap Screens"), "",    0, on_swap);
	menu.separator ();
	menu.item (TR ("Zoom 1x"),      "",    0, on_zoom1);
	menu.item (TR ("Zoom 2x"),      "",    0, on_zoom2);
	menu.item (TR ("Zoom 3x"),      "",    0, on_zoom3);
	menu.separator ();
	menu.item (TR ("Show Speed"),   "F12", 0, on_stats);
	menu.menu (TR ("Sound"));
	menu.item (TR ("Sound On / Off"), "", 0, on_sound);
	menu.publish ();
	if (wantFull) full_screen (true);

	static short pcm[4096 * 2];
	if (!ec_init (&g_ec, SW, SH * 2, nds_frame)) return 1;
	if (ec_on_core (&g_ec))						// a second app core: the 3D there
	{
		int c = kapi_core_acquire ();
		unsigned char *stack = c >= 0 ? new unsigned char[256 * 1024] : 0;
		if (c >= 0 && stack && kapi_core_run (c, r3d_core, 0, stack + 256 * 1024) == 0)
		{
			g_r3dCore = c;
			g_m->render3dKick = r3d_kick; g_m->render3dWait = r3d_wait;
		}
		else if (c >= 0) kapi_core_release (c);
	}
	g_loading = false;
	const unsigned perFrame = SOUND_RATE * 10000 / 598261;		// sound frames per video frame
	unsigned t0 = kapi_get_ticks (); unsigned asked = 0;
	unsigned lastSave = kapi_get_ticks ();
	unsigned long long stT = now_us (), drawUs = 0, stEmuUs = 0; unsigned stDone = 0, stShown = 0, stQueued = 0;
	while (!should_exit ())
	{
		pump_events ();
		g_ec.btn = buttons ();
		if (g_paused) { root.invalidate (true); show_frame (); kapi_msleep (20); continue; }
		if (g_sound && g_audio == 0) { g_audio = ak_out_open (0, 0) == 1 ? 1 : -1; g_audioOn = g_audio == 1; }
		bool audio = g_sound && g_audio == 1;
		if (audio)
		{
			unsigned freeFrames = (unsigned) ak_out_free ();
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
			unsigned due = (unsigned) ((unsigned long long) (kapi_get_ticks () - t0) * 5983 / 10000);	// (ticks are 1/100 s)
			if (due - asked > 6 && due > asked) asked = due - 1;
			while (asked < due && ec_pending (&g_ec) < 3) { ec_request (&g_ec, 1); asked++; }
		}
		ec_pump (&g_ec);
		if (ec_take (&g_ec)) { unsigned long long d0 = now_us (); show_frame (); drawUs += now_us () - d0; stShown++; }
		else kapi_msleep (2);
		unsigned long long tn = now_us ();
		if (tn - stT >= 1000000)
		{
			unsigned long long el = tn - stT;
			unsigned doneNow = g_ec.done, stEmu = doneNow - stDone;
			unsigned long long emuNow = g_ec.emuUs, emuUs = emuNow - stEmuUs;
			int k = 0;
			fmt_num (g_statText, &k, (unsigned) ((unsigned long long) stEmu * 10000000ull / el), 1); cat (g_statText, &k, TR (" fps  shown "));
			fmt_num (g_statText, &k, (unsigned) ((unsigned long long) stShown * 10000000ull / el), 1); cat (g_statText, &k, TR ("  emu "));
			fmt_num (g_statText, &k, stEmu ? (unsigned) (emuUs / stEmu / 100) : 0, 1); cat (g_statText, &k, TR (" ms  draw "));
			fmt_num (g_statText, &k, stShown ? (unsigned) (drawUs / stShown / 100) : 0, 1); cat (g_statText, &k, " ms");
			if (audio) { cat (g_statText, &k, TR ("  sound ")); fmt_num (g_statText, &k, stQueued * 1000 / SOUND_RATE, 0); cat (g_statText, &k, " ms"); }
			cat (g_statText, &k, g_m->useJit ? "  JIT" : TR ("  interpreter"));
			if (g_r3dCore >= 0) cat (g_statText, &k, TR ("  3D on a core"));
			stT = tn; stDone = doneNow; stEmuUs = emuNow; drawUs = 0; stShown = 0;
		}
		if (kapi_get_ticks () - lastSave > 300 && g_m->saveDirty) { ec_hold (&g_ec); save_ram (); ec_resume (&g_ec); lastSave = kapi_get_ticks (); }
	}
	ec_shutdown (&g_ec);
	if (g_r3dCore >= 0) { g_r3dStop = 1; ec_sev (); kapi_core_release (g_r3dCore); }
	save_ram ();
	if (g_fs) uk_win_fullscreen_end ();
	return 0;
}
