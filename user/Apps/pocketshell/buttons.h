//
// buttons.h -- pocketshell and a handheld's BUTTONS (the Waveshare PocketTerm35: a D-pad, A / B / X / Y, L / R, Start /
// Select; docs/COMPACT-SHELL-STUDY.md section 20.8). The shell reads them through InputKit (inputkit/inputkit.h): the
// ACTIONS -- Up, Accept, Back, Alt1, Prev, Next... -- whatever they come from, a pad's button or a key, bound in ONE
// file, SD:/etc/input.ini (the section [buttons] of SD:/etc/pocketui.ini, where this shell typed a key for each
// button, is gone: nothing types keys any more).
//
// The shell has the pad only while it has the keys -- on the home and under its overlays (the kernel gives a pad's
// buttons to the program that has the keys; PocketUI names the shell then): over an app the pad is the app's -- a
// UIKit app navigates by itself (uikit/adapt.h), a game reads it as it wants. Start and Select are never the shell's.
//
//   the directions   the focus (the home's grid, the Running pills, the switcher, quick settings)
//   Accept (A)       open, switch, do               Back (B)   back, stay, close; on the home: the last app
//   Alt1 (X)         close the chosen app           Prev / Next (L / R)   the category before / after
//   Home             a pad's middle button: Home
//
// Part of main.cpp (one unit).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//

static void shell_event (unsigned long, int ev, long v);
static void home_key (unsigned long, int ev, long v);
static void set_tab (int t);
static void toggle_home (bool key);

// An action of InputKit's, for the shell's screen that shows.
static void shell_action (int a)
{
	int code = a == IN_UP ? KEY_UP : a == IN_DOWN ? KEY_DOWN : a == IN_LEFT ? KEY_LEFT : a == IN_RIGHT ? KEY_RIGHT
		 : a == IN_ACCEPT ? KEY_ENTER : a == IN_BACK ? 0x1b : a == IN_ALT1 ? KEY_DEL : 0;
	if (a == IN_HOME) { toggle_home (true); return; }
	if (g_over != OV_NONE) { if (code) shell_event (0, UK_SHELL_KEYEV, UK_SHELL_KEY (0, code)); return; }
	if (!g_home) return;
	if (a == IN_PREV || a == IN_NEXT) { set_tab (g_tab + (a == IN_PREV ? -1 : 1)); return; }
	if (code) home_key (0, GUI_EVENT_KEY, code);
}

// Each turn of the shell's loop: the pad's events (pressed, repeated).
static void buttons_poll (void)
{
	struct in_event ev[8];
	int n = in_poll (ev, 8);
	static bool said;					// (the Pi's kmsg: a pad reaches the shell)
	if (n > 0 && !said) { said = true; ax_puts ("pocketshell: a pad's buttons reach the shell (InputKit)\n"); }
	for (int i = 0; i < n; i++) if (ev[i].kind != IN_RELEASE) shell_action (ev[i].action);
}
