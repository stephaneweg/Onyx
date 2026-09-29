/*
 * Copyright 2014 Vincent Sanders <vince@netsurf-browser.org>
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

/**
 * \file
 * Internal font handling interfaces.
 *
 * These functions provide font related services. They all work on
 * UTF-8 strings with lengths given.
 */

#ifndef NETSURF_HTML_FONT_H
#define NETSURF_HTML_FONT_H

#include <stdbool.h>

struct plot_font_style;

/**
 * Populate a font style using data from a computed CSS style
 *
 * \param unit_len_ctx  Length conversion context
 * \param css      Computed style to consider
 * \param fstyle   Font style to populate
 */
void font_plot_style_from_css(const css_unit_ctx *unit_len_ctx,
			      const css_computed_style *css,
			      struct plot_font_style *fstyle);

/**
 * Onyx: the font's vertical metrics, whole px (the frontend's: false without).
 */
bool font_metrics(const struct plot_font_style *fstyle, int *ascent,
		int *descent, int *line_gap);

/**
 * Onyx: where a text's baseline is in a box as tall as its line-height (px from its
 * top): its font's ascent below half the leading (floored, as Blink), else NetSurf's
 * three quarters of the height.
 */
int font_baseline(const struct plot_font_style *fstyle, int line_height);

#endif
