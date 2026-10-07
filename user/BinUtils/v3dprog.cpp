//
// v3dprog -- checks the programmable GPU (kapi v61 gpu_program / gpu_render2) on the Pi: draws
// small frames with shaders generated at run time (user/Libs/v3d/shaders.h) and compares the pixels
// with what they must be. One line a test (PASS / FAIL, with the first wrong pixel), then the
// total. Run it from the terminal or telnet: `v3dprog`.
//
#include "appkit/appkit.h"
#include "v3d/shaders.h"
#include "v3d/gxtev.h"
#include "v3d/gxtev_ref.h"

#define W 64
#define H 64
static unsigned s_Px[W * H];
static qpu::Prog s_VS, s_VS12, s_CS, s_Flat, s_FlatF, s_Vary, s_VaryF, s_Vary8, s_Tex;
static int s_nFail = 0, s_nTests = 0;
static unsigned s_View[4];					// x y scales, z scale / offset

static void puthex (unsigned v)
{
	char b[9]; for (int i = 0; i < 8; i++) b[i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 15]; b[8] = 0;
	ax_puts (b);
}
static void putint (int v) { char b[16]; ax_itoa (v, b); ax_puts (b); }
static unsigned fbits (float f) { union { float f; unsigned u; } c; c.f = f; return c.u; }

static void result (const char *name, bool ok, int x = -1, int y = -1, unsigned got = 0, unsigned want = 0)
{
	s_nTests++;
	ax_puts (ok ? "PASS  " : "FAIL  "); ax_puts (name);
	if (!ok && x >= 0)
	{
		ax_puts ("  (pixel "); putint (x); ax_puts (","); putint (y); ax_puts (": ");
		puthex (got & 0xFFFFFF); ax_puts (", expected "); puthex (want & 0xFFFFFF); ax_puts (")");
	}
	ax_putln ("");
	if (!ok) s_nFail++;
}

static bool near (unsigned a, unsigned b, int tol)
{
	for (int k = 0; k < 24; k += 8)
	{
		int d = (int) ((a >> k) & 255) - (int) ((b >> k) & 255);
		if (d < -tol || d > tol) return false;
	}
	return true;
}

// the pixels at the listed points against the expected colours
struct Pt { int x, y; unsigned c; };
static void expect (const char *name, const Pt *p, int n, int tol = 2)
{
	for (int i = 0; i < n; i++)
	{
		unsigned got = s_Px[p[i].y * W + p[i].x];
		if (!near (got, p[i].c, tol)) { result (name, false, p[i].x, p[i].y, got, p[i].c); return; }
	}
	result (name, true);
}

static int program (const qpu::Prog &vs, const qpu::Prog &fs, int inputs, int varyings, unsigned flags)
{
	struct kapi_gpu_program P;
	P.vs = vs.words (); P.nvs = (unsigned) vs.count ();
	P.cs = s_CS.words (); P.ncs = (unsigned) s_CS.count ();
	P.fs = fs.words (); P.nfs = (unsigned) fs.count ();
	P.inputs = (unsigned) inputs; P.csInputs = 4; P.csOutputs = 6; P.varyings = (unsigned) varyings;
	P.flags = flags;
	return kapi_gpu_program (-1, &P);
}

// vertices: a rectangle (NDC, y up) as two triangles, n floats a vertex: x y z w, then the
// attributes of its left and right edges (a gradient when they differ)
static float s_V[4096];
static unsigned s_nV = 0, s_nStride = 8;
static void quad (float x0, float y0, float x1, float y1, float z, const float *attrL, const float *attrR, float w = 1.0f)
{
	float P[4][3] = { { x0, y0, 0 }, { x1, y0, 1 }, { x1, y1, 1 }, { x0, y1, 0 } };
	static const int T[6] = { 0, 1, 2, 0, 2, 3 };
	for (int k = 0; k < 6; k++)
	{
		float *v = s_V + s_nV * s_nStride;
		const float *p = P[T[k]];
		v[0] = p[0] * w; v[1] = p[1] * w; v[2] = z * w; v[3] = w;
		for (unsigned j = 4; j < s_nStride; j++) v[j] = (p[2] != 0 ? attrR : attrL)[j - 4];
		s_nV++;
	}
}

static kapi_gpu_batch2 batch (int prog, unsigned first, unsigned count)
{
	kapi_gpu_batch2 b; memset (&b, 0, sizeof b);
	b.first = first; b.count = count; b.program = prog;
	b.vsUni = 0; b.vsNUni = 4; b.csUni = 0; b.csNUni = 2;
	for (int k = 0; k < 8; k++) { b.tex[k] = -1; b.texUni[k] = -1; }
	return b;
}

