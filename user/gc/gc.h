//
// gc/gc.h -- the Nintendo GameCube core of Onyx (gcemu): the Gekko CPU (a PowerPC 750CL at
// 486 MHz with the paired-single unit and the quantized loads / stores), the memory (24 MB of
// MEM1, the locked cache, the BAT address translation), then the chips around it (Flipper:
// the command processor and the GX graphics, the video interface, the DSP and its audio, the
// DVD drive, the serial ports for the pads, the EXI bus for the memory cards and the IPL).
//
//   * gc_cpu.cpp  the Gekko: an interpreter (every PowerPC instruction the games use, the
//                 floating point in the host's IEEE doubles as the 750's, the paired singles,
//                 the exceptions, the timebase and the decrementer)
//   * gc_mem.cpp  the address translation (BAT) and the physical map
//   * gc_hw.cpp   the chips' registers: PI (the interrupts), VI (the picture: the lines, the
//                 display interrupts, the framebuffer in YUV 4:2:2 -> RGB), SI (the pads),
//                 EXI (the RTC / SRAM, no memory card yet), DI (the DVD drive, reading an
//                 ISO / GCM image), AI / DSP (the mailboxes, the audio and ARAM DMAs), MI
//   * gc_jit.cpp  the Gekko's JIT (AArch64 hosts): its code translated to native code a block
//                 at a time, the interpreter for what it does not translate
//   * gc_boot.cpp starting a program: a .dol, or a disc image through its apploader (run by
//                 the CPU, as the IPL does), with the memory the IPL leaves
//
// Memory is kept in the console's byte order (big-endian): a 32-bit read byte-swaps.
//
#ifndef GC_GC_H
#define GC_GC_H

