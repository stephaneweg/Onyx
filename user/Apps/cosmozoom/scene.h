//
// scene.h -- a race's pictures: each player's camera behind its ship, the world's pieces in front of it, the
// ships (their models built from the parts, painted, lit and outlined; leaning, bobbing, squashed when they
// land), their engines' flames and trails, their shadows, the item boxes turning, the speed pads' arrows, the
// bubbles, the goo and the springs, the shields, the sparkles. Everything made into one Frame for the GPU.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef CZ_SCENE_H
#define CZ_SCENE_H

#include "race.h"
#include "world.h"
#include "textures.h"

namespace cz {

// ---- a ship's model, from its parts ----------------------------------------------------------------------------------
struct ShipModel
{
	Mesh mesh;
	enum { TRAIL = 14 };
	V3 trail[6][TRAIL]; int ntrail = 0; float trailT = 0;	// the exhausts' last places (the ribbons)
	V3 flames[6]; int nflame = 0;	// where the exhausts are (model space)
	V3 arc[2]; bool hasArc = false;	// the two ends of the energy arc between the engines (podracer pods)
	V3 pads[4]; int npad = 0;	// the hover pads under the hull (a soft glow)
	float length = 3;
};

static unsigned hueColor (float h)
{
	h -= floorf (h); h *= 6;
	int i = (int) h; float f = h - (float) i;
	float r, g, b;
	switch (i)
	{
	case 0: r = 1; g = f; b = 0; break;
	case 1: r = 1 - f; g = 1; b = 0; break;
	case 2: r = 0; g = 1; b = f; break;
	case 3: r = 0; g = 1 - f; b = 1; break;
	case 4: r = f; g = 0; b = 1; break;
	default: r = 1; g = 0; b = 1 - f; break;
	}
	return col01 (0.35f + 0.65f * r, 0.35f + 0.65f * g, 0.35f + 0.65f * b);
}

// The paint of a triangle: its own colour, or the player's by the pattern
static unsigned paintOf (const Tri &T, const Build &b, const Mesh &m)
{
	if (T.paint != 1 && T.paint != 2) return T.base;
	unsigned c1 = PAINTS[clampi (b.paint, 0, NPAINT - 1)], c2 = PAINTS[clampi (b.paint2, 0, NPAINT - 1)];
	if (T.paint == 2) return c2;
	V3 c = (T.p[0] + T.p[1] + T.p[2]) * (1.0f / 3);
	V3 sz = m.hi - m.lo;
	float u = sz.z > 0 ? (c.z - m.lo.z) / sz.z : 0, v = sz.y > 0 ? (c.y - m.lo.y) / sz.y : 0, w = sz.x > 0 ? (c.x - m.lo.x) / sz.x : 0;
	// the patterns touch the main colour only, in touches: the model's own parts in the accent stay its accent,
	// the main colour stays the main one (the user, 2026-10-15)
	switch (b.pattern)
	{
	case 1: return fabsf (v - 0.55f) < 0.06f ? c2 : c1;				// a thin stripe along
	case 2: return v < 0.22f ? c2 : c1;						// two-tone: the underside
	case 3: return fabsf (w - 0.5f) < 0.06f && v > 0.5f ? c2 : c1;			// a racing stripe on top
	case 4: return u > 0.85f && (((int) (u * 24) + (int) (w * 8)) & 1) ? c2 : c1;	// checks on the nose
	case 5: return rgbMix (c1, hueColor (u * 0.85f), 0.45f);				// rainbow, over the main colour
	case 6: return w < 0.5f && v > 0.5f ? rgbMix (c1, c2, 0.35f) : c1;		// split: a tinted half on top
	}
	return c1;
}

static void buildShip (ShipModel &S, ModelLib &lib, const Build &b, int pilot)
{
	S.mesh.tris.clear (); S.mesh.ngroups = 0; S.mesh.npt = 0; S.nflame = 0; S.hasArc = false; S.npad = 0;
	// (the parts raw: painted and lit together below)
	const Mesh *hull = lib.rawMesh (HULLS[clampi (b.hull, 0, NHULL - 1)].model);
	const char *wm = WINGS[clampi (b.wing, 0, NWING - 1)].model;
	const Mesh *wing = *wm ? lib.rawMesh (wm) : 0;		// ("No Fins": no model)
	const Mesh *eng = lib.rawMesh (ENGINES[clampi (b.engine, 0, NENGINE - 1)].model);
	const Mesh *pil = lib.rawMesh (PILOTS[clampi (pilot, 0, NPILOT - 1)].model);
	Mesh raw;
	if (hull) appendMesh (raw, *hull, M4::identity ());
	else
	{
		// (no model: a simple wedge, so that the game runs without its files)
		static Mesh wedge; static bool made = false;
		if (!made)
		{
			made = true;
			V3 P[6] = { V3 (0, 0.3f, 1.6f), V3 (-0.9f, 0, -1.2f), V3 (0.9f, 0, -1.2f), V3 (0, 0.8f, -1.0f), V3 (-0.9f, 0.4f, -1.4f), V3 (0.9f, 0.4f, -1.4f) };
			int F[][3] = { { 0, 2, 1 }, { 0, 3, 2 }, { 0, 1, 3 }, { 1, 2, 5 }, { 1, 5, 4 }, { 3, 4, 5 }, { 1, 4, 3 }, { 2, 3, 5 } };
			for (auto &f : F) { Tri *t = wedge.tris.push (); for (int k = 0; k < 3; k++) { t->p[k] = P[f[k]]; t->col[k] = 0xC0C0C0; } t->base = 0xC0C0C0; t->alpha = 255; t->paint = 1; t->group = -1; }
			wedge.bounds ();
		}
		appendMesh (raw, wedge, M4::identity ());
	}
	V3 at;
	if (wing)
	{
		V3 w = hull && hull->point ("wing", &at) ? at : V3 (0.6f, 0.2f, -0.3f);
		appendMesh (raw, *wing, M4::translate (w));
		appendMesh (raw, *wing, M4::translate (V3 (-w.x, w.y, w.z)) * M4::scale (-1, 1, 1));
	}
	// a hull with its engines built in ("noengine": the engine part only counts in the stats, the flames leave the
	// hull's own exhausts) or an opaque canopy ("nopilot": no pilot drawn) -- the ships made after the user's models
	V3 tmp;
	bool builtIn = hull && hull->point ("noengine", &tmp);
	if (hull && hull->point ("nopilot", &tmp)) pil = 0;
	if (eng && !builtIn)
	{
		V3 e = hull && hull->point ("engine", &at) ? at : V3 (0, 0.3f, -1.3f);
		appendMesh (raw, *eng, M4::translate (e));
		for (int i = 0; i < eng->npt && S.nflame < 6; i++)
			if (!strncmp (eng->ptName[i], "flame", 5)) S.flames[S.nflame++] = eng->pt[i] + e;
		V3 a1, a2;
		S.hasArc = eng->point ("arc1", &a1) && eng->point ("arc2", &a2);
		if (S.hasArc) { S.arc[0] = a1 + e; S.arc[1] = a2 + e; }
	}
	if (pil)
	{
		V3 p = hull && hull->point ("pilot", &at) ? at : V3 (0, 0.6f, 0.2f);
		appendMesh (raw, *pil, M4::translate (p));
	}
	if (hull) for (int i = 0; i < hull->npt && S.nflame < 6; i++) if (!strncmp (hull->ptName[i], "flame", 5)) S.flames[S.nflame++] = hull->pt[i];
	if (hull) for (int i = 0; i < hull->npt && S.npad < 4; i++) if (!strncmp (hull->ptName[i], "pad", 3)) S.pads[S.npad++] = hull->pt[i];
	if (!S.nflame) S.flames[S.nflame++] = V3 (0, 0.35f, raw.lo.z);
	raw.bounds ();
	for (int i = 0; i < raw.tris.n; i++)
	{
		Tri &T = raw.tris[i];
		unsigned c = paintOf (T, b, raw);
		if (T.paint) { T.base = c; for (int k = 0; k < 3; k++) T.col[k] = c; }
	}
	lightShip (raw);
	if (lib.outline) addOutline (raw, 0.045f);
	appendMesh (S.mesh, raw, M4::identity ());
	S.length = raw.hi.z - raw.lo.z;
}

// ---- the Time Trial's ghost: the best race's places, every 0.1 s -------------------------------------------------------------
struct Ghost
{
	enum { MAX = 8000 };
	struct Sample { float s, l, h, ya; };
	Sample *rec = 0; int n = 0;		// what this race does
	Sample *best = 0; int nbest = 0;	// the best one's (shown)
	Build build; int pilot = 0; bool show = false;
	ShipModel model;
	void alloc () { if (!rec) { rec = (Sample *) calloc (MAX, sizeof (Sample)); best = (Sample *) calloc (MAX, sizeof (Sample)); } }
	void record (const Craft &C, float t) { if (!rec || t < 0) return; int k = (int) (t * 10); if (k < MAX && k >= n) { for (int j = n; j <= k; j++) rec[j] = Sample { C.s, C.l, C.h, C.ya }; n = k + 1; } }
	bool at (float t, Sample *o) const
	{
		if (!show || nbest < 2 || t < 0) return false;
		float f = t * 10; int k = (int) f; if (k + 1 >= nbest) return false;
		float q = f - (float) k; const Sample &a = best[k], &b = best[k + 1];
		o->s = a.s + (b.s - a.s) * q; if (fabsf (b.s - a.s) > 100) o->s = b.s;
		o->l = a.l + (b.l - a.l) * q; o->h = a.h + (b.h - a.h) * q; o->ya = a.ya + (b.ya - a.ya) * q;
		return true;
	}
};

// ---- the sparkles -----------------------------------------------------------------------------------------------------
struct Particle { V3 p, v; float life, max, size, grav; unsigned col; };
struct Particles
{
	enum { MAX = 700 };
	Particle p[MAX]; int n = 0;
	void add (const V3 &at, const V3 &v, float life, float size, unsigned col, float grav = 0)
	{
		if (n >= MAX) { int k = (int) (at.x * 13 + at.z * 7) & (MAX - 1); if (k >= MAX) k = 0; p[k] = Particle { at, v, life, life, size, grav, col }; return; }
		p[n++] = Particle { at, v, life, life, size, grav, col };
	}
	void step (float dt)
	{
		for (int i = 0; i < n; )
		{
			Particle &q = p[i];
			q.life -= dt;
			if (q.life <= 0) { p[i] = p[--n]; continue; }
			q.v.y -= q.grav * dt; q.p += q.v * dt; q.v *= 1.0f - 1.5f * dt;
			i++;
		}
	}
	void clear () { n = 0; }
};

// ---- a camera ---------------------------------------------------------------------------------------------------------
struct Camera
{
	V3 eye, at, up; float fov = 1.15f; bool init = false;
	float look = 0;			// > 0: looking back
	float camL = 0, camH = 0, camBack = 9;
	void follow (const Race &R, int i, float dt)
	{
		const Craft &C = R.c[i];
		const Track &T = *R.T;
		V3 p, u; T.surf (C.s, C.l, C.h, &p, &u);
		// along the track: tied to the ship; across and up: following it softly
		float wantBack = 7.5f + C.v * 0.025f + (C.boostT > 0 ? 1.0f : 0);
		if (!init) { camL = C.l; camH = C.h; camBack = wantBack; }
		camL += T.wrapL (C.s, C.l - camL) * clampf (dt * 6, 0, 1);
		camH += (C.h - camH) * clampf (dt * 5, 0, 1);
		camBack += (wantBack - camBack) * clampf (dt * 3, 0, 1);
		V3 want = T.pointAt (C.s - camBack, camL, camH * 0.7f + 3.5f);
		V3 wantAt = T.pointAt (C.s + 9, camL * 0.5f + C.l * 0.5f, C.h * 0.8f + 1.3f);
		if (look > 0) { want = T.pointAt (C.s + 7, C.l, C.h + 2.2f); wantAt = T.pointAt (C.s - 6, C.l, C.h + 0.8f); }
		if (!init || look > 0) { eye = want; at = wantAt; up = u; init = true; }
		eye = want; at = wantAt;
		up = norm (lerp (up, u, clampf (dt * 6, 0, 1)));
		float wantFov = 1.15f + clampf (C.v / 50, 0, 1) * 0.12f + (C.boostT > 0 || C.warpT > 0 ? 0.12f : 0);
		fov = lerpf (fov, wantFov, clampf (dt * 4, 0, 1));
	}
	// the camera turning around a ship (the start, the end)
	void orbit (const Race &R, int i, float t)
	{
		const Craft &C = R.c[i];
		V3 p = R.T->pointAt (C.s, C.l, C.h + 0.8f);
		V3 c, f, r, u; R.T->frameAt (C.s, &c, &f, &r, &u);
		float a = 0.6f + t * 0.9f;
		eye = p + f * (cosf (a) * 8.5f) + r * (sinf (a) * 8.5f) + u * (2.2f + t * 0.3f);
		at = p; up = u; fov = 1.0f; init = false;
	}
};

// ---- the scene ----------------------------------------------------------------------------------------------------------
struct Scene
{
	World world;
	ModelLib lib;
	ShipModel ship[MAXCRAFT];
	Ghost ghost;
	Particles fx;
	const Mesh *mBox = 0, *mBubble = 0, *mGoo = 0, *mSpring = 0, *mTraffic = 0, *mStar = 0, *mNebula = 0, *mWarp = 0;
	float time = 0;

