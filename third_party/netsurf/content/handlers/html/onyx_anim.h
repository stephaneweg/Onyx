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
 * Onyx: CSS transitions, CSS animations, the Web Animations API and the animation frames
 * (requestAnimationFrame) of an HTML content -- one timeline (html/onyx_anim.c).
 */

#ifndef NETSURF_HTML_ONYX_ANIM_H
#define NETSURF_HTML_ONYX_ANIM_H

#include <stdbool.h>

struct html_content;
struct dom_node;
struct box;
struct css_select_results;
struct css_computed_style;

/**
 * The style selection's hook (box_construct.c box_get_style): an element's new style.
 * Transitions start when an animatable property changed, CSS animations when their names
 * appear; the result's style replaced by the animated one (the element's base style kept
 * here). Nothing for a page without transitions or animations.
 */
void onyx_anim_styled(struct html_content *c, struct dom_node *n,
		struct css_select_results *res, const struct css_computed_style *parent_style);

/** A rebox begins: the old boxes' styles noted (the styles transitions start from). */
void onyx_anim_rebox_begin(struct html_content *c, struct box *old_layout);

/** The rebox is over: the elements it did not style (removed, not displayed) stop. */
void onyx_anim_rebox_end(struct html_content *c, bool success);

/** The content is destroyed: its animations freed. */
void onyx_anim_fini(struct html_content *c);

/** The scripts read styles or geometry: what they changed of the animations shown first. */
void onyx_anim_flush(struct html_content *c);

/** The scripts asked for an animation frame (requestAnimationFrame). */
void onyx_anim_request_frame(struct html_content *c);

/** Whether the element (or, n NULL, the document) has animations running. */
bool onyx_anim_running(struct html_content *c);

/* ---- the Web Animations API (the natives of quickjs/qjs.c) ---- */

/** An effect's timing (ms), as element.animate()'s options give it. */
struct onyx_anim_timing {
	double delay, end_delay, duration, iterations, iteration_start, rate;
	int direction;		/* 0 normal, 1 reverse, 2 alternate, 3 alternate-reverse */
	int fill;		/* 0 none, 1 forwards, 2 backwards, 3 both (auto: none) */
	const char *easing;	/* a CSS <easing-function> text, or NULL (linear) */
};

/**
 * element.animate(): keyframes as CSS declaration texts at offsets (0..1), each with its
 * easing (NULL: linear), playing now. Its id (> 0), 0 if it could not be made.
 */
int onyx_anim_create(struct html_content *c, struct dom_node *n, const char *const *css,
		const double *offsets, const char *const *easings, int nkf,
		const struct onyx_anim_timing *timing);

enum onyx_anim_op {
	ONYX_ANIM_PLAY, ONYX_ANIM_PAUSE, ONYX_ANIM_CANCEL, ONYX_ANIM_FINISH, ONYX_ANIM_REVERSE,
	ONYX_ANIM_SET_TIME, ONYX_ANIM_SET_RATE, ONYX_ANIM_SET_START, ONYX_ANIM_COMMIT
};

/** An animation (any kind) controlled; false if it is gone. */
bool onyx_anim_control(struct html_content *c, int id, enum onyx_anim_op op, double arg);

/** What the scripts read of an animation. */
struct onyx_anim_info {
	double current_time;	/* ms, NAN: unresolved */
	double start_time;	/* ms on the document's timeline, NAN: unresolved */
	double rate;
	double end_time;	/* delay + active duration + end delay (INFINITY) */
	double progress;	/* the iteration's progress, NAN: no effect now */
	double iteration;	/* the current iteration, NAN: none */
	int kind;		/* 0 transition, 1 CSS animation, 2 a script's */
	const char *play_state;	/* "idle", "running", "paused", "finished" */
	const char *name;	/* the transition's property, the CSS animation's name */
	struct dom_node *node;
	double delay, duration, iterations, end_delay;
	int direction, fill;
};

bool onyx_anim_info(struct html_content *c, int id, struct onyx_anim_info *out);

/**
 * The ids of the animations of n (NULL: of the document), in composite order
 * (transitions, CSS animations, the scripts'): how many (up to max).
 */
int onyx_anim_list(struct html_content *c, struct dom_node *n, int *ids, int max);

/**
 * getComputedStyle's transform: the canonical transform text of a style as a 2D matrix,
 * lengths against a w x h box ("none" when there is none).
 */
void onyx_anim_transform_text(const struct css_computed_style *style,
		const struct html_content *c, float w, float h, char *buf, int len);

#endif
