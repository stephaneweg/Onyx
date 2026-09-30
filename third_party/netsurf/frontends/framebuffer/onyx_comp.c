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
 * Onyx: the browser's view composited -- GPU compositing, stage 2 (docs/06 §22, docs/07 §6).
 *
 * The band: the page painted by the core (the CPU plotters, as ever) into a RAM surface as wide
 * as the view and a few views high, a ring of document rows -- document row y is the band's row
 * y mod its height. Its valid rows are one run [vy0, vy1) that always holds the view's; a
 * scroll moves the view over them (nothing painted), rows that come into view are painted, and
 * in idle turns the rows around the view are painted ahead (a piece a turn). The damage the
 * core reports (document rectangles) is painted again where the band is valid, even out of
 * view. The band is a texture of user/gpucomp, updated where it was painted.
 *
 * The retained layers: a group with an opacity and / or a transform (no filter, no blend mode,
 * no backdrop: what gpucomp composites) is offered by the core (netsurf/onyx_paint.h,
 * ONYX_LAYER_OFFER). Accepted, its pixels are painted apart (premultiplied, as onyx_layer.c's
 * isolated groups, over black and white) into a buffer of its own, uploaded, and it is left out
 * of the band; each frame composites it over the band through its matrix, in its clip, at its
 * opacity. Its pixels are painted again only where damage reaches them; a change of its matrix
 * or its opacity alone is a composite (onyx_comp_layer_update). A plot operation painted into
 * the band after it and over it would be covered by it: the layer is then "demoted" -- painted
 * in place again, as ever (onyx_comp_note sees each operation's rectangle).
 *
 * A frame: the band's rows in view (one or two pieces of the ring) and the layers that reach
 * the view, one gpc_composite straight into the window's canvas (onyx_surface_hole: the back
 * buffer's copies leave that part alone). On the Pi the V3D composites; elsewhere, or when the
 * GPU fails its self-test or stops, gpucomp's CPU path does the same.
 */

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libnsfb.h>
#include <libnsfb_plot.h>

#include "utils/utils.h"
#include "utils/nsoption.h"
#include "netsurf/types.h"
#include "netsurf/plotters.h"
#include "netsurf/browser_window.h"
#include "netsurf/onyx_paint.h"

struct fbtk_bitmap;	/* (framebuffer.h names it) */
#include "framebuffer/gui.h"
#include "framebuffer/framebuffer.h"
#include "framebuffer/onyx_paint.h"
#include "framebuffer/onyx_comp.h"

#include "gpucomp/gpucomp.h"		/* user/gpucomp */
#include "netsurf/onyx_chrome.h"	/* user/netsurf: the canvas */
#include "netsurf/onyx_perf.h"

#define BAND_VIEWS	3		/* the band: that many views high */
#define BAND_MAX_PX	(8 * 1024 * 1024)	/* (its pixels at most: 32 MB) */
#define DAMAGE_MAX	24		/* damaged rectangles kept apart (more: merged) */
#define PAINTED_MAX	64		/* rectangles painted in an update (the layers' coverage) */
#define LAYER_MAX_PX	(4 * 1024 * 1024)	/* a layer larger: painted in place */
#define LAYERS_BUDGET_PX (24 * 1024 * 1024)	/* the layers' pixels (~96 MB of textures) */
#define UPDATE_LOOPS	4		/* (demotions, stale layers: paint again, that many times) */

struct crect {
	int x0, y0, x1, y1;
};

static inline bool cr_empty(const struct crect *r)
{
	return r->x1 <= r->x0 || r->y1 <= r->y0;
}

static inline bool cr_meet(const struct crect *a, const struct crect *b)
{
	return a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 && b->y0 < a->y1;
}

static inline struct crect cr_and(struct crect a, const struct crect *b)
{
	if (a.x0 < b->x0) a.x0 = b->x0;
	if (a.y0 < b->y0) a.y0 = b->y0;
	if (a.x1 > b->x1) a.x1 = b->x1;
	if (a.y1 > b->y1) a.y1 = b->y1;
	return a;
}

static inline void cr_add(struct crect *a, const struct crect *b)
{
	if (cr_empty(b))
		return;
	if (cr_empty(a)) {
		*a = *b;
		return;
	}
	if (b->x0 < a->x0) a->x0 = b->x0;
	if (b->y0 < a->y0) a->y0 = b->y0;
	if (b->x1 > a->x1) a->x1 = b->x1;
	if (b->y1 > a->y1) a->y1 = b->y1;
}

static inline long long cr_area(const struct crect *r)
{
	return cr_empty(r) ? 0 : (long long) (r->x1 - r->x0) * (r->y1 - r->y0);
}

/** floor(a / b), b > 0 */
static inline int fdiv(int a, int b)
{
	return a >= 0 ? a / b : -((-a + b - 1) / b);
}

/* ---- a retained layer ------------------------------------------------------------------ */

struct clayer {
	const void *key;		/* the box */
	const void *tree;		/* its box tree (a new one: its pixels painted again) */
	struct crect b;			/* its pixels' rectangle, document px (untransformed) */
	bool transformed;
	float m[6];			/* document px -> document px */
	float lm[6];			/* the matrix about its origin (onyx_layer's lm) */
	float ox, oy;			/* that origin, document px */
	float opacity;
	struct crect clip;		/* document px (open sides: +-INT_MAX / 2) */
	struct crect fp;		/* where it shows: its bounds through m, in its clip */
	uint32_t *px;			/* premultiplied 0xAARRGGBB, its rectangle's size */
	gpc_tex *tex;
	struct crect dmg;		/* its pixels to paint again (document px, untransformed) */
	unsigned seen;			/* the update it was last offered in */
	bool demoted;			/* painted in place (never offered again until a reset) */
	bool conflict;			/* something was painted over it in the band */
	bool fresh;			/* offered for the first time in this update */
	bool in_piece;			/* offered in the piece being painted */
};

#define OPEN (INT_MAX / 2)

static struct {
	bool on;			/* composited frames */
	bool reported;
	gpc_ctx *g;
	/* the band */
	nsfb_t *surf;
	uint32_t *px;
	int bw, bh;			/* its size: the view's width, rows of the ring */
	gpc_tex *tex;
	int bx;				/* the document column of its column 0 */
	int vy0, vy1;			/* its valid rows (document) */
	int vw, vh;			/* the view it was made for */
	struct browser_window *bwin;
	bool reset;			/* the whole document to paint again */
	struct crect dmg[DAMAGE_MAX];
	int ndmg;
	/* an update going on */
	bool painting;			/* the core paints into the band */
	int org_x, org_y;		/* the document point at the band surface's 0, 0 */
	struct crect piece;		/* the piece being painted (band surface px) */
	unsigned gen;
	struct crect painted[PAINTED_MAX];	/* document px */
	int npainted;
	int last_offered;		/* the layer offered last in this piece (-1: none) */
	/* the retained layers, in their painting order */
	struct clayer **layers;
	int nlayers, layers_cap;
	long long layers_px;
	struct clayer *passing;		/* the layer whose pixels are being painted */
	/* the view (the last redraw's) */
	struct onyx_comp_view view;
	bool have_view;
} C;

bool onyx_comp_noting;

static void *comp_alloc(unsigned long n)
{
	return malloc(n);
}

static void comp_free(void *p)
{
	free(p);
}

/* ---- the GPU's self-test ------------------------------------------------------------------- */

/** max difference of a channel, and the pixels off by more than 4 */
static void st_compare(const unsigned *a, int sa, const unsigned *b, int sb, int w, int h,
		int *maxd, int *bad)
{
	*maxd = 0;
	*bad = 0;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			unsigned p = a[y * sa + x], q = b[y * sb + x];
			int m = 0;
			for (int k = 0; k < 24; k += 8) {
				int d = (int) ((p >> k) & 255) - (int) ((q >> k) & 255);
				if (d < 0)
					d = -d;
				if (d > m)
					m = d;
			}
			if (m > *maxd)
				*maxd = m;
			if (m > 4)
				(*bad)++;
		}
}

