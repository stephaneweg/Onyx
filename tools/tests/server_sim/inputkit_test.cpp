//
// inputkit_test.cpp -- InputKit's host test (user/Kits/inputkit): the binding file, a key -> its action, the words a
// hint shows, and -- with the stand-in kernel's pad (SIM_PAD=1: a known model, two buttons held) -- the pad's events
// and the state giving the SAME actions as gamepad.h's own reading. Linked with the stand-in kernel and InputKit's
// source; prints "inputkit: PASS / FAIL ...", its exit status the failures' count.
//   g++ ... inputkit_test.cpp fakekapi.o user/Kits/inputkit/inputkit.cpp ;  SIM_PAD=1 ./inputkit_test
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <stdio.h>
#include <string.h>
#include "inputkit/inputkit.h"
#include "gamepad.h"

static int g_fail;
static void check (bool ok, const char *what, long got = 0)
{
	if (!ok) g_fail++;
	printf ("inputkit: %s %s", ok ? "PASS" : "FAIL", what);
	if (!ok) printf (" -- got %ld", got);
	printf ("\n");
}

int main (void)
{
	// ---- the defaults: a key is its action, with its modifiers
	in_load_text (0);
	check (in_key_action (KEY_UP, 0) == IN_UP && in_key_action (KEY_RIGHT, 0) == IN_RIGHT, "defaults: the arrows are the directions");
	check (in_key_action (KEY_ENTER, 0) == IN_ACCEPT && in_key_action (27, 0) == IN_BACK, "defaults: Enter accepts, Esc goes back");
	check (in_key_action (KEY_DEL, 0) == IN_ALT1 && in_key_action (KEY_F1 + 9, MOD_SHIFT) == IN_ALT2, "defaults: Delete is Alt1, Shift+F10 Alt2");
	check (in_key_action (KEY_PGUP, MOD_CTRL) == IN_PREV && in_key_action (KEY_PGDN, MOD_CTRL) == IN_NEXT, "defaults: Ctrl+Page Up / Down are Prev / Next");
	check (in_key_action (KEY_TAB, MOD_CTRL) == IN_NEXT && in_key_action (KEY_TAB, MOD_CTRL | MOD_SHIFT) == IN_PREV, "defaults: Ctrl+Tab / Ctrl+Shift+Tab too");
	check (in_key_action (KEY_PGUP, 0) == -1 && in_key_action (KEY_TAB, 0) == -1, "defaults: Page Up, Tab alone are no action", in_key_action (KEY_PGUP, 0));
	check (in_key_action (KEY_F1 + 9, 0) == IN_MENU && in_key_action (27, MOD_CTRL) == IN_HOME, "defaults: F10 is Menu, Ctrl+Esc Home");
	check (in_key_action (KEY_ENTER, MOD_CTRL) == -1, "defaults: the modifiers must be the binding's", in_key_action (KEY_ENTER, MOD_CTRL));
	check (in_pad_mask (IN_ACCEPT) == PAD_A && in_pad_mask (IN_BACK) == PAD_B && in_pad_mask (IN_PREV) == PAD_L && in_pad_mask (IN_NEXT) == PAD_R, "defaults: A, B, L, R", in_pad_mask (IN_ACCEPT));
	check (in_pad_mask (IN_START) == PAD_START && in_pad_mask (IN_SELECT) == PAD_SELECT && in_pad_mask (IN_MENU) == 0, "defaults: Start and Select are their own actions, no pad button is Menu");
	char w[32];
	check (in_binding (IN_PREV, IN_SRC_KEY, w, sizeof w) && !strcmp (w, "Ctrl+PageUp"), "in_binding: the key's words");
	check (in_binding (IN_ACCEPT, IN_SRC_PAD, w, sizeof w) && !strcmp (w, "A"), "in_binding: the pad's button");
	check (!in_binding (IN_START, IN_SRC_KEY, w, sizeof w), "in_binding: no key for Start");
	check (!strcmp (in_action_name (IN_NEXT), "next"), "in_action_name");

	// ---- a binding file: what it says replaces the defaults, the rest stays
	in_load_text ("# a test\n[keys]\naccept = Enter, F5, Space\nback = none\nnext = Alt+n ; a comment\n[pad]\naccept = b\nback = a, x\nmenu = select\n");
	check (in_key_action (KEY_F1 + 4, 0) == IN_ACCEPT && in_key_action (' ', 0) == IN_ACCEPT && in_key_action (KEY_ENTER, 0) == IN_ACCEPT, "file: several keys for an action");
	check (in_key_action (27, 0) == -1, "file: none removes the key", in_key_action (27, 0));
	check (in_key_action ('n', MOD_ALT) == IN_NEXT && in_key_action ('N', MOD_ALT) == IN_NEXT && in_key_action (KEY_PGDN, MOD_CTRL) == -1, "file: a character with a modifier; the default it replaces is gone");
	check (in_key_action (KEY_UP, 0) == IN_UP, "file: what it does not name keeps its default");
	check (in_pad_mask (IN_ACCEPT) == PAD_B && in_pad_mask (IN_BACK) == (PAD_A | PAD_X) && in_pad_mask (IN_MENU) == PAD_SELECT, "file: the pad's buttons", in_pad_mask (IN_BACK));
	struct in_event e;
	check (in_key_event (KEY_F1 + 4, 0, &e) == 1 && e.action == IN_ACCEPT && e.source == IN_SRC_KEY && e.kind == IN_PRESS && e.code == KEY_F1 + 4, "in_key_event: the action and its source");

	// ---- the pad (SIM_PAD=1): events, then the state -- the same actions as gamepad.h reads
	in_load_text (0);
	unsigned b = pad_buttons (0);					// gamepad.h's own reading of the simulated pad
	unsigned want = 0;
	for (int a = 0; a < IN_ACTIONS; a++) if (in_pad_mask (a) & b) want |= IN_BIT (a);
	check (b != 0 && want != 0, "the simulated pad holds buttons bound to actions (SIM_PAD=1)", (long) b);
	struct in_event ev[16];
	int n = in_poll (ev, 16);
	unsigned got = 0; bool pads = n > 0;
	for (int i = 0; i < n; i++) { if (ev[i].kind == IN_PRESS) got |= IN_BIT (ev[i].action); if (ev[i].source != IN_SRC_PAD || ev[i].player != 0) pads = false; }
	check (got == want && pads, "in_poll: a press for each action held, from the pad", (long) got);
	check (in_poll (ev, 16) == 0, "in_poll: nothing new while the buttons stay held (no direction held)");
	check (in_claimed () == 0, "not claimed yet");
	struct in_state st;
	check (in_state (0, &st) == 1 && (st.held & want) == want && st.source == IN_SRC_PAD, "in_state: the same actions held", (long) st.held);
	n = in_poll (ev, 16);						// (claimed: what was held is released for the interface, then nothing)
	bool rel = true;
	for (int i = 0; i < n; i++) if (ev[i].kind != IN_RELEASE) rel = false;
	check (in_claimed () == 1 && rel && in_poll (ev, 16) == 0, "in_state claims the pad: the interface sees its buttons released, then nothing", n);
	in_claim (0);
	check (in_claimed () == 0, "in_claim (0) gives it back");
	printf ("inputkit: %d failed\n", g_fail);
	return g_fail;
}
