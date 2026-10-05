//
// tee -- copy stdin to stdout AND into files (to keep what a pipeline shows).
//   usage: tee [-a] <file ...>          -a: add to the files instead of replacing them
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see tool.h).
//
#define TOOL_NAME "tee"
#include "tool.h"

int tool_main (int argc, char **argv)
{
	int append = 0, a = 1, rc = 0, n = 0;
	if (a < argc && t_streq (argv[a], "-a")) { append = 1; a++; }
	void *out[16];
	for (; a < argc && n < 16; a++)
	{
		out[n] = t_create (argv[a], append);
		if (out[n] == 0) { t_err ("cannot write", argv[a]); rc = 1; } else n++;
	}
	struct t_file *f = t_open (0);
	char b[1024]; long k;
	while ((k = t_read (f, b, sizeof b)) > 0)
	{
		t_put (b, k); t_flush ();
		for (int i = 0; i < n; i++) t_fwrite (out[i], b, k);
	}
	for (int i = 0; i < n; i++) t_fclose (out[i]);
	t_close (f);
	return rc;
}
