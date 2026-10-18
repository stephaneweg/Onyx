//
// draw2d.h -- the game's flat drawing over the 3D picture: the HUD and the menus. Rounded panels, gradients,
// discs and rings, stars, bars, see-through colours (0xAARRGGBB: AA = 0 opaque .. 255 invisible is NOT used:
// here alpha is 0..255 opaque), into any 0x00RRGGBB buffer. Text is the game's (main.cpp: UIKit's faces).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef CZ_DRAW2D_H
#define CZ_DRAW2D_H

#include "math3d.h"

namespace cz {

struct Surf
{
	unsigned *px; int w, h, stride;
	int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;		// the clip
	void setClip (int x0, int y0, int x1, int y1) { cx0 = x0 < 0 ? 0 : x0; cy0 = y0 < 0 ? 0 : y0; cx1 = x1 > w ? w : x1; cy1 = y1 > h ? h : y1; }
	void noClip () { cx0 = cy0 = 0; cx1 = w; cy1 = h; }

	inline void blend (int x, int y, unsigned c, int a)
	{
		if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1 || a <= 0) return;
		unsigned &d = px[(long) y * stride + x];
		if (a >= 255) { d = c & 0xFFFFFF; return; }
		unsigned rb = ((c & 0xFF00FF) * (unsigned) a + (d & 0xFF00FF) * (unsigned) (255 - a)) >> 8;
		unsigned g = ((c & 0x00FF00) * (unsigned) a + (d & 0x00FF00) * (unsigned) (255 - a)) >> 8;
		d = (rb & 0xFF00FF) | (g & 0x00FF00);
	}
	void span (int x0, int x1, int y, unsigned c, int a)
	{
		if (y < cy0 || y >= cy1) return;
		if (x0 < cx0) x0 = cx0;
		if (x1 > cx1) x1 = cx1;
		if (a >= 255) { unsigned *p = px + (long) y * stride; for (int x = x0; x < x1; x++) p[x] = c & 0xFFFFFF; return; }
		for (int x = x0; x < x1; x++) blend (x, y, c, a);
	}
	void rect (int x, int y, int rw, int rh, unsigned c, int a = 255) { for (int j = y; j < y + rh; j++) span (x, x + rw, j, c, a); }
	// a vertical gradient
	void vgrad (int x, int y, int rw, int rh, unsigned top, unsigned bot, int a = 255)
	{
		for (int j = 0; j < rh; j++) span (x, x + rw, y + j, rgbMix (top, bot, rh > 1 ? (float) j / (float) (rh - 1) : 0), a);
	}
	// a rounded rectangle (anti-aliased corners); grad: a second colour at the bottom (0xFF000000: none)
	void rrect (int x, int y, int rw, int rh, int r, unsigned c, int a = 255, unsigned bot = 0xFF000000u)
	{
		if (r * 2 > rh) r = rh / 2;
		if (r * 2 > rw) r = rw / 2;
		for (int j = 0; j < rh; j++)
		{
			unsigned col = bot == 0xFF000000u ? c : rgbMix (c, bot, rh > 1 ? (float) j / (float) (rh - 1) : 0);
			float dy = 0;
			if (j < r) dy = (float) (r - j) - 0.5f;
			else if (j >= rh - r) dy = (float) (j - (rh - r)) + 0.5f;
			if (dy <= 0) { span (x, x + rw, y + j, col, a); continue; }
			float dx = sqrtf (fmaxf ((float) (r * r) - dy * dy, 0));
			float inset = (float) r - dx;
			int ii = (int) inset;
			float frac = inset - (float) ii;
			span (x + ii + 1, x + rw - ii - 1, y + j, col, a);
			blend (x + ii, y + j, col, (int) ((float) a * (1 - frac)));
			blend (x + rw - ii - 1, y + j, col, (int) ((float) a * (1 - frac)));
		}
	}
	// a panel: a dark outline, its colour, a light line at its top
	void panel (int x, int y, int rw, int rh, int r, unsigned c, unsigned outline, int a = 255, int thick = 3)
	{
		rrect (x - thick, y - thick, rw + 2 * thick, rh + 2 * thick, r + thick, outline, a);
		rrect (x, y, rw, rh, r, c, a, rgbScale (c, 0.86f));
		rrect (x + r / 2, y + 2, rw - r, rh / 3 > 4 ? 3 : 1, 2, rgbMix (c, 0xFFFFFF, 0.5f), a / 2);
	}
	void disc (float cx, float cy, float r, unsigned c, int a = 255)
	{
		int y0 = (int) (cy - r - 1), y1 = (int) (cy + r + 1);
		for (int y = y0; y <= y1; y++)
		{
			float dy = (float) y + 0.5f - cy;
			float q = r * r - dy * dy;
			if (q < 0) continue;
			float dx = sqrtf (q);
			int xa = (int) (cx - dx), xb = (int) (cx + dx);
			span (xa + 1, xb, y, c, a);
			float fa = (cx - dx) - (float) xa, fb = (cx + dx) - (float) xb;
			blend (xa, y, c, (int) ((float) a * (1 - fa)));
			blend (xb, y, c, (int) ((float) a * fb));
		}
	}
	void ring (float cx, float cy, float r, float t, unsigned c, int a = 255)
	{
		int y0 = (int) (cy - r - 1), y1 = (int) (cy + r + 1);
		for (int y = y0; y <= y1; y++)
			for (int x = (int) (cx - r - 1); x <= (int) (cx + r + 1); x++)
			{
				float d = sqrtf (((float) x + 0.5f - cx) * ((float) x + 0.5f - cx) + ((float) y + 0.5f - cy) * ((float) y + 0.5f - cy));
				float k = clampf (t * 0.5f - fabsf (d - (r - t * 0.5f)) + 0.5f, 0, 1);
				if (k > 0) blend (x, y, c, (int) ((float) a * k));
			}
	}
	// a filled polygon (convex or not: even-odd), points as x0 y0 x1 y1 ...
	void poly (const float *p, int n, unsigned c, int a = 255)
	{
		float ymin = 1e9f, ymax = -1e9f;
		for (int i = 0; i < n; i++) { ymin = fminf (ymin, p[i * 2 + 1]); ymax = fmaxf (ymax, p[i * 2 + 1]); }
		for (int y = (int) ymin; y <= (int) ymax; y++)
		{
			float sy = (float) y + 0.5f, xs[32]; int k = 0;
			for (int i = 0; i < n && k < 32; i++)
			{
				float x0 = p[i * 2], y0 = p[i * 2 + 1], x1 = p[((i + 1) % n) * 2], y1 = p[((i + 1) % n) * 2 + 1];
				if ((y0 <= sy && y1 > sy) || (y1 <= sy && y0 > sy)) xs[k++] = x0 + (sy - y0) / (y1 - y0) * (x1 - x0);
			}
			for (int i = 1; i < k; i++) for (int j = i; j > 0 && xs[j] < xs[j - 1]; j--) { float t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
			for (int i = 0; i + 1 < k; i += 2) span ((int) (xs[i] + 0.5f), (int) (xs[i + 1] + 0.5f), y, c, a);
		}
	}
	void star (float cx, float cy, float r, unsigned c, int a = 255, float rot = 0, float inner = 0.45f)
	{
		float p[20];
		for (int i = 0; i < 10; i++)
		{
			float ang = rot - PI / 2 + PI * (float) i / 5, rr = (i & 1) ? r * inner : r;
			p[i * 2] = cx + cosf (ang) * rr; p[i * 2 + 1] = cy + sinf (ang) * rr;
		}
		poly (p, 10, c, a);
	}
	void line (float x0, float y0, float x1, float y1, float t, unsigned c, int a = 255)
	{
		float dx = x1 - x0, dy = y1 - y0, l = sqrtf (dx * dx + dy * dy);
		if (l < 0.01f) { disc (x0, y0, t * 0.5f, c, a); return; }
		float nx = -dy / l * t * 0.5f, ny = dx / l * t * 0.5f;
		float p[8] = { x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny };
		poly (p, 4, c, a);
	}
	// darkens / lightens everything (a dim behind a card)
	void shade (int a, unsigned c = 0x140A2E) { for (int y = cy0; y < cy1; y++) span (cx0, cx1, y, c, a); }
};

} // namespace cz

#endif
