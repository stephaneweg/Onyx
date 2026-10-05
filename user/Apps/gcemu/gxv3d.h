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
#include <kern/v3d_clip.h>

namespace gxv3d
{
using gc::u8; using gc::u32; using gc::s32; using gc::u64;

// the ARM's clock (no kapi call: the app core may read it), for F12's times; its rate: clockRate ()
static inline u64 clockTicks ()
{
#if defined (__aarch64__)
	u64 t; asm volatile ("mrs %0, cntvct_el0" : "=r" (t)); return t;
#else
	return 0;
#endif
}
static inline u64 clockRate ()
{
#if defined (__aarch64__)
	u64 f; asm volatile ("mrs %0, cntfrq_el0" : "=r" (f)); return f ? f : 1;
#else
	return 1;
#endif
}

enum { MAX_PROGS = 1024, ARENA_WORDS = 1 << 20, ARENA_UNI = 1 << 18 };
enum { MAX_FLOATS = 3 << 20, MAX_BATCHES = 4096, MAX_UNIS = 1 << 18, FAN_ROOM = 1 << 18 };

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

// The vertices are recorded in the clip space of the whole EFB (EFB_W x EFB_H: x -1..1 = its columns
// 0..640, y 1..-1 its rows 0..528), the scissors in its pixels: the rectangle the frame's XFB copy
// takes is known at its end only (a copy to a texture meanwhile has its own) -- frameView maps them
// onto it when the frame is drawn
enum { EFB_W = 640, EFB_H = 528 };

struct Batch
{
	u32 first, count;			// (vertices: floats from `off`, `stride` a vertex)
	u32 off; int stride;
	int prog;
	u32 flags, blend, wmask;
	float scissor[4];			// x0 y0 x1 y1 in the EFB's pixels
	u32 uni;				// its fragment uniforms in the frame's
	int texSlot[8]; u32 texFlags[8];	// (Machine::tex[] slots, the lookups')
};

struct Frame
{
	float *v; u32 nf, nv;
	Batch *b; u32 nb;
	u32 *u; u32 nu;
	u32 clear; bool keep;
	float rect[4];				// the EFB's rectangle the XFB copy took: x, y, w, h
	bool inVbuf, framed;			// (gcemu: v in the GPU's reach -- drawn in place; its x / y framed there already)
	void reset () { nf = nv = nb = nu = 0; rect[0] = rect[1] = 0; rect[2] = EFB_W; rect[3] = 480; framed = false; }
};

// A frame's EFB clip space -> its picture's: x' = t[0] x + t[1] w, y' = t[2] y + t[3] w
static inline void frameView (const Frame &F, float t[4])
{
	float cx = F.rect[0], cy = F.rect[1], cw = F.rect[2], ch = F.rect[3];
	t[0] = (float) EFB_W / cw; t[1] = ((float) EFB_W - 2 * cx) / cw - 1;
	t[2] = (float) EFB_H / ch; t[3] = 1 - ((float) EFB_H - 2 * cy) / ch;
}
// a batch's scissor -> 0..1 over the frame's picture (x0 y0 x1 y1)
static inline void frameScissor (const Frame &F, const Batch &b, float o[4])
{
	for (int k = 0; k < 4; k++) o[k] = (b.scissor[k] - F.rect[k & 1]) / F.rect[2 + (k & 1)];
}

class Rec : public gc::GxGpu
{
public:
	Prog prog[MAX_PROGS]; volatile int nProg = 0;
	unsigned long long *arena = 0; u32 arenaN = 0;	// the programs' words
	gxtev::Uni *uniArena = 0; u32 uniN = 0;
	// three frames: the one built, the last one finished (ready), the one the main thread draws
	// (held -- the kernel reads its vertices where they are: not built into meanwhile)
	Frame frame[3]; int build = 0; volatile int ready = -1, held = -1;
	u32 serial = 0;					// (frames finished)
	u32 unsupported = 0, skipped = 0;		// (stats: draws with a feature not generated, not drawn)
	// (stats) the draws left out, by reason: SK_*; onSkip (the PC's tests): told each one
	enum { SK_PRIM, SK_PROG, SK_NOTEX, SK_COPYTEX, SK_ZNEVER, SK_CULLALL, SK_NOWRITE, SK_LIMIT, SK_N };
	u32 skipWhy[SK_N] = {};
	void (*onSkip) (int why, const gc::GxState &s, int nv, int arg) = 0;
	void skip (int why, const gc::GxState &s, int nv, int arg, bool count = true)
	{
		skipWhy[why]++;
		if (count) skipped++;
		if (onSkip) onSkip (why, s, nv, arg);
	}
	volatile u64 drawTicks = 0, drawVerts = 0;	// (stats: the time in draw (), the vertices it made)
	float *tmp = 0;					// (a draw's vertices before its triangles)
	u8 *oc = 0;					// (theirs: outside the kernel's near plane 1, behind the eye 2)
	volatile u64 behind = 0;			// (stats: the triangles left out, all behind one of those planes)

	bool init ()
	{
		arena = new unsigned long long[ARENA_WORDS]; uniArena = new gxtev::Uni[ARENA_UNI];
		tmp = new float[0x10000 * 68]; oc = new u8[0x10000];
		for (int i = 0; i < 256; i++) inv255[i] = (float) i / 255.0f;
		pl.valid = false;
		for (int i = 0; i < 3; i++)
		{
#ifndef GXV3D_HOST
			// (kapi v63: in the GPU's reach, drawn where it is -- room past the vertices for the
			// triangles the kernel clips; none left: our memory, the kernel's copy)
			frame[i].v = (float *) kapi_gpu_vbuf ((MAX_FLOATS + FAN_ROOM) * 4);
			frame[i].inVbuf = frame[i].v != 0;
			if (!frame[i].v) frame[i].v = new float[MAX_FLOATS];
#else
			frame[i].v = new float[MAX_FLOATS]; frame[i].inVbuf = false;
#endif
			frame[i].b = new Batch[MAX_BATCHES]; frame[i].u = new u32[MAX_UNIS];
			if (!frame[i].v || !frame[i].b || !frame[i].u) return false;
			frame[i].reset (); frame[i].clear = 0; frame[i].keep = false;
		}
		return arena && uniArena && tmp && oc;
	}