namespace gc {

typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
typedef signed char s8; typedef short s16; typedef int s32; typedef long long s64; typedef float f32;

enum { MEM1_SIZE = 24 * 1024 * 1024, LCACHE_SIZE = 16 * 1024 };
enum { CPU_HZ = 486000000, BUS_HZ = 162000000, TB_HZ = BUS_HZ / 4, CYC_PER_TB = CPU_HZ / TB_HZ };

// The host's writable and executable memory, for the JIT's code (Onyx: kapi_code_alloc) -> 0 none.
extern void *(*codeAlloc) (u32 size);
struct Jit;

static inline u32 bswap32 (u32 v) { return __builtin_bswap32 (v); }
static inline u16 bswap16 (u16 v) { return (u16) __builtin_bswap16 (v); }

// a single's bits -> the double the 750 loads (exactly, NaNs and denormals kept)
static inline u64 cvtToDouble (u32 v)
{
	u64 x = v, e = (x >> 23) & 0xFF, frac = x & 0x007FFFFF;
	if (e > 0 && e < 255)
	{
		u64 y = !(e >> 7), z = y << 61 | y << 60 | y << 59;
		return ((x & 0xC0000000) << 32) | z | ((x & 0x3FFFFFFF) << 29);
	}
	if (e == 0 && frac != 0)
	{
		e = 1023 - 126;
		do { frac <<= 1; e--; } while (!(frac & 0x00800000));
		return ((x & 0x80000000) << 32) | (e << 52) | ((frac & 0x007FFFFF) << 29);
	}
	u64 y = e >> 7, z = y << 61 | y << 60 | y << 59;
	return ((x & 0xC0000000) << 32) | z | ((x & 0x3FFFFFFF) << 29);
}

// a double -> the single's bits the 750 stores
static inline u32 cvtToSingle (u64 x)
{
	u32 e = (u32) (x >> 52) & 0x7FF;
	if (e > 896 || (x & ~0x8000000000000000ull) == 0) return (u32) ((x >> 32) & 0xC0000000) | (u32) ((x >> 29) & 0x3FFFFFFF);
	if (e >= 874)
	{
		u32 t = (u32) (0x80000000 | ((x & 0x000FFFFFFFFFFFFFull) >> 21));
		t >>= 905 - e;
		return t | (u32) ((x >> 32) & 0x80000000);
	}
	return (u32) ((x >> 32) & 0xC0000000) | (u32) ((x >> 29) & 0x3FFFFFFF);
}

// ---- the graphics of a frame, for a renderer (the layout of kapi v53/v54: kapi_gpu_vertex3 / _batch) ----
// The GX's triangles in clip space (the GPU divides and clips), texture coordinates 0..1 across
// their texture, the TEV's result as texel x colour + colour2; batches with a texture and a state.
struct GVertex { float x, y, z, w, s, t; u8 r, g, b, a, r2, g2, b2, a2; };
struct GBatch { u32 first, count; s32 tex; u32 flags; float m[16]; };
enum
{
	GF_ZALWAYS = 7, GF_NOZWRITE = 1 << 3, GF_CULL_BACK = 1 << 4, GF_CULL_FRONT = 1 << 5,
	GF_BLEND_SHIFT = 8, GF_LINEAR = 1 << 12, GF_WRAP_S_SHIFT = 13, GF_WRAP_T_SHIFT = 15, GF_NOMATRIX = 1 << 17,
	GF_ALPHATEST = 1 << 18				// (+ the threshold 0..255 << 19)
};
struct GTexture { u32 *px; int w, h, cap, levels; u64 key; u32 lastUse; bool dirty; };	// px: 0xAARRGGBB, its mipmaps after (cap: its room)

// the time spent in a function, added to a counter on leaving it (the ARM's clock: AArch64 hosts
// only, the others count nothing) -- the front ends' speed lines
static inline u64 gcClock ()
{
#if defined (__aarch64__)
	u64 t; asm volatile ("mrs %0, cntvct_el0" : "=r" (t)); return t;
#else
	return 0;
#endif
}
struct GcTimed { u64 &acc; u64 t0; GcTimed (u64 &a) : acc (a), t0 (gcClock ()) {} ~GcTimed () { acc += gcClock () - t0; } };

// The ARM's performance counters (gcemu --pmu, AArch64 at EL1: the core that runs the machine
// starts them): cycles, instructions, L1D refills, L2 refills, branch mispredictions. GcPmu adds
// what a function used to a counter set (0: nothing).
enum { PMU_N = 5 };
static inline void gcPmuStart (const u32 *ev = 0)		// (ev: the 4 events, else these)
{
#if defined (__aarch64__)
	static const u32 def[4] = { 0x08, 0x03, 0x17, 0x10 };
	if (!ev) ev = def;
	asm volatile ("msr pmevtyper0_el0, %0" :: "r" ((u64) ev[0]));	// INST_RETIRED
	asm volatile ("msr pmevtyper1_el0, %0" :: "r" ((u64) ev[1]));	// L1D_CACHE_REFILL
	asm volatile ("msr pmevtyper2_el0, %0" :: "r" ((u64) ev[2]));	// L2D_CACHE_REFILL
	asm volatile ("msr pmevtyper3_el0, %0" :: "r" ((u64) ev[3]));	// BR_MIS_PRED
	asm volatile ("msr pmccfiltr_el0, %0" :: "r" ((u64) 0));
	asm volatile ("msr pmcntenset_el0, %0" :: "r" ((u64) 0x8000000Full));
	asm volatile ("msr pmcr_el0, %0" :: "r" ((u64) 0x47));		// E, P, C, LC (64-bit cycles)
	asm volatile ("isb");
#else
	(void) ev;
#endif
}
static inline void gcPmuRead (u64 v[PMU_N])
{
#if defined (__aarch64__)
	u64 t;
	asm volatile ("mrs %0, pmccntr_el0" : "=r" (t)); v[0] = t;
	asm volatile ("mrs %0, pmevcntr0_el0" : "=r" (t)); v[1] = t;
	asm volatile ("mrs %0, pmevcntr1_el0" : "=r" (t)); v[2] = t;
	asm volatile ("mrs %0, pmevcntr2_el0" : "=r" (t)); v[3] = t;
	asm volatile ("mrs %0, pmevcntr3_el0" : "=r" (t)); v[4] = t;
#else
	for (int k = 0; k < PMU_N; k++) v[k] = 0;
#endif
}
static inline void gcPmuAdd (u64 *acc, const u64 *a, const u64 *b)	// (the event counters: 32 bits)
{
	acc[0] += b[0] - a[0];
	for (int k = 1; k < PMU_N; k++) acc[k] += (u32) (b[k] - a[k]);
}
struct GcPmu { u64 *acc; u64 v0[PMU_N]; GcPmu (u64 *a) : acc (a) { if (acc) gcPmuRead (v0); }
	       ~GcPmu () { if (acc) { u64 v[PMU_N]; gcPmuRead (v); gcPmuAdd (acc, v0, v); } } };

// the Zelda microcode's audio (gc_zelda.cpp): its mixing buffers (0x50 samples: a frame), the tables
// the game gives it, where its voices (VPBs) and its reverbs are
struct ZeldaMix
{
	s16 fl[0x50], fr[0x50], bl[0x50], br[0x50];			// front / back, left / right
	s16 flR[0x50], frR[0x50], blR[0x50], brR[0x50];			// (their reverb)
	s16 u0R[0x50], u1R[0x50], u0[0x50], u1[0x50], u2[0x50];		// (other reverb / mixing buffers)
	s16 resample[0x100], patterns[0x100], sine[0x80], afc[0x20];
	s16 last8[4][8]; u16 reverbFrame[4];
	u32 vpbBase, reverbBase; u16 outVolume; bool prepared;
};

// the Zelda microcode's variants (Dolphin's flags): its protocol (gc_dsp.cpp), its mixing (gc_zelda.cpp)
enum
{
	Z_LIGHT = 1, Z_SYNC_PER_FRAME = 2, Z_NO_CMD_0D = 4, Z_GBA_CRYPTO = 8, Z_WEIRD_CMD_0C = 16, Z_COMBINED_CMD_0D = 32,
	Z_FOUR_DESTS = 64, Z_TINY_VPB = 128, Z_VOLUME_STEP = 256, Z_LOUDER = 512
};

// the microcode the DSP runs (gc_dsp.cpp): which one, where its mail protocol is
enum { DSP_AX, DSP_ZELDA, DSP_CARD };
struct DspUcode
{
	u32 crc; int kind; u32 flags; int state;
	bool uploading; int uploadStep; u32 upload[10];		// (a task switch: the next microcode's description)
	// the Zelda microcode: the commands' words, the frames of audio asked for, the voices ready
	u32 zExpected, zPending, zCmd[64]; int zRd, zWr; bool zCanExec, zSecondHalf;
	u32 zVoices, zFrames, zFrame, zVoice, zSyncMax, zOutL, zOutR;
	u16 zSkip[256];
	ZeldaMix mix;
};
struct GFrame
{
	enum { MAXV = 3 * 60000, MAXB = 4096 };
	GVertex *v; int nv;
	GBatch *b; int nb;
	u32 clear;					// 0xRRGGBB
	int width, height;				// the EFB area copied to the XFB
};

// ---- the GX for a GPU with shaders (gc_gxgpu.cpp) ------------------------------------------------------------
// A front end with a programmable GPU (NintendoEMU's OpenGL) sets Machine::gpu: the draws reach it as
// the game gave them -- the vertices in model space with their matrix indices; the XF memory (the
// matrices, the lights: Machine::xfRegs, changed when xfSerial does); the state of the XF and of the
// TEV as a uniform block (GxState, std140) -- and the EFB copies (to textures, to the XFB), in the
// order the game made them, while the machine runs (on its thread). The GPU does the transform, the
// lighting, the texgens, the TEV per pixel, the fog, the alpha test; the EFB is its render target and
// a copy of it to a texture is a texture of the GPU (not in MEM1). Without it, gc_gxdraw.cpp
// transforms and colours the vertices on the CPU (the frames of GPU triangles of kapi gpu_render).
struct GxVertex { float pos[3], nrm[3]; u8 c0[4], c1[4]; float tc[8][2]; u8 mtx[12]; };	// mtx: pos, tex 0..7
enum { GX_TRIANGLES, GX_LINES, GX_POINTS };
enum { GX_VCD_NRM = 1, GX_VCD_C0 = 2, GX_VCD_C1 = 4 };
struct GxState
{
	// the uniform block (std140: rows of 4 x 32 bits) -- the vertex stage
	s32 vtx[4];			// the VCD (GX_VCD_*), the colour channels, the texgens, dual texture
	s32 chan[4];			// COLOR0 / COLOR1 / ALPHA0 / ALPHA1 control
	u32 matAmb[4];			// MATERIAL0 / MATERIAL1 / AMBIENT0 / AMBIENT1 (RGBA8)
	s32 texgen[8][4];		// TEXMTXINFO, POSTMTXINFO, -, -
	f32 proj[8];			// the projection's 6 parameters, its type (1: orthographic), -
	f32 vp[4];			// the viewport's signs (x, y: the picture upside down), the EFB's size
	// the pixel stage
	s32 gen[4];			// TEV stages, indirect stages, the alpha test's logic, the EFB's format
	s32 tevC[16], tevA[16];		// the stages' colour / alpha combiners
	s32 tref[16];			// the stages' map | coordinate << 3 | enabled << 6 | channel << 7
	s32 ksel[16];			// the stages' konst colour | konst alpha << 5
	s32 swap[16];			// the 4 swap tables: the source of r, g, b, a
	s32 regs[16], konst[16];	// PREV / C0 / C1 / C2 (s11), K0..K3 (rgba 0..255)
	s32 alpha[4];			// the alpha test: comparison 0, 1, reference 0, 1
	s32 zenv[4];			// the z texture: op, format, bias; the destination alpha (0x100 | alpha)
	s32 fogI[4];			// the fog: type, orthographic, b magnitude, b shift
	f32 fogF[4];			// a, c, the range's centre, enabled
	f32 fogColor[4];		// (0..255)
	f32 fogK[12];			// the range adjustment's k
	s32 ind[16];			// the stages' indirect command
	s32 indMtx[12];			// the 3 indirect matrices (BP 0x06-0x0E)
	s32 indRef[4];			// IREF, the indirect scales (SS0, SS1), -
	f32 texSize[8][4];		// the maps: width, height (the texels a coordinate of 1 spans)
	f32 tcScale[8][4];		// the coordinates: the rasterizer's scale (s, t), projective, -
	// the render state (not in the uniform block)
	u32 zmode, cmode0, cmode1, peCtrl, cull, lpSize;
	s32 scissor[4];			// x0, y0, x1, y1 in the EFB
	f32 viewport[6];		// x, y, w, h in the EFB; the depth range (0..1)
	u32 texMode[8][2];		// the maps' TX_SETMODE0 / 1
	s32 tex[8];			// the maps' textures: an index in tex[], GX_TEX_COPY + an EFB copy's slot, -1
	u32 serial;			// (changes with the state)
};
enum { GX_UBO_BYTES = 1200, GX_TEX_COPY = 0x10000, GX_COPIES = 64 };
struct GxCopy
{
	s32 x, y, w, h;			// the EFB's rectangle
	u32 addr; s32 slot;		// where it goes: an EFB copy's slot (-1: the XFB)
	u32 fmt;			// the texture format (0..15, Dolphin's EFBCopyFormat)
	bool half, intensity, depth, clear, toXfb;
	u32 efbFmt;			// the EFB's pixel format (PE_CONTROL)
	u32 clearColor, clearZ;		// ARGB, 24-bit
	bool colorMask, alphaMask, zMask;
};
class Machine;
struct GxGpu
{
	virtual void draw (Machine &m, const GxState &s, const GxVertex *v, int nv, const u32 *idx, int ni, int prim) = 0;
	virtual void copy (Machine &m, const GxCopy &c) = 0;
	virtual ~GxGpu () {}
};

class Machine
{
public:
	Machine ();
	~Machine ();
	void reset ();

