// A whole race of the computer's pilots on the PC: the laps, the places, the items -- the race's logic tested.
#include "Apps/cosmozoom/race.h"
#include <stdio.h>
using namespace cz;
static Track T; static Race R;
int main (int argc, char **argv)
{
	const char *trk = argc > 1 ? argv[1] : "sdcard/apps/cosmozoom.app/tracks/harbour.trk";
	if (!T.load (trk)) { fprintf (stderr, "no track %s\n", trk); return 1; }
	for (int i = 0; i < 8; i++) { R.c[i].pilot = i; R.c[i].build = PILOTS[i].build; R.c[i].human = false; R.c[i].skill = 0.9f + 0.015f * i; }
	R.start (&T, 8, 3, 1, argc > 2 ? atoi (argv[2]) : 7);
	int counts[32] = { 0 }; int frames = 0;
	while (!R.over && frames < 60 * 600)
	{
		R.step (1.0f / 60, 0); frames++;
		for (int e = 0; e < R.nev; e++) counts[R.ev[e].kind]++;
		for (int i = 0; i < 8; i++) if (R.c[i].s != R.c[i].s || R.c[i].l != R.c[i].l) { printf ("NaN craft %d at frame %d\n", i, frames); return 1; }
	}
	printf ("track %s: %.0f m, %d segs; race %.1f s (%d frames)\n", T.name, T.length, T.n, R.time, frames);
	for (int p = 0; p < 8; p++) for (int i = 0; i < 8; i++) if (R.c[i].place == p)
		printf (" %d. %-6s fin=%d time %.1f best lap %.1f\n", p + 1, PILOTS[R.c[i].pilot].name, R.c[i].finished, R.c[i].finishTime, R.c[i].bestLap);
	const char *nm[] = { "none","count","go","boostpad","box","itemgot","use","hit","blocked","wall","bump","lap","final","finish","ramp","land","miniturbo","spring","pop","placeup" };
	for (int k = 0; k < 20; k++) printf ("%s=%d ", nm[k], counts[k]);
	printf ("\n");
	return 0;
}
