//
// gc/gc_dsp.cpp -- the DSP at a high level: what its ROM and the games' microcodes answer through
// the mailboxes, not their instructions (the protocols as Dolphin's DSP HLE knows them).
//
//   * the mailboxes: the DSP's mails queue up; the CPU sees the first one (DMBH bit 15) unless the
//     DSP is halted (CSR bit 2), reading its low half takes it. A mail sent "with an interrupt"
//     raises DSPINT at once if the mailbox is free, else once the CPU has read the one in it.
//   * the ROM (after a reset): mails 0x8071FEED, then takes the microcode's description (0x80F3xxxx
//     keys, each followed by its value: A001 its address, A002 its length, C002 its IRAM address,
//     B002 the DRAM length, D001 its start) and runs it. Which microcode: a checksum of its image
//     (Dolphin's "ector" CRC, so its table of the known ones applies).
//   * the IPL's init code (__OSInitAudioSystem clears CSR bit 11): mails 0x80544348.
//   * AX (most games): a command list a frame (0xBABE + its size, then its address), answered with
//     DSP_YIELD; the CPU then says what next (0xCDD1xxxx: continue, a new microcode, a reset).
//   * the "Zelda" microcode (Nintendo EAD's games: Wind Waker, Mario Sunshine, Pikmin...): DSP_INIT
//     and a handshake, then commands in batches (a count, then the words), each acknowledged
//     (DSP_SYNC + 0xF355xxxx); command 02 renders frames of audio, the CPU telling which voices are
//     ready (sync mails), DSP_FRAME_END when done. The "light" variant (the IPL, Luigi's Mansion...)
//     has fixed command sizes and other acknowledgements.
//   * the upload of another microcode (0xCDD10001 then 10 mails: a task switch, e.g. to the memory
//     card's): the one left is kept to come back to (then DSP_RESUME).
// The audio itself is not mixed yet: the Zelda microcode's frames are written silent, the AX
// command lists are acknowledged without being run.
//
#include "gc/gc.h"
#ifdef GC_TRACE
#include <stdio.h>
#define DTRACE(...) printf (__VA_ARGS__)
#else
#define DTRACE(...) ((void) 0)
#endif

