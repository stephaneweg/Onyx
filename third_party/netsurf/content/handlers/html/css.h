/*
 * Copyright 2020 Vincent Sanders <vince@netsurf-browser.org>
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
 * HTML content handler CSS interface.
 */

#ifndef NETSURF_HTML_CSS_H
#define NETSURF_HTML_CSS_H

/**
 * Initialise html content css handling.
 *
 * \return NSERROR_OK on success else error code
 */
nserror html_css_init(void);

/**
 * Finalise html content css handling.
 */
void html_css_fini(void);

/**
 * create a new css selection context for an html content.
 *
 * \param c The html content to create css selction on.
 * \param select_ctx A pointer to receive the new context.
 * \return NSERROR_OK on success and \a select_ctx updated else error code
 */
nserror html_css_new_selection_context(struct html_content *c, css_select_ctx **select_ctx);

/**
 * Initialise core stylesheets for a content
 *
 * \param c content structure to update
 * \return NSERROR_OK on success or error code
 */
nserror html_css_new_stylesheets(struct html_content *c);

/**
 * Initialise quirk stylesheets for a content
 *
 * \param c content structure to update
 * \return NSERROR_OK on success or error code
 */
nserror html_css_quirks_stylesheets(struct html_content *c);

/**
 * Free all css stylesheets associated with an HTML content. 
 *
 * \param html The HTML content to free stylesheets from.
 * \return NSERROR_OK on success or error code.
 */
nserror html_css_free_stylesheets(struct html_content *html);

/**
 * determine if any of the stylesheets were loaded insecurely
 *
 * \param htmlc The HTML content to check.
 * \return true if there were insecurely loadd stylesheets else false.
 */
bool html_css_saw_insecure_stylesheets(struct html_content *htmlc);

/**
 * process a css stylesheet dom LINK node 
 *
 * \param htmlc The HTML content.
 * \param node the DOM link node to process. 
 * \return true on success else false.
 */
bool html_css_process_link(struct html_content *htmlc, dom_node *node);

/**
 * process a css style dom node 
 *
 * \param htmlc The HTML content.
 * \param node the DOM node to process. 
 * \return true on success else false.
 */
bool html_css_process_style(struct html_content *htmlc, dom_node *node);

/**
 * process a css style dom node update 
 *
 * \param htmlc The HTML content.
 * \param node the DOM node to process. 
 * \return true on success else false.
 */
bool html_css_update_style(struct html_content *htmlc, dom_node *node);

/**
 * Onyx: a <style> element now in the document (perhaps inside an inserted subtree):
 * its inline sheet created and parsed if it has none yet.
 */
void html_css_style_inserted(struct html_content *htmlc, dom_node *node);

/**
 * Onyx: the <style> elements a script changed converted now, while it waits for a
 * style (getComputedStyle, a rectangle): their rules apply to what it reads next, as
 * in browsers -- they were converted at the next turns.
 */
void html_css_flush_sync(struct html_content *htmlc);

/**
 * Onyx: a node taken out of the document -- a <style> or <link>'s sheet no longer applies.
 */
void html_css_node_removed(struct html_content *htmlc, dom_node *node);



#endif
