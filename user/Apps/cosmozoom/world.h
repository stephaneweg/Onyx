//
// world.h -- a circuit's scenery, made once when it is loaded, in the soft rounded low-poly look of the user's
// reference (docs/cosmozoom/refs/art-direction.png): the road (a thick floating slab with rounded edges, striped round
// rails, the tunnels, the tubes, the gaps), the start gate, the city around it (rounded capsule towers in pastel
// colours, signs), floating islands with round trees and rocks, cloud puffs, the ground (a sea of clouds, the sea,
// land) and the sky (day, dusk, night, aurora, deep space: the stars, the nebulae, the planets, the clouds) -- cut into pieces of CHUNK segments, each with a sphere around it: only the pieces in front of
// the camera are sent to the GPU. What moves (the speed pads, the capsules, the traffic) is drawn each picture by the
// scene.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef CZ_WORLD_H
#define CZ_WORLD_H

#include "track.h"
#include "gfx.h"
#include "textures.h"

namespace cz {

// The models, by name (SD:/apps/cosmozoom.app/models/<name>.obj): read once; a variant per colouring (paint1 /
// paint2: a building's walls and trims) lit and outlined when first asked.
struct ModelLib
{
	enum { MAX = 96, MAXV = 160 };
	char name[MAX][24]; Mesh *raw[MAX]; int n = 0;
	struct Variant { int model; unsigned c1, c2; bool outline; Mesh *m; } var[MAXV]; int nvar = 0;
	char dir[160] = "";
	bool outline = false;			// (the soft look: no black outline; the ships' parts are taken raw by buildShip)
	const Mesh *rawMesh (const char *nm)
	{
		for (int i = 0; i < n; i++) if (!strcmp (name[i], nm)) return raw[i];
		if (n >= MAX) return 0;
		Mesh *m = new Mesh;
		char path[256]; snprintf (path, sizeof path, "%s/%s.obj", dir, nm);
		if (!loadObj (path, *m)) { delete m; m = 0; }
		snprintf (name[n], sizeof name[n], "%s", nm); raw[n] = m; n++;
		return m;
	}
	int index (const char *nm) { rawMesh (nm); for (int i = 0; i < n; i++) if (!strcmp (name[i], nm)) return i; return -1; }
	// the model lit, painted c1 / c2 (0xFF000000: its own colours), outlined or not
	const Mesh *get (const char *nm, unsigned c1 = 0xFF000000u, unsigned c2 = 0xFF000000u, int withOutline = -1)
	{
		bool ol = withOutline < 0 ? outline : withOutline != 0;
		int mi = index (nm);
		if (mi < 0 || !raw[mi]) return 0;
		for (int i = 0; i < nvar; i++) if (var[i].model == mi && var[i].c1 == c1 && var[i].c2 == c2 && var[i].outline == ol) return var[i].m;
		Mesh *m = new Mesh;
		appendMesh (*m, *raw[mi], M4::identity ());
		for (int i = 0; i < m->tris.n; i++)
		{
			Tri &T = m->tris[i];
			unsigned c = T.paint == 1 ? c1 : T.paint == 2 ? c2 : 0xFF000000u;
			if (c != 0xFF000000u) { T.base = c; for (int k = 0; k < 3; k++) T.col[k] = c; }
		}
		lightMesh (*m);
		if (ol) { float sz = len (m->hi - m->lo); addOutline (*m, clampf (sz * 0.010f, 0.03f, 0.22f)); }
		if (nvar < MAXV) { var[nvar++] = Variant { mi, c1, c2, ol, m }; }
		return m;
	}
};

struct World
{
	enum { CHUNK = 12, MAXCH = Track::MAXSEG / CHUNK + 4 };
	VList chunk[MAXCH]; int nch = 0;	// the solid pieces
	VList chunkLow[MAXCH];			// the same, simplified: drawn far away (the road a strip, the props boxes)
	VList glow[MAXCH];			// what glows: drawn after, added (no depth written)
	VList clear[MAXCH];			// what is see-through: the force fields (alpha, no depth written)
	VList signs[MAXCH];			// the billboards' pictures (added, the TX_SIGNS texture)
	VList far, farGlow;			// the ground, the far city, the hills (always drawn)
	VList sky, skyGlow, skyObj;		// around the camera (drawn without its position): the dome; the stars, the suns (TX_GLOW); the planets
	VList skyBand;				// the skybox's clouds: a band textured with TX_CLOUDS round the horizon
	float glowK = 1;			// the light streaks' strength (1 at night .. 0.5 by day)
	unsigned fogCol = 0;
	struct Traffic { V3 c; float r, h, speed, ph; unsigned col; } traffic[16]; int ntraffic = 0;

	static unsigned shade (unsigned c, const V3 &n) { return litColor (c, norm (n), sunDir ()); }

	void build (const Track &T, ModelLib &lib, unsigned seed)
	{
		for (int i = 0; i < MAXCH; i++) { chunk[i].v.clear (); chunkLow[i].v.clear (); glow[i].v.clear (); clear[i].v.clear (); signs[i].v.clear (); }
		far.v.clear (); farGlow.v.clear (); sky.v.clear (); skyGlow.v.clear (); skyObj.v.clear (); skyBand.v.clear (); ntraffic = 0;
		nch = (T.n + CHUNK - 1) / CHUNK;
		if (nch > MAXCH) nch = MAXCH;
		glowK = T.timeOfDay == Track::T_DAY ? 0.5f : T.timeOfDay == Track::T_DUSK ? 0.7f : 1.0f;
		fogCol = T.sky[1];
		Rng rng (seed);
		for (int i = 0; i < T.n; i++) roadSeg (T, i, chunk[i / CHUNK], glow[i / CHUNK], clear[i / CHUNK]);
		startGate (T, lib);
		city (T, lib, rng);
		props (T, lib, rng);
		ground (T, lib, rng);
		makeSky (T, lib, rng);
		for (int i = 0; i < nch; i++)
		{
			chunk[i].bound (); chunkLow[i].bound ();
			VList *o[3] = { &glow[i], &clear[i], &signs[i] };
			for (VList *L : o) { L->bound (); if (chunk[i].radius > L->radius) { L->centre = chunk[i].centre; L->radius = chunk[i].radius; } }
		}
		far.bound (); farGlow.bound ();
	}

