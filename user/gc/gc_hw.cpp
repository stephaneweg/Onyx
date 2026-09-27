//
// gc/gc_hw.cpp -- the GameCube's chips as the CPU sees them (the registers at 0x0C000000):
// PI (the interrupts to the CPU, the GX FIFO pointers), VI (the video: the scan lines, the four
// display interrupts, the framebuffer shown), SI (the pads, polled), EXI (the RTC and the SRAM
// of the IPL chip; no memory card yet), DI (the DVD drive: reads from the disc image), AI and
// the DSP interface (the audio DMA, the ARAM DMA, the mailboxes), MI. The command processor
// and the pixel engine are in gc_gx.cpp.
//
#include "gc/gc.h"

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
	zero (diReg, sizeof diReg); zero (aiReg, sizeof aiReg); zero (dspReg, sizeof dspReg); zero (miReg, sizeof miReg);
	zero (irqCount, sizeof irqCount);
	diDoneAt = ~0ull; dicover = 0; aiSampleAt = 0;
	dspMailIn = dspMailOut = 0; dspMailOutValid = false; dspBootStep = 0;
	aidmaNextAt = ~0ull; aidmaLeft = 0; aidmaAddr = 0;
	gpBytes = 0; frames = 0;
	zero (cpReg16, sizeof cpReg16); zero (peReg16, sizeof peReg16);
	zero (cpRegs, sizeof cpRegs); zero (xfRegs, sizeof xfRegs); zero (bpRegs, sizeof bpRegs); zero (bpKonst, sizeof bpKonst);
	gxInit ();
	cpFifoBase = cpFifoEnd = cpFifoRptr = cpFifoWptr = cpBreak = 0;
	gxCmds = gxPrims = gxVerts = gxCopies = 0;
	dspQHead = dspQTail = 0; dspBootN = 0; dspUcode = 0; dspCmdlistLeft = 0;
	// the video: NTSC or PAL (from the disc's region), 640 x 480 until the game sets it up
	vi[0x02 / 2] = pal ? 0x0101 : 0x0001;			// DCR: enabled, the format
	vi[0x00 / 2] = (u16) ((pal ? 287 : 240) << 4 | 6);	// VTR: active lines a field
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

