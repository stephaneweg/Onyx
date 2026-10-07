//
// gpctest -- the GPU compositing service (user/Libs/gpucomp) on the PC: each scene composited three
// ways -- the CPU path (GPC_F_CPU), the GPU path on the software V3D of hostkapi.cpp (the kernel's
// FS_TEX in the QPU simulator) and a reference in doubles written here from the definitions
// (gpucomp.h) -- and the pictures compared channel by channel. Then the GPU's partial updates,
// textures the GPU refuses (drawn by the CPU between GPU frames), the GPU lost in the middle, and
// the CPU path's time at 1920 x 1080. sh tools/tests/run_gpucomp_test.sh
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <vector>
#include "appkit/appkit.h"
#include "gpucomp/gpucomp.h"

extern "C" void hostkapi_soft (int on);
extern "C" void hostkapi_hang (void);
extern "C" void hostkapi_refuse (int n);

static void *al (unsigned long n) { return malloc (n); }
static void fr (void *p) { free (p); }
static int g_fail = 0, g_tests = 0;

// ---- textures: smooth colour ramps + noise + an alpha pattern, premultiplied -----------------------------
static unsigned g_seed = 1;
static unsigned rnd (void) { g_seed = g_seed * 1103515245u + 12345u; return g_seed >> 8; }
static std::vector<unsigned> makeTex (int w, int h, int kind)
{
	std::vector<unsigned> px ((size_t) w * h);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			unsigned a = 255;
			if (kind == 1) a = (unsigned) (128 + 127 * sin (x * 0.07) * cos (y * 0.05));	// translucent waves
			if (kind == 2) a = ((x / 8 + y / 8) & 1) ? 255 : 60;				// checker of alpha
			unsigned r = (unsigned) (x * 255 / (w > 1 ? w - 1 : 1)), g = (unsigned) (y * 255 / (h > 1 ? h - 1 : 1));
			unsigned b = (unsigned) ((x ^ y) & 255);
			if (kind == 3) { r = (x * 37 + y * 11) & 255; g = (x * 5 + y * 71) & 255; b = rnd () & 255; }	// sharp (tiles)
			r = (r + (rnd () & 7)) & 255;
			r = r * a / 255; g = g * a / 255; b = b * a / 255;
			px[(size_t) y * w + x] = a << 24 | r << 16 | g << 8 | b;
		}
	return px;
}

