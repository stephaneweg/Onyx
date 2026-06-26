//
// kernel.cpp
//
#include "kernel.h"
#include <circle/machineinfo.h>
#include <circle/memory.h>
#include <circle/sched/task.h>
#include <circle/synchronize.h>
#include <circle/string.h>
#include <circle/util.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/input/keymap.h>		// CKeyMap, PHY_MAX_CODE, K_CTRLTAB (SetKeyMapData)
#include <circle/input/mouse.h>
#include <kern/trapframe.h>
#include <kern/addrspace.h>
#include <kern/applaunch.h>
#include <kern/stream.h>
#include <kern/kapitable.h>
#include <kern/layout.h>
#include <kern/elf.h>
#include <kern/gui/gimage.h>
#include <kern/net.h>
#include <circle/net/ipaddress.h>
#include <circle/net/ntpdaemon.h>

static const char FromKernel[] = "kernel";

// WLAN firmware (brcmfmac43455-sdio.*) lives here; the user's SSID/PSK go in the
// config file. Both must be staged on the SD card -- see docs/NETWORK.md.
#define WLAN_FIRMWARE_PATH	"SD:/firmware/"
#define WLAN_CONFIG_FILE	"SD:/etc/wpa_supplicant.conf"

// Network globals (declared in kern/net.h). g_pNet is published once the kernel
// object exists; g_bNetUp flips TRUE when the link is DHCP-bound.
CNetSubSystem	 *g_pNet   = 0;
volatile boolean  g_bNetUp = FALSE;

// Clock: timezone offset from UTC in minutes (system.ini "timezone="; default CET
// +60, set "120" for CEST summer time) + the NTP server to sync against once the
// link is up (system.ini "ntp="). The NTP daemon updates CTimer's wall clock.
static int  g_nTimeZoneMin = 60;
static char g_szNtpServer[64] = "pool.ntp.org";

// Defined in arch/aarch64/exception.cpp: route kernel panics to this displayed
// framebuffer so an exception is visible after the compositor takes the screen.
void SetPanicGraphics (C2DGraphics *p2D);

// Verbose logging flag: when on, the kernel logs app lifecycle events (spawn / exit
// / orphan kill). Set at boot from SD:system.ini (verbose=1) and toggled at runtime
// via kapi_set_verbose / the `verbose` command. VLOG(...) is a gated CLogger::Write.
boolean g_bVerbose = FALSE;
#define VLOG(...)	do { if (g_bVerbose) CLogger::Get ()->Write (__VA_ARGS__); } while (0)

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
	// We take the ELF PATH (not a preloaded buffer): the SD read happens in Run(), on
	// THIS task's thread the first time the scheduler switches to us -- so the launcher /
	// shell that spawned us is never blocked by the (slow, ~MB) read.
	// pStdin/pStdout/pProcess/pArgs are for spawned processes (default 0 for plain
	// launches): the streams + spawn handle are installed into the address space so
	// the app's stdio + exit status work; pArgs becomes kapi_get_args.
	CUserProcessTask (const char *pPath, const char *pName, CLogger *pLogger,
			  CStream *pStdin = 0, CStream *pStdout = 0, CProcess *pProcess = 0,
			  const char *pArgs = 0, const char *pCwd = 0, unsigned nParentPid = 0)
	:	CTask (0x40000),	// 256 KB
		m_pLogger (pLogger),
		m_pStdin (pStdin), m_pStdout (pStdout), m_pProcess (pProcess),
		m_nParentPid (nParentPid)
	{
		SetName (pName);	// copies into CTask::m_Name (caller's may be transient)
		unsigned p = 0;		// copy the path (the caller's string may be transient)
		if (pPath != 0)
			for (; pPath[p] != '\0' && p < sizeof (m_Path) - 1; p++) m_Path[p] = pPath[p];
		m_Path[p] = '\0';
		unsigned i = 0;
		if (pArgs != 0)
			for (; pArgs[i] != '\0' && i < sizeof (m_Args) - 1; i++) m_Args[i] = pArgs[i];
		m_Args[i] = '\0';
		unsigned k = 0;		// inherited working directory (empty => child defaults to root)
		if (pCwd != 0)
			for (; pCwd[k] != '\0' && k < sizeof (m_Cwd) - 1; k++) m_Cwd[k] = pCwd[k];
		m_Cwd[k] = '\0';
	}

	void Run (void) override
	{
		CAddressSpace *pAS = new CAddressSpace ();
		if (pAS == 0 || !pAS->IsValid ())
		{
			m_pLogger->Write (GetName (), LogError, "address space creation failed");
			// The AS never took the streams here, so release the caller's refs ourselves.
			if (m_pStdin  != 0) m_pStdin->Release ();
			if (m_pStdout != 0) m_pStdout->Release ();
			if (m_pProcess != 0) { m_pProcess->nStatus = -1; m_pProcess->bDone = TRUE; }
			delete pAS;
			return;
		}

		// Install stdio + spawn handle + argv before the app runs (the AS owns the
		// stream refs from here, and releases them / marks the process on teardown -- so
		// every failure path below just `delete pAS`).
		pAS->SetStdin (m_pStdin);
		pAS->SetStdout (m_pStdout);
		pAS->SetProcess (m_pProcess);
		pAS->SetArgs (m_Args);
		if (m_Cwd[0] != '\0') pAS->SetCwd (m_Cwd);	// else keep the default root
		pAS->SetParentPid (m_nParentPid);		// 0 = no parent (drawer launch)

		// Load the ELF from SD HERE -- on our own thread, deferred to our first schedule
		// ("when we are switched to") -- so the launcher/shell wasn't blocked by the read.
		// Read in chunks, yielding between them, so the compositor + the rest of the UI
		// keep running while we load (the SD read is the slow part; we're single-core).
		FIL File;
		if (f_open (&File, m_Path, FA_READ) != FR_OK)
		{
			m_pLogger->Write (GetName (), LogError, "cannot open %s", m_Path);
			delete pAS;
			return;
		}
		unsigned nSize = (unsigned) f_size (&File);
		u8 *pELF = new u8[nSize];
		if (pELF == 0)
		{
			f_close (&File);
			m_pLogger->Write (GetName (), LogError, "out of memory loading %s", m_Path);
			delete pAS;
			return;
		}
		unsigned nDone = 0; boolean bRead = TRUE;
		while (nDone < nSize)
		{
			unsigned nChunk = nSize - nDone;
			if (nChunk > 0x20000) nChunk = 0x20000;		// 128 KB, then yield
			UINT nRead = 0;
			if (f_read (&File, pELF + nDone, nChunk, &nRead) != FR_OK || nRead == 0)
			{
				bRead = FALSE; break;
			}
			nDone += nRead;
			CScheduler::Get ()->Yield ();			// let the UI/compositor run
		}
		f_close (&File);
		if (!bRead)
		{
			delete [] pELF;
			m_pLogger->Write (GetName (), LogError, "read failed %s", m_Path);
			delete pAS;
			return;
		}

		u64 ulEntry = 0;
		boolean bLoaded = LoadELF (pELF, nSize, pAS, &ulEntry);

		// The file image is no longer needed: LoadELF copied the segments into the
		// app's own frames. Free it now.
		delete [] pELF;

		if (!bLoaded)
		{
			m_pLogger->Write (GetName (), LogError, "ELF load failed");
			delete pAS;
			return;
		}

		// Become this address space (kernel stays mapped + EL1-accessible).
		SetUserData (pAS, TASK_USER_DATA_USER);
		pAS->Activate ();

		m_pLogger->Write (GetName (), LogNotice, "running (EL1) entry %lp ASID %u",
				  (void *) ulEntry, (unsigned) pAS->GetASID ());

		// Direct EL1 call into the app. It calls kapi_* directly; loops or exits.
		((void (*) (void)) ulEntry) ();

		// App returned: terminate (frees the address space + window).
		CScheduler::Get ()->GetCurrentTask ()->Terminate ();
	}

