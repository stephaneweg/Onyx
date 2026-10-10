//
// uikit/padhint.h -- a handheld's buttons as small flat glyphs, and lines of hints made of them ("[+] move  (A) choose
// (B) back"): what PocketUI's screens show at their foot on a device with a D-pad and A / B / X / Y, L / R, Start /
// Select (the Waveshare PocketTerm35: docs/COMPACT-SHELL-STUDY.md section 20, docs/compact-shell/mockups/pocketterm/
// button-map.png). Drawn by code, in the theme's colours (the flat look's tokens; else shades of the face); the text
// in the installed face.
//
//   static const UkPadHint H[] = { { "+", TR ("move") }, { "A", TR ("choose") }, { "B", TR ("back") } };
//   int w = uk_pad_hints (cv, 0, 0, 18, H, 3, C_DIS, true);         // measured
//   uk_pad_hints (cv, (cv.w - w) / 2, y, 18, H, 3, C_DIS);          // centred
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _uikit_padhint_h
#define _uikit_padhint_h

#include "uikit/canvas.h"

namespace uikit {

// A button's glyph, h px high, at (x, y) -> its width (measure: nothing drawn). kind: 'A' 'B' 'X' 'Y' (a disc, A's
// in the accent), 'L' 'R' (a pill), 'S' Start, 's' Select (a pill with the word), '+' the D-pad, '-' its left / right
// arms lit, '|' its up / down arms lit.
int uk_pad_glyph (Canvas &cv, int x, int y, int h, int kind, bool measure = false);
// A key's cap (a keyboard's equivalent: "Esc", "F10") as an outlined box -> its width.
int uk_pad_key (Canvas &cv, int x, int y, int h, const char *label, bool measure = false);

// A hint: its glyphs (each character a kind of uk_pad_glyph; a word in brackets, "[Esc]", a key's cap) and its word.
struct UkPadHint { const char *glyphs; const char *word; };
// The line of hints at (x, y), h px high, the words in `ink` -> its width (measure: nothing drawn).
int uk_pad_hints (Canvas &cv, int x, int y, int h, const UkPadHint *hints, int n, unsigned ink, bool measure = false);

} // namespace uikit

#endif
