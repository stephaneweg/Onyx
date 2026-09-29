/*
 * Copyright 2005 James Bursa <bursa@users.sourceforge.net>
 *           2008 Vincent Sanders <vince@simtec.co.uk>
 *
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

/*
 * Onyx: the fonts, found by their CSS names (font-family) as a browser does:
 *
 *  - a document's web fonts (@font-face, fetched by content/handlers/html/onyx_webfont.c
 *    and handed over by fb_font_add_face), while that document is the scope;
 *  - the card's fonts (/res/fonts), under their names and those of the fonts they stand
 *    in for, metric for metric: Liberation Sans for Arial, Liberation Serif for Times New
 *    Roman, Selawik for Segoe UI, Gelasio for Georgia (fb_card_faces, fb_font_aliases);
 *  - the generic families as Chrome's on Windows: serif Times New Roman, sans-serif Arial,
 *    monospace Consolas (DejaVu Sans Mono here);
 *
 * and, for a character a face lacks, the next of the list, then DejaVu Sans. In a family,
 * the face is chosen as CSS Fonts says (style, then the nearest weight); an italic with
 * no italic face is its roman slanted. Advances are unhinted -- fractional, as a browser
 * lays text out -- each glyph drawn at its pen position rounded; glyphs hinted lightly
 * (vertically only). letter-spacing, word-spacing and small-caps are applied here.
 */

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include <ft2build.h>
#include FT_CACHE_H
#include FT_ADVANCES_H
#include FT_MULTIPLE_MASTERS_H
#include FT_TRUETYPE_TABLES_H

#include "netsurf/inttypes.h"
#include "utils/filepath.h"
#include "utils/utf8.h"
#include "utils/log.h"
#include "utils/nsoption.h"
#include "netsurf/utf8.h"
#include "netsurf/layout.h"
#include "netsurf/browser.h"
#include "netsurf/plot_style.h"

#include "framebuffer/gui.h"
#include "framebuffer/font.h"
#include "framebuffer/findfile.h"

/* glyph cache minimum size */
#define CACHE_MIN_SIZE (100 * 1024)

static FT_Library library;
static FTC_Manager ft_cmanager;
static FTC_CMapCache ft_cmap_cache ;
static FTC_ImageCache ft_image_cache;

int ft_load_type;

/* cache manager faceID data to create freetype faceid on demand */
typedef struct fb_faceid_s {
	char *fontfile;		/* path to font (Onyx: NULL for a web font) */
	int index;		/* index of font */
	int cidx;		/* character map index for unicode */
	/* Onyx */
	unsigned char *data;	/* a web font's file (its own copy) */
	size_t size;
	int wmin, wmax;		/* its weights (a range: a variable font) */
	bool italic;		/* italic (or oblique) */
	bool slant;		/* a roman face slanted (its family has no italic) */
	int var_weight;		/* a variable font: its wght axis set to it (0: not) */
	struct fb_faceid_s *slanted;	/* this roman face's slanted copy, once made */
	struct fb_faceid_s *variants;	/* a range's instances made (var_weight) */
	struct fb_faceid_s *variant_next;
	struct fb_faceid_s *origin;	/* a copy (slanted, instance): its family's face */
	int v_asc, v_desc, v_gap;	/* its vertical metrics, design units (v_upm: 0 */
	int v_upm;			/* until read, fb_face_vmetrics) */
} fb_faceid_t;

/** Onyx: a family -- the card's, or a document's web fonts' */
struct fb_family {
	struct fb_family *next;
	char *name;			/* lower case */
	const void *owner;		/* a web font's document; NULL: the card's */
	int nfaces;
	fb_faceid_t **face;
};

static struct fb_family *fb_families;	/* the web's (first), then the card's */
static const void *fb_scope;		/* the document whose web fonts are in use */
static unsigned fb_generation = 1;	/* +1 when a web font comes or goes */