private:
	char	    m_Path[256];	// ELF path; loaded in Run() on our own thread
	CLogger	   *m_pLogger;
	CStream	   *m_pStdin;
	CStream	   *m_pStdout;
	CProcess   *m_pProcess;
	char	    m_Args[256];
	char	    m_Cwd[256];
	unsigned    m_nParentPid;
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

// Cascade kill: when a process dies, its still-running children must die too (e.g.
// killing the terminal also kills its shell + whatever the shell spawned). We track
// each app's parent pid; here we terminate any app whose parent pid is no longer a
// live task. Run from the reaper (a normal task context), so over a few passes a
// dead parent's whole subtree is torn down. Parent pid 0 = no parent (never orphaned).
struct OrphanScan
{
	unsigned pids[MAX_TASKS]; int npids;
	CTask   *kid[MAX_TASKS]; unsigned kidparent[MAX_TASKS]; int nkid;
};
static boolean OrphanCollect (CTask *pTask, const char *, TTaskState State,
			      TTaskFlags, void *pParam)
{
	if (State == TaskStateTerminated) return TRUE;
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	if (pAS == 0) return TRUE;				// kernel task: no pid/parent
	OrphanScan *s = (OrphanScan *) pParam;
	if (s->npids < MAX_TASKS) s->pids[s->npids++] = pAS->GetPid ();
	unsigned par = pAS->GetParentPid ();
	if (par != 0 && s->nkid < MAX_TASKS) { s->kid[s->nkid] = pTask; s->kidparent[s->nkid] = par; s->nkid++; }
	return TRUE;
}
static void TerminateOrphans (void)
{
	if (!CScheduler::IsActive ()) return;
	static OrphanScan s;					// static: keep it off the stack
	s.npids = 0; s.nkid = 0;
	CScheduler::Get ()->EnumerateTasks (OrphanCollect, &s);
	for (int i = 0; i < s.nkid; i++)
	{
		boolean bAlive = FALSE;
		for (int j = 0; j < s.npids; j++) if (s.pids[j] == s.kidparent[i]) { bAlive = TRUE; break; }
		if (!bAlive)
		{
			VLOG ("proc", LogNotice, "orphan %s (parent pid %u gone) terminated",
			      s.kid[i]->GetName (), s.kidparent[i]);
			CScheduler::Get ()->TerminateTask (s.kid[i]);
		}
	}
}

