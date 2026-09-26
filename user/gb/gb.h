//
// gb/gb.h -- a Game Boy / Game Boy Color emulator core: the SM83 CPU, the PPU (DMG and CGB,
// drawn a scanline at a time), the timer, the joypad, OAM / HDMA, the MBC1 / MBC2 / MBC3
// (+ clock) / MBC5 cartridges and the 4-channel APU. Plain portable C++ (no libc): it runs
// in the gbemu app on Onyx and on a PC for the tests (tools/tests/run_gb_test.sh).
//
//   gb::Machine *m = new gb::Machine;
//   m->load (rom, size);            // the ROM stays the caller's; CGB if the header says so
//   m->setSaveRam (sav, n);         // a battery save, if any (m->sram / m->sramSize)
//   each frame: m->setButtons (b); m->runFrame (); show m->fb (160 x 144, 0x00RRGGBB);
//               m->audioRead (frames, n) (s16 L/R at m->setAudioRate's rate)
//
#ifndef ONYX_GB_H
#define ONYX_GB_H

namespace gb {

enum { W = 160, H = 144 };
enum { BTN_RIGHT = 1, BTN_LEFT = 2, BTN_UP = 4, BTN_DOWN = 8, BTN_A = 16, BTN_B = 32, BTN_SELECT = 64, BTN_START = 128 };

class Machine
{
public:
	Machine ();
	~Machine ();
	bool load (const unsigned char *rom, int size);	// false: not a Game Boy ROM
	void reset ();
	void runFrame ();					// one video frame (~16.74 ms of Game Boy time)
	void setButtons (int mask) { buttons = mask; }
	void setAudioRate (int hz);
	int  audioRead (short *lr, int maxFrames);		// s16 stereo frames produced so far
	void setSaveRam (const unsigned char *data, int n);
	void setDmgPalette (const unsigned *four);		// 4 colours, lightest first (DMG games)

	unsigned fb[W * H];					// the last frame, 0x00RRGGBB
	char title[17];						// from the ROM header
	bool cgb;						// running in Game Boy Color mode
	unsigned char *sram; int sramSize;			// cartridge RAM (battery saves)
	bool battery, sramDirty;
	// Serial output (the test ROMs print there): the bytes sent, 0-terminated.
	char serial[256]; int serialLen;

	// ---- the machine ---------------------------------------------------------------------------
private:
	const unsigned char *rom; int romSize, romBanks;
	int mbc, romBank, ramBank, mbc1Mode; bool ramOn;
	int rtcReg[5], rtcLatched[5], rtcSel; bool rtcLatchPrimed; long long rtcCycles;
	unsigned char vram[2][0x2000], wram[8][0x1000], oam[0xA0], hram[0x80], io[0x80];
	unsigned char ie;
	int vbank, wbank;
	// CPU
	unsigned char a, f, b, c, d, e, h, l; unsigned short sp, pc;
	bool ime, halted, haltBug; int eiDelay;
	bool doubleSpeed;
	// timer
	unsigned divCounter;
	// PPU
	int lineDots, ly, winLine; bool frameDone, statLine;
	unsigned char bgPalCgb[64], obPalCgb[64];
	unsigned bgRGB[32], obRGB[32];				// the CGB palettes as 0x00RRGGBB
	unsigned dmgColors[4];
	// HDMA
	bool hdmaActive; int hdmaLen; unsigned short hdmaSrc, hdmaDst;
	int buttons;
	// APU
	struct Square { bool on, dac; int len, duty, dutyPos, vol, envPeriod, envTimer, envDir, freq, timer; bool lenOn;
			int sweepPeriod, sweepTimer, sweepShift, sweepDir, shadow; bool sweepOn; };
	struct Wave { bool on, dac; int len, vol, freq, timer, pos; bool lenOn; unsigned char sample; };
	struct Noise { bool on, dac; int len, vol, envPeriod, envTimer, envDir, shift, width, div, timer; unsigned lfsr; bool lenOn; };
	Square sq[2]; Wave wv; Noise ns;
	int frameSeq, fsTimer;
	int apuPending;						// cycles not yet run by the APU (it runs in batches)
	int rate; long long sampleAcc;				// output sample clock (x rate, in cycles)
	enum { ABUF = 8192 };
	short abuf[ABUF * 2]; int ahead, atail;

	unsigned char read8 (unsigned short addr);
	void write8 (unsigned short addr, unsigned char v);
	unsigned char readIO (int r);
	void writeIO (int r, unsigned char v);
	void writeMBC (unsigned short addr, unsigned char v);
	unsigned char readSram (unsigned short addr);
	void writeSram (unsigned short addr, unsigned char v);
	int step ();
	int haltSkip ();						// one instruction (or an interrupt / halt): its T-cycles
	int cbOp ();
	void tick (int cycles);					// timer, PPU, APU, DMA for T-cycles
	void timerTick (int cycles);
	void ppuTick (int dots);
	void renderLine ();
	void setMode (int m);
	void checkStat ();
	void hdmaBlock ();
	void apuTick (int cycles);
	void apuFlush ();
	void apuWrite (int r, unsigned char v);
	unsigned char apuRead (int r);
	void frameSequencer ();
	void mixSample ();
	void trigger (int ch);
	unsigned cgbColor (const unsigned char *pal, int i);
	void rtcTick (int cycles);
	void irq (int bit) { io[0x0F] |= (unsigned char) (1 << bit); }

	// CPU helpers
	unsigned char fetch () { unsigned char v = read8 (pc); if (haltBug) haltBug = false; else pc++; return v; }
	unsigned short fetch16 () { unsigned char lo = fetch (); return (unsigned short) (lo | (fetch () << 8)); }
	unsigned char getR (int i);
	void setR (int i, unsigned char v);
	unsigned short bc () const { return (unsigned short) ((b << 8) | c); }
	unsigned short de () const { return (unsigned short) ((d << 8) | e); }
	unsigned short hl () const { return (unsigned short) ((h << 8) | l); }
	void setBC (unsigned short v) { b = (unsigned char) (v >> 8); c = (unsigned char) v; }
	void setDE (unsigned short v) { d = (unsigned char) (v >> 8); e = (unsigned char) v; }
	void setHL (unsigned short v) { h = (unsigned char) (v >> 8); l = (unsigned char) v; }
	void push (unsigned short v) { sp--; write8 (sp, (unsigned char) (v >> 8)); sp--; write8 (sp, (unsigned char) v); }
	unsigned short pop () { unsigned char lo = read8 (sp++); return (unsigned short) (lo | (read8 (sp++) << 8)); }
	void alu (int op, unsigned char v);
	unsigned char inc (unsigned char v);
	unsigned char dec (unsigned char v);
	void addHL (unsigned short v);
	unsigned short addSP (unsigned char e8);
	bool cond (int cc);
};

} // namespace gb

#endif
