// Cosmo Zoom! -- a circuit seen from above: the road coloured by its height (dark blue low .. cyan .. yellow ..
// magenta high), the upper branch drawn over the lower one where the layout crosses itself, the features marked
// (start: green bar, boost pads: cyan, capsule rows: yellow, jumps: red, loops: white, tubes: teal, twists: orange
// edges, tunnels: grey), the decor props as small squares. It also checks the layout and prints it: the length,
// the heights, the tightest turn, the steepest slope, the crossings (and how far above each other they pass).
//   trackmap [-r] track.trk out.ppm [t ...]     (-r: the Remix, driven the other way round; each t: the road's
//   centre, height and heading there -- for the decor)
// Build: g++ -std=gnu++17 -O2 -w -Iuser -Iuser/Libs -o /tmp/trackmap tools/tests/cosmozoom/trackmap.cpp
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "Apps/cosmozoom/track.h"
#include <stdio.h>
#include <math.h>
#include <algorithm>
#include <vector>
using namespace cz;

static Track T, T0;
static const int W = 720, H = 720;
static unsigned px[W * H];

// a stretch's width as it lies on the ground: a tube's is its diameter, not its round
static float footW (const Seg &S) { return S.tube > 0.5f ? 2 * TUBE_R : S.w; }
static void disc (float cx, float cy, float r, unsigned c)
{
	int x0 = (int) floorf (cx - r), x1 = (int) ceilf (cx + r), y0 = (int) floorf (cy - r), y1 = (int) ceilf (cy + r);
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++)
		{
			if (x < 0 || y < 0 || x >= W || y >= H) continue;
			float dx = (float) x + 0.5f - cx, dy = (float) y + 0.5f - cy;
			if (dx * dx + dy * dy <= r * r) px[y * W + x] = c;
		}
}
static unsigned mix (unsigned a, unsigned b, float t)
{
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	int r = (int) ((float) ((a >> 16) & 255) * (1 - t) + (float) ((b >> 16) & 255) * t);
	int g = (int) ((float) ((a >> 8) & 255) * (1 - t) + (float) ((b >> 8) & 255) * t);
	int bl = (int) ((float) (a & 255) * (1 - t) + (float) (b & 255) * t);
	return (unsigned) (r << 16 | g << 8 | bl);
}
static unsigned heightCol (float f)
{
	static const unsigned R[] = { 0x1A2A8C, 0x22B6FF, 0x7CFF6A, 0xFFE14D, 0xFF2BD6 };
	f = f < 0 ? 0 : f > 1 ? 1 : f;
	float x = f * 4; int i = (int) x; if (i > 3) i = 3;
	return mix (R[i], R[i + 1], x - (float) i);
}

