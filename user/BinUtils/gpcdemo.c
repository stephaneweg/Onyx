//
// gpcdemo -- the GPU compositing service (user/Libs/gpucomp/gpucomp.h) shown, checked and timed: a web
// page of 1920 x 2600 (two GPU textures high) scrolling smoothly under a rotating picture, a
// translucent card that sways and fades, a banner and a clipped zoom -- composited by the V3D
// straight into the window, or by the CPU.
//
//   gpcdemo [cpu]                 the demo in a window (960 x 540), until it is closed; a line a
//                                 second on the terminal: the backend, ms a composite, frames/s
//   gpcdemo bench [w h [frames]]  1920 x 1080 by default, 60 frames: the uploads (a whole texture,
//                                 a rectangle, a band), then ms a frame, GPU then CPU, for the
//                                 page alone and for the five layers
//   gpcdemo test [w h]            the GPU's pictures against the CPU's (640 x 360 by default): a
//                                 PASS / FAIL line a scene, then ALL PASS n/n
//
// Reads and writes no file. Built for the PC too (tools/tests/run_gpucomp_test.sh: -DGPC_HOST,
// the stand-in kernel tools/tests/gpucomp/hostkapi.cpp).
//
#include "appkit/appkit.h"
#include "uikit/win.h"		// the window API (UIKit's: uk_win_*)
#include "umm.h"
#include "gpucomp/gpucomp.h"
#ifdef GPC_HOST
#include <time.h>
#endif

static void *al (unsigned long n) { return umm_malloc (n); }
static void fr (void *p) { umm_free (p); }

static unsigned now_us (void)
{
#ifdef GPC_HOST
	struct timespec ts; clock_gettime (CLOCK_MONOTONIC, &ts);
	return (unsigned) (ts.tv_sec * 1000000ull + ts.tv_nsec / 1000);
#else
	return kapi_clock_us ();
#endif
}

// ---- output -------------------------------------------------------------------------------------------------
static void putu (unsigned v) { char b[16]; ax_itoa ((int) v, b); ax_puts (b); }
static void putms (unsigned us)				// "12.34"
{
	putu (us / 1000); ax_puts (".");
	unsigned f = (us % 1000) / 10; if (f < 10) ax_puts ("0");
	putu (f);
}
static int atoi_ (const char *s) { int v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }

// ---- the textures (premultiplied ARGB) ------------------------------------------------------------------------
static unsigned s_seed = 12345;
static unsigned rnd (void) { s_seed = s_seed * 1103515245u + 12345u; return s_seed >> 8; }
static unsigned premul (unsigned a, unsigned r, unsigned g, unsigned b)
{ return a << 24 | (r * a / 255) << 16 | (g * a / 255) << 8 | (b * a / 255); }