	// ---- the road (the soft low-poly look, docs/cosmozoom/refs/art-direction.png): a thick floating slab with
	// rounded edges, a smooth surface with faint lane marks, a striped round rail along each edge; rolled into a tube
	// where it is one; no pylons -- the track floats ----------------------------------------------------------------------
	void roadSeg (const Track &T, int i, VList &L, VList &G, VList &C)
	{
		(void) C;
		const Seg &A = T.seg[i], &B = T.seg[(i + 1) % T.n];
		if ((A.flags & F_GAP) && !(B.flags & F_GAP)) { gapEdge (T, B, L); }
		if (A.flags & F_GAP) { if (!(T.seg[(i + T.n - 1) % T.n].flags & F_GAP)) gapEdge (T, A, L); return; }
		float tube = fmaxf (A.tube, B.tube);
		bool closed = A.tube > 0.985f && B.tube > 0.985f;
		roadLow (T, i, A, B, tube, chunkLow[i / CHUNK]);
		bool tun = (A.flags & F_TUNNEL) != 0 && tube <= 0;
		bool start = (A.flags & F_START) || i == T.n - 1;
		auto SP = [&] (const Seg &S, float l, float h, V3 *n = 0, V3 *lat = 0) { V3 p; Track::surfOf (S.c, S.r, S.u, S.tube, l, h, &p, n, lat); return p; };
		// the user's road profile (2026-10-15, docs/cosmozoom/DESIGN.md: drawn at 150 mm for 15 m): three lanes split by
		// small rounded grooves, a raised rounded border each side, 0.5 m thick; the lanes stretch with the width, the
		// borders keep their shape. Tubes keep the plain rolled surface below.
		if (tube <= 0.01f && (A.fork > 0 || B.fork > 0))
		{
			forkRoad (T, i, A, B, start, L, G);
		}
		else if (tube <= 0.01f)
		{
			profileRoad (T, i, A, B, start, L);
		}
		else
		{
			// the surface
			int cols = 4 + (int) (tube * 20 + 0.5f);
			float ea = A.w * 0.5f, eb = B.w * 0.5f;
			unsigned band = T.road[0];		// (one colour all along: no darker stretches)
			for (int k = 0; k < cols; k++)
			{
				float f0 = -1 + 2.0f * (float) k / (float) cols, f1 = -1 + 2.0f * (float) (k + 1) / (float) cols;
				V3 a0 = SP (A, f0 * ea, 0), a1 = SP (A, f1 * ea, 0), b0 = SP (B, f0 * eb, 0), b1 = SP (B, f1 * eb, 0);
				V3 nm = SP (A, (f0 + f1) * 0.5f * ea, 0) - SP (A, (f0 + f1) * 0.5f * ea, -1);
				unsigned c = band;
				if (tube > 0.4f) c = (k % 5 == 2) ? rgbMix (T.road[0], 0xFFFFFF, 0.25f) : T.road[0];	// (a tube: light stripes along it)
				if (start) c = ((k + (i == T.n - 1 ? 1 : 0)) & 1) ? 0xFFFFFF : 0x2A3350;
				L.face (a0, a1, b1, b0, shade (c, nm), nm);
				if (!closed)
				{
					V3 u0 = SP (A, f0 * ea, -1.1f), u1 = SP (A, f1 * ea, -1.1f), v0 = SP (B, f0 * eb, -1.1f), v1 = SP (B, f1 * eb, -1.1f);
					L.face (u0, u1, v1, v0, shade (rgbScale (T.road[0], 0.7f), -nm), -nm);
				}
			}
			// the lane grooves: thin darker lines, continuous
			if (!start && tube <= 0.4f)
				for (int k = -1; k <= 1; k += 2)
				{
					float l = (float) k * A.w / 6, lb = (float) k * B.w / 6, wl = 0.07f;
					L.face (SP (A, l - wl, 0.015f), SP (A, l + wl, 0.015f), SP (B, lb + wl, 0.015f), SP (B, lb - wl, 0.015f), shade (rgbScale (band, 0.84f), A.u), A.u);
				}
			// a speed pad's darker panel under its glowing chevrons
			if ((A.flags & F_BOOST) && tube <= 0.4f)
			{
				float la = A.lane * A.w * 0.5f, lb = A.lane * B.w * 0.5f, hw = 2.3f;
				L.face (SP (A, la - hw, 0.02f), SP (A, la + hw, 0.02f), SP (B, lb + hw, 0.02f), SP (B, lb - hw, 0.02f), shade (rgbScale (T.road[0], 0.72f), A.u), A.u);
			}
			// a tube's white rings every 20 m
			if (tube > 0.4f && (i % 10) == 0)
				for (int k = 0; k < cols; k++)
				{
					float f0 = -1 + 2.0f * (float) k / (float) cols, f1 = -1 + 2.0f * (float) (k + 1) / (float) cols;
					V3 nm = SP (A, (f0 + f1) * 0.5f * ea, 0) - SP (A, (f0 + f1) * 0.5f * ea, -1);
					L.face (SP (A, f0 * ea, 0.03f), SP (A, f1 * ea, 0.03f), SP (B, f1 * eb, 0.03f) * 0.4f + SP (A, f1 * ea, 0.03f) * 0.6f, SP (B, f0 * eb, 0.03f) * 0.4f + SP (A, f0 * ea, 0.03f) * 0.6f, shade (0xF4F6FF, nm), nm);
				}
			if (closed) return;
			// the edges: rounded down to the underside, a striped round rail on top
			for (int side = -1; side <= 1; side += 2)
			{
				float s = (float) side;
				V3 nA, latA, nB, latB;
				V3 ao = SP (A, s * ea, 0, &nA, &latA), bo = SP (B, s * eb, 0, &nB, &latB);
				V3 outA = latA * s, outB = latB * s;
				// the slab's rounded side: a half round from the top edge down to the underside
				const int R = 4;
				unsigned sc = rgbScale (T.road[0], 0.82f);
				for (int k = 0; k < R; k++)
				{
					float t0 = PI * (float) k / R, t1 = PI * (float) (k + 1) / R;
					auto E = [&] (const V3 &o, const V3 &n, const V3 &out, float t) { return o - n * (0.55f - 0.55f * cosf (t)) + out * (0.4f * sinf (t)); };
					V3 p0 = E (ao, nA, outA, t0), p1 = E (ao, nA, outA, t1), q0 = E (bo, nB, outB, t0), q1 = E (bo, nB, outB, t1);
					V3 fn = outA * sinf ((t0 + t1) * 0.5f) + nA * cosf ((t0 + t1) * 0.5f);
					L.face (p0, q0, q1, p1, shade (sc, fn), fn);
				}
				// the kerb: a raised rounded lip along the edge, inside the road's width (lighter on top)
				{
					static const float KP[][2] = { { -1.05f, 0.0f }, { -0.80f, 0.20f }, { -0.50f, 0.38f }, { -0.18f, 0.47f }, { 0.0f, 0.40f }, { 0.04f, 0.0f } };
					const int NK = 6;
					unsigned kc = rgbMix (T.road[0], 0xFFFFFF, 0.12f);
					for (int k = 0; k + 1 < NK; k++)
					{
						auto KPt = [&] (const Seg &S, float e, const V3 &n, const V3 &out, int j) { V3 o = SP (S, s * e, 0); return o + out * KP[j][0] + n * KP[j][1]; };
						V3 p0 = KPt (A, ea, nA, outA, k), p1 = KPt (A, ea, nA, outA, k + 1), q0 = KPt (B, eb, nB, outB, k), q1 = KPt (B, eb, nB, outB, k + 1);
						V3 fn = nA * (KP[k + 1][0] - KP[k][0]) - outA * (KP[k + 1][1] - KP[k][1]);
						if (dot (fn, nA) < 0) fn = -fn;
						L.face (p0, q0, q1, p1, shade (k < 2 ? rgbScale (kc, 0.94f) : kc, fn), fn);
					}
				}
				// the rail: an 8-sided round bar on the kerb, its stripes in turn (2 m each)
				unsigned rc = T.rails[(i & 1)];
				const int K = 8;
				float rr = 0.36f;
				V3 cA = SP (A, s * (ea - 0.3f), 0.45f + rr * 0.85f), cB = SP (B, s * (eb - 0.3f), 0.45f + rr * 0.85f);
				for (int k = 0; k < K; k++)
				{
					float t0 = 2 * PI * (float) k / K, t1 = 2 * PI * (float) (k + 1) / K;
					V3 d0A = nA * cosf (t0) + outA * sinf (t0), d1A = nA * cosf (t1) + outA * sinf (t1);
					V3 d0B = nB * cosf (t0) + outB * sinf (t0), d1B = nB * cosf (t1) + outB * sinf (t1);
					V3 fn = (d0A + d1A) * 0.5f;
					L.face (cA + d0A * rr, cB + d0B * rr, cB + d1B * rr, cA + d1A * rr, shade (rc, fn), fn);
				}

			}
		}
		// the tunnel: the user's profile (2026-10-15, refs/road_profile/TunnelProfile.obj, drawn for a 15 m road): a
		// round tube hugging the road, 7.5 m inside, its wall 1.5 m, its centre 1.8 m above the lanes; scaled with the
		// road's width. Outside, a raised lighter ring every 6 m (the ribbed look); inside, darker at the crown, lights
		if (tun)
		{
			const int K = 24;
			float sa = A.w / 15.0f, sb = B.w / 15.0f;
			bool rib = (i % 3) == 0, rib2 = (i % 6) == 0;		// (a rib every 12 m)
			auto TP = [&] (const Seg &S, float sc, float a, float rad)
			{
				V3 p; Track::surfOf (S.c, S.r, S.u, 0, cosf (a) * rad * sc, 1.8f * sc + sinf (a) * rad * sc, &p, 0, 0); return p;
			};
			unsigned wall = T.walls[0], ribc = rgbMix (T.walls[0], 0xFFFFFF, 0.3f);
			for (int k = 0; k < K; k++)
			{
				float a0 = 2 * PI * (float) k / K, a1 = 2 * PI * (float) (k + 1) / K, am = (a0 + a1) * 0.5f;
				V3 out = A.r * cosf (am) + A.u * sinf (am);
				// inside: darker toward the crown
				unsigned ci = rgbScale (rib ? rgbMix (wall, 0xFFFFFF, 0.18f) : wall, 1.0f - 0.3f * fmaxf (0.0f, sinf (am)));	// (the rings show inside too)
				L.face (TP (A, sa, a0, 7.5f), TP (B, sb, a0, 7.5f), TP (B, sb, a1, 7.5f), TP (A, sa, a1, 7.5f), shade (ci, -out), -out);
				// outside: a rib every 12 m, its sides chamfered at 45 degrees
				auto TL = [&] (float t, float a, float rad) { return lerp (TP (A, sa, a, rad), TP (B, sb, a, rad), t); };
				if (rib2)
				{
					static const float RT[4] = { 0.0f, 0.15f, 0.85f, 1.0f }, RR[4] = { 9.0f, 9.4f, 9.4f, 9.0f };
					for (int j = 0; j < 3; j++)
					{
						V3 nj = out + A.f * (j == 0 ? -1.0f : j == 2 ? 1.0f : 0.0f);
						L.face (TL (RT[j], a0, RR[j]), TL (RT[j + 1], a0, RR[j + 1]), TL (RT[j + 1], a1, RR[j + 1]), TL (RT[j], a1, RR[j]), shade (j == 1 ? ribc : rgbMix (ribc, wall, 0.4f), nj), nj);
					}
				}
				else L.face (TP (A, sa, a0, 9.0f), TP (B, sb, a0, 9.0f), TP (B, sb, a1, 9.0f), TP (A, sa, a1, 9.0f), shade (wall, out), out);
			}
			// the tube's mouths: the wall's ring at its two ends, its outer edge chamfered
			bool first = !(T.seg[(i + T.n - 1) % T.n].flags & F_TUNNEL), last = !(B.flags & F_TUNNEL) || !(T.seg[(i + 2) % T.n].flags & F_TUNNEL);
			for (int e = 0; e < 2; e++)
			{
				if (!(e == 0 ? first : last)) continue;
				float te = e == 0 ? 0.0f : 1.0f, ti = e == 0 ? 0.15f : 0.85f;
				V3 n = e == 0 ? -A.f : B.f;
				auto TL = [&] (float t, float a, float rad) { return lerp (TP (A, sa, a, rad), TP (B, sb, a, rad), t); };
				for (int k = 0; k < K; k++)
				{
					float a0 = 2 * PI * (float) k / K, a1 = 2 * PI * (float) (k + 1) / K, am = (a0 + a1) * 0.5f;
					V3 out = A.r * cosf (am) + A.u * sinf (am);
					L.face (TL (te, a0, 7.5f), TL (te, a1, 7.5f), TL (te, a1, 8.7f), TL (te, a0, 8.7f), shade (ribc, n), n);
					L.face (TL (te, a0, 8.7f), TL (te, a1, 8.7f), TL (ti, a1, 9.0f), TL (ti, a0, 9.0f), shade (ribc, n + out), n + out);
				}
			}
			// a light under the crown every 6 m
			if (rib)
			{
				V3 c = TP (A, sa, PI / 2, 7.45f);
				V3 dx = A.r * 1.4f, dz = A.f * 0.6f;
				G.face (c - dx - dz, c + dx - dz, c + dx + dz, c - dx + dz, 0xFFF2C0, -A.u);
			}
		}
	}
	// the road profile, in the drawing's millimetres (x across, z up; the lanes' top at z 5): the top from the left
	// border's outside to the right one's, then the flat underside. kind: 0 lane, 1 groove, 2 border (striped), 3 side
	struct ProfPt { float x, z; unsigned char kind; };
	static const ProfPt *roadProfile (int *n)
	{
		static const ProfPt P[] = {
			{ -75.00f, 0.0f, 3 }, { -75.00f, 6.00f, 2 }, { -74.71f, 6.71f, 2 }, { -74.00f, 7.00f, 2 }, { -71.00f, 7.00f, 2 },
			{ -70.29f, 6.71f, 2 }, { -70.00f, 6.00f, 2 },
			{ -70.00f, 5.00f, 0 }, { -24.75f, 5.00f, 1 }, { -24.36f, 4.92f, 1 }, { -24.04f, 4.71f, 1 }, { -23.33f, 4.00f, 1 },
			{ -22.63f, 4.71f, 1 }, { -22.30f, 4.92f, 1 }, { -21.92f, 5.00f, 0 },
			{ 21.92f, 5.00f, 1 }, { 22.30f, 4.92f, 1 }, { 22.63f, 4.71f, 1 }, { 23.33f, 4.00f, 1 }, { 24.04f, 4.71f, 1 },
			{ 24.36f, 4.92f, 1 }, { 24.75f, 5.00f, 0 },
			{ 70.00f, 5.00f, 2 }, { 70.00f, 6.00f, 2 }, { 70.29f, 6.71f, 2 }, { 71.00f, 7.00f, 2 }, { 74.00f, 7.00f, 2 },
			{ 74.71f, 6.71f, 2 }, { 75.00f, 6.00f, 3 }, { 75.00f, 0.0f, 3 },
		};
		*n = (int) (sizeof P / sizeof P[0]);
		return P;
	}
	// (side, ca, cb: a fork's branch -- A and B its own, moved to its place -- cut at the centre line where the two
	// still overlap: side 1 the right one, nothing left of -ca / -cb; side -1 the left one)
	void profileRoad (const Track &T, int i, const Seg &A, const Seg &B, bool start, VList &L, int side = 0, float ca = 0, float cb = 0)
	{
		int n; const ProfPt *P = roadProfile (&n);
		auto SP = [&] (const Seg &S, float l, float h) { V3 p; Track::surfOf (S.c, S.r, S.u, S.tube, l, h, &p, 0, 0); return p; };
		float ha = A.w * 0.5f, hb = B.w * 0.5f;
		// a drawing point -> (across, above the lanes) in metres at this segment's half width
		auto Mc = [&] (const ProfPt &q, float half, float c, float *l, float *h)
		{
			float inner = half - 0.5f, ax = fabsf (q.x);
			float m = ax <= 70 ? ax / 70 * inner : inner + (ax - 70) * 0.1f;
			*l = q.x < 0 ? -m : m; *h = (q.z - 5) * 0.1f;
			if (side > 0 && *l < -c) *l = -c;
			if (side < 0 && *l > c) *l = c;
		};

		unsigned band = T.road[0];		// (one colour all along: no darker stretches)
		unsigned stripe = rgbScale (rgbVivid (T.rails[(i & 1)], 1.35f), 0.93f);	// (deeper than the files' pastels)
		for (int k = 0; k + 1 < n; k++)
		{
			float la0, ha0, la1, ha1, lb0, hb0, lb1, hb1;
			Mc (P[k], ha, ca, &la0, &ha0); Mc (P[k + 1], ha, ca, &la1, &ha1); Mc (P[k], hb, cb, &lb0, &hb0); Mc (P[k + 1], hb, cb, &lb1, &hb1);
			if (side && la0 == la1 && lb0 == lb1) continue;		// (cut away: the other branch's side)
			V3 a0 = SP (A, la0, ha0), a1 = SP (A, la1, ha1), b0 = SP (B, lb0, hb0), b1 = SP (B, lb1, hb1);
			// the outward normal: the profile's edge turned left (up for the top, out for the sides)
			float dl = la1 - la0, dh = ha1 - ha0;
			V3 nm = A.u * dl - A.r * dh, nmB = B.u * dl - B.r * dh;
			unsigned c;
			switch (P[k].kind)
			{
			case 0: c = start ? (((int) ((la0 + la1) * 0.5f + 20) + (i == T.n - 1 ? 1 : 0)) & 1 ? 0xFFFFFF : 0x2A3350) : band; break;
			case 1: c = rgbScale (band, 0.8f); break;
			case 2: c = stripe; break;
			default: c = rgbScale (T.road[0], 0.78f); break;
			}
			L.face2 (a0, a1, b1, b0, shade (c, nm), shade (c, nmB), nm);	// (lit at each end: no steps in the curves)
		}
		// the underside, flat
		{
			float l0, h0, l1, h1, m0, g0, m1, g1;
			Mc (P[0], ha, ca, &l0, &h0); Mc (P[n - 1], ha, ca, &l1, &h1); Mc (P[0], hb, cb, &m0, &g0); Mc (P[n - 1], hb, cb, &m1, &g1);
			V3 nm = -A.u;
			L.face (SP (A, l0, h0), SP (A, l1, h1), SP (B, m1, g1), SP (B, m0, g0), shade (rgbScale (T.road[0], 0.7f), nm), nm);
		}
		// a speed pad's darker panel under its glowing chevrons
		if ((A.flags & F_BOOST) && !side)
		{
			float la = A.lane * ha, lb = A.lane * hb, hw = 2.3f;
			L.face (SP (A, la - hw, 0.02f), SP (A, la + hw, 0.02f), SP (B, lb + hw, 0.02f), SP (B, lb - hw, 0.02f), shade (rgbScale (T.road[0], 0.72f), A.u), A.u);
		}
	}
	// ---- a fork: its two branches, each the road's own profile, cut at the centre line while they still overlap; the
	// speed pads on them; where they part (and meet again), a wedge between them in the rails' stripes, and at the split a
	// little sign with its arrows to the left and to the right. The gap past the wedge stays open.
	static Seg branchSeg (const Seg &S, int k)
	{
		Seg b = S;
		b.c = S.c + S.r * S.bl[k] + S.u * S.bh[k];
		return b;
	}
	static bool apart (const Seg &S) { return fabsf (S.bl[1] - S.bl[0]) > S.w || fabsf (S.bh[1] - S.bh[0]) > 2.5f; }
	// a point of a fork's segment: across l from the centre line, h above branch k
	static V3 forkPt (const Seg &S, float l, float h, int k)
	{
		V3 p; Track::surfOf (S.c + S.u * S.bh[k], S.r, S.u, 0, l, h, &p, 0, 0); return p;
	}
	void forkRoad (const Track &T, int i, const Seg &A, const Seg &B, bool start, VList &L, VList &G)
	{
		bool apA = apart (A), apB = apart (B);
		for (int k = 0; k < 2; k++)
		{
			// (still touching: each cut at the centre line, the right one -- the greater across -- on its right)
			const Seg &R = A.fork > 0 ? A : B;
			int side = R.bl[k] > R.bl[1 - k] ? 1 : R.bl[k] < R.bl[1 - k] ? -1 : k ? 1 : -1;
			profileRoad (T, i, branchSeg (A, k), branchSeg (B, k), start, L, side, apA ? 1e9f : fabsf (A.bl[k]), apB ? 1e9f : fabsf (B.bl[k]));
		}
		float sA = (float) i * SEG, sB = sA + SEG;
		auto near = [&] (const Seg &S, float l) { return fabsf (l - S.bl[1]) < fabsf (l - S.bl[0]) || (S.bl[0] == S.bl[1] && l >= 0) ? 1 : 0; };
		if (A.flags & F_BOOST)
		{
			float la = T.laneL (sA, A.lane), lb = T.laneL (sB, A.lane), hw = 2.3f;
			int k = near (A, la);
			L.face (forkPt (A, la - hw, 0.02f, k), forkPt (A, la + hw, 0.02f, k), forkPt (B, lb + hw, 0.02f, k), forkPt (B, lb - hw, 0.02f, k), shade (rgbScale (T.road[0], 0.72f), A.u), A.u);
		}
		// the wedge: from the nose (the gap opening) while the gap is under WG metres; at the meeting the same
		const float WG = 8, TOP = 0.9f, BOT = -0.5f;
		float gA = fabsf (A.bl[1] - A.bl[0]) - A.w, gB = fabsf (B.bl[1] - B.bl[0]) - B.w;
		bool level = fabsf (A.bh[1] - A.bh[0]) < 0.5f && fabsf (B.bh[1] - B.bh[0]) < 0.5f;
		if (level && (gA > 0 || gB > 0) && gA <= WG && gB <= WG)
		{
			float wa = fmaxf (gA, 0) * 0.5f, wb = fmaxf (gB, 0) * 0.5f;
			float ma = (A.bl[0] + A.bl[1]) * 0.5f, mb = (B.bl[0] + B.bl[1]) * 0.5f;
			int kl = A.bl[0] <= A.bl[1] ? 0 : 1, kr = 1 - kl;
			unsigned stripe = rgbScale (rgbVivid (T.rails[(i & 1)], 1.35f), 0.93f), side2 = rgbScale (stripe, 0.85f);
			V3 aL = forkPt (A, ma - wa, TOP, kl), aR = forkPt (A, ma + wa, TOP, kr), bL = forkPt (B, mb - wb, TOP, kl), bR = forkPt (B, mb + wb, TOP, kr);
			V3 aM = (aL + aR) * 0.5f + A.u * 0.15f, bM = (bL + bR) * 0.5f + B.u * 0.15f;	// (a low ridge on top)
			V3 nl = A.u + A.r * -0.5f, nr = A.u + A.r * 0.5f;
			L.face (aL, aM, bM, bL, shade (stripe, nl), nl);
			L.face (aM, aR, bR, bM, shade (rgbMix (stripe, 0xFFFFFF, 0.2f), nr), nr);
			// its sides, down to the slab's underside
			V3 aLb = forkPt (A, ma - wa, BOT, kl), aRb = forkPt (A, ma + wa, BOT, kr), bLb = forkPt (B, mb - wb, BOT, kl), bRb = forkPt (B, mb + wb, BOT, kr);
			L.face (aLb, aL, bL, bLb, shade (side2, -A.r), -A.r);
			L.face (aR, aRb, bRb, bR, shade (side2, A.r), A.r);
			// its end where the gap goes on opening (the void past it)
			const Seg &C = T.seg[(i + 2) % T.n], &Z = T.seg[(i + T.n - 1) % T.n];
			bool endB = fabsf (C.bl[1] - C.bl[0]) - C.w > WG || fabsf (C.bh[1] - C.bh[0]) >= 0.5f;
			bool endA = fabsf (Z.bl[1] - Z.bl[0]) - Z.w > WG || fabsf (Z.bh[1] - Z.bh[0]) >= 0.5f;
			if (endA || endB)
			{
				V3 n = endB ? B.f : -A.f;
				unsigned ec = shade (side2, n);
				if (endB) { L.face (bLb, bRb, bR, bL, ec, n); L.face (bL, bR, bM, bM, ec, n); }
				if (endA) { L.face (aLb, aRb, aR, aL, ec, -A.f); L.face (aL, aR, aM, aM, ec, -A.f); }
			}
			// the sign at the split: a panel on a post, its arrows pointing left and right
			if (gB > gA && gA < 3.0f && gB >= 3.0f) forkSign (T, B, mb, B.bh[kl], L, G);
		}
		// a crossing: the upper branch a thick deck where it passes over the lower one (its edges in the rails'
		// stripes), pillars at its edges clear of the lower road, a soft shadow on the lower road under it
		for (int k = 0; k < 2; k++)
		{
			int o = 1 - k;
			if (A.bh[k] < A.bh[o] + 2.5f || B.bh[k] < B.bh[o] + 2.5f) continue;
			float dA = fabsf (A.bl[k] - A.bl[o]), dB = fabsf (B.bl[k] - B.bl[o]);
			if (dA > A.w + 4 && dB > B.w + 4) continue;
			const float DK = -1.6f;
			float ea = A.w * 0.5f, eb = B.w * 0.5f;
			unsigned stripe = rgbScale (rgbVivid (T.rails[(i & 1)], 1.35f), 0.93f), under = rgbScale (T.road[0], 0.62f);
			for (int sd = -1; sd <= 1; sd += 2)
			{
				float s = (float) sd;
				V3 a0 = forkPt (A, A.bl[k] + s * ea, -0.5f, k), a1 = forkPt (A, A.bl[k] + s * ea, DK, k);
				V3 b0 = forkPt (B, B.bl[k] + s * eb, -0.5f, k), b1 = forkPt (B, B.bl[k] + s * eb, DK, k);
				L.face (a0, a1, b1, b0, shade (stripe, A.r * s), A.r * s);
			}
			L.face (forkPt (A, A.bl[k] - ea, DK, k), forkPt (A, A.bl[k] + ea, DK, k), forkPt (B, B.bl[k] + eb, DK, k), forkPt (B, B.bl[k] - eb, DK, k), shade (under, -A.u), -A.u);
			// the shadow below: where the deck overhangs the lower road
			float oa0 = fmaxf (A.bl[k] - ea, A.bl[o] - ea + 0.6f), oa1 = fminf (A.bl[k] + ea, A.bl[o] + ea - 0.6f);
			float ob0 = fmaxf (B.bl[k] - eb, B.bl[o] - eb + 0.6f), ob1 = fminf (B.bl[k] + eb, B.bl[o] + eb - 0.6f);
			if (oa1 > oa0 && ob1 > ob0)
				L.face (forkPt (A, oa0, 0.03f, o), forkPt (A, oa1, 0.03f, o), forkPt (B, ob1, 0.03f, o), forkPt (B, ob0, 0.03f, o), shade (rgbScale (T.road[0], 0.6f), A.u), A.u);
			// pillars every 10 m, down to the ground, where the lower road is not under them
			if ((i % 5) == 0)
				for (int sd = -1; sd <= 1; sd += 2)
				{
					float l = A.bl[k] + (float) sd * (ea - 0.7f);
					if (fabsf (l - A.bl[o]) < ea + 1.6f) continue;
					V3 top = forkPt (A, l, DK, k), bot = top; bot.y = T.groundY;
					box (L, top, bot, 0.45f, T.walls[0] ? T.walls[0] : 0xEEF0F8);
				}
		}
	}
	void forkSign (const Track &T, const Seg &S, float mid, float rise, VList &L, VList &G)
	{
		V3 base; Track::surfOf (S.c + S.u * rise, S.r, S.u, 0, mid, 0.9f, &base, 0, 0);
		V3 r = S.r, u = S.u, f = S.f;
		unsigned post = rgbMix (T.walls[0] ? T.walls[0] : 0xEEF0F8, 0xFFFFFF, 0.2f), panel = 0x2A3350;
		// the post
		V3 p0 = base - f * 0.2f, p1 = base + u * 2.4f - f * 0.2f;
		for (int k = 0; k < 4; k++)
		{
			float a0 = PI * 0.5f * (float) k, a1 = a0 + PI * 0.5f;
			V3 d0 = r * (cosf (a0) * 0.16f) + f * (sinf (a0) * 0.16f), d1 = r * (cosf (a1) * 0.16f) + f * (sinf (a1) * 0.16f), nm = d0 + d1;
			L.face (p0 + d0, p0 + d1, p1 + d1, p1 + d0, shade (post, nm), nm);
		}
		// the panel: 3.2 x 1.3 m, a rim in the rails' colour
		V3 c = base + u * 3.0f - f * 0.25f;
		float hw = 1.6f, hh = 0.65f, dz = 0.12f;
		V3 q[4] = { c - r * hw - u * hh, c + r * hw - u * hh, c + r * hw + u * hh, c - r * hw + u * hh };
		L.face (q[0] - f * dz, q[1] - f * dz, q[2] - f * dz, q[3] - f * dz, shade (panel, -f), -f);
		L.face (q[1] + f * dz, q[0] + f * dz, q[3] + f * dz, q[2] + f * dz, shade (panel, f), f);
		for (int k = 0; k < 4; k++)
		{
			V3 a = q[k], b = q[(k + 1) % 4], nm = (a + b) * 0.5f - c;
			L.face (a + f * dz, b + f * dz, b - f * dz, a - f * dz, shade (T.rails[0], nm), nm);
		}
		// the arrows, glowing: a chevron each way
		unsigned ac = rgbMix (T.rails[1] == 0xFFFFFF ? T.rails[0] : T.rails[1], 0xFFFFFF, 0.35f);
		for (int sd = -1; sd <= 1; sd += 2)
		{
			float s = (float) sd;
			V3 tip = c + r * (s * 1.4f) - f * (dz + 0.02f), m = c + r * (s * 0.85f) - f * (dz + 0.02f);
			V3 b0 = m + u * 0.5f, b1 = m - u * 0.5f, w = r * (s * -0.3f);
			G.face (b0, b0 + w, tip + w, tip, ac, -f);
			G.face (tip, tip + w, b1 + w, b1, ac, -f);
			// and a bar to it: an arrow
			V3 q0 = c + r * (s * 0.2f) - f * (dz + 0.02f);
			G.face (q0 + u * 0.1f, tip + w * 0.6f + u * 0.1f, tip + w * 0.6f - u * 0.1f, q0 - u * 0.1f, ac, -f);
		}
	}
	// the road far away (chunkLow): its top as one strip (a tube: 8 sides round), the borders' tops in their stripes, the
	// underside; a tunnel's tube as 8 sides
	void roadLow (const Track &T, int i, const Seg &A, const Seg &B, float tube, VList &L)
	{
		auto SP = [&] (const Seg &S, float l, float h) { V3 p; Track::surfOf (S.c, S.r, S.u, S.tube, l, h, &p, 0, 0); return p; };
		if (tube <= 0.01f && (A.fork > 0 || B.fork > 0))
		{
			// a fork: each branch a strip (cut at the centre line while they overlap), its borders, its underside
			unsigned st = rgbScale (rgbVivid (T.rails[(i & 1)], 1.35f), 0.93f);
			bool apA = apart (A), apB = apart (B);
			for (int k = 0; k < 2; k++)
			{
				const Seg &R = A.fork > 0 ? A : B;
				int sd = R.bl[k] > R.bl[1 - k] ? 1 : R.bl[k] < R.bl[1 - k] ? -1 : k ? 1 : -1;
				Seg a = branchSeg (A, k), b = branchSeg (B, k);
				float s = (float) sd, ea = A.w * 0.5f, eb = B.w * 0.5f;
				float ia = -s * (apA ? ea : fminf (ea, fabsf (A.bl[k]))), ib = -s * (apB ? eb : fminf (eb, fabsf (B.bl[k])));	// (the inner edges, local)
				float oa = s * ea, ob = s * eb;
				L.face (SP (a, ia, 0), SP (a, oa, 0), SP (b, ob, 0), SP (b, ib, 0), shade (T.road[0], A.u), A.u);
				L.face (SP (a, ia, -0.5f), SP (a, oa, -0.5f), SP (b, ob, -0.5f), SP (b, ib, -0.5f), shade (rgbScale (T.road[0], 0.7f), -A.u), -A.u);
				L.face (SP (a, s * (ea - 0.5f), 0.2f), SP (a, oa, 0.2f), SP (b, ob, 0.2f), SP (b, s * (eb - 0.5f), 0.2f), shade (st, A.u), A.u);
				if (apA && apB)
					L.face (SP (a, -s * (ea - 0.5f), 0.2f), SP (a, -s * ea, 0.2f), SP (b, -s * eb, 0.2f), SP (b, -s * (eb - 0.5f), 0.2f), shade (st, A.u), A.u);
			}
			return;
		}
		float ea = A.w * 0.5f, eb = B.w * 0.5f;
		int cols = tube > 0.01f ? 8 : 1;
		for (int k = 0; k < cols; k++)
		{
			float f0 = -1 + 2.0f * (float) k / (float) cols, f1 = -1 + 2.0f * (float) (k + 1) / (float) cols;
			V3 a0 = SP (A, f0 * ea, 0), a1 = SP (A, f1 * ea, 0), b0 = SP (B, f0 * eb, 0), b1 = SP (B, f1 * eb, 0);
			V3 nm = SP (A, (f0 + f1) * 0.5f * ea, 0) - SP (A, (f0 + f1) * 0.5f * ea, -1);
			L.face (a0, a1, b1, b0, shade (T.road[0], nm), nm);
			if (tube <= 0.985f) L.face (SP (A, f0 * ea, -0.5f), SP (A, f1 * ea, -0.5f), SP (B, f1 * eb, -0.5f), SP (B, f0 * eb, -0.5f), shade (rgbScale (T.road[0], 0.7f), -nm), -nm);
		}
		if (tube <= 0.01f)
			for (int sd = -1; sd <= 1; sd += 2)
			{
				float s0 = (float) sd;
				unsigned st = rgbScale (rgbVivid (T.rails[(i & 1)], 1.35f), 0.93f);
				L.face (SP (A, s0 * (ea - 0.5f), 0.2f), SP (A, s0 * ea, 0.2f), SP (B, s0 * eb, 0.2f), SP (B, s0 * (eb - 0.5f), 0.2f), shade (st, A.u), A.u);
			}
		if ((A.flags & F_TUNNEL) && tube <= 0)
		{
			const int K = 8; float sa = A.w / 15.0f, sb = B.w / 15.0f;
			for (int k = 0; k < K; k++)
			{
				float a0 = 2 * PI * (float) k / K, a1 = 2 * PI * (float) (k + 1) / K, am = (a0 + a1) * 0.5f;
				auto TP = [&] (const Seg &S, float sc, float a) { V3 p; Track::surfOf (S.c, S.r, S.u, 0, cosf (a) * 9.0f * sc, 1.8f * sc + sinf (a) * 9.0f * sc, &p, 0, 0); return p; };
				V3 out = A.r * cosf (am) + A.u * sinf (am);
				L.face (TP (A, sa, a0), TP (B, sb, a0), TP (B, sb, a1), TP (A, sa, a1), shade (T.walls[0], out), out);
			}
		}
	}
	// a model far away (chunkLow): its box -- turned and scaled as it is -- in its own average colours (the top, the
	// sides); the small ones (trees, lamps) left out
	struct LowCol { const Mesh *m; unsigned side, top; };
	LowCol lowCols[128]; int nLowCols = 0;
	void lowCols_of (const Mesh *m, unsigned *side, unsigned *top)
	{
		for (int i = 0; i < nLowCols; i++) if (lowCols[i].m == m) { *side = lowCols[i].side; *top = lowCols[i].top; return; }
		float sr = 0, sg = 0, sb = 0, sw = 0, tr = 0, tg = 0, tb = 0, tw = 0;
		for (int i = 0; i < m->tris.n; i++)
		{
			const Tri &T = m->tris[i];
			V3 n = cross (T.p[1] - T.p[0], T.p[2] - T.p[0]); float a = len (n); if (a <= 0) continue;
			unsigned c = T.col[0]; float r = (float) ((c >> 16) & 255), g = (float) ((c >> 8) & 255), b = (float) (c & 255);
			if (n.y / a > 0.6f) { tr += r * a; tg += g * a; tb += b * a; tw += a; } else { sr += r * a; sg += g * a; sb += b * a; sw += a; }
		}
		*side = sw > 0 ? (unsigned) (sr / sw) << 16 | (unsigned) (sg / sw) << 8 | (unsigned) (sb / sw) : 0xC0C0C0;
		*top = tw > 0 ? (unsigned) (tr / tw) << 16 | (unsigned) (tg / tw) << 8 | (unsigned) (tb / tw) : *side;
		if (nLowCols < 128) lowCols[nLowCols++] = { m, *side, *top };
	}
	void placeLow (const Mesh *m, const M4 &M, VList &L)
	{
		V3 lo = m->lo, hi = m->hi;
		V3 c[8];
		for (int k = 0; k < 8; k++) c[k] = M.point (V3 (k & 1 ? hi.x : lo.x, k & 2 ? hi.y : lo.y, k & 4 ? hi.z : lo.z));
		if (len (c[7] - c[0]) < 9) return;
		unsigned side, top; lowCols_of (m, &side, &top);
		// (its colours are lit already: the faces only darkened a little by their side)
		V3 up = c[2] - c[0], ex = c[1] - c[0], ez = c[4] - c[0];
		L.face (c[2], c[3], c[7], c[6], top, up);
		L.face (c[0], c[1], c[3], c[2], rgbScale (side, 0.92f), -ez);
		L.face (c[4], c[5], c[7], c[6], rgbScale (side, 1.0f), ez);
		L.face (c[0], c[4], c[6], c[2], rgbScale (side, 0.85f), -ex);
		L.face (c[1], c[5], c[7], c[3], rgbScale (side, 0.97f), ex);
	}
	// the end of the road before / after a gap: a striped lip, glowing
	void gapEdge (const Track &T, const Seg &S, VList &L)
	{
		float h = S.w * 0.5f;
		V3 a = S.c - S.r * h, b = S.c + S.r * h;
		for (int k = 0; k < 8; k++)
		{
			V3 p0 = lerp (a, b, (float) k / 8), p1 = lerp (a, b, (float) (k + 1) / 8);
			unsigned c = (k & 1) ? T.rails[0] : 0xFFFFFF;
			L.face (p0, p1, p1 - S.u * 1.2f, p0 - S.u * 1.2f, c, S.f);
			L.face (p0, p1, p1 - S.u * 1.2f, p0 - S.u * 1.2f, c, -S.f);
		}
	}
	// a glowing ring around a vertical axis
	static void ring (VList &G, const V3 &c, float r, unsigned col)
	{
		const int K = 12;
		for (int k = 0; k < K; k++)
		{
			float a0 = 2 * PI * k / K, a1 = 2 * PI * (k + 1) / K;
			V3 p0 = c + V3 (cosf (a0) * r, 0, sinf (a0) * r), p1 = c + V3 (cosf (a1) * r, 0, sinf (a1) * r);
			V3 o = (p0 + p1) * 0.5f - c;
			G.face (p0, p1, p1 + V3 (0, 0.4f, 0), p0 + V3 (0, 0.4f, 0), col, o);
		}
	}
	// a square column from top down to bottom (vertical), half side hs
	static void box (VList &L, const V3 &top, const V3 &bottom, float hs, unsigned c)
	{
		V3 ax[4] = { V3 (hs, 0, hs), V3 (-hs, 0, hs), V3 (-hs, 0, -hs), V3 (hs, 0, -hs) };
		for (int k = 0; k < 4; k++)
		{
			V3 a = ax[k], b = ax[(k + 1) % 4];
			V3 n = a + b;
			L.face (bottom + a, bottom + b, top + b, top + a, shade (c, n), n);
		}
	}

