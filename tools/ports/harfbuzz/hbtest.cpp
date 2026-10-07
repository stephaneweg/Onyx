/*
 * hbtest -- the smoke test of Onyx's HarfBuzz port (tools/ports/harfbuzz; docs/03 §5.5): text shaped with
 * the card's fonts through FreeType (hb-ft), as WebKit shapes it --
 * Latin ligatures and kerning, Arabic joining forms and the lam-alef ligature (right to left), Devanagari
 * conjuncts and the reordered i-matra. Prints the glyph ids, clusters and advances of each run; one line
 * per check (PASS / FAIL / SKIP), a summary; the exit status is the number of failures.
 *
 *   hbtest [font directory]          default SD:/res/fonts
 *
 * The fonts: DejaVu Sans (Latin, Arabic), Liberation Sans (Latin kerning); a font with Devanagari is
 * looked for in the directory (none of the card's fonts has it: the check is then SKIPped).
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is hereby granted,
 * free of charge, to any person obtaining a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the following conditions: The above
 * copyright notice and this permission notice shall be included in all copies or substantial portions of
 * the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_fail, g_skip;
static FT_Library g_ft;

static void check (int ok, const char *what)
{
	if (ok)
		g_pass++;
	else
		g_fail++;
	printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
}

static void skip (const char *what)
{
	g_skip++;
	printf ("SKIP  %s\n", what);
}

typedef struct {
	unsigned n;
	hb_glyph_info_t *info;
	hb_glyph_position_t *pos;
	hb_buffer_t *buf;
	hb_direction_t dir;
	hb_script_t script;
} Run;

/* shape UTF-8 text with a font at 32 px: the buffer's script and direction guessed (HarfBuzz's own
   Unicode functions: hb-icu's are checked by skiatest, which links ICU anyway -- here, no 16 MB of ICU
   data), the font's default features (liga, kern, the script's shaping) */
static Run shape (hb_font_t *font, const char *text, const char *features)
{
	Run r;
	r.buf = hb_buffer_create ();
	hb_buffer_add_utf8 (r.buf, text, -1, 0, -1);
	hb_buffer_guess_segment_properties (r.buf);
	r.dir = hb_buffer_get_direction (r.buf);
	r.script = hb_buffer_get_script (r.buf);
	hb_feature_t feats[8];
	unsigned nf = 0;
	if (features) {
		char tmp[128];
		snprintf (tmp, sizeof tmp, "%s", features);
		for (char *f = strtok (tmp, ","); f && nf < 8; f = strtok (NULL, ","))
			if (hb_feature_from_string (f, -1, &feats[nf]))
				nf++;
	}
	hb_shape (font, r.buf, feats, nf);
	r.info = hb_buffer_get_glyph_infos (r.buf, &r.n);
	r.pos = hb_buffer_get_glyph_positions (r.buf, NULL);
	return r;
}

static void print_run (const char *label, hb_font_t *font, Run *r)
{
	char tag[5];
	hb_tag_to_string (hb_script_to_iso15924_tag (r->script), tag);
	tag[4] = 0;
	printf ("      %s: %s %s, %u glyphs:", label, tag, hb_direction_to_string (r->dir), r->n);
	for (unsigned i = 0; i < r->n; i++) {
		char name[32];
		if (!hb_font_get_glyph_name (font, r->info[i].codepoint, name, sizeof name))
			snprintf (name, sizeof name, "g%u", r->info[i].codepoint);
		printf (" %s=%u@%u+%.1f", name, r->info[i].codepoint, r->info[i].cluster, r->pos[i].x_advance / 64.0);
	}
	printf ("\n");
}

static hb_font_t *open_font (const char *path, FT_Face *facep)
{
	FT_Face face;
	if (FT_New_Face (g_ft, path, 0, &face))
		return NULL;
	FT_Set_Pixel_Sizes (face, 0, 32);
	*facep = face;
	return hb_ft_font_create_referenced (face);
}

static int total_advance (Run *r)
{
	int a = 0;
	for (unsigned i = 0; i < r->n; i++)
		a += r->pos[i].x_advance;
	return a;
}

