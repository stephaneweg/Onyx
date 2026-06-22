//
// kernel.cpp
//
#include "kernel.h"
#include <circle/machineinfo.h>
#include <circle/memory.h>
#include <circle/sched/task.h>

static const char FromKernel[] = "kernel";

//
// Demo worker for milestone #3: a kernel thread that logs at a fixed interval and
// then sleeps, yielding the CPU. Two of these at different intervals make the
// scheduler's interleaving visible over the serial console. (Cooperative for now:
// the sleep is what yields; timer-IRQ preemption arrives in #4.)
//
class CHeartbeatTask : public CTask
{
public:
	CHeartbeatTask (CLogger *pLogger, const char *pName, unsigned nIntervalMs)
	:	m_pLogger (pLogger),
		m_pName (pName),
		m_nIntervalMs (nIntervalMs)
	{
		SetName (pName);
	}

	void Run (void) override
	{
		unsigned nCount = 0;
		while (1)
		{
			m_pLogger->Write (m_pName, LogNotice, "beat %u (every %u ms)",
					  ++nCount, m_nIntervalMs);

			CScheduler::Get ()->MsSleep (m_nIntervalMs);
		}
	}

private:
	CLogger	   *m_pLogger;
	const char *m_pName;
	unsigned    m_nIntervalMs;
};

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

	// --- Milestone #3 demo: kernel threads scheduled by our CScheduler ---------
	//
	// m_Scheduler is already constructed (it created the "main" task -- this very
	// context). Spawn two worker threads with different periods, then let this
	// (main) thread also participate. With no preemption yet, control passes
	// whenever a thread sleeps; the interleaving over serial shows the scheduler
	// and the block/wake path working.
	m_Logger.Write (FromKernel, LogNotice, "starting scheduler with 2 worker threads");

	new CHeartbeatTask (&m_Logger, "beat-A", 700);
	new CHeartbeatTask (&m_Logger, "beat-B", 1100);

	unsigned nUptime = 0;
	while (1)
	{
		m_Scheduler.MsSleep (5000);
		m_Logger.Write (FromKernel, LogNotice, "main: uptime ~%u s", nUptime += 5);
	}

	return ShutdownHalt;
}
