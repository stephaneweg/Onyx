//
// snes/snes.h -- a Super Nintendo (Super Famicom) core: the 5A22 CPU (65C816, the DMA / HDMA
// channels, the multiplier / divider, the H/V timers, NMI / IRQ, the auto-joypad read), the
// PPU (modes 0-7 a line at a time: 2/4/8 bpp backgrounds, 8x8 and 16x16 tiles, offset-per-
// tile, mosaic, Mode 7 and EXTBG, the sprites, the two windows, colour math on the sub
// screen or the fixed colour, brightness), the sound unit (the SPC700 with its IPL boot ROM
// and timers, and the S-DSP: 8 BRR voices, ADSR / GAIN, noise, pitch modulation, echo + FIR)
// and the LoROM / HiROM cartridges with their battery RAM. NTSC (60 Hz) and PAL (50 Hz).
// No enhancement chip (Super FX, SA-1, DSP-n...). Integer only, no libc: runs in any Onyx
// app (and on the PC for the tests).
//
//   snes::Machine *m = new snes::Machine;
//   m->load (rom, size);             // a .sfc / .smc file (a 512-byte copier header is skipped)
//   each frame: m->setButtons (mask); m->runFrame (); -> m->fb (256 x m->height, 0x00RRGGBB)
//   m->audioRead (lr, n)             // s16 stereo at setAudioRate
//
#ifndef _snes_snes_h
#define _snes_snes_h

namespace snes {

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef short s16;
typedef unsigned int u32;
typedef int s32;
typedef unsigned long long u64;

enum { W = 256, H = 239 };
// The controller's buttons: the bits of the joypad word ($4218), as the pad sends them
enum { BTN_B = 0x8000, BTN_Y = 0x4000, BTN_SELECT = 0x2000, BTN_START = 0x1000, BTN_UP = 0x0800, BTN_DOWN = 0x0400,
       BTN_LEFT = 0x0200, BTN_RIGHT = 0x0100, BTN_A = 0x0080, BTN_X = 0x0040, BTN_L = 0x0020, BTN_R = 0x0010 };

class Machine
{
public:
	Machine ();
	~Machine ();
	bool load (const u8 *rom, int size);		// false: not a SNES ROM (or an enhancement chip)
	void reset ();
	void runFrame ();				// one video frame
	void setButtons (int mask) { pad = (u16) mask; }
	void setAudioRate (int hz);
	int  audioRead (short *lr, int maxFrames);	// s16 stereo frames produced so far
	void setSaveRam (const u8 *data, int n);
	void setPal (bool on);				// PAL timing (50 Hz); load() reads it from the header

	u32  fb[W * H];					// the last frame, 0x00RRGGBB
	int  height;					// its lines: 224 (239 with overscan)
	bool pal;					// PAL timing in use
	bool hirom;					// the cartridge's map
	char title[22];					// from the header
	int  chip;					// the header's chip byte ($FFD6): an enhancement chip if >= 3
	enum { SRAM_MAX = 0x20000 };
	u8   sram[SRAM_MAX];				// the cartridge RAM (battery-backed)
	u32  sramSize;
	bool sramDirty;
	u64  now;					// master clock cycles since power-on
	int  frames;

	// ---- (public for the tests) ----
	// CPU
	u16 A, X, Y, S, D, PC; u8 DB, PB, P; bool E;
	bool waiting, stopped;
	u8  wram[0x20000];
	u8  aram[0x10000];				// the sound unit's RAM
	u8  vram[0x10000];

private:
	// ---- cartridge / bus ----
	const u8 *rom; u32 romSize;
	const u8 *rdPage[4096];				// 4 KB pages of the 24-bit space: direct reads
	u8 *wrPage[4096];				// ... direct writes (WRAM)
	u8  pageSpeed[4096];				// master cycles per access
	u8  mdr;					// the open bus
	bool fastRom;					// MEMSEL
	u32 romMirror (u32 a) const;
	void mapPages ();
	u8   read (u32 a);
	void write (u32 a, u8 v);
	u8   ioRead (u32 a);
	void ioWrite (u32 a, u8 v);
	int  ioSpeed (u32 a) const;
	u8   readB (u8 a);				// the B bus ($21xx)
	void writeB (u8 a, u8 v);
	u8   dmaReadA (u32 a);
	void dmaWriteA (u32 a, u8 v);

	// ---- CPU (snes_cpu.cpp) ----
	bool nmiPending, irqLine;
	void cpuStep ();
	void interrupt (u16 vecNative, u16 vecEmu, bool brk);
	u8   fetch () { u8 v = read ((u32) PB << 16 | PC); PC++; return v; }
	u16  fetch16 () { u16 l = fetch (); return (u16) (l | fetch () << 8); }
	void io () { now += 6; }
	void push8 (u8 v);
	u8   pull8 ();
	void pushN (u8 v) { write (S, v); S--; }	// the 65816 ops: S not kept in page 1
	u8   pullN () { S++; return read (S); }
	void fixE () { if (E) { S = (u16) (0x100 | (S & 0xFF)); X &= 0xFF; Y &= 0xFF; P |= 0x30; } }
	void setP (u8 v);

