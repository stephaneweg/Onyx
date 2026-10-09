//
// nds/nds.h -- a Nintendo DS emulator core, written for Onyx: the two processors (the ARM946E-S,
// ARMv5TE with its CP15 and its tightly-coupled memories, and the ARM7TDMI), the memory maps and
// the VRAM banks, the I/O (interrupts, timers, DMA, the IPC between the two, the divider and the
// square root, the keys, the SPI's firmware / touch screen / power, the real-time clock), the
// cartridge (its commands, its save: EEPROM, Flash or FRAM found by use), the BIOS calls done in
// C++ (no BIOS image needed: the "direct boot" loads the game's two programs as the BIOS would),
// the two 2D engines, the 3D engine (geometry + a software rasterizer, which can run on another
// core), the sound (16 channels, PCM / ADPCM / PSG / noise, capture) and a JIT (ARM / Thumb to
// AArch64, nds_jit.cpp). Integer only, no libc: it runs in the ndsemu app on Onyx and on a PC for
// the tests (tools/tests/nds).
//
//   nds::Machine *m = new nds::Machine;
//   m->load (rom, size);              // the ROM stays the caller's; false: not a DS ROM
//   m->setSaveData (sav, n);          // a save, if any (m->save / m->saveSize / m->saveDirty)
//   each frame: m->setButtons (b); m->setTouch (down, x, y); m->runFrame ();
//               show m->screen[0] (the top) and m->screen[1] (the bottom), 256 x 192 each, 0x00RRGGBB;
//               m->audioRead (frames, n) (s16 L/R at m->setAudioRate's rate)
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef ONYX_NDS_H
#define ONYX_NDS_H

namespace nds {

typedef unsigned char u8; typedef unsigned short u16; typedef unsigned u32; typedef int s32;
typedef unsigned long long u64; typedef long long s64; typedef signed char s8; typedef short s16;

enum { W = 256, H = 192 };
enum { BTN_A = 1, BTN_B = 2, BTN_SELECT = 4, BTN_START = 8, BTN_RIGHT = 16, BTN_LEFT = 32, BTN_UP = 64,
       BTN_DOWN = 128, BTN_R = 256, BTN_L = 512, BTN_X = 1024, BTN_Y = 2048, BTN_LID = 4096 };
enum { SAVE_UNKNOWN, SAVE_NONE, SAVE_EEPROM512, SAVE_EEPROM, SAVE_EEPROM3, SAVE_FLASH };

// The master clock: the ARM9's (67.03 MHz); the bus (the ARM7, the timers, the video) runs at half.
enum { LINE_CYCLES = 4260, HBLANK_CYCLES = 3072, LINES = 263, FRAME_CYCLES = LINE_CYCLES * LINES };
static const u32 ARM7_HZ = 33513982;

class Machine;
class Jit;

typedef u32 __attribute__ ((may_alias)) ua32;
typedef u16 __attribute__ ((may_alias)) ua16;

inline void mcopy (void *d, const void *s, u32 n) { u8 *a = (u8 *) d; const u8 *b = (const u8 *) s; while (n--) *a++ = *b++; }
inline void mfill (void *d, u8 v, u32 n) { u8 *a = (u8 *) d; while (n--) *a++ = v; }
inline u16 rd16 (const u8 *p) { return (u16) (p[0] | (p[1] << 8)); }
inline u32 rd32 (const u8 *p) { return (u32) p[0] | ((u32) p[1] << 8) | ((u32) p[2] << 16) | ((u32) p[3] << 24); }
inline void wr16 (u8 *p, u16 v) { p[0] = (u8) v; p[1] = (u8) (v >> 8); }
inline void wr32 (u8 *p, u32 v) { p[0] = (u8) v; p[1] = (u8) (v >> 8); p[2] = (u8) (v >> 16); p[3] = (u8) (v >> 24); }

// ---- a processor -----------------------------------------------------------------------------------------
// r[15] holds the address of the instruction being run + 8 (ARM) / + 4 (Thumb) while it runs; between
// two, the next one's address. ts: the processor's time in master cycles.
struct Arm
{
	u32 r[16];
	u32 cpsr, spsr;
	u32 bankR13[6], bankR14[6], bankSpsr[6], bankFiq[5], bankUsr[5];
	s64 ts;					// its time (master cycles)
	s64 target;				// run until
	Machine *m;
	int num;				// 0 the ARM9, 1 the ARM7
	int shift;				// master cycles a cycle of its own: 0 ARM9, 1 ARM7
	bool halted;
	bool branched;
	int  cyc;				// the cycles of the instruction being run (its own)
	bool intrWait;				// an IntrWait / VBlankIntrWait being waited (BIOS, nds_bios.cpp)
	u32  intrMask;
	// the ARM9's CP15
	u32 cpCtl, cpDtcm, cpItcm;
	u32 dtcmBase, dtcmSize, itcmSize;	// (size 0: off)
	u32 cpRegs[64];				// the protection unit's settings and the rest: kept, unused
	u32 excBase;				// 0xFFFF0000 or 0
	// the JIT's
	bool jitOn;
#ifdef NDS_DEBUG
	u32 hist[4096]; unsigned histN;			// the last instructions' addresses
#endif

