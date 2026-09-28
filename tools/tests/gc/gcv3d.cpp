// gcv3d -- gcemu's TEV renderer (user/Apps/gcemu/gxv3d.h) on the PC: a program (.dol) or a disc
// image (.iso / .gcm) runs with the recorder as Machine::gpu, and its last frame is drawn by a
// software V3D -- the kernel's clipping (v3d_clip.h), the rasterizer's viewport, perspective-
// correct varyings, depth, culling, scissor, write masks, blending, and the generated fragment
// shaders run in the QPU simulator (tools/qpu/qpusim) -- into a .ppm.
//   gcv3d <file.dol | file.iso> <fields> <out.ppm> [width height]
//   GCV3D_EVERY=n: every n-th finished frame too (out.ppm -> out_<k>.ppm); GCV3D_DUMP=1: the batches,
//   2: their programs' configurations too; GCV3D_SAVE=<file.gxf>: the last frame dumped as gcemu does
//   GCV3D_SKIPLOG=file: each draw the recorder leaves out (field, reason, vertices, the maps' textures)
//   GC_CARD=file: a memory card in slot A (a 2 MB .sav / Dolphin .raw: read, never written back);
//   GC_PAD="f0-f1:hex;...": the pad's buttons (Machine::PAD_*) held from field f0 to f1 (0x10000 /
//   0x20000 / 0x40000 / 0x80000: the stick left / right / up / down), as gcrun's
//   gcv3d --replay <frame.gxf> <out.ppm>: a frame gcemu dumped on the Pi (F9 / --diag), drawn here at
//   the Pi's size, and the picture the Pi's GPU made of it -> out_pi.ppm (the two to compare)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <kern/kapi_abi.h>
#include <kern/v3d_clip.h>
#define GXV3D_HOST
#include "Apps/gcemu/gxv3d.h"
#include "qpusim.h"

using namespace gc;
using gxv3d::Rec; using gxv3d::Frame; using gxv3d::Batch; using gxv3d::Prog;

static Rec rec;
static int W = 640, H = 480;
static std::vector<unsigned> col; static std::vector<unsigned char> alp; static std::vector<float> zb;
static long nPixels = 0, nTris = 0, nFail = 0;

static float factor (int f, const float s[4], const float d[4], int ch)
{
	switch (f)
	{
	case 0: return 0; case 1: return 1;
	case 2: return s[ch]; case 3: return 1 - s[ch];
	case 4: return d[ch]; case 5: return 1 - d[ch];
	case 6: return s[3]; case 7: return 1 - s[3];
	case 8: return d[3]; case 9: return 1 - d[3];
	default: return 1;
	}
}
static float eqn (int e, float s, float d)
{
	switch (e) { case 1: return s - d; case 2: return d - s; case 3: return s < d ? s : d; case 4: return s > d ? s : d; }
	return s + d;
}
static bool ztest (int f, float z, float old)
{
	switch (f) { case 0: case 1: return z < old; case 2: return z == old; case 3: return z <= old; case 4: return z > old; case 5: return z != old; case 6: return z >= old; case 7: return true; }
	return false;
}

