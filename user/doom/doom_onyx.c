//
// doom_onyx.c -- Doom on Onyx: the doomgeneric platform layer (third_party/doomgeneric).
//
//   doom [-iwad <file.wad>] [doomgeneric args]      (or a .wad path: runners.ini "wad" -- a
//                                                    whole game becomes -iwad, a mod -file)
//   * The game files live in SD:/doom: the IWAD (the first found of doom2.wad, plutonia.wad,
//     tnt.wad, doom.wad, doom1.wad, freedoom2.wad, freedoom1.wad -- Freedoom ships with
//     Onyx), the settings (default.cfg) and the saved games (savegame/<iwad>/).
//   * The picture: Doom's 320 x 200, doubled into the 640 x 400 window (the window canvas IS
//     doomgeneric's screen buffer: no copy); F11 or Game > Full Screen stretches it to the
//     display at 4:3 (Doom's pixels were not square), centred.
//   * Keys: arrows move, Ctrl fires, Space opens doors / uses, Alt + arrows strafes (also
//     , and .), Shift runs, 1-7 weapons, Tab the map, Esc the menu, F1-F10 as in DOS Doom.
//     A USB gamepad: the d-pad, A fire, B use, X run, L / R strafe, Start the menu, Select
//     the map (user/gamepad.h).
//   * Sound effects and music: doom_sound.c.
//   * Two threads when an app core is free (kapi v51): the engine (doomgeneric_Tick) runs on
//     the app core, undisturbed; this main thread keeps the window, the input, the sound
//     output and the music, and runs the engine's syscalls (malloc, files, printf: the RPC of
//     libc/onyx_syscalls.c). Pictures go through a triple buffer, keys through a queue.
//     Without a free core, all runs here as before.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "kapi.h"
#include "gamepad.h"
#include "notify.h"
#include "doomgeneric.h"
// Doom's key codes (doomkeys.h, whose names clash with kapi.h's KEY_*)
#define DK_RIGHT	0xae
#define DK_LEFT		0xac
#define DK_UP		0xad
#define DK_DOWN		0xaf
#define DK_STRAFE_L	0xa0
#define DK_STRAFE_R	0xa1
#define DK_USE		0xa2
#define DK_FIRE		0xa3
#define DK_ESCAPE	27
#define DK_ENTER	13
#define DK_TAB		9
#define DK_BACKSPACE	0x7f
#define DK_RSHIFT	(0x80 + 0x36)
#define DK_RALT		(0x80 + 0x38)

void onyx_decorate (void);					// doom_uikit.cpp
void onyx_menu (void (*full) (void), void (*quit) (void));

static unsigned *s_canvas = 0;				// the window's 640 x 400 client area
static unsigned *s_fs = 0; static int s_fsw, s_fsh;	// full screen back buffer
static int s_wantFull = 0;
int g_doomOnCore = 0;						// the engine runs on an app core
static int s_core = -1;

// libc/onyx_syscalls.c: the syscalls made on an app core, run by the main thread
void onyx_rpc_enable (int on);
void onyx_rpc_serve (void);
long onyx_rpc3 (long (*fn) (long, long, long), long a, long b, long c);
void onyx_sound_service (void);					// doom_sound.c

static inline void fence (void) { __asm__ volatile ("dmb ish" ::: "memory"); }
static unsigned xchg (volatile unsigned *p, unsigned v)
{
	unsigned old, fail;
	__asm__ volatile ("1: ldaxr %w0, [%2]\n"
			  "   stlxr %w1, %w3, [%2]\n"
			  "   cbnz  %w1, 1b\n"
			  : "=&r" (old), "=&r" (fail) : "r" (p), "r" (v) : "memory");
	return old;
}

// the pictures: a triple buffer (the engine draws in back, the main thread shows front)
static pixel_t *s_slot[3];
static unsigned s_back = 0, s_front = 1;
static volatile unsigned s_mid = 2;				// slot | 4 when it holds a new picture

