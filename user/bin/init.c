//
// init -- the first program the kernel starts at boot (no arguments). It reads
// /etc/autostart and runs each line as a shell command, exactly like the terminal:
// the first word is a /bin tool (/bin/<word>.elf) and the rest is its argv. Desktop
// apps are launched with the `run` tool (e.g. "run panel"). Blank lines and lines
// starting with '#' are ignored. Fire-and-forget (kapi_exec): init launches
// everything and exits; the started programs keep running.
//
#include "kapi.h"
#include "applib.h"

static char g_buf[2048];

static void run_line (char *line)
{
	while (*line == ' ' || *line == '\t') line++;		// trim leading
	if (*line == '\0' || *line == '#') return;

	char *args = line;					// split: first token + rest
	while (*args != '\0' && *args != ' ' && *args != '\t') args++;
	if (*args != '\0') { *args++ = '\0'; while (*args == ' ' || *args == '\t') args++; }

	char path[128]; int p = 0;				// /bin/<token>.elf
	ax_strcat (path, sizeof path, &p, "SD:bin/");
	ax_strcat (path, sizeof path, &p, line);
	ax_strcat (path, sizeof path, &p, ".elf");

	if (!kapi_exec (path, args))
	{
		ax_puts ("init: cannot run "); ax_putln (path);
	}
}

int main (void)
{
	void *f = kapi_open ("SD:etc/autostart");
	if (f == 0) { ax_putln ("init: no /etc/autostart"); return 1; }
	int n = kapi_read (f, g_buf, sizeof (g_buf) - 1);
	kapi_close (f);
	if (n <= 0) return 0;
	g_buf[n] = '\0';

	int start = 0;
	for (int i = 0; i <= n; i++)
	{
		if (i == n || g_buf[i] == '\n' || g_buf[i] == '\r')
		{
			g_buf[i] = '\0';
			if (i > start) run_line (&g_buf[start]);
			start = i + 1;
		}
	}
	return 0;
}
