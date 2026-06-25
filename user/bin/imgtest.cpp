//
// imgtest -- proof that the image codecs (zlib + libpng) decode end-to-end on Onyx.
//
// Decodes a small embedded PNG with onyximg::decode (../img/image.hpp) and prints its
// dimensions + a few pixels. Self-contained (no SD asset). Newlib app: it links libpng
// + zlib + libm and uses printf/malloc, so it is OPT-IN -- build it only when the codec
// libs are present (see ../bin/Makefile, IMG_DIR; recipe in ../img/README.md):
//
//     make -C user/img                                   # build the libs once
//     make -C user/bin IMG_DIR=../img imgtest.elf
//
#include <stdio.h>
#include <stdlib.h>
#include "img/image.hpp"

// 4x4 8-bit RGBA PNG, top-left pixel = opaque red (decodes to 0xFFFF0000).
static const unsigned char test_png[] = {
	137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,4,0,0,0,4,8,6,0,0,0,169,241,158,126,0,
	0,0,44,73,68,65,84,120,218,21,200,49,1,0,48,16,2,49,132,33,140,177,162,240,71,239,135,44,
	209,164,89,111,65,33,249,205,8,234,139,16,8,154,139,18,8,138,15,92,5,35,176,220,90,181,107,
	0,0,0,0,73,69,78,68,174,66,96,130,
};
static const int test_png_len = 101;

int main(void)
{
	int w = 0, h = 0;
	unsigned *px = onyximg::decode(test_png, test_png_len, &w, &h);
	if (!px)
	{
		printf("imgtest: decode FAILED\n");
		return 1;
	}

	printf("imgtest: decoded embedded PNG -> %dx%d (0xAARRGGBB)\n", w, h);
	printf("  top-left   = %08X  (expect FFFF0000 = opaque red)\n", px[0]);
	if (w * h >= 16)
		printf("  bottom-right = %08X\n", px[w * h - 1]);

	int ok = (w == 4 && h == 4 && px[0] == 0xFFFF0000u);
	printf("imgtest: %s\n", ok ? "PASS" : "FAIL");

	free(px);
	return ok ? 0 : 1;
}
