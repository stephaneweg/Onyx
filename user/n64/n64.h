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
	void setPad (int n, u32 buttons, int x, int y) { if (n >= 0 && n < 4) { padBtn[n] = (u16) buttons; padX[n] = (s8) x; padY[n] = (s8) y; } }

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

	// the debugger's view (tests)
	bool halted;					// an error stopped the CPU
	u32  rspTasks[8];				// the tasks run, by type
	u32  irqCount[6];				// MI interrupts raised: SP SI AI VI PI DP
	u32  lastTask;
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
	// the controllers
	u16  padBtn[4]; s8 padX[4], padY[4];
};

// the words of the host order <-> the bytes of the N64 (big-endian)
static inline u32 bswap32 (u32 v) { return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24); }

} // namespace n64

#endif
