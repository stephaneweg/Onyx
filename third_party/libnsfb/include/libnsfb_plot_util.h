/*
 * Copyright 2009 Vincent Sanders <vince@simtec.co.uk>
 *
 * This file is part of libnsfb, http://www.netsurf-browser.org/
 * Licenced under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 *
 * This is the exported interface for the libnsfb graphics library. 
 */

#ifndef _LIBNSFB_PLOT_UTIL_H
#define _LIBNSFB_PLOT_UTIL_H 1


/* alpha blend two pixels together */
static inline nsfb_colour_t 
nsfb_plot_ablend(nsfb_colour_t pixel, nsfb_colour_t scrpixel)
{
    /* Onyx: exact -- (s * a + d * (255 - a)) / 255, its integer part, each
     * channel: alpha 255 is the source, 0 the screen (with 256 - a and >> 8,
     * an opaque pixel let 1/256 of the screen through and every blend was a
     * little dark). x / 255 = (x + 1 + (x >> 8)) >> 8 for x <= 65025, two
     * channels at once (no carry between them). Acid2's eyes, as its
     * reference rendering. */
    uint32_t opacity = pixel >> 24;
    uint32_t transp = 0xFF - opacity;
    uint32_t rb, g;

    rb = (pixel & 0xFF00FF) * opacity + (scrpixel & 0xFF00FF) * transp;
    rb = ((rb + 0x00010001 + ((rb >> 8) & 0x00FF00FF)) >> 8) & 0x00FF00FF;
    g  = (((pixel & 0x00FF00) >> 8) * opacity +
          ((scrpixel & 0x00FF00) >> 8) * transp);
    g  = ((g + 1 + (g >> 8)) >> 8) << 8;

    return rb | (g & 0xFF00);
}


bool nsfb_plot_clip(const nsfb_bbox_t * restrict clip, nsfb_bbox_t * restrict rect);

bool nsfb_plot_clip_ctx(nsfb_t *nsfb, nsfb_bbox_t * restrict rect);

bool nsfb_plot_clip_line(const nsfb_bbox_t * restrict clip, nsfb_bbox_t * restrict line);

bool nsfb_plot_clip_line_ctx(nsfb_t *nsfb, nsfb_bbox_t * restrict line);

/** Obtain a bounding box which is the superset of two source boxes.
 *
 */
bool nsfb_plot_add_rect(const nsfb_bbox_t *box1, const nsfb_bbox_t *box2, nsfb_bbox_t *result);

/** Find if two boxes intersect. */
bool nsfb_plot_bbox_intersect(const nsfb_bbox_t *box1, const nsfb_bbox_t *box2);

#endif /* _LIBNSFB_PLOT_UTIL_H */
