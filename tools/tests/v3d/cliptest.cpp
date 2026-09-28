// cliptest -- kern/v3d_clip.h (the triangles the kernel gives the V3D: clipped against the near plane
// and a guard band) on the PC: the edge cases, then random triangles drawn by the BASIC 3D's software
// renderer as they are and clipped: the same picture (the clipping cuts nothing visible).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "kern/v3d_clip.h"
#include "basic/bas3d.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf ("FAIL: "); printf (__VA_ARGS__); printf ("\n"); } } while (0)
static kapi_gpu_vertex3 V (float x, float y, float z, float w) { kapi_gpu_vertex3 v; memset (&v, 0, sizeof v); v.x = x; v.y = y; v.z = z; v.w = w; v.r = v.g = v.b = v.a = 255; return v; }
static bool ok (const kapi_gpu_vertex3 &v) { return v.w > 0 && v.z >= -v.w * 1.0001f && fabsf (v.x) <= CLIP_GUARD * v.w * 1.0001f && fabsf (v.y) <= CLIP_GUARD * v.w * 1.0001f; }
static unsigned rnd_s = 12345; static float rnd (float a, float b) { rnd_s = rnd_s * 1103515245u + 12345u; return a + (b - a) * ((rnd_s >> 8) & 0xFFFF) / 65535.0f; }

