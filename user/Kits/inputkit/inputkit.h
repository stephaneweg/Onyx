//
// inputkit.h -- InputKit (SD:/lib/inputkit.so; docs/06-KITS-GUIDE.md "InputKit", docs/21-INPUTKIT.md): the keyboard
// and the gamepads as ONE input. A program asks for an ACTION -- Up, Accept, Next... -- never "the arrow key or the
// D-pad": each action has its sources, keys (with their modifiers) and pad buttons, from one binding file,
// SD:/etc/input.ini (the defaults below without it). The pads' own layouts -- which raw button is A on this model --
// stay gamepad.h's (SD:/etc/gamepad.ini, the Gamepad applet): InputKit is built on it.
//
// Two ways to read, for two kinds of programs:
//   * EVENTS, for an interface: in_poll gives the pads' actions pressed, repeated (the keyboard's cadence: 0.3 s then
//     12 a second, the directions only) and released, each with its source so that a line of hints shows the right
//     glyph; a key the program received is turned into its action by in_key_action. Only the program that has the
//     keys gets a pad's events (the kernel's rule: a pad reads as nothing pressed in the others).
//   * STATE, for a game: in_state gives the actions held now, per player (a pad each; player 0 also the keyboard's
//     keys held), and the sticks. A program that reads the state OWNS the pad (in_claimed): the interface's
//     navigation leaves it alone.
// Text entry is not InputKit's: the characters keep coming as keys.
//
//   struct in_event ev[8];
//   for (int i = 0, n = in_poll (ev, 8); i < n; i++)
//       if (ev[i].kind != IN_RELEASE && ev[i].action == IN_NEXT) next_tab ();
//   int a = in_key_action (key, kapi_get_modifiers ());      // in a key handler: IN_UP ..., -1 not an action
//   struct in_state st; if (in_state (0, &st) && (st.held & IN_BIT (IN_ACCEPT))) jump ();
//
// SD:/etc/input.ini -- two sections, a line an action, several sources separated by commas ("none": no source):
//   [keys]  up = Up          down = Down        left = Left       right = Right
//           accept = Enter   back = Esc         alt1 = Delete     alt2 = Shift+F10
//           prev = Ctrl+PageUp, Ctrl+Shift+Tab  next = Ctrl+PageDown, Ctrl+Tab
//           menu = F10       home = Ctrl+Esc    start = none      select = none     l2 = none   r2 = none
//   [pad]   up = up          down = down        left = left       right = right     (gamepad.h's button names)
//           accept = a       back = b           alt1 = x          alt2 = y
//           prev = l         next = r           menu = none       home = home
//           start = start    select = select    l2 = l2           r2 = r2
// A key: Up Down Left Right Enter Esc Tab Space Backspace Delete Home End PageUp PageDown F1..F12 or a character,
// with Ctrl+ Shift+ Alt+ before it. (PocketUI's own system keys -- the Super key tapped: the menu, held: Home --
// are the server's, not this file's.)
//
// C and C++: link lib/inputkit.imp.a (C++) or lib/inputkit.imp_c.a (C); the interface is append-only (inputkit.abi).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _inputkit_h
#define _inputkit_h
#include "appkit/appkit.h"

#ifdef __cplusplus
extern "C" {
#endif

// The actions.
enum { IN_UP = 0, IN_DOWN, IN_LEFT, IN_RIGHT,
       IN_ACCEPT,		// A / Enter: open, confirm, activate
       IN_BACK,			// B / Esc: back, cancel, close
       IN_ALT1,			// X / Delete: the second action (close the chosen app, remove)
       IN_ALT2,			// Y / Shift+F10: the options, a context menu
       IN_PREV, IN_NEXT,	// L / R, Ctrl+Page Up / Down: the tab, category, section before / after
       IN_MENU,			// F10: the app's menus (a handheld's CMD key is PocketUI's own)
       IN_HOME,			// a pad's middle button / Ctrl+Esc
       IN_START, IN_SELECT,	// a game's
       IN_L2, IN_R2,
       IN_ACTIONS };
#define IN_BIT(a)	(1u << (a))

enum { IN_SRC_NONE = 0, IN_SRC_KEY = 1, IN_SRC_PAD = 2 };
enum { IN_PRESS = 1, IN_REPEAT = 2, IN_RELEASE = 3 };

struct in_event
{
	int action;			// IN_*
	int kind;			// IN_PRESS, IN_REPEAT, IN_RELEASE
	int source;			// IN_SRC_PAD (in_poll), IN_SRC_KEY (in_key_event)
	int code;			// the pad's button (gamepad.h's PAD_* bit) or the key's code
	int mods;			// a key's modifiers (MOD_*)
	int player;			// the pad's number, 0..3
};

struct in_state
{
	unsigned held;			// IN_BIT (action) for each action held now
	int lx, ly, rx, ry;		// the sticks, -1000..1000
	int connected;			// 1: a pad is there for this player (player 0: the keyboard counts)
	int source;			// IN_SRC_PAD / IN_SRC_KEY: where the last held action came from
};

// SD:/etc/input.ini (and SD:/etc/gamepad.ini) read again -> 1 the file was there, 0 the defaults. Read by itself at
// the first call of any function below.
int in_load (void);
// The bindings from a text (a test, a preview) instead of the file -> 1.
int in_load_text (const char *text);

// The action a key IS, with the modifiers held (MOD_CTRL | MOD_SHIFT | MOD_ALT) -> IN_*, -1 none.
int in_key_action (int key, unsigned mods);
// The same as an event (IN_PRESS, IN_SRC_KEY) -> 1, 0 not an action.
int in_key_event (int key, unsigned mods, struct in_event *out);

// The pads' events since the last call (this program's pads: only while it has the keys) -> how many (max at most).
// The directions repeat while held. Nothing while the program has claimed the pad (in_state, in_claim).
int in_poll (struct in_event *out, int max);

// A game's reading: the actions held now by `player` (0..3: that pad; player 0 also the keys held on the keyboard --
// kapi_key_held), the sticks -> 1, 0 no such player. The first call claims the pad for this program.
int in_state (int player, struct in_state *out);
// The pad is this program's own (1) or the interface's again (0); in_claimed: 1 once in_state or in_claim (1) was called.
void in_claim (int on);
int  in_claimed (void);

// An action's name ("accept") and the words of its first source of a kind ("Enter", "Ctrl+PageUp"; a pad's: "A",
// "L", "D-pad up") into out -> 1, 0 it has none: what a hint line writes.
const char *in_action_name (int action);
int in_binding (int action, int source, char *out, int cap);
// The PAD_* bits bound to an action (gamepad.h), 0 none.
unsigned in_pad_mask (int action);

// A program's app.txt says its pad is its own: the category Games or Emulators, or "pad = own" (the programs that
// still read gamepad.h themselves) -> 1. app: its folder's name, 0 = this program.
int in_app_owns_pad (const char *app);

#ifdef __cplusplus
}
#endif

#endif
