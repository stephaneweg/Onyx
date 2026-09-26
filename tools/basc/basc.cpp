//
// basc -- the Onyx BASIC compiler on the build machine: basc <program.bas> <program.bax>
// Built from the same core as /bin/basic (user/basic); a .bax is the same on every
// machine. `make stage` uses it for the apps written in BASIC (kernel/Makefile).
//
#include <stdio.h>
#include "basic/bas.h"

int main (int argc, char **argv)
{
	if (argc != 3) { fprintf (stderr, "usage: basc <program.bas> <program.bax>\n"); return 1; }
	FILE *f = fopen (argv[1], "rb");
	if (!f) { fprintf (stderr, "basc: cannot read %s\n", argv[1]); return 1; }
	static char src[1 << 20];
	size_t n = fread (src, 1, sizeof src - 1, f); fclose (f); src[n] = 0;
	bas::Error e;
	bas::Program *p = bas::compile (src, &e);
	if (!p) { fprintf (stderr, "%s:%d: %s\n", argv[1], e.line, e.msg); return 2; }
	char *bytes; int len = bas::saveBax (p, &bytes);
	bas::destroy (p);
	FILE *o = fopen (argv[2], "wb");
	if (!o || fwrite (bytes, 1, (size_t) len, o) != (size_t) len || fclose (o) != 0) { fprintf (stderr, "basc: cannot write %s\n", argv[2]); return 1; }
	delete [] bytes;
	return 0;
}
