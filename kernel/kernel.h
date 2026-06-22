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
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/types.h>

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
	CSerialDevice		m_Serial;		// serial console (headless bring-up)
	CExceptionHandler	m_ExceptionHandler;	// Circle's handler (replaced in #4)
	CInterruptSystem	m_Interrupt;
	CTimer			m_Timer;
	CLogger			m_Logger;
};

#endif