static void drawFrame (Machine &m, const Frame &F, const char *path)
{
	float view[4]; gxv3d::frameView (F, view);		// (the EFB's clip space onto the XFB copy's rectangle)
	col.assign ((size_t) W * H, F.clear & 0xFFFFFF); alp.assign ((size_t) W * H, 255); zb.assign ((size_t) W * H, 1.0f);
	for (u32 bi = 0; bi < F.nb; bi++)
	{
		const Batch &b = F.b[bi];
		const Prog &P = rec.prog[b.prog];
		qpusim::Run run;
		for (u32 k = 0; k < P.nUni; k++)
		{
			const gxtev::Uni &u = rec.uniArena[P.uni + k];
			run.uniforms.push_back (u.kind == gxtev::U_TEXP0 ? 0x10000u * (u.a + 1) | 3 : F.u[b.uni + k]);
		}
		bool ok = true;
		for (int L = 0; L < P.nLook; L++)
		{
			int slot = b.texSlot[L];
			if (slot < 0 || !m.tex[slot].px) { ok = false; break; }
			const GTexture &T = m.tex[slot];
			qpusim::Texture t; t.w = T.w; t.h = T.h;
			t.wrapS = (int) ((b.texFlags[L] >> 13) & 3); t.wrapT = (int) ((b.texFlags[L] >> 15) & 3);
			if (t.wrapS == 3) t.wrapS = 0;
			if (t.wrapT == 3) t.wrapT = 0;
			t.linear = (b.texFlags[L] & KAPI_GPU_B_LINEAR) != 0;
			for (int i = 0; i < T.w * T.h; i++) { u32 c = T.px[i]; t.px.push_back ((c & 0xFF00FF00) | ((c >> 16) & 255) | ((c & 255) << 16)); }
			run.textures[0x10000u * (unsigned) (L + 1)] = t;
		}
		if (!ok) continue;
		float sc[4]; gxv3d::frameScissor (F, b, sc);
		int x0s = (int) (sc[0] * W + 0.5f), y0s = (int) (sc[1] * H + 0.5f), x1s = (int) (sc[2] * W + 0.5f), y1s = (int) (sc[3] * H + 0.5f);
		if (x0s < 0) x0s = 0;
		if (y0s < 0) y0s = 0;
		if (x1s > W) x1s = W;
		if (y1s > H) y1s = H;
		if (getenv ("GCV3D_DUMP"))
		{
			printf ("  batch %u: prog %d, %u vertices, stride %d, flags %X blend %X wmask %X scissor %.3f %.3f %.3f %.3f\n", bi, b.prog, b.count, b.stride, b.flags, b.blend, b.wmask,
				sc[0], sc[1], sc[2], sc[3]);
			if (atoi (getenv ("GCV3D_DUMP")) >= 2)				// (2: the program's configuration too)
			{
				const gxtev::Config &c = P.cfg;
				printf ("    program: %u words, flags %X, %d varyings, %d lookups, %u uniforms; %d stages, alpha %d %d logic %d, efb %d, dst alpha %d, proj %02X\n",
					P.nWords, P.flags, P.nVary, P.nLook, P.nUni, c.nStages, c.alphaFunc[0], c.alphaFunc[1], c.alphaLogic, c.efbFmt, (int) c.dstAlpha, c.projMask);
				for (int st = 0; st < c.nStages; st++) printf ("      stage %d: cenv %06X aenv %06X tref %03X ksel %03X\n", st, c.cenv[st], c.aenv[st], c.tref[st], c.ksel[st]);
			}
			for (u32 k = 0; k < b.count && k < 6; k++) { const float *v = F.v + b.off + k * (u32) b.stride; printf ("    "); for (int j = 0; j < b.stride; j++) printf (" %.3f", v[j]); printf ("\n"); }
		}
		int zf = (int) (b.flags & 7); bool zw = !(b.flags & KAPI_GPU_B_NOZWRITE) && zf != 7;
		int n = b.stride;
		for (u32 t = 0; t + 3 <= b.count; t += 3)
		{
			static float out[21 * 64];
			unsigned k = V3DClipTriangleN (F.v + b.off + (size_t) t * n, (unsigned) n, out);
			for (unsigned f = 0; f + 3 <= k; f += 3)
			{
				const float *v[3] = { out + f * n, out + (f + 1) * n, out + (f + 2) * n };
				float sx[3], sy[3], sz[3], iw[3];
				for (int i = 0; i < 3; i++)
				{
					iw[i] = 1.0f / v[i][3];
					float nx = view[0] * v[i][0] * iw[i] + view[1], ny = view[2] * v[i][1] * iw[i] + view[3], nz = v[i][2] * iw[i];
					sx[i] = (nx + 1) * 0.5f * W; sy[i] = (1 - ny) * 0.5f * H; sz[i] = nz * 0.5f + 0.5f;
				}
				float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);	// (y down: < 0 counter-clockwise)
				if (area == 0) continue;
				bool ccw = area < 0;
				if ((b.flags & KAPI_GPU_B_CULL_FRONT) && ccw) continue;
				if ((b.flags & KAPI_GPU_B_CULL_BACK) && !ccw) continue;
				nTris++;
				int bx0 = (int) floorf (fminf (sx[0], fminf (sx[1], sx[2]))), bx1 = (int) ceilf (fmaxf (sx[0], fmaxf (sx[1], sx[2])));
				int by0 = (int) floorf (fminf (sy[0], fminf (sy[1], sy[2]))), by1 = (int) ceilf (fmaxf (sy[0], fmaxf (sy[1], sy[2])));
				if (bx0 < x0s) bx0 = x0s;
				if (by0 < y0s) by0 = y0s;
				if (bx1 > x1s) bx1 = x1s;
				if (by1 > y1s) by1 = y1s;
				std::vector<qpusim::Pixel> px; std::vector<int> at; std::vector<float> zs;
				for (int y = by0; y < by1; y++)
					for (int x = bx0; x < bx1; x++)
					{
						float px_ = x + 0.5f, py_ = y + 0.5f;
						float l0 = ((sx[1] - px_) * (sy[2] - py_) - (sx[2] - px_) * (sy[1] - py_)) / area;
						float l1 = ((sx[2] - px_) * (sy[0] - py_) - (sx[0] - px_) * (sy[2] - py_)) / area;
						float l2 = 1 - l0 - l1;
						if (l0 < 0 || l1 < 0 || l2 < 0) continue;
						float z = l0 * sz[0] + l1 * sz[1] + l2 * sz[2];
						if (z < 0 || z > 1) continue;
						size_t o = (size_t) y * W + x;
						if (!ztest (zf, z, zb[o])) continue;
						float q = l0 * iw[0] + l1 * iw[1] + l2 * iw[2];
						qpusim::Pixel p; p.nTlb = 0; p.written = false;
						for (int j = 4; j < n; j++) p.vary.push_back ((l0 * v[0][j] * iw[0] + l1 * v[1][j] * iw[1] + l2 * v[2][j] * iw[2]) / q);
						px.push_back (p); at.push_back ((int) o); zs.push_back (z);
					}
				if (px.empty ()) continue;
				if (!qpusim::runFragment ((const uint64_t *) (rec.arena + P.words), (int) P.nWords, px, run))
				{ if (nFail++ < 3) printf ("  the simulator: %s (program %d)\n", run.error.c_str (), b.prog); continue; }
				for (size_t i = 0; i < px.size (); i++)
				{
					if (!px[i].written) continue;
					size_t o = (size_t) at[i];
					float s4[4], d4[4];
					for (int c = 0; c < 4; c++) s4[c] = (float) ((px[i].rgba >> (c * 8)) & 255) / 255.0f;
					u32 dc = col[o];
					d4[0] = (float) ((dc >> 16) & 255) / 255.0f; d4[1] = (float) ((dc >> 8) & 255) / 255.0f; d4[2] = (float) (dc & 255) / 255.0f; d4[3] = alp[o] / 255.0f;
					float r4[4];
					if (b.blend & 1)
					{
						u32 v = b.blend;
						int cs = (int) (v >> 4 & 15), cd = (int) (v >> 8 & 15), as = (int) (v >> 12 & 15), ad = (int) (v >> 16 & 15), ce = (int) (v >> 20 & 7), ae = (int) (v >> 24 & 7);
						for (int c = 0; c < 3; c++) r4[c] = eqn (ce, s4[c] * factor (cs, s4, d4, c), d4[c] * factor (cd, s4, d4, c));
						r4[3] = eqn (ae, s4[3] * factor (as, s4, d4, 3), d4[3] * factor (ad, s4, d4, 3));
					}
					else for (int c = 0; c < 4; c++) r4[c] = s4[c];
					u8 ob[4];
					for (int c = 0; c < 4; c++) { float x = r4[c] < 0 ? 0 : r4[c] > 1 ? 1 : r4[c]; ob[c] = (u8) lrintf (x * 255.0f); }
					u32 nc = dc;
					if (!(b.wmask & 1)) nc = (nc & 0x00FFFF) | (u32) ob[0] << 16;
					if (!(b.wmask & 2)) nc = (nc & 0xFF00FF) | (u32) ob[1] << 8;
					if (!(b.wmask & 4)) nc = (nc & 0xFFFF00) | ob[2];
					col[o] = nc;
					if (!(b.wmask & 8)) alp[o] = ob[3];
					if (zw) zb[o] = zs[i];
					nPixels++;
				}
			}
		}
	}
	FILE *fo = fopen (path, "wb"); if (!fo) return;
	fprintf (fo, "P6\n%d %d\n255\n", W, H);
	for (size_t i = 0; i < (size_t) W * H; i++) { u32 c = col[i]; fputc ((int) (c >> 16) & 255, fo); fputc ((int) (c >> 8) & 255, fo); fputc ((int) c & 255, fo); }
	fclose (fo);
	printf ("  frame: %u batches, %u vertices, %u uniforms -> %s (%ld triangles, %ld pixels)\n", F.nb, F.nv, F.nu, path, nTris, nPixels);
}

