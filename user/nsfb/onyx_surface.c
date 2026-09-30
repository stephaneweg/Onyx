/*
 * onyx_surface.c -- a libnsfb surface backend for Onyx (NetSurf brick 6).
 *
 * libnsfb is NetSurf's framebuffer abstraction: its software plotters write pixels into
 * a surface's buffer, and a "surface" backend supplies that buffer + flushes it + feeds
 * input. This backend makes the PAGE AREA of NetSurf's Onyx window the libnsfb surface: the
 * window is user/netsurf/onyx_chrome.cpp's (a wtk window, the native toolbar in its top band),
 * the page is its canvas below the band:
 *
 *   - initialise : onyx_chrome_open() -> the page's area in the window's canvas. NetSurf draws
 *                  into a BACK BUFFER of the page's size (nsfb->ptr), never into the canvas:
 *                  a redraw clears then paints, and the compositor -- the apps are preempted
 *                  -- showed those half-drawn states (the page flickered at each restyle).
 *   - update     : the rectangle NetSurf redrew is copied from the back buffer into the
 *                  canvas; the main loop presents the window once an iteration, after its
 *                  redraws (onyx_chrome_flush: a scroll's copy and its new band together).
 *   - input      : the chrome owns the kapi pointer / key handlers; it hands the page's events
 *                  to ours (their y relative to the page), which we translate into a ring of
 *                  nsfb_event_t served to libnsfb's poll-style nsfb_event()/input(). A resize
 *                  of the window (maximise) becomes an NSFB_EVENT_RESIZE.
 *
 * Format: NSFB_FMT_XRGB8888. On little-endian the 32bpp-xrgb8888 plotter packs a colour
 * into a 0x00RRGGBB word -- exactly the Onyx canvas layout, so no R/B swap is needed.
 *
 * This is Onyx glue (kept in the Onyx repo, like onyx_tls.hpp / image.hpp); the vendored
 * libnsfb is UNPATCHED. We register under the name "onyx" with a private type value, so
 * the app resolves it with nsfb_type_from_name("onyx") -- no change to libnsfb's enum.
 * See user/nsfb/README.md.
 */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "libnsfb.h"
#include "libnsfb_plot.h"
#include "libnsfb_event.h"

#include "nsfb.h"
#include "surface.h"
#include "plot.h"

#include "kapi.h"		/* Onyx app ABI: windows, present, input handlers */
#include "netsurf/onyx_chrome.h"	/* the window + its native toolbar */

#define UNUSED(x) ((x) = (x))

/* A private surface-type key (libnsfb looks surfaces up by this value; nothing indexes an
 * array by it -- verified). Sits well clear of the real nsfb_type_e values. */
#define NSFB_SURFACE_ONYX 0x4F4E5958	/* 'ONYX' */

/* Onyx feeds input through callbacks; we buffer translated events in a small ring and let
 * onyx_input() drain it. One app == one window == one surface, so a single static ring is
 * fine (the handlers carry no context pointer). */
#define ONYX_RING 64
static struct {
	nsfb_event_t ev[ONYX_RING];
	volatile int head, tail;
} ring;

static void ring_push(const nsfb_event_t *e)
{
	int n = (ring.head + 1) % ONYX_RING;
	if (n == ring.tail)
		return;			/* full: drop the oldest-but-one (just discard) */
	ring.ev[ring.head] = *e;
	ring.head = n;
}

/* Map an Onyx logical key (kapi.h KEY_*, printable = ASCII) to an nsfb keycode. */
static enum nsfb_key_code_e map_key(long k)
{
	switch (k) {
	case KEY_BACKSPACE:	return NSFB_KEY_BACKSPACE;
	case KEY_TAB:		return NSFB_KEY_TAB;
	case KEY_ENTER:		return NSFB_KEY_RETURN;
	case KEY_UP:		return NSFB_KEY_UP;
	case KEY_DOWN:		return NSFB_KEY_DOWN;
	case KEY_LEFT:		return NSFB_KEY_LEFT;
	case KEY_RIGHT:		return NSFB_KEY_RIGHT;
	case KEY_HOME:		return NSFB_KEY_HOME;
	case KEY_END:		return NSFB_KEY_END;
	case KEY_PGUP:		return NSFB_KEY_PAGEUP;
	case KEY_PGDN:		return NSFB_KEY_PAGEDOWN;
	case KEY_DEL:		return NSFB_KEY_DELETE;
	default:
		if (k > 0 && k < 0x100)
			return (enum nsfb_key_code_e) k;	/* printable ASCII coincides */
		return NSFB_KEY_UNKNOWN;
	}
}

