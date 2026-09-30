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
 * Onyx: web fonts -- a document's @font-face rules fetched (onyx_webfont.c).
 */

#ifndef NETSURF_HTML_ONYX_WEBFONT_H
#define NETSURF_HTML_ONYX_WEBFONT_H

struct html_content;

/**
 * The document's @font-face rules (its author style sheets', their imports' and @media
 * blocks'): each face's first source the frontend reads is fetched, handed to it
 * (guit->layout->add_face) and the document laid out again. Called once its style
 * sheets are in.
 */
void onyx_webfont_scan(struct html_content *c);

/** The document's web fonts: fetches stopped, faces forgotten (it is destroyed). */
void onyx_webfont_release(struct html_content *c);

/** The document is the one whose web fonts text is measured and drawn with next. */
void onyx_webfont_scope(struct html_content *c);

/**
 * A face a script loaded (the CSS Font Loading API: new FontFace + document.fonts.add): given
 * to the font code for the document, which is laid out again. `data` is the font file's bytes.
 */
bool onyx_webfont_add_script_face(struct html_content *c, const char *family, int wmin,
		int wmax, bool italic, const uint8_t *data, size_t size);

#endif