	void load (const Track &T, unsigned seed) { world.build (T, lib, seed); fx.clear (); mBox = lib.get ("itembox"); mBubble = lib.get ("bubble"); mGoo = lib.get ("goo"); mSpring = lib.get ("spring"); mTraffic = lib.get ("traffic_ship"); mStar = lib.get ("starbit"); mNebula = lib.get ("nebula"); mWarp = lib.get ("warpstar"); }
	void ships (const Race &R) { for (int i = 0; i < R.n; i++) buildShip (ship[i], lib, R.c[i].build, R.c[i].pilot); }

	// the matrix of craft i (model -> world)
	M4 shipMatrix (const Race &R, int i) const
	{
		const Craft &C = R.c[i];
		float hover = 0.6f + sinf (C.bob) * 0.06f;
		V3 p, u, r, f;
		R.T->surf (C.s, C.l, C.h + hover, &p, &u, &r, &f);
		f = norm (f - u * dot (f, u));
		float yaw = C.ya + C.spin + (C.driftDir ? (float) C.driftDir * 0.32f : 0);
		V3 fw = f * cosf (yaw) + r * sinf (yaw);
		V3 rt = r * cosf (yaw) - f * sinf (yaw);
		float lean = clampf (C.yawRate * 0.25f, -0.44f, 0.44f) + (C.driftDir ? (float) C.driftDir * 0.18f : 0) + sinf (C.bob * 0.5f) * 0.05f;
		V3 rt2 = rt * cosf (lean) - u * sinf (lean), up2 = u * cosf (lean) + rt * sinf (lean);
		float pitch = clampf (C.vh * 0.03f, -0.3f, 0.3f);
		V3 fw2 = fw * cosf (pitch) + up2 * sinf (pitch); up2 = up2 * cosf (pitch) - fw * sinf (pitch);
		float sq = C.squash;
		// (the model's +X is the ship's left: -right)
		return M4::frame (-rt2, up2, fw2, p, 1 + sq * 0.12f, 1 - sq * 0.2f, 1 + sq * 0.12f);
	}