/** a scene like a page's: an opaque band scrolled (nearest), a rotated translucent card, a
 * faded one scaled in a clip -> the pixels in t. mode 1: the band alone; 2: the cards alone,
 * over what the target holds (no clear: the GPU loads the target first) */
static int st_scene(gpc_ctx *g, gpc_tex *page, gpc_tex *card, const gpc_target *t, int mode)
{
	gpc_layer L[3];
	int n = 1, band_only = mode == 1;

	if (mode == 2) {
		for (int y = 0; y < t->h; y++)
			for (int x = 0; x < t->w; x++)
				t->pixels[y * t->stride + x] = ((unsigned) (x * 5) << 16) |
					((unsigned) (y * 7) & 0xff) << 8 | ((x + y) & 0x40 ? 0xc0 : 0x20);
	}
	gpc_layer_init(&L[0], page);
	L[0].src_x = 13;
	L[0].src_y = 27;
	L[0].src_w = (float) t->w;
	L[0].src_h = (float) t->h;
	L[0].flags = GPC_L_OPAQUE | GPC_L_NEAREST;
	if (!band_only) {
		gpc_layer_init(&L[1], card);
		gpc_matrix_translate(&L[1].m, 60, 20);
		gpc_matrix_rotate(&L[1].m, 0.35f);
		L[1].opacity = 230;
		gpc_layer_init(&L[2], card);
		gpc_matrix_translate(&L[2].m, 120.5f, 50.25f);
		gpc_matrix_scale(&L[2].m, 1.5f, 1.25f);
		L[2].clip[0] = 110;
		L[2].clip[1] = 40;
		L[2].clip[2] = 60;
		L[2].clip[3] = 50;
		L[2].opacity = 128;
		n = 3;
	}
	if (mode == 2)
		return gpc_composite(g, t, L + 1, n - 1, 0, 0);
	return gpc_composite(g, t, L, n, 0x000000, GPC_C_CLEAR);
}

/** the GPU's pictures against the CPU path's: true when they agree */
static bool st_run(gpc_ctx *G, char *why, size_t whylen)
{
	enum { W = 192, H = 108, PW = 256, PH = 256, CW = 64, CH = 48 };
	gpc_config cc = { comp_alloc, comp_free, GPC_F_CPU };
	gpc_ctx *K = gpc_create(&cc);
	unsigned *page = malloc(PW * PH * 4), *card = malloc(CW * CH * 4);
	unsigned *pc = malloc(W * H * 4), *pg;
	gpc_tex *tg[2] = { NULL, NULL }, *tc[2] = { NULL, NULL };
	int stride = 0, maxd, bad, r1, r2;
	bool ok = false;

	if (K == NULL || page == NULL || card == NULL || pc == NULL) {
		snprintf(why, whylen, "no memory");
		goto done;
	}
	for (int y = 0; y < PH; y++)
		for (int x = 0; x < PW; x++)
			page[y * PW + x] = 0xff000000u | ((unsigned) (x * 255 / PW) << 16) |
				((unsigned) (y * 255 / PH) << 8) | (((x ^ y) & 16) ? 0xe0 : 0x30);
	for (int y = 0; y < CH; y++)
		for (int x = 0; x < CW; x++) {
			/* a disc's coverage, premultiplied */
			float dx = x + 0.5f - CW / 2.0f, dy = y + 0.5f - CH / 2.0f;
			float d = 22 - sqrtf(dx * dx + dy * dy);
			unsigned a = d >= 1 ? 255 : d <= 0 ? 0 : (unsigned) (d * 255);
			unsigned r = a * 200 / 255, gg = a * (x * 4) / 255 / 1, b = a * 90 / 255;
			if (gg > a)
				gg = a;
			card[y * CW + x] = (a << 24) | (r << 16) | (gg << 8) | b;
		}
	pg = gpc_target_alloc(G, W, H, &stride);
	tg[0] = gpc_tex_create(G, PW, PH, page, PW);
	tg[1] = gpc_tex_create(G, CW, CH, card, CW);
	tc[0] = gpc_tex_create(K, PW, PH, page, PW);
	tc[1] = gpc_tex_create(K, CW, CH, card, CW);
	if (pg == NULL || tg[0] == NULL || tg[1] == NULL || tc[0] == NULL || tc[1] == NULL) {
		snprintf(why, whylen, "no memory for the textures");
		goto done;
	}
	for (int mode = 1; mode >= 0; mode = mode == 1 ? 2 : mode == 2 ? 0 : -1) {
		static const char *what[] = { "scene", "page", "scene over the target (no clear)" };
		gpc_target TG = { pg, W, H, stride, 0 }, TC = { pc, W, H, W, 0 };
		r1 = st_scene(G, tg[0], tg[1], &TG, mode);
		r2 = st_scene(K, tc[0], tc[1], &TC, mode);
		if (r1 != GPC_OK || r2 != GPC_OK) {
			snprintf(why, whylen, "%s", r1 == GPC_LOST ? "the GPU stopped" :
					"a composite failed");
			goto done;
		}
		st_compare(pg, stride, pc, W, W, H, &maxd, &bad);
		/* the band's texels copied as they are; the layers within the
		 * GPU's filtering (gpcdemo test's allowance) */
		if (mode == 1 ? maxd > 1 : bad > W * H / 500) {
			snprintf(why, whylen, "the %s differs (max %d, %d pixels off)",
					what[mode], maxd, bad);
			goto done;
		}
	}
	ok = gpc_backend(G) == GPC_BACKEND_GPU;
	if (!ok)
		snprintf(why, whylen, "the GPU stopped");
done:
	if (tg[0] != NULL) gpc_tex_destroy(G, tg[0]);
	if (tg[1] != NULL) gpc_tex_destroy(G, tg[1]);
	if (K != NULL)
		gpc_destroy(K);
	gpc_target_free(G, pg);
	free(page);
	free(card);
	free(pc);
	return ok;
}