	// ---- placing a model: its solid triangles in the chunk, its glow in the glow list, a billboard's panel a sign ----
	int signCell = 0;
	void place (const Mesh *m, const M4 &M, int chunkIdx, unsigned signCol = 0)
	{
		if (!m) return;
		if (chunkIdx >= 0 && chunkIdx < nch) placeLow (m, M, chunkLow[chunkIdx]);
		VList &L = chunkIdx >= 0 && chunkIdx < nch ? chunk[chunkIdx] : far;
		VList &G = chunkIdx >= 0 && chunkIdx < nch ? glow[chunkIdx] : farGlow;
		VList &S = chunkIdx >= 0 && chunkIdx < nch ? signs[chunkIdx] : farGlow;
		int panel = m->group ("panel");
		int cell = signCell++ % 8;
		float cu = (float) (cell % 2) * 0.5f, cv = (float) (cell / 2) * 0.25f;
		L.v.reserve (L.v.n + m->tris.n * 3);
		for (int i = 0; i < m->tris.n; i++)
		{
			const Tri &T = m->tris[i];
			bool glowing = (T.flags & TRI_GLOW) != 0;
			bool sign = panel >= 0 && T.group == panel && chunkIdx >= 0 && signCol;
			VList &D = sign ? S : glowing ? G : L;
			for (int k = 0; k < 3; k++)
			{
				Vtx *p = D.v.push (); if (!p) return;
				unsigned c = T.col[k];
				if (sign) c = rgbMix (signCol, 0xFFFFFF, 0.25f);
				else if (glowing) c = rgbScale (c, glowK * 0.9f + 0.1f);
				setVtx (*p, M.point (T.p[k]), sign ? cu + clampf (T.s[k], 0, 1) * 0.5f : T.s[k], sign ? cv + clampf (T.t[k], 0, 1) * 0.25f : T.t[k], c, T.alpha);
			}
		}
	}
	static M4 at (const V3 &p, float yaw, float sx, float sy, float sz) { return M4::translate (p) * M4::rotY (yaw) * M4::scale (sx, sy, sz); }

