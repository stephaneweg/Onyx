//
// n64/n64.h -- a Nintendo 64 core: the R4300i CPU (MIPS III, 64-bit registers, COP0 with the
// TLB and the exceptions, COP1 floating point), the RCP's interfaces (MI, VI, AI, PI, SI with
// the PIF and the controllers, RI, the SP and DP registers), the boot (the PIF + IPL3 work
// done directly: the first megabyte of the game copied, the registers and the system variables
// set, the entry point called) and the cartridge (ROM, SRAM / EEPROM saves).
//
// The graphics and the sound tasks of the RSP are done at a high level (n64_gfx.cpp,
// n64_audio.cpp): what the microcode would do, not its instructions. The picture: the VI shows
// the framebuffer in RDRAM (the CPU's drawing) or the triangles of the display lists (drawn by
// a renderer the app supplies: the GPU on Onyx, software on the PC).
//
// Integer and float (the FPU): runs in any Onyx app (FP flags) and on the PC for the tests.
//
//   n64::Machine *m = new n64::Machine;
//   m->load (rom, size);          // .z64 (big-endian), .v64 (byte-swapped), .n64 (little-endian)
//   each frame: m->setPad (0, buttons, x, y); m->runFrame (); -> m->fb (m->fbW x m->fbH, 0x00RRGGBB)
//
#ifndef _n64_n64_h
#define _n64_n64_h

namespace n64 {

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef short s16;
typedef unsigned int u32;
typedef int s32;
typedef unsigned long long u64;
typedef long long s64;

enum { RDRAM_SIZE = 8 << 20 };				// with the Expansion Pak
enum { FB_MAX_W = 640, FB_MAX_H = 576 };

// the controller (the bits of its status word, as the pad sends them)
enum { BTN_A = 0x8000, BTN_B = 0x4000, BTN_Z = 0x2000, BTN_START = 0x1000, BTN_DUP = 0x0800, BTN_DDOWN = 0x0400,
       BTN_DLEFT = 0x0200, BTN_DRIGHT = 0x0100, BTN_L = 0x0020, BTN_R = 0x0010, BTN_CUP = 0x0008, BTN_CDOWN = 0x0004,
       BTN_CLEFT = 0x0002, BTN_CRIGHT = 0x0001 };

// ---- the graphics of a frame, for a renderer (the layout of kapi v53: kapi_gpu_vertex3 / _batch) ----
// The display lists' triangles, in clip space (x y z w, the GPU divides and clips), texture
// coordinates 0..1 across their texture, a colour; batches of them with a texture and a state.
struct GVertex { float x, y, z, w, s, t; u8 r, g, b, a, r2, g2, b2, a2; };	// (r2..a2: added, v54)
struct GBatch { u32 first, count; s32 tex; u32 flags; float m[16]; };
enum
{
	GF_ZALWAYS = 7, GF_ZLEQUAL = 3, GF_NOZWRITE = 1 << 3, GF_CULL_BACK = 1 << 4, GF_CULL_FRONT = 1 << 5,
	GF_BLEND_ALPHA = 1 << 8, GF_LINEAR = 1 << 12, GF_WRAP_S_SHIFT = 13, GF_WRAP_T_SHIFT = 15, GF_NOMATRIX = 1 << 17,
	GF_ALPHATEST = 1 << 18				// (+ the threshold 0..255 << 19)
};
struct GTexture { u32 *px; int w, h; u64 key; u32 lastUse; bool dirty; };	// px: 0xAARRGGBB
struct GFrame
{
	enum { MAXV = 3 * 40000, MAXB = 4096 };
	GVertex *v; int nv;
	GBatch *b; int nb;
	u32 clear;					// 0xRRGGBB, the fill of the whole screen
	int width, height;				// the N64 framebuffer the coordinates are in
	u32 cimg;					// its address
};

// MI interrupt bits
enum { MI_SP = 1, MI_SI = 2, MI_AI = 4, MI_VI = 8, MI_PI = 16, MI_DP = 32 };

class Machine
{
public:
	Machine ();
	~Machine ();
	bool load (const u8 *rom, u32 size);		// false: not an N64 ROM
	void reset ();
	void runFrame ();				// until the next vertical interrupt of the VI
	void setAudioRate (int hz);			// the host's sample rate (0: no sound kept)
	int  audioRead (short *lr, int maxFrames);	// stereo frames at that rate, → how many
	void setPad (int n, u32 buttons, int x, int y) { if (n >= 0 && n < 4) { padBtn[n] = (u16) buttons; padX[n] = (s8) x; padY[n] = (s8) y; } }

	// The host's copy of the frame it last showed (0x00RRGGBB, any size, drawn by its GPU or
	// software renderer): written back into RDRAM as the N64's framebuffer when a game reads
	// that framebuffer as a texture (Ocarina of Time's pause background copies it). Call it
	// while the machine is not running (lockstep), after each frame shown.
	void fbSnapshot (const u32 *px, int w, int h, int stride);

