//
// verbose -- show or set the kernel's verbose-logging flag (app start/spawn/exit/
// kill events). `verbose` prints the state; `verbose on|off` toggles it at runtime
// AND persists it to SD:system.ini so it survives a reboot. View the logs with kmsg.
//   usage: verbose [on|off]
//
#include "appkit/appkit.h"
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
	// persist: the verbose= line replaced, the file's other lines (timezone, ntp, hostname, the
	// comments) kept -- added at the end when there is none
	static char in[4096], out[4200];
	const char *line = on ? "verbose=1\n" : "verbose=0\n";
	int n = 0, o = 0, done = 0;
	void *f = kapi_open ("SD:etc/system.ini");
	if (f) { n = kapi_read (f, in, sizeof in - 1); kapi_close (f); if (n < 0) n = 0; }
	for (int k = 0; k < n; )
	{
		int s = k; while (k < n && in[k] != '\n') k++;
		int e = k; if (k < n) k++;
		int t = s; while (t < e && (in[t] == ' ' || in[t] == '\t')) t++;
		const char *key = "verbose"; int m = 0; while (key[m] && t + m < e && in[t + m] == key[m]) m++;
		if (!done && key[m] == '\0') { for (const char *v = line; *v; ) out[o++] = *v++; done = 1; continue; }
		for (int q = s; q < k && o < (int) sizeof out - 12; q++) out[o++] = in[q];
		if (k == n && e == n && in[n - 1] != '\n') out[o++] = '\n';
	}
	if (!done) for (const char *v = line; *v; ) out[o++] = *v++;
	kapi_save_file ("SD:etc/system.ini", out, (unsigned) o);
	ax_puts ("verbose -> "); ax_putln (on ? "on" : "off");
	return 0;
}