static unsigned s_Uni[4096];
static int render (const kapi_gpu_batch2 *b, unsigned nb, unsigned nUni, unsigned clear, bool keep = false)
{
	kapi_gpu_frame F; F.pixels = s_Px; F.w = W; F.h = H; F.stride = W; F.clear = clear; F.flags = keep ? KAPI_GPU_F_KEEP : 0;
	return kapi_gpu_render2 (&F, s_V, s_nV, s_nStride, b, nb, s_Uni, nUni);
}

static bool check (const char *name, int r)
{
	if (r == 0) return true;
	s_nTests++; s_nFail++;
	ax_puts ("FAIL  "); ax_puts (name); ax_puts ("  (gpu_render2 returned "); putint (r); ax_putln (")");
	return false;
}


// ---- the GameCube's TEV (user/Libs/v3d/gxtev): generated shaders against the CPU's reference --------------------
// Each configuration draws 64 quads (8 x 8 pixels), each with its own rasterized colours and texels
// (constant over the quad: the texel centres), and its quad's centre must be what gxtev_ref.h says.
static unsigned s_Seed = 12345;
static unsigned rnd () { s_Seed = s_Seed * 1103515245u + 12345u; return s_Seed >> 8; }
static int rr (int n) { return (int) (rnd () % (unsigned) n); }
static gxtev::Shader s_Tev;
static qpu::Prog s_TevVS;
static float s_TV[64 * 6 * 40];
static unsigned s_TU[4096];

static void randomTev (gxtev::Config &c, int nStages, int nLooks)
{
	memset (&c, 0, sizeof c);
	c.nStages = nStages;
	int looks = 0;
	for (int st = 0; st < nStages; st++)
	{
		c.cenv[st] = rnd () & 0xFFFFFF; c.aenv[st] = rnd () & 0xFFFFFF;
		unsigned tr = (unsigned) rr (8) | (unsigned) rr (8) << 3;
		if (looks < nLooks && (nStages - st <= nLooks - looks || rr (2))) { tr |= 64; looks++; }
		static const int chans[4] = { 0, 1, 7, 0 };
		tr |= (unsigned) chans[rr (4)] << 7;
		c.tref[st] = tr;
		c.ksel[st] = (unsigned) rr (32) | (unsigned) rr (32) << 5;
	}
	for (int k = 0; k < 16; k++) c.swap[k] = (unsigned char) (rr (2) ? k & 3 : rr (4));
	c.alphaFunc[0] = (unsigned char) (rr (3) ? 7 : rr (8)); c.alphaFunc[1] = (unsigned char) (rr (3) ? 7 : rr (8));
	c.alphaLogic = (unsigned char) rr (4);
	c.efbFmt = (unsigned char) rr (3);
	c.dstAlpha = rr (4) == 0;
	c.projMask = (unsigned char) rr (256);
}

