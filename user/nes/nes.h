//
// nes/nes.h -- a Nintendo Entertainment System (Famicom) core: the 2A03 CPU (6502, the
// official opcodes and the stable unofficial ones), the 2C02 PPU (a scanline renderer
// driven dot by dot for the timings: vblank / NMI, sprite 0 hit, the MMC3 IRQ), the APU
// (2 pulses, triangle, noise, DMC; the frame counter), the controller, and the cartridges
// most games use: mappers 0 (NROM), 1 (MMC1), 2 (UxROM), 3 (CNROM), 4 (MMC3), 7 (AxROM),
// 66 (GxROM), with their battery RAM. NTSC (60 Hz) and PAL (50 Hz) timings.
// Integer only, no libc: runs in any Onyx app (and on the PC for the tests).
//
//   nes::Machine *m = new nes::Machine;
//   m->load (rom, size);             // an iNES / NES 2.0 file
//   each frame: m->setButtons (mask); m->runFrame (); -> m->fb (256 x 240, 0x00RRGGBB)
//   m->audioRead (lr, n)             // s16 stereo at setAudioRate
//
#ifndef _nes_nes_h
#define _nes_nes_h

namespace nes {

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

enum { W = 256, H = 240 };
enum { BTN_A = 1, BTN_B = 2, BTN_SELECT = 4, BTN_START = 8, BTN_UP = 16, BTN_DOWN = 32, BTN_LEFT = 64, BTN_RIGHT = 128 };

class Machine
{
public:
	Machine ();
	~Machine ();
	bool load (const u8 *rom, int size);		// false: not a NES ROM / a mapper not supported
	void reset ();
	void runFrame ();				// one video frame
	void setButtons (int mask) { pad = (u8) mask; }
	void setAudioRate (int hz);
	int  audioRead (short *lr, int maxFrames);	// s16 stereo frames produced so far
	void setSaveRam (const u8 *data, int n);
	void setPal (bool on);				// PAL timing (50 Hz); load() guesses from the header

	u32  fb[W * H];					// the last frame, 0x00RRGGBB
	bool pal;					// PAL timing in use
	int  mapper;					// the cartridge's mapper number
	u8   sram[0x2000];				// $6000-$7FFF (battery RAM when battery)
	bool battery, sramDirty;
	// the tests: blargg's ROMs report through $6000 (status) / $6004 (text)
	u64  cycles;					// CPU cycles since power-on

private:
	// ---- cartridge ----
	const u8 *prg; u32 prgSize;			// PRG ROM
	const u8 *chrRom; u32 chrSize;			// CHR ROM (0: CHR RAM)
	u8  chrRam[0x8000];
	bool chrIsRam;
	u32 prgMap[4];					// $8000/$A000/$C000/$E000: offsets in prg
	u32 chrMap[8];					// 1 KB PPU banks: offsets in chr
	int mirror;					// 0 horizontal, 1 vertical, 2 one-screen A, 3 one-screen B, 4 four-screen
	bool fourScreen;
	u8  ciram[0x1000];				// nametables (2 KB; 4 KB for four-screen)
	// mapper registers
	u8  mmc1Shift, mmc1Count, mmc1Ctrl, mmc1Chr0, mmc1Chr1, mmc1Prg; u64 mmc1LastWrite;
	u8  mmc3Sel, mmc3Regs[8], mmc3Latch, mmc3Counter; bool mmc3Reload, mmc3IrqOn, mmc3Irq, mmc3PrgMode, mmc3ChrMode;
	u8  simpleBank;
	bool sramOn;
	void mapperReset ();
	void mapperWrite (u16 a, u8 v);
	void updateBanks ();
	void mmc3Clock ();				// a scanline (A12 rising edge)
	bool a12; u64 a12LowSince;			// the PPU address line A12 on CPU accesses ($2006 / $2007)
	void a12Access (u16 ad);

	// ---- CPU ----
	u8  a, x, y, s, p; u16 pc;
	u8  ram[0x800];
	bool nmiPending, irqLine;
	int  stall;					// CPU cycles stolen (OAM DMA)
	u8  read (u16 a);
	void write (u16 a, u8 v);
	u16 read16 (u16 a) { return (u16) (read (a) | (read ((u16) (a + 1)) << 8)); }
	int  step ();					// one instruction: its cycles
	void push (u8 v) { write ((u16) (0x100 | s--), v); }
	u8   pull () { return read ((u16) (0x100 | ++s)); }
	void interrupt (u16 vector, bool brk);
	void setZN (u8 v) { p = (u8) ((p & 0x7D) | (v & 0x80) | (v ? 0 : 2)); }
	void adc (u8 v);
	void cmp (u8 r, u8 v);

	// ---- PPU ----
	u8  ctrl, mask, status, oamAddr, oam[256], palRam[32];
	u16 v, t; u8 fineX; bool wLatch; u8 readBuf, openBus;
	int  line, dot; bool oddFrame, frameDone;
	int  sprite0Dot;				// the dot of this line's sprite 0 hit, or -1
	bool nmiOccurred;
	u64  dotAcc;					// PAL: dots x 5 per CPU cycles x 16
	u8  ppuRead (u16 a);
	void ppuWrite (u16 a, u8 v);
	u8  regRead (int r);
	void regWrite (int r, u8 v);
	void ppuRun (int cpuCycles);
	void ppuDot ();
	void renderLine ();
	void nmiCheck ();
	const u8 *chrPtr (u16 a) const;			// pattern memory $0000-$1FFF
	u8  *ntPtr (u16 a);				// nametable byte ($2000-$2FFF)
	bool rendering () const { return (mask & 0x18) != 0; }

	// ---- APU ----
	struct Pulse { bool on; int duty, seq, timer, period, len, vol, envPeriod, envDiv, envVol, sweepPeriod, sweepDiv, sweepShift; bool constVol, halt, envStart, sweepOn, sweepNeg, sweepReload; };
	struct Tri { bool on, halt, linReload; int timer, period, len, linCounter, linPeriod, seq; };
	struct Noise { bool on, halt, constVol, envStart, mode; int timer, period, len, vol, envPeriod, envDiv, envVol; u16 lfsr; };
	struct Dmc { bool on, irqOn, loop, irq, silence; int rate, timer, level, bits, shift, bufFull, buf; u16 addr, start; int remain, len; };
	Pulse pu[2]; Tri tr; Noise no; Dmc dmc;
	int  frameMode; bool frameIrqOff, frameIrq; int frameCycle;
	int  apuPending;				// CPU cycles not yet run by the APU
	int  instrCyc, apuAhead;			// this instruction's cycles / those the APU already ran
	void apuSync ();				// the APU up to this instruction's last cycle (its access)
	int  rate; u64 sampleAcc; int cpuHz;
	long long hpAcc;				// (a DC-blocking high-pass on the output)
	int  lastOut;
	enum { ABUF = 8192 };
	short abuf[ABUF * 2]; int ahead, atail;
	void apuWrite (u16 a, u8 v);
	u8   apuStatus ();
	void apuRun (int cycles);
	void apuFlush () { if (apuPending) { int n = apuPending; apuPending = 0; apuRun (n); } }
	void quarterFrame ();
	void halfFrame ();
	int  mixSample ();

	// ---- input ----
	u8  pad, padShift; bool strobe;
};

} // namespace nes

#endif