/* ---- start, stop ----------------------------------------------------------------------------- */

/* exported interface documented in framebuffer/onyx_comp.h */
void onyx_comp_init(void)
{
	const char *env = getenv("NS_GPU");
	gpc_config cfg = { comp_alloc, comp_free, 0 };
	char why[96] = "";
	bool on = nsoption_bool(gpu_compositing);

	if (env != NULL && *env != '\0')
		on = strcmp(env, "0") != 0;
	if (!on) {
		fprintf(stderr, "netsurf: compositing off (Choices: gpu_compositing:0)\n");
		return;
	}
	if (env != NULL && strcmp(env, "cpu") == 0)
		cfg.flags = GPC_F_CPU;
	C.g = gpc_create(&cfg);
	if (C.g == NULL)
		return;
	if (gpc_backend(C.g) == GPC_BACKEND_GPU) {
		if (!st_run(C.g, why, sizeof why)) {
			/* the CPU path for the session */
			gpc_destroy(C.g);
			cfg.flags = GPC_F_CPU;
			C.g = gpc_create(&cfg);
			if (C.g == NULL)
				return;
			fprintf(stderr, "netsurf: compositing: the GPU failed its "
					"self-test (%s): the CPU\n", why);
		} else {
			fprintf(stderr, "netsurf: compositing: the GPU passed its "
					"self-test\n");
		}
	}
	fprintf(stderr, "netsurf: compositing on: %s\n", gpc_info(C.g));
	C.on = true;
	C.bwin = NULL;
}

static void layer_free(struct clayer *l);
static void band_free(void);

/* exported interface documented in framebuffer/onyx_comp.h */
void onyx_comp_finalise(void)
{
	band_free();
	for (int i = 0; i < C.nlayers; i++)
		layer_free(C.layers[i]);
	free(C.layers);
	C.layers = NULL;
	C.nlayers = C.layers_cap = 0;
	if (C.g != NULL)
		gpc_destroy(C.g);
	C.g = NULL;
	C.on = false;
	onyx_surface_hole(0, 0, 0, 0);
}

/* exported interface documented in framebuffer/onyx_comp.h */
bool onyx_comp_on(void)
{
	return C.on;
}

/* ---- the band --------------------------------------------------------------------------------- */

static void band_free(void)
{
	if (C.tex != NULL)
		gpc_tex_destroy(C.g, C.tex);
	C.tex = NULL;
	if (C.surf != NULL)
		nsfb_free(C.surf);
	C.surf = NULL;
	C.px = NULL;
	C.bw = C.bh = 0;
	C.vy0 = C.vy1 = 0;
	C.ndmg = 0;
}

/** a band for a view of w x h -> false: none (the view is then painted as ever) */
static bool band_make(int w, int h)
{
	int bh = h * BAND_VIEWS, stride;
	uint8_t *p;

	band_free();
	if (w <= 0 || h <= 0)
		return false;
	while (bh > h && (long long) w * bh > BAND_MAX_PX)
		bh -= h / 2;
	if (bh < h)
		bh = h;
	C.surf = nsfb_new(NSFB_SURFACE_RAM);
	if (C.surf == NULL)
		return false;
	if (nsfb_set_geometry(C.surf, w, bh, NSFB_FMT_XRGB8888) != 0 ||
	    nsfb_init(C.surf) != 0 || nsfb_get_buffer(C.surf, &p, &stride) != 0 ||
	    stride != w * 4) {
		nsfb_free(C.surf);
		C.surf = NULL;
		return false;
	}
	C.px = (uint32_t *) p;
	C.bw = w;
	C.bh = bh;
	C.tex = gpc_tex_create(C.g, w, bh, NULL, w);
	if (C.tex == NULL) {
		band_free();
		return false;
	}
	C.vy0 = C.vy1 = 0;
	C.ndmg = 0;
	return true;
}

/* ---- the layers' registry ---------------------------------------------------------------------- */

static void layer_drop_pixels(struct clayer *l)
{
	if (l->tex != NULL)
		gpc_tex_destroy(C.g, l->tex);
	l->tex = NULL;
	if (l->px != NULL)
		C.layers_px -= cr_area(&l->b);
	free(l->px);
	l->px = NULL;
}

static void layer_free(struct clayer *l)
{
	layer_drop_pixels(l);
	free(l);
}

static int layer_find(const void *key)
{
	for (int i = 0; i < C.nlayers; i++)
		if (C.layers[i]->key == key)
			return i;
	return -1;
}

static void layer_remove(int i)
{
	layer_free(C.layers[i]);
	memmove(C.layers + i, C.layers + i + 1, sizeof(*C.layers) * (C.nlayers - i - 1));
	C.nlayers--;
	if (C.last_offered == i)
		C.last_offered = -1;
	else if (C.last_offered > i)
		C.last_offered--;
}

/** layer i moved to just after layer j (j < i) */
static void layer_move_after(int i, int j)
{
	struct clayer *l = C.layers[i];

	memmove(C.layers + j + 2, C.layers + j + 1, sizeof(*C.layers) * (i - j - 1));
	C.layers[j + 1] = l;
}

