// A circuit's picture from a place on it, by the software renderer -> a .ppm (the PC; tests).
#include "Apps/cosmozoom/world.h"
#include <stdio.h>
using namespace cz;
static Track T; static World W; static ModelLib lib; static Frame F; static TexStore tex;
int main (int argc, char **argv)
{
	const char *trk = argc > 1 ? argv[1] : "sdcard/apps/cosmozoom.app/tracks/test.trk";
	float s = argc > 2 ? (float) atof (argv[2]) : 0;
	const char *out = argc > 3 ? argv[3] : "/tmp/view.ppm";
	snprintf (lib.dir, sizeof lib.dir, "sdcard/apps/cosmozoom.app/models");
	if (!T.load (trk)) { fprintf (stderr, "no track\n"); return 1; }
	W.build (T, lib, 1);
	const int w = 640, h = 360;
	V3 eye = T.pointAt (s - 9, 0, 3.2f), at = T.pointAt (s + 6, 0, 1.0f);
	M4 P = M4::perspective (65 * PI / 180, (float) w / h, 0.5f, 1500), V = M4::lookAt (eye, at, V3 (0, 1, 0));
	M4 VP = P * V;
	M4 Vsky = V; Vsky.m[3] = Vsky.m[7] = Vsky.m[11] = 0;
	F.clear ();
	F.begin (P * Vsky, 0, B_NOZWRITE | B_ZALWAYS); F.add (W.sky); F.end ();
	F.begin (VP, 0, B_CULL); F.add (W.far);
	for (int i = 0; i < W.nch; i++) F.add (W.chunk[i]);
	F.end ();
	F.begin (VP, 0, B_ALPHA | B_NOZWRITE);
	for (int i = 0; i < W.nch; i++) F.add (W.clear[i]);
	F.end ();
	F.begin (VP, 0, B_ADD | B_NOZWRITE);
	for (int i = 0; i < W.nch; i++) F.add (W.glow[i]);
	F.end ();
	static unsigned px[w * h]; static float zb[w * h];
	softRender (F, tex, px, w, h, w, zb, 0, false);
	FILE *f = fopen (out, "wb"); fprintf (f, "P6 %d %d 255\n", w, h);
	for (int i = 0; i < w * h; i++) { unsigned char c[3] = { (unsigned char) (px[i] >> 16), (unsigned char) (px[i] >> 8), (unsigned char) px[i] }; fwrite (c, 1, 3, f); }
	fclose (f);
	printf ("segments %d length %.0f verts %d\n", T.n, T.length, F.v.n);
	return 0;
}
