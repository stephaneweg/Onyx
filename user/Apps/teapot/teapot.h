//
// teapot.h -- the teapot demo's portable part (Onyx app and PC test): the Utah teapot's
// Bezier patches tessellated once, then each frame rotated, lit (per vertex: ambient +
// diffuse + specular, two-sided) and projected into a triangle list of GPU vertices
// (kapi_gpu_vertex: NDC position + RGBA8); and a software renderer for that same list
// (z-buffer, Gouraud), used when there is no GPU -- and to check the picture on a PC.
// No libc: its own sin / cos, sqrt from the FPU.
//
#ifndef _teapot_h
#define _teapot_h

namespace teapot {

#include "teapot_data.inc"

struct Vertex { float x, y, z; unsigned char r, g, b, a; };	// = struct kapi_gpu_vertex
struct V3 { float x, y, z; };

static inline float tsqrt (float v) { return __builtin_sqrtf (v); }
static float tsin (float x)
{
	const float PI = 3.14159265f, TWO_PI = 6.28318531f;
	while (x > PI) x -= TWO_PI;
	while (x < -PI) x += TWO_PI;
	float x2 = x * x;				// Taylor to x^11: < 1e-6 on [-pi, pi]
	return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110)))));
}
static inline float tcos (float x) { return tsin (x + 1.57079633f); }
static inline V3 sub (V3 a, V3 b) { return V3 { a.x - b.x, a.y - b.y, a.z - b.z }; }
static inline V3 cross (V3 a, V3 b) { return V3 { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static inline float dot (V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 norm (V3 a) { float l = tsqrt (dot (a, a)); return l > 1e-12f ? V3 { a.x / l, a.y / l, a.z / l } : V3 { 0, 0, 1 }; }

enum { N = 10 };				// segments per patch side
enum { PATCHES = 6 * 4 + 4 * 2, GRID = (N + 1) * (N + 1) };
enum { TRIS = PATCHES * N * N * 2, VERTS = TRIS * 3 };

// The tessellated model: per patch a (N+1)^2 grid of points and normals (teapot space: z up).
struct Model { V3 p[PATCHES][GRID]; V3 n[PATCHES][GRID]; };

static void bern (float t, float b[4], float d[4])
{
	float s = 1 - t;
	b[0] = s * s * s; b[1] = 3 * t * s * s; b[2] = 3 * t * t * s; b[3] = t * t * t;
	d[0] = -3 * s * s; d[1] = 3 * s * s - 6 * t * s; d[2] = 6 * t * s - 3 * t * t; d[3] = 3 * t * t;
}

static void evalPatch (const V3 cp[16], float u, float v, V3 *pos, V3 *du, V3 *dv)
{
	float bu[4], du_[4], bv[4], dv_[4];
	bern (u, bu, du_); bern (v, bv, dv_);
	V3 p = { 0, 0, 0 }, a = { 0, 0, 0 }, b = { 0, 0, 0 };
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
		{
			const V3 &c = cp[i * 4 + j];
			float wp = bu[i] * bv[j], wu = du_[i] * bv[j], wv = bu[i] * dv_[j];
			p.x += c.x * wp; p.y += c.y * wp; p.z += c.z * wp;
			a.x += c.x * wu; a.y += c.y * wu; a.z += c.z * wu;
			b.x += c.x * wv; b.y += c.y * wv; b.z += c.z * wv;
		}
	*pos = p; *du = a; *dv = b;
}

static void build (Model &m)
{
	static const float Q[4][2] = { { 1, 1 }, { -1, 1 }, { -1, -1 }, { 1, -1 } };
	int k = 0;
	for (int ip = 0; ip < 10; ip++)
	{
		int copies = ip < 6 ? 4 : 2;
		for (int c = 0; c < copies; c++, k++)
		{
			float sx = ip < 6 ? Q[c][0] : 1, sy = ip < 6 ? Q[c][1] : (c ? -1.0f : 1.0f);
			V3 cp[16];
			for (int i = 0; i < 16; i++)
			{
				const float *q = cpdata_teapot[patchdata_teapot[ip][i]];
				cp[i] = V3 { q[0] * sx, q[1] * sy, q[2] };
			}
			for (int i = 0; i <= N; i++)
				for (int j = 0; j <= N; j++)
				{
					float u = (float) i / N, v = (float) j / N;
					V3 p, du, dv;
					evalPatch (cp, u, v, &p, &du, &dv);
					V3 nn = cross (du, dv);
					if (dot (nn, nn) < 1e-10f)			// a pole (the lid's knob, the bottom)
					{
						V3 p2, du2, dv2;
						evalPatch (cp, u < 0.5f ? u + 0.01f : u - 0.01f, v < 0.5f ? v + 0.01f : v - 0.01f, &p2, &du2, &dv2);
						nn = cross (du2, dv2);
					}
					m.p[k][i * (N + 1) + j] = p;
					m.n[k][i * (N + 1) + j] = norm (nn);
				}
		}
	}
}

// One frame: rotation angles, the output size (for the aspect), the vertices (VERTS).
static int frame (const Model &m, float yaw, float pitch, int w, int h, Vertex *out)
{
	float cy = tcos (yaw), sy = tsin (yaw), cp = tcos (pitch), sp = tsin (pitch);
	float aspect = (float) w / (float) h, f = 2.5f;		// focal length (cot of half the fov)
	const float camZ = 8.0f, zNear = 4.0f, zFar = 12.0f;
	V3 L = norm (V3 { -0.5f, 0.8f, 0.9f }), H = norm (V3 { L.x, L.y, L.z + 1 });
	int n = 0;
	for (int k = 0; k < PATCHES; k++)
	{
		Vertex g[GRID];
		for (int i = 0; i < GRID; i++)
		{
			// teapot space (z up, height 0..3.15) -> world (y up, centred), then rotate
			V3 p = m.p[k][i], nn = m.n[k][i];
			V3 w0 = { p.x, p.z - 1.55f, -p.y }, n0 = { nn.x, nn.z, -nn.y };
			V3 w1 = { w0.x * cy + w0.z * sy, w0.y, -w0.x * sy + w0.z * cy };	// yaw (around y)
			V3 n1 = { n0.x * cy + n0.z * sy, n0.y, -n0.x * sy + n0.z * cy };
			V3 w2 = { w1.x, w1.y * cp - w1.z * sp, w1.y * sp + w1.z * cp };	// pitch (around x)
			V3 n2 = { n1.x, n1.y * cp - n1.z * sp, n1.y * sp + n1.z * cp };
			// light (two-sided: the inside of the lid, the spout)
			float d = dot (n2, L), s = dot (n2, H);
			if (d < 0) { d = -d; s = -s; }
			if (s < 0) s = 0;
			float s2 = s * s, s4 = s2 * s2, s8 = s4 * s4, spec = s8 * s8 * s8;	// ~ s^24
			float r = 0.10f + 0.80f * d * 0.95f + 0.60f * spec;
			float gg = 0.08f + 0.80f * d * 0.55f + 0.60f * spec;
			float b = 0.12f + 0.80f * d * 0.25f + 0.60f * spec;
			// perspective: the camera at z = camZ looking at -z
			float z = camZ - w2.z;						// distance in front
			float ndcX = f * w2.x / (aspect * z), ndcY = f * w2.y / z;
			float ndcZ = ((zFar + zNear) - 2 * zFar * zNear / z) / (zFar - zNear);
			auto c8 = [] (float v) -> unsigned char { return (unsigned char) (v <= 0 ? 0 : v >= 1 ? 255 : v * 255.0f + 0.5f); };
			g[i] = Vertex { ndcX, ndcY, ndcZ, c8 (r), c8 (gg), c8 (b), 255 };
		}
		for (int i = 0; i < N; i++)
			for (int j = 0; j < N; j++)
			{
				int a = i * (N + 1) + j, b = a + 1, c = a + N + 1, d = c + 1;
				out[n++] = g[a]; out[n++] = g[c]; out[n++] = g[b];
				out[n++] = g[b]; out[n++] = g[c]; out[n++] = g[d];
			}
	}
	return n;
}

// ---- the software renderer (no GPU): the same triangles, z-buffered, Gouraud-shaded ------------------
static void raster (const Vertex *v, int n, unsigned clear, unsigned *px, int w, int h, int stride, float *zbuf)
{
	for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) px[(long) y * stride + x] = clear;
	for (int i = 0; i < w * h; i++) zbuf[i] = 2.0f;
	for (int t = 0; t + 2 < n; t += 3)
	{
		const Vertex *P[3] = { &v[t], &v[t + 1], &v[t + 2] };
		float X[3], Y[3];
		for (int k = 0; k < 3; k++) { X[k] = (P[k]->x + 1) * 0.5f * w; Y[k] = (1 - P[k]->y) * 0.5f * h; }
		float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
		if (area == 0) continue;
		float minX = X[0], maxX = X[0], minY = Y[0], maxY = Y[0];
		for (int k = 1; k < 3; k++)
		{
			if (X[k] < minX) minX = X[k];
			if (X[k] > maxX) maxX = X[k];
			if (Y[k] < minY) minY = Y[k];
			if (Y[k] > maxY) maxY = Y[k];
		}
		int x0 = (int) minX, x1 = (int) maxX + 1, y0 = (int) minY, y1 = (int) maxY + 1;
		x0 = x0 < 0 ? 0 : x0; y0 = y0 < 0 ? 0 : y0; x1 = x1 > w ? w : x1; y1 = y1 > h ? h : y1;
		float inv = 1.0f / area;
		for (int y = y0; y < y1; y++)
			for (int x = x0; x < x1; x++)
			{
				float px_ = x + 0.5f, py_ = y + 0.5f;
				float w0 = ((X[1] - px_) * (Y[2] - py_) - (X[2] - px_) * (Y[1] - py_)) * inv;
				float w1 = ((X[2] - px_) * (Y[0] - py_) - (X[0] - px_) * (Y[2] - py_)) * inv;
				float w2 = 1 - w0 - w1;
				if (w0 < 0 || w1 < 0 || w2 < 0) continue;
				float z = w0 * P[0]->z + w1 * P[1]->z + w2 * P[2]->z;
				float &zb = zbuf[y * w + x];
				if (z >= zb) continue;
				zb = z;
				int r = (int) (w0 * P[0]->r + w1 * P[1]->r + w2 * P[2]->r);
				int g = (int) (w0 * P[0]->g + w1 * P[1]->g + w2 * P[2]->g);
				int b = (int) (w0 * P[0]->b + w1 * P[1]->b + w2 * P[2]->b);
				px[(long) y * stride + x] = (unsigned) (r << 16 | g << 8 | b);
			}
	}
}

} // namespace teapot

#endif