// --replay: a frame gcemu dumped (F9 / --diag, gxv3d::dumpFrame) -- its programs, textures, vertices
// -- drawn here at the Pi's size into out.ppm, and the picture the Pi's GPU made of it into out_pi.ppm
static int replay (Machine &m, const char *path, const char *out)
{
	FILE *f = fopen (path, "rb"); if (!f) { printf ("FAIL: %s\n", path); return 1; }
	fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
	std::vector<u8> d ((size_t) n);
	if (fread (d.data (), 1, (size_t) n, f) != (size_t) n) { fclose (f); return 1; }
	fclose (f);
	size_t at = 0;
	auto get = [&] (void *p, size_t k) { if (at + k > d.size ()) return false; memcpy (p, d.data () + at, k); at += k; return true; };
	gxv3d::DumpHead hd;
	if (!get (&hd, sizeof hd) || memcmp (hd.magic, "GXF1", 4) || hd.version != 2) { printf ("FAIL: not a frame dump (or an older one)\n"); return 1; }
	if (hd.sizeBatch != sizeof (Batch) || hd.sizeProg != sizeof (Prog)) { printf ("FAIL: the dump's layout (batch %u, program %u) is not ours (%u, %u)\n", hd.sizeBatch, hd.sizeProg, (u32) sizeof (Batch), (u32) sizeof (Prog)); return 1; }
	Frame &F = rec.frame[0]; F.reset ();
	if (hd.nf > gxv3d::MAX_FLOATS || hd.nb > gxv3d::MAX_BATCHES || hd.nu > gxv3d::MAX_UNIS) { printf ("FAIL: too big\n"); return 1; }
	bool ok = get (F.v, (size_t) hd.nf * 4) && get (F.b, (size_t) hd.nb * sizeof (Batch)) && get (F.u, (size_t) hd.nu * 4);
	F.nf = hd.nf; F.nv = hd.nv; F.nb = hd.nb; F.nu = hd.nu; F.clear = hd.clear; F.keep = hd.keep != 0;
	for (int k = 0; k < 4; k++) F.rect[k] = hd.rect[k];
	for (u32 i = 0; ok && i < hd.nProgs; i++)
	{
		u32 idx; Prog p;
		ok = get (&idx, 4) && get (&p, sizeof p) && idx < (u32) gxv3d::MAX_PROGS;
		if (!ok) break;
		p.words = rec.arenaN; p.uni = rec.uniN; p.handle = -1;
		ok = get (rec.arena + rec.arenaN, (size_t) p.nWords * 8) && get (rec.uniArena + rec.uniN, (size_t) p.nUni * sizeof (gxtev::Uni));
		rec.arenaN += p.nWords; rec.uniN += p.nUni;
		rec.prog[idx] = p;
		if ((int) idx >= rec.nProg) rec.nProg = (int) idx + 1;
	}
	for (u32 i = 0; ok && i < hd.nTex; i++)
	{
		u32 w3[3];
		ok = get (w3, sizeof w3) && w3[0] < (u32) Machine::MAX_TEX && w3[1] && w3[2] && w3[1] * w3[2] <= 4096u * 4096u;
		if (!ok) break;
		GTexture &T = m.tex[w3[0]];
		T.px = new u32[w3[1] * w3[2]]; T.w = (int) w3[1]; T.h = (int) w3[2]; T.cap = T.w * T.h; T.levels = 1;
		ok = get (T.px, (size_t) T.cap * 4);
	}
	std::vector<u32> pic ((size_t) hd.pw * hd.ph);
	if (ok && hd.pw > 0) ok = get (pic.data (), pic.size () * 4);
	if (!ok) { printf ("FAIL: the dump is cut short\n"); return 1; }
	rec.ready = 0;
	printf ("the dump: %u batches, %u vertices, %u uniforms, %u programs, %u textures; on the Pi: gpu_render2 %d, a %dx%d picture\n",
		hd.nb, hd.nv, hd.nu, hd.nProgs, hd.nTex, hd.ret, hd.pw, hd.ph);
	if (hd.pw > 0)
	{
		W = hd.pw; H = hd.ph;
		char p[512]; snprintf (p, sizeof p, "%.*s_pi.ppm", (int) (strlen (out) - 4), out);
		FILE *fo = fopen (p, "wb");
		if (fo)
		{
			fprintf (fo, "P6\n%d %d\n255\n", hd.pw, hd.ph);
			for (u32 c : pic) { fputc ((int) (c >> 16) & 255, fo); fputc ((int) (c >> 8) & 255, fo); fputc ((int) c & 255, fo); }
			fclose (fo);
			printf ("  the Pi's picture -> %s\n", p);
		}
	}
	drawFrame (m, F, out);
	return nFail ? 1 : 0;
}

