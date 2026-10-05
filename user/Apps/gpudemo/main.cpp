//
// gpudemo -- the V3D GPU's full pipeline (kapi v53 gpu_texture / gpu_render): six textured
// cubes turning, each transformed by the GPU with its own 4 x 4 matrix (model * view *
// projection, computed once per cube by the CPU: the vertices themselves never change), then a
// glass pane in front (alpha blending) and glowing sparks (additive blending).
//
// The six textures have the sizes that select each memory layout the texture unit reads (a
// line of utiles 4 x 4, 1- and 2-column UBLINEAR 8 x 8 and 16 x 16, UIF 64 x 64, UIF with XOR
// 256 x 256, UIF padded 100 x 60): each shows an arrow, a border and its own colours, so a
// wrong layout shows as a scrambled face. The label under a cube names its texture.
//
//   F: nearest / linear filtering    C: culling (back / none / front)    B: blending on / off
//   T: textures on / off    Space: pause    the title bar line: frames a second, time of a frame
//
#include "kapi.h"
#include "uikit/uikit.h"

using namespace uikit;

enum { WIN_W = 640, WIN_H = 480 };
enum { NCUBES = 6, NSPARKS = 12 };
enum { MAX_V = NCUBES * 36 + 6 + NSPARKS * 6, MAX_B = NCUBES + 1 + 1 };

static kapi_gpu_vertex3 g_v[MAX_V];
static kapi_gpu_batch g_b[MAX_B];
static int g_tex[NCUBES + 2];
static const int TEXW[NCUBES] = { 4, 8, 16, 64, 256, 100 }, TEXH[NCUBES] = { 4, 8, 16, 64, 256, 60 };
static const char *const TEXNAME[NCUBES] = { "4x4 LT", "8x8 UB1", "16x16 UB2", "64x64 UIF", "256 UIF/XOR", "100x60 UIF" };
static float g_time = 0;
static bool g_paused = false, g_linear = false, g_blend = true, g_textures = true, g_ok = false;
static int g_cull = 1;					// 0 none, 1 back, 2 front
static char g_info[96] = "", g_status[160] = "";
static unsigned long long g_frameUs = 0;
static int g_err = 0;

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

// ---- a little matrix maths (row by row: clip = M * v) ------------------------------------------------
static float tsin (float x)
{
	const float PI = 3.14159265f, TWO_PI = 6.28318531f;
	while (x > PI) x -= TWO_PI;
	while (x < -PI) x += TWO_PI;
	float x2 = x * x;
	return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110)))));
}
static inline float tcos (float x) { return tsin (x + 1.57079633f); }

struct Mat { float m[16]; };
static Mat mul (const Mat &a, const Mat &b)
{
	Mat r;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
		{
			float s = 0;
			for (int k = 0; k < 4; k++) s += a.m[i * 4 + k] * b.m[k * 4 + j];
			r.m[i * 4 + j] = s;
		}
	return r;
}
static Mat ident (void) { Mat r = { { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 } }; return r; }
static Mat translate (float x, float y, float z) { Mat r = ident (); r.m[3] = x; r.m[7] = y; r.m[11] = z; return r; }
static Mat scale (float s) { Mat r = ident (); r.m[0] = r.m[5] = r.m[10] = s; return r; }
static Mat rotY (float a) { Mat r = ident (); float c = tcos (a), s = tsin (a); r.m[0] = c; r.m[2] = s; r.m[8] = -s; r.m[10] = c; return r; }
static Mat rotX (float a) { Mat r = ident (); float c = tcos (a), s = tsin (a); r.m[5] = c; r.m[6] = -s; r.m[9] = s; r.m[10] = c; return r; }
static Mat perspective (float f, float aspect, float n, float fa)	// f = cot (fov / 2), OpenGL style
{
	Mat r = { { f / aspect, 0, 0, 0,  0, f, 0, 0,  0, 0, (fa + n) / (n - fa), 2 * fa * n / (n - fa),  0, 0, -1, 0 } };
	return r;
}

