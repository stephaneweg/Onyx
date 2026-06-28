//
// ipc.cpp -- CMailbox + the IPC kapis (register_shell / shell_request / mailbox_send /
// mailbox_recv). See kern/ipc.h. The kernel just routes opaque {from_pid,type,bytes}
// messages between per-process mailboxes; the registered shell is a single pid.
//
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

void IpcOnProcessGone (unsigned nPid)
{
	if (nPid != 0 && nPid == g_nShellPid)
	{
		g_nShellPid = 0;		// the shell died -- no router until one re-registers
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
