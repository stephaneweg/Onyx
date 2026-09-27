//
// render_host.cpp -- the BASIC 3D on the PC (tools/tests/run_basic3d_test.sh): a bas::ScreenHost
// with no window (its software renderer draws), which runs a program and saves the active page
// at each RENDER3D as out_<n>.ppm -- to look at, and checked for the colours the test expects.
//   basic3d_host prog.bas outdir
//
#include "basic/basscreen.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

struct FileHost : bas::ScreenHost
{
	const char *dir = "."; int shots = 0; unsigned t = 0;
	bool openWindow (int, int) override { return true; }
	void resizeWindow (int, int) override {}
	void present (bool) override {}
	void pumpEvents () override {}
	bool stopRequested () override { return false; }
	unsigned nowMs () override { return t += 16; }
	void sleepRaw (int) override {}
	int inputLine (char *buf, int) override { buf[0] = 0; return -1; }
	void finished (bool) override {}				// (no "press any key")
	int render3d (const bas::G3Vertex *v, int nv, const bas::G3Batch *b, int nb, unsigned clear, bool keep) override
	{
		int r = ScreenHost::render3d (v, nv, b, nb, clear, keep);
		char path[512]; snprintf (path, sizeof path, "%s/out_%d.ppm", dir, shots++);
		FILE *f = fopen (path, "wb");
		if (f)
		{
			fprintf (f, "P6\n%d %d\n255\n", W, H);
			for (int i = 0; i < W * H; i++) { unsigned c = db ()[i]; unsigned char p[3] = { (unsigned char) (c >> 16), (unsigned char) (c >> 8), (unsigned char) c }; fwrite (p, 1, 3, f); }
			fclose (f);
		}
		printf ("render3d %d: %d vertices, %d batches -> %s\n", shots - 1, nv, nb, path);
		return r;
	}
};

int main (int argc, char **argv)
{
	if (argc < 3) { fprintf (stderr, "usage: basic3d_host prog.bas outdir\n"); return 2; }
	FILE *f = fopen (argv[1], "rb");
	if (!f) { perror (argv[1]); return 1; }
	static char src[1 << 20]; size_t n = fread (src, 1, sizeof src - 1, f); src[n] = 0; fclose (f);
	bas::Error e;
	bas::Program *p = bas::compile (src, &e);
	if (!p) { printf ("compile error line %d: %s\n", e.line, e.msg); return 1; }
	FileHost h; h.dir = argv[2];
	int rc = bas::run (p, h, &e);
	if (rc) printf ("runtime error line %d: %s\n", e.line, e.msg);
	bas::destroy (p);
	return rc ? 1 : 0;
}
