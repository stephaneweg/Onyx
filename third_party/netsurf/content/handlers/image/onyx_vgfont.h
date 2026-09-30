/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 */

/**
 * \file
 * Onyx: the card's fonts as PlutoVG faces (onyx_vgfont.c): SVG <text>, canvas text.
 */

#ifndef NETSURF_IMAGE_ONYX_VGFONT_H
#define NETSURF_IMAGE_ONYX_VGFONT_H

#include <stdbool.h>
#include <plutovg.h>

/**
 * The face for a CSS family list (len bytes, -1: NUL-terminated), weight and style: the
 * first family the card has, else sans-serif (Liberation Sans), else DejaVu Sans; NULL if
 * no file could be read. Loaded once, kept (not the caller's).
 */
plutovg_font_face_t *onyx_vg_font(const char *families, int len, bool bold, bool italic);

#endif