	// ---- the CPU (gc_cpu.cpp) ----
	u32 gpr[32];
	double ps[32][2];				// the FPRs: [0] (ps0) is the FPR of the plain FPU, [1] ps1
	u32 cr, lr, ctr, xer, msr, fpscr;
	double fprfVal; bool fprfPending;		// (the JIT) FPSCR's FPRF is that result's class, to set
	u32 pc, npc, curPc;				// the instruction to run, the next one, the one running
	u32 srr0, srr1, dar, dsisr, sprg[4], ear, pvr, dec;
	u32 hid0, hid1, hid2, hid4, gqr[8], l2cr, wpar, dmaU, dmaL, mmcr0, mmcr1, pmc[4], thrm[3], ictc;
	u32 ibat[8], dbat[8];				// upper, lower pairs (IBAT0U, IBAT0L, IBAT1U...)
	u32 sr[16], sdr1;
	u64 cycles;					// CPU cycles run
	u64 jitUntil;					// the JIT's blocks chain until then (0: back to jitRun now)
	u64 jitScratch;					// (the JIT: a 64-bit value read by a helper)
	u64 jitEnd;					// (the JIT: jitUntil as its cycle countdown in x26 started, or resynced)
	u64 jitArg[3];					// (the x86-64 JIT: its helpers' arguments)
	u32 gatherN; u8 gather[64];			// the write-gather pipe (0x0C008000): its bytes, sent 32 at a time
	Jit *jit;					// the JIT (0: the interpreter runs the CPU)
	bool jitFlush;					// its code is to be thrown away (a BAT changed, a reset)
	bool halted; char haltMsg[96];
	u32 extIrq;					// an external interrupt is asserted (the PI's)

