//
// ui.h -- the game's flat drawing: the text (UIKit's FreeType faces, outlined), the panels and buttons of the
// menus, the gadgets' icons, the pilots' helmets, and the race's HUD (the place, the racers, the gadget's slot,
// the laps and times, the map, the speed, the messages). Drawn over the 3D picture; laid out on a 1280 x 720
// page scaled to the screen (UiPage).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef CZ_UI_H
#define CZ_UI_H

#include "uikit/uikit.h"
#include "uikit/lang.h"
#include "fontkit/uikitface.h"
#include "draw2d.h"
#include "race.h"
#include <stdio.h>

namespace cz {

// the colours of the interface (DESIGN.md: ink, white, the candy accents)
enum : unsigned
{
	C_INK = 0x1E1838, C_WHITE = 0xFFFFFF, C_PINK = 0xFF3D9A, C_YELLOW = 0xFFD23F, C_MINT = 0x3DDC97, C_SKY = 0x4CC9F0,
	C_ORANGE = 0xFF8C2B, C_GRAPE = 0x8B5CF6, C_RED = 0xF2384B, C_PANEL = 0xF4F6FF, C_DIM = 0x8A8FB0, C_TEAL = 0x22C3B5
};

// ---- text ---------------------------------------------------------------------------------------------------------
struct Text
{
	FtTextFace *faces[2][96] = { { 0 } };		// [bold][px]
	uikit::Canvas cv;
	bool ok = true;
	FtTextFace *face (int px, bool bold)
	{
		if (px < 8) px = 8;
		if (px > 95) px = 95;
		FtTextFace *&f = faces[bold ? 1 : 0][px];
		if (!f && ok)
		{
			f = new FtTextFace;
			if (!f->open (bold ? "DejaVu Sans" : "DejaVu Sans", px)) { delete f; f = 0; ok = false; }
		}
		return f;
	}
	int width (const char *s, int px, bool bold = true)
	{
		FtTextFace *f = face (px, bold);
		uikit::UkFaceScope sc (f);
		return uikit::uk_text_w (s, (bold ? 2 : 0) | (italic ? 1 : 0));
	}
	// align: 0 left, 1 centre, 2 right (x is that point); outline: its thickness (0 none)
	bool italic = false;			// (the HUD's numbers: bold italic)
	void draw (Surf &S, int x, int y, int px, const char *s, unsigned col, int align = 0, int outline = 0, bool bold = true, unsigned ink = C_INK)
	{
		if (!s || !*s) return;
		FtTextFace *f = face (px, bold);
		uikit::UkFaceScope sc (f);
		// (the canvas is the surface's clip rectangle: the text cut at its edges)
		int ox = S.cx1 > S.cx0 ? S.cx0 : 0, oy = S.cy1 > S.cy0 ? S.cy0 : 0;
		int cw = S.cx1 > S.cx0 ? S.cx1 - S.cx0 : S.w, ch = S.cy1 > S.cy0 ? S.cy1 - S.cy0 : S.h;
		cv.adopt (S.px + (long) oy * S.stride + ox, cw, ch, S.stride);
		x -= ox; y -= oy;
		int st = (bold ? 2 : 0) | (italic ? 1 : 0);
		int w = uikit::uk_text_w (s, st);
		int x0 = align == 1 ? x - w / 2 : align == 2 ? x - w : x;
		int h = px + px / 2;
		if (outline > 0)
		{
			static const int D[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
			for (int r = outline; r > 0; r -= 2)
				for (int k = 0; k < 8; k++)
				{
					int dx = D[k][0] * r, dy = D[k][1] * r;
					if (k >= 4) { dx = dx * 7 / 10; dy = dy * 7 / 10; }
					uikit::uk_text_l (cv, x0 + dx, y + dy, h, s, ink, st);
				}
			uikit::uk_text_l (cv, x0, y + outline, h, s, ink, st);	// (a little shadow under)
		}
		uikit::uk_text_l (cv, x0, y, h, s, col, st);
	}
};

// ---- the page: a 1280 x 720 layout scaled to the screen (or a view) ------------------------------------------------
struct UiPage
{
	float k = 1; int ox = 0, oy = 0, w = 1280, h = 720;
	void fit (int W, int H, int x0 = 0, int y0 = 0)
	{
		k = fminf ((float) W / 1280.0f, (float) H / 720.0f);
		w = (int) (1280 * k); h = (int) (720 * k);
		ox = x0 + (W - w) / 2; oy = y0 + (H - h) / 2;
	}
	int X (float x) const { return ox + (int) (x * k); }
	int Y (float y) const { return oy + (int) (y * k); }
	int L (float v) const { int r = (int) (v * k + 0.5f); return r < 1 ? 1 : r; }
};

// ---- the HUD's panels: dark, see-through, a neon frame ------------------------------------------------------------------
static void neonPanel (Surf &S, int x, int y, int w, int h, int r, unsigned accent = C_SKY, int alpha = 170)
{
	// the soft look: a rounded see-through deep-blue panel, a soft drop shadow, a light rim (the accent when chosen)
	int rr = r + h / 6;
	S.rrect (x + 2, y + 4, w, h, rr, 0x101830, alpha / 3);
	S.rrect (x - 2, y - 2, w + 4, h + 4, rr + 2, accent == C_SKY || accent == 0x3A4A8A ? 0xFFFFFF : accent, accent == C_SKY || accent == 0x3A4A8A ? 150 : 255);
	S.rrect (x, y, w, h, rr, 0x22305A, alpha + 20 > 255 ? 255 : alpha + 20);
	if (h < 80) S.rrect (x + rr / 2, y + 2, w - rr, h / 3 > 3 ? h / 3 : 3, rr / 2, 0xFFFFFF, 24);
}

// ---- a pilot's helmet (the racers' list, the menus) ------------------------------------------------------------------
static void helmet (Surf &S, float cx, float cy, float r, unsigned col, unsigned trim, int style = 0)
{
	S.disc (cx, cy + r * 0.05f, r + 2.5f, C_INK);
	S.disc (cx, cy, r, col);
	// a stripe over the top
	if (style % 3 == 1) { S.rect ((int) (cx - r * 0.18f), (int) (cy - r), (int) (r * 0.36f), (int) (r * 0.9f), trim); }
	else if (style % 3 == 2) { float p[8] = { cx - r * 0.9f, cy - r * 0.15f, cx + r * 0.9f, cy - r * 0.15f, cx + r * 0.8f, cy - r * 0.38f, cx - r * 0.8f, cy - r * 0.38f }; S.poly (p, 4, trim); }
	// the visor: dark, a glint
	float v[12] = { cx - r * 0.78f, cy - r * 0.02f, cx + r * 0.78f, cy - r * 0.02f, cx + r * 0.66f, cy + r * 0.48f, cx + r * 0.2f, cy + r * 0.62f, cx - r * 0.2f, cy + r * 0.62f, cx - r * 0.66f, cy + r * 0.48f };
	S.poly (v, 6, 0x232045);
	S.line (cx - r * 0.5f, cy + r * 0.12f, cx - r * 0.15f, cy + r * 0.12f, r * 0.12f, 0x9FE8FF, 200);
	S.disc (cx - r * 0.35f, cy - r * 0.5f, r * 0.15f, 0xFFFFFF, 120);
}

// ---- the gadgets' icons ---------------------------------------------------------------------------------------------
static void chevron (Surf &S, float x, float y, float r, unsigned col)
{
	float p[12] = { x - r, y - r * 0.6f, x, y + r * 0.2f, x + r, y - r * 0.6f, x + r, y - r * 0.05f, x, y + r * 0.75f, x - r, y - r * 0.05f };
	S.poly (p, 6, col);
}
static void chevronH (Surf &S, float x, float y, float r, unsigned col, bool left)
{
	float d = left ? -1.0f : 1.0f;
	float p[12] = { x - d * r * 0.6f, y - r, x - d * r * 0.1f, y - r, x + d * r * 0.6f, y, x - d * r * 0.1f, y + r, x - d * r * 0.6f, y + r, x + d * r * 0.0f, y };
	S.poly (p, 6, col);
}
static void itemIcon (Surf &S, int it, float cx, float cy, float r, float t = 0)
{
	if (it <= IT_NONE || it >= IT_COUNT) return;
	unsigned c = ITEMS[it].color;
	switch (ITEMS[it].effect)
	{
	case FX_BOOST:
		if (it == IT_TRIPLE) for (int k = 0; k < 3; k++) chevron (S, cx, cy - r * 0.55f + (float) k * r * 0.42f, r * 0.55f, k == 1 ? C_YELLOW : c);
		else { chevron (S, cx, cy - r * 0.3f, r * 0.7f, C_YELLOW); chevron (S, cx, cy + r * 0.2f, r * 0.7f, c); }
		break;
	case FX_SHIELD:
		S.ring (cx, cy, r * 0.85f, r * 0.22f, c); S.disc (cx, cy, r * 0.5f, c, 90); S.disc (cx - r * 0.3f, cy - r * 0.35f, r * 0.15f, C_WHITE, 200);
		break;
	case FX_SHOT:
		S.disc (cx, cy, r * 0.8f, c, 170); S.ring (cx, cy, r * 0.8f, r * 0.1f, C_WHITE); S.disc (cx - r * 0.3f, cy - r * 0.3f, r * 0.18f, C_WHITE, 230);
		break;
	case FX_DROP:
	{
		float p[16];
		for (int k = 0; k < 8; k++) { float a = (float) k * PI / 4; float rr = r * (0.62f + 0.18f * sinf ((float) k * 2.3f)); p[k * 2] = cx + cosf (a) * rr; p[k * 2 + 1] = cy + sinf (a) * rr * 0.75f + r * 0.1f; }
		S.poly (p, 8, c); S.disc (cx + r * 0.25f, cy - r * 0.05f, r * 0.14f, C_WHITE, 200); S.disc (cx - r * 0.25f, cy + r * 0.15f, r * 0.09f, C_WHITE, 160);
		break;
	}
	case FX_WELL:
		for (int k = 0; k < 3; k++) S.ring (cx, cy, r * (0.85f - 0.25f * (float) k), r * 0.1f, k & 1 ? C_WHITE : c);
		S.disc (cx, cy, r * 0.15f, C_INK);
		break;
	case FX_BEAM:
		S.line (cx - r * 0.8f, cy + r * 0.6f, cx + r * 0.8f, cy - r * 0.6f, r * 0.35f, c, 200);
		S.line (cx - r * 0.8f, cy + r * 0.6f, cx + r * 0.8f, cy - r * 0.6f, r * 0.12f, C_WHITE);
		S.disc (cx + r * 0.75f, cy - r * 0.55f, r * 0.2f, C_WHITE);
		break;
	case FX_EMP:
		S.star (cx, cy, r * 0.9f, c, 255, t, 0.35f); S.star (cx, cy, r * 0.45f, C_WHITE, 255, -t, 0.4f);
		break;
	case FX_NEBULA:
		S.disc (cx - r * 0.35f, cy, r * 0.42f, c); S.disc (cx + r * 0.35f, cy - r * 0.05f, r * 0.45f, c); S.disc (cx, cy - r * 0.3f, r * 0.45f, c);
		{ float p[8] = { cx + r * 0.05f, cy + r * 0.15f, cx - r * 0.2f, cy + r * 0.6f, cx + r * 0.05f, cy + r * 0.55f, cx - r * 0.1f, cy + r * 0.95f }; S.poly (p, 4, C_YELLOW); }
		break;
	case FX_WARP:
		S.star (cx, cy, r * 0.95f, C_INK, 255, t * 0.3f); S.star (cx, cy, r * 0.82f, c, 255, t * 0.3f); S.star (cx, cy, r * 0.4f, C_WHITE, 220, t * 0.3f);
		break;
	}
}

// ---- ordinals ---------------------------------------------------------------------------------------------------------
static const char *ordinal (int place, char *buf, int cap)
{
	// English: 1st 2nd 3rd 4th; French: 1er 2e 3e
	bool fr = !strcmp (uk_lang (), "fr");
	if (fr) snprintf (buf, (size_t) cap, place == 1 ? "%der" : "%de", place);
	else snprintf (buf, (size_t) cap, "%d%s", place, place == 1 ? "st" : place == 2 ? "nd" : place == 3 ? "rd" : "th");
	return buf;
}
static void fmtTime (float t, char *b, int cap)
{
	if (t < 0) t = 0;
	int ms = (int) (t * 100 + 0.5f);
	snprintf (b, (size_t) cap, "%d:%02d.%02d", ms / 6000, (ms / 100) % 60, ms % 100);
}

// ---- the map: the track's centre line seen from above ---------------------------------------------------------------
struct MiniMap
{
	enum { N = 160 };
	float px[N], pz[N]; int n = 0;
	float lox, loz, sc;
	void make (const Track &T)
	{
		n = 0;
		float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
		for (int k = 0; k < N; k++)
		{
			const V3 &p = T.seg[k * T.n / N].c;
			px[k] = p.x; pz[k] = p.z; n++;
			x0 = fminf (x0, p.x); x1 = fmaxf (x1, p.x); z0 = fminf (z0, p.z); z1 = fmaxf (z1, p.z);
		}
		lox = (x0 + x1) * 0.5f; loz = (z0 + z1) * 0.5f; sc = fmaxf (x1 - x0, z1 - z0) * 0.5f + 1;
	}
	// a world point -> the map's square (cx, cy, half size r); x mirrored so that the map is as seen from above
	void at (float wx, float wz, float cx, float cy, float r, float *ox, float *oy) const
	{
		*ox = cx - (wx - lox) / sc * r; *oy = cy - (wz - loz) / sc * r;
	}
	void draw (Surf &S, const Race &R, float cx, float cy, float r, int me)
	{
		for (int pass = 0; pass < 2; pass++)
			for (int k = 0; k < n; k++)
			{
				float ax, ay, bx, by;
				at (px[k], pz[k], cx, cy, r, &ax, &ay); at (px[(k + 1) % n], pz[(k + 1) % n], cx, cy, r, &bx, &by);
				S.line (ax, ay, bx, by, pass ? r * 0.05f : r * 0.11f, pass ? C_WHITE : C_INK);
			}
		for (int pass = 0; pass < 2; pass++)
			for (int i = 0; i < R.n; i++)
			{
				bool big = i == me || R.c[i].human;
				if ((pass == 1) != big) continue;
				V3 p = R.T->pointAt (R.c[i].s, 0, 0);
				float x, y; at (p.x, p.z, cx, cy, r, &x, &y);
				float d = big ? r * 0.1f : r * 0.065f;
				S.disc (x, y, d + 2, C_INK);
				S.disc (x, y, d, PILOTS[R.c[i].pilot].color);
			}
	}
};

} // namespace cz

#endif
