//
// n3dsemu -- the Onyx Nintendo 3DS emulator (the core: user/Emulators/n3ds; its window: as ndsemu's).
//
//   n3dsemu <game.3ds | .cci | .cxi | .3dsx> [--fullscreen] [--soft]   (without a game: opens the Game Library)
//   * The two screens one above the other, the bottom one centred (View > Side by Side: next to each other),
//     zoomed 1x / 2x; F11 or View > Full Screen: the whole display, the proportions kept.
//   * The touch screen: the mouse's left button on the bottom screen (or a touch screen's finger).
//   * Keys: the keyboard is pad 0 (gamepad.h's [keyboard], the Gamepad applet: by default the arrows, X = A,
//     Z = B, S = X, A = Y, Q / W = L / R, Enter = Start, Backspace = Select); a USB gamepad too, the buttons by
//     their place as on Nintendo's pads. The arrows (a pad's cross) move the Circle Pad -- what most games walk
//     with -- or are the + Control Pad (Controls > Arrows Are the + Control Pad); a gamepad's left stick is
//     always the Circle Pad.
//   * A game is a decrypted dump (no key is here); it is read from its file as it plays. Its saves and extra
//     data are <game>.sav beside it: read at the start, written a few seconds after the game wrote and at the end.
//   * The machine runs on this thread (its processor's JIT allocates as it goes); the picture is drawn by the
//     V3D (the fragments: generated shaders) with the free application core's help for the vertices, or by the
//     software renderer on both (--soft, or a frame the GPU cannot do). The right eye's picture is not drawn.
//   * The sound: the DSP's mix (32728 samples a second) brought to the output's rate; Sound > Sound On / Off.
//   * The pace is the clock's, 59.8 frames a second. When the Pi is late the next frames are not drawn (up to three
//     in a row: the game and its sound keep their speed, the picture's rate drops) -- View > Draw Every Frame: never.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include <sys/time.h>
#include "n3ds/onyxhost.h"		// (AppKit, the core, its host on Onyx)
#include "audiokit/audiokit.h"
#include "gamepad.h"
#include "uikit/uikit.h"
#include "uikit/lang.h"
#include "fontkit/uikitface.h"		// FreeType's text (DejaVu Sans) for every widget

using namespace uikit;

static n3ds::Machine *g_m = 0;
static char g_rom_path[256], g_sav_path[260];
static int g_zoom = 1, g_zmax = 2;				// (the largest zoom the screen holds: the window is made at it)
static bool g_side = false, g_paused = false, g_stats = false;
static bool g_everyFrame = false;				// never leave a frame's picture out
static bool g_arrowsPad = false;				// the arrows are the + Control Pad (else the Circle Pad)
static unsigned *g_fs = 0; static int g_fsw, g_fsh;		// full screen back buffer
static Root *g_root = 0;
static char g_statText[160] = "";
static bool g_sound = true; static int g_audio = 0;		// (0 not tried, 1 ours, -1 none)
static int g_stride;						// the window canvas's row pitch (its largest width)
static bool g_loading = true;
static char g_loadName[64];
static volatile int g_touchDown = 0, g_touchX = 0, g_touchY = 0;
static unsigned g_top[n3ds::TOP_W * n3ds::SCREEN_H], g_bottom[n3ds::BOTTOM_W * n3ds::SCREEN_H];

enum { TW = n3ds::TOP_W, BW = n3ds::BOTTOM_W, SH = n3ds::SCREEN_H };

static unsigned long long now_us (void) { struct timeval tv; gettimeofday (&tv, 0); return (unsigned long long) tv.tv_sec * 1000000ull + (unsigned long long) tv.tv_usec; }
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

// ---- the save: what the game wrote (its save data, its extra data), one block beside the game ----------------
static void save_storage (void)
{
	if (!g_m || !g_m->storageDirty) return;
	const n3ds::u32 need = g_m->storageExport (0, 0);
	if (!need) { g_m->storageDirty = false; return; }
	unsigned char *b = (unsigned char *) malloc (need);
	if (!b) return;
	if (g_m->storageExport (b, need) == need && kapi_save_file (g_sav_path, (const char *) b, need) >= 0) g_m->storageDirty = false;
	free (b);
}
static void load_storage (void)
{
	void *f = kapi_open (g_sav_path);
	if (!f) return;
	const unsigned n = kapi_fsize (f);
	unsigned char *b = n && n < (64u << 20) ? (unsigned char *) malloc (n) : 0;
	const int r = b ? kapi_read (f, b, n) : 0;
	kapi_close (f);
	if (r > 0) g_m->storageImport (b, (n3ds::u32) r);
	free (b);
}

