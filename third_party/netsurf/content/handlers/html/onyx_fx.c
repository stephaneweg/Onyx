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
 * Onyx: a box's compositing effects (html/onyx_fx.h) -- the transform functions, the
 * transform-origin, the filters, the blend mode, read from libcss's canonical texts
 * (libcss src/parse/properties/onyx_css3b.c), and the geometry the layout, the hit test and
 * the scripts need to see a box where it is painted.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/utils.h"
#include "utils/errors.h"
#include "netsurf/content.h"
#include "netsurf/onyx_paint.h"
#include "content/content.h"
#include "css/utils.h"
#include "html/box.h"
#include "html/private.h"
#include "html/box_inspect.h"
#include "html/onyx_paint.h"
#include "html/onyx_fx.h"

#define DEG (3.14159265358979f / 180)

/* ---- lengths of the canonical texts --------------------------------------------------- */

/** A value's length in px ("12px", "50%" of ref, "1.5em", "3vw"...; an angle's degrees; a
 * plain number as is); p moved past it. */
static float fx_len(const char **pp, float ref, const css_computed_style *style,
		const css_unit_ctx *u, float scale)
{
	const char *p = *pp;
	char *e;
	float v = strtof(p, &e);
	css_unit unit;

	p = e;
	if (*p == '%') {
		*pp = p + 1;
		return v * ref / 100;
	}
	if (strncmp(p, "px", 2) == 0) {
		*pp = p + 2;
		return v * scale;
	}
	if (strncmp(p, "deg", 3) == 0) {
		*pp = p + 3;
		return v;
	}
	if (strncmp(p, "rem", 3) == 0) {
		unit = CSS_UNIT_REM;
		p += 3;
	} else if (strncmp(p, "em", 2) == 0) {
		unit = CSS_UNIT_EM;
		p += 2;
	} else if (strncmp(p, "vw", 2) == 0) {
		unit = CSS_UNIT_VW;
		p += 2;
	} else if (strncmp(p, "vh", 2) == 0) {
		unit = CSS_UNIT_VH;
		p += 2;
	} else {
		*pp = p;
		return v * scale;	/* (a number) */
	}
	*pp = p;
	if (style == NULL || u == NULL)
		return v * 16 * scale;
	return FIXTOFLT(css_unit_len2device_px(style, u, FLTTOFIX(v), unit)) * scale;
}

/** The next argument: past a ',' (and spaces). */
static const char *fx_next(const char *p)
{
	while (*p == ' ')
		p++;
	if (*p == ',')
		p++;
	while (*p == ' ')
		p++;
	return p;
}

/* ---- matrices -------------------------------------------------------------------------- */

/* exported function documented in html/onyx_fx.h */
void onyx_matrix_mul(const float p[6], const float q[6], float r[6])
{
	float t[6];

	t[0] = p[0] * q[0] + p[2] * q[1];
	t[1] = p[1] * q[0] + p[3] * q[1];
	t[2] = p[0] * q[2] + p[2] * q[3];
	t[3] = p[1] * q[2] + p[3] * q[3];
	t[4] = p[0] * q[4] + p[2] * q[5] + p[4];
	t[5] = p[1] * q[4] + p[3] * q[5] + p[5];
	memcpy(r, t, sizeof(t));
}

/* exported function documented in html/onyx_fx.h */
bool onyx_matrix_invert(const float m[6], float inv[6])
{
	float det = m[0] * m[3] - m[1] * m[2];

	if (fabsf(det) < 1e-9f)
		return false;
	inv[0] = m[3] / det;
	inv[1] = -m[1] / det;
	inv[2] = -m[2] / det;
	inv[3] = m[0] / det;
	inv[4] = -(inv[0] * m[4] + inv[2] * m[5]);
	inv[5] = -(inv[1] * m[4] + inv[3] * m[5]);
	return true;
}