static void tevTest (const char *name, const gxtev::Config &cf)
{
	if (!gxtev::build (cf, s_Tev)) { s_nTests++; s_nFail++; ax_puts ("FAIL  "); ax_puts (name); ax_puts (" (not generated: "); ax_puts (s_Tev.err); ax_putln (")"); return; }
	int nIn = 4 + s_Tev.nVary;
	s_TevVS.n = 0; s_TevVS.bad = false; qpu::passVS (s_TevVS, nIn);
	struct kapi_gpu_program P;
	P.vs = s_TevVS.words (); P.nvs = (unsigned) s_TevVS.count (); P.cs = s_CS.words (); P.ncs = (unsigned) s_CS.count ();
	P.fs = s_Tev.prog.words (); P.nfs = (unsigned) s_Tev.prog.count ();
	P.inputs = (unsigned) nIn; P.csInputs = 4; P.csOutputs = 6; P.varyings = (unsigned) s_Tev.nVary; P.flags = s_Tev.flags;
	int prog = kapi_gpu_program (-1, &P);
	// the dynamic values, the textures (4 x 4 each lookup)
	gxtev::Dyn dy;
	for (int k = 0; k < 16; k++) { dy.regs[k] = rr (2048) - 1024; dy.konst[k] = rr (256); }
	dy.aref[0] = rr (256); dy.aref[1] = rr (256); dy.dstA = (float) rr (256) / 255.0f;
	static unsigned Tx[8][16];
	int th[8];
	for (int L = 0; L < s_Tev.nLook; L++)
	{
		for (int i = 0; i < 16; i++) Tx[L][i] = rnd () | (unsigned) rr (256) << 24;
		th[L] = kapi_gpu_texture (-1, Tx[L], 4, 4, 4);
	}
	// the quads: per quad its colours (bytes / 255) and texels (their centres, x q when projective)
	struct Q { int c0[4], c1[4], tx[8], ty[8]; float q[8]; } quads[64];
	unsigned nV = 0;
	for (int i = 0; i < 64; i++)
	{
		Q &qd = quads[i];
		for (int k = 0; k < 4; k++) { qd.c0[k] = rr (256); qd.c1[k] = rr (256); }
		for (int L = 0; L < 8; L++) { qd.tx[L] = rr (4); qd.ty[L] = rr (4); qd.q[L] = rr (2) ? 1.0f : 2.0f; }
		float x0 = -1 + (i & 7) * 0.25f, y1 = 1 - (i >> 3) * 0.25f;
		float Pp[4][2] = { { x0, y1 - 0.25f }, { x0 + 0.25f, y1 - 0.25f }, { x0 + 0.25f, y1 }, { x0, y1 } };
		static const int T6[6] = { 0, 1, 2, 0, 2, 3 };
		for (int k = 0; k < 6; k++)
		{
			float *v = s_TV + nV * (unsigned) nIn;
			v[0] = Pp[T6[k]][0]; v[1] = Pp[T6[k]][1]; v[2] = 0; v[3] = 1;
			for (int j = 0; j < s_Tev.nVary; j++)
			{
				const gxtev::Vary &vy = s_Tev.vary[j];
				float f;
				if (vy.kind == gxtev::V_C0) f = (float) qd.c0[vy.a] / 255.0f;
				else if (vy.kind == gxtev::V_C1) f = (float) qd.c1[vy.a] / 255.0f;
				else
				{
					int L = vy.a; float q = s_Tev.look[L].proj ? qd.q[L] : 1.0f;
					f = vy.b == 0 ? ((float) qd.tx[L] + 0.5f) / 4.0f * q : vy.b == 1 ? ((float) qd.ty[L] + 0.5f) / 4.0f * q : q;
				}
				v[4 + j] = f;
			}
			nV++;
		}
	}
	// the uniforms: view (0..3), then the fragment shader's
	for (int k = 0; k < 4; k++) s_TU[k] = s_View[k];
	kapi_gpu_batch2 b = batch (prog, 0, nV);
	b.fsUni = 4; b.fsNUni = (unsigned) s_Tev.nUni;
	for (int i = 0; i < s_Tev.nUni; i++)
	{
		const gxtev::Uni &u = s_Tev.uni[i];
		unsigned v = 0;
		switch (u.kind)
		{
		case gxtev::U_CONST: v = u.value; break;
		case gxtev::U_REG: v = (unsigned) dy.regs[u.a * 4 + u.b]; break;
		case gxtev::U_KONST: v = (unsigned) dy.konst[u.a * 4 + u.b]; break;
		case gxtev::U_ALPHAREF: v = (unsigned) dy.aref[u.a]; break;
		case gxtev::U_TEXP0: b.tex[u.a] = th[u.a]; b.texUni[u.a] = i; b.texFlags[u.a] = 0; break;
		case gxtev::U_DSTALPHA: v = fbits (dy.dstA); break;
		default: break;
		}
		s_TU[4 + i] = v;
	}
	kapi_gpu_frame F; F.pixels = s_Px; F.w = W; F.h = H; F.stride = W; F.clear = 0x123456; F.flags = 0;
	int r = prog < 0 ? prog : kapi_gpu_render2 (&F, s_TV, nV, (unsigned) nIn, &b, 1, s_TU, 4 + (unsigned) s_Tev.nUni);
	if (prog >= 0) kapi_gpu_program (prog, 0);
	for (int L = 0; L < s_Tev.nLook; L++) if (th[L] >= 0) kapi_gpu_texture (th[L], 0, 0, 0, 0);
	if (r != 0) { s_nTests++; s_nFail++; ax_puts ("FAIL  "); ax_puts (name); ax_puts (" (gpu_render2 / gpu_program: "); putint (r); ax_putln (")"); return; }
	// the quads' centres
	for (int i = 0; i < 64; i++)
	{
		const Q &qd = quads[i];
		int texel[8][4];
		for (int L = 0; L < s_Tev.nLook; L++)
		{
			unsigned c = Tx[L][qd.ty[L] * 4 + qd.tx[L]];			// 0xAARRGGBB -> R G B A
			texel[L][0] = (int) (c >> 16 & 255); texel[L][1] = (int) (c >> 8 & 255); texel[L][2] = (int) (c & 255); texel[L][3] = (int) (c >> 24);
		}
		int o[4];
		bool pass = gxtev::reference (cf, dy, qd.c0, qd.c1, texel, o);
		unsigned want = 0x123456;
		if (pass)
		{
			int fmt = cf.efbFmt <= 2 ? cf.efbFmt : 0, e[3];
			for (int ch = 0; ch < 3; ch++)
			{
				int shr = fmt == 1 ? 2 : fmt == 2 ? (ch == 1 ? 2 : 3) : 0, mx = fmt == 1 ? 63 : fmt == 2 ? (ch == 1 ? 63 : 31) : 255;
				e[ch] = ((o[ch] >> shr) * 255 * 2 + mx) / (2 * mx);		// (rounded)
			}
			want = (unsigned) (e[0] << 16 | e[1] << 8 | e[2]);
		}
		int x = (i & 7) * 8 + 4, y = (i >> 3) * 8 + 4;
		unsigned got = s_Px[y * W + x];
		if (!near (got, want, 1)) { result (name, false, x, y, got, want); return; }
	}
	s_nTests++;
	ax_puts ("PASS  "); ax_puts (name); ax_puts (" ("); putint (cf.nStages); ax_puts (" stages, "); putint (s_Tev.nLook);
	ax_puts (" lookups, "); putint (s_Tev.prog.count ()); ax_putln (" instructions)");
}