int main (int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "SD:/res/fonts";
	char path[512];
	printf ("hbtest: HarfBuzz %s, FreeType, fonts in %s\n", hb_version_string (), dir);
	if (FT_Init_FreeType (&g_ft)) {
		printf ("FAIL  FT_Init_FreeType\n");
		return 1;
	}
	FT_Int ma, mi, pa;
	FT_Library_Version (g_ft, &ma, &mi, &pa);
	printf ("hbtest: FreeType %d.%d.%d\n", ma, mi, pa);
	check (strcmp (hb_version_string (), "14.5.1") == 0, "HarfBuzz 14.5.1");

	/* ---- Latin: ligatures (Liberation Sans or DejaVu Sans), kerning ---- */
	FT_Face lf;
	snprintf (path, sizeof path, "%s/LiberationSans-Regular.ttf", dir);
	hb_font_t *lat = open_font (path, &lf);
	FT_Face df;
	snprintf (path, sizeof path, "%s/DejaVuSans.ttf", dir);
	hb_font_t *dejavu = open_font (path, &df);
	check (lat != NULL && dejavu != NULL, "fonts: LiberationSans-Regular.ttf and DejaVuSans.ttf opened by FreeType");
	if (!lat || !dejavu)
		return 1;

	Run r = shape (dejavu, "office fluffy", NULL);
	print_run ("DejaVu Sans \"office fluffy\"", dejavu, &r);
	Run r2 = shape (dejavu, "office fluffy", "-liga");
	print_run ("  the same, -liga", dejavu, &r2);
	check (r.script == HB_SCRIPT_LATIN && r.dir == HB_DIRECTION_LTR, "Latin: script and direction guessed");
	check (r.n < r2.n && r2.n == 13, "Latin: ligatures (liga) -- fewer glyphs than characters, none without it");
	hb_buffer_destroy (r.buf);
	hb_buffer_destroy (r2.buf);

	r = shape (lat, "AVAWAY Tokyo", NULL);
	print_run ("Liberation Sans \"AVAWAY Tokyo\"", lat, &r);
	r2 = shape (lat, "AVAWAY Tokyo", "-kern");
	print_run ("  the same, -kern", lat, &r2);
	check (total_advance (&r) < total_advance (&r2), "Latin: kerning (GPOS / kern) narrows AV, AW, AY, To");
	printf ("      advance %.1f px kerned, %.1f px without\n", total_advance (&r) / 64.0, total_advance (&r2) / 64.0);
	hb_buffer_destroy (r.buf);
	hb_buffer_destroy (r2.buf);

	/* ---- Arabic (DejaVu Sans): right to left, joining forms, lam-alef ---- */
	r = shape (dejavu, "سلام", NULL);	/* seen, lam, alef, meem */
	print_run ("DejaVu Sans \"سلام\"", dejavu, &r);
	check (r.script == HB_SCRIPT_ARABIC && r.dir == HB_DIRECTION_RTL, "Arabic: script Arab, right to left");
	/* lam + alef -> one ligature glyph: 3 glyphs for 4 characters; in visual order the last glyph is seen's
	   initial form (cluster 0) */
	check (r.n == 3 && r.info[r.n - 1].cluster == 0, "Arabic: the lam-alef ligature, visual order (RTL)");
	Run iso = shape (dejavu, "س", NULL);
	check (iso.n == 1 && r.n > 0 && r.info[r.n - 1].codepoint != iso.info[0].codepoint,
	       "Arabic: seen takes its initial form (not the isolated glyph)");
	hb_buffer_destroy (iso.buf);
	hb_buffer_destroy (r.buf);

	/* ---- Devanagari: a font in the directory with KA (U+0915) and a deva / dev2 GSUB ---- */
	hb_font_t *deva = NULL;
	FT_Face devf = NULL;
	char devname[256] = "";
	DIR *d = opendir (dir);
	struct dirent *e;
	while (d && !deva && (e = readdir (d))) {
		size_t l = strlen (e->d_name);
		if (l < 5 || (strcasecmp (e->d_name + l - 4, ".ttf") && strcasecmp (e->d_name + l - 4, ".otf")))
			continue;
		snprintf (path, sizeof path, "%s/%s", dir, e->d_name);
		FT_Face f;
		hb_font_t *hf = open_font (path, &f);
		if (!hf)
			continue;
		if (FT_Get_Char_Index (f, 0x0915) && FT_Get_Char_Index (f, 0x094D) && FT_Get_Char_Index (f, 0x093F)) {
			deva = hf;
			devf = f;
			snprintf (devname, sizeof devname, "%s", e->d_name);
		} else
			hb_font_destroy (hf);
	}
	if (d)
		closedir (d);
	if (!deva)
		skip ("Devanagari: no font with Devanagari in the directory (the card's DejaVu / Liberation / Gelasio / Selawik have none)");
	else {
		/* KA VIRAMA SSA I-matra: the conjunct kssa, the i-matra reordered before it; then "हिन्दी" */
		r = shape (deva, "क्षि हिन्दी", NULL);
		char label[300];
		snprintf (label, sizeof label, "%s \"क्षि हिन्दी\"", devname);
		print_run (label, deva, &r);
		unsigned imatra = FT_Get_Char_Index (devf, 0x093F);
		check (r.script == HB_SCRIPT_DEVANAGARI && r.dir == HB_DIRECTION_LTR, "Devanagari: script Deva, left to right");
		/* 4 code points -> at most 2 glyphs for the first cluster, the first glyph the i-matra (or its
		   variant: a glyph other than KA's) */
		unsigned first = 0;
		while (first < r.n && r.info[first].cluster == 0)
			first++;
		check (first >= 1 && first <= 2 && r.info[0].codepoint != FT_Get_Char_Index (devf, 0x0915),
		       "Devanagari: kssa conjunct (KA+VIRAMA+SSA one glyph), the i-matra reordered in front");
		(void) imatra;
		hb_buffer_destroy (r.buf);
		hb_font_destroy (deva);
	}

	hb_font_destroy (lat);
	hb_font_destroy (dejavu);
	printf ("hbtest: %d passed, %d failed, %d skipped\n", g_pass, g_fail, g_skip);
	return g_fail;
}
