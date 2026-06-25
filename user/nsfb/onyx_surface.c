/*
 * onyx_surface.c -- a libnsfb surface backend for Onyx (NetSurf brick 6).
 *
 * libnsfb is NetSurf's framebuffer abstraction: its software plotters write pixels into
 * a surface's buffer, and a "surface" backend supplies that buffer + flushes it + feeds
 * input. This backend makes the Onyx window CONTENT CANVAS the libnsfb surface:
 *
 *   - initialise : kapi_create_window() -> the 0x00RRGGBB canvas IS nsfb->ptr.
 *   - update     : kapi_present() flushes the canvas to screen.
 *   - input      : Onyx delivers input via CALLBACKS (kapi_set_pointer/key_handler +
 *                  kapi_pump_events); we translate them into a ring of nsfb_event_t and
 *                  serve libnsfb's poll-style nsfb_event()/input() from it.
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

#include "libnsfb.h"
#include "libnsfb_plot.h"
#include "libnsfb_event.h"

#include "nsfb.h"
#include "surface.h"
#include "plot.h"

#include "kapi.h"		/* Onyx app ABI: windows, present, input handlers */

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

	if (nsfb->ptr != NULL) {		/* window already up -> resize it */
		unsigned *c = kapi_resize_window(nsfb->width, nsfb->height);
		if (c != NULL)
			nsfb->ptr = (uint8_t *) c;
	}
	nsfb->linelen = (nsfb->width * nsfb->bpp) / 8;
	return 0;
}

static int onyx_initialise(nsfb_t *nsfb)
{
	unsigned *canvas;

	if (nsfb->width <= 0 || nsfb->height <= 0)
		return -1;

	canvas = kapi_create_window(nsfb->width, nsfb->height, "NetSurf");
	if (canvas == NULL)
		return -1;

	nsfb->ptr = (uint8_t *) canvas;
	nsfb->linelen = (nsfb->width * nsfb->bpp) / 8;	/* bpp set by select_plotters */

	ring.head = ring.tail = 0;
	nsfb->surface_priv = &ring;

	kapi_set_pointer_handler(onyx_pointer);
	kapi_set_key_handler(onyx_key);
	return 0;
}

static int onyx_finalise(nsfb_t *nsfb)
{
	/* The canvas is owned by the kernel window; it is released when the app exits.
	 * Just drop our reference so the core does not free() a non-malloc'd pointer. */
	nsfb->ptr = NULL;
	return 0;
}

static int onyx_update(nsfb_t *nsfb, nsfb_bbox_t *box)
{
	UNUSED(nsfb);
	UNUSED(box);			/* no dirty-rect kapi yet: present the whole canvas */
	kapi_present();
	return 0;
}

static bool onyx_input(nsfb_t *nsfb, nsfb_event_t *event, int timeout)
{
	UNUSED(nsfb);

	kapi_pump_events();		/* dispatch pending Onyx events -> our handlers */

	if (ring.tail == ring.head && timeout != 0) {
		/* nothing yet: wait a little (capped) and pump again. -1 == forever. */
		int slice = (timeout < 0 || timeout > 20) ? 20 : timeout;
		kapi_msleep(slice);
		kapi_pump_events();
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