	void emitFor (const Race &R, float dt)
	{
		time += dt;
		for (int e = 0; e < R.nev; e++)
		{
			const Event &E = R.ev[e];
			switch (E.kind)
			{
			case EV_BOX:
				for (int k = 0; k < 18; k++) { float a = (float) k * 0.35f; fx.add (E.at, V3 (cosf (a) * 6, 3 + (float) (k % 5), sinf (a) * 6), 0.7f, 0.35f, hueColor ((float) k / 18), 8); }
				break;
			case EV_HIT: case EV_POP: case EV_BLOCKED:
				for (int k = 0; k < 16; k++) { float a = (float) k * 0.39f; fx.add (E.at + V3 (0, 1, 0), V3 (cosf (a) * 5, sinf (a * 3) * 3 + 2, sinf (a) * 5), 0.6f, 0.4f, E.kind == EV_BLOCKED ? 0x5CF2C8 : 0x9FE8FF, 4); }
				break;
			case EV_WALL: case EV_BUMP:
				for (int k = 0; k < 8; k++) fx.add (E.at + V3 (0, 0.6f, 0), V3 ((float) (k - 4), 2 + (float) (k & 3), (float) ((k * 5) % 7 - 3)), 0.35f, 0.22f, 0xFFF4A0, 10);
				break;
			case EV_FINISH:
				for (int k = 0; k < 60; k++) { float a = (float) k * 0.7f; fx.add (E.at + V3 (0, 2, 0), V3 (cosf (a) * 8, 6 + (float) (k % 7), sinf (a) * 8), 2.0f, 0.35f, hueColor ((float) k * 0.13f), 9); }
				break;
			case EV_MINITURBO: case EV_BOOSTPAD:
				for (int k = 0; k < 10; k++) fx.add (E.at + V3 (0, 0.6f, 0), V3 ((float) (k - 5) * 0.6f, 1.5f, (float) ((k * 3) % 5 - 2)), 0.45f, 0.3f, E.arg == 2 ? 0xFF7AE0 : 0x7FE8FF, 2);
				break;
			}
		}
		// the trails' ribbons (a point every 30 ms) and the slide's sparks
		for (int i = 0; i < R.n; i++)
		{
			const Craft &C = R.c[i];
			M4 M = shipMatrix (R, i);
			ShipModel &S = ship[i];
			S.trailT += dt;
			if (S.trailT >= 0.03f || S.ntrail == 0)
			{
				S.trailT = 0;
				for (int k = 0; k < S.nflame; k++)
				{
					for (int j = ShipModel::TRAIL - 1; j > 0; j--) S.trail[k][j] = S.trail[k][j - 1];
					S.trail[k][0] = M.point (S.flames[k]);
					if (S.ntrail == 0) for (int j = 1; j < ShipModel::TRAIL; j++) S.trail[k][j] = S.trail[k][0];
				}
				S.ntrail = ShipModel::TRAIL;
			}
			else for (int k = 0; k < S.nflame; k++) S.trail[k][0] = M.point (S.flames[k]);
			if (C.v < 5) continue;
			if (C.driftDir)
			{
				unsigned col = C.driftCharge > 2.2f ? 0xFF6FD8 : C.driftCharge > 1.0f ? 0x4FC3FF : 0xFFF4A0;
				V3 p = M.point (V3 ((float) C.driftDir * 0.9f, 0, -1.0f));
				fx.add (p, V3 ((float) ((int) (time * 97) % 5 - 2), 2.5f, 0), 0.3f, 0.2f, col, 9);
			}
		}
		fx.step (dt);
	}