/** the card's fonts (/res/fonts), by family; a file missing is skipped */
static const struct fb_card_face {
	const char *family;
	const char *file;
	int weight;
	bool italic;
	int asc, desc, upm;	/* vertical metrics of the font it stands in for (0: its own) */
} fb_card_faces[] = {
	/* Liberation 2 (SIL OFL): Arial's and Times New Roman's metrics */
	{ "liberation sans", "LiberationSans-Regular.ttf", 400, false },
	{ "liberation sans", "LiberationSans-Bold.ttf", 700, false },
	{ "liberation sans", "LiberationSans-Italic.ttf", 400, true },
	{ "liberation sans", "LiberationSans-BoldItalic.ttf", 700, true },
	{ "liberation serif", "LiberationSerif-Regular.ttf", 400, false },
	{ "liberation serif", "LiberationSerif-Bold.ttf", 700, false },
	{ "liberation serif", "LiberationSerif-Italic.ttf", 400, true },
	{ "liberation serif", "LiberationSerif-BoldItalic.ttf", 700, true },
	/* Selawik (SIL OFL, Microsoft): Segoe UI's -- no italic (slanted) */
	{ "selawik", "selawkl.ttf", 300, false },
	{ "selawik", "selawksl.ttf", 350, false },
	{ "selawik", "selawk.ttf", 400, false },
	{ "selawik", "selawksb.ttf", 600, false },
	{ "selawik", "selawkb.ttf", 700, false },
	/* Gelasio (SIL OFL): Georgia's widths -- and, here, its heights (Gelasio's are taller) */
	{ "gelasio", "Gelasio-Regular.ttf", 400, false, 1878, 449, 2048 },
	{ "gelasio", "Gelasio-Bold.ttf", 700, false, 1878, 449, 2048 },
	{ "gelasio", "Gelasio-Italic.ttf", 400, true, 1878, 449, 2048 },
	{ "gelasio", "Gelasio-BoldItalic.ttf", 700, true, 1878, 449, 2048 },
	/* DejaVu: the fallback, for its many characters */
	{ "dejavu sans", NETSURF_FB_FONT_SANS_SERIF, 400, false },
	{ "dejavu sans", NETSURF_FB_FONT_SANS_SERIF_BOLD, 700, false },
	{ "dejavu sans", NETSURF_FB_FONT_SANS_SERIF_ITALIC, 400, true },
	{ "dejavu sans", NETSURF_FB_FONT_SANS_SERIF_ITALIC_BOLD, 700, true },
	{ "dejavu serif", NETSURF_FB_FONT_SERIF, 400, false },
	{ "dejavu serif", NETSURF_FB_FONT_SERIF_BOLD, 700, false },
	{ "dejavu serif", "DejaVuSerif-Italic.ttf", 400, true },
	{ "dejavu serif", "DejaVuSerif-BoldItalic.ttf", 700, true },
	{ "dejavu sans mono", NETSURF_FB_FONT_MONOSPACE, 400, false },
	{ "dejavu sans mono", NETSURF_FB_FONT_MONOSPACE_BOLD, 700, false },
};

/** the fonts the card's stand in for (and other names of theirs) */
static const struct fb_font_alias {
	const char *name;
	const char *family;
} fb_font_aliases[] = {
	{ "arial", "liberation sans" },
	{ "helvetica", "liberation sans" },
	{ "helvetica neue", "liberation sans" },
	{ "arimo", "liberation sans" },
	{ "tahoma", "liberation sans" },
	{ "trebuchet ms", "liberation sans" },
	{ "times new roman", "liberation serif" },
	{ "times", "liberation serif" },
	{ "tinos", "liberation serif" },
	{ "segoe ui", "selawik" },
	{ "system-ui", "selawik" },
	{ "georgia", "gelasio" },
	{ "verdana", "dejavu sans" },
	{ "lucida sans unicode", "dejavu sans" },
	{ "lucida grande", "dejavu sans" },
	{ "bitstream vera sans", "dejavu sans" },
	{ "bitstream vera serif", "dejavu serif" },
	{ "courier new", "dejavu sans mono" },
	{ "courier", "dejavu sans mono" },
	{ "consolas", "dejavu sans mono" },
	{ "monaco", "dejavu sans mono" },
	{ "menlo", "dejavu sans mono" },
	{ "lucida console", "dejavu sans mono" },
	{ "bitstream vera sans mono", "dejavu sans mono" },
};

/** the generic families (plot_font_generic_family_t), as Chrome's on Windows */
static const char *const fb_generic_names[] = {
	[PLOT_FONT_FAMILY_SANS_SERIF] = "liberation sans",	/* Arial */
	[PLOT_FONT_FAMILY_SERIF] = "liberation serif",		/* Times New Roman */
	[PLOT_FONT_FAMILY_MONOSPACE] = "dejavu sans mono",	/* Consolas */
	[PLOT_FONT_FAMILY_CURSIVE] = "liberation sans",	/* (Comic Sans MS) */
	[PLOT_FONT_FAMILY_FANTASY] = "liberation sans",	/* (Impact) */
};
#define FB_GENERICS (sizeof(fb_generic_names) / sizeof(fb_generic_names[0]))

static struct fb_family *fb_generic[FB_GENERICS];
static struct fb_family *fb_fallback;	/* DejaVu Sans */

/** the slant of a slanted face: x += y / 4 (Chrome's synthetic oblique) */
#define FB_SLANT 0x4000

/**
 * map cache manager handle to face id
 */
static FT_Error
ft_face_requester(FTC_FaceID face_id,
		  FT_Library  library,
		  FT_Pointer request_data,
		  FT_Face *face )
{
        FT_Error error;
        fb_faceid_t *fb_face = (fb_faceid_t *)face_id;
        int cidx;

	if (fb_face->data != NULL) {	/* Onyx: a web font */
		error = FT_New_Memory_Face(library, fb_face->data,
				fb_face->size, fb_face->index, face);
	} else {
		error = FT_New_Face(library, fb_face->fontfile,
				fb_face->index, face);
	}
        if (error) {
                NSLOG(netsurf, INFO, "Could not find font (code %d)", error);
        } else {

                error = FT_Select_Charmap(*face, FT_ENCODING_UNICODE);
                if (error) {
                        NSLOG(netsurf, INFO,
                              "Could not select charmap (code %d)", error);
                } else {
                        for (cidx = 0; cidx < (*face)->num_charmaps; cidx++) {
                                if ((*face)->charmap == (*face)->charmaps[cidx]) {
                                        fb_face->cidx = cidx;
                                        break;
                                }
                        }
                }
		if (fb_face->slant) {	/* Onyx: an oblique made of a roman */
			FT_Matrix m = { 0x10000, FB_SLANT, 0, 0x10000 };
			FT_Set_Transform(*face, &m, NULL);
		}
		if (fb_face->var_weight != 0 && FT_HAS_MULTIPLE_MASTERS(*face)) {
			/* Onyx: a variable font at its weight */
			FT_MM_Var *mm;

			if (FT_Get_MM_Var(*face, &mm) == 0) {
				FT_Fixed coords[16];
				FT_UInt i, n = mm->num_axis < 16 ?
						mm->num_axis : 16;

				for (i = 0; i < n; i++) {
					FT_Var_Axis *a = &mm->axis[i];

					coords[i] = a->def;
					if (a->tag == FT_MAKE_TAG('w','g','h','t')) {
						FT_Fixed w = (FT_Fixed)
							fb_face->var_weight << 16;

						if (w < a->minimum)
							w = a->minimum;
						if (w > a->maximum)
							w = a->maximum;
						coords[i] = w;
					}
				}
				FT_Set_Var_Design_Coordinates(*face, n, coords);
				FT_Done_MM_Var(library, mm);
			}
		}
        }
        NSLOG(netsurf, INFO, "Loaded face from %s",
	      fb_face->fontfile != NULL ? fb_face->fontfile : "(web font)");

        return error;
}