	void reset (Machine *mm, int n);
	int  modeBank (u32 m) const;
	void setMode (u32 mode);
	void setCpsr (u32 v);
	void setPC (u32 v);			// a jump (the T bit unchanged)
	void jumpX (u32 v);			// a jump that can change the state (BX: bit 0)
	void irq ();
	void undefined ();
	int  stepArm ();
	int  stepThumb ();
	void execArm (u32 op);			// one ARM instruction, r[15] = its address + 8 (the JIT's fallback)
	void execThumb (u32 op);		// one Thumb instruction, r[15] = its address + 4
	void run ();				// the interpreter, until ts >= target or halted
	bool cond (u32 c) const;
	// memory, by processor
	u32  read32 (u32 a);
	u32  read16 (u32 a);
	u32  read8 (u32 a);
	void write32 (u32 a, u32 v);
	void write16 (u32 a, u32 v);
	void write8 (u32 a, u32 v);
	u32  fetch32 (u32 a);
	u32  fetch16 (u32 a);
	// instruction groups (nds_cpu.cpp)
	u32  shiftImm (u32 op, bool &c);
	u32  shiftReg (u32 op, bool &c);
	void armDataProc (u32 op);
	void armMul (u32 op);
	void armMulLong (u32 op);
	void armHalf (u32 op);
	void armSingle (u32 op);
	void armBlock (u32 op);
	void armPsr (u32 op);
	void armMisc (u32 op);			// the ARMv5 additions in the multiply / misc space
	void armCop (u32 op);
	void cp15Write (int cn, int cm, int op2, u32 v);
	u32  cp15Read (int cn, int cm, int op2);
	void updateTcm ();
};

// ---- a 2D engine -----------------------------------------------------------------------------------------
struct Gpu2D
{
	Machine *m;
	int num;					// 0 engine A, 1 engine B
	u8  reg[0x70];					// 0x04000000 / 0x04001000 .. +0x6F, as written
	u32 dispcnt;
	s32 affX[2], affY[2];				// BG2 / BG3 internal reference points
	u16 masterBright;
	u32 line[W];					// the line being made (0x00RRGGBB)
	// the line's layers: colour (15-bit + flags), the second-best colour for blending
	u32 bgLine[4][W + 8];				// per BG: bit 31 = transparent, bit 0-14 colour, bit 16 = 3D (alpha in bit 17-21)
	u32 objLine[W];					// bit 31 transparent, 0-14 colour, 16-17 prio, 18 semi-transparent, 19 bitmap obj, 20-24 bitmap alpha
	u8  objWin[W];
	bool mosaicDone;

