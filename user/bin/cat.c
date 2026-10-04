//
// cat -- concatenate. Usage: cat [-n] [file ...]. With file arguments, copies each file
// to stdout ("-": stdin); with none, copies stdin to stdout (until EOF). -n numbers the lines.
// A /bin console tool.
//
#define TOOL_NAME "cat"
#include "tool.h"

int tool_main (int argc, char **argv)
{
	int a = 1, number = 0, rc = 0, bol = 1;
	long line = 0;
	if (a < argc && t_streq (argv[a], "-n")) { number = 1; a++; }
	int files = argc - a;
	for (int i = a; i < argc || (files == 0 && i == a); i++)
	{
		const char *path = files ? argv[i] : 0;
		struct t_file *f = t_open (path);
		if (f == 0) { t_err ("cannot open", path); rc = 1; continue; }
		char b[1024]; long k;
		while ((k = t_read (f, b, sizeof b)) > 0)
		{
			if (!number) t_put (b, k);
			else
				for (long j = 0; j < k; j++)
				{
					if (bol) { t_putnumw (++line, 6); t_putc ('\t'); bol = 0; }
					t_putc (b[j]);
					if (b[j] == '\n') bol = 1;
				}
			if (f->is_stdin) t_flush ();
		}
		t_close (f);
	}
	return rc;
}