/* ---- Onyx: the families ------------------------------------------------------ */

static struct fb_family *fb_family_new(const char *name, const void *owner)
{
	struct fb_family *f = calloc(1, sizeof(*f));
	size_t i;

	if (f == NULL)
		return NULL;
	f->name = strdup(name);
	if (f->name == NULL) {
		free(f);
		return NULL;
	}
	for (i = 0; f->name[i] != '\0'; i++)
		f->name[i] = tolower((unsigned char)f->name[i]);
	f->owner = owner;
	f->next = fb_families;
	fb_families = f;
	return f;
}

static bool fb_family_add(struct fb_family *f, fb_faceid_t *face)
{
	fb_faceid_t **n = realloc(f->face, (f->nfaces + 1) * sizeof(*n));

	if (n == NULL)
		return false;
	f->face = n;
	f->face[f->nfaces++] = face;
	return true;
}

/** the family of that name (lower case), the document's web fonts first */
static struct fb_family *fb_family_named(const char *name)
{
	struct fb_family *f, *card = NULL;

	for (f = fb_families; f != NULL; f = f->next) {
		if (strcmp(f->name, name) != 0)
			continue;
		if (f->owner == NULL)
			card = f;
		else if (f->owner == fb_scope)
			return f;
	}
	return card;
}

/** the family a CSS name means: a web font's, the card's, or one it stands in for */
static struct fb_family *fb_family_find(const char *css_name, size_t len)
{
	char name[64];
	struct fb_family *f;
	size_t i;

	if (len >= sizeof(name))
		return NULL;
	for (i = 0; i < len; i++)
		name[i] = tolower((unsigned char)css_name[i]);
	name[len] = '\0';

	f = fb_family_named(name);
	if (f != NULL)
		return f;
	for (i = 0; i < sizeof(fb_font_aliases) / sizeof(fb_font_aliases[0]); i++) {
		if (strcmp(fb_font_aliases[i].name, name) == 0)
			return fb_family_named(fb_font_aliases[i].family);
	}
	return NULL;
}

/** how far a face's weights are from the one wanted (CSS Fonts 4, 5.2): 0 is its own */
static int fb_weight_distance(int w, int lo, int hi)
{
	if (lo <= w && w <= hi)
		return 0;
	if (w >= 400 && w <= 500) {
		/* up to 500, then lighter, then heavier */
		if (lo > w && lo <= 500)
			return lo - w;
		if (hi < w)
			return 1000 + w - hi;
		return 2000 + lo - w;
	}
	if (w < 400) {
		/* lighter, then heavier */
		if (hi < w)
			return w - hi;
		return 1000 + lo - w;
	}
	/* heavier, then lighter */
	if (lo > w)
		return lo - w;
	return 1000 + w - hi;
}

/** a roman face's slanted copy (an italic of a family with none) */
static fb_faceid_t *fb_face_slanted(fb_faceid_t *roman)
{
	fb_faceid_t *s;

	if (roman->slanted != NULL)
		return roman->slanted;
	s = calloc(1, sizeof(*s));
	if (s == NULL)
		return roman;
	*s = *roman;
	s->fontfile = roman->fontfile != NULL ? strdup(roman->fontfile) : NULL;
	s->italic = true;
	s->slant = true;
	s->slanted = NULL;
	s->variants = s->variant_next = NULL;
	s->origin = roman->origin != NULL ? roman->origin : roman;
	roman->slanted = s;
	return s;
}

/** a variable face's instance at a weight of its range */
static fb_faceid_t *fb_face_instance(fb_faceid_t *face, int weight)
{
	fb_faceid_t *v;

	for (v = face->variants; v != NULL; v = v->variant_next) {
		if (v->var_weight == weight)
			return v;
	}
	v = calloc(1, sizeof(*v));
	if (v == NULL)
		return face;
	*v = *face;
	v->fontfile = face->fontfile != NULL ? strdup(face->fontfile) : NULL;
	v->wmin = v->wmax = v->var_weight = weight;
	v->slanted = NULL;
	v->variants = NULL;
	v->origin = face;
	v->variant_next = face->variants;
	face->variants = v;
	return v;
}

