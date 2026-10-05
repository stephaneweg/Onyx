// onyxsurface.c -- the kernel's shared surfaces and the screen's size for WebKit's compositor
// (USE(GRAPHICS_LAYER_ONYX): Source/WebKit/Shared/onyx/OnyxSurface.h declares these functions; the
// web process composites a page into a surface, the UI process shows it). WebKit's own files do
// not include appkit.h (the newlib apps' header): this file does, and is linked with the program
// beside user/gpucomp/gpucomp.c (build-web.sh, build-wk2test.sh).
//
// A surface is mapped once a process: the kernel gives a new place in the address space at every
// kapi_surface_map and never takes one back (kern/layout.h: USER_SURFACE_BASE).
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see fetch.sh).
#include "appkit/appkit.h"

#define MAPS 64					// the kernel's MAX_SURFACES

static struct { int id; unsigned *px; } s_maps[MAPS];

void onyx_screen_size (int *w, int *h)
{
	int sw = 0, sh = 0;
	kapi_screen_size (&sw, &sh);
	if (w) *w = sw;
	if (h) *h = sh;
}

int onyx_surface_create (int w, int h)
{
	if (w <= 0 || h <= 0) return 0;
	int id = kapi_surface_create (w, h);
	return id > 0 ? id : 0;
}

unsigned *onyx_surface_map (int id, int *w, int *h)
{
	int sw = 0, sh = 0, slot = -1;
	unsigned *px = 0;
	if (id <= 0 || !kapi_surface_size (id, &sw, &sh) || sw <= 0 || sh <= 0) return 0;
	for (int i = 0; i < MAPS; i++)
	{
		if (s_maps[i].id == id) { px = s_maps[i].px; break; }
		if (!s_maps[i].id && slot < 0) slot = i;
	}
	if (!px)
	{
		px = kapi_surface_map (id);
		if (!px) return 0;
		if (slot >= 0) { s_maps[slot].id = id; s_maps[slot].px = px; }
	}
	if (w) *w = sw;
	if (h) *h = sh;
	return px;
}

void onyx_surface_destroy (int id)
{
	if (id <= 0) return;
	kapi_surface_destroy (id);
	// (the mapping stays in this process: the entry is kept, the id is not given again)
}
