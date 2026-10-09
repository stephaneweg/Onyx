//
// nds/nds.cpp -- the machine: its memories, the reset and the direct boot, the run of a frame (the
// two processors in turns of a few hundred cycles, the ARM7 at half the ARM9's clock, the events
// between: the lines, the timers), the video timing (263 lines, HBlank at 1536 bus cycles, VBlank
// at 192), the header and the icon, the sound read out, the save given and taken.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see nds.h).
//
#include "nds/nds.h"

namespace nds {

enum { SLICE = 256 };					// master cycles a processor runs before the other

Machine::Machine ()
{
	mainRam = new u8[0x400000];
	vram = new u8[0xA4000];
	for (int c = 0; c < 2; c++) { rdPage[c] = new u8 *[NPAGES]; wrPage[c] = new u8 *[NPAGES]; }
	gpu3d.fifoCmd = new u32[Gpu3D::FIFO_SIZE];
	gpu3d.fifoPar = new u32[Gpu3D::FIFO_SIZE * 2];
	for (int i = 0; i < 2; i++) { gpu3d.vram[i] = new Vertex[Gpu3D::MAX_VERTS]; gpu3d.pram[i] = new Polygon[Gpu3D::MAX_POLYS]; }
	cart.rom = 0; cart.romSize = 0; cart.romMask = 0; cart.save = 0; cart.saveSize = 0; cart.saveType = SAVE_UNKNOWN; cart.saveDirty = false;
	render3dKick = 0; render3dCtx = 0; render3dWait = 0;
	jit = 0; useJit = false;
	bios7Key = 0; romCopy = 0;
	title[0] = 0; code[0] = 0; hasIcon = false;
	save = 0; saveSize = 0; saveDirty = false;
	keys = 0; touchDown = false; touchX = touchY = 0;
	const char *nm = "Onyx"; int i = 0; for (; nm[i]; i++) userName[i] = nm[i]; userName[i] = 0;
	userLang = 1; userBMonth = 1; userBDay = 1; userColor = 4;
	rtcYear = 2026; rtcMon = 1; rtcDay = 1; rtcHour = 12; rtcMin = 0; rtcSec = 0; rtcDow = 4;
	spu.rate = 48000;
	lastError[0] = 0;
	frames = 0; cyclesRun = 0;
	mfill (zero16k, 0, sizeof zero16k);
}

Machine::~Machine ()
{
	delete [] mainRam; delete [] vram;
	for (int c = 0; c < 2; c++) { delete [] rdPage[c]; delete [] wrPage[c]; }
	delete [] gpu3d.fifoCmd; delete [] gpu3d.fifoPar;
	for (int i = 0; i < 2; i++) { delete [] gpu3d.vram[i]; delete [] gpu3d.pram[i]; }
	delete [] cart.save;
	delete [] romCopy;
}

void Machine::setBios7 (const u8 *b, u32 n)
{
	if (n < 0x30 + 0x1048) return;
	mcopy (bios7Copy, b + 0x30, 0x1048);
	bios7Key = bios7Copy;
}

void Machine::setUser (const char *name, int lang, int birthMonth, int birthDay, int color)
{
	int i = 0; for (; name && name[i] && i < 10; i++) userName[i] = name[i]; userName[i] = 0;
	userLang = lang; userBMonth = birthMonth; userBDay = birthDay; userColor = color;
}

void Machine::setTime (int year, int mon, int day, int hour, int min, int sec)
{
	rtcYear = year; rtcMon = mon; rtcDay = day; rtcHour = hour; rtcMin = min; rtcSec = sec;
	// the day of the week (Zeller-like: 2000-01-01 was a Saturday)
	int y = year, mo = mon;
	if (mo < 3) { mo += 12; y--; }
	int k = y % 100, j = y / 100;
	int h = (day + 13 * (mo + 1) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;	// 0 Saturday
	rtcDow = (h + 6) % 7;					// 0 Sunday
}

bool Machine::load (const u8 *rom, u32 size)
{
	if (size < 0x200) return false;
	u32 entry9 = rd32 (rom + 0x24), ram9 = rd32 (rom + 0x28), off9 = rd32 (rom + 0x20), size9 = rd32 (rom + 0x2C);
	if ((ram9 >> 24) != 0x02 || (entry9 >> 24) != 0x02 || off9 >= size || !size9) return false;
	cart.rom = rom; cart.romSize = size;
	u32 p = 1; while (p < size) p <<= 1;
	cart.romMask = p - 1;
	u32 mb = p >> 20; if (!mb) mb = 1;
	cart.chipId = 0x000000C2 | ((mb - 1) << 8);
	int i = 0;
	for (; i < 12; i++) { char c = (char) rom[i]; if (c < 32 || c >= 127) break; title[i] = c; }
	title[i] = 0;
	for (i = 0; i < 4; i++) { char c = (char) rom[0x0C + i]; code[i] = (c >= 32 && c < 127) ? c : '?'; }
	code[4] = 0;
	// the banner's icon: 32 x 32, 4 bits a pixel in 8 x 8 tiles, 16 colours (0 transparent)
	hasIcon = false;
	u32 ban = rd32 (rom + 0x68);
	if (ban && ban + 0x240 <= size)
	{
		const u8 *bits = rom + ban + 0x20, *pal = rom + ban + 0x220;
		for (int y = 0; y < 32; y++)
			for (int x = 0; x < 32; x++)
			{
				int tile = (y / 8) * 4 + x / 8;
				u8 b = bits[tile * 32 + (y & 7) * 4 + (x & 7) / 2];
				int c = (x & 1) ? b >> 4 : b & 15;
				u16 col = rd16 (pal + c * 2);
				u8 *o = icon + (y * 32 + x) * 4;
				o[0] = (u8) ((col & 31) * 255 / 31); o[1] = (u8) (((col >> 5) & 31) * 255 / 31); o[2] = (u8) (((col >> 10) & 31) * 255 / 31);
				o[3] = c ? 255 : 0;
			}
		hasIcon = true;
	}
	reset ();
	return true;
}

void Machine::setSaveData (const u8 *data, u32 n)
{
	int t;
	if (n == 512) t = SAVE_EEPROM512;
	else if (n <= 0x10000) t = SAVE_EEPROM;
	else if (n == 0x20000) t = SAVE_EEPROM3;
	else t = SAVE_FLASH;
	cart.setType (t, n);
	for (u32 i = 0; i < n; i++) cart.save[i] = data[i];
	cart.saveDirty = false;
	save = cart.save; saveSize = cart.saveSize; saveDirty = false;
}

void Machine::reset ()
{
	mfill (mainRam, 0, 0x400000);
	mfill (wram, 0, sizeof wram); mfill (wram7, 0, sizeof wram7);
	mfill (itcm, 0, sizeof itcm); mfill (dtcm, 0, sizeof dtcm);
	mfill (pal, 0, sizeof pal); mfill (oam, 0, sizeof oam);
	mfill (vram, 0, 0xA4000);
	mfill (wifiRam, 0, sizeof wifiRam); mfill (wifiReg, 0, sizeof wifiReg);
	for (int i = 0; i < 9; i++) vramcnt[i] = 0;
	wramcnt = 0;
	vramMap ();
	arm9.reset (this, 0);
	arm7.reset (this, 1);
	gpuA.reset (this, 0); gpuB.reset (this, 1);
	gpu3d.reset (this);
	r3d.reset (this);
	spu.reset (this);
	cart.reset ();
	for (int c = 0; c < 2; c++)
	{
		ie[c] = iflag[c] = 0; ime[c] = 0;
		dispstat[c] = 0; ipcSync[c] = 0; fifoN[c] = fifoR[c] = 0; fifoCnt[c] = 0x0101; fifoLast[c] = 0;
		keycnt[c] = 0; postflg[c] = 0;
		for (int i = 0; i < 4; i++) { tm[c][i].reload = tm[c][i].counter = tm[c][i].ctl = 0; tm[c][i].last = 0; Dma &d = dma[c][i]; d.sad = d.dad = d.cnt = d.src = d.dst = d.count = 0; d.on = false; }
	}
	for (int i = 0; i < 4; i++) dmaFill[i] = 0;
	divcnt = 0; divNum = divDen = divRes = divRem = 0; sqrtcnt = 0; sqrtParam = 0; sqrtRes = 0;
	exmemcnt = 0; rcnt = 0; haltcnt = 0; biosProt = 0; wifiWait[0] = wifiWait[1] = 0;
	powcnt1 = 0; powcnt2 = 0;
	spicnt = 0; spiData = 0; fwState = 0; fwCmd = 0; fwAddrGot = 0; fwAddr = 0; fwStatus = 0;
	pmIndex = 0; pmGotIndex = false; for (int i = 0; i < 8; i++) pmRegs[i] = 0;
	pmRegs[0] = 0x0C; pmRegs[4] = 3;
	tscValue = 0; tscPos = 0;
	rtcIo = 0; rtcBitIn = 0; rtcPos = 0; rtcCmd = 0; rtcIn = 0; rtcOutPos = rtcOutBit = 0; rtcStat1 = 0; rtcStat2 = 0; rtcTicks = 0;
	dispcapcnt = 0; dispFifoN = 0;
	now = 0; running = -1;
	for (int i = 0; i < EV_COUNT; i++) evTime[i] = 0x7FFFFFFFFFFFFFFFll;
	nextEv = 0x7FFFFFFFFFFFFFFFll;
	vcount = 0; lineStart = 0; inHblank = false; frameDone = false;
	schedule (EV_HBLANK, HBLANK_CYCLES);
	schedule (EV_LINE, LINE_CYCLES);
	pagesDirty = true;
	biosMake ();
	firmwareMake ();
	if (cart.rom) directBoot ();
	pagesUpdate ();
	if (jit) jitFlushAll (this);
}

void Machine::computeNext ()
{
	s64 t = 0x7FFFFFFFFFFFFFFFll;
	for (int i = 0; i < EV_COUNT; i++) if (evTime[i] < t) t = evTime[i];
	nextEv = t;
}

void Machine::runEvents ()
{
	for (;;)
	{
		int best = -1; s64 bt = 0x7FFFFFFFFFFFFFFFll;
		for (int i = 0; i < EV_COUNT; i++) if (evTime[i] < bt) { bt = evTime[i]; best = i; }
		if (best < 0 || bt > now) break;
		evTime[best] = 0x7FFFFFFFFFFFFFFFll;
		s64 keep = now; now = bt;
		switch (best)
		{
		case EV_HBLANK: hblank (); break;
		case EV_LINE: lineEnd (); break;
		default:
			if (best >= EV_TIMER && best < EV_TIMER + 8) timerOverflow ((best - EV_TIMER) >> 2, (best - EV_TIMER) & 3);
			break;
		}
		now = keep;
	}
	computeNext ();
}

void Machine::hblank ()
{
	inHblank = true;
	for (int c = 0; c < 2; c++)
	{
		dispstat[c] |= 2;
		if (dispstat[c] & 0x10) raise (c, 1);
	}
	if (vcount < H)
	{
		displayLine (vcount);
		dmaStart (0, 2);
	}
}

void Machine::lineEnd ()
{
	inHblank = false;
	for (int c = 0; c < 2; c++) dispstat[c] &= ~2;
	vcount++;
	lineStart += LINE_CYCLES;
	if (vcount == LINES) vcount = 0;
	if (vcount == H)
	{
		for (int c = 0; c < 2; c++)
		{
			dispstat[c] |= 1;
			if (dispstat[c] & 8) raise (c, 0);
			dmaStart (c, 1);
		}
		gpuA.vblank (); gpuB.vblank ();
		gpu3d.vblank ();
		frameDone = true;
		rtcTicks += FRAME_CYCLES;
		if (rtcTicks >= 2 * (s64) ARM7_HZ) { rtcTicks -= 2 * (s64) ARM7_HZ; rtcTick (); }
		// the keypad interrupt
		for (int c = 0; c < 2; c++)
			if (keycnt[c] & 0x4000)
			{
				u16 sel = keycnt[c] & 0x3FF, pressed = (u16) (keys & 0x3FF);
				bool hit = (keycnt[c] & 0x8000) ? (pressed & sel) == sel : (pressed & sel) != 0;
				if (hit) raise (c, 12);
			}
	}
	if (vcount == LINES - 1) for (int c = 0; c < 2; c++) dispstat[c] &= ~1;
	if (vcount == 0) { gpuA.mosaicDone = gpuB.mosaicDone = false; }
	for (int c = 0; c < 2; c++)
	{
		int lyc = (dispstat[c] >> 8) | ((dispstat[c] & 0x80) << 1);
		if (vcount == lyc) { dispstat[c] |= 4; if (dispstat[c] & 0x20) raise (c, 2); }
		else dispstat[c] &= ~4;
	}
	spu.run (now);
	schedule (EV_HBLANK, lineStart + HBLANK_CYCLES);
	schedule (EV_LINE, lineStart + LINE_CYCLES);
}

void Machine::runCpu (Arm &c)
{
	if (useJit && jit && c.jitOn) jitRun (this, c);
	else c.run ();
}

void Machine::runSlice (s64 target)
{
	arm9.target = target; running = 0; runCpu (arm9);
	arm7.target = target; running = 1; runCpu (arm7);
	running = -1;
}

void Machine::runFrame ()
{
	frameDone = false;
	while (!frameDone)
	{
		if (pagesDirty) pagesUpdate ();
		s64 target = nextEv;
		if (target > now + SLICE) target = now + SLICE;
		runSlice (target);
		cyclesRun += (u64) (target - now);
		now = target;
		if (now >= nextEv) runEvents ();
	}
	frames++;
	save = cart.save; saveSize = cart.saveSize;
	if (cart.saveDirty) { saveDirty = true; cart.saveDirty = false; }
}

void Machine::setAudioRate (int hz) { spu.rate = hz; }

int Machine::audioRead (short *lr, int maxFrames)
{
	int n = 0;
	while (n < maxFrames && spu.atail != spu.ahead)
	{
		lr[n * 2] = spu.abuf[spu.atail * 2]; lr[n * 2 + 1] = spu.abuf[spu.atail * 2 + 1];
		spu.atail = (spu.atail + 1) & (Spu::ABUF - 1);
		n++;
	}
	return n;
}

void Machine::codeWritten (u32 a, int cpu)
{
	if (jit) jitInvalidate (this, cpu, a);
}

} // namespace nds