/** a family's face as used for a weight and style: its instance, slanted */
static fb_faceid_t *fb_face_use(fb_faceid_t *face, int weight, bool italic)
{
	if (face->wmin < face->wmax) {
		int w = weight < face->wmin ? face->wmin :
			weight > face->wmax ? face->wmax : weight;
		face = fb_face_instance(face, w);
	}
	if (italic && !face->italic)
		face = fb_face_slanted(face);
	return face;
}

/**
 * The face of a family for a weight and style: its italics for italic text (else its
 * romans -- fb_face_use slants them), the nearest weight. Web font faces of one style
 * and weight may cover different characters (unicode-range): the first is returned,
 * fb_chain_add_family adds the others after it.
 */
static fb_faceid_t *fb_family_match(struct fb_family *fam, int weight, bool italic,
		int *distance)
{
	fb_faceid_t *best = NULL;
	int i, best_d = 0;
	bool have = false, use;

	for (i = 0; i < fam->nfaces; i++) {
		if (fam->face[i]->italic == italic)
			have = true;
	}
	use = have ? italic : !italic;
	for (i = 0; i < fam->nfaces; i++) {
		fb_faceid_t *f = fam->face[i];
		int d;

		if (f->italic != use)
			continue;
		d = fb_weight_distance(weight, f->wmin, f->wmax);
		if (best == NULL || d < best_d) {
			best = f;
			best_d = d;
		}
	}
	if (distance != NULL)
		*distance = best_d;
	return best;
}


/* ---- Onyx: the faces a text is set in ------------------------------------------ */

#define FB_CHAIN_MAX 12

/** the faces to try for each character, in order */
struct fb_chain {
	int n;
	fb_faceid_t *face[FB_CHAIN_MAX];
};

static void fb_chain_add(struct fb_chain *c, fb_faceid_t *face)
{
	int i;

	if (face == NULL || c->n == FB_CHAIN_MAX)
		return;
	for (i = 0; i < c->n; i++) {
		if (c->face[i] == face)
			return;
	}
	c->face[c->n++] = face;
}

/** a family's faces for the weight and style: the best, and its equals (unicode-range) */
static void fb_chain_add_family(struct fb_chain *c, struct fb_family *fam, int weight,
		bool italic)
{
	fb_faceid_t *best;
	int i, d;

	if (fam == NULL)
		return;
	best = fb_family_match(fam, weight, italic, &d);
	if (best == NULL)
		return;
	fb_chain_add(c, fb_face_use(best, weight, italic));
	if (fam->owner == NULL)
		return;
	for (i = 0; i < fam->nfaces; i++) {
		fb_faceid_t *f = fam->face[i];

		if (f != best && f->italic == best->italic &&
		    fb_weight_distance(weight, f->wmin, f->wmax) == d)
			fb_chain_add(c, fb_face_use(f, weight, italic));
	}
}

/** the name map: a family name (an interned lwc_string, held) -> its family */
#define FB_NAMES 256
static struct fb_name {
	lwc_string *name;
	struct fb_family *family;
	const void *scope;
	unsigned generation;
} fb_names[FB_NAMES];

static struct fb_family *fb_family_of(lwc_string *name)
{
	uintptr_t h = ((uintptr_t)name >> 4) * 2654435761u;
	int i;

	for (i = 0; i < 8; i++) {
		struct fb_name *e = &fb_names[(h + i) % FB_NAMES];

		if (e->name == name) {
			if (e->scope != fb_scope ||
			    e->generation != fb_generation) {
				e->family = fb_family_find(lwc_string_data(name),
						lwc_string_length(name));
				e->scope = fb_scope;
				e->generation = fb_generation;
			}
			return e->family;
		}
		if (e->name == NULL || i == 7) {
			if (e->name != NULL)
				lwc_string_unref(e->name);
			e->name = lwc_string_ref(name);
			e->family = fb_family_find(lwc_string_data(name),
					lwc_string_length(name));
			e->scope = fb_scope;
			e->generation = fb_generation;
			return e->family;
		}
	}
	return NULL;
}

static void fb_chain_make(const plot_font_style_t *fstyle, struct fb_chain *c)
{
	bool italic = (fstyle->flags & (FONTF_ITALIC | FONTF_OBLIQUE)) != 0;
	lwc_string * const *name;

	c->n = 0;
	for (name = fstyle->families; name != NULL && *name != NULL; name++)
		fb_chain_add_family(c, fb_family_of(*name), fstyle->weight, italic);
	if ((unsigned)fstyle->family < FB_GENERICS)
		fb_chain_add_family(c, fb_generic[fstyle->family],
				fstyle->weight, italic);
	fb_chain_add_family(c, fb_fallback, fstyle->weight, italic);
}


/* ---- Onyx: glyphs and advances ------------------------------------------------- */

/** the advances met (unhinted, 16.16 px), by face, size and glyph */
#define FB_ADVANCES 8192
static struct fb_advance {
	const fb_faceid_t *face;
	FT_UInt size;		/* 26.6 pt */
	FT_UInt gi;
	FT_Fixed advance;
} fb_advances[FB_ADVANCES];

