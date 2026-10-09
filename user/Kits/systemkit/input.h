//
// input.h -- the keyboard's layout and the mouse's wheel: the layouts of SD:/etc/keymaps (*.kmap), one taken at once
// and kept for every boot (SD:/etc/autostart's `keyb XX` line), and the wheel's speed (the lines a notch scrolls:
// SD:/etc/theme.txt `wheelspeed=`, the kernel reads it at boot; UIKit's uk_win_wheel_set applies it now). The
// Keyboard & Mouse applet and the console's Keyboard page share it. C and C++.
//
//   char maps[INPUT_KEYMAPS_MAX][12]; int n = input_keymaps (maps, INPUT_KEYMAPS_MAX);   // "BE", "DE"... sorted
//   input_keymap_set ("BE");        // INPUT_KEPT, INPUT_TAKEN (not kept), 0 not loaded
//   input_wheel_save (5);
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
#ifndef _input_onyx_h
#define _input_onyx_h
#include "appkit/appkit.h"
#include "sk_api.h"

#define INPUT_KEYMAPS_MAX	24
#define INPUT_KEPT		1	// input_keymap_set: taken, and kept for every boot
#define INPUT_TAKEN		2	// ... taken, the autostart not written

// The layouts on the card (SD:/etc/keymaps/<CODE>.kmap), their codes sorted into out -> how many.
SK_API int input_keymaps (char (*out)[12], int max);
// A layout's name in English ("Belgian (azerty)": the caller translates it), "" for one Onyx does not ship.
SK_API const char *input_keymap_name (const char *code);
// The layout in use (the kernel's) into out -> its length.
SK_API int input_keymap_now (char *out, int cap);
// A layout taken now and kept -> INPUT_KEPT, INPUT_TAKEN, 0 (could not be loaded).
SK_API int input_keymap_set (const char *code);
// Is the layout an AZERTY one (the letters' row starts a, z) -> 1; QWERTZ -> 2; else 0 (QWERTY's; a pad's keyboard
// on the screen follows it).
SK_API int input_keymap_kind (const char *code);
// The wheel's speed kept (1..16; 3 when none said); written -> 1, 0 not (the caller applies it: uk_win_wheel_set).
SK_API int input_wheel (void);
SK_API int input_wheel_save (int lines);

#if defined (SK_BODIES_INLINE) && !defined (SK_IMPL)
#include "input.inc"
#endif

#endif