// ---- the reference (doubles) -----------------------------------------------------------------------------------
struct RefTex { int w, h; const unsigned *px; };
static void refSample (const RefTex &t, double u, double v, bool nearest, double out[4])
{
	auto at = [&] (int x, int y, double o[4]) {
		x = x < 0 ? 0 : x >= t.w ? t.w - 1 : x; y = y < 0 ? 0 : y >= t.h ? t.h - 1 : y;
		unsigned c = t.px[(size_t) y * t.w + x];
		o[0] = (c >> 16) & 255; o[1] = (c >> 8) & 255; o[2] = c & 255; o[3] = c >> 24;
	};
	if (nearest) { at ((int) floor (u), (int) floor (v), out); return; }
	double fx = u - 0.5, fy = v - 0.5;
	int x0 = (int) floor (fx), y0 = (int) floor (fy);
	double ax = fx - x0, ay = fy - y0, a[4], b[4], c[4], d[4];
	at (x0, y0, a); at (x0 + 1, y0, b); at (x0, y0 + 1, c); at (x0 + 1, y0 + 1, d);
	for (int k = 0; k < 4; k++) out[k] = (a[k] * (1 - ax) + b[k] * ax) * (1 - ay) + (c[k] * (1 - ax) + d[k] * ax) * ay;
}
static void reference (std::vector<double> &buf, int W, int H, bool alpha, const gpc_layer *Ls, const RefTex *texs, int n)
{
	for (int i = 0; i < n; i++)
	{
		const gpc_layer &L = Ls[i];
		const RefTex &t = texs[i];
		double det = (double) L.m.a * L.m.d - (double) L.m.b * L.m.c;
		double ia = L.m.d / det, ib = -L.m.b / det, ic = -L.m.c / det, id = L.m.a / det;
		double lx0 = fmax (0, -L.src_x), lx1 = fmin (L.src_w, t.w - L.src_x), ly0 = fmax (0, -L.src_y), ly1 = fmin (L.src_h, t.h - L.src_y);
		int cx0 = 0, cy0 = 0, cx1 = W, cy1 = H;
		if (L.clip[2] > 0 && L.clip[3] > 0)
		{ cx0 = std::max (0, L.clip[0]); cy0 = std::max (0, L.clip[1]); cx1 = std::min (W, L.clip[0] + L.clip[2]); cy1 = std::min (H, L.clip[1] + L.clip[3]); }
		for (int y = cy0; y < cy1; y++)
			for (int x = cx0; x < cx1; x++)
			{
				double X = x + 0.5 - L.m.e, Y = y + 0.5 - L.m.f;
				double lx = ia * X + ic * Y, ly = ib * X + id * Y;
				if (!(lx >= lx0 - 1e-7 && lx < lx1 - 1e-7 && ly >= ly0 - 1e-7 && ly < ly1 - 1e-7)) continue;	// (top-left)
				double s[4]; refSample (t, L.src_x + lx, L.src_y + ly, (L.flags & GPC_L_NEAREST) != 0, s);
				double *d = &buf[((size_t) y * W + x) * 4];
				double op = L.opacity / 255.0;
				for (int k = 0; k < 4; k++) s[k] *= op;
				if (L.blend == GPC_B_MASK) { refSample (t, L.src_x + lx, L.src_y + ly, (L.flags & GPC_L_NEAREST) != 0, s); op = 1; }
				double sa = s[3] / 255.0, da = alpha ? d[3] / 255.0 : 1.0, ia2 = 1 - sa;
				for (int k = 0; k < 3; k++)
				{
					double sc = s[k], dc = d[k], c;
					switch (L.blend)
					{
					case GPC_B_MULTIPLY: c = sc * dc / 255 + dc * ia2 + sc * (1 - da); break;
					case GPC_B_SCREEN: c = sc + dc - sc * dc / 255; break;
					case GPC_B_ADD: c = fmin (255, sc + dc); break;
					case GPC_B_SUBTRACT: c = fmax (0, dc - sc) + sc * (1 - da); break;
					case GPC_B_LIGHTEN: c = fmax (sc, dc); break;
					case GPC_B_MASK: c = dc * sa; break;
					case GPC_B_CUTOUT: c = dc * ia2; break;
					default: c = sc + dc * ia2; break;
					}
					d[k] = fmin (255, c);
				}
				if (alpha) d[3] = L.blend == GPC_B_MASK ? d[3] * sa : L.blend == GPC_B_CUTOUT ? d[3] * ia2 : s[3] + d[3] * ia2;
			}
	}
}

// ---- comparing ---------------------------------------------------------------------------------------------------
// pixels whose channels differ by more than tol; *maxd the largest difference
static long diff (const unsigned *a, const std::vector<double> &ref, int W, int H, bool alpha, int tol, int *maxd, int *wx, int *wy)
{
	long n = 0; *maxd = 0;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
		{
			unsigned c = a[(size_t) y * W + x];
			const double *r = &ref[((size_t) y * W + x) * 4];
			int got[4] = { (int) ((c >> 16) & 255), (int) ((c >> 8) & 255), (int) (c & 255), (int) (c >> 24) };
			int m = 0;
			for (int k = 0; k < (alpha ? 4 : 3); k++) { int d = abs (got[k] - (int) lrint (r[k])); if (d > m) m = d; }
			if (m > *maxd) { *maxd = m; *wx = x; *wy = y; }
			if (m > tol) n++;
		}
	return n;
}