/** a string being set: its style, its faces, its size */
struct fb_run {
	const plot_font_style_t *fstyle;
	struct fb_chain chain;
	FT_UInt size;		/* 26.6 pt */
	FT_UInt small;		/* small-caps: the capitals' size for the lower case */
	FT_Fixed letter, word;	/* letter-spacing, word-spacing: 16.16 px */
};

/** a character's glyph */
struct fb_glyph {
	fb_faceid_t *face;
	FT_UInt gi;
	FT_UInt size;
};

static void fb_run_init(struct fb_run *run, const plot_font_style_t *fstyle)
{
	run->fstyle = fstyle;
	fb_chain_make(fstyle, &run->chain);
	run->size = (fstyle->size * 64) / PLOT_STYLE_SCALE;
	run->small = run->size * 7 / 10;
	run->letter = ((FT_Fixed)fstyle->letter_spacing * 65536) / PLOT_STYLE_SCALE;
	run->word = ((FT_Fixed)fstyle->word_spacing * 65536) / PLOT_STYLE_SCALE;
}

static void fb_scaler(FTC_ScalerRec *srec, fb_faceid_t *face, FT_UInt size)
{
	srec->face_id = (FTC_FaceID)face;
	srec->width = srec->height = size;
	srec->pixel = 0;
	srec->x_res = srec->y_res = browser_get_dpi();
}

static FT_Fixed fb_advance(fb_faceid_t *face, FT_UInt size, FT_UInt gi)
{
	uintptr_t h = (((uintptr_t)face >> 4) * 31 + size * 7) * 2654435761u + gi;
	struct fb_advance *e = &fb_advances[h % FB_ADVANCES];
	FTC_ScalerRec srec;
	FT_Size ftsize;
	FT_Fixed adv;

	if (e->face == face && e->size == size && e->gi == gi)
		return e->advance;
	fb_scaler(&srec, face, size);
	if (FTC_Manager_LookupSize(ft_cmanager, &srec, &ftsize) != 0 ||
	    FT_Get_Advance(ftsize->face, gi, FT_LOAD_NO_HINTING, &adv) != 0)
		return 0;
	e->face = face;
	e->size = size;
	e->gi = gi;
	e->advance = adv;
	return adv;
}

/** characters with no width (a soft hyphen, the zero width ones) */
static bool fb_invisible_char(uint32_t ucs4)
{
	return ucs4 == 0x00ad || (ucs4 >= 0x200b && ucs4 <= 0x200f) ||
		ucs4 == 0x2060 || ucs4 == 0xfeff;
}

/**
 * A character's glyph (in the first face of the run's having it) and its advance, the
 * spacings included: 16.16 px.
 */
static FT_Fixed fb_run_glyph(struct fb_run *run, uint32_t ucs4, struct fb_glyph *g)
{
	FT_Fixed adv;
	int i;

	g->face = NULL;
	g->gi = 0;
	g->size = run->size;
	if (fb_invisible_char(ucs4))
		return 0;
	if ((run->fstyle->flags & FONTF_SMALLCAPS) &&
	    ((ucs4 >= 'a' && ucs4 <= 'z') ||
	     (ucs4 >= 0xe0 && ucs4 <= 0xfe && ucs4 != 0xf7))) {
		ucs4 -= 0x20;
		g->size = run->small;
	}
	for (i = 0; i < run->chain.n; i++) {
		fb_faceid_t *f = run->chain.face[i];

		g->gi = FTC_CMapCache_Lookup(ft_cmap_cache, (FTC_FaceID)f,
				f->cidx, ucs4);
		if (g->gi != 0) {
			g->face = f;
			break;
		}
	}
	if (g->face == NULL) {
		if (ucs4 == 0xa0)	/* a font without a no-break space */
			return fb_run_glyph(run, ' ', g);
		if (run->chain.n == 0)
			return 0;
		g->face = run->chain.face[0];	/* its missing glyph */
	}
	adv = fb_advance(g->face, g->size, g->gi) + run->letter;
	if (ucs4 == ' ' || ucs4 == 0xa0)
		adv += run->word;
	return adv;
}

/** a glyph's image, rendered */
static FT_Glyph fb_glyph_image(struct fb_glyph *g)
{
	FTC_ScalerRec srec;
	FT_Glyph glyph;

	if (g->face == NULL)
		return NULL;
	fb_scaler(&srec, g->face, g->size);
	if (FTC_ImageCache_LookupScaler(ft_image_cache, &srec,
			FT_LOAD_RENDER | ft_load_type, g->gi, &glyph, NULL) != 0)
		return NULL;
	return glyph;
}

/** 16.16 px to the nearest pixel */
#define FB_PX(f) ((int)(((f) + 0x8000) >> 16))


/* ---- the faces' set up ------------------------------------------------------- */

/**
 * Onyx: the card's fonts registered (fb_card_faces) -- those whose file is found; the
 * files are opened on first use.
 */
