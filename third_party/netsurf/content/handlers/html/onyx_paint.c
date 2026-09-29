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
 * Onyx: CSS3's rounded, graded and shadowed boxes for the HTML redraw -- a box's
 * border-radius (reduced as CSS says when adjacent corners overlap), its box-shadow, and
 * its background gradient: libcss keeps a gradient as the background image's "URL", a
 * canonical text (libcss src/parse/properties/onyx_background.c):
 *
 *   onyx-gradient:<kind>;<param>=<value>;...;S=<stop>,<stop>,...
 *
 *   kind   L linear, R radial, C conic; "rL" etc. when repeating
 *   L      a=<deg> (the angle), or c=<tl|tr|bl|br> (to a corner)
 *   R      sh=<c|e>, sz=<cs|cc|fs|fc> or rx=<len>;ry=<len>, x=<len>;y=<len> (the centre)
 *   C      f=<deg> (from), x=<len>;y=<len>
 *   stop   <AARRGGBB, or "cc" for currentColor>@<len, or "-" for automatic>
 *   len    a number and its unit: % px em rem vw vh pt deg
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "utils/utils.h"
#include "utils/log.h"
#include "netsurf/onyx_paint.h"
#include "css/utils.h"

#include "html/box.h"
#include "html/onyx_paint.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define GRADIENT_PREFIX "onyx-gradient:"

/* ---- radii ------------------------------------------------------------------------------ */

/* exported function documented in html/onyx_paint.h */
bool onyx_box_radii(const css_computed_style *style, const css_unit_ctx *unit_len_ctx,
		float scale, struct onyx_rrect *r)
{
	typedef uint8_t (*radius_fn)(const css_computed_style *, css_fixed *, css_unit *);
	static const radius_fn fn[4] = {
		css_computed_border_top_left_radius,
		css_computed_border_top_right_radius,
		css_computed_border_bottom_right_radius,
		css_computed_border_bottom_left_radius,
	};
	float w = r->x1 - r->x0, h = r->y1 - r->y0, f = 1;
	bool any = false;

	for (int k = 0; k < 4; k++) {
		css_fixed len = 0;
		css_unit unit = CSS_UNIT_PX;

		r->rx[k] = r->ry[k] = 0;
		if (style == NULL || fn[k](style, &len, &unit) != CSS_BORDER_RADIUS_SET ||
		    len <= 0)
			continue;
		if (unit == CSS_UNIT_PCT) {
			r->rx[k] = FIXTOFLT(len) * w / 100;
			r->ry[k] = FIXTOFLT(len) * h / 100;
		} else {
			float px = FIXTOFLT(css_unit_len2device_px(style, unit_len_ctx,
					len, unit)) * scale;
			r->rx[k] = r->ry[k] = px;
		}
		if (r->rx[k] > 0 && r->ry[k] > 0)
			any = true;
	}
	if (!any)
		return false;

	/* the corners on a side may take at most its length: all reduced alike */
#define FIT(len, a, b) do { float s = (a) + (b); \
		if (s > 0 && (len) / s < f) f = (len) / s; } while (0)
	FIT(w, r->rx[ONYX_TL], r->rx[ONYX_TR]);
	FIT(w, r->rx[ONYX_BL], r->rx[ONYX_BR]);
	FIT(h, r->ry[ONYX_TL], r->ry[ONYX_BL]);
	FIT(h, r->ry[ONYX_TR], r->ry[ONYX_BR]);
#undef FIT
	if (f < 1) {
		for (int k = 0; k < 4; k++) {
			r->rx[k] *= f;
			r->ry[k] *= f;
		}
	}
	return true;
}

/* exported function documented in html/onyx_paint.h */
void onyx_rrect_inset(const struct onyx_rrect *o, float l, float t, float rt, float b,
		struct onyx_rrect *i)
{
	i->x0 = o->x0 + l;
	i->y0 = o->y0 + t;
	i->x1 = o->x1 - rt;
	i->y1 = o->y1 - b;
	if (i->x1 < i->x0)
		i->x1 = i->x0;
	if (i->y1 < i->y0)
		i->y1 = i->y0;
	i->rx[ONYX_TL] = o->rx[ONYX_TL] - l;
	i->ry[ONYX_TL] = o->ry[ONYX_TL] - t;
	i->rx[ONYX_TR] = o->rx[ONYX_TR] - rt;
	i->ry[ONYX_TR] = o->ry[ONYX_TR] - t;
	i->rx[ONYX_BR] = o->rx[ONYX_BR] - rt;
	i->ry[ONYX_BR] = o->ry[ONYX_BR] - b;
	i->rx[ONYX_BL] = o->rx[ONYX_BL] - l;
	i->ry[ONYX_BL] = o->ry[ONYX_BL] - b;
	for (int k = 0; k < 4; k++) {
		if (i->rx[k] < 0 || i->ry[k] < 0)
			i->rx[k] = i->ry[k] = 0;
	}
}

