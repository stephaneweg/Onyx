//
// ipc.cpp -- CMailbox + the IPC kapis (register_shell / shell_request / mailbox_send /
// mailbox_recv). See kern/ipc.h. The kernel just routes opaque {from_pid,type,bytes}
// messages between per-process mailboxes; the registered shell is a single pid.
//
#include <kern/vfs.h>
#include <kern/ipc.h>
#include <kern/addrspace.h>
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/util.h>		// memcpy

// ---- CMailbox --------------------------------------------------------------
CMailbox::CMailbox (void)
:	m_nHead (0), m_nTail (0)
{
}

boolean CMailbox::Push (unsigned nFromPid, int nType, const void *pData, unsigned nLen)
{
	if (nLen > MAILBOX_MSG_MAX)
	{
		return FALSE;
	}
	m_Lock.Acquire ();
	unsigned nNext = (m_nHead + 1) % MAILBOX_SLOTS;
	if (nNext == m_nTail)			// full
	{
		m_Lock.Release ();
		return FALSE;
	}
	TMailMsg *pSlot = &m_Slots[m_nHead];
	pSlot->from_pid = nFromPid;
	pSlot->type     = nType;
	pSlot->len      = nLen;
	if (pData != 0 && nLen != 0)
	{
		memcpy (pSlot->data, pData, nLen);
	}
	m_nHead = nNext;
	m_Lock.Release ();
	return TRUE;
}

boolean CMailbox::Pop (TMailMsg *pOut)
{
	m_Lock.Acquire ();
	if (m_nHead == m_nTail)			// empty
	{
		m_Lock.Release ();
		return FALSE;
	}
	*pOut = m_Slots[m_nTail];
	m_nTail = (m_nTail + 1) % MAILBOX_SLOTS;
	m_Lock.Release ();
	return TRUE;
}

// ---- routing helpers -------------------------------------------------------
static unsigned g_nShellPid = 0;		// the registered shell, or 0

// Named services (ABI v40): a process registers under a short name ("notify", ...);
// clients look the pid up and talk to it with mailbox_send / mailbox_recv.
#define IPC_MAX_SERVICES	16
#define IPC_NAME_MAX		24
static struct { char name[IPC_NAME_MAX]; unsigned pid; } s_Services[IPC_MAX_SERVICES];

static boolean NameEq (const char *a, const char *b)
{
	for (unsigned i = 0; i < IPC_NAME_MAX; i++)
	{
		if (a[i] != b[i]) return FALSE;
		if (a[i] == '\0') return TRUE;
	}
	return TRUE;
}

