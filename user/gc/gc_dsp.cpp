//
// gc/gc_dsp.cpp -- the DSP at a high level: what its ROM and the games' microcodes answer through
// the mailboxes, not their instructions. After a reset the ROM mails 0x8071FEED; the CPU then
// sends the microcode's description (IRAM / DRAM addresses and sizes, its start: 0x80F3xxxx
// mail pairs) and the microcode starts: it mails DSP_INIT (0xDCD10000). Then the AX microcode
// (most games' audio) takes a command list a frame (0xBABExxxx: its size, then its address) and
// answers when done (the audio mixing itself comes later: silent for now).
//
#include "gc/gc.h"

namespace gc {

enum { PI_DSP = 0x40 };
enum { DSP_INIT = 0xDCD10000u, DSP_RESUME = 0xDCD10001u, DSP_YIELD = 0xDCD10002u, DSP_DONE = 0xDCD10003u,
       DSP_SYNC = 0xDCD10004u, DSP_FRAME_END = 0xDCD10005u };

void Machine::dspIrqUpdate ()
{
	u16 c = dspReg[0x0A / 2];
	bool on = ((c & 0x08) && (c & 0x10)) || ((c & 0x20) && (c & 0x40)) || ((c & 0x80) && (c & 0x100));
	if (on) piRaise (PI_DSP); else piLower (PI_DSP);
}

// a mail from the DSP: queued; the CPU reads them in order (DSPINT when asked)
void Machine::dspPush (u32 mail, bool irq)
{
	int next = (dspQTail + 1) & 15;
	if (next != dspQHead) { dspQueue[dspQTail] = mail; dspQTail = next; }
	if (irq) { dspReg[0x0A / 2] |= 0x80; dspIrqUpdate (); }
	dspHleStep ();
}

// the next queued mail into the DSP -> CPU mailbox once the last one was read
void Machine::dspHleStep ()
{
	if (dspMailOutValid || dspQHead == dspQTail) return;
	dspMailOut = dspQueue[dspQHead]; dspQHead = (dspQHead + 1) & 15;
	dspMailOutValid = true;
}

void Machine::dspReset ()
{
	dspQHead = dspQTail = 0; dspMailOutValid = false;
	dspBootN = 0; dspBootKey = 0; dspUcode = 0; dspCmdlistLeft = 0; dspBootStep = 1;
	dspReg[0x0A / 2] &= ~0x0801u;
	dspPush (0x8071FEEDu, false);					// the ROM is ready
}

void Machine::dspMailReceived (u32 mail)
{
	dspMailIn = mail & 0x7FFFFFFF;					// (read at once: the CPU sees it taken)
	dspMailsIn++; dspLastMail = mail;
	if (dspBootStep == 1)						// the ROM: the microcode's description, in pairs
	{
		// key (0x80F3xxxx), then its value (as Dolphin's ROM HLE): A001 its address in main memory,
		// A002 its length, C002 its IRAM address, B002 the DRAM length, D001 its start: it runs.
		// By key, not by place: some games send more (or boot the DSP twice without a reset).
		u32 m = mail | 0x80000000u;
		if (dspBootKey == 0 && (m & 0xFFFF0000u) == 0x80F30000u) { dspBootKey = m; return; }
		u32 key = dspBootKey; dspBootKey = 0;
		u32 v = mail & 0x7FFFFFFF;
		if (key == 0x80F3A001u) dspBootMails[0] = v;
		else if (key == 0x80F3A002u) dspBootMails[1] = v;
		else if (key == 0x80F3D001u)
		{
			// which microcode: a checksum of its IRAM image (the AX one, the Zelda ones, the IPL's...)
			u32 iram = dspBootMails[0], len = dspBootMails[1];
			if (len > 0x2000) len = 0x2000;
			u32 h = 0;
			for (u32 i = 0; i < len && (iram & 0x01FFFFFF) + i < MEM1_SIZE; i++) h = h * 31 + mem1[(iram & 0x01FFFFFF) + i];
			dspUcode = h; dspBootStep = 2;
			dspPush (DSP_INIT, true);
		}
		return;
	}
	// the AX microcode: 0xBABE + the command list's size, then its address; done at once
	if (dspCmdlistLeft == 0 && (mail & 0x7FFF0000u) == 0x3ABE0000u) { dspCmdlistLeft = 1; return; }
	if (dspCmdlistLeft) { dspCmdlistLeft = 0; dspPush (DSP_SYNC, true); return; }
}

} // namespace gc