/* exported function documented in html/onyx_fx.h */
void onyx_matrix_bbox(const float m[6], float x0, float y0, float x1, float y1,
		float *bx0, float *by0, float *bx1, float *by1)
{
	float xs[4] = { x0, x1, x1, x0 }, ys[4] = { y0, y0, y1, y1 };
	float ax0 = INFINITY, ay0 = INFINITY, ax1 = -INFINITY, ay1 = -INFINITY;

	for (int i = 0; i < 4; i++) {
		float x = m[0] * xs[i] + m[2] * ys[i] + m[4];
		float y = m[1] * xs[i] + m[3] * ys[i] + m[5];
		if (x < ax0) ax0 = x;
		if (x > ax1) ax1 = x;
		if (y < ay0) ay0 = y;
		if (y > ay1) ay1 = y;
	}
	*bx0 = ax0;
	*by0 = ay0;
	*bx1 = ax1;
	*by1 = ay1;
}

/** The transform functions of a canonical text multiplied into m (w x h: the border box, the
 * percentages' reference). */
static void fx_functions(const char *t, const css_computed_style *style,
		const css_unit_ctx *u, float w, float h, float scale, float m[6])
{
	while (t != NULL && *t != '\0') {
		float f[6] = { 1, 0, 0, 1, 0, 0 };
		const char *p = strchr(t, '(');

		if (p == NULL)
			break;
		p++;
		if (strncmp(t, "translate(", 10) == 0) {
			f[4] = fx_len(&p, w, style, u, scale);
			p = fx_next(p);
			f[5] = fx_len(&p, h, style, u, scale);
		} else if (strncmp(t, "scale(", 6) == 0) {
			f[0] = strtof(p, (char **) &p);
			p = fx_next(p);
			f[3] = strtof(p, (char **) &p);
		} else if (strncmp(t, "rotate(", 7) == 0) {
			float a = fx_len(&p, 0, style, u, 1) * DEG;
			f[0] = cosf(a);
			f[1] = sinf(a);
			f[2] = -f[1];
			f[3] = f[0];
		} else if (strncmp(t, "skew(", 5) == 0) {
			float ax = fx_len(&p, 0, style, u, 1) * DEG, ay;
			p = fx_next(p);
			ay = fx_len(&p, 0, style, u, 1) * DEG;
			f[2] = tanf(ax);
			f[1] = tanf(ay);
		} else if (strncmp(t, "matrix(", 7) == 0) {
			for (int i = 0; i < 6; i++) {
				f[i] = strtof(p, (char **) &p);
				p = fx_next(p);
			}
			f[4] *= scale;
			f[5] *= scale;
		}
		onyx_matrix_mul(m, f, m);
		t = strchr(t, ' ');
		if (t != NULL)
			t++;
	}
}

/** The style's transform (translate, rotate, scale, then transform), without its origin;
 * false when it has none. */
static bool fx_css_matrix(const css_computed_style *style, const css_unit_ctx *u,
		float w, float h, float scale, float m[6])
{
	lwc_string *text;
	bool any = false;

	m[0] = m[3] = 1;
	m[1] = m[2] = m[4] = m[5] = 0;
	if (style == NULL)
		return false;
	text = NULL;
	if (css_computed_translate(style, &text) == CSS_ONYX_TEXT_SET && text != NULL) {
		fx_functions(lwc_string_data(text), style, u, w, h, scale, m);
		any = true;
	}
	text = NULL;
	if (css_computed_rotate(style, &text) == CSS_ONYX_TEXT_SET && text != NULL) {
		fx_functions(lwc_string_data(text), style, u, w, h, scale, m);
		any = true;
	}
	text = NULL;
	if (css_computed_scale(style, &text) == CSS_ONYX_TEXT_SET && text != NULL) {
		fx_functions(lwc_string_data(text), style, u, w, h, scale, m);
		any = true;
	}
	text = NULL;
	if (css_computed_transform(style, &text) == CSS_ONYX_TEXT_SET && text != NULL) {
		fx_functions(lwc_string_data(text), style, u, w, h, scale, m);
		any = true;
	}
	return any;
}

static bool fx_linear_identity(const float m[6])
{
	return fabsf(m[0] - 1) < 1e-4f && fabsf(m[3] - 1) < 1e-4f &&
			fabsf(m[1]) < 1e-4f && fabsf(m[2]) < 1e-4f;
}