// ---- time: the ARM generic timer (apps run at EL1) ----------------------------------------
static unsigned long long now_us (void)
{
	unsigned long long c, f;
	__asm__ volatile ("mrs %0, cntpct_el0" : "=r" (c));
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (f));
	return f ? c * 1000000ull / f : 0;
}
uint32_t DG_GetTicksMs (void) { return (uint32_t) (now_us () / 1000); }
void onyx_music_poll (void);					// doom_sound.c
void DG_SleepMs (uint32_t ms)
{
	if (g_doomOnCore)						// (the app core: wait on the clock)
	{
		unsigned long long end = now_us () + (ms ? ms : 1) * 1000ull;
		while (now_us () < end) __asm__ volatile ("yield");
		return;
	}
	onyx_music_poll ();
	kapi_msleep (ms ? ms : 1);
}
void DG_SetWindowTitle (const char *title) { (void) title; }

// ---- files (helpers for the patched doomgeneric sources) ------------------------------------
// (run by the main thread when the engine calls them on its app core: onyx_rpc3)
static long x_exists (long path, long b, long c)
{
	(void) b; (void) c;
	void *h = kapi_open ((const char *) path);
	if (!h) return 0;
	kapi_close (h);
	return 1;
}
int onyx_file_exists (const char *path)			// (without reading it: a WAD is big)
{
	return (int) onyx_rpc3 (x_exists, (long) path, 0, 0);
}
static long x_mkdir (long path, long b, long c) { (void) b; (void) c; return kapi_mkdir ((const char *) path) == 0 ? 0 : -1; }
int mkdir (const char *path, mode_t mode) { (void) mode; return (int) onyx_rpc3 (x_mkdir, (long) path, 0, 0); }
// (newlib's rename goes through link + unlink, which Onyx has not: the kernel renames)
static long x_rename (long from, long to, long c)
{
	(void) c;
	kapi_remove ((const char *) to);
	return kapi_rename ((const char *) from, (const char *) to) == 0 ? 0 : -1;
}
int rename (const char *from, const char *to) { return (int) onyx_rpc3 (x_rename, (long) from, (long) to, 0); }
int _link (const char *from, const char *to) { (void) from; (void) to; return -1; }

// ---- the picture -------------------------------------------------------------------------------
static void full_screen (int on)
{
	if (on && !s_fs)
	{
		s_fs = kapi_fullscreen_begin (&s_fsw, &s_fsh);
		if (s_fs) memset (s_fs, 0, (size_t) s_fsw * s_fsh * 4);
	}
	else if (!on && s_fs) { kapi_fullscreen_end (); s_fs = 0; }
}

static const pixel_t *s_shown;					// the picture to show (see present)
static void blit_full (void)
{
	// 320 x 200 at 4:3: height * 4 / 3 wide, the display's height (or narrower screens: width)
	int oh = s_fsh, ow = s_fsh * 4 / 3;
	if (ow > s_fsw) { ow = s_fsw; oh = s_fsw * 3 / 4; }
	int ox = (s_fsw - ow) / 2, oy = (s_fsh - oh) / 2;
	static int xmap[4096];
	int n = ow < 4096 ? ow : 4096;
	for (int x = 0; x < n; x++) xmap[x] = x * DOOMGENERIC_RESX / ow;
	for (int y = 0; y < oh; y++)
	{
		int sy = y * DOOMGENERIC_RESY / oh;
		unsigned *d = s_fs + (long) (oy + y) * s_fsw + ox;
		if (y > 0 && sy == (y - 1) * DOOMGENERIC_RESY / oh) { memcpy (d, d - s_fsw, (size_t) n * 4); continue; }
		const unsigned *s = (const unsigned *) s_shown + sy * DOOMGENERIC_RESX;
		for (int x = 0; x < n; x++) d[x] = s[xmap[x]];
	}
}

