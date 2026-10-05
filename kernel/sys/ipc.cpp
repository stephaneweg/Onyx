//
// ipc.cpp -- CMailbox + the IPC kapis (register_shell / shell_request / mailbox_send /
// mailbox_recv). See kern/ipc.h. The kernel just routes opaque {from_pid,type,bytes}
// messages between per-process mailboxes; the registered shell is a single pid.
//
#include <kern/sound.h>
#include <kern/vfs.h>
#include <kern/ramfs.h>
#include <kern/ipc.h>
#include <kern/addrspace.h>
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/util.h>		// memcpy
#include <kern/uaccess.h>		// the app's pointers (the kapis below)
#include <kern/iowait.h>		// a blocking mailbox_recv sleeps on the I/O generation
#include <kern/kapi_abi.h>		// KAPI_WAIT_FOREVER

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
	IoWake ();				// (a receiver asleep in mailbox_recv: kapi_mailbox_recv)
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
CAddressSpace *IpcFindAS (unsigned nPid) { return FindASByPid (nPid); }

void IpcOnProcessGone (unsigned nPid)
{
	VfsOnProcessGone (nPid);		// a file-system provider that died (kern/vfs.h)
	RamFsOnProcessGone (nPid);		// its RAM: files / folders left open (kern/ramfs.h)
	SoundOnProcessGone (nPid);		// it owned the audio output: silence + free it
	for (unsigned i = 0; nPid != 0 && i < IPC_MAX_SERVICES; i++)
	{
		if (s_Services[i].pid == nPid)
		{
			s_Services[i].pid = 0;	// its services go with it
			s_Services[i].name[0] = '\0';
		}
	}
}

boolean IpcPost (const char *pService, int nType, const void *pData, unsigned nLen)
{
	if (pService == 0 || nLen > MAILBOX_MSG_MAX) return FALSE;
	for (unsigned i = 0; i < IPC_MAX_SERVICES; i++)
	{
		if (s_Services[i].pid == 0 || !NameEq (s_Services[i].name, pService)) continue;
		CAddressSpace *pAS = FindASByPid (s_Services[i].pid);
		CMailbox *pMb = pAS != 0 ? pAS->GetOrCreateMailbox () : 0;
		return pMb != 0 && pMb->Push (0, nType, pData, nLen);
	}
	return FALSE;
}

void IpcNotify (const char *pTitle, const char *pText)
{
	u8 Msg[MAILBOX_MSG_MAX];
	unsigned n = 0;
	for (unsigned k = 0; pTitle && pTitle[k] && n < 80; k++) Msg[n++] = (u8) pTitle[k];
	Msg[n++] = 0;
	for (unsigned k = 0; pText && pText[k] && n < MAILBOX_MSG_MAX - 1; k++) Msg[n++] = (u8) pText[k];
	Msg[n++] = 0;
	IpcPost ("notify", 1, Msg, n);		// NOTIFY_MSG_SHOW
}

// Register the caller as service `pName`. 1 = registered (or already ours), 0 = the
// name is held by another live process / bad name / table full.
extern "C" int kapi_ipc_register (const char *pUserName)
{
	CAddressSpace *pAS = CurAS ();
	CUserStr UserName (pUserName, 256, TRUE);	// (a copy: IPC_NAME_MAX - 1 are kept)
	const char *pName = UserName.Get ();
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
extern "C" int kapi_ipc_lookup (const char *pUserName)
{
	CUserStr UserName (pUserName, 256, TRUE);	// (NameEq compares IPC_NAME_MAX at most)
	const char *pName = UserName.Get ();
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

// The payload, copied into the kernel before the mailbox's lock is taken (fault-safe): FALSE on
// a bad pointer. Longer than a message: left to Push (it refuses it).
static boolean PayloadIn (u8 *pBuf, const void *pIn, unsigned nLen)
{
	return pIn == 0 || nLen == 0 || nLen > MAILBOX_MSG_MAX || UserCopyIn (pBuf, pIn, nLen);
}

extern "C" int kapi_mailbox_send (int nTargetPid, int nType, const void *pUserIn, unsigned nLen)
{
	u8 In[MAILBOX_MSG_MAX];
	if (!PayloadIn (In, pUserIn, nLen))
	{
		return 0;
	}
	const void *pIn = pUserIn != 0 ? (const void *) In : 0;
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
	// (the outputs checked before a message is taken off the mailbox)
	unsigned nMost = nCap < MAILBOX_MSG_MAX ? nCap : MAILBOX_MSG_MAX;
	if (   (pFromPid != 0 && !UserRange (pFromPid, sizeof *pFromPid))
	    || (pType != 0 && !UserRange (pType, sizeof *pType))
	    || (pBuf != 0 && !UserRange (pBuf, nMost)))
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
		u32 nGen = IoGen ();			// (taken before the look: a Push after it changes it)
		if (pMb->Pop (&Msg))
		{
			if (pFromPid != 0) UserPut (pFromPid, (int) Msg.from_pid);
			if (pType    != 0) UserPut (pType, Msg.type);
			unsigned n = Msg.len;
			if (n > nCap) n = nCap;
			if (pBuf != 0 && n != 0 && !UserCopyOut (pBuf, Msg.data, n)) return -1;
			return (int) n;			// payload length delivered
		}
		if (!bBlocking || !CScheduler::IsActive ())
		{
			return -1;			// empty (non-blocking) / no scheduler
		}
		// Asleep until a message lands (CMailbox::Push wakes the I/O waiters). It was a Yield:
		// the receiver stayed ready and took a turn of the processor at every round of the
		// scheduler -- clipd, the one service that waits this way, held most of core 0 for
		// ever (state R, no system call: seen on the Pi, every program four times slower).
		IoWait (nGen, KAPI_WAIT_FOREVER);
	}
}
