// gcv3d -- gcemu's TEV renderer (user/Apps/gcemu/gxv3d.h) on the PC: a program (.dol) or a disc
// image (.iso / .gcm) runs with the recorder as Machine::gpu, and its last frame is drawn by a
// software V3D -- the kernel's clipping (v3d_clip.h), the rasterizer's viewport, perspective-
// correct varyings, depth, culling, scissor, write masks, blending, and the generated fragment
// shaders run in the QPU simulator (tools/qpu/qpusim) -- into a .ppm.
//   gcv3d <file.dol | file.iso> <fields> <out.ppm> [width height]
//   GCV3D_EVERY=n: every n-th finished frame too (out.ppm -> out_<k>.ppm)
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
		int x0s = (int) (b.scissor[0] * W + 0.5f), y0s = (int) (b.scissor[1] * H + 0.5f), x1s = (int) (b.scissor[2] * W + 0.5f), y1s = (int) (b.scissor[3] * H + 0.5f);
		if (x0s < 0) x0s = 0;
		if (y0s < 0) y0s = 0;
		if (x1s > W) x1s = W;
		if (y1s > H) y1s = H;
		if (getenv ("GCV3D_DUMP"))
		{
			printf ("  batch %u: prog %d, %u vertices, stride %d, flags %X blend %X wmask %X scissor %.3f %.3f %.3f %.3f\n", bi, b.prog, b.count, b.stride, b.flags, b.blend, b.wmask,
				b.scissor[0], b.scissor[1], b.scissor[2], b.scissor[3]);
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
					float nx = v[i][0] * iw[i], ny = v[i][1] * iw[i], nz = v[i][2] * iw[i];
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

static FILE *g_disc;
static bool discRead (void *, u32 off, u32 len, u8 *dst)
{
	if (fseeko (g_disc, (off_t) off, SEEK_SET)) return false;
	return fread (dst, 1, len, g_disc) == len;
}

int main (int argc, char **argv)
{
	if (argc < 4) { fprintf (stderr, "gcv3d <file.dol | file.iso> <fields> <out.ppm> [width height]\n"); return 2; }
	if (argc > 5) { W = atoi (argv[4]); H = atoi (argv[5]); }
	static Machine m;
	if (!rec.init ()) { printf ("FAIL: memory\n"); return 1; }
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
		fseeko (g_disc, 0, SEEK_END); off_t n = ftello (g_disc);
		m.discRead = discRead;
		ok = m.loadDiscImage ((u32) n);
	}
	if (!ok) { printf ("FAIL: not loaded\n"); return 1; }
	m.gpu = &rec;
	int every = getenv ("GCV3D_EVERY") ? atoi (getenv ("GCV3D_EVERY")) : 0;
	u32 lastSerial = 0; int shots = 0;
	for (int i = 0; i < atoi (argv[2]) && !m.halted; i++)
	{
		m.runFrame ();
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
	drawFrame (m, rec.frame[rec.ready], argv[3]);
	return nFail ? 1 : 0;
}