void Machine::viOutput ()
{
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

// ---- one field ---------------------------------------------------------------------------------------------------
void Machine::runFrame ()
{
	int endLine = viLine <= viLinesFrame / 2 ? viLinesFrame / 2 : viLinesFrame;
	u64 guard = cycles + (u64) CPU_HZ;			// (a second at most)
	while (!halted && cycles < guard)
	{
		u64 until = viNextLine;
		if (diDoneAt < until) until = diDoneAt;
		if (aidmaNextAt < until) until = aidmaNextAt;
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
	if (cycles >= aidmaNextAt)
	{
		// the audio DMA reads 32 bytes (8 stereo frames at 32 kHz) a block; a new block:
		// the next one starts, the DSP interrupts when the whole buffer has begun
		if (aidmaLeft > 0) aidmaLeft--;
		if (aidmaLeft == 0)
		{
			u16 ctl = dspReg[0x36 / 2];
			if (ctl & 0x8000)
			{
				aidmaLeft = ctl & 0x7FFF;
				dspReg[0x0A / 2] |= 0x08;			// AIDINT
				if (dspReg[0x0A / 2] & 0x10) piRaise (PI_DSP);
			}
		}
		dspReg[0x3A / 2] = (u16) aidmaLeft;
		aidmaNextAt = (dspReg[0x36 / 2] & 0x8000) ? cycles + (u64) CPU_HZ * 8 / 32000 : ~0ull;
	}
}

// ---- SI: the pads ---------------------------------------------------------------------------------------------
void Machine::setPad (int n, u32 b, int sx, int sy, int cx, int cy, int l, int r)
{
	if (n < 0 || n > 3) return;
	padBtn[n] = (u16) b; padSX[n] = (s8) sx; padSY[n] = (s8) sy; padCX[n] = (s8) cx; padCY[n] = (s8) cy;
	padL[n] = (u8) l; padR[n] = (u8) r;
}

// the report of a poll (analog mode 3): buttons, stick, C stick, triggers -> the channel's INBUF
void Machine::siPollAll ()
{
	for (int c = 0; c < 4; c++)
	{
		u32 h, l;
		if (c == 0)
		{
			u16 b = padBtn[c] | 0x0080;
			h = (u32) b << 16 | (u32) (u8) (padSX[c] + 128) << 8 | (u8) (padSY[c] + 128);
			l = (u32) (u8) (padCX[c] + 128) << 24 | (u32) (u8) (padCY[c] + 128) << 16 | (u32) padL[c] << 8 | padR[c];
		}
		else { h = 0x80000000u; l = 0; }			// ErrStat: nothing there
		siReg[c * 3 + 1] = h; siReg[c * 3 + 2] = l;
	}
	// SISR: RDST for channel 0, NOREP for the others
	siReg[0x38 / 4] = (0x20u << 24) | (0x08u << 16) | (0x08u << 8) | 0x08u;
}

// a transfer through the SI buffer (the ID of the pad, its origin...)
void Machine::siTransfer ()
{
	u32 &cs = siReg[0x34 / 4];
	int ch = (int) (cs >> 1) & 3;
	u8 cmd = siBuf[0];
	int inLen = (int) (cs >> 8) & 0x7F; if (!inLen) inLen = 128;
	if (ch != 0) { cs |= 0x20000000; siReg[0x38 / 4] |= 0x08u << (24 - ch * 8); }	// COMERR, NOREP
	else
	{
		u8 r[128]; zero (r, sizeof r);
		switch (cmd)
		{
		case 0x00: case 0xFF: r[0] = 0x09; r[1] = 0x00; r[2] = 0x00; break;		// a standard pad
		case 0x41: case 0x42:							// the origin / calibrate
			r[0] = 0x00; r[1] = 0x80; r[2] = 0x80; r[3] = 0x80; r[4] = 0x80; r[5] = 0x80; r[6] = 0x1F; r[7] = 0x1F; break;
		default: break;
		}
		for (int i = 0; i < inLen && i < 128; i++) siBuf[i] = r[i];
	}
	cs &= ~1u;							// TSTART done
	cs |= 0x80000000u;						// TCINT
	if (cs & 0x40000000u) piRaise (PI_SI);
}

// ---- EXI: the IPL chip (RTC, SRAM); the memory card slots are empty ------------------------------------------
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
	if (!dma)
	{
		u32 data = e[4], r = 0;
		if (ch == 0 && cs == 2) r = exiIpl (ch, data, rw != 0, len);	// (cs 1 = bit 8 = value 2)
		else if (ch == 2 && cs == 1) r = 0x04120000;			// (AD16: its ID)
		else r = 0;							// nothing there
		if (rw != 1) e[4] = r;
	}
	else if (rw == 0)
	{
		u8 *p = ptr (e[1] & 0x01FFFFFF);
		for (u32 i = 0; p && i < e[2]; i++) p[i] = 0;
	}
	e[3] &= ~1u;
	e[0] |= 8;							// TCINT
	if (e[0] & 4) piRaise (PI_EXI);
}

// ---- DI: the DVD drive ------------------------------------------------------------------------------------------
bool Machine::readDisc (u32 off, u32 len, u32 dst)
{
	u8 *d = ptr (dst & 0x01FFFFFF);
	if (!d || (dst & 0x01FFFFFF) + len > MEM1_SIZE) return false;
	jitInvalidate (dst & 0x01FFFFFF, len);
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
		else readDisc (diReg[3] << 2, len, diReg[5]);
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
		u8 *p = ptr (w & 0x01FFFFFF);
		if (p && (w & 0x01FFFFFF) + 32 <= MEM1_SIZE) for (int i = 0; i < 32; i++) p[i] = gather[done + i];
		w += 32;
		gpBytes += 32;
		if (piFifoEnd && w >= (piFifoEnd & 0x03FFFFFF)) w = piFifoBase & 0x03FFFFFF;
		piFifoWptr = w;
		done += 32;
	}
	for (u32 i = done; i < gatherN; i++) gather[i - done] = gather[i];
	gatherN -= done;
	if (done) gxFifoKick ();
}

