//
// gc/gc_hw.cpp -- the GameCube's chips as the CPU sees them (the registers at 0x0C000000):
// PI (the interrupts to the CPU, the GX FIFO pointers), VI (the video: the scan lines, the four
// display interrupts, the framebuffer shown), SI (the pads, polled), EXI (the RTC and the SRAM
// of the IPL chip; no memory card yet), DI (the DVD drive: reads from the disc image), AI and
// the DSP interface (the audio DMA, the ARAM DMA, the mailboxes), MI. The command processor
// and the pixel engine are in gc_gx.cpp.
//
#include "gc/gc.h"
#ifdef GC_TRACE
#include <stdio.h>
#define STRACE(...) printf (__VA_ARGS__)
#else
#define STRACE(...) ((void) 0)
#endif

namespace gc {

static void zero (void *p, u32 n) { u8 *d = (u8 *) p; while (n--) *d++ = 0; }

enum { PI_DI = 0x4, PI_SI = 0x8, PI_EXI = 0x10, PI_AI = 0x20, PI_DSP = 0x40, PI_VI = 0x100,
       PI_PE_TOKEN = 0x200, PI_PE_FINISH = 0x400, PI_CP = 0x800 };

void Machine::piRaise (u32 bits)
{
	for (int i = 0; i < 16; i++) if (bits & ~piIntsr & (1u << i)) irqCount[i]++;
	piIntsr |= bits; piUpdate ();
}
void Machine::piLower (u32 bits) { piIntsr &= ~bits; piUpdate (); }
void Machine::piUpdate () { extIrq = (piIntsr & piIntmr) != 0; jitUntil = 0; }

// ---- reset -----------------------------------------------------------------------------------------------------
void Machine::hwReset ()
{
	piIntsr = 0; piIntmr = 0; piFifoBase = piFifoEnd = piFifoWptr = 0; gatherN = 0;
	zero (vi, sizeof vi); zero (siReg, sizeof siReg); zero (siBuf, sizeof siBuf); siPoll = 0;
	zero (exiReg, sizeof exiReg); zero (exiCmd, sizeof exiCmd); zero (exiPhase, sizeof exiPhase);
	for (int i = 0; i < 3; i++) exiTcAt[i] = ~0ull;
	exiReg[0][0] = exiReg[1][0] = 0x800;			// EXTINT: the slots' devices, as the IPL leaves them
	zero (diReg, sizeof diReg); diReads = diLastOff = 0; hwLastRead = hwLastReadN = 0; dspBootKey = dspMailsIn = dspLastMail = 0; zero (aiReg, sizeof aiReg); zero (dspReg, sizeof dspReg); zero (miReg, sizeof miReg);
	zero (irqCount, sizeof irqCount);
	diDoneAt = ~0ull; dicover = 0; aiSampleAt = 0;
	aiReg[0] = 0x40;					// AICR: the DMA at 32 kHz (AIDFR), as the IPL leaves it
	audioW = audioR = audioFrac = 0; audioPrev[0] = audioPrev[1] = 0;
	dspMailIn = 0; dspBootStep = 0; dspLastOut = 0; dspHaveSaved = false;
	aidmaNextAt = aidIrqAt = dspIrqAt = ~0ull; aidmaLeft = 0; aidmaAddr = 0;
	gpBytes = 0; frames = 0;
	zero (cpReg16, sizeof cpReg16); zero (peReg16, sizeof peReg16);
	zero (cpRegs, sizeof cpRegs); zero (xfRegs, sizeof xfRegs); zero (bpRegs, sizeof bpRegs); zero (bpKonst, sizeof bpKonst);
	gxInit ();
	cpFifoBase = cpFifoEnd = cpFifoRptr = cpFifoWptr = cpBreak = 0;
	gxCmds = gxPrims = gxVerts = gxCopies = gxIndirect = texDecodes = 0;
	// the DSP as the IPL leaves it: halted, its ROM ready (its mail seen once it runs)
	dspQHead = dspQTail = 0; dspUcode = 0; zero (&dspUc, sizeof dspUc);
	dspReg[0x0A / 2] = 0x0804;				// CSR: DSPINIT, HALT
	dspReset ();
	dspReg[0x16 / 2] = 1;					// AR_MODE: the ARAM controller is ready (ARInit waits for it)
	dspReg[0x1A / 2] = 156;					// AR_REFRESH
	// the video as the IPL leaves it: NTSC or PAL (from the disc's region), 640 x 480 until the game
	// sets it up; the timings, and the two display interrupts (the middle of the frame, its first
	// line) that VIInit keeps when it finds the VI enabled -- the games' retrace
	vi[0x02 / 2] = pal ? 0x0101 : 0x0001;			// DCR: enabled, the format
	vi[0x00 / 2] = (u16) ((pal ? 287 : 240) << 4 | 6);	// VTR: active lines a field
	vi[0x04 / 2] = 71 << 8 | 105; vi[0x06 / 2] = 429;	// HTR0: HCS, HCE, HLW
	vi[0x08 / 2] = 0x02EA; vi[0x0A / 2] = 0x5140;		// HTR1: HBS640 373, HBE640 162, HSY 64
	vi[0x0C / 2] = 5; vi[0x0E / 2] = 502;			// VTO: PSB, PRB
	vi[0x10 / 2] = 4; vi[0x12 / 2] = 503;			// VTE
	vi[0x14 / 2] = vi[0x16 / 2] = 12 | 520 << 5;		// BBOI
	vi[0x18 / 2] = vi[0x1A / 2] = 13 | 519 << 5;		// BBEI
	vi[0x30 / 2] = (u16) (0x1000 | (pal ? 313 : 263)); vi[0x32 / 2] = 430;	// DI0: enabled, VCT, HCT
	vi[0x34 / 2] = 0x1000 | 1; vi[0x36 / 2] = 1;		// DI1: line 1
	vi[0x48 / 2] = 0x2828;					// HSW: 640 pixels a line, the stride
	viLinesFrame = pal ? 625 : 525;
	viCyclesLine = (u64) CPU_HZ / ((pal ? 25 : 30) * (u64) viLinesFrame);
	viLine = 1; viNextLine = cycles + viCyclesLine;
	fbW = 640; fbH = 480;
	for (int i = 0; i < FB_MAX_W * FB_MAX_H; i++) fb[i] = 0;
	// the SRAM: English, stereo, its two checksums
	zero (sram, sizeof sram);
	sram[0x13] = 0x2C;					// flags (stereo...)
	sram[0x12] = 0;						// language: English
	{
		u16 c = 0, ci = 0;
		for (int i = 0x0C; i < 0x14; i += 2) { u16 w = (u16) (sram[i] << 8 | sram[i + 1]); c = (u16) (c + w); ci = (u16) (ci + (u16) ~w); }
		sram[0] = (u8) (c >> 8); sram[1] = (u8) c; sram[2] = (u8) (ci >> 8); sram[3] = (u8) ci;
	}
	rtcBase = 0x2F000000;					// (a fixed date: the RTC has no clock of its own here)
	// the memory cards stay in their slots: an erased one formatted, the SRAM's flash ID set from theirs
	for (int s = 0; s < 2; s++) { cardReset (s); if (card[s].flash) cardAttach (s); }
	zero (aram, ARAM_SIZE);
}

// ---- the video ------------------------------------------------------------------------------------------------
// VI registers (16-bit words): VTR 0x00 (ACV: active lines a field, bits 4-13), DCR 0x02 (ENB,
// NIN non-interlaced, FMT), TFBL 0x1C / BFBL 0x24 (the top / bottom field's framebuffer: bits
// 0-23, << 5 when bit 28), DPV 0x2C (the beam's line), DI0-3 0x30-0x3C (INT bit 31, ENB 28,
// VCT 16-25, HCT 0-9), HSW 0x48 (WPL bits 8-14: 16-pixel reads a line; STD 0-7: the stride in
// 32-byte units).
static inline u32 vi32 (const u16 *v, int off) { return (u32) v[off / 2] << 16 | v[off / 2 + 1]; }

void Machine::viLineStep ()
{
	viLine++;
	if (viLine > viLinesFrame) viLine = 1;
	bool any = false;
	for (int n = 0; n < 4; n++)
	{
		u16 &hi = vi[(0x30 + n * 4) / 2];
		if ((hi & 0x1000) && (u32) (hi & 0x3FF) == (u32) viLine) hi |= 0x8000;
		if ((hi & 0x8000) && (hi & 0x1000)) any = true;
	}
	if (any) piRaise (PI_VI); else piLower (PI_VI);
}

// the framebuffer the VI shows (YUV 4:2:2, Y0 U Y1 V) -> fb (RGB)
static inline u8 clamp8 (int v) { return (u8) (v < 0 ? 0 : v > 255 ? 255 : v); }

// The VI shows an XFB an EFB copy went to: the GX frame is its picture (the copy itself is not
// done in MEM1), what the front ends show -- even while the game draws nothing new (loading).
bool Machine::xfbIsCopy ()
{
	u32 tfbl = vi32 (vi, 0x1C);
	u32 top = ((tfbl & 0x00FFFFFF) << ((tfbl & 0x10000000) ? 5 : 0)) & 0x01FFFFFF;
	return (tfbl & 0x00FFFFFF) && (top == (xfbCopyAddr[0] & 0x01FFFFFF) || top == (xfbCopyAddr[1] & 0x01FFFFFF));
}

void Machine::viOutput ()
{
	if (xfbIsCopy () && gfxReady >= 0) return;			// (not shown: the GX frame is)
	u32 tfbl = vi32 (vi, 0x1C), bfbl = vi32 (vi, 0x24);
	u32 top = (tfbl & 0x00FFFFFF) << ((tfbl & 0x10000000) ? 5 : 0);
	u32 bot = (bfbl & 0x00FFFFFF) << ((bfbl & 0x10000000) ? 5 : 0);
	u32 hsw = vi[0x48 / 2];
	int width = (int) ((hsw >> 8) & 0x7F) * 16, stride = (int) (hsw & 0xFF) * 32;
	int acv = (vi[0] >> 4) & 0x3FF;
	bool nin = vi[0x02 / 2] & 4;
	if (width <= 0 || width > FB_MAX_W) width = 640;
	if (stride <= 0) stride = width * 2;
	int height = nin ? acv : acv * 2;
	if (height <= 0 || height > FB_MAX_H) height = pal ? 574 : 480;
	fbW = width; fbH = height;
	// black while the game has not given the VI its picture (the frame buffer's address still 0:
	// the start of memory -- the OS globals, the game's code -- would show as noise) or has
	// switched the display off: as the console, that shows nothing until then
	if ((tfbl & 0x00FFFFFF) == 0 || !(vi[0x02 / 2] & 1))
	{
		for (int i = 0; i < width * height; i++) fb[i] = 0;
		return;
	}
	for (int y = 0; y < height; y++)
	{
		u32 base = nin ? top + (u32) (y * stride) : ((y & 1) ? bot : top) + (u32) ((y >> 1) * stride);
		const u8 *p = ptr (base & 0x01FFFFFF);
		u32 *d = fb + y * width;
		if (!p || (base & 0x01FFFFFF) + (u32) width * 2 > MEM1_SIZE) { for (int x = 0; x < width; x++) d[x] = 0; continue; }
		for (int x = 0; x < width; x += 2, p += 4)
		{
			int y0 = p[0] - 16, u = p[1] - 128, y1 = p[2] - 16, v = p[3] - 128;
			int r = 409 * v, g = -100 * u - 208 * v, b = 516 * u;
			int c0 = 298 * y0 + 128, c1 = 298 * y1 + 128;
			d[x] = (u32) clamp8 ((c0 + r) >> 8) << 16 | (u32) clamp8 ((c0 + g) >> 8) << 8 | clamp8 ((c0 + b) >> 8);
			d[x + 1] = (u32) clamp8 ((c1 + r) >> 8) << 16 | (u32) clamp8 ((c1 + g) >> 8) << 8 | clamp8 ((c1 + b) >> 8);
		}
	}
}

// ---- what the front end shows (F12): where the CPU is, what the game has done so far -------------------------
void Machine::status (char *out, int cap)
{
	static const char HX[] = "0123456789ABCDEF";
	int n = 0;
	auto put = [&] (const char *t) { while (*t && n < cap - 1) out[n++] = *t++; };
	auto hex = [&] (u32 v) { for (int i = 7; i >= 0 && n < cap - 1; i--) out[n++] = HX[(v >> (i * 4)) & 15]; };
	auto dec = [&] (u32 v) { char t[12]; int k = 0; do { t[k++] = (char) ('0' + v % 10); v /= 10; } while (v); while (k && n < cap - 1) out[n++] = t[--k]; };
	u32 tfbl = vi32 (vi, 0x1C);
	put ("pc "); hex (pc);
	put (", DVD "); dec (diReads); put (" reads (at "); hex (diLastOff); put (")");
	put (", picture: "); put ((tfbl & 0x00FFFFFF) == 0 ? "not set up yet" : (vi[0x02 / 2] & 1) ? "on" : "off");
	put (", 3D frames "); dec (gfxSerial);
	put (", DSP step "); dec ((u32) dspBootStep);
	if (dspBootStep == 2) { put (dspUc.kind == DSP_ZELDA ? " Zelda " : dspUc.kind == DSP_CARD ? " card " : " AX "); hex (dspUcode); }
	put (" (mails "); dec (dspMailsIn); put (", last "); hex (dspLastMail); put (")");
	// what it waits for: the register it reads over and over, the interrupts (pending / enabled)
	put (" | polls "); hex (hwLastRead); put (" x"); dec (hwLastReadN);
	put (", EE "); dec ((msr >> 15) & 1); put (", PI "); hex (piIntsr); put ("/"); hex (piIntmr);
	put (", DSP csr "); hex (dspReg[0x0A / 2]); put (", DI "); hex (diReg[0]); put (" "); hex (diReg[7]);
	put (", EXI0 "); hex (exiReg[0][0]); put (" "); hex (exiReg[0][3]);
	put (jit ? ", JIT" : ", interpreter");
	if (halted) { put (", stopped: "); put (haltMsg); }
	out[n] = 0;
}

// ---- one field ---------------------------------------------------------------------------------------------------
void Machine::runFrame ()
{
	__atomic_add_fetch (&texEpoch, 1u, __ATOMIC_RELAXED);	// (gpuTexture's answers: sampled again each field)
	// (the lines run 1..viLinesFrame: the first field ends halfway, the second at the frame's end)
	int endLine = viLine >= viLinesFrame / 2 && viLine < viLinesFrame ? viLinesFrame : viLinesFrame / 2;
	u64 guard = cycles + (u64) CPU_HZ;			// (a second at most)
	while (!halted && cycles < guard)
	{
		u64 until = viNextLine; int why = 0;
		if (diDoneAt < until) { until = diDoneAt; why = 1; }
		if (aidmaNextAt < until) { until = aidmaNextAt; why = 2; }
		if (aidIrqAt < until) { until = aidIrqAt; why = 3; }
		if (dspIrqAt < until) { until = dspIrqAt; why = 4; }
		for (int ch = 0; ch < 2; ch++)
		{
			if (exiTcAt[ch] < until) { until = exiTcAt[ch]; why = 5; }
			if (card[ch].doneAt < until) { until = card[ch].doneAt; why = 6; }
		}
		// while the AI plays, each sample is an event: a loop polling its counter (AIInit times its
		// edges) is not skipped past one (the JIT's polling loops run to the next event)
		if (aiReg[0] & 1)
		{
			u64 per = (u64) CPU_HZ / ((aiReg[0] & 2) ? 48000 : 32000);
			u64 nx = aiSampleAt + per;
			if (nx <= cycles) nx = aiSampleAt + per * ((cycles - aiSampleAt) / per + 1);
			if (nx < until) { until = nx; why = 7; }
		}
		evWhy[why]++;
		run (until);
		events ();
		if (cycles >= viNextLine)
		{
			viNextLine += viCyclesLine;
			viLineStep ();
			if (viLine == endLine) break;
		}
	}
	viOutput ();
	siPollAll ();
	frames++;
}

void Machine::events ()
{
	if (cycles >= diDoneAt)
	{
		diDoneAt = ~0ull;
		diReg[0] |= 0x10;					// TCINT
		diReg[7] &= ~1u;					// (DICR: TSTART done)
		if (diReg[0] & 0x08) piRaise (PI_DI);
	}
	if (cycles >= dspIrqAt) { dspIrqAt = ~0ull; dspInterrupt (0); }	// (a DSP mail's interrupt, delayed)
	for (int ch = 0; ch < 2; ch++)					// the memory cards: a DMA's end, a command's
	{
		if (cycles >= exiTcAt[ch]) { exiTcAt[ch] = ~0ull; exiReg[ch][0] |= 8; exiUpdate (); }
		if (cycles >= card[ch].doneAt) cardDone (ch);
	}
	if (cycles >= aidIrqAt) { aidIrqAt = ~0ull; dspReg[0x0A / 2] |= 0x08; dspIrqUpdate (); }	// AIDINT: a DMA started
	if (cycles >= aidmaNextAt)
	{
		// the audio DMA reads 32 bytes (8 stereo frames at 32 kHz) a block; at the buffer's end it
		// starts again from the address and length set now, and the DSP interrupts (AIDINT)
		if (aidmaLeft > 0) { audioBlock (aidmaAddr); aidmaLeft--; aidmaAddr += 32; }
		if (aidmaLeft == 0)
		{
			u16 ctl = dspReg[0x36 / 2];
			aidmaAddr = ((u32) dspReg[0x30 / 2] << 16 | dspReg[0x32 / 2]) & 0x03FFFFE0;
			aidmaLeft = ctl & 0x7FFF;
			dspReg[0x0A / 2] |= 0x08;				// AIDINT
			dspIrqUpdate ();
		}
		aidmaNextAt = (dspReg[0x36 / 2] & 0x8000) ? aidmaNextAt + (u64) CPU_HZ * 8 / aidRate () : ~0ull;
	}
}

// ---- SI: the pads ---------------------------------------------------------------------------------------------
void Machine::setPad (int n, u32 b, int sx, int sy, int cx, int cy, int l, int r)
{
	if (n < 0 || n > 3) return;
	padBtn[n] = (u16) b; padSX[n] = (s8) sx; padSY[n] = (s8) sy; padCX[n] = (s8) cx; padCY[n] = (s8) cy;
	padL[n] = (u8) l; padR[n] = (u8) r;
}

// COMCSR (0x34): TCINT 31, TCINTMSK 30, COMERR 29 (the last transfer failed), RDSTINT 28 (a poll's
// data is there), RDSTINTMSK 27, OUTLNGTH 16-22, INLNGTH 8-14, CHANNEL 1-2, TSTART 0. SISR (0x38),
// a byte a channel (channel 0 the top one): RDST 0x20, WRST 0x10, NOREP 8, COLL 4, OVRUN 2, UNRUN 1;
// WR (bit 31) sends the channels' output commands.
void Machine::siUpdate ()
{
	u32 &cs = siReg[0x34 / 4];
	cs = (siReg[0x38 / 4] & 0x20202020u) ? cs | 0x10000000u : cs & ~0x10000000u;
	if (((cs & 0x80000000u) && (cs & 0x40000000u)) || ((cs & 0x10000000u) && (cs & 0x08000000u))) piRaise (PI_SI);
	else piLower (PI_SI);
}

// the report of a poll (analog mode 3): buttons, stick, C stick, triggers -> the channel's INBUF;
// the other channels answer nothing (NOREP, ERRSTAT + ERRLATCH)
void Machine::siPollAll ()
{
	for (int c = 0; c < 4; c++)
	{
		if (c == 0)
		{
			u16 b = padBtn[c] | 0x0080;
			siReg[1] = (u32) b << 16 | (u32) (u8) (padSX[c] + 128) << 8 | (u8) (padSY[c] + 128);
			siReg[2] = (u32) (u8) (padCX[c] + 128) << 24 | (u32) (u8) (padCY[c] + 128) << 16 | (u32) padL[c] << 8 | padR[c];
			siReg[0x38 / 4] |= 0x20000000u;			// RDST0
		}
		else
		{
			siReg[c * 3 + 1] |= 0xC0000000u;
			siReg[0x38 / 4] |= 0x08000000u >> (c * 8);		// NOREP
		}
	}
	siUpdate ();
}

// a transfer through the SI buffer (the ID of the pad, its origin...), done at once
void Machine::siTransfer ()
{
	u32 &cs = siReg[0x34 / 4];
	int ch = (int) (cs >> 1) & 3;
	u8 cmd = siBuf[0];
	int inLen = (int) (cs >> 8) & 0x7F; if (!inLen) inLen = 128;
	if (ch != 0) { cs |= 0x20000000; siReg[0x38 / 4] |= 0x08000000u >> (ch * 8); }	// COMERR, NOREP
	else
	{
		u8 r[128]; zero (r, sizeof r);
		switch (cmd)
		{
		case 0x00: case 0xFF: r[0] = 0x09; r[1] = 0x00; r[2] = 0x00; break;		// a standard pad
		case 0x40:								// a poll (direct)
			r[0] = (u8) (siReg[1] >> 24); r[1] = (u8) (siReg[1] >> 16); r[2] = (u8) (siReg[1] >> 8); r[3] = (u8) siReg[1];
			r[4] = (u8) (siReg[2] >> 24); r[5] = (u8) (siReg[2] >> 16); r[6] = (u8) (siReg[2] >> 8); r[7] = (u8) siReg[2];
			break;
		case 0x41: case 0x42:							// the origin / calibrate
			r[0] = 0x00; r[1] = 0x80; r[2] = 0x80; r[3] = 0x80; r[4] = 0x80; r[5] = 0x80; r[6] = 0x1F; r[7] = 0x1F; break;
		default: break;
		}
		for (int i = 0; i < inLen && i < 128; i++) siBuf[i] = r[i];
		cs &= ~0x20000000u;
	}
	cs &= ~1u;							// TSTART done
	cs |= 0x80000000u;						// TCINT
	siUpdate ();
}

// ---- EXI: the IPL chip (RTC, SRAM), the memory cards (gc_card.cpp) ------------------------------------------------
// A channel's status (reg 0): EXIINTMASK 0, EXIINT 1 (the device's: a card's command done), TCINTMASK
// 2, TCINT 3, CLK 4-6, CS 7-9 (a chip select each), EXTINTMASK 10, EXTINT 11 (a device came / went),
// EXT 12 (a card in the slot), ROMDIS 13.
void Machine::exiUpdate ()
{
	bool on = false;
	for (int ch = 0; ch < 3; ch++)
	{
		u32 &st = exiReg[ch][0];
		if (ch < 2 && card[ch].flash && card[ch].intSwitch && card[ch].intSet) st |= 2;
		if (((st & 2) && (st & 1)) || ((st & 8) && (st & 4)) || ((st & 0x800) && (st & 0x400))) on = true;
	}
	if (on) piRaise (PI_EXI); else piLower (PI_EXI);
}

u32 Machine::exiIpl (int ch, u32 data, bool write, int len)
{
	if (exiPhase[ch] == 0)						// the command: bit 31 write, the address << 6
	{
		exiCmd[ch] = data; exiPhase[ch] = 1;
		return 0;
	}
	u32 addr = (exiCmd[ch] & 0x7FFFFFC0) >> 6, r = 0;
	bool wr = exiCmd[ch] & 0x80000000u;
	for (int i = 0; i < len; i++)
	{
		u32 a = addr + (u32) (exiPhase[ch] - 1) + (u32) i;
		u8 byte = 0;
		if (a >= 0x800000 && a < 0x800004)
		{
			u32 t = rtcBase + (u32) (cycles / CPU_HZ);
			byte = (u8) (t >> (24 - (a - 0x800000) * 8));
		}
		else if (a >= 0x800004 && a < 0x800044)
		{
			if (wr && write) sram[a - 0x800004] = (u8) (data >> (24 - i * 8));
			byte = sram[a - 0x800004];
		}
		r |= (u32) byte << (24 - i * 8);
	}
	exiPhase[ch] += len;
	return r;
}

void Machine::exiTransfer (int ch)
{
	u32 *e = exiReg[ch];
	u32 cr = e[3];
	int cs = (int) (e[0] >> 7) & 7;					// the chip selected
	bool dma = cr & 2;
	int rw = (int) (cr >> 2) & 3, len = (int) ((cr >> 4) & 3) + 1;
	bool isCard = ch < 2 && cs == 1 && card[ch].flash;		// (a memory card: chip select 0)
	e[3] &= ~1u;
	if (!dma)
	{
		u32 data = e[4], r = 0;
		if (isCard)							// its bytes, the first in the top one
			for (int i = 0; i < len; i++) r |= (u32) cardByte (ch, rw != 0 ? (u8) (data >> (24 - i * 8)) : 0) << (24 - i * 8);
		else if (ch == 0 && cs == 2) r = exiIpl (ch, data, rw != 0, len);	// (cs 1 = bit 8 = value 2)
		else if (ch == 2 && cs == 1) r = 0x04120000;			// (AD16: its ID)
		else r = 0;							// nothing there
		if (rw != 1) e[4] = r;
	}
	else if (isCard && rw < 2)						// the data of a card's command: done after its time
	{
		u32 t = cardDma (ch, e[1], e[2], rw == 0);
		exiTcAt[ch] = cycles + (t ? t : 1);
		return;
	}
	else if (ch == 0 && cs == 2 && rw < 2)				// the IPL chip by DMA (the OS reads the SRAM so)
	{
		u32 m = e[1] & 0x01FFFFFF, n = e[2];
		if (m + n > MEM1_SIZE) n = m < MEM1_SIZE ? MEM1_SIZE - m : 0;
		for (u32 i = 0; i < n; i += 4)
		{
			int k = n - i < 4 ? (int) (n - i) : 4;
			u32 w = 0;
			if (rw == 1) for (int j = 0; j < k; j++) w |= (u32) mem1[m + i + j] << (24 - j * 8);
			u32 r = exiIpl (ch, w, rw == 1, k);
			if (rw == 0) for (int j = 0; j < k; j++) mem1[m + i + j] = (u8) (r >> (24 - j * 8));
		}
	}
	else if (rw == 0)
	{
		u8 *p = ptr (e[1] & 0x01FFFFFF);
		for (u32 i = 0; p && i < e[2]; i++) p[i] = 0;
	}
	e[0] |= 8;							// TCINT
	exiUpdate ();
}

// ---- DI: the DVD drive ------------------------------------------------------------------------------------------
bool Machine::readDisc (u32 off, u32 len, u32 dst)
{
	u8 *d = ptr (dst & 0x01FFFFFF);
	if (!d || (dst & 0x01FFFFFF) + len > MEM1_SIZE) return false;
	jitInvalidate (dst & 0x01FFFFFF, len); __atomic_add_fetch (&texEpoch, 1u, __ATOMIC_RELAXED);
	if (!disc && discRead) return discRead (discCtx, off, len, d);
	for (u32 i = 0; i < len; i++) d[i] = disc && off + i < discSize ? disc[off + i] : 0;
	return true;
}

// DISR 0 (TCINT 4 / its mask 3, DEINT 2 / 1), DICVR 1, DICMDBUF 2-4, DIMAR 5, DILENGTH 6, DICR 7
// (TSTART 0, DMA 1), DIIMMBUF 8, DICFG 9
void Machine::diCommand ()
{
	u32 cmd = diReg[2] >> 24, sub = (diReg[2] >> 16) & 0xFF;
	u32 len = diReg[6], delay = 20000;
	switch (cmd)
	{
	case 0xA8:							// read (DMA)
		if (sub == 0x40) readDisc (0, 0x20, diReg[5]);		// the disc ID
		else { readDisc (diReg[3] << 2, len, diReg[5]); diReads++; diLastOff = diReg[3] << 2; }
		delay = 20000 + len * 40;
		diReg[5] += len; diReg[6] = 0;
		break;
	case 0x12:							// inquiry: the drive's date / revision
	{
		u8 *d = ptr (diReg[5] & 0x01FFFFFF);
		static const u8 inq[32] = { 0x00, 0x00, 0x00, 0x02, 0x20, 0x02, 0x04, 0x02, 0x61, 0 };
		for (u32 i = 0; d && i < 32 && i < len; i++) d[i] = inq[i];
		diReg[5] += len; diReg[6] = 0;
		break;
	}
	case 0xE0: diReg[8] = 0; break;					// the last error: none
	case 0xE2: diReg[8] = 0; break;					// the audio stream's status
	default: break;							// seek, stop motor, audio config...
	}
	diDoneAt = cycles + delay;
}

// ---- the GX FIFO: the write-gather pipe's bytes go to the FIFO in memory (gc_gx.cpp reads them) -------------
void Machine::gpWrite (u32 v, int size)
{
	for (int i = 0; i < size; i++) gather[gatherN++] = (u8) (v >> ((size - 1 - i) * 8));
	if (gatherN >= 32) gatherFlush ();
}

// The pipe sends 32 bytes at a time (GXFlush pads with NOPs): each burst to the FIFO's write
// pointer (wrapping at its end), then the commands run.
void Machine::gatherFlush ()
{
	u32 done = 0;
	while (gatherN - done >= 32)
	{
		u32 w = piFifoWptr & 0x03FFFFFF;
		if (gxAsync) gxRoom (w);				// (the FIFO full: the GX first)
		u8 *p = ptr (w & 0x01FFFFFF);
		if (p && (w & 0x01FFFFFF) + 32 <= MEM1_SIZE) for (int i = 0; i < 32; i++) p[i] = gather[done + i];
		w += 32;
		gpBytes += 32;
		if (piFifoEnd && w >= (piFifoEnd & 0x03FFFFFF)) w = piFifoBase & 0x03FFFFFF;
		__atomic_store_n (&piFifoWptr, w, __ATOMIC_RELEASE);	// (the burst's bytes before: for the GX's core)
		done += 32;
	}
	for (u32 i = done; i < gatherN; i++) gather[i - done] = gather[i];
	gatherN -= done;
	if (!done) return;
	if (gxAsync)
	{
#if defined (__aarch64__)
		asm volatile ("dsb ish\n\tsev" ::: "memory");		// (the GX's core woken)
#endif
	}
	else gxFifoKick ();
}

// ---- the registers ---------------------------------------------------------------------------------------------
u32 Machine::hwRead (u32 pa, int size)
{
	u32 off = pa & 0xFFFF;
	if (pa == hwLastRead) hwLastReadN++; else { hwLastRead = pa; hwLastReadN = 1; }
	switch (pa >> 12)
	{
	case 0x0C000: return cpRead (off, size);			// the command processor (gc_gx.cpp)
	case 0x0C001: return peRead (off, size);			// the pixel engine
	case 0x0C002:							// VI
	{
		off &= 0x7F;
		if (off == 0x2C || off == 0x2D) vi[0x2C / 2] = (u16) viLine;
		if (off == 0x2E || off == 0x2F) vi[0x2E / 2] = (u16) (1 + (viCyclesLine - (viNextLine > cycles ? viNextLine - cycles : 0)) * 858 / viCyclesLine);
		if (size == 4) return vi32 (vi, (int) (off & ~3u));
		return vi[off / 2];
	}
	case 0x0C003:							// PI
		switch (off & 0xFF)
		{
		case 0x00: return piIntsr | 0x10000;			// (+ RSWST: the reset button up)
		case 0x04: return piIntmr;
		case 0x0C: return piFifoBase;
		case 0x10: return piFifoEnd;
		case 0x14: return piFifoWptr;
		case 0x2C: return 0x246500B1;				// the Flipper's revision
		}
		return 0;
	case 0x0C004: return miReg[(off & 0x7F) / 2];		// MI
	case 0x0C005:							// the DSP interface
	{
		off &= 0x7F;
		if (size == 4) return (u32) hwRead (pa, 2) << 16 | hwRead (pa + 2, 2);
		switch (off)
		{
		case 0x00: return dspMailIn >> 16;			// (the CPU -> DSP mailbox: bit 15 = not read yet)
		case 0x02: return dspMailIn & 0xFFFF;
		case 0x04: return dspMailHigh ();			// the DSP -> CPU mailbox
		case 0x06: return dspMailLow ();
		case 0x0A: return dspReg[0x0A / 2] & ~0x0603u;		// (reset, PIINT, the DMAs: done)
		case 0x3A: return (u16) (aidmaLeft > 0 ? aidmaLeft - 1 : 0);	// (the blocks left, less the one playing)
		}
		return dspReg[off / 2];
	}
	case 0x0C006:
		if (off < 0x6400)					// DI
		{
			int r = (int) (off & 0x3F) / 4;
			if (r == 1) return diReg[1] & ~1u;		// (the cover: closed)
			if (r == 9) return 1;
			return r < 10 ? diReg[r] : 0;
		}
		if (off < 0x6800)					// SI
		{
			u32 o = off & 0xFF;
			if (o >= 0x80) { u32 i = o - 0x80; return (u32) siBuf[i] << 24 | siBuf[i + 1] << 16 | siBuf[i + 2] << 8 | siBuf[i + 3]; }
			if (o < 0x30 && (o % 12) != 0) { siReg[0x38 / 4] &= ~(0x20000000u >> ((o / 12) * 8)); siUpdate (); }	// (reading INBUF: RDST off)
			STRACE ("SI read %02X = %08X (pc %08X)\n", o, siReg[o / 4], curPc);
			return siReg[o / 4];
		}
		if (off < 0x6C00)					// EXI
		{
			u32 o = off & 0xFF; int ch = (int) o / 0x14, r = (int) (o % 0x14) / 4;
			if (ch > 2) return 0;
			if (r == 0) return (exiReg[ch][0] & ~0x1000u) | (ch < 2 && card[ch].flash ? 0x1000u : 0);	// (EXT: a card in the slot)
			return exiReg[ch][r];
		}
		{							// AI
			int r = (int) (off & 0x1F) / 4;
			if (r == 2) aiUpdate ();
			return r < 4 ? aiReg[r] : 0;
		}
	}
	return 0;
}

void Machine::hwWrite (u32 pa, u32 v, int size)
{
	u32 off = pa & 0xFFFF;
	if ((pa & 0xFFFFF000) == 0x0C008000) { gpWrite (v, size); return; }	// the write-gather pipe
	switch (pa >> 12)
	{
	case 0x0C000: cpWrite (off, v, size); return;
	case 0x0C001: peWrite (off, v, size); return;
	case 0x0C002:							// VI
	{
		off &= 0x7E;
		if (size == 4) { hwWrite (pa, v >> 16, 2); hwWrite (pa + 2, v & 0xFFFF, 2); return; }
		u16 w = (u16) v;
		if (off >= 0x30 && off <= 0x3C && !(off & 2))		// DIn: writing INT = 0 clears it
		{
			vi[off / 2] = w;
			bool any = false;
			for (int n = 0; n < 4; n++) { u16 hi = vi[(0x30 + n * 4) / 2]; if ((hi & 0x8000) && (hi & 0x1000)) any = true; }
			if (!any) piLower (PI_VI);
			return;
		}
		vi[off / 2] = w;
		if (off == 0x02) { bool p = ((w >> 8) & 3) == 1; if (p != pal) { pal = p; viLinesFrame = pal ? 625 : 525; viCyclesLine = (u64) CPU_HZ / ((pal ? 25 : 30) * (u64) viLinesFrame); } }
		return;
	}
	case 0x0C003:							// PI
		switch (off & 0xFF)
		{
		case 0x00: piIntsr &= ~(v & 0x3); piUpdate (); return;	// (the reset switch / PI error)
		case 0x04: piIntmr = v; piUpdate (); return;
		case 0x0C: if (gxAsync) gxSync (); piFifoBase = v & 0x03FFFFE0; return;
		case 0x10: if (gxAsync) gxSync (); piFifoEnd = v & 0x03FFFFE0; return;
		case 0x14: if (gxAsync) gxSync (); __atomic_store_n (&piFifoWptr, v & 0x03FFFFE0, __ATOMIC_RELEASE); return;
		}
		return;
	case 0x0C004: miReg[(off & 0x7E) / 2] = (u16) v; return;	// MI
	case 0x0C005:							// the DSP interface
	{
		off &= 0x7E;
		if (size == 4) { hwWrite (pa, v >> 16, 2); hwWrite (pa + 2, v & 0xFFFF, 2); return; }
		u16 w = (u16) v;
		switch (off)
		{
		case 0x00: dspMailIn = (dspMailIn & 0xFFFF) | (u32) w << 16; return;
		case 0x02: dspMailIn = (dspMailIn & 0xFFFF0000) | w; dspMailReceived (dspMailIn); return;	// (as written: bit 31 is the mail's)
		case 0x0A:
		{
			u16 &csr = dspReg[0x0A / 2];
			u16 old = csr;
			// the interrupt bits written as 1 are acknowledged; HALT (4), the masks and DSPINIT
			// (0x800) are kept; RES (1), PIINT (2) and the DMA states (0x200, 0x400) read 0 (done)
			u16 ack = w & (0x08 | 0x20 | 0x80);
			csr = (u16) ((csr & ~ack & (0x08 | 0x20 | 0x80)) | (w & 0x0954));
			if (w & 1)						// a reset: the ROM again, the audio DMA stopped
			{
				dspReset ();
				dspReg[0x36 / 2] = 0; aidmaNextAt = aidIrqAt = ~0ull; aidmaLeft = 0;
			}
			// DSPINIT 1 -> 0: the DSP runs the IPL's init code (__OSInitAudioSystem): it mails
			// 0x80544348 (seen once the DSP is not halted), then halts
			if ((old & 0x800) && !(csr & 0x800)) dspSetProgram (3);
			dspIrqUpdate ();
			return;
		}
		case 0x12: dspReg[0x12 / 2] = w & 0x7F; return;		// AR_INFO (the ARAM's size)
		case 0x16: return;						// AR_MODE: read only
		case 0x1A: dspReg[0x1A / 2] = w & 0x7FF; return;		// AR_REFRESH
		case 0x20: case 0x24: dspReg[off / 2] = w & 0x3FF; return;	// the ARAM DMA's addresses
		case 0x22: case 0x26: dspReg[off / 2] = w & 0xFFE0; return;
		case 0x28: dspReg[0x28 / 2] = w & 0x83FF; return;
		case 0x2A:							// ARAM DMA count low: the transfer
		{
			dspReg[0x2A / 2] = w & 0xFFE0;
			u32 mm = ((u32) dspReg[0x20 / 2] << 16 | dspReg[0x22 / 2]) & 0x01FFFFFF;
			u32 ar = ((u32) dspReg[0x24 / 2] << 16 | dspReg[0x26 / 2]) & 0x03FFFFFF;
			u32 cnt = (u32) (dspReg[0x28 / 2] & 0x3FF) << 16 | dspReg[0x2A / 2];
			bool toMain = dspReg[0x28 / 2] & 0x8000;
			if (mm + cnt > MEM1_SIZE) cnt = mm < MEM1_SIZE ? MEM1_SIZE - mm : 0;
			if (toMain) { jitInvalidate (mm, cnt); __atomic_add_fetch (&texEpoch, 1u, __ATOMIC_RELAXED); }
			// the ARAM is 16 MB; above, the expansion port (nothing there: reads 0, writes lost)
			for (u32 i = 0; i < cnt; i++)
			{
				u32 a = ar + i;
				if (toMain) mem1[mm + i] = a < ARAM_SIZE ? aram[a] : 0;
				else if (a < ARAM_SIZE) aram[a] = mem1[mm + i];
			}
			dspReg[0x22 / 2] = (u16) ((mm + cnt) & 0xFFE0); dspReg[0x20 / 2] = (u16) ((mm + cnt) >> 16);
			dspReg[0x26 / 2] = (u16) ((ar + cnt) & 0xFFE0); dspReg[0x24 / 2] = (u16) (((ar + cnt) >> 16) & 0x3FF);
			dspReg[0x28 / 2] &= 0x8000; dspReg[0x2A / 2] = 0;
			dspReg[0x0A / 2] |= 0x20;				// ARINT
			dspIrqUpdate ();
			return;
		}
		case 0x30: dspReg[0x30 / 2] = w & 0x3FF; return;		// the audio DMA's address
		case 0x32: dspReg[0x32 / 2] = w & 0xFFE0; return;
		case 0x36:							// the audio DMA's control: start / stop
		{
			bool was = dspReg[0x36 / 2] & 0x8000;
			dspReg[0x36 / 2] = w;
			if (!was && (w & 0x8000))				// started: from this buffer; AIDINT soon
			{
				aidmaAddr = ((u32) dspReg[0x30 / 2] << 16 | dspReg[0x32 / 2]) & 0x03FFFFE0;
				aidmaLeft = w & 0x7FFF;
				aidmaNextAt = cycles + (u64) CPU_HZ * 8 / aidRate ();
				aidIrqAt = cycles + 200;
			}
			else if (!(w & 0x8000)) aidmaNextAt = ~0ull;
			return;
		}
		case 0x3A: return;						// (the blocks left: read only)
		}
		dspReg[off / 2] = w;
		return;
	}
	case 0x0C006:
		if (off < 0x6400)					// DI
		{
			int r = (int) (off & 0x3F) / 4;
			if (r == 0)
			{
				u32 ack = v & 0x54;				// the interrupt bits written as 1: acknowledged
				diReg[0] = (diReg[0] & ~ack & 0x54) | (v & 0x2B);
				if (!(diReg[0] & diReg[0] >> 1 & 0x2A)) piLower (PI_DI);
				return;
			}
			if (r == 1) { diReg[1] = (diReg[1] & ~(v & 4)) | (v & 2); return; }
			if (r < 10) diReg[r] = v;
			if (r == 7 && (v & 1)) diCommand ();
			return;
		}
		if (off < 0x6800)					// SI
		{
			u32 o = off & 0xFF;
			STRACE ("SI write %02X = %08X size %d (pc %08X)\n", o, v, size, curPc);
			if (o >= 0x80) { u32 i = o - 0x80; if (size == 4) { siBuf[i] = (u8) (v >> 24); siBuf[i + 1] = (u8) (v >> 16); siBuf[i + 2] = (u8) (v >> 8); siBuf[i + 3] = (u8) v; } else siBuf[i] = (u8) v; return; }
			if (o == 0x34)
			{
				// the channel, the lengths and the masks as written; TCINT written as 1: acknowledged;
				// COMERR (the last transfer's) and RDSTINT (the polls') are not written
				u32 &cs = siReg[0x34 / 4];
				u32 keep = cs & (v & 0x80000000u ? 0x20000000u : 0xA0000000u);
				cs = keep | (v & 0x487F7F06u);
				if (v & 1) siTransfer (); else siUpdate ();
				return;
			}
			if (o == 0x38)						// the error bits written as 1: cleared; WR: sent
			{
				u32 &sr = siReg[0x38 / 4];
				sr &= ~(v & 0x0F0F0F0Fu);
				if (v & 0x80000000u) sr &= ~0x90101010u;
				siUpdate ();
				return;
			}
			siReg[o / 4] = v;
			return;
		}
		if (off < 0x6C00)					// EXI
		{
			u32 o = off & 0xFF; int ch = (int) o / 0x14, r = (int) (o % 0x14) / 4;
			if (ch > 2) return;
			u32 *e = exiReg[ch];
			if (ch == 0) STRACE ("EXI0 write r%d = %08X (status %08X, pc %08X)\n", r, v, e[0], curPc);
			if (r == 0)
			{
				u32 ack = v & 0x80A;				// EXTINT, TCINT, EXIINT written as 1: acknowledged
				u32 oldCs = e[0] & 0x380;
				e[0] = (e[0] & ~ack & 0x80A) | (v & ~0x180Au);
				u32 cs = e[0] & 0x380;
				if (cs != oldCs)				// (a new chip select: a new command)
				{
					exiPhase[ch] = 0;
					if (ch < 2 && card[ch].flash && ((cs ^ oldCs) & 0x80)) cardCs (ch, (cs & 0x80) != 0);
				}
				exiUpdate ();
				return;
			}
			e[r] = v;
			if (r == 3 && (v & 1)) exiTransfer (ch);
			return;
		}
		{							// AI
			int r = (int) (off & 0x1F) / 4;
			if (r == 0)
			{
				u32 &c = aiReg[0];
				aiUpdate ();
				if (v & 0x20) aiReg[2] = 0;			// SCRESET
				u32 ack = v & 0x08;				// AIINT written as 1: acknowledged
				c = (c & ~ack & 0x08) | (v & ~0x28u);
				if (!((c & 8) && (c & 4))) piLower (PI_AI);
				return;
			}
			if (r < 4) aiReg[r] = v;
			return;
		}
	}
}

// ---- the sound out: the audio DMA's blocks, resampled (linearly) to the host's rate ------------------------------
// A block is 8 stereo frames, big-endian, the right channel first (as the AX microcode writes them).
void Machine::audioBlock (u32 addr)
{
	if (!audioHostRate || addr + 32 > MEM1_SIZE) return;
	u32 step = (u32) (((u64) aidRate () << 16) / audioHostRate);	// (source frames an output frame, 16.16)
	const u8 *p = mem1 + addr;
	for (int i = 0; i < 8; i++, p += 4)
	{
		s32 r = (s16) (p[0] << 8 | p[1]), l = (s16) (p[2] << 8 | p[3]);
		while (audioFrac < 0x10000)				// (the outputs between the last frame and this one)
		{
			if (audioW - audioR < AUDIO_RING)			// (full: the host is behind -- dropped)
			{
				s16 *o = audioBuf + (audioW & (AUDIO_RING - 1)) * 2;
				o[0] = (s16) (audioPrev[0] + (s32) (((s64) (l - audioPrev[0]) * audioFrac) >> 16));
				o[1] = (s16) (audioPrev[1] + (s32) (((s64) (r - audioPrev[1]) * audioFrac) >> 16));
				audioW++;
			}
			audioFrac += step;
		}
		audioFrac -= 0x10000;
		audioPrev[0] = (s16) l; audioPrev[1] = (s16) r;
	}
}

int Machine::audioRead (s16 *lr, int maxFrames)
{
	int n = 0;
	for (; n < maxFrames && audioR != audioW; n++, audioR++)
	{
		const s16 *i = audioBuf + (audioR & (AUDIO_RING - 1)) * 2;
		lr[n * 2] = i[0]; lr[n * 2 + 1] = i[1];
	}
	return n;
}

// the streaming sample counter (AISCNT): counts while PSTAT, interrupts at AIIT
void Machine::aiUpdate ()
{
	u64 now = cycles;
	if (!(aiReg[0] & 1)) { aiSampleAt = now; return; }
	u32 rate = (aiReg[0] & 2) ? 48000 : 32000;
	u64 per = (u64) CPU_HZ / rate;
	while (aiSampleAt + per <= now)
	{
		aiSampleAt += per;
		aiReg[2]++;
		if (aiReg[2] == aiReg[3] && (aiReg[0] & 4)) { aiReg[0] |= 8; piRaise (PI_AI); }
	}
}

} // namespace gc