/** x' = m x (a point) */
static inline void mat_apply(const float m[6], float x, float y, float *ox, float *oy)
{
	*ox = m[0] * x + m[2] * y + m[4];
	*oy = m[1] * x + m[3] * y + m[5];
}

/** the bounding box of rectangle r through m */
static struct crect mat_bbox(const float m[6], const struct crect *r)
{
	float xs[4] = { r->x0, r->x1, r->x1, r->x0 }, ys[4] = { r->y0, r->y0, r->y1, r->y1 };
	float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
	struct crect o;

	for (int i = 0; i < 4; i++) {
		float x, y;
		mat_apply(m, xs[i], ys[i], &x, &y);
		if (x < bx0) bx0 = x;
		if (y < by0) by0 = y;
		if (x > bx1) bx1 = x;
		if (y > by1) by1 = y;
	}
	o.x0 = (int) floorf(bx0) - 1;
	o.y0 = (int) floorf(by0) - 1;
	o.x1 = (int) ceilf(bx1) + 1;
	o.y1 = (int) ceilf(by1) + 1;
	return o;
}

static bool mat_invert(const float m[6], float r[6])
{
	double det = (double) m[0] * m[3] - (double) m[1] * m[2];

	if (det > -1e-9 && det < 1e-9)
		return false;
	r[0] = (float) (m[3] / det);
	r[1] = (float) (-m[1] / det);
	r[2] = (float) (-m[2] / det);
	r[3] = (float) (m[0] / det);
	r[4] = (float) (((double) m[2] * m[5] - (double) m[3] * m[4]) / det);
	r[5] = (float) (((double) m[1] * m[4] - (double) m[0] * m[5]) / det);
	return true;
}

/** m = T(ox, oy) lm T(-ox, -oy) */
static void mat_about(const float lm[6], float ox, float oy, float m[6])
{
	memcpy(m, lm, sizeof(float) * 6);
	m[4] += ox - (lm[0] * ox + lm[2] * oy);
	m[5] += oy - (lm[1] * ox + lm[3] * oy);
}

static void layer_footprint(struct clayer *l)
{
	struct crect f = l->transformed ? mat_bbox(l->m, &l->b) : l->b;

	l->fp = cr_and(f, &l->clip);
}

/* ---- damage --------------------------------------------------------------------------------- */

static void damage_add(struct crect r)
{
	int best = -1;
	long long cost = 0;

	if (cr_empty(&r) || C.px == NULL)
		return;
	/* (only what the band holds: its columns, its valid rows) */
	if (r.x0 < C.bx) r.x0 = C.bx;
	if (r.x1 > C.bx + C.bw) r.x1 = C.bx + C.bw;
	if (r.y0 < C.vy0) r.y0 = C.vy0;
	if (r.y1 > C.vy1) r.y1 = C.vy1;
	if (cr_empty(&r))
		return;
	for (int i = 0; i < C.ndmg; i++) {
		struct crect u = C.dmg[i];
		long long c;
		if (r.x0 >= u.x0 && r.y0 >= u.y0 && r.x1 <= u.x1 && r.y1 <= u.y1)
			return;		/* (already in) */
		cr_add(&u, &r);
		c = cr_area(&u) - cr_area(&C.dmg[i]) - cr_area(&r);
		if (best < 0 || c < cost) {
			best = i;
			cost = c;
		}
	}
	if (C.ndmg < DAMAGE_MAX && (best < 0 || cost > 0)) {
		C.dmg[C.ndmg++] = r;
		return;
	}
	cr_add(&C.dmg[best], &r);	/* (merged with the one it grows least) */
}

/** the layers' pixels damaged by a document rectangle, and where they show */
static void damage_layers(const struct crect *r)
{
	for (int i = 0; i < C.nlayers; i++) {
		struct clayer *l = C.layers[i];
		struct crect d, shows;
		float inv[6];

		if (l->demoted || l->px == NULL)
			continue;
		/* the rectangle in its own (untransformed) px -- the core's
		 * rectangles are the layout's -- and through its inverse matrix */
		d = cr_and(*r, &l->b);
		if (l->transformed && mat_invert(l->m, inv)) {
			struct crect back = mat_bbox(inv, r);
			back = cr_and(back, &l->b);
			cr_add(&d, &back);
		}
		if (cr_empty(&d))
			continue;
		cr_add(&l->dmg, &d);
		/* (the band painted where that part shows: the core then offers
		 * the layer, and its pixels are painted) */
		shows = l->transformed ? mat_bbox(l->m, &d) : d;
		damage_add(cr_and(shows, &l->clip));
	}
}

/* exported interface documented in framebuffer/onyx_comp.h */
void onyx_comp_damage(int x0, int y0, int x1, int y1)
{
	struct crect r = { x0, y0, x1, y1 };

	if (!C.on || cr_empty(&r))
		return;
	damage_layers(&r);
	damage_add(r);
}

/* exported interface documented in framebuffer/onyx_comp.h */
void onyx_comp_damage_all(void)
{
	if (C.on)
		C.reset = true;
}

/* ---- painting into the band ----------------------------------------------------------------- */

static void painted_add(const struct crect *r)
{
	if (C.npainted < PAINTED_MAX)
		C.painted[C.npainted++] = *r;
	else
		C.npainted = PAINTED_MAX + 1;	/* (too many: coverage unknown) */
}

/** whether r lies in the rectangles painted in this update */
static bool painted_covers(struct crect r, int from, int depth)
{
	if (cr_empty(&r))
		return true;
	if (C.npainted > PAINTED_MAX || depth > 8)
		return false;
	for (int i = from; i < C.npainted; i++) {
		const struct crect *p = &C.painted[i];
		struct crect a, b2, c, d;
		if (!cr_meet(&r, p))
			continue;
		/* r less p: above, below, left, right of it */
		a = r; a.y1 = p->y0 > r.y0 ? p->y0 : r.y0;
		b2 = r; b2.y0 = p->y1 < r.y1 ? p->y1 : r.y1;
		c = r; c.y0 = a.y1; c.y1 = b2.y0; c.x1 = p->x0 > r.x0 ? p->x0 : r.x0;
		d = r; d.y0 = a.y1; d.y1 = b2.y0; d.x0 = p->x1 < r.x1 ? p->x1 : r.x1;
		return painted_covers(a, i + 1, depth + 1) &&
			painted_covers(b2, i + 1, depth + 1) &&
			painted_covers(c, i + 1, depth + 1) &&
			painted_covers(d, i + 1, depth + 1);
	}
	return false;
}