/* exported function documented in html/onyx_paint.h -- Onyx: a transform that is only a
 * translation (after composing its functions) is the layout's (the box moved as a relative
 * offset moves it); any other is painted through a layer (onyx_box_matrix) */
bool onyx_box_translate(const css_computed_style *style, const css_unit_ctx *unit_len_ctx,
		float w, float h, float *tx, float *ty)
{
	float m[6];

	*tx = *ty = 0;
	if (!fx_css_matrix(style, unit_len_ctx, w, h, 1, m) || !fx_linear_identity(m))
		return false;
	*tx = m[4];
	*ty = m[5];
	return *tx != 0 || *ty != 0;
}

/* exported function documented in html/onyx_fx.h */
bool onyx_box_matrix(const css_computed_style *style, const css_unit_ctx *unit_len_ctx,
		const struct box *box, float scale, float m[6])
{
	float bw, bh, bx, by, ox, oy;
	float t[6] = { 1, 0, 0, 1, 0, 0 };
	lwc_string *text = NULL;

	if (style == NULL)
		return false;
	bw = (box->border[LEFT].width + box->padding[LEFT] + box->width +
			box->padding[RIGHT] + box->border[RIGHT].width) * scale;
	bh = (box->border[TOP].width + box->padding[TOP] + box->height +
			box->padding[BOTTOM] + box->border[BOTTOM].width) * scale;
	if (!fx_css_matrix(style, unit_len_ctx, bw, bh, scale, m) || fx_linear_identity(m))
		return false;
	/* about the transform-origin (the border box's; 50% 50% by default) */
	bx = -box->border[LEFT].width * scale;
	by = -box->border[TOP].width * scale;
	ox = bw / 2;
	oy = bh / 2;
	if (css_computed_transform_origin(style, &text) == CSS_ONYX_TEXT_SET &&
	    text != NULL) {
		const char *p = lwc_string_data(text);
		ox = fx_len(&p, bw, style, unit_len_ctx, scale);
		while (*p == ' ')
			p++;
		oy = fx_len(&p, bh, style, unit_len_ctx, scale);
	}
	t[4] = bx + ox;
	t[5] = by + oy;
	onyx_matrix_mul(t, m, m);
	t[4] = -t[4];
	t[5] = -t[5];
	onyx_matrix_mul(m, t, m);
	return true;
}

/* ---- filters --------------------------------------------------------------------------- */

/** A filter text's functions (n of them at most); currentColor the style's colour. */
static int fx_filters(const char *t, const css_computed_style *style,
		const css_unit_ctx *u, float scale, struct onyx_filter *f, int n)
{
	static const struct {
		const char *name;
		enum onyx_filter_op op;
	} amounts[] = {
		{ "brightness(", ONYX_FILTER_BRIGHTNESS }, { "contrast(", ONYX_FILTER_CONTRAST },
		{ "grayscale(", ONYX_FILTER_GRAYSCALE }, { "invert(", ONYX_FILTER_INVERT },
		{ "opacity(", ONYX_FILTER_OPACITY }, { "saturate(", ONYX_FILTER_SATURATE },
		{ "sepia(", ONYX_FILTER_SEPIA }
	};
	int k = 0;

	while (t != NULL && *t != '\0' && k < n) {
		const char *p = strchr(t, '(');
		struct onyx_filter *e = &f[k];

		if (p == NULL)
			break;
		p++;
		memset(e, 0, sizeof(*e));
		if (strncmp(t, "blur(", 5) == 0) {
			e->op = ONYX_FILTER_BLUR;
			e->v = fx_len(&p, 0, style, u, scale);
			if (e->v > 0.2f)
				k++;
		} else if (strncmp(t, "hue-rotate(", 11) == 0) {
			e->op = ONYX_FILTER_HUE_ROTATE;
			e->v = fx_len(&p, 0, style, u, 1);
			k++;
		} else if (strncmp(t, "drop-shadow(", 12) == 0) {
			e->op = ONYX_FILTER_DROP_SHADOW;
			e->dx = fx_len(&p, 0, style, u, scale);
			p = fx_next(p);
			e->dy = fx_len(&p, 0, style, u, scale);
			p = fx_next(p);
			e->v = fx_len(&p, 0, style, u, scale);
			p = fx_next(p);
			if (*p == '#') {
				e->argb = (uint32_t) strtoul(p + 1, NULL, 16);
			} else {
				css_color c = 0xff000000;
				css_computed_color(style, &c);
				e->argb = c;
			}
			if ((e->argb >> 24) != 0)
				k++;
		} else {
			for (unsigned i = 0; i < sizeof(amounts) / sizeof(amounts[0]); i++) {
				if (strncmp(t, amounts[i].name, strlen(amounts[i].name)) != 0)
					continue;
				e->op = amounts[i].op;
				e->v = strtof(p, NULL);
				/* (the identities left out) */
				if ((e->op == ONYX_FILTER_GRAYSCALE || e->op == ONYX_FILTER_INVERT ||
				     e->op == ONYX_FILTER_SEPIA) ? e->v != 0 : e->v != 1)
					k++;
				break;
			}
		}
		t = strchr(t, ' ');
		if (t != NULL)
			t++;
	}
	return k;
}