	// ---- the program of a configuration (the app core: the shader's generation, cached)
	gxtev::Config lastCfg; int lastProg = -1;		// (the last draw's configuration, its program)
	int findProg (const gxtev::Config &c)
	{
		if (lastProg >= 0 && !__builtin_memcmp (&lastCfg, &c, sizeof c)) return lastProg;
		int pi = findProg2 (c);
		if (pi >= 0) { lastCfg = c; lastProg = pi; }
		return pi;
	}
	int findProg2 (const gxtev::Config &c)
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
	// the lights (XF 0x600..), read once a draw: colour, angle / distance attenuations (the latter
	// normalized too, for the specular with a diffuse function), position, direction
	struct Light { float col[4], cosA[3], distA[3], daN[3], pos[3], dir[3]; };
	Light lt[8];
	static int lightMask (int ctl) { return (ctl & 2) ? (((ctl >> 2) & 15) | ((ctl >> 7) & 0xF0)) : 0; }
	void readLights (const gc::Machine &m, int mask)
	{
		for (int L = 0; L < 8; L++)
		{
			if (!(mask & (1 << L))) continue;
			u32 b = 0x600 + (u32) L * 16;
			Light &l = lt[L];
			rgba (m.xfRegs[b + 3], l.col);
			for (int k = 0; k < 3; k++) { l.cosA[k] = f (m, b + 4 + k); l.distA[k] = f (m, b + 7 + k); l.pos[k] = f (m, b + 10 + k); l.dir[k] = f (m, b + 13 + k); }
			for (int k = 0; k < 3; k++) l.daN[k] = l.distA[k];
			if (dot3 (l.distA, l.distA) != 0) norm3 (l.daN);
		}
	}
	// a colour channel's colour or alpha (0..255), its components c0..c1 - 1: the material x (the
	// ambient + the lights)
	void channel (int ctl, const float regMat[4], const float regAmb[4], const float vcol[4],
		      const float pos[3], const float n[3], float out[4], int c0, int c1) const
	{
		const float *mat = (ctl & 1) ? vcol : regMat;
		if (!(ctl & 2)) { for (int c = c0; c < c1; c++) out[c] = mat[c]; return; }
		float lacc[4];
		for (int c = c0; c < c1; c++) lacc[c] = (ctl & 64) ? vcol[c] : regAmb[c];
		int mask = lightMask (ctl);
		int dfn = (ctl >> 7) & 3, atn = (ctl >> 9) & 3;
		for (int L = 0; L < 8; L++)
		{
			if (!(mask & (1 << L))) continue;
			const Light &li = lt[L];
			const float *lcol = li.col, *cosA = li.cosA, *distA = li.distA, *lpos = li.pos, *ldir = li.dir;
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
				const float *da = dfn == 0 ? distA : li.daN;
				float den = da[0] + da[1] * cs + da[2] * cs * cs;
				float num = cosA[0] + cosA[1] * cs + cosA[2] * cs * cs; if (num < 0) num = 0;
				at = den != 0 ? num / den : 0;
			}
			else { if (dot3 (ld, ld) > 0) norm3 (ld); else { ld[0] = n[0]; ld[1] = n[1]; ld[2] = n[2]; } }
			float df = dfn == 0 ? 1.0f : dfn == 1 ? dot3 (ld, n) : (dot3 (ld, n) > 0 ? dot3 (ld, n) : 0);
			for (int c = c0; c < c1; c++) { float x = at * df * lcol[c]; lacc[c] += __builtin_roundf (x); }
		}
		for (int c = c0; c < c1; c++)
		{
			float l = lacc[c] < 0 ? 0 : lacc[c] > 255 ? 255 : lacc[c];
			out[c] = __builtin_floorf (mat[c] * (l + __builtin_floorf (l / 128.0f)) / 256.0f);
		}
	}