	// the picture of the last frame (from the VI registers), 0x00RRGGBB
	u32  fb[FB_MAX_W * FB_MAX_H];
	int  fbW, fbH;
	bool pal;					// the ROM's region (50 Hz)
	char title[21];
	u32  cic;					// the boot chip (6101, 6102, 6103, 6105, 6106)
	int  frames;
	u64  cycles;					// CPU cycles (93.75 MHz) since power-on

	// ---- (public for the tests) ----
	// the CPU
	u64 r[32]; u64 hi, lo;
	u32 pc, npc;					// the instruction to run, the one after it
	u32 curPc;					// the instruction running
	bool inDelay;					// pc is a branch delay slot
	bool nextDelay;					// ... the next one will be
	u64 cp0[32];
	u64 fpr[32]; u32 fcr31;
	bool llbit;
	struct TlbEntry { u32 mask, hi, lo0, lo1; };	// hi: VPN2 | ASID (G in lo0 & lo1)
	TlbEntry tlb[32];
	// memory
	u32 *rdram;					// words in the host's order (a byte is at address ^ 3)
	u32 *rom; u32 romSize;				// the cartridge, the same way
	u32 dmem[1024], imem[1024];			// the RSP's memories
	u8  pifRam[64];
	// the RCP registers
	u32 mi[4];					// MODE, VERSION, INTR, INTR_MASK
	u32 vi[14];
	u32 ai[6];
	u32 pi[13];
	u32 si[7];
	u32 ri[8];
	u32 sp[8], spPc;
	u32 dp[8];
	u32 rdramReg[10];
	// the saves
	enum { SRAM_SIZE = 0x8000 * 4, EEPROM_SIZE = 2048 };
	u8  sram[SRAM_SIZE]; bool sramDirty;
	u8  eeprom[EEPROM_SIZE]; int eepromSize; bool eepromDirty;
	int saveType;					// 0 none / unknown, 1 SRAM, 2 EEPROM 4 Kbit, 3 EEPROM 16 Kbit

	// the graphics (n64_gfx.cpp): the frame being built, the last one finished, the textures
	enum { MAX_TEX = 256, TEX_PIXELS = 8192 };
	GFrame gfxFrame[2]; int gfxBuild;		// built into gfxFrame[gfxBuild], the other one is shown
	int  gfxReady;					// the index of the last finished frame, -1 none
	u32  gfxSerial;					// frames finished so far
	GTexture tex[MAX_TEX]; u32 texClock;