	// ---- one view -----------------------------------------------------------------------------------------------------
	int nearIdx[World::MAXCH], nearN = 0;		// the chunks drawn whole this frame (their glows, glass, signs too)
	void view (Frame &F, const Race &R, const Camera &cam, int w, int h, int me)
	{
		const Track &T = *R.T;
		M4 P = M4::perspective (cam.fov, (float) w / (float) (h > 0 ? h : 1), 0.4f, 1600);
		M4 V = M4::lookAt (cam.eye, cam.at, cam.up);
		M4 VP = P * V;
		M4 Vs = V; Vs.m[3] = Vs.m[7] = Vs.m[11] = 0;
		F.begin (P * Vs, 0, B_NOZWRITE | B_ZALWAYS); F.add (world.sky); F.end ();
		if (world.skyGlow.v.n) { F.begin (P * Vs, TX_GLOW, B_NOZWRITE | B_ZALWAYS | B_ADD | B_LINEAR | B_CLAMP); F.add (world.skyGlow); F.end (); }
		if (world.skyObj.v.n) { F.begin (P * Vs, 0, B_NOZWRITE | B_ZALWAYS | B_CULL); F.add (world.skyObj); F.end (); }
		if (world.skyBand.v.n) { F.begin (P * Vs, TX_CLOUDS, B_ALPHA | B_NOZWRITE | B_ZALWAYS | B_LINEAR); F.add (world.skyBand); F.end (); }
		// the world's pieces in front
		V3 fwd = norm (cam.at - cam.eye);
		auto visible = [&] (const VList &L) -> bool
		{
			if (!L.v.n) return false;
			V3 d = L.centre - cam.eye;
			float dist = len (d);
			if (dist - L.radius > 480) return false;
			if (dot (d, fwd) < -L.radius) return false;
			float cl[4]; VP.clip (L.centre, cl);
			if (cl[3] > 0)
			{
				float k = L.radius * 1.8f;
				if (cl[0] > cl[3] + k || cl[0] < -cl[3] - k || cl[1] > cl[3] + k || cl[1] < -cl[3] - k) return false;
			}
			return true;
		};
		// the chunks nearest first, as many as the GPU takes in one frame (KAPI_GPU_MAX_VERTS for everything: the
		// rest -- ships, glows, trails -- keeps its share); the farthest left out past it
		F.begin (VP, 0, B_CULL);
		F.add (world.far);
		{
			static int ord[World::MAXCH]; static float od[World::MAXCH]; int no = 0;
			for (int i = 0; i < world.nch; i++)
				if (visible (world.chunk[i])) { ord[no] = i; od[no] = len (world.chunk[i].centre - cam.eye); no++; }
			for (int a = 1; a < no; a++) { int ki = ord[a]; float kd = od[a]; int b = a - 1; while (b >= 0 && od[b] > kd) { ord[b + 1] = ord[b]; od[b + 1] = od[b]; b--; } ord[b + 1] = ki; od[b + 1] = kd; }
			// near: the whole chunk; past LOD_NEAR its simplified one (the road a strip, the props boxes)
			const int BUDGET = 112000;	// (of KAPI_GPU_MAX_VERTS 196608: the ships, the glows, the trails after)
			const float LOD_NEAR = 130;
			nearN = 0;
			for (int a = 0; a < no; a++)
			{
				const VList &L = od[a] - world.chunk[ord[a]].radius * 0.5f < LOD_NEAR ? world.chunk[ord[a]] : world.chunkLow[ord[a]];
				if (F.v.n + L.v.n > BUDGET) break;
				F.add (L);
				if (&L == &world.chunk[ord[a]] && nearN < World::MAXCH) nearIdx[nearN++] = ord[a];
			}
		}
		F.end ();
		// the traffic in the sky: ships flying round
		if (mTraffic)
			for (int k = 0; k < world.ntraffic; k++)
			{
				const World::Traffic &Tr = world.traffic[k];
				float a = Tr.ph + time * Tr.speed;
				V3 p = Tr.c + V3 (cosf (a) * Tr.r, 0, sinf (a) * Tr.r); p.y = Tr.h + sinf (time * 0.7f + (float) k) * 3;
				V3 fw = norm (V3 (-sinf (a), 0, cosf (a)) * (Tr.speed > 0 ? 1.0f : -1.0f));
				M4 M = M4::frame (norm (cross (V3 (0, 1, 0), fw)), V3 (0, 1, 0), fw, p, 3, 3, 3);
				F.begin (VP * M, 0, B_CULL); F.mesh (*mTraffic, M4::identity ()); F.end ();
				F.begin (VP * M, 0, B_ADD | B_NOZWRITE); F.mesh (*mTraffic, M4::identity (), 255, 0, 2); F.end ();
			}
		// the ships
		for (int i = 0; i < R.n; i++)
		{
			const Craft &C = R.c[i];
			M4 M = shipMatrix (R, i);
			if (dot (M.point (V3 ()) - cam.eye, fwd) < -5) continue;
			unsigned add = 0;
			if (C.warpT > 0) add = rgbScale (hueColor (time * 1.5f), 0.45f);
			else if (C.invulT > 0 && ((int) (time * 12) & 1)) add = 0x505050;
			else if (C.boostT > 0) add = 0x201008;
			F.begin (VP * M, 0, B_CULL);
			F.mesh (ship[i].mesh, M4::identity (), 255, add);
			F.end ();
		}
		// the shadows, the moving things
		F.begin (VP, 0, B_ALPHA | B_NOZWRITE);
		for (int i = 0; i < R.n; i++)
		{
			const Craft &C = R.c[i];
			V3 p, u, r, f; T.surf (C.s, C.l, 0.06f, &p, &u, &r, &f);
			// a soft round shadow: a fan, dark under the hull, clear at its rim (the ship's length and width)
			float lift = 1 + C.h * 0.25f, a = 165 / (1 + C.h * 0.3f);
			float hl = fmaxf (ship[i].length, 2.5f) * 0.55f * lift, hw = hl * 0.62f;
			const int K = 16;
			Vtx *v = F.room (K * 3); if (!v) continue;
			for (int k = 0; k < K; k++)
			{
				float a0 = 2 * PI * (float) k / K, a1 = 2 * PI * (float) (k + 1) / K;
				setVtx (v[0], p, 0, 0, 0x1A1030, (unsigned) a);
				setVtx (v[1], p + f * (cosf (a1) * hl) + r * (sinf (a1) * hw), 0, 0, 0x1A1030, 0);
				setVtx (v[2], p + f * (cosf (a0) * hl) + r * (sinf (a0) * hw), 0, 0, 0x1A1030, 0);
				v += 3;
			}
		}
		F.end ();
		dynamic (F, R, VP);
		// the ghost: the best race's ship, see-through
		{
			Ghost::Sample g;
			if (ghost.at (R.time, &g))
			{
				V3 p, u, r, f; T.surf (g.s, g.l, g.h + 0.6f, &p, &u, &r, &f);
				f = norm (f - u * dot (f, u));
				V3 fw = f * cosf (g.ya) + r * sinf (g.ya), rt = r * cosf (g.ya) - f * sinf (g.ya);
				M4 M = M4::frame (-rt, u, fw, p);
				F.begin (VP * M, 0, B_ALPHA | B_CULL); F.mesh (ghost.model.mesh, M4::identity (), 90, 0x203040); F.end ();
			}
		}
		// the force fields, the ships' glass
		F.begin (VP, 0, B_ALPHA | B_NOZWRITE);
		for (int j = 0; j < nearN; j++) F.add (world.clear[nearIdx[j]]);
		F.end ();
		for (int i = 0; i < R.n; i++)
		{
			M4 M = shipMatrix (R, i);
			F.begin (VP * M, 0, B_ALPHA | B_NOZWRITE | B_CULL); F.mesh (ship[i].mesh, M4::identity (), 255, 0, 1); F.end ();
		}
		boxGlass (F, R, VP);
		// what glows: the world's lights, the flames, the sparkles, the shields
		for (int i = 0; i < R.n; i++)
		{
			M4 M = shipMatrix (R, i);
			F.begin (VP * M, 0, B_ADD | B_NOZWRITE); F.mesh (ship[i].mesh, M4::identity (), 255, 0, 2); F.end ();
		}
		F.begin (VP, TX_SIGNS, B_LINEAR | B_CLAMP);
		for (int j = 0; j < nearN; j++) F.add (world.signs[nearIdx[j]]);
		F.end ();
		F.begin (VP, 0, B_ADD | B_NOZWRITE);
		F.add (world.farGlow);
		for (int j = 0; j < nearN; j++) F.add (world.glow[nearIdx[j]]);
		F.end ();
		F.begin (VP, TX_CHEVRON, B_ADD | B_NOZWRITE | B_LINEAR);
		boostPads (F, R, cam);
		F.end ();
		F.begin (VP, 0, B_ADD | B_NOZWRITE);
		V3 cr = norm (cross (fwd, cam.up)), cu = norm (cross (cr, fwd));
		for (int i = 0; i < R.n; i++)
		{
			const Craft &C = R.c[i];
			const ShipModel &S = ship[i];
			if (C.v < 4 || !S.ntrail) continue;
			unsigned col = C.warpT > 0 ? hueColor (time * 2 + (float) i * 0.1f) : C.boostT > 0 ? 0xFF8A3C : PAINTS[clampi (C.build.paint, 0, NPAINT - 1)];
			if (col == PAINTS[9]) col = 0x9FE8FF;			// (a white ship: a pale cyan trail)
			float wdt = C.boostT > 0 || C.warpT > 0 ? 0.22f : 0.11f;
			for (int k = 0; k < S.nflame; k++)
				for (int j = 0; j + 1 < ShipModel::TRAIL; j++)
				{
					V3 a = S.trail[k][j], b = S.trail[k][j + 1];
					V3 d = b - a; if (len (d) < 0.01f) continue;
					V3 side = norm (cross (d, a - cam.eye));
					float f0 = 1 - (float) j / ShipModel::TRAIL, f1 = 1 - (float) (j + 1) / ShipModel::TRAIL;
					unsigned c0 = rgbScale (col, 0.3f * f0 * f0 * f0), c1 = rgbScale (col, 0.3f * f1 * f1 * f1);	// (soft, fading fast)
					Vtx *v = F.room (6); if (!v) break;
					V3 a0 = a - side * (wdt * f0), a1 = a + side * (wdt * f0), b0 = b - side * (wdt * f1), b1 = b + side * (wdt * f1);
					setVtx (v[0], a0, 0, 0, c0); setVtx (v[1], a1, 0, 0, c0); setVtx (v[2], b1, 0, 0, c1);
					setVtx (v[3], a0, 0, 0, c0); setVtx (v[4], b1, 0, 0, c1); setVtx (v[5], b0, 0, 0, c1);
				}
		}
		F.end ();
		F.begin (VP, TX_GLOW, B_ADD | B_NOZWRITE | B_LINEAR | B_CLAMP);
		for (int k = 0; k < fx.n; k++)
		{
			const Particle &q = fx.p[k];
			float f = q.life / q.max, s = q.size * (0.6f + 0.8f * f);
			unsigned col = rgbScale (q.col, 0.5f + 0.5f * f);
			F.quad (q.p - cr * s - cu * s, q.p + cr * s - cu * s, q.p + cr * s + cu * s, q.p - cr * s + cu * s, col);
		}
		F.end ();
		F.begin (VP, TX_GLOW, B_ADD | B_NOZWRITE | B_LINEAR | B_CLAMP);
		for (int i = 0; i < R.n; i++)
		{
			const Craft &C = R.c[i];
			M4 M = shipMatrix (R, i);
			const ShipModel &S = ship[i];
			float thr = C.v > 1 ? clampf (C.v / 30, 0.3f, 1.0f) : 0.3f;
			if (C.boostT > 0) thr *= 1.8f;
			// the exhausts: soft plumes, a few glowing puffs shrinking backward, in the hull's colour toward pink
			for (int k = 0; k < S.nflame; k++)
			{
				V3 p = M.point (S.flames[k]);
				V3 back = norm (M.dir (V3 (0, 0, -1)));
				float L = 0.8f + thr * 1.6f + sinf (time * 40 + (float) k) * 0.12f;
				unsigned col = C.boostT > 0 ? 0xFFA040 : rgbMix (PAINTS[clampi (C.build.paint, 0, NPAINT - 1)], 0xFF8CD8, 0.55f);
				plume (F, p, back, L, 0.42f, col, cr, cu);
			}
		}
		F.end ();
		F.begin (VP, 0, B_ADD | B_NOZWRITE);
		for (int i = 0; i < R.n; i++)
		{
			const Craft &C = R.c[i];
			M4 M = shipMatrix (R, i);
			const ShipModel &S = ship[i];
			if (S.hasArc) arc (F, M.point (S.arc[0]), M.point (S.arc[1]), cr, cu, (unsigned) i * 7919u);
			if (C.shieldT > 0 || C.bubbleT > 0)
				sphere (F, M.point (V3 (0, 0.4f, 0)), C.bubbleT > 0 ? 2.0f : 2.3f, C.bubbleT > 0 ? 0x2A5A80 : rgbScale (0x5CF2C8, 0.35f + 0.1f * sinf (time * 6)));
		}
		F.end ();
		(void) me;
	}

