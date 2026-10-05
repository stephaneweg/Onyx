//
// 3dforge/frender.h -- the 3D view's drawing: the camera (orthographic, turned around its target), a frame as
// triangles in batches -- the bodies lit and transformed by a matrix, the lines (edges, grid) as thin quads --
// drawn by the GPU (kapi_gpu_render) or, without one, by the z-buffer here over the very same triangles; and
// picking (a ray against the bodies' triangles). The frame is drawn larger than shown (SS times) and scaled down:
// smooth edges.
//
// MIT licence (Onyx).
//
#ifndef _3dforge_frender_h
#define _3dforge_frender_h

#include "appkit/appkit.h"
#include "fdoc.h"

namespace forge {

struct Cam
{
	double az, el, scale;		// degrees; pixels a millimetre
	V3 t;				// the point looked at (the centre of the view)
	int w, h;			// the view, pixels
	V3 d, r, u;			// toward the eye, right, up
	Cam () : az (-58), el (28), scale (4), w (100), h (100) { set (); }
	void set ()
	{
		double a = az * PI / 180, e = el * PI / 180;
		d = V3 (cos (e) * cos (a), cos (e) * sin (a), sin (e));
		r = V3 (-sin (a), cos (a), 0);
		u = V3 (-sin (e) * cos (a), -sin (e) * sin (a), cos (e));
	}
	void screen (const V3 &p, double *x, double *y, double *depth = 0) const
	{
		V3 q = p - t; *x = w / 2.0 + dot (q, r) * scale; *y = h / 2.0 - dot (q, u) * scale;
		if (depth) *depth = dot (q, d);
	}
	// The point of the view's plane under a pixel (the ray goes from it along -d, and comes from +d).
	V3 under (double x, double y) const { return t + r * ((x - w / 2.0) / scale) + u * ((h / 2.0 - y) / scale); }
	// Where the ray under a pixel meets a plane (false: it runs along it).
	bool onPlane (double x, double y, const Plane &pl, V3 *out) const
	{
		V3 o = under (x, y); double dn = dot (d, pl.n);
		if (fabs (dn) < 1e-6) return false;
		*out = o + d * (dot (pl.o - o, pl.n) / dn); return true;
	}
	// How far along the line c + k n the ray under a pixel passes the nearest.
	double along (double x, double y, const V3 &c, const V3 &n) const
	{
		V3 o = under (x, y), w0 = c - o; double b = dot (n, d), den = 1 - b * b;
		if (den < 1e-6) return 0;
		return (b * dot (d, w0) - dot (n, w0)) / den;
	}
};

struct Hit { int body, tri; V3 p; double depth; };

// The nearest triangle of the shown bodies under a pixel.
static bool pick (const Doc &doc, const Cam &cam, double x, double y, Hit *hit)
{
	V3 o = cam.under (x, y), dir = -cam.d; bool got = false; double best = -1e30;
	for (size_t bi = 0; bi < doc.bodies.size (); bi++)
	{
		const Body &b = doc.bodies[bi]; bool vis = true;
		for (const BodyProp &p : doc.props) if (p.id == b.id) vis = p.visible;
		if (!vis) continue;
		const RMesh &m = b.mesh;
		for (int i = 0; i < m.tris (); i++)
		{
			if (dot (m.n[i], dir) >= 0) continue;
			const V3 &a = m.v[m.t[i * 3]]; V3 e1 = m.v[m.t[i * 3 + 1]] - a, e2 = m.v[m.t[i * 3 + 2]] - a;
			V3 pv = cross (dir, e2); double det = dot (e1, pv);
			if (fabs (det) < 1e-12) continue;
			V3 tv = o - a; double uu = dot (tv, pv) / det;
			if (uu < -1e-9 || uu > 1 + 1e-9) continue;
			V3 qv = cross (tv, e1); double vv = dot (dir, qv) / det;
			if (vv < -1e-9 || uu + vv > 1 + 1e-9) continue;
			double k = dot (e2, qv) / det, depth = -k;		// (k along -d: the nearest has the largest depth)
			if (depth > best) { best = depth; got = true; hit->body = (int) bi; hit->tri = i; hit->p = o + dir * k; hit->depth = dot (hit->p - cam.t, cam.d); }
		}
	}
	return got;
}

// ---- a frame: vertices and batches (kapi_gpu_render's own structures) ----------------------------------------------
static const int SS = 2;
struct Scene
{
	std::vector<kapi_gpu_vertex3> v;
	std::vector<kapi_gpu_batch> b;
	int W, H;			// the target (SS times the view)
	double R;			// half the depth range, millimetres
	const Cam *cam;
	std::vector<unsigned> rt; std::vector<float> zb;