namespace gc {

enum { PI_DSP = 0x40 };
enum { CSR_HALT = 0x04, CSR_AIDINT = 0x08, CSR_ARINT = 0x20, CSR_DSPINT = 0x80 };
// the task mails, DSP -> CPU and CPU -> DSP
enum : u32
{
	DSP_INIT = 0xDCD10000u, DSP_RESUME = 0xDCD10001u, DSP_YIELD = 0xDCD10002u, DSP_DONE = 0xDCD10003u,
	DSP_SYNC = 0xDCD10004u, DSP_FRAME_END = 0xDCD10005u,
	MAIL_RESUME = 0xCDD10000u, MAIL_NEW_UCODE = 0xCDD10001u, MAIL_RESET = 0xCDD10002u, MAIL_CONTINUE = 0xCDD10003u
};
// the Zelda microcode's mail states
enum { ZS_WAITING, ZS_RENDERING, ZS_WRITING_CMD, ZS_HALTED };
// AX's
enum { AX_WAIT_SIZE, AX_WAIT_ADDR, AX_WAIT_TASK };

// the checksum Dolphin names the microcodes by
static u32 ectorCrc (const u8 *p, u32 n)
{
	u32 crc = 0;
	for (u32 i = 0; i < n; i++) { crc ^= p[i]; crc = crc << 3 | crc >> 29; }
	return crc;
}

void Machine::dspIrqUpdate ()
{
	u16 c = dspReg[0x0A / 2];
	bool on = ((c & 0x08) && (c & 0x10)) || ((c & 0x20) && (c & 0x40)) || ((c & 0x80) && (c & 0x100));
	if (on) piRaise (PI_DSP); else piLower (PI_DSP);
}

// the DSP interrupts the CPU (DSPINT): now, or after delay cycles
void Machine::dspInterrupt (u32 delay)
{
	if (!delay) { dspReg[0x0A / 2] |= CSR_DSPINT; dspIrqUpdate (); return; }
	u64 at = cycles + delay;
	if (at < dspIrqAt) dspIrqAt = at;
}

// a mail from the DSP
void Machine::dspPush (u32 mail, bool irq, u32 delay)
{
	int next = (dspQTail + 1) & (DSP_QUEUE - 1);
	if (next == dspQHead) return;					// (full: lost)
	if (irq)
	{
		if (dspQHead == dspQTail) dspInterrupt (delay);
		else dspQIrq[dspQHead] = 1;				// (once the CPU has read the mail in the box)
	}
	dspQueue[dspQTail] = mail; dspQIrq[dspQTail] = 0;
	dspQTail = next;
	DTRACE ("  DSP -> %08X%s\n", mail, irq ? " (irq)" : "");
}

// the CPU reads the DSP -> CPU mailbox: its high half (bit 15: a mail is there), then its low half
// (which takes it); an empty box reads the last mail, bit 31 clear
u16 Machine::dspMailHigh ()
{
	if (!(dspReg[0x0A / 2] & CSR_HALT) && dspQHead != dspQTail) dspLastOut = dspQueue[dspQHead];
	return (u16) (dspLastOut >> 16);
}

u16 Machine::dspMailLow ()
{
	if (!(dspReg[0x0A / 2] & CSR_HALT) && dspQHead != dspQTail)
	{
		dspLastOut = dspQueue[dspQHead];
		bool irq = dspQIrq[dspQHead];
		dspQHead = (dspQHead + 1) & (DSP_QUEUE - 1);
		DTRACE ("  CPU reads %08X (pc %08X)\n", dspLastOut, curPc);
		if (irq) dspInterrupt (0);
	}
	u16 v = (u16) dspLastOut;
	dspLastOut &= 0x7FFFFFFF;
	return v;
}

// a program starts on the DSP: its pending mails are dropped
void Machine::dspSetProgram (int step)
{
	dspQHead = dspQTail = 0;
	dspBootStep = step;
	dspBootKey = 0;
	if (step == 1) dspPush (0x8071FEEDu, false);			// the ROM: ready for a microcode
	else if (step == 3) dspPush (0x80544348u, false);		// the IPL's init code, then it halts
}

void Machine::dspReset ()
{
	dspHaveSaved = false;
	dspSetProgram (1);
}

// the microcode at addr (len bytes) starts: which one it is, its first mails
void Machine::dspStartUcode (u32 addr, u32 len)
{
	u32 a = addr & 0x01FFFFFF;
	if (a > MEM1_SIZE) a = MEM1_SIZE;
	if (len > MEM1_SIZE - a) len = MEM1_SIZE - a;
	u32 crc = ectorCrc (mem1 + a, len);
	DspUcode &u = dspUc;
	for (u32 i = 0; i < sizeof u; i++) ((u8 *) &u)[i] = 0;
	u.crc = crc; u.kind = DSP_AX;
	static const struct { u32 crc, flags; } zelda[] = {
		{ 0x24B22038, Z_LIGHT | Z_FOUR_DESTS | Z_TINY_VPB | Z_VOLUME_STEP | Z_NO_CMD_0D | Z_WEIRD_CMD_0C },	// the IPL, NTSC
		{ 0x6BA3B3EA, Z_LIGHT | Z_FOUR_DESTS | Z_NO_CMD_0D },		// the IPL, PAL
		{ 0xDF059F68, Z_LIGHT | Z_NO_CMD_0D | Z_GBA_CRYPTO },		// Pikmin (NTSC demo)
		{ 0x4BE6A5CB, Z_LIGHT | Z_NO_CMD_0D | Z_GBA_CRYPTO },		// Pikmin (NTSC), Animal Crossing
		{ 0x42F64AC4, Z_LIGHT | Z_NO_CMD_0D | Z_WEIRD_CMD_0C },		// Luigi's Mansion
		{ 0x267FD05A, Z_SYNC_PER_FRAME | Z_NO_CMD_0D },			// Pikmin (PAL)
		{ 0x56D36052, Z_SYNC_PER_FRAME | Z_NO_CMD_0D },			// Super Mario Sunshine
		{ 0x86840740, 0 },						// The Wind Waker
		{ 0x2FCDF1EC, Z_LOUDER },					// Four Swords, Mario Kart DD, Pikmin 2...
		{ 0x6CA33A6D, Z_LOUDER | Z_COMBINED_CMD_0D },			// Twilight Princess, DK Jungle Beat
	};
	for (u32 i = 0; i < sizeof zelda / sizeof zelda[0]; i++)
		if (zelda[i].crc == crc) { u.kind = DSP_ZELDA; u.flags = zelda[i].flags; }
	if (crc == 0x65D6CC6F) u.kind = DSP_CARD;				// the memory card's unlock
	dspUcode = crc;
	dspSetProgram (2);
	if (u.kind == DSP_ZELDA)
	{
		u.zCanExec = true;
		if (u.flags & Z_LIGHT) dspPush (0x88881111u, false);
		else { dspPush (DSP_INIT, true); dspPush (0xF3551111u, false); }	// (a handshake)
	}
	else dspPush (DSP_INIT, true);
}

// a task switch: the 10 mails describing the next microcode (its IRAM image: mails 3 and 4)
void Machine::dspUpload (u32 mail)
{
	DspUcode &u = dspUc;
	u.upload[u.uploadStep++] = mail;
	if (u.uploadStep < 10) return;
	u.uploading = false; u.uploadStep = 0;
	u32 addr = u.upload[3] & 0x01FFFFFF, len = u.upload[4] & 0xFFFF;
	if (addr > MEM1_SIZE) addr = MEM1_SIZE;
	if (len > MEM1_SIZE - addr) len = MEM1_SIZE - addr;
	u32 crc = ectorCrc (mem1 + addr, len);
	if (dspHaveSaved && dspSaved.crc == crc)			// back to the one left: it resumes
	{
		dspUc = dspSaved; dspHaveSaved = false;
		dspUcode = crc;
		dspQHead = dspQTail = 0;
		dspPush (DSP_RESUME, true);
		return;
	}
	if (!dspHaveSaved) { dspSaved = dspUc; dspHaveSaved = true; }
	dspStartUcode (u.upload[3], len);
}

// ---- AX: a command list a frame -----------------------------------------------------------------
void Machine::dspAxMail (u32 mail)
{
	DspUcode &u = dspUc;
	switch (u.state)
	{
	case AX_WAIT_SIZE:
		if ((mail & 0xFFFF0000u) == 0xBABE0000u) u.state = AX_WAIT_ADDR;
		break;
	case AX_WAIT_ADDR:
		// (the list is not run yet: no mixing) done, the DSP yields; as Dolphin, the interrupt a
		// little later (a game hangs if it comes at once)
		dspPush (DSP_YIELD, true, 2500);
		u.state = AX_WAIT_TASK;
		break;
	case AX_WAIT_TASK:
		mail = 0xCDD10000u | (mail & 0xFFFF);
		if (mail == MAIL_RESUME) { dspPush (DSP_RESUME, true); u.state = AX_WAIT_SIZE; }
		else if (mail == MAIL_NEW_UCODE) { u.uploading = true; u.state = AX_WAIT_SIZE; }
		else if (mail == MAIL_RESET) dspSetProgram (1);
		else if (mail == MAIL_CONTINUE) u.state = AX_WAIT_SIZE;
		break;
	}
}

// ---- the Zelda microcode ------------------------------------------------------------------------
u32 Machine::dspZRead ()
{
	DspUcode &u = dspUc;
	if (u.zRd == u.zWr) return 0;
	u32 v = u.zCmd[u.zRd]; u.zRd = (u.zRd + 1) & 63;
	return v;
}

void Machine::dspZAck (bool done, u32 sync)
{
	DspUcode &u = dspUc;
	if (u.flags & Z_LIGHT)						// (the command handler's address)
	{
		dspPush (0x80000000u | (2 * ((sync >> 8) & 0x7F) + 0x62), false);
		return;
	}
	dspPush (done ? DSP_FRAME_END : DSP_SYNC, true);
	if (!done) dspPush (0xF3550000u | (sync & 0xFFFF), false);
}

// the frames asked for (gc_zelda.cpp mixes them), as far as the voices the CPU has said ready allow
void Machine::dspZRender ()
{
	DspUcode &u = dspUc;
	if (u.zFrame == u.zFrames) return;
	while (u.zFrame < u.zFrames)
	{
		if (u.zVoice == 0) zPrepare ();
		u32 voices = u.zVoices < 256 * 16 ? u.zVoices : 256 * 16;
		while (u.zVoice < voices)
		{
			if (u.zVoice >= u.zSyncMax) return;			// (not ready yet: the next sync mail)
			if (u.zSkip[u.zVoice >> 4] & (0x8000 >> (u.zVoice & 15))) zAddVoice (u.zVoice);
			u.zVoice++;
		}
		if (!(u.flags & Z_LIGHT)) dspZAck (false, 0xFF00 | u.zFrame);
		zFinalize ();
		u.zVoice = 0; u.zSyncMax = 0; u.zFrame++;
	}
	if (!(u.flags & Z_LIGHT)) { dspZAck (true, 0); u.zCanExec = false; }
	else u.state = ZS_WAITING;
}

void Machine::dspZRun ()
{
	DspUcode &u = dspUc;
	if (u.zFrame != u.zFrames || !u.zCanExec) return;		// (rendering, or waiting for the CPU)
	while (u.zPending)
	{
		u32 m = dspZRead ();
		if (!(m & 0x80000000u)) { if (u.zRd == u.zWr) { u.zPending = 0; break; } continue; }
		u32 cmd = (m >> 24) & 0x7F, sync = m >> 16, extra = m & 0xFFFF;
		u.zPending--;
		switch (cmd)
		{
		case 0x00: case 0x0A: case 0x0B: case 0x0F:			// nothing
			dspZAck (false, sync);
			break;
		case 0x03:
			if (!(u.flags & Z_LIGHT)) dspZAck (false, sync);
			break;
		case 0x01:							// set up: the voices (their VPBs), the tables, the reverbs
		{
			ZeldaMix &z = u.mix;
			u.zVoices = (u16) extra;
			z.vpbBase = dspZRead ();
			u32 t = dspZRead () & 0x01FFFFFF;			// (resampling coefficients, patterns, a sine)
			auto rd = [&] (u32 a) -> s16 { return a + 1 < MEM1_SIZE ? (s16) (mem1[a] << 8 | mem1[a + 1]) : 0; };
			for (u32 i = 0; i < 0x100; i++) { z.resample[i] = rd (t + i * 2); z.patterns[i] = rd (t + 0x200 + i * 2); }
			if (!(u.flags & Z_LIGHT)) for (u32 i = 0; i < 0x80; i++) z.sine[i] = rd (t + 0x400 + i * 2);
			u32 afc = dspZRead () & 0x01FFFFFF;
			for (u32 i = 0; i < 0x20; i++) z.afc[i] = rd (afc + i * 2);
			z.reverbBase = dspZRead ();
			dspZAck (false, sync);
			break;
		}
		case 0x02:							// render frames
			u.zFrames = (m >> 16) & 0xFF;
			u.mix.outVolume = (u16) extra;
			u.zOutL = dspZRead (); u.zOutR = dspZRead ();
			if (u.flags & Z_COMBINED_CMD_0D) { dspZRead (); dspZRead (); }
			u.zFrame = 0; u.zVoice = 0;
			if (u.flags & Z_LIGHT) { dspZAck (false, u.zFrames); u.state = ZS_RENDERING; }
			else dspZRender ();
			return;
		case 0x0C:
			if (u.flags & Z_GBA_CRYPTO) dspZRead ();		// (the GBA's key: not computed)
			else if (u.flags & Z_WEIRD_CMD_0C) { dspZRead (); dspZRead (); }
			dspZAck (false, sync);
			break;
		case 0x0D:
			if (!(u.flags & Z_NO_CMD_0D)) dspZRead ();
			dspZAck (false, sync);
			break;
		case 0x0E:
			dspZRead ();
			dspZAck (false, sync);
			break;
		default:							// (04-09 crash the microcode)
			u.state = ZS_HALTED;
			return;
		}
	}
}

void Machine::dspZWrite (u32 v) { DspUcode &u = dspUc; u.zCmd[u.zWr] = v; u.zWr = (u.zWr + 1) & 63; }

void Machine::dspZeldaMail (u32 mail)
{
	DspUcode &u = dspUc;
	if (u.flags & Z_LIGHT)
	{
		switch (u.state)
		{
		case ZS_WAITING:
		{
			dspZWrite (mail);
			bool add = true;
			switch ((mail >> 24) & 0x7F)
			{
			case 0x01: u.zExpected = 4; break;
			case 0x02: u.zExpected = 2; break;
			case 0x03: add = false; u.zExpected = 0; break;
			case 0x0C: u.zExpected = (u.flags & Z_GBA_CRYPTO) ? 1 : (u.flags & Z_WEIRD_CMD_0C) ? 2 : 0; break;
			default: u.zExpected = 0; break;
			}
			if (u.zExpected) u.state = ZS_WRITING_CMD;
			else if (add) { u.zPending++; dspZRun (); }
			break;
		}
		case ZS_WRITING_CMD:
			dspZWrite (mail);
			if (--u.zExpected == 0) { u.zPending++; u.state = ZS_WAITING; dspZRun (); }
			break;
		case ZS_RENDERING:						// (no per-voice sync)
			u.zSyncMax = 0xFFFFFFFFu;
			for (int i = 0; i < 256; i++) u.zSkip[i] = 0xFFFF;
			dspZRender ();
			dspInterrupt (0);
			break;
		}
		return;
	}
	switch (u.state)
	{
	case ZS_WAITING:
		if (mail & 0x80000000u)						// the answer to DSP_FRAME_END
		{
			mail = 0xCDD10000u | (mail & 0xFFFF);
			if (mail == MAIL_NEW_UCODE) { u.zCanExec = true; dspZRun (); u.uploading = true; }
			else if (mail == MAIL_RESET) { u.state = ZS_HALTED; dspSetProgram (1); }
			else if (mail == MAIL_CONTINUE) { u.zCanExec = true; dspZRun (); }
			else u.state = ZS_HALTED;
		}
		else if (!(mail & 0xFFFF)) u.state = u.zFrame != u.zFrames ? ZS_RENDERING : ZS_HALTED;	// a sync: the next mail
		else { u.state = ZS_WRITING_CMD; u.zExpected = mail & 0xFFFF; }	// a batch of commands: its words
		break;
	case ZS_RENDERING:							// which voices are ready
		if (u.flags & Z_SYNC_PER_FRAME)					// (64 voices' flags in two mails)
		{
			int base = u.zSecondHalf ? 2 : 0;
			u.zSkip[base] = (u16) (mail >> 16); u.zSkip[base + 1] = (u16) mail;
			if (u.zSecondHalf) u.zSyncMax = 0xFFFF;
			dspZRender ();
			if (u.zSecondHalf) u.state = ZS_WAITING;
			u.zSecondHalf = !u.zSecondHalf;
		}
		else								// (16 more voices: which of them play)
		{
			u.zSyncMax = (((mail >> 16) & 0xF) + 1) << 4;
			u.zSkip[(mail >> 16) & 0xFF] = (u16) mail;
			dspZRender ();
			u.state = ZS_WAITING;
		}
		break;
	case ZS_WRITING_CMD:
		dspZWrite (mail);
		if (--u.zExpected == 0) { u.zPending++; u.state = ZS_WAITING; dspZRun (); }
		break;
	}
}

// ---- a mail from the CPU ------------------------------------------------------------------------
void Machine::dspMailReceived (u32 mail)
{
	dspMailIn = mail & 0x7FFFFFFF;					// (taken at once: the CPU sees it read)
	dspMailsIn++; dspLastMail = mail;
	DTRACE ("CPU -> %08X (pc %08X)\n", mail, curPc);
	if (dspBootStep == 1)						// the ROM: the microcode's description
	{
		if (!dspBootKey)
		{
			if ((mail & 0xFFFF0000u) == 0x80F30000u) dspBootKey = mail;
			else dspPush (0xFEEE0000u | (mail & 0xFFFF), false);
			return;
		}
		u32 key = dspBootKey; dspBootKey = 0;
		if (key == 0x80F3A001u) dspBootMails[0] = mail;
		else if (key == 0x80F3A002u) dspBootMails[1] = mail & 0xFFFF;
		else if (key == 0x80F3D001u) dspStartUcode (dspBootMails[0], dspBootMails[1]);
		return;
	}
	if (dspBootStep != 2) return;					// (the init code, nothing: no mails)
	if (dspUc.uploading) { dspUpload (mail); return; }
	switch (dspUc.kind)
	{
	case DSP_AX: dspAxMail (mail); break;
	case DSP_ZELDA: dspZeldaMail (mail); break;
	case DSP_CARD:							// the card's unlock: one request, done
		if (dspUc.state == 0) { if (mail == 0xFF000000u) dspUc.state = 1; }
		else if (dspUc.state == 1) { dspPush (DSP_DONE, true); dspUc.state = 2; }
		else
		{
			mail = 0xCDD10000u | (mail & 0xFFFF);
			if (mail == MAIL_NEW_UCODE) dspUc.uploading = true;
			else if (mail == MAIL_RESET) dspSetProgram (1);
		}
		break;
	}
}

} // namespace gc