// GCV3D_SKIPLOG: the draws the recorder leaves out
static FILE *g_skipLog; static int g_field;
static void onSkip (int why, const GxState &s, int nv, int arg)
{
	static const char *W[Rec::SK_N] = { "prim", "prog", "notex", "copytex", "znever", "cullall", "nowrite", "limit" };
	fprintf (g_skipLog, "%d %s nv %d arg %d stages %d tex", g_field, W[why], nv, arg, s.gen[0]);
	for (int k = 0; k < 8; k++) fprintf (g_skipLog, " %d", s.tex[k]);
	fprintf (g_skipLog, "\n");
}

// GC_PAD: "f0-f1:hex;f0-f1:hex..." -> the buttons held at that field
static u32 padAt (int field)
{
	const char *s = getenv ("GC_PAD");
	u32 b = 0;
	while (s && *s)
	{
		int f0 = (int) strtol (s, (char **) &s, 10), f1 = f0;
		if (*s == '-') f1 = (int) strtol (s + 1, (char **) &s, 10);
		if (*s == ':') s++;
		u32 v = (u32) strtoul (s, (char **) &s, 16);
		if (field >= f0 && field <= f1) b |= v;
		while (*s == ';' || *s == ' ') s++;
	}
	return b;
}

static FILE *g_disc;
static bool discRead (void *, u32 off, u32 len, u8 *dst)
{
	if (fseeko (g_disc, (off_t) off, SEEK_SET)) return false;
	return fread (dst, 1, len, g_disc) == len;
}

