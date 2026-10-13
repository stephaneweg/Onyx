//
// wipeout_onyx.c -- wipEout on Onyx: the platform layer (platform.h) of wipeout-rewrite
// (third_party/wipeout-rewrite, Dominic Szablewski's rewrite of the 1995 game), with its
// software renderer (render_software.c).
//
// Copyright (c) 2026 the Onyx authors. MIT licence (this file); the game's sources have no
// licence of their own: docs/LICENSING.md.
//
//   wipeout [--fullscreen]
//   * The game's files are NOT part of Onyx: the PlayStation (NTSC) data, a few menu models of
//     the PC version, the music as QOA (wipeout/music/trackNN.qoa, optional) and the intro as
//     MPEG-1 (wipeout/intro.mpeg, optional), in SD:/wipeout (SD:/wipeout/textures/,
//     SD:/wipeout/track01/, ...). The settings and the best times go to SD:/wipeout/save.dat.
//   * The picture: the game draws at 240 lines (the menu's SCREEN RESOLUTION: 240P; 480P and
//     NATIVE -- the window's size, at most 540 lines full screen -- cost more), with the
//     window's or the display's shape; doubled into the 640 x 480 window, stretched to the
//     display full screen (F11, Alt+Enter, Game > Full Screen, or the game's options).
//   * Keys as on the PC: arrows steer, X thrust, Z fire, C / V air brakes, A the view, Enter
//     start / pause, Esc, Backspace back. A USB gamepad (user/Include/gamepad.h): the d-pad or
//     the left stick, A thrust, X fire, L / R air brakes, Y the view, Start; the sticks are
//     also given as analog (the game's options can bind them).
//   * Sound: the game mixes its own (float stereo, 44100 Hz) -- turned into AudioKit's output
//     (ak_out_*) after each frame, a little more than a frame ahead.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include "appkit/appkit.h"
#include "uikit/win.h"			// the window (UIKit's: uk_win_*)
#include "systemkit/systemkit.h"	// notify
#include "audiokit/audiokit.h"		// the sound output: ak_out_*
#include "gamepad.h"

#include "platform.h"
#include "input.h"
#include "system.h"
#include "render.h"
#include "utils.h"

#define WIN_W		640			// the window's client area (the PlayStation's 4:3)
#define WIN_H		480
#define NATIVE_MAX_H	540			// NATIVE full screen: at most this many lines

void onyx_ui_init (void);			// wipeout_uikit.cpp
void onyx_menu (void (*full) (void), void (*quit) (void));
const char *onyx_tr (const char *en);
#define TR(s)	onyx_tr (s)			// (uikit/lang.h's, from C)

static int s_exit = 0;

// ---- time: the ARM generic timer ------------------------------------------------------------------
double platform_now (void)
{
	unsigned long long c, f;
	__asm__ volatile ("mrs %0, cntpct_el0" : "=r" (c));
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (f));
	return f ? (double) c / (double) f : 0;
}

void platform_exit (void) { s_exit = 1; }

// ---- errors: said, then the game ends (utils.h's die) -----------------------------------------------
void onyx_die (const char *where, const char *fmt, ...)
{
	char msg[256];
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (msg, sizeof msg, fmt, ap);
	va_end (ap);
	printf ("wipeout: %s: %s\n", where, msg);
	notify ("wipEout", msg);
	exit (1);
}

// ---- files: the game's under SD:/ (its names begin with "wipeout/"), ours in SD:/wipeout -----------
#define PATH_ASSETS	"SD:/"
#define PATH_USERDATA	"SD:/wipeout/"
static char s_path[320];

static const char *path_of (const char *dir, const char *name)
{
	snprintf (s_path, sizeof s_path, "%s%s", dir, name);
	return s_path;
}

