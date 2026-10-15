//
// world.h -- a circuit's scenery, made once when it is loaded: the road (its plates, its light streaks, the rails and
// their force fields, its underside and the pylons under it, the tunnels, the tubes, the gaps), the start gate, the
// city around it (the towers in the circuit's colours, the neon billboards, the lamps, the highways far off), the
// ground (plazas, sea, sand, snow...) and the sky (day, dusk, night, aurora, deep space: the stars, the nebulae, the
// planets, the clouds) -- cut into pieces of CHUNK segments, each with a sphere around it: only the pieces in front of
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
	bool outline = true;			// (the ships' parts are taken raw: buildShip lights them together)
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
		if (!outline && withOutline < 0) return raw[mi];		// (raw: the ships' parts)
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

	// ---- the road: plates, light streaks, the rails and their force fields; rolled into a tube where it is one -------
	void roadSeg (const Track &T, int i, VList &L, VList &G, VList &C)
	{
		const Seg &A = T.seg[i], &B = T.seg[(i + 1) % T.n];
		if ((A.flags & F_GAP) && !(B.flags & F_GAP)) { gapEdge (T, B, L); }
		if (A.flags & F_GAP) { if (!(T.seg[(i + T.n - 1) % T.n].flags & F_GAP)) gapEdge (T, A, L); return; }
		float tube = fmaxf (A.tube, B.tube);
		bool closed = A.tube > 0.985f && B.tube > 0.985f;
		bool tun = (A.flags & F_TUNNEL) != 0 && tube <= 0;
		bool start = (A.flags & F_START) || i == T.n - 1;
		bool seam = (i & 3) == 3;
		auto SP = [&] (const Seg &S, float l, float h, V3 *n = 0, V3 *lat = 0) { V3 p; Track::surfOf (S.c, S.r, S.u, S.tube, l, h, &p, n, lat); return p; };
		// the surface: plates across (more of them as it rolls round)
		int cols = 4 + (int) (tube * 16 + 0.5f);
		float ea = A.w * 0.5f - (closed ? 0 : 0.6f), eb = B.w * 0.5f - (closed ? 0 : 0.6f);
		for (int k = 0; k < cols; k++)
		{
			float f0 = -1 + 2.0f * (float) k / (float) cols, f1 = -1 + 2.0f * (float) (k + 1) / (float) cols;
			float g = (k == 0 && !closed) ? 0 : 0.05f, g1 = (k == cols - 1 && !closed) ? 0 : 0.05f;
			V3 na;
			V3 a0 = SP (A, f0 * ea + g, 0), a1 = SP (A, f1 * ea - g1, 0);
			V3 b0 = SP (B, f0 * eb + g, 0), b1 = SP (B, f1 * eb - g1, 0);
			V3 nm = SP (A, (f0 + f1) * 0.5f * ea, 0) - SP (A, (f0 + f1) * 0.5f * ea, -1);
			unsigned c = T.road[(i / 4) & 1];
			if (tube > 0.4f) c = (k % 5 == 0) ? rgbMix (T.road[0], T.rails[0], 0.75f) : T.road[(k & 1)];	// (a tube: stripes along it)
			if (seam) c = rgbScale (c, 0.88f);
			if (start) c = ((k + (i == T.n - 1 ? 1 : 0)) & 1) ? 0xFFFFFF : 0x202040;
			L.face (a0, a1, b1, b0, shade (c, nm), nm);
			if (k > 0 || closed)
			{
				V3 s0 = SP (A, f0 * ea - 0.05f, -0.02f), s1 = SP (A, f0 * ea + 0.05f, -0.02f);
				V3 t0 = SP (B, f0 * eb - 0.05f, -0.02f), t1 = SP (B, f0 * eb + 0.05f, -0.02f);
				L.face (s0, s1, t1, t0, rgbScale (T.road[0], 0.6f), nm);
			}
			if (!closed)
			{
				V3 u0 = SP (A, f0 * (ea + 0.6f), -0.5f), u1 = SP (A, f1 * (ea + 0.6f), -0.5f);
				V3 v0 = SP (B, f0 * (eb + 0.6f), -0.5f), v1 = SP (B, f1 * (eb + 0.6f), -0.5f);
				L.face (u0, u1, v1, v0, rgbScale (T.wall, 0.55f), -nm);
			}
		}
		// the light streaks: long thin glowing lines along the road (cyan and white), dashed
		if (!start)
		{
			static const float LANES[] = { -0.82f, -0.55f, -0.18f, 0.18f, 0.55f, 0.82f };
			for (int k = 0; k < 6; k++)
			{
				int ph = (i + k * 3) % 9;
				if (ph > 5 && k != 0 && k != 5) continue;
				float l = LANES[k];
				float wl = (k == 0 || k == 5) ? 0.14f : 0.07f;
				unsigned c = (k == 2 || k == 3) ? 0xFFFFFF : 0x22E6FF;
				if (tube > 0.4f) c = 0xFFFFFF;
				c = rgbScale (c, glowK * ((k == 0 || k == 5) ? 0.8f : 0.55f));
				V3 nm = A.u;
				G.face (SP (A, l * ea - wl, 0.03f), SP (A, l * ea + wl, 0.03f), SP (B, l * eb + wl, 0.03f), SP (B, l * eb - wl, 0.03f), c, nm);
			}
		}
		// a tube's white rings every 20 m
		if (tube > 0.4f && (i % 10) == 0)
			for (int k = 0; k < cols; k++)
			{
				float f0 = -1 + 2.0f * (float) k / (float) cols, f1 = -1 + 2.0f * (float) (k + 1) / (float) cols;
				V3 nm = SP (A, (f0 + f1) * 0.5f * ea, 0) - SP (A, (f0 + f1) * 0.5f * ea, -1);
				G.face (SP (A, f0 * ea, 0.04f), SP (A, f1 * ea, 0.04f), SP (B, f1 * eb, 0.04f) * 0.3f + SP (A, f1 * ea, 0.04f) * 0.7f, SP (B, f0 * eb, 0.04f) * 0.3f + SP (A, f0 * ea, 0.04f) * 0.7f, rgbScale (0xFFFFFF, 0.6f), nm);
			}
		if (closed) { pylon (T, A, i, L, G); return; }
		for (int side = -1; side <= 1; side += 2)
		{
			float s = (float) side;
			unsigned rail = T.rails[side < 0 ? 0 : 1];
			V3 nA, latA, nB, latB;
			V3 ai = SP (A, s * ea, 0), ao = SP (A, s * A.w * 0.5f, 0, &nA, &latA);
			V3 bi = SP (B, s * eb, 0), bo = SP (B, s * B.w * 0.5f, 0, &nB, &latB);
			// the edge: a bright strip in the rail's colour, white now and then
			L.face (ai, ao, bo, bi, ((i / 3) & 1) ? rail : rgbMix (rail, 0xFFFFFF, 0.5f), nA);
			// the rail: a low metal kerb, its top glowing in the rail's colour, chevrons on its side
			V3 outA = latA * s, outB = latB * s;
			V3 ci = ao + nA * 0.4f, co = ao + outA * 0.8f + nA * 0.4f;
			V3 di = bo + nB * 0.4f, dO = bo + outB * 0.8f + nB * 0.4f;
			L.face (ao, bo, di, ci, ((i & 3) == 0) ? rgbMix (rail, 0xFFFFFF, 0.3f) : shade (rgbScale (T.wall, 0.8f), -outA), -outA);
			L.face (ci, co, dO, di, rgbScale (T.wall, 0.75f), nA);
			V3 e0 = co - nA * 0.9f, e1 = dO - nB * 0.9f;
			L.face (co, dO, e1, e0, shade (rgbScale (T.wall, 0.7f), outA), outA);
			// the glowing tube along the rail's top
			{
				V3 m0 = (ci + co) * 0.5f + nA * 0.05f, m1 = (di + dO) * 0.5f + nB * 0.05f;
				G.face (m0 - outA * 0.18f, m0 + outA * 0.18f, m1 + outB * 0.18f, m1 - outB * 0.18f, rail, nA);
				G.face (m0, m1, m1 + nB * 0.25f, m0 + nA * 0.25f, rgbScale (rail, 0.7f), -outA);
			}
			// the force field over the rail: see-through, in the rail's colour, clearer toward its top (none in a tunnel)
			if (!tun && tube < 0.3f)
			{
				float wh = 1.6f;
				V3 w0 = (ci + co) * 0.5f, x0 = (di + dO) * 0.5f;
				unsigned gc = rgbMix (rail, 0xFFFFFF, 0.2f);
				Vtx *p;
				C.v.reserve (C.v.n + 6);
				p = C.v.push (); setVtx (*p, w0, 0, 0, gc, 110);
				p = C.v.push (); setVtx (*p, x0, 0, 0, gc, 110);
				p = C.v.push (); setVtx (*p, x0 + nB * wh, 0, 0, gc, 0);
				p = C.v.push (); setVtx (*p, w0, 0, 0, gc, 110);
				p = C.v.push (); setVtx (*p, x0 + nB * wh, 0, 0, gc, 0);
				p = C.v.push (); setVtx (*p, w0 + nA * wh, 0, 0, gc, 0);
				if ((i % 6) == 0)	// a post now and then: a slim pillar, a light on its top
				{
					V3 base = (ci + co) * 0.5f;
					box (L, base + nA * 1.6f, base, 0.13f, rgbScale (T.wall, 0.9f));
					V3 tip = base + nA * 1.7f;
					G.face (tip - outA * 0.25f - nA * 0.15f, tip + outA * 0.25f - nA * 0.15f, tip + outA * 0.25f + nA * 0.15f, tip - outA * 0.25f + nA * 0.15f, rail, -A.f);
				}
			}
		}
		pylon (T, A, i, L, G);
		// the tunnel: a tube of ribs, bright rings now and then
		if (tun)
		{
			const int K = 10;
			float ra = A.w * 0.5f + 1.8f, rb = B.w * 0.5f + 1.8f;
			bool rib = (i % 4) == 0;
			for (int k = 0; k < K; k++)
			{
				float a0 = PI * (float) k / K, a1 = PI * (float) (k + 1) / K;
				V3 p0 = A.c + A.r * (cosf (a0) * ra) + A.u * (sinf (a0) * ra * 0.6f);
				V3 p1 = A.c + A.r * (cosf (a1) * ra) + A.u * (sinf (a1) * ra * 0.6f);
				V3 q0 = B.c + B.r * (cosf (a0) * rb) + B.u * (sinf (a0) * rb * 0.6f);
				V3 q1 = B.c + B.r * (cosf (a1) * rb) + B.u * (sinf (a1) * rb * 0.6f);
				V3 inward = A.c - (p0 + p1) * 0.5f;
				if (rib) G.face (p0, q0, q1, p1, (k & 1) ? T.rails[1] : T.rails[0], inward);
				else L.face (p0, q0, q1, p1, shade ((k & 1) ? T.wall : rgbScale (T.wall, 0.85f), inward), inward);
				V3 o = (p0 - A.c) * 0.08f, oq = (q0 - B.c) * 0.08f, o1 = (p1 - A.c) * 0.08f, oq1 = (q1 - B.c) * 0.08f;
				L.face (p0 + o, q0 + oq, q1 + oq1, p1 + o1, shade (rgbScale (T.walls[0], 0.9f), -inward), -inward);
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
	// the anti-gravity pylons under a raised road: a slim column, glowing rings
	void pylon (const Track &T, const Seg &A, int i, VList &L, VList &G)
	{
		if (i % 14 != 0 || (A.flags & (F_LOOP | F_TWIST))) return;
		if (!strcmp (T.groundKind, "none")) return;
		V3 top = A.c - A.u * (A.tube > 0.5f ? 2 * TUBE_R : 1.6f);
		if (top.y - T.groundY < 3.0f) return;
		V3 bot (top.x, T.groundY, top.z);
		box (L, top, bot, 0.7f, rgbMix (T.walls[0], 0xFFFFFF, 0.2f));
		box (L, bot + V3 (0, 1.2f, 0), bot, 1.8f, rgbScale (T.walls[0], 0.8f));
		int k = 0;
		for (float y = bot.y + 4; y < top.y - 1; y += 6, k++) ring (G, V3 (top.x, y, top.z), 0.95f, rgbScale (T.rails[k & 1], glowK));
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

	// ---- the city: towers in the circuit's colours along the track, billboards, lamps, highways far off ----------------
	void city (const Track &T, ModelLib &lib, Rng &rng)
	{
		if (T.city <= 0) return;
		static const char *TOWERS[] = { "tower_setback", "tower_chamfer", "tower_round", "tower_stepped", "tower_twin", "tower_slim" };
		static const char *BOARDS[] = { "billboard_pole", "billboard_tall", "billboard_pole" };
		unsigned wall = T.walls[0], trim = T.walls[1];
		// towers: a row each side, every ~35 m / density, 25..90 m from the road
		int step = (int) (34.0f / (SEG * clampf (T.city, 0.2f, 1.5f)));
		for (int side = -1; side <= 1; side += 2)
			for (int i = rng.n (step); i < T.n; i += step + rng.n (3))
			{
				const Seg &S = T.seg[i];
				if (S.flags & (F_LOOP | F_TWIST)) continue;
				for (int row = 0; row < 2; row++)
				{
					float d = S.w * 0.5f + 22 + (float) row * 38 + rng.range (0, 22);
					V3 p = S.c + V3 (S.r.x, 0, S.r.z) * ((float) side * d);
					p.y = T.groundY;
					float rad = 12;
					if (!clearOfRoad (T, p, rad + 6)) continue;
					const char *nm = TOWERS[rng.n (6)];
					unsigned w = rgbScale (wall, rng.range (0.85f, 1.12f));
					const Mesh *m = lib.get (nm, w, trim, 0);
					float hs = rng.range (0.6f, 1.5f) * (row ? 1.25f : 1.0f), ws = rng.range (0.85f, 1.2f);
					float yaw = atan2f (S.f.x, S.f.z) + (float) rng.n (4) * PI / 2;
					place (m, at (p, yaw, ws, hs, ws), i / CHUNK);
					// an antenna on some
					if (rng.f () < 0.25f && m) place (lib.get ("antenna_mast", 0xFF000000u, 0xFF000000u, 0), at (p + V3 (0, (m->hi.y) * hs, 0), yaw, 1, 1, 1), i / CHUNK);
				}
			}
		// billboards facing the road, every ~70 m, the sides in turn
		int bstep = (int) (70 / SEG);
		for (int i = bstep / 2, k = 0; i < T.n; i += bstep, k++)
		{
			const Seg &S = T.seg[i];
			if (S.flags & (F_LOOP | F_TWIST | F_TUNNEL) || S.tube > 0) continue;
			float side = (k & 1) ? 1.0f : -1.0f;
			float d = S.w * 0.5f + 7 + rng.range (0, 5);
			V3 p = S.c + V3 (S.r.x, 0, S.r.z) * (side * d);
			p.y = fminf (S.c.y - 1.5f, T.groundY + 30);
			if (S.c.y - T.groundY < 12) p.y = T.groundY;
			if (!clearOfRoad (T, p, 4)) continue;
			// facing the coming ships: turned toward the road and back up the track
			V3 face = norm (V3 (-S.f.x, 0, -S.f.z) * 0.8f + V3 (-S.r.x, 0, -S.r.z) * side * 0.6f);
			float yaw = atan2f (face.x, face.z);
			const Mesh *m = lib.get (BOARDS[k % 3], rgbScale (wall, 0.8f), T.neon[k % 3]);
			place (m, at (p, yaw, 1, 1, 1), i / CHUNK, T.neon[k % 3]);
		}
		// street lamps along the rails
		int lstep = (int) (36 / SEG);
		for (int i = 0, k = 0; i < T.n; i += lstep, k++)
		{
			const Seg &S = T.seg[i];
			if (S.flags & (F_LOOP | F_TWIST | F_TUNNEL | F_GAP) || S.tube > 0 || S.c.y - T.groundY > 6) continue;
			float side = (k & 1) ? 1.0f : -1.0f;
			V3 p = S.c + S.r * (side * (S.w * 0.5f + 2.2f)) - S.u * 0.4f;
			float yaw = atan2f (-S.r.x * side, -S.r.z * side);
			place (lib.get ("street_lamp", 0xFF000000u, 0xFF000000u, 0), at (p, yaw, 1, 1, 1), i / CHUNK);
		}
		// the far city: a ring of towers beyond, and highways
		float cx = (T.lo.x + T.hi.x) * 0.5f, cz = (T.lo.z + T.hi.z) * 0.5f;
		float R = fmaxf (T.hi.x - T.lo.x, T.hi.z - T.lo.z) * 0.5f + 140;
		for (int k = 0; k < 40; k++)
		{
			float a = 2 * PI * (float) k / 40 + rng.range (-0.05f, 0.05f), rr = R + rng.range (0, 120);
			V3 p (cx + cosf (a) * rr, T.groundY, cz + sinf (a) * rr);
			const Mesh *m = lib.get (TOWERS[rng.n (6)], rgbScale (wall, rng.range (0.8f, 1.05f)), trim, 0);
			float hs = rng.range (1.2f, 2.6f);
			place (m, at (p, rng.range (0, 6.28f), 1.4f, hs, 1.4f), -1);
		}
		for (int k = 0; k < 18; k++)
		{
			float a = 2 * PI * (float) k / 18;
			V3 p (cx + cosf (a) * (R - 40), T.groundY, cz + sinf (a) * (R - 40));
			place (lib.get ("highway_straight", rgbScale (wall, 0.9f), trim, 0), at (p, -a, 1, 1, 1.05f * (2 * PI * (R - 40) / 18) / 40), -1);
		}
		// the traffic: ships flying round
		for (int k = 0; k < 10 && ntraffic < 16; k++)
		{
			Traffic &F = traffic[ntraffic++];
			F.c = V3 (cx + rng.range (-60, 60), 0, cz + rng.range (-60, 60)); F.r = R * rng.range (0.3f, 0.9f); F.h = T.groundY + rng.range (35, 110);
			F.speed = rng.range (0.05f, 0.14f) * (rng.f () < 0.5f ? -1 : 1); F.ph = rng.range (0, 6.28f); F.col = T.neon[k % 3];
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

	// ---- the ground ----------------------------------------------------------------------------------------------------
	void ground (const Track &T, ModelLib &lib, Rng &rng)
	{
		(void) lib;
		const char *kind = T.groundKind;
		if (!strcmp (kind, "none")) return;
		float cx = (T.lo.x + T.hi.x) * 0.5f, cz = (T.lo.z + T.hi.z) * 0.5f;
		float R = fmaxf (T.hi.x - T.lo.x, T.hi.z - T.lo.z) * 0.5f + 420;
		bool water = !strcmp (kind, "water"), cityG = !strcmp (kind, "city"), clouds = !strcmp (kind, "clouds");
		const float CELL = cityG ? 40 : 60;
		int N = (int) (2 * R / CELL) + 1;
		float y = T.groundY;
		for (int a = 0; a < N; a++)
			for (int b = 0; b < N; b++)
			{
				float x0 = cx - R + (float) a * CELL, z0 = cz - R + (float) b * CELL;
				float dx = x0 + CELL / 2 - cx, dz = z0 + CELL / 2 - cz;
				if (dx * dx + dz * dz > R * R * 1.1f) continue;
				unsigned c = T.ground[(a + b) & 1];
				if (water) c = rgbScale (T.ground[0], 1.0f + rng.range (-0.05f, 0.05f));
				else c = rgbScale (c, 1.0f + rng.range (-0.04f, 0.04f));
				float in = cityG ? 1.5f : 0;		// (the streets between the city's blocks)
				far.face (V3 (x0 + in, y, z0 + CELL - in), V3 (x0 + CELL - in, y, z0 + CELL - in), V3 (x0 + CELL - in, y, z0 + in), V3 (x0 + in, y, z0 + in), shade (c, V3 (0, 1, 0)), V3 (0, 1, 0));
				if (cityG)
				{
					unsigned st = rgbScale (T.ground[0], 0.6f);
					far.face (V3 (x0, y - 0.05f, z0 + CELL), V3 (x0 + CELL, y - 0.05f, z0 + CELL), V3 (x0 + CELL, y - 0.05f, z0), V3 (x0, y - 0.05f, z0), st, V3 (0, 1, 0));
					// the streets' lights (at night: lines of glow)
					if (glowK > 0.6f && ((a + 2 * b) % 3) == 0)
						farGlow.face (V3 (x0, y + 0.05f, z0 + 0.3f), V3 (x0 + CELL, y + 0.05f, z0 + 0.3f), V3 (x0 + CELL, y + 0.05f, z0 - 0.3f), V3 (x0, y + 0.05f, z0 - 0.3f), rgbScale (T.neon[(a + b) % 3], 0.5f), V3 (0, 1, 0));
				}
				if (water && rng.f () < 0.25f)
				{
					// sparkles on the water
					float sx = x0 + rng.range (0, CELL), sz = z0 + rng.range (0, CELL), l = rng.range (2, 6);
					farGlow.face (V3 (sx - l, y + 0.05f, sz + 0.25f), V3 (sx + l, y + 0.05f, sz + 0.25f), V3 (sx + l, y + 0.05f, sz - 0.25f), V3 (sx - l, y + 0.05f, sz - 0.25f), rgbScale (T.neon[1], 0.35f * glowK + 0.15f), V3 (0, 1, 0));
				}
			}
		// the edge of the world: hills / dunes / ice / cloud banks in the horizon's colour
		if (!cityG && !water)
			for (int k = 0; k < 30; k++)
			{
				float a = 2 * PI * (float) k / 30 + rng.range (-0.05f, 0.05f), rr = R * rng.range (0.8f, 0.97f);
				V3 c (cx + cosf (a) * rr, y, cz + sinf (a) * rr);
				float hh = rng.range (30, 90) * (clouds ? 0.4f : 1), ww = rng.range (50, 110);
				mound (far, c, ww, hh, rgbMix (T.ground[0], T.sky[1], rng.range (0.2f, 0.5f)));
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