	// a lit channel's plan (the draw's): its control's parts, the lights it sums (in their order)
	struct ChanPlan { int ctl, dfn, atn, nL; bool matV, ambV; Light L[8]; };
	void chanPlan (ChanPlan &c, int ctl) const
	{
		c.ctl = ctl; c.dfn = (ctl >> 7) & 3; c.atn = (ctl >> 9) & 3; c.matV = (ctl & 1) != 0; c.ambV = (ctl & 64) != 0;
		c.nL = 0;
		int mask = lightMask (ctl);
		for (int L = 0; L < 8; L++) if (mask & (1 << L)) c.L[c.nL++] = lt[L];
	}
	// channel () through a plan (the same arithmetic: lit only -- (ctl & 2) set)
	static void chanEval (const ChanPlan &c, const float regMat[4], const float regAmb[4], const float vcol[4],
			      const float pos[3], const float n[3], float out[4], int c0, int c1)
	{
		const float *mat = c.matV ? vcol : regMat;
		float lacc[4];
		for (int k = c0; k < c1; k++) lacc[k] = c.ambV ? vcol[k] : regAmb[k];
		int dfn = c.dfn, atn = c.atn;
		for (int j = 0; j < c.nL; j++)
		{
			const Light &li = c.L[j];
			const float *lcol = li.col, *cosA = li.cosA, *distA = li.distA, *lpos = li.pos, *ldir = li.dir;
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
				const float *da = dfn == 0 ? distA : li.daN;
				float den = da[0] + da[1] * cs + da[2] * cs * cs;
				float num = cosA[0] + cosA[1] * cs + cosA[2] * cs * cs; if (num < 0) num = 0;
				at = den != 0 ? num / den : 0;
			}
			else { if (dot3 (ld, ld) > 0) norm3 (ld); else { ld[0] = n[0]; ld[1] = n[1]; ld[2] = n[2]; } }
			float df = dfn == 0 ? 1.0f : dfn == 1 ? dot3 (ld, n) : (dot3 (ld, n) > 0 ? dot3 (ld, n) : 0);
			for (int k = c0; k < c1; k++) { float x = at * df * lcol[k]; lacc[k] += __builtin_roundf (x); }
		}
		for (int k = c0; k < c1; k++)
		{
			float l = lacc[k] < 0 ? 0 : lacc[k] > 255 ? 255 : lacc[k];
			out[k] = __builtin_floorf (mat[k] * (l + __builtin_floorf (l / 128.0f)) / 256.0f);
		}
	}

	// ---- a draw (its time counted)
	void draw (gc::Machine &m, const gc::GxState &s, const gc::GxVertex *vx, int nv, const u32 *idx, int ni, int prim) override
	{
		u64 t0 = clockTicks ();
		record (m, s, vx, nv, idx, ni, prim);
		drawTicks = drawTicks + (clockTicks () - t0);
	}
	// ---- a draw's plan: what record () makes of the GX state (its serial) and of the XF memory (the
	// matrices, the lights: xfSerial) -- the last draw's for ~90 % of The Wind Waker's (4.6 vertices
	// each): made again only when one of them changed (a machine reset: both go on); the matrices of
	// the vertices' indices kept across the draws while the XF memory is the same. The same
	// arithmetic as before: the recordings bit for bit the same (GCV3D_VHASH).
	struct TgPlan { u32 info, pr; int srcSel, type, sr; bool three, norm; float post[12]; };
	struct VyPlan { int kind, a, coord; float scale, ts, inv; };	// (kind 0 / 1: a colour; 2: a coordinate's q; 3: s / t -- inv = 1 / ts: a power of two)
	enum { PLAN_UNIS = 512 };
	struct Plan
	{
		bool valid; u32 serial, xfSerial;
		int why, whyArg; bool whyCount, unsup;		// (the state's draws left out: SK_*, -1 none)
		int pi; Batch b; u32 u[PLAN_UNIS];		// (the batch but its vertices; its uniforms, when they fit)
		float ax, bx, ay, by, az, bz, m0[4], m1[4], a0[4], a1[4], P0, P1, P2, P3, P4, P5, VP0, VP1;
		bool hasN, h0, h1, proj, lit0, lit1, lit0c, lit1c, lit0L, lit0aL, lit1L, lit1aL, a0Own, a1Own, needN, dual;
		int tMax, c0End, c1End, lmask, vtx1, nVary;
		ChanPlan cp0, cp0a, cp1, cp1a;
		TgPlan tp[8]; VyPlan vp[64];
		float Mp[12], Mn[9], Mt[8][12]; u32 lastPm, lastTm[8];	// (the matrices of the last indices: the XF memory's)
		u32 lastSerial, lastNb;				// (the batch it made last: its frame (serial), its index)
	};
	Plan pl;
	float inv255[256];				// (i / 255.0f: a colour's varying -- the colours are integers 0..255)
	static u32 uniValue (const gc::GxState &s, const gxtev::Uni &u)
	{
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
		return v;
	}
	// the state's part (its serial): the TEV's configuration and program, the textures, the batch and
	// its uniforms, the vertex stage's plan; pl.why: the draws left out
	void planState (gc::Machine &m, const gc::GxState &s)
	{
		(void) m;
		pl.valid = true; pl.serial = s.serial; pl.why = -1; pl.whyArg = 0; pl.whyCount = true; pl.lastNb = ~0u;
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
		pl.unsup = unsup;
		int pi = findProg (c);
		if (pi < 0) { pl.why = SK_PROG; return; }
		pl.pi = pi;
		const Prog &P = prog[pi];
		// its textures
		Batch &b = pl.b;
		for (int k = 0; k < 8; k++) { b.texSlot[k] = -1; b.texFlags[k] = 0; }
		for (int L = 0; L < P.nLook; L++)
		{
			int mp = P.look[L].map, t = s.tex[mp];
			if (t < 0 || t >= gc::GX_TEX_COPY) { pl.why = t < 0 ? SK_NOTEX : SK_COPYTEX; pl.whyArg = mp; return; }	// (an EFB copy: not yet)
			b.texSlot[L] = t;
			u32 tm0 = s.texMode[mp][0];
			static const u32 WRAP[4] = { 1, 0, 2, 0 };			// GX clamp, repeat, mirror -> ours
			b.texFlags[L] = KAPI_GPU_B_WRAP_S (WRAP[tm0 & 3]) | KAPI_GPU_B_WRAP_T (WRAP[(tm0 >> 2) & 3]) | ((tm0 & 0x10) ? KAPI_GPU_B_LINEAR : 0);
		}
		// the space the vertices are recorded in: the whole EFB (frameView: onto the XFB copy's rectangle)
		const float cx = 0, cy = 0, cw = EFB_W, ch = EFB_H;
		// the batch's state
		b.prog = pi; b.stride = 4 + P.nVary;
		u32 zm = s.zmode, flags = 0;
		if (!(zm & 1)) flags |= KAPI_GPU_B_ZFUNC (7) | KAPI_GPU_B_NOZWRITE;
		else
		{
			u32 zf = (zm >> 1) & 7;
			if (zf == 0) { pl.why = SK_ZNEVER; pl.whyCount = false; return; }	// (never)
			flags |= KAPI_GPU_B_ZFUNC (zf);
			if (!(zm & 0x10)) flags |= KAPI_GPU_B_NOZWRITE;
		}
		// (GX's front: clockwise on the screen; the kernel's: counter-clockwise)
		if (s.cull == 1) flags |= KAPI_GPU_B_CULL_FRONT;		// (GX: the back culled)
		else if (s.cull == 2) flags |= KAPI_GPU_B_CULL_BACK;
		else if (s.cull == 3) { pl.why = SK_CULLALL; pl.whyCount = false; return; }
		b.flags = flags; b.blend = blend;
		b.wmask = ((cm >> 3) & 1 ? 0 : 7) | (hasAlpha && ((cm >> 4) & 1) ? 0 : 8);
		if (b.wmask == 15 && !(zm & 0x10)) { pl.why = SK_NOWRITE; pl.whyCount = false; return; }	// (nothing written)
		for (int k = 0; k < 4; k++) b.scissor[k] = (float) s.scissor[k];
		// its uniforms
		for (u32 i = 0; i < P.nUni && i < PLAN_UNIS; i++) pl.u[i] = uniValue (s, uniArena[P.uni + i]);
		// the vertex stage
		const float *vpf = s.viewport;
		float vx0 = vpf[0], vy0 = vpf[1], vw = vpf[2], vh = vpf[3], d0 = vpf[4], d1 = vpf[5];
		pl.ax = (2 * vx0 - 2 * cx) / cw - 1; pl.bx = vw / cw;		// x' = ax W + bx (X + W)
		pl.ay = 1 - (2 * vy0 - 2 * cy) / ch; pl.by = vh / ch;		// y' = ay W - by (Y + W)
		pl.az = 2 * d0 - 1; pl.bz = d1 - d0;				// z' = az W + bz (Z + W)
		bool hasN = (s.vtx[0] & gc::GX_VCD_NRM) != 0, h1 = (s.vtx[0] & gc::GX_VCD_C1) != 0;
		pl.hasN = hasN; pl.h0 = (s.vtx[0] & gc::GX_VCD_C0) != 0; pl.h1 = h1;
		rgba (s.matAmb[0], pl.m0); rgba (s.matAmb[1], pl.m1); rgba (s.matAmb[2], pl.a0); rgba (s.matAmb[3], pl.a1);
		pl.proj = s.proj[6] != 0;
		int tMax = -1;							// (the texgens up to the last one a lookup reads:
		for (int L = 0; L < P.nLook; L++) if (P.look[L].coord > tMax) tMax = P.look[L].coord;	// an emboss one's source is earlier)
		if (tMax >= s.vtx[2]) tMax = s.vtx[2] - 1;
		pl.tMax = tMax;
		// the colour channels the program reads (its varyings, a texgen from a colour), lit only
		// then: COLOR0 / 1's RGB, ALPHA0 / 1 (from COLOR's lighting when its control is the same);
		// the normal only for a lit channel
		bool u0 = false, u0a = false, u1 = false, u1a = false;
		for (int k = 0; k < P.nVary; k++)
		{
			const gxtev::Vary &vy = P.vary[k];
			if (vy.kind == gxtev::V_C0) { if (vy.a < 3) u0 = true; else u0a = true; }
			else if (vy.kind == gxtev::V_C1) { if (vy.a < 3) u1 = true; else u1a = true; }
		}
		for (int t = 0; t <= tMax; t++)
		{
			int type = (int) ((u32) s.texgen[t][0] >> 4) & 7;
			if (type == 2) u0 = true; else if (type == 3) u1 = true;
		}
		if (s.vtx[1] < 2 && !h1) { u0 = u0 || u1; u0a = u0a || u1a; }	// (COLOR1 from COLOR0)
		bool lit0 = s.vtx[1] != 0, lit1 = s.vtx[1] >= 2;		// (else the vertex colours as they are)
		bool same0 = s.chan[2] == s.chan[0], same1 = s.chan[3] == s.chan[1];
		int c0End = u0 ? (u0a && same0 ? 4 : 3) : 0, c1End = u1 ? (u1a && same1 ? 4 : 3) : 0;
		bool a0Own = lit0 && u0a && !(u0 && same0), a1Own = lit1 && u1a && !(u1 && same1);
		pl.lmask = (lit0 && u0 ? lightMask (s.chan[0]) : 0) | (a0Own ? lightMask (s.chan[2]) : 0)
			 | (lit1 && u1 ? lightMask (s.chan[1]) : 0) | (a1Own ? lightMask (s.chan[3]) : 0);
		pl.needN = hasN && pl.lmask != 0;
		pl.lit0 = lit0; pl.lit1 = lit1; pl.c0End = c0End; pl.c1End = c1End; pl.a0Own = a0Own; pl.a1Own = a1Own;
		pl.lit0c = lit0 && c0End; pl.lit1c = lit1 && c1End;
		pl.lit0L = pl.lit0c && (s.chan[0] & 2); pl.lit0aL = lit0 && a0Own && (s.chan[2] & 2);
		pl.lit1L = pl.lit1c && (s.chan[1] & 2); pl.lit1aL = lit1 && a1Own && (s.chan[3] & 2);
		pl.P0 = s.proj[0]; pl.P1 = s.proj[1]; pl.P2 = s.proj[2]; pl.P3 = s.proj[3]; pl.P4 = s.proj[4]; pl.P5 = s.proj[5];
		pl.VP0 = s.vp[0]; pl.VP1 = s.vp[1];
		pl.vtx1 = s.vtx[1]; pl.dual = s.vtx[3] != 0;
		// the texgens' plans (their post-transform matrices: planXf) and the varyings'
		for (int t = 0; t <= tMax; t++)
		{
			TgPlan &q = pl.tp[t];
			q.info = (u32) s.texgen[t][0]; q.srcSel = (int) (q.info >> 7) & 31; q.type = (int) (q.info >> 4) & 7;
			q.sr = (int) (q.info >> 12) & 7; q.three = (q.info & 2) != 0;
			u32 pinfo = (u32) s.texgen[t][1];
			q.pr = pinfo & 63; q.norm = (pinfo & 256) != 0;
		}
		pl.nVary = P.nVary;
		for (int k = 0; k < P.nVary; k++)
		{
			const gxtev::Vary &vy = P.vary[k];
			VyPlan &q = pl.vp[k];
			if (vy.kind == gxtev::V_C0) { q.kind = 0; q.a = vy.a; }
			else if (vy.kind == gxtev::V_C1) { q.kind = 1; q.a = vy.a; }
			else
			{
				const gxtev::Lookup &lk = P.look[vy.a];
				q.coord = lk.coord; q.a = vy.b;
				if (vy.b == 2) q.kind = 2;
				else
				{
					q.kind = 3; q.ts = s.texSize[lk.map][vy.b]; q.scale = s.tcScale[lk.coord][vy.b];
					u32 bits; __builtin_memcpy (&bits, &q.ts, 4);			// (a power of two -- no mantissa, normal:
					q.inv = q.ts > 0 && !(bits & 0x7FFFFF) && (bits >> 23) ? 1.0f / q.ts : 0;	// * 1 / ts is / ts)
				}
			}
		}
	}
	// the XF memory's part: the lit channels' lights, the texgens' post-transform matrices; the
	// matrices of the indices forgotten
	void planXf (gc::Machine &m, const gc::GxState &s)
	{
		pl.xfSerial = m.xfSerial;
		readLights (m, pl.lmask);
		if (pl.lit0c) chanPlan (pl.cp0, s.chan[0]);
		if (pl.lit0 && pl.a0Own) chanPlan (pl.cp0a, s.chan[2]);
		if (pl.lit1c) chanPlan (pl.cp1, s.chan[1]);
		if (pl.lit1 && pl.a1Own) chanPlan (pl.cp1a, s.chan[3]);
		for (int t = 0; t <= pl.tMax; t++)
		{
			TgPlan &q = pl.tp[t];
			for (int k = 0; k < 3; k++) for (int c = 0; c < 4; c++) q.post[k * 4 + c] = f (m, 0x500 + ((q.pr + (u32) k) & 63) * 4 + (u32) c);
		}
		pl.lastPm = ~0u;
		for (int t = 0; t < 8; t++) pl.lastTm[t] = ~0u;
	}

	void record (gc::Machine &m, const gc::GxState &s, const gc::GxVertex *vx, int nv, const u32 *idx, int ni, int prim)
	{
		if (prim != gc::GX_TRIANGLES || ni < 3) { skip (SK_PRIM, s, nv, prim, false); return; }	// (lines, points: not yet)
		Frame &F = frame[build];
		bool fresh = !pl.valid || pl.serial != s.serial;
		if (fresh) planState (m, s);
		if (pl.unsup) unsupported++;
		if (pl.why >= 0) { skip (pl.why, s, nv, pl.whyArg, pl.whyCount); return; }
		if (fresh || pl.xfSerial != m.xfSerial) planXf (m, s);
		const Prog &P = prog[pl.pi];
		// its batch: the last one again when this plan made it right before (its vertices then follow
		// on), else a new one -- merged below with the last when they match, as before
		if (F.nu + P.nUni > MAX_UNIS || F.nb >= MAX_BATCHES) { skip (SK_LIMIT, s, nv, 0); return; }
		bool again = !fresh && pl.lastSerial == serial && F.nb && pl.lastNb == F.nb - 1
			  && F.b[F.nb - 1].off + F.b[F.nb - 1].count * (u32) F.b[F.nb - 1].stride == F.nf;
		Batch b = pl.b;
		b.uni = F.nu;
		if (!again)
		{
			if (P.nUni <= PLAN_UNIS) for (u32 i = 0; i < P.nUni; i++) F.u[F.nu++] = pl.u[i];
			else for (u32 i = 0; i < P.nUni; i++) F.u[F.nu++] = uniValue (s, uniArena[P.uni + i]);
		}
		// the vertices (the plan's constants in locals: the loop's stores to tmp do not make them read again)
		int stride = b.stride;
		if (nv > 0x10000) nv = 0x10000;
		const float ax = pl.ax, bx = pl.bx, ay = pl.ay, by = pl.by, az = pl.az, bz = pl.bz;
		const bool hasN = pl.hasN, h0 = pl.h0, h1 = pl.h1, proj = pl.proj;
		float m0[4], m1[4], a0[4], a1[4];
		for (int k = 0; k < 4; k++) { m0[k] = pl.m0[k]; m1[k] = pl.m1[k]; a0[k] = pl.a0[k]; a1[k] = pl.a1[k]; }
		const int tMax = pl.tMax, c0End = pl.c0End, c1End = pl.c1End;
		const bool lit0 = pl.lit0, lit1 = pl.lit1, a0Own = pl.a0Own, a1Own = pl.a1Own, needN = pl.needN;
		const bool lit0c = pl.lit0c, lit1c = pl.lit1c, lit0L = pl.lit0L, lit0aL = pl.lit0aL, lit1L = pl.lit1L, lit1aL = pl.lit1aL;
		const ChanPlan &cp0 = pl.cp0, &cp0a = pl.cp0a, &cp1 = pl.cp1, &cp1a = pl.cp1a;
		const float P0 = pl.P0, P1 = pl.P1, P2 = pl.P2, P3 = pl.P3, P4 = pl.P4, P5 = pl.P5;
		const float VP0 = pl.VP0, VP1 = pl.VP1;
		const int vtx1 = pl.vtx1; const bool dual = pl.dual;
		const TgPlan *tp = pl.tp; const VyPlan *vp = pl.vp;
		const int nVary = pl.nVary;
		float Mp[12], Mn[9]; u32 lastPm = pl.lastPm;			// (the position / normal matrices of the last index)
		if (lastPm != ~0u) { for (int k = 0; k < 12; k++) Mp[k] = pl.Mp[k]; for (int k = 0; k < 9; k++) Mn[k] = pl.Mn[k]; }
		float Mt[8][12]; u32 lastTm[8];					// (each texgen's of its last index)
		for (int t = 0; t < 8; t++)
		{
			lastTm[t] = pl.lastTm[t];
			if (t <= tMax && lastTm[t] != ~0u) for (int k = 0; k < 12; k++) Mt[t][k] = pl.Mt[t][k];
		}
		float tg[8][3];
		for (int t = 0; t < 8; t++) { tg[t][0] = tg[t][1] = 0; tg[t][2] = 1; }	// (the ones above tMax: so)
		for (int i = 0; i < nv; i++)
		{
			const gc::GxVertex &v = vx[i];
			float *o = tmp + (size_t) i * (size_t) stride;
			u32 pm = v.mtx[0];
			if (pm != lastPm)
			{
				lastPm = pm;
				for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) Mp[r * 4 + c] = f (m, ((pm + (u32) r) & 63) * 4 + (u32) c);
				u32 nb = (pm & 31) * 3;
				for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) Mn[r * 3 + c] = f (m, 0x400 + ((nb + (u32) r * 3 + (u32) c) % 96));
			}
			float p4[4] = { v.pos[0], v.pos[1], v.pos[2], 1 };
			float eye[3];
			for (int r = 0; r < 3; r++) eye[r] = Mp[r * 4] * p4[0] + Mp[r * 4 + 1] * p4[1] + Mp[r * 4 + 2] * p4[2] + Mp[r * 4 + 3];
			float n[3] = { 0, 0, 0 };
			if (needN)
			{
				for (int r = 0; r < 3; r++) n[r] = Mn[r * 3] * v.nrm[0] + Mn[r * 3 + 1] * v.nrm[1] + Mn[r * 3 + 2] * v.nrm[2];
				norm3 (n);
			}
			float X, Y, Z, W;
			if (proj) { X = P0 * eye[0] + P1; Y = P2 * eye[1] + P3; Z = P4 * eye[2] + P5; W = 1; }
			else { X = P0 * eye[0] + P1 * eye[2]; Y = P2 * eye[1] + P3 * eye[2]; Z = P4 * eye[2] + P5; W = -eye[2]; }
			X *= VP0; Y *= VP1; Z = 2 * Z + W;
			o[0] = ax * W + bx * (X + W); o[1] = ay * W - by * (Y + W); o[2] = az * W + bz * (Z + W); o[3] = W;
			oc[i] = (u8) ((V3DClipDistN (o, 0) >= 0 ? 0 : 1) | (V3DClipDistN (o, 1) >= 0 ? 0 : 2));	// (NaN: outside)
			// the colour channels
			float raw0[4], raw1[4], v0[4], v1[4], col0[4] = { 0, 0, 0, 0 }, col1[4] = { 0, 0, 0, 0 };
			for (int k = 0; k < 4; k++) { raw0[k] = v.c0[k]; raw1[k] = v.c1[k]; v0[k] = h0 ? raw0[k] : 255; }
			for (int k = 0; k < 4; k++) v1[k] = h1 ? raw1[k] : v0[k];
			if (lit0c) { if (lit0L) chanEval (cp0, m0, a0, v0, eye, n, col0, 0, c0End); else for (int k = 0; k < c0End; k++) col0[k] = cp0.matV ? v0[k] : m0[k]; }
			if (lit0 && a0Own) { if (lit0aL) chanEval (cp0a, m0, a0, v0, eye, n, col0, 3, 4); else col0[3] = cp0a.matV ? v0[3] : m0[3]; }
			if (lit1c) { if (lit1L) chanEval (cp1, m1, a1, v1, eye, n, col1, 0, c1End); else for (int k = 0; k < c1End; k++) col1[k] = cp1.matV ? v1[k] : m1[k]; }
			if (lit1 && a1Own) { if (lit1aL) chanEval (cp1a, m1, a1, v1, eye, n, col1, 3, 4); else col1[3] = cp1a.matV ? v1[3] : m1[3]; }
			if (vtx1 == 0) for (int k = 0; k < 4; k++) col0[k] = h0 ? raw0[k] : 255;
			if (vtx1 < 2) for (int k = 0; k < 4; k++) col1[k] = h1 ? raw1[k] : col0[k];
			// the texture coordinates of the lookups
			for (int t = 0; t <= tMax; t++)
			{
				const TgPlan &q = tp[t];
				float in[4] = { 0, 0, 1, 1 };
				if (q.srcSel == 0) { in[0] = v.pos[0]; in[1] = v.pos[1]; in[2] = v.pos[2]; }
				else if (q.srcSel == 1) { if (hasN) { in[0] = v.nrm[0]; in[1] = v.nrm[1]; in[2] = v.nrm[2]; } else in[2] = 0; }
				else if (q.srcSel >= 5 && q.srcSel <= 12) { in[0] = v.tc[q.srcSel - 5][0]; in[1] = v.tc[q.srcSel - 5][1]; }
				if (!(q.info & 4)) in[2] = 1;
				if (q.type == 0)
				{
					u32 r = v.mtx[1 + t];
					float *T = Mt[t];
					if (r != lastTm[t])
					{
						lastTm[t] = r;
						for (int k = 0; k < 3; k++) for (int c = 0; c < 4; c++) T[k * 4 + c] = f (m, ((r + (u32) k) & 63) * 4 + (u32) c);
					}
					for (int k = 0; k < 2; k++) tg[t][k] = T[k * 4] * in[0] + T[k * 4 + 1] * in[1] + T[k * 4 + 2] * in[2] + T[k * 4 + 3] * in[3];
					if (q.three) tg[t][2] = T[8] * in[0] + T[9] * in[1] + T[10] * in[2] + T[11] * in[3];
					else tg[t][2] = 1;
					if (dual)
					{
						float qv[3] = { tg[t][0], tg[t][1], tg[t][2] };
						if (q.norm && dot3 (qv, qv) > 0) norm3 (qv);
						for (int k = 0; k < 3; k++) tg[t][k] = q.post[k * 4] * qv[0] + q.post[k * 4 + 1] * qv[1] + q.post[k * 4 + 2] * qv[2] + q.post[k * 4 + 3];
					}
				}
				else if (q.type == 1)				// (emboss: from an earlier texgen -- a later one or itself: not made yet, 0 0 1)
				{
					if (q.sr >= t) { tg[t][0] = 0; tg[t][1] = 0; tg[t][2] = 1; }
					else { tg[t][0] = tg[q.sr][0]; tg[t][1] = tg[q.sr][1]; tg[t][2] = tg[q.sr][2]; }
				}
				else { const float *cc = q.type == 2 ? col0 : col1; tg[t][0] = cc[0] / 255.0f; tg[t][1] = cc[1] / 255.0f; tg[t][2] = 1; }
			}
			// the varyings, in the program's order
			for (int k = 0; k < nVary; k++)
			{
				const VyPlan &q = vp[k];
				float x;
				if (q.kind == 0) x = inv255[(int) col0[q.a]];		// (the colours: integers 0..255)
				else if (q.kind == 1) x = inv255[(int) col1[q.a]];
				else if (q.kind == 2) x = tg[q.coord][2];
				else x = q.ts > 0 ? (q.inv > 0 ? tg[q.coord][q.a] * q.scale * q.inv : tg[q.coord][q.a] * q.scale / q.ts) : 0;
				o[4 + k] = x;
			}
		}
		drawVerts = drawVerts + (u64) nv;
		pl.lastPm = lastPm;
		if (lastPm != ~0u) { for (int k = 0; k < 12; k++) pl.Mp[k] = Mp[k]; for (int k = 0; k < 9; k++) pl.Mn[k] = Mn[k]; }
		for (int t = 0; t <= tMax; t++)
		{
			pl.lastTm[t] = lastTm[t];
			if (lastTm[t] != ~0u) for (int k = 0; k < 12; k++) pl.Mt[t][k] = Mt[t][k];
		}
		// its triangles
		u32 need = (u32) ni * (u32) stride;
		if (F.nf + need > MAX_FLOATS) { skip (SK_LIMIT, s, nv, 1); F.nu = b.uni; return; }
		// (a triangle whose three vertices are before the near plane, or behind the eye, is left out:
		// the kernel's clipping would drop it -- its first two planes, the same expressions -- after
		// the copies here, in Out::prepare and in the kernel)
		b.off = F.nf; b.first = F.nv;
		u32 nt = (u32) ni / 3, kept = 0;
		float *d = F.v + F.nf;
		for (u32 t = 0; t < nt; t++)
		{
			u32 k0 = idx[t * 3], k1 = idx[t * 3 + 1], k2 = idx[t * 3 + 2];
			if (k0 >= (u32) nv) k0 = 0;
			if (k1 >= (u32) nv) k1 = 0;
			if (k2 >= (u32) nv) k2 = 0;
			if (oc[k0] & oc[k1] & oc[k2]) continue;
			const u32 k[3] = { k0, k1, k2 };
			for (int c = 0; c < 3; c++)
			{
				const float *sv = tmp + (size_t) k[c] * (size_t) stride;
				for (int j = 0; j < stride; j++) d[j] = sv[j];
				d += stride;
			}
			kept++;
		}
		behind = behind + (nt - kept);
		b.count = kept * 3;
		if (!b.count) { F.nu = b.uni; return; }				// (all of it behind the eye)
		F.nf += b.count * (u32) stride; F.nv += b.count;
		if (again) { F.b[F.nb - 1].count += b.count; return; }	// (the plan's batch: on)
		// (the same program and state right after: one batch)
		if (F.nb)
		{
			Batch &l = F.b[F.nb - 1];
			if (l.prog == b.prog && l.flags == b.flags && l.blend == b.blend && l.wmask == b.wmask && l.off + l.count * (u32) l.stride == b.off
			    && !__builtin_memcmp (l.scissor, b.scissor, sizeof b.scissor) && !__builtin_memcmp (l.texSlot, b.texSlot, sizeof b.texSlot)
			    && !__builtin_memcmp (l.texFlags, b.texFlags, sizeof b.texFlags) && sameUni (F, l.uni, b.uni, P.nUni))
			{ l.count += b.count; F.nu = b.uni; pl.lastSerial = serial; pl.lastNb = F.nb - 1; return; }
		}
		F.b[F.nb++] = b; pl.lastSerial = serial; pl.lastNb = F.nb - 1;
	}
	static bool sameUni (const Frame &F, u32 a, u32 b, u32 n) { for (u32 i = 0; i < n; i++) if (F.u[a + i] != F.u[b + i]) return false; return true; }

	// ---- an EFB copy: to the XFB, the frame is done
	void copy (gc::Machine &m, const gc::GxCopy &c) override
	{
		Frame &F = frame[build];
		if (c.toXfb)
		{
			m.gxLock ();					// (a reader taking the ready one: after it)
			F.rect[0] = (float) c.x; F.rect[1] = (float) c.y; F.rect[2] = (float) c.w; F.rect[3] = (float) c.h;
			if (c.w < 16) { F.rect[0] = 0; F.rect[2] = EFB_W; }
			if (c.h < 16) { F.rect[1] = 0; F.rect[3] = 480; }
			__sync_synchronize ();
			ready = build; serial++;
			int nb = 0; while (nb == ready || nb == held) nb++;	// (neither the ready one nor the drawn one)
			build = nb;
			Frame &N = frame[build];
			N.reset ();
			N.keep = !c.clear;
			N.clear = c.clear ? c.clearColor & 0xFFFFFF : F.clear;
			m.gxUnlock ();
			return;
		}
		if (c.clear) { F.reset (); F.keep = false; F.clear = c.clearColor & 0xFFFFFF; }	// (drawn into a texture: dropped)
	}
};

