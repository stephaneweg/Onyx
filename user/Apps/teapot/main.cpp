//
// teapot -- the V3D GPU demo: the Utah teapot turning, lit, drawn by the Raspberry Pi 4's GPU
// (kapi v52 gpu_draw: depth-tested Gouraud triangles). The geometry and the lighting are made
// by the CPU each frame (teapot.h: 6400 triangles); the GPU rasterizes them into the window.
// Without a usable GPU (or with G) the same triangles are drawn by the CPU instead.
//
//   Space: pause    G: GPU / software    Up / Down: tilt    the title bar line: the renderer,
//   the triangles, frames a second and the time of one frame.
//
#include "kapi.h"
#include "uikit/uikit.h"
#include "Apps/teapot/teapot.h"

using namespace uikit;

enum { WIN_W = 640, WIN_H = 480 };

static teapot::Model g_model;
static teapot::Vertex g_verts[teapot::VERTS];
static float g_zbuf[WIN_W * WIN_H];
static float g_yaw = 0, g_pitch = 0.35f;
static bool g_paused = false, g_gpuOk = false, g_useGpu = true;
static char g_gpuInfo[96] = "";
static char g_status[160] = "";
static unsigned long long g_frameUs = 0;
static Root *g_root = 0;

static inline unsigned long long now_us (void)
{
	unsigned long long c, f;
	asm volatile ("mrs %0, cntpct_el0" : "=r" (c));
	asm volatile ("mrs %0, cntfrq_el0" : "=r" (f));
	return f ? c * 1000000ull / f : 0;
}
static void cat (char *d, int *n, const char *s) { while (*s && *n < 158) d[(*n)++] = *s++; d[*n] = 0; }
static void catnum (char *d, int *n, unsigned v, int dec)
{
	char t[16]; int k = 0; unsigned div = 1;
	for (int i = 0; i < dec; i++) div *= 10;
	unsigned ip = v / div, fp = v % div;
	do { t[k++] = (char) ('0' + ip % 10); ip /= 10; } while (ip);
	while (k) d[(*n)++] = t[--k];
	if (dec) { d[(*n)++] = '.'; for (unsigned m = div / 10; m; m /= 10) d[(*n)++] = (char) ('0' + (fp / m) % 10); }
	d[*n] = 0;
}

class TeapotRoot : public Root
{
public:
	TeapotRoot () : Root (WIN_W, WIN_H, "Teapot (V3D)") {}
	void onDraw () override
	{
		unsigned long long t0 = now_us ();
		int n = teapot::frame (g_model, g_yaw, g_pitch, width, height, g_verts);
		bool drawn = false;
		if (g_gpuOk && g_useGpu)
		{
			int r = kapi_gpu_draw ((const struct kapi_gpu_vertex *) g_verts, (unsigned) n, 0x1C1C2A,
					       canvas.px, width, height, canvas.stride);
			if (r == 0) drawn = true;
			else { g_gpuOk = false; kapi_gpu_info (g_gpuInfo, sizeof g_gpuInfo); }	// (why, from now on)
		}
		if (!drawn) teapot::raster (g_verts, n, 0x1C1C2A, canvas.px, width, height, canvas.stride, g_zbuf);
		g_frameUs = now_us () - t0;
		canvas.fillRect (0, 0, width, 20, 0x101018);
		canvas.text (6, 2, g_status, drawn ? 0x80FF80 : 0xFFC060);
		if (g_paused) canvas.text (width - 60, 2, "paused", 0xFFFFFF);
	}
	bool onKey (long k) override
	{
		if (k == ' ') { g_paused = !g_paused; return true; }
		if (k == 'g' || k == 'G') { g_useGpu = !g_useGpu; return true; }
		if (k == KEY_UP) { g_pitch += 0.1f; return true; }
		if (k == KEY_DOWN) { g_pitch -= 0.1f; return true; }
		return Root::onKey (k);
	}
};

int main (void)
{
	teapot::build (g_model);
	g_gpuOk = kapi_gpu_info (g_gpuInfo, sizeof g_gpuInfo) == 1;
	TeapotRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	root.attach ();
	unsigned long long last = now_us (), statT = last;
	unsigned frames = 0;
	while (!should_exit ())
	{
		pump_events ();
		unsigned long long t = now_us ();
		if (!g_paused) g_yaw += (float) (t - last) * 0.0000009f;	// ~ one turn in 7 s
		last = t;
		root.invalidate (true);
		root.draw ();
		kapi_present ();
		frames++;
		if (t - statT >= 1000000 || !g_status[0])
		{
			int n = 0; g_status[0] = 0;
			bool gpu = g_gpuOk && g_useGpu;
			cat (g_status, &n, gpu ? g_gpuInfo : g_gpuOk ? "software (G: back to the GPU)" : "software -- ");
			if (!g_gpuOk) cat (g_status, &n, g_gpuInfo[0] ? g_gpuInfo : "no GPU");
			cat (g_status, &n, "   "); catnum (g_status, &n, teapot::TRIS, 0);
			cat (g_status, &n, " triangles   ");
			catnum (g_status, &n, t > statT ? (unsigned) ((unsigned long long) frames * 10000000ull / (t - statT + 1)) : 0, 1);
			cat (g_status, &n, " fps   ");
			catnum (g_status, &n, (unsigned) (g_frameUs / 100), 1);
			cat (g_status, &n, " ms");
			frames = 0; statT = t;
		}
		kapi_msleep (1);
	}
	return 0;
}