//
// Reaper: a dedicated kernel thread that reclaims tasks which have ended (closed
// apps) -- freeing their address space, window, and the CTask + stack. Kept as a
// proper scheduler task (like the compositor), separate from CKernel::Run (the
// special "main" task), so the teardown always runs in a normal task context.
//
class CReaperTask : public CTask
{
public:
	CReaperTask (void)
	{
		SetName ("reaper");
	}

	void Run (void) override
	{
		for (;;)
		{
			TerminateOrphans ();			// kill children of dead parents
			CScheduler::Get ()->ReapTerminatedTasks ();
			CScheduler::Get ()->MsSleep (50);
		}
	}
};

//
// Input (#13): a kernel thread that pumps USB plug-and-play and, once a mouse /
// keyboard appears, wires its events into the window manager. Mouse moves/clicks
// drive the cursor + window raise/drag in CWindowManager; key presses are logged
// for now (widget routing arrives with the widget toolkit, #14/#15).
//
// Current keyboard layout name (e.g. "FR", "BE"); empty => none loaded yet. Set by the
// keyb command / theme editor via kapi_set_keymap_data; reported by kapi_get_keymap.
static char g_szKeyMap[8] = "";

// The live keyboard layout, kept as a plain RAM table -- the kernel compiles in NO
// country map. Zero (all KeyNone) at boot; kapi_set_keymap_data copies a .kmap payload
// here, then onto the attached keyboard. Re-applied on every keyboard (re-)attach so a
// hot re-plug keeps the layout (a fresh CKeyMap starts empty). Row-major [phyCode][table].
static u16 g_KeyMap[PHY_MAX_CODE + 1][K_CTRLTAB + 1];