/* exported function documented in html/onyx_paint.h */
void onyx_rrect_outset(const struct onyx_rrect *o, float d, struct onyx_rrect *r)
{
	r->x0 = o->x0 - d;
	r->y0 = o->y0 - d;
	r->x1 = o->x1 + d;
	r->y1 = o->y1 + d;
	for (int k = 0; k < 4; k++) {
		/* (a sharp corner stays sharp; a round one grows with the spread) */
		r->rx[k] = o->rx[k] > 0 ? o->rx[k] + d : 0;
		r->ry[k] = o->ry[k] > 0 ? o->ry[k] + d : 0;
		if (r->rx[k] < 0 || r->ry[k] < 0)
			r->rx[k] = r->ry[k] = 0;
	}
	if (r->x1 < r->x0)
		r->x1 = r->x0 = (o->x0 + o->x1) / 2;
	if (r->y1 < r->y0)
		r->y1 = r->y0 = (o->y0 + o->y1) / 2;
}

/* ---- box-shadow ------------------------------------------------------------------------- */

/* exported function documented in html/onyx_paint.h */
bool onyx_box_shadow(const css_computed_style *style, const css_unit_ctx *unit_len_ctx,
		float scale, struct onyx_box_shadow *sh)
{
	css_fixed x = 0, y = 0, blur = 0, spread = 0;
	css_unit xu = CSS_UNIT_PX, yu = CSS_UNIT_PX, bu = CSS_UNIT_PX, su = CSS_UNIT_PX;
	css_color colour = 0;
	uint8_t type;

	if (style == NULL)
		return false;
	type = css_computed_box_shadow(style, &x, &xu, &y, &yu, &blur, &bu,
			&spread, &su, &colour);
	if (type != CSS_BOX_SHADOW_SET && type != CSS_BOX_SHADOW_SET_INSET)
		return false;
	if ((colour >> 24) == 0)
		return false;		/* (transparent) */
	sh->inset = type == CSS_BOX_SHADOW_SET_INSET;
	sh->x = FIXTOFLT(css_unit_len2device_px(style, unit_len_ctx, x, xu)) * scale;
	sh->y = FIXTOFLT(css_unit_len2device_px(style, unit_len_ctx, y, yu)) * scale;
	sh->blur = FIXTOFLT(css_unit_len2device_px(style, unit_len_ctx, blur, bu)) * scale;
	sh->spread = FIXTOFLT(css_unit_len2device_px(style, unit_len_ctx, spread, su)) *
			scale;
	if (sh->blur < 0)
		sh->blur = 0;
	sh->colour = nscss_color_to_ns(colour);
	return true;
}

/* ---- gradients -------------------------------------------------------------------------- */

/* exported function documented in html/onyx_paint.h */
const char *onyx_background_gradient(const css_computed_style *style)
{
	lwc_string *img = NULL;
	const char *s;

	if (style == NULL ||
	    css_computed_background_image(style, &img) != CSS_BACKGROUND_IMAGE_IMAGE ||
	    img == NULL)
		return NULL;
	s = lwc_string_data(img);
	if (strncmp(s, GRADIENT_PREFIX, sizeof GRADIENT_PREFIX - 1) != 0)
		return NULL;
	return s + sizeof GRADIENT_PREFIX - 1;
}

/* exported function documented in html/onyx_paint.h */
bool onyx_is_gradient_url(const char *url)
{
	return url != NULL &&
		strncmp(url, GRADIENT_PREFIX, sizeof GRADIENT_PREFIX - 1) == 0;
}

/** A length of the spec: its value and unit; `*end` past it. */
struct glen {
	float v;
	char unit[4];		/* "%", "px", "em", "rem", "vw", "vh", "pt", "deg", "" */
};

static const char *glen_parse(const char *p, struct glen *l)
{
	char *e;
	int n = 0;

	l->v = strtof(p, &e);
	if (e == p)
		return NULL;
	p = e;
	while (n < 3 && ((*p >= 'a' && *p <= 'z') || *p == '%'))
		l->unit[n++] = *p++;
	l->unit[n] = '\0';
	return p;
}

