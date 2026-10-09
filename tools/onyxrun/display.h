//
// display.h -- the PC's screen and input for the runner (display.cpp): the frame buffer Elegant presents into (what a
// Pi's HDMI would show), shown in a window of the PC (Windows: a Win32 window) or kept in memory (--headless: written
// to a picture by --shot); the PC's mouse and keyboard -- or a script of input (--input) -- sent to the graphics
// server's input ring (k_ws.cpp), as the kernel's USB callbacks do (kern/wsrv.h).
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#ifndef ONYXRUN_DISPLAY_H
#define ONYXRUN_DISPLAY_H

#include "onyxrun.h"

struct DisplayOpts
{
	int w = 1280, h = 800;			// the screen (--screen WxH)
	bool headless = false;
	std::string title = "Onyx";
	std::string shot;			// --shot FILE.bmp: the screen written there ...
	int shotAfterMs = 0;			// ... after this long (then the runner ends, status 0)
	std::string input;			// --input SCRIPT: "wait MS; move X Y; down X Y; up X Y; click X Y; key TEXT; ..."
};

bool display_start (const DisplayOpts &o);
void display_size (int *w, int *h);
// The server's pixels (0x00RRGGBB, stride pixels a row) for [x, x + w) x [y, y + h) copied into the screen, shown.
void display_present (const u32 *pixels, int stride, int x, int y, int w, int h);
// The screen as it is now into dst (w x h must be the screen's) -> false: not that size.
bool display_grab (u32 *dst, int w, int h);
bool display_write_bmp (const std::string &path);
// The screen's own memory (host shared memory, 64 KB rounded: *len) -- fullscreen_direct maps it in a program; while
// display_direct (true) the window shows it 60 times a second.
u8 *display_screen_memory (u64 *len);
void display_direct (bool on);

// (k_ws.cpp) the input, as the kernel's feeders give it (kern/wsrv.h)
void ws_input_pointer (int x, int y, unsigned buttons, int wheel);
void ws_input_key (const char *keys);			// the keyboard's cooked string (UTF-8, VT100 escapes)
void ws_input_mods (unsigned mods);			// 1 Ctrl, 2 Shift, 4 Alt, 8 Super
void ws_input_held (int key, bool down);		// a logical key code (key_held's)
void ws_quit_request (void);				// the PC's window closed

#endif