static const struct plotter_table *comp_plotters = &fb_plotters;

/** the caret, a line into the band surface (within the piece's clip) */
static void paint_caret(const struct onyx_comp_view *v)
{
	nsfb_bbox_t line, clip;
	nsfb_plot_pen_t pen;

	if (!v->caret)
		return;
	line.x0 = line.x1 = v->cx - C.org_x;
	line.y0 = v->cy - C.org_y;
	line.y1 = v->cy + v->ch - C.org_y;
	clip.x0 = C.piece.x0;
	clip.y0 = C.piece.y0;
	clip.x1 = C.piece.x1;
	clip.y1 = C.piece.y1;
	if (line.x0 < clip.x0 || line.x0 >= clip.x1 || line.y1 <= clip.y0 ||
	    line.y0 >= clip.y1)
		return;
	nsfb_plot_set_clip(C.surf, &clip);
	pen.stroke_type = NFSB_PLOT_OPTYPE_SOLID;
	pen.stroke_width = 1;
	pen.stroke_colour = 0xFF0000FF;
	nsfb_plot_line(C.surf, &line, &pen);
}

/** document rectangle r (within one period of the ring, in the band's columns) painted */
static void paint_piece(const struct onyx_comp_view *v, struct crect r)
{
	int oy = fdiv(r.y0, C.bh) * C.bh;
	struct redraw_context ctx = {
		.interactive = true,
		.background_images = true,
		.plot = comp_plotters
	};
	struct rect clip;
	nsfb_t *was;

	C.org_x = C.bx;
	C.org_y = oy;
	C.piece.x0 = r.x0 - C.bx;
	C.piece.y0 = r.y0 - oy;
	C.piece.x1 = r.x1 - C.bx;
	C.piece.y1 = r.y1 - oy;
	clip.x0 = C.piece.x0;
	clip.y0 = C.piece.y0;
	clip.x1 = C.piece.x1;
	clip.y1 = C.piece.y1;
	C.last_offered = -1;
	for (int i = 0; i < C.nlayers; i++) {
		C.layers[i]->conflict = false;
		C.layers[i]->in_piece = false;
	}

	was = framebuffer_set_surface(C.surf);
	C.painting = true;
	onyx_comp_noting = true;
	browser_window_redraw(v->bw, -C.bx, -oy, &clip, &ctx);
	onyx_comp_noting = false;
	paint_caret(v);
	C.painting = false;
	framebuffer_set_surface(was);

	gpc_tex_update(C.g, C.tex, C.piece.x0, C.piece.y0, C.piece.x1 - C.piece.x0,
			C.piece.y1 - C.piece.y0,
			C.px + (size_t) C.piece.y0 * C.bw + C.piece.x0, C.bw);
	painted_add(&r);

	/* a layer something was painted over: in place from now on */
	for (int i = 0; i < C.nlayers; i++) {
		struct clayer *l = C.layers[i];
		if (!l->conflict)
			continue;
		l->conflict = false;
		l->demoted = true;
		layer_drop_pixels(l);
		damage_add(l->fp);
	}
}

/** a document rectangle painted (split at the ring's period) */
static void paint_rect(const struct onyx_comp_view *v, struct crect r)
{
	if (r.x0 < C.bx) r.x0 = C.bx;
	if (r.x1 > C.bx + C.bw) r.x1 = C.bx + C.bw;
	while (!cr_empty(&r)) {
		struct crect p = r;
		int end = (fdiv(r.y0, C.bh) + 1) * C.bh;
		if (p.y1 > end)
			p.y1 = end;
		paint_piece(v, p);
		r.y0 = p.y1;
	}
}

/** the rows [a, b) made valid (painted where they are not): the band's run stays one run */
static void band_cover(const struct onyx_comp_view *v, int a, int b)
{
	struct crect r = { C.bx, 0, C.bx + C.bw, 0 };

	if (b - a > C.bh)
		b = a + C.bh;
	if (C.vy1 <= C.vy0 || a > C.vy1 || b < C.vy0) {
		C.vy0 = C.vy1 = a;	/* (apart from the run: a new one) */
		C.ndmg = 0;
	}
	if (a < C.vy0) {
		if (C.vy1 > a + C.bh)
			C.vy1 = a + C.bh;	/* (the ring's rows the new ones take) */
		r.y0 = a;
		r.y1 = C.vy0;
		C.vy0 = a;
		paint_rect(v, r);
	}
	if (b > C.vy1) {
		if (C.vy0 < b - C.bh)
			C.vy0 = b - C.bh;
		r.y0 = C.vy1;
		r.y1 = b;
		C.vy1 = b;
		paint_rect(v, r);
	}
	/* (damage out of the run: gone with it) */
	for (int i = 0; i < C.ndmg; i++) {
		if (C.dmg[i].y0 < C.vy0) C.dmg[i].y0 = C.vy0;
		if (C.dmg[i].y1 > C.vy1) C.dmg[i].y1 = C.vy1;
	}
}

/** the document's extent the band may hold (its rows) */
static void doc_extent(const struct onyx_comp_view *v, int *top, int *bottom)
{
	int w = 0, h = 0;

	browser_window_get_extents(v->bw, true, &w, &h);
	*top = 0;
	*bottom = h > v->sy + v->h ? h : v->sy + v->h;
}

/* ---- the frame ----------------------------------------------------------------------------- */

static void reupload_all(void)
{
	/* the GPU was lost: everything again (the CPU path now) */
	if (C.tex != NULL)
		gpc_tex_update(C.g, C.tex, 0, 0, C.bw, C.bh, C.px, C.bw);
	for (int i = 0; i < C.nlayers; i++) {
		struct clayer *l = C.layers[i];
		if (l->tex != NULL && l->px != NULL)
			gpc_tex_update(C.g, l->tex, 0, 0, l->b.x1 - l->b.x0,
					l->b.y1 - l->b.y0, l->px, l->b.x1 - l->b.x0);
	}
}

#define FRAME_LAYERS 64

