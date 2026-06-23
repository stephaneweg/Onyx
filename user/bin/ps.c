//
// ps -- list running processes. The kernel returns one line per task as
// "<pid> <a|k> <state> <name>" (pid 0 = kernel task); we print it in columns.
// State: R ready, S sleeping, B blocked, N new. Kind: a app (killable), k kernel.
//
#include "kapi.h"
#include "applib.h"

static int parse_uint (const char *s, int *pi)
{
	int v = 0, i = *pi;
	while (s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); i++; }
	*pi = i;
	return v;
}

int main (void)
{
	static char buf[4096];
	kapi_list_procs (buf, sizeof (buf));

	ax_putln (" PID  K  S  NAME");

	int i = 0;
	while (buf[i] != '\0')
	{
		int pid = parse_uint (buf, &i);
		while (buf[i] == ' ') i++;
		char kind = buf[i] ? buf[i++] : '?';
		while (buf[i] == ' ') i++;
		char st = buf[i] ? buf[i++] : '?';
		while (buf[i] == ' ') i++;

		char name[64]; int n = 0;
		while (buf[i] != '\0' && buf[i] != '\n' && n < 63) name[n++] = buf[i++];
		name[n] = '\0';
		if (buf[i] == '\n') i++;

		// "%4d  %c  %c  %s"
		char line[96]; int p = 0;
		char ps[12]; int pl = ax_itoa (pid, ps);
		for (int s = 0; s < 4 - pl; s++) line[p++] = ' ';
		for (int s = 0; s < pl; s++) line[p++] = ps[s];
		line[p++] = ' '; line[p++] = ' '; line[p++] = kind;
		line[p++] = ' '; line[p++] = ' '; line[p++] = st;
		line[p++] = ' '; line[p++] = ' ';
		for (int s = 0; name[s]; s++) line[p++] = name[s];
		line[p] = '\0';
		ax_putln (line);
	}
	return 0;
}
