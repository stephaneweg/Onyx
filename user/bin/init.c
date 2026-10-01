//
// init -- the first program the kernel starts at boot (no arguments). It reads
// /etc/autostart and runs each line as a shell command, exactly like the terminal:
// the first word is a /bin tool (/bin/<word>) and the rest is its argv. Desktop
// apps are launched with the `run` tool (e.g. "run panel"); the builtin `sleep <s>`
// pauses init between lines (staggered startup). Blank lines and lines
// starting with '#' are ignored. Fire-and-forget (kapi_exec): init launches
// everything and exits; the started programs keep running -- but `wait <command>` runs the
// command and waits for its end before the next line (`wait pkg commit`: the packages staged
// for the next boot moved in before the desktop starts, docs/pkg/README.md).
//
#include "kapi.h"
#include "applib.h"

static char g_buf[32768];		// (the whole autostart: it was 2 KB, a longer file lost its last lines)

static void run_line (char *line)
{
	while (*line == ' ' || *line == '\t') line++;		// trim leading
	if (*line == '\0' || *line == '#') return;

	char *args = line;					// split: first token + rest
	while (*args != '\0' && *args != ' ' && *args != '\t') args++;
	if (*args != '\0') { *args++ = '\0'; while (*args == ' ' || *args == '\t') args++; }

	// Builtin: `sleep <seconds>` pauses init itself before the next line (a /bin tool
	// would run concurrently, since init launches every line fire-and-forget).
	if (ax_streq (line, "sleep"))
	{
		unsigned s = 0;
		for (int i = 0; args[i] >= '0' && args[i] <= '9'; i++) s = s * 10 + (unsigned) (args[i] - '0');
		kapi_msleep (s * 1000);
		return;
	}

	// Builtin: `wait <command>`: run it and wait for its end
	int wait = 0;
	if (ax_streq (line, "wait") && *args)
	{
		wait = 1; line = args;
		while (*args != '\0' && *args != ' ' && *args != '\t') args++;
		if (*args != '\0') { *args++ = '\0'; while (*args == ' ' || *args == '\t') args++; }
	}

	char path[128]; int p = 0;				// /bin/<token>
	ax_strcat (path, sizeof path, &p, "SD:bin/");
	ax_strcat (path, sizeof path, &p, line);

	if (wait)
	{
		void *proc = kapi_spawn (path, args, 0, 0);
		if (proc) kapi_wait (proc);
		else { ax_puts ("init: cannot run "); ax_putln (path); }
		return;
	}
	if (!kapi_exec (path, args))
	{
		ax_puts ("init: cannot run "); ax_putln (path);
	}
}

int main (void)
{
	void *f = kapi_open ("SD:etc/autostart");
	if (f == 0) { ax_putln ("init: no /etc/autostart"); return 1; }
	int n = 0;
	for (;;)						// (a read may return less than asked: until the end)
	{
		int r = kapi_read (f, g_buf + n, (unsigned) (sizeof (g_buf) - 1 - n));
		if (r <= 0) break;
		n += r;
		if (n >= (int) sizeof (g_buf) - 1) { ax_putln ("init: /etc/autostart too long: its end ignored"); break; }
	}
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