// ---- a frame dumped (gcemu: F9, --diag): what the recorder made -- the frame's vertices, batches and
// uniforms, the programs and the textures its batches use -- and the picture the GPU made of it, for
// tools/tests/gc/gcv3d.cpp --replay (the same frame through the PC's software V3D, to compare)
struct DumpHead
{
	char magic[4]; u32 version, sizeBatch, sizeProg;
	u32 nf, nv, nb, nu, clear, keep, nProgs, nTex;
	int pw, ph, ret; u32 zero;			// (the picture's size, gpu_render2's result)
	float rect[4];					// (version 2) the frame's XFB copy rectangle
};
// its size; written into d when d != 0 (the machine not running: the frame and textures still)
static inline unsigned long long dumpFrame (const Rec &r, const gc::Machine &m, const Frame &F,
					   const unsigned *pic, int pw, int ph, int picStride, int ret, u8 *d)
{
	unsigned long long n = 0;
	auto put = [&] (const void *p, unsigned long long k) { if (d) __builtin_memcpy (d + n, p, k); n += k; };
	static u8 progUsed[MAX_PROGS], texUsed[gc::Machine::MAX_TEX];
	__builtin_memset (progUsed, 0, sizeof progUsed); __builtin_memset (texUsed, 0, sizeof texUsed);
	u32 nProgs = 0, nTex = 0;
	for (u32 i = 0; i < F.nb; i++)
	{
		const Batch &b = F.b[i];
		if (b.prog < 0 || b.prog >= MAX_PROGS) continue;
		if (!progUsed[b.prog]) { progUsed[b.prog] = 1; nProgs++; }
		for (int L = 0; L < r.prog[b.prog].nLook; L++)
		{
			int t = b.texSlot[L];
			if (t >= 0 && t < gc::Machine::MAX_TEX && !texUsed[t] && m.tex[t].px && m.tex[t].w > 0) { texUsed[t] = 1; nTex++; }
		}
	}
	DumpHead H; __builtin_memset (&H, 0, sizeof H);
	H.magic[0] = 'G'; H.magic[1] = 'X'; H.magic[2] = 'F'; H.magic[3] = '1';
	H.version = 2; H.sizeBatch = sizeof (Batch); H.sizeProg = sizeof (Prog);
	for (int k = 0; k < 4; k++) H.rect[k] = F.rect[k];
	if (F.framed) { H.rect[0] = H.rect[1] = 0; H.rect[2] = EFB_W; H.rect[3] = EFB_H; }	// (its vertices framed in place already)
	H.nf = F.nf; H.nv = F.nv; H.nb = F.nb; H.nu = F.nu; H.clear = F.clear; H.keep = F.keep; H.nProgs = nProgs; H.nTex = nTex;
	H.pw = pic ? pw : 0; H.ph = pic ? ph : 0; H.ret = ret;
	put (&H, sizeof H);
	put (F.v, (unsigned long long) F.nf * 4); put (F.b, (unsigned long long) F.nb * sizeof (Batch)); put (F.u, (unsigned long long) F.nu * 4);
	for (u32 i = 0; i < MAX_PROGS; i++)
	{
		if (!progUsed[i]) continue;
		const Prog &p = r.prog[i];
		put (&i, 4); put (&p, sizeof p);
		put (r.arena + p.words, (unsigned long long) p.nWords * 8); put (r.uniArena + p.uni, (unsigned long long) p.nUni * sizeof (gxtev::Uni));
	}
	for (u32 i = 0; i < (u32) gc::Machine::MAX_TEX; i++)
	{
		if (!texUsed[i]) continue;
		const gc::GTexture &T = m.tex[i];
		u32 w3[3] = { i, (u32) T.w, (u32) T.h };
		put (w3, sizeof w3); put (T.px, (unsigned long long) T.w * (unsigned long long) T.h * 4);
	}
	if (pic) for (int y = 0; y < ph; y++) put (pic + (long) y * picStride, (unsigned long long) pw * 4);
	return n;
}