// newlib's stat (utils.c's file_exists): asked of the kernel. A folder may end with '/'.
int _stat (const char *path, struct stat *st)
{
	char p[320];
	snprintf (p, sizeof p, "%s", path);
	size_t n = strlen (p);
	while (n > 4 && p[n - 1] == '/') p[--n] = 0;
	struct kapi_stat ks;
	if (kapi_path_stat (p, &ks) != 0) { errno = ENOENT; return -1; }
	memset (st, 0, sizeof *st);
	st->st_size = (off_t) ks.size;
	st->st_mode = (ks.attr & 0x10) ? S_IFDIR | 0755 : S_IFREG | 0644;
	return 0;
}

bool platform_asset_exists (const char *name)
{
	struct stat st;
	return _stat (path_of (PATH_ASSETS, name), &st) == 0;
}
FILE *platform_open_asset (const char *name, const char *mode) { return fopen (path_of (PATH_ASSETS, name), mode); }
uint8_t *platform_load_asset (const char *name, uint32_t *bytes_read) { return file_load (path_of (PATH_ASSETS, name), bytes_read); }
uint8_t *platform_load_userdata (const char *name, uint32_t *bytes_read)
{
	if (!file_exists (path_of (PATH_USERDATA, name))) { *bytes_read = 0; return NULL; }
	return file_load (s_path, bytes_read);
}
uint32_t platform_store_userdata (const char *name, void *bytes, int32_t len)
{
	kapi_mkdir ("SD:/wipeout");
	return file_store (path_of (PATH_USERDATA, name), bytes, len);
}

// ---- the picture -----------------------------------------------------------------------------------
static unsigned *s_canvas;			// the window's WIN_W x WIN_H client area
static unsigned *s_fs; static int s_fsw, s_fsh;	// full screen: the display's pixels
static int s_wantFull = 0;
static render_resolution_t s_res = RENDER_RES_240P;
static rgba_t *s_buf; static vec2i_t s_size;	// the game's picture (its render size)
static int s_resize = 0;			// the render size is to change before the next frame

static void out_size (int *w, int *h) { *w = s_fs ? s_fsw : WIN_W; *h = s_fs ? s_fsh : WIN_H; }

// The size the game draws at: its lines (240, 480, or the output's), the output's shape.
static vec2i_t render_size_now (void)
{
	int ow, oh; out_size (&ow, &oh);
	int h = s_res == RENDER_RES_240P ? 240 : s_res == RENDER_RES_480P ? 480 : oh;
	if (h > NATIVE_MAX_H && s_res == RENDER_RES_NATIVE) h = NATIVE_MAX_H;
	if (h > oh) h = oh;
	int w = (ow * h + oh / 2) / oh;
	return vec2i (w, h);
}

vec2i_t platform_screen_size (void) { return render_size_now (); }

void platform_set_resolution (render_resolution_t res)
{
	if (res != s_res) { s_res = res; s_resize = 1; }
}

bool platform_get_fullscreen (void) { return s_wantFull != 0; }
void platform_set_fullscreen (bool on) { s_wantFull = on ? 1 : 0; }

static void apply_fullscreen (void)
{
	if (s_wantFull && !s_fs)
	{
		s_fs = uk_win_fullscreen_begin (&s_fsw, &s_fsh);
		if (!s_fs) s_wantFull = 0;
		else memset (s_fs, 0, (size_t) s_fsw * s_fsh * 4);
	}
	else if (!s_wantFull && s_fs) { uk_win_fullscreen_end (); s_fs = 0; }
	else return;
	s_resize = 1;
}

rgba_t *platform_get_screenbuffer (int32_t *pitch)
{
	*pitch = s_size.x * (int32_t) sizeof (rgba_t);
	return s_buf;
}

static void prepare_frame (void)
{
	apply_fullscreen ();
	vec2i_t want = render_size_now ();
	if (!s_buf || s_resize || want.x != s_size.x || want.y != s_size.y)
	{
		s_resize = 0;
		rgba_t *b = realloc (s_buf, (size_t) want.x * want.y * sizeof (rgba_t));
		if (!b) die ("No memory for a %d x %d picture", want.x, want.y);
		s_buf = b; s_size = want;
		system_resize (want);
	}
}