// `v3dprog ww <w> <h> <flags> <percent> <variant>`: The Wind Waker's first frame as gcemu draws it --
// the TEV program of its background (the rasterized colour, RGBA6), one quad covering <percent> of a
// w x h target, the batch's flags (gcemu's: F = depth always, no depth write) -- to find what stops
// the GPU there (it did not with the 64 x 64 tests). variant: +1 the plain varyings shader instead
// of the TEV's, +2 the TEV in RGB8, +4 no scissor, +8 the flat colour shader (4 uniforms, final section),
// +16 the flat colour shader with a thread switch. Prints gpu_render2's result.
static unsigned s_Big[1920 * 1080];
static int wwFrame (int w, int h, unsigned flags, float scale, int variant)
{
	if (w < 16 || h < 16 || w * h > 1920 * 1080) { ax_putln ("ww: bad size"); return 1; }
	gxtev::Config ww; memset (&ww, 0, sizeof ww);
	ww.nStages = 1; ww.cenv[0] = 0x08FFFA; ww.aenv[0] = 0x08FFD0; ww.tref[0] = 0; ww.ksel[0] = 6;
	for (int k = 0; k < 16; k++) ww.swap[k] = (unsigned char) (k & 3);
	ww.alphaFunc[0] = ww.alphaFunc[1] = 7; ww.efbFmt = (variant & 2) ? 0 : 1;
	if (!gxtev::build (ww, s_Tev)) { ax_putln ("ww: not generated"); return 1; }
	int nIn = 4 + s_Tev.nVary;
	s_TevVS.n = 0; s_TevVS.bad = false; qpu::passVS (s_TevVS, nIn);
	struct kapi_gpu_program P;
	P.vs = s_TevVS.words (); P.nvs = (unsigned) s_TevVS.count (); P.cs = s_CS.words (); P.ncs = (unsigned) s_CS.count ();
	P.fs = s_Tev.prog.words (); P.nfs = (unsigned) s_Tev.prog.count ();
	P.inputs = (unsigned) nIn; P.csInputs = 4; P.csOutputs = 6; P.varyings = (unsigned) s_Tev.nVary; P.flags = s_Tev.flags;
	if (variant & 1) { P.fs = s_VaryF.words (); P.nfs = (unsigned) s_VaryF.count (); P.varyings = 4; P.flags = KAPI_GPU_P_FS_FINAL; }
	if (variant & 8) { P.fs = s_FlatF.words (); P.nfs = (unsigned) s_FlatF.count (); P.varyings = 0; P.flags = KAPI_GPU_P_FS_4WAY | KAPI_GPU_P_FS_FINAL; }
	if (variant & 16) { P.fs = s_Flat.words (); P.nfs = (unsigned) s_Flat.count (); P.varyings = 0; P.flags = 0; }
	int prog = kapi_gpu_program (-1, &P);
	ax_puts ("ww: program "); putint (prog); ax_puts (" ("); putint ((int) P.nfs); ax_puts (" instructions, flags ");
	putint ((int) P.flags); ax_puts ("), target "); putint (w); ax_puts (" x "); putint (h); ax_puts (", batch flags ");
	puthex (flags); ax_puts (", quad "); putint ((int) (scale * 100)); ax_puts ("%, variant "); putint (variant); ax_putln ("");
	if (prog < 0) return 1;
	// the quad (gcemu's: z -0, w 1, its bottom row just above the frame's), the colour black, alpha 1
	float x0 = -scale, x1 = scale, y0 = -0.998f * scale, y1 = scale;
	float Pp[6][2] = { { x0, y1 }, { x1, y1 }, { x1, y0 }, { x0, y1 }, { x1, y0 }, { x0, y0 } };
	for (int k = 0; k < 6; k++)
	{
		float *v = s_TV + k * nIn;
		v[0] = Pp[k][0]; v[1] = Pp[k][1]; v[2] = -0.0f; v[3] = 1;
		for (int j = 0; j < s_Tev.nVary; j++) v[4 + j] = s_Tev.vary[j].a == 3 ? 1.0f : 0.0f;
	}
	unsigned view[4]; qpu::viewUniforms (w, h, view);
	for (int k = 0; k < 4; k++) s_TU[k] = view[k];
	kapi_gpu_batch2 b = batch (prog, 0, 6);
	b.flags = flags; b.fsUni = 4; b.fsNUni = (unsigned) s_Tev.nUni;
	if (!(variant & 4)) { b.scissor[0] = 0; b.scissor[1] = 0; b.scissor[2] = w; b.scissor[3] = (int) (h * 0.998f + 0.5f); }
	for (int i = 0; i < s_Tev.nUni; i++) s_TU[4 + i] = s_Tev.uni[i].kind == gxtev::U_CONST ? s_Tev.uni[i].value : 0;
	if (variant & 1) { b.fsNUni = 0; }			// (varyFS: no uniforms)
	if (variant & 24) { b.fsNUni = 4; s_TU[4] = s_TU[5] = s_TU[6] = 0; s_TU[7] = fbits (1.0f); }	// (flatFS: the colour)
	kapi_gpu_frame F; F.pixels = s_Big; F.w = w; F.h = h; F.stride = w; F.clear = 0x404040; F.flags = 0;
	int r = kapi_gpu_render2 (&F, s_TV, 6, (unsigned) nIn, &b, 1, s_TU, 4 + (unsigned) s_Tev.nUni);
	kapi_gpu_program (prog, 0);
	ax_puts ("ww: gpu_render2 "); putint (r); ax_puts (", the centre pixel "); puthex (s_Big[(h / 2) * w + w / 2] & 0xFFFFFF);
	ax_puts (", a corner "); puthex (s_Big[2 * w + 2] & 0xFFFFFF); ax_putln ("");
	return r ? 1 : 0;
}

