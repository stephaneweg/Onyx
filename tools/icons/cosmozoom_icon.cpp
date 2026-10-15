// cosmozoom_icon -- Cosmo Zoom!'s icon: the Nova Streak drawn by the game's own scene code (software), 160 x 160, on a
// round night-blue badge with light streaks; tools/icons/cosmozoom_icon.sh brings it down to 40 x 40 (magenta: see-through).
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
#include "Apps/cosmozoom/scene.h"
#include <stdio.h>
using namespace cz;
int main (int argc, char **argv)
{
	static ModelLib lib; static ShipModel S; static Frame F; static Textures tx;
	snprintf (lib.dir, sizeof lib.dir, "sdcard/apps/cosmozoom.app/models");
	tx.make ();
	Build b = { 1, 3, 0, 0, 9, 1 };			// Nova Streak, swept fins, the ion jet, magenta and white
	buildShip (S, lib, b, 0);
	const int W = 160;
	static unsigned px[W * W]; static float zb[W * W];
	float L = len (S.mesh.hi - S.mesh.lo);
	V3 c = (S.mesh.hi + S.mesh.lo) * 0.5f;
	V3 eye = c + norm (V3 (1.0f, 0.5f, 0.7f)) * (L * 0.72f);
	M4 VP = M4::perspective (0.75f, 1, 0.1f, 100) * M4::lookAt (eye, c, V3 (0, 1, 0));
	F.begin (VP, 0, B_CULL); F.mesh (S.mesh, M4::identity ()); F.end ();
	F.begin (VP, 0, B_ALPHA | B_NOZWRITE | B_CULL); F.mesh (S.mesh, M4::identity (), 255, 0, 1); F.end ();
	F.begin (VP, 0, B_ADD | B_NOZWRITE); F.mesh (S.mesh, M4::identity (), 255, 0, 2); F.end ();
	// the badge first (a dark disc, streaks), then the ship over it
	for (int y = 0; y < W; y++)
		for (int x = 0; x < W; x++)
		{
			float dx = (float) x - 79.5f, dy = (float) y - 79.5f, d = sqrtf (dx * dx + dy * dy);
			unsigned col = 0xFF00FF;
			if (d < 78) col = rgbMix (0x1A2A7A, 0x0A1030, (float) y / W);
			if (d < 78 && fabsf ((float) (x - y) * 0.7f + 30) < 3) col = 0xFF2BD6;
			if (d < 78 && fabsf ((float) (x - y) * 0.7f - 10) < 2) col = 0x22E6FF;
			if (d >= 72 && d < 78) col = 0x22E6FF;
			px[y * W + x] = col;
		}
	softRender (F, tx.store, px, W, W, W, zb, 0, true);
	FILE *f = fopen (argc > 1 ? argv[1] : "/tmp/cz_icon.ppm", "wb"); fprintf (f, "P6 %d %d 255\n", W, W);
	for (int i = 0; i < W * W; i++) { unsigned char q[3] = { (unsigned char) (px[i] >> 16), (unsigned char) (px[i] >> 8), (unsigned char) px[i] }; fwrite (q, 1, 3, f); }
	fclose (f);
	return 0;
}