// The picture (R, G, B, A bytes) into the output (0x00RRGGBB), stretched.
static void end_frame (void)
{
	unsigned *dst = s_fs ? s_fs : s_canvas;
	int ow, oh; out_size (&ow, &oh);
	static int xmap[4096];
	int n = ow < 4096 ? ow : 4096;
	for (int x = 0; x < n; x++) xmap[x] = x * s_size.x / ow;
	const unsigned *src = (const unsigned *) s_buf;
	int prev = -1;
	for (int y = 0; y < oh; y++)
	{
		int sy = y * s_size.y / oh;
		unsigned *d = dst + (long) y * ow;
		if (sy == prev) { memcpy (d, d - ow, (size_t) n * 4); continue; }
		prev = sy;
		const unsigned *s = src + (long) sy * s_size.x;
		for (int x = 0; x < n; x++)
		{
			unsigned p = s[xmap[x]];
			d[x] = ((p & 0xff) << 16) | (p & 0xff00) | ((p >> 16) & 0xff);
		}
	}
	if (s_fs) kapi_present_fb ();
	else uk_win_present ();
}

// ---- sound -----------------------------------------------------------------------------------------
static void (*s_mix) (float *buffer, uint32_t len) = 0;
static int s_audio = 0;				// 1 the output is ours, -1 none

void platform_set_audio_mix_cb (void (*cb) (float *buffer, uint32_t len))
{
	s_mix = cb;
	if (!s_audio) s_audio = ak_out_open (0, 0) == 1 ? 1 : -1;
}

// Keep a little more than a frame queued (the last frame's time, 2048 frames at least).
static void sound_service (double frame_s)
{
	if (s_audio != 1 || !s_mix) return;
	enum { MAXF = 8192 };
	static float f[MAXF * 2];
	static short o[MAXF * 2];
	int ahead = (int) (frame_s * 1.5 * SOUND_RATE);
	if (ahead < 2048) ahead = 2048;
	if (ahead > MAXF) ahead = MAXF;
	int want = ahead - ak_out_queued ();
	int room = ak_out_free ();
	if (want > room) want = room;
	if (want <= 0) return;
	s_mix (f, (uint32_t) want * 2);
	for (int i = 0; i < want * 2; i++)
	{
		float v = f[i] * 32767.0f;
		o[i] = (short) (v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : v);
	}
	ak_out_write (o, want);
}

// ---- input -----------------------------------------------------------------------------------------
static float s_state[INPUT_BUTTON_MAX];
static unsigned char s_tap[INPUT_BUTTON_MAX];	// pressed by a key event: released at the next poll

static void set_button (button_t b, float v)
{
	if (s_state[b] == v) return;
	s_state[b] = v;
	input_set_button_state (b, v);
}

static void on_full (void) { s_wantFull = !s_wantFull; }
static void on_quit (void) { s_exit = 1; }