	// is p clear of the road (the centre line farther than the half width + margin)? nearest: the closest segment
	static bool clearOfRoad (const Track &T, const V3 &p, float margin, int *nearest = 0, float *dist = 0)
	{
		float best = 1e18f; int bi = 0;
		for (int i = 0; i < T.n; i += 2)
		{
			V3 d = T.seg[i].c - p; d.y = 0;
			float q = dot (d, d);
			if (q < best) { best = q; bi = i; }
		}
		if (nearest) *nearest = bi;
		if (dist) *dist = sqrtf (best);
		float hw = T.seg[bi].w * 0.5f + T.seg[bi].fork + margin;	// (a fork: both its branches)
		return best > hw * hw;
	}

	void startGate (const Track &T, ModelLib &lib)
	{
		const Seg &S = T.seg[1];
		const Mesh *g = lib.get ("start_gate", T.walls[0] == 0 ? 0xFF000000u : rgbMix (T.walls[0], 0xFFFFFF, 0.15f), T.rails[0]);
		if (!g) return;
		float sc = (S.w + 2) / 16.0f;
		place (g, M4::frame (-S.r, S.u, S.f, S.c, sc, sc, sc), 0);
	}

	// ---- the city: rounded capsule towers in pastel colours rising from below, floating islands with trees and rocks,
	// cloud puffs, a few signs and lamps by the road, ships flying round ----------------------------------------------------
	static unsigned pastel (Rng &rng, const Track &T)
	{
		// (the user's reference: coral, lemon, mint, lilac, white, sky blue, apricot, pink -- saturated pastels)
		static const unsigned P[] = { 0xF0705A, 0xF5D04A, 0x98D98A, 0xA98BEA, 0xEEF0F8, 0x7CBDF0, 0xF79A5A, 0xF29BB8 };
		unsigned c = P[rng.n (8)];
		return T.walls[0] ? rgbMix (c, T.walls[0], 0.25f) : c;
	}
	void city (const Track &T, ModelLib &lib, Rng &rng)
	{
		static const char *TOWERS[] = { "tower_setback", "tower_chamfer", "tower_round", "tower_stepped", "tower_twin", "tower_slim" };
		float baseY = T.groundY;
		// the towers: rows each side, every ~35 m / density, 30..120 m from the road, standing on the ground below
		if (T.city > 0)
		{
			int step = (int) (36.0f / (SEG * clampf (T.city, 0.2f, 1.5f)));
			for (int side = -1; side <= 1; side += 2)
				for (int i = rng.n (step); i < T.n; i += step + rng.n (3))
				{
					const Seg &S = T.seg[i];
					if (S.flags & (F_LOOP | F_TWIST)) continue;
					for (int row = 0; row < 2; row++)
					{
						float d = S.w * 0.5f + S.fork + 30 + (float) row * 40 + rng.range (0, 25);
						V3 p = S.c + V3 (S.r.x, 0, S.r.z) * ((float) side * d);
						p.y = baseY;
						if (!clearOfRoad (T, p, 18)) continue;
						unsigned tc = pastel (rng, T);
						const Mesh *m = lib.get (TOWERS[rng.n (6)], tc, rgbMix (tc, 0xFFFFFF, 0.22f), 0);	// (its top and bands its own colour, a little lighter)
						float hs = rng.range (0.6f, 1.3f) * (row ? 1.2f : 1.0f), ws = rng.range (0.9f, 1.3f);
						place (m, at (p, (float) rng.n (4) * PI / 2 + atan2f (S.f.x, S.f.z), ws, hs, ws), i / CHUNK);
					}
				}
			// signs by the road, every ~140 m, the sides in turn
			int bstep = (int) (140 / SEG);
			for (int i = bstep / 2, k = 0; i < T.n; i += bstep, k++)
			{
				const Seg &S = T.seg[i];
				if (S.flags & (F_LOOP | F_TWIST | F_TUNNEL) || S.tube > 0) continue;
				float side = (k & 1) ? 1.0f : -1.0f;
				V3 p = S.c + V3 (S.r.x, 0, S.r.z) * (side * (S.w * 0.5f + S.fork + 8)) - V3 (0, 6, 0);
				if (!clearOfRoad (T, p, 4)) continue;
				V3 face = norm (V3 (-S.f.x, 0, -S.f.z) * 0.8f + V3 (-S.r.x, 0, -S.r.z) * side * 0.6f);
				const Mesh *m = lib.get ("billboard_pole", pastel (rng, T), T.neon[k % 3], 0);
				place (m, at (p, atan2f (face.x, face.z), 1, 1, 1), i / CHUNK, T.neon[k % 3]);
			}
			// the far ring: more towers (a real city only)
			if (T.city >= 0.5f)
			{
				float cx = (T.lo.x + T.hi.x) * 0.5f, cz = (T.lo.z + T.hi.z) * 0.5f;
				float R = fmaxf (T.hi.x - T.lo.x, T.hi.z - T.lo.z) * 0.5f + 150;
				for (int k = 0; k < 24; k++)
				{
					float a = 2 * PI * (float) k / 24 + rng.range (-0.05f, 0.05f), rr = R + rng.range (0, 140);
					V3 p (cx + cosf (a) * rr, baseY, cz + sinf (a) * rr);
					{ unsigned tc = pastel (rng, T); place (lib.get (TOWERS[rng.n (6)], tc, rgbMix (tc, 0xFFFFFF, 0.22f), 0), at (p, rng.range (0, 6.28f), 1.5f, rng.range (1.2f, 2.4f), 1.5f), -1); }
				}
			}
		}
		islands (T, lib, rng);
		// the traffic: ships flying round
		float cx = (T.lo.x + T.hi.x) * 0.5f, cz = (T.lo.z + T.hi.z) * 0.5f;
		float R = fmaxf (T.hi.x - T.lo.x, T.hi.z - T.lo.z) * 0.5f + 100;
		for (int k = 0; k < 8 && ntraffic < 16; k++)
		{
			Traffic &F = traffic[ntraffic++];
			F.c = V3 (cx + rng.range (-60, 60), 0, cz + rng.range (-60, 60)); F.r = R * rng.range (0.3f, 0.9f); F.h = T.lo.y + rng.range (25, 90);
			F.speed = rng.range (0.05f, 0.12f) * (rng.f () < 0.5f ? -1 : 1); F.ph = rng.range (0, 6.28f); F.col = T.neon[k % 3];
		}
	}
	// floating islands (grass, rocks, round trees) and cloud puffs around the track, at all heights
	void islands (const Track &T, ModelLib &lib, Rng &rng)
	{
		if (T.timeOfDay == Track::T_SPACE && !strcmp (T.groundKind, "none")) { /* (asteroids instead: the track's own props) */ }
		static const char *ISL[] = { "island_small", "island_big", "island_long" };
		static const char *TREES[] = { "tree_round", "tree_round2", "bush_round", "rock_round", "rock_round2" };
		float cx = (T.lo.x + T.hi.x) * 0.5f, cz = (T.lo.z + T.hi.z) * 0.5f;
		float rx = (T.hi.x - T.lo.x) * 0.5f + 120, rz = (T.hi.z - T.lo.z) * 0.5f + 120;
		bool space = T.timeOfDay == Track::T_SPACE;
		int nIsl = space ? 0 : 40, nCloud = space ? 0 : 36;
		for (int k = 0, tries = 0; k < nIsl && tries < nIsl * 10; tries++)
		{
			V3 p (cx + rng.range (-rx, rx), 0, cz + rng.range (-rz, rz));
			int ni = 0; float dist = 0;
			clearOfRoad (T, p, 0, &ni, &dist);
			float roadY = T.seg[ni].c.y;
			// near the road: well below it or well above the ships; farther: anywhere
			if (dist < T.seg[ni].w * 0.5f + T.seg[ni].fork + 25) continue;
			p.y = dist < 60 ? roadY - rng.range (10, 30) : roadY + rng.range (-35, 30);
			const char *nm = ISL[rng.n (3)];
			const Mesh *m = lib.get (nm, 0xFF000000u, 0xFF000000u, 0);
			if (!m) break;
			float sc = rng.range (0.8f, 1.6f), yaw = rng.range (0, 6.28f);
			int ch = dist < 110 ? ni / CHUNK : -1;
			if (ch < 0) { lowIsland (far, p, (m->hi.x - m->lo.x) * 0.5f * sc, (m->hi.y - m->lo.y) * sc); k++; continue; }	// (far: a cheap one)
			place (m, at (p, yaw, sc, sc, sc), ch);
			// trees and rocks on the grass (the near islands only: the far ones too small to see them)
			float r = (m->hi.x - m->lo.x) * 0.3f * sc;
			int nt = ch < 0 ? 0 : 1 + rng.n (4);
			for (int t = 0; t < nt; t++)
			{
				float a = rng.range (0, 6.28f), d = rng.range (0, r);
				const Mesh *tm = lib.get (TREES[rng.n (5)], 0xFF000000u, 0xFF000000u, 0);
				if (tm) place (tm, at (p + V3 (cosf (a) * d, 0, sinf (a) * d), rng.range (0, 6.28f), sc * 0.9f, sc * rng.range (0.8f, 1.2f), sc * 0.9f), ch);
			}
			k++;
		}
		for (int k = 0; k < nCloud; k++)
		{
			V3 p (cx + rng.range (-rx * 1.4f, rx * 1.4f), T.lo.y + rng.range (-40, 70), cz + rng.range (-rz * 1.4f, rz * 1.4f));
			int ni = 0; float dist = 0;
			clearOfRoad (T, p, 0, &ni, &dist);
			if (dist < T.seg[ni].w * 0.5f + T.seg[ni].fork + 30 && fabsf (p.y - T.seg[ni].c.y) < (T.seg[ni].fork > 0 ? 36 : 20)) continue;	// (a fork: its lower branch too)
			float sc = rng.range (0.7f, 1.8f);
			if (dist >= 110) { for (int j = 0; j < 3; j++) lowPuff (far, p + V3 (rng.range (-8, 8) * sc, 0, rng.range (-8, 8) * sc), 7 * sc); continue; }
			const Mesh *m = lib.get ("cloud_puff", 0xFF000000u, 0xFF000000u, 0);
			if (!m) break;
			place (m, at (p, rng.range (0, 6.28f), sc, sc * 0.8f, sc), ni / CHUNK);
		}
	}