static void present(const struct onyx_comp_view *v)
{
	gpc_layer L[FRAME_LAYERS];
	int n = 0, stride, cw, ch, r, first, row;
	unsigned *canvas = onyx_surface_canvas(&stride, &cw, &ch);
	gpc_target t;
	struct crect view = { v->sx, v->sy, v->sx + v->w, v->sy + v->h };
	uint64_t t0 = onyx_perf_now();

	if (canvas == NULL || v->x < 0 || v->y < 0 || v->x + v->w > cw || v->y + v->h > ch)
		return;
	t.pixels = canvas + (size_t) v->y * stride + v->x;
	t.w = v->w;
	t.h = v->h;
	t.stride = stride;
	t.flags = 0;

	/* the band's rows in view: one or two pieces of the ring */
	row = v->sy - fdiv(v->sy, C.bh) * C.bh;
	first = C.bh - row;
	if (first > v->h)
		first = v->h;
	gpc_layer_init(&L[n], C.tex);
	L[n].src_x = (float) (v->sx - C.bx);
	L[n].src_y = (float) row;
	L[n].src_w = (float) v->w;
	L[n].src_h = (float) first;
	L[n].flags = GPC_L_OPAQUE | GPC_L_NEAREST;
	n++;
	if (first < v->h) {
		gpc_layer_init(&L[n], C.tex);
		L[n].src_x = (float) (v->sx - C.bx);
		L[n].src_y = 0;
		L[n].src_w = (float) v->w;
		L[n].src_h = (float) (v->h - first);
		L[n].m.f = (float) first;
		L[n].flags = GPC_L_OPAQUE | GPC_L_NEAREST;
		n++;
	}
	/* the layers over it, in their order */
	for (int i = 0; i < C.nlayers && n < FRAME_LAYERS; i++) {
		struct clayer *l = C.layers[i];
		gpc_layer *g = &L[n];
		struct crect c;
		unsigned op;

		if (l->demoted || l->tex == NULL || !cr_meet(&l->fp, &view))
			continue;
		op = (unsigned) (l->opacity * 255 + 0.5f);
		if (op == 0)
			continue;
		gpc_layer_init(g, l->tex);
		g->opacity = op > 255 ? 255 : op;
		if (l->transformed) {
			/* layer px -> document (its rectangle's origin, then m) -> view */
			g->m.a = l->m[0];
			g->m.b = l->m[1];
			g->m.c = l->m[2];
			g->m.d = l->m[3];
			g->m.e = l->m[0] * l->b.x0 + l->m[2] * l->b.y0 + l->m[4] - v->sx;
			g->m.f = l->m[1] * l->b.x0 + l->m[3] * l->b.y0 + l->m[5] - v->sy;
		} else {
			g->m.e = (float) (l->b.x0 - v->sx);
			g->m.f = (float) (l->b.y0 - v->sy);
			g->flags |= GPC_L_NEAREST;	/* (whole pixels: as painted) */
		}
		c = cr_and(l->clip, &view);
		if (cr_empty(&c))
			continue;
		g->clip[0] = c.x0 - v->sx;
		g->clip[1] = c.y0 - v->sy;
		g->clip[2] = c.x1 - c.x0;
		g->clip[3] = c.y1 - c.y0;
		n++;
	}
	r = gpc_composite(C.g, &t, L, n, 0, 0);
	if (r == GPC_LOST) {
		fprintf(stderr, "netsurf: compositing: %s\n", gpc_info(C.g));
		reupload_all();
		gpc_composite(C.g, &t, L, n, 0, 0);
	}
	onyx_surface_hole(v->x, v->y, v->x + v->w, v->y + v->h);
	onyx_chrome_present_later();
	if (t0 != 0) {
		char what[64];
		snprintf(what, sizeof what, "present %dx%d %s %d layers", v->w, v->h,
				gpc_backend(C.g) == GPC_BACKEND_GPU ? "gpu" : "cpu", n);
		onyx_perf_log(what, t0);
	}
}

/** the damage painted; the layers painted over (demoted) or gone painted again; stale ones
 * dropped */
static void paint_damage(const struct onyx_comp_view *v)
{
	for (int loop = 0; loop < UPDATE_LOOPS && C.ndmg > 0; loop++) {
		struct crect d[DAMAGE_MAX];
		int n = C.ndmg;

		memcpy(d, C.dmg, sizeof(d[0]) * n);
		C.ndmg = 0;
		for (int i = 0; i < n; i++)
			paint_rect(v, d[i]);
		/* layers not offered where they were painted over: gone (their
		 * box has no effect now, or no box) -- dropped when the band was
		 * painted over all they cover, else that painted first */
		for (int i = 0; i < C.nlayers; i++) {
			struct clayer *l = C.layers[i];
			struct crect f;
			bool met = false;

			if (l->fresh && !l->demoted) {
				/* (a layer new in this update: the band painted again
				 * where it shows and was painted before -- its box may
				 * be there, painted in place) */
				l->fresh = false;
				f = l->fp;
				if (f.x0 < C.bx) f.x0 = C.bx;
				if (f.x1 > C.bx + C.bw) f.x1 = C.bx + C.bw;
				if (f.y0 < C.vy0) f.y0 = C.vy0;
				if (f.y1 > C.vy1) f.y1 = C.vy1;
				if (!painted_covers(f, 0, 0))
					damage_add(f);
				continue;
			}
			if (l->seen == C.gen || l->demoted)
				continue;
			f = l->fp;
			if (f.x0 < C.bx) f.x0 = C.bx;
			if (f.x1 > C.bx + C.bw) f.x1 = C.bx + C.bw;
			if (f.y0 < C.vy0) f.y0 = C.vy0;
			if (f.y1 > C.vy1) f.y1 = C.vy1;
			if (cr_empty(&f))
				continue;
			for (int k = 0; k < C.npainted && k < PAINTED_MAX; k++)
				if (cr_meet(&f, &C.painted[k]))
					met = true;
			if (!met)
				continue;
			if (painted_covers(f, 0, 0)) {
				layer_remove(i);
				i--;
			} else {
				damage_add(f);
			}
		}
	}
}

/** reset: the band's run collapses to the view (painted now; the rest ahead, later); the
 * layers' pixels painted again when offered; the layers out of view forgotten */
