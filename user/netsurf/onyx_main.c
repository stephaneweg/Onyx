/*
 * onyx_main.c -- entry shim for NetSurf on Onyx.
 *
 * Onyx newlib apps are entered as main(void) (crt0libc) and read their command line via
 * kapi_get_args(), whereas NetSurf's framebuffer frontend has a Unix-style
 * main(argc, argv) that expects a URL argument. We compile that frontend main as
 * `netsurf_main` (-Dmain=netsurf_main on gui.c, in netsurf-app.mk) and provide the real
 * main() here: it builds argv and calls netsurf_main.
 *
 * We force "-f onyx": the frontend otherwise auto-picks a default libnsfb surface, and its
 * picker (framebuffer_pick_default_fename: `type < NSFB_SURFACE_COUNT`) SKIPS our "onyx"
 * surface because its registered type value is intentionally large -- so it falls back to
 * the off-screen "ram" surface and renders with NO window (looks like a hang). "-f onyx"
 * selects our window surface explicitly. With no URL, NetSurf opens NETSURF_HOMEPAGE.
 */
#include <string.h>
#include "kapi.h"

extern int netsurf_main(int argc, char **argv);

int main(void)
{
	static char args[1024];
	char *argv[6];
	int argc = 0;

	argv[argc++] = (char *) "netsurf";
	argv[argc++] = (char *) "-f";
	argv[argc++] = (char *) "onyx";		/* our window surface, not the RAM fallback */

	args[0] = '\0';
	kapi_get_args (args, sizeof args);
	{
		char *p = args;
		while (*p == ' ' || *p == '\t') p++;
		if (*p != '\0')
			argv[argc++] = p;	/* the URL (a single token) */
	}
	argv[argc] = 0;

	return netsurf_main (argc, argv);
}