	void run (u64 untilCycle);			// run the CPU until then (or halted)
	void step ();
	void exception (u32 vector, u32 ret);		// taken now: SRR0 = ret, SRR1 = msr
	u64  decAt; bool decPending; u64 tbBase;	// the decrementer's next underflow (cycle), TB at cycle 0
	u32  decRead ();
	void decWrite (u32 v);
	u32  resvAddr; bool resv;			// lwarx / stwcx.
	u32  idleSkips;					// (stats) idle loops skipped
	u8   idleHit;					// the JIT skipped a polling loop's time (gxAsync: the GX waited for first)
	void checkInterrupts ();
	bool jitEnable ();				// the JIT runs the CPU from now on (false: no code memory / not AArch64)
	void jitRun (u64 untilCycle);
	void jitInvalidate (u32 pa, u32 len);		// code written there (icbi, a DMA): its blocks are dropped
	u32  jitBlocks, jitCompiles;			// (stats) blocks translated, now / in all
	bool jitProfile;				// (the tests, gcemu --jitprof) count each block's runs, and:
	u32 *jitInterpOps;				// (given: 65536) the instructions the JIT left to the interpreter,
							// by primary opcode << 10 | the extended one's 10 bits
	u32 jitSlowMem[2][16];				// the accesses off MEM1's fast path: loads / stores, by the
							// address's top 4 bits
	u64 jitEnters;					// the JIT entered from jitRun
	void jitStats (u64 &runs, u64 &hostInsns, u64 &guestInsns);
	int  jitReport (char *out, int cap, int nTop);	// the profile as text -> its length
	int  jitHotCode (u8 *out, int cap, int nTop);	// the costliest blocks' guest and host code -> bytes
	u32  fpscrNow () { if (fprfPending) { fprfPending = false; setFprf (fprfVal); } return fpscr; }	// (FPRF set)
	bool jitHot (int n, u32 &pc, u64 &runs, const u32 *&code, u32 &words);
	bool jitCode (u32 pc, const u32 *&code, u32 &words);

