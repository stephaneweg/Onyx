//
// basc -- the Onyx BASIC compiler on the build machine:
//   basc <program.bas> <program.bax>
//   basc --standalone <runtime> <program.bas> <main>
// Built from the same core as /bin/basic (user/Libs/basic); a .bax is the same on every
// machine. `make stage` uses it for the apps written in BASIC (kernel/Makefile). --standalone
// writes what the editor's Make App > Standalone makes on the Pi: the runtime (the card's
// SD:/bin/basic) with the compiled program attached (bas::attachBax) -- an app's `main`, run
// like any native program, in machine code.
//
#include <stdio.h>
#include <string.h>
#include "basic/bas.h"

static char *read_all (const char *path, long *len)
{
	FILE *f = fopen (path, "rb");
	if (!f) return 0;
	fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
	char *b = new char[n + 1];
	if (fread (b, 1, (size_t) n, f) != (size_t) n) { fclose (f); delete [] b; return 0; }
	fclose (f); b[n] = 0; *len = n;
	return b;
}

int main (int argc, char **argv)
{
	const char *runtime = 0;
	if (argc == 5 && !strcmp (argv[1], "--standalone")) { runtime = argv[2]; argv += 2; argc -= 2; }
	if (argc != 3) { fprintf (stderr, "usage: basc <program.bas> <program.bax>\n       basc --standalone <runtime> <program.bas> <main>\n"); return 1; }
	long n = 0;
	char *src = read_all (argv[1], &n);
	if (!src) { fprintf (stderr, "basc: cannot read %s\n", argv[1]); return 1; }
	bas::Error e;
	bas::Program *p = bas::compile (src, &e);
	delete [] src;
	if (!p) { fprintf (stderr, "%s:%d: %s\n", argv[1], e.line, e.msg); return 2; }
	char *bytes; int len = bas::saveBax (p, &bytes);
	bas::destroy (p);
	if (runtime)
	{
		long rlen = 0;
		char *rt = read_all (runtime, &rlen);
		if (!rt) { fprintf (stderr, "basc: cannot read the runtime %s\n", runtime); return 1; }
		char *out = 0;
		int olen = bas::attachBax (rt, (int) rlen, bytes, len, &out);
		delete [] rt; delete [] bytes;
		bytes = out; len = olen;
	}
	FILE *o = fopen (argv[2], "wb");
	if (!o || fwrite (bytes, 1, (size_t) len, o) != (size_t) len || fclose (o) != 0) { fprintf (stderr, "basc: cannot write %s\n", argv[2]); return 1; }
	delete [] bytes;
	return 0;
}
