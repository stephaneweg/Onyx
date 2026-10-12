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

// THE HINT LINE (pocket and console; the desktop has none): a 30 px line at the window's foot that says the keys the
// view -- or a mode of it -- has of its own, with the device's glyphs: "[+] move  (A) put  [F5] Check". The app gives
// its hints (copied: 8 at most; those that do not fit the width are left out from the end) each time they change;
// `mode` is a mode's name shown first, as a pill ("MOVE", "CROP": the only place a mode is announced). n = 0: no line.
// -> the height the line takes at the window's foot (30; 0: none, and always 0 on the desktop, where nothing is
// made): the app's layout leaves it. uk_hint_line_height: what it takes now.
//   int foot = uk_hint_line (&root, H, 3);            // in the app's layout: ... root.height - foot ...
// hints = 0 with n = UK_HINTS_STANDARD: what every window of tools and a page has -- "[F6] tools  [Alt][+] scroll
// [CMD] Menu" -- in UIKit's own words (an app with no key of its own but these).
class Root;
enum { UK_HINTS_STANDARD = -1 };
int uk_hint_line (Root *root, const UkPadHint *hints, int n, const char *mode = 0);
int uk_hint_line_height (Root *root);

} // namespace uikit

#endif