// ---- the textures -------------------------------------------------------------------------------------------
// Each: a border, a diagonal gradient in its colours, an arrow pointing up-right (so a flip or a
// scramble shows), a dot in the top-left corner.
static unsigned g_px[256 * 256];
static void makeTexture (int k)
{
	static const unsigned A[NCUBES] = { 0xE04040, 0x40C060, 0x4070E0, 0xE0A030, 0xA050D0, 0x30B0B0 };
	int w = TEXW[k], h = TEXH[k];
	unsigned c = A[k];
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			int g = (x * 128 / w + y * 127 / h);			// 0..255
			unsigned r = ((c >> 16) & 255) * (128 + g / 2) / 255, gg = ((c >> 8) & 255) * (128 + g / 2) / 255, b = (c & 255) * (128 + g / 2) / 255;
			unsigned p = 0xFF000000 | r << 16 | gg << 8 | b;
			int bw = w >= 16 ? w / 16 : 1;
			if (x < bw || y < bw || x >= w - bw || y >= h - bw) p = 0xFFFFFFFF;		// border
			// the arrow: a shaft from bottom-left to top-right, a head at the top right
			int u = x * 16 / w, v = (h - 1 - y) * 16 / h;			// 0..15, v up
			if (w >= 16 && ((u == v && u >= 3 && u <= 12) || (u >= 9 && u <= 12 && v == 12) || (v >= 9 && v <= 12 && u == 12)))
				p = 0xFF101010;
			if (w < 16 && x == w - 1 - y) p = 0xFF101010;			// (tiny ones: a diagonal)
			if (x < w / 4 + 1 && y < h / 4 + 1 && w >= 8) p = 0xFFFFFF00;	// the top-left dot
			g_px[y * w + x] = p;
		}
	g_tex[k] = kapi_gpu_texture (-1, g_px, w, h, w);
}
static void makeGlass (void)		// 128 x 128: a frame, a faint bluish centre, "ONYX"-ish bars
{
	const int S = 128;
	for (int y = 0; y < S; y++)
		for (int x = 0; x < S; x++)
		{
			unsigned a = 60, col = 0x80B0FF;
			if (x < 6 || y < 6 || x >= S - 6 || y >= S - 6) { a = 230; col = 0xE0E8FF; }
			else if (((x / 8) + (y / 8)) % 2 == 0 && y > 50 && y < 78) { a = 150; col = 0xFFFFFF; }
			g_px[y * S + x] = a << 24 | col;
		}
	g_tex[NCUBES] = kapi_gpu_texture (-1, g_px, S, S, S);
}
static void makeSpark (void)		// 32 x 32: a round glow (alpha = the brightness)
{
	const int S = 32;
	for (int y = 0; y < S; y++)
		for (int x = 0; x < S; x++)
		{
			int dx = 2 * x - S + 1, dy = 2 * y - S + 1, d2 = dx * dx + dy * dy, r2 = S * S;
			int a = d2 >= r2 ? 0 : 255 - d2 * 255 / r2;
			a = a * a / 255;
			g_px[y * S + x] = (unsigned) a << 24 | 0xFFD070;
		}
	g_tex[NCUBES + 1] = kapi_gpu_texture (-1, g_px, S, S, S);
}