	// a crackling energy arc from a to b: three jagged strands, changing every 40 ms
	void arc (Frame &F, const V3 &a, const V3 &b, const V3 &cr, const V3 &cu, unsigned seed)
	{
		Rng r (seed + (unsigned) (time * 25) * 2654435761u);
		V3 d = b - a; float L = len (d); if (L < 0.01f) return;
		V3 side = norm (cross (d, cu)); if (len (cross (d, cu)) < 1e-4f) side = cr;
		V3 upv = norm (cross (side, d));
		for (int strand = 0; strand < 3; strand++)
		{
			const int K = 8;
			V3 prev = a;
			unsigned col = strand == 0 ? 0xFFB0F0 : 0xC040C0;
			float w = strand == 0 ? 0.05f : 0.09f;
			for (int k = 1; k <= K; k++)
			{
				float f = (float) k / K, amp = sinf (f * PI) * L * 0.12f;
				V3 p = k == K ? b : a + d * f + side * (r.range (-1, 1) * amp) + upv * (r.range (-1, 1) * amp);
				V3 sd = norm (cross (p - prev, cu)) * w;
				F.quad (prev - sd, p - sd, p + sd, prev + sd, col);
				F.quad (prev - upv * w, p - upv * w, p + upv * w, prev + upv * w, col);
				prev = p;
			}
		}
	}
	// an exhaust's plume: glow sprites along it, the first bright and wide, the last faint and small
	static void plume (Frame &F, const V3 &p, const V3 &dir, float L, float r, unsigned col, const V3 &cr, const V3 &cu)
	{
		const int K = 5;
		for (int k = 0; k < K; k++)
		{
			float f = (float) k / (K - 1);
			V3 c = p + dir * (L * f * 0.9f);
			float s = r * (1.25f - 0.8f * f);
			unsigned cc = rgbScale (k == 0 ? rgbMix (col, 0xFFFFFF, 0.45f) : col, 0.85f * (1 - f * 0.75f));
			F.quad (c - cr * s - cu * s, c + cr * s - cu * s, c + cr * s + cu * s, c - cr * s + cu * s, cc);
		}
	}
	static void flame (Frame &F, const V3 &p, const V3 &dir, float L, float r, unsigned col, const V3 &cr, const V3 &cu)
	{
		// two crossed quads, bright at the nozzle
		V3 tip = p + dir * L;
		V3 s1 = norm (cross (dir, cu)) * r, s2 = norm (cross (dir, cr)) * r;
		Vtx *v = F.room (12); if (!v) return;
		V3 sides[2] = { s1, s2 };
		for (int k = 0; k < 2; k++)
		{
			V3 a = p - sides[k], b = p + sides[k];
			setVtx (v[0], a, 0, 0, col); setVtx (v[1], b, 0, 0, col); setVtx (v[2], tip, 0, 0, 0x000000);
			setVtx (v[3], b, 0, 0, col); setVtx (v[4], a, 0, 0, col); setVtx (v[5], tip, 0, 0, 0x000000);
			v += 6;
		}
	}
	static void sphere (Frame &F, const V3 &c, float r, unsigned col)
	{
		const int S = 10, Rr = 6;
		for (int j = 0; j < Rr; j++)
			for (int k = 0; k < S; k++)
			{
				float e0 = -PI / 2 + PI * (float) j / Rr, e1 = -PI / 2 + PI * (float) (j + 1) / Rr;
				float a0 = 2 * PI * (float) k / S, a1 = 2 * PI * (float) (k + 1) / S;
				auto P = [&] (float a, float e) { return c + V3 (cosf (a) * cosf (e), sinf (e), sinf (a) * cosf (e)) * r; };
				// brighter at the rim: the middle rows dimmer
				unsigned cc = rgbScale (col, 0.6f + 0.4f * fabsf (sinf (e0 + 0.3f)));
				F.quad (P (a0, e0), P (a1, e0), P (a1, e1), P (a0, e1), cc);
			}
	}