	void reset (Machine *mm, int n);
	void write8 (u32 off, u8 v);
	u8   read8 (u32 off) const;
	void renderLine (int y, u32 *out);		// out: 256 pixels 0x00RRGGBB
	void vblank ();					// the reference points reloaded
	// helpers
	u16  r16 (u32 off) const { return (u16) (reg[off] | (reg[off + 1] << 8)); }
	u8  *bgVram (u32 off);				// the BG / OBJ VRAM of this engine (16 KB pages)
	u8  *objVram (u32 off);
	u16  bgExtPal (int slot, int idx);
	u16  objExtPal (int idx);
	void renderText (int bg, int y);
	void renderAffine (int bg, int y);
	void renderExtended (int bg, int y);
	void renderLarge (int bg, int y);
	void render3D (int y);
	void renderObjs (int y);
	void compose (int y, u32 *out);
	u32  windowMask (int x, int y) const;
};

// ---- the 3D engine ---------------------------------------------------------------------------------------
struct Vertex
{
	s32 x, y, z, w;					// clip coordinates (20.12-ish)
	s32 cr, cg, cb;					// colour, 9.? (0..0x1FF)
	s32 s, t;					// texture coordinates, 12.4
	// after the viewport
	s32 sx, sy;					// screen position (sx 0..256, sy 0..192)
	s32 sz;						// depth (24-bit: z or w)
	s32 sw;						// w (for the perspective)
	bool clipped;
};
struct Polygon
{
	Vertex *v[10];
	int nv;
	u32 attr, texParam, palBase;
	bool translucent, facingFront, wBuffer;
	s32 ymin, ymax;					// screen rows covered
	s32 sortKey;
	int index;
};

struct Gpu3D
{
	Machine *m;
	// the command FIFO (the commands waiting: after a SWAP_BUFFERS, until the next VBlank)
	enum { FIFO_SIZE = 1 << 16 };
	u32 *fifoCmd;					// cmd | (number of params << 8)
	u32 *fifoPar;
	u32 fifoHead, fifoTail;				// (counts of entries)
	// the command being received through 0x04000400 (packed)
	u32 packedCmds; int packedLeft, paramsLeft, curCmd, nParam; u32 params[32];
	// direct port command being gathered
	int  portCmd, portCount; u32 portParams[32];
	u32 parHead, parTail;
	bool swapPending; u32 swapParam;
	bool polyFront;
	bool busy;
	// matrices (4x4, 20.12, row-major as the DS: m[row*4+col], vectors are rows: v' = v x M)
	s32 proj[16], pos[16], vec[16], tex[16], clip[16];
	s32 projStack[16], posStack[32][16], vecStack[32][16], texStack[16];
	int projSp, posSp, texSp;
	int mtxMode;
	bool clipDirty;
	bool stackError;
	// vertex state
	s32 vx, vy, vz;					// the last vertex (for the relative forms)
	s32 cr, cg, cb;					// the vertex colour (0..31 scaled x 0x200 / 32? kept 0..31 <<4)
	s32 ts, tt;					// texture coordinate (raw, 12.4)
	s32 tsOut, ttOut;
	s32 nx, ny, nz;
	u32 polyAttr, polyAttrPending, texParam, palBase;
	u32 difAmb, speEmi;
	s32 lightVec[4][3], halfVec[4][3]; u32 lightColor[4];
	u8  shininess[128];
	bool useShininess;
	int  primType; bool inBegin; int vtxInPrim;
	Vertex primV[4]; int primCount;			// the vertices of the polygon being made
	bool stripOdd;
	Vertex *lastStrip[2];				// the shared vertices of a strip (in vertex RAM)
	s32 vpX1, vpY1, vpX2, vpY2;
	// results
	u32 posResult[4], vecResult[3];
	bool boxResult;
	// vertex / polygon RAM: two sets (one drawn, one being filled)
	enum { MAX_VERTS = 2048 * 10, MAX_POLYS = 2048 };
	Vertex *vram[2]; Polygon *pram[2];
	int nVerts[2], nPolys[2];
	int cur;					// the set being filled
	bool overflow;
	u32 dispcnt3d;
	// rendering registers (latched at the swap into the drawn set's copy)
	u8  regs[0x80];					// 0x04000330 .. 0x040003AF
	u16 edgeColor[8]; u8 alphaRef; u32 clearColor; u16 clearDepth, clearOffset; u32 fogColor; u16 fogOffset;
	u8  fogTable[32]; u16 toonTable[32];
	int  rdSet; u32 rdDispcnt, rdSwap;		// what the rasterizer draws
	u8  rdRegs[0x80];