static void fb_card_fonts(void)
{
	char buf[PATH_MAX];
	size_t i;

	for (i = 0; i < sizeof(fb_card_faces) / sizeof(fb_card_faces[0]); i++) {
		const struct fb_card_face *cf = &fb_card_faces[i];
		struct fb_family *fam;
		fb_faceid_t *face;
		FILE *fp;

		if (filepath_sfind(respaths, buf, cf->file) == NULL)
			continue;
		fp = fopen(buf, "rb");
		if (fp == NULL)
			continue;
		fclose(fp);

		face = calloc(1, sizeof(*face));
		if (face == NULL)
			continue;
		face->fontfile = strdup(buf);
		face->wmin = face->wmax = cf->weight;
		face->italic = cf->italic;
		if (cf->upm != 0) {
			face->v_asc = cf->asc;
			face->v_desc = cf->desc;
			face->v_gap = 0;
			face->v_upm = cf->upm;
		}
		fam = fb_family_named(cf->family);
		if (fam == NULL)
			fam = fb_family_new(cf->family, NULL);
		if (fam == NULL || face->fontfile == NULL ||
		    !fb_family_add(fam, face)) {
			free(face->fontfile);
			free(face);
		}
	}
}

/* exported interface documented in framebuffer/font.h */
bool fb_font_init(void)
{
        FT_Error error;
        FT_ULong max_cache_size;
        FT_UInt max_faces = 24;	/* (Onyx: the card's and the web's) */
	FT_Face aface;
	size_t i;

        /* freetype library initialise */
        error = FT_Init_FreeType( &library );
        if (error) {
                NSLOG(netsurf, INFO,
                      "Freetype could not initialised (code %d)", error);
                return false;
        }

        /* set the Glyph cache size up */
        max_cache_size = nsoption_int(fb_font_cachesize) * 1024;

	if (max_cache_size < CACHE_MIN_SIZE) {
		max_cache_size = CACHE_MIN_SIZE;
	}

        /* cache manager initialise -- Onyx: sizes for the many a page uses */
        error = FTC_Manager_New(library,
                                max_faces,
                                48,
                                max_cache_size,
                                ft_face_requester,
                                NULL,
                                &ft_cmanager);
        if (error) {
                NSLOG(netsurf, INFO,
                      "Freetype could not initialise cache manager (code %d)",
                      error);
                FT_Done_FreeType(library);
                return false;
        }

        error = FTC_CMapCache_New(ft_cmanager, &ft_cmap_cache);

        error = FTC_ImageCache_New(ft_cmanager, &ft_image_cache);

	/* Onyx: the card's fonts; DejaVu Sans, the fallback, must be there */
	fb_card_fonts();
	for (i = 0; i < FB_GENERICS; i++)
		fb_generic[i] = fb_family_named(fb_generic_names[i]);
	fb_fallback = fb_family_named("dejavu sans");
	if (fb_fallback == NULL || fb_fallback->nfaces == 0 ||
	    FTC_Manager_LookupFace(ft_cmanager,
			(FTC_FaceID)fb_fallback->face[0], &aface) != 0) {
                NSLOG(netsurf, INFO, "Could not find the default font");
                FTC_Manager_Done(ft_cmanager);
                FT_Done_FreeType(library);
                return false;
	}
	for (i = 0; i < FB_GENERICS; i++) {
		if (fb_generic[i] == NULL)
			fb_generic[i] = fb_fallback;
	}

        /* set the default render mode -- Onyx: hinted lightly (vertically) */
        if (nsoption_bool(fb_font_monochrome) == true)
                ft_load_type = FT_LOAD_MONOCHROME | FT_LOAD_TARGET_MONO;
        else
                ft_load_type = FT_LOAD_TARGET_LIGHT;

        return true;
}

/** a copy of a face (slanted, an instance) dropped: not its file, the face's */
static void fb_face_drop(fb_faceid_t *copy)
{
	FTC_Manager_RemoveFaceID(ft_cmanager, (FTC_FaceID)copy);
	free(copy->fontfile);
	free(copy);
}

static void fb_face_free(fb_faceid_t *face)
{
	fb_faceid_t *v, *next;

	for (v = face->variants; v != NULL; v = next) {
		next = v->variant_next;
		if (v->slanted != NULL)
			fb_face_drop(v->slanted);
		fb_face_drop(v);
	}
	if (face->slanted != NULL)
		fb_face_drop(face->slanted);
	FTC_Manager_RemoveFaceID(ft_cmanager, (FTC_FaceID)face);
	free(face->fontfile);
	free(face->data);
	free(face);
}

/* exported interface documented in framebuffer/font.h */
bool fb_font_finalise(void)
{
	struct fb_family *f, *next;
	int i;

	for (f = fb_families; f != NULL; f = next) {
		next = f->next;
		for (i = 0; i < f->nfaces; i++)
			fb_face_free(f->face[i]);
		free(f->face);
		free(f->name);
		free(f);
	}
	fb_families = NULL;
	for (i = 0; i < FB_NAMES; i++) {
		if (fb_names[i].name != NULL)
			lwc_string_unref(fb_names[i].name);
		fb_names[i].name = NULL;
	}

        FTC_Manager_Done(ft_cmanager);
        FT_Done_FreeType(library);

        return true;
}


/* ---- Onyx: web fonts -------------------------------------------------------------- */