// GC_HASH=n: every n fields a line "hash <field> <MEM1's FNV-1a> pc <pc> cycles <cycles>" (two runs compared)
static void hashLine (Machine &m, int field)
{
	const char *h = getenv ("GC_HASH");
	if (!h || atoi (h) <= 0 || field % atoi (h)) return;
	unsigned long long x = 1469598103934665603ull;
	const unsigned long long *p = (const unsigned long long *) m.mem1;
	for (u32 i = 0; i < MEM1_SIZE / 8; i++) { x ^= p[i]; x *= 1099511628211ull; }
	printf ("hash %d %016llX pc %08X cycles %llu\n", field, x, m.pc, (unsigned long long) m.cycles);
	fflush (stdout);
}

int main (int argc, char **argv)
{
	if (argc < 4) { fprintf (stderr, "gcv3d <file.dol | file.iso> <fields> <out.ppm> [width height]\ngcv3d --replay <frame.gxf> <out.ppm>\n"); return 2; }
	if (argc > 5) { W = atoi (argv[4]); H = atoi (argv[5]); }
	static Machine m;
	if (!rec.init ()) { printf ("FAIL: memory\n"); return 1; }
	if (!strcmp (argv[1], "--replay")) return replay (m, argv[2], argv[3]);
	const char *path = argv[1];
	size_t pl = strlen (path);
	bool ok;
	if (pl > 4 && !strcmp (path + pl - 4, ".dol"))
	{
		FILE *f = fopen (path, "rb"); if (!f) { printf ("FAIL: %s\n", path); return 1; }
		fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
		std::vector<u8> d ((size_t) n); if (fread (d.data (), 1, (size_t) n, f) != (size_t) n) return 1; fclose (f);
		ok = m.loadDol (d.data (), (u32) n);
	}
	else
	{
		g_disc = fopen (path, "rb"); if (!g_disc) { printf ("FAIL: %s\n", path); return 1; }
		static u8 cardImg[Machine::CARD_SIZE];
		if (getenv ("GC_CARD"))					// a memory card in slot A (never written back)
		{
			for (u32 i = 0; i < sizeof cardImg; i++) cardImg[i] = 0xFF;
			FILE *cf = fopen (getenv ("GC_CARD"), "rb");
			if (cf) { size_t k = fread (cardImg, 1, sizeof cardImg, cf); fclose (cf); printf ("card: %u bytes read\n", (u32) k); }
			m.cardInsert (0, cardImg, sizeof cardImg);
		}
		fseeko (g_disc, 0, SEEK_END); off_t n = ftello (g_disc);
		m.discRead = discRead;
		ok = m.loadDiscImage ((u32) n);
	}
	if (!ok) { printf ("FAIL: not loaded\n"); return 1; }
	m.gpu = &rec;
	int every = getenv ("GCV3D_EVERY") ? atoi (getenv ("GCV3D_EVERY")) : 0;
	u32 lastSerial = 0; int shots = 0;
	if (getenv ("GCV3D_SKIPLOG")) { g_skipLog = fopen (getenv ("GCV3D_SKIPLOG"), "w"); if (g_skipLog) rec.onSkip = onSkip; }
	for (int i = 0; i < atoi (argv[2]) && !m.halted; i++)
	{
		g_field = i;
		u32 pb = padAt (i);
		m.setPad (0, pb & 0xFFFF, (pb & 0x10000) ? -100 : (pb & 0x20000) ? 100 : 0, (pb & 0x40000) ? 100 : (pb & 0x80000) ? -100 : 0, 0, 0, 0, 0);
		m.runFrame ();
		hashLine (m, i + 1);
		if (every > 0 && rec.serial != lastSerial && rec.serial % (u32) every == 0 && rec.ready >= 0)
		{
			lastSerial = rec.serial;
			char p[512]; snprintf (p, sizeof p, "%.*s_%d.ppm", (int) (strlen (argv[3]) - 4), argv[3], ++shots);
			drawFrame (m, rec.frame[rec.ready], p);
		}
	}
	printf ("%d fields, pc %08X%s%s; %u frames, %d programs, %u draws with a feature not generated, %u not drawn\n", m.frames, m.pc,
		m.halted ? " HALTED: " : "", m.haltMsg, rec.serial, (int) rec.nProg, rec.unsupported, rec.skipped);
	if (rec.ready < 0) { printf ("FAIL: no frame\n"); return 1; }
	if (getenv ("GCV3D_SAVE"))						// the last frame as gcemu dumps it (for --replay)
	{
		const Frame &F = rec.frame[rec.ready];
		std::vector<u8> d ((size_t) gxv3d::dumpFrame (rec, m, F, 0, 0, 0, 0, 0, 0));
		gxv3d::dumpFrame (rec, m, F, 0, 0, 0, 0, 0, d.data ());
		FILE *fo = fopen (getenv ("GCV3D_SAVE"), "wb");
		if (fo) { fwrite (d.data (), 1, d.size (), fo); fclose (fo); printf ("  the frame dumped -> %s (%u bytes)\n", getenv ("GCV3D_SAVE"), (u32) d.size ()); }
	}
	drawFrame (m, rec.frame[rec.ready], argv[3]);
	return nFail ? 1 : 0;
}
