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
	VList glow[MAXCH];			// what glows: drawn after, added (no depth written)
	VList clear[MAXCH];			// what is see-through: the force fields (alpha, no depth written)
	VList signs[MAXCH];			// the billboards' pictures (added, the TX_SIGNS texture)
	VList far, farGlow;			// the ground, the far city, the hills (always drawn)
	VList sky, skyGlow, skyObj;		// around the camera (drawn without its position): the dome; the stars, the suns (TX_GLOW); the planets, the clouds
	float glowK = 1;			// the light streaks' strength (1 at night .. 0.5 by day)
	unsigned fogCol = 0;
	struct Traffic { V3 c; float r, h, speed, ph; unsigned col; } traffic[16]; int ntraffic = 0;

	static unsigned shade (unsigned c, const V3 &n) { return rgbScale (c, toonShade (norm (n), sunDir ())); }

	void build (const Track &T, ModelLib &lib, unsigned seed)
	{
		for (int i = 0; i < MAXCH; i++) { chunk[i].v.clear (); glow[i].v.clear (); clear[i].v.clear (); signs[i].v.clear (); }
		far.v.clear (); farGlow.v.clear (); sky.v.clear (); skyGlow.v.clear (); skyObj.v.clear (); ntraffic = 0;
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
			chunk[i].bound ();
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
		(void) G; (void) C;
		const Seg &A = T.seg[i], &B = T.seg[(i + 1) % T.n];
		if ((A.flags & F_GAP) && !(B.flags & F_GAP)) { gapEdge (T, B, L); }
		if (A.flags & F_GAP) { if (!(T.seg[(i + T.n - 1) % T.n].flags & F_GAP)) gapEdge (T, A, L); return; }
		float tube = fmaxf (A.tube, B.tube);
		bool closed = A.tube > 0.985f && B.tube > 0.985f;
		bool tun = (A.flags & F_TUNNEL) != 0 && tube <= 0;
		bool start = (A.flags & F_START) || i == T.n - 1;
		auto SP = [&] (const Seg &S, float l, float h, V3 *n = 0, V3 *lat = 0) { V3 p; Track::surfOf (S.c, S.r, S.u, S.tube, l, h, &p, n, lat); return p; };
		// the surface
		int cols = 4 + (int) (tube * 20 + 0.5f);
		float ea = A.w * 0.5f, eb = B.w * 0.5f;
		unsigned band = ((i / 8) & 1) ? T.road[1] : T.road[0];
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
		// faint lane marks (lighter, dashed)
		if (!start && (i % 6) < 3)
			for (int k = -1; k <= 1; k += 2)
			{
				float l = (float) k * (tube > 0.4f ? 0 : A.w / 6), lb = (float) k * (tube > 0.4f ? 0 : B.w / 6), wl = 0.12f;
				if (tube > 0.4f) continue;
				L.face (SP (A, l - wl, 0.02f), SP (A, l + wl, 0.02f), SP (B, lb + wl, 0.02f), SP (B, lb - wl, 0.02f), shade (rgbMix (band, 0xFFFFFF, 0.3f), A.u), A.u);
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
			// the rail: an 8-sided round bar on the edge, its stripes in turn (2 m each)
			unsigned rc = T.rails[(i & 1)];
			const int K = 8;
			float rr = 0.42f;
			V3 cA = SP (A, s * (ea - 0.35f), rr * 0.8f), cB = SP (B, s * (eb - 0.35f), rr * 0.8f);
			for (int k = 0; k < K; k++)
			{
				float t0 = 2 * PI * (float) k / K, t1 = 2 * PI * (float) (k + 1) / K;
				V3 d0A = nA * cosf (t0) + outA * sinf (t0), d1A = nA * cosf (t1) + outA * sinf (t1);
				V3 d0B = nB * cosf (t0) + outB * sinf (t0), d1B = nB * cosf (t1) + outB * sinf (t1);
				V3 fn = (d0A + d1A) * 0.5f;
				L.face (cA + d0A * rr, cB + d0B * rr, cB + d1B * rr, cA + d1A * rr, shade (rc, fn), fn);
			}
			// a small round post under the rail now and then (a joint)
			if ((i % 12) == 0) box (L, cA + nA * 0.1f, cA - nA * 0.9f, 0.18f, rgbScale (T.road[0], 0.75f));
		}
		// the tunnel: a rounded arch with lighter ribs
		if (tun)
		{
			const int K = 10;
			float ra = A.w * 0.5f + 1.8f, rb = B.w * 0.5f + 1.8f;
			bool rib = (i % 4) == 0;
			for (int k = 0; k < K; k++)
			{
				float a0 = PI * (float) k / K, a1 = PI * (float) (k + 1) / K;
				V3 p0 = A.c + A.r * (cosf (a0) * ra) + A.u * (sinf (a0) * ra * 0.7f);
				V3 p1 = A.c + A.r * (cosf (a1) * ra) + A.u * (sinf (a1) * ra * 0.7f);
				V3 q0 = B.c + B.r * (cosf (a0) * rb) + B.u * (sinf (a0) * rb * 0.7f);
				V3 q1 = B.c + B.r * (cosf (a1) * rb) + B.u * (sinf (a1) * rb * 0.7f);
				V3 inward = A.c - (p0 + p1) * 0.5f;
				unsigned c = rib ? rgbMix (T.walls[0], 0xFFFFFF, 0.35f) : T.walls[0];
				L.face (p0, q0, q1, p1, shade (c, inward), inward);
				V3 o = (p0 - A.c) * 0.1f, oq = (q0 - B.c) * 0.1f, o1 = (p1 - A.c) * 0.1f, oq1 = (q1 - B.c) * 0.1f;
				L.face (p0 + o, q0 + oq, q1 + oq1, p1 + o1, shade (T.walls[0], -inward), -inward);
			}
		}
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
			bool sign = glowing && panel >= 0 && T.group == panel && chunkIdx >= 0;
			VList &D = sign ? S : glowing ? G : L;
			for (int k = 0; k < 3; k++)
			{
				Vtx *p = D.v.push (); if (!p) return;
				unsigned c = T.col[k];
				if (glowing) c = rgbScale (sign ? signCol : c, sign ? 1.0f : glowK * 0.9f + 0.1f);
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
		float hw = T.seg[bi].w * 0.5f + margin;
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
		static const unsigned P[] = { 0xFF8A80, 0xFFD966, 0x9EE6B8, 0xC9B6FF, 0xF4F6FF, 0x9AD7FF, 0xFFB38A, 0xB8F0E6 };
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
						float d = S.w * 0.5f + 30 + (float) row * 40 + rng.range (0, 25);
						V3 p = S.c + V3 (S.r.x, 0, S.r.z) * ((float) side * d);
						p.y = baseY;
						if (!clearOfRoad (T, p, 18)) continue;
						const Mesh *m = lib.get (TOWERS[rng.n (6)], pastel (rng, T), 0xF4F6FF, 0);
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
				V3 p = S.c + V3 (S.r.x, 0, S.r.z) * (side * (S.w * 0.5f + 8)) - V3 (0, 6, 0);
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
				for (int k = 0; k < 36; k++)
				{
					float a = 2 * PI * (float) k / 36 + rng.range (-0.05f, 0.05f), rr = R + rng.range (0, 140);
					V3 p (cx + cosf (a) * rr, baseY, cz + sinf (a) * rr);
					place (lib.get (TOWERS[rng.n (6)], pastel (rng, T), 0xF4F6FF, 0), at (p, rng.range (0, 6.28f), 1.5f, rng.range (1.2f, 2.4f), 1.5f), -1);
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
			if (dist < T.seg[ni].w * 0.5f + 25) continue;
			p.y = dist < 60 ? roadY - rng.range (10, 30) : roadY + rng.range (-35, 30);
			const char *nm = ISL[rng.n (3)];
			const Mesh *m = lib.get (nm, 0xFF000000u, 0xFF000000u, 0);
			if (!m) break;
			float sc = rng.range (0.8f, 1.6f), yaw = rng.range (0, 6.28f);
			int ch = dist < 110 ? ni / CHUNK : -1;
			place (m, at (p, yaw, sc, sc, sc), ch);
			// trees and rocks on the grass
			float r = (m->hi.x - m->lo.x) * 0.3f * sc;
			int nt = 1 + rng.n (4);
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
			if (dist < T.seg[ni].w * 0.5f + 30 && fabsf (p.y - T.seg[ni].c.y) < 20) continue;
			const Mesh *m = lib.get ("cloud_puff", 0xFF000000u, 0xFF000000u, 0);
			if (!m) break;
			float sc = rng.range (0.7f, 1.8f);
			place (m, at (p, rng.range (0, 6.28f), sc, sc * 0.8f, sc), dist < 110 ? ni / CHUNK : -1);
		}
	}

	// ---- the track file's own props ------------------------------------------------------------------------------------
	void props (const Track &T, ModelLib &lib, Rng &rng)
	{
		for (int k = 0; k < T.nprops; k++)
		{
			const PropLine &P = T.props[k];
			const Mesh *m = lib.get (P.model, T.walls[0], T.walls[1]);
			if (!m) continue;
			int step = (int) (P.spacing / SEG); if (step < 1) step = 1;
			int i0 = (int) (P.t0 * T.n), i1 = (int) (P.t1 * T.n);
			for (int i = i0 + rng.n (step); i < i1 && i < T.n; i += step)
				for (int side = -1; side <= 1; side += 2)
				{
					if (P.side && side != P.side) continue;
					const Seg &S = T.seg[i];
					if (S.flags & (F_TUNNEL | F_LOOP | F_GAP | F_TWIST) || S.tube > 0) continue;
					float d = S.w * 0.5f + 4.0f + rng.f () * 3.0f + (m->hi.x - m->lo.x) * 0.5f * P.scale;
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
			const Mesh *m = lib.get (S.model, T.walls[0], T.walls[1]);
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
			const Mesh *m = lib.get (D.model, T.walls[0], T.walls[1]);
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
			for (int k = 0; k < 70; k++)
			{
				const Mesh *m = lib.get ("cloud_puff", 0xFF000000u, 0xFF000000u, 0);
				if (!m) break;
				V3 p (cx + rng.range (-R, R) * 0.8f, y + rng.range (2, 8), cz + rng.range (-R, R) * 0.8f);
				float sc = rng.range (1.5f, 3.5f);
				place (m, at (p, rng.range (0, 6.28f), sc, sc * 0.6f, sc), -1);
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
					unsigned c = rgbMix (T.sky[1], T.sky[0], f);
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
		// clouds by day / dusk: flat white puffs low in the sky
		if (tod <= Track::T_DUSK)
			for (int k = 0; k < 22; k++)
			{
				float a = rng.range (0, 2 * PI), e = rng.range (0.03f, 0.3f);
				V3 c = V3 (cosf (a) * cosf (e), sinf (e), sinf (a) * cosf (e)) * 800;
				unsigned col = tod == Track::T_DUSK ? 0xFFC8B0 : 0xFFFFFF;
				for (int j = 0; j < 4; j++) puff (c + V3 (rng.range (-40, 40), rng.range (-5, 10), rng.range (-40, 40)), rng.range (25, 50), col);
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
		const int S = 10, Rr = 4;
		for (int j = 0; j < Rr; j++)
			for (int k = 0; k < S; k++)
			{
				float e0 = PI / 2 * (float) j / Rr, e1 = PI / 2 * (float) (j + 1) / Rr;
				float a0 = 2 * PI * (float) k / S, a1 = 2 * PI * (float) (k + 1) / S;
				auto P = [&] (float a, float e) { return c + V3 (cosf (a) * cosf (e) * r, sinf (e) * r * 0.55f, sinf (a) * cosf (e) * r); };
				unsigned cc = rgbScale (col, 0.86f + 0.14f * (float) j / Rr);
				skyObj.tri (P (a0, e0), P (a1, e1), P (a1, e0), cc);
				skyObj.tri (P (a0, e0), P (a0, e1), P (a1, e1), cc);
			}
	}
};

} // namespace cz

#endif
