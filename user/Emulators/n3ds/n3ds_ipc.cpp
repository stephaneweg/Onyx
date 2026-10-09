//
// n3ds/n3ds_ipc.cpp -- the services: the ones the core emulates (their names, their functions) and `srv:`, the
// service manager a program asks for the others. A program connects to the port "srv:", registers, then asks for
// a service by its name and gets a session; each request is a command buffer in its thread's TLS (the header:
// command, plain words, translated words), answered in place.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (see n3ds.h).
//
#include <string.h>
#include "n3ds/n3ds.h"

namespace n3ds {

static const struct { const char *name; ServiceFn fn; } SERVICES[] = {
	{ "srv:",     srvRequest },
	{ "gsp::Gpu", gspRequest },
	{ "APT:U",    aptRequest }, { "APT:S", aptRequest }, { "APT:A", aptRequest },
	{ "hid:USER", hidRequest }, { "hid:SPVR", hidRequest },
	{ "fs:USER",  fsRequest },
	{ "cfg:u",    cfgRequest }, { "cfg:s", cfgRequest }, { "cfg:i", cfgRequest },
	{ "ptm:u",    ptmRequest }, { "ptm:sysm", ptmRequest },
	{ "dsp::DSP", dspRequest },
};

// Services that exist on the console and are not emulated: a game stops at once when one of them cannot be
// opened, so they are opened and every command is answered "done, nothing" -- and noted, by its number.
static const char *const ABSENT[] = {
	"ac:u", "ac:i", "act:u", "act:a", "am:app", "am:net", "am:u", "am:sys", "boss:U", "boss:P", "cam:u", "cam:c", "cecd:u", "cecd:s",
	"csnd:SND", "dlp:SRVR", "dlp:CLNT", "dlp:FKCL", "err:f", "frd:u", "frd:a", "gsp::Lcd", "http:C", "ir:u", "ir:USER",
	"ir:rst", "ldr:ro", "mcu::HWC", "mic:u", "ndm:u", "news:u", "news:s", "nfc:u", "nfc:m", "nim:aoc", "nim:u", "nwm::UDS", "nwm::EXT",
	"ps:ps", "ptm:gets", "ptm:sets", "ptm:play", "pxi:dev", "qtm:u", "qtm:s", "soc:U", "ssl:C", "y2r:u",
};
static void absentRequest (Machine *m, Session *s, u32 *cmd)
{
	m->note ("%s %04x", s->name, (unsigned) (cmd[0] >> 16));
	const u32 id = cmd[0] >> 16;
	cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;
	for (int i = 2; i < 8; i++) cmd[i] = 0;
}

Session *Machine::serviceOpen (const char *name)
{
	for (size_t i = 0; i < sizeof SERVICES / sizeof SERVICES[0]; i++)
		if (strcmp (SERVICES[i].name, name) == 0) return new Session (SERVICES[i].fn, SERVICES[i].name);
	for (size_t i = 0; i < sizeof ABSENT / sizeof ABSENT[0]; i++)
		if (strcmp (ABSENT[i], name) == 0) return new Session (absentRequest, ABSENT[i]);
	return 0;
}

void ipcStub (Machine *m, const char *service, u32 *cmd)
{
	m->note ("%s command %08x", service, (unsigned) cmd[0]);
	cmd[0] = ipcHeader (cmd[0] >> 16, 1, 0);
	cmd[1] = RES_OK;
}

void srvRequest (Machine *m, Session *, u32 *cmd)
{
	const u32 id = cmd[0] >> 16;
	switch (id)
	{
	case 0x01:								// RegisterClient
	case 0x09:								// Subscribe (a notification)
	case 0x0A:								// Unsubscribe
	case 0x0C:								// PublishToSubscriber
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;
		break;
	case 0x02:								// EnableNotification -> a semaphore, released at each notification
	{
		u32 h = m->handleNew (new Semaphore (0, 0x7FFFFFFF));
		cmd[0] = ipcHeader (id, 1, 2); cmd[1] = h ? (u32) RES_OK : (u32) RES_OUT_OF_HANDLES; cmd[2] = 0; cmd[3] = h;
		break;
	}
	case 0x05:								// GetServiceHandle (name: 8 bytes, its length, flags)
	{
		char name[9]; memcpy (name, &cmd[1], 8); name[8] = 0;
		if (cmd[3] < 8) name[cmd[3]] = 0;
		Session *s = m->serviceOpen (name);
		u32 h = s ? m->handleNew (s) : 0;
		if (!s) m->note ("service %s", name);
		if (!h) { cmd[0] = ipcHeader (id, 1, 0); cmd[1] = s ? (u32) RES_OUT_OF_HANDLES : (u32) RES_NO_SERVICE; break; }
		cmd[0] = ipcHeader (id, 1, 2); cmd[1] = RES_OK; cmd[2] = 0; cmd[3] = h;
		break;
	}
	case 0x0B:								// ReceiveNotification -> its number (none)
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 0;
		break;
	default:
		ipcStub (m, "srv:", cmd);
		break;
	}
}

}