	// ---- the track file's own props ------------------------------------------------------------------------------------
	void props (const Track &T, ModelLib &lib, Rng &rng)
	{
		for (int k = 0; k < T.nprops; k++)
		{
			const PropLine &P = T.props[k];
			const Mesh *m = lib.get (P.model, T.walls[0] ? T.walls[0] : pastel (rng, T), T.walls[1]);
			if (!m) continue;
			int step = (int) (P.spacing / SEG); if (step < 1) step = 1;
			int i0 = (int) (P.t0 * T.n), i1 = (int) (P.t1 * T.n);
			for (int i = i0 + rng.n (step); i < i1 && i < T.n; i += step)
				for (int side = -1; side <= 1; side += 2)
				{
					if (P.side && side != P.side) continue;
					const Seg &S = T.seg[i];
					if (S.flags & (F_TUNNEL | F_LOOP | F_GAP | F_TWIST) || S.tube > 0) continue;
					float d = S.w * 0.5f + S.fork + 4.0f + rng.f () * 3.0f + (m->hi.x - m->lo.x) * 0.5f * P.scale;
					V3 p = S.c + V3 (S.r.x, 0, S.r.z) * ((float) side * d);
					p.y = S.c.y - 2 > T.groundY ? T.groundY : S.c.y - 0.6f;
					float yaw = atan2f (S.f.x, S.f.z) + (side > 0 ? -PI / 2 : PI / 2) + rng.range (-0.3f, 0.3f);
					if (!clearOfRoad (T, p, 2.0f)) continue;
					float sc = P.scale * rng.range (0.85f, 1.15f);
					place (m, at (p, yaw, sc, sc, sc), i / CHUNK);
				}
		}
		float cx = (T.lo.x + T.hi.x) * 0.5f, cz = (T.lo.z + T.hi.z) * 0.5f;
		float rx = (T.hi.x - T.lo.x) * 0.5f + 60, rz = (T.hi.z - T.lo.z) * 0.5f + 60;
		for (int k = 0; k < T.nscat; k++)
		{
			const Scatter &S = T.scat[k];
			const Mesh *m = lib.get (S.model, T.walls[0] ? T.walls[0] : pastel (rng, T), T.walls[1]);
			if (!m) continue;
			for (int j = 0, tries = 0; j < S.count && tries < S.count * 8; tries++)
			{
				V3 p (cx + rng.range (-rx, rx), T.groundY, cz + rng.range (-rz, rz));
				int ni = 0; float dist = 0;
				if (!clearOfRoad (T, p, 6.0f + len (m->hi - m->lo) * 0.5f * S.scale, &ni, &dist)) continue;
				float sc = S.scale * rng.range (0.8f, 1.25f);
				place (m, at (p, rng.range (0, 2 * PI), sc, sc, sc), dist < 90 ? ni / CHUNK : -1);
				j++;
			}
		}
		for (int k = 0; k < T.ndecor; k++)
		{
			const Decor &D = T.decor[k];
			const Mesh *m = lib.get (D.model, T.walls[0] ? T.walls[0] : pastel (rng, T), T.walls[1]);
			V3 p (D.x, D.hasY ? D.y : T.groundY, D.z);
			int ni = 0; float dist = 0; clearOfRoad (T, p, 0, &ni, &dist);
			place (m, at (p, D.yaw, D.scale, D.scale, D.scale), dist < 90 ? ni / CHUNK : -1);
		}
	}

