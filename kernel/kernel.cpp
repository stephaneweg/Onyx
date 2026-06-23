//
// kernel.cpp
//
#include "kernel.h"
#include <circle/machineinfo.h>
#include <circle/memory.h>
#include <circle/sched/task.h>
#include <circle/synchronize.h>
#include <circle/util.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/input/mouse.h>
#include <kern/trapframe.h>
#include <kern/addrspace.h>
#include <kern/layout.h>
#include <kern/elf.h>
#include <kern/gui/gimage.h>

static const char FromKernel[] = "kernel";

// Defined in arch/aarch64/exception.cpp: route kernel panics to this displayed
// framebuffer so an exception is visible after the compositor takes the screen.
void SetPanicGraphics (C2DGraphics *p2D);

//
// CUserProcessTask (Option C): launch an ELF as an EL1 app in its own address
// space. The thread builds a private address space, loads the ELF segments
// (EL1-executable), activates the space, and CALLS the entry point directly --
// the app runs in EL1 on this thread's (large) stack and calls kapi_* kernel
// functions directly (resolved at link). Apps are isolated from each other (own
// page tables) but not from the kernel. The thread is preempted like any other.
//
class CUserProcessTask : public CTask
{
public:
	// Large stack: the app and the kernel functions it calls share this thread stack.
	CUserProcessTask (const u8 *pELF, unsigned nSize, const char *pName, CLogger *pLogger)
	:	CTask (0x40000),	// 256 KB
		m_pELF (pELF), m_nSize (nSize), m_pName (pName), m_pLogger (pLogger)
	{
		SetName (pName);
	}

	void Run (void) override
	{
		CAddressSpace *pAS = new CAddressSpace ();
		if (pAS == 0 || !pAS->IsValid ())
		{
			m_pLogger->Write (m_pName, LogError, "address space creation failed");
			return;
		}

		u64 ulEntry = 0;
		boolean bLoaded = LoadELF (m_pELF, m_nSize, pAS, &ulEntry);

		// The file image is no longer needed: LoadELF copied the segments into the
		// app's own frames. Free it now (it was new[]'d by LoadFileFromSD).
		delete [] (u8 *) m_pELF;
		m_pELF = 0;

		if (!bLoaded)
		{
			m_pLogger->Write (m_pName, LogError, "ELF load failed");
			delete pAS;
			return;
		}

		// Become this address space (kernel stays mapped + EL1-accessible).
		SetUserData (pAS, TASK_USER_DATA_USER);
		pAS->Activate ();

		m_pLogger->Write (m_pName, LogNotice, "running (EL1) entry %lp ASID %u",
				  (void *) ulEntry, (unsigned) pAS->GetASID ());

		// Direct EL1 call into the app. It calls kapi_* directly; loops or exits.
		((void (*) (void)) ulEntry) ();

		// App returned: terminate (frees the address space + window).
		CScheduler::Get ()->GetCurrentTask ()->Terminate ();
	}

private:
	const u8   *m_pELF;
	unsigned    m_nSize;
	const char *m_pName;
	CLogger	   *m_pLogger;
};

//
// Compositor (#10): a kernel thread that composites all windows onto the screen
// and presents at ~60 fps. It is the single owner of UpdateDisplay(); window
// owners only draw into their own canvas.
//
class CCompositorTask : public CTask
{
public:
	CCompositorTask (C2DGraphics *p2D, CWindowManager *pWM)
	:	m_p2D (p2D), m_pWM (pWM)
	{
		SetName ("compositor");
	}

	void Run (void) override
	{
		int nW = (int) m_p2D->GetWidth ();
		int nH = (int) m_p2D->GetHeight ();
		for (;;)
		{
			if (DebugConsoleActive ())
			{
				// An app exited: the debug console owns the display now. Stop
				// presenting so we don't fight it for the framebuffer.
				CScheduler::Get ()->MsSleep (100);
				continue;
			}
			GImage Screen ((u32 *) m_p2D->GetBuffer (), nW, nH);
			m_pWM->Composite (&Screen);
			m_p2D->UpdateDisplay ();
			CScheduler::Get ()->MsSleep (16);
		}
	}

private:
	C2DGraphics    *m_p2D;
	CWindowManager *m_pWM;
};