#define PAGE_W 1920
#define PAGE_H 2600
static void fill (unsigned *p, int stride, int x, int y, int w, int h, unsigned c)
{
	for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) p[(long) j * stride + i] = c;
}
// a page: a header, columns of "text" (grey bars), boxes of colour, a grid of thumbnails
static unsigned *make_page (void)
{
	unsigned *p = (unsigned *) al ((unsigned long) PAGE_W * PAGE_H * 4);
	if (!p) return 0;
	fill (p, PAGE_W, 0, 0, PAGE_W, PAGE_H, 0xFFFFFFFF);
	fill (p, PAGE_W, 0, 0, PAGE_W, 110, 0xFF1D3557);
	fill (p, PAGE_W, 60, 35, 360, 40, 0xFFF1FAEE);
	for (int k = 0; k < 6; k++) fill (p, PAGE_W, 900 + k * 160, 45, 120, 20, 0xFFA8DADC);
	for (int y = 160; y < PAGE_H - 40; y += 28)
	{
		int col = (y / 700) & 1;
		int x0 = col ? 1020 : 80, len = 300 + (int) (rnd () % 520);
		if ((y / 28) % 9 == 0) { fill (p, PAGE_W, x0, y - 6, 700, 22, 0xFF264653); continue; }	// a heading
		fill (p, PAGE_W, x0, y, len, 12, 0xFF6C757D);
		if (rnd () % 5 == 0) fill (p, PAGE_W, x0 + len + 12, y, 90, 12, 0xFF1D70B8);		// a link
	}
	for (int k = 0; k < 12; k++)							// thumbnails
	{
		int x = 80 + (k % 4) * 440, y = 1500 + (k / 4) * 300;
		unsigned c = premul (255, 60 + (unsigned) k * 15, 120 + (unsigned) (k * 37 % 120), 200 - (unsigned) k * 12);
		fill (p, PAGE_W, x, y, 400, 260, c);
		fill (p, PAGE_W, x + 20, y + 200, 360, 14, 0xFFFFFFFF);
	}
	return p;
}
// a picture: a round plasma with an anti-aliased edge (alpha)
static unsigned *make_image (int n)
{
	unsigned *p = (unsigned *) al ((unsigned long) n * n * 4);
	if (!p) return 0;
	int c = n / 2, r2 = (n / 2 - 2) * (n / 2 - 2);
	for (int y = 0; y < n; y++)
		for (int x = 0; x < n; x++)
		{
			int dx = x - c, dy = y - c, d2 = dx * dx + dy * dy;
			unsigned a = d2 <= r2 - 2 * n ? 255 : d2 >= r2 + 2 * n ? 0 : (unsigned) (255 * (r2 + 2 * n - d2) / (4 * n));
			unsigned r = (unsigned) ((x * 3 + y) & 255), g = (unsigned) ((x ^ y) & 255), b = (unsigned) ((y * 2 - x) & 255);
			p[y * n + x] = premul (a, r, g, b);
		}
	return p;
}
// a card: a translucent panel, a darker border, a gradient
static unsigned *make_card (int w, int h)
{
	unsigned *p = (unsigned *) al ((unsigned long) w * h * 4);
	if (!p) return 0;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			int edge = x < 6 || y < 6 || x >= w - 6 || y >= h - 6;
			unsigned a = edge ? 255 : 170;
			unsigned r = edge ? 30 : (unsigned) (230 - y * 100 / h), g = edge ? 30 : 180, b = edge ? 60 : (unsigned) (120 + x * 120 / w);
			p[y * w + x] = premul (a, r, g, b);
		}
	return p;
}
static unsigned *make_banner (int w, int h)
{
	unsigned *p = (unsigned *) al ((unsigned long) w * h * 4);
	if (!p) return 0;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) p[y * w + x] = premul (200, 20, 20, (unsigned) (40 + (x * 80 / w)));
	for (int k = 0; k < 8; k++) fill (p, w, 40 + k * 230, 28, 180, 24, 0xFFE9C46A);
	return p;
}

typedef struct Scene
{
	gpc_tex *page, *image, *card, *banner;
	unsigned *pagePx;				// (the page's pixels: the damaged box redrawn from them)
} Scene;

static int scene_make (gpc_ctx *g, Scene *S, unsigned *page, unsigned *image, unsigned *card, unsigned *banner)
{
	S->page = gpc_tex_create (g, PAGE_W, PAGE_H, page, PAGE_W);
	S->image = gpc_tex_create (g, 256, 256, image, 256);
	S->card = gpc_tex_create (g, 480, 300, card, 480);
	S->banner = gpc_tex_create (g, 1920, 80, banner, 1920);
	S->pagePx = page;
	return S->page && S->image && S->card && S->banner;
}
static void scene_free (gpc_ctx *g, Scene *S)
{
	gpc_tex_destroy (g, S->page); gpc_tex_destroy (g, S->image); gpc_tex_destroy (g, S->card); gpc_tex_destroy (g, S->banner);
}