/** A length in px (a percentage of `ref`; an angle as is). */
static float glen_px(const struct glen *l, float ref, const css_computed_style *style,
		const css_unit_ctx *u, float scale)
{
	css_unit unit;

	if (strcmp(l->unit, "%") == 0)
		return l->v * ref / 100;
	if (strcmp(l->unit, "px") == 0 || l->unit[0] == '\0')
		return l->v * scale;
	if (strcmp(l->unit, "deg") == 0)
		return l->v;
	if (strcmp(l->unit, "em") == 0)
		unit = CSS_UNIT_EM;
	else if (strcmp(l->unit, "rem") == 0)
		unit = CSS_UNIT_REM;
	else if (strcmp(l->unit, "vw") == 0)
		unit = CSS_UNIT_VW;
	else if (strcmp(l->unit, "vh") == 0)
		unit = CSS_UNIT_VH;
	else if (strcmp(l->unit, "pt") == 0)
		unit = CSS_UNIT_PT;
	else
		return l->v * scale;
	return FIXTOFLT(css_unit_len2device_px(style, u, FLTTOFIX(l->v), unit)) * scale;
}

static uint32_t hex8(const char *p)
{
	uint32_t v = 0;
	for (int i = 0; i < 8; i++) {
		char c = p[i];
		v <<= 4;
		if (c >= '0' && c <= '9') v |= c - '0';
		else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
	}
	return v;
}