static void comp_reset(const struct onyx_comp_view *v)
{
	struct crect view = { v->sx, v->sy, v->sx + v->w, v->sy + v->h };

	C.vy0 = C.vy1 = v->sy;
	C.ndmg = 0;
	for (int i = 0; i < C.nlayers; i++) {
		struct clayer *l = C.layers[i];
		if (!cr_meet(&l->fp, &view)) {
			layer_remove(i);
			i--;
			continue;
		}
		l->demoted = false;	/* (a new layout: offered again) */
		l->dmg = l->b;
	}
}

/* exported interface documented in framebuffer/onyx_comp.h */
bool onyx_comp_redraw(const struct onyx_comp_view *v, bool prepaint)
{
	int top, bottom, want0, want1, margin;
	uint64_t t0 = onyx_perf_now();

	if (!C.on)
		return false;
	if (v->w <= 0 || v->h <= 0)
		return false;
	C.view = *v;
	C.have_view = true;
	if (C.px == NULL || v->w != C.vw || v->h != C.vh || v->bw != C.bwin) {
		C.vw = v->w;
		C.vh = v->h;
		C.bwin = v->bw;
		for (int i = C.nlayers - 1; i >= 0; i--)
			layer_remove(i);
		if (!band_make(v->w, v->h)) {
			/* no band: the view painted as ever (onyx_comp_on false) */
			fprintf(stderr, "netsurf: compositing: no memory for the "
					"band: off\n");
			onyx_comp_finalise();
			return false;
		}
		C.bx = v->sx;
		C.reset = true;
	}
	if (v->sx != C.bx) {
		C.bx = v->sx;
		C.reset = true;		/* (a sideways scroll: a new band) */
	}
	C.gen++;
	C.npainted = 0;
	if (C.reset) {
		C.reset = false;
		comp_reset(v);
	}
	doc_extent(v, &top, &bottom);

	/* the view's rows first, then the damage */
	band_cover(v, v->sy, v->sy + v->h);
	paint_damage(v);
	if (!prepaint || C.npainted > 0 || C.ndmg > 0) {
		present(v);
		if (t0 != 0) {
			char what[64];
			snprintf(what, sizeof what, "frame %s", C.npainted ?
					"painted" : "composite");
			onyx_perf_log(what, t0);
		}
	}

	/* the rows around the view, painted ahead (a piece a turn) */
	margin = (C.bh - v->h) / 2;
	want0 = v->sy - margin;
	want1 = v->sy + v->h + margin;
	if (v->dy > 0) {
		/* (scrolling down: more below) */
		want1 += want0 - top < 0 ? 0 : margin / 2;
		want0 += margin / 2;
	} else if (v->dy < 0) {
		want0 -= margin / 2;
		want1 -= margin / 2;
	}
	if (want0 < top)
		want0 = top;
	if (want1 > bottom)
		want1 = bottom;
	if (want1 - want0 > C.bh) {
		if (v->dy < 0)
			want1 = want0 + C.bh;
		else
			want0 = want1 - C.bh;
	}
	if (want0 >= C.vy0 && want1 <= C.vy1)
		return false;
	if (prepaint && C.npainted == 0) {
		int chunk = v->h / 3 < 96 ? 96 : v->h / 3;
		uint64_t t1 = onyx_perf_now();
		bool below = want1 > C.vy1 &&
			(v->dy >= 0 || want0 >= C.vy0);
		if (below)
			band_cover(v, C.vy1, C.vy1 + chunk < want1 ? C.vy1 + chunk : want1);
		else
			band_cover(v, want0 > C.vy0 - chunk ? want0 : C.vy0 - chunk, C.vy0);
		paint_damage(v);
		onyx_perf_log("band ahead", t1);
	}
	return !(want0 >= C.vy0 && want1 <= C.vy1);
}

/* ---- the retained layers' protocol (onyx_layer.c, framebuffer.c) --------------------------- */

/* exported interface documented in framebuffer/onyx_comp.h */
void onyx_comp_note(nsfb_t *surface, int x0, int y0, int x1, int y1)
{
	struct crect r;

	if (!C.painting || surface != C.surf || C.passing != NULL || C.nlayers == 0)
		return;
	r.x0 = (x0 > C.piece.x0 ? x0 : C.piece.x0) + C.org_x;
	r.y0 = (y0 > C.piece.y0 ? y0 : C.piece.y0) + C.org_y;
	r.x1 = (x1 < C.piece.x1 ? x1 : C.piece.x1) + C.org_x;
	r.y1 = (y1 < C.piece.y1 ? y1 : C.piece.y1) + C.org_y;
	if (cr_empty(&r))
		return;
	/* (the layers offered in this piece so far: painted under this) */
	for (int i = 0; i < C.nlayers; i++) {
		struct clayer *l = C.layers[i];
		if (l->seen == C.gen && !l->demoted && l->px != NULL &&
		    l->in_piece && cr_meet(&r, &l->fp))
			l->conflict = true;
	}
}

/** a side of the core's clip on the piece's edge: not known (the piece's, not the layer's) */
static struct crect offer_clip(const struct onyx_layer *l)
{
	struct crect c;

	c.x0 = l->cx0 <= C.piece.x0 ? -OPEN : l->cx0 + C.org_x;
	c.y0 = l->cy0 <= C.piece.y0 ? -OPEN : l->cy0 + C.org_y;
	c.x1 = l->cx1 >= C.piece.x1 ? OPEN : l->cx1 + C.org_x;
	c.y1 = l->cy1 >= C.piece.y1 ? OPEN : l->cy1 + C.org_y;
	return c;
}

static bool layers_grow(void)
{
	if (C.nlayers == C.layers_cap) {
		int cap = C.layers_cap ? C.layers_cap * 2 : 32;
		struct clayer **n = realloc(C.layers, sizeof(*n) * cap);
		if (n == NULL)
			return false;
		C.layers = n;
		C.layers_cap = cap;
	}
	return true;
}

/** key painted in place (not offered again until a reset) */
static void layer_demote(const void *key, int i)
{
	struct clayer *e;

	if (i < 0) {
		if (!layers_grow() || (e = calloc(1, sizeof(*e))) == NULL)
			return;
		e->key = key;
		C.layers[C.nlayers++] = e;
	} else {
		e = C.layers[i];
		layer_drop_pixels(e);
	}
	e->demoted = true;
}

