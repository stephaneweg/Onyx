/*
 * Copyright 2007 Daniel Silverstone <dsilvers@digital-scurf.org>
 * Copyright 2007 Rob Kendrick <rjek@netsurf-browser.org>
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

#ifndef _NETSURF_UTILS_USERAGENT_H_
#define _NETSURF_UTILS_USERAGENT_H_

#include <stdbool.h>

/** Retrieve the core user agent for this release.
 *
 * The string returned can be relied upon to exist for the duration of
 * the execution of the program. There is no need to copy it.
 */
const char * user_agent_string(void);

/** Free any memory allocated for the user_agent_string
 *
 * After calling this, the value returned by \ref user_agent_string()
 * is to be considered invalid.
 */
void free_user_agent_string(void);

/**
 * Onyx: the User-Agent for a host -- a desktop Chrome's for the sites the user asked the
 * desktop version of (the toolbar's "Desktop site"), the default one (a mobile Chrome's)
 * for the others.
 */
const char *user_agent_for_host(const char *host);

/** Onyx: the full version of the Chrome the User-Agents name ("142.0.7444.176": the
 * high-entropy client hints, navigator.userAgentData) */
const char *user_agent_chrome_full(void);

/** Onyx: whether a host's site gets the desktop version */
bool user_agent_is_desktop(const char *host);

/** Onyx: the desktop version for a host's site, or not (the list kept on the card) */
void user_agent_set_desktop(const char *host, bool desktop);

#endif