	void begin (const Cam &c, double radius)
	{
		cam = &c; W = c.w * SS; H = c.h * SS; R = radius < 200 ? 200 : radius;
		v.clear (); b.clear ();
		if ((int) rt.size () != W * H) rt.assign (W * H, 0);
	}
	void batch (unsigned flags, bool matrix)
	{
		if (!b.empty ()) b.back ().count = (unsigned) v.size () - b.back ().first;
		kapi_gpu_batch k; memset (&k, 0, sizeof k);
		k.first = (unsigned) v.size (); k.texture = -1; k.flags = flags | (matrix ? 0 : KAPI_GPU_B_NOMATRIX);
		if (matrix)
		{
			// clip = M (x y z 1): x across the view, y up, z -1 (near) .. 1 (far)
			const Cam &c = *cam; double sx = c.scale * 2 / c.w, sy = c.scale * 2 / c.h, sz = -1 / R;
			float *m = k.matrix;
			m[0] = (float) (c.r.x * sx); m[1] = (float) (c.r.y * sx); m[2] = (float) (c.r.z * sx); m[3] = (float) (-dot (c.t, c.r) * sx);
			m[4] = (float) (c.u.x * sy); m[5] = (float) (c.u.y * sy); m[6] = (float) (c.u.z * sy); m[7] = (float) (-dot (c.t, c.u) * sy);
			m[8] = (float) (c.d.x * sz); m[9] = (float) (c.d.y * sz); m[10] = (float) (c.d.z * sz); m[11] = (float) (-dot (c.t, c.d) * sz);
			m[15] = 1;
		}
		b.push_back (k);
	}
	void end () { if (!b.empty ()) b.back ().count = (unsigned) v.size () - b.back ().first; }
	void vert (double x, double y, double z, unsigned rgb, int a = 255)
	{
		kapi_gpu_vertex3 k; memset (&k, 0, sizeof k);
		k.x = (float) x; k.y = (float) y; k.z = (float) z; k.w = 1;
		k.r = (unsigned char) (rgb >> 16); k.g = (unsigned char) (rgb >> 8); k.b = (unsigned char) rgb; k.a = (unsigned char) a;
		v.push_back (k);
	}
	// A point of the model -> the clip space of a batch without matrix; bias: millimetres toward the eye.
	void clip (const V3 &p, double bias, double *x, double *y, double *z) const
	{
		V3 q = p - cam->t;
		*x = dot (q, cam->r) * cam->scale * 2 / cam->w; *y = dot (q, cam->u) * cam->scale * 2 / cam->h; *z = -(dot (q, cam->d) + bias) / R;
	}
	// A line of the model, px wide on the screen (a quad).
	void line (const V3 &p0, const V3 &p1, double px, unsigned rgb, int a = 255, double bias = 0)
	{
		double x0, y0, z0, x1, y1, z1; clip (p0, bias, &x0, &y0, &z0); clip (p1, bias, &x1, &y1, &z1);
		double dx = (x1 - x0) * cam->w, dy = (y1 - y0) * cam->h, l = sqrt (dx * dx + dy * dy);
		if (l < 1e-9) return;
		double nx = -dy / l * px / cam->w, ny = dx / l * px / cam->h;
		vert (x0 + nx, y0 + ny, z0, rgb, a); vert (x0 - nx, y0 - ny, z0, rgb, a); vert (x1 - nx, y1 - ny, z1, rgb, a);
		vert (x0 + nx, y0 + ny, z0, rgb, a); vert (x1 - nx, y1 - ny, z1, rgb, a); vert (x1 + nx, y1 + ny, z1, rgb, a);
	}
	// The background: two colours, top to bottom.
	void backdrop (unsigned top, unsigned bottom)
	{
		batch (KAPI_GPU_B_ZFUNC (KAPI_GPU_Z_ALWAYS) | KAPI_GPU_B_NOZWRITE, false);
		vert (-1, 1, 0.999, top); vert (-1, -1, 0.999, bottom); vert (1, -1, 0.999, bottom);
		vert (-1, 1, 0.999, top); vert (1, -1, 0.999, bottom); vert (1, 1, 0.999, top);
	}
};

static inline unsigned shade_rgb (unsigned c, double k)
{
	int r = (int) (((c >> 16) & 255) * k), g = (int) (((c >> 8) & 255) * k), b = (int) ((c & 255) * k);
	if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
	return (unsigned) (r << 16 | g << 8 | b);
}
static inline unsigned mix_rgb (unsigned a, unsigned b, double t)
{
	int r = (int) (((a >> 16) & 255) * (1 - t) + ((b >> 16) & 255) * t), g = (int) (((a >> 8) & 255) * (1 - t) + ((b >> 8) & 255) * t), bl = (int) ((a & 255) * (1 - t) + (b & 255) * t);
	return (unsigned) (r << 16 | g << 8 | bl);
}
// How lit a face of normal n is: a light over the eye's left shoulder, a weaker one from the right.
static inline double lit (const Cam &c, const V3 &n)
{
	static const double k1 = 1 / sqrt (0.74 * 0.74 + 0.52 * 0.52 + 0.34 * 0.34);
	double a = (dot (n, c.d) * 0.74 + dot (n, c.u) * 0.52 - dot (n, c.r) * 0.34) * k1, b = dot (n, c.r) * 0.8 + dot (n, c.d) * 0.6;
	return 0.54 + 0.54 * (a > 0 ? a : 0) + 0.10 * (b > 0 ? b : 0);
}

// A body's triangles into the current batch (with a matrix). tint: faces shown chosen (their colour), -1 none.
static void scene_body (Scene &sc, const RMesh &m, unsigned colour, int alpha, int tintFace = -1, unsigned tint = 0, double fade = 0, unsigned fadeTo = 0)
{
	const Cam &c = *sc.cam;
	for (int i = 0; i < m.tris (); i++)
	{
		if (dot (m.n[i], c.d) <= 0) continue;
		double k = lit (c, m.n[i]);
		unsigned col = shade_rgb (colour, k);
		if (m.grp[i] == tintFace) col = mix_rgb (col, shade_rgb (tint, 0.72 + 0.34 * k), 0.7);
		if (fade > 0) col = mix_rgb (col, fadeTo, fade);
		for (int j = 0; j < 3; j++) { const V3 &p = m.v[m.t[i * 3 + j]]; sc.vert (p.x, p.y, p.z, col, alpha); }
	}
}
// Its lines: where two faces meet, and the outline of a curved face (an edge between a triangle turned to the eye and
// one turned away). Into the current batch (no matrix).
static void scene_edges (Scene &sc, const RMesh &m, unsigned colour, double px, int alpha = 255)
{
	const Cam &c = *sc.cam; double bias = 2.0 / c.scale;
	for (const Chain &ch : m.chains)
	{
		int n = (int) ch.pts.size ();
		for (int k = 0; k < (ch.closed ? n : n - 1); k++) sc.line (m.v[ch.pts[k]], m.v[ch.pts[(k + 1) % n]], px * SS, colour, alpha, bias);
	}
	for (size_t i = 0; i + 3 < m.smooth.size (); i += 4)
	{
		bool f0 = dot (m.n[m.smooth[i + 2]], c.d) > 0, f1 = dot (m.n[m.smooth[i + 3]], c.d) > 0;
		if (f0 != f1) sc.line (m.v[m.smooth[i]], m.v[m.smooth[i + 1]], px * SS, colour, alpha, bias);
	}
}

// ---- without a GPU: the same frame by the processor ----------------------------------------------------------------
static void soft_render (Scene &sc, unsigned clear)
{
	int W = sc.W, H = sc.H;
	if ((int) sc.zb.size () != W * H) sc.zb.assign (W * H, 0);
	for (int i = 0; i < W * H; i++) { sc.rt[i] = clear; sc.zb[i] = 2; }
	for (const kapi_gpu_batch &bt : sc.b)
	{
		int zf = bt.flags & 7; bool zwrite = !(bt.flags & KAPI_GPU_B_NOZWRITE), cull = (bt.flags & KAPI_GPU_B_CULL_BACK) != 0;
		bool blend = ((bt.flags >> 8) & 15) == KAPI_GPU_BLEND_ALPHA, mat = !(bt.flags & KAPI_GPU_B_NOMATRIX);
		const float *m = bt.matrix;
		for (unsigned i = bt.first; i + 2 < bt.first + bt.count; i += 3)
		{
			float X[3], Y[3], Z[3];
			for (int k = 0; k < 3; k++)
			{
				const kapi_gpu_vertex3 &q = sc.v[i + k]; float x = q.x, y = q.y, z = q.z;
				if (mat) { x = m[0] * q.x + m[1] * q.y + m[2] * q.z + m[3]; y = m[4] * q.x + m[5] * q.y + m[6] * q.z + m[7]; z = m[8] * q.x + m[9] * q.y + m[10] * q.z + m[11]; }
				X[k] = (x * 0.5f + 0.5f) * W; Y[k] = (0.5f - y * 0.5f) * H; Z[k] = z;
			}
			float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);	// (y down: a front face is negative)
			if (area == 0 || (cull && area > 0)) continue;
			int x0 = (int) floorf (std::min (X[0], std::min (X[1], X[2]))), x1 = (int) ceilf (std::max (X[0], std::max (X[1], X[2])));
			int y0 = (int) floorf (std::min (Y[0], std::min (Y[1], Y[2]))), y1 = (int) ceilf (std::max (Y[0], std::max (Y[1], Y[2])));
			if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W - 1) x1 = W - 1; if (y1 > H - 1) y1 = H - 1;
			const kapi_gpu_vertex3 &c0 = sc.v[i], &c1 = sc.v[i + 1], &c2 = sc.v[i + 2];
			bool flatc = c0.r == c1.r && c0.g == c1.g && c0.b == c1.b && c0.r == c2.r && c0.g == c2.g && c0.b == c2.b && c0.a == c1.a && c0.a == c2.a;
			float inv = 1 / area;
			for (int y = y0; y <= y1; y++)
			{
				float py = y + 0.5f;
				for (int x = x0; x <= x1; x++)
				{
					float px = x + 0.5f;
					float w0 = ((X[1] - px) * (Y[2] - py) - (X[2] - px) * (Y[1] - py)) * inv;
					float w1 = ((X[2] - px) * (Y[0] - py) - (X[0] - px) * (Y[2] - py)) * inv;
					float w2 = 1 - w0 - w1;
					if (w0 < 0 || w1 < 0 || w2 < 0) continue;
					float z = w0 * Z[0] + w1 * Z[1] + w2 * Z[2]; float &zz = sc.zb[y * W + x];
					if (z < -1 || z > 1) continue;
					if (zf == KAPI_GPU_Z_LESS ? !(z < zz) : zf == KAPI_GPU_Z_LEQUAL ? !(z <= zz) : false) continue;
					int r, g, b, a;
					if (flatc) { r = c0.r; g = c0.g; b = c0.b; a = c0.a; }
					else
					{
						r = (int) (w0 * c0.r + w1 * c1.r + w2 * c2.r); g = (int) (w0 * c0.g + w1 * c1.g + w2 * c2.g);
						b = (int) (w0 * c0.b + w1 * c1.b + w2 * c2.b); a = (int) (w0 * c0.a + w1 * c1.a + w2 * c2.a);
					}
					unsigned &d = sc.rt[y * W + x];
					if (blend && a < 255)
					{
						int dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
						r = (r * a + dr * (255 - a)) / 255; g = (g * a + dg * (255 - a)) / 255; b = (b * a + db * (255 - a)) / 255;
					}
					d = (unsigned) (r << 16 | g << 8 | b);
					if (zwrite) zz = z;
				}
			}
		}
	}
}

