//
// tools/icons/archiver_icon.cpp -- the Archiver's dock / launcher icon (sdcard/apps/archiver.app/icon.bmp)
// drawn by the app's own code: the crate of user/Apps/archiver/icons.h (the one in its window), at 4
// times the size on black and on white (its coverage from the two), brought down to 40 x 40; the
// pixels more than half covered kept, the others magenta (the see-through key of the icons).
//   sh tools/icons/archiver_icon.sh
//
#include <stdio.h>
#include "Apps/archiver/icons.h"

using namespace uikit;
int main (int argc, char **argv)
{
	const int S = 160, N = 40;
	static unsigned blk[S * S], wht[S * S];
	Canvas a, b;
	a.adopt (blk, S, S); b.adopt (wht, S, S);
	a.clear (0x000000); b.clear (0xFFFFFF);
	ui::icon_archive (a, 8, 4, 144); ui::icon_archive (b, 8, 4, 144);
	FILE *f = fopen (argc > 1 ? argv[1] : "icon.ppm", "wb");
	fprintf (f, "P6\n%d %d\n255\n", N, N);
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++)
		{
			double r = 0, g = 0, bl = 0, al = 0;
			for (int j = 0; j < 4; j++)
				for (int i = 0; i < 4; i++)
				{
					unsigned p = blk[(y * 4 + j) * S + x * 4 + i], q = wht[(y * 4 + j) * S + x * 4 + i];
					double alpha = 1.0 - (((q >> 8) & 255) - ((p >> 8) & 255)) / 255.0;	// (green: no blue / red bias)
					al += alpha; r += (p >> 16) & 255; g += (p >> 8) & 255; bl += p & 255;
				}
			al /= 16;
			unsigned char px[3] = { 255, 0, 255 };
			if (al >= 0.5)
			{
				px[0] = (unsigned char) (r / 16 / al > 255 ? 255 : r / 16 / al); px[1] = (unsigned char) (g / 16 / al > 255 ? 255 : g / 16 / al);
				px[2] = (unsigned char) (bl / 16 / al > 255 ? 255 : bl / 16 / al);
				if (px[0] == 255 && px[1] == 0 && px[2] == 255) px[2] = 254;	// (never the key by chance)
			}
			fwrite (px, 1, 3, f);
		}
	fclose (f);
	return 0;
}
