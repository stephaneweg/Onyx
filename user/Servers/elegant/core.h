//
// core.h -- Elegant's window manager, as the rest of the server sees it.
//
// The window manager and the compositor themselves are the kernel's sources (kernel/gui/window.cpp,
// gimage.cpp), built for a user process with the stand-ins of port/ -- one code until the kernel's
// copy is retired. Their classes stay behind this file: core.cpp is the only one that includes
// kern/gui/window.h (its constants are also AppKit's, under the same names).
//
// One thread calls these (the server's loop): no lock.
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
#ifndef ELEGANT_CORE_H
#define ELEGANT_CORE_H

#define EL_WINDOWS_MAX	16		// (the kernel's WM_MAX_WINDOWS, until the list is the server's own)

// The window manager made, for a screen of w x h. 1, or 0: no memory.
int el_core_start (int w, int h);
// A generated wallpaper (the kernel's: cells tinted onto `base`).
void el_core_wallpaper (unsigned base, int points, unsigned seed);

// A window: its outer top left, its client size, WIN_FLAG_* -> its number (0 .. EL_WINDOWS_MAX - 1),
// or -1. Its pixels: the client area, and the frame's two copies (active 1 / inactive 0; 0 for a
// borderless window), 0x00RRGGBB, the row `*w` pixels.
int el_core_window_add (int x, int y, int w, int h, const char *title, unsigned flags, unsigned owner_pid);
unsigned *el_core_window_canvas (int id, int *w, int *h);
unsigned *el_core_window_frame (int id, int active, int *w, int *h);
void el_core_window_present (int id);		// its client area changed
int el_core_window_closing (int id);		// its close button was used
void el_core_window_remove (int id);

// The pointer, in screen coordinates: buttons bit 0 left, 1 right, 2 middle; wheel in notches.
void el_core_pointer (int x, int y, unsigned buttons, int wheel);

// What changed since the last call drawn into `screen` (w x h, the row w pixels) -> 1, or 0:
// nothing changed, nothing drawn.
int el_core_compose (unsigned *screen, int w, int h);

#endif