int main ()
{
	kapi_gpu_vertex3 out[21];
	kapi_gpu_vertex3 in1[3] = { V (-0.5f, -0.5f, 0, 1), V (0.5f, -0.5f, 0, 1), V (0, 0.5f, 0, 1) };
	CHECK (V3DClipTriangle (in1, out) == 3 && memcmp (out, in1, sizeof in1) == 0, "inside: unchanged");
	kapi_gpu_vertex3 in2[3] = { V (0, 0, -5, -2), V (1, 0, -5, -2), V (0, 1, -5, -2) };
	CHECK (V3DClipTriangle (in2, out) == 0, "behind the eye: dropped");
	kapi_gpu_vertex3 in3[3] = { V (0, 0, 0.5f, 1), V (1, 0, -3, -1), V (0, 1, 0.5f, 1) };	// crossing the eye plane
	unsigned n3 = V3DClipTriangle (in3, out);
	CHECK (n3 >= 3, "crossing: kept");
	for (unsigned i = 0; i < n3; i++) CHECK (ok (out[i]), "crossing: vertex %u outside (w %g z %g)", i, out[i].w, out[i].z);
	kapi_gpu_vertex3 in4[3] = { V (0, 0, 0, 1), V (46000, 0, 0, 1), V (0, 46000, 0, 1) };	// ~50000 screens away
	unsigned n4 = V3DClipTriangle (in4, out);
	CHECK (n4 >= 3, "huge: kept");
	for (unsigned i = 0; i < n4; i++) CHECK (ok (out[i]), "huge: vertex %u outside (x %g y %g w %g)", i, out[i].x, out[i].y, out[i].w);
	kapi_gpu_vertex3 in5[3] = { V (NAN, 0, 0, 1), V (1, 0, 0, 1), V (0, 1, 0, 1) };
	unsigned n5 = V3DClipTriangle (in5, out);
	for (unsigned i = 0; i < n5; i++) CHECK (ok (out[i]), "NaN: a vertex out");

	// random triangles (some behind, some far off-screen), as they are vs clipped: the same picture
	enum { W = 320, H = 240, NT = 3000 };
	static bas::G3Vertex v[NT * 3]; static kapi_gpu_vertex3 c[NT * 21];
	static unsigned a[W * H], b[W * H]; static float zb[W * H];
	int nc = 0;
	for (int i = 0; i < NT * 3; i++)
	{
		bas::G3Vertex &p = v[i]; memset (&p, 0, sizeof p);
		// (in front of the eye, w > 0: the software renderer's clipping is the near plane only; some
		// crossing it, z < -w, some thousands of screens away)
		float w = rnd (0.05f, 3.0f), s = rnd (0, 1) < 0.1f ? 20000 : 3;
		p.x = rnd (-s, s) * fabsf (w); p.y = rnd (-s, s) * fabsf (w); p.z = rnd (-1.2f, 1.0f) * fabsf (w); p.w = w;
		p.r = (unsigned char) (i * 37); p.g = (unsigned char) (i * 91); p.b = (unsigned char) (i * 13); p.a = 255;
	}
	long outside = 0;
	for (int t = 0; t < NT; t++)
	{
		kapi_gpu_vertex3 T[3];
		for (int k = 0; k < 3; k++) memcpy (&T[k], &v[t * 3 + k], sizeof T[k]);
		unsigned k = V3DClipTriangle (T, c + nc);
		for (unsigned j = 0; j < k; j++) if (!ok (c[nc + j])) { if (outside < 3) printf ("  outside: x %g y %g z %g w %g\n", c[nc + j].x, c[nc + j].y, c[nc + j].z, c[nc + j].w); outside++; }
		nc += (int) k;
	}
	CHECK (outside == 0, "random: %ld vertices outside after clipping", outside);
	bas::G3Batch b1; memset (&b1, 0, sizeof b1); b1.first = 0; b1.count = NT * 3; b1.flags = bas::G3_NOMATRIX | 7;	// (z: always: order only)
	bas::G3Batch b2 = b1; b2.count = (unsigned) nc;
	auto none = [] (int) -> const bas::G3Texture * { return 0; };
	bas::swRender (a, W, H, W, zb, v, NT * 3, &b1, 1, 0, false, none);
	bas::swRender (b, W, H, W, zb, (const bas::G3Vertex *) c, nc, &b2, 1, 0, false, none);
	int diff = 0; for (int i = 0; i < W * H; i++) { int d = 0; for (int s = 0; s < 24; s += 8) d += abs ((int) ((a[i] >> s) & 255) - (int) ((b[i] >> s) & 255)); if (d > 24) diff++; }
	printf ("random: %d triangles -> %d vertices after clipping, %d pixels of %d differ\n", NT, nc, diff, W * H);
	CHECK (diff < W * H / 500, "random: the pictures differ (%d pixels)", diff);

	// the generic vertices (gpu_render2: n floats): the same positions as V3DClipTriangle's; a
	// triangle whose three vertices are V3DInsideN (the kernel then copies it as it is) unchanged
	int insideN = 0;
	for (int t = 0; t < NT; t++)
	{
		enum { N = 7 };
		float in[3 * N], outN[21 * N];
		kapi_gpu_vertex3 T[3], o3[21];
		for (int k = 0; k < 3; k++)
		{
			const bas::G3Vertex &p = v[t * 3 + k];
			T[k] = V (p.x, p.y, p.z, p.w);
			float *q = in + k * N; q[0] = p.x; q[1] = p.y; q[2] = p.z; q[3] = p.w; q[4] = (float) t; q[5] = (float) k; q[6] = 1;
		}
		unsigned m = V3DClipTriangleN (in, N, outN), m3 = V3DClipTriangle (T, o3);
		CHECK (m == m3, "N: triangle %d: %u vertices, not %u", t, m, m3);
		for (unsigned j = 0; j < m && j < m3; j++)
			CHECK (outN[j * N] == o3[j].x && outN[j * N + 1] == o3[j].y && outN[j * N + 2] == o3[j].z && outN[j * N + 3] == o3[j].w,
			       "N: triangle %d vertex %u moved", t, j);
		if (V3DInsideN (in) && V3DInsideN (in + N) && V3DInsideN (in + 2 * N))
		{
			insideN++;
			CHECK (m == 3 && memcmp (outN, in, sizeof in) == 0, "N: triangle %d inside but changed", t);
		}
	}
	printf ("generic vertices: %d of %d triangles inside (copied as they are)\n", insideN, NT);
	puts (fails ? "FAIL" : "ok");
	return fails ? 1 : 0;
}
