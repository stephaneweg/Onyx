//
// kernel.cpp
//
#include "kernel.h"
#include <circle/machineinfo.h>
#include <circle/memory.h>

static const char FromKernel[] = "kernel";

CKernel::CKernel (void)
:	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer)
{
	m_ActLED.Blink (5);		// visible sign of life before the console is up
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	if (bOK)
	{
		bOK = m_Serial.Initialize (115200);
	}

	if (bOK)
	{
		// Log straight to the serial port (no framebuffer in this milestone).
		bOK = m_Logger.Initialize (&m_Serial);
	}

	if (bOK)
	{
		bOK = m_Interrupt.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Timer.Initialize ();
	}

	return bOK;
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice,
			"Multi-process kernel on Circle -- milestone #2 (boot + console)");
	m_Logger.Write (FromKernel, LogNotice, "Compiled on " __DATE__ " " __TIME__);

	CMachineInfo *pInfo = CMachineInfo::Get ();
	m_Logger.Write (FromKernel, LogNotice, "Running on %s, %lu MB RAM",
			pInfo->GetMachineName (),
			(unsigned long) (CMemorySystem::Get ()->GetMemSize () / 0x100000));

	// Heartbeat. This proves the full path works: GIC -> timer IRQ -> tick ->
	// serial output. It is the placeholder for "start the scheduler" (#3); for
	// now there is no scheduler, so we busy-wait on the 1 Hz system timer.
	unsigned nLastTime = m_Timer.GetTime ();
	unsigned nUptime = 0;
	while (1)
	{
		while (nLastTime == m_Timer.GetTime ())
		{
			// spin until the next second elapses
		}
		nLastTime = m_Timer.GetTime ();

		m_Logger.Write (FromKernel, LogNotice, "alive -- uptime %u s", ++nUptime);
	}

	return ShutdownHalt;
}