// Show a picture: stretched full screen, or into the window (which is the picture itself
// when the engine runs here).
static void show_picture (const pixel_t *pic)
{
	s_shown = pic;
	if (s_wantFull != (s_fs != 0)) full_screen (s_wantFull);
	if (s_fs) { blit_full (); kapi_present_fb (); return; }
	if ((const void *) pic != (const void *) s_canvas) memcpy (s_canvas, pic, DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);
	kapi_present ();
}

void DG_DrawFrame (void)
{
	if (!g_doomOnCore) { show_picture (DG_ScreenBuffer); return; }
	// the app core: hand the picture over, draw the next one in a free slot
	fence ();
	s_back = xchg (&s_mid, s_back | 4) & 3;
	DG_ScreenBuffer = s_slot[s_back];
}

// ---- input ----------------------------------------------------------------------------------------
#define QN 128
static unsigned char s_qkey[QN], s_qdown[QN];
static volatile int s_qh = 0, s_qt = 0;				// written by the main thread / the engine
static unsigned char s_down[256];				// Doom keys held (as last reported)
static unsigned char s_tap[256];				// tapped keys: released at the next poll

static void push (int down, unsigned char key)
{
	int n = (s_qh + 1) % QN;
	if (n == s_qt) return;
	s_qkey[s_qh] = key; s_qdown[s_qh] = (unsigned char) down;
	fence ();
	s_qh = n;
}

static void on_full (void) { s_wantFull = !s_wantFull; }
static void quit (void)
{
	if (g_doomOnCore) { kapi_core_release (s_core); g_doomOnCore = 0; }	// (the engine stops first)
	exit (0);
}
static void on_quit (void) { quit (); }

// Keys that are not followed while held (kapi_key_held knows arrows, Enter, Esc, Space,
// letters, digits): each press is a tap, released at the next poll.
static void key_event (unsigned long sender, int event, long value)
{
	(void) sender;
	if (event != GUI_EVENT_KEY) return;
	unsigned char k = 0;
	if (value == KEY_F1 + 10) { on_full (); return; }		// F11
	if (value >= KEY_F1 && value <= KEY_F1 + 9) k = (unsigned char) (0x80 + 0x3b + (value - KEY_F1));
	else if (value == KEY_F1 + 11) k = 0x80 + 0x58;
	else switch (value)
	{
	case 9: k = 9; break;						// Tab: the map
	case 8: k = 0x7f; break;					// Backspace
	case KEY_PGUP: k = 0x80 + 0x49; break;
	case KEY_PGDN: k = 0x80 + 0x51; break;
	case KEY_HOME: k = 0x80 + 0x47; break;
	case KEY_END: k = 0x80 + 0x4f; break;
	case KEY_DEL: k = 0x80 + 0x53; break;
	case '-': case '=': case ',': case '.': case '/': case '[': case ']': case '\'': case ';': case '`':
	case '+': case '_': case '<': case '>': case '?': k = (unsigned char) value; break;
	default: return;						// (followed as held keys)
	}
	push (1, k);
	s_tap[k] = 1;
}

