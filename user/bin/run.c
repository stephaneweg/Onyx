//
// run -- launch a GUI app from the shell. `run <name>` starts apps/<name>.app/
// main.elf (the same thing the app drawer does); a name containing '/' is taken as
// a full ELF path instead. Any extra arguments are passed to the app as argv.
//   usage: run <app|path> [args...]
// Examples:  run mandelbrot      run tinypad SD:/notes.txt      run SD:/bin/ls.elf
//
#include "kapi.h"
#include "applib.h"

int main (void)
{
	char args[160];
	kapi_get_args (args, sizeof (args));

	// First token = app name or ELF path; the remainder = the app's own arguments.
	int i = 0;
	while (args[i] == ' ') i++;
	char name[128]; int n = 0;
	while (args[i] != '\0' && args[i] != ' ' && n < 127) name[n++] = args[i++];
	name[n] = '\0';
	while (args[i] == ' ') i++;
	const char *rest = &args[i];

	if (name[0] == '\0')
	{
		ax_putln ("usage: run <app|path> [args...]");
		return 1;
	}

	int has_slash = 0;
	for (int j = 0; name[j] != '\0'; j++) if (name[j] == '/') has_slash = 1;

	int ok;
	if (has_slash)
	{
		ok = kapi_exec (name, rest);			// explicit ELF path
	}
	else if (rest[0] != '\0')
	{
		// App name + arguments: build apps/<name>.app/main.elf and pass argv.
		char path[160]; int p = 0;
		ax_strcat (path, sizeof (path), &p, "SD:apps/");
		ax_strcat (path, sizeof (path), &p, name);
		ax_strcat (path, sizeof (path), &p, ".app/main.elf");
		ok = kapi_exec (path, rest);
	}
	else
	{
		ok = kapi_launch (name);			// app by name, no args
	}

	if (!ok)
	{
		ax_puts ("run: cannot launch ");
		ax_putln (name);
		return 1;
	}
	return 0;
}
