//
// basic/bas3dscene.h -- the VM side of the BASIC 3D statements (basvm.cpp): the frame being
// built between SCENE3D and RENDER3D. The model matrix and its stack (IDENTITY3D, TRANSLATE3D,
// ROTATE3D, SCALE3D, PUSH3D, POP3D), the camera (CAMERA3D: eye, target, field of view), the
// light (LIGHT3D: the direction it comes from, the ambient part), the state (COLOR3D,
// TEXTURE3D, BLEND3D, DEPTH3D, CULL3D), and the geometry (VERTEX3D, CUBE3D, SPHERE3D,
// CYLINDER3D, PLANE3D) turned into vertices + batches (basic/bas3d.h): the vertices stay in
// model space, each batch carries projection * view * model -- the GPU transforms them. The
// shapes are lit here (per vertex, the normal turned by the model matrix); VERTEX3D is not.
//
#ifndef ONYX_BAS3DSCENE_H
#define ONYX_BAS3DSCENE_H

#include "basic/bas3d.h"
#include "basic/basint.h"
#include "basic/basnum.h"

namespace bas {

struct Scene3D
{
	enum { STACK = 32, MAXV = 3 * 65536, MAXB = 4096 };
	Vec<G3Vertex> v; Vec<G3Batch> b;
	float model[16], stack[STACK][16]; int nstack;
	float view[16], proj[16];
	float eye[3], target[3], fov;
	float light[3]; float ambient; bool lit;
	unsigned color; int alpha; int texture; unsigned flags;	// flags: the G3_* of the next batch
	unsigned clear; bool keep;
	bool dirty;						// the next triangle starts a batch
	G3Vertex pend[3]; int npend;				// VERTEX3D
	bool full;						// (too many vertices / batches: the rest is dropped)
	// the unit sphere / cylinder of each detail (6..64), built at its first use (their sines and
	// cosines are costly): triangles already turned outwards, then scaled at each use
	struct CTri { float P[3][3], N[3][3], T[2 * 3]; };
	Vec<CTri> sph[65], cyl[65];
	Vec<CTri> *collect = 0;					// (tri () fills it instead of drawing)

	Scene3D () { eye[0] = 0; eye[1] = 2; eye[2] = 6; target[0] = target[1] = target[2] = 0; fov = 60;
		     light[0] = 0.4f; light[1] = 1; light[2] = 0.6f; ambient = 0.3f; lit = true; normL (); begin (0, false); }

