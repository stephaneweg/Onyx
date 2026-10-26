# Onyx — InputKit reference

*The reference of **InputKit** (`SD:/lib/inputkit.so`, `user/Kits/inputkit`): what it is for, how a program uses it, and every operation it exposes. The operations' part is made from the kit's headers by `tools/docgen/kitdocs.py` — the headers are the source. Overview of all the kits: [The Kits](06-KITS-GUIDE.md).*

## Contents

1. [What it is](#what-it-is)
2. [Using it](#using-it)
3. [Index](#index)
4. [`inputkit/inputkit.h`](#inputkitinputkith)

---

## What it is

InputKit is the keyboard and the gamepads as one input: a program asks for an action -- Up, Accept, Back, Prev, Next... -- and never for 'the arrow key or the D-pad'. Each action has its keys and its pad buttons, from one file (SD:/etc/input.ini); an interface reads events, a game the state. UIKit's navigation is built on it.

| | |
|---|---|
| Include | `#include "inputkit/inputkit.h"` |
| Link | `lib/inputkit.imp.a` (C++) or `lib/inputkit.imp_c.a` (C) |
| Library | `SD:/lib/inputkit.so` — 12 entries in its table (`user/Kits/inputkit/inputkit.abi`, append-only) |
| Sources | `user/Kits/inputkit/` |

## Using it

A program asks for an **action** — `IN_UP`, `IN_ACCEPT`, `IN_NEXT`... — never for "the arrow key or the D-pad".
Each action has its **sources**, keys (with their modifiers) and pad buttons, in one file, **`SD:/etc/input.ini`**
(two sections, `[keys]` and `[pad]`; without the file, or for what it does not say, the defaults):

| action | keys | pad | what it is for |
|---|---|---|---|
| `up` `down` `left` `right` | the arrows | the D-pad (and the left stick) | the focus, the caret |
| `accept` | Enter | A | open, confirm, activate |
| `back` | Esc | B | cancel, close, back |
| `alt1` | Delete | X | the second action: close the chosen app, remove |
| `alt2` | Shift+F10 | Y | the options: a context menu |
| `prev` / `next` | Ctrl+Page Up, Ctrl+Shift+Tab / Ctrl+Page Down, Ctrl+Tab | L / R | the tab, the category, the section before / after |
| `menu` | F10 | — | the app's menus (PocketUI's CMD key is the server's own) |
| `home` | Ctrl+Esc | the middle button | Home |
| `start` `select` `l2` `r2` | — | Start, Select, L2, R2 | a game's |

Which raw button of a given pad model is "A" stays **`gamepad.h`**'s business (`SD:/etc/gamepad.ini`, the Control
Panel's Gamepad applet): InputKit is built on it, and the emulators that still read `gamepad.h` are untouched.

Two ways to read, for two kinds of programs:

- **events**, for an interface — `in_poll` gives the pads' actions *pressed*, *repeated* (the directions, at the
  keyboard's cadence) and *released*, each saying its source (so a hint shows the right glyph: `in_binding`); a key
  the program received becomes its action with `in_key_action (key, kapi_get_modifiers ())`. Only the program that
  has the keys gets a pad's events: a background app never moves;
- **state**, for a game — `in_state (player, &st)`: the actions held now (`st.held & IN_BIT (IN_ACCEPT)`), the
  sticks, per player; player 0 also reads the keyboard's held keys. A program that reads the state **owns the pad**
  (`in_claimed`): the interface's navigation leaves it alone.

```cpp
#include "inputkit/inputkit.h"

// an interface with its own loop: the pad and the keys through the same switch
static void act (int action)
{
	switch (action)
	{
	case IN_LEFT:   move (-1); break;
	case IN_RIGHT:  move (1); break;
	case IN_ACCEPT: open_chosen (); break;
	case IN_BACK:   close_me (); break;
	case IN_NEXT:   next_page (); break;
	}
}
static void on_key (unsigned long, int, long key) { int a = in_key_action ((int) key, kapi_get_modifiers ()); if (a >= 0) act (a); }
// ... each turn:
struct in_event ev[8];
for (int i = 0, n = in_poll (ev, 8); i < n; i++) if (ev[i].kind != IN_RELEASE) act (ev[i].action);

// a game: the state
struct in_state st;
if (in_state (0, &st)) { if (st.held & IN_BIT (IN_LEFT)) x--; if (st.held & IN_BIT (IN_ACCEPT)) jump (); }
```

**A UIKit app writes none of this**: a `Root` takes its navigation from InputKit (docs/03 §5.10.8) — the pad's
directions, A, B and X act as the arrows, Enter, Esc and Delete on the focused control, L / R step the window's
tabs, Y is the context menu. Text is not InputKit's: characters keep coming as keys.

## Index

Everything the headers declare, in their order — the details are in each header's part below.

| Name | What it does | Header |
|---|---|---|
| `in_event` | (a type) | `inputkit.h` |
| `in_state` | (a type) | `inputkit.h` |
| `in_load` | SD:/etc/input.ini (and SD:/etc/gamepad.ini) read again -> 1 the file was there, 0 the defaults. | `inputkit.h` |
| `in_load_text` | The bindings from a text (a test, a preview) instead of the file -> 1. | `inputkit.h` |
| `in_key_action` | The action a key IS, with the modifiers held (MOD_CTRL \| MOD_SHIFT \| MOD_ALT) -> IN_*, -1 none. | `inputkit.h` |
| `in_key_event` | The same as an event (IN_PRESS, IN_SRC_KEY) -> 1, 0 not an action. | `inputkit.h` |
| `in_poll` | The pads' events since the last call (this program's pads | `inputkit.h` |
| `in_claim` | The pad is this program's own (1) or the interface's again (0) | `inputkit.h` |
| `in_claimed` | The pad is this program's own (1) or the interface's again (0) | `inputkit.h` |
| `in_action_name` | An action's name ("accept") and the words of its first source of a kind ("Enter", "Ctrl+PageUp" | `inputkit.h` |
| `in_binding` | An action's name ("accept") and the words of its first source of a kind ("Enter", "Ctrl+PageUp" | `inputkit.h` |
| `in_pad_mask` | The PAD_* bits bound to an action (gamepad.h), 0 none. | `inputkit.h` |
| `in_app_owns_pad` | A program's app.txt says its pad is its own | `inputkit.h` |

---

## `inputkit/inputkit.h`

inputkit.h -- InputKit (SD:/lib/inputkit.so; docs/06-KITS-GUIDE.md "InputKit", docs/21-INPUTKIT.md): the keyboard and the gamepads as ONE input. A program asks for an ACTION -- Up, Accept, Next... -- never "the arrow key or the D-pad": each action has its sources, keys (with their modifiers) and pad buttons, from one binding file, SD:/etc/input.ini (the defaults below without it). The pads' own layouts -- which raw button is A on this model -- stay gamepad.h's (SD:/etc/gamepad.ini, the Gamepad applet): InputKit is built on it.

Two ways to read, for two kinds of programs:

```
  * EVENTS, for an interface: in_poll gives the pads' actions pressed, repeated (the keyboard's cadence: 0.3 s then
    12 a second, the directions only) and released, each with its source so that a line of hints shows the right
    glyph; a key the program received is turned into its action by in_key_action. Only the program that has the
    keys gets a pad's events (the kernel's rule: a pad reads as nothing pressed in the others).
  * STATE, for a game: in_state gives the actions held now, per player (a pad each; player 0 also the keyboard's
    keys held), and the sticks. A program that reads the state OWNS the pad (in_claimed): the interface's
    navigation leaves it alone.
```

Text entry is not InputKit's: the characters keep coming as keys.

```
  struct in_event ev[8];
  for (int i = 0, n = in_poll (ev, 8); i < n; i++)
      if (ev[i].kind != IN_RELEASE && ev[i].action == IN_NEXT) next_tab ();
  int a = in_key_action (key, kapi_get_modifiers ());      // in a key handler: IN_UP ..., -1 not an action
  struct in_state st; if (in_state (0, &st) && (st.held & IN_BIT (IN_ACCEPT))) jump ();
```

SD:/etc/input.ini -- two sections, a line an action, several sources separated by commas ("none": no source):

```
  [keys]  up = Up          down = Down        left = Left       right = Right
          accept = Enter   back = Esc         alt1 = Delete     alt2 = Shift+F10
          prev = Ctrl+PageUp, Ctrl+Shift+Tab  next = Ctrl+PageDown, Ctrl+Tab
          menu = F10       home = Ctrl+Esc    start = none      select = none     l2 = none   r2 = none
  [pad]   up = up          down = down        left = left       right = right     (gamepad.h's button names)
          accept = a       back = b           alt1 = x          alt2 = y
          prev = l         next = r           menu = none       home = home
          start = start    select = select    l2 = l2           r2 = r2
```

A key: Up Down Left Right Enter Esc Tab Space Backspace Delete Home End PageUp PageDown F1..F12 or a character, with Ctrl+ Shift+ Alt+ before it. (PocketUI's own system keys -- the Super key tapped: the menu, held: Home -- are the server's, not this file's.)

C and C++: link lib/inputkit.imp.a (C++) or lib/inputkit.imp_c.a (C); the interface is append-only (inputkit.abi).

MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).

The actions.

```cpp
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
```

SD:/etc/input.ini (and SD:/etc/gamepad.ini) read again -> 1 the file was there, 0 the defaults. Read by itself at the first call of any function below.

```cpp
int in_load (void);
```

The bindings from a text (a test, a preview) instead of the file -> 1.

```cpp
int in_load_text (const char *text);
```

The action a key IS, with the modifiers held (MOD_CTRL | MOD_SHIFT | MOD_ALT) -> IN_*, -1 none.

```cpp
int in_key_action (int key, unsigned mods);
```

The same as an event (IN_PRESS, IN_SRC_KEY) -> 1, 0 not an action.

```cpp
int in_key_event (int key, unsigned mods, struct in_event *out);
```

The pads' events since the last call (this program's pads: only while it has the keys) -> how many (max at most). The directions repeat while held. Nothing while the program has claimed the pad (in_state, in_claim).

```cpp
int in_poll (struct in_event *out, int max);
```

A game's reading: the actions held now by `player` (0..3: that pad; player 0 also the keys held on the keyboard -- kapi_key_held), the sticks -> 1, 0 no such player. The first call claims the pad for this program.

```cpp
int in_state (int player, struct in_state *out);
```

The pad is this program's own (1) or the interface's again (0); in_claimed: 1 once in_state or in_claim (1) was called.

```cpp
void in_claim (int on);
int  in_claimed (void);
```

An action's name ("accept") and the words of its first source of a kind ("Enter", "Ctrl+PageUp"; a pad's: "A", "L", "D-pad up") into out -> 1, 0 it has none: what a hint line writes.

```cpp
const char *in_action_name (int action);
int in_binding (int action, int source, char *out, int cap);
```

The PAD_* bits bound to an action (gamepad.h), 0 none.

```cpp
unsigned in_pad_mask (int action);
```

A program's app.txt says its pad is its own: the category Games or Emulators, or "pad = own" (the programs that still read gamepad.h themselves) -> 1. app: its folder's name, 0 = this program.

```cpp
int in_app_owns_pad (const char *app);

}
#endif
```
