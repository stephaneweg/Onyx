//
// gpuview.cpp -- a frame the 3DS core recorded for a GPU (n3dstest's N3DS_GPUDUMP: a .gpf file) drawn on the PC
// by a software V3D: the stock vertex transform (clip space -> the target, the first row at the top), a
// rasterizer with perspective-correct varyings, the depth test, the scissor, the write masks, the blending, and
// each batch's generated fragment shader run in the QPU simulator (tools/qpu/qpusim) -- into a .ppm to set
// beside the software renderer's picture of the same frame (<prefix>_<k>_soft.ppm).
//
//   gpuview <frame.gpf> <out.ppm> [soft.ppm]       (soft.ppm: the two compared -- how many pixels differ, by how much)
//
// MIT License -- Copyright (c) 2026 Stephane Wegener (see docs/LICENSING.md)
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include "n3ds/n3ds.h"
#include "qpusim.h"

using n3ds::u32;
using n3ds::GpuBatch;

static float factor (int f, const float s[4], const float d[4], int ch)
{
	switch (f)
	{
	case 0: return 0; case 1: return 1;
	case 2: return s[ch]; case 3: return 1 - s[ch];
	case 4: return d[ch]; case 5: return 1 - d[ch];
	case 6: return s[3]; case 7: return 1 - s[3];
	case 8: return d[3]; case 9: return 1 - d[3];
	case 14: return ch == 3 ? 1 : (s[3] < 1 - d[3] ? s[3] : 1 - d[3]);
	default: return 0;							// (the constant colour: the kernel's is 0)
	}
}
static float eqn (int e, float s, float sf, float d, float df)
{
	switch (e) { case 1: return s * sf - d * df; case 2: return d * df - s * sf; case 3: return s < d ? s : d; case 4: return s > d ? s : d; }
	return s * sf + d * df;
}
static bool ztest (int f, float z, float old)
{
	switch (f) { case 0: case 1: return z < old; case 2: return z == old; case 3: return z <= old; case 4: return z > old; case 5: return z != old; case 6: return z >= old; default: return true; }
}