static int g_gpu = -1;			// 1: the GPU draws; 0: the processor (none, or it failed once)
static bool g_gpuOff = false;		// (the user's choice: View > Draw with the processor)

// Draw the frame and bring it down into dst (w x h of the camera, stride pixels a row). true: by the GPU.
static bool scene_show (Scene &sc, unsigned clear, unsigned *dst, int stride)
{
	sc.end ();
	if (g_gpu < 0) { char info[96]; g_gpu = kapi_gpu_info (info, sizeof info) == 1 ? 1 : 0; }
	bool gpu = false;
	// (the kernel's limits: 65 536 triangles a frame, a target of 2048 at most -- past them, the processor draws)
	if (g_gpu == 1 && !g_gpuOff && !sc.v.empty () && sc.v.size () <= KAPI_GPU_MAX_VERTS && sc.b.size () <= KAPI_GPU_MAX_BATCHES && sc.W <= 2048 && sc.H <= 2048)
	{
		kapi_gpu_frame f; f.pixels = &sc.rt[0]; f.w = sc.W; f.h = sc.H; f.stride = sc.W; f.clear = clear; f.flags = 0;
		if (kapi_gpu_render (&f, &sc.v[0], (unsigned) sc.v.size (), &sc.b[0], (unsigned) sc.b.size ()) == 0) gpu = true;
		else g_gpu = 0;
	}
	if (!gpu) soft_render (sc, clear);
	int w = sc.cam->w, h = sc.cam->h;
	for (int y = 0; y < h; y++)
	{
		const unsigned *s0 = &sc.rt[(y * SS) * sc.W], *s1 = s0 + sc.W; unsigned *o = dst + y * stride;
		for (int x = 0; x < w; x++)
		{
			unsigned a = s0[x * 2], b = s0[x * 2 + 1], c = s1[x * 2], e = s1[x * 2 + 1];
			unsigned rb = ((a & 0xFF00FF) + (b & 0xFF00FF) + (c & 0xFF00FF) + (e & 0xFF00FF) + 0x020002) >> 2 & 0xFF00FF;
			unsigned g = ((a & 0x00FF00) + (b & 0x00FF00) + (c & 0x00FF00) + (e & 0x00FF00) + 0x000200) >> 2 & 0x00FF00;
			o[x] = rb | g;
		}
	}
	return gpu;
}

} // namespace forge

#endif
