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
#include <stdlib.h>

namespace n3ds {

enum { APPID_HOME_MENU = 0x101, APPID_APPLICATION = 0x300, APTCMD_WAKEUP = 1, APTCMD_REQUEST = 2, APTCMD_RESPONSE = 3, APTCMD_WAKEUP_EXIT = 0xA };

static u32 share (Machine *m, Object *o) { o->refs++; return m->handleNew (o); }

static u32 get32 (const u8 *p) { return (u32) p[0] | (u32) p[1] << 8 | (u32) p[2] << 16 | (u32) p[3] << 24; }
static void put32 (u8 *p, u32 v) { p[0] = (u8) v; p[1] = (u8) (v >> 8); p[2] = (u8) (v >> 16); p[3] = (u8) (v >> 24); }

// The font goes into FCRAM where the console keeps it (the programs map it past their own share (Memory::fontVa), and give the GPU its
// glyph sheets by their linear address), after a header of 0x80 bytes. A BCFNT file holds offsets from its start;
// the console hands the font with addresses instead ("CFNU"): the font's three tables, the sheets, and the
// chains of width and character-map blocks.
bool Machine::setSharedFont (const u8 *bcfnt, u32 size)
{
	if (size < 0x34 || size > FONT_SIZE - 0x80 || memcmp (bcfnt, "CFNT", 4) != 0 || memcmp (bcfnt + 0x14, "FINF", 4) != 0) return false;
	if (bcfnt != fontFile)								// (kept: a game's memory mode moves the font)
	{
		u8 *copy = (u8 *) malloc (size);
		if (!copy) return false;
		memcpy (copy, bcfnt, size);
		free (fontFile); fontFile = copy; fontFileSize = size; bcfnt = copy;
	}
	u8 *block = mem.fcram + mem.appMem, *f = block + 0x80;
	const u32 base = mem.fontVa () + 0x80;
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

// The Mii chooser's answer: a guest Mii was picked. The console has no Mii of the user's here, and a game may not
// take "none" for an answer (Super Mario 3D Land stops). The Mii is OURS -- plain middle values for every feature,
// the user's name -- in the console's layout: 0x5C bytes, then their CRC-16 (CCITT, big-endian) after two zeros.
static void miiGuest (Machine *m, u8 *out)
{
	memset (out, 0, 0x84);
	put32 (out, 0); put32 (out + 4, 1); put32 (out + 8, 0);			// one was selected; a guest; the first
	u8 *mii = out + 0xC;
	mii[0] = 3; mii[1] = 0x01; mii[3] = 0x30;				// the format's version; may be copied, no region lock, the Japan / America / Europe character set (0x30 here was Taiwan's); made on a 3DS
	// (as a Mii made in this console's Mii Maker: the console's number, the Mii's own -- an ordinary one's, with its
	// date --, the maker's network address, and below the author's name. A Mii with none of them is nobody's: Mario
	// Kart 7 then says there is no Mii in Mii Maker)
	memcpy (mii + 4, "OnyxPi4", 8);
	mii[0xC] = 0x91; mii[0xD] = 0x23; mii[0xE] = 0x45; mii[0xF] = 0x67;
	mii[0x10] = 0x02; mii[0x11] = 0x4F; mii[0x12] = 0x6E; mii[0x13] = 0x79; mii[0x14] = 0x78; mii[0x15] = 0x01;
	mii[0x02] = 0x10;							// its place in Mii Maker: the first
	mii[0x18] = 0; mii[0x19] = 0x44;					// a man, no birthday, a colour; a favourite (the user's own)
	for (int i = 0; i < 10 && m->user.name[i]; i++) { mii[0x1A + i * 2] = (u8) m->user.name[i]; mii[0x1B + i * 2] = (u8) (m->user.name[i] >> 8); }
	if (!m->user.name[0]) { mii[0x1A] = 'O'; mii[0x1C] = 'n'; mii[0x1E] = 'y'; mii[0x20] = 'x'; }
	memcpy (mii + 0x48, mii + 0x1A, 20);					// the author: the user
	mii[0x2E] = 64; mii[0x2F] = 64;						// width, height
	put32 (mii + 0x34, 4u << 9 | 3u << 13 | 4u << 16 | 2u << 21 | 12u << 25);	// the eyes: size, turn, apart, height
	put32 (mii + 0x38, 4u << 8 | 3u << 12 | 6u << 16 | 2u << 21 | 10u << 25);	// the eyebrows
	const u16 nose = 4 << 5 | 9 << 9, mouth = 4 << 9 | 3 << 13, mouth2 = 13, beard = 4 << 6 | 10 << 10, glasses = 4 << 7 | 10 << 11;
	mii[0x3C] = (u8) nose; mii[0x3D] = (u8) (nose >> 8); mii[0x3E] = (u8) mouth; mii[0x3F] = (u8) (mouth >> 8);
	mii[0x40] = (u8) mouth2; mii[0x42] = (u8) beard; mii[0x43] = (u8) (beard >> 8); mii[0x44] = (u8) glasses; mii[0x45] = (u8) (glasses >> 8);
	u16 crc = 0;
	for (int i = 0; i < 0x5E; i++)
	{
		crc ^= (u16) (mii[i] << 8);
		for (int b = 0; b < 8; b++) crc = (u16) (crc & 0x8000 ? crc << 1 ^ 0x1021 : crc << 1);
	}
	mii[0x5E] = (u8) (crc >> 8); mii[0x5F] = (u8) crc;
	for (int i = 0; i < 10; i++) { out[0x6C + i * 2] = mii[0x1A + i * 2]; out[0x6D + i * 2] = mii[0x1B + i * 2]; }	// the guest's name
}
// ... the Mii alone (0x60 bytes), for who asks for the user's (the friends service).
void guestMii (Machine *m, u8 mii[0x60]) { u8 all[0x84]; miiGuest (m, all); memcpy (mii, all + 0xC, 0x60); }

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
	case 0x16:								// PreloadLibraryApplet (applet id)
	case 0x18:								// PrepareToStartLibraryApplet (applet id)
		if (a.applet != cmd[1]) m->note ("library applet %03x", (unsigned) cmd[1]);
		a.applet = cmd[1];
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;
		break;
	case 0x17:								// FinishPreloadingLibraryApplet
	case 0x40:								// SendCaptureBufferInfo (the screens' pictures, for the applet)
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;
		break;
	// A library applet (the console's own keyboard, Mii chooser, error box...) is not run: it "ends" at once and
	// the program gets its parameter block back with the answer of somebody who closed it -- nothing chosen.
	case 0x0C:								// SendParameter (from, to, command, size, a handle, the parameters)
	case 0x1E:								// StartLibraryApplet (applet id, size, a handle, the parameters)
	{
		const u32 applet = id == 0x0C ? cmd[2] : cmd[1], given = id == 0x0C ? cmd[4] : cmd[2], from = id == 0x0C ? cmd[8] : cmd[6];
		u32 size = given < sizeof a.reply ? given : (u32) sizeof a.reply;
		memset (a.reply, 0, sizeof a.reply);
		m->mem.read (from, a.reply, size);
		if (id == 0x0C) m->note ("SendParameter %x -> %x command %x, %u bytes: %08x %08x %08x %08x", (unsigned) cmd[1], (unsigned) cmd[2], (unsigned) cmd[3], (unsigned) cmd[4], (unsigned) get32 (a.reply), (unsigned) get32 (a.reply + 4), (unsigned) get32 (a.reply + 8), (unsigned) get32 (a.reply + 12));
		if (applet == 0x406) m->note ("the game shows an error box: type %u, code %08x (%u bytes)", (unsigned) get32 (a.reply), (unsigned) get32 (a.reply + 4), (unsigned) size);
		if (applet == 0x406 && size >= 0xEF0) put32 (a.reply + 0xEEC, 1);	// the error box (or the EULA): closed, "success"
		if ((applet == 0x402 || applet == 0x404) && sizeof a.reply >= 0x84) { miiGuest (m, a.reply); if (size < 0x84) size = 0x84; }
		a.replySize = size; a.replySender = applet; a.replyCmd = APTCMD_WAKEUP_EXIT; a.replyCapture = false;
		if (id == 0x0C && cmd[3] == APTCMD_REQUEST)			// the program asks where to put the screens' pictures:
		{								// the applet answers with a memory block for them
			u32 want = get32 (a.reply);
			if (want < 0x1000 || want > 0x00400000) want = 0x00200000;
			want = (want + PAGE_SIZE - 1) & ~(u32) (PAGE_SIZE - 1);
			if (!a.capture) { u8 *h = (u8 *) calloc (want, 1); if (h) a.capture = new SharedMem (h, want); }
			a.replyCmd = APTCMD_RESPONSE; a.replyCapture = a.capture != 0;
		}
		if (a.replyCmd == APTCMD_WAKEUP_EXIT) a.applet = 0;
		a.pending = true;
		if (a.param) { a.param->signaled = true; m->wakeWaiters (a.param); }
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;
		break;
	}
	case 0x09:								// IsRegistered (app id)
		cmd[0] = ipcHeader (id, 2, 0); cmd[2] = cmd[1] == APPID_APPLICATION || cmd[1] == APPID_HOME_MENU || (a.applet && cmd[1] == a.applet); cmd[1] = RES_OK;
		break;
	case 0x0B:								// InquireNotification (app id) -> none
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 0;
		break;
	case 0x0D:								// ReceiveParameter (app id, buffer size) -> sender, command, size, a handle, the buffer
	case 0x0E:								// GlanceParameter (the same, the parameter stays)
	{
		const u32 *stat = cmd + 0x40;					// (the thread's static buffers: where the data goes)
		const bool had = a.pending;
		const u32 room = cmd[2];
		u32 size = 0, sender = APPID_HOME_MENU, command = had ? (u32) APTCMD_WAKEUP : 0;
		if (had && a.replyCmd)						// (an applet's answer)
		{
			size = a.replySize < room ? a.replySize : room; sender = a.replySender; command = a.replyCmd;
			m->mem.write (stat[1], a.reply, size);
		}
		const u32 object = had && a.replyCmd && a.replyCapture ? share (m, a.capture) : 0;
		if (id == 0x0D) { a.pending = false; a.replyCmd = 0; a.replySize = 0; a.replyCapture = false; }
		cmd[0] = ipcHeader (id, 4, 4);
		cmd[1] = had ? (u32) RES_OK : 0xC8A0CFEFu;			// (nothing sent)
		cmd[2] = sender; cmd[3] = command; cmd[4] = size;
		cmd[5] = 0; cmd[6] = object;					// (a handle, or none)
		cmd[7] = size << 14 | 2; cmd[8] = stat[1];
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
		if (!a.font) a.font = new SharedMem (m->mem.fcram + m->mem.appMem, FONT_SIZE, m->mem.fontVa ());
		cmd[0] = ipcHeader (id, 2, 2); cmd[1] = RES_OK; cmd[2] = m->mem.fontVa (); cmd[3] = 0; cmd[4] = share (m, a.font);
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
