//
// ipc.h
//
// Minimal inter-process messaging for the activity-shell model. The kernel is a thin
// message router: every process can own a CMailbox (a small ring of messages); a user
// compositor registers as THE shell (kapi_register_shell). Apps post to the shell with
// kapi_shell_request (the kernel stamps the caller's pid); the shell replies / pushes
// async events with kapi_mailbox_send; both drain their mailbox with kapi_mailbox_recv.
//
// The kernel is payload-agnostic: a message is just {from_pid, type, bytes}. The
// meaning of `type` is the user-side shell protocol (user/shell_proto.h). from_pid 0
// means "from the kernel" (e.g. a future process-gone notice).
//
#ifndef _kern_ipc_h
#define _kern_ipc_h

#include <circle/spinlock.h>
#include <circle/types.h>

#define MAILBOX_SLOTS		32	// ring depth (messages buffered before drops)
#define MAILBOX_MSG_MAX		128	// max payload bytes per message

struct TMailMsg
{
	unsigned from_pid;		// sender pid (0 = kernel)
	int	 type;			// user-defined message type
	unsigned len;			// payload length (<= MAILBOX_MSG_MAX)
	u8	 data[MAILBOX_MSG_MAX];
};

class CMailbox
{
public:
	CMailbox (void);

	// Queue a message (copies up to MAILBOX_MSG_MAX bytes). FALSE if the ring is full
	// (message dropped) or the payload is too large.
	boolean Push (unsigned nFromPid, int nType, const void *pData, unsigned nLen);

	// Pop the oldest message into *pOut. FALSE if the mailbox is empty.
	boolean Pop (TMailMsg *pOut);

	boolean Empty (void) const	{ return m_nHead == m_nTail; }

private:
	TMailMsg	  m_Slots[MAILBOX_SLOTS];
	volatile unsigned m_nHead;	// next slot to write
	volatile unsigned m_nTail;	// next slot to read
	CSpinLock	  m_Lock;
};

// Teardown hook: called when a process exits, so the IPC layer can forget it (clears
// the registered shell if it was the shell). Defined in sys/ipc.cpp.
void IpcOnProcessGone (unsigned nPid);

#endif // _kern_ipc_h