// The keys kapi_key_held does not follow (and the text typed): taps.
static void key_event (unsigned long sender, int event, long value)
{
	(void) sender;
	if (event != GUI_EVENT_KEY) return;
	if (value == KEY_F1 + 10 || (value == KEY_ENTER && (kapi_get_modifiers () & MOD_ALT))) { on_full (); return; }
	if (value >= 32 && value < 127) input_textinput ((int32_t) value);
	button_t b = INPUT_INVALID;
	if (value >= KEY_F1 && value <= KEY_F12) b = (button_t) (INPUT_KEY_F1 + (value - KEY_F1));
	else switch (value)
	{
	case KEY_BACKSPACE: b = INPUT_KEY_BACKSPACE; break;
	case KEY_TAB: b = INPUT_KEY_TAB; break;
	case KEY_HOME: b = INPUT_KEY_HOME; break;
	case KEY_END: b = INPUT_KEY_END; break;
	case KEY_PGUP: b = INPUT_KEY_PAGEUP; break;
	case KEY_PGDN: b = INPUT_KEY_PAGEDOWN; break;
	case KEY_DEL: b = INPUT_KEY_DELETE; break;
	case '-': b = INPUT_KEY_MINUS; break;
	case '=': b = INPUT_KEY_EQUALS; break;
	case '[': b = INPUT_KEY_LEFTBRACKET; break;
	case ']': b = INPUT_KEY_RIGHTBRACKET; break;
	case '\\': b = INPUT_KEY_BACKSLASH; break;
	case ';': b = INPUT_KEY_SEMICOLON; break;
	case '\'': b = INPUT_KEY_APOSTROPHE; break;
	case '`': b = INPUT_KEY_TILDE; break;
	case ',': b = INPUT_KEY_COMMA; break;
	case '.': b = INPUT_KEY_PERIOD; break;
	case '/': b = INPUT_KEY_SLASH; break;
	default: return;					// (followed as held keys)
	}
	set_button (b, 1.0f);
	s_tap[b] = 1;
}

static float axis (int v, int neg)			// -1000..1000 -> one half, 0..1
{
	v = neg ? -v : v;
	return v > 0 ? (v > 1000 ? 1.0f : v / 1000.0f) : 0.0f;
}

static void poll_input (void)
{
	for (int b = 0; b < INPUT_BUTTON_MAX; b++) if (s_tap[b]) { s_tap[b] = 0; set_button ((button_t) b, 0.0f); }
	kapi_pump_events ();
	if (kapi_should_exit ()) s_exit = 1;

	static const struct { int key; button_t b; } HELD[] = {
		{ KEY_UP, INPUT_KEY_UP }, { KEY_DOWN, INPUT_KEY_DOWN }, { KEY_LEFT, INPUT_KEY_LEFT },
		{ KEY_RIGHT, INPUT_KEY_RIGHT }, { KEY_ENTER, INPUT_KEY_RETURN }, { 27, INPUT_KEY_ESCAPE },
		{ ' ', INPUT_KEY_SPACE } };
	for (unsigned i = 0; i < sizeof HELD / sizeof HELD[0]; i++) set_button (HELD[i].b, kapi_key_held (HELD[i].key) ? 1.0f : 0.0f);
	for (int c = 'a'; c <= 'z'; c++) set_button ((button_t) (INPUT_KEY_A + (c - 'a')), kapi_key_held (c) ? 1.0f : 0.0f);
	set_button (INPUT_KEY_0, kapi_key_held ('0') ? 1.0f : 0.0f);
	for (int c = '1'; c <= '9'; c++) set_button ((button_t) (INPUT_KEY_1 + (c - '1')), kapi_key_held (c) ? 1.0f : 0.0f);
	unsigned m = kapi_get_modifiers ();
	set_button (INPUT_KEY_LCTRL, (m & MOD_CTRL) ? 1.0f : 0.0f);
	set_button (INPUT_KEY_LSHIFT, (m & MOD_SHIFT) ? 1.0f : 0.0f);
	set_button (INPUT_KEY_LALT, (m & MOD_ALT) ? 1.0f : 0.0f);

	// the gamepads: every pad's buttons, the first pad's sticks
	unsigned p = 0; int lx = 0, ly = 0, rx = 0, ry = 0, sticks = 0;
	for (int i = 0; i < PAD_MAX; i++)
	{
		struct pad_input in;
		if (!pad_read (i, &in) || !in.connected) continue;
		p |= in.buttons;
		if (!sticks) { lx = in.lx; ly = in.ly; rx = in.rx; ry = in.ry; sticks = 1; }
	}
	static const struct { unsigned bit; button_t b; } PAD[] = {
		{ PAD_A, INPUT_GAMEPAD_A }, { PAD_B, INPUT_GAMEPAD_B }, { PAD_X, INPUT_GAMEPAD_X }, { PAD_Y, INPUT_GAMEPAD_Y },
		{ PAD_L, INPUT_GAMEPAD_L_SHOULDER }, { PAD_R, INPUT_GAMEPAD_R_SHOULDER },
		{ PAD_L2, INPUT_GAMEPAD_L_TRIGGER }, { PAD_R2, INPUT_GAMEPAD_R_TRIGGER },
		{ PAD_SELECT, INPUT_GAMEPAD_SELECT }, { PAD_START, INPUT_GAMEPAD_START },
		{ PAD_L3, INPUT_GAMEPAD_L_STICK_PRESS }, { PAD_R3, INPUT_GAMEPAD_R_STICK_PRESS }, { PAD_HOME, INPUT_GAMEPAD_HOME },
		{ PAD_UP, INPUT_GAMEPAD_DPAD_UP }, { PAD_DOWN, INPUT_GAMEPAD_DPAD_DOWN },
		{ PAD_LEFT, INPUT_GAMEPAD_DPAD_LEFT }, { PAD_RIGHT, INPUT_GAMEPAD_DPAD_RIGHT } };
	for (unsigned i = 0; i < sizeof PAD / sizeof PAD[0]; i++) set_button (PAD[i].b, (p & PAD[i].bit) ? 1.0f : 0.0f);
	set_button (INPUT_GAMEPAD_L_STICK_UP, axis (ly, 1)); set_button (INPUT_GAMEPAD_L_STICK_DOWN, axis (ly, 0));
	set_button (INPUT_GAMEPAD_L_STICK_LEFT, axis (lx, 1)); set_button (INPUT_GAMEPAD_L_STICK_RIGHT, axis (lx, 0));
	set_button (INPUT_GAMEPAD_R_STICK_UP, axis (ry, 1)); set_button (INPUT_GAMEPAD_R_STICK_DOWN, axis (ry, 0));
	set_button (INPUT_GAMEPAD_R_STICK_LEFT, axis (rx, 1)); set_button (INPUT_GAMEPAD_R_STICK_RIGHT, axis (rx, 0));
}

