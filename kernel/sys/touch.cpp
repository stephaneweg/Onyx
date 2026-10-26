//
// touch.cpp -- a touch screen as the pointer (cmdline.txt touch=1): the Goodix GT911 on the I2C bus 1 (GPIO 2 SDA,
// 3 SCL; address 0x5D, or 0x14) -- the Waveshare PocketTerm35's 3.5-inch screen. A kernel task polls it 60 times a
// second (its interrupt and reset pins are the board's, not known here) and gives the window server what a mouse
// would (WsInputPointer):
//
//   * one finger: the pointer at that place (a move without the button first, so that what is under it knows the
//     pointer is there), then, as a phone does: a tap is a click; moved up or down, a SCROLL -- the wheel, a notch
//     every 20 pixels, the page following the finger, no button (the user, 2026-10-26); moved sideways or held still
//     0.4 s, a DRAG -- the left button down where it touched until it lets go (a slider, a scroll bar, a selection,
//     a window's title). (No sideways scroll: the system has no horizontal wheel.)
//   * two fingers moved up or down: the wheel too, no button;
//   * the controller's coordinates (its configuration's x / y range: 640 x 480 there) scaled to the screen's size
//     now (it changes: a game's own resolution).
//
// The GT911 (its datasheet's "coordinate information"): 0x8140 the product id ("911"), 0x8048 / 0x804A the x / y
// range (little-endian), 0x814E the status (bit 7: new points; bits 0..3: how many), from 0x814F 8 bytes a point
// (its track id, x, y, size, a reserved byte); the status is written 0 once read. The bus is shared with the programs
// through sys/gpio.cpp (GpioI2cKernel*: no lock needed, the kernel is not preempted).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include <kern/touch.h>
#include <kern/gpio.h>
#include <kern/wsrv.h>
#include <kern/gui/window.h>
#include <circle/sched/task.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/util.h>

static const char FromTouch[] = "touch";

#define GT_ID		0x8140
#define GT_XMAX		0x8048
#define GT_STATUS	0x814E
#define GT_POINT1	0x814F
#define POLL_MS		16
#define WHEEL_STEP	20			// a wheel's notch: this many pixels of the screen
#define SLOP		10			// a finger moved less (pixels) is still
#define HOLD_US		400000			// a finger held still this long presses the button (a drag follows)

class CTouchTask : public CTask
{
public:
	CTouchTask (void) : m_ucAddr (0), m_nXMax (640), m_nYMax (480) { SetName ("touch"); }

