// skiademo -- the visual check of Onyx's Skia port on the Pi (tools/ports/skia; docs/04): the scene of
// scene.h (the one skiatest checks) rendered by Skia's CPU raster back end straight into a window's canvas
// (raw kapi: uk_win_create's 0x00RRGGBB buffer wrapped as an N32 SkSurface), with the render time.
// Space draws it again (the time of a second, warm render); Esc or the close box quits.
//
//   skiademo [font directory]          default SD:/res/fonts/
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is hereby granted, free
// of charge, to any person obtaining a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice
// and this permission notice shall be included in all copies or substantial portions of the Software. THE
// SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
#include "scene.h"
#include "appkit/appkit.h"
#include "uikit/win.h"		// the window API (UIKit's: uk_win_*)
#include <stdio.h>
#include <time.h>

#define W 800
#define H 600

static unsigned *g_fb;
static sk_sp<SkFontMgr> g_mgr;
static bool g_redraw = true, g_quit = false;
static int g_frames;

static double now_ms ()
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static void render ()
{
	// the window's canvas is 0x00RRGGBB per pixel: in memory B, G, R, X -- Skia's N32 here (SK_R32_SHIFT=16)
	SkImageInfo info = SkImageInfo::MakeN32 (W, H, kOpaque_SkAlphaType);
	sk_sp<SkSurface> s = SkSurfaces::WrapPixels (info, g_fb, W * 4);
	if (!s)
		return;
	double t0 = now_ms ();
	scene::Result r = scene::draw (s->getCanvas (), W, H, g_mgr);
	double t1 = now_ms ();
	g_frames++;
	char line[160];
	snprintf (line, sizeof line, "Skia m%d, CPU raster: %.0f ms (render %d)  --  Space: again, Esc: quit", SK_MILESTONE, t1 - t0, g_frames);
	sk_sp<SkTypeface> tf = g_mgr ? g_mgr->matchFamilyStyle ("sans-serif", SkFontStyle::Normal ()) : nullptr;
	if (tf) {
		scene::Shaped sh = scene::shape (tf, 14, line);
		scene::draw_text (s->getCanvas (), tf, 14, sh, 12, H - 8, 0xffe0e0e0);
	}
	printf ("skiademo: %s (%d font families)\n", line, r.families);
}

static void on_key (unsigned long, int ev, long key)
{
	if (ev != GUI_EVENT_KEY)
		return;
	if (key == ' ')
		g_redraw = true;
	else if (key == 27 || key == 'q')
		g_quit = true;
}

int main (int argc, char **argv)
{
	const char *fonts = argc > 1 ? argv[1] : "SD:/res/fonts/";
	g_fb = uk_win_create (W, H, "Skia demo");
	if (!g_fb) {
		fprintf (stderr, "skiademo: no window\n");
		return 1;
	}
	g_mgr = SkFontMgr_New_Onyx (fonts);
	uk_win_on_key (on_key);
	while (!g_quit && !kapi_should_exit ()) {
		kapi_pump_events ();
		if (g_redraw) {
			g_redraw = false;
			render ();
			uk_win_present ();
		}
		kapi_msleep (30);
	}
	return 0;
}
