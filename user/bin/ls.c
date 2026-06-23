//
// ls -- list a directory. Usage: ls [path]   (default: the current directory).
// One entry per line; directories get a trailing '/'. Writes to stdout.
//
#include "kapi.h"
#include "applib.h"

int main (void)
{
	char args[128];
	kapi_get_args (args, sizeof (args));

	// First whitespace-delimited token = the path (default root).
	char path[128];
	int i = 0, p = 0;
	while (args[i] == ' ') i++;
	while (args[i] && args[i] != ' ' && p < (int) sizeof (path) - 1) path[p++] = args[i++];
	path[p] = '\0';
	if (p == 0) { path[0] = '.'; path[1] = '\0'; }		// "." resolves to the cwd

	void *d = kapi_opendir (path);
	if (d == 0)
	{
		ax_puts ("ls: cannot open ");
		ax_putln (path);
		return 1;
	}

	struct kapi_dirent ent;
	while (kapi_readdir (d, &ent))
	{
		ax_puts (ent.name);
		if (ent.is_dir) ax_puts ("/");
		kapi_stdout_write ("\n", 1);
	}
	kapi_closedir (d);
	return 0;
}