	void reset (Machine *mm);
	void write32 (u32 a, u32 v);			// 0x04000400 .. 0x040005FF and the registers
	void write16 (u32 a, u16 v);
	void write8 (u32 a, u8 v);
	u32  read32 (u32 a);
	void cmdPush (int cmd, const u32 *p, int n);
	void exec (int cmd, const u32 *p);
	void drain ();					// run the commands queued, until a swap
	void vblank ();					// a swap pending: done now
	int  fifoCount () const { return (int) (fifoHead - fifoTail); }
	u32  gxstat ();
	void updateClip ();
	void mtxLoad (s32 *d, const s32 *s);
	void mtxMult (s32 *d, const s32 *s, int rows, int cols);
	void addVertex (s32 x, s32 y, s32 z);
	void lighting ();
	void emitPolygon ();
	void boxTest (const u32 *p);
	void texCoordTransform (int mode);
};

struct Render3D
{
	Machine *m;
	u32 color[W * H];				// 0x00RRGGBB | alpha (0..31) << 24, bit 30 = drawn
	u32 depth[W * H];
	u32 attr[W * H];				// polygon id (bits 24-29), opaque id, edge, fog flags
	u8  stencil[W * H];
	volatile int linesDone;				// rows ready (0..192)
	void reset (Machine *mm);
	void renderFrame ();				// the whole frame (the set Gpu3D says)
	void drawPolygon (const Polygon *p);
	void finish ();					// edges, fog, anti-aliasing
	u32  sampleTex (const Polygon *p, s32 s, s32 t, u32 vcolor);
};

// ---- the sound -------------------------------------------------------------------------------------------
struct Spu
{
	Machine *m;
	struct Chan
	{
		u32 cnt, sad; u16 tmr, pnt; u32 len;
		bool on;
		u32 pos;					// in samples (from sad)
		u32 timer;					// 16-bit count up (fraction kept in a wider count)
		s32 sample;
		// ADPCM
		s32 adpcmVal, adpcmIdx, loopVal, loopIdx; bool adpcmLoopSaved;
		u32 lfsr; int psgPos;
		u32 fifo[8]; int fifoN;
	} ch[16];
	struct Cap { u8 cnt; u32 dad; u16 len; u32 pos; u32 timer; } cap[2];
	u16 soundcnt, bias;
	s64 last;					// master cycles done up to
	int rate; s64 acc;				// output rate, the 32768 Hz -> rate step
	s32 prevL, prevR, curL, curR;
	enum { ABUF = 16384 };
	short abuf[ABUF * 2]; int ahead, atail;
	void reset (Machine *mm);
	void run (s64 now);
	void sample ();					// one 32768 Hz sample
	void write8 (u32 a, u8 v);
	void write16 (u32 a, u16 v);
	void write32 (u32 a, u32 v);
	u32  read32 (u32 a);
	void start (int c);
};

// ---- the cartridge ---------------------------------------------------------------------------------------
struct Cart
{
	const u8 *rom; u32 romSize, romMask;
	u32 chipId;
	u8  cmd[8];
	u32 romctrl; u16 spicnt;
	u32 xferAddr, xferLeft, xferPos; int xferCmd;
	u32 dataLatch; bool dataReady;
	// the save chip
	u8 *save; u32 saveSize; int saveType; bool saveDirty;
	int spiState, spiCmd, spiAddrBytes, spiAddrGot; u32 spiAddr; bool spiWel; u8 spiStatus;
	u8  detectBuf[300]; int detectLen; int detectCmd;	// (the first write, to find the chip)
	u8  spiOut;
	void reset ();
	u8   spiXfer (u8 v, bool hold);
	void spiEnd ();
	void detect ();
	void setType (int t, u32 size);
};

// ---- the machine -----------------------------------------------------------------------------------------
class Machine
{
public:
	Machine ();
	~Machine ();
	bool load (const u8 *rom, u32 size);		// false: not a DS ROM
	void setBios7 (const u8 *b, u32 n);		// optional (only its KEY1 table: an encrypted secure area)
	void reset ();
	void runFrame ();				// up to the next VBlank (560190 master cycles, 59.83 Hz)
	void setButtons (int mask) { keys = mask; }
	void setTouch (bool down, int x, int y) { touchDown = down; touchX = x; touchY = y; }
	void setAudioRate (int hz);
	int  audioRead (short *lr, int maxFrames);
	void setSaveData (const u8 *data, u32 n);
	void setTime (int year, int mon, int day, int hour, int min, int sec);	// the RTC (year 2000..2099)
	void setUser (const char *name, int lang, int birthMonth, int birthDay, int color);	// the firmware's settings

