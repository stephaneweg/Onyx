//
// gfx.h -- a frame for the GPU: the vertices and the batches in the kernel's layout (kapi v53/v54:
// struct kapi_gpu_vertex3 / kapi_gpu_batch, the very layout of the BASIC's bas3d.h), built by the game each
// picture, then drawn by the V3D GPU (kapi_gpu_render, in main.cpp) or -- without one, and on the PC -- by the
// software renderer of user/Libs/basic/bas3d.h over the very same triangles.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef CZ_GFX_H
#define CZ_GFX_H

#include "math3d.h"
#include "mesh.h"
#include "basic/bas3d.h"

namespace cz {

typedef bas::G3Vertex Vtx;
typedef bas::G3Batch Batch;

enum
{
	B_NOZWRITE = bas::G3_NOZWRITE, B_CULL = bas::G3_CULL_BACK, B_CULLFRONT = bas::G3_CULL_FRONT,
	B_ALPHA = 1 << bas::G3_BLEND_SHIFT, B_ADD = 2 << bas::G3_BLEND_SHIFT, B_LINEAR = bas::G3_LINEAR,
	B_CLAMP = (1 << bas::G3_WRAP_S_SHIFT) | (1 << bas::G3_WRAP_T_SHIFT), B_NOMATRIX = bas::G3_NOMATRIX,
	B_ZALWAYS = 7, B_ZLEQUAL = 3
};

static inline void setVtx (Vtx &v, const V3 &p, float s, float t, unsigned rgb, unsigned a = 255, unsigned add = 0)
{
	v.x = p.x; v.y = p.y; v.z = p.z; v.w = 1; v.s = s; v.t = t;
	v.r = (unsigned char) (rgb >> 16); v.g = (unsigned char) (rgb >> 8); v.b = (unsigned char) rgb; v.a = (unsigned char) a;
	v.r2 = (unsigned char) (add >> 16); v.g2 = (unsigned char) (add >> 8); v.b2 = (unsigned char) add; v.a2 = 0;
}

// A list of triangles in world (or model) space, kept from frame to frame (the world's pieces, the models).
struct VList
{
	Arr<Vtx> v;
	V3 centre; float radius = 0;		// a sphere around it (the culling)
	void tri (const V3 &a, const V3 &b, const V3 &c, unsigned ca, unsigned cb, unsigned cc, unsigned alpha = 255)
	{
		v.reserve (v.n + 3);
		Vtx *p = v.push (); if (!p) return; setVtx (*p, a, 0, 0, ca, alpha);
		p = v.push (); setVtx (*p, b, 0, 0, cb, alpha);
		p = v.push (); setVtx (*p, c, 0, 0, cc, alpha);
	}
	void tri (const V3 &a, const V3 &b, const V3 &c, unsigned col) { tri (a, b, c, col, col, col); }
	// a quad a b c d (counter-clockwise from its front)
	void quad (const V3 &a, const V3 &b, const V3 &c, const V3 &d, unsigned col) { tri (a, b, c, col); tri (a, c, d, col); }
	// the same, turned to face n (its winding made counter-clockwise seen from n's side)
	void face (const V3 &a, const V3 &b, const V3 &c, const V3 &d, unsigned col, const V3 &n)
	{
		V3 cn = cross (b - a, c - a) + cross (c - a, d - a);
		if (dot (cn, n) < 0) quad (d, c, b, a, col); else quad (a, b, c, d, col);
	}
	void quadUV (const V3 &a, const V3 &b, const V3 &c, const V3 &d, unsigned col, float s0, float t0, float s1, float t1, unsigned alpha = 255)
	{
		v.reserve (v.n + 6);
		Vtx *p;
		p = v.push (); setVtx (*p, a, s0, t1, col, alpha); p = v.push (); setVtx (*p, b, s1, t1, col, alpha); p = v.push (); setVtx (*p, c, s1, t0, col, alpha);
		p = v.push (); setVtx (*p, a, s0, t1, col, alpha); p = v.push (); setVtx (*p, c, s1, t0, col, alpha); p = v.push (); setVtx (*p, d, s0, t0, col, alpha);
	}
	void mesh (const Mesh &m, const M4 &M)
	{
		v.reserve (v.n + m.tris.n * 3);
		for (int i = 0; i < m.tris.n; i++)
		{
			const Tri &T = m.tris[i];
			for (int k = 0; k < 3; k++) { Vtx *p = v.push (); if (!p) return; setVtx (*p, M.point (T.p[k]), T.s[k], T.t[k], T.col[k], T.alpha); }
		}
	}
	void bound ()
	{
		if (!v.n) { centre = V3 (); radius = 0; return; }
		V3 lo (1e9f, 1e9f, 1e9f), hi (-1e9f, -1e9f, -1e9f);
		for (int i = 0; i < v.n; i++)
		{
			if (v[i].x < lo.x) lo.x = v[i].x; if (v[i].y < lo.y) lo.y = v[i].y; if (v[i].z < lo.z) lo.z = v[i].z;
			if (v[i].x > hi.x) hi.x = v[i].x; if (v[i].y > hi.y) hi.y = v[i].y; if (v[i].z > hi.z) hi.z = v[i].z;
		}
		centre = (lo + hi) * 0.5f; radius = len (hi - lo) * 0.5f;
	}
};

// One picture: the vertices and the batches handed to the GPU at once.
struct Frame
{
	Arr<Vtx> v;
	Arr<Batch> b;
	int texOf[64];				// the game's texture number -> the backend's handle
	void clear () { v.clear (); b.clear (); }
	// A new batch: the vertices added from now on, through M, with its texture (the game's number, 0: none)
	Batch *begin (const M4 &M, int tex, unsigned flags)
	{
		Batch *B = b.push (); if (!B) return 0;
		B->first = (unsigned) v.n; B->count = 0; B->texture = tex; B->flags = flags;
		memcpy (B->m, M.m, sizeof B->m);
		return B;
	}
	void end () { if (b.n) b.p[b.n - 1].count = (unsigned) v.n - b.p[b.n - 1].first; }
	void add (const VList &L)
	{
		v.reserve (v.n + L.v.n);
		if (v.cap < v.n + L.v.n) return;
		memcpy (v.p + v.n, L.v.p, sizeof (Vtx) * (size_t) L.v.n); v.n += L.v.n;
	}
	static int partOf (const Tri &T) { return (T.flags & TRI_GLOW) ? 2 : (T.flags & TRI_ALPHA) ? 1 : 0; }
	Vtx *room (int k) { v.reserve (v.n + k); if (v.cap < v.n + k) return 0; Vtx *p = v.p + v.n; v.n += k; return p; }
	void tri (const V3 &a, const V3 &b2, const V3 &c, unsigned col, unsigned alpha = 255, unsigned add = 0)
	{
		Vtx *p = room (3); if (!p) return;
		setVtx (p[0], a, 0, 0, col, alpha, add); setVtx (p[1], b2, 0, 0, col, alpha, add); setVtx (p[2], c, 0, 0, col, alpha, add);
	}
	void quad (const V3 &a, const V3 &b2, const V3 &c, const V3 &d, unsigned col, unsigned alpha = 255, float s0 = 0, float t0 = 0, float s1 = 1, float t1 = 1, unsigned add = 0)
	{
		Vtx *p = room (6); if (!p) return;
		setVtx (p[0], a, s0, t1, col, alpha, add); setVtx (p[1], b2, s1, t1, col, alpha, add); setVtx (p[2], c, s1, t0, col, alpha, add);
		setVtx (p[3], a, s0, t1, col, alpha, add); setVtx (p[4], c, s1, t0, col, alpha, add); setVtx (p[5], d, s0, t0, col, alpha, add);
	}
	// A mesh through M (its lit colours; add: added). part: 0 the solid triangles, 1 the see-through ones
	// (glass: an alpha batch), 2 the glowing ones (an additive batch)
	void mesh (const Mesh &m, const M4 &M, unsigned alphaMul = 255, unsigned add = 0, int part = 0)
	{
		int cnt = 0;
		for (int i = 0; i < m.tris.n; i++) if (partOf (m.tris[i]) == part) cnt++;
		Vtx *p = room (cnt * 3); if (!p) return;
		for (int i = 0; i < m.tris.n; i++)
		{
			const Tri &T = m.tris[i];
			if (partOf (T) != part) continue;
			unsigned a = (unsigned) T.alpha * alphaMul / 255;
			for (int k = 0; k < 3; k++) setVtx (*p++, M.point (T.p[k]), T.s[k], T.t[k], T.col[k], a, (T.flags & TRI_OUTLINE) ? 0 : add);
		}
	}
};

// ---- the textures: made by the game (0xAARRGGBB), numbered 1.. ------------------------------------------------
struct TexStore
{
	enum { MAX = 32 };
	unsigned *px[MAX]; int w[MAX], h[MAX]; int n = 0;
	TexStore () { for (int i = 0; i < MAX; i++) { px[i] = 0; w[i] = h[i] = 0; } }
	int add (unsigned *pixels, int W, int H) { if (n + 1 >= MAX) return 0; n++; px[n] = pixels; w[n] = W; h[n] = H; return n; }
};

// The software picture of a frame (no GPU; the PC): bas3d's renderer, into dst.
static inline void softRender (const Frame &F, const TexStore &T, unsigned *dst, int W, int H, int stride, float *zb, unsigned clear, bool keep)
{
	static bas::G3Texture tx[TexStore::MAX];
	for (int i = 1; i <= T.n; i++) { tx[i].px = T.px[i]; tx[i].w = T.w[i]; tx[i].h = T.h[i]; }
	bas::swRender (dst, W, H, stride, zb, F.v.p, F.v.n, F.b.p, F.b.n, clear, keep,
		       [&] (int t) -> const bas::G3Texture * { return t > 0 && t <= T.n ? &tx[t] : 0; });
}

} // namespace cz

#endif
