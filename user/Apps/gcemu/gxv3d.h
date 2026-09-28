//
// gxv3d.h -- gcemu's GX on the V3D with the TEV as shaders (kapi v61): gc::GxGpu for
// Machine::gpu, as NintendoEMU's OpenGL renderer (pc/NintendoEMU/core/gxgl.cpp) but split in two:
//
//   * on the app core, while the machine runs (no kapi call there): each draw's vertices are
//     transformed, lit and given their texture coordinates (gxgl.cpp's vertex shader, in C++), in
//     the clip space of the EFB's rectangle the XFB copy takes; the draw's TEV configuration gets
//     its fragment shader (user/v3d/gxtev: generated once, cached by its key), whose varyings the
//     vertices are written as; the TEV registers, konst colours, alpha references go into the
//     frame's uniforms; the depth / blending / culling / masks / scissor into the batch. A copy of
//     the EFB to the XFB ends the frame (the next one is built in the other buffer).
//   * on the main thread, the machine waiting: the new programs (gpu_program: the vertex shader
//     hands the varyings on, user/v3d/shaders.h), the textures decoded since (gpu_texture), then
//     the frame (gpu_render2) into the window.
//
// Not yet: the EFB copies to textures (a texture read from one: the batch is not drawn; a copy
// that clears the EFB drops what was drawn before it), the indirect texturing, the fog, the z
// texture, lines and points, the logic ops.
//
#ifndef GCEMU_GXV3D_H
#define GCEMU_GXV3D_H
#include "gc/gc.h"
#include "v3d/gxtev.h"
#include "v3d/shaders.h"

namespace gxv3d
{
using gc::u8; using gc::u32; using gc::s32; using gc::u64;

enum { MAX_PROGS = 1024, ARENA_WORDS = 1 << 20, ARENA_UNI = 1 << 18 };
enum { MAX_FLOATS = 3 << 20, MAX_BATCHES = 4096, MAX_UNIS = 1 << 18 };

// a TEV configuration's program: its code and layout (made on the app core), its GPU handle (the main thread's)
struct Prog
{
	gxtev::Config cfg; unsigned long long key;
	u32 words, nWords;			// (in the arena)
	unsigned flags;
	int nVary; gxtev::Vary vary[64];
	int nLook; gxtev::Lookup look[gxtev::MAX_LOOKUPS];
	u32 uni, nUni;				// (its uniform kinds, in the arena)
	int handle;				// -1: not given to the GPU yet, -2: refused
};

struct Batch
{
	u32 first, count;			// (vertices: floats from `off`, `stride` a vertex)
	u32 off; int stride;
	int prog;
	u32 flags, blend, wmask;
	float scissor[4];			// x0 y0 x1 y1 over the frame (0..1)
	u32 uni;				// its fragment uniforms in the frame's
	int texSlot[8]; u32 texFlags[8];	// (Machine::tex[] slots, the lookups')
};

struct Frame
{
	float *v; u32 nf, nv;
	Batch *b; u32 nb;
	u32 *u; u32 nu;
	u32 clear; bool keep;
	void reset () { nf = nv = nb = nu = 0; }
};

class Rec : public gc::GxGpu
{
public:
	Prog prog[MAX_PROGS]; volatile int nProg = 0;
	unsigned long long *arena = 0; u32 arenaN = 0;	// the programs' words
	gxtev::Uni *uniArena = 0; u32 uniN = 0;
	Frame frame[2]; int build = 0; volatile int ready = -1;
	u32 serial = 0;					// (frames finished)
	u32 unsupported = 0, skipped = 0;		// (stats: draws with a feature not generated, not drawn)
	float *tmp = 0;					// (a draw's vertices before its triangles)

	bool init ()
	{
		arena = new unsigned long long[ARENA_WORDS]; uniArena = new gxtev::Uni[ARENA_UNI];
		tmp = new float[0x10000 * 68];
		for (int i = 0; i < 2; i++)
		{
			frame[i].v = new float[MAX_FLOATS]; frame[i].b = new Batch[MAX_BATCHES]; frame[i].u = new u32[MAX_UNIS];
			if (!frame[i].v || !frame[i].b || !frame[i].u) return false;
			frame[i].reset (); frame[i].clear = 0; frame[i].keep = false;
		}
		return arena && uniArena && tmp;
	}