static void poll_input (void)
{
	for (int k = 0; k < 256; k++) if (s_tap[k]) { push (0, (unsigned char) k); s_tap[k] = 0; }
	kapi_pump_events ();
	if (kapi_should_exit ()) quit ();
	unsigned char want[256];
	memset (want, 0, sizeof want);
	static const struct { int onyx; unsigned char doom; } H[] = {
		{ KEY_UP, DK_UP }, { KEY_DOWN, DK_DOWN }, { KEY_LEFT, DK_LEFT }, { KEY_RIGHT, DK_RIGHT },
		{ KEY_ENTER, DK_ENTER }, { 27, DK_ESCAPE }, { ' ', DK_USE } };
	for (unsigned i = 0; i < sizeof H / sizeof H[0]; i++) if (kapi_key_held (H[i].onyx)) want[H[i].doom] = 1;
	for (int c = 'a'; c <= 'z'; c++) if (kapi_key_held (c)) want[c] = 1;
	for (int c = '0'; c <= '9'; c++) if (kapi_key_held (c)) want[c] = 1;
	unsigned m = kapi_get_modifiers ();
	if (m & MOD_CTRL) want[DK_FIRE] = 1;
	if (m & MOD_SHIFT) want[DK_RSHIFT] = 1;
	if (m & MOD_ALT) want[DK_RALT] = 1;
	unsigned p = pad_buttons (-1);
	if (p & PAD_UP) want[DK_UP] = 1;
	if (p & PAD_DOWN) want[DK_DOWN] = 1;
	if (p & PAD_LEFT) want[DK_LEFT] = 1;
	if (p & PAD_RIGHT) want[DK_RIGHT] = 1;
	if (p & (PAD_A | PAD_R2)) { want[DK_FIRE] = 1; want[DK_ENTER] = 1; }	// (Enter: the menus)
	if (p & PAD_B) { want[DK_USE] = 1; want[0x7f] = 1; }			// (Backspace: back in the menus)
	if (p & PAD_X) want[DK_RSHIFT] = 1;
	if (p & PAD_L) want[DK_STRAFE_L] = 1;
	if (p & PAD_R) want[DK_STRAFE_R] = 1;
	if (p & PAD_START) want[DK_ESCAPE] = 1;
	if (p & PAD_SELECT) want[DK_TAB] = 1;
	if (p & PAD_Y) want['y'] = 1;						// (answers "quit? y/n")
	for (int k = 0; k < 256; k++)
		if (want[k] != s_down[k] && !s_tap[k]) { push (want[k], (unsigned char) k); s_down[k] = want[k]; }
}

int DG_GetKey (int *pressed, unsigned char *key)
{
	static uint32_t lastPoll = 0;
	uint32_t t = DG_GetTicksMs ();
	if (!g_doomOnCore && s_qt == s_qh && t != lastPoll) { lastPoll = t; poll_input (); }	// (else: the main thread polls)
	if (s_qt == s_qh) return 0;
	fence ();
	*pressed = s_qdown[s_qt]; *key = s_qkey[s_qt];
	fence ();
	s_qt = (s_qt + 1) % QN;
	return 1;
}

// ---- start ----------------------------------------------------------------------------------------
void DG_Init (void)
{
	s_canvas = kapi_create_window (DOOMGENERIC_RESX, DOOMGENERIC_RESY, "Doom");
	if (!s_canvas) { printf ("doom: no window\n"); exit (1); }
	onyx_decorate ();
	free (DG_ScreenBuffer);
	DG_ScreenBuffer = (pixel_t *) s_canvas;			// Doom draws straight into the window
	memset (s_canvas, 0, DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);
	kapi_set_key_handler (key_event);
	onyx_menu (on_full, on_quit);
	kapi_present ();
}

static const char *const IWADS[] = { "doom2.wad", "plutonia.wad", "tnt.wad", "doom.wad", "doom1.wad",
				     "freedoom2.wad", "freedoom1.wad", 0 };

// "IWAD" (a whole game) or "PWAD" (a mod over one)?
static int wad_kind (const char *path)
{
	void *h = kapi_open (path);
	if (!h) return 0;
	char m[4] = { 0, 0, 0, 0 };
	kapi_read (h, m, 4); kapi_close (h);
	return !memcmp (m, "IWAD", 4) ? 1 : !memcmp (m, "PWAD", 4) ? 2 : 0;
}

static void engine (void *arg)				// on the app core
{
	(void) arg;
	for (;;) doomgeneric_Tick ();
}

