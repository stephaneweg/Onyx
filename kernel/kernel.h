//
// kernel.h
//
// Our top-level kernel object. Replaces the Circle sample CKernel. It takes over
// after sysinit() (MMU + GIC + static ctors already done) and, for now, brings up
// a serial console and proves the timer IRQ works (milestone #2). Later, Run()
// will hand control to our preemptive scheduler instead of looping.
//
#ifndef _kernel_kernel_h
#define _kernel_kernel_h

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/screen.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/2dgraphics.h>
#include <circle/sched/scheduler.h>
#include <circle/types.h>
#include <SDCard/emmc.h>
#include <fatfs/ff.h>
#include <kern/gui/window.h>

#define SCREEN_WIDTH	640
#define SCREEN_HEIGHT	480

enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

class CKernel
{
public:
	CKernel (void);
	~CKernel (void);

	boolean Initialize (void);

	TShutdownMode Run (void);

private:
	// Do not change this order: members are constructed top-to-bottom and some
	// constructors depend on earlier ones (m_Timer needs m_Interrupt; m_Logger
	// needs m_Options and m_Timer). Mirrors Circle's documented convention.
	//
	// Note: no CMemorySystem member -- sysinit() already created the singleton
	// that owns the MMU/heap; we reach it via CMemorySystem::Get() when needed.
	CActLED			m_ActLED;
	CKernelOptions		m_Options;
	CDeviceNameService	m_DeviceNameService;
	CScreenDevice		m_Screen;		// HDMI text console (boot log visible w/o serial)
	CSerialDevice		m_Serial;		// serial console
	CExceptionHandler	m_ExceptionHandler;	// Circle's handler (replaced in #4)
	CInterruptSystem	m_Interrupt;
	CTimer			m_Timer;
	CLogger			m_Logger;
	C2DGraphics		m_2DGraphics;		// HDMI framebuffer (double-buffered, VSync)
	CEMMCDevice		m_EMMC;			// SD card (#11)
	FATFS			m_FileSystem;		// FatFs mount of the SD card
	CScheduler		m_Scheduler;		// our preemptive-ready scheduler (#3)
	CWindowManager		m_WindowManager;	// compositor (#10)

	boolean			m_bGraphics;		// framebuffer available?
	boolean			m_bSDMounted;		// SD card mounted?
};

#endif