	// ---- memory (gc_mem.cpp) ----
	u8 *mem1;					// MEM1, big-endian
	u8 lcache[LCACHE_SIZE];			// the locked L1 data cache (dcbz_l, at 0xE0000000)
	bool translate (u32 ea, u32 &pa, bool data, bool write);	// false: no BAT covers it (DSI / ISI)
	void batRebuild ();				// after a BAT / MSR change
	u8  read8 (u32 ea);
	u16 read16 (u32 ea);
	u32 read32 (u32 ea);
	u64 read64 (u32 ea);
	void write8 (u32 ea, u8 v);
	void write16 (u32 ea, u16 v);
	void write32 (u32 ea, u32 v);
	void write64 (u32 ea, u64 v);
	u32  fetch (u32 ea);
	u8  *ptr (u32 pa);				// a physical address in MEM1 / the locked cache, or 0
	bool memFault;					// the last access had no translation (an exception is taken)

	// the hardware registers (0x0C000000 physical; gc_hw.cpp)
	u32  hwRead (u32 pa, int size);
	void hwWrite (u32 pa, u32 v, int size);

	// ---- the machine (gc_hw.cpp, gc_boot.cpp) ----
	bool loadDol (const u8 *dol, u32 size);		// a program: its sections, the IPL's state
	bool loadDisc (const u8 *iso, u32 size);	// a disc image (kept by the caller): boot it
	void runFrame ();				// until the next video field
	void setPad (int n, u32 buttons, int sx, int sy, int cx, int cy, int l, int r);
	// the picture of the last field (0x00RRGGBB)
	enum { FB_MAX_W = 720, FB_MAX_H = 576 };
	u32 fb[FB_MAX_W * FB_MAX_H]; int fbW, fbH;
	bool pal; char title[64]; char gameId[8];
	int frames;
	u32 irqCount[16];				// (the tests) PI interrupts raised, by bit
	// the disc: all of it in memory (disc), or read on demand through discRead (the app's
	// file: a disc image is 1.4 GB)
	const u8 *disc; u32 discSize;
	bool (*discRead) (void *ctx, u32 offset, u32 len, u8 *dst); void *discCtx;
	bool loadDiscImage (u32 size);			// boot the disc read through discRead
	u8 *aram;					// the DSP's ARAM (16 MB)
	enum { ARAM_SIZE = 16 * 1024 * 1024 };
	// pad buttons (PAD_*: as the SI report's first half)
	enum { PAD_LEFT = 0x0001, PAD_RIGHT = 0x0002, PAD_DOWN = 0x0004, PAD_UP = 0x0008, PAD_Z = 0x0010,
	       PAD_R = 0x0020, PAD_L = 0x0040, PAD_A = 0x0100, PAD_B = 0x0200, PAD_X = 0x0400, PAD_Y = 0x0800,
	       PAD_START = 0x1000 };

