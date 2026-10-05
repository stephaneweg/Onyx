//
// gc/gc_card.cpp -- the memory cards (EXI channel 0 = slot A, channel 1 = slot B; chip select 0):
// Nintendo's flash protocol, a byte at a time -- a command, its address bytes, then the data
// (immediate, or by DMA): the ID (its size), the status, read, page program (128 bytes), sector
// erase (8 KB), chip erase, the interrupt when a command is done (after the flash's time). The
// flash is an image the front end keeps (Dolphin's .raw layout): an image never written is all
// 0xFF (erased), and the game offers to format it (its header then matches this console's SRAM).
// The card's unlock (the key the DSP computes) is not checked: the status says unlocked.
//
#include "gc/gc.h"
#ifdef GC_TRACE
#include <stdio.h>
#define CTRACE(...) printf (__VA_ARGS__)
#else
#define CTRACE(...) ((void) 0)
#endif

namespace gc {

enum { ST_BUSY = 0x80, ST_UNLOCKED = 0x40, ST_ERASE_ERR = 0x10, ST_PROG_ERR = 0x08, ST_READY = 0x01 };
enum { SECTOR = 0x2000 };
enum { CYC_ERASE = 5000, CYC_PROGRAM = 5000 };			// (a command's time, as Dolphin)

void Machine::cardInsert (int s, u8 *flash, u32 size)
{
	if (s < 0 || s > 1) return;
	card[s].flash = flash; card[s].size = flash ? size : 0; card[s].dirty = false;
	cardReset (s);
}

void Machine::cardReset (int s)						// (its protocol's state)
{
	Card &c = card[s];
	c.status = ST_BUSY | ST_UNLOCKED | ST_READY; c.cmd = 0; c.pos = 0; c.addr = 0;
	c.intSwitch = 0; c.intSet = false; c.doneAt = ~0ull;
	for (int i = 0; i < 128; i++) c.prog[i] = 0;
}

// the card's size code (in Mbits: 4 = 59 blocks ... 128 = 2043 blocks)
static u32 cardId (u32 size) { return size / (128 * 1024); }

// ---- the card's format (as the SDK's CARDFormat writes it) -------------------------------------------------------
// the additive / inverse checksums of big-endian 16-bit words -> 4 bytes at out
static void sum16 (const u8 *p, u32 n, u8 *out)
{
	u16 c = 0, ci = 0;
	for (u32 i = 0; i < n; i += 2) { u16 w = (u16) (p[i] << 8 | p[i + 1]); c = (u16) (c + w); ci = (u16) (ci + (u16) (w ^ 0xFFFF)); }
	if (c == 0xFFFF) c = 0;
	if (ci == 0xFFFF) ci = 0;
	out[0] = (u8) (c >> 8); out[1] = (u8) c; out[2] = (u8) (ci >> 8); out[3] = (u8) ci;
}
static inline u64 cardRand (u64 r) { return (r * 0x41C64E6Dull + 0x3039) >> 16; }

// A card in a slot at a reset: an erased one is formatted -- its serial made from this console's
// SRAM flash ID (as the SDK does) --, and the SRAM's flash ID is set from the card's header (as
// Dolphin does), so a card formatted elsewhere is this console's too.
void Machine::cardAttach (int s)
{
	Card &c = card[s];
	u8 *f = c.flash;
	u8 *fid = sram + 0x14 + s * 12;				// (OSSramEx.flashID[s])
	bool blank = true;
	for (int i = 0; i < 0x26 && blank; i++) if (f[i] != 0xFF) blank = false;
	if (blank)
	{
		for (u32 i = 0; i < 5 * SECTOR; i++) f[i] = 0xFF;
		u64 t = (u64) rtcBase * TB_HZ, r = t;		// (the time of the format)
		for (int i = 0; i < 12; i++) { r = cardRand (r); f[i] = (u8) (fid[i] + (u8) r); r = cardRand (r) & 0x7FFF; }
		for (int i = 0; i < 8; i++) f[0x0C + i] = (u8) (t >> (56 - i * 8));
		for (int i = 0; i < 4; i++) f[0x14 + i] = sram[0x0C + i];	// (the SRAM's counter bias)
		f[0x18] = f[0x19] = f[0x1A] = 0; f[0x1B] = sram[0x12];		// (its language)
		for (int i = 0x1C; i < 0x22; i++) f[i] = 0;			// (DTV status, device 0)
		u32 mb = cardId (c.size);
		f[0x22] = (u8) (mb >> 8); f[0x23] = (u8) mb; f[0x24] = 0; f[0x25] = 0;	// (its size, ANSI)
		sum16 (f, 0x1FC, f + 0x1FC);
		for (int d = 1; d <= 2; d++)				// the directory, twice: no file
		{
			u8 *b = f + d * SECTOR;
			b[0x1FFA] = b[0x1FFB] = 0;
			sum16 (b, 0x1FFC, b + 0x1FFC);
		}
		for (int d = 3; d <= 4; d++)				// the block allocation table, twice: all free
		{
			u8 *b = f + d * SECTOR;
			for (u32 i = 0; i < SECTOR; i++) b[i] = 0;
			u32 freeBlocks = c.size / SECTOR - 5;
			b[6] = (u8) (freeBlocks >> 8); b[7] = (u8) freeBlocks; b[9] = 4;
			sum16 (b + 4, SECTOR - 4, b);
		}
		c.dirty = true;
	}
	// the SRAM's flash ID from the header's serial and format time
	u64 r = 0; u8 sum = 0;
	for (int i = 0; i < 8; i++) r = r << 8 | f[0x0C + i];
	for (int i = 0; i < 12; i++) { r = cardRand (r); fid[i] = (u8) (f[i] - (u8) r); sum = (u8) (sum + fid[i]); r = cardRand (r) & 0x7FFF; }
	sram[0x3A + s] = (u8) (sum ^ 0xFF);
}

void Machine::cardDone (int s)
{
	Card &c = card[s];
	c.doneAt = ~0ull;
	c.status = (u8) ((c.status | ST_READY) & ~ST_BUSY);
	c.intSet = true;
	exiUpdate ();
}

// a byte through the card: in = the CPU's (a command's byte), -> the card's answer
u8 Machine::cardByte (int s, u8 in)
{
	Card &c = card[s];
	u8 out = 0xFF;
	u32 mask = c.size - 1;
	if (c.pos == 0)
	{
		c.cmd = in;
		if (in == 0x89)						// clear status (at once)
		{
			c.status = (u8) ((c.status & ~(ST_PROG_ERR | ST_ERASE_ERR)) | ST_READY);
			c.intSet = false;
			exiUpdate ();
			return 0xFF;
		}
	}
	else switch (c.cmd)
	{
	case 0x00:							// the ID: a dummy byte, then the size code
		out = c.pos == 1 ? 0x80 : (u8) (cardId (c.size) >> (24 - ((c.pos - 2) & 3) * 8));
		break;
	case 0x52:							// read: 4 address bytes, 4 dummy ones, then the data
		if (c.pos == 1) c.addr = (u32) in << 17;
		else if (c.pos == 2) c.addr |= (u32) in << 9;
		else if (c.pos == 3) c.addr |= (u32) (in & 3) << 7;
		else if (c.pos == 4) c.addr |= in & 0x7F;
		if (c.pos > 1)
		{
			out = c.flash[c.addr & mask];
			if (c.pos >= 9) c.addr = (c.addr & ~0x1FFu) | ((c.addr + 1) & 0x1FF);
		}
		break;
	case 0x83: out = c.status; break;				// the status
	case 0x85: out = (u8) ((c.pos & 1) ? 0x21 : 0xC2); break;	// the chip's ID (0xC221: Nintendo's)
	case 0x81: if (c.pos == 1) c.intSwitch = in; break;	// the interrupt on / off
	case 0xF1:							// sector erase: 2 address bytes (the rest at the end)
		if (c.pos == 1) c.addr = (u32) in << 17;
		else if (c.pos == 2) c.addr |= (u32) in << 9;
		break;
	case 0xF2:							// page program: 4 address bytes, the data
		if (c.pos == 1) c.addr = (u32) in << 17;
		else if (c.pos == 2) c.addr |= (u32) in << 9;
		else if (c.pos == 3) c.addr |= (u32) (in & 3) << 7;
		else if (c.pos == 4) c.addr |= in & 0x7F;
		else c.prog[(c.pos - 5) & 127] = in;
		break;
	default: break;							// (wake up, sleep, chip erase...)
	}
	CTRACE ("card %d: [%u] %02X -> %02X (cmd %02X addr %X)\n", s, c.pos, in, out, c.cmd, c.addr);
	c.pos++;
	return out;
}

// the chip select: selected, a new command; deselected, the command ends (erase, program)
void Machine::cardCs (int s, bool selected)
{
	Card &c = card[s];
	CTRACE ("card %d: %s (pc %08X)\n", s, selected ? "selected" : "deselected", curPc);
	if (selected) { c.pos = 0; return; }
	u32 mask = c.size - 1;
	switch (c.cmd)
	{
	case 0xF1:							// sector erase
		if (c.pos > 2)
		{
			u32 a = c.addr & mask & ~(SECTOR - 1u);
			for (u32 i = 0; i < SECTOR; i++) c.flash[a + i] = 0xFF;
			c.dirty = true;
			c.status = (u8) ((c.status | ST_BUSY) & ~ST_READY);
			c.doneAt = cycles + CYC_ERASE;
		}
		break;
	case 0xF4:							// chip erase
		if (c.pos > 2) { for (u32 i = 0; i < c.size; i++) c.flash[i] = 0xFF; c.dirty = true; }
		break;
	case 0xF2:							// page program: the bytes given (a DMA wrote its own)
		if (c.pos >= 5)
		{
			u32 n = c.pos - 5;
			for (u32 i = 0; i < n; i++)
			{
				c.flash[c.addr & mask] = c.prog[i & 127];
				c.addr = (c.addr & ~0x1FFu) | ((c.addr + 1) & 0x1FF);
			}
			c.dirty = true;
			c.status = (u8) (c.status & ~ST_BUSY);
			c.doneAt = cycles + CYC_PROGRAM;
		}
		break;
	}
	c.cmd = 0xFF;
}

// the data of a command by DMA: read from / written to the flash at its address -> the cycles it takes
u32 Machine::cardDma (int s, u32 mem, u32 len, bool toMem)
{
	Card &c = card[s];
	u32 m = mem & 0x01FFFFFF, mask = c.size - 1;
	if (m + len > MEM1_SIZE) len = m < MEM1_SIZE ? MEM1_SIZE - m : 0;
	CTRACE ("card %d: DMA %s %X bytes at card %X, memory %08X (pc %08X)\n", s, toMem ? "read" : "write", len, c.addr, mem, curPc);
	if (toMem)
	{
		jitInvalidate (m, len);
		for (u32 i = 0; i < len; i++) mem1[m + i] = c.flash[(c.addr + i) & mask];
		return len * (CPU_HZ / (512 * 1024));			// (512 KB/s)
	}
	for (u32 i = 0; i < len; i++) c.flash[(c.addr + i) & mask] = mem1[m + i];
	c.dirty = true;
	return len * (CPU_HZ / 98432);					// (96 KB/s)
}

} // namespace gc