	void boostPads (Frame &F, const Race &R, const Camera &cam)
	{
		const Track &T = *R.T;
		for (int i = 0; i < T.n; i++)
		{
			const Seg &S = T.seg[i];
			if (!(S.flags & F_BOOST)) continue;
			if (len (S.c - cam.eye) > 300) continue;
			float lane = S.lane * S.w * 0.5f, s0 = (float) i * SEG;
			// the chevrons scroll forward (the texture's t), pulsing
			float sc = fmodf (time * 2.0f, 1.0f);
			unsigned col = rgbMix (0x3FD8FF, 0xB8FFFF, 0.5f + 0.5f * sinf (time * 8));
			V3 a = T.pointAt (s0, lane - 2.0f, 0.05f), b = T.pointAt (s0, lane + 2.0f, 0.05f);
			V3 c = T.pointAt (s0 + SEG, lane + 2.0f, 0.05f), d = T.pointAt (s0 + SEG, lane - 2.0f, 0.05f);
			float t0 = (float) (i % 3) / 3.0f - sc;
			F.quad (a, b, c, d, col, 255, 0, t0 + 1.0f / 3, 1, t0);
		}
	}

	// An item box's place, turning and bobbing (false: not there, waiting to come back)
	bool boxMatrix (const Race &R, int b, M4 *M) const
	{
		const Box &B = R.box[b];
		if (B.respawn > 0.5f) return false;
		float grow = B.respawn > 0 ? clampf (1 - B.respawn * 2, 0, 1) : 1;
		V3 p, u, r, f; R.T->surf (B.s, B.l, 1.3f + sinf (time * 3 + (float) b) * 0.2f, &p, &u, &r, &f);
		float a = time * 1.6f + (float) b;
		*M = M4::frame (-r, u, f, p) * M4::rotY (a) * M4::rotX (0.6f) * M4::rotZ (0.6f) * M4::scale (grow, grow, grow);
		return true;
	}
	// The item boxes' see-through shells, with the other glass (after everything opaque)
	void boxGlass (Frame &F, const Race &R, const M4 &VP)
	{
		if (!mBox) return;
		for (int b = 0; b < R.nbox; b++)
		{
			M4 M; if (!boxMatrix (R, b, &M)) continue;
			F.begin (VP * M, 0, B_ALPHA | B_NOZWRITE | B_CULL); F.mesh (*mBox, M4::identity (), 255, 0, 1); F.end ();
		}
	}