int main (int argc, char **argv)
{
	bool rev = argc > 1 && !strcmp (argv[1], "-r");	// the Remix: driven the other way round
	if (rev) { argv++; argc--; }
	if (argc < 3) { fprintf (stderr, "trackmap [-r] track.trk out.ppm [t ...]\n"); return 1; }
	if (!T.load (argv[1], false, rev)) { fprintf (stderr, "no track %s\n", argv[1]); return 1; }
	const int n = T.n;
	// ---- the checks
	float maxc = 0; int maxci = 0;
	for (int i = 0; i < n; i++) if (!(T.seg[i].flags & F_LOOP) && fabsf (T.seg[i].curv) > maxc) { maxc = fabsf (T.seg[i].curv); maxci = i; }
	float maxGrade = 0; int maxGi = 0;
	for (int i = 0; i < n; i++)
	{
		if (T.seg[i].flags & F_LOOP) continue;
		int j = (i + 10) % n; if (T.seg[j].flags & F_LOOP) continue;
		float g = fabsf (T.seg[j].c.y - T.seg[i].c.y) / (10 * SEG);
		if (g > maxGrade) { maxGrade = g; maxGi = i; }
	}
	printf ("%s (%s): %.0f m, %d segments, %d points; height %.1f .. %.1f m; tightest turn r %.0f m at t %.3f; steepest %.0f %% at t %.3f\n",
		T.name, T.nameFr, T.length, n, T.ncp, T.lo.y, T.hi.y, maxc > 0 ? 1 / maxc : 0, (float) maxci / n, maxGrade * 100, (float) maxGi / n);
	// the control points' spacing
	float dmin = 1e9f, dmax = 0;
	for (int i = 0; i < T.ncp; i++) { V3 d = T.cp[(i + 1) % T.ncp] - T.cp[i]; float l = len (d); dmin = fminf (dmin, l); dmax = fmaxf (dmax, l); }
	// the control points' t, as the features count it (the lap without its loops)
	{
		char *txt = readFile (argv[1]);
		T0.parse (txt); free (txt);
		for (int k = 0; k < T0.nfeat; k++) if (T0.feat[k].kind == F_LOOP) T0.feat[k].kind = F_ITEMS, T0.feat[k].t0 = -5;
		T0.build ();
	}
	printf ("  control points spaced %.0f .. %.0f m; their t (loops left out):", dmin, dmax);
	for (int k = 0; k < T0.ncp; k++)
	{
		float best = 1e18f; int bi = 0;
		for (int i = 0; i < T0.n; i++) { V3 d = T0.seg[i].c - T0.cp[k]; float q = dot (d, d); if (q < best) { best = q; bi = i; } }
		printf (" %d:%.3f", k, (float) bi / T0.n);
	}
	printf ("\n");
	// the crossings: two stretches far apart along the lap and close above the ground
	int nx = 0;
	std::vector<char> seen (n, 0);
	for (int i = 0; i < n; i += 1)
	{
		if (T.seg[i].flags & F_LOOP) continue;
		float best = 1e9f; int bj = -1; float bdy = 0;
		for (int j = 0; j < n; j++)
		{
			int dd = abs (i - j); if (dd > n / 2) dd = n - dd;
			if (dd < 40 || (T.seg[j].flags & F_LOOP)) continue;
			V3 d = T.seg[j].c - T.seg[i].c;
			float hd = sqrtf (d.x * d.x + d.z * d.z);
			float need = (footW (T.seg[i]) + footW (T.seg[j])) * 0.5f + 2;
			if (hd < need && hd < best) { best = hd; bj = j; bdy = d.y; }
		}
		if (bj >= 0 && !seen[i])
		{
			// a cluster: the smallest height difference over it
			float mdy = fabsf (bdy); int k = i;
			while (k < n)
			{
				bool any = false;
				for (int j = 0; j < n; j++)
				{
					int dd = abs (k - j); if (dd > n / 2) dd = n - dd;
					if (dd < 40) continue;
					V3 d = T.seg[j].c - T.seg[k].c;
					if (sqrtf (d.x * d.x + d.z * d.z) < (footW (T.seg[k]) + footW (T.seg[j])) * 0.5f + 2) { any = true; mdy = fminf (mdy, fabsf (d.y)); }
				}
				if (!any) break;
				seen[k] = 1; k++;
			}
			if (i < bj) { printf ("  crossing: t %.3f and t %.3f, %.1f m apart in height%s\n", (float) i / n, (float) bj / n, mdy, mdy < 10 ? "  <-- TOO CLOSE" : ""); nx++; }
		}
	}
	if (!nx) printf ("  no crossing\n");
	// the pylons under a raised road (world.h: every 14th segment, down to the ground): one standing on a road
	// that passes below?
	if (strcmp (T.groundKind, "none"))
		for (int i = 0; i < n; i += 14)
		{
			const Seg &A = T.seg[i];
			if (A.flags & (F_LOOP | F_TWIST)) continue;
			V3 top = A.c - A.u * (A.tube > 0.5f ? 2 * TUBE_R : 1.6f);
			if (top.y - T.groundY < 3.0f) continue;
			for (int j = 0; j < n; j++)
			{
				int dd = abs (i - j); if (dd > n / 2) dd = n - dd;
				if (dd < 10 || T.seg[j].c.y > top.y - 2) continue;
				V3 d = T.seg[j].c - top;
				if (sqrtf (d.x * d.x + d.z * d.z) < footW (T.seg[j]) * 0.5f + 1.5f)
				{ printf ("  pylon at t %.3f stands on the road at t %.3f  <-- IN THE WAY\n", (float) i / n, (float) j / n); break; }
			}
		}
	for (int k = 3; k < argc; k++)
	{
		float t = (float) atof (argv[k]);
		int i = ((int) (t * n) % n + n) % n;
		const Seg &S = T.seg[i];
		printf ("  at t %.3f: x %.1f z %.1f y %.1f, heading (yaw) %.0f deg, width %.0f\n", t, S.c.x, S.c.z, S.c.y, atan2f (S.f.x, S.f.z) * 180 / PI, S.w);
	}
	// ---- the picture
	for (int i = 0; i < W * H; i++) px[i] = 0x0C1030;
	for (int y = 0; y < H; y += 40) for (int x = 0; x < W; x++) px[y * W + x] = 0x161C48;
	for (int x = 0; x < W; x += 40) for (int y = 0; y < H; y++) px[y * W + x] = 0x161C48;
	float sx = T.hi.x - T.lo.x, sz = T.hi.z - T.lo.z;
	float span = fmaxf (sx, sz) + 60, sc = (float) (W - 40) / span;
	float ox = (T.lo.x + T.hi.x) * 0.5f, oz = (T.lo.z + T.hi.z) * 0.5f;
	auto X = [&] (const V3 &p) { return (float) W * 0.5f + (p.x - ox) * sc; };
	auto Y = [&] (const V3 &p) { return (float) H * 0.5f + (p.z - oz) * sc; };	// seen from above: z down the picture
	// 20 m grid ticks: a scale bar of 100 m
	for (int x = 20; x < 20 + (int) (100 * sc); x++) for (int y = H - 22; y < H - 18; y++) px[y * W + x] = 0xFFFFFF;
	std::vector<int> ord (n);
	for (int i = 0; i < n; i++) ord[i] = i;
	std::sort (ord.begin (), ord.end (), [] (int a, int b) { return T.seg[a].c.y < T.seg[b].c.y; });
	float hr = T.hi.y - T.lo.y; if (hr < 1) hr = 1;
	for (int pass = 0; pass < 2; pass++)
		for (int q = 0; q < n; q++)
		{
			int i = ord[q];
			const Seg &S = T.seg[i];
			float w = S.tube > 0.5f ? 2 * TUBE_R : S.w;
			float r = fmaxf (1.5f, w * 0.5f * sc);
			float x = X (S.c), y = Y (S.c);
			if (pass == 0) { disc (x, y, r + 1.5f, 0x000000); continue; }
			unsigned c = heightCol ((S.c.y - T.lo.y) / hr);
			if (S.flags & F_TUNNEL) c = mix (c, 0x707070, 0.7f);
			if (S.flags & F_LOOP) c = 0xFFFFFF;
			disc (x, y, r, c);
			if (S.tube > 0.5f) disc (x, y, r * 0.55f, (i / 4) & 1 ? 0x1FB5A8 : 0xFF2BD6);
			if (S.flags & F_TWIST) { V3 e1 = S.c + S.r * (S.w * 0.5f), e2 = S.c - S.r * (S.w * 0.5f); disc (X (e1), Y (e1), 1.6f, 0xFF8A1F); disc (X (e2), Y (e2), 1.6f, 0xFF8A1F); }
		}
	for (int i = 0; i < n; i++)
	{
		const Seg &S = T.seg[i];
		float x = X (S.c), y = Y (S.c);
		if (S.flags & F_BOOST) { V3 p = S.c + S.r * (S.lane * S.w * 0.5f); disc (X (p), Y (p), 4, 0x00FFFF); }
		if (S.flags & F_ITEMS) for (int k = -2; k <= 2; k++) { V3 p = S.c + S.r * ((float) k * S.w * 0.2f); disc (X (p), Y (p), 2.5f, 0xFFE14D); }
		if (S.flags & F_RAMP) disc (x, y, 6, 0xFF3030);
		if (S.flags & F_GAP) disc (x, y, 3, 0x000000);
		if (S.flags & F_START)
			for (int k = -10; k <= 10; k++) { V3 p = S.c + S.r * ((float) k * S.w * 0.05f); disc (X (p), Y (p), 2.5f, 0x30FF60); }
	}
	// the way round: arrows every 10 % of the lap
	for (int k = 0; k < 10; k++)
	{
		const Seg &S = T.seg[k * n / 10];
		V3 a = S.c - S.f * 12, b = S.c + S.f * 12;
		for (int j = 0; j <= 12; j++) { V3 p = lerp (a, b, (float) j / 12); disc (X (p), Y (p), 1.2f + (float) j * 0.18f, 0xFFFFFF); }
	}
	for (int k = 0; k < T.ndecor; k++)
	{
		V3 p (T.decor[k].x, 0, T.decor[k].z);
		float x = X (p), y = Y (p);
		for (int a = -3; a <= 3; a++) for (int b = -3; b <= 3; b++) { int xx = (int) x + a, yy = (int) y + b; if (xx >= 0 && yy >= 0 && xx < W && yy < H) px[yy * W + xx] = (abs (a) == 3 || abs (b) == 3) ? 0x000000 : 0xFFFFFF; }
	}
	FILE *f = fopen (argv[2], "wb");
	if (!f) return 1;
	fprintf (f, "P6 %d %d 255\n", W, H);
	for (int i = 0; i < W * H; i++) { unsigned char c[3] = { (unsigned char) (px[i] >> 16), (unsigned char) (px[i] >> 8), (unsigned char) px[i] }; fwrite (c, 1, 3, f); }
	fclose (f);
	return 0;
}
