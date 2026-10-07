//
// bmp.cpp -- an icon's picture (bmp.h): read by ImageKit (imgload.cpp's img_load), given as the icons
// are drawn.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include "uikit.h"
#include "bmp.h"
#include "imagekit/img/imgload.hpp"

namespace ui {

unsigned *icon_load (const char *path, int *pw, int *ph)
{
	ImgFrames im;
	if (!img_load (path, &im) || im.n < 1 || im.px[0] == 0) return 0;
	unsigned *px = im.px[0];
	for (int i = 1; i < im.n; i++) { delete [] im.px[i]; im.px[i] = 0; }	// (an animation: its first picture)
	long n = (long) im.w * im.h;
	bool alpha = false;							// (a format without transparency says 0 everywhere)
	for (long i = 0; i < n && !alpha; i++) alpha = (px[i] >> 24) != 0;
	for (long i = 0; i < n; i++)
		px[i] = (alpha && (px[i] >> 24) < 128) ? 0x00FF00FFu : (px[i] & 0x00FFFFFFu);
	*pw = im.w; *ph = im.h;
	return px;
}
unsigned *bmp_decode (const char *path, int *pw, int *ph) { return icon_load (path, pw, ph); }

} // namespace ui
