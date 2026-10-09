//
// n3ds/n3ds_cfg.cpp -- the small system services a program asks about the console and its user:
//   cfg:u / cfg:s / cfg:i  the console's settings (the region, the model, and "blocks" by number: the language,
//                          the user's name, the birthday, the country, the sound's output...);
//   ptm:u / ptm:sysm       power and the like (the lid, the battery, the step counter).
// Nothing of a real console: an Old 3DS of the European region, the user's name and language given by the host
// (Machine::setUser: Onyx's own settings), a full battery, the lid open.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (see n3ds.h).
//
#include <string.h>
#include "n3ds/n3ds.h"

namespace n3ds {

void Machine::setUser (const char *name, int language, int region)
{
	int n = 0;
	for (; name && name[n] && n < 10; n++) user.name[n] = (u16) (u8) name[n];
	for (; n < 11; n++) user.name[n] = 0;
	user.language = (u8) language; user.region = (u8) region;
}

// A settings block: its bytes, or 0 when we do not have it.
static u32 block (const Machine *m, u32 id, u8 *out, u32 size)
{
	memset (out, 0, size);
	switch (id)
	{
	case 0x00050005: if (size >= 4) { const float f = 62.0f; memcpy (out, &f, 4); } return size;	// stereo camera settings (the eyes' distance first)
	case 0x00070001: out[0] = 1; return 1;								// sound: stereo
	case 0x000A0000: if (size >= 0x1C) memcpy (out, m->user.name, 22); return 0x1C;			// the user's name (UTF-16)
	case 0x000A0001: out[0] = 1; if (size > 1) out[1] = 1; return 2;					// birthday: the 1st of January
	case 0x000A0002: out[0] = m->user.language; return 1;						// the language
	case 0x000B0000: if (size >= 4) { out[2] = 1; out[3] = m->user.region == 2 ? 77 : 49; } return 4;	// the country (France, or the USA)
	case 0x000B0001: case 0x000B0002: return size;							// the country's, the state's names
	case 0x000B0003: return size;									// coordinates
	case 0x000C0000: return size;									// parental controls: none
	case 0x000D0000: if (size >= 4) { out[0] = 1; out[1] = 0; out[2] = 1; out[3] = 0; } return 4;	// the EULA: accepted
	case 0x000F0004: return 4;									// the model: an Old 3DS
	case 0x00030001: return 8;									// the user's time offset
	case 0x00090001: return 8;									// the console's unique id
	case 0x00130000: return 4;									// debug mode: off
	case 0x00160000: return 4;
	case 0x000E0000: return size;
	}
	return 0;
}

void cfgRequest (Machine *m, Session *s, u32 *cmd)
{
	const u32 id = cmd[0] >> 16;
	switch (id)
	{
	case 0x0001:								// GetConfigInfoBlk2 (size, block, the buffer)
	case 0x0401:								// GetConfigInfoBlk8 (the same, for cfg:s / cfg:i)
	case 0x0801:
	{
		const u32 size = cmd[1], blk = cmd[2], dst = cmd[4];
		u8 buf[0x800];
		u32 got = size <= sizeof buf ? block (m, blk, buf, size) : 0;
		if (!got) { m->note ("%s block %08x", s->name, (unsigned) blk); cmd[0] = ipcHeader (id, 1, 0); cmd[1] = 0xD8A103F9; break; }	// (not found)
		m->mem.write (dst, buf, size);
		cmd[0] = ipcHeader (id, 1, 2); cmd[1] = RES_OK;
		break;
	}
	case 0x0002:								// SecureInfoGetRegion -> 0 Japan, 1 USA, 2 Europe...
	case 0x0406: case 0x0816:
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = m->user.region;
		break;
	case 0x0003:								// GenHashConsoleUnique (a salt) -> a 64-bit number
		cmd[0] = ipcHeader (id, 3, 0); cmd[2] = cmd[1] * 0x9E3779B1u + 0x0BADC0DE; cmd[3] = 0x4F6E7978; cmd[1] = RES_OK;
		break;
	case 0x0004:								// GetRegionCanadaUSA -> no
	case 0x0005:								// GetSystemModel -> an Old 3DS
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 0;
		break;
	case 0x0006:								// GetModelNintendo2DS -> 0 for a 2DS
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 1;
		break;
	default:
		ipcStub (m, s->name, cmd);
		break;
	}
}

void ptmRequest (Machine *m, Session *s, u32 *cmd)
{
	const u32 id = cmd[0] >> 16;
	switch (id)
	{
	case 0x0005:								// GetAdapterState -> plugged in
	case 0x0006:								// GetShellState -> open
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 1;
		break;
	case 0x0007:								// GetBatteryLevel -> full (0..5)
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 5;
		break;
	case 0x0008:								// GetBatteryChargeState -> not charging
	case 0x0009:								// GetPedometerState -> off
	case 0x000C:								// GetTotalStepCount
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 0;
		break;
	case 0x080F:								// (sysm) IsLegacyPowerOff, and the notifications' acknowledgements
	default:
		if (id >= 0x0401) { cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 0; break; }	// ptm:sysm's: accepted
		ipcStub (m, s->name, cmd);
		break;
	}
}

}