int main (void)
{
	char info[96];
	if (!kapi_gpu_info (info, sizeof info)) { ax_puts ("no GPU: "); ax_putln (info); return 1; }
	ax_puts ("GPU: "); ax_putln (info);
	if (kapi_abi_version () < 61) { ax_putln ("the kernel is older than kapi v61 (gpu_program)"); return 1; }
	qpu::passVS (s_VS, 8); qpu::passVS (s_VS12, 12); qpu::passCS (s_CS);
	qpu::flatFS (s_Flat, false); qpu::flatFS (s_FlatF, true);
	qpu::varyFS (s_Vary, 4, false); qpu::varyFS (s_VaryF, 4, true); qpu::varyFS (s_Vary8, 8, false);
	qpu::texFS (s_Tex);
	const qpu::Prog *all[] = { &s_VS, &s_VS12, &s_CS, &s_Flat, &s_FlatF, &s_Vary, &s_VaryF, &s_Vary8, &s_Tex };
	for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++)
		if (!all[i]->ok ()) { ax_puts ("shader "); putint ((int) i); ax_puts (": instruction "); putint (all[i]->badAt); ax_putln (" not encodable"); return 1; }
	qpu::viewUniforms (W, H, s_View);
	for (int k = 0; k < 4; k++) s_Uni[k] = s_View[k];		// every batch: vs 0..3, cs 0..1
	{
		char args[128]; kapi_get_args (args, sizeof args);
		int a = 0; while (args[a] == ' ') a++;
		if (args[a] == 'w' && args[a + 1] == 'w')			// the experiment above
		{
			a += 2;
			auto num = [&] (int def) -> int
			{
				while (args[a] == ' ') a++;
				if (!args[a]) return def;
				int v = 0, base = 10;
				if (args[a] == '0' && args[a + 1] == 'x') { base = 16; a += 2; }
				for (;; a++)
				{
					char ch = args[a]; int dgt;
					if (ch >= '0' && ch <= '9') dgt = ch - '0';
					else if (base == 16 && ch >= 'a' && ch <= 'f') dgt = ch - 'a' + 10;
					else if (base == 16 && ch >= 'A' && ch <= 'F') dgt = ch - 'A' + 10;
					else break;
					v = v * base + dgt;
				}
				return v;
			};
			int w = num (640), h = num (480);
			unsigned fl = (unsigned) num (0xF);
			int pct = num (100), variant = num (0);
			return wwFrame (w, h, fl, (float) pct / 100.0f, variant);
		}
	}

	int pFlat = program (s_VS, s_Flat, 8, 0, 0);			// (2 threads, a thread switch)
	int pFlatF = program (s_VS, s_FlatF, 8, 0, KAPI_GPU_P_FS_4WAY | KAPI_GPU_P_FS_FINAL);
	int pVary = program (s_VS, s_Vary, 8, 4, KAPI_GPU_P_FS_4WAY);
	int pVaryF = program (s_VS, s_VaryF, 8, 4, KAPI_GPU_P_FS_FINAL);
	int pVary8 = program (s_VS12, s_Vary8, 12, 8, KAPI_GPU_P_FS_4WAY);
	int pTex = program (s_VS, s_Tex, 8, 4, KAPI_GPU_P_FS_4WAY);	// (the varyings s t + 2 not read)
	if (pFlat < 0 || pFlatF < 0 || pVary < 0 || pVaryF < 0 || pVary8 < 0 || pTex < 0)
	{
		ax_puts ("gpu_program failed: "); putint (pFlat); ax_puts (" "); putint (pFlatF); ax_puts (" "); putint (pVary); ax_puts (" ");
		putint (pVaryF); ax_puts (" "); putint (pVary8); ax_puts (" "); putint (pTex); ax_putln ("");
		return 1;
	}
	static const float Red[4] = { 1, 0, 0, 1 }, Green[4] = { 0, 1, 0, 1 }, Blue[4] = { 0, 0, 1, 1 };
	static const float Black[4] = { 0, 0, 0, 1 }, White[4] = { 1, 1, 1, 1 }, Zero[8] = { 0 };
	unsigned t0 = kapi_get_ticks ();

	// 1, 2: a colour from the uniforms, over the whole frame (both fragment shader kinds)
	for (int f = 0; f < 2; f++)
	{
		s_nStride = 8; s_nV = 0; quad (-1, -1, 1, 1, 0, Zero, Zero);
		s_Uni[4] = fbits (0.25f); s_Uni[5] = fbits (0.5f); s_Uni[6] = fbits (0.75f); s_Uni[7] = fbits (1.0f);
		kapi_gpu_batch2 b = batch (f ? pFlatF : pFlat, 0, 6); b.fsUni = 4; b.fsNUni = 4;
		const char *nm = f ? "uniform colour (single segment, 4 threads)" : "uniform colour (thread switch, 2 threads)";
		if (check (nm, render (&b, 1, 8, 0x000000)))
		{
			static const Pt p[] = { { 0, 0, 0x4080BF }, { 63, 0, 0x4080BF }, { 0, 63, 0x4080BF }, { 63, 63, 0x4080BF }, { 31, 32, 0x4080BF } };
			expect (nm, p, 5);
		}
	}
	// 3, 4: varyings -- two flat halves, then a gradient
	for (int f = 0; f < 2; f++)
	{
		s_nStride = 8; s_nV = 0;
		quad (-1, -1, 0, 1, 0, Red, Red); quad (0, -1, 1, 1, 0, Blue, Blue);
		kapi_gpu_batch2 b = batch (f ? pVaryF : pVary, 0, 12);
		const char *nm = f ? "varyings (single segment)" : "varyings (4 threads)";
		if (check (nm, render (&b, 1, 4, 0x000000)))
		{
			static const Pt p[] = { { 2, 5, 0xFF0000 }, { 30, 60, 0xFF0000 }, { 33, 5, 0x0000FF }, { 62, 60, 0x0000FF } };
			expect (nm, p, 4);
		}
	}
	{
		s_nStride = 8; s_nV = 0; quad (-1, -1, 1, 1, 0, Black, White);
		kapi_gpu_batch2 b = batch (pVary, 0, 6);
		if (check ("gradient", render (&b, 1, 4, 0x000000)))
		{
			Pt p[8];
			for (int i = 0; i < 8; i++)
			{
				int x = i * 9 + 1; unsigned v = (unsigned) (((float) x + 0.5f) / 64.0f * 255.0f + 0.5f);
				p[i].x = x; p[i].y = 20 + i * 3; p[i].c = v << 16 | v << 8 | v;
			}
			expect ("gradient (interpolation)", p, 8, 4);
		}
	}
	// 5: 12 floats a vertex, 8 varyings (the vertex shader's copy loop)
	{
		s_nStride = 12; s_nV = 0;
		float A[8] = { 0, 1, 0, 1, 0.3f, 0.3f, 0.3f, 0.3f }, B[8] = { 1, 0, 0, 1, 0.6f, 0.6f, 0.6f, 0.6f };
		quad (-1, -1, 0, 1, 0, A, A); quad (0, -1, 1, 1, 0, B, B);
		kapi_gpu_batch2 b = batch (pVary8, 0, 12);
		if (check ("12-float vertices", render (&b, 1, 4, 0x000000)))
		{
			static const Pt p[] = { { 5, 5, 0x00FF00 }, { 40, 40, 0xFF0000 } };
			expect ("12-float vertices, 8 varyings", p, 2);
		}
	}
	// 6: a texture (4 x 4 texels, nearest): each 16 x 16 block of the frame is one texel
	{
		static unsigned Tx[16];
		for (int i = 0; i < 16; i++) Tx[i] = 0xFF000000u | (unsigned) ((i * 16) << 16 | (255 - i * 16) << 8 | ((i & 3) * 80));
		int t = kapi_gpu_texture (-1, Tx, 4, 4, 4);
		s_nStride = 8; s_nV = 0;
		float L[4] = { 0, 0, 0, 0 }, R[4] = { 1, 0, 0, 0 };		// s t (t: 0 at the top)
		float *v;
		quad (-1, -1, 1, 1, 0, L, R);
		for (unsigned k = 0; k < 6; k++) { v = s_V + k * 8; v[5] = v[1] > 0 ? 0.0f : 1.0f; }
		s_Uni[4] = 0; s_Uni[5] = 0;					// (p0 p1: written by the kernel)
		kapi_gpu_batch2 b = batch (pTex, 0, 6); b.fsUni = 4; b.fsNUni = 2;
		b.tex[0] = t; b.texUni[0] = 0; b.texFlags[0] = KAPI_GPU_B_WRAP_S (1) | KAPI_GPU_B_WRAP_T (1);
		if (t < 0) { s_nTests++; s_nFail++; ax_putln ("FAIL  texture (gpu_texture failed)"); }
		else if (check ("texture", render (&b, 1, 6, 0x000000)))
		{
			Pt p[16];
			for (int i = 0; i < 16; i++) { p[i].x = (i & 3) * 16 + 8; p[i].y = (i >> 2) * 16 + 8; p[i].c = Tx[i] & 0xFFFFFF; }
			expect ("texture (nearest, t = 0 at the top)", p, 16);
		}
		if (t >= 0) kapi_gpu_texture (t, 0, 0, 0, 0);
	}
	// 7: the scissor (x 8..27, y 16..25 from the top)
	{
		s_nStride = 8; s_nV = 0; quad (-1, -1, 1, 1, 0, Zero, Zero);
		s_Uni[4] = fbits (1); s_Uni[5] = fbits (1); s_Uni[6] = fbits (1); s_Uni[7] = fbits (1);
		kapi_gpu_batch2 b = batch (pFlatF, 0, 6); b.fsUni = 4; b.fsNUni = 4;
		b.scissor[0] = 8; b.scissor[1] = 16; b.scissor[2] = 20; b.scissor[3] = 10;
		if (check ("scissor", render (&b, 1, 8, 0x102030)))
		{
			static const Pt p[] = { { 8, 16, 0xFFFFFF }, { 27, 25, 0xFFFFFF }, { 7, 20, 0x102030 }, { 28, 20, 0x102030 },
						{ 15, 15, 0x102030 }, { 15, 26, 0x102030 }, { 15, 40, 0x102030 } };
			expect ("scissor (top-left origin)", p, 7);
		}
	}
	// 8: blending (source alpha 0.5 over the kept pixels) ; 9: the colour write mask
	{
		for (int i = 0; i < W * H; i++) s_Px[i] = 0x204080;
		s_nStride = 8; s_nV = 0; quad (-1, -1, 1, 1, 0, Zero, Zero);
		s_Uni[4] = fbits (1); s_Uni[5] = fbits (0); s_Uni[6] = fbits (0); s_Uni[7] = fbits (0.5f);
		kapi_gpu_batch2 b = batch (pFlatF, 0, 6); b.fsUni = 4; b.fsNUni = 4;
		b.blend = KAPI_GPU_BLEND2 (6, 7, 6, 7, 0, 0);
		if (check ("blending", render (&b, 1, 8, 0, true)))
		{
			static const Pt p[] = { { 10, 10, 0x8F2040 }, { 50, 50, 0x8F2040 } };
			expect ("blending (src alpha, 1 - src alpha)", p, 2, 3);
		}
		s_Uni[4] = fbits (1); s_Uni[5] = fbits (1); s_Uni[6] = fbits (1); s_Uni[7] = fbits (1);
		b = batch (pFlatF, 0, 6); b.fsUni = 4; b.fsNUni = 4; b.wmask = 2;	// G not written
		if (check ("write mask", render (&b, 1, 8, 0x000000)))
		{
			static const Pt p[] = { { 10, 10, 0xFF00FF }, { 50, 50, 0xFF00FF } };
			expect ("colour write mask (green kept)", p, 2);
		}
	}
	// 10: depth -- red at z 0, then green in front on the left, behind on the right
	{
		s_nStride = 8; s_nV = 0;
		quad (-1, -1, 1, 1, 0, Red, Red);
		quad (-1, -1, 0, 1, -0.5f, Green, Green); quad (0, -1, 1, 1, 0.5f, Green, Green);
		kapi_gpu_batch2 b[2] = { batch (pVary, 0, 6), batch (pVary, 6, 12) };
		if (check ("depth", render (b, 2, 4, 0x000000)))
		{
			static const Pt p[] = { { 10, 30, 0x00FF00 }, { 50, 30, 0xFF0000 } };
			expect ("depth test (less)", p, 2);
		}
	}
	// 11: the near plane crossing the frame: z from 0 (bottom) to -2 (top) -> the top half cut
	{
		s_nStride = 8; s_nV = 0;
		float *v; quad (-1, -1, 1, 1, 0, Blue, Blue);
		for (unsigned k = 0; k < 6; k++) { v = s_V + k * 8; v[2] = v[1] > 0 ? -2.0f : 0.0f; }
		kapi_gpu_batch2 b = batch (pVary, 0, 6);
		if (check ("near plane", render (&b, 1, 4, 0x000000)))
		{
			static const Pt p[] = { { 32, 40, 0x0000FF }, { 32, 60, 0x0000FF }, { 32, 20, 0x000000 }, { 32, 2, 0x000000 } };
			expect ("near-plane clipping", p, 4);
		}
	}
	// 12: 64 batches, each its own uniforms (a grid of 8 x 8 colours)
	{
		static kapi_gpu_batch2 b[64];
		s_nStride = 8; s_nV = 0;
		unsigned nUni = 4;
		for (int i = 0; i < 64; i++)
		{
			float x0 = -1 + (i & 7) * 0.25f, y1 = 1 - (i >> 3) * 0.25f;
			quad (x0, y1 - 0.25f, x0 + 0.25f, y1, 0, Zero, Zero);
			b[i] = batch (i & 1 ? pFlat : pFlatF, (unsigned) i * 6, 6);
			b[i].fsUni = nUni; b[i].fsNUni = 4;
			s_Uni[nUni++] = fbits ((float) (i & 7) / 7.0f); s_Uni[nUni++] = fbits ((float) (i >> 3) / 7.0f);
			s_Uni[nUni++] = fbits (0.5f); s_Uni[nUni++] = fbits (1);
		}
		if (check ("64 batches", render (b, 64, nUni, 0x000000)))
		{
			Pt p[64];
			for (int i = 0; i < 64; i++)
			{
				unsigned r = (unsigned) ((float) (i & 7) / 7.0f * 255.0f + 0.5f), g = (unsigned) ((float) (i >> 3) / 7.0f * 255.0f + 0.5f);
				p[i].x = (i & 7) * 8 + 4; p[i].y = (i >> 3) * 8 + 4; p[i].c = r << 16 | g << 8 | 128;
			}
			expect ("64 batches, their own uniforms", p, 64);
		}
	}

	// 13..: the TEV
	{
		gxtev::Config cf; memset (&cf, 0, sizeof cf);
		cf.nStages = 1;
		cf.cenv[0] = 0xF << 12 | 8 << 8 | 10 << 4 | 0xF | 1u << 19;		// MODULATE: texc x rasc
		cf.aenv[0] = 7u << 13 | 4u << 10 | 5u << 7 | 7u << 4 | 1u << 19;
		cf.tref[0] = 64;
		for (int k = 0; k < 16; k++) cf.swap[k] = (unsigned char) (k & 3);
		cf.alphaFunc[0] = cf.alphaFunc[1] = 7;
		tevTest ("TEV modulate", cf);
		// The Wind Waker's first frame (the Nintendo logo's background): the rasterized colour, no
		// texture, the alpha test always passing, the EFB in RGBA6 -- the program that stopped the GPU
		// in gcemu (a frame that never finished rendering)
		{
			gxtev::Config ww; memset (&ww, 0, sizeof ww);
			ww.nStages = 1; ww.cenv[0] = 0x08FFFA; ww.aenv[0] = 0x08FFD0; ww.tref[0] = 0; ww.ksel[0] = 6;
			for (int k = 0; k < 16; k++) ww.swap[k] = (unsigned char) (k & 3);
			ww.alphaFunc[0] = ww.alphaFunc[1] = 7; ww.efbFmt = 1;
			tevTest ("TEV rasterized colour, RGBA6 (Wind Waker's first)", ww);
			ww.efbFmt = 0;
			tevTest ("TEV rasterized colour, RGB8", ww);
		}
		static const int shape[][2] = { { 1, 0 }, { 2, 1 }, { 3, 2 }, { 4, 4 }, { 6, 5 }, { 8, 8 }, { 12, 3 }, { 16, 6 } };
		for (int k = 0; k < 8; k++)
		{
			char nm[32] = "TEV random "; int p = 11; nm[p++] = (char) ('1' + k); nm[p] = 0;
			randomTev (cf, shape[k][0], shape[k][1]);
			tevTest (nm, cf);
		}
	}

	unsigned t1 = kapi_get_ticks ();
	int Hs[] = { pFlat, pFlatF, pVary, pVaryF, pVary8, pTex };
	for (int h : Hs) kapi_gpu_program (h, 0);
	ax_puts (s_nFail ? "FAILED: " : "ALL PASS: "); putint (s_nTests - s_nFail); ax_puts ("/"); putint (s_nTests);
	ax_puts (" tests ok ("); putint ((int) (t1 - t0) * 10); ax_putln (" ms)");		// (100 ticks a second)
	return s_nFail ? 1 : 0;
}
