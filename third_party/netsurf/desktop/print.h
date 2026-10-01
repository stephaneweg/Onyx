/*
 * Copyright 2008 Adam Blokus <adamblokus@gmail.com>
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

/** \file
 * The redraw's printing flags. Onyx: NetSurf's printing (print.c, save as PDF) is removed --
 * Jet does not print; the flags html/redraw.c reads stay (defined there), always off.
 */

#ifndef NETSURF_DESKTOP_PRINT_H
#define NETSURF_DESKTOP_PRINT_H

#include <stdbool.h>

/*is the content currently redrawn for printing?*/
extern bool html_redraw_printing;
/*if something is partially under this Y coordinate it won't be drawn...*/
extern int html_redraw_printing_border;
/*...and the highest of the tops of all cropped elements will be remembered*/
extern int html_redraw_printing_top_cropped;

#endif