/* kapi pointer handler: value packs (changed<<40)|(buttons<<32)|(x<<16)|y. */
static void onyx_pointer(unsigned long sender, int event, long value)
{
	nsfb_event_t e;
	UNUSED(sender);

	switch (event) {
	case GUI_EVENT_PTR_MOVE:
	case GUI_EVENT_PTR_ENTER:
		e.type = NSFB_EVENT_MOVE_ABSOLUTE;
		e.value.vector.x = GUI_PTR_X(value);
		e.value.vector.y = GUI_PTR_Y(value);
		e.value.vector.z = 0;
		ring_push(&e);
		break;

	case GUI_EVENT_PTR_DOWN:
	case GUI_EVENT_PTR_UP: {
		int ch = GUI_PTR_CHANGED(value);	/* 1 left / 2 right / 4 middle */
		/* tell NetSurf where the button event happened first */
		e.type = NSFB_EVENT_MOVE_ABSOLUTE;
		e.value.vector.x = GUI_PTR_X(value);
		e.value.vector.y = GUI_PTR_Y(value);
		e.value.vector.z = 0;
		ring_push(&e);

		e.type = (event == GUI_EVENT_PTR_DOWN) ? NSFB_EVENT_KEY_DOWN
						       : NSFB_EVENT_KEY_UP;
		if (ch & 1)		e.value.keycode = NSFB_KEY_MOUSE_1;	/* left   */
		else if (ch & 4)	e.value.keycode = NSFB_KEY_MOUSE_2;	/* middle */
		else if (ch & 2)	e.value.keycode = NSFB_KEY_MOUSE_3;	/* right  */
		else			break;
		ring_push(&e);
		break;
	}
	case GUI_EVENT_PTR_WHEEL: {
		/* the wheel: NetSurf scrolls on buttons 4 (up) and 5 (down), 100 px a notch, by
		 * moving the pixels already drawn and drawing the band that comes in (fb_pan) */
		int n = GUI_PTR_WHEEL(value);	/* notches: + forward (up), - back (down) */
		enum nsfb_key_code_e k = (n > 0) ? NSFB_KEY_MOUSE_4 : NSFB_KEY_MOUSE_5;
		if (n < 0)
			n = -n;
		e.type = NSFB_EVENT_MOVE_ABSOLUTE;	/* (where: the element under it may scroll) */
		e.value.vector.x = GUI_PTR_X(value);
		e.value.vector.y = GUI_PTR_Y(value);
		e.value.vector.z = 0;
		ring_push(&e);
		while (n-- > 0) {
			e.type = NSFB_EVENT_KEY_DOWN;
			e.value.keycode = k;
			ring_push(&e);
			e.type = NSFB_EVENT_KEY_UP;
			ring_push(&e);
		}
		break;
	}
	default:
		break;
	}
}

/* kapi key handler: value = ASCII char or KEY_* code. Onyx delivers presses only. */
static void onyx_key(unsigned long sender, int event, long value)
{
	nsfb_event_t e;
	UNUSED(sender);
	if (event != GUI_EVENT_KEY)
		return;
	e.type = NSFB_EVENT_KEY_DOWN;
	e.value.keycode = map_key(value);
	ring_push(&e);
}

/* The page in the window's canvas (its first pixel, its stride in pixels, its size) and the
 * back buffer NetSurf draws into (the page's size, packed rows). */
static unsigned *s_page;
static int s_pstride;
static uint32_t *s_back;
static int s_bw, s_bh;

/* A back buffer for a page of w x h at page / stride (its pixels taken from the canvas: what
 * is on screen stays). nsfb->ptr / linelen / width / height set. */
static int onyx_back(nsfb_t *nsfb, unsigned *page, int stride, int w, int h)
{
	uint32_t *b;
	int y;

	if (w <= 0 || h <= 0)
		return -1;
	b = malloc((size_t) w * (size_t) h * 4);
	if (b == NULL)
		return -1;
	for (y = 0; y < h; y++)
		memcpy(b + (size_t) y * w, page + (size_t) y * stride, (size_t) w * 4);
	free(s_back);
	s_back = b;
	s_bw = w;
	s_bh = h;
	s_page = page;
	s_pstride = stride;
	nsfb->ptr = (uint8_t *) b;
	nsfb->width = w;
	nsfb->height = h;
	nsfb->linelen = w * 4;
	return 0;
}

static int onyx_defaults(nsfb_t *nsfb)
{
	int sw = 800, sh = 600;
	kapi_screen_size(&sw, &sh);
	nsfb->width = sw;
	nsfb->height = sh;
	nsfb->format = NSFB_FMT_XRGB8888;
	select_plotters(nsfb);		/* sets bpp = 32 + the xrgb8888 plotter table */
	return 0;
}

