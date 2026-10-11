//
// vsclip_pi.cpp -- on the Pi: does the V3D clip by itself what the kernel clips on the processor? The same
// triangles (inside the view, far beyond its sides, through the eye's plane, beyond the near and far planes) are
// drawn twice with the stock shaders by kapi_gpu_render3 -- as usual, then with KAPI_GPU_B_NOCLIP (the kernel
// leaves the vertices alone: what a batch with a generated vertex shader needs) -- and the two pictures compared.
//
//   vsclip [out.ppm]        (the two pictures side by side when a file is named)
//
// Built by hand as an Onyx program (see docs/3DS-EMULATOR-STUDY.md, 9v). MIT License -- Copyright (c) 2026
// Stephane Wegener (see docs/LICENSING.md)
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "appkit/appkit.h"
#include "v3d/qpu.h"
#include "v3d/shaders.h"

enum { W = 256, H = 256 };
static unsigned pixA[W * H], pixB[W * H];

static int tri (float *v, int n, const float p[3][4], unsigned rgb)
{
	for (int k = 0; k < 3; k++)
	{
		float *d = v + (n + k) * 8;
		memcpy (d, p[k], sizeof (float) * 4);
		d[4] = (float) (rgb >> 16 & 255) / 255.0f * p[k][3]; d[5] = (float) (rgb >> 8 & 255) / 255.0f * p[k][3]; d[6] = (float) (rgb & 255) / 255.0f * p[k][3]; d[7] = p[k][3];
		d[4] = (float) (rgb >> 16 & 255) / 255.0f; d[5] = (float) (rgb >> 8 & 255) / 255.0f; d[6] = (float) (rgb & 255) / 255.0f; d[7] = 1.0f;
	}
	return n + 3;
}

int main (int argc, char **argv)
{
	char gpu[96];
	if (kapi_gpu_info (gpu, sizeof gpu) <= 0 || !qpu::setVersion (qpu::versionOf (gpu))) { printf ("no GPU\n"); return 1; }
	static qpu::Prog vs, cs, fs;
	qpu::passVS (vs, 8); qpu::passCS (cs); qpu::varyFS (fs, 4, true);
	struct kapi_gpu_program kp;
	memset (&kp, 0, sizeof kp);
	kp.vs = vs.words (); kp.nvs = (unsigned) vs.count (); kp.cs = cs.words (); kp.ncs = (unsigned) cs.count (); kp.fs = fs.words (); kp.nfs = (unsigned) fs.count ();
	kp.inputs = 8; kp.csInputs = 4; kp.csOutputs = 6; kp.varyings = 4; kp.flags = qpu::programFlags (KAPI_GPU_P_FS_FINAL);
	const int prog = kapi_gpu_program (-1, &kp);
	if (prog < 0) { printf ("gpu_program: %d\n", prog); return 1; }
	static float v[64 * 8];
	int n = 0;
	const float inside[3][4] = { { -0.5f, -0.5f, 0.0f, 1.0f }, { 0.5f, -0.4f, 0.0f, 1.0f }, { 0.0f, 0.6f, 0.0f, 1.0f } };
	const float wide[3][4] = { { -40.0f, -0.9f, 0.2f, 1.0f }, { 40.0f, -0.7f, 0.2f, 1.0f }, { 0.3f, 30.0f, 0.2f, 1.0f } };		// far beyond the sides
	const float eye[3][4] = { { -0.8f, 0.8f, 0.1f, 1.0f }, { 0.9f, 0.7f, 0.1f, 1.0f }, { 0.2f, -3.0f, -1.5f, -1.0f } };		// one vertex behind the eye
	const float deep[3][4] = { { -0.9f, -0.2f, -3.0f, 1.0f }, { -0.2f, -0.2f, 3.0f, 1.0f }, { -0.5f, 0.9f, 0.0f, 1.0f } };		// through the near and the far planes
	const float thin[3][4] = { { 0.2f, 0.1f, 0.3f, 0.01f * 20 }, { 0.9f * 0.2f * 5, 0.2f, 0.3f, 0.2f * 5 }, { 0.6f * 3, 0.9f * 3, 0.3f * 3, 3.0f } };	// different w's
	n = tri (v, n, wide, 0x2040C0); n = tri (v, n, inside, 0xE04020); n = tri (v, n, eye, 0x20C040); n = tri (v, n, deep, 0xE0E020); n = tri (v, n, thin, 0xC020C0);
	unsigned vu[4]; qpu::viewUniforms (W, H, vu);
	int diffs = -1, drawnA = 0, drawnB = 0;
	for (int pass = 0; pass < 2; pass++)
	{
		struct kapi_gpu_batch3 b;
		memset (&b, 0, sizeof b);
		b.b.count = (unsigned) n; b.b.program = prog; b.b.flags = KAPI_GPU_B_ZFUNC (7) | KAPI_GPU_B_NOZWRITE | (pass ? KAPI_GPU_B_NOCLIP : 0);
		b.b.vsUni = 0; b.b.vsNUni = 4; b.b.csUni = 0; b.b.csNUni = 2;
		for (int k = 0; k < 8; k++) { b.b.tex[k] = -1; b.b.texUni[k] = -1; }
		b.off = 0; b.stride = 8;
		unsigned *px = pass ? pixB : pixA;
		struct kapi_gpu_frame fr = { px, W, H, W, 0x101010, 0 };
		const int r = kapi_gpu_render3 (&fr, v, (unsigned) n * 8, &b, 1, vu, 4, 0);
		if (r != 0) { printf ("gpu_render3 (%s): %d\n", pass ? "not clipped by the kernel" : "clipped by the kernel", r); return 1; }
	}
	diffs = 0;
	for (int i = 0; i < W * H; i++)
	{
		if ((pixA[i] & 0xFFFFFF) != 0x101010) drawnA++;
		if ((pixB[i] & 0xFFFFFF) != 0x101010) drawnB++;
		const unsigned a = pixA[i], c = pixB[i];
		int worst = 0;
		for (int s = 0; s < 24; s += 8) { const int d = abs ((int) (a >> s & 255) - (int) (c >> s & 255)); if (d > worst) worst = d; }
		if (worst > 8) diffs++;
	}
	printf ("%s: the kernel's clipping draws %d pixels, the GPU's own %d; %d of %d differ\n", diffs * 200 < W * H ? "ok  " : "FAIL", drawnA, drawnB, diffs, W * H);
	if (argc > 1)
	{
		FILE *o = fopen (argv[1], "wb");
		if (o)
		{
			fprintf (o, "P6\n%d %d\n255\n", W * 2, H);
			for (int y = 0; y < H; y++)
				for (int x = 0; x < W * 2; x++)
				{
					const unsigned c = x < W ? pixA[y * W + x] : pixB[y * W + x - W];
					const unsigned char q[3] = { (unsigned char) (c >> 16), (unsigned char) (c >> 8), (unsigned char) c };
					fwrite (q, 1, 3, o);
				}
			fclose (o);
		}
	}
	return diffs * 200 < W * H ? 0 : 1;
}
