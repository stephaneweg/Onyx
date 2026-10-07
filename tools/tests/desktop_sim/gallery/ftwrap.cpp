//
// ftwrap -- (the desktop simulator only) runs an unchanged uikit app with FreeType's face installed
// first: the app's main.cpp is compiled with -Dmain=app_main and linked with this. SIM_FT="Family,px"
// (default DejaVu Sans, 13). Used by studio.sh (the Widget Showcase under the face).
//
#include "fontkit/uikitface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int app_main (void);

int main (void)
{
	char fam[64] = "DejaVu Sans"; int px = 13;
	const char *e = getenv ("SIM_FT");
	if (e) { const char *c = strchr (e, ','); int n = c ? (int) (c - e) : (int) strlen (e); if (n > 63) n = 63; memcpy (fam, e, n); fam[n] = 0; if (c) px = atoi (c + 1); }
	if (!ft_uikit_install (fam, px)) fprintf (stderr, "ftwrap: no TrueType font\n");
	return app_main ();
}