static void check (const char *what, bool ok, const char *detail)
{
	g_tests++;
	printf ("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail[0] ? "  " : "", detail);
	if (!ok) g_fail++;
}

// a scene: the textures, the layers (tex index), the target -> composited by both paths, compared
struct Scene
{
	const char *name;
	int W, H; bool alpha; unsigned clear;
	std::vector<std::vector<unsigned>> tex; std::vector<int> tw, th;
	std::vector<gpc_layer> layers; std::vector<int> li;		// (layers[i].tex: set per context)
	long edgeAllow;						// pixels allowed off (coverage ties on slanted edges)
};

static void runScene (Scene &S)
{
	std::vector<double> ref ((size_t) S.W * S.H * 4);
	for (int i = 0; i < S.W * S.H; i++)
	{
		unsigned c = S.alpha ? S.clear : S.clear & 0xFFFFFF;
		ref[i * 4] = (c >> 16) & 255; ref[i * 4 + 1] = (c >> 8) & 255; ref[i * 4 + 2] = c & 255; ref[i * 4 + 3] = c >> 24;
	}
	std::vector<RefTex> rt;
	for (size_t i = 0; i < S.layers.size (); i++) rt.push_back ({ S.tw[S.li[i]], S.th[S.li[i]], S.tex[S.li[i]].data () });
	reference (ref, S.W, S.H, S.alpha, S.layers.data (), rt.data (), (int) S.layers.size ());
	for (int soft = 0; soft < 2; soft++)
	{
		hostkapi_soft (soft);
		gpc_config cfg = { al, fr, soft ? 0u : (unsigned) GPC_F_CPU };
		gpc_ctx *g = gpc_create (&cfg);
		std::vector<gpc_tex *> T;
		for (size_t k = 0; k < S.tex.size (); k++) T.push_back (gpc_tex_create (g, S.tw[k], S.th[k], S.tex[k].data (), S.tw[k]));
		std::vector<gpc_layer> L = S.layers;
		for (size_t i = 0; i < L.size (); i++) L[i].tex = T[S.li[i]];
		std::vector<unsigned> out ((size_t) S.W * S.H, 0xDEADBEEF);
		gpc_target t = { out.data (), S.W, S.H, S.W, S.alpha ? (unsigned) GPC_T_ALPHA : 0u };
		int r = gpc_composite (g, &t, L.data (), (int) L.size (), S.clear, GPC_C_CLEAR);
		gpc_stats st; gpc_get_stats (g, &st);
		int maxd = 0, wx = -1, wy = -1;
		long bad = diff (out.data (), ref, S.W, S.H, S.alpha, soft ? 3 : 2, &maxd, &wx, &wy);
		char name[160], det[200];
		snprintf (name, sizeof name, "%-34s %s", S.name, soft ? "GPU (software V3D)" : "CPU");
		snprintf (det, sizeof det, "(result %d, %u gpu_render, max diff %d at %d,%d, %ld pixels over the tolerance, %ld allowed)",
			  r, st.last_gpu_calls, maxd, wx, wy, bad, S.edgeAllow);
		bool ok = r == 0 && bad <= S.edgeAllow && (soft ? st.last_gpu_calls > 0 && gpc_backend (g) == GPC_BACKEND_GPU : st.last_gpu_calls == 0);
		check (name, ok, det);
		gpc_destroy (g);
	}
}

static gpc_layer layer (float sx, float sy, float sw, float sh, unsigned op = 255, unsigned flags = 0)
{
	gpc_layer L; gpc_layer_init (&L, 0);
	L.src_x = sx; L.src_y = sy; L.src_w = sw; L.src_h = sh; L.opacity = op; L.flags = flags;
	return L;
}

int main (void)
{
	// ---- the scenes
	std::vector<Scene> scenes;
	auto base = [] (const char *n, int W, int H, bool alpha, unsigned clear) { Scene s; s.name = n; s.W = W; s.H = H; s.alpha = alpha; s.clear = clear; s.edgeAllow = 0; return s; };
	auto addTex = [] (Scene &s, int w, int h, int kind) { s.tex.push_back (makeTex (w, h, kind)); s.tw.push_back (w); s.th.push_back (h); };
	{
		Scene s = base ("identity + whole-texel offsets", 320, 240, false, 0x203040);
		addTex (s, 300, 200, 0); addTex (s, 120, 90, 1);
		gpc_layer a = layer (0, 0, 300, 200, 255, GPC_L_OPAQUE); gpc_matrix_translate (&a.m, 7, 11);
		gpc_layer b = layer (0, 0, 120, 90); gpc_matrix_translate (&b.m, 150, 100);
		s.layers = { a, b }; s.li = { 0, 1 }; scenes.push_back (s);
	}
	{
		Scene s = base ("scroll: fractional source offset", 256, 160, false, 0xFFFFFF);
		addTex (s, 256, 600, 3);
		gpc_layer a = layer (0, 123.37f, 256, 160); a.flags = GPC_L_OPAQUE;
		s.layers = { a }; s.li = { 0 }; scenes.push_back (s);
	}
	{
		// (scales whose edges land on pixel centres exactly: the tie goes the same way on both
		// sides; with 1.7 the edge at 20.5 + 70 x 1.7f is 1/300000 pixel below a centre, which
		// the CPU sees and the GPU's 1/256-pixel vertex positions do not -- a row of difference)
		Scene s = base ("scale 1.75 and 0.4375, opacity", 300, 220, false, 0x000000);
		addTex (s, 90, 70, 0); addTex (s, 200, 160, 2);
		gpc_layer a = layer (0, 0, 90, 70); gpc_matrix_translate (&a.m, 10.25f, 20.5f); gpc_matrix_scale (&a.m, 1.75f, 1.75f);
		gpc_layer b = layer (0, 0, 200, 160, 200); gpc_matrix_translate (&b.m, 180, 60); gpc_matrix_scale (&b.m, 0.4375f, 0.4375f);
		s.layers = { a, b }; s.li = { 0, 1 }; scenes.push_back (s);
	}
	{
		Scene s = base ("scale 1.7 (edges near pixel centres)", 300, 220, false, 0x000000);
		addTex (s, 90, 70, 0);
		gpc_layer a = layer (0, 0, 90, 70); gpc_matrix_translate (&a.m, 10.25f, 20.5f); gpc_matrix_scale (&a.m, 1.7f, 1.7f);
		s.layers = { a }; s.li = { 0 }; s.edgeAllow = 160; scenes.push_back (s);
	}
	{
		Scene s = base ("rotate 30 degrees about the centre, opacity 180", 300, 300, false, 0x404040);
		addTex (s, 160, 120, 1);
		gpc_layer a = layer (0, 0, 160, 120, 180); gpc_matrix_translate (&a.m, 150, 150); gpc_matrix_rotate (&a.m, 0.5235988f); gpc_matrix_translate (&a.m, -80, -60);
		s.layers = { a }; s.li = { 0 }; s.edgeAllow = 12; scenes.push_back (s);
	}
	{
		Scene s = base ("skew, clip rectangle", 260, 200, false, 0x102030);
		addTex (s, 140, 100, 0);
		gpc_layer a = layer (0, 0, 140, 100); gpc_matrix_translate (&a.m, 40, 40); gpc_matrix_skew (&a.m, 0.3f, 0.1f);
		a.clip[0] = 60; a.clip[1] = 50; a.clip[2] = 120; a.clip[3] = 80;
		s.layers = { a }; s.li = { 0 }; s.edgeAllow = 8; scenes.push_back (s);
	}
	{
		Scene s = base ("nearest, scale 2", 200, 160, false, 0x000000);
		addTex (s, 60, 50, 3);
		gpc_layer a = layer (0, 0, 60, 50, 255, GPC_L_NEAREST); gpc_matrix_translate (&a.m, 13, 9); gpc_matrix_scale (&a.m, 2, 2);
		s.layers = { a }; s.li = { 0 }; scenes.push_back (s);
	}
	{
		Scene s = base ("an ARGB target (GPC_T_ALPHA)", 200, 150, true, 0x00000000);
		addTex (s, 120, 100, 1); addTex (s, 100, 80, 2);
		gpc_layer a = layer (0, 0, 120, 100, 230); gpc_matrix_translate (&a.m, 10, 10);
		gpc_layer b = layer (0, 0, 100, 80, 140); gpc_matrix_translate (&b.m, 70, 50); gpc_matrix_scale (&b.m, 1.2f, 1.1f);
		s.layers = { a, b }; s.li = { 0, 1 }; scenes.push_back (s);
	}
	// the blend modes, over an ARGB target (a translucent layer under) and an opaque one
	static const char *const BN[GPC_B_COUNT] = { "normal", "multiply", "screen", "add", "subtract", "lighten", "mask", "cut out" };
	static char bnames[2][GPC_B_COUNT][64];
	for (int al = 0; al < 2; al++)
		for (int bm = 1; bm < GPC_B_COUNT; bm++)
		{
			snprintf (bnames[al][bm], sizeof bnames[al][bm], "blend %s, %s target", BN[bm], al ? "ARGB" : "opaque");
			Scene s = base (bnames[al][bm], 200, 150, al != 0, al ? 0x00000000 : 0x406080);
			addTex (s, 140, 110, al ? 1 : 0); addTex (s, 120, 90, 2);
			gpc_layer a = layer (0, 0, 140, 110, 255); gpc_matrix_translate (&a.m, 10, 10);
			gpc_layer b = layer (0, 0, 120, 90, 200); gpc_matrix_translate (&b.m, 60, 40); b.blend = (unsigned) bm;
			s.layers = { a, b }; s.li = { 0, 1 }; scenes.push_back (s);
		}
	{
		Scene s = base ("tiles: a 2200-texel-wide texture's seam x3", 320, 64, false, 0x000000);
		addTex (s, 2200, 40, 3);
		gpc_layer a = layer (2000, 0, 100, 20); gpc_matrix_scale (&a.m, 3.2f, 3.1f);
		s.layers = { a }; s.li = { 0 }; scenes.push_back (s);
	}
	{
		Scene s = base ("tiles: a 2200-texel-tall texture, rotated", 200, 200, false, 0x000000);
		addTex (s, 40, 2200, 3);
		gpc_layer a = layer (0, 2010, 40, 70); gpc_matrix_translate (&a.m, 100, 20); gpc_matrix_rotate (&a.m, 0.3f); gpc_matrix_scale (&a.m, 2.3f, 2.3f);
		s.layers = { a }; s.li = { 0 }; s.edgeAllow = 12; scenes.push_back (s);
	}
	{
		Scene s = base ("a target 2100 wide: two GPU parts", 2100, 40, false, 0x336699);
		addTex (s, 300, 30, 0);
		gpc_layer a = layer (0, 0, 300, 30, 220); gpc_matrix_translate (&a.m, 900, 4); gpc_matrix_scale (&a.m, 1.5f, 1.0f);
		s.layers = { a }; s.li = { 0 }; scenes.push_back (s);
	}
	for (Scene &s : scenes) runScene (s);

	// ---- a rectangle updated on the GPU's texture: the same picture as the CPU's
	{
		std::vector<unsigned> px = makeTex (2300, 50, 3), patch = makeTex (300, 30, 1);
		std::vector<unsigned> outs[2];
		for (int soft = 0; soft < 2; soft++)
		{
			hostkapi_soft (soft);
			gpc_config cfg = { al, fr, soft ? 0u : (unsigned) GPC_F_CPU };
			gpc_ctx *g = gpc_create (&cfg);
			gpc_tex *t = gpc_tex_create (g, 2300, 50, px.data (), 2300);
			int r = gpc_tex_update (g, t, 1900, 10, 300, 30, patch.data (), 300);	// (across the tiles' seam)
			gpc_layer L; gpc_layer_init (&L, t); L.src_x = 1880; L.src_w = 340;
			outs[soft].assign (340 * 50, 0);
			gpc_target tg = { outs[soft].data (), 340, 50, 340, 0 };
			int r2 = gpc_composite (g, &tg, &L, 1, 0, GPC_C_CLEAR);
			if (r || r2) printf ("  (update %d, composite %d)\n", r, r2);
			gpc_destroy (g);
		}
		int maxd = 0;
		for (size_t i = 0; i < outs[0].size (); i++)
			for (int k = 0; k < 24; k += 8) maxd = std::max (maxd, abs ((int) ((outs[0][i] >> k) & 255) - (int) ((outs[1][i] >> k) & 255)));
		char det[80]; snprintf (det, sizeof det, "(max diff CPU / GPU %d)", maxd);
		check ("gpc_tex_update across two tiles", maxd <= 3, det);
	}

	// ---- textures the GPU refuses: drawn by the CPU between two GPU frames, in order
	{
		std::vector<unsigned> a = makeTex (100, 100, 0), b = makeTex (100, 100, 1), c = makeTex (100, 100, 2);
		std::vector<unsigned> outs[2];
		for (int soft = 0; soft < 2; soft++)
		{
			hostkapi_soft (soft);
			gpc_config cfg = { al, fr, soft ? 0u : (unsigned) GPC_F_CPU };
			gpc_ctx *g = gpc_create (&cfg);
			gpc_tex *ta = gpc_tex_create (g, 100, 100, a.data (), 100);
			if (soft) hostkapi_refuse (1);
			gpc_tex *tb = gpc_tex_create (g, 100, 100, b.data (), 100);
			gpc_tex *tc = gpc_tex_create (g, 100, 100, c.data (), 100);
			gpc_layer L[3]; gpc_layer_init (&L[0], ta); gpc_layer_init (&L[1], tb); gpc_layer_init (&L[2], tc);
			gpc_matrix_translate (&L[1].m, 30, 30); gpc_matrix_translate (&L[2].m, 60, 60);
			outs[soft].assign (160 * 160, 0);
			gpc_target tg = { outs[soft].data (), 160, 160, 160, 0 };
			gpc_composite (g, &tg, L, 3, 0x808080, GPC_C_CLEAR);
			gpc_stats st; gpc_get_stats (g, &st);
			if (soft) { char det[80]; snprintf (det, sizeof det, "(%u gpu_render, %u CPU layer)", st.last_gpu_calls, st.last_cpu_layers);
				    check ("a refused texture: GPU, CPU, GPU runs", st.last_gpu_calls == 2 && st.last_cpu_layers == 1, det); }
			gpc_destroy (g);
		}
		int maxd = 0;
		for (size_t i = 0; i < outs[0].size (); i++)
			for (int k = 0; k < 24; k += 8) maxd = std::max (maxd, abs ((int) ((outs[0][i] >> k) & 255) - (int) ((outs[1][i] >> k) & 255)));
		char det[80]; snprintf (det, sizeof det, "(max diff CPU / mixed %d)", maxd);
		check ("a refused texture: the same picture", maxd <= 3, det);
	}

	// ---- the GPU lost: GPC_LOST, the CPU from then on, the textures lost until uploaded again
	{
		hostkapi_soft (1);
		std::vector<unsigned> a = makeTex (64, 64, 0);
		gpc_config cfg = { al, fr, 0 };
		gpc_ctx *g = gpc_create (&cfg);
		gpc_tex *t = gpc_tex_create (g, 64, 64, a.data (), 64);
		gpc_layer L; gpc_layer_init (&L, t);
		std::vector<unsigned> out (64 * 64);
		gpc_target tg = { out.data (), 64, 64, 64, 0 };
		int r1 = gpc_composite (g, &tg, &L, 1, 0, GPC_C_CLEAR);
		hostkapi_hang ();
		int r2 = gpc_composite (g, &tg, &L, 1, 0, GPC_C_CLEAR);	// (the first frame after: fine)
		int r3 = gpc_composite (g, &tg, &L, 1, 0, GPC_C_CLEAR);	// (then it hangs)
		int lost = gpc_tex_lost (t), be = gpc_backend (g);
		int r4 = gpc_tex_update (g, t, 0, 0, 64, 64, a.data (), 64);
		int r5 = gpc_composite (g, &tg, &L, 1, 0, GPC_C_CLEAR);
		int ok = 1;
		for (int i = 0; i < 64 * 64; i++) if ((out[i] & 0xFFFFFF) != (a[i] & 0xFFFFFF)) { ok = 0; break; }
		char det[120]; snprintf (det, sizeof det, "(composites %d %d %d, lost %d, backend %d, update %d, composite %d, picture %s)", r1, r2, r3, lost, be, r4, r5, ok ? "right" : "wrong");
		check ("the GPU lost in the middle", r1 == 0 && r2 == 0 && r3 == GPC_LOST && lost && be == GPC_BACKEND_CPU && r4 == 0 && r5 == 0 && !gpc_tex_lost (t) && ok, det);
		gpc_destroy (g);
	}

	// ---- the CPU path's time at 1920 x 1080 (the PC: a reference only; the Pi: /bin/gpcdemo bench)
	{
		hostkapi_soft (0);
		gpc_config cfg = { al, fr, GPC_F_CPU };
		gpc_ctx *g = gpc_create (&cfg);
		std::vector<unsigned> page = makeTex (1920, 3000, 0), card = makeTex (600, 400, 1), img = makeTex (512, 512, 2);
		gpc_tex *tp = gpc_tex_create (g, 1920, 3000, page.data (), 1920), *tc = gpc_tex_create (g, 600, 400, card.data (), 600);
		gpc_tex *ti = gpc_tex_create (g, 512, 512, img.data (), 512);
		std::vector<unsigned> out (1920 * 1080);
		gpc_target tg = { out.data (), 1920, 1080, 1920, 0 };
		gpc_layer L[4];
		gpc_layer_init (&L[0], tp); L[0].src_h = 1080; L[0].flags = GPC_L_OPAQUE;
		gpc_layer_init (&L[1], tp); L[1].src_y = 500.5f; L[1].src_h = 1080; L[1].src_w = 1920; L[1].opacity = 128;
		gpc_layer_init (&L[2], tc); gpc_matrix_translate (&L[2].m, 960, 540); gpc_matrix_rotate (&L[2].m, 0.4f); gpc_matrix_translate (&L[2].m, -300, -200);
		gpc_layer_init (&L[3], ti); gpc_matrix_translate (&L[3].m, 100, 100); gpc_matrix_scale (&L[3].m, 1.5f, 1.5f); L[3].opacity = 200;
		const char *names[4] = { "opaque page, whole texels (a copy)", "page, half-texel scroll, 50 % (bilinear)", "600 x 400 card rotated (bilinear)", "512 x 512 scaled x1.5, 78 %" };
		for (int k = 0; k < 4; k++)
		{
			struct timespec a0, a1; clock_gettime (CLOCK_MONOTONIC, &a0);
			for (int i = 0; i < 10; i++) gpc_composite (g, &tg, &L[k], 1, 0, 0);
			clock_gettime (CLOCK_MONOTONIC, &a1);
			printf ("  CPU path on the PC, 1920 x 1080, %-42s %.2f ms\n", names[k], ((a1.tv_sec - a0.tv_sec) * 1e3 + (a1.tv_nsec - a0.tv_nsec) / 1e6) / 10);
		}
		gpc_destroy (g);
	}
	printf ("%s: %d/%d\n", g_fail ? "FAILED" : "ALL PASS", g_tests - g_fail, g_tests);
	return g_fail != 0;
}
