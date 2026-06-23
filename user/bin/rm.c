//
// rm -- remove files (or empty directories). Usage: rm <path ...>
//
#include "kapi.h"
#include "applib.h"

int main (void)
{
	char args[256];
	int len = kapi_get_args (args, sizeof (args));
	int i = 0, rc = 0;
	while (i < len)
	{
		while (i < len && (args[i] == ' ' || args[i] == '\t')) i++;
		char path[128]; int p = 0;
		while (i < len && args[i] != ' ' && args[i] != '\t' && p < (int) sizeof path - 1)
			path[p++] = args[i++];
		path[p] = '\0';
		if (p == 0) continue;
		if (kapi_remove (path) != 0) { ax_puts ("rm: cannot remove "); ax_putln (path); rc = 1; }
	}
	return rc;
}
