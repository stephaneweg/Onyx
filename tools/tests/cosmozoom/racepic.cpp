// A race of the computer's pilots on the PC, pictures of it taken by the game's own scene (the software renderer)
//   racepic track.trk out_prefix [seconds...]
#include "Apps/cosmozoom/scene.h"
#include <stdio.h>
using namespace cz;
static Track T; static Race R; static Scene S; static Frame F; static Textures tx; static Camera cam;
static void save (const char *path, const unsigned *px, int w, int h)
{
	FILE *f = fopen (path, "wb"); fprintf (f, "P6 %d %d 255\n", w, h);
	for (int i = 0; i < w * h; i++) { unsigned char c[3] = { (unsigned char) (px[i] >> 16), (unsigned char) (px[i] >> 8), (unsigned char) px[i] }; fwrite (c, 1, 3, f); }
	fclose (f);
}
int main (int argc, char **argv)
{
	const char *trk = argc > 1 ? argv[1] : "sdcard/apps/cosmozoom.app/tracks/harbour.trk";
	const char *out = argc > 2 ? argv[2] : "/tmp/race";
	snprintf (S.lib.dir, sizeof S.lib.dir, "sdcard/apps/cosmozoom.app/models");
	if (!T.load (trk)) { fprintf (stderr, "no track\n"); return 1; }
	for (int i = 0; i < 8; i++) { R.c[i].pilot = i; R.c[i].build = PILOTS[i].build; R.c[i].human = false; R.c[i].skill = 0.9f + 0.012f * i; }
	tx.make (); R.start (&T, 8, 3, 1, 5);
	S.load (T, 3); S.ships (R);
	const int w = 1280, h = 720; static unsigned px[w * h]; static float zb[w * h];
	int shot = 0; int follow = argc > 3 ? atoi (argv[3]) : 7;
	float when[16]; int nw = 0;
	for (int k = 4; k < argc && nw < 16; k++) when[nw++] = (float) atof (argv[k]);
	if (!nw) { when[0] = -2; when[1] = 4; when[2] = 12; when[3] = 20; nw = 4; }
	float dt = 1.0f / 60;
	while (shot < nw && R.time < 300)
	{
		R.step (dt, 0); S.emitFor (R, dt); cam.follow (R, follow, dt);
		if (R.time >= when[shot])
		{
			F.clear (); S.view (F, R, cam, w, h, follow);
			softRender (F, tx.store, px, w, h, w, zb, 0x8FD8FF, false);
			char p[256]; snprintf (p, sizeof p, "%s%d.ppm", out, shot); save (p, px, w, h);
			printf ("%s: t %.1f, ship %d at s %.0f l %.1f place %d, %d vertices %d batches\n", p, R.time, follow, R.c[follow].s, R.c[follow].l, R.c[follow].place, F.v.n, F.b.n);
			shot++;
		}
	}
	return 0;
}
