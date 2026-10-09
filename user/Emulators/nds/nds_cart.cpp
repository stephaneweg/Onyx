//
// nds/nds_cart.cpp -- the game card: its commands after the boot (B7: read 0x200 bytes, the 4 KB
// page wrapping; B8: the chip ID), the ROM control register and the data port (a DMA or the
// processor takes the words), the transfer-done interrupt; the save chip on the AUXSPI: EEPROM
// 512 B (1 address byte, A8 in the command), EEPROM / FRAM (2 address bytes, 64 KB here), EEPROM
// 128 KB (3) and Flash (3 address bytes, 512 KB here), found from the save's size or else from the
// game's first write (its length and its address: a whole page of one of them); the secure area's
// KEY1 decryption (only with the ARM7 BIOS's key table, given by the user: most dumps do not need it).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see nds.h).
//
#include "nds/nds.h"

namespace nds {

void Cart::reset ()
{
	for (int i = 0; i < 8; i++) cmd[i] = 0;
	romctrl = 0; spicnt = 0;
	xferAddr = xferLeft = xferPos = 0; xferCmd = 0;
	dataLatch = 0; dataReady = false;
	spiState = 0; spiCmd = 0; spiAddrBytes = 0; spiAddrGot = 0; spiAddr = 0; spiWel = false; spiStatus = 0;
	detectLen = 0; detectCmd = 0; spiOut = 0;
}

// (no allocation: the machine may run on an app core -- the buffer is made with the machine, SAVE_MAX)
void Cart::setType (int t, u32 size)
{
	saveType = t;
	if (size > SAVE_MAX) size = SAVE_MAX;
	if (size != saveSize)
	{
		for (u32 i = saveSize; i < size; i++) save[i] = 0xFF;
		saveSize = size;
	}
	spiAddrBytes = t == SAVE_EEPROM512 ? 1 : t == SAVE_EEPROM ? 2 : 3;
}

// The first complete write of a game with no save yet: the address bytes that make it one page.
void Cart::detect ()
{
	int n = detectLen;
	const u8 *b = detectBuf;
	int type = SAVE_UNKNOWN;
	if (detectCmd == 0x9F || detectCmd == 0xDB || detectCmd == 0xD8) type = SAVE_FLASH;
	else if (detectCmd == 0x02 || detectCmd == 0x0A)
	{
		// whole pages first: 16 (EEPROM 512 B), 32 / 128 (EEPROM), 256 (Flash)
		if (n > 3 + 256) type = SAVE_EEPROM;				// (longer than any page: FRAM, 2 address bytes)
		else if (n == 1 + 16 && (b[0] & 15) == 0) type = SAVE_EEPROM512;
		else if ((n == 2 + 32 || n == 2 + 128) && (b[1] & 31) == 0) type = SAVE_EEPROM;
		else if (n == 3 + 256 && b[2] == 0) type = SAVE_FLASH;
		else
		{
			// else what fits in a page of each
			int d2 = n - 2, d3 = n - 3, d1 = n - 1;
			u32 a2 = (u32) (b[0] << 8 | b[1]), a3 = (u32) (b[0] << 16 | b[1] << 8 | b[2]);
			if (d2 > 0 && d2 <= 128 && (a2 & 127) + (u32) d2 <= 128) type = SAVE_EEPROM;
			else if (d3 > 0 && d3 <= 256 && (a3 & 255) + (u32) d3 <= 256 && a3 < 0x100000) type = SAVE_FLASH;
			else if (d1 > 0 && d1 <= 16) type = SAVE_EEPROM512;
			else type = SAVE_FLASH;
		}
	}
	if (type == SAVE_UNKNOWN) return;
	if (type == SAVE_EEPROM512) setType (type, 512);
	else if (type == SAVE_EEPROM) setType (type, 0x10000);
	else setType (type, 0x80000);
	// the write it was made of, done now
	int ab = spiAddrBytes;
	u32 a = 0;
	for (int i = 0; i < ab && i < n; i++) a = (a << 8) | b[i];
	if (saveType == SAVE_EEPROM512 && detectCmd == 0x0A) a |= 0x100;
	for (int i = ab; i < n; i++) { save[a % saveSize] = b[i]; a++; }
	saveDirty = true;
}

u8 Cart::spiXfer (u8 v, bool hold)
{
	u8 out = 0xFF;
	if (spiState == 0)
	{
		spiCmd = v; spiState = 1; spiAddr = 0; spiAddrGot = 0;
		if (v == 0x06) spiWel = true;
		else if (v == 0x04) spiWel = false;
		if (saveType == SAVE_UNKNOWN) { detectCmd = v; detectLen = 0; }
		out = 0xFF;
	}
	else if (saveType == SAVE_UNKNOWN)
	{
		if (detectLen < (int) sizeof detectBuf) detectBuf[detectLen++] = v;
		out = spiCmd == 0x05 ? (u8) (spiWel ? 2 : 0) : 0xFF;
	}
	else if (saveType == SAVE_NONE) out = 0xFF;
	else
	{
		int c = spiCmd;
		bool e512 = saveType == SAVE_EEPROM512;
		bool isRead = c == 0x03 || (c == 0x0B);
		bool isWrite = c == 0x02 || c == 0x0A;
		switch (c)
		{
		case 0x05: out = (u8) (spiWel ? 2 : 0); break;
		case 0x01: break;					// (status write: nothing kept)
		case 0x9F: { static const u8 ID[3] = { 0xC2, 0x22, 0x13 }; out = spiAddrGot < 3 ? ID[spiAddrGot] : 0xFF; spiAddrGot++; break; }
		default:
			if (!isRead && !isWrite && c != 0xDB && c != 0xD8) break;
			if (spiAddrGot < spiAddrBytes)
			{
				spiAddr = (spiAddr << 8) | v; spiAddrGot++;
				if (spiAddrGot == spiAddrBytes)
				{
					if (e512 && (c == 0x0B || c == 0x0A)) spiAddr |= 0x100;
					if (c == 0xDB) { u32 p = spiAddr & ~0xFFu; for (u32 i = 0; i < 256; i++) save[(p + i) % saveSize] = 0xFF; saveDirty = true; }
					if (c == 0xD8) { u32 p = spiAddr & ~0xFFFFu; for (u32 i = 0; i < 0x10000; i++) save[(p + i) % saveSize] = 0xFF; saveDirty = true; }
					if (c == 0x0B && !e512) spiAddrGot = 100;	// (Flash fast read: a dummy byte next)
				}
				break;
			}
			if (spiAddrGot == 100) { spiAddrGot = spiAddrBytes; break; }
			if (isRead) { out = save[spiAddr % saveSize]; spiAddr++; }
			else if (isWrite && (spiWel || saveType != SAVE_FLASH))
			{
				save[spiAddr % saveSize] = v; saveDirty = true;
				// (within its page)
				u32 page = saveType == SAVE_FLASH ? 256 : saveType == SAVE_EEPROM512 ? 16 : 128;
				spiAddr = (spiAddr & ~(page - 1)) | ((spiAddr + 1) & (page - 1));
			}
			break;
		}
	}
	if (!hold) spiEnd ();
	return out;
}

void Cart::spiEnd ()
{
	if (spiState && saveType == SAVE_UNKNOWN && (detectCmd == 0x02 || detectCmd == 0x0A || detectCmd == 0x9F || detectCmd == 0xDB || detectCmd == 0xD8) && (detectLen > 0 || detectCmd == 0x9F)) detect ();
	if (spiState && (spiCmd == 0x02 || spiCmd == 0x0A || spiCmd == 0xDB || spiCmd == 0xD8)) spiWel = false;
	spiState = 0;
}

void Machine::cartSpiCnt (u16 v)
{
	u16 old = cart.spicnt;
	cart.spicnt = (u16) ((v & 0xE043) | (old & 0x80) );
	if ((old & 0x40) && !(v & 0x40)) cart.spiEnd ();		// (chip select released)
}

void Machine::cartSpiData (u8 v)
{
	if (!(cart.spicnt & 0x8000) || !(cart.spicnt & 0x2000)) return;
	cart.spiOut = cart.spiXfer (v, (cart.spicnt & 0x40) != 0);
}

// ---- the ROM ---------------------------------------------------------------------------------------------------------
static u32 cartWord (Machine *m, u32 pos)
{
	Cart &c = m->cart;
	switch (c.xferCmd)
	{
	case 0xB7:
	{
		u32 a = (c.xferAddr & ~0xFFFu) | ((c.xferAddr + pos) & 0xFFF);
		a &= c.romMask;
		if (a < 0x8000) a = 0x8000 + (a & 0x1FF);
		u32 v = 0;
		for (int i = 0; i < 4; i++) v |= (u32) (a + (u32) i < c.romSize ? c.rom[a + (u32) i] : 0xFF) << (i * 8);
		return v;
	}
	case 0x00:							// the header (before the boot: not used by games)
	{
		u32 a = (pos & 0xFFF);
		u32 v = 0;
		for (int i = 0; i < 4; i++) v |= (u32) (a + (u32) i < c.romSize ? c.rom[a + (u32) i] : 0xFF) << (i * 8);
		return v;
	}
	case 0xB8: case 0x90: case 0x1B8: return c.chipId;
	default: return 0xFFFFFFFF;
	}
}

void Machine::cartRomCtrl (int cpu, u32 v)
{
	Cart &c = cart;
	c.romctrl = (c.romctrl & 0x00800000) | (v & 0xFF7FFFFF);
	if (!(v & 0x80000000)) { c.romctrl &= ~0x00800000u; return; }
	int bs = (int) (v >> 24) & 7;
	u32 len = bs == 0 ? 0 : bs == 7 ? 4 : 0x100u << bs;
	u8 c0 = c.cmd[0];
	c.xferCmd = c0;
	c.xferAddr = ((u32) c.cmd[1] << 24) | ((u32) c.cmd[2] << 16) | ((u32) c.cmd[3] << 8) | c.cmd[4];
	c.xferPos = 0;
	c.xferLeft = len;
	if (!len) { cartDone (cpu); return; }
	c.romctrl |= 0x00800000;
	dmaStart (cpu, 5);
}

u32 Machine::cartReadData (int cpu)
{
	Cart &c = cart;
	if (!c.xferLeft) return 0xFFFFFFFF;
	u32 v = cartWord (this, c.xferPos);
	c.xferPos += 4; c.xferLeft -= 4;
	if (!c.xferLeft) cartDone (cpu);
	return v;
}

void Machine::cartDone (int cpu)
{
	cart.romctrl &= ~0x80800000u;
	if (cart.spicnt & 0x4000) raise (cpu, 19);
	(void) cpu;
}

// ---- the secure area (KEY1) ---------------------------------------------------------------------------------------------
namespace {
struct Key1
{
	u32 kb[0x412];
	u32 code[3];
	u32 f (u32 x) const
	{
		u32 r = kb[0x12 + (x >> 24)];
		r += kb[0x112 + ((x >> 16) & 0xFF)];
		r ^= kb[0x212 + ((x >> 8) & 0xFF)];
		r += kb[0x312 + (x & 0xFF)];
		return r;
	}
	void encrypt (u32 *p) const
	{
		u32 y = p[0], x = p[1];
		for (int i = 0; i < 16; i++) { u32 z = kb[i] ^ x; x = f (z) ^ y; y = z; }
		p[0] = x ^ kb[16]; p[1] = y ^ kb[17];
	}
	void decrypt (u32 *p) const
	{
		u32 y = p[0], x = p[1];
		for (int i = 17; i >= 2; i--) { u32 z = kb[i] ^ x; x = f (z) ^ y; y = z; }
		p[0] = x ^ kb[1]; p[1] = y ^ kb[0];
	}
	void apply (int modulo)
	{
		encrypt (code + 1);
		encrypt (code + 0);
		u32 scratch[2] = { 0, 0 };
		for (int i = 0; i < 0x12; i++)
		{
			u32 k = code[i % (modulo / 4)];
			kb[i] ^= (k >> 24) | ((k >> 8) & 0xFF00) | ((k << 8) & 0xFF0000) | (k << 24);
		}
		for (int i = 0; i < 0x412; i += 2) { encrypt (scratch); kb[i] = scratch[1]; kb[i + 1] = scratch[0]; }
	}
	void init (const u8 *table, u32 id, int level, int modulo)
	{
		for (int i = 0; i < 0x412; i++) kb[i] = rd32 (table + i * 4);
		code[0] = id; code[1] = id / 2; code[2] = id * 2;
		if (level >= 1) apply (modulo);
		if (level >= 2) apply (modulo);
		code[1] *= 2; code[2] /= 2;
		if (level >= 3) apply (modulo);
	}
};
}

void Machine::decryptSecureArea (u8 *arm9)
{
	static Key1 k;
	u32 id = rd32 (cart.rom + 0x0C);
	u32 w[2];
	k.init (bios7Key, id, 2, 8);
	w[0] = rd32 (arm9); w[1] = rd32 (arm9 + 4);
	k.decrypt (w);
	wr32 (arm9, w[0]); wr32 (arm9 + 4, w[1]);
	k.init (bios7Key, id, 3, 8);
	for (int i = 0; i < 0x800; i += 8)
	{
		w[0] = rd32 (arm9 + i); w[1] = rd32 (arm9 + i + 4);
		k.decrypt (w);
		wr32 (arm9 + i, w[0]); wr32 (arm9 + i + 4, w[1]);
	}
}

} // namespace nds