	u32  screen[2][W * H];				// [0] top, [1] bottom: 0x00RRGGBB
	char title[13];					// from the header
	char code[5];					// the game code ("AMCE")
	u8   icon[32 * 32 * 4];				// the banner's icon, RGBA (if any)
	bool hasIcon;
	u8  *save; u32 saveSize; bool saveDirty;	// the cartridge's save memory (cart.save)
	int  saveType () const { return cart.saveType; }
	u64  frames;

	// --- the 3D renderer's helper core (the app may run Render3D on another core) ---
	void (*render3dKick) (void *) ;			// 0: rendered inline at the VBlank; else called to start it
	void *render3dCtx;
	void (*render3dWait) (void *, int line);	// waits until linesDone > line
	Render3D r3d;
	void render3dNow () { r3d.renderFrame (); }

	// --- the JIT ---
	Jit *jit;
	bool jitEnable (void *(*codeAlloc) (u32 size));	// false: none (no memory)
	bool useJit;

	// statistics
	u64  cyclesRun;
	char lastError[96];

// ------------------------------------------------------------------------------------------------------
// (public for the processors, the JIT and the tests)
	Arm  arm9, arm7;
	Gpu2D gpuA, gpuB;
	Gpu3D gpu3d;
	Spu  spu;
	Cart cart;
	const u8 *bios7Key; u8 bios7Copy[0x1048];

	// memories
	u8 *mainRam;					// 4 MB
	u8 wram[0x8000];				// shared, 32 KB
	u8 wram7[0x10000];				// the ARM7's, 64 KB
	u8 itcm[0x8000], dtcm[0x4000];
	u8 pal[0x800], oam[0x800];
	u8 *vram;					// the banks A..I (656 KB)
	u8 bios9[0x1000], bios7[0x4000];		// ours (the interrupt entries, nds_bios.cpp)
	u8 firmware[0x40000];				// made up (nds_bios.cpp)
	u8 wifiRam[0x2000]; u8 wifiReg[0x1000];
	enum { BANK_A, BANK_B, BANK_C, BANK_D, BANK_E, BANK_F, BANK_G, BANK_H, BANK_I };
	static const u32 bankOff[9], bankSize[9];
	u8 vramcnt[9];
	u8 wramcnt;
	u8 vramstat;
	// the VRAM's 16 KB pages: the mask of the banks mapped at each, and the first one's memory
	u16 mapABG[32], mapBBG[8], mapAOBJ[16], mapBOBJ[8], mapLCDC[41], mapARM7[16];
	u8 *ptrABG[32], *ptrBBG[8], *ptrAOBJ[16], *ptrBOBJ[8], *ptrARM7[16];
	u16 mapTex[8];  u8 *ptrTex[8];			// 4 texture slots of 128 KB (16 KB pages: 32... see vramMap)
	u8 *ptrTexPage[32];				// texture image: 512 KB in 16 KB pages
	u8 *ptrTexPal[8];				// texture palettes: 96 KB in 16 KB pages (6 used)
	u8 *ptrABGExt[4], *ptrBBGExt[4], *ptrAOBJExt, *ptrBOBJExt;	// 8 KB slots
	u8 zero16k[0x4000];
	void vramMap ();
	u8  *vramPtr (u32 a);				// the ARM9's view, 0 unmapped
	u8  *vramPtr7 (u32 a);
	u32  vramRead16 (u32 a);
	void vramWrite16 (u32 a, u16 v);
	void vramWrite8 (u32 a, u8 v);
	void vramWrite32 (u32 a, u32 v);