// ---- the registers ---------------------------------------------------------------------------------------------
u32 Machine::hwRead (u32 pa, int size)
{
	u32 off = pa & 0xFFFF;
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
		case 0x04: dspHleStep (); return dspMailOutValid ? (dspMailOut >> 16) | 0x8000 : dspMailOut >> 16 & 0x7FFF;
		case 0x06: { u16 v = (u16) dspMailOut; dspMailOutValid = false; dspHleStep (); return v; }
		case 0x0A: return dspReg[0x0A / 2] & ~0x0801u;		// (reset / init done)
		case 0x3A: return (u16) aidmaLeft;
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
			if (o < 0x30 && (o % 12) == 4) siReg[0x38 / 4] &= ~(0x20u << (24 - (o / 12) * 8));	// (reading INBUFH: RDST off)
			return siReg[o / 4];
		}
		if (off < 0x6C00)					// EXI
		{
			u32 o = off & 0xFF; int ch = (int) o / 0x14, r = (int) (o % 0x14) / 4;
			if (ch > 2) return 0;
			if (r == 0) return (exiReg[ch][0] & ~0x1000u) | (ch == 0 ? 0 : 0);	// (EXT: nothing plugged in the slots)
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
		case 0x0C: piFifoBase = v & 0x03FFFFE0; return;
		case 0x10: piFifoEnd = v & 0x03FFFFE0; return;
		case 0x14: piFifoWptr = v & 0x03FFFFE0; return;
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
		case 0x02: dspMailIn = (dspMailIn & 0xFFFF0000) | w; dspMailReceived (dspMailIn | 0x80000000u); return;
		case 0x0A:
		{
			u16 &csr = dspReg[0x0A / 2];
			// the interrupt bits written as 1 are acknowledged; the masks and control bits are set
			u16 ack = w & (0x08 | 0x20 | 0x80);
			csr = (u16) ((csr & ~ack & (0x08 | 0x20 | 0x80)) | (w & ~(0x08 | 0x20 | 0x80 | 0x01)));
			if (w & 1) dspReset ();
			if (w & 2) csr |= 0x80;					// PIINT: the CPU interrupts the DSP (-> HLE)
			dspIrqUpdate ();
			return;
		}
		case 0x28: dspReg[0x28 / 2] = w; return;
		case 0x2A:							// ARAM DMA count low: the transfer
		{
			dspReg[0x2A / 2] = w;
			u32 mm = (u32) dspReg[0x20 / 2] << 16 | dspReg[0x22 / 2], ar = (u32) dspReg[0x24 / 2] << 16 | dspReg[0x26 / 2];
			u32 cnt = ((u32) (dspReg[0x28 / 2] & 0x7FFF) << 16 | w);
			bool toMain = dspReg[0x28 / 2] & 0x8000;
			u8 *m = ptr (mm & 0x01FFFFFF);
			if (toMain) jitInvalidate (mm & 0x01FFFFFF, cnt);
			for (u32 i = 0; m && i < cnt && (mm & 0x01FFFFFF) + i < MEM1_SIZE; i++)
			{
				u32 a = (ar + i) & (ARAM_SIZE - 1);
				if (toMain) m[i] = aram[a]; else aram[a] = m[i];
			}
			dspReg[0x28 / 2] = 0; dspReg[0x2A / 2] = 0;
			dspReg[0x0A / 2] |= 0x20;				// ARINT
			dspIrqUpdate ();
			return;
		}
		case 0x36:							// the AI DMA's control: start / stop
			dspReg[0x36 / 2] = w;
			aidmaAddr = (u32) dspReg[0x30 / 2] << 16 | dspReg[0x32 / 2];
			if (w & 0x8000) { aidmaLeft = w & 0x7FFF; if (aidmaNextAt == ~0ull) aidmaNextAt = cycles + (u64) CPU_HZ * 8 / 32000; }
			return;
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
			if (o >= 0x80) { u32 i = o - 0x80; if (size == 4) { siBuf[i] = (u8) (v >> 24); siBuf[i + 1] = (u8) (v >> 16); siBuf[i + 2] = (u8) (v >> 8); siBuf[i + 3] = (u8) v; } else siBuf[i] = (u8) v; return; }
			if (o == 0x34)
			{
				u32 &cs = siReg[0x34 / 4];
				u32 ack = v & 0x90000000u;			// TCINT / RDSTINT written as 1: acknowledged
				cs = (cs & ~ack & 0x90000000u) | (v & ~0x90000000u);
				if (!((cs & 0x80000000u) && (cs & 0x40000000u))) piLower (PI_SI);
				if (v & 1) siTransfer ();
				return;
			}
			if (o == 0x38) { siReg[0x38 / 4] &= ~(v & 0x0F0F0F0F); return; }	// (the error bits written: cleared)
			siReg[o / 4] = v;
			return;
		}
		if (off < 0x6C00)					// EXI
		{
			u32 o = off & 0xFF; int ch = (int) o / 0x14, r = (int) (o % 0x14) / 4;
			if (ch > 2) return;
			u32 *e = exiReg[ch];
			if (r == 0)
			{
				u32 ack = v & 0x80A;				// EXTINT, TCINT, EXIINT written as 1: acknowledged
				u32 oldCs = e[0] & 0x380;
				e[0] = (e[0] & ~ack & 0x80A) | (v & ~0x80Au);
				if ((e[0] & 0x380) != oldCs) exiPhase[ch] = 0;	// (a new chip select: a new command)
				if (!((e[0] & 8) && (e[0] & 4))) piLower (PI_EXI);
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