#ifndef GXV3D_HOST		// (tools/tests/gc/gcv3d.cpp: the recording alone, drawn by its software V3D)
// ---- the main thread: a finished frame into pixels ----------------------------------------------------------------
struct Out
{
	int gpuTex[gc::Machine::MAX_TEX];
	float *rv = 0; u32 rvCap = 0;			// (an older kernel: the vertices at one stride)
	unsigned *ru = 0;
	kapi_gpu_batch2 *rb = 0;
	kapi_gpu_batch3 *rb3 = 0;			// (gpu_render3's: the frame's vertices where they are)
	bool v62 = false;
	qpu::Prog vs, cs;
	// what the last frame gave (F12, --diag): gpu_render2's result (0 ok, -1 no GPU, -2 bad
	// arguments, -3 the GPU did not finish, -4 no memory; 1 no frame yet), the batches recorded,
	// given to the GPU, left out (their program refused, a texture missing, nothing visible, over
	// the vertex limit); the programs and textures the kernel refused so far, the last error; the
	// time (clock ticks, summed) making the kernel's arrays and in gpu_render2, the vertices given
	struct Stats { int ret; u32 recorded, drawn, noProg, noTex, empty, limit, frames, progFails, texFails; int progErr, texErr;
		       u64 prepTicks, gpuTicks, gpuVerts; };
	Stats st;
	bool init ()
	{
		for (int i = 0; i < gc::Machine::MAX_TEX; i++) gpuTex[i] = -1;
		v62 = kapi_abi_version () >= 62;
		rvCap = v62 ? 0 : MAX_FLOATS * 2; rv = v62 ? 0 : new float[rvCap];
		ru = new unsigned[MAX_UNIS + 8]; rb = new kapi_gpu_batch2[MAX_BATCHES]; rb3 = new kapi_gpu_batch3[MAX_BATCHES];
		qpu::passCS (cs);
		__builtin_memset (&st, 0, sizeof st); st.ret = 1;
		return (rv || v62) && ru && rb && rb3;
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
			if (h2 < 0) { st.texFails++; st.texErr = h2; }
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
		if (h < 0) { st.progFails++; st.progErr = h; }
		p.handle = h >= 0 ? h : -2;
		return p.handle;
	}
	// the frame prepared (prepare: the kernel's arrays rv / rb / ru), and the one the last submit drew
	bool prepOk = false; int prepFrame = -1, prepW = 0, prepH = 0, prepStride = 4; u32 prepSerial = 0;
	u32 prepNv = 0, prepNb = 0, prepNu = 0, prepClear = 0; bool prepKeep = false;
	const float *prepV = 0; u32 prepNf = 0; float prepView[4];	// (gpu_render3: the held frame's vertices, their framing)
	Frame *prepF = 0;
	bool shownOk = false; const unsigned *shownPx = 0; int shownW = 0, shownH = 0, shownStride = 0; u32 shownSerial = 0;