	// ---- the program of a configuration (the app core: the shader's generation, cached)
	int findProg (const gxtev::Config &c)
	{
		unsigned long long k = gxtev::key (c);
		int n = nProg;
		for (int i = n - 1; i >= 0; i--)
			if (prog[i].key == k && !__builtin_memcmp (&prog[i].cfg, &c, sizeof c)) return i;
		if (n >= MAX_PROGS) return -1;
		static gxtev::Shader sh;
		if (!gxtev::build (c, sh)) return -1;
		if (arenaN + (u32) sh.prog.count () > ARENA_WORDS || uniN + (u32) sh.nUni > ARENA_UNI) return -1;
		Prog &p = prog[n];
		p.cfg = c; p.key = k; p.flags = sh.flags; p.handle = -1;
		p.words = arenaN; p.nWords = (u32) sh.prog.count ();
		for (int i = 0; i < sh.prog.count (); i++) arena[arenaN++] = sh.prog.words ()[i];
		p.nVary = sh.nVary; for (int i = 0; i < sh.nVary; i++) p.vary[i] = sh.vary[i];
		p.nLook = sh.nLook; for (int i = 0; i < sh.nLook; i++) p.look[i] = sh.look[i];
		p.uni = uniN; p.nUni = (u32) sh.nUni;
		for (int i = 0; i < sh.nUni; i++) uniArena[uniN++] = sh.uni[i];
		if (sh.unsupported) unsupported++;
		__sync_synchronize ();
		nProg = n + 1;
		return n;
	}