	// the chips' state
	u32 piIntsr, piIntmr, piFifoBase, piFifoEnd, piFifoWptr;
	u16 vi[0x80];					// the VI registers (16-bit words)
	int viLine, viLinesFrame; u64 viNextLine; u64 viCyclesLine;
	u32 siReg[0x40]; u8 siBuf[128]; u32 siPoll;
	u32 exiReg[3][5]; u32 exiCmd[3]; int exiPhase[3]; u8 sram[64]; u32 rtcBase;
	u64 exiTcAt[3];					// (a card's DMA: its transfer done then)
	// the memory cards (gc_card.cpp): slot A, B -- their flash is the front end's (Dolphin's .raw)
	struct Card { u8 *flash; u32 size; bool dirty; u8 status, cmd, intSwitch; bool intSet; u32 pos, addr; u64 doneAt; u8 prog[128]; };
	Card card[2];
	enum { CARD_SIZE = 2 * 1024 * 1024 };			// (251 blocks)
	void cardInsert (int slot, u8 *flash, u32 size);	// (0: the slot empty)
	u8   cardByte (int slot, u8 in);
	void cardCs (int slot, bool selected);
	u32  cardDma (int slot, u32 mem, u32 len, bool toMem);
	void cardDone (int slot);
	void cardAttach (int slot);
	void cardReset (int slot);
	void exiUpdate ();
	u32 diReg[10]; u64 diDoneAt; u32 dicover;
	u32 aiReg[4]; u64 aiSampleAt;
	u16 dspReg[0x40]; u32 dspMailIn; int dspBootStep;
	u64 aidmaNextAt, aidIrqAt, dspIrqAt; u32 aidmaLeft, aidmaAddr;
	u16 miReg[0x40];
	u16 padBtn[4]; s8 padSX[4], padSY[4], padCX[4], padCY[4]; u8 padL[4], padR[4];
	void piRaise (u32 bits);
	void piLower (u32 bits);
	void piUpdate ();
	void viLineStep ();
	void viOutput ();
	void siTransfer ();
	void siPollAll ();
	void siUpdate ();
	void exiTransfer (int ch);
	u32  exiIpl (int ch, u32 data, bool write, int len);
	void diCommand ();
	void events ();
	void hleBootState ();				// the memory / registers the IPL leaves
	bool readDisc (u32 offset, u32 len, u32 dst);	// DMA from the disc image into MEM1
	void hwReset ();
	void aiUpdate ();
	// the GX FIFO (gc_gx.cpp): the write-gather pipe's bytes into the FIFO in memory; the
	// command processor reads them (the commands: CP / XF / BP registers, display lists, the
	// primitives -- their vertices), the pixel engine (tokens, "draw done")
	void gpWrite (u32 v, int size);
	void gatherFlush ();				// its whole 32-byte bursts into the GX FIFO
	u32 gpBytes;
	u16 cpReg16[0x40];				// the CP's MMIO registers (0x0C000000)
	u32 cpFifoBase, cpFifoEnd, cpFifoRptr, cpFifoWptr, cpBreak;
	u16 peReg16[0x40];				// the PE's (0x0C001000)
	u32 cpRegs[0x100];				// the CP registers loaded by the FIFO (VCD, VAT, array bases / strides)
	u32 xfRegs[0x1100];				// the XF memory (matrices 0x000-0x4FF, lights 0x600-, registers 0x1000-)
	u32 bpRegs[0x100];				// the BP registers
	u32 bpKonst[8];					// the TEV's konst colours (BP 0xE0-0xE7 with bit 23)
	u32 gxCmds, gxPrims, gxVerts, gxCopies, gxIndirect, texDecodes;	// (the tests: the GPU's draws with indirect texturing, the textures decoded)
	u64 timeFifo, timePrim, timeTex;		// (gcClock ticks in gxFifoKick, gxPrimitive, gpuTexture: all in)
	bool pmuOn; u64 pmuFifo[PMU_N];			// (the front end's --pmu) the counters' counts in gxFifoKick
	// The GX on a core of its own (gxAsync, gc_gx.cpp): the front end runs gxStep there in a loop;
	// the CPU's side publishes the FIFO's write pointer (gatherFlush: waiting when the FIFO is
	// full), waits for the GX at the CP / PE / FIFO registers (gxSync), takes its interrupts
	// (gxIrqTake); gxDoneW: the write pointer the GX has run to; gxLock: the frames and the
	// textures the GX makes, against a reader (a front end drawing a frame).
	bool gxAsync; volatile u32 gxDoneW, gxIrqBits, gxLockV; u64 gxWaitTicks;
	bool gxStep ();					// (the GX's core) the FIFO up to the CPU's writes -> false: nothing new
	void gxSync ();					// (the CPU's) everything written run by the GX (else: gxFifoKick)
	void gxRaise (u32 bits);			// (the GX's) an interrupt for the CPU
	void gxIrqTake ();				// (the CPU's) the GX's interrupts, raised here
	void gxRoom (u32 w);				// (the CPU's) room in the FIFO at w for a burst
	void gxLock ();
	void gxUnlock ();
	u32 cpRead (u32 off, int size);
	void cpWrite (u32 off, u32 v, int size);
	u32 peRead (u32 off, int size);
	void peWrite (u32 off, u32 v, int size);
	void gxFifoKick ();				// run the commands written so far
	int  gxCommand (const u8 *p, int avail, bool inDl);	// one command: its length, 0 = not all there
	void gxRunDl (u32 addr, u32 size);
	int  gxVertexSize (int vat);
	void gxBp (u32 v);
	void gxXf (u32 addr, int n, const u8 *data);
	void gxPrimitive (int prim, int vat, int count, const u8 *verts);	// (gc_gxdraw.cpp later: the drawing)
	void gxCopy (u32 v);				// an EFB copy (to the XFB or a texture)
	// the drawing (gc_gxdraw.cpp): frames of GPU triangles, the textures decoded
	enum { MAX_TEX = 256, TEX_POOL = 16 << 20 };
	GFrame gfxFrame[2]; int gfxBuild, gfxReady; u32 gfxSerial;
	GTexture tex[MAX_TEX]; u32 texClock;
	s16 texFind[1024];				// (a key's slot in tex[], by its low bits: a hint, checked)
	u32 *texPool; u32 texPoolTop, texFlushes;	// the textures' pixels (TEX_POOL texels, made with the
							// machine: it may run where nothing can be allocated -- gcemu's
							// app core); full, every texture is forgotten (texFlush)
	void texFlush ();
	// a map's last answer (gpuTexture): its registers, the texture's key; good until texEpoch
	// changes -- each field, an EFB copy, a TLUT loaded, a DMA into MEM1, the pool emptied
	struct TexMemo { u32 img0, img3, tlut, mode0, mode1, epoch; int slot; u64 key; };
	TexMemo texMemo[8]; u32 texEpoch;
	u8 *tmem;					// the TMEM (1 MB: the TLUTs)
	u32 gxClearNext;				// the colour the next frame starts with
	u32 xfbCopyAddr[2];				// the last two XFBs an EFB copy went to (double buffering)
	bool xfbIsCopy ();				// the VI shows one: its picture is the last GX frame (not MEM1's)
	void gxInit ();
	int  gxTexture (int map, bool mips = false);	// the texture of a map (decoded, cached) -> its index, -1
	void gxEmit (const GVertex *v3, int tex, u32 flags);
	// the GX for a GPU with shaders (gc_gxgpu.cpp)
	GxGpu *gpu;					// (0: the frames of gc_gxdraw.cpp)
	GxState gxs; bool gxsDirty; u32 xfSerial;
	struct EfbCopy { u32 addr, bytes; u16 w, h; u8 fmt; u64 hash; u32 use; };
	EfbCopy efbCopies[GX_COPIES];			// (the copies to textures: where they went, the memory there then)
	void gxGpuPrimitive (int prim, int count, const GxVertex *v, u32 vcd);
	void gxGpuState (u32 vcd);
	void gxGpuCopy (u32 v);
	int  gpuTexture (int map);
	// the DSP (gc_dsp.cpp): its ROM and microcodes at a high level -- the mailboxes, the boot, the
	// protocols of AX and of the Zelda microcode
	enum { DSP_QUEUE = 64 };
	void dspReset ();
	void dspSetProgram (int step);
	void dspMailReceived (u32 mail);
	u16  dspMailHigh ();
	u16  dspMailLow ();
	void dspIrqUpdate ();
	void dspInterrupt (u32 delay);
	void dspPush (u32 mail, bool irq, u32 delay = 0);
	void dspStartUcode (u32 addr, u32 len);
	void dspUpload (u32 mail);
	void dspAxMail (u32 mail);
	void dspZeldaMail (u32 mail);
	void dspZRun ();
	void dspZRender ();
	void dspZAck (bool done, u32 sync);
	u32  dspZRead ();
	void dspZWrite (u32 v);
	// the Zelda microcode's mixing (gc_zelda.cpp): a frame begun, a voice added, the frame out
	void zPrepare ();
	void zAddVoice (u32 id);
	void zFinalize ();
	// the sound out (gc_hw.cpp): the audio DMA's samples, at its rate, handed to the host at its own
	enum { AUDIO_RING = 16384 };
	s16 audioBuf[AUDIO_RING * 2]; u32 audioW, audioR, audioHostRate, audioFrac; s16 audioPrev[2];
	void setAudioRate (int rate) { audioHostRate = (u32) rate; }	// (0: no sound kept)
	int  audioRead (s16 *lr, int maxFrames);		// -> the frames written (stereo, at the host's rate)
	void audioBlock (u32 addr);				// (a block of the audio DMA: 8 frames, R L big-endian)
	u32  aidRate () const { return (aiReg[0] & 0x40) ? 32000 : 48000; }	// (AICR.AIDFR: the DMA's rate)
	u32 dspQueue[DSP_QUEUE]; u8 dspQIrq[DSP_QUEUE]; int dspQHead, dspQTail; u32 dspLastOut;
	u32 dspBootMails[2]; u32 dspUcode;
	DspUcode dspUc, dspSaved; bool dspHaveSaved;