// ---- the layout ------------------------------------------------------------------------------------------
// the two screens' places in a w x h area at a scale (s / 256 of a 3DS pixel): top-left corners
static void screens_at (int w, int h, int &s, int &tx, int &ty, int &bx, int &by)
{
	const int lw = g_side ? TW + BW : TW, lh = g_side ? SH : 2 * SH;
	s = w * 256 / lw; if (h * 256 / lh < s) s = h * 256 / lh;
	const int ow = lw * s / 256, oh = lh * s / 256;
	const int ox = (w - ow) / 2, oy = (h - oh) / 2;
	tx = ox; ty = oy;
	if (g_side) { bx = ox + TW * s / 256; by = oy; }
	else { bx = ox + (TW - BW) / 2 * s / 256; by = oy + SH * s / 256; }
}

// a screen (sw x SH) scaled into dst (pitch), at (x, y), scale s / 256
static void blit (unsigned *dst, int pitch, int dw, int dh, int x, int y, int s, const unsigned *src, int sw)
{
	const int ow = sw * s / 256, oh = SH * s / 256;
	static int xmap[4096];
	for (int i = 0; i < ow && i < 4096; i++) xmap[i] = i * 256 / s;
	for (int j = 0; j < oh; j++)
	{
		const int yy = y + j; if (yy < 0 || yy >= dh) continue;
		const int sy = j * 256 / s;
		unsigned *d = dst + (long) yy * pitch + x;
		if (j > 0 && sy == (j - 1) * 256 / s && yy > 0)	// the same source row: copy the line above
		{
			const unsigned *u = d - pitch;
			for (int i = 0; i < ow && x + i < dw; i++) d[i] = u[i];
			continue;
		}
		const unsigned *r = src + sy * sw;
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
			return;
		}
		int s, tx, ty, bx, by;
		screens_at (width, height, s, tx, ty, bx, by);
		canvas.clear (0);							// (around the screens: the bottom one is narrower)
		blit (canvas.px, canvas.stride, width, height, tx, ty, s, g_top, TW);
		blit (canvas.px, canvas.stride, width, height, bx, by, s, g_bottom, BW);
		if (g_paused) canvas.text (8, 8, TR ("Paused"), 0xFFFFFF);
		if (g_stats) { canvas.fillRect (0, height - 20, width, 20, 0); canvas.text (4, height - 18, g_statText, 0x00FFFF60); }
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) br; (void) bm; (void) wheel;
		if (g_loading) return false;
		int s, tx, ty, bx, by;
		screens_at (width, height, s, tx, ty, bx, by);
		const int x = (mx - bx) * 256 / s, y = (my - by) * 256 / s;
		if (bl && x >= 0 && y >= 0 && x < BW && y < SH) { g_touchX = x; g_touchY = y; g_touchDown = 1; }
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
	blit (g_fs, g_fsw, g_fsw, g_fsh, tx, ty, s, g_top, TW);
	blit (g_fs, g_fsw, g_fsw, g_fsh, bx, by, s, g_bottom, BW);
	if (g_stats)
	{
		Canvas c; c.adopt (g_fs, g_fsw, g_fsh);
		c.fillRect (0, g_fsh - 20, g_fsw, 20, 0); c.text (4, g_fsh - 18, g_statText, 0x00FFFF60);
	}
}