	// The machine waiting: its last finished frame into the kernel's arrays for a w x h target (the
	// new programs and textures given to the kernel first) -- nothing to do when that frame is
	// already prepared. -> false: no frame.
	bool prepare (Rec &r, gc::Machine &m, int w, int h)
	{
		m.gxLock ();					// (the GX's core: no flip, no texture made meanwhile)
		bool ok = prepareLocked (r, m, w, h);
		m.gxUnlock ();
		return ok;
	}
	bool prepareLocked (Rec &r, gc::Machine &m, int w, int h)
	{
		int fi = r.ready;
		if (fi < 0) { prepOk = false; return false; }
		if (prepOk && fi == prepFrame && r.serial == prepSerial && w == prepW && h == prepH) return true;
		u64 t0 = clockTicks ();
		r.held = fi;					// (the GX builds into the other two meanwhile)
		const Frame &F = r.frame[fi];
		textures (m);
		int maxStride = 4;
		for (u32 i = 0; i < F.nb; i++) if (F.b[i].stride > maxStride) maxStride = F.b[i].stride;
		unsigned vu[4]; qpu::viewUniforms (w, h, vu);
		for (int k = 0; k < 4; k++) ru[k] = vu[k];
		float view[4]; frameView (F, view);
		for (u32 i = 0; i < F.nu; i++) ru[4 + i] = F.u[i];
		u32 nv = 0, nb = 0;
		st.recorded = F.nb; st.noProg = st.noTex = st.empty = st.limit = 0;
		for (u32 i = 0; i < F.nb; i++)
		{
			const Batch &b = F.b[i];
			const Prog &p = r.prog[b.prog];
			int hnd = program (r, b.prog);
			if (hnd < 0) { st.noProg++; continue; }
			// (the kernel's clipping adds vertices only for the triangles across the near plane: an
			// eighth of its limit left for them -- what goes beyond, the kernel drops, as here)
			if (nv + b.count > KAPI_GPU_MAX_VERTS - KAPI_GPU_MAX_VERTS / 8 || (!v62 && (nv + b.count) * (u32) maxStride > rvCap)) { st.limit += F.nb - i; break; }
			kapi_gpu_batch2 &o = rb[nb];
			__builtin_memset (&o, 0, sizeof o);
			o.first = nv; o.count = b.count; o.program = hnd; o.flags = b.flags; o.blend = b.blend; o.wmask = b.wmask;
			float sc[4]; frameScissor (F, b, sc);
			int x0 = (int) (sc[0] * (float) w + 0.5f), y0 = (int) (sc[1] * (float) h + 0.5f);
			int x1 = (int) (sc[2] * (float) w + 0.5f), y1 = (int) (sc[3] * (float) h + 0.5f);
			if (x0 < 0) x0 = 0;
			if (y0 < 0) y0 = 0;
			if (x1 > w) x1 = w;
			if (y1 > h) y1 = h;
			if (x1 <= x0 || y1 <= y0) { st.empty++; continue; }
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
			if (!ok) { st.noTex++; continue; }
			if (v62)				// (the kernel reads them where they are, framed on the way)
			{
				kapi_gpu_batch3 &o3 = rb3[nb];
				o3.b = o; o3.b.first = 0; o3.off = b.off; o3.stride = (unsigned) b.stride;
				nv += b.count; nb++;
				continue;
			}
			// the vertices at the frame's stride, onto the XFB copy's rectangle
			const float *sv = F.v + b.off;
			float *d = rv + (size_t) nv * (size_t) maxStride;
			for (u32 k = 0; k < b.count; k++)
			{
				d[0] = view[0] * sv[0] + view[1] * sv[3]; d[1] = view[2] * sv[1] + view[3] * sv[3];
				for (int j = 2; j < b.stride; j++) d[j] = sv[j];
				sv += b.stride; d += maxStride;
			}
			nv += b.count; nb++;
		}
		prepNv = nv; prepNb = nb; prepNu = 4 + F.nu; prepStride = maxStride; prepClear = F.clear; prepKeep = F.keep;
		prepV = F.v; prepNf = F.nf; for (int k = 0; k < 4; k++) prepView[k] = view[k];
		prepF = &r.frame[fi];
		prepFrame = fi; prepSerial = r.serial; prepW = w; prepH = h; prepOk = true;
		st.drawn = nb;
		st.prepTicks += clockTicks () - t0;
		return true;
	}
	bool prepared (int w, int h) const { return prepOk && w == prepW && h == prepH; }
	// Any time (the arrays are ours, the kernel copies them): the prepared frame drawn into px --
	// not again when px already has it. -> false: gpu_render2 failed.
	bool submit (unsigned *px, int stride)
	{
		if (shownOk && px == shownPx && stride == shownStride && prepW == shownW && prepH == shownH && prepSerial == shownSerial)
			return true;
		struct kapi_gpu_frame fr = { px, prepW, prepH, stride, prepClear, prepKeep ? KAPI_GPU_F_KEEP : 0u };
		st.frames++; st.gpuVerts += prepNv;
		u64 t1 = clockTicks ();
		// (a frame in the GPU's reach: framed in place by the first call -- then as it is)
		bool inPlace = v62 && prepF && prepF->inVbuf;
		st.ret = v62 ? kapi_gpu_render3 (&fr, prepV, prepNf, rb3, prepNb, ru, prepNu, inPlace && prepF->framed ? 0 : prepView)
			     : kapi_gpu_render2 (&fr, rv, prepNv, (unsigned) prepStride, rb, prepNb, ru, prepNu);
		if (inPlace && st.ret != -1 && st.ret != -2) prepF->framed = true;
		st.gpuTicks += clockTicks () - t1;
		shownOk = st.ret == 0; shownPx = px; shownStride = stride; shownW = prepW; shownH = prepH; shownSerial = prepSerial;
		return st.ret == 0;
	}
};

#endif // GXV3D_HOST

} // namespace gxv3d
#endif