/* exported interface documented in framebuffer/font.h */
nserror fb_font_add_face(const void *owner, const char *family, int weight_min,
		int weight_max, bool italic, const uint8_t *data, size_t size)
{
	struct fb_family *fam = NULL, *f;
	fb_faceid_t *face;
	FT_Face aface;
	char name[64];
	size_t i;

	if (owner == NULL || family == NULL || data == NULL || size == 0 ||
	    strlen(family) >= sizeof(name))
		return NSERROR_BAD_PARAMETER;
	for (i = 0; family[i] != '\0'; i++)
		name[i] = tolower((unsigned char)family[i]);
	name[i] = '\0';

	face = calloc(1, sizeof(*face));
	if (face == NULL)
		return NSERROR_NOMEM;
	face->data = malloc(size);
	if (face->data == NULL) {
		free(face);
		return NSERROR_NOMEM;
	}
	memcpy(face->data, data, size);
	face->size = size;
	face->wmin = weight_min;
	face->wmax = weight_max < weight_min ? weight_min : weight_max;
	face->italic = italic;
	/* one weight of a variable font (a face per weight, one file): at it */
	if (face->wmin == face->wmax)
		face->var_weight = face->wmin;

	/* a font FreeType reads (TrueType, OpenType; WOFF, WOFF2 when built in) */
	if (FTC_Manager_LookupFace(ft_cmanager, (FTC_FaceID)face, &aface) != 0) {
		FTC_Manager_RemoveFaceID(ft_cmanager, (FTC_FaceID)face);
		free(face->data);
		free(face);
		return NSERROR_INVALID;
	}

	for (f = fb_families; f != NULL; f = f->next) {
		if (f->owner == owner && strcmp(f->name, name) == 0) {
			fam = f;
			break;
		}
	}
	if (fam == NULL)
		fam = fb_family_new(name, owner);
	if (fam == NULL || !fb_family_add(fam, face)) {
		fb_face_free(face);
		return NSERROR_NOMEM;
	}
	fb_generation++;
	NSLOG(netsurf, INFO, "web font %s %d-%d%s: %zu bytes", name, face->wmin,
	      face->wmax, italic ? " italic" : "", size);
	return NSERROR_OK;
}

/* exported interface documented in framebuffer/font.h */
void fb_font_release_faces(const void *owner)
{
	struct fb_family **pf = &fb_families, *f;
	bool any = false;
	int i;

	if (owner == NULL)
		return;
	while ((f = *pf) != NULL) {
		if (f->owner != owner) {
			pf = &f->next;
			continue;
		}
		*pf = f->next;
		for (i = 0; i < f->nfaces; i++)
			fb_face_free(f->face[i]);
		free(f->face);
		free(f->name);
		free(f);
		any = true;
	}
	if (any) {
		/* the advances of the faces gone (their addresses may come back) */
		memset(fb_advances, 0, sizeof(fb_advances));
		fb_generation++;
	}
	if (fb_scope == owner)
		fb_scope = NULL;
}

/* exported interface documented in framebuffer/font.h */
const void *fb_font_set_scope(const void *owner)
{
	const void *old = fb_scope;

	fb_scope = owner;
	return old;
}


/* ---- measuring and setting text --------------------------------------------------- */

/* exported interface documented in framebuffer/font.h */
void fb_font_glyphs(const plot_font_style_t *fstyle, const char *string,
		size_t length, fb_glyph_cb cb, void *pw)
{
	struct fb_run run;
	struct fb_glyph g;
	FT_Fixed pen = 0;
	size_t nxtchr = 0;

	fb_run_init(&run, fstyle);
	while (nxtchr < length) {
		uint32_t ucs4 = utf8_to_ucs4(string + nxtchr, length - nxtchr);
		FT_Fixed adv;
		FT_Glyph glyph;

		nxtchr = utf8_next(string, length, nxtchr);
		adv = fb_run_glyph(&run, ucs4, &g);
		glyph = fb_glyph_image(&g);
		if (glyph != NULL)
			cb(pw, glyph, FB_PX(pen));
		pen += adv;
	}
}

/* exported interface documented in framebuffer/freetype_font.h */
nserror
fb_font_width(const plot_font_style_t *fstyle,
                         const char *string, size_t length,
                         int *width)
{
	struct fb_run run;
	struct fb_glyph g;
	FT_Fixed pen = 0;
	size_t nxtchr = 0;

	fb_run_init(&run, fstyle);
	while (nxtchr < length) {
		uint32_t ucs4 = utf8_to_ucs4(string + nxtchr, length - nxtchr);

		nxtchr = utf8_next(string, length, nxtchr);
		pen += fb_run_glyph(&run, ucs4, &g);
	}
	*width = FB_PX(pen);
	return NSERROR_OK;
}


/* exported interface documented in framebuffer/freetype_font.h */
nserror
fb_font_position(const plot_font_style_t *fstyle,
		const char *string, size_t length,
		int x, size_t *char_offset, int *actual_x)
{
	struct fb_run run;
	struct fb_glyph g;
	FT_Fixed pen = 0;
	size_t nxtchr = 0;

	fb_run_init(&run, fstyle);
	while (nxtchr < length) {
		uint32_t ucs4 = utf8_to_ucs4(string + nxtchr, length - nxtchr);
		FT_Fixed next = pen + fb_run_glyph(&run, ucs4, &g);

		if (FB_PX(next) > x) {
			/* x is in this character: the nearer of its edges */
			if (x - FB_PX(pen) <= FB_PX(next) - x) {
				*char_offset = nxtchr;
				*actual_x = FB_PX(pen);
			} else {
				*char_offset = utf8_next(string, length, nxtchr);
				*actual_x = FB_PX(next);
			}
			return NSERROR_OK;
		}
		pen = next;
		nxtchr = utf8_next(string, length, nxtchr);
	}
	*char_offset = length;
	*actual_x = FB_PX(pen);
	return NSERROR_OK;
}