//
// Input (#13): a kernel thread that pumps USB plug-and-play and, once a mouse /
// keyboard appears, wires its events into the window manager. Mouse moves/clicks
// drive the cursor + window raise/drag in CWindowManager; key presses are logged
// for now (widget routing arrives with the widget toolkit, #14/#15).
//
class CInputTask : public CTask
{
public:
	CInputTask (CUSBHCIDevice *pUSB, CDeviceNameService *pDNS, CDisplay *pDisplay,
		    CLogger *pLogger)
	:	m_pUSB (pUSB), m_pDNS (pDNS), m_pDisplay (pDisplay), m_pLogger (pLogger),
		m_pMouse (0), m_pKeyboard (0)
	{
		SetName ("input");
		s_pThis = this;
	}

	void Run (void) override
	{
		Detect ();			// devices already present at boot
		for (;;)
		{
			if (m_pUSB->UpdatePlugAndPlay () || m_pMouse == 0 || m_pKeyboard == 0)
			{
				Detect ();
			}
			CScheduler::Get ()->MsSleep (100);
		}
	}

private:
	void Detect (void)
	{
		if (m_pKeyboard == 0)
		{
			m_pKeyboard = (CUSBKeyboardDevice *)
				m_pDNS->GetDevice ("ukbd1", FALSE);
			if (m_pKeyboard != 0)
			{
				m_pKeyboard->RegisterRemovedHandler (KeyboardRemoved);
				m_pKeyboard->RegisterKeyPressedHandler (KeyPressedStub);
				m_pLogger->Write ("input", LogNotice, "keyboard attached");
			}
		}

		if (m_pMouse == 0)
		{
			m_pMouse = (CMouseDevice *) m_pDNS->GetDevice ("mouse1", FALSE);
			if (m_pMouse != 0)
			{
				m_pMouse->RegisterRemovedHandler (MouseRemoved);
				// Cooked mode: absolute coords clamped to the display; bCursor
				// FALSE -- our compositor draws the cursor itself.
				if (m_pMouse->Setup (m_pDisplay, FALSE))
				{
					m_pMouse->RegisterEventHandler (MouseEventStub);
					m_pLogger->Write ("input", LogNotice, "mouse attached");
				}
			}
		}
	}

	static void MouseEventStub (TMouseEvent /*Event*/, unsigned nButtons,
				    unsigned nPosX, unsigned nPosY, int /*nWheelMove*/)
	{
		if (CWindowManager::Get () != 0)
		{
			CWindowManager::Get ()->OnMouse ((int) nPosX, (int) nPosY, nButtons);
		}
	}

	static void KeyPressedStub (const char *pString)
	{
		if (s_pThis != 0 && s_pThis->m_pLogger != 0)
		{
			s_pThis->m_pLogger->Write ("kbd", LogNotice, "key: %s", pString);
		}
	}

	static void MouseRemoved (CDevice *, void *)
	{
		if (s_pThis != 0) { s_pThis->m_pMouse = 0; }
	}

	static void KeyboardRemoved (CDevice *, void *)
	{
		if (s_pThis != 0) { s_pThis->m_pKeyboard = 0; }
	}

	CUSBHCIDevice	   *m_pUSB;
	CDeviceNameService *m_pDNS;
	CDisplay	   *m_pDisplay;
	CLogger		   *m_pLogger;
	CMouseDevice       * volatile m_pMouse;
	CUSBKeyboardDevice * volatile m_pKeyboard;

	static CInputTask  *s_pThis;
};

CInputTask *CInputTask::s_pThis = 0;

