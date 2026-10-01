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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/config.h"
#include "utils/utsname.h"
#include "desktop/version.h"
#include "utils/log.h"
#include "utils/useragent.h"
#include "utils/nsoption.h"

static const char *core_user_agent_string = NULL;

#ifndef NETSURF_UA_FORMAT_STRING
#define NETSURF_UA_FORMAT_STRING "Mozilla/5.0 (%s) NetSurf/%d.%d"
#endif

/**
 * Prepare core_user_agent_string with a string suitable for use as a
 * user agent in HTTP requests.
 */
static void
user_agent_build_string(void)
{
        struct utsname un;
        const char *sysname = "Unknown";
        char *ua_string;
        int len;

        if (uname(&un) >= 0) {
                sysname = un.sysname;
                if (strcmp(sysname, "Linux") == 0) {
			/* Force desktop, not mobile */
                        sysname = "X11; Linux";
                }
        }

        len = snprintf(NULL, 0, NETSURF_UA_FORMAT_STRING,
                       sysname,
                       netsurf_version_major,
                       netsurf_version_minor);
        ua_string = malloc(len + 1);
        if (!ua_string) {
                /** \todo this needs handling better */
                return;
        }
        snprintf(ua_string, len + 1,
                 NETSURF_UA_FORMAT_STRING,
                 sysname,
                 netsurf_version_major,
                 netsurf_version_minor);

        core_user_agent_string = ua_string;

        NSLOG(netsurf, INFO, "Built user agent \"%s\"",
              core_user_agent_string);
}

/* Onyx: the Chrome the User-Agents name -- one version for the headers, the client hints
 * (user/netsurf/onyx_fetch.c) and navigator.userAgentData (quickjs/dom.js). Chrome's
 * reduced User-Agent (since Chrome 110): "Chrome/<major>.0.0.0", "Android 10; K", "Windows
 * NT 10.0"; the full version only in the high-entropy client hints. An old version stands
 * out (Google's "unusual traffic"): bump it now and then. */
#define ONYX_CHROME_MAJOR "142"
#define ONYX_CHROME_FULL "142.0.7444.176"

/* Onyx: jet.ini (below) */
enum { UA_INI_DEFAULT, UA_INI_DESKTOP, UA_INI_SITE };
static const char *onyx_ini_ua(int what, const char *host);

/* This is a function so that later we can override it trivially */
const char *
user_agent_string(void)
{
	/* Onyx: a current Chrome's on Android -- sites serve their WOFF2 fonts and do not
	 * turn "an unknown browser" away, and the big ones (Google, Facebook, Yahoo) send their
	 * light mobile pages: their desktop script applications are too heavy for QuickJS on
	 * the Pi -- unless Choices' user_agent says otherwise (a desktop Chrome's: see
	 * SD:/res/Choices); the HTTP requests (user/netsurf/onyx_fetch.c) and
	 * navigator.userAgent alike */
	const char *choice = nsoption_charp(user_agent);
	static char netsurf_ua[64];

	const char *ini = onyx_ini_ua(UA_INI_DEFAULT, NULL);

	if (ini != NULL)
		return ini;		/* (Onyx: jet.ini's [user_agent] default) */
	if (choice != NULL && choice[0] != '\0')
		return choice;
	/* Onyx (Jet Browser, the user's choice 2026-10-01): NetSurf's own, honest User-Agent by
	 * default -- claiming Chrome got Google and DuckDuckGo's bot checks (a Chrome whose TLS
	 * and scripts are not Chrome's), and their full script applications are too heavy for
	 * the Pi anyway; as NetSurf they serve their light HTML pages. "Desktop site" still
	 * sends a desktop Chrome's to the sites the user picks (below). */
	if (netsurf_ua[0] == '\0')
		snprintf(netsurf_ua, sizeof netsurf_ua, "Mozilla/5.0 (X11; Linux aarch64) NetSurf/%d.%d",
				netsurf_version_major, netsurf_version_minor);
	return netsurf_ua;
	(void) user_agent_build_string;
}

/* Public API documented in useragent.h */
const char *user_agent_chrome_full(void)
{
	return ONYX_CHROME_FULL;
}

