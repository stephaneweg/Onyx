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
 * Onyx: the card's fonts as PlutoVG faces, for the text of SVG images (<text>) and of
 * <canvas> (fillText): a CSS family list to one of the files of SD:/res/fonts, as
 * frontends/framebuffer/font_freetype.c maps Chrome's Windows fonts -- Liberation (Arial,
 * Times New Roman), Gelasio (Georgia), Selawik (Segoe UI), DejaVu (Verdana, the monospace
 * ones, the rest). Each file is read once, by PlutoVG's stb_truetype, and kept.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <plutovg.h>

#include "utils/log.h"
#include "image/onyx_vgfont.h"

#ifndef NETSURF_FB_FONTPATH
#define NETSURF_FB_FONTPATH "/res/fonts"
#endif

#define VGFONT_FACES 16

static struct {
	char file[40];
	plutovg_font_face_t *face;
	bool failed;
} vgfont_faces[VGFONT_FACES];

static plutovg_font_face_t *vgfont_file(const char *file)
{
	char path[256];
	int i;

	for (i = 0; i < VGFONT_FACES; i++) {
		if (vgfont_faces[i].file[0] == '\0')
			break;
		if (strcmp(vgfont_faces[i].file, file) == 0)
			return vgfont_faces[i].failed ? NULL : vgfont_faces[i].face;
	}
	if (i == VGFONT_FACES)
		return NULL;
	snprintf(vgfont_faces[i].file, sizeof(vgfont_faces[i].file), "%s", file);
	snprintf(path, sizeof path, "%s/%s", NETSURF_FB_FONTPATH, file);
	vgfont_faces[i].face = plutovg_font_face_load_from_file(path, 0);
	vgfont_faces[i].failed = vgfont_faces[i].face == NULL;
	if (vgfont_faces[i].failed)
		NSLOG(netsurf, INFO, "no font %s", path);
	return vgfont_faces[i].face;
}

/** one family (lower case) to a font file: regular, bold, italic, bold italic */
static const char *vgfont_family_file(const char *f, bool bold, bool italic)
{
	static const struct { const char *family; const char *file[4]; } map[] = {
		{ "serif", { "LiberationSerif-Regular.ttf", "LiberationSerif-Bold.ttf",
			"LiberationSerif-Italic.ttf", "LiberationSerif-BoldItalic.ttf" } },
		{ "times new roman", { "LiberationSerif-Regular.ttf", "LiberationSerif-Bold.ttf",
			"LiberationSerif-Italic.ttf", "LiberationSerif-BoldItalic.ttf" } },
		{ "times", { "LiberationSerif-Regular.ttf", "LiberationSerif-Bold.ttf",
			"LiberationSerif-Italic.ttf", "LiberationSerif-BoldItalic.ttf" } },
		{ "georgia", { "Gelasio-Regular.ttf", "Gelasio-Bold.ttf",
			"Gelasio-Italic.ttf", "Gelasio-BoldItalic.ttf" } },
		{ "monospace", { "DejaVuSansMono.ttf", "DejaVuSansMono-Bold.ttf",
			"DejaVuSansMono.ttf", "DejaVuSansMono-Bold.ttf" } },
		{ "courier new", { "DejaVuSansMono.ttf", "DejaVuSansMono-Bold.ttf",
			"DejaVuSansMono.ttf", "DejaVuSansMono-Bold.ttf" } },
		{ "courier", { "DejaVuSansMono.ttf", "DejaVuSansMono-Bold.ttf",
			"DejaVuSansMono.ttf", "DejaVuSansMono-Bold.ttf" } },
		{ "consolas", { "DejaVuSansMono.ttf", "DejaVuSansMono-Bold.ttf",
			"DejaVuSansMono.ttf", "DejaVuSansMono-Bold.ttf" } },
		{ "verdana", { "DejaVuSans.ttf", "DejaVuSans-Bold.ttf",
			"DejaVuSans-Oblique.ttf", "DejaVuSans-BoldOblique.ttf" } },
		{ "dejavu sans", { "DejaVuSans.ttf", "DejaVuSans-Bold.ttf",
			"DejaVuSans-Oblique.ttf", "DejaVuSans-BoldOblique.ttf" } },
		{ "segoe ui", { "selawk.ttf", "selawkb.ttf", "selawk.ttf", "selawkb.ttf" } },
		{ "system-ui", { "selawk.ttf", "selawkb.ttf", "selawk.ttf", "selawkb.ttf" } },
		{ "sans-serif", { "LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf",
			"LiberationSans-Italic.ttf", "LiberationSans-BoldItalic.ttf" } },
		{ "arial", { "LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf",
			"LiberationSans-Italic.ttf", "LiberationSans-BoldItalic.ttf" } },
		{ "helvetica", { "LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf",
			"LiberationSans-Italic.ttf", "LiberationSans-BoldItalic.ttf" } },
	};
	int k = (bold ? 1 : 0) + (italic ? 2 : 0);
	size_t i;

	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		if (strcmp(map[i].family, f) == 0)
			return map[i].file[k];
	}
	return NULL;
}

/* exported interface documented in image/onyx_vgfont.h */
plutovg_font_face_t *onyx_vg_font(const char *families, int len, bool bold, bool italic)
{
	static const char *const sans[4] = { "LiberationSans-Regular.ttf",
		"LiberationSans-Bold.ttf", "LiberationSans-Italic.ttf",
		"LiberationSans-BoldItalic.ttf" };
	plutovg_font_face_t *face = NULL;
	const char *p = families, *end = families + (len < 0 ? (int) strlen(families) : len);
	char one[64];

	/* the first family of the list the card has */
	while (p != NULL && p < end && face == NULL) {
		size_t n = 0;
		const char *file;

		while (p < end && (*p == ' ' || *p == ',' || *p == '"' || *p == '\''))
			p++;
		while (p < end && *p != ',' && n + 1 < sizeof one) {
			char ch = *p++;

			if (ch == '"' || ch == '\'')
				continue;
			one[n++] = (ch >= 'A' && ch <= 'Z') ? ch + 32 : ch;
		}
		while (p < end && *p != ',')
			p++;
		while (n > 0 && one[n - 1] == ' ')
			n--;
		one[n] = '\0';
		if (n > 0 && (file = vgfont_family_file(one, bold, italic)) != NULL)
			face = vgfont_file(file);
	}
	/* (an unknown family: sans-serif, as the canvas' default) */
	if (face == NULL)
		face = vgfont_file(sans[(bold ? 1 : 0) + (italic ? 2 : 0)]);
	if (face == NULL)
		face = vgfont_file("DejaVuSans.ttf");
	return face;
}
