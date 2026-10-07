//
// gba/gba.h -- a Game Boy Advance emulator core: the ARM7TDMI CPU (ARM + Thumb), the memory
// map with wait states, the PPU (modes 0-5, affine layers, sprites, windows, blending,
// mosaic; a scanline at a time), DMA, timers, the sound (the 4 PSG channels + Direct Sound
// A / B), the BIOS calls done in C++ (no BIOS image needed) and the cartridge saves (SRAM,
// Flash 64 / 128 KB, EEPROM 512 B / 8 KB). Plain portable C++ (no libc): it runs in the
// gbaemu app on Onyx and on a PC for the tests (tools/tests/run_gba_test.sh).
//
//   gba::Machine *m = new gba::Machine;
//   m->load (rom, size);            // the ROM stays the caller's
//   m->setSaveRam (sav, n);         // a save, if any (m->save / m->saveSize)
//   each frame: m->setButtons (b); m->runFrame (); show m->fb (240 x 160, 0x00RRGGBB);
//               m->audioRead (frames, n) (s16 L/R at m->setAudioRate's rate)
//
#ifndef ONYX_GBA_H
#define ONYX_GBA_H

namespace gba {

typedef unsigned char u8; typedef unsigned short u16; typedef unsigned u32; typedef int s32;
typedef unsigned long long u64; typedef long long s64; typedef signed char s8; typedef short s16;

enum { W = 240, H = 160 };
enum { BTN_A = 1, BTN_B = 2, BTN_SELECT = 4, BTN_START = 8, BTN_RIGHT = 16, BTN_LEFT = 32, BTN_UP = 64,
       BTN_DOWN = 128, BTN_R = 256, BTN_L = 512 };
enum { SAVE_NONE, SAVE_SRAM, SAVE_FLASH64, SAVE_FLASH128, SAVE_EEPROM };

class Machine
{
public:
	Machine ();
	~Machine ();
	bool load (const u8 *rom, int size);			// false: not a GBA ROM
	void reset ();
	void runFrame ();					// one video frame (280896 cycles, ~16.74 ms)
	void setButtons (int mask) { keys = mask; }
	void setAudioRate (int hz);
	int  audioRead (short *lr, int maxFrames);		// s16 stereo frames produced so far
	void setSaveRam (const u8 *data, int n);

	u32  fb[W * H];						// the last frame, 0x00RRGGBB
	char title[13];						// from the ROM header
	char code[5];						// the game code (e.g. "BZMP")
	int  saveType;						// SAVE_*
	u8  *save; int saveSize;				// the cartridge's save memory
	bool saveDirty;

private:
	// ---- memory ----------------------------------------------------------------------------
	const u8 *rom; u32 romSize, romMask;
	u8 bios[0x4000], ewram[0x40000], iwram[0x8000], pal[0x400], vram[0x18000], oam[0x400];
	u8 io[0x400];
	u32 biosLatch;						// the last BIOS word read (BIOS protection)
	u32 openBus () const { return (cpsr & 0x20) ? pipe[1] * 0x10001u : pipe[1]; }	// unmapped: the prefetched opcode
	int waitN[16], waitS[16], waitN32[16], waitS32[16];	// access cycles per region (addr >> 24)
	bool prefetch;

	u8  read8 (u32 a);
	u16 read16 (u32 a);
	u32 read32 (u32 a);
	void write8 (u32 a, u8 v);
	void write16 (u32 a, u16 v);
	void write32 (u32 a, u32 v);
	u16 ioRead16 (u32 a);
	void ioWrite16 (u32 a, u16 v);
	void ioWrite8 (u32 a, u8 v);
	void updateWait ();

	// ---- CPU -------------------------------------------------------------------------------
	u32 r[16];
	u32 cpsr, spsr;
	u32 bankR13[6], bankR14[6], bankSpsr[6], bankFiq[5], bankUsr[5];
	bool branched;
	bool halted;
	int  cyc;						// cycles of the instruction being run
	bool irqLine;						// IME && (IE & IF)
	int  intrWait;						// HLE IntrWait: 0 no, 1 first call done
	u32  fetchPC;
	u32  pipe[2]; bool pipeOk;				// the two prefetched opcodes (self-modifying code sees them)
	int modeBank (u32 mode);
	void setMode (u32 mode);
	void setCpsr (u32 v);
	void setPC (u32 v);
	void irq ();
	int  stepArm ();
	int  stepThumb ();
	void armDataProc (u32 op);
	void armMul (u32 op);
	void armMulLong (u32 op);
	void armHalf (u32 op);
	void armSingle (u32 op);
	void armBlock (u32 op);
	void armPsr (u32 op);
	u32  shiftImm (u32 op, bool &c);
	u32  shiftReg (u32 op, bool &c);
	void swi (u32 n);
	void checkIrq () { irqLine = (io[0x208] & 1) && (ieReg () & ifReg ()); }
	u16  ieReg () const { return (u16) (io[0x200] | (io[0x201] << 8)); }
	u16  ifReg () const { return (u16) (io[0x202] | (io[0x203] << 8)); }
	void raise (int bit);