// Copy a raw row-major [phyCode][table] map onto a CKeyMap through the public SetEntry
// (phyCode 0 is skipped -- SetEntry rejects it). Used to (re-)apply g_KeyMap to a keyboard.
static void LoadKeyMapTable (CKeyMap *pKeyMap, const u16 *pMap)
{
	for (u8 nTable = 0; nTable <= K_CTRLTAB; nTable++)
		for (u8 nPhy = 1; nPhy <= PHY_MAX_CODE; nPhy++)
			pKeyMap->SetEntry (nTable, nPhy, pMap[nPhy * (K_CTRLTAB + 1) + nTable]);
}

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

	// Load a raw keymap table onto the live keyboard AND into the persistent g_KeyMap
	// snapshot: (PHY_MAX_CODE+1) x (K_CTRLTAB+1) u16 entries, row-major
	// (m_KeyMap[phyCode][table]). The bytes come from a SD:/etc/keymaps/<X>.kmap file
	// (via kapi_set_keymap_data); we keep a copy (so a later keyboard re-plug restores
	// the layout) and push it onto the live keyboard through the public SetEntry, so
	// layouts need no kernel rebuild. Returns FALSE only if the blob is malformed
	// (null / wrong size). When valid the snapshot is ALWAYS updated and TRUE returned;
	// it is pushed onto the live keyboard at once if one is attached, otherwise applied
	// on the next attach (Detect() re-applies g_KeyMap). So a layout set before the
	// keyboard enumerates is never lost -- this is what kills the boot-time `keyb` race
	// (slow USB enumeration used to make keyb give up, leaving the keyboard map-less).
	static boolean SetKeyMapData (const void *pData, unsigned nLen)
	{
		if (pData == 0)
		{
			return FALSE;
		}
		if (nLen != (unsigned) (PHY_MAX_CODE + 1) * (K_CTRLTAB + 1) * sizeof (u16))
		{
			return FALSE;
		}
		const u16 *pMap = (const u16 *) pData;
		for (u8 nTable = 0; nTable <= K_CTRLTAB; nTable++)
			for (u8 nPhy = 0; nPhy <= PHY_MAX_CODE; nPhy++)
				g_KeyMap[nPhy][nTable] = pMap[nPhy * (K_CTRLTAB + 1) + nTable];
		// Apply live if a keyboard is already up; otherwise the snapshot is enough --
		// Detect() loads g_KeyMap onto the keyboard the moment it attaches.
		if (s_pThis != 0 && s_pThis->m_pKeyboard != 0)
		{
			LoadKeyMapTable (s_pThis->m_pKeyboard->GetKeyMap (), (const u16 *) g_KeyMap);
		}
		return TRUE;
	}

	// Is a USB keyboard attached and ready right now? Exposed to userspace via
	// kapi_kbd_ready so the `keyb` tool can poll before applying a layout at boot
	// (it may run before USB enumeration finishes).
	static boolean HasKeyboard (void) { return s_pThis != 0 && s_pThis->m_pKeyboard != 0; }

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
				// Apply the current layout snapshot (empty until keyb loads a .kmap),
				// so a hot re-plug keeps the layout -- a fresh CKeyMap starts empty.
				LoadKeyMapTable (m_pKeyboard->GetKeyMap (), (const u16 *) g_KeyMap);
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

	// Circle's cooked mouse reports the *changed button mask* on MouseDown/MouseUp
	// (not the full state) and the full state only on MouseMove. If we forwarded that
	// raw value, a release (MouseUp, mask=1) would look like "still pressed", so the
	// WM would miss the press edge of a second click -- breaking double-click. So we
	// reconstruct the real button bitmask: Down sets the bit, Up clears it, Move syncs.
	static void MouseEventStub (TMouseEvent Event, unsigned nButtons,
				    unsigned nPosX, unsigned nPosY, int nWheelMove)
	{
		static unsigned s_nButtons = 0;
		switch (Event)
		{
		case MouseEventMouseDown: s_nButtons |= nButtons;  break;	// nButtons = changed mask
		case MouseEventMouseUp:   s_nButtons &= ~nButtons; break;
		case MouseEventMouseMove: s_nButtons = nButtons;   break;	// full state
		case MouseEventMouseWheel:					// scroll notch, no button change
			if (CWindowManager::Get () != 0)
				CWindowManager::Get ()->OnMouseWheel ((int) nPosX, (int) nPosY, nWheelMove);
			return;
		default: break;
		}
		if (CWindowManager::Get () != 0)
		{
			CWindowManager::Get ()->OnMouse ((int) nPosX, (int) nPosY, s_nButtons);
		}
	}

	static void KeyPressedStub (const char *pString)
	{
		// Route to the focused widget (a textbox); the WM edits its text + posts a
		// TEXT_CHANGED event to the owning app.
		if (CWindowManager::Get () != 0)
		{
			CWindowManager::Get ()->OnKey (pString);
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

// Keyboard-layout control exposed to the kapi layer (sys/kapi.cpp). The kernel no longer
// compiles in any country map, so loading one *by name* (kapi_set_keymap) is unsupported
// and always fails -- layouts ship as SD:/etc/keymaps/*.kmap and are loaded by name+data
// through KernelSetKeyMapData. The ABI slot stays (append-only); the userspace helper
// (ax_load_keymap) treats the failure as "no compiled map" and falls back to the file.
boolean KernelSetKeyMap (const char *pName)
{
	(void) pName;
	return FALSE;
}

// Load a keymap from a raw table (validated SD:/etc/keymaps/<X>.kmap payload) and
// record its name for reporting. Used by kapi_set_keymap_data so layouts can be added
// as files without recompiling the kernel.
boolean KernelSetKeyMapData (const char *pName, const void *pData, unsigned nLen)
{
	if (!CInputTask::SetKeyMapData (pData, nLen))
	{
		return FALSE;
	}
	unsigned i = 0;
	if (pName != 0)
		for (; pName[i] != '\0' && i < sizeof (g_szKeyMap) - 1; i++) g_szKeyMap[i] = pName[i];
	g_szKeyMap[i] = '\0';
	return TRUE;
}

const char *KernelGetKeyMap (void)
{
	return g_szKeyMap;
}

// Keyboard readiness, exposed to userspace (kapi_kbd_ready). The `keyb` tool polls
// this at boot and applies the layout only once the keyboard has enumerated -- the
// kernel no longer applies any layout itself (cmdline keymap= is ignored).
boolean KernelKeyboardReady (void)
{
	return CInputTask::HasKeyboard ();
}

// Verbose flag control, exposed to the kapi layer (kapi_set_verbose / get_verbose).
void KernelSetVerbose (boolean bOn)
{
	g_bVerbose = bOn;
	CLogger::Get ()->Write ("verbose", LogNotice, "verbose logging %s", bOn ? "ON" : "OFF");
}
boolean KernelGetVerbose (void) { return g_bVerbose; }

//
// CNetBringupTask -- bring up WLAN + the TCP/IP stack on the primary core in the
// background, so a missing firmware / wpa_supplicant.conf / access point never
// blocks (or slows) the GUI boot. The order is mandatory: the WLAN device must be
// up before the net subsystem, and wpa_supplicant associates after the stack is
// initialized. Once Initialize() returns, Circle's own CNetTask / CPHYTask keep
// the stack running on our scheduler; this task just waits for the link, logs the
// address and exits (it has no user address space, so returning is clean).
//
class CNetBringupTask : public CTask
{
public:
	CNetBringupTask (CBcm4343Device *pWLAN, CNetSubSystem *pNet,
			 CWPASupplicant *pWPA, CLogger *pLogger)
	:	m_pWLAN (pWLAN), m_pNet (pNet), m_pWPA (pWPA), m_pLogger (pLogger)
	{
		SetName ("net");
	}

	void Run (void) override
	{
		g_pNet = m_pNet;		// publish (still down until associated)

		m_pLogger->Write (FromKernel, LogNotice,
				  "net: bringing up WLAN (firmware " WLAN_FIRMWARE_PATH ")");
		if (!m_pWLAN->Initialize ())
		{
			m_pLogger->Write (FromKernel, LogWarning,
				"net: WLAN init failed -- is " WLAN_FIRMWARE_PATH " present?");
			return;
		}

		if (!m_pNet->Initialize (FALSE))	// FALSE: don't block here for activate
		{
			m_pLogger->Write (FromKernel, LogWarning, "net: TCP/IP init failed");
			return;
		}

		m_pLogger->Write (FromKernel, LogNotice,
				  "net: associating (" WLAN_CONFIG_FILE ") ...");
		if (!m_pWPA->Initialize ())
		{
			m_pLogger->Write (FromKernel, LogWarning,
				"net: wpa_supplicant init failed -- is " WLAN_CONFIG_FILE " present?");
			return;
		}

		// Wait for the link to come up (DHCP bind). Log progress occasionally so a
		// stuck association is visible, but never give up -- the AP may appear later.
		unsigned nWaited = 0;
		while (!m_pNet->IsRunning ())
		{
			CScheduler::Get ()->MsSleep (250);
			if ((nWaited += 250) % 10000 == 0)
				m_pLogger->Write (FromKernel, LogNotice,
						  "net: still associating (%u s) ...", nWaited / 1000);
		}

		CString IPString;
		m_pNet->GetConfig ()->GetIPAddress ()->Format (&IPString);
		m_pLogger->Write (FromKernel, LogNotice, "net: up, IP %s",
				  (const char *) IPString);
		g_bNetUp = TRUE;

		// Sync the wall clock over NTP (its own background task; updates CTimer so
		// kapi_get_datetime / the agenda / log timestamps show real local time).
		new CNTPDaemon (g_szNtpServer, m_pNet);
		m_pLogger->Write (FromKernel, LogNotice, "net: NTP started (%s)", g_szNtpServer);
	}

private:
	CBcm4343Device *m_pWLAN;
	CNetSubSystem  *m_pNet;
	CWPASupplicant *m_pWPA;
	CLogger	       *m_pLogger;
};

// Resolution: cmdline.txt "width="/"height=" override the defaults (1024x768).
// m_Options is constructed before m_Screen/m_2DGraphics, so it is safe to query here.
#define OPT_W(opt)	((opt).GetWidth ()  != 0 ? (int) (opt).GetWidth ()  : SCREEN_WIDTH)
#define OPT_H(opt)	((opt).GetHeight () != 0 ? (int) (opt).GetHeight () : SCREEN_HEIGHT)

CKernel::CKernel (void)
:	m_CPUThrottle (CPUSpeedMaximum),	// 1.5 GHz from boot, not the ~600 MHz idle default
	m_Screen (OPT_W (m_Options), OPT_H (m_Options)),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_2DGraphics (OPT_W (m_Options), OPT_H (m_Options), FALSE),	// VSync off: avoid page-flip present
	m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
	m_USB (&m_Interrupt, &m_Timer, TRUE),			// TRUE: enable plug-and-play
	m_WLAN (WLAN_FIRMWARE_PATH),				// WLAN firmware dir on the SD card
	m_Net (0, 0, 0, 0, DEFAULT_HOSTNAME, NetDeviceTypeWLAN),// DHCP over WLAN
	m_WPASupplicant (WLAN_CONFIG_FILE),			// SSID/PSK supplied by the user
	m_FbConsole (&m_2DGraphics),
	m_bGraphics (FALSE),
	m_bSDMounted (FALSE),
	m_bUSB (FALSE)
{
	m_ActLED.Blink (5);		// visible sign of life before the console is up

	// Publish the chosen resolution so the GUI (compositor, wallpaper, cursor clamp,
	// dialog centering, kapi_screen_size) uses the real size, not the defaults.
	g_nScreenWidth  = OPT_W (m_Options);
	g_nScreenHeight = OPT_H (m_Options);

	// Keyboard layout is NOT applied here: cmdline keymap= is ignored. Userspace owns
	// it -- the autostart `keyb <XX>` tool polls kapi_kbd_ready then kapi_set_keymap.
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

// Load a mouse-cursor bitmap (SimpleOS mousecur.bin): w*h raw bytes, one per pixel,
// 1 = white, 2 = black, anything else = transparent. Returns a GImage (with the
// magenta transparency key) the compositor blits, or 0 if unavailable.
static GImage *LoadCursor (const char *pPath, int nW, int nH)
{
	unsigned nSize = 0;
	u8 *pData = LoadFileFromSD (pPath, &nSize);
	if (pData == 0)
	{
		return 0;
	}
	GImage *pImg = new GImage;
	if (pImg == 0) { delete [] pData; return 0; }
	pImg->SetSize (nW, nH);
	if (!pImg->IsValid ()) { delete [] pData; delete pImg; return 0; }

	for (int i = 0; i < nW * nH; i++)
	{
		u8 code = ((unsigned) i < nSize) ? pData[i] : 0;
		u32 col = (code == 1) ? 0x00FFFFFF
			: (code == 2) ? 0x00000000 : GIMAGE_TRANSPARENT;
		pImg->SetPixel (i % nW, i / nW, col);
	}
	delete [] pData;
	return pImg;
}

static boolean KeyEq (const char *s, const char *e, const char *pLit)
{
	while (s < e && *pLit != '\0' && *s == *pLit) { s++; pLit++; }
	return s == e && *pLit == '\0';
}

// Read SD:system.ini at boot for system-wide settings (currently just verbose=0/1).
static void ReadSystemConfig (void)
{
	unsigned nSize = 0;
	u8 *pData = LoadFileFromSD ("SD:etc/system.ini", &nSize);
	if (pData == 0) return;
	const char *p = (const char *) pData, *pEnd = p + nSize;
	while (p < pEnd)
	{
		const char *ls = p;
		while (p < pEnd && *p != '\n' && *p != '\r') p++;
		const char *le = p;
		while (p < pEnd && (*p == '\n' || *p == '\r')) p++;
		while (ls < le && (*ls == ' ' || *ls == '\t')) ls++;
		if (ls >= le || *ls == '#' || *ls == ';') continue;
		const char *eq = ls;
		while (eq < le && *eq != '=') eq++;
		if (eq >= le) continue;
		const char *ke = eq;
		while (ke > ls && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
		const char *vs = eq + 1;
		while (vs < le && (*vs == ' ' || *vs == '\t')) vs++;
		if (KeyEq (ls, ke, "verbose")) g_bVerbose = (vs < le && *vs == '1');
		else if (KeyEq (ls, ke, "timezone"))
		{
			const char *q = vs; int neg = 0; long v = 0; boolean any = FALSE;
			if (q < le && (*q == '-' || *q == '+')) { neg = (*q == '-'); q++; }
			while (q < le && *q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); q++; any = TRUE; }
			if (any) g_nTimeZoneMin = (int) (neg ? -v : v);
		}
		else if (KeyEq (ls, ke, "ntp"))
		{
			unsigned i = 0;
			for (const char *q = vs; q < le && i < sizeof (g_szNtpServer) - 1; q++)
			{
				if (*q == ' ' || *q == '\t') break;
				g_szNtpServer[i++] = *q;
			}
			if (i > 0) g_szNtpServer[i] = '\0';
		}
	}
	delete [] pData;
}

// Quick existence check (metadata only -- a directory lookup, NOT the file body). Used
// before deferring an app's load so a missing path fails immediately at the caller (the
// heavy ~MB read still happens later on the new task's own thread).
static boolean SdFileExists (const char *pPath)
{
	FILINFO Info;
	return f_stat (pPath, &Info) == FR_OK && !(Info.fattrib & AM_DIR);
}

// Launch an app by folder name: SD:apps/<name>.app/main.elf -> a new EL1 process.
// Safe to call from any task context (cooperative); the new task runs when scheduled.
static boolean LaunchApp (const char *pName, CLogger *pLogger)
{
	CString Path;
	Path.Format ("SD:apps/%s.app/main.elf", pName);

	// Verify the file exists NOW (cheap) so a bad name fails here, not asynchronously.
	if (!SdFileExists ((const char *) Path))
	{
		pLogger->Write (FromKernel, LogError, "launch: not found %s", (const char *) Path);
		return FALSE;
	}

	// Deferred load: hand the PATH to the task; it reads the ELF on its own thread when
	// the scheduler first switches to it, so this caller (often the UI) returns at once.
	CUserProcessTask *pTask = new CUserProcessTask ((const char *) Path, pName, pLogger);
	if (pTask == 0)
	{
		pLogger->Write (FromKernel, LogError, "launch: out of memory for %s", pName);
		return FALSE;
	}
	pLogger->Write (FromKernel, LogNotice, "launch: %s (deferred)", pName);
	return TRUE;
}

// Non-static wrapper exported to the rest of the kernel (kapi_launch). Spawns an
// app by folder name from any task context; logs via the global logger.
boolean LaunchAppByName (const char *pName)
{
	if (pName == 0 || pName[0] == '\0')
	{
		return FALSE;
	}
	return LaunchApp (pName, CLogger::Get ());
}

// Spawn a console process: load the ELF at pElfPath and run it with the given
// stdin/stdout streams + argv. Returns a CProcess handle (poll/free with kapi_wait),
// or 0 on failure. The child address space takes a ref on each stream (the caller
// keeps its own); the handle's done/status are set when the child exits.
CProcess *SpawnProcess (const char *pElfPath, const char *pArgs,
			CStream *pStdin, CStream *pStdout, const char *pCwd,
			unsigned nParentPid)
{
	if (pElfPath == 0)
	{
		return 0;
	}
	if (!SdFileExists (pElfPath))		// missing -> immediate failure (shell prints "not found")
	{
		return 0;
	}

	CProcess *pProc = new CProcess;
	if (pProc == 0)
	{
		return 0;
	}
	pProc->bDone = FALSE;
	pProc->nStatus = 0;

	if (pStdin  != 0) pStdin->AddRef ();		// the child AS will release these
	if (pStdout != 0) pStdout->AddRef ();

	// Deferred load: the task reads pElfPath on its own thread. If the file is missing,
	// it marks pProc done (status -1) so a waiter unblocks -- the failure surfaces async.
	CUserProcessTask *pTask = new CUserProcessTask (pElfPath, pElfPath, CLogger::Get (),
				      pStdin, pStdout, pProc, pArgs, pCwd, nParentPid);
	if (pTask == 0)
	{
		if (pStdin  != 0) pStdin->Release ();
		if (pStdout != 0) pStdout->Release ();
		delete pProc;
		return 0;
	}
	VLOG ("proc", LogNotice, "spawn %s (parent pid %u, deferred)", pElfPath, nParentPid);
	return pProc;
}

// Derive a short task name from an ELF path. "SD:apps/tinypad.app/main.elf" -> the
// parent folder minus ".app" ("tinypad"); "SD:/bin/ls.elf" -> the basename minus
// ".elf" ("ls"). Falls back to "app". Writes up to nCap-1 chars into pOut.
static void NameFromPath (const char *pPath, char *pOut, unsigned nCap)
{
	if (nCap == 0) return;
	unsigned nLen = 0;
	while (pPath[nLen] != '\0') nLen++;

	// Find the last '/' and the segment after it.
	int nSlash = -1;
	for (unsigned i = 0; i < nLen; i++) if (pPath[i] == '/') nSlash = (int) i;
	const char *pSeg = pPath + nSlash + 1;	// basename ("main.elf" or "ls.elf")

	// If the basename is "main.elf", use the parent dir name instead.
	boolean bMain = pSeg[0] == 'm' && pSeg[1] == 'a' && pSeg[2] == 'i' && pSeg[3] == 'n'
			&& pSeg[4] == '.';
	const char *pStart; int nSegLen;
	if (bMain && nSlash > 0)
	{
		int nPrev = -1;
		for (int i = nSlash - 1; i >= 0; i--) if (pPath[i] == '/') { nPrev = i; break; }
		pStart = pPath + nPrev + 1;
		nSegLen = nSlash - (nPrev + 1);
		// Strip a trailing ".app".
		if (nSegLen >= 4 && pStart[nSegLen - 4] == '.' && pStart[nSegLen - 3] == 'a'
		    && pStart[nSegLen - 2] == 'p' && pStart[nSegLen - 1] == 'p')
			nSegLen -= 4;
	}
	else
	{
		pStart = pSeg;
		nSegLen = 0;
		while (pStart[nSegLen] != '\0' && pStart[nSegLen] != '.') nSegLen++;	// drop ".elf"
	}

	if (nSegLen <= 0) { pStart = "app"; nSegLen = 3; }
	unsigned j = 0;
	for (int i = 0; i < nSegLen && j < nCap - 1; i++) pOut[j++] = pStart[i];
	pOut[j] = '\0';
}

// Run an arbitrary ELF by absolute path with an argv string. Fire-and-forget: no
// stdio streams and no CProcess handle (nothing to wait on / free), so the task
// just terminates and the reaper reclaims it. Returns TRUE if the ELF loaded.
boolean ExecPath (const char *pElfPath, const char *pArgs)
{
	if (pElfPath == 0 || pElfPath[0] == '\0')
	{
		return FALSE;
	}
	if (!SdFileExists (pElfPath))		// fail now if missing; body read is still deferred
	{
		return FALSE;
	}
	char Name[40];
	NameFromPath (pElfPath, Name, sizeof (Name));
	// Deferred load (see CUserProcessTask): the task reads pElfPath on its own thread.
	return new CUserProcessTask (pElfPath, Name, CLogger::Get (), 0, 0, 0, pArgs) != 0;
}

// Log the .app subdirectories of /apps (validates FatFs directory enumeration;
// the basis for the shell's app list later).
static void EnumerateApps (CLogger *pLogger)
{
	DIR Dir;
	if (f_opendir (&Dir, "SD:apps") != FR_OK)
	{
		pLogger->Write (FromKernel, LogWarning, "no /apps directory");
		return;
	}
	unsigned nCount = 0;
	for (;;)
	{
		FILINFO Info;
		if (f_readdir (&Dir, &Info) != FR_OK || Info.fname[0] == '\0')
		{
			break;
		}
		if (Info.fattrib & AM_DIR)
		{
			pLogger->Write (FromKernel, LogNotice, "  app dir: %s", Info.fname);
			nCount++;
		}
	}
	f_closedir (&Dir);
	pLogger->Write (FromKernel, LogNotice, "/apps: %u entries", nCount);
}

// Boot the userland: the kernel just launches the init program (PID-1 style, no
// arguments). init reads /etc/autostart and starts everything from there (see
// user/bin/init.c), so all the launch policy lives in userland, not the kernel.
//
// Which ELF to run is the cmdline.txt option "init=" (e.g. init=SD:/bin/init.elf);
// it defaults to SD:bin/init.elf when absent, so existing cards keep booting. This
// lets you swap the init program (a recovery shell, a different launcher) without
// rebuilding the kernel.
void CKernel::StartAutostart (void)
{
	const char *pInit = m_Options.GetAppOptionString ("init", "SD:bin/init.elf");
	if (!ExecPath (pInit, ""))
	{
		m_Logger.Write (FromKernel, LogWarning, "cannot start init '%s'", pInit);
	}
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
		// The CPU was pinned to its max clock at construction (m_CPUThrottle).
		// Report it so a slow boot is easy to spot: it should read ~1500 MHz on a
		// Pi 4, not ~600 MHz.
		m_Logger.Write (FromKernel, LogNotice, "ARM clock: %u MHz (max %u MHz)",
				m_CPUThrottle.GetClockRate () / 1000000,
				m_CPUThrottle.GetMaxClockRate () / 1000000);

		// Firmware-reported board RAM (4096 / 8192 ...): the truth against which the
		// high-zone reclaim below is judged. If this says 8192 but "above 4GB" is 0,
		// the >4GB reclaim fell back (bug); if this says 4096, 0 above 4GB is correct.
		CMachineInfo *pMI = CMachineInfo::Get ();
		m_Logger.Write (FromKernel, LogNotice, "board RAM (firmware): %u MB",
				pMI != 0 ? pMI->GetRAMSize () : 0);

		// High-zone page allocator (app frames). SetupHighMem runs before the logger
		// exists, so report its result here: total high RAM for apps + how much was
		// reclaimed above 4GB from the device tree (0 if the board has none).
		unsigned long nHighMB = (unsigned long)
			((CMemorySystem::GetPagerHighFreeSpace () + (1024*1024 - 1)) / (1024*1024));
		unsigned long n4GMB = (unsigned long)
			((CMemorySystem::GetHighMem4GSize () + (1024*1024 - 1)) / (1024*1024));
		m_Logger.Write (FromKernel, LogNotice,
				"high page zone: %lu MB for apps across %u segment(s), %lu MB above 4GB",
				nHighMB, CMemorySystem::GetHighSegCount (), n4GMB);
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

		// Publish the kapi ABI table (apps call the kernel through it). Must run
		// before any address space is built (each one maps the table).
		KApiTableInit ();
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

			ReadSystemConfig ();		// SD:system.ini -> verbose flag, timezone, etc.
			m_Timer.SetTimeZone (g_nTimeZoneMin);	// local time for the clock/agenda

			// Mouse cursor: SimpleOS mousecur.bin -- 12x19 bytes, 1=white 2=black
			// else transparent. Built into a GImage the compositor blits. (Window
			// chrome is drawn user-side now, so the kernel loads no skins.)
			m_WindowManager.SetCursor (LoadCursor ("SD:skins/mousecur.bin", 12, 19));
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
	m_Logger.Write (FromKernel, LogNotice, "Onyx -- a lean OS on Circle (codename Zircon)");
	m_Logger.Write (FromKernel, LogNotice,
			"Multi-process kernel + GUI, EL1 apps via direct kapi calls");
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
		if (!m_bSDMounted)
		{
			m_Logger.Write (FromKernel, LogError, "no SD card: cannot launch apps");
		}
		else
		{
			EnumerateApps (&m_Logger);
			StartAutostart ();		// spawn the init program (cmdline init=)
		}

		// Start input BEFORE the boot-log pause. The input task pumps USB plug-and-play,
		// so the keyboard/mouse enumerate during these 6 s and the layout that autostart's
		// `keyb` records is installed on the keyboard by the time the desktop appears --
		// instead of racing the old 6 s delay. The mouse/key handlers guard on
		// CWindowManager::Get(), so events arriving before the compositor presents are
		// harmless (nothing is drawn until the compositor takes over the display).
		if (m_bUSB)
		{
			new CInputTask (&m_USB, &m_DeviceNameService,
					m_2DGraphics.GetDisplay (), &m_Logger);
			m_Logger.Write (FromKernel, LogNotice, "input started");
		}

		// Keep the boot log readable for a few seconds, THEN start the compositor
		// (which takes over the display). If the screen goes black only after this,
		// the problem is the framebuffer present, not the boot/loading path.
		m_Logger.Write (FromKernel, LogNotice, "boot OK -- starting graphics in 6 s ...");
		m_Scheduler.MsSleep (6000);

		new CCompositorTask (&m_2DGraphics, &m_WindowManager);
		m_Logger.Write (FromKernel, LogNotice, "compositor started");

		// Reaper: reclaims closed apps (address space + window + task) in its own
		// task context.
		new CReaperTask;
		m_Logger.Write (FromKernel, LogNotice, "reaper started");
	}
	else
	{
		m_Logger.Write (FromKernel, LogWarning, "no framebuffer; idling");
	}

	// Network: bring up WLAN + TCP/IP in the background. Needs the SD card (WLAN
	// firmware + wpa_supplicant.conf live there). Fully non-fatal -- the GUI runs
	// whether or not WiFi associates; apps test NetIsUp() before using sockets.
	if (m_bSDMounted)
	{
		new CNetBringupTask (&m_WLAN, &m_Net, &m_WPASupplicant, &m_Logger);
		m_Logger.Write (FromKernel, LogNotice, "net: bring-up task started");
	}

	// The "main" task has nothing left to do; reaping is the dedicated reaper task's
	// job now. Just idle.
	for (;;)
	{
		m_Scheduler.MsSleep (1000);
	}

	return ShutdownHalt;
}