/* Public API documented in useragent.h */
void
free_user_agent_string(void)
{
	if (core_user_agent_string != NULL) {
		/* Nasty cast because we need to de-const it to free it */
		free((void *)core_user_agent_string);
		core_user_agent_string = NULL;
	}
}


/* ---- Onyx: the desktop version for chosen sites ("Desktop site") -------------------- */

#include <stdio.h>
#include <strings.h>

#ifndef ONYX_NS_DATAPATH
#define ONYX_NS_DATAPATH ""
#endif
#define UA_SITES_FILE ONYX_NS_DATAPATH "desktop-sites"	/* one site per line */
#define UA_SITES_MAX 256

static const char ua_desktop[] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
	"AppleWebKit/537.36 (KHTML, like Gecko) Chrome/" ONYX_CHROME_MAJOR ".0.0.0 Safari/537.36";
static char *ua_sites[UA_SITES_MAX];
static int ua_nsites = -1;		/* (-1: the file not read yet) */

/* a host's site: its registrable domain, approximated -- the last two labels, or three
 * under a short second level of a country code (bbc.co.uk, x.com.au) */
static const char *ua_site_of(const char *host)
{
	const char *p = host + strlen(host), *dots[3] = { NULL, NULL, NULL };
	int n = 0;

	while (p > host && n < 3) {
		p--;
		if (*p == '.')
			dots[n++] = p;
	}
	if (n < 1)
		return host;
	if (n >= 2) {
		size_t tld = strlen(dots[0] + 1), sld = (size_t) (dots[0] - dots[1] - 1);
		if (tld == 2 && sld <= 3)	/* co.uk, com.au, ac.be... */
			return n >= 3 ? dots[2] + 1 : host;
		return dots[1] + 1;
	}
	return host;
}

static void ua_sites_load(void)
{
	FILE *f;
	char line[256];

	ua_nsites = 0;
	f = fopen(UA_SITES_FILE, "r");
	if (f == NULL)
		return;
	while (ua_nsites < UA_SITES_MAX && fgets(line, sizeof line, f) != NULL) {
		size_t l = strcspn(line, " \t\r\n");
		line[l] = '\0';
		if (l > 0)
			ua_sites[ua_nsites++] = strdup(line);
	}
	fclose(f);
}

static void ua_sites_save(void)
{
	FILE *f = fopen(UA_SITES_FILE, "w");
	int i;

	if (f == NULL)
		return;
	for (i = 0; i < ua_nsites; i++)
		if (ua_sites[i] != NULL)
			fprintf(f, "%s\n", ua_sites[i]);
	fclose(f);
}

/* Public API documented in useragent.h */
bool user_agent_is_desktop(const char *host)
{
	const char *site;
	int i;

	if (host == NULL || host[0] == '\0')
		return false;
	if (ua_nsites < 0)
		ua_sites_load();
	site = ua_site_of(host);
	for (i = 0; i < ua_nsites; i++)
		if (ua_sites[i] != NULL && strcasecmp(ua_sites[i], site) == 0)
			return true;
	return false;
}

/* Public API documented in useragent.h */
void user_agent_set_desktop(const char *host, bool desktop)
{
	const char *site;
	int i;

	if (host == NULL || host[0] == '\0' || user_agent_is_desktop(host) == desktop)
		return;
	site = ua_site_of(host);
	if (desktop) {
		if (ua_nsites >= UA_SITES_MAX)
			return;
		ua_sites[ua_nsites++] = strdup(site);
	} else {
		for (i = 0; i < ua_nsites; i++)
			if (ua_sites[i] != NULL && strcasecmp(ua_sites[i], site) == 0) {
				free(ua_sites[i]);
				ua_sites[i] = ua_sites[--ua_nsites];
				i--;
			}
	}
	ua_sites_save();
}

/* Public API documented in useragent.h */
const char *user_agent_for_host(const char *host)
{
	const char *site = onyx_ini_ua(UA_INI_SITE, host), *desk;

	if (site != NULL)
		return site;		/* (jet.ini's [sites]: this site's own) */
	if (user_agent_is_desktop(host)) {
		desk = onyx_ini_ua(UA_INI_DESKTOP, NULL);
		return desk != NULL ? desk : ua_desktop;
	}
	return user_agent_string();
}

