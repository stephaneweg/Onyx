/*
 * Copyright 2008 Vincent Sanders <vince@simtec.co.uk>
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

#ifndef NETSURF_FB_FONT_FREETYPE_H
#define NETSURF_FB_FONT_FREETYPE_H

#include <ft2build.h>  
#include FT_FREETYPE_H 
#include FT_GLYPH_H

extern int ft_load_type;

/**
 * Onyx: a glyph of a string being drawn -- its image (rendered), at x: its pen position,
 * in px from the string's start.
 */
typedef void (*fb_glyph_cb)(void *pw, FT_Glyph glyph, int x);

/**
 * Onyx: a string's glyphs, as fb_font_width lays them out (fractional advances, the
 * letter- and word-spacing, each character in the first face of its style having it).
 */
void fb_font_glyphs(const plot_font_style_t *fstyle, const char *string,
		size_t length, fb_glyph_cb cb, void *pw);

/**
 * Onyx: a web font (@font-face) of a document: used for its family, weights and style
 * while that document is the scope (fb_font_set_scope). The file (TrueType / OpenType,
 * WOFF / WOFF2 when FreeType reads them) is copied.
 *
 * eturn NSERROR_OK, NSERROR_INVALID for a file FreeType does not read
 */
nserror fb_font_add_face(const void *owner, const char *family, int weight_min,
		int weight_max, bool italic, const uint8_t *data, size_t size);

/** Onyx: a document's web fonts forgotten (the document is gone). */
void fb_font_release_faces(const void *owner);

/**
 * Onyx: the document whose web fonts the text measured and drawn next may use (NULL:
 * none).
 *
 * eturn the previous one
 */
const void *fb_font_set_scope(const void *owner);

#endif /* NETSURF_FB_FONT_FREETYPE_H */