static void set_size (void)
{
	if (g_zoom > g_zmax) g_zoom = g_zmax;
	const int w = (g_side ? TW + BW : TW) * g_zoom, h = (g_side ? SH : 2 * SH) * g_zoom;
	g_root->canvas.adopt (uk_win_resize (w, h), w, h, g_stride);
	uikit::uk_decorate_window ();
	g_root->width = w; g_root->height = h;
	g_root->canvas.clear (0);
	g_root->invalidate (true);
}
static void on_zoom1 () { g_zoom = 1; set_size (); }
static void on_zoom2 () { g_zoom = 2; set_size (); }
static void on_side () { g_side = !g_side; set_size (); if (g_fs) { for (long i = 0; i < (long) g_fsw * g_fsh; i++) g_fs[i] = 0; } }
static void on_full () { full_screen (!g_fs); }
static void on_pause () { g_paused = !g_paused; g_root->invalidate (true); }
static void on_stats () { g_stats = !g_stats; g_root->invalidate (true); }
static void on_arrows () { g_arrowsPad = !g_arrowsPad; }
static void on_every () { g_everyFrame = !g_everyFrame; }
static void on_sound ()
{
	g_sound = !g_sound;
	if (!g_sound && g_audio == 1) { ak_out_close (); g_audio = 0; }
}

// The sound the machine mixed since the last frame, at the output's rate (a straight line between two samples),
// given to the output -- what it has no room for is dropped (the picture's pace leads).
static void play_sound (void)
{
	static short in[2048 * 2], out[4096 * 2];
	static int last[2]; static unsigned frac;
	int k;
	while ((k = g_m->audioRead (in, 2048)) > 0)
	{
		if (!g_sound) continue;
		if (g_audio == 0) g_audio = ak_out_open (0, 0) == 1 ? 1 : -1;
		if (g_audio != 1) continue;
		const unsigned step = (unsigned) ((unsigned long long) n3ds::Machine::AUDIO_RATE * 65536 / SOUND_RATE);
		int n = 0;
		for (int i = 0; i < k; )
		{
			if (n >= 4096) break;
			const int f = (int) (frac >> 4);
			out[n * 2] = (short) ((last[0] * (4096 - f) + in[i * 2] * f) >> 12);
			out[n * 2 + 1] = (short) ((last[1] * (4096 - f) + in[i * 2 + 1] * f) >> 12);
			n++;
			frac += step;
			while (frac >= 65536 && i < k) { frac -= 65536; last[0] = in[i * 2]; last[1] = in[i * 2 + 1]; i++; }
		}
		const int room = ak_out_free ();
		if (n > room) n = room;
		if (n > 0) ak_out_write (out, n);
	}
}
static void on_quit () { if (g_m) g_m->gpuSync (); save_storage (); n3ds_onyx_shutdown (); kapi_exit (0); }

bool EmuRoot::onKey (long k)
{
	if (g_loading) return Root::onKey (k);
	if (k == KEY_F1 + 10) { on_full (); return true; }			// F11
	if (k == 27 && g_fs) { full_screen (false); return true; }
	if (k == 'p' || k == 'P') { on_pause (); return true; }
	if (k == KEY_F1 + 11) { on_stats (); return true; }		// F12
	return Root::onKey (k);
}

// what the player holds: the buttons, the Circle Pad (-156..156 each way)
static void input (n3ds::u32 &buttons, int &cx, int &cy)
{
	n3ds::u32 b = 0;
	const unsigned p = pad_buttons (-1);
	if (p & PAD_B) b |= n3ds::BTN_A;					// (by place: the right one is A)
	if (p & PAD_A) b |= n3ds::BTN_B;
	if (p & PAD_Y) b |= n3ds::BTN_X;
	if (p & PAD_X) b |= n3ds::BTN_Y;
	if (p & PAD_START) b |= n3ds::BTN_START;
	if (p & PAD_SELECT) b |= n3ds::BTN_SELECT;
	if (p & (PAD_L | PAD_L2)) b |= n3ds::BTN_L;
	if (p & (PAD_R | PAD_R2)) b |= n3ds::BTN_R;
	int dx = ((p & PAD_RIGHT) ? 1 : 0) - ((p & PAD_LEFT) ? 1 : 0), dy = ((p & PAD_UP) ? 1 : 0) - ((p & PAD_DOWN) ? 1 : 0);
	cx = 0; cy = 0;
	if (g_arrowsPad)
	{
		if (dx > 0) b |= n3ds::BTN_RIGHT; else if (dx < 0) b |= n3ds::BTN_LEFT;
		if (dy > 0) b |= n3ds::BTN_UP; else if (dy < 0) b |= n3ds::BTN_DOWN;
	}
	else { cx = dx * 156; cy = dy * 156; }
	// a gamepad's left stick: the Circle Pad (a dead zone at the centre)
	for (int i = 0; i < 4; i++)
	{
		struct pad_input in;
		if (!pad_read (i, &in) || !in.connected) continue;
		if (in.lx > 150 || in.lx < -150 || in.ly > 150 || in.ly < -150) { cx = in.lx * 156 / 1000; cy = -in.ly * 156 / 1000; }
	}
	if (cx > 40) b |= n3ds::BTN_CPAD_RIGHT; else if (cx < -40) b |= n3ds::BTN_CPAD_LEFT;
	if (cy > 40) b |= n3ds::BTN_CPAD_UP; else if (cy < -40) b |= n3ds::BTN_CPAD_DOWN;
	buttons = b;
}