/* exported function documented in html/onyx_fx.h */
void onyx_filter_outset(const struct onyx_filter *f, int n, float out[4])
{
	float l = 0, t = 0, r = 0, b = 0;

	for (int i = 0; i < n; i++) {
		if (f[i].op == ONYX_FILTER_BLUR) {
			float e = ceilf(f[i].v * 3) + 1;
			l += e;
			t += e;
			r += e;
			b += e;
		} else if (f[i].op == ONYX_FILTER_DROP_SHADOW) {
			/* (the shadow: the image moved, blurred at half its radius) */
			float e = ceilf(f[i].v * 1.5f) + 1;
			float sl = e - f[i].dx, st = e - f[i].dy;
			float sr = e + f[i].dx, sb = e + f[i].dy;
			if (sl > l) l = sl;
			if (st > t) t = st;
			if (sr > r) r = sr;
			if (sb > b) b = sb;
		}
	}
	out[0] = l;
	out[1] = t;
	out[2] = r;
	out[3] = b;
}

/* exported function documented in html/onyx_fx.h */
void onyx_box_filter_outset(const css_computed_style *style,
		const css_unit_ctx *unit_len_ctx, float out[4])
{
	struct onyx_filter f[ONYX_FILTER_MAX];
	lwc_string *text = NULL;
	int n = 0;

	if (style != NULL && css_computed_filter(style, &text) == CSS_ONYX_TEXT_SET &&
	    text != NULL)
		n = fx_filters(lwc_string_data(text), style, unit_len_ctx, 1, f,
				ONYX_FILTER_MAX);
	onyx_filter_outset(f, n, out);
}

/* ---- the effects of a box -------------------------------------------------------------- */

/* exported function documented in html/onyx_fx.h */
bool onyx_fx_style(const css_computed_style *style)
{
	css_fixed o = INTTOFIX(1);
	lwc_string *text;

	if (style == NULL)
		return false;
	if (css_computed_opacity(style, &o) == CSS_OPACITY_SET && o < INTTOFIX(1))
		return true;
	text = NULL;
	if (css_computed_transform(style, &text) == CSS_ONYX_TEXT_SET)
		return true;
	if (css_computed_translate(style, &text) == CSS_ONYX_TEXT_SET)
		return true;
	if (css_computed_rotate(style, &text) == CSS_ONYX_TEXT_SET)
		return true;
	if (css_computed_scale(style, &text) == CSS_ONYX_TEXT_SET)
		return true;
	if (css_computed_filter(style, &text) == CSS_ONYX_TEXT_SET)
		return true;
	if (css_computed_backdrop_filter(style, &text) == CSS_ONYX_TEXT_SET)
		return true;
	if (css_computed_mix_blend_mode(style, &text) == CSS_ONYX_TEXT_SET)
		return true;
	return false;
}