// ---- start -----------------------------------------------------------------------------------------
int main (void)
{
	static char args[256];
	kapi_get_args (args, sizeof args);
	if (strstr (args, "--fullscreen")) s_wantFull = 1;
	kapi_chdir ("SD:/wipeout");
	onyx_ui_init ();
	if (!platform_asset_exists ("wipeout/track01/"))
	{
		notify ("wipEout", TR ("No game data in SD:/wipeout (see SD:/wipeout/README.txt)"));
		return 1;
	}
	s_canvas = uk_win_create (WIN_W, WIN_H, "wipEout");
	if (!s_canvas) { printf ("wipeout: no window\n"); return 1; }
	memset (s_canvas, 0, WIN_W * WIN_H * 4);
	uk_win_on_key (key_event);
	onyx_menu (on_full, on_quit);
	uk_win_present ();

	int full = s_wantFull;
	prepare_frame ();
	system_init ();					// (save.dat's fullscreen set here: --fullscreen wins)
	if (full) s_wantFull = 1;
	double last = platform_now (), frame_s = 1.0 / 60;
	while (!s_exit)
	{
		poll_input ();
		prepare_frame ();
		system_update ();
		end_frame ();
		sound_service (frame_s);
		// at most 60 pictures a second (the game moves by the time passed)
		double now = platform_now ();
		frame_s = now - last;
		if (frame_s < 1.0 / 60) { kapi_msleep ((unsigned) ((1.0 / 60 - frame_s) * 1000)); now = platform_now (); frame_s = now - last; }
		last = now;
	}
	system_cleanup ();
	if (s_fs) uk_win_fullscreen_end ();
	if (s_audio == 1) ak_out_close ();
	return 0;
}