// the layers at time t (seconds, x 1000) for a target W x H -> how many
static int scene_layers (const Scene *S, unsigned tms, int W, int H, gpc_layer *L)
{
	float t = (float) tms * 0.001f, sx = (float) W / 1920.0f, s, c;
	int n = 0;
	// the page: scrolling (fractional texels), scaled to the target's width
	gpc_layer_init (&L[n], S->page);
	float span = (float) PAGE_H - (float) H / sx;
	float y = (float) (((unsigned) (t * 90.0f)) % (unsigned) (2 * span));
	y += t * 90.0f - (float) (unsigned) (t * 90.0f);			// (the fraction)
	L[n].src_y = y < span ? y : 2 * span - y; L[n].src_h = (float) H / sx;
	gpc_matrix_scale (&L[n].m, sx, sx);
	L[n].flags = GPC_L_OPAQUE; n++;
	// a picture rotating, breathing
	gpc_layer_init (&L[n], S->image);
	gpc_sincos (t * 1.3f, &s, &c);
	gpc_matrix_translate (&L[n].m, (float) W * 0.72f, (float) H * 0.42f);
	gpc_matrix_rotate (&L[n].m, t * 0.6f);
	gpc_matrix_scale (&L[n].m, sx * (1.3f + 0.3f * s), sx * (1.3f + 0.3f * s));
	gpc_matrix_translate (&L[n].m, -128, -128); n++;
	// a card: sways, skews, fades
	gpc_layer_init (&L[n], S->card);
	gpc_sincos (t, &s, &c);
	gpc_matrix_translate (&L[n].m, (float) W * 0.35f, (float) H * 0.5f);
	gpc_matrix_rotate (&L[n].m, 0.25f * s);
	gpc_matrix_skew (&L[n].m, 0.15f * c, 0);
	gpc_matrix_scale (&L[n].m, sx * 1.4f, sx * 1.4f);
	gpc_matrix_translate (&L[n].m, -240, -150);
	L[n].opacity = (unsigned) (170 + 85 * s); n++;
	// a banner at the bottom, 80 % opaque
	gpc_layer_init (&L[n], S->banner);
	gpc_matrix_translate (&L[n].m, 0, (float) H - 80.0f * sx);
	gpc_matrix_scale (&L[n].m, sx, sx);
	L[n].opacity = 210; n++;
	// a zoom of the page's top, nearest, clipped to a box
	gpc_layer_init (&L[n], S->page);
	L[n].src_x = 40; L[n].src_y = 20; L[n].src_w = 240; L[n].src_h = 120;
	gpc_matrix_translate (&L[n].m, (float) W * 0.05f, (float) H * 0.08f);
	gpc_matrix_scale (&L[n].m, sx * 2.0f, sx * 2.0f);
	L[n].clip[0] = (int) ((float) W * 0.05f); L[n].clip[1] = (int) ((float) H * 0.08f);
	L[n].clip[2] = (int) (360 * sx); L[n].clip[3] = (int) (180 * sx);
	L[n].flags = GPC_L_NEAREST | GPC_L_OPAQUE; n++;
	return n;
}

// the page's "live" box (a counter bar) redrawn: its pixels changed, that rectangle uploaded
static void scene_damage (gpc_ctx *g, Scene *S, unsigned k)
{
	int bx = 1300, by = 140, bw = 480, bh = 60;
	for (int y = 0; y < bh; y++)
		for (int x = 0; x < bw; x++)
			S->pagePx[(by + y) * PAGE_W + bx + x] = x < (int) ((k * 37) % (unsigned) bw) ? 0xFFE76F51 : 0xFF2A9D8F;
	gpc_tex_update (g, S->page, bx, by, bw, bh, S->pagePx + by * PAGE_W + bx, PAGE_W);
}

static unsigned *s_page, *s_image, *s_card, *s_banner;
static int make_all (void)
{
	s_page = make_page (); s_image = make_image (256); s_card = make_card (480, 300); s_banner = make_banner (1920, 80);
	return s_page && s_image && s_card && s_banner;
}

// ---- the demo ---------------------------------------------------------------------------------------------------
static int demo (int forceCpu)
{
	int W = 960, H = 540;
	gpc_config cfg = { al, fr, forceCpu ? GPC_F_CPU : 0u };
	gpc_ctx *g = gpc_create (&cfg);
	Scene S;
	if (!g || !make_all () || !scene_make (g, &S, s_page, s_image, s_card, s_banner)) { ax_putln ("gpcdemo: no memory"); return 1; }
	unsigned *canvas = uk_win_create (W, H, "GPU compositing (gpucomp)");
	if (!canvas) { ax_putln ("gpcdemo: no window"); return 1; }
	ax_puts ("gpcdemo: "); ax_putln (gpc_info (g));
	gpc_target T = { canvas, W, H, W, 0 };
	gpc_layer L[8];
	unsigned t0 = now_us (), last = t0, frames = 0, us = 0, k = 0;
	while (!kapi_should_exit ())
	{
		kapi_pump_events ();
		unsigned t = now_us ();
		if ((frames & 15) == 0) scene_damage (g, &S, k++);
		int n = scene_layers (&S, (t - t0) / 1000, W, H, L);
		int r = gpc_composite (g, &T, L, n, 0x000000, GPC_C_CLEAR);
		if (r == GPC_LOST)				// (the GPU stopped: the CPU from now on, the textures again)
		{
			ax_puts ("gpcdemo: the GPU stopped -- "); ax_putln (gpc_info (g));
			scene_free (g, &S); scene_make (g, &S, s_page, s_image, s_card, s_banner);
		}
		gpc_stats st; gpc_get_stats (g, &st); us += st.last_us;
		uk_win_present ();
		frames++;
		if (t - last >= 1000000)
		{
			ax_puts (gpc_backend (g) == GPC_BACKEND_GPU ? "GPU  " : "CPU  "); putms (us / frames);
			ax_puts (" ms a composite, "); putu (frames * 1000000u / (t - last)); ax_putln (" frames/s");
			last = t; frames = 0; us = 0;
		}
	}
	gpc_destroy (g);
	return 0;
}