	static void ident (float *m) { for (int i = 0; i < 16; i++) m[i] = (i % 5) == 0 ? 1.0f : 0.0f; }
	static void mul (const float *a, const float *c, float *out)	// out = a * c (out may be neither)
	{
		float r[16];
		for (int i = 0; i < 4; i++)
			for (int j = 0; j < 4; j++)
				r[i * 4 + j] = a[i * 4] * c[j] + a[i * 4 + 1] * c[4 + j] + a[i * 4 + 2] * c[8 + j] + a[i * 4 + 3] * c[12 + j];
		for (int i = 0; i < 16; i++) out[i] = r[i];
	}
	void post (const float *t) { mul (model, t, model); dirty = true; }	// model = model * t
	static float fsqrt (float x) { return (float) nsqrt ((double) x); }
	void normL ()
	{
		float l = fsqrt (light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
		lit = l > 1e-6f;
		if (lit) { light[0] /= l; light[1] /= l; light[2] /= l; }
	}

	// SCENE3D: a new frame (the camera and the light stay)
	void begin (unsigned clr, bool kp)
	{
		v.n = 0; b.n = 0; ident (model); nstack = 0;
		color = 0xFFFFFF; alpha = 255; texture = 0; flags = G3_CULL_BACK; npend = 0;
		clear = clr; keep = kp; dirty = true; full = false;
	}
	void identity () { ident (model); dirty = true; }
	void translate (float x, float y, float z) { float t[16]; ident (t); t[3] = x; t[7] = y; t[11] = z; post (t); }
	void scale (float x, float y, float z) { float t[16]; ident (t); t[0] = x; t[5] = y; t[10] = z; post (t); }
	void rotate (float ax, float ay, float az)		// degrees; X first, then Y, then Z
	{
		const float D = 3.14159265f / 180;
		float t[16];
		if (az != 0) { float c = (float) ncos (az * D), s = (float) nsin (az * D); ident (t); t[0] = c; t[1] = -s; t[4] = s; t[5] = c; post (t); }
		if (ay != 0) { float c = (float) ncos (ay * D), s = (float) nsin (ay * D); ident (t); t[0] = c; t[2] = s; t[8] = -s; t[10] = c; post (t); }
		if (ax != 0) { float c = (float) ncos (ax * D), s = (float) nsin (ax * D); ident (t); t[5] = c; t[6] = -s; t[9] = s; t[10] = c; post (t); }
	}
	bool push () { if (nstack >= STACK) return false; for (int i = 0; i < 16; i++) stack[nstack][i] = model[i]; nstack++; return true; }
	bool pop () { if (nstack <= 0) return false; nstack--; for (int i = 0; i < 16; i++) model[i] = stack[nstack][i]; dirty = true; return true; }
	void setFlags (unsigned f) { if (f != flags) { flags = f; dirty = true; } }
	void setTexture (int t) { if (t != texture) { texture = t; dirty = true; } }

	// projection * view (aspect = the displayed width / height of the page)
	void camera (float aspect)
	{
		float f[3] = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
		float l = fsqrt (f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
		if (l < 1e-6f) { f[0] = 0; f[1] = 0; f[2] = -1; } else { f[0] /= l; f[1] /= l; f[2] /= l; }
		float up[3] = { 0, 1, 0 };
		if (f[1] > 0.999f || f[1] < -0.999f) { up[1] = 0; up[2] = -1; }
		float s[3] = { f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0] };
		l = fsqrt (s[0] * s[0] + s[1] * s[1] + s[2] * s[2]); s[0] /= l; s[1] /= l; s[2] /= l;
		float u[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0] };
		float V[16] = { s[0], s[1], s[2], -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]),
				u[0], u[1], u[2], -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]),
				-f[0], -f[1], -f[2], f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2],
				0, 0, 0, 1 };
		float a = fov * 3.14159265f / 360, ct = (float) (ncos (a) / nsin (a)), n = 0.1f, fa = 1000;
		float P[16] = { ct / aspect, 0, 0, 0,  0, ct, 0, 0,  0, 0, (fa + n) / (n - fa), 2 * fa * n / (n - fa),  0, 0, -1, 0 };
		for (int i = 0; i < 16; i++) { view[i] = V[i]; proj[i] = P[i]; }
		dirty = true;
	}

	// ---- geometry ------------------------------------------------------------------------------------------
	// a batch for the next triangle (a new one when the matrix / texture / state changed)
	bool open ()
	{
		if (full) return false;
		if (v.n + 3 > MAXV) { full = true; return false; }
		if (dirty || b.n == 0)
		{
			if (b.n >= MAXB) { full = true; return false; }
			G3Batch B;
			B.first = (unsigned) v.n; B.count = 0; B.texture = texture; B.flags = flags;
			float pv[16]; mul (proj, view, pv); mul (pv, model, B.m);
			b.push (B);
			dirty = false;
		}
		return true;
	}
	void put (float x, float y, float z, float s, float t, float shade)
	{
		G3Vertex q;
		q.x = x; q.y = y; q.z = z; q.w = 1; q.s = s; q.t = t;
		auto ch = [shade] (unsigned c) -> unsigned char { float f = (float) c * shade; return (unsigned char) (f >= 255 ? 255 : f < 0 ? 0 : f + 0.5f); };
		q.r = ch ((color >> 16) & 255); q.g = ch ((color >> 8) & 255); q.b = ch (color & 255);
		q.a = (unsigned char) alpha; q.r2 = q.g2 = q.b2 = q.a2 = 0;
		v.push (q);
	}
	float shade (float nx, float ny, float nz)		// the light on a model-space normal
	{
		if (!lit) return 1;
		const float *m = model;
		float wx = m[0] * nx + m[1] * ny + m[2] * nz, wy = m[4] * nx + m[5] * ny + m[6] * nz, wz = m[8] * nx + m[9] * ny + m[10] * nz;
		float l = fsqrt (wx * wx + wy * wy + wz * wz);
		float d = l > 1e-9f ? (wx * light[0] + wy * light[1] + wz * light[2]) / l : 0;
		return ambient + (1 - ambient) * (d > 0 ? d : 0);
	}
	// a lit triangle (positions P, normals N, texture coordinates T); flip: turned to face its normal
	void tri (const float P[3][3], const float N[3][3], const float T[3][2], bool orient)
	{
		int o[3] = { 0, 1, 2 };
		if (orient)
		{
			float ax = P[1][0] - P[0][0], ay = P[1][1] - P[0][1], az = P[1][2] - P[0][2];
			float bx = P[2][0] - P[0][0], by = P[2][1] - P[0][1], bz = P[2][2] - P[0][2];
			float cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
			if (cx * cx + cy * cy + cz * cz < 1e-12f) return;	// (degenerate: a pole)
			float nx = N[0][0] + N[1][0] + N[2][0], ny = N[0][1] + N[1][1] + N[2][1], nz = N[0][2] + N[1][2] + N[2][2];
			if (cx * nx + cy * ny + cz * nz < 0) { o[1] = 2; o[2] = 1; }
		}
		if (collect)
		{
			CTri c;
			for (int k = 0; k < 3; k++)
			{
				for (int m = 0; m < 3; m++) { c.P[k][m] = P[o[k]][m]; c.N[k][m] = N[o[k]][m]; }
				c.T[2 * k] = T[o[k]][0]; c.T[2 * k + 1] = T[o[k]][1];
			}
			collect->push (c);
			return;
		}
		if (!open ()) return;
		for (int k = 0; k < 3; k++)
			put (P[o[k]][0], P[o[k]][1], P[o[k]][2], T[o[k]][0], T[o[k]][1], shade (N[o[k]][0], N[o[k]][1], N[o[k]][2]));
		b[b.n - 1].count += 3;
	}
	void drawCached (const Vec<CTri> &L, float kx, float ky, float kz)
	{
		for (int i = 0; i < L.n; i++)
		{
			const CTri &c = L[i];
			if (!open ()) return;
			for (int k = 0; k < 3; k++)
				put (c.P[k][0] * kx, c.P[k][1] * ky, c.P[k][2] * kz, c.T[2 * k], c.T[2 * k + 1], shade (c.N[k][0], c.N[k][1], c.N[k][2]));
			b[b.n - 1].count += 3;
		}
	}
	void quad (const float P[4][3], const float N[3], bool orient)	// corners in order, one normal
	{
		static const float ST[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
		float n3[3][3] = { { N[0], N[1], N[2] }, { N[0], N[1], N[2] }, { N[0], N[1], N[2] } };
		float a[3][3] = { { P[0][0], P[0][1], P[0][2] }, { P[1][0], P[1][1], P[1][2] }, { P[2][0], P[2][1], P[2][2] } };
		float ta[3][2] = { { ST[0][0], ST[0][1] }, { ST[1][0], ST[1][1] }, { ST[2][0], ST[2][1] } };
		tri (a, n3, ta, orient);
		float c[3][3] = { { P[0][0], P[0][1], P[0][2] }, { P[2][0], P[2][1], P[2][2] }, { P[3][0], P[3][1], P[3][2] } };
		float tc[3][2] = { { ST[0][0], ST[0][1] }, { ST[2][0], ST[2][1] }, { ST[3][0], ST[3][1] } };
		tri (c, n3, tc, orient);
	}
	// VERTEX3D: three make a triangle (unlit, as given: counter-clockwise = the front)
	void vertex (float x, float y, float z, float s, float t)
	{
		G3Vertex &q = pend[npend++];
		q.x = x; q.y = y; q.z = z; q.w = 1; q.s = s; q.t = t;
		q.r = (unsigned char) ((color >> 16) & 255); q.g = (unsigned char) ((color >> 8) & 255); q.b = (unsigned char) (color & 255);
		q.a = (unsigned char) alpha; q.r2 = q.g2 = q.b2 = q.a2 = 0;
		if (npend < 3) return;
		npend = 0;
		if (!open ()) return;
		for (int k = 0; k < 3; k++) v.push (pend[k]);
		b[b.n - 1].count += 3;
	}
	void cube (float size)
	{
		static const float F[6][9] = {			// normal, u, v (u x v = the normal)
			{ 0, 0, 1,   1, 0, 0,   0, 1, 0 }, { 0, 0, -1,  -1, 0, 0,  0, 1, 0 },
			{ 1, 0, 0,   0, 0, -1,  0, 1, 0 }, { -1, 0, 0,  0, 0, 1,   0, 1, 0 },
			{ 0, 1, 0,   1, 0, 0,   0, 0, -1 }, { 0, -1, 0,  1, 0, 0,  0, 0, 1 } };
		static const float CU[4] = { -1, 1, 1, -1 }, CV[4] = { -1, -1, 1, 1 };
		float h = size / 2;
		for (int f = 0; f < 6; f++)
		{
			const float *d = F[f];
			float P[4][3];
			for (int c = 0; c < 4; c++)
				for (int k = 0; k < 3; k++) P[c][k] = h * (d[k] + CU[c] * d[3 + k] + CV[c] * d[6 + k]);
			quad (P, d, false);
		}
	}
	void plane (float w, float d)				// on y = 0, facing up
	{
		float x = w / 2, z = d / 2;
		const float P[4][3] = { { -x, 0, z }, { x, 0, z }, { x, 0, -z }, { -x, 0, -z } };
		const float N[3] = { 0, 1, 0 };
		quad (P, N, false);
	}
	void sphere (float r, int detail)
	{
		int sl = detail < 6 ? 6 : detail > 64 ? 64 : detail;
		if (sph[sl].n == 0) { collect = &sph[sl]; buildSphere (sl); collect = 0; }
		drawCached (sph[sl], r, r, r);
	}
	void buildSphere (int sl)
	{
		int st = sl / 2; const float r = 1;
		const float PI = 3.14159265f;
		for (int i = 0; i < st; i++)
			for (int j = 0; j < sl; j++)
			{
				float P[4][3], N[4][3], T[4][2];
				int ii[4] = { i, i + 1, i + 1, i }, jj[4] = { j, j, j + 1, j + 1 };
				for (int k = 0; k < 4; k++)
				{
					float th = PI * (float) ii[k] / (float) st, ph = 2 * PI * (float) jj[k] / (float) sl;
					float sth = (float) nsin (th);
					N[k][0] = sth * (float) ncos (ph); N[k][1] = (float) ncos (th); N[k][2] = -sth * (float) nsin (ph);
					for (int c = 0; c < 3; c++) P[k][c] = r * N[k][c];
					T[k][0] = (float) jj[k] / (float) sl; T[k][1] = (float) ii[k] / (float) st;
				}
				float a[3][3], na[3][3], ta[3][2], c[3][3], nc[3][3], tc[3][2];
				const int A[3] = { 0, 1, 2 }, C[3] = { 0, 2, 3 };
				for (int k = 0; k < 3; k++)
					for (int m = 0; m < 3; m++) { a[k][m] = P[A[k]][m]; na[k][m] = N[A[k]][m]; c[k][m] = P[C[k]][m]; nc[k][m] = N[C[k]][m]; }
				for (int k = 0; k < 3; k++) { ta[k][0] = T[A[k]][0]; ta[k][1] = T[A[k]][1]; tc[k][0] = T[C[k]][0]; tc[k][1] = T[C[k]][1]; }
				tri (a, na, ta, true);
				tri (c, nc, tc, true);
			}
	}
	void cylinder (float r, float h, int detail)		// around y, from y = 0 to h, closed
	{
		int sl = detail < 6 ? 6 : detail > 64 ? 64 : detail;
		if (cyl[sl].n == 0) { collect = &cyl[sl]; buildCylinder (sl); collect = 0; }
		drawCached (cyl[sl], r, h, r);
	}
	void buildCylinder (int sl)
	{
		const float r = 1, h = 1;
		const float PI = 3.14159265f;
		for (int j = 0; j < sl; j++)
		{
			float a0 = 2 * PI * (float) j / (float) sl, a1 = 2 * PI * (float) (j + 1) / (float) sl;
			float c0 = (float) ncos (a0), s0 = -(float) nsin (a0), c1 = (float) ncos (a1), s1 = -(float) nsin (a1);
			// the side: smooth normals
			float P[3][3] = { { r * c0, 0, r * s0 }, { r * c1, 0, r * s1 }, { r * c1, h, r * s1 } };
			float N[3][3] = { { c0, 0, s0 }, { c1, 0, s1 }, { c1, 0, s1 } };
			float T[3][2] = { { (float) j / (float) sl, 1 }, { (float) (j + 1) / (float) sl, 1 }, { (float) (j + 1) / (float) sl, 0 } };
			tri (P, N, T, true);
			float P2[3][3] = { { r * c0, 0, r * s0 }, { r * c1, h, r * s1 }, { r * c0, h, r * s0 } };
			float N2[3][3] = { { c0, 0, s0 }, { c1, 0, s1 }, { c0, 0, s0 } };
			float T2[3][2] = { { (float) j / (float) sl, 1 }, { (float) (j + 1) / (float) sl, 0 }, { (float) j / (float) sl, 0 } };
			tri (P2, N2, T2, true);
			// the caps
			for (int cap = 0; cap < 2; cap++)
			{
				float y = cap ? h : 0, ny = cap ? 1.0f : -1.0f;
				float Pc[3][3] = { { 0, y, 0 }, { r * c0, y, r * s0 }, { r * c1, y, r * s1 } };
				float Nc[3][3] = { { 0, ny, 0 }, { 0, ny, 0 }, { 0, ny, 0 } };
				float Tc[3][2] = { { 0.5f, 0.5f }, { 0.5f + 0.5f * c0, 0.5f + 0.5f * s0 }, { 0.5f + 0.5f * c1, 0.5f + 0.5f * s1 } };
				tri (Pc, Nc, Tc, true);
			}
		}
	}
};

} // namespace bas

#endif