	// what a front end shows to tell a slow game from a stuck one (F12): the DVD reads so far
	u32 diReads, diLastOff;
	u32 dspBootKey, dspMailsIn, dspLastMail;		// (the DSP's boot: the pending key; mails the CPU sent)
	u32 hwLastRead, hwLastReadN;				// (the register the CPU polls: its address, reads in a row)
	void status (char *out, int cap);			// "pc 80012345, DVD 123 reads, picture: ..., DSP ..."

private:
	friend struct Jit;
	void exec (u32 op);
	void op4 (u32 op);				// the paired singles
	void op19 (u32 op);
	void op31 (u32 op);
	void op59 (u32 op);
	void op63 (u32 op);
	u32  mfspr (u32 n);
	void mtspr (u32 n, u32 v);
	void setCr0 (u32 v) { u32 f = (s32) v < 0 ? 8 : v ? 4 : 2; cr = (cr & 0x0FFFFFFF) | (f | (xer >> 31)) << 28; }
	void setCr1 () { cr = (cr & 0xF0FFFFFF) | ((fpscr >> 28) << 24); }
	void setFprf (double d);
	double quantLoad (u32 ea, int type, int scale, int &size);
	void quantStore (u32 ea, double v, int type, int scale, int &size);
	// the BATs as a map of 128 KB blocks: physical block + 1 (0: not mapped), data / instruction
	u32 dmap[0x8000], imap[0x8000];
};

} // namespace gc

#endif
