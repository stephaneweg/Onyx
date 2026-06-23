//
// kill -- terminate a process by PID (see `ps`). By default it asks the app to
// close cleanly (it gets to finish / clean up); --force (-f) terminates it hard.
//   usage: kill <pid> [--force|-f]
//
#include "kapi.h"
#include "applib.h"

int main (void)
{
	char args[128];
	kapi_get_args (args, sizeof (args));

	int i = 0;
	while (args[i] == ' ') i++;

	int pid = 0, any = 0;
	while (args[i] >= '0' && args[i] <= '9') { pid = pid * 10 + (args[i] - '0'); i++; any = 1; }

	int force = 0;
	while (args[i] == ' ') i++;
	if (args[i] == '-')				// optional flag after the pid
	{
		char opt[16]; int o = 0;
		while (args[i] != '\0' && args[i] != ' ' && o < 15) opt[o++] = args[i++];
		opt[o] = '\0';
		if (ax_streq (opt, "--force") || ax_streq (opt, "-f")) force = 1;
	}

	if (!any)
	{
		ax_putln ("usage: kill <pid> [--force|-f]");
		return 1;
	}

	int r = kapi_kill_pid (pid, force);
	if (r == 1)
	{
		char b[12]; ax_itoa (pid, b);
		ax_puts (force ? "killed (force) pid " : "signalled pid ");
		ax_putln (b);
		return 0;
	}
	if (r == 0) ax_putln ("kill: no such pid");
	else        ax_putln ("kill: protected (kernel task or self)");
	return 1;
}