	// ---- the vertex stage (gxgl.cpp's VS)
	static float f (const gc::Machine &m, u32 a) { float r; __builtin_memcpy (&r, &m.xfRegs[a], 4); return r; }
	static void rgba (u32 c, float o[4]) { o[0] = (float) (c >> 24); o[1] = (float) ((c >> 16) & 255); o[2] = (float) ((c >> 8) & 255); o[3] = (float) (c & 255); }
	static float dot3 (const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
	static void norm3 (float *v) { float l = __builtin_sqrtf (dot3 (v, v)); if (l > 0) { v[0] /= l; v[1] /= l; v[2] /= l; } }
	// a colour channel's colour or alpha (0..255): the material x (the ambient + the lights)
	static void channel (const gc::Machine &m, int ctl, const float regMat[4], const float regAmb[4], const float vcol[4],
			     const float pos[3], const float n[3], float out[4])
	{
		const float *mat = (ctl & 1) ? vcol : regMat;
		if (!(ctl & 2)) { for (int c = 0; c < 4; c++) out[c] = mat[c]; return; }
		float lacc[4];
		for (int c = 0; c < 4; c++) lacc[c] = (ctl & 64) ? vcol[c] : regAmb[c];
		int mask = ((ctl >> 2) & 15) | ((ctl >> 7) & 0xF0);
		int dfn = (ctl >> 7) & 3, atn = (ctl >> 9) & 3;
		for (int L = 0; L < 8; L++)
		{
			if (!(mask & (1 << L))) continue;
			u32 b = 0x600 + (u32) L * 16;
			float lcol[4]; rgba (m.xfRegs[b + 3], lcol);
			float cosA[3] = { f (m, b + 4), f (m, b + 5), f (m, b + 6) }, distA[3] = { f (m, b + 7), f (m, b + 8), f (m, b + 9) };
			float lpos[3] = { f (m, b + 10), f (m, b + 11), f (m, b + 12) }, ldir[3] = { f (m, b + 13), f (m, b + 14), f (m, b + 15) };
			float ld[3] = { lpos[0] - pos[0], lpos[1] - pos[1], lpos[2] - pos[2] }, at = 1.0f;
			if (atn == 3)
			{
				float d2 = dot3 (ld, ld), d = __builtin_sqrtf (d2);
				if (d > 0) { ld[0] /= d; ld[1] /= d; ld[2] /= d; }
				float cs = dot3 (ld, ldir); if (cs < 0) cs = 0;
				float den = distA[0] + distA[1] * d + distA[2] * d2;
				float num = cosA[0] + cosA[1] * cs + cosA[2] * cs * cs; if (num < 0) num = 0;
				at = den != 0 ? num / den : 0;
			}
			else if (atn == 1)
			{
				if (dot3 (ld, ld) > 0) norm3 (ld); else { ld[0] = n[0]; ld[1] = n[1]; ld[2] = n[2]; }
				float cs = 0;
				if (dot3 (n, ld) >= 0) { cs = dot3 (n, ldir); if (cs < 0) cs = 0; }
				float da[3] = { distA[0], distA[1], distA[2] };
				if (!(dfn == 0 || dot3 (distA, distA) == 0)) norm3 (da);
				float den = da[0] + da[1] * cs + da[2] * cs * cs;
				float num = cosA[0] + cosA[1] * cs + cosA[2] * cs * cs; if (num < 0) num = 0;
				at = den != 0 ? num / den : 0;
			}
			else { if (dot3 (ld, ld) > 0) norm3 (ld); else { ld[0] = n[0]; ld[1] = n[1]; ld[2] = n[2]; } }
			float df = dfn == 0 ? 1.0f : dfn == 1 ? dot3 (ld, n) : (dot3 (ld, n) > 0 ? dot3 (ld, n) : 0);
			for (int c = 0; c < 4; c++) { float x = at * df * lcol[c]; lacc[c] += __builtin_roundf (x); }
		}
		for (int c = 0; c < 4; c++)
		{
			float l = lacc[c] < 0 ? 0 : lacc[c] > 255 ? 255 : lacc[c];
			out[c] = __builtin_floorf (mat[c] * (l + __builtin_floorf (l / 128.0f)) / 256.0f);
		}
	}

	// ---- a draw
	void draw (gc::Machine &m, const gc::GxState &s, const gc::GxVertex *vx, int nv, const u32 *idx, int ni, int prim) override
	{
		if (prim != gc::GX_TRIANGLES || ni < 3) return;			// (lines, points: not yet)
		Frame &F = frame[build];
		// the TEV's configuration
		gxtev::Config c; __builtin_memset (&c, 0, sizeof c);
		c.nStages = s.gen[0] < 1 ? 1 : s.gen[0] > 16 ? 16 : s.gen[0];
		for (int st = 0; st < c.nStages; st++) { c.cenv[st] = (u32) s.tevC[st]; c.aenv[st] = (u32) s.tevA[st]; c.tref[st] = (u32) s.tref[st]; c.ksel[st] = (u32) s.ksel[st]; }
		for (int k = 0; k < 16; k++) c.swap[k] = (u8) (s.swap[k] & 3);
		c.alphaFunc[0] = (u8) (s.alpha[0] & 7); c.alphaFunc[1] = (u8) (s.alpha[1] & 7); c.alphaLogic = (u8) (s.gen[2] & 3);
		int fmt = (int) (s.peCtrl & 7);
		c.efbFmt = (u8) (fmt <= 2 ? fmt : 0);
		// blending / masks (gxgl.cpp): the source alpha is the TEV's, so the destination alpha
		// replaces it only when the blending does not read it
		u32 cm = s.cmode0;
		bool hasAlpha = fmt == 1;
		u32 blend = 0;
		static const u8 SRC[8] = { 0, 1, 4, 5, 6, 7, 8, 9 }, DST[8] = { 0, 1, 2, 3, 6, 7, 8, 9 };
		bool readsSrcA = false;
		if (cm & 0x800) blend = KAPI_GPU_BLEND2 (1, 1, 1, 1, 2, 2);		// (subtract: dst - src)
		else if (cm & 1)
		{
			u32 bs = SRC[(cm >> 8) & 7], bd = DST[(cm >> 5) & 7];
			if (!hasAlpha)
			{
				if (bs == 8) bs = 1; else if (bs == 9) bs = 0;
				if (bd == 8) bd = 1; else if (bd == 9) bd = 0;
			}
			readsSrcA = bs == 6 || bs == 7 || bd == 6 || bd == 7;
			blend = KAPI_GPU_BLEND2 (bs, bd, bs, bd, 0, 0);
		}
		c.dstAlpha = (s.zenv[3] & 0x100) && !readsSrcA;
		for (int i = 0; i < 8; i++) if (s.tcScale[i][2] != 0) c.projMask |= (u8) (1 << i);
		bool unsup = false;
		for (int st = 0; st < c.nStages; st++) if (s.ind[st]) unsup = true;
		if (s.fogI[0] || s.zenv[0]) unsup = true;
		if (unsup) unsupported++;
		int pi = findProg (c);
		if (pi < 0) { skipped++; return; }
		const Prog &P = prog[pi];
		// its textures
		Batch b;
		for (int k = 0; k < 8; k++) { b.texSlot[k] = -1; b.texFlags[k] = 0; }
		for (int L = 0; L < P.nLook; L++)
		{
			int mp = P.look[L].map, t = s.tex[mp];
			if (t < 0 || t >= gc::GX_TEX_COPY) { skipped++; return; }		// (an EFB copy: not yet)
			b.texSlot[L] = t;
			u32 tm0 = s.texMode[mp][0];
			static const u32 WRAP[4] = { 1, 0, 2, 0 };			// GX clamp, repeat, mirror -> ours
			b.texFlags[L] = KAPI_GPU_B_WRAP_S (WRAP[tm0 & 3]) | KAPI_GPU_B_WRAP_T (WRAP[(tm0 >> 2) & 3]) | ((tm0 & 0x10) ? KAPI_GPU_B_LINEAR : 0);
		}
		// the EFB's rectangle the XFB copy takes: the frame
		u32 src = m.bpRegs[0x49], sz = m.bpRegs[0x4A];
		float cx = (float) (src & 0x3FF), cy = (float) ((src >> 10) & 0x3FF);
		float cw = (float) ((sz & 0x3FF) + 1), ch = (float) (((sz >> 10) & 0x3FF) + 1);
		if (cw < 16) { cx = 0; cw = 640; }
		if (ch < 16) { cy = 0; ch = 480; }
		// the batch's state
		b.prog = pi; b.stride = 4 + P.nVary;
		u32 zm = s.zmode, flags = 0;
		if (!(zm & 1)) flags |= KAPI_GPU_B_ZFUNC (7) | KAPI_GPU_B_NOZWRITE;
		else
		{
			u32 zf = (zm >> 1) & 7;
			if (zf == 0) return;						// (never)
			flags |= KAPI_GPU_B_ZFUNC (zf);
			if (!(zm & 0x10)) flags |= KAPI_GPU_B_NOZWRITE;
		}
		// (GX's front: clockwise on the screen; the kernel's: counter-clockwise)
		if (s.cull == 1) flags |= KAPI_GPU_B_CULL_FRONT;		// (GX: the back culled)
		else if (s.cull == 2) flags |= KAPI_GPU_B_CULL_BACK;
		else if (s.cull == 3) return;
		b.flags = flags; b.blend = blend;
		b.wmask = ((cm >> 3) & 1 ? 0 : 7) | (hasAlpha && ((cm >> 4) & 1) ? 0 : 8);
		if (b.wmask == 15 && !(zm & 0x10)) return;			// (nothing written)
		b.scissor[0] = ((float) s.scissor[0] - cx) / cw; b.scissor[1] = ((float) s.scissor[1] - cy) / ch;
		b.scissor[2] = ((float) s.scissor[2] - cx) / cw; b.scissor[3] = ((float) s.scissor[3] - cy) / ch;
		// its uniforms
		if (F.nu + P.nUni > MAX_UNIS || F.nb >= MAX_BATCHES) { skipped++; return; }
		b.uni = F.nu;
		for (u32 i = 0; i < P.nUni; i++)
		{
			const gxtev::Uni &u = uniArena[P.uni + i];
			u32 v = 0;
			switch (u.kind)
			{
			case gxtev::U_CONST: v = u.value; break;
			case gxtev::U_REG: v = (u32) s.regs[u.a * 4 + u.b]; break;
			case gxtev::U_KONST: v = (u32) s.konst[u.a * 4 + u.b]; break;
			case gxtev::U_ALPHAREF: v = (u32) s.alpha[2 + u.a]; break;
			case gxtev::U_DSTALPHA: { float a = (float) (s.zenv[3] & 255) / 255.0f; __builtin_memcpy (&v, &a, 4); } break;
			default: break;						// (the TMU words: the kernel's)
			}
			F.u[F.nu++] = v;
		}
		// the vertices
		int stride = b.stride;
		if (nv > 0x10000) nv = 0x10000;
		const float *vpf = s.viewport;
		float vx0 = vpf[0], vy0 = vpf[1], vw = vpf[2], vh = vpf[3], d0 = vpf[4], d1 = vpf[5];
		float ax = (2 * vx0 - 2 * cx) / cw - 1, bx = vw / cw;		// x' = ax W + bx (X + W)
		float ay = 1 - (2 * vy0 - 2 * cy) / ch, by = vh / ch;		// y' = ay W - by (Y + W)
		float az = 2 * d0 - 1, bz = d1 - d0;				// z' = az W + bz (Z + W)
		bool hasN = (s.vtx[0] & gc::GX_VCD_NRM) != 0, h0 = (s.vtx[0] & gc::GX_VCD_C0) != 0, h1 = (s.vtx[0] & gc::GX_VCD_C1) != 0;
		float m0[4], m1[4], a0[4], a1[4];
		rgba (s.matAmb[0], m0); rgba (s.matAmb[1], m1); rgba (s.matAmb[2], a0); rgba (s.matAmb[3], a1);
		bool proj = s.proj[6] != 0;
		int tMax = -1;							// (the texgens up to the last one a lookup reads:
		for (int L = 0; L < P.nLook; L++) if (P.look[L].coord > tMax) tMax = P.look[L].coord;	// an emboss one's source is earlier)
		if (tMax >= s.vtx[2]) tMax = s.vtx[2] - 1;
		for (int i = 0; i < nv; i++)
		{
			const gc::GxVertex &v = vx[i];
			float *o = tmp + (size_t) i * (size_t) stride;
			u32 pm = v.mtx[0];
			float p4[4] = { v.pos[0], v.pos[1], v.pos[2], 1 };
			float eye[3];
			for (int r = 0; r < 3; r++) { u32 a = ((pm + (u32) r) & 63) * 4; eye[r] = f (m, a) * p4[0] + f (m, a + 1) * p4[1] + f (m, a + 2) * p4[2] + f (m, a + 3); }
			float n[3] = { 0, 0, 0 };
			if (hasN)
			{
				u32 nb = (pm & 31) * 3;
				for (int r = 0; r < 3; r++)
				{
					u32 w = nb + (u32) r * 3;
					n[r] = f (m, 0x400 + (w % 96)) * v.nrm[0] + f (m, 0x400 + ((w + 1) % 96)) * v.nrm[1] + f (m, 0x400 + ((w + 2) % 96)) * v.nrm[2];
				}
				norm3 (n);
			}
			float X, Y, Z, W;
			if (proj) { X = s.proj[0] * eye[0] + s.proj[1]; Y = s.proj[2] * eye[1] + s.proj[3]; Z = s.proj[4] * eye[2] + s.proj[5]; W = 1; }
			else { X = s.proj[0] * eye[0] + s.proj[1] * eye[2]; Y = s.proj[2] * eye[1] + s.proj[3] * eye[2]; Z = s.proj[4] * eye[2] + s.proj[5]; W = -eye[2]; }
			X *= s.vp[0]; Y *= s.vp[1]; Z = 2 * Z + W;
			o[0] = ax * W + bx * (X + W); o[1] = ay * W - by * (Y + W); o[2] = az * W + bz * (Z + W); o[3] = W;
			// the colour channels
			float raw0[4], raw1[4], v0[4], v1[4], col0[4], col1[4], t4[4];
			for (int k = 0; k < 4; k++) { raw0[k] = v.c0[k]; raw1[k] = v.c1[k]; v0[k] = h0 ? raw0[k] : 255; }
			for (int k = 0; k < 4; k++) v1[k] = h1 ? raw1[k] : v0[k];
			channel (m, s.chan[0], m0, a0, v0, eye, n, col0);
			channel (m, s.chan[2], m0, a0, v0, eye, n, t4); col0[3] = t4[3];
			channel (m, s.chan[1], m1, a1, v1, eye, n, col1);
			channel (m, s.chan[3], m1, a1, v1, eye, n, t4); col1[3] = t4[3];
			if (s.vtx[1] == 0) for (int k = 0; k < 4; k++) col0[k] = h0 ? raw0[k] : 255;
			if (s.vtx[1] < 2) for (int k = 0; k < 4; k++) col1[k] = h1 ? raw1[k] : col0[k];
			// the texture coordinates of the lookups
			float tg[8][3];
			for (int t = 0; t < 8; t++) { tg[t][0] = tg[t][1] = 0; tg[t][2] = 1; }
			for (int t = 0; t <= tMax; t++)
			{
				u32 info = (u32) s.texgen[t][0];
				int srcSel = (int) (info >> 7) & 31, type = (int) (info >> 4) & 7;
				float in[4] = { 0, 0, 1, 1 };
				if (srcSel == 0) { in[0] = v.pos[0]; in[1] = v.pos[1]; in[2] = v.pos[2]; }
				else if (srcSel == 1) { if (hasN) { in[0] = v.nrm[0]; in[1] = v.nrm[1]; in[2] = v.nrm[2]; } else in[2] = 0; }
				else if (srcSel >= 5 && srcSel <= 12) { in[0] = v.tc[srcSel - 5][0]; in[1] = v.tc[srcSel - 5][1]; }
				if (!(info & 4)) in[2] = 1;
				if (type == 0)
				{
					u32 r = v.mtx[1 + t];
					for (int k = 0; k < 2; k++) { u32 a = ((r + (u32) k) & 63) * 4; tg[t][k] = f (m, a) * in[0] + f (m, a + 1) * in[1] + f (m, a + 2) * in[2] + f (m, a + 3) * in[3]; }
					if (info & 2) { u32 a = ((r + 2) & 63) * 4; tg[t][2] = f (m, a) * in[0] + f (m, a + 1) * in[1] + f (m, a + 2) * in[2] + f (m, a + 3) * in[3]; }
					else tg[t][2] = 1;
					if (s.vtx[3])
					{
						u32 pinfo = (u32) s.texgen[t][1], pr = pinfo & 63;
						float q[3] = { tg[t][0], tg[t][1], tg[t][2] };
						if ((pinfo & 256) && dot3 (q, q) > 0) norm3 (q);
						for (int k = 0; k < 3; k++) { u32 a = 0x500 + ((pr + (u32) k) & 63) * 4; tg[t][k] = f (m, a) * q[0] + f (m, a + 1) * q[1] + f (m, a + 2) * q[2] + f (m, a + 3); }
					}
				}
				else if (type == 1) { int sr = (int) (info >> 12) & 7; tg[t][0] = tg[sr][0]; tg[t][1] = tg[sr][1]; tg[t][2] = tg[sr][2]; }
				else { const float *cc = type == 2 ? col0 : col1; tg[t][0] = cc[0] / 255.0f; tg[t][1] = cc[1] / 255.0f; tg[t][2] = 1; }
			}
			// the varyings, in the program's order
			for (int k = 0; k < P.nVary; k++)
			{
				const gxtev::Vary &vy = P.vary[k];
				float x;
				if (vy.kind == gxtev::V_C0) x = col0[vy.a] / 255.0f;
				else if (vy.kind == gxtev::V_C1) x = col1[vy.a] / 255.0f;
				else
				{
					const gxtev::Lookup &lk = P.look[vy.a];
					const float *t = tg[lk.coord];
					if (vy.b == 2) x = t[2];
					else { float ts = s.texSize[lk.map][vy.b]; x = ts > 0 ? t[vy.b] * s.tcScale[lk.coord][vy.b] / ts : 0; }
				}
				o[4 + k] = x;
			}
		}
		// its triangles
		u32 need = (u32) ni * (u32) stride;
		if (F.nf + need > MAX_FLOATS) { skipped++; F.nu = b.uni; return; }
		b.off = F.nf; b.first = F.nv; b.count = (u32) (ni / 3 * 3);
		float *d = F.v + F.nf;
		for (int i = 0; i < (int) b.count; i++)
		{
			u32 k = idx[i]; if (k >= (u32) nv) k = 0;
			const float *sv = tmp + (size_t) k * (size_t) stride;
			for (int j = 0; j < stride; j++) d[j] = sv[j];
			d += stride;
		}
		F.nf += b.count * (u32) stride; F.nv += b.count;
		// (the same program and state right after: one batch)
		if (F.nb)
		{
			Batch &l = F.b[F.nb - 1];
			if (l.prog == b.prog && l.flags == b.flags && l.blend == b.blend && l.wmask == b.wmask && l.off + l.count * (u32) l.stride == b.off
			    && !__builtin_memcmp (l.scissor, b.scissor, sizeof b.scissor) && !__builtin_memcmp (l.texSlot, b.texSlot, sizeof b.texSlot)
			    && !__builtin_memcmp (l.texFlags, b.texFlags, sizeof b.texFlags) && sameUni (F, l.uni, b.uni, P.nUni))
			{ l.count += b.count; F.nu = b.uni; return; }
		}
		F.b[F.nb++] = b;
	}
	static bool sameUni (const Frame &F, u32 a, u32 b, u32 n) { for (u32 i = 0; i < n; i++) if (F.u[a + i] != F.u[b + i]) return false; return true; }

	// ---- an EFB copy: to the XFB, the frame is done
	void copy (gc::Machine &m, const gc::GxCopy &c) override
	{
		(void) m;
		Frame &F = frame[build];
		if (c.toXfb)
		{
			__sync_synchronize ();
			ready = build; serial++;
			build ^= 1;
			Frame &N = frame[build];
			N.reset ();
			N.keep = !c.clear;
			N.clear = c.clear ? c.clearColor & 0xFFFFFF : F.clear;
			return;
		}
		if (c.clear) { F.reset (); F.keep = false; F.clear = c.clearColor & 0xFFFFFF; }	// (drawn into a texture: dropped)
	}
};

#ifndef GXV3D_HOST		// (tools/tests/gc/gcv3d.cpp: the recording alone, drawn by its software V3D)
// ---- the main thread: a finished frame into pixels ----------------------------------------------------------------
struct Out
{
	int gpuTex[gc::Machine::MAX_TEX];
	float *rv = 0; u32 rvCap = 0;			// (the vertices at one stride)
	unsigned *ru = 0;
	kapi_gpu_batch2 *rb = 0;
	qpu::Prog vs, cs;
	bool init ()
	{
		for (int i = 0; i < gc::Machine::MAX_TEX; i++) gpuTex[i] = -1;
		rvCap = MAX_FLOATS * 2; rv = new float[rvCap]; ru = new unsigned[MAX_UNIS + 8]; rb = new kapi_gpu_batch2[MAX_BATCHES];
		qpu::passCS (cs);
		return rv && ru && rb;
	}
	// the textures decoded since (Machine::tex[]: dirty)
	void textures (gc::Machine &m)
	{
		for (int i = 0; i < gc::Machine::MAX_TEX; i++)
		{
			gc::GTexture &T = m.tex[i];
			if (!T.dirty) continue;
			int w = T.w, h = T.h;
			if (w <= 0 || h <= 0 || !T.px || w * h > T.cap) continue;
			int h2 = kapi_gpu_texture (gpuTex[i], T.px, w, h, w);
			if (h2 < 0 && gpuTex[i] >= 0) h2 = kapi_gpu_texture (-1, T.px, w, h, w);
			gpuTex[i] = h2;
			T.dirty = false;
		}
	}
	int program (Rec &r, int pi)
	{
		Prog &p = r.prog[pi];
		if (p.handle != -1) return p.handle;
		int nIn = 4 + p.nVary;
		vs.n = 0; vs.bad = false; qpu::passVS (vs, nIn);
		struct kapi_gpu_program P;
		P.vs = vs.words (); P.nvs = (unsigned) vs.count (); P.cs = cs.words (); P.ncs = (unsigned) cs.count ();
		P.fs = r.arena + p.words; P.nfs = p.nWords;
		P.inputs = (unsigned) nIn; P.csInputs = 4; P.csOutputs = 6; P.varyings = (unsigned) p.nVary; P.flags = p.flags;
		int h = kapi_gpu_program (-1, &P);
		p.handle = h >= 0 ? h : -2;
		return p.handle;
	}
	// -> false: no frame (or it failed)
	bool render (Rec &r, gc::Machine &m, unsigned *px, int w, int h, int stride)
	{
		int fi = r.ready;
		if (fi < 0) return false;
		const Frame &F = r.frame[fi];
		textures (m);
		int maxStride = 4;
		for (u32 i = 0; i < F.nb; i++) if (F.b[i].stride > maxStride) maxStride = F.b[i].stride;
		unsigned view[4]; qpu::viewUniforms (w, h, view);
		for (int k = 0; k < 4; k++) ru[k] = view[k];
		for (u32 i = 0; i < F.nu; i++) ru[4 + i] = F.u[i];
		u32 nv = 0, nb = 0;
		for (u32 i = 0; i < F.nb; i++)
		{
			const Batch &b = F.b[i];
			const Prog &p = r.prog[b.prog];
			int hnd = program (r, b.prog);
			if (hnd < 0) continue;
			if (nv + b.count > KAPI_GPU_MAX_VERTS / 2 || (nv + b.count) * (u32) maxStride > rvCap) break;
			kapi_gpu_batch2 &o = rb[nb];
			__builtin_memset (&o, 0, sizeof o);
			o.first = nv; o.count = b.count; o.program = hnd; o.flags = b.flags; o.blend = b.blend; o.wmask = b.wmask;
			int x0 = (int) (b.scissor[0] * (float) w + 0.5f), y0 = (int) (b.scissor[1] * (float) h + 0.5f);
			int x1 = (int) (b.scissor[2] * (float) w + 0.5f), y1 = (int) (b.scissor[3] * (float) h + 0.5f);
			if (x0 < 0) x0 = 0;
			if (y0 < 0) y0 = 0;
			if (x1 > w) x1 = w;
			if (y1 > h) y1 = h;
			if (x1 <= x0 || y1 <= y0) continue;
			o.scissor[0] = x0; o.scissor[1] = y0; o.scissor[2] = x1 - x0; o.scissor[3] = y1 - y0;
			o.vsUni = 0; o.vsNUni = 4; o.csUni = 0; o.csNUni = 2;
			o.fsUni = 4 + b.uni; o.fsNUni = p.nUni;
			bool ok = true;
			for (int k = 0; k < 8; k++) { o.tex[k] = -1; o.texUni[k] = -1; }
			for (u32 k = 0; k < p.nUni; k++)
			{
				const gxtev::Uni &u = r.uniArena[p.uni + k];
				if (u.kind != gxtev::U_TEXP0) continue;
				int t = b.texSlot[u.a] >= 0 ? gpuTex[b.texSlot[u.a]] : -1;
				if (t < 0) { ok = false; break; }
				o.tex[u.a] = t; o.texUni[u.a] = (int) k; o.texFlags[u.a] = b.texFlags[u.a];
			}
			if (!ok) continue;
			// the vertices at the frame's stride
			const float *sv = F.v + b.off;
			float *d = rv + (size_t) nv * (size_t) maxStride;
			for (u32 k = 0; k < b.count; k++)
			{
				for (int j = 0; j < b.stride; j++) d[j] = sv[j];
				sv += b.stride; d += maxStride;
			}
			nv += b.count; nb++;
		}
		struct kapi_gpu_frame fr = { px, w, h, stride, F.clear, F.keep ? KAPI_GPU_F_KEEP : 0u };
		return kapi_gpu_render2 (&fr, rv, nv, (unsigned) maxStride, rb, nb, ru, 4 + F.nu) == 0;
	}
};

#endif // GXV3D_HOST

} // namespace gxv3d
#endif