static int
onyx_set_geometry(nsfb_t *nsfb, int width, int height, enum nsfb_format_e format)
{
	if (width > 0)
		nsfb->width = width;
	if (height > 0)
		nsfb->height = height;
	/* this backend only does 32bpp 0x00RRGGBB; ignore other requests */
	if (format == NSFB_FMT_ANY || format == NSFB_FMT_ARGB8888)
		format = NSFB_FMT_XRGB8888;
	nsfb->format = format;
	select_plotters(nsfb);

	if (nsfb->ptr != NULL) {
		/* the window was resized by the chrome (maximise): take the page as it is now */
		int stride, w, h;
		unsigned *c = onyx_chrome_page(&stride, &w, &h);
		if (c != NULL)
			onyx_back(nsfb, c, stride, w, h);	/* (a new back buffer of the new size) */
		return 0;
	}
	nsfb->linelen = (nsfb->width * nsfb->bpp) / 8;
	return 0;
}

static int onyx_initialise(nsfb_t *nsfb)
{
	unsigned *canvas;
	int stride;

	if (nsfb->width <= 0 || nsfb->height <= 0)
		return -1;

	canvas = onyx_chrome_open(nsfb->width, nsfb->height, &stride);
	if (canvas == NULL)
		return -1;

	if (onyx_back(nsfb, canvas, stride, nsfb->width, nsfb->height) != 0)
		return -1;

	ring.head = ring.tail = 0;
	nsfb->surface_priv = &ring;

	onyx_chrome_set_page_handlers(onyx_pointer, onyx_key);
	return 0;
}

static int onyx_finalise(nsfb_t *nsfb)
{
	/* The canvas is owned by the kernel window; it is released when the app exits. The back
	 * buffer is ours. */
	free(s_back);
	s_back = NULL;
	nsfb->ptr = NULL;
	return 0;
}

static int onyx_update(nsfb_t *nsfb, nsfb_bbox_t *box)
{
	int x0 = 0, y0 = 0, x1 = s_bw, y1 = s_bh, y;

	UNUSED(nsfb);
	if (s_back == NULL || s_page == NULL)
		return 0;
	if (box != NULL) {		/* the rectangle redrawn, clipped to the page */
		x0 = box->x0 < 0 ? 0 : box->x0;
		y0 = box->y0 < 0 ? 0 : box->y0;
		x1 = box->x1 > s_bw ? s_bw : box->x1;
		y1 = box->y1 > s_bh ? s_bh : box->y1;
	}
	if (x1 > x0 && y1 > y0) {
		for (y = y0; y < y1; y++)
			memcpy(s_page + (size_t) y * s_pstride + x0,
			       s_back + (size_t) y * s_bw + x0, (size_t) (x1 - x0) * 4);
	}
	onyx_chrome_present_later();	/* (the loop presents: onyx_chrome_flush) */
	return 0;
}

static bool onyx_input(nsfb_t *nsfb, nsfb_event_t *event, int timeout)
{
	UNUSED(nsfb);

	/* dispatch pending Onyx events: the band's to wtk, the page's -> our handlers */
	if (onyx_chrome_pump()) {
		int w, h;
		onyx_chrome_page(NULL, &w, &h);
		event->type = NSFB_EVENT_RESIZE;	/* the window was resized */
		event->value.resize.w = w;
		event->value.resize.h = h;
		return true;
	}

	if (ring.tail == ring.head && timeout != 0) {
		/* nothing yet: wait for an event -- the pointer, a key, a fetch thread's post
		 * (onyx_fetch.c), the close box -- up to the next timer (capped; -1 forever) */
		int slice = (timeout < 0 || timeout > 100) ? 100 : timeout;
		if (onyx_chrome_pump_wait(slice)) {
			int w, h;
			onyx_chrome_page(NULL, &w, &h);
			event->type = NSFB_EVENT_RESIZE;
			event->value.resize.w = w;
			event->value.resize.h = h;
			return true;
		}
	}

	if (ring.tail == ring.head) {
		if (kapi_should_exit()) {	/* window close box -> ask NetSurf to quit */
			event->type = NSFB_EVENT_CONTROL;
			event->value.controlcode = NSFB_CONTROL_QUIT;
			return true;
		}
		return false;
	}

	*event = ring.ev[ring.tail];
	ring.tail = (ring.tail + 1) % ONYX_RING;
	return true;
}

const nsfb_surface_rtns_t onyx_rtns = {
	.defaults = onyx_defaults,
	.initialise = onyx_initialise,
	.finalise = onyx_finalise,
	.input = onyx_input,
	.geometry = onyx_set_geometry,
	.update = onyx_update,
};

NSFB_SURFACE_DEF(onyx, NSFB_SURFACE_ONYX, &onyx_rtns)
