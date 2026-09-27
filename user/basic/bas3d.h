//
// basic/bas3d.h -- the 3D of Onyx BASIC (SCENE3D ... RENDER3D): what a frame is made of, and a
// software renderer for it.
//
// A frame is a list of vertices and of batches in the layout of the kernel's GPU interface
// (kapi v53 struct kapi_gpu_vertex3 / kapi_gpu_batch, the same flag bits): each batch draws a
// range of the vertices (triangles) with its own 4 x 4 matrix (clip = M * (x y z w), row by
// row), texture (the BASIC texture number, 0 = none) and state. The Onyx runtime hands it to
// the GPU (kapi_gpu_render); when there is none -- and on the PC -- swRender() below draws the
// same picture: the triangles clipped against the near plane, perspective-correct colours and
// texture coordinates, depth test, culling (front = counter-clockwise, y up), blending.
// Plain portable C++ (no libc): the Onyx runtime, the PC runtime and the host tests.
//
#ifndef ONYX_BAS3D_H
#define ONYX_BAS3D_H

namespace bas {

struct G3Vertex { float x, y, z, w, s, t; unsigned char r, g, b, a, r2, g2, b2, a2; };	// (r2..a2: added)
struct G3Batch { unsigned first, count; int texture; unsigned flags; float m[16]; };
static_assert (sizeof (G3Vertex) == 32 && sizeof (G3Batch) == 80, "the kapi v53 layout");

// the flag bits (= KAPI_GPU_B_*)
enum
{
	G3_ZFUNC = 7,					// 0 less (default), 2 equal, 3 lequal, 4 greater, 5 notequal, 6 gequal, 7 always
	G3_NOZWRITE = 1 << 3, G3_CULL_BACK = 1 << 4, G3_CULL_FRONT = 1 << 5,
	G3_BLEND_SHIFT = 8,				// 0 opaque, 1 alpha, 2 add, 3 multiply, 4 premultiplied
	G3_LINEAR = 1 << 12, G3_WRAP_S_SHIFT = 13, G3_WRAP_T_SHIFT = 15,	// wrap: 0 repeat, 1 clamp, 2 mirror
	G3_NOMATRIX = 1 << 17, G3_ALPHATEST = 1 << 18		// (+ the threshold << 19)
};

struct G3Texture { const unsigned *px; int w, h; };	// 0xAARRGGBB

// ---- the software renderer ------------------------------------------------------------------------------------
struct G3Clip { float x, y, z, w, s, t, r, g, b, a, r2, g2, b2, a2; };	// a vertex in clip space (colours 0..255)

static inline float g3lerp (float a, float b, float f) { return a + (b - a) * f; }

static inline void g3sample (const G3Texture *T, float s, float t, unsigned flags, float out[4])
{
	auto wrap = [] (int i, int n, int mode) -> int
	{
		if (mode == 1) return i < 0 ? 0 : i >= n ? n - 1 : i;
		if (mode == 2)
		{
			int p = 2 * n; i %= p; if (i < 0) i += p;
			return i < n ? i : p - 1 - i;
		}
		i %= n; return i < 0 ? i + n : i;
	};
	int ws = (int) (flags >> G3_WRAP_S_SHIFT) & 3, wt = (int) (flags >> G3_WRAP_T_SHIFT) & 3;
	auto texel = [&] (int x, int y, float k, float *o)
	{
		unsigned c = T->px[wrap (y, T->h, wt) * T->w + wrap (x, T->w, ws)];
		o[0] += k * (float) ((c >> 16) & 255); o[1] += k * (float) ((c >> 8) & 255);
		o[2] += k * (float) (c & 255); o[3] += k * (float) (c >> 24);
	};
	out[0] = out[1] = out[2] = out[3] = 0;
	float u = s * (float) T->w, v = t * (float) T->h;
	if (!(flags & G3_LINEAR))
	{
		int x = (int) u, y = (int) v;
		if ((float) x > u) x--;
		if ((float) y > v) y--;
		texel (x, y, 1, out);
		return;
	}
	u -= 0.5f; v -= 0.5f;
	int x = (int) u, y = (int) v;
	if ((float) x > u) x--;
	if ((float) y > v) y--;
	float fx = u - (float) x, fy = v - (float) y;
	texel (x, y, (1 - fx) * (1 - fy), out); texel (x + 1, y, fx * (1 - fy), out);
	texel (x, y + 1, (1 - fx) * fy, out); texel (x + 1, y + 1, fx * fy, out);
}

// One triangle already in clip space, in front of the near plane (only the rows ry0 .. ry1 - 1:
// a band of the picture, several drawn at once by threads on the PC).
static void g3raster (unsigned *dst, int W, int H, int stride, float *zb, const G3Clip *c, unsigned flags, const G3Texture *T,
		      int ry0 = 0, int ry1 = 1 << 30)
{
	float X[3], Y[3], Z[3], IW[3];
	for (int k = 0; k < 3; k++)
	{
		IW[k] = 1.0f / c[k].w;
		X[k] = (c[k].x * IW[k] + 1) * 0.5f * (float) W;
		Y[k] = (1 - c[k].y * IW[k]) * 0.5f * (float) H;
		Z[k] = c[k].z * IW[k] * 0.5f + 0.5f;
	}
	float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
	if (area == 0) return;
	bool front = area < 0;				// counter-clockwise with y up = clockwise on the screen
	if ((flags & G3_CULL_BACK) && !front) return;
	if ((flags & G3_CULL_FRONT) && front) return;
	int i1 = 1, i2 = 2;
	if (area < 0) { i1 = 2; i2 = 1; area = -area; }	// (edge functions: one orientation)
	int o[3] = { 0, i1, i2 };
	float minX = X[0], maxX = X[0], minY = Y[0], maxY = Y[0];
	for (int k = 1; k < 3; k++)
	{
		if (X[k] < minX) minX = X[k];
		if (X[k] > maxX) maxX = X[k];
		if (Y[k] < minY) minY = Y[k];
		if (Y[k] > maxY) maxY = Y[k];
	}
	int x0 = minX < 0 ? 0 : (int) minX, y0 = minY < 0 ? 0 : (int) minY;
	int x1 = maxX >= (float) W ? W - 1 : (int) maxX, y1 = maxY >= (float) H ? H - 1 : (int) maxY;
	if (y0 < ry0) y0 = ry0;
	if (y1 > ry1 - 1) y1 = ry1 - 1;
	if (y0 > y1) return;
	float inv = 1.0f / area;
	unsigned zf = flags & G3_ZFUNC; if (zf == 0) zf = 1;
	bool zw = !(flags & G3_NOZWRITE) && zf != 7;
	int blend = (int) (flags >> G3_BLEND_SHIFT) & 15;
	// the attributes over w (perspective-correct), per vertex in edge order
	float A[3][10];
	for (int k = 0; k < 3; k++)
	{
		const G3Clip &v = c[o[k]]; float iw = IW[o[k]];
		A[k][0] = v.s * iw; A[k][1] = v.t * iw; A[k][2] = v.r * iw; A[k][3] = v.g * iw; A[k][4] = v.b * iw; A[k][5] = v.a * iw;
		A[k][6] = v.r2 * iw; A[k][7] = v.g2 * iw; A[k][8] = v.b2 * iw; A[k][9] = v.a2 * iw;
	}
	float PX[3] = { X[o[0]], X[o[1]], X[o[2]] }, PY[3] = { Y[o[0]], Y[o[1]], Y[o[2]] };
	float PZ[3] = { Z[o[0]], Z[o[1]], Z[o[2]] }, PW[3] = { IW[o[0]], IW[o[1]], IW[o[2]] };
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++)
		{
			float px = (float) x + 0.5f, py = (float) y + 0.5f;
			float b0 = ((PX[1] - px) * (PY[2] - py) - (PX[2] - px) * (PY[1] - py)) * inv;
			float b1 = ((PX[2] - px) * (PY[0] - py) - (PX[0] - px) * (PY[2] - py)) * inv;
			float b2 = 1 - b0 - b1;
			if (b0 < 0 || b1 < 0 || b2 < 0) continue;
			float z = b0 * PZ[0] + b1 * PZ[1] + b2 * PZ[2];
			if (z < 0 || z > 1) continue;
			float &zr = zb[y * W + x];
			bool pass;
			switch (zf)
			{
			case 2: pass = z == zr; break;
			case 3: pass = z <= zr; break;
			case 4: pass = z > zr; break;
			case 5: pass = z != zr; break;
			case 6: pass = z >= zr; break;
			case 7: pass = true; break;
			default: pass = z < zr;
			}
			if (!pass) continue;
			float iw = b0 * PW[0] + b1 * PW[1] + b2 * PW[2], w = 1.0f / iw;
			float at[10];
			for (int k = 0; k < 10; k++) at[k] = (b0 * A[0][k] + b1 * A[1][k] + b2 * A[2][k]) * w;
			float r = at[2], g = at[3], bl = at[4], a = at[5];
			if (T)
			{
				float tx[4]; g3sample (T, at[0], at[1], flags, tx);
				r *= tx[0] / 255; g *= tx[1] / 255; bl *= tx[2] / 255; a *= tx[3] / 255;
			}
			r += at[6]; g += at[7]; bl += at[8]; a += at[9];		// (the added colour)
			if (r > 255) r = 255;
			if (g > 255) g = 255;
			if (bl > 255) bl = 255;
			if (a > 255) a = 255;
			if ((flags & G3_ALPHATEST) && a < (float) ((flags >> 19) & 255)) continue;
			unsigned &d = dst[(long) y * stride + x];
			float dr = (float) ((d >> 16) & 255), dg = (float) ((d >> 8) & 255), db = (float) (d & 255), fa = a / 255;
			switch (blend)
			{
			case 1: r = r * fa + dr * (1 - fa); g = g * fa + dg * (1 - fa); bl = bl * fa + db * (1 - fa); break;
			case 2: r = r * fa + dr; g = g * fa + dg; bl = bl * fa + db; break;
			case 3: r = r * dr / 255; g = g * dg / 255; bl = bl * db / 255; break;
			case 4: r = r + dr * (1 - fa); g = g + dg * (1 - fa); bl = bl + db * (1 - fa); break;
			default: break;
			}
			auto c8 = [] (float f) -> unsigned { return f <= 0 ? 0u : f >= 255 ? 255u : (unsigned) (f + 0.5f); };
			d = c8 (r) << 16 | c8 (g) << 8 | c8 (bl);
			if (zw) zr = z;
		}
}