int main (int argc, char **argv)
{
	if (argc < 3) { fprintf (stderr, "gpuview <frame.gpf> <out.ppm> [soft.ppm]\n"); return 2; }
	FILE *in = fopen (argv[1], "rb");
	if (!in) { fprintf (stderr, "cannot open %s\n", argv[1]); return 2; }
	u32 head[8];
	if (fread (head, 4, 8, in) != 8 || head[0] != 0x31465047) { fprintf (stderr, "not a recorded frame\n"); return 2; }
	const int W = (int) head[1], H = (int) head[2];
	struct Prog { std::vector<uint64_t> w; u32 nVary; };
	std::vector<Prog> progs (head[3]);
	for (auto &p : progs) { u32 n[2]; fread (n, 4, 2, in); p.nVary = n[1]; p.w.resize (n[0]); fread (p.w.data (), 8, n[0], in); }
	std::vector<qpusim::Texture> texs (head[4]);
	for (auto &t : texs)
	{
		u32 wh[2]; fread (wh, 4, 2, in);
		t.w = (int) wh[0]; t.h = (int) wh[1]; t.wrapS = t.wrapT = 0; t.linear = false;
		if (wh[0]) { t.px.resize ((size_t) wh[0] * wh[1]); fread (t.px.data (), 4, t.px.size (), in); }
	}
	std::vector<GpuBatch> batches (head[5]);
	fread (batches.data (), sizeof (GpuBatch), batches.size (), in);
	std::vector<u32> uni (head[6]);
	fread (uni.data (), 4, uni.size (), in);
	std::vector<float> v (head[7]);
	fread (v.data (), 4, v.size (), in);
	std::vector<u32> px ((size_t) W * H);
	fread (px.data (), 4, px.size (), in);
	fclose (in);

	std::vector<float> col ((size_t) W * H * 4), zb ((size_t) W * H, 1.0f);
	for (size_t i = 0; i < px.size (); i++) { col[i * 4] = (float) (px[i] >> 16 & 255) / 255.0f; col[i * 4 + 1] = (float) (px[i] >> 8 & 255) / 255.0f; col[i * 4 + 2] = (float) (px[i] & 255) / 255.0f; col[i * 4 + 3] = (float) (px[i] >> 24) / 255.0f; }
	long nTris = 0, nPixels = 0, nFail = 0;
	int bi = 0;
	for (const GpuBatch &b : batches)
	{
		const Prog &P = progs[b.program];
		qpusim::Run run;
		for (u32 i = 0; i < b.nUni; i++) run.uniforms.push_back (uni[b.uni + i]);
		for (int k = 0; k < 3; k++)
			if (b.tex[k] >= 0 && b.texUni[k] >= 0)
			{
				const u32 key = 0x10000u * (u32) (k + 1);
				qpusim::Texture t = texs[(size_t) b.tex[k]];
				t.wrapS = (int) (b.texFlags[k] >> 13 & 3); t.wrapT = (int) (b.texFlags[k] >> 15 & 3);
				run.textures[key] = t;
				run.uniforms[(size_t) b.texUni[k]] = key | 3;
			}
		const int zf = (int) (b.flags & 7); const bool zwrite = !(b.flags & 8);
		int sx0 = b.scissor[0], sy0 = b.scissor[1], sx1 = sx0 + b.scissor[2], sy1 = sy0 + b.scissor[3];
		if (b.scissor[2] <= 0) { sx0 = 0; sy0 = 0; sx1 = W; sy1 = H; }
		if (sx0 < 0) sx0 = 0; if (sy0 < 0) sy0 = 0; if (sx1 > W) sx1 = W; if (sy1 > H) sy1 = H;
		std::vector<qpusim::Pixel> frag; std::vector<int> where; std::vector<float> depth;
		for (u32 t = 0; t + 2 < b.count; t += 3)
		{
			const float *q[3] = { &v[b.off + (size_t) t * b.stride], &v[b.off + (size_t) (t + 1) * b.stride], &v[b.off + (size_t) (t + 2) * b.stride] };
			float X[3], Y[3], Z[3], IW[3];
			bool ok = true;
			for (int i = 0; i < 3; i++)
			{
				if (!(q[i][3] > 0)) { ok = false; break; }
				IW[i] = 1.0f / q[i][3];
				X[i] = (q[i][0] * IW[i] * 0.5f + 0.5f) * (float) W; Y[i] = (0.5f - q[i][1] * IW[i] * 0.5f) * (float) H; Z[i] = q[i][2] * IW[i] * 0.5f + 0.5f;
			}
			if (!ok) continue;
			float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (Y[1] - Y[0]) * (X[2] - X[0]);
			if (area == 0) continue;
			nTris++;
			int x0 = (int) floorf (fminf (X[0], fminf (X[1], X[2]))), x1 = (int) ceilf (fmaxf (X[0], fmaxf (X[1], X[2])));
			int y0 = (int) floorf (fminf (Y[0], fminf (Y[1], Y[2]))), y1 = (int) ceilf (fmaxf (Y[0], fmaxf (Y[1], Y[2])));
			if (x0 < sx0) x0 = sx0; if (y0 < sy0) y0 = sy0; if (x1 > sx1) x1 = sx1; if (y1 > sy1) y1 = sy1;
			frag.clear (); where.clear (); depth.clear ();
			for (int y = y0; y < y1; y++)
				for (int x = x0; x < x1; x++)
				{
					const float fx = (float) x + 0.5f, fy = (float) y + 0.5f;
					float w0 = ((X[2] - X[1]) * (fy - Y[1]) - (Y[2] - Y[1]) * (fx - X[1])) / area;
					float w1 = ((X[0] - X[2]) * (fy - Y[2]) - (Y[0] - Y[2]) * (fx - X[2])) / area;
					float w2 = 1.0f - w0 - w1;
					if (w0 < 0 || w1 < 0 || w2 < 0) continue;
					const float z = w0 * Z[0] + w1 * Z[1] + w2 * Z[2];
					if (!ztest (zf, z, zb[(size_t) y * W + x])) continue;
					const float p0 = w0 * IW[0], p1 = w1 * IW[1], p2 = w2 * IW[2], ps = 1.0f / (p0 + p1 + p2);
					qpusim::Pixel f;
					for (u32 n = 0; n < P.nVary; n++) f.vary.push_back ((p0 * q[0][4 + n] + p1 * q[1][4 + n] + p2 * q[2][4 + n]) * ps);
					f.nTlb = 0;
					frag.push_back (f); where.push_back (y * W + x); depth.push_back (z);
				}
			if (frag.empty ()) continue;
			if (!qpusim::runFragment (P.w.data (), (int) P.w.size (), frag, run)) { if (!nFail++) fprintf (stderr, "batch %d: the simulator: %s\n", bi, run.error.c_str ()); continue; }
			for (size_t i = 0; i < frag.size (); i++)
			{
				if (!frag[i].written) continue;
				nPixels++;
				const size_t at = (size_t) where[i];
				if (zwrite) zb[at] = depth[i];
				float s[4], d[4], o[4];
				for (int c = 0; c < 4; c++) { s[c] = (float) (frag[i].rgba >> (c * 8) & 255) / 255.0f; d[c] = col[at * 4 + c]; }
				if (b.blend & 1)
				{
					const int cs = (int) (b.blend >> 4 & 15), cd = (int) (b.blend >> 8 & 15), as = (int) (b.blend >> 12 & 15), ad = (int) (b.blend >> 16 & 15);
					const int ce = (int) (b.blend >> 20 & 7), ae = (int) (b.blend >> 24 & 7);
					for (int c = 0; c < 3; c++) o[c] = eqn (ce, s[c], factor (cs, s, d, c), d[c], factor (cd, s, d, c));
					o[3] = eqn (ae, s[3], factor (as, s, d, 3), d[3], factor (ad, s, d, 3));
				}
				else for (int c = 0; c < 4; c++) o[c] = s[c];
				for (int c = 0; c < 4; c++) if (!(b.wmask >> c & 1)) col[at * 4 + c] = o[c] < 0 ? 0 : o[c] > 1 ? 1 : o[c];
			}
		}
		bi++;
	}
	FILE *o = fopen (argv[2], "wb");
	if (!o) { fprintf (stderr, "cannot write %s\n", argv[2]); return 2; }
	fprintf (o, "P6\n%d %d\n255\n", W, H);
	std::vector<unsigned char> rgb ((size_t) W * H * 3);
	for (size_t i = 0; i < (size_t) W * H; i++) for (int c = 0; c < 3; c++) rgb[i * 3 + c] = (unsigned char) lrintf (col[i * 4 + c] * 255.0f);
	fwrite (rgb.data (), 1, rgb.size (), o);
	fclose (o);
	printf ("%s: %dx%d, %zu batches, %ld triangles, %ld pixels drawn%s\n", argv[1], W, H, batches.size (), nTris, nPixels, nFail ? " -- SIMULATOR ERRORS" : "");
	if (argc > 3)
	{
		FILE *sf = fopen (argv[3], "rb");
		int sw = 0, shh = 0, mx = 0;
		if (sf && fscanf (sf, "P6 %d %d %d", &sw, &shh, &mx) == 3 && sw == W && shh == H)
		{
			fgetc (sf);
			std::vector<unsigned char> soft ((size_t) W * H * 3);
			fread (soft.data (), 1, soft.size (), sf);
			long diff = 0, big = 0; int worst = 0;
			for (size_t i = 0; i < (size_t) W * H; i++)
			{
				int dmax = 0;
				for (int c = 0; c < 3; c++) { const int dd = abs ((int) rgb[i * 3 + c] - (int) soft[i * 3 + c]); if (dd > dmax) dmax = dd; }
				if (dmax > 2) diff++;
				if (dmax > 32) big++;
				if (dmax > worst) worst = dmax;
			}
			printf ("against the software renderer's: %ld pixels of %d differ by more than 2 (%.1f %%), %ld by more than 32; the worst %d\n", diff, W * H, 100.0 * (double) diff / (double) (W * H), big, worst);
		}
		else printf ("%s: not a picture of this size\n", argv[3]);
		if (sf) fclose (sf);
	}
	return nFail ? 1 : 0;
}