static void show_frame (void)
{
	if (g_fs) { blit_full (); kapi_present_fb (); }
	else { g_root->invalidate (true); g_root->draw (); uk_win_present (); }
}

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
			if (args[i + 2] == 's' && args[i + 3] == 'o') g_useGpu = 0;
		}
	{ int b = slen (g_rom_path); while (b > 0 && g_rom_path[b - 1] != '/' && g_rom_path[b - 1] != ':') b--; scpy (g_loadName, g_rom_path + b, sizeof g_loadName); }

	// the window at its largest (its buffer keeps that pitch), then shown at the size chosen
	int scw = 0, sch = 0;
	kapi_screen_size (&scw, &sch);
	g_zmax = 2;
	while (g_zmax > 1 && ((TW + BW) * g_zmax > scw || 2 * SH * g_zmax > sch - 80)) g_zmax--;
	if (g_zmax >= 2 && TW * 2 <= scw && 2 * SH * 2 <= sch - 80) g_zoom = 2;
	g_stride = (TW + BW) * g_zmax;
	EmuRoot root ((TW + BW) * g_zmax, 2 * SH * g_zmax, "Nintendo 3DS");
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	set_size ();
	root.attach ();
	root.invalidate (true); root.draw (); uk_win_present ();

	// the machine, the game
	g_m = new n3ds::Machine;
	if (!g_m || !g_m->init ()) { uk_messagebox (TR ("Nintendo 3DS"), TR ("Not enough memory for the console."), MB_OK); return 1; }
	{
		void *ff = kapi_open ("SD:/apps/n3dsemu.app/sysfont.bcfnt");	// (the shared system font: ours, made from DejaVu Sans)
		if (ff)
		{
			const unsigned fn = kapi_fsize (ff);
			unsigned char *fb = fn && fn < (8u << 20) ? (unsigned char *) malloc (fn) : 0;
			if (fb && kapi_read (ff, fb, fn) == (int) fn) g_m->setSharedFont (fb, fn);
			kapi_close (ff);						// (the machine has its copy)
			free (fb);
		}
	}
	n3ds_onyx_setup (g_m);
	static n3ds::Source src;
	if (!kfileOpen (g_rom_path, &src)) { uk_messagebox (TR ("Nintendo 3DS"), TR ("This file cannot be opened."), MB_OK); return 1; }
	scpy (g_sav_path, g_rom_path, sizeof g_sav_path);
	{
		int e = slen (g_sav_path), dd = e; while (dd > 0 && g_sav_path[dd - 1] != '.' && g_sav_path[dd - 1] != '/') dd--;
		if (dd > 0 && g_sav_path[dd - 1] == '.') e = dd - 1;
		scpy (g_sav_path + e, ".sav", (int) sizeof g_sav_path - e);
	}
	load_storage ();
	if (!g_m->loadFrom (src))
	{
		g_loading = false;
		uk_messagebox (TR ("Nintendo 3DS"), TR ("This is not a Nintendo 3DS game this emulator can run: it needs a decrypted .3ds / .cci / .cxi dump, or a .3dsx program."), MB_OK);
		n3ds_onyx_shutdown ();
		return 1;
	}

	static Menu menu;
	menu.menu (TR ("Game"));
	menu.item (TR ("Pause"),        "P",   0, on_pause);
	menu.separator ();
	menu.item (TR ("Quit"),         "^Q",  UK_CTRL ('Q'), on_quit);
	menu.menu (TR ("View"));
	menu.item (TR ("Full Screen"),  "F11", 0, on_full);
	menu.item (TR ("Side by Side"), "",    0, on_side);
	menu.separator ();
	menu.item (TR ("Zoom 1x"),      "",    0, on_zoom1);
	if (g_zmax >= 2) menu.item (TR ("Zoom 2x"), "", 0, on_zoom2);
	menu.separator ();
	menu.item (TR ("Show Speed"),   "F12", 0, on_stats);
	menu.item (TR ("Draw Every Frame"), "", 0, on_every);
	menu.menu (TR ("Controls"));
	menu.item (TR ("Arrows Are the + Control Pad"), "", 0, on_arrows);
	menu.menu (TR ("Sound"));
	menu.item (TR ("Sound On / Off"), "", 0, on_sound);
	menu.publish ();
	if (wantFull) full_screen (true);

	g_loading = false;
	const unsigned long long frameUs = 16715;			// 59.83 frames a second
	unsigned long long next = now_us (), stT = next, emuUs = 0, drawUs = 0, lastSave = next;
	unsigned stFrames = 0, stDrawn = 0, skipped = 0;
	unsigned long long gpuBefore = 0, softBefore = 0;
	while (!should_exit () && !g_m->exited)
	{
		pump_events ();
		if (g_paused) { show_frame (); kapi_msleep (20); next = now_us (); continue; }
		unsigned long long t = now_us ();
		if (t < next) { kapi_msleep (1); continue; }			// (ahead of the console's pace)
		// late by a frame or more: this one's picture is left out (not more than three in a row); too late to catch
		// up at all: the clock starts again from here
		const bool skip = !g_everyFrame && t - next > frameUs && skipped < 3;
		if (t - next > 8 * frameUs) next = t;
		next += frameUs;
		n3ds::u32 b; int cx, cy;
		input (b, cx, cy);
		g_m->setInput (b, cx, cy, g_touchDown != 0, g_touchX, g_touchY);
		g_m->skipDraw = skip;
		g_m->runFrame ();
		const unsigned long long t1 = now_us ();
		play_sound ();
		stFrames++; emuUs += t1 - t;
		if (skip) skipped++;
		else
		{
			skipped = 0;
			g_m->gpuSync ();
			g_m->screenImage (n3ds::SCREEN_TOP, g_top);
			g_m->screenImage (n3ds::SCREEN_BOTTOM, g_bottom);
			show_frame ();
			stDrawn++;
		}
		const unsigned long long t2 = now_us ();
		drawUs += t2 - t1;
		if (t2 - stT >= 1000000)
		{
			const unsigned long long el = t2 - stT;
			int k = 0;
			fmt_num (g_statText, &k, (unsigned) ((unsigned long long) stFrames * 10000000ull / el), 1); cat (g_statText, &k, TR (" fps  shown "));
			fmt_num (g_statText, &k, (unsigned) ((unsigned long long) stDrawn * 10000000ull / el), 1); cat (g_statText, &k, TR ("  emu "));
			fmt_num (g_statText, &k, stFrames ? (unsigned) (emuUs / stFrames / 100) : 0, 1); cat (g_statText, &k, TR (" ms  draw "));
			fmt_num (g_statText, &k, stDrawn ? (unsigned) (drawUs / stDrawn / 100) : 0, 1); cat (g_statText, &k, " ms");
			const unsigned long long gf = g_m->gsp.gpuFrames - gpuBefore, sf = g_m->gsp.softFrames - softBefore;
			cat (g_statText, &k, gf && !sf ? TR ("  GPU") : gf ? TR ("  GPU + software") : TR ("  software"));
			if (g_m->helpers) cat (g_statText, &k, TR ("  + a core"));
			gpuBefore = g_m->gsp.gpuFrames; softBefore = g_m->gsp.softFrames;
			stT = t2; emuUs = 0; drawUs = 0; stFrames = 0; stDrawn = 0;
		}
		if (g_m->storageDirty && t2 - lastSave > 3000000) { save_storage (); lastSave = t2; }
	}
	g_m->gpuSync ();
	save_storage ();
	n3ds_onyx_shutdown ();
	if (g_audio == 1) ak_out_close ();
	if (g_fs) uk_win_fullscreen_end ();
	return 0;
}
