//
// verbose -- show or set the kernel's verbose-logging flag (app start/spawn/exit/
// kill events). `verbose` prints the state; `verbose on|off` toggles it at runtime
// AND persists it to SD:system.ini so it survives a reboot. View the logs with kmsg.
//   usage: verbose [on|off]
//
#include "kapi.h"
#include "applib.h"

int main (void)
{
	char args[32];
	kapi_get_args (args, sizeof (args));
	int i = 0; while (args[i] == ' ') i++;

	if (args[i] == '\0')					// no arg: report
	{
		ax_puts ("verbose: ");
		ax_putln (kapi_get_verbose () ? "on" : "off");
		ax_putln ("usage: verbose on|off");
		return 0;
	}

	int on;
	if (args[i] == 'o' && args[i + 1] == 'n') on = 1;
	else if (args[i] == 'o' && args[i + 1] == 'f') on = 0;
	else { ax_putln ("usage: verbose on|off"); return 1; }

	kapi_set_verbose (on);					// runtime
	kapi_save_file ("SD:etc/system.ini", on ? "verbose=1\n" : "verbose=0\n",
			on ? 10u : 10u);			// persist (both strings are 10 bytes)
	ax_puts ("verbose -> "); ax_putln (on ? "on" : "off");
	return 0;
}