/* exported function documented in html/onyx_fx.h */
bool onyx_fx_box(const struct box *box)
{
	if (box->style == NULL || (box->flags & STYLE_OWNED))
		return false;
	switch (box->type) {
	case BOX_BLOCK: case BOX_INLINE_BLOCK: case BOX_TABLE: case BOX_TABLE_CELL:
	case BOX_FLEX: case BOX_INLINE_FLEX:
		break;
	case BOX_INLINE:
		if (box->object == NULL)
			return false;	/* (an inline's pieces: redraw.c groups them) */
		break;
	default:
		return false;
	}
	/* (a list marker shares its item's style) */
	if (box->parent != NULL && box->parent->list_marker == box)
		return false;
	return true;
}

static enum onyx_blend fx_blend(const char *s)
{
	static const struct {
		const char *name;
		enum onyx_blend mode;
	} modes[] = {
		{ "multiply", ONYX_BLEND_MULTIPLY }, { "screen", ONYX_BLEND_SCREEN },
		{ "overlay", ONYX_BLEND_OVERLAY }, { "darken", ONYX_BLEND_DARKEN },
		{ "lighten", ONYX_BLEND_LIGHTEN }, { "color-dodge", ONYX_BLEND_COLOR_DODGE },
		{ "color-burn", ONYX_BLEND_COLOR_BURN }, { "hard-light", ONYX_BLEND_HARD_LIGHT },
		{ "soft-light", ONYX_BLEND_SOFT_LIGHT }, { "difference", ONYX_BLEND_DIFFERENCE },
		{ "exclusion", ONYX_BLEND_EXCLUSION }, { "plus-lighter", ONYX_BLEND_PLUS_LIGHTER },
		{ "plus-darker", ONYX_BLEND_PLUS_DARKER }
	};

	for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); i++)
		if (strcmp(s, modes[i].name) == 0)
			return modes[i].mode;
	return ONYX_BLEND_NORMAL;	/* (hue, saturation, color, luminosity: not drawn) */
}

/* exported function documented in html/onyx_fx.h */
bool onyx_box_fx(const struct html_content *html, const struct box *box, float scale,
		struct onyx_fx *fx)
{
	const css_computed_style *style = box->style;
	const css_unit_ctx *u = &html->unit_len_ctx;
	css_fixed o = INTTOFIX(1);
	lwc_string *text;

	if (!onyx_fx_box(box) || !onyx_fx_style(style))
		return false;
	fx->opacity = 1;
	if (css_computed_opacity(style, &o) == CSS_OPACITY_SET && o < INTTOFIX(1))
		fx->opacity = o <= 0 ? 0 : FIXTOFLT(o);
	fx->matrix = onyx_box_matrix(style, u, box, scale, fx->m);
	fx->nfilter = 0;
	text = NULL;
	if (css_computed_filter(style, &text) == CSS_ONYX_TEXT_SET && text != NULL)
		fx->nfilter = fx_filters(lwc_string_data(text), style, u, scale,
				fx->filter, ONYX_FILTER_MAX);
	fx->nbackdrop = 0;
	text = NULL;
	if (css_computed_backdrop_filter(style, &text) == CSS_ONYX_TEXT_SET && text != NULL)
		fx->nbackdrop = fx_filters(lwc_string_data(text), style, u, scale,
				fx->backdrop, ONYX_FILTER_MAX);
	fx->blend = ONYX_BLEND_NORMAL;
	text = NULL;
	if (css_computed_mix_blend_mode(style, &text) == CSS_ONYX_TEXT_SET && text != NULL)
		fx->blend = fx_blend(lwc_string_data(text));
	return fx->opacity < 1 || fx->matrix || fx->nfilter > 0 || fx->nbackdrop > 0 ||
			fx->blend != ONYX_BLEND_NORMAL;
}

/* ---- geometry: where a box is painted ---------------------------------------------------- */

