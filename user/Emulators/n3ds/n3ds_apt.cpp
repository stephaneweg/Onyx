//
// n3ds/n3ds_apt.cpp -- APT (APT:U, APT:S, APT:A), the service that runs the application's life with the system's
// applets (the HOME menu, the software keyboard...). Here there is only the application: it gets its lock and its
// two events, and when it enables itself the "system" sends it the parameter every application waits for at its
// start -- the wake-up. No HOME menu, no sleep, no other applet yet.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (see n3ds.h).
//
#include <string.h>
#include "n3ds/n3ds.h"

namespace n3ds {

enum { APPID_HOME_MENU = 0x101, APPID_APPLICATION = 0x300, APTCMD_WAKEUP = 1 };

static u32 share (Machine *m, Object *o) { o->refs++; return m->handleNew (o); }

static u32 get32 (const u8 *p) { return (u32) p[0] | (u32) p[1] << 8 | (u32) p[2] << 16 | (u32) p[3] << 24; }
static void put32 (u8 *p, u32 v) { p[0] = (u8) v; p[1] = (u8) (v >> 8); p[2] = (u8) (v >> 16); p[3] = (u8) (v >> 24); }

// The font goes into FCRAM where the console keeps it (the programs map it at VA_FONT, and give the GPU its
// glyph sheets by their linear address), after a header of 0x80 bytes. A BCFNT file holds offsets from its start;
// the console hands the font with addresses instead ("CFNU"): the font's three tables, the sheets, and the
// chains of width and character-map blocks.
bool Machine::setSharedFont (const u8 *bcfnt, u32 size)
{
	if (size < 0x34 || size > FONT_SIZE - 0x80 || memcmp (bcfnt, "CFNT", 4) != 0 || memcmp (bcfnt + 0x14, "FINF", 4) != 0) return false;
	u8 *block = mem.fcram + FONT_FCRAM, *f = block + 0x80;
	const u32 base = VA_FONT + 0x80;
	memset (block, 0, FONT_SIZE);
	memcpy (f, bcfnt, size);
	put32 (block, 2); put32 (block + 4, 1); put32 (block + 8, size);		// loaded, the region, its size
	memcpy (f, "CFNU", 4);
	u8 *finf = f + 0x14;
	const u32 tglp = get32 (finf + 0x10), cwdh = get32 (finf + 0x14), cmap = get32 (finf + 0x18);
	if (tglp < 0x34 || tglp + 0x18 > size) return false;
	put32 (finf + 0x10, tglp + base);
	put32 (f + tglp + 0x14, get32 (f + tglp + 0x14) + base);			// the sheets
	put32 (finf + 0x14, cwdh ? cwdh + base : 0);
	for (u32 at = cwdh, n = 0; at && n < 64; n++)
	{
		if (at + 8 > size) return false;
		const u32 next = get32 (f + at + 4);
		put32 (f + at + 4, next ? next + base : 0);
		at = next;
	}
	put32 (finf + 0x18, cmap ? cmap + base : 0);
	for (u32 at = cmap, n = 0; at && n < 64; n++)
	{
		if (at + 12 > size) return false;
		const u32 next = get32 (f + at + 8);
		put32 (f + at + 8, next ? next + base : 0);
		at = next;
	}
	apt.fontReady = true;
	return true;
}

void aptRequest (Machine *m, Session *s, u32 *cmd)
{
	const u32 id = cmd[0] >> 16;
	Machine::Apt &a = m->apt;
	switch (id)
	{
	case 0x01:								// GetLockHandle (flags) -> the applet's attributes, its state, a mutex
		if (!a.lock) a.lock = new Mutex;
		cmd[0] = ipcHeader (id, 3, 2); cmd[1] = RES_OK; cmd[2] = cmd[1]; cmd[3] = 0; cmd[4] = 0; cmd[5] = share (m, a.lock);
		cmd[2] = 0;
		break;
	case 0x02:								// Initialize (app id, attributes) -> the signal and the parameter events
		if (!a.signal) a.signal = new Event (RESET_ONESHOT);
		if (!a.param) a.param = new Event (RESET_ONESHOT);
		cmd[0] = ipcHeader (id, 1, 3); cmd[1] = RES_OK; cmd[2] = 0x04000000;
		cmd[3] = share (m, a.signal); cmd[4] = share (m, a.param);
		break;
	case 0x03:								// Enable (attributes): the system wakes the application up
		a.pending = true;
		if (a.param) { a.param->signaled = true; m->wakeWaiters (a.param); }
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;
		break;
	case 0x05:								// GetAppletManInfo (position) -> position, requested, HOME menu, current
		cmd[0] = ipcHeader (id, 5, 0); cmd[1] = RES_OK; cmd[2] = 0; cmd[3] = 0; cmd[4] = APPID_HOME_MENU; cmd[5] = APPID_APPLICATION;
		break;
	case 0x06:								// GetAppletInfo (app id) -> title, media, registered, loaded, attributes
		cmd[0] = ipcHeader (id, 7, 0); cmd[1] = RES_OK; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; cmd[6] = 1; cmd[7] = 0;
		break;
	case 0x09:								// IsRegistered (app id)
		cmd[0] = ipcHeader (id, 2, 0); cmd[2] = cmd[1] == APPID_APPLICATION || cmd[1] == APPID_HOME_MENU; cmd[1] = RES_OK;
		break;
	case 0x0B:								// InquireNotification (app id) -> none
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 0;
		break;
	case 0x0D:								// ReceiveParameter (app id, buffer size) -> sender, command, size, a handle, the buffer
	case 0x0E:								// GlanceParameter (the same, the parameter stays)
	{
		const u32 *stat = cmd + 0x40;					// (the thread's static buffers: where the data goes)
		const bool had = a.pending;
		if (id == 0x0D) a.pending = false;
		cmd[0] = ipcHeader (id, 4, 4);
		cmd[1] = had ? (u32) RES_OK : 0xC8A0CFEFu;			// (nothing sent)
		cmd[2] = APPID_HOME_MENU; cmd[3] = had ? APTCMD_WAKEUP : 0; cmd[4] = 0;
		cmd[5] = 0; cmd[6] = 0;						// (no handle)
		cmd[7] = 2; cmd[8] = stat[1];					// (an empty buffer)
		break;
	}
	case 0x44:								// GetSharedFont -> where to map it, the font's memory
	{
		if (!a.fontReady)
		{
			m->note ("the system font (no sysfont.bcfnt given)");
			cmd[0] = ipcHeader (id, 1, 0); cmd[1] = 0xC8A0CFEFu;
			break;
		}
		if (!a.font) a.font = new SharedMem (m->mem.fcram + FONT_FCRAM, FONT_SIZE, VA_FONT);
		cmd[0] = ipcHeader (id, 2, 2); cmd[1] = RES_OK; cmd[2] = VA_FONT; cmd[3] = 0; cmd[4] = share (m, a.font);
		break;
	}
	case 0x4B:								// AppletUtility (id, sizes, a buffer) -> the applet's result
	case 0x50:								// GetApplicationCpuTimeLimit -> percent
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = id == 0x50 ? a.cpuLimit : 0;
		break;
	case 0x4F:								// SetApplicationCpuTimeLimit (1, percent)
		a.cpuLimit = cmd[2];
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;
		break;
	case 0x51:								// GetStartupArgument -> none
	case 0x101:								// GetTargetPlatform -> an Old 3DS
	case 0x102:								// CheckNew3DS -> no
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 0;
		break;
	case 0x04:								// Finalize
	case 0x3E:								// ReplySleepQuery
	case 0x3F:								// ReplySleepNotificationComplete
	case 0x43:								// NotifyToWait
	case 0x55:								// SetScreencapPostPermission
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;
		break;
	default:
		ipcStub (m, s->name, cmd);
		break;
	}
}

}