// ---- the benchmark ------------------------------------------------------------------------------------------------
static void bench_run (gpc_ctx *g, Scene *S, const gpc_target *T, int nFrames, int all, const char *what)
{
	gpc_layer L[8];
	unsigned sum = 0, mn = ~0u, mx = 0, gpuCalls = 0;
	for (int f = 0; f < nFrames; f++)
	{
		int n = scene_layers (S, (unsigned) f * 33, T->w, T->h, L);
		unsigned a = now_us ();
		gpc_composite (g, T, L, all ? n : 1, 0, all ? GPC_C_CLEAR : 0);
		unsigned d = now_us () - a;
		gpc_stats st; gpc_get_stats (g, &st); gpuCalls += st.last_gpu_calls;
		sum += d; if (d < mn) mn = d; if (d > mx) mx = d;
	}
	ax_puts (gpc_backend (g) == GPC_BACKEND_GPU ? "  GPU  " : "  CPU  "); ax_puts (what);
	ax_puts (": "); putms (sum / (unsigned) nFrames); ax_puts (" ms a frame (min "); putms (mn); ax_puts (", max "); putms (mx);
	ax_puts ("; "); putu (gpuCalls); ax_putln (" gpu_render)");
}

static int bench (int W, int H, int nFrames)
{
	if (!make_all ()) { ax_putln ("gpcdemo: no memory"); return 1; }
	for (int pass = 0; pass < 2; pass++)
	{
		gpc_config cfg = { al, fr, pass ? GPC_F_CPU : 0u };
		gpc_ctx *g = gpc_create (&cfg);
		if (!g) return 1;
		if (pass == 0 && gpc_backend (g) != GPC_BACKEND_GPU) { ax_puts ("gpcdemo bench: "); ax_putln (gpc_info (g)); gpc_destroy (g); continue; }
		ax_puts ("gpcdemo bench: "); putu ((unsigned) W); ax_puts (" x "); putu ((unsigned) H); ax_puts (", "); putu ((unsigned) nFrames);
		ax_puts (" frames, "); ax_putln (gpc_info (g));
		Scene S;
		unsigned a = now_us ();
		int ok = scene_make (g, &S, s_page, s_image, s_card, s_banner);
		unsigned up = now_us () - a;
		if (!ok) { ax_putln ("  no memory for the textures"); gpc_destroy (g); return 1; }
		a = now_us (); gpc_tex_update (g, S.page, 700, 900, 256, 256, s_page + 900 * PAGE_W + 700, PAGE_W); unsigned upR = now_us () - a;
		a = now_us (); gpc_tex_update (g, S.page, 0, 2000, 1920, 64, s_page + 2000 * PAGE_W, PAGE_W); unsigned upB = now_us () - a;
		ax_puts ("  uploads: the 4 textures (1920 x 2600 page, 256 x 256, 480 x 300, 1920 x 80) "); putms (up);
		ax_puts (" ms; a 256 x 256 rectangle "); putms (upR); ax_puts (" ms; a 1920 x 64 band "); putms (upB); ax_putln (" ms");
		int stride = W;
		unsigned *px = gpc_target_alloc (g, W, H, &stride);
		if (!px) { ax_putln ("  no memory for the target"); scene_free (g, &S); gpc_destroy (g); return 1; }
		gpc_target T = { px, W, H, stride, 0 };
		bench_run (g, &S, &T, nFrames, 0, "the page alone, scrolling (opaque)  ");
		bench_run (g, &S, &T, nFrames, 1, "5 layers (rotation, skew, alpha, clip)");
		gpc_target_free (g, px);
		scene_free (g, &S);
		gpc_destroy (g);
	}
	return 0;
}

