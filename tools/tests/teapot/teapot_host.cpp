//
// teapot_host -- the teapot demo's geometry and software renderer (user/Apps/teapot/teapot.h)
// on the PC: a few frames into PPM pictures, and the timing.
//   teapot_host <out_prefix> [w h]      -> <out_prefix>_0.ppm ... _3.ppm
//
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "Apps/teapot/teapot.h"

int main (int argc, char **argv)
{
	const char *pre = argc > 1 ? argv[1] : "teapot";
	int w = argc > 3 ? atoi (argv[2]) : 640, h = argc > 3 ? atoi (argv[3]) : 480;
	static teapot::Model m;
	teapot::build (m);
	static teapot::Vertex v[teapot::VERTS];
	unsigned *px = new unsigned[w * h]; float *zb = new float[w * h];
	clock_t t0 = clock ();
	int n = 0;
	for (int f = 0; f < 4; f++)
	{
		n = teapot::frame (m, f * 1.3f, 0.35f, w, h, v);
		teapot::raster (v, n, 0x202030, px, w, h, w, zb);
		char name[256]; snprintf (name, sizeof name, "%s_%d.ppm", pre, f);
		FILE *o = fopen (name, "wb"); fprintf (o, "P6\n%d %d\n255\n", w, h);
		for (int i = 0; i < w * h; i++) { fputc (px[i] >> 16, o); fputc ((px[i] >> 8) & 255, o); fputc (px[i] & 255, o); }
		fclose (o);
	}
	printf ("%d triangles, %.1f ms a frame (software)\n", n / 3, (double) (clock () - t0) * 1000 / CLOCKS_PER_SEC / 4);
	return 0;
}
