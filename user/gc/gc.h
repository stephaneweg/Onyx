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
//   * gc_boot.cpp starting a program: a .dol, or a disc image through its apploader (run by
//                 the CPU, as the IPL does), with the memory the IPL leaves
//
// Memory is kept in the console's byte order (big-endian): a 32-bit read byte-swaps.
//
#ifndef GC_GC_H
#define GC_GC_H

namespace gc {

typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
typedef signed char s8; typedef short s16; typedef int s32; typedef long long s64;

enum { MEM1_SIZE = 24 * 1024 * 1024, LCACHE_SIZE = 16 * 1024 };
enum { CPU_HZ = 486000000, BUS_HZ = 162000000, TB_HZ = BUS_HZ / 4, CYC_PER_TB = CPU_HZ / TB_HZ };

static inline u32 bswap32 (u32 v) { return __builtin_bswap32 (v); }
static inline u16 bswap16 (u16 v) { return (u16) __builtin_bswap16 (v); }

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
struct GTexture { u32 *px; int w, h; u64 key; u32 lastUse; bool dirty; };	// px: 0xAARRGGBB
struct GFrame
{
	enum { MAXV = 3 * 60000, MAXB = 4096 };
	GVertex *v; int nv;
	GBatch *b; int nb;
	u32 clear;					// 0xRRGGBB
	int width, height;				// the EFB area copied to the XFB
};

class Machine
{
public:
	Machine ();
	~Machine ();
	void reset ();

	// ---- the CPU (gc_cpu.cpp) ----
	u32 gpr[32];
	double ps0[32], ps1[32];			// the FPRs: ps0 is the FPR of the plain FPU
	u32 cr, lr, ctr, xer, msr, fpscr;
	u32 pc, npc, curPc;				// the instruction to run, the next one, the one running
	u32 srr0, srr1, dar, dsisr, sprg[4], ear, pvr, dec;
	u32 hid0, hid1, hid2, hid4, gqr[8], l2cr, wpar, dmaU, dmaL, mmcr0, mmcr1, pmc[4], thrm[3], ictc;
	u32 ibat[8], dbat[8];				// upper, lower pairs (IBAT0U, IBAT0L, IBAT1U...)
	u32 sr[16], sdr1;
	u64 cycles;					// CPU cycles run
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
	void checkInterrupts ();

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
	// the disc
	const u8 *disc; u32 discSize;
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
	u32 diReg[10]; u64 diDoneAt; u32 dicover;
	u32 aiReg[4]; u64 aiSampleAt;
	u16 dspReg[0x40]; u32 dspMailIn, dspMailOut; bool dspMailOutValid; int dspBootStep;
	u64 aidmaNextAt; u32 aidmaLeft, aidmaAddr;
	u16 miReg[0x40];
	u16 padBtn[4]; s8 padSX[4], padSY[4], padCX[4], padCY[4]; u8 padL[4], padR[4];
	void piRaise (u32 bits);
	void piLower (u32 bits);
	void piUpdate ();
	void viLineStep ();
	void viOutput ();
	void siTransfer ();
	void siPollAll ();
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
	u32 gpBytes;
	u16 cpReg16[0x40];				// the CP's MMIO registers (0x0C000000)
	u32 cpFifoBase, cpFifoEnd, cpFifoRptr, cpFifoWptr, cpBreak;
	u16 peReg16[0x40];				// the PE's (0x0C001000)
	u32 cpRegs[0x100];				// the CP registers loaded by the FIFO (VCD, VAT, array bases / strides)
	u32 xfRegs[0x1100];				// the XF memory (matrices 0x000-0x4FF, lights 0x600-, registers 0x1000-)
	u32 bpRegs[0x100];				// the BP registers
	u32 bpKonst[8];					// the TEV's konst colours (BP 0xE0-0xE7 with bit 23)
	u32 gxCmds, gxPrims, gxVerts, gxCopies;	// (the tests)
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
	enum { MAX_TEX = 256 };
	GFrame gfxFrame[2]; int gfxBuild, gfxReady; u32 gfxSerial;
	GTexture tex[MAX_TEX]; u32 texClock;
	u8 *tmem;					// the TMEM (1 MB: the TLUTs)
	u32 gxClearNext;				// the colour the next frame starts with
	void gxInit ();
	int  gxTexture (int map);			// the texture of a map (decoded, cached) -> its index, -1
	void gxEmit (const GVertex *v3, int tex, u32 flags);
	// the DSP (gc_dsp.cpp): its ROM and microcode at a high level -- the mailboxes, the boot
	void dspReset ();
	void dspMailReceived (u32 mail);
	void dspHleStep ();
	void dspIrqUpdate ();
	void dspPush (u32 mail, bool irq);
	u32 dspQueue[16]; int dspQHead, dspQTail;
	u32 dspBootMails[10]; int dspBootN; u32 dspUcode; u32 dspCmdlistLeft;

private:
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