// ---- the self-test: the GPU's pictures against the CPU's ---------------------------------------------------------------
static int s_tests = 0, s_fail = 0;
static void compare (const char *name, const unsigned *a, const unsigned *b, int W, int H, int alpha, int allow)
{
	int maxd = 0, bad = 0, wx = -1, wy = -1;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
		{
			unsigned p = a[y * W + x], q = b[y * W + x];
			int m = 0;
			for (int k = 0; k < (alpha ? 32 : 24); k += 8)
			{
				int d = (int) ((p >> k) & 255) - (int) ((q >> k) & 255);
				if (d < 0) d = -d;
				if (d > m) m = d;
			}
			if (m > maxd) { maxd = m; wx = x; wy = y; }
			if (m > 4) bad++;
		}
	s_tests++;
	int ok = bad <= allow;
	if (!ok) s_fail++;
	ax_puts (ok ? "PASS  " : "FAIL  "); ax_puts (name); ax_puts ("  (max diff "); putu ((unsigned) maxd);
	if (wx >= 0) { ax_puts (" at "); putu ((unsigned) wx); ax_puts (","); putu ((unsigned) wy); }
	ax_puts (", "); putu ((unsigned) bad); ax_puts (" pixels off by more than 4, "); putu ((unsigned) allow); ax_putln (" allowed)");
}

