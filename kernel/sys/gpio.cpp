//
// gpio.cpp -- the 40-pin header for the programs (kapi v92 gpio_ctl; kern/gpio.h, KAPI_GPIO_* in
// kern/kapi_abi.h; docs/02 "GPIO"). Over Circle's CGPIOPin, CGPIOManager (the edges' interrupt),
// CI2CMaster (bus 1) and CSPIMaster (SPI 0); the PWM's registers by hand (below: why).
//
//   * One owner a pin (a pid). A pin is taken by the first call that needs it (a mode, PWM, a bus)
//     and given back by KAPI_GPIO_M_FREE, KAPI_GPIO_RELEASE or the process's end -- then it is an
//     input with no pull again, so that nothing is left driving a wire.
//   * Reserved, never given: GPIO 0 / 1 (the HAT's ID EEPROM), 14 / 15 (the kernel's serial console).
//     Off the header and the system's anyway: 30..33 (the Bluetooth UART), 40 / 41 (the jack's PWM
//     audio), 42 (the activity LED) -- gpio_ctl only knows GPIO 0..27.
//   * PWM: the header's pins are on PWM0 (12 / 18 its channel 1, 13 / 19 its channel 2, alternate
//     functions 0 and 5); the jack's sound is on PWM1 (GPIO 40 / 41, sys/sound.cpp). The two share ONE
//     clock, which the sound sets to 125 MHz and stops when the jack stops. So the header's PWM never
//     changes the clock's rate: it runs it at the sound's same 125 MHz (frequency = 125 MHz / range,
//     mark-space mode) and starts it again when the jack's output stopped it (GpioPwmClockKeep).
//   * Edges: an input of the caller's, its rising / falling edges caught by the GPIO interrupt
//     (CGPIOManager, made at the first need) and queued, with the system timer's microseconds, for
//     the owner: up to 8 processes with a queue of 256 events each (the oldest kept, "lost" marked).
//   * I2C bus 1 (GPIO 2 SDA / 3 SCL, the header's 1k8 pull-ups) and SPI 0 (GPIO 7..11): one process at a
//     time, their pins taken with them.
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
#include <kern/gpio.h>
#include <kern/kapi_abi.h>
#include <kern/addrspace.h>
#include <kern/uaccess.h>
#include <circle/gpiopin.h>
#include <circle/gpiomanager.h>
#include <circle/gpioclock.h>
#include <circle/i2cmaster.h>
#include <circle/spimaster.h>
#include <circle/interrupt.h>
#include <circle/memio.h>
#include <circle/bcm2835.h>
#include <circle/synchronize.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/new.h>
#include <circle/util.h>

#if RASPPI <= 4		// (the Pi 5: at the end)

#define NPINS		KAPI_GPIO_PINS
#define NQUEUES		8
#define QSIZE		256			// events a queue (a power of two)
#define XFER_MAX	4096			// an I2C / SPI transfer's bytes at most
#define PWM_CLOCK	125000000		// the PWM clock's rate: the jack's sound's own (pwmsoundbasedevice.cpp, RPi 4)
#define CM_PWMCTL	(ARM_CM_BASE + 0xA0)	// (GPIOClockPWM: 20 * 8)
#define CM_BUSY		(1 << 7)

static const char *const s_pReserved[NPINS] =
{
	"HAT ID EEPROM", "HAT ID EEPROM", 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, "serial console", "serial console", 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
};

static unsigned  s_nOwner[NPINS];		// pid, 0: free
static u8        s_nMode[NPINS];		// KAPI_GPIO_M_*
static u8        s_nEdges[NPINS];		// KAPI_GPIO_RISING | _FALLING, queued for the owner
static CGPIOPin *s_pPin[NPINS];			// (made when the pin is taken in a GPIO mode)
static CGPIOManager *s_pManager = 0;		// the GPIO interrupt (the first edges asked)

// PWM0's two channels: their pin (-1: off), frequency, duty
static int      s_nPwmPin[2] = { -1, -1 };
static unsigned s_nPwmFreq[2], s_nPwmDuty[2];
static CGPIOClock s_PwmClock (GPIOClockPWM);