// ---- the geometry ------------------------------------------------------------------------------------------
static int g_cubeFirst;
static void buildCube (int first)
{
	// per face: normal n, u, v with u x v = n (counter-clockwise seen from outside)
	static const float F[6][9] = {
		{ 0, 0, 1,   1, 0, 0,   0, 1, 0 }, { 0, 0, -1,  -1, 0, 0,  0, 1, 0 },
		{ 1, 0, 0,   0, 0, -1,  0, 1, 0 }, { -1, 0, 0,  0, 0, 1,   0, 1, 0 },
		{ 0, 1, 0,   1, 0, 0,   0, 0, -1 }, { 0, -1, 0,  1, 0, 0,  0, 0, 1 } };
	static const float CU[4] = { -1, 1, 1, -1 }, CV[4] = { -1, -1, 1, 1 };
	static const float ST[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
	static const int TRI[6] = { 0, 1, 2, 0, 2, 3 };
	static const unsigned char SHADE[6] = { 255, 170, 225, 195, 240, 150 };	// a fixed light
	int n = first;
	for (int f = 0; f < 6; f++)
		for (int t = 0; t < 6; t++)
		{
			int c = TRI[t];
			const float *d = F[f];
			kapi_gpu_vertex3 &v = g_v[n++];
			v.x = d[0] + CU[c] * d[3] + CV[c] * d[6];
			v.y = d[1] + CU[c] * d[4] + CV[c] * d[7];
			v.z = d[2] + CU[c] * d[5] + CV[c] * d[8];
			v.w = 1;
			v.s = ST[c][0]; v.t = ST[c][1];
			v.r = v.g = v.b = SHADE[f]; v.a = 255;
			v.r2 = v.g2 = v.b2 = v.a2 = 0;
		}
}
static void quad (int first, float x0, float y0, float x1, float y1, float z, unsigned char a)
{
	const float P[4][4] = { { x0, y0, 0, 1 }, { x1, y0, 1, 1 }, { x1, y1, 1, 0 }, { x0, y1, 0, 0 } };
	static const int TRI[6] = { 0, 1, 2, 0, 2, 3 };
	for (int t = 0; t < 6; t++)
	{
		const float *p = P[TRI[t]];
		kapi_gpu_vertex3 &v = g_v[first + t];
		v.x = p[0]; v.y = p[1]; v.z = z; v.w = 1; v.s = p[2]; v.t = p[3];
		v.r = v.g = v.b = 255; v.a = a; v.r2 = v.g2 = v.b2 = v.a2 = 0;
	}
}

static float g_lx[NCUBES], g_ly[NCUBES];		// the labels (screen positions)

static int frame (int w, int h, unsigned *px, int stride)
{
	Mat P = perspective (1.8f, (float) w / (float) h, 1.0f, 30.0f);
	Mat V = mul (translate (0, 0, -9.0f), rotX (0.25f));
	int nb = 0;
	for (int k = 0; k < NCUBES; k++)
	{
		float cx = (float) (k % 3 - 1) * 3.2f, cy = k < 3 ? 1.6f : -1.8f;
		Mat Mo = mul (translate (cx, cy, 0), mul (rotY (g_time * (0.6f + 0.1f * k) + k), mul (rotX (g_time * 0.4f + k * 0.7f), scale (0.95f))));
		Mat MVP = mul (P, mul (V, Mo));
		kapi_gpu_batch &b = g_b[nb++];
		b.first = (unsigned) g_cubeFirst; b.count = 36;
		b.texture = g_textures ? g_tex[k] : -1;
		b.flags = (g_linear ? KAPI_GPU_B_LINEAR : 0) | (g_cull == 1 ? KAPI_GPU_B_CULL_BACK : g_cull == 2 ? KAPI_GPU_B_CULL_FRONT : 0);
		for (int i = 0; i < 16; i++) b.matrix[i] = MVP.m[i];
		// the label: the cube's centre, 1.4 below, projected
		float X = MVP.m[3] - 1.4f * MVP.m[1], Y = MVP.m[7] - 1.4f * MVP.m[5], W = MVP.m[15] - 1.4f * MVP.m[13];
		g_lx[k] = (X / W + 1) * 0.5f * w; g_ly[k] = (1 - Y / W) * 0.5f * h;
	}
	// the glass pane (in front of the lower middle), then the sparks: blended, no depth writes
	int nFirstPane = g_cubeFirst + 36;
	float sw = 1.6f + 0.3f * tsin (g_time * 0.7f);
	quad (nFirstPane, -sw, -1.2f, sw, 1.2f, 0, 255);
	{
		Mat M = mul (P, mul (V, mul (translate (1.6f * tsin (g_time * 0.5f), -0.2f, 2.2f), rotY (0.3f * tsin (g_time * 0.9f)))));
		kapi_gpu_batch &b = g_b[nb++];
		b.first = (unsigned) nFirstPane; b.count = 6;
		b.texture = g_textures ? g_tex[NCUBES] : -1;
		b.flags = KAPI_GPU_B_LINEAR | KAPI_GPU_B_NOZWRITE | KAPI_GPU_B_BLEND (g_blend ? KAPI_GPU_BLEND_ALPHA : KAPI_GPU_BLEND_NONE);
		for (int i = 0; i < 16; i++) b.matrix[i] = M.m[i];
	}
	// sparks: quads already in clip space around the scene (the identity matrix)
	int nFirstSpark = nFirstPane + 6;
	for (int i = 0; i < NSPARKS; i++)
	{
		float a = g_time * 0.8f + i * 0.5236f, r = 0.75f + 0.1f * tsin (g_time * 2 + i);
		float x = r * tcos (a), y = 0.8f * r * tsin (a), s = 0.07f + 0.02f * tsin (g_time * 3 + i * 1.7f);
		quad (nFirstSpark + i * 6, x - s, y - s * w / h, x + s, y + s * w / h, -0.99f, 255);
	}
	{
		kapi_gpu_batch &b = g_b[nb++];
		b.first = (unsigned) nFirstSpark; b.count = NSPARKS * 6;
		b.texture = g_textures ? g_tex[NCUBES + 1] : -1;
		b.flags = KAPI_GPU_B_LINEAR | KAPI_GPU_B_NOZWRITE | KAPI_GPU_B_NOMATRIX | KAPI_GPU_Z_ALWAYS
			| KAPI_GPU_B_BLEND (g_blend ? KAPI_GPU_BLEND_ADD : KAPI_GPU_BLEND_NONE);
	}
	kapi_gpu_frame f = { px, w, h, stride, 0x141822, 0 };
	return kapi_gpu_render (&f, g_v, (unsigned) (nFirstSpark + NSPARKS * 6), g_b, (unsigned) nb);
}

class DemoRoot : public Root
{
public:
	DemoRoot () : Root (WIN_W, WIN_H, "GPU demo (V3D)") {}
	void onDraw () override
	{
		if (!g_ok)
		{
			canvas.fillRect (0, 0, width, height, 0x141822);
			canvas.text (20, 40, "No usable GPU (kapi v53):", 0xFFC060);
			canvas.text (20, 60, g_info[0] ? g_info : "this kernel is too old", 0xFFFFFF);
			return;
		}
		unsigned long long t0 = now_us ();
		int r = frame (width, height, canvas.px, canvas.stride);
		g_frameUs = now_us () - t0;
		if (r != 0)
		{
			g_err = r; g_ok = false;
			kapi_gpu_info (g_info, sizeof g_info);
			return;
		}
		for (int k = 0; k < NCUBES; k++)
			canvas.text ((int) g_lx[k] - 40, (int) g_ly[k], TEXNAME[k], 0xC0C8D8);
		canvas.fillRect (0, 0, width, 20, 0x101018);
		canvas.text (6, 2, g_status, 0x80FF80);
		canvas.fillRect (0, height - 20, width, 20, 0x101018);
		canvas.text (6, height - 18, "F filter  C cull  B blend  T textures  Space pause", 0xA0A8B8);
	}
	bool onKey (long k) override
	{
		if (k == ' ') { g_paused = !g_paused; return true; }
		if (k == 'f' || k == 'F') { g_linear = !g_linear; return true; }
		if (k == 'c' || k == 'C') { g_cull = (g_cull + 1) % 3; return true; }
		if (k == 'b' || k == 'B') { g_blend = !g_blend; return true; }
		if (k == 't' || k == 'T') { g_textures = !g_textures; return true; }
		return Root::onKey (k);
	}
};

int main (void)
{
	g_ok = kapi_gpu_info (g_info, sizeof g_info) == 1;
	if (g_ok)
	{
		for (int k = 0; k < NCUBES; k++) makeTexture (k);
		makeGlass ();
		makeSpark ();
		for (int k = 0; k < NCUBES + 2; k++)
			if (g_tex[k] < 0) { g_ok = false; g_err = g_tex[k]; }
		if (!g_ok) { int n = 0; g_info[0] = 0; cat (g_info, &n, "gpu_texture failed: "); catnum (g_info, &n, (unsigned) -g_err, 0); }
	}
	g_cubeFirst = 0;
	buildCube (g_cubeFirst);
	DemoRoot root;
	if (root.canvas.px == 0) return 1;
	root.attach ();
	unsigned long long last = now_us (), statT = last;
	unsigned frames = 0;
	while (!should_exit ())
	{
		pump_events ();
		unsigned long long t = now_us ();
		if (!g_paused) g_time += (float) (t - last) * 0.000001f;
		last = t;
		root.invalidate (true);
		root.draw ();
		kapi_present ();
		frames++;
		if (t - statT >= 1000000 || !g_status[0])
		{
			int n = 0; g_status[0] = 0;
			cat (g_status, &n, g_info);
			cat (g_status, &n, g_linear ? "   linear" : "   nearest");
			cat (g_status, &n, g_cull == 1 ? "  cull back" : g_cull == 2 ? "  cull front" : "  no cull");
			cat (g_status, &n, "   ");
			catnum (g_status, &n, t > statT ? (unsigned) ((unsigned long long) frames * 10000000ull / (t - statT + 1)) : 0, 1);
			cat (g_status, &n, " fps   ");
			catnum (g_status, &n, (unsigned) (g_frameUs / 100), 1);
			cat (g_status, &n, " ms");
			if (g_err) { cat (g_status, &n, "   error "); catnum (g_status, &n, (unsigned) -g_err, 0); }
			frames = 0; statT = t;
		}
		kapi_msleep (1);
	}
	return 0;
}