	// ---- the ground: a sea of clouds below (the default: the track floats), the sea, a plaza, sand, snow... -------------
	void ground (const Track &T, ModelLib &lib, Rng &rng)
	{
		const char *kind = T.groundKind;
		if (!strcmp (kind, "none")) return;
		float cx = (T.lo.x + T.hi.x) * 0.5f, cz = (T.lo.z + T.hi.z) * 0.5f;
		float R = fmaxf (T.hi.x - T.lo.x, T.hi.z - T.lo.z) * 0.5f + 520;
		bool clouds = !strcmp (kind, "clouds") || !strcmp (kind, "islands");
		bool water = !strcmp (kind, "water") || !strcmp (kind, "sea");
		const float CELL = 60;
		int N = (int) (2 * R / CELL) + 1;
		float y = T.groundY;
		unsigned c0 = clouds ? rgbMix (T.sky[1], T.sky[0], 0.35f) : T.ground[0], c1 = clouds ? rgbMix (T.sky[1], 0xFFFFFF, 0.35f) : T.ground[1];
		for (int a = 0; a < N; a++)
			for (int b = 0; b < N; b++)
			{
				float x0 = cx - R + (float) a * CELL, z0 = cz - R + (float) b * CELL;
				float dx = x0 + CELL / 2 - cx, dz = z0 + CELL / 2 - cz;
				if (dx * dx + dz * dz > R * R * 1.1f) continue;
				// gentle swells (clouds, sea), a soft checker on land
				auto H = [&] (float x, float z) { return (clouds || water) ? sinf (x * 0.021f + z * 0.013f) * (clouds ? 5.0f : 0.6f) + cosf (z * 0.017f - x * 0.008f) * (clouds ? 4.0f : 0.5f) : 0.0f; };
				V3 p00 (x0, y + H (x0, z0), z0), p10 (x0 + CELL, y + H (x0 + CELL, z0), z0), p11 (x0 + CELL, y + H (x0 + CELL, z0 + CELL), z0 + CELL), p01 (x0, y + H (x0, z0 + CELL), z0 + CELL);
				unsigned c = (clouds || water) ? rgbMix (c0, c1, 0.5f + 0.5f * sinf ((float) (a * 7 + b * 13))) : T.ground[(a + b) & 1];
				V3 n = cross (p11 - p00, p10 - p00); if (n.y < 0) n = -n;
				far.face (p01, p11, p10, p00, shade (c, n), V3 (0, 1, 0));
			}
		// a sea of clouds: round cloud banks on it (it reads as clouds far below, not as a floor)
		if (clouds)
			for (int k = 0; k < 28; k++)		// (cheap puffs: they are far below -- the GPU's budget)
			{
				V3 p (cx + rng.range (-R, R) * 0.8f, y + rng.range (2, 8), cz + rng.range (-R, R) * 0.8f);
				float r = rng.range (12, 26);
				for (int j = 0; j < 3; j++) lowPuff (far, p + V3 (rng.range (-r, r), 0, rng.range (-r, r)), r * rng.range (0.6f, 1.0f));
			}
		// land: soft hills round the edge in the horizon's colour
		if (!clouds && !water)
			for (int k = 0; k < 30; k++)
			{
				float a = 2 * PI * (float) k / 30 + rng.range (-0.05f, 0.05f), rr = R * rng.range (0.75f, 0.95f);
				V3 c (cx + cosf (a) * rr, y, cz + sinf (a) * rr);
				mound (far, c, rng.range (50, 110), rng.range (30, 90), rgbMix (T.ground[0], T.sky[1], rng.range (0.2f, 0.5f)));
			}
	}
	// a cheap floating island (far away): a grass disc with a soft rim over a rocky cone, 8 sides
	void lowIsland (VList &L, const V3 &c, float r, float h)
	{
		const int S = 8;
		V3 tip = c - V3 (0, h, 0);
		for (int k = 0; k < S; k++)
		{
			float a0 = 2 * PI * (float) k / S, a1 = 2 * PI * (float) (k + 1) / S;
			V3 e0 (cosf (a0) * r, 0, sinf (a0) * r), e1 (cosf (a1) * r, 0, sinf (a1) * r);
			V3 i0 = e0 * 0.85f + V3 (0, 0.6f, 0), i1 = e1 * 0.85f + V3 (0, 0.6f, 0);
			L.face (c + i0, c + i1, c + V3 (0, 0.6f, 0), c + V3 (0, 0.6f, 0), shade (0x7CCB4E, V3 (0, 1, 0)), V3 (0, 1, 0));
			V3 sn = (e0 + e1) * 0.5f;
			L.face (c + e0, c + e1, c + i1, c + i0, shade (0x6BBF45, sn + V3 (0, r, 0)), sn + V3 (0, r, 0));
			L.face (c + e0 - V3 (0, 1.2f, 0), c + e1 - V3 (0, 1.2f, 0), c + e1, c + e0, shade (0x5AAE3C, sn), sn);
			L.tri (c + e0 - V3 (0, 1.2f, 0), tip, c + e1 - V3 (0, 1.2f, 0), shade (0xA27E66, sn - V3 (0, r * 0.4f, 0)));
		}
	}
	// a cheap cloud ball (far away): 8 sides, 3 rings, white on top, bluish below
	void lowPuff (VList &L, const V3 &c, float r)
	{
		const int S = 8, Rr = 3;
		for (int j = 0; j < Rr; j++)
			for (int k = 0; k < S; k++)
			{
				float e0 = -0.3f + (PI / 2 + 0.3f) * (float) j / Rr, e1 = -0.3f + (PI / 2 + 0.3f) * (float) (j + 1) / Rr;
				float a0 = 2 * PI * (float) k / S, a1 = 2 * PI * (float) (k + 1) / S;
				auto P = [&] (float a, float e) { return c + V3 (cosf (a) * cosf (e) * r, sinf (e) * r * 0.7f, sinf (a) * cosf (e) * r); };
				unsigned col = rgbMix (0xD3E0F4, 0xFFFFFF, (float) (j + 1) / Rr);
				L.tri (P (a0, e0), P (a1, e1), P (a1, e0), col);
				L.tri (P (a0, e0), P (a0, e1), P (a1, e1), col);
			}
	}
	void mound (VList &L, const V3 &c, float r, float h, unsigned col)
	{
		const int S = 9, R = 3;
		for (int j = 0; j < R; j++)
			for (int k = 0; k < S; k++)
			{
				float a0 = 2 * PI * k / S, a1 = 2 * PI * (k + 1) / S;
				float e0 = (float) j / R, e1 = (float) (j + 1) / R;
				auto P = [&] (float a, float e) { float rad = r * cosf (e * PI / 2); return c + V3 (cosf (a) * rad, h * sinf (e * PI / 2), sinf (a) * rad); };
				V3 p00 = P (a0, e0), p01 = P (a1, e0), p10 = P (a0, e1), p11 = P (a1, e1);
				V3 n = cross (p01 - p00, p10 - p00);
				if (n.y < 0) n = -n;
				unsigned cc = shade (col, n);
				if (j == R - 1) { if (dot (cross (p10 - p00, p01 - p00), n) < 0) L.tri (p00, p01, p10, cc); else L.tri (p00, p10, p01, cc); }
				else L.face (p00, p10, p11, p01, cc, n);
			}
	}

