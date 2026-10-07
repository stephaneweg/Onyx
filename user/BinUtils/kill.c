//
// kill -- terminate a process by PID (see `ps`). By default it asks the app to
// close cleanly (it gets to finish / clean up); --force (-f) terminates it hard; --tree (-t)
// terminates it and every process under it (what it started, what they started...) at once.
//   usage: kill <pid> [--force|-f|--tree|-t]
//
#include "appkit/appkit.h"

int main (void)
{
	char args[128];
	kapi_get_args (args, sizeof (args));

	int i = 0;
	while (args[i] == ' ') i++;

	int pid = 0, any = 0;
	while (args[i] >= '0' && args[i] <= '9') { pid = pid * 10 + (args[i] - '0'); i++; any = 1; }

	int force = 0, tree = 0;
	while (args[i] == ' ') i++;
	if (args[i] == '-')				// optional flag after the pid
	{
		char opt[16]; int o = 0;
		while (args[i] != '\0' && args[i] != ' ' && o < 15) opt[o++] = args[i++];
		opt[o] = '\0';
		if (ax_streq (opt, "--force") || ax_streq (opt, "-f")) force = 1;
		if (ax_streq (opt, "--tree") || ax_streq (opt, "-t")) tree = 1;
	}

	if (!any)
	{
		ax_putln ("usage: kill <pid> [--force|-f|--tree|-t]");
		return 1;
	}

	if (tree)					// the process and everything under it
	{
		int n = kapi_proc_tree (pid, KAPI_TREE_KILL, 0, 0);
		char b[12];
		if (n >= 0)
		{
			ax_puts ("killed pid "); ax_itoa (pid, b); ax_puts (b);
			ax_puts ("'s tree: "); ax_itoa (n, b); ax_puts (b); ax_putln (" process(es)");
			return 0;
		}
		if (n == -KAPI_ESRCH) ax_putln ("kill: no such pid");
		else if (n == -KAPI_EPERM) ax_putln ("kill: protected (the tree holds this process)");
		else ax_putln ("kill: --tree needs a newer kernel");
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
