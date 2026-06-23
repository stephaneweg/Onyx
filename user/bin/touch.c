//
// touch -- create an empty file if it does not exist. Usage: touch <path ...>
// (no timestamps yet, so existing files are left untouched).
//
#include "kapi.h"
#include "applib.h"

int main (void)
{
	char args[256];
	int len = kapi_get_args (args, sizeof (args));
	int i = 0;
	while (i < len)
	{
		while (i < len && (args[i] == ' ' || args[i] == '\t')) i++;
		char path[128]; int p = 0;
		while (i < len && args[i] != ' ' && args[i] != '\t' && p < (int) sizeof path - 1)
			path[p++] = args[i++];
		path[p] = '\0';
		if (p == 0) continue;
		void *f = kapi_open (path);
		if (f != 0) { kapi_close (f); continue; }	// already exists
		kapi_save_file (path, "", 0);			// create empty
	}
	return 0;
}