/** room for px more pixels of layers: those out of the band's valid rows dropped first */
static bool layers_room(long long px)
{
	for (int i = 0; i < C.nlayers && C.layers_px + px > LAYERS_BUDGET_PX; i++) {
		struct clayer *l = C.layers[i];
		if (l->px != NULL && (l->fp.y1 <= C.vy0 || l->fp.y0 >= C.vy1)) {
			layer_drop_pixels(l);
			l->dmg = l->b;
		}
	}
	return C.layers_px + px <= LAYERS_BUDGET_PX;
}

/* exported interface documented in framebuffer/onyx_comp.h */
bool onyx_comp_offer(struct onyx_layer *l)
{
	struct crect b, clip, d;
	struct clayer *e;
	long long area;
	int i;
	bool fresh = false;

	l->rx0 = l->ry0 = l->rx1 = l->ry1 = 0;
	/* (a composited redraw into the band only: not in a group painted by the CPU,
	 * nor under a rounded clip -- its corners are the band's) */
	if (!C.painting || C.passing != NULL || onyx_fb_layer_depth() > 0 ||
	    onyx_fb_round_clip_depth() > 0 || l->key == NULL)
		return false;
	i = layer_find(l->key);
	if (i >= 0 && C.layers[i]->demoted)
		return false;
	b.x0 = l->x0 + C.org_x;
	b.y0 = l->y0 + C.org_y;
	b.x1 = l->x1 + C.org_x;
	b.y1 = l->y1 + C.org_y;
	area = cr_area(&b);
	if (area <= 0 || area > LAYER_MAX_PX) {
		layer_demote(l->key, i);
		return false;
	}
	clip = offer_clip(l);

	if (i < 0) {
		if (!layers_grow() || (e = calloc(1, sizeof(*e))) == NULL)
			return false;
		e->key = l->key;
		C.layers[C.nlayers++] = e;
		i = C.nlayers - 1;
		fresh = true;
	}
	e = C.layers[i];

	/* its pixels: a new rectangle (or tree) -> all painted again */
	if (e->px == NULL || e->tree != l->tree || e->b.x0 != b.x0 || e->b.y0 != b.y0 ||
	    e->b.x1 != b.x1 || e->b.y1 != b.y1) {
		int w = b.x1 - b.x0, h = b.y1 - b.y0;
		layer_drop_pixels(e);
		if (!layers_room(area) || (e->px = calloc((size_t) w * h, 4)) == NULL) {
			layer_demote(l->key, i);
			return false;
		}
		C.layers_px += area;
		e->b = b;
		e->tex = gpc_tex_create(C.g, w, h, NULL, w);
		if (e->tex == NULL) {
			layer_demote(l->key, i);
			return false;
		}
		e->dmg = b;
		e->tree = l->tree;
	}
	/* its properties (target px -> document px) */
	e->transformed = l->transformed;
	memcpy(e->lm, l->lm, sizeof(e->lm));
	e->ox = l->ox + C.org_x;
	e->oy = l->oy + C.org_y;
	if (e->transformed)
		mat_about(e->lm, e->ox, e->oy, e->m);
	e->opacity = l->opacity;
	/* (a side of the clip known now replaces one not known) */
	if (fresh || clip.x0 != -OPEN) e->clip.x0 = clip.x0;
	if (fresh || clip.y0 != -OPEN) e->clip.y0 = clip.y0;
	if (fresh || clip.x1 != OPEN) e->clip.x1 = clip.x1;
	if (fresh || clip.y1 != OPEN) e->clip.y1 = clip.y1;
	layer_footprint(e);
	e->seen = C.gen;
	e->in_piece = true;
	e->fresh = e->fresh || fresh;

	/* its painting order: after the layer offered before it */
	if (C.last_offered >= 0 && i < C.last_offered) {
		int j = C.last_offered;
		memmove(C.layers + i, C.layers + i + 1, sizeof(*C.layers) * (j - i));
		C.layers[j] = e;
		i = j;
	}
	C.last_offered = i;

	/* the part of its pixels to paint */
	d = cr_and(e->dmg, &e->b);
	e->dmg.x0 = e->dmg.x1 = 0;
	if (!cr_empty(&d)) {
		l->rx0 = d.x0 - C.org_x;
		l->ry0 = d.y0 - C.org_y;
		l->rx1 = d.x1 - C.org_x;
		l->ry1 = d.y1 - C.org_y;
		C.passing = e;
	}
	return true;
}

/* exported interface documented in framebuffer/onyx_comp.h */
void onyx_comp_store(const struct onyx_layer *l, const uint32_t *argb, int x0, int y0,
		int w, int h)
{
	struct clayer *e = C.passing;
	int lw, lx, ly;

	if (e == NULL || e->px == NULL || l->key != e->key)
		return;
	lw = e->b.x1 - e->b.x0;
	lx = x0 + C.org_x - e->b.x0;
	ly = y0 + C.org_y - e->b.y0;
	if (lx < 0 || ly < 0 || lx + w > lw || ly + h > e->b.y1 - e->b.y0)
		return;
	for (int y = 0; y < h; y++)
		memcpy(e->px + (size_t) (ly + y) * lw + lx, argb + (size_t) y * w,
				(size_t) w * 4);
	gpc_tex_update(C.g, e->tex, lx, ly, w, h, e->px + (size_t) ly * lw + lx, lw);
}

/* exported interface documented in framebuffer/onyx_comp.h */
void onyx_comp_offer_end(const struct onyx_layer *l)
{
	(void) l;
	C.passing = NULL;
}

/* exported interface documented in framebuffer/onyx_comp.h */
bool onyx_comp_layer_update(const void *key, const float *lm, float opacity)
{
	struct clayer *e;
	struct crect fp;
	float m[6];
	int i = layer_find(key);

	if (!C.on || !C.have_view || C.painting || i < 0)
		return false;
	e = C.layers[i];
	if (e->demoted || e->px == NULL || (lm != NULL) != e->transformed)
		return false;
	if (lm != NULL) {
		mat_about(lm, e->ox, e->oy, m);
		fp = cr_and(mat_bbox(m, &e->b), &e->clip);
		/* (it may only cover what it covered: nothing painted after it
		 * lies over that) */
		if (fp.x0 < e->fp.x0 || fp.y0 < e->fp.y0 || fp.x1 > e->fp.x1 ||
		    fp.y1 > e->fp.y1)
			return false;
		memcpy(e->lm, lm, sizeof(e->lm));
		memcpy(e->m, m, sizeof(e->m));
	}
	e->opacity = opacity;
	present(&C.view);
	return true;
}
