/*
 * onyx_main.c -- entry shim for NetSurf on Onyx.
 *
 * Onyx newlib apps are entered as main(void) (crt0libc) and read their command line via
 * kapi_get_args(), whereas NetSurf's framebuffer frontend has a Unix-style
 * main(argc, argv) that expects a URL argument. We compile that frontend main as
 * `netsurf_main` (-Dmain=netsurf_main on gui.c, in netsurf-app.mk) and provide the real
 * main() here: it turns the Onyx args string into argv = { "netsurf", <url?> } and calls
 * netsurf_main. With no argument NetSurf opens its homepage (NETSURF_HOMEPAGE).
 */
#include <string.h>
#include "kapi.h"

extern int netsurf_main(int argc, char **argv);

int main(void)
{
	static char args[1024];
	char *argv[3];
	int argc = 0;

	argv[argc++] = (char *) "netsurf";

	args[0] = '\0';
	kapi_get_args(args, sizeof args);
	/* trim leading spaces; a single token (the URL) is all NetSurf needs here */
	{
		char *p = args;
		while (*p == ' ' || *p == '\t') p++;
		if (*p != '\0')
			argv[argc++] = p;
	}
	argv[argc] = 0;

	return netsurf_main(argc, argv);
}