static int selftest (int W, int H)
{
	if (!make_all ()) { ax_putln ("gpcdemo: no memory"); return 1; }
	gpc_config cg = { al, fr, 0 }, cc = { al, fr, GPC_F_CPU }, ca = { al, fr, GPC_F_ASYNC };
	gpc_ctx *G = gpc_create (&cg), *C = gpc_create (&cc);
	if (!G || !C) return 1;
	ax_puts ("gpcdemo test: "); ax_putln (gpc_info (G));
	if (gpc_backend (G) != GPC_BACKEND_GPU) ax_putln ("  (no GPU: the CPU against itself -- the GPU's checks need the Pi)");
	Scene SG, SC;
	if (!scene_make (G, &SG, s_page, s_image, s_card, s_banner) || !scene_make (C, &SC, s_page, s_image, s_card, s_banner))
	{ ax_putln ("  no memory for the textures"); return 1; }
	int stride;
	unsigned *pg = gpc_target_alloc (G, W, H, &stride), *pc = (unsigned *) al ((unsigned long) W * H * 4);
	if (!pg || !pc) { ax_putln ("  no memory"); return 1; }
	gpc_layer LG[8], LC[8];
	// a few moments of the scene: the whole of it, then each layer alone
	static const char *names[] = { "scene at 0 s", "scene at 1.7 s", "scene at 4.2 s", "scene at 9.9 s" };
	static const unsigned when[] = { 0, 1700, 4200, 9900 };
	int allow = W * H / 500;				// (edges: coverage ties, the GPU's 1/256-pixel positions)
	for (int k = 0; k < 4; k++)
	{
		int n = scene_layers (&SG, when[k], W, H, LG); scene_layers (&SC, when[k], W, H, LC);
		gpc_target TG = { pg, W, H, stride, 0 }, TC = { pc, W, H, W, 0 };
		int r1 = gpc_composite (G, &TG, LG, n, 0x102030, GPC_C_CLEAR), r2 = gpc_composite (C, &TC, LC, n, 0x102030, GPC_C_CLEAR);
		if (r1 || r2) { ax_puts ("  (composite: "); putu ((unsigned) -r1); ax_puts (" "); putu ((unsigned) -r2); ax_putln (")"); }
		compare (names[k], pg, pc, W, H, 0, allow);
		for (int i = 0; i < n && k == 1; i++)
		{
			static const char *ln[] = { "  the page (scroll)", "  the picture (rotation)", "  the card (skew, opacity)", "  the banner", "  the zoom (nearest, clip)" };
			gpc_composite (G, &TG, &LG[i], 1, 0x000000, GPC_C_CLEAR); gpc_composite (C, &TC, &LC[i], 1, 0x000000, GPC_C_CLEAR);
			compare (ln[i], pg, pc, W, H, 0, allow);
		}
	}
	// over what the target holds (KEEP), and into an ARGB target (the alpha kept)
	{
		gpc_target TG = { pg, W, H, stride, 0 }, TC = { pc, W, H, W, 0 };
		// (a pattern, not one colour: a target loaded from the wrong rows must show -- the load
		// packet's stride was 8 times too small, kern/v3d_cl.h)
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
				pc[y * W + x] = pg[y * stride + x] = ((unsigned) (x * 3) & 0xFF) << 16 | ((unsigned) (y * 5) & 0xFF) << 8 | (((x ^ y) & 16) ? 0xE0 : 0x30);
		int n = scene_layers (&SG, 2500, W, H, LG); scene_layers (&SC, 2500, W, H, LC);
		gpc_composite (G, &TG, LG + 1, n - 1, 0, 0); gpc_composite (C, &TC, LC + 1, n - 1, 0, 0);
		compare ("over the target's pixels (no clear)", pg, pc, W, H, 0, allow);
		TG.flags = TC.flags = GPC_T_ALPHA;
		gpc_composite (G, &TG, LG + 1, n - 1, 0x00000000, GPC_C_CLEAR); gpc_composite (C, &TC, LC + 1, n - 1, 0x00000000, GPC_C_CLEAR);
		compare ("an ARGB target (GPC_T_ALPHA)", pg, pc, W, H, 1, allow);
	}
	// a damaged rectangle uploaded, across the page's two textures (rows 2040..2060)
	{
		for (int y = 2030; y < 2070; y++) for (int x = 100; x < 900; x++) s_page[y * PAGE_W + x] = ((x ^ y) & 8) ? 0xFFFF0000 : 0xFF0000FF;
		gpc_tex_update (G, SG.page, 100, 2030, 800, 40, s_page + 2030 * PAGE_W + 100, PAGE_W);
		gpc_tex_update (C, SC.page, 100, 2030, 800, 40, s_page + 2030 * PAGE_W + 100, PAGE_W);
		gpc_layer a, b; gpc_layer_init (&a, SG.page); gpc_layer_init (&b, SC.page);
		a.src_y = b.src_y = 2000; a.src_h = b.src_h = 100; a.src_w = b.src_w = 1000;
		gpc_matrix_scale (&a.m, (float) W / 1000.0f, (float) H / 100.0f); b.m = a.m;
		gpc_target TG = { pg, W, H, stride, 0 }, TC = { pc, W, H, W, 0 };
		gpc_composite (G, &TG, &a, 1, 0, GPC_C_CLEAR); gpc_composite (C, &TC, &b, 1, 0, GPC_C_CLEAR);
		compare ("a rectangle updated across two textures", pg, pc, W, H, 0, allow);
	}
	// the same composite through gpc_submit / gpc_wait on a thread (GPC_F_ASYNC)
	{
		gpc_ctx *A = gpc_create (&ca);
		Scene SA;
		if (A && scene_make (A, &SA, s_page, s_image, s_card, s_banner))
		{
			int n = scene_layers (&SA, 3300, W, H, LG); scene_layers (&SC, 3300, W, H, LC);
			gpc_target TG = { pg, W, H, stride, 0 }, TC = { pc, W, H, W, 0 };
			int f = gpc_submit (A, &TG, LG, n, 0x000000, GPC_C_CLEAR);
			int r = gpc_wait (A, f);
			gpc_composite (C, &TC, LC, n, 0x000000, GPC_C_CLEAR);
			if (r) { ax_puts ("  (async composite: "); putu ((unsigned) -r); ax_putln (")"); }
			compare ("gpc_submit / gpc_wait (a thread)", pg, pc, W, H, 0, allow);
			scene_free (A, &SA);
			gpc_destroy (A);
		}
	}
	ax_puts (s_fail ? "FAILED: " : "ALL PASS: "); putu ((unsigned) (s_tests - s_fail)); ax_puts ("/"); putu ((unsigned) s_tests); ax_putln ("");
	scene_free (G, &SG); scene_free (C, &SC);
	gpc_destroy (G); gpc_destroy (C);
	return s_fail != 0;
}

int main (void)
{
	char args[128]; args[0] = 0;
	kapi_get_args (args, sizeof args);
	char *w[5] = { 0, 0, 0, 0, 0 }; int n = 0;
	for (char *p = args; *p && n < 5; )
	{
		while (*p == ' ') p++;
		if (!*p) break;
		w[n++] = p;
		while (*p && *p != ' ') p++;
		if (*p) *p++ = 0;
	}
	if (n >= 1 && ax_streq (w[0], "bench"))
		return bench (n >= 3 ? atoi_ (w[1]) : 1920, n >= 3 ? atoi_ (w[2]) : 1080, n >= 4 ? atoi_ (w[3]) : 60);
	if (n >= 1 && ax_streq (w[0], "test"))
		return selftest (n >= 3 ? atoi_ (w[1]) : 640, n >= 3 ? atoi_ (w[2]) : 360);
	return demo (n >= 1 && ax_streq (w[0], "cpu"));
}
