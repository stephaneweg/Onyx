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