/* exported function documented in html/onyx_fx.h */
void onyx_fx_child_bounds(const css_unit_ctx *unit_len_ctx, const struct box *box,
		int *x0, int *y0, int *x1, int *y1)
{
	float fx0, fy0, fx1, fy1, out[4], m[6];
	bool clip_x = false, clip_y = false;

	fx0 = -box->border[LEFT].width;
	fy0 = -box->border[TOP].width;
	fx1 = box->padding[LEFT] + box->width + box->padding[RIGHT] +
			box->border[RIGHT].width;
	fy1 = box->padding[TOP] + box->height + box->padding[BOTTOM] +
			box->border[BOTTOM].width;
	if (box->style != NULL) {
		clip_x = css_computed_overflow_x(box->style) != CSS_OVERFLOW_VISIBLE;
		clip_y = css_computed_overflow_y(box->style) != CSS_OVERFLOW_VISIBLE;
	}
	if (!clip_x) {
		if (box->descendant_x0 < fx0) fx0 = box->descendant_x0;
		if (box->descendant_x1 > fx1) fx1 = box->descendant_x1;
	}
	if (!clip_y) {
		if (box->descendant_y0 < fy0) fy0 = box->descendant_y0;
		if (box->descendant_y1 > fy1) fy1 = box->descendant_y1;
	}
	if (onyx_fx_box(box)) {
		onyx_box_filter_outset(box->style, unit_len_ctx, out);
		fx0 -= out[0];
		fy0 -= out[1];
		fx1 += out[2];
		fy1 += out[3];
		if (onyx_box_matrix(box->style, unit_len_ctx, box, 1, m))
			onyx_matrix_bbox(m, fx0, fy0, fx1, fy1, &fx0, &fy0, &fx1, &fy1);
	}
	*x0 = box->x + (int) floorf(fx0);
	*y0 = box->y + (int) floorf(fy0);
	*x1 = box->x + (int) ceilf(fx1);
	*y1 = box->y + (int) ceilf(fy1);
}

/* exported function documented in html/onyx_fx.h */
bool onyx_fx_page_rect(const struct html_content *html, const struct box *box, bool self,
		bool filters, float r[4])
{
	const css_unit_ctx *u = &html->unit_len_ctx;
	bool changed = false;
	const struct box *b;

	for (b = self ? box : box->parent; b != NULL; b = b->parent) {
		float m[6], out[4];
		int bx, by;

		if (!onyx_fx_box(b) || !onyx_fx_style(b->style))
			continue;
		if (filters) {
			onyx_box_filter_outset(b->style, u, out);
			if (out[0] != 0 || out[1] != 0 || out[2] != 0 || out[3] != 0) {
				r[0] -= out[0];
				r[1] -= out[1];
				r[2] += out[2];
				r[3] += out[3];
				changed = true;
			}
		}
		if (!onyx_box_matrix(b->style, u, b, 1, m))
			continue;
		box_coords((struct box *) b, &bx, &by);
		m[4] += bx - (m[0] * bx + m[2] * by);
		m[5] += by - (m[1] * bx + m[3] * by);
		onyx_matrix_bbox(m, r[0], r[1], r[2], r[3], &r[0], &r[1], &r[2], &r[3]);
		changed = true;
	}
	return changed;
}

/* exported function documented in html/onyx_fx.h */
bool onyx_fx_style_differs(const css_computed_style *a, const css_computed_style *b)
{
	css_fixed oa = INTTOFIX(1), ob = INTTOFIX(1);
	lwc_string *ta, *tb;
	uint8_t (*const text[])(const css_computed_style *, lwc_string **) = {
		css_computed_transform, css_computed_translate, css_computed_rotate,
		css_computed_scale, css_computed_transform_origin, css_computed_filter,
		css_computed_backdrop_filter, css_computed_mix_blend_mode
	};

	if (a == b)
		return false;
	if (a == NULL || b == NULL)
		return true;
	if (css_computed_opacity(a, &oa) != css_computed_opacity(b, &ob) || oa != ob)
		return true;
	for (unsigned i = 0; i < sizeof(text) / sizeof(text[0]); i++) {
		ta = tb = NULL;
		if (text[i](a, &ta) != text[i](b, &tb) || ta != tb)
			return true;
	}
	return false;
}