CKernel::CKernel (void)
:	m_Screen (SCREEN_WIDTH, SCREEN_HEIGHT),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_2DGraphics (SCREEN_WIDTH, SCREEN_HEIGHT, FALSE),	// VSync off: avoid page-flip present
	m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
	m_USB (&m_Interrupt, &m_Timer, TRUE),			// TRUE: enable plug-and-play
	m_FbConsole (&m_2DGraphics),
	m_bGraphics (FALSE),
	m_bSDMounted (FALSE),
	m_bUSB (FALSE)
{
	m_ActLED.Blink (5);		// visible sign of life before the console is up
}

// Read a whole file from the mounted SD card into a freshly allocated buffer.
// Returns the buffer (caller may leak it for a one-shot process load) + its size,
// or 0 on any error.
static u8 *LoadFileFromSD (const char *pPath, unsigned *pSize)
{
	FIL File;
	if (f_open (&File, pPath, FA_READ) != FR_OK)
	{
		return 0;
	}

	unsigned nSize = (unsigned) f_size (&File);
	u8 *pBuffer = new u8[nSize];
	if (pBuffer == 0)
	{
		f_close (&File);
		return 0;
	}

	UINT nRead = 0;
	if (f_read (&File, pBuffer, nSize, &nRead) != FR_OK || nRead != nSize)
	{
		delete [] pBuffer;
		f_close (&File);
		return 0;
	}

	f_close (&File);
	*pSize = nSize;
	return pBuffer;
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	// Bring up the HDMI text console FIRST (like VMKernel) so the boot log is
	// visible on screen even without a serial cable.
	if (bOK)
	{
		bOK = m_Screen.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Serial.Initialize (115200);
	}

	if (bOK)
	{
		// Log through a switch: normally the HDMI boot console (m_Screen). When an
		// app exits we flip it to the framebuffer console so messages stay visible
		// after the compositor takes the display.
		m_LogSwitch.SetNormal (&m_Screen);
		DebugConsoleRegister (&m_LogSwitch, &m_FbConsole);
		bOK = m_Logger.Initialize (&m_LogSwitch);
	}

	if (bOK)
	{
		bOK = m_Interrupt.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Timer.Initialize ();
	}

	if (bOK)
	{
		// Take over exception handling from Circle: install our VBAR_EL1 (EL0/EL1
		// vectors + trap frame + syscall path), and drive the scheduler's time
		// slice from the 100 Hz timer tick -> preemptive multitasking (#4).
		install_vectors ();
		m_Timer.RegisterPeriodicHandler (PeriodicTick);

		// Per-process address spaces (#5): remember the kernel TTBR0 and switch
		// TTBR0/ASID on every task switch based on the task's address space.
		AddrSpaceInit ();
		m_Scheduler.RegisterTaskSwitchHandler (AddressSpaceTaskSwitch);
		m_Scheduler.RegisterTaskTerminationHandler (AddressSpaceTaskTerminate);
	}

	// Framebuffer is optional (needs an attached display). Do not fail boot if it
	// is unavailable -- the console + scheduler still run.
	if (bOK)
	{
		m_bGraphics = m_2DGraphics.Initialize ();
		if (!m_bGraphics)
		{
			m_Logger.Write (FromKernel, LogWarning, "no framebuffer/display");
		}
		else
		{
			// Route kernel panics to the displayed framebuffer (the boot console
			// stops being scanned out once the compositor takes over).
			SetPanicGraphics (&m_2DGraphics);
		}
	}

	// SD card is optional too: mount it so apps can be loaded from the card. If it
	// is absent, we fall back to the ELF images embedded in the kernel.
	if (bOK)
	{
		if (m_EMMC.Initialize () && f_mount (&m_FileSystem, "SD:", 1) == FR_OK)
		{
			m_bSDMounted = TRUE;
			m_Logger.Write (FromKernel, LogNotice, "SD card mounted (SD:)");
		}
		else
		{
			m_Logger.Write (FromKernel, LogWarning,
					"no SD card; using embedded apps");
		}
	}

	// USB host (mouse + keyboard). Optional: if it fails, the GUI still runs, just
	// without input. Initialize() scans for devices already attached at boot; the
	// input thread later pumps UpdatePlugAndPlay() for hot-plug.
	if (bOK)
	{
		m_bUSB = m_USB.Initialize ();
		if (!m_bUSB)
		{
			m_Logger.Write (FromKernel, LogWarning, "no USB host; input disabled");
		}
	}

	return bOK;
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice,
			"Multi-process kernel on Circle (EL1 apps + direct kapi calls)");
	m_Logger.Write (FromKernel, LogNotice, "Compiled on " __DATE__ " " __TIME__);

	CMachineInfo *pInfo = CMachineInfo::Get ();
	m_Logger.Write (FromKernel, LogNotice, "Running on %s, %lu MB RAM",
			pInfo->GetMachineName (),
			(unsigned long) (CMemorySystem::Get ()->GetMemSize () / 0x100000));

	// Load + spawn the two demos (Option C: EL1 apps that link against kapi_*).
	// They create their windows and draw into the shared canvas; the compositor is
	// started AFTER a readable pause so the boot log stays on screen first.
	if (m_bGraphics)
	{
		static const char *Names[3] = { "demoA", "demoB", "demoC" };
		static const char *Paths[3] = { "SD:demoA.elf", "SD:demoB.elf", "SD:demoC.elf" };

		if (!m_bSDMounted)
		{
			m_Logger.Write (FromKernel, LogError, "no SD card: cannot load demos");
		}
		else
		{
			for (int i = 0; i < 3; i++)
			{
				unsigned nSize = 0;
				const u8 *pElf = LoadFileFromSD (Paths[i], &nSize);
				if (pElf == 0)
				{
					m_Logger.Write (FromKernel, LogError, "cannot load %s", Paths[i]);
					continue;
				}
				m_Logger.Write (FromKernel, LogNotice, "%s: loaded from %s (%u bytes)",
						Names[i], Paths[i], nSize);
				new CUserProcessTask (pElf, nSize, Names[i], &m_Logger);
			}
		}

		// Keep the boot log readable for a few seconds, THEN start the compositor
		// (which takes over the display). If the screen goes black only after this,
		// the problem is the framebuffer present, not the boot/loading path.
		m_Logger.Write (FromKernel, LogNotice, "boot OK -- starting graphics in 6 s ...");
		m_Scheduler.MsSleep (6000);

		new CCompositorTask (&m_2DGraphics, &m_WindowManager);
		m_Logger.Write (FromKernel, LogNotice, "compositor started");

		// Start input only after the compositor: the mouse handler drives the
		// cursor + window raise/drag, which only make sense once we're presenting.
		if (m_bUSB)
		{
			new CInputTask (&m_USB, &m_DeviceNameService,
					m_2DGraphics.GetDisplay (), &m_Logger);
			m_Logger.Write (FromKernel, LogNotice, "input started");
		}
	}
	else
	{
		m_Logger.Write (FromKernel, LogWarning, "no framebuffer; idling");
	}

	// Janitor loop: reap tasks that have ended (closed apps) in a safe context,
	// freeing their address spaces. Terminated tasks stop being scheduled
	// immediately; this is the "en amont du scheduler" reclamation stage.
	//
	// Heartbeat (only once the debug console owns the screen, i.e. after an app
	// exited): proves the kernel is still alive AFTER a reap -- if "alive N" keeps
	// incrementing past "reap: done", the close succeeded and the static screen is
	// just the stopped compositor, not a crash.
	for (;;)
	{
		// ISOLATION TEST: reaping disabled. Terminated tasks are just left alone
		// (skipped by GetNextTask), nothing is freed. If close now works (GUI stays
		// live), the reap/teardown is the culprit.
		// m_Scheduler.ReapTerminatedTasks ();
		m_Scheduler.MsSleep (100);
	}

	return ShutdownHalt;
}