static CAddressSpace *CurAS (void)
{
	if (!CScheduler::IsActive ())
	{
		return 0;
	}
	CTask *pTask = CScheduler::Get ()->GetCurrentTask ();
	return (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
}

struct FindCtx { unsigned nPid; CAddressSpace *pFound; };

static boolean FindASByPidCb (CTask *pTask, const char *, TTaskState State,
			      TTaskFlags, void *pParam)
{
	if (State == TaskStateTerminated) return TRUE;
	FindCtx *c = (FindCtx *) pParam;
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	if (pAS != 0 && pAS->GetPid () == c->nPid) c->pFound = pAS;
	return TRUE;
}

static CAddressSpace *FindASByPid (unsigned nPid)
{
	if (nPid == 0 || !CScheduler::IsActive ())
	{
		return 0;
	}
	FindCtx Ctx = { nPid, 0 };
	CScheduler::Get ()->EnumerateTasks (FindASByPidCb, &Ctx);
	return Ctx.pFound;
}

boolean IpcPidAlive (unsigned nPid) { return FindASByPid (nPid) != 0; }

void IpcOnProcessGone (unsigned nPid)
{
	VfsOnProcessGone (nPid);		// a file-system provider that died (kern/vfs.h)
	if (nPid != 0 && nPid == g_nShellPid)
	{
		g_nShellPid = 0;		// the shell died -- no router until one re-registers
	}
	for (unsigned i = 0; nPid != 0 && i < IPC_MAX_SERVICES; i++)
	{
		if (s_Services[i].pid == nPid)
		{
			s_Services[i].pid = 0;	// its services go with it
			s_Services[i].name[0] = '\0';
		}
	}
}

void IpcNotify (const char *pTitle, const char *pText)
{
	for (unsigned i = 0; i < IPC_MAX_SERVICES; i++)
	{
		if (s_Services[i].pid == 0 || !NameEq (s_Services[i].name, "notify")) continue;
		CAddressSpace *pAS = FindASByPid (s_Services[i].pid);
		CMailbox *pMb = pAS != 0 ? pAS->GetOrCreateMailbox () : 0;
		if (pMb == 0) return;
		u8 Msg[MAILBOX_MSG_MAX];
		unsigned n = 0;
		for (unsigned k = 0; pTitle && pTitle[k] && n < 80; k++) Msg[n++] = (u8) pTitle[k];
		Msg[n++] = 0;
		for (unsigned k = 0; pText && pText[k] && n < MAILBOX_MSG_MAX - 1; k++) Msg[n++] = (u8) pText[k];
		Msg[n++] = 0;
		pMb->Push (0, 1, Msg, n);		// NOTIFY_MSG_SHOW
		return;
	}
}

// ---- kapis -----------------------------------------------------------------
extern "C" int kapi_register_shell (void)
{
	CAddressSpace *pAS = CurAS ();
	if (pAS == 0)
	{
		return 0;
	}
	g_nShellPid = pAS->GetPid ();
	pAS->GetOrCreateMailbox ();		// make sure the main mailbox exists
	return 1;
}

// Register the caller as service `pName`. 1 = registered (or already ours), 0 = the
// name is held by another live process / bad name / table full.
extern "C" int kapi_ipc_register (const char *pName)
{
	CAddressSpace *pAS = CurAS ();
	if (pAS == 0 || pName == 0 || pName[0] == '\0')
	{
		return 0;
	}
	char Name[IPC_NAME_MAX];
	unsigned n = 0;
	for (; pName[n] != '\0' && n < IPC_NAME_MAX - 1; n++) Name[n] = pName[n];
	Name[n] = '\0';
	unsigned nMe = pAS->GetPid ();
	int nFree = -1;
	for (unsigned i = 0; i < IPC_MAX_SERVICES; i++)
	{
		if (s_Services[i].pid != 0 && NameEq (s_Services[i].name, Name))
		{
			if (s_Services[i].pid == nMe) return 1;
			if (FindASByPid (s_Services[i].pid) != 0) return 0;	// taken by a live process
			s_Services[i].pid = 0;					// stale entry
		}
		if (s_Services[i].pid == 0 && nFree < 0) nFree = (int) i;
	}
	if (nFree < 0)
	{
		return 0;
	}
	for (unsigned i = 0; i <= n; i++) s_Services[nFree].name[i] = Name[i];
	s_Services[nFree].pid = nMe;
	pAS->GetOrCreateMailbox ();
	return 1;
}

// pid of the process registered as `pName`, or 0.
extern "C" int kapi_ipc_lookup (const char *pName)
{
	if (pName == 0)
	{
		return 0;
	}
	for (unsigned i = 0; i < IPC_MAX_SERVICES; i++)
	{
		if (s_Services[i].pid != 0 && NameEq (s_Services[i].name, pName))
		{
			if (FindASByPid (s_Services[i].pid) != 0) return (int) s_Services[i].pid;
			s_Services[i].pid = 0;
			s_Services[i].name[0] = '\0';
		}
	}
	return 0;
}

extern "C" int kapi_shell_request (int nType, const void *pIn, unsigned nLen)
{
	if (g_nShellPid == 0)
	{
		return -1;			// no shell registered
	}
	CAddressSpace *pShell = FindASByPid (g_nShellPid);
	if (pShell == 0)
	{
		g_nShellPid = 0;		// stale -- shell vanished
		return -1;
	}
	CMailbox *pMb = pShell->GetOrCreateMailbox ();
	if (pMb == 0)
	{
		return -1;
	}
	CAddressSpace *pMe = CurAS ();
	unsigned nFrom = (pMe != 0) ? pMe->GetPid () : 0;
	return pMb->Push (nFrom, nType, pIn, nLen) ? 1 : 0;	// 0 = mailbox full
}

extern "C" int kapi_mailbox_send (int nTargetPid, int nType, const void *pIn, unsigned nLen)
{
	CAddressSpace *pTarget = FindASByPid ((unsigned) nTargetPid);
	if (pTarget == 0)
	{
		return 0;			// no such process
	}
	CMailbox *pMb = pTarget->GetOrCreateMailbox ();
	if (pMb == 0)
	{
		return 0;
	}
	CAddressSpace *pMe = CurAS ();
	unsigned nFrom = (pMe != 0) ? pMe->GetPid () : 0;
	return pMb->Push (nFrom, nType, pIn, nLen) ? 1 : 0;
}

extern "C" int kapi_mailbox_recv (int *pFromPid, int *pType, void *pBuf, unsigned nCap, int bBlocking)
{
	CAddressSpace *pAS = CurAS ();
	if (pAS == 0)
	{
		return -1;
	}
	CMailbox *pMb = pAS->GetOrCreateMailbox ();
	if (pMb == 0)
	{
		return -1;
	}
	TMailMsg Msg;
	for (;;)
	{
		if (pMb->Pop (&Msg))
		{
			if (pFromPid != 0) *pFromPid = (int) Msg.from_pid;
			if (pType    != 0) *pType    = Msg.type;
			unsigned n = Msg.len;
			if (n > nCap) n = nCap;
			if (pBuf != 0 && n != 0) memcpy (pBuf, Msg.data, n);
			return (int) n;			// payload length delivered
		}
		if (!bBlocking || !CScheduler::IsActive ())
		{
			return -1;			// empty (non-blocking) / no scheduler
		}
		CScheduler::Get ()->Yield ();		// block: hand the CPU over until a message lands
	}
}
