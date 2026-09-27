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

	// the hardware registers (0x0C000000 physical; gc_hw.cpp -- the tests supply their own)
	virtual u32  hwRead (u32 pa, int size);
	virtual void hwWrite (u32 pa, u32 v, int size);

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