/* ---- Onyx: SD:/apps/jet.app/jet.ini -- the User-Agents, edited by the user -------------
 *
 *   [user_agent]
 *   default = Mozilla/5.0 (X11; Linux aarch64) NetSurf/3.12     ; every site
 *   desktop = Mozilla/5.0 (Windows NT 10.0; ...) Chrome/...     ; "Desktop site"'s
 *   [sites]
 *   example.com = Mozilla/5.0 ...                                ; one site's (and its
 *                                                                ;  subdomains')
 *
 * Read once, at the first request (an edit takes effect at Jet Browser's next start). An
 * empty or missing value keeps the built-in one; "; " and "#" start a comment line. */
#define UA_INI_FILE ONYX_NS_DATAPATH "jet.ini"
#define UA_INI_SITES 64

static bool ua_ini_read;
static char *ua_ini_default, *ua_ini_desktop;
static struct { char *host, *ua; } ua_ini_site[UA_INI_SITES];
static int ua_ini_nsites;

static char *ua_trim(char *p)
{
	char *e;

	while (*p == ' ' || *p == '\t')
		p++;
	e = p + strlen(p);
	while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = '\0';
	return p;
}

static void ua_ini_load(void)
{
	FILE *f;
	char line[1024], section[32] = "";

	ua_ini_read = true;
	f = fopen(UA_INI_FILE, "r");
	if (f == NULL)
		return;
	while (fgets(line, sizeof line, f) != NULL) {
		char *p = ua_trim(line), *eq, *k, *v;

		if (*p == '\0' || *p == ';' || *p == '#')
			continue;
		if (*p == '[') {
			char *e = strchr(p, ']');
			if (e != NULL) {
				*e = '\0';
				snprintf(section, sizeof section, "%s", ua_trim(p + 1));
			}
			continue;
		}
		eq = strchr(p, '=');
		if (eq == NULL)
			continue;
		*eq = '\0';
		k = ua_trim(p);
		v = ua_trim(eq + 1);
		if (*v == '\0')
			continue;
		if (strcasecmp(section, "user_agent") == 0) {
			if (strcasecmp(k, "default") == 0 && ua_ini_default == NULL)
				ua_ini_default = strdup(v);
			else if (strcasecmp(k, "desktop") == 0 && ua_ini_desktop == NULL)
				ua_ini_desktop = strdup(v);
		} else if (strcasecmp(section, "sites") == 0 && ua_ini_nsites < UA_INI_SITES) {
			ua_ini_site[ua_ini_nsites].host = strdup(k);
			ua_ini_site[ua_ini_nsites].ua = strdup(v);
			if (ua_ini_site[ua_ini_nsites].host != NULL && ua_ini_site[ua_ini_nsites].ua != NULL)
				ua_ini_nsites++;
		}
	}
	fclose(f);
	NSLOG(netsurf, INFO, "jet.ini: default %s, desktop %s, %d sites",
			ua_ini_default != NULL ? "set" : "built-in",
			ua_ini_desktop != NULL ? "set" : "built-in", ua_ini_nsites);
}

/* a host is the site's, or one of its subdomains (www.example.com for example.com) */
static bool ua_host_in(const char *host, const char *site)
{
	size_t h = strlen(host), s = strlen(site);

	if (h == s)
		return strcasecmp(host, site) == 0;
	return h > s && host[h - s - 1] == '.' && strcasecmp(host + h - s, site) == 0;
}

static const char *onyx_ini_ua(int what, const char *host)
{
	int i;

	if (!ua_ini_read)
		ua_ini_load();
	if (what == UA_INI_DEFAULT)
		return ua_ini_default;
	if (what == UA_INI_DESKTOP)
		return ua_ini_desktop;
	if (host == NULL || host[0] == '\0')
		return NULL;
	for (i = 0; i < ua_ini_nsites; i++)
		if (ua_host_in(host, ua_ini_site[i].host))
			return ua_ini_site[i].ua;
	return NULL;
}
