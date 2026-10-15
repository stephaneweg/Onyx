// objview: a model (.obj) seen as the game draws it -- the toon light, the outline, the glass, the glow -- from
// four sides, on a light background, into a .ppm (the PC: the modeller's preview).
//   objview model.obj out.ppm [size]
#include "Apps/cosmozoom/gfx.h"
#include <stdio.h>
using namespace cz;
static Frame F; static TexStore tex;
int main (int argc, char **argv)
{
	if (argc < 3) { fprintf (stderr, "objview model.obj out.ppm [size]\n"); return 2; }
	static Mesh m;
	if (!loadObj (argv[1], m)) { fprintf (stderr, "cannot read %s\n", argv[1]); return 1; }
	int ntris = m.tris.n;
	lightMesh (m);
	float sz = len (m.hi - m.lo);
	addOutline (m, clampf (sz * 0.012f, 0.03f, 0.25f));
	int S = argc > 3 ? atoi (argv[3]) : 320;
	int W = S * 2, H = S * 2;
	unsigned *px = (unsigned *) malloc (sizeof (unsigned) * W * H); float *zb = (float *) malloc (sizeof (float) * S * S);
	V3 c = (m.lo + m.hi) * 0.5f; float r = sz * 0.5f + 0.01f;
	// front-left 3/4, side (from +X), top, back 3/4
	V3 dirs[4] = { norm (V3 (0.8f, 0.45f, 1.0f)), V3 (1, 0.05f, 0.001f), V3 (0.001f, 1, 0.12f), norm (V3 (-0.9f, 0.5f, -1.0f)) };
	static unsigned tile[320 * 320 * 4];
	for (int k = 0; k < 4; k++)
	{
		V3 eye = c + dirs[k] * (r * 2.6f);
		M4 VP = M4::perspective (0.8f, 1, r * 0.2f, r * 8) * M4::lookAt (eye, c, fabsf (dirs[k].y) > 0.9f ? V3 (0, 0, 1) : V3 (0, 1, 0));
		F.clear ();
		F.begin (VP, 0, B_CULL); F.mesh (m, M4::identity ()); F.end ();
		F.begin (VP, 0, B_ALPHA | B_NOZWRITE); F.mesh (m, M4::identity (), 255, 0, 1); F.end ();
		F.begin (VP, 0, B_ADD | B_NOZWRITE); F.mesh (m, M4::identity (), 255, 0, 2); F.end ();
		unsigned *t = (unsigned *) malloc (sizeof (unsigned) * S * S);
		softRender (F, tex, t, S, S, S, zb, k == 2 ? 0xDDEBFF : 0xEAF4FF, false);
		for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) px[(y + (k / 2) * S) * W + x + (k % 2) * S] = t[y * S + x];
		free (t);
	}
	FILE *f = fopen (argv[2], "wb"); fprintf (f, "P6 %d %d 255\n", W, H);
	for (int i = 0; i < W * H; i++) { unsigned char b[3] = { (unsigned char) (px[i] >> 16), (unsigned char) (px[i] >> 8), (unsigned char) px[i] }; fwrite (b, 1, 3, f); }
	fclose (f);
	printf ("%s: %d triangles, size %.2f x %.2f x %.2f m (x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f), %d groups, %d sockets\n", argv[1], ntris,
		m.hi.x - m.lo.x, m.hi.y - m.lo.y, m.hi.z - m.lo.z, m.lo.x, m.hi.x, m.lo.y, m.hi.y, m.lo.z, m.hi.z, m.ngroups, m.npt);
	for (int i = 0; i < m.npt; i++) printf ("  socket %s %.2f %.2f %.2f\n", m.ptName[i], m.pt[i].x, m.pt[i].y, m.pt[i].z);
	return 0;
}
