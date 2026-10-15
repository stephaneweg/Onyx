// A circuit's picture from a place on it, by the software renderer -> a .ppm (the PC; tests).
//   view track.trk s out.ppm             (from the track, at s metres)
//   view track.trk aerial out.ppm [deg]  (the whole circuit from far away, from the side `deg` around it)
#include "Apps/cosmozoom/world.h"
#include <stdio.h>
#include <string.h>
using namespace cz;
static Track T; static World W; static ModelLib lib; static Frame F; static TexStore tex;
int main (int argc, char **argv)
{
	const char *trk = argc > 1 ? argv[1] : "sdcard/apps/cosmozoom.app/tracks/harbour.trk";
	bool aerial = argc > 2 && !strcmp (argv[2], "aerial");
	float s = argc > 2 && !aerial ? (float) atof (argv[2]) : 0;
	const char *out = argc > 3 ? argv[3] : "/tmp/view.ppm";
	snprintf (lib.dir, sizeof lib.dir, "sdcard/apps/cosmozoom.app/models");
	if (!T.load (trk)) { fprintf (stderr, "no track\n"); return 1; }
	W.build (T, lib, 1);
	const int w = aerial ? 1600 : 1280, h = aerial ? 900 : 720;
	V3 eye = T.pointAt (s - 9, 0, 3.2f), at = T.pointAt (s + 6, 0, 1.0f);
	float fov = 65, zfar = 1500;
	if (aerial)
	{
		// the circuit's bounds; the eye far out and up, looking at its middle
		V3 lo (1e9f, 1e9f, 1e9f), hi (-1e9f, -1e9f, -1e9f);
		for (float t = 0; t < T.length; t += 5) { V3 p = T.pointAt (t, 0, 0); lo = V3 (fminf (lo.x, p.x), fminf (lo.y, p.y), fminf (lo.z, p.z)); hi = V3 (fmaxf (hi.x, p.x), fmaxf (hi.y, p.y), fmaxf (hi.z, p.z)); }
		at = (lo + hi) * 0.5f;
		float r = len (hi - lo) * 0.5f, a = (argc > 4 ? (float) atof (argv[4]) : 35) * PI / 180;
		eye = at + V3 (sinf (a), 0.75f, cosf (a)) * (r * 1.55f);
		fov = 50; zfar = r * 8 + 3000;
	}
	M4 P = M4::perspective (fov * PI / 180, (float) w / h, aerial ? 2.0f : 0.5f, zfar), V = M4::lookAt (eye, at, V3 (0, 1, 0));
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
	static unsigned px[1600 * 900]; static float zb[1600 * 900];
	softRender (F, tex, px, w, h, w, zb, 0, false);
	FILE *f = fopen (out, "wb"); fprintf (f, "P6 %d %d 255\n", w, h);
	for (int i = 0; i < w * h; i++) { unsigned char c[3] = { (unsigned char) (px[i] >> 16), (unsigned char) (px[i] >> 8), (unsigned char) px[i] }; fwrite (c, 1, 3, f); }
	fclose (f);
	printf ("segments %d length %.0f verts %d\n", T.n, T.length, F.v.n);
	return 0;
}