	// the fast memory tables (16 KB pages; 0: through the functions) -- the interpreter and the JIT
	enum { PAGE_BITS = 14, NPAGES = 1 << 18 };
	u8 **rdPage[2], **wrPage[2];
	void pagesUpdate ();				// after a map changes (WRAMCNT, VRAMCNT, TCMs)
	bool pagesDirty;

	// buses (nds_mem.cpp)
	u32  a9Read32 (u32 a); u32 a9Read16 (u32 a); u32 a9Read8 (u32 a);
	void a9Write32 (u32 a, u32 v); void a9Write16 (u32 a, u32 v); void a9Write8 (u32 a, u32 v);
	u32  a7Read32 (u32 a); u32 a7Read16 (u32 a); u32 a7Read8 (u32 a);
	void a7Write32 (u32 a, u32 v); void a7Write16 (u32 a, u32 v); void a7Write8 (u32 a, u32 v);
	u32  bus9Read32 (u32 a); u32 bus9Read16 (u32 a); u32 bus9Read8 (u32 a);	// (no TCM: DMA)
	void bus9Write32 (u32 a, u32 v); void bus9Write16 (u32 a, u32 v); void bus9Write8 (u32 a, u32 v);
	u32  busRead32 (int cpu, u32 a) { return cpu ? a7Read32 (a) : bus9Read32 (a); }
	u16  busRead16 (int cpu, u32 a) { return (u16) (cpu ? a7Read16 (a) : bus9Read16 (a)); }
	void busWrite32 (int cpu, u32 a, u32 v) { if (cpu) a7Write32 (a, v); else bus9Write32 (a, v); }
	void busWrite16 (int cpu, u32 a, u16 v) { if (cpu) a7Write16 (a, v); else bus9Write16 (a, v); }
	u32  io9Read32 (u32 a); u32 io9Read16 (u32 a); u32 io9Read8 (u32 a);
	void io9Write32 (u32 a, u32 v); void io9Write16 (u32 a, u32 v); void io9Write8 (u32 a, u32 v);
	u32  io7Read32 (u32 a); u32 io7Read16 (u32 a); u32 io7Read8 (u32 a);
	void io7Write32 (u32 a, u32 v); void io7Write16 (u32 a, u32 v); void io7Write8 (u32 a, u32 v);
	void codeWritten (u32 a, int cpu);		// a write where the JIT has code

	// interrupts
	u32 ie[2], iflag[2]; u8 ime[2];
	void raise (int cpu, int bit);
	void checkIrq (int cpu);
	bool irqLine (int cpu) const { return ime[cpu] && (ie[cpu] & iflag[cpu]); }
	bool wake (int cpu) const { return (ie[cpu] & iflag[cpu]) != 0; }

	// time (master cycles)
	s64 now;					// the slice's start
	s64 curTime ();					// the running processor's time
	int  running;					// -1 none, 0 ARM9, 1 ARM7
	enum { EV_HBLANK, EV_LINE, EV_TIMER, EV_SPU = EV_TIMER + 8, EV_CART9, EV_CART7, EV_SPI, EV_COUNT };
	s64 evTime[EV_COUNT]; s64 nextEv;
	void schedule (int ev, s64 t) { evTime[ev] = t; if (t < nextEv) nextEv = t; }
	void cancel (int ev) { evTime[ev] = 0x7FFFFFFFFFFFFFFFll; }
	void runEvents ();
	void computeNext ();
	bool frameDone;
	int  vcount; s64 lineStart; bool inHblank;
	u16  dispstat[2];
	u16  powcnt1, powcnt2;
	void hblank ();
	void lineEnd ();