int main (void)
{
	// the arguments: the kernel gives one string ("quoted paths" allowed)
	static char args[512]; static char *argv[32]; int argc = 0;
	kapi_get_args (args, sizeof args);
	argv[argc++] = "doom";
	for (char *p = args; *p && argc < 31; )
	{
		while (*p == ' ') p++;
		if (!*p) break;
		if (*p == '"') { argv[argc++] = ++p; while (*p && *p != '"') p++; }
		else { argv[argc++] = p; while (*p && *p != ' ') p++; }
		if (*p) *p++ = 0;
	}
	kapi_chdir ("SD:/doom");
	// a bare .wad path: the game (-iwad) or a mod (-file); --fullscreen starts full screen
	static char *av[40]; int ac = 0;
	static char iwad[256];
	av[ac++] = "doom";
	av[ac++] = "-mb"; av[ac++] = "32";				// (Doom's zone memory: 32 MB, Freedoom's maps are big)
	int haveIwad = 0;
	for (int i = 1; i < argc && ac < 34; i++)
	{
		const char *a = argv[i];
		size_t n = strlen (a);
		if (!strcmp (a, "--fullscreen")) { s_wantFull = 1; continue; }
		if (!strcmp (a, "-iwad")) haveIwad = 1;
		if (n > 4 && a[0] != '-' && !strcasecmp (a + n - 4, ".wad") && (i == 1 || argv[i - 1][0] != '-'))
		{
			if (wad_kind (a) == 2) { av[ac++] = "-file"; av[ac++] = (char *) a; continue; }
			if (!haveIwad) { snprintf (iwad, sizeof iwad, "%s", a); av[ac++] = "-iwad"; av[ac++] = iwad; haveIwad = 1; }
			continue;
		}
		av[ac++] = (char *) a;
	}
	if (!haveIwad)
		for (int i = 0; IWADS[i]; i++)
		{
			snprintf (iwad, sizeof iwad, "SD:/doom/%s", IWADS[i]);
			if (onyx_file_exists (iwad)) { av[ac++] = "-iwad"; av[ac++] = iwad; haveIwad = 1; break; }
		}
	if (!haveIwad)
	{
		notify ("Doom", "No game file (.wad) in SD:/doom");
		return 1;
	}
	av[ac] = 0;
	doomgeneric_Create (ac, av);

	// The engine on an app core if one is free (its pictures in three slots of its own).
	static unsigned char stack[1 << 20] __attribute__ ((aligned (16)));
	for (int i = 0; i < 3; i++) s_slot[i] = malloc (DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);
	if (s_slot[0] && s_slot[1] && s_slot[2] && (s_core = kapi_core_acquire ()) >= 0)
	{
		for (int i = 0; i < 3; i++) memcpy (s_slot[i], s_canvas, DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);
		DG_ScreenBuffer = s_slot[s_back];
		g_doomOnCore = 1;
		onyx_rpc_enable (1);
		fence ();
		if (kapi_core_run (s_core, engine, 0, stack + sizeof stack) != 0)
		{
			g_doomOnCore = 0; onyx_rpc_enable (0);
			DG_ScreenBuffer = (pixel_t *) s_canvas;
			kapi_core_release (s_core); s_core = -1;
		}
	}
	if (!g_doomOnCore) for (;;) doomgeneric_Tick ();		// (no app core: everything here)

	// The main thread: input, the engine's syscalls, sound + music, the pictures.
	for (;;)
	{
		poll_input ();
		onyx_rpc_serve ();
		onyx_sound_service ();
		if (s_mid & 4)
		{
			s_front = xchg (&s_mid, s_front) & 3;
			fence ();
			show_picture (s_slot[s_front]);
		}
		if (kapi_core_state (s_core) == KAPI_CORE_FAULT)
		{
			notify ("Doom", "The game stopped (a fault, see kmsg)");
			g_doomOnCore = 0;
			kapi_core_release (s_core);
			exit (1);
		}
		onyx_rpc_serve ();
		kapi_msleep (1);
	}
	return 0;
}
