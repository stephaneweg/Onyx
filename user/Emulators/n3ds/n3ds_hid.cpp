//
// n3ds/n3ds_hid.cpp -- HID (hid:USER, hid:SPVR): the buttons, the circle pad and the touch screen. A program gets
// a shared page and five events; the service writes the pad's and the touch screen's states into the page, each
// in a ring of 8 entries with the index of the newest, and signals the events. Here once a frame (the console:
// every 4 ms). The accelerometer and the gyroscope: at rest.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (see n3ds.h).
//
#include <string.h>
#include "n3ds/n3ds.h"

namespace n3ds {

// The shared page: the pad from 0 (ticks, ticks before, index, -, -, the state now, 8 entries of 16 bytes from
// 0x28: held, pressed, released, the circle pad), the touch screen from 0xA8 (ticks, ticks before, index, -, the
// raw state, 8 entries of 8 bytes from +0x20: x, y, down).
enum { HID_PAD = 0x00, HID_PAD_ENTRIES = 0x28, HID_TOUCH = 0xA8, HID_TOUCH_ENTRIES = 0xA8 + 0x20, HID_SHM_SIZE = 0x1000 };

void Machine::setInput (u32 buttons, int cpadX, int cpadY, bool touch, int touchX, int touchY)
{
	hid.buttons = buttons & 0xFFF;
	hid.cpadX = (s16) cpadX; hid.cpadY = (s16) cpadY;
	if (cpadX > 40) hid.buttons |= BTN_CPAD_RIGHT; else if (cpadX < -40) hid.buttons |= BTN_CPAD_LEFT;
	if (cpadY > 40) hid.buttons |= BTN_CPAD_UP; else if (cpadY < -40) hid.buttons |= BTN_CPAD_DOWN;
	hid.touch = touch && touchX >= 0 && touchX < BOTTOM_W && touchY >= 0 && touchY < SCREEN_H;
	hid.touchX = (u16) touchX; hid.touchY = (u16) touchY;
}

void hidUpdate (Machine *m)
{
	Machine::Hid &h = m->hid;
	if (!h.shared) return;
	u8 *p = h.shared->host;
	const s64 ticks = (s64) m->now;
	// the pad
	u32 before; memcpy (&before, p + HID_PAD_ENTRIES + h.padIndex * 0x10, 4);
	h.padIndex = (h.padIndex + 1) & 7;
	if (h.padIndex == 0) { memcpy (p + HID_PAD + 8, p + HID_PAD, 8); memcpy (p + HID_PAD, &ticks, 8); }
	memcpy (p + HID_PAD + 0x10, &h.padIndex, 4);
	memcpy (p + HID_PAD + 0x1C, &h.buttons, 4);
	u32 e[4] = { h.buttons, h.buttons & ~before, before & ~h.buttons, (u32) (u16) h.cpadX | (u32) (u16) h.cpadY << 16 };
	memcpy (p + HID_PAD_ENTRIES + h.padIndex * 0x10, e, sizeof e);
	// the touch screen
	h.touchIndex = (h.touchIndex + 1) & 7;
	if (h.touchIndex == 0) { memcpy (p + HID_TOUCH + 8, p + HID_TOUCH, 8); memcpy (p + HID_TOUCH, &ticks, 8); }
	memcpy (p + HID_TOUCH + 0x10, &h.touchIndex, 4);
	u32 t[2] = { h.touch ? (u32) h.touchX | (u32) h.touchY << 16 : 0, h.touch ? 1u : 0u };
	memcpy (p + HID_TOUCH_ENTRIES + h.touchIndex * 8, t, sizeof t);
	for (int i = 0; i < 2; i++) if (h.events[i]) { h.events[i]->signaled = true; m->wakeWaiters (h.events[i]); }
}

void hidRequest (Machine *m, Session *s, u32 *cmd)
{
	const u32 id = cmd[0] >> 16;
	Machine::Hid &h = m->hid;
	switch (id)
	{
	case 0x0A:								// GetIPCHandles -> the shared page, the pad's two events, the sensors' three
	{
		if (!h.shared)
		{
			u8 *host = m->mem.allocTop (HID_SHM_SIZE);
			if (!host) { cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OUT_OF_MEMORY; break; }
			h.shared = new SharedMem (host, HID_SHM_SIZE);
			for (int i = 0; i < 5; i++) h.events[i] = new Event (RESET_ONESHOT);
		}
		cmd[0] = ipcHeader (id, 1, 7); cmd[1] = RES_OK; cmd[2] = 0x14000000;
		h.shared->refs++; cmd[3] = m->handleNew (h.shared);
		for (int i = 0; i < 5; i++) { h.events[i]->refs++; cmd[4 + i] = m->handleNew (h.events[i]); }
		break;
	}
	case 0x11: case 0x12: case 0x13: case 0x14:				// the accelerometer and the gyroscope on / off
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;
		break;
	case 0x15:								// GetGyroscopeLowRawToDpsCoefficient -> 14.375
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 0x41660000;
		break;
	case 0x17:								// GetSoundVolume -> the slider (0..63)
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 0x3F;
		break;
	default:
		ipcStub (m, s->name, cmd);
		break;
	}
}

}
