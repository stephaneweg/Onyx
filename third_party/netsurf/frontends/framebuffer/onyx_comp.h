/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Onyx: the browser's view composited (GPU compositing, stage 2 -- onyx_comp.c; docs/06 §25).
 *
 * The page is painted into a band taller than the view (a ring of document rows) and the
 * compositing layers the redraw promotes (opacity, transforms: netsurf/onyx_paint.h's retained
 * layers) into buffers of their own; all are kept between redraws, uploaded as textures of
 * user/gpucomp where they were damaged, and each frame is one gpc_composite of the band's rows
 * in view and the layers (matrix, clip, opacity) into the window's canvas. Scrolling moves the
 * band's rows: nothing is painted again.
 */

#ifndef NETSURF_FB_ONYX_COMP_H
#define NETSURF_FB_ONYX_COMP_H

#include <stdbool.h>
#include <stdint.h>

struct browser_window;
struct onyx_layer;

/** What the compositor shows: the browser widget's view. */
struct onyx_comp_view {
	struct browser_window *bw;
	int x, y, w, h;		/* the view in the surface (the browser widget), px */
	int sx, sy;		/* its scroll offsets (document px at the view's top left) */
	int dy;			/* the last scroll's direction (rows painted ahead that way) */
	bool caret;		/* the caret: a line of the document at cx, cy, ch high */
	int cx, cy, ch;
};

/**
 * Start (after the surface is up): the Choices option gpu_compositing (NS_GPU=0 / 1 / cpu on
 * the PC bench), the GPU's self-test (a small scene composited by the GPU and by gpucomp's CPU
 * path: a mismatch, or the GPU lost, and the CPU path is used for the session).
 */
void onyx_comp_init(void);

/** How the compositor asks for a frame (gui.c: the browser widget redrawn). */
void onyx_comp_set_request(void (*request)(void));
void onyx_comp_finalise(void);

/** Whether the view is composited (else: the back buffer, as ever). */
bool onyx_comp_on(void);

/** A rectangle of the document to paint again (document px). */
void onyx_comp_damage(int x0, int y0, int x1, int y1);

/** The whole document to paint again (the view first, the rest of the band later). */
void onyx_comp_damage_all(void);

/** Onyx (docs/06 §32): the next frame composites the whole view again (a dialog drawn over
 * the page closed: the canvas holds it) -- else only what changed since the last frame. */
void onyx_comp_present_all(void);

/** A new page in the window: the retained layers (the page before's) dropped, all painted
 * again. */
void onyx_comp_page_changed(void);

/**
 * The view: its rows painted where they are not yet, the damage painted, the frame composited
 * into the canvas. prepaint: an idle turn -- a piece of the band out of view painted ahead.
 * Returns true when rows out of view are still to be painted (schedule a prepaint turn).
 */
bool onyx_comp_redraw(const struct onyx_comp_view *v, bool prepaint);

/**
 * A retained layer's properties changed, nothing else (its transform or its opacity: an
 * animation, a hover): the frame is composited again, nothing painted. key: the layer's
 * (netsurf/onyx_paint.h, struct onyx_layer's key); m: its matrix about its own origin, as
 * onyx_layer's lm (NULL: none); returns false when that is not possible (no such layer, or
 * it would cover what is painted over it) -- the caller then redraws its rectangles.
 */
bool onyx_comp_layer_update(const void *key, const float *lm, float opacity);

/* ---- for the plotters (framebuffer.c, onyx_layer.c) ---- */

/** A plot operation's bounds on the current surface (the conflicts with the layers). */
void onyx_comp_note(struct nsfb_s *surface, int x0, int y0, int x1, int y1);
extern bool onyx_comp_noting;	/* (a composited redraw is going on: note the operations) */

/** A retained layer offered (onyx_layer_begin, pass ONYX_LAYER_OFFER): accepted (true, and
 * l->r* the part of it to paint, target px), or to be painted as ever (false). */
bool onyx_comp_offer(struct onyx_layer *l);
/** Its offer ends (onyx_layer_end, ONYX_LAYER_OFFER). */
void onyx_comp_offer_end(const struct onyx_layer *l);
/** A part of its pixels painted (the passes' result, premultiplied 0xAARRGGBB, w x h at
 * target px x0, y0): kept and uploaded. */
void onyx_comp_store(const struct onyx_layer *l, const uint32_t *argb, int x0, int y0,
		int w, int h);

#endif
