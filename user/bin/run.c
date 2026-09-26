//
// run -- launch a GUI app from the shell. `run <name>` starts apps/<name>.app (its
// main, or main.bas / main.bax by its runner: launch.h, SD:/etc/runners.ini); a name
// containing '/' or ':' is a program file (an ELF, or a format with a runner, e.g. a
// .bas / .bax BASIC program). Any extra arguments are passed to the program.
//   usage: run <app|path> [args...]
// Examples:  run mandelbrot   run tinypad SD:/notes.txt   run SD:/basic/examples/arkanoid.bas
//
#include "kapi.h"
#include "applib.h"
#include "launch.h"

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
	for (int j = 0; name[j] != '\0'; j++) if (name[j] == '/' || name[j] == ':') has_slash = 1;

	int ok = has_slash ? lx_open (name, rest)		// a program file (ELF or with a runner)
			   : lx_launch (name, rest);		// an app by name

	if (!ok)
	{
		ax_puts ("run: cannot launch ");
		ax_putln (name);
		return 1;
	}
	return 0;
}
