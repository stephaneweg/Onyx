//
// cat -- concatenate. Usage: cat [file ...]. With file arguments, copies each file
// to stdout; with none, copies stdin to stdout (until EOF). A /bin console tool.
//
#include "kapi.h"
#include "applib.h"

static void cat_stream_in (void)		// stdin -> stdout
{
	char buf[256];
	int n;
	while ((n = kapi_stdin_read (buf, sizeof (buf))) > 0)
	{
		kapi_stdout_write (buf, (unsigned) n);
	}
}

static int cat_file (const char *path)
{
	void *f = kapi_open (path);
	if (f == 0) { ax_puts ("cat: cannot open "); ax_putln (path); return 1; }
	char buf[256];
	int n;
	while ((n = kapi_read (f, buf, sizeof (buf))) > 0)
	{
		kapi_stdout_write (buf, (unsigned) n);
	}
	kapi_close (f);
	return 0;
}

int main (void)
{
	char args[256];
	int len = kapi_get_args (args, sizeof (args));

	// Any non-space argument?
	int any = 0;
	for (int i = 0; i < len; i++) if (args[i] != ' ' && args[i] != '\t') { any = 1; break; }
	if (!any) { cat_stream_in (); return 0; }

	// Walk whitespace-separated file paths.
	int i = 0, rc = 0;
	while (i < len)
	{
		while (i < len && (args[i] == ' ' || args[i] == '\t')) i++;
		char path[128]; int p = 0;
		while (i < len && args[i] != ' ' && args[i] != '\t' && p < (int) sizeof (path) - 1)
			path[p++] = args[i++];
		path[p] = '\0';
		if (p > 0) rc |= cat_file (path);
	}
	return rc;
}