// Every triangle of the batches, transformed and clipped against the near plane, in order:
// each(tri[3], flags, texture) -- tex (number -> texture, or 0) resolves the batches' textures.
template <typename TexFn, typename EachFn>
static void swTriangles (const G3Vertex *v, int nv, const G3Batch *bt, int nb, TexFn tex, EachFn each)
{
	static const float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	for (int bi = 0; bi < nb; bi++)
	{
		const G3Batch &B = bt[bi];
		const float *M = (B.flags & G3_NOMATRIX) ? I : B.m;
		const G3Texture *T = B.texture > 0 ? tex (B.texture) : 0;
		for (unsigned t = 0; t + 3 <= B.count && B.first + t + 3 <= (unsigned) nv; t += 3)
		{
			G3Clip in[3];
			for (int k = 0; k < 3; k++)
			{
				const G3Vertex &p = v[B.first + t + k];
				G3Clip &q = in[k];
				q.x = M[0] * p.x + M[1] * p.y + M[2] * p.z + M[3] * p.w;
				q.y = M[4] * p.x + M[5] * p.y + M[6] * p.z + M[7] * p.w;
				q.z = M[8] * p.x + M[9] * p.y + M[10] * p.z + M[11] * p.w;
				q.w = M[12] * p.x + M[13] * p.y + M[14] * p.z + M[15] * p.w;
				q.s = p.s; q.t = p.t; q.r = p.r; q.g = p.g; q.b = p.b; q.a = p.a;
				q.r2 = p.r2; q.g2 = p.g2; q.b2 = p.b2; q.a2 = p.a2;
			}
			// clipped against the near plane (z >= -w, w > 0): a polygon of up to 4 vertices
			G3Clip poly[4]; int n = 0;
			const float EPS = 1e-5f;
			for (int k = 0; k < 3; k++)
			{
				const G3Clip &a = in[k], &b = in[(k + 1) % 3];
				float da = a.z + a.w, db = b.z + b.w;
				bool ina = da >= 0 && a.w > EPS, inb = db >= 0 && b.w > EPS;
				if (ina) poly[n++] = a;
				if (ina != inb && n < 4)
				{
					float f = da / (da - db);
					G3Clip &q = poly[n++];
					q.x = g3lerp (a.x, b.x, f); q.y = g3lerp (a.y, b.y, f); q.z = g3lerp (a.z, b.z, f); q.w = g3lerp (a.w, b.w, f);
					q.s = g3lerp (a.s, b.s, f); q.t = g3lerp (a.t, b.t, f); q.r = g3lerp (a.r, b.r, f);
					q.g = g3lerp (a.g, b.g, f); q.b = g3lerp (a.b, b.b, f); q.a = g3lerp (a.a, b.a, f);
					q.r2 = g3lerp (a.r2, b.r2, f); q.g2 = g3lerp (a.g2, b.g2, f); q.b2 = g3lerp (a.b2, b.b2, f); q.a2 = g3lerp (a.a2, b.a2, f);
					if (q.w <= EPS) n--;
				}
			}
			for (int k = 1; k + 1 < n; k++)
			{
				G3Clip tri[3] = { poly[0], poly[k], poly[k + 1] };
				each (tri, B.flags, T);
			}
		}
	}
}

// The whole frame into dst (0x00RRGGBB, W x H, stride pixels a row); zb: W * H floats.
template <typename TexFn>
static void swRender (unsigned *dst, int W, int H, int stride, float *zb, const G3Vertex *v, int nv,
		      const G3Batch *bt, int nb, unsigned clear, bool keep, TexFn tex)
{
	if (!keep) for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) dst[(long) y * stride + x] = clear;
	for (int i = 0; i < W * H; i++) zb[i] = 1.0f;
	swTriangles (v, nv, bt, nb, tex, [&] (const G3Clip *tri, unsigned flags, const G3Texture *T)
	{
		g3raster (dst, W, H, stride, zb, tri, flags, T);
	});
}

} // namespace bas

#endif