	void Run (void) override
	{
		if (!GpioI2cKernelOpen (400000))
		{
			CLogger::Get ()->Write (FromTouch, LogWarning, "the I2C bus 1 cannot be opened: no touch screen");
			return;
		}
		if (!Probe ())
		{
			CLogger::Get ()->Write (FromTouch, LogWarning, "no GT911 at 0x5D or 0x14 on the I2C bus 1: no touch screen");
			return;
		}
		CLogger::Get ()->Write (FromTouch, LogNotice, "GT911 at 0x%02X: %u x %u, the pointer", (unsigned) m_ucAddr, m_nXMax, m_nYMax);

		enum { M_NONE, M_WAIT, M_SCROLL, M_DRAG } nMode = M_NONE;	// one finger: its gesture
		boolean bTwo = FALSE;
		int nLastX = 0, nLastY = 0, nTwoY = 0, nFails = 0, nX0 = 0, nY0 = 0, nScrollY = 0;
		unsigned nT0 = 0;
		for (;;)
		{
			CScheduler::Get ()->MsSleep (POLL_MS);

			u8 ucStatus;
			if (!ReadReg (GT_STATUS, &ucStatus, 1))
			{
				if (++nFails == 50) CLogger::Get ()->Write (FromTouch, LogWarning, "the GT911 does not answer");
				continue;
			}
			nFails = 0;
			if (!(ucStatus & 0x80)) continue;		// (nothing new)

			unsigned nPoints = ucStatus & 0x0F;
			u8 Pt[16];
			if (nPoints > 2) nPoints = 2;			// (the first two are all it needs)
			boolean bOK = nPoints == 0 || ReadReg (GT_POINT1, Pt, 8 * nPoints);
			WriteReg (GT_STATUS, 0);			// (read: the controller may fill it again)
			if (!bOK) continue;

			if (nPoints == 0)				// let go
			{
				if (nMode == M_WAIT)			// touched and let go, still: a click there
				{
					WsInputPointer (nX0, nY0, 1, 0);
					WsInputPointer (nX0, nY0, 0, 0);
				}
				else if (nMode == M_DRAG) WsInputPointer (nLastX, nLastY, 0, 0);
				nMode = M_NONE; bTwo = FALSE;
				continue;
			}

			int x = Scale (Pt[1] | Pt[2] << 8, m_nXMax, g_nScreenWidth);
			int y = Scale (Pt[3] | Pt[4] << 8, m_nYMax, g_nScreenHeight);
			if (nPoints >= 2)				// two fingers: the wheel
			{
				int y2 = Scale (Pt[11] | Pt[12] << 8, m_nYMax, g_nScreenHeight);
				int ym = (y + y2) / 2, xm = (x + Scale (Pt[9] | Pt[10] << 8, m_nXMax, g_nScreenWidth)) / 2;
				if (nMode == M_DRAG) WsInputPointer (nLastX, nLastY, 0, 0);	// (the drag left)
				nMode = M_SCROLL;				// (and no click when they let go)
				if (!bTwo) { bTwo = TRUE; nTwoY = ym; }
				int nNotches = (ym - nTwoY) / WHEEL_STEP;
				if (nNotches != 0)
				{
					WsInputPointer (xm, ym, 0, nNotches);	// (fingers down: the page goes down, as the wheel forward)
					nTwoY += nNotches * WHEEL_STEP;
				}
				nLastX = xm; nLastY = ym;
				continue;
			}
			if (bTwo) continue;				// (one finger left of two: nothing until all let go)

			unsigned nNow = CTimer::Get ()->GetClockTicks ();
			if (nMode == M_NONE)				// touched: the pointer there, the gesture not known yet
			{
				WsInputPointer (x, y, 0, 0);
				nMode = M_WAIT; nX0 = x; nY0 = y; nT0 = nNow; nScrollY = y;
			}
			else if (nMode == M_WAIT)
			{
				int dx = x - nX0, dy = y - nY0;
				if (dx < 0) dx = -dx;
				if (dy < 0) dy = -dy;
				if (dy > SLOP && dy >= dx) { nMode = M_SCROLL; nScrollY = y; }	// up / down: a scroll
				else if (dx > SLOP || nNow - nT0 >= HOLD_US)		// sideways, or held: a drag
				{
					WsInputPointer (nX0, nY0, 1, 0);		// (pressed where it was touched)
					nMode = M_DRAG;
				}
			}
			if (nMode == M_SCROLL)
			{
				int nNotches = (y - nScrollY) / WHEEL_STEP;
				if (nNotches != 0)
				{
					WsInputPointer (x, y, 0, nNotches);		// (the page follows the finger: down, the wheel forward)
					nScrollY += nNotches * WHEEL_STEP;
				}
			}
			else if (nMode == M_DRAG) WsInputPointer (x, y, 1, 0);
			nLastX = x; nLastY = y;
		}
	}

private:
	static int Scale (int v, unsigned nMax, int nScreen)
	{
		if (nMax == 0) return 0;
		int r = (int) ((long) v * nScreen / (long) nMax);
		return r < 0 ? 0 : r >= nScreen ? nScreen - 1 : r;
	}

	boolean ReadReg (unsigned nReg, u8 *pBuf, unsigned nLen)
	{
		u8 Addr[2] = { (u8) (nReg >> 8), (u8) nReg };
		return GpioI2cKernelXfer (m_ucAddr, Addr, 2, pBuf, nLen) == (int) nLen;
	}

	boolean WriteReg (unsigned nReg, u8 ucValue)
	{
		u8 Buf[3] = { (u8) (nReg >> 8), (u8) nReg, ucValue };
		return GpioI2cKernelXfer (m_ucAddr, Buf, 3, 0, 0) >= 0;
	}

	boolean Probe (void)
	{
		static const u8 Addrs[2] = { 0x5D, 0x14 };
		for (unsigned a = 0; a < 2; a++)
		{
			m_ucAddr = Addrs[a];
			u8 Id[4];
			if (!ReadReg (GT_ID, Id, 4) || Id[0] != '9' || Id[1] != '1' || Id[2] != '1') continue;
			u8 R[4];
			if (ReadReg (GT_XMAX, R, 4))
			{
				unsigned xm = R[0] | R[1] << 8, ym = R[2] | R[3] << 8;
				if (xm >= 64 && xm <= 4096 && ym >= 64 && ym <= 4096) { m_nXMax = xm; m_nYMax = ym; }
			}
			return TRUE;
		}
		return FALSE;
	}

private:
	u8	 m_ucAddr;
	unsigned m_nXMax, m_nYMax;
};

void TouchStart (void)
{
	new CTouchTask;
}