/**
 * Find where to split a string to make it fit a width.
 *
 * \param  fstyle       style for this text
 * \param  string       UTF-8 string to measure
 * \param  length       length of string, in bytes
 * \param  x            width available
 * \param  char_offset  updated to offset in string of actual_x, [1..length]
 * \param  actual_x     updated to x coordinate of character closest to x
 * \return  true on success, false on error and error reported
 *
 * On exit, char_offset indicates first character after split point.
 *
 * Note: char_offset of 0 should never be returned.
 *
 *   Returns:
 *     char_offset giving split point closest to x, where actual_x <= x
 *   else
 *     char_offset giving split point closest to x, where actual_x > x
 *
 * Returning char_offset == length means no split possible
 */
static nserror
fb_font_split(const plot_font_style_t *fstyle,
		const char *string, size_t length,
		int x, size_t *char_offset, int *actual_x)
{
	struct fb_run run;
	struct fb_glyph g;
	FT_Fixed pen = 0;
	size_t nxtchr = 0;
	int last_space_x = 0;
	size_t last_space_idx = 0;

	fb_run_init(&run, fstyle);
	while (nxtchr < length) {
		uint32_t ucs4 = utf8_to_ucs4(string + nxtchr, length - nxtchr);

		if (ucs4 == 0x20) {
			last_space_x = FB_PX(pen);
			last_space_idx = nxtchr;
		}
		pen += fb_run_glyph(&run, ucs4, &g);
		if (FB_PX(pen) > x && last_space_idx != 0) {
			/* string has exceeded available width and we've
			 * found a space; return previous space */
			*actual_x = last_space_x;
			*char_offset = last_space_idx;
			return NSERROR_OK;
		}
		nxtchr = utf8_next(string, length, nxtchr);
	}

	*char_offset = nxtchr;
	*actual_x = FB_PX(pen);

	return NSERROR_OK;
}

/**
 * Onyx: a face's vertical metrics, as Chrome reads them on Windows (DirectWrite): OS/2's
 * typographic ones when the font asks for them (USE_TYPO_METRICS), else its Windows
 * ascent and descent and the line gap making up hhea's line spacing.
 */
static bool fb_face_vmetrics(fb_faceid_t *f)
{
	FT_Face face;
	TT_OS2 *os2;
	TT_HoriHeader *hhea;

	if (f->v_upm != 0)
		return true;
	if (FTC_Manager_LookupFace(ft_cmanager, (FTC_FaceID)f, &face) != 0)
		return false;
	os2 = FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
	hhea = FT_Get_Sfnt_Table(face, FT_SFNT_HHEA);
	if (os2 != NULL && os2->version != 0xFFFF && (os2->fsSelection & (1 << 7))) {
		f->v_asc = os2->sTypoAscender;
		f->v_desc = -os2->sTypoDescender;
		f->v_gap = os2->sTypoLineGap;
	} else if (os2 != NULL && os2->version != 0xFFFF &&
		   os2->usWinAscent + os2->usWinDescent > 0) {
		int spacing = hhea != NULL ? hhea->Ascender - hhea->Descender +
				hhea->Line_Gap : 0;

		f->v_asc = os2->usWinAscent;
		f->v_desc = os2->usWinDescent;
		f->v_gap = spacing - f->v_asc - f->v_desc;
	} else {
		f->v_asc = face->ascender;
		f->v_desc = -face->descender;
		f->v_gap = face->height - face->ascender + face->descender;
	}
	if (f->v_gap < 0)
		f->v_gap = 0;
	f->v_upm = face->units_per_EM > 0 ? face->units_per_EM : 1000;
	return true;
}

/**
 * Onyx: the vertical metrics of the style's first face, in whole pixels (rounded one by
 * one, as Blink does): its ascent, descent and line gap -- line-height: normal is their
 * sum, a line's baseline half the leading below its ascent.
 */
static nserror fb_font_metrics(const plot_font_style_t *fstyle, int *ascent,
		int *descent, int *line_gap)
{
	struct fb_chain c;
	fb_faceid_t *f;
	double px;

	fb_chain_make(fstyle, &c);
	if (c.n == 0)
		return NSERROR_INVALID;
	f = c.face[0]->origin != NULL ? c.face[0]->origin : c.face[0];
	if (!fb_face_vmetrics(f))
		return NSERROR_INVALID;
	px = (double)fstyle->size / PLOT_STYLE_SCALE * browser_get_dpi() / 72.0 /
			f->v_upm;
	*ascent = (int)(f->v_asc * px + 0.5);
	*descent = (int)(f->v_desc * px + 0.5);
	*line_gap = (int)(f->v_gap * px + 0.5);
	return NSERROR_OK;
}

static struct gui_layout_table layout_table = {
	.width = fb_font_width,
	.position = fb_font_position,
	.split = fb_font_split,
	.add_face = fb_font_add_face,		/* Onyx: web fonts */
	.release_faces = fb_font_release_faces,
	.set_scope = fb_font_set_scope,
	.metrics = fb_font_metrics,
};

struct gui_layout_table *framebuffer_layout_table = &layout_table;


struct gui_utf8_table *framebuffer_utf8_table = NULL;

/*
 * Local Variables:
 * c-basic-offset:8
 * End:
 */