	// timers (4 per processor, counted in bus cycles)
	struct Timer { u16 reload, counter; u16 ctl; s64 last; } tm[2][4];
	void timerUpdate (int cpu, int i);
	u16  timerRead (int cpu, int i);
	void timerWrite (int cpu, int i, bool ctl, u16 v);
	void timerSchedule (int cpu, int i);
	void timerOverflow (int cpu, int i);

	// DMA (4 per processor)
	struct Dma { u32 sad, dad, cnt, src, dst, count; bool on; u32 fill; } dma[2][4];
	u32 dmaFill[4];
	void dmaWrite (int cpu, int ch, u32 cnt);
	void dmaStart (int cpu, int timing);		// ARM9: 1 vblank 2 hblank 3 display 4 main memory display 5 cart 7 GX FIFO; ARM7: 1 vblank 2 cart
	void dmaRun (int cpu, int ch);
	int  dmaTiming (int cpu, int ch) const;

	// IPC
	u16 ipcSync[2];
	u32 fifo[2][16]; int fifoN[2], fifoR[2];	// [cpu]: the FIFO cpu sends into
	u16 fifoCnt[2]; u32 fifoLast[2];
	u32  ipcRecv (int cpu);
	void ipcSend (int cpu, u32 v);
	void ipcFifoCnt (int cpu, u16 v);
	u16  ipcFifoCntRead (int cpu);

	// divider / square root
	u16 divcnt; s64 divNum, divDen, divRes, divRem; u16 sqrtcnt; u64 sqrtParam; u32 sqrtRes;
	void divide (); void squareRoot ();

	// keys, misc
	int keys; bool touchDown; int touchX, touchY;
	u16 keycnt[2];
	u16 exmemcnt; u8 postflg[2]; u16 rcnt;
	u16 haltcnt;
	u32 biosProt;
	u8  wifiWait[2];

	// SPI (ARM7): power manager, firmware, touch screen
	u16 spicnt; u8 spiData;
	int fwState, fwCmd, fwAddrGot; u32 fwAddr; u8 fwStatus;
	u8  pmIndex; bool pmGotIndex; u8 pmRegs[8];
	u16 tscValue; int tscPos;
	void spiWrite (u8 v);
	void touchCalibrate (int sx, int sy, u16 &ax, u16 &ay);
	void firmwareMake ();
	char userName[11]; int userLang, userBMonth, userBDay, userColor;

	// RTC (ARM7, 0x04000138)
	u16 rtcIo; int rtcBitIn, rtcPos, rtcCmd; u8 rtcIn, rtcOut[8]; int rtcOutPos, rtcOutBit;
	u8 rtcStat1, rtcStat2;
	int rtcYear, rtcMon, rtcDay, rtcHour, rtcMin, rtcSec, rtcDow; s64 rtcTicks;
	void rtcWrite (u16 v);
	void rtcByte (u8 v);
	void rtcTick ();

	// cartridge I/O (whichever processor owns the slot)
	void cartRomCtrl (int cpu, u32 v);
	u32  cartReadData (int cpu);
	void cartSpiCnt (u16 v);
	void cartSpiData (u8 v);
	void cartDone (int cpu);
	void decryptSecureArea (u8 *arm9);

	// BIOS (nds_bios.cpp)
	void biosMake ();
	void swi (Arm &c, u32 n);
	bool directBoot ();
	u32  callGuest (Arm &c, u32 fn, u32 a0, u32 a1, u32 a2);
	u8  *romCopy;					// (only when the secure area had to be decrypted)

	// the displays
	u32 dispcapcnt;
	void captureLine (int y, const u32 *lineA);
	void displayLine (int y);
	u16 dispFifo[16]; u32 dispFifoN;

	// halts, the run
	void runSlice (s64 target);
	void runCpu (Arm &c);

	// sound buffer exposure
	friend struct Arm;
};

// The JIT (nds_jit.cpp): one per machine, both processors.
bool jitAvailable ();
void jitRun (Machine *m, Arm &c);			// runs c until c.ts >= c.target or halted
void jitInvalidate (Machine *m, int cpu, u32 addr);	// code at addr's page changed
void jitFlushAll (Machine *m);

} // namespace nds

#endif
