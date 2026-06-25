/*
 * nsfbdemo -- proof that libnsfb draws into an Onyx window through the Onyx surface
 * backend (user/nsfb/onyx_surface.c, NetSurf brick 6).
 *
 * Opens an "onyx" libnsfb surface (== an Onyx window content canvas), draws a few shapes
 * with libnsfb's own software plotters, presents, then runs an event loop: the cursor
 * leaves a trail and a click drops a marker, proving input flows kapi -> libnsfb. Quit
 * with 'q' / Esc or the window close box.
 *
 * Newlib app linking libnsfb (+ libm). OPT-IN: build the lib once (make -C user/nsfb),
 * then  make -C user/bin NSFB_DIR=../nsfb nsfbdemo.elf   (see user/nsfb/README.md).
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "libnsfb.h"
#include "libnsfb_plot.h"
#include "libnsfb_event.h"

/* nsfb_colour_t is ABGR (red in the low byte); full alpha = opaque. */
#define RGB(r, g, b)	(0xFF000000u | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))

#define W 480
#define H 320

static void marker(nsfb_t *nsfb, int x, int y, int s, nsfb_colour_t c)
{
	nsfb_bbox_t r = { x - s, y - s, x + s, y + s };
	nsfb_bbox_t all = { 0, 0, W, H };
	nsfb_claim(nsfb, &r);
	nsfb_plot_rectangle_fill(nsfb, &r, c);
	nsfb_update(nsfb, &all);
}

int main(void)
{
	nsfb_t *nsfb;
	nsfb_event_t e;
	nsfb_bbox_t screen = { 0, 0, W, H };
	nsfb_plot_pen_t pen;
	int running = 1;
	int last_x = W / 2, last_y = H / 2;

	nsfb = nsfb_new(nsfb_type_from_name("onyx"));
	if (nsfb == NULL) {
		printf("nsfbdemo: no onyx surface registered\n");
		return 1;
	}
	nsfb_set_geometry(nsfb, W, H, NSFB_FMT_XRGB8888);
	if (nsfb_init(nsfb) != 0) {
		printf("nsfbdemo: nsfb_init failed\n");
		return 1;
	}

	/* one frame of static content via libnsfb's plotters */
	nsfb_claim(nsfb, &screen);
	nsfb_plot_clg(nsfb, RGB(28, 28, 40));

	nsfb_bbox_t box = { 40, 40, 220, 150 };
	nsfb_plot_rectangle_fill(nsfb, &box, RGB(210, 70, 70));		/* red rectangle */

	nsfb_bbox_t outline = { 250, 40, 440, 150 };
	nsfb_plot_rectangle(nsfb, &outline, 3, RGB(90, 160, 230), false, false);

	nsfb_bbox_t ell = { 60, 180, 200, 290 };
	nsfb_plot_ellipse_fill(nsfb, &ell, RGB(90, 200, 130));		/* green ellipse */

	pen.stroke_type = NFSB_PLOT_OPTYPE_SOLID;
	pen.stroke_width = 2;
	pen.stroke_colour = RGB(240, 220, 90);
	pen.stroke_pattern = 0;
	pen.fill_type = NFSB_PLOT_OPTYPE_NONE;
	pen.fill_colour = 0;
	nsfb_bbox_t line = { 250, 280, 440, 180 };
	nsfb_plot_line(nsfb, &line, &pen);				/* yellow line */

	nsfb_update(nsfb, &screen);
	printf("nsfbdemo: window up (%dx%d). move/click to draw; q or Esc to quit.\n", W, H);

	while (running) {
		if (!nsfb_event(nsfb, &e, 50))
			continue;	/* timed out with nothing -- keep polling */

		switch (e.type) {
		case NSFB_EVENT_CONTROL:
			if (e.value.controlcode == NSFB_CONTROL_QUIT)
				running = 0;
			break;
		case NSFB_EVENT_KEY_DOWN:
			if (e.value.keycode == NSFB_KEY_ESCAPE || e.value.keycode == NSFB_KEY_q)
				running = 0;
			else if (e.value.keycode == NSFB_KEY_MOUSE_1)
				marker(nsfb, last_x, last_y, 5, RGB(255, 255, 255));
			break;
		case NSFB_EVENT_MOVE_ABSOLUTE:
			last_x = e.value.vector.x;
			last_y = e.value.vector.y;
			marker(nsfb, last_x, last_y, 1, RGB(120, 200, 255));
			break;
		default:
			break;
		}
	}

	nsfb_free(nsfb);
	printf("nsfbdemo: bye\n");
	return 0;
}