/* exported function documented in html/onyx_paint.h */
bool onyx_gradient_resolve(const char *spec, const css_computed_style *style,
		const css_unit_ctx *u, float scale,
		float bx, float by, float bw, float bh, struct onyx_gradient *g)
{
	const char *p = spec;
	float angle = 180;		/* linear: "to bottom" */
	char corner[3] = "";
	bool circle = false;		/* radial: an ellipse */
	char size[3] = "fc";		/* radial: farthest-corner */
	struct glen rx = { -1, "" }, ry = { -1, "" };
	struct glen cx = { 50, "%" }, cy = { 50, "%" };
	float from = 0;
	struct glen spos[ONYX_GRAD_MAX_STOPS];
	bool sauto[ONYX_GRAD_MAX_STOPS];
	float len = 1;			/* the gradient ray's length, px (linear, radial) */
	css_color current = 0;

	memset(g, 0, sizeof *g);
	if (spec == NULL || bw <= 0 || bh <= 0)
		return false;

	/* the kind */
	if (*p == 'r') {
		g->repeating = true;
		p++;
	}
	switch (*p) {
	case 'L': g->kind = ONYX_GRAD_LINEAR; break;
	case 'R': g->kind = ONYX_GRAD_RADIAL; break;
	case 'C': g->kind = ONYX_GRAD_CONIC; break;
	default: return false;
	}
	p++;

	/* the parameters, up to the stops */
	while (*p == ';') {
		const char *k = ++p, *v;
		size_t kl;

		while (*p != '\0' && *p != '=' && *p != ';')
			p++;
		if (*p != '=')
			return false;
		kl = p - k;
		v = ++p;
		if (kl == 1 && *k == 'S')
			break;			/* the stops */
		while (*p != '\0' && *p != ';')
			p++;
		if (kl == 1 && *k == 'a') {
			angle = strtof(v, NULL);
		} else if (kl == 1 && *k == 'c') {
			corner[0] = v[0];
			corner[1] = v[1];
		} else if (kl == 2 && strncmp(k, "sh", 2) == 0) {
			circle = v[0] == 'c';
		} else if (kl == 2 && strncmp(k, "sz", 2) == 0) {
			size[0] = v[0];
			size[1] = v[1];
		} else if (kl == 2 && strncmp(k, "rx", 2) == 0) {
			glen_parse(v, &rx);
		} else if (kl == 2 && strncmp(k, "ry", 2) == 0) {
			glen_parse(v, &ry);
		} else if (kl == 1 && *k == 'x') {
			glen_parse(v, &cx);
		} else if (kl == 1 && *k == 'y') {
			glen_parse(v, &cy);
		} else if (kl == 1 && *k == 'f') {
			from = strtof(v, NULL);
		}
	}

	/* the stops */
	if (style != NULL)
		css_computed_color(style, &current);
	while (*p != '\0' && g->nstops < ONYX_GRAD_MAX_STOPS) {
		int n = g->nstops;
		if (p[0] == 'c' && p[1] == 'c') {
			g->argb[n] = current;
			p += 2;
		} else {
			if (strlen(p) < 8)
				break;
			g->argb[n] = hex8(p);
			p += 8;
		}
		if (*p != '@')
			break;
		p++;
		if (*p == '-') {
			sauto[n] = true;
			p++;
		} else {
			const char *e = glen_parse(p, &spos[n]);
			if (e == NULL)
				break;
			sauto[n] = false;
			p = e;
		}
		g->nstops++;
		if (*p == ',')
			p++;
		else
			break;
	}
	if (g->nstops == 0)
		return false;

	/* the geometry */
	switch (g->kind) {
	case ONYX_GRAD_LINEAR: {
		float a, dx, dy, c, s;
		if (corner[0] != '\0') {
			/* to a corner: perpendicular to the line through the two others */
			float base = atan2f(bh, bw);
			bool top = corner[0] == 't', right = corner[1] == 'r';
			if (top && right) a = base;
			else if (top) a = -base;
			else if (right) a = (float) M_PI - base;
			else a = (float) M_PI + base;
		} else {
			a = angle * (float) M_PI / 180;
		}
		s = sinf(a);
		c = cosf(a);
		dx = s;
		dy = -c;
		len = fabsf(bw * s) + fabsf(bh * c);
		if (len < 1e-3f)
			len = 1e-3f;
		g->x0 = bx + bw / 2 - dx * len / 2;
		g->y0 = by + bh / 2 - dy * len / 2;
		g->dx = dx * len;
		g->dy = dy * len;
		break;
	}
	case ONYX_GRAD_RADIAL: {
		float ccx = bx + glen_px(&cx, bw, style, u, scale);
		float ccy = by + glen_px(&cy, bh, style, u, scale);
		float dl = fabsf(ccx - bx), dr = fabsf(bx + bw - ccx);
		float dt = fabsf(ccy - by), db = fabsf(by + bh - ccy);
		float hx, hy;
		g->cx = ccx;
		g->cy = ccy;
		if (rx.v >= 0) {
			hx = glen_px(&rx, bw, style, u, scale);
			hy = ry.v >= 0 ? glen_px(&ry, bh, style, u, scale) : hx;
		} else {
			bool closest = size[0] == 'c', side = size[1] == 's';
			hx = closest ? fminf(dl, dr) : fmaxf(dl, dr);
			hy = closest ? fminf(dt, db) : fmaxf(dt, db);
			if (circle) {
				if (side) {
					hx = hy = closest ? fminf(hx, hy) : fmaxf(hx, hy);
				} else {
					hx = hy = sqrtf(hx * hx + hy * hy);
				}
			} else if (!side) {
				hx *= 1.41421356f;
				hy *= 1.41421356f;
			}
		}
		if (hx < 0.5f) hx = 0.5f;
		if (hy < 0.5f) hy = 0.5f;
		g->rx = hx;
		g->ry = hy;
		len = hx;
		break;
	}
	case ONYX_GRAD_CONIC:
		g->cx = bx + glen_px(&cx, bw, style, u, scale);
		g->cy = by + glen_px(&cy, bh, style, u, scale);
		g->from = from / 360;
		len = 1;
		break;
	}

	/* the stops' positions, as t: the known ones, then the automatic ones */
	for (int i = 0; i < g->nstops; i++) {
		if (sauto[i])
			continue;
		if (g->kind == ONYX_GRAD_CONIC) {
			g->pos[i] = strcmp(spos[i].unit, "%") == 0 ? spos[i].v / 100
								   : spos[i].v / 360;
		} else {
			g->pos[i] = glen_px(&spos[i], len, style, u, scale) / len;
		}
	}
	if (sauto[0]) {
		g->pos[0] = 0;
		sauto[0] = false;
	}
	if (sauto[g->nstops - 1]) {
		g->pos[g->nstops - 1] = 1;
		sauto[g->nstops - 1] = false;
	}
	for (int i = 1; i < g->nstops; i++) {
		/* (increasing: a stop before the one before is moved to it) */
		if (!sauto[i] && g->pos[i] < g->pos[i - 1])
			g->pos[i] = g->pos[i - 1];
	}
	for (int i = 1; i < g->nstops - 1; i++) {
		int j;
		if (!sauto[i])
			continue;
		for (j = i; j < g->nstops && sauto[j]; j++)
			;
		for (int k = i; k < j; k++)
			g->pos[k] = g->pos[i - 1] + (g->pos[j] - g->pos[i - 1]) *
					(k - i + 1) / (j - i + 1);
		i = j - 1;
	}
	return true;
}