static CI2CMaster *s_pI2C = 0;  static unsigned s_nI2COwner = 0;
static CSPIMaster *s_pSPI = 0;  static unsigned s_nSPIOwner = 0;

struct TQueue
{
	unsigned nPid;				// 0: unused
	volatile unsigned nHead, nTail;		// written by the interrupt / read by the call
	volatile u8 bLost;
	struct kapi_gpio_event Ev[QSIZE];
};
static TQueue *s_pQueue[NQUEUES];

static CAddressSpace *CurrentAS (void)
{
	if (!CScheduler::IsActive ()) return 0;
	CTask *pTask = CScheduler::Get ()->GetCurrentTask ();
	return (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
}
static unsigned CallerPid (void) { CAddressSpace *pAS = CurrentAS (); return pAS != 0 ? pAS->GetPid () : 0; }

static unsigned Levels (void) { return read32 (ARM_GPIO_GPLEV0) & ((1u << NPINS) - 1); }

// ---- the edges -------------------------------------------------------------------------------------

static TQueue *QueueOf (unsigned nPid, boolean bMake)
{
	for (unsigned i = 0; i < NQUEUES; i++) if (s_pQueue[i] != 0 && s_pQueue[i]->nPid == nPid) return s_pQueue[i];
	if (!bMake) return 0;
	for (unsigned i = 0; i < NQUEUES; i++)
	{
		if (s_pQueue[i] == 0) s_pQueue[i] = new TQueue;
		if (s_pQueue[i] == 0) return 0;
		if (s_pQueue[i]->nPid == 0)
		{
			TQueue *q = s_pQueue[i];
			q->nHead = q->nTail = 0; q->bLost = 0;
			q->nPid = nPid;				// (last: the interrupt looks for it)
			return q;
		}
	}
	return 0;
}

// The interrupt: one pin's edge (pParam: its number), queued for its owner.
static void EdgeIRQ (void *pParam)
{
	unsigned nPin = (unsigned) (uintptr) pParam;
	if (nPin >= NPINS) return;
	u8 nLevel = (Levels () >> nPin) & 1;
	u8 nEdge = nLevel ? KAPI_GPIO_RISING : KAPI_GPIO_FALLING;
	if (!(s_nEdges[nPin] & nEdge)) return;			// (both are caught; only the asked ones go)
	TQueue *q = 0;
	for (unsigned i = 0; i < NQUEUES; i++) if (s_pQueue[i] != 0 && s_pQueue[i]->nPid == s_nOwner[nPin]) { q = s_pQueue[i]; break; }
	if (q == 0) return;
	unsigned nNext = (q->nHead + 1) & (QSIZE - 1);
	if (nNext == q->nTail) { q->bLost = 1; return; }	// full: the oldest are kept
	struct kapi_gpio_event *e = &q->Ev[q->nHead];
	e->pin = (u8) nPin; e->edge = nEdge; e->level = nLevel; e->lost = q->bLost; e->reserved = 0;
	e->us = CTimer::GetClockTicks64 ();
	q->bLost = 0;
	DataMemBarrier ();
	q->nHead = nNext;
}

static void EdgesOff (unsigned nPin)
{
	if (s_nEdges[nPin] == 0) return;
	s_nEdges[nPin] = 0;
	CGPIOPin *p = s_pPin[nPin];
	if (p != 0)
	{
		p->DisableInterrupt ();
		p->DisableInterrupt2 ();
		p->DisconnectInterrupt ();
	}
}

// ---- a pin's mode ----------------------------------------------------------------------------------

static TGPIOMode CircleMode (unsigned nMode)
{
	switch (nMode)
	{
	case KAPI_GPIO_M_IN_PULLUP:	return GPIOModeInputPullUp;
	case KAPI_GPIO_M_IN_PULLDOWN:	return GPIOModeInputPullDown;
	case KAPI_GPIO_M_OUT:		return GPIOModeOutput;
	default:
		if (nMode >= KAPI_GPIO_M_ALT0 && nMode <= KAPI_GPIO_M_ALT0 + 5)
			return (TGPIOMode) (GPIOModeAlternateFunction0 + (nMode - KAPI_GPIO_M_ALT0));
		return GPIOModeInput;
	}
}

// The pin set to a mode (the kernel's side: the checks are the caller's).
static void SetPin (unsigned nPin, unsigned nMode, unsigned nOwner)
{
	EdgesOff (nPin);
	delete s_pPin[nPin]; s_pPin[nPin] = 0;
	TGPIOMode m = CircleMode (nMode);
	if (nMode == KAPI_GPIO_M_PWM)
		m = (nPin == 12 || nPin == 13) ? GPIOModeAlternateFunction0 : GPIOModeAlternateFunction5;
	if (nMode == KAPI_GPIO_M_I2C || nMode == KAPI_GPIO_M_SPI)
		m = GPIOModeAlternateFunction0;			// (the bus's own driver set them too)
	s_pPin[nPin] = new CGPIOPin (nPin, m, s_pManager);
	s_nMode[nPin] = (u8) nMode;
	s_nOwner[nPin] = nMode == KAPI_GPIO_M_FREE ? 0 : nOwner;
}

// May the caller take this pin? 0, or the error.
static long Takeable (unsigned nPin, unsigned nPid)
{
	if (nPin >= NPINS) return -KAPI_EINVAL;
	if (s_pReserved[nPin] != 0) return -KAPI_EPERM;
	if (s_nOwner[nPin] != 0 && s_nOwner[nPin] != nPid) return -KAPI_EBUSY;
	if (s_nOwner[nPin] == nPid && (s_nMode[nPin] == KAPI_GPIO_M_I2C || s_nMode[nPin] == KAPI_GPIO_M_SPI))
		return -KAPI_EBUSY;				// (the caller's bus: closed first)
	return 0;
}

// ---- PWM -------------------------------------------------------------------------------------------

static int PwmChannel (unsigned nPin) { return nPin == 12 || nPin == 18 ? 0 : nPin == 13 || nPin == 19 ? 1 : -1; }

void GpioPwmClockKeep (void)
{
	if (s_nPwmPin[0] < 0 && s_nPwmPin[1] < 0) return;
	if (read32 (CM_PWMCTL) & CM_BUSY) return;		// (running: at the sound's 125 MHz, or ours)
	s_PwmClock.StartRate (PWM_CLOCK);
}

static void PwmApply (void)
{
	u32 nCtl = 0;
	for (int c = 0; c < 2; c++)
	{
		if (s_nPwmPin[c] < 0) continue;
		u32 nRange = PWM_CLOCK / s_nPwmFreq[c];
		u32 nData = (u32) (((u64) nRange * s_nPwmDuty[c]) / 10000);
		write32 (c == 0 ? ARM_PWM_RNG1 : ARM_PWM_RNG2, nRange);
		write32 (c == 0 ? ARM_PWM_DAT1 : ARM_PWM_DAT2, nData);
		nCtl |= c == 0 ? (1 << 0) | (1 << 7) : (1 << 8) | (1 << 15);	// PWEN, MSEN
	}
	write32 (ARM_PWM_CTL, nCtl);
}

static void PwmOff (unsigned nPin)
{
	int c = PwmChannel (nPin);
	if (c < 0 || s_nPwmPin[c] != (int) nPin) return;
	s_nPwmPin[c] = -1;
	PeripheralEntry ();
	PwmApply ();
	PeripheralExit ();
}

static long Pwm (unsigned nPin, unsigned nFreq, unsigned nDuty, unsigned nPid)
{
	int c = PwmChannel (nPin);
	if (c < 0) return -KAPI_EINVAL;
	if (nFreq < 1 || nFreq > 1000000 || nDuty > 10000) return -KAPI_EINVAL;
	long r = Takeable (nPin, nPid);
	if (r < 0) return r;
	if (s_nPwmPin[c] >= 0 && s_nPwmPin[c] != (int) nPin) return -KAPI_EBUSY;	// (12 / 18, 13 / 19: one channel)
	if (s_nMode[nPin] != KAPI_GPIO_M_PWM) SetPin (nPin, KAPI_GPIO_M_PWM, nPid);
	s_nPwmPin[c] = (int) nPin; s_nPwmFreq[c] = nFreq; s_nPwmDuty[c] = nDuty;
	PeripheralEntry ();
	GpioPwmClockKeep ();
	PwmApply ();
	PeripheralExit ();
	return 0;
}

// ---- a pin given back --------------------------------------------------------------------------------

static void FreePin (unsigned nPin)
{
	if (s_nMode[nPin] == KAPI_GPIO_M_PWM) PwmOff (nPin);
	SetPin (nPin, KAPI_GPIO_M_FREE, 0);
}

static void CloseBus (int nBus)
{
	if (nBus == KAPI_GPIO_BUS_I2C && s_pI2C != 0)
	{
		delete s_pI2C; s_pI2C = 0; s_nI2COwner = 0;
		FreePin (2); FreePin (3);
	}
	if (nBus == KAPI_GPIO_BUS_SPI && s_pSPI != 0)
	{
		delete s_pSPI; s_pSPI = 0; s_nSPIOwner = 0;
		for (unsigned p = 7; p <= 11; p++) FreePin (p);
	}
}

void GpioOnProcessGone (unsigned nPid)
{
	if (nPid == 0) return;
	if (s_nI2COwner == nPid) CloseBus (KAPI_GPIO_BUS_I2C);
	if (s_nSPIOwner == nPid) CloseBus (KAPI_GPIO_BUS_SPI);
	for (unsigned p = 0; p < NPINS; p++) if (s_nOwner[p] == nPid) FreePin (p);
	TQueue *q = QueueOf (nPid, FALSE);
	if (q != 0) q->nPid = 0;
}

// ---- the buses ----------------------------------------------------------------------------------------

static long BusPins (unsigned nFirst, unsigned nLast, unsigned nPid)
{
	for (unsigned p = nFirst; p <= nLast; p++)
	{
		long r = Takeable (p, nPid);
		if (r < 0) return r;
	}
	return 0;
}

static long I2COpen (unsigned nClock, unsigned nPid)
{
	if (s_pI2C != 0) return s_nI2COwner == nPid ? 0 : -KAPI_EBUSY;
	long r = BusPins (2, 3, nPid);
	if (r < 0) return r;
	FreePin (2); FreePin (3);
	s_pI2C = new CI2CMaster (1, FALSE, 0);
	if (s_pI2C == 0 || !s_pI2C->Initialize ()) { delete s_pI2C; s_pI2C = 0; return -KAPI_EIO; }
	if (nClock != 0) s_pI2C->SetClock (nClock < 1000 ? 1000 : nClock > 1000000 ? 1000000 : nClock);
	s_nI2COwner = nPid;
	for (unsigned p = 2; p <= 3; p++) { s_nOwner[p] = nPid; s_nMode[p] = KAPI_GPIO_M_I2C; }
	return 0;
}

static long I2CXfer (const struct kapi_gpio_i2c *pUser, unsigned nPid)
{
	if (s_pI2C == 0 || s_nI2COwner != nPid) return -KAPI_EPERM;
	struct kapi_gpio_i2c X;
	if (pUser == 0 || !UserGet (&X, pUser)) return -KAPI_EFAULT;
	if (X.addr > 0x7F || X.wlen > XFER_MAX || X.rlen > XFER_MAX || (X.wlen == 0 && X.rlen == 0)) return -KAPI_EINVAL;
	u8 *pBuf = new u8[X.wlen + X.rlen + 1];
	if (pBuf == 0) return -KAPI_ENOMEM;
	long r = 0;
	if (X.wlen > 0 && !UserCopyIn (pBuf, X.wr, X.wlen)) r = -KAPI_EFAULT;
	else if (X.wlen > 0 && X.rlen > 0 && X.wlen <= 16)
		r = s_pI2C->WriteReadRepeatedStart ((u8) X.addr, pBuf, X.wlen, pBuf + X.wlen, X.rlen);
	else
	{
		if (X.wlen > 0) r = s_pI2C->Write ((u8) X.addr, pBuf, X.wlen);
		if (r >= 0 && X.rlen > 0) r = s_pI2C->Read ((u8) X.addr, pBuf + X.wlen, X.rlen);
	}
	if (r < 0 && r != -KAPI_EFAULT) r = -KAPI_EIO;
	else if (r >= 0 && X.rlen > 0 && !UserCopyOut (X.rd, pBuf + X.wlen, X.rlen)) r = -KAPI_EFAULT;
	delete [] pBuf;
	return r;
}

static long I2CScan (unsigned char *pUser, unsigned nPid)
{
	if (s_pI2C == 0 || s_nI2COwner != nPid) return -KAPI_EPERM;
	u8 Map[16]; memset (Map, 0, sizeof Map);
	long n = 0;
	for (unsigned a = 0x08; a <= 0x77; a++)		// (the reserved addresses are not asked)
	{
		u8 b;
		if (s_pI2C->Read ((u8) a, &b, 1) == 1) { Map[a / 8] |= (u8) (1 << (a % 8)); n++; }
	}
	if (pUser == 0 || !UserCopyOut (pUser, Map, sizeof Map)) return -KAPI_EFAULT;
	return n;
}

static long SPIOpen (unsigned nClock, unsigned nMode, unsigned nPid)
{
	if (nMode > 3) return -KAPI_EINVAL;
	if (nClock == 0) nClock = 1000000;
	if (nClock < 4000 || nClock > 125000000) return -KAPI_EINVAL;
	if (s_pSPI != 0)
	{
		if (s_nSPIOwner != nPid) return -KAPI_EBUSY;
		s_pSPI->SetClock (nClock); s_pSPI->SetMode (nMode >> 1, nMode & 1);
		return 0;
	}
	long r = BusPins (7, 11, nPid);
	if (r < 0) return r;
	for (unsigned p = 7; p <= 11; p++) FreePin (p);
	s_pSPI = new CSPIMaster (nClock, nMode >> 1, nMode & 1, 0);
	if (s_pSPI == 0 || !s_pSPI->Initialize ()) { delete s_pSPI; s_pSPI = 0; return -KAPI_EIO; }
	s_nSPIOwner = nPid;
	for (unsigned p = 7; p <= 11; p++) { s_nOwner[p] = nPid; s_nMode[p] = KAPI_GPIO_M_SPI; }
	return 0;
}

static long SPIXfer (const struct kapi_gpio_spi *pUser, unsigned nPid)
{
	if (s_pSPI == 0 || s_nSPIOwner != nPid) return -KAPI_EPERM;
	struct kapi_gpio_spi X;
	if (pUser == 0 || !UserGet (&X, pUser)) return -KAPI_EFAULT;
	if (X.cs > 1 || X.len == 0 || X.len > XFER_MAX) return -KAPI_EINVAL;
	u8 *pBuf = new u8[2 * X.len];
	if (pBuf == 0) return -KAPI_ENOMEM;
	long r;
	if (X.tx != 0) { if (!UserCopyIn (pBuf, X.tx, X.len)) { delete [] pBuf; return -KAPI_EFAULT; } }
	else memset (pBuf, 0, X.len);
	r = s_pSPI->WriteRead (X.cs, pBuf, pBuf + X.len, X.len);
	if (r < 0) r = -KAPI_EIO;
	else if (X.rx != 0 && !UserCopyOut (X.rx, pBuf + X.len, X.len)) r = -KAPI_EFAULT;
	delete [] pBuf;
	return r;
}

// ---- the calls ----------------------------------------------------------------------------------------

static long Info (struct kapi_gpio_pin *pUser, long nMax)
{
	if (nMax <= 0) return NPINS;
	if (nMax > NPINS) nMax = NPINS;
	unsigned nLev = Levels ();
	for (long i = 0; i < nMax; i++)
	{
		struct kapi_gpio_pin P;
		memset (&P, 0, sizeof P);
		P.pin = (u8) i; P.mode = s_nMode[i]; P.level = (nLev >> i) & 1; P.owner = s_nOwner[i];
		if (s_pReserved[i] != 0) { P.flags |= KAPI_GPIO_F_RESERVED; strncpy (P.reason, s_pReserved[i], sizeof P.reason - 1); }
		int c = PwmChannel ((unsigned) i);
		if (c >= 0) P.flags |= KAPI_GPIO_F_PWM_CAPABLE;
		if (s_nEdges[i]) P.flags |= KAPI_GPIO_F_EDGES;
		if (c >= 0 && s_nPwmPin[c] == i) { P.pwm_freq = s_nPwmFreq[c]; P.pwm_duty = s_nPwmDuty[c]; }
		if (!UserPut (pUser + i, P)) return -KAPI_EFAULT;
	}
	return nMax;
}

static long Mode (unsigned nPin, unsigned nMode, unsigned nPid)
{
	if (nMode > KAPI_GPIO_M_ALT0 + 5 || nMode == KAPI_GPIO_M_PWM || nMode == KAPI_GPIO_M_I2C || nMode == KAPI_GPIO_M_SPI)
		return -KAPI_EINVAL;
	long r = Takeable (nPin, nPid);
	if (r < 0) return r;
	if (nMode == KAPI_GPIO_M_FREE) { if (s_nOwner[nPin] == nPid) FreePin (nPin); return 0; }
	if (s_nMode[nPin] == KAPI_GPIO_M_PWM) PwmOff (nPin);
	SetPin (nPin, nMode, nPid);
	return 0;
}

static long Edges (unsigned nPin, unsigned nEdges, unsigned nPid)
{
	if (nPin >= NPINS || nEdges > 3) return -KAPI_EINVAL;
	if (s_nOwner[nPin] != nPid) return -KAPI_EPERM;
	unsigned m = s_nMode[nPin];
	if (m != KAPI_GPIO_M_IN && m != KAPI_GPIO_M_IN_PULLUP && m != KAPI_GPIO_M_IN_PULLDOWN) return -KAPI_EINVAL;
	EdgesOff (nPin);
	if (nEdges == 0) return 0;
	if (QueueOf (nPid, TRUE) == 0) return -KAPI_ENOMEM;
	if (s_pManager == 0)
	{
		s_pManager = new CGPIOManager (CInterruptSystem::Get ());
		if (s_pManager == 0 || !s_pManager->Initialize ()) { delete s_pManager; s_pManager = 0; return -KAPI_EIO; }
	}
	delete s_pPin[nPin];				// (again, with the manager: its interrupt goes there)
	s_pPin[nPin] = new CGPIOPin (nPin, CircleMode (m), s_pManager);
	CGPIOPin *p = s_pPin[nPin];
	s_nEdges[nPin] = (u8) nEdges;
	p->ConnectInterrupt (EdgeIRQ, (void *) (uintptr) nPin);
	p->EnableInterrupt (GPIOInterruptOnRisingEdge);		// (both, always: the level after says
	p->EnableInterrupt2 (GPIOInterruptOnFallingEdge);	//  which; EdgeIRQ keeps the asked ones)
	return 0;
}

static long Events (struct kapi_gpio_event *pUser, long nMax, unsigned nWaitMs, unsigned nPid)
{
	if (nMax <= 0 || pUser == 0) return -KAPI_EINVAL;
	if (nWaitMs > 1000) nWaitMs = 1000;
	TQueue *q = QueueOf (nPid, FALSE);
	if (q == 0) { if (nWaitMs) CScheduler::Get ()->MsSleep (nWaitMs); return 0; }
	unsigned nStart = CTimer::Get ()->GetTicks ();		// (ticks: 100 a second)
	while (q->nHead == q->nTail && nWaitMs > 0)
	{
		CScheduler::Get ()->MsSleep (2);
		if ((CTimer::Get ()->GetTicks () - nStart) * 10 >= nWaitMs) break;
	}
	long n = 0;
	while (n < nMax && q->nTail != q->nHead)
	{
		struct kapi_gpio_event e = q->Ev[q->nTail];
		DataMemBarrier ();
		if (!UserPut (pUser + n, e)) return n > 0 ? n : -KAPI_EFAULT;
		q->nTail = (q->nTail + 1) & (QSIZE - 1);
		n++;
	}
	return n;
}

extern "C" long kapi_gpio_ctl (int nOp, long a0, long a1, long a2)
{
	unsigned nPid = CallerPid ();
	switch (nOp)
	{
	case KAPI_GPIO_INFO:	 return Info ((struct kapi_gpio_pin *) a0, a1);
	case KAPI_GPIO_READ:	 return (unsigned long) a0 < NPINS ? (long) ((Levels () >> a0) & 1) : -KAPI_EINVAL;
	case KAPI_GPIO_READ_ALL: return (long) Levels ();
	case KAPI_GPIO_NOW:	 return (long) CTimer::GetClockTicks64 ();
	default:		 break;
	}
	if (nPid == 0) return -KAPI_EPERM;
	switch (nOp)
	{
	case KAPI_GPIO_MODE:	 return Mode ((unsigned) a0, (unsigned) a1, nPid);
	case KAPI_GPIO_WRITE:
		if ((unsigned long) a0 >= NPINS) return -KAPI_EINVAL;
		if (s_nOwner[a0] != nPid) return -KAPI_EPERM;
		if (s_nMode[a0] != KAPI_GPIO_M_OUT || s_pPin[a0] == 0) return -KAPI_EINVAL;	// (the caller's, not an output)
		s_pPin[a0]->Write (a1 ? HIGH : LOW);
		return 0;
	case KAPI_GPIO_PWM:	 return Pwm ((unsigned) a0, (unsigned) a1, (unsigned) a2, nPid);
	case KAPI_GPIO_EDGES:	 return Edges ((unsigned) a0, (unsigned) a1, nPid);
	case KAPI_GPIO_EVENTS:	 return Events ((struct kapi_gpio_event *) a0, a1, (unsigned) a2, nPid);
	case KAPI_GPIO_I2C_OPEN: return I2COpen ((unsigned) a0, nPid);
	case KAPI_GPIO_I2C_XFER: return I2CXfer ((const struct kapi_gpio_i2c *) a0, nPid);
	case KAPI_GPIO_I2C_SCAN: return I2CScan ((unsigned char *) a0, nPid);
	case KAPI_GPIO_SPI_OPEN: return SPIOpen ((unsigned) a0, (unsigned) a1, nPid);
	case KAPI_GPIO_SPI_XFER: return SPIXfer ((const struct kapi_gpio_spi *) a0, nPid);
	case KAPI_GPIO_CLOSE:
		if (a0 == KAPI_GPIO_BUS_I2C) { if (s_nI2COwner != nPid) return -KAPI_EPERM; CloseBus (KAPI_GPIO_BUS_I2C); return 0; }
		if (a0 == KAPI_GPIO_BUS_SPI) { if (s_nSPIOwner != nPid) return -KAPI_EPERM; CloseBus (KAPI_GPIO_BUS_SPI); return 0; }
		return -KAPI_EINVAL;
	case KAPI_GPIO_RELEASE:	 GpioOnProcessGone (nPid); return 0;
	default:		 return -KAPI_EINVAL;
	}
}

#else	// RASPPI >= 5

// The Pi 5: the header's pins are on the RP1 (its own GPIO block, PWM, I2C and SPI on PCIe), not on
// the registers above -- that port is to come (docs/PI5-PORT.md). Until then gpio_ctl says ENODEV.

void GpioOnProcessGone (unsigned nPid) {}
void GpioPwmClockKeep (void) {}

extern "C" long kapi_gpio_ctl (int nOp, long a0, long a1, long a2)
{
	return -KAPI_ENODEV;
}

#endif