	// the debugger's view (tests)
	bool halted;					// an error stopped the CPU
	u32  rspTasks[8];				// the tasks run, by type
	u32  irqCount[6];				// MI interrupts raised: SP SI AI VI PI DP
	u32  lastTask;
	u32  audioTasks, audioUnknown;			// audio tasks done / of a microcode not known
	u32  excCount[32]; u32 lastExcPc = 0; int lastExcCode = -1;	// (the tests)
#ifdef N64_TRACE
	bool traceOn = false; u32 traceBuf[4096]; u64 traceN = 0;
#endif
	char haltMsg[96];

private:
	// ---- the CPU (n64_cpu.cpp) ----
	void step ();
	void exec (u32 op);
	void cop0 (u32 op);
	void cop1 (u32 op);
	void exception (int code, int ce = 0);
	void checkInterrupts ();
	void tlbWrite (int i);
	void tlbProbe ();
	bool translate (u32 va, u32 &pa, bool write);
	u32  lastTlb;					// the entry of the last hit
	int  tlbMissVector;				// 0: refill vector, 1: general (set by translate)
	u32  badVa;
	bool excPending;				// a memory access raised an exception
	u64  countBase;					// Count = (cycles - countBase) / 2
	u32  count ();
	void scheduleCompare ();
	// memory, virtual (with the exceptions)
	bool rd8 (u32 va, u64 &v);
	bool rd16 (u32 va, u64 &v);
	bool rd32 (u32 va, u64 &v);
	bool rd64 (u32 va, u64 &v);
	bool wr8 (u32 va, u32 v);
	bool wr16 (u32 va, u32 v);
	bool wr32 (u32 va, u32 v);
	bool wr64 (u32 va, u64 v);
	u32  fetch (u32 va, bool &ok);
	// ---- the bus (n64_bus.cpp) ----
	u32  readIo (u32 pa);				// a 32-bit word outside RDRAM
	u32  readSub (u32 pa);				// the word an 8 / 16-bit read sees there
	void writeIo (u32 pa, u32 v, u32 mask);		// mask: the bytes written (as a big-endian word)
	void raise (u32 bits);				// MI interrupt
	void lower (u32 bits);
	void piDma (bool toRdram);
	u32  piHalf (u32 cartAddr);
	u32  piDuration (u32 len);
	void siDma (bool toRdram);
	void pifProcess ();
	void aiPush ();
	void spStatusWrite (u32 v);
	void spDma (bool toRdram);
	void runRsp ();					// the halt bit was cleared: do the task
	// ---- the graphics (n64_gfx.cpp) ----
	void gfxInit ();
	void gfxTask ();
	void gfxDl (u32 addr);
	void gfxCmd (u32 w0, u32 w1, u32 &pcDl, int &sp, u32 *stack);
	void gfxVtx (u32 addr, int n, int v0);
	void gfxTri (int a, int b, int c);
	void gfxRect (float x0, float y0, float x1, float y1, float s0, float t0, float s1, float t1, bool tex, bool fill);
	void gfxRdp (u32 w0, u32 w1);
	void gfxLoad (u32 w0, u32 w1, int kind);
	int  gfxTexture (int tile);
	void gfxCombine (const float *shade, const float *texel, float *out);
	u32  gfxFlags ();
	void gfxEmit (const GVertex *v3, int tex, u32 flags);
	u32  seg (u32 a) { return (segment[(a >> 24) & 15] + (a & 0xFFFFFF)) & 0x7FFFFF; }
	u8   rdB (u32 a) { return ((const u8 *) rdram)[(a & 0x7FFFFF) ^ 3]; }
	u16  rdH (u32 a) { return *(const u16 *) ((const u8 *) rdram + ((a & 0x7FFFFE) ^ 2)); }
	u32  rdW (u32 a) { return rdram[(a & 0x7FFFFC) >> 2]; }
	// the RSP's state (F3DEX2)
	u32  segment[16];
	float mtxStack[10][16]; int mtxSp; float proj[16]; float mvp[16]; bool mvpDirty;
	struct Vtx { float x, y, z, w; float s, t; float r, g, b, a; u32 clip; };
	Vtx  vtx[64];
	u32  geom;
	float texScaleS, texScaleT; int texTile, texOn;
	struct Light { float r, g, b, x, y, z; };
	Light lights[8]; int numLights;
	s16  vpScale[4], vpTrans[4];
	u32  rdpHalf1, rdpHalf2;
	float fogMul, fogOff;
	// the RDP's state
	u32  omH, omL;					// the other modes
	u32  combH, combL;
	float primC[4], envC[4], blendC[4], fogC[4]; u32 fillColor; float primDepth;
	u32  cimg, cimgW, zimg, timg, timgW, timgSiz, timgFmt;
	struct Tile { int fmt, siz, line, tmem, pal, cmt, maskt, shiftt, cms, masks, shifts; int sl, tl, sh, th; };
	Tile tiles[8];
	u8   tmem[4096];
	u32  tmemSerial;				// changes when TMEM is loaded
	int  scissor[4];
	u32  cimgSiz; int rectTile; bool drawMain; bool dlEnd;
	// the framebuffers drawn by the renderer (not in RDRAM): written back from the host's copy
	u32 *fbSnap; int fbSnapW, fbSnapH; u32 fbSnapSerial;
	struct FbRec { u32 addr, w, h, written; } fbRec[4]; int fbRecNext;
	void fbNote (u32 addr, u32 w);
	void fbWriteback (u32 addr, u32 len);
	void rectCpu (float x0, float y0, float x1, float y1, float s0, float t0, float s1, float t1,
		      int texId, const float *col, const float *add, bool fill);
	u32  viW () const { return vi[2] & 0xFFF; }
	void viLine ();
	void viOutput ();
	u32  cartRead (u32 pa);
	void bootHle ();
	// events: the next cycle something happens
	enum { EV_VI, EV_COMPARE, EV_PI, EV_SI, EV_AI, EV_SP, EV_DP, EV_N };
	u64  evAt[EV_N];
	u64  nextEvent;
	void schedule (int ev, u64 delta) { evAt[ev] = cycles + delta; if (evAt[ev] < nextEvent) nextEvent = evAt[ev]; }
	void events ();
	int  viLineNow, viLines;			// the half-line counter, lines a field
	u64  viCyclesLine;
	bool frameDone;
	// audio
	u32  aiFifo[2][2]; int aiCount;			// the queued DMAs (address, length)
	u32  aiRate () const;
	void aiOutput (u32 addr, u32 len, u32 rate);
	enum { ABUF = 8192 };
	s16  abuf[ABUF * 2]; u32 aHead, aTail;		// the output ring (host rate)
	u32  outRate, resAcc; s32 lastL, lastR;
	// the audio microcode (n64_audio.cpp): its DMEM image and its state
	void audioTask ();
	s16  aS (u32 a);
	void aW (u32 a, s16 v);
	void aLoad (u32 dmem, u32 addr, u32 count);
	void aSave (u32 dmem, u32 addr, u32 count);
	void aAdpcm (u32 flags, u32 state);
	void aResample (u32 flags, u32 pitch, u32 state);
	void aEnvMix (u32 w0, u32 w1);
	u8   aDmem[4096];
	s16  aBook[256];
	u32  aIn, aOut, aCount, aLoop;
	u16  aEnvV[3], aEnvS[3];
	// the controllers
	u16  padBtn[4]; s8 padX[4], padY[4];
};

// the words of the host order <-> the bytes of the N64 (big-endian)
static inline u32 bswap32 (u32 v) { return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24); }

} // namespace n64

#endif