	// ---- the 5A22's registers, DMA, timing (snes.cpp) ----
	u8  nmitimen, wrio, wrmpya, wrdivl, wrdivh; u16 rddiv, rdmpy;
	u16 htime, vtime; bool nmiFlag, timeUp, autoJoyBusy;
	u16 joy1; u16 pad; u16 padShift; bool padStrobe;
	u8  hdmaen;
	u32 wmAddr;					// the WRAM port ($2180-$2183)
	struct Dma { u8 ctrl, bbad, a1b, dasb, ntrl; u16 a1t, das, a2a; bool doTransfer, done; u8 unused; } dma[8];
	void dmaRun (u8 mask);
	void hdmaInit ();
	void hdmaRun ();
	int  line;					// the scanline (0 .. 261 / 311)
	u64  lineStart;					// master clock at its dot 0
	int  lines () const { return pal ? 312 : 262; }
	int  vdisp () const { return overscan ? 240 : 225; }
	int  evStep;					// the next event of this line
	u64  irqAt;					// master clock of the H/V IRQ in this line (or ~0)
	bool frameDone;
	void lineEvent ();
	void computeIrq ();
	u16  hcounter () const { return (u16) ((now - lineStart) >> 2); }

	// ---- PPU (snes_ppu.cpp) ----
	u8  inidisp, obsel, bgmode, mosaic, bgsc[4], bgnba[2], vmain, m7sel;
	u16 bghofs[4], bgvofs[4]; u8 bgLatch, bgLatchH;
	s16 m7a, m7b, m7c, m7d, m7x, m7y, m7hofs, m7vofs; u8 m7Latch;
	u16 vramAddr, vramBuf;
	u16 oamAddr, oamReload; bool oamPrioRot; u8 oamLatch; u8 oam[544];
	u16 cgAddr; u8 cgLatch; u8 cgram[512];
	u8  w12sel, w34sel, wobjsel, wh[4], wbglog, wobjlog, tm, ts, tmw, tsw, cgwsel, cgadsub, setini;
	u16 fixedColor;
	bool overscan;
	u16 hLatch, vLatch; bool hFlip, vFlip, counterLatched;
	u8  stat77;
	u8  ppu1Bus, ppu2Bus;
	u8   ppuRead (u8 r);
	void ppuWrite (u8 r, u8 v);
	u16  vramRemap () const;
	void vramStep (bool high);
	void latchCounters ();
	void renderLine (int y);
	void ppuReset ();
	// the line being made
	u16 bgCol[4][256]; u8 bgPri[4][256];		// a layer's pixels: 15-bit colour, priority (0xFF: none)
	u16 objCol[256]; u8 objPri[256]; bool objMath[256];
	bool winMask[6][256]; bool winValid[6];
	void renderBg (int n, int y, int bpp);
	void renderMode7 (int y);
	void renderObj (int y);
	bool *window (int layer);
	u32  colorLut[16][32];				// brightness x 5-bit component -> 8 bits

	// ---- the sound unit (snes_apu.cpp) ----
	u8  spcA, spcX, spcY, spcS, spcP; u16 spcPC;
	u8  cpuToApu[4], apuToCpu[4];
	u8  spcCtrl, dspAddr, spcTimerTarget[3], spcTimerOut[3]; u16 spcTimerDiv[3]; u8 spcTimerCnt[3]; u8 spcF8, spcF9;
	bool spcSleep;
	u64 spcCycles;					// SPC700 clocks run
	int dspClock;					// SPC clocks to the next DSP sample (32 each)
	void apuSync ();				// the sound unit up to now
	void apuReset ();
	int  spcStep ();
	u8   spcRead (u16 a);
	void spcWrite (u16 a, u8 v);
	u8   spcRd (u16 a) { return a >= 0xF0 && a < 0x100 ? spcRead (a) : (a >= 0xFFC0 && (spcCtrl & 0x80)) ? spcRead (a) : aram[a]; }
	void spcWr (u16 a, u8 v) { if (a >= 0xF0 && a < 0x100) spcWrite (a, v); else aram[a] = v; }
	void spcTimers (int cycles);
	// the S-DSP
	u8  dsp[128];
	struct Voice { int bufPos; s16 buf[24]; int interpPos; u16 brrAddr; int brrOffset; int konDelay; int envMode; int env, hiddenEnv; int out; };
	Voice voice[8];
	u8  konPending, endx; bool everyOther;
	int dspCounter; u16 noise; int echoOffset, echoLength; s16 echoHist[8][2]; int echoHistPos;
	void dspWrite (u8 a, u8 v);
	void dspSample ();
	void decodeBrr (Voice &v);
	void runEnvelope (Voice &v, int n);
	bool counterTick (int rate) const;
	// output: 32 kHz -> the host rate
	int  rate; u32 resAcc;
	s16  lastL, lastR;
	enum { ABUF = 8192 };
	short abuf[ABUF * 2]; int ahead, atail;
	void dspOut (int l, int r);
};

} // namespace snes

#endif