	void dynamic (Frame &F, const Race &R, const M4 &VP)
	{
		const Track &T = *R.T;
		// the item boxes: turning, bobbing
		for (int b = 0; b < R.nbox; b++)
		{
			M4 M; if (!boxMatrix (R, b, &M)) continue;
			F.begin (VP * M, 0, B_CULL);
			if (mBox) F.mesh (*mBox, M4::identity ());		// (the '?' marks; the see-through shell: boxGlass)
			else cube (F, 0.8f, time + (float) b);
			F.end ();
		}
		// the star bits: turning, bobbing
		for (int b = 0; b < R.nbit; b++)
		{
			const Bit &B = R.bit[b];
			if (B.respawn > 0) continue;
			V3 p, u, r, f; T.surf (B.s, B.l, 1.0f + sinf (time * 4 + (float) b) * 0.15f, &p, &u, &r, &f);
			M4 M = M4::frame (-r, u, f, p) * M4::rotY (time * 3 + (float) b) * M4::scale (1.2f, 1.2f, 1.2f);
			F.begin (VP * M, 0, B_CULL);
			if (mStar) F.mesh (*mStar, M4::identity ()); else cube (F, 0.3f, time);
			F.end ();
		}
		// the ion storm on its way, then over the leader
		if (R.nebulaOn)
		{
			V3 p = T.pointAt (R.nebulaS, R.nebulaL, 6.0f + sinf (time * 3) * 0.4f);
			M4 M = M4::translate (p) * M4::rotY (time * 0.8f) * M4::scale (1.6f, 1.6f, 1.6f);
			F.begin (VP * M, 0, B_CULL);
			if (mNebula) F.mesh (*mNebula, M4::identity ()); else cube (F, 1.5f, time);
			F.end ();
		}
		// the bubbles in flight, the goo, the springs
		for (int k = 0; k < MAXPROJ; k++)
		{
			const Proj &P = R.proj[k];
			if (!P.on) continue;
			V3 p = T.pointAt (P.s, P.l, P.h + 0.3f);
			M4 M = M4::translate (p) * M4::rotY (time * 4) * M4::scale (0.9f, 0.9f, 0.9f);
			F.begin (VP * M, 0, B_CULL);
			if (mBubble) F.mesh (*mBubble, M4::identity ()); else cube (F, 0.6f, time);
			F.end ();
		}
		for (int k = 0; k < MAXHAZ; k++)
		{
			const Hazard &H = R.haz[k];
			if (!H.on) continue;
			V3 p, u, r, f; T.surf (H.s, H.l, 0.05f, &p, &u, &r, &f);
			bool spring = ITEMS[H.kind].effect == FX_WELL;
			float sq = 1 + 0.08f * sinf (time * 8 + (float) k);
			M4 M = M4::frame (-r, u, f, p, sq, 2 - sq, sq) * M4::rotY (H.spin * (spring ? 3.0f : 0.3f));
			const Mesh *m = spring ? mSpring : mGoo;
			F.begin (VP * M, 0, B_CULL);
			if (m) F.mesh (*m, M4::identity ()); else cube (F, 0.7f, (float) k);
			F.end ();
		}
	}
	// a coloured cube (models missing)
	static void cube (Frame &F, float s, float t)
	{
		V3 c[8]; for (int i = 0; i < 8; i++) c[i] = V3 ((i & 1) ? s : -s, (i & 2) ? s : -s, (i & 4) ? s : -s);
		int f[6][4] = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
		for (int k = 0; k < 6; k++) F.quad (c[f[k][0]], c[f[k][1]], c[f[k][2]], c[f[k][3]], hueColor (t * 0.3f + (float) k / 6));
	}
};

} // namespace cz

#endif