	// ---- the sky ---------------------------------------------------------------------------------------------------------
	void skyQuad (const V3 &dir, float size, unsigned col, float rot = 0)
	{
		V3 d = norm (dir) * 850, side = norm (cross (d, V3 (0, 1, 0)));
		if (len (cross (d, V3 (0, 1, 0))) < 1e-3f) side = V3 (1, 0, 0);
		V3 up = norm (cross (side, d));
		float c = cosf (rot), s = sinf (rot);
		V3 a = side * c + up * s, b = up * c - side * s;
		skyGlow.quadUV (d - a * size - b * size, d + a * size - b * size, d + a * size + b * size, d - a * size + b * size, col, 0, 0, 1, 1);
	}
	void makeSky (const Track &T, ModelLib &lib, Rng &rng)
	{
		const int S = 20, R = 7;
		float rad = 900;
		int tod = T.timeOfDay;
		for (int j = 0; j < R; j++)
			for (int k = 0; k < S; k++)
			{
				float a0 = 2 * PI * k / S, a1 = 2 * PI * (k + 1) / S;
				float e0 = -0.5f + 2.07f * (float) j / R, e1 = -0.5f + 2.07f * (float) (j + 1) / R;
				if (e1 > PI / 2) e1 = PI / 2;
				auto P = [&] (float a, float e) { return V3 (cosf (a) * cosf (e) * rad, sinf (e) * rad, sinf (a) * cosf (e) * rad); };
				auto C = [&] (float e, float a) {
					float f = clampf ((e + 0.05f) / 0.85f, 0, 1); f = sqrtf (f);
					unsigned hor = tod <= Track::T_DUSK ? rgbMix (T.sky[1], T.sky[0], 0.35f) : T.sky[1];	// (a deeper horizon: no washed-out haze)
					unsigned c = rgbVivid (rgbMix (hor, T.sky[0], f), 1.15f);
					if (tod == Track::T_DUSK) c = rgbMix (c, 0xFF9A5A, clampf (1 - fabsf (wrapAngle (a - 0.8f)) / 1.2f, 0, 1) * (1 - f) * 0.5f);
					return c; };
				V3 p00 = P (a0, e0), p01 = P (a1, e0), p10 = P (a0, e1), p11 = P (a1, e1);
				sky.tri (p00, p01, p11, C (e0, a0), C (e0, a1), C (e1, a1));
				sky.tri (p00, p11, p10, C (e0, a0), C (e1, a1), C (e1, a0));
			}
		// the stars (night, aurora, space)
		if (tod >= Track::T_NIGHT)
		{
			int ns = tod == Track::T_SPACE ? 700 : 260;
			for (int k = 0; k < ns; k++)
			{
				float a = rng.range (0, 2 * PI), e = tod == Track::T_SPACE ? asinf (rng.range (-0.95f, 1)) : asinf (rng.range (0.12f, 1));
				float b = rng.range (0.3f, 1.0f); b *= b;
				unsigned col = rgbScale (rgbMix (0xFFFFFF, rng.f () < 0.5f ? 0x9FD8FF : 0xFFD8F0, rng.f () * 0.6f), b);
				skyQuad (V3 (cosf (a) * cosf (e), sinf (e), sinf (a) * cosf (e)), rng.range (1.5f, 4.0f), col);
			}
		}
		// deep space: nebulae (big soft clouds of colour), the galaxy's band
		if (tod == Track::T_SPACE)
		{
			for (int k = 0; k < 26; k++)
			{
				float a = rng.range (0, 2 * PI), e = rng.range (-0.5f, 0.9f);
				unsigned col = rgbScale (T.neon[k % 3], rng.range (0.15f, 0.35f));
				skyQuad (V3 (cosf (a) * cosf (e), sinf (e), sinf (a) * cosf (e)), rng.range (90, 240), col, rng.range (0, 3));
			}
			for (int k = 0; k < 40; k++)
			{
				float t = (float) k / 40, a = t * 2 * PI, e = 0.35f * sinf (a * 1.0f + 0.6f);
				skyQuad (V3 (cosf (a) * cosf (e), sinf (e), sinf (a) * cosf (e)), rng.range (60, 110), rgbScale (0xC8B8FF, 0.16f), rng.range (0, 3));
			}
		}
		// the aurora: curtains of green and violet over the horizon
		if (tod == Track::T_AURORA)
			for (int k = 0; k < 48; k++)
			{
				float a = 2 * PI * (float) k / 48;
				unsigned col = rgbScale (k & 1 ? 0x3FD9A0 : 0x8A5CFF, 0.22f + 0.12f * sinf ((float) k * 0.9f));
				for (int j = 0; j < 3; j++) skyQuad (V3 (cosf (a), 0.25f + 0.12f * (float) j + 0.08f * sinf (a * 3), sinf (a)), 70, col, 1.57f);
			}
		// the sun(s): a big soft disc (day, dusk)
		if (tod == Track::T_DAY) skyQuad (V3 (-0.45f, 0.75f, 0.4f), 80, 0xFFF4D0);
		if (tod == Track::T_DUSK) { skyQuad (V3 (0.7f, 0.06f, 0.71f), 110, 0xFF9A5A); skyQuad (V3 (0.9f, 0.1f, 0.43f), 60, 0xFFD08A); }
		// planets (space, night): big lit spheres in the sky, a ring around some
		if (tod >= Track::T_NIGHT)
		{
			int np = tod == Track::T_SPACE ? 3 : 1;
			for (int k = 0; k < np; k++)
			{
				float a = rng.range (0, 2 * PI), e = rng.range (0.12f, 0.5f);
				V3 c = V3 (cosf (a) * cosf (e), sinf (e), sinf (a) * cosf (e)) * 780;
				float r = rng.range (50, k == 0 ? 160 : 80);
				planet (c, r, T.neon[k % 3], k == 0);
			}
		}
		// clouds by day / dusk: the skybox's band (the user: lighter than 3D clouds) -- a ring round the horizon textured
		// with TX_CLOUDS (round cumulus painted once), the texture twice round, tinted at dusk
		if (tod <= Track::T_DUSK)
		{
			const int K = 16;
			const float e0 = 0.03f, e1 = 0.40f, rr = 800;
			unsigned tint = tod == Track::T_DUSK ? 0xFFD8C4 : 0xFFFFFF;
			for (int k = 0; k < K; k++)
			{
				float a0 = 2 * PI * (float) k / K, a1 = 2 * PI * (float) (k + 1) / K;
				auto P = [&] (float a, float e) { return V3 (cosf (a) * cosf (e) * rr, sinf (e) * rr, sinf (a) * cosf (e) * rr); };
				float s0 = 2.0f * (float) k / K, s1 = 2.0f * (float) (k + 1) / K;
				// (quadUV: a b c d with a, b at the bottom: t1 there, t0 at the top)
				skyBand.quadUV (P (a0, e0), P (a1, e0), P (a1, e1), P (a0, e1), tint, s0, 0.0f, s1, 1.0f);
			}
		}
		(void) lib;
	}
	void planet (const V3 &c, float r, unsigned col, bool ringed)
	{
		const int S = 18, Rr = 10;
		V3 sun = norm (V3 (-0.6f, 0.5f, 0.6f));
		for (int j = 0; j < Rr; j++)
			for (int k = 0; k < S; k++)
			{
				float e0 = -PI / 2 + PI * (float) j / Rr, e1 = -PI / 2 + PI * (float) (j + 1) / Rr;
				float a0 = 2 * PI * (float) k / S, a1 = 2 * PI * (float) (k + 1) / S;
				auto N = [&] (float a, float e) { return V3 (cosf (a) * cosf (e), sinf (e), sinf (a) * cosf (e)); };
				auto C = [&] (const V3 &n) { float d = dot (n, sun); unsigned base = rgbMix (col, 0xFFFFFF, 0.15f + 0.1f * sinf (n.y * 9)); return rgbScale (base, 0.25f + 0.75f * smooth01 (d * 0.8f + 0.4f)); };
				V3 n00 = N (a0, e0), n01 = N (a1, e0), n10 = N (a0, e1), n11 = N (a1, e1);
				skyObj.tri (c + n00 * r, c + n11 * r, c + n01 * r, C (n00), C (n11), C (n01));
				skyObj.tri (c + n00 * r, c + n10 * r, c + n11 * r, C (n00), C (n10), C (n11));
			}
		if (ringed)
		{
			V3 ax = norm (V3 (0.3f, 1, 0.2f)), s1 = norm (cross (ax, V3 (1, 0, 0))), s2 = cross (ax, s1);
			const int K = 40;
			for (int k = 0; k < K; k++)
			{
				float a0 = 2 * PI * (float) k / K, a1 = 2 * PI * (float) (k + 1) / K;
				V3 d0 = s1 * cosf (a0) + s2 * sinf (a0), d1 = s1 * cosf (a1) + s2 * sinf (a1);
				unsigned rc = rgbMix (col, 0xFFFFFF, 0.5f);
				skyObj.tri (c + d0 * (r * 1.4f), c + d1 * (r * 1.4f), c + d1 * (r * 2.1f), rc);
				skyObj.tri (c + d0 * (r * 1.4f), c + d1 * (r * 2.1f), c + d0 * (r * 2.1f), rgbScale (rc, 0.85f));
			}
		}
	}
	void puff (const V3 &c, float r, unsigned col)
	{
		// a ball of cloud cut flat underneath: white on top, a soft blue-grey below
		const int S = 9, Rr = 4;
		const float e00 = -0.35f;
		for (int j = 0; j < Rr; j++)
			for (int k = 0; k < S; k++)
			{
				float e0 = e00 + (PI / 2 - e00) * (float) j / Rr, e1 = e00 + (PI / 2 - e00) * (float) (j + 1) / Rr;
				float a0 = 2 * PI * (float) k / S, a1 = 2 * PI * (float) (k + 1) / S;
				auto P = [&] (float a, float e) { return c + V3 (cosf (a) * cosf (e) * r, sinf (e) * r * 0.85f, sinf (a) * cosf (e) * r); };
				auto C = [&] (float e) { return rgbMix (rgbMix (col, 0xC9D8F0, 0.55f), col, smooth01 ((e - e00) / (PI / 2 - e00) * 1.6f)); };
				skyObj.tri (P (a0, e0), P (a1, e1), P (a1, e0), C (e0), C (e1), C (e0));
				skyObj.tri (P (a0, e0), P (a0, e1), P (a1, e1), C (e0), C (e1), C (e1));
			}
		// the flat bottom
		for (int k = 0; k < S; k++)
		{
			float a0 = 2 * PI * (float) k / S, a1 = 2 * PI * (float) (k + 1) / S;
			auto P = [&] (float a) { return c + V3 (cosf (a) * cosf (e00) * r, sinf (e00) * r * 0.85f, sinf (a) * cosf (e00) * r); };
			skyObj.tri (P (a0), P (a1), c + V3 (0, sinf (e00) * r * 0.85f, 0), rgbMix (col, 0xC9D8F0, 0.6f));
		}
	}
};

} // namespace cz

#endif