	// ---- time ------------------------------------------------------------------------------
	u64  now;						// cycles since reset
	u64  lineStart;					// the cycle the current line began
	bool inHblank;
	int  vcount;
	bool frameDone;
	bool evDirty;						// a timer changed: the next event moved
	void ppuEvent ();
	u64  nextPpu () const { return lineStart + (inHblank ? 1232 : 960); }

	// timers
	struct Timer { u16 reload, counter; u16 ctl; u64 last; int sub; } tm[4];
	void timersUpdate ();
	u64  timersNext ();
	void timerOverflow (int i);
	u16  timerRead (int i);
	void timerWrite (int i, bool ctl, u16 v);

	// DMA
	struct Dma { u32 src, dst, cnt; u16 ctl; bool on; } dma[4];
	void dmaStart (int timing);				// 1 vblank, 2 hblank, 3 special (sound FIFO)
	void dmaRun (int ch);
	void dmaWrite (int ch, u16 ctl);

	// ---- PPU ---------------------------------------------------------------------------------
	s32 affX[2], affY[2];					// BG2 / BG3 internal reference points
	u16 io16 (u32 off) const { return (u16) (io[off] | (io[off + 1] << 8)); }
	void renderLine ();
	void renderText (int bg, u32 *line);
	void renderAffine (int bg, u32 *line);
	void renderBitmap (int mode, u32 *line);
	void renderObjs (u32 *line, u8 *prio, u8 *winMask);
	void latchAffine ();

	// ---- sound -------------------------------------------------------------------------------
	struct Square { bool on; int len, duty, dutyPos, vol, envPeriod, envTimer, envDir, freq, timer; bool lenOn;
			int sweepPeriod, sweepTimer, sweepShift, sweepDir, shadow; bool sweepOn; };
	struct Wave { bool on; int len, vol, freq, timer, pos; bool lenOn; int sample; };
	struct Noise { bool on; int len, vol, envPeriod, envTimer, envDir, shift, width, div, timer; u32 lfsr; bool lenOn; };
	Square sq[2]; Wave wv; Noise ns;
	u8 waveRam[32];						// the two banks of 16 bytes
	int frameSeq, fsTimer, psgAcc;
	s8 fifo[2][32]; int fifoLen[2], fifoRd[2], fifoWr[2]; int dsOut[2];
	int rate; s64 sampleAcc;
	enum { ABUF = 8192 };
	short abuf[ABUF * 2]; int ahead, atail;
	u64 apuLast;
	void apuRun ();					// the PSG and the samples up to now
	void apuWrite (u32 off, u8 v);
	u8   apuRead (u32 off);
	void trigger (int ch);
	void frameSequencer ();
	void mixSample ();
	void fifoTimer (int t);
	void fifoPush (int ch, u32 v);

	// ---- saves -------------------------------------------------------------------------------
	u8   saveBuf[0x20000];
	int  flashState, flashBank; bool flashId, flashErase, flashWrite, flashBankSel;
	int  eeBits; u64 eeBuf; int eeState, eeAddrBits, eeCmd; u32 eeAddr; int eeReadPos;
	u8   saveRead8 (u32 a);
	void saveWrite8 (u32 a, u8 v);
	u16  eepromRead ();
	void eepromWrite (u16 v);
	void detectSave ();

public:
	u8 debugRead8 (u32 a) { return read8 (a); }
	void debugRegs (u32 *out16, u32 *cpsrOut) const { for (int i = 0; i < 16; i++) out16[i] = r[i]; *cpsrOut = cpsr; }
private:
	int keys;
	u16 keyInput () const { return (u16) (~keys & 0x3FF); }
};

} // namespace gba

#endif
