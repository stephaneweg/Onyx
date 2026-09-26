//
// vfs.cpp -- user-space file-system providers (see kern/vfs.h), ABI v44.
//
// All of this runs in task context on a cooperative kernel (the kernel is never
// preempted), so the slot tables need no lock: a task only loses the CPU when it
// blocks on an event or yields.
//
#include <kern/vfs.h>
#include <kern/kapi_abi.h>		// struct kapi_dirent, struct kapi_vfs_req
#include <kern/addrspace.h>
#include <kern/applaunch.h>		// ExecPath (auto-start a provider)
#include <kern/ipc.h>			// IpcPidAlive
#include <circle/sched/scheduler.h>
#include <circle/sched/synchronizationevent.h>
#include <circle/timer.h>
#include <circle/util.h>
#include <circle/new.h>

#define MAX_PROVIDERS	4
#define MAX_REQS	16
#define MAX_VFILES	16
#define MAX_VDIRS	8
#define REQ_TIMEOUT_US	(120 * 1000000u)	// a whole FTP download can take a while

struct TProvider
{
	char		       Prefix[16];
	unsigned	       nPid;
	CSynchronizationEvent *pEvent;		// set when a request is queued for it
};

enum { REQ_FREE, REQ_PENDING, REQ_TAKEN, REQ_DONE };

struct TReq
{
	volatile int	       nState;
	unsigned	       nId;
	unsigned	       nProvider;	// provider pid
	int		       nOp;
	char		       Path[VFS_PATH_MAX], Path2[VFS_PATH_MAX];
	long		       a0, a1, a2;
	u8		      *pIn;  unsigned nInLen;	// kernel copy of the request payload
	u8		      *pOut; unsigned nOutLen;	// kernel copy of the answer
	int		       nStatus;
	CSynchronizationEvent *pDone;		// set by vfs_reply
};

struct TVFile { boolean bUsed; char Path[VFS_PATH_MAX]; int nFid; unsigned nSize, nPos; };
struct TVDir  { boolean bUsed; u8 *pList; unsigned nLen, nPos; };

static TProvider s_Prov[MAX_PROVIDERS];
static TReq      s_Req[MAX_REQS];
static TVFile    s_File[MAX_VFILES];
static TVDir     s_Dir[MAX_VDIRS];
static unsigned  s_nNextId = 1;

// Prefixes whose daemon is started on demand.
static const struct { const char *pPrefix; const char *pDaemon; } s_Auto[] = {
	{ "FTP:",  "SD:/bin/ftpfs" },
	{ "FTPS:", "SD:/bin/ftpfs" },
};

static boolean HasPrefix (const char *p, const char *pre)
{
	for (; *pre; p++, pre++)
	{
		char a = *p, b = *pre;
		if (a >= 'a' && a <= 'z') a -= 32;
		if (b >= 'a' && b <= 'z') b -= 32;
		if (a != b) return FALSE;
	}
	return TRUE;
}

static TProvider *ProviderFor (const char *pPath)
{
	TProvider *pBest = 0; unsigned nBest = 0;
	for (unsigned i = 0; i < MAX_PROVIDERS; i++)
	{
		if (s_Prov[i].nPid == 0 || !HasPrefix (pPath, s_Prov[i].Prefix)) continue;
		unsigned n = strlen (s_Prov[i].Prefix);
		if (n > nBest) { pBest = &s_Prov[i]; nBest = n; }		// longest prefix wins
	}
	if (pBest != 0 && !IpcPidAlive (pBest->nPid)) { VfsOnProcessGone (pBest->nPid); return 0; }
	return pBest;
}

boolean VfsHandles (const char *pPath)
{
	if (pPath == 0) return FALSE;
	if (ProviderFor (pPath) != 0) return TRUE;
	for (unsigned i = 0; i < sizeof s_Auto / sizeof s_Auto[0]; i++)
		if (HasPrefix (pPath, s_Auto[i].pPrefix)) return TRUE;
	return FALSE;
}

// The provider for pPath, auto-starting its daemon if needed (waits up to ~5 s).
static TProvider *ProviderStart (const char *pPath)
{
	TProvider *p = ProviderFor (pPath);
	if (p != 0) return p;
	for (unsigned i = 0; i < sizeof s_Auto / sizeof s_Auto[0]; i++)
	{
		if (!HasPrefix (pPath, s_Auto[i].pPrefix)) continue;
		if (!ExecPath (s_Auto[i].pDaemon, "")) return 0;
		for (unsigned t = 0; t < 500 && p == 0; t++)
		{
			CScheduler::Get ()->MsSleep (10);
			p = ProviderFor (pPath);
		}
		return p;
	}
	return 0;
}

static void FreeReq (TReq *r)
{
	delete [] r->pIn;  r->pIn = 0;
	delete [] r->pOut; r->pOut = 0;
	r->nState = REQ_FREE;
}

int VfsCall (int nOp, const char *pPath, const char *pPath2, long a0, long a1, long a2,
	     const void *pIn, unsigned nInLen, void *pOut, unsigned nOutCap, unsigned *pOutLen)
{
	if (pOutLen) *pOutLen = 0;
	TProvider *pProv = ProviderStart (pPath);
	if (pProv == 0) return -100;
	TReq *r = 0;
	for (unsigned i = 0; i < MAX_REQS && r == 0; i++) if (s_Req[i].nState == REQ_FREE) r = &s_Req[i];
	if (r == 0) return -101;					// too many requests in flight

	r->nId = s_nNextId++; if (s_nNextId == 0) s_nNextId = 1;
	r->nProvider = pProv->nPid; r->nOp = nOp;
	strncpy (r->Path, pPath ? pPath : "", VFS_PATH_MAX - 1); r->Path[VFS_PATH_MAX - 1] = '\0';
	strncpy (r->Path2, pPath2 ? pPath2 : "", VFS_PATH_MAX - 1); r->Path2[VFS_PATH_MAX - 1] = '\0';
	r->a0 = a0; r->a1 = a1; r->a2 = a2;
	r->pIn = 0; r->nInLen = 0; r->pOut = 0; r->nOutLen = 0; r->nStatus = -100;
	if (pIn != 0 && nInLen > 0)
	{
		r->pIn = new u8[nInLen];
		if (r->pIn == 0) return -102;
		memcpy (r->pIn, pIn, nInLen);				// the caller's memory is mapped here
		r->nInLen = nInLen;
	}
	if (r->pDone == 0) r->pDone = new CSynchronizationEvent;
	r->pDone->Clear ();
	r->nState = REQ_PENDING;
	pProv->pEvent->Set ();						// wake the provider

	unsigned nStart = CTimer::Get ()->GetClockTicks ();
	while (r->nState != REQ_DONE)
	{
		r->pDone->WaitWithTimeout (200000);			// 200 ms, then re-check
		r->pDone->Clear ();
		if (r->nState == REQ_DONE) break;
		if (!IpcPidAlive (r->nProvider) || CTimer::Get ()->GetClockTicks () - nStart > REQ_TIMEOUT_US)
		{
			FreeReq (r);
			return -100;
		}
	}
	int nStatus = r->nStatus;
	if (r->pOut != 0 && pOut != 0)
		memcpy (pOut, r->pOut, r->nOutLen < nOutCap ? r->nOutLen : nOutCap);
	if (pOutLen) *pOutLen = r->nOutLen;
	FreeReq (r);
	return nStatus;
}

void VfsOnProcessGone (unsigned nPid)
{
	for (unsigned i = 0; i < MAX_PROVIDERS; i++)
		if (s_Prov[i].nPid == nPid) s_Prov[i].nPid = 0;
	for (unsigned i = 0; i < MAX_REQS; i++)				// wake its waiters (they fail)
		if (s_Req[i].nState != REQ_FREE && s_Req[i].nProvider == nPid && s_Req[i].pDone)
			s_Req[i].pDone->Set ();
}

// ---- provider kapis (ABI v44) ------------------------------------------------------------
static unsigned MyPid (void)
{
	CAddressSpace *pAS = (CAddressSpace *) CScheduler::Get ()->GetCurrentTask ()->GetUserData (TASK_USER_DATA_USER);
	return pAS != 0 ? pAS->GetPid () : 0;
}

extern "C" int kapi_vfs_register (const char *pPrefix)
{
	unsigned nPid = MyPid ();
	if (nPid == 0 || pPrefix == 0 || pPrefix[0] == '\0') return 0;
	TProvider *pFree = 0;
	for (unsigned i = 0; i < MAX_PROVIDERS; i++)
	{
		if (s_Prov[i].nPid != 0 && HasPrefix (s_Prov[i].Prefix, pPrefix) && strlen (s_Prov[i].Prefix) == strlen (pPrefix))
		{
			if (s_Prov[i].nPid == nPid) return 1;
			if (IpcPidAlive (s_Prov[i].nPid)) return 0;		// taken
			s_Prov[i].nPid = 0;
		}
		if (s_Prov[i].nPid == 0 && pFree == 0) pFree = &s_Prov[i];
	}
	if (pFree == 0) return 0;
	strncpy (pFree->Prefix, pPrefix, sizeof pFree->Prefix - 1);
	pFree->Prefix[sizeof pFree->Prefix - 1] = '\0';
	if (pFree->pEvent == 0) pFree->pEvent = new CSynchronizationEvent;
	pFree->nPid = nPid;
	return 1;
}

// Next request for the calling provider (any of its prefixes): fills *pReq, returns 1;
// 0 if none -- blocking waits up to ~0.5 s for one first (so the provider can also poll
// its mailbox between requests).
extern "C" int kapi_vfs_next (struct kapi_vfs_req *pReq, int bBlocking)
{
	unsigned nPid = MyPid ();
	CSynchronizationEvent *pEv = 0;
	for (unsigned i = 0; i < MAX_PROVIDERS; i++) if (s_Prov[i].nPid == nPid) { pEv = s_Prov[i].pEvent; break; }
	if (pEv == 0 || pReq == 0) return 0;
	for (int nTry = 0; ; nTry++)
	{
		pEv->Clear ();
		for (unsigned i = 0; i < MAX_REQS; i++)
		{
			TReq *r = &s_Req[i];
			if (r->nState != REQ_PENDING || r->nProvider != nPid) continue;
			r->nState = REQ_TAKEN;
			pReq->id = r->nId; pReq->op = r->nOp;
			memcpy (pReq->path, r->Path, sizeof pReq->path);
			memcpy (pReq->path2, r->Path2, sizeof pReq->path2);
			pReq->a0 = r->a0; pReq->a1 = r->a1; pReq->a2 = r->a2;
			pReq->in_len = r->nInLen;
			return 1;
		}
		if (!bBlocking || nTry > 0) return 0;
		pEv->WaitWithTimeout (500000);
	}
}

static TReq *ReqById (unsigned nId)
{
	for (unsigned i = 0; i < MAX_REQS; i++)
		if (s_Req[i].nState == REQ_TAKEN && s_Req[i].nId == nId) return &s_Req[i];
	return 0;
}

// Copy (part of) a request's payload (SAVE data) into the provider.
extern "C" int kapi_vfs_req_data (unsigned nId, void *pBuf, unsigned nCap, unsigned nOffset)
{
	TReq *r = ReqById (nId);
	if (r == 0 || pBuf == 0 || nOffset >= r->nInLen) return 0;
	unsigned n = r->nInLen - nOffset; if (n > nCap) n = nCap;
	memcpy (pBuf, r->pIn + nOffset, n);
	return (int) n;
}

// Answer request nId: status + data (copied), then wake the caller.
extern "C" int kapi_vfs_reply (unsigned nId, int nStatus, const void *pData, unsigned nLen)
{
	TReq *r = ReqById (nId);
	if (r == 0) return 0;
	if (pData != 0 && nLen > 0)
	{
		r->pOut = new u8[nLen];
		if (r->pOut != 0) { memcpy (r->pOut, pData, nLen); r->nOutLen = nLen; }
	}
	r->nStatus = nStatus;
	r->nState = REQ_DONE;
	if (r->pDone) r->pDone->Set ();
	return 1;
}

// ---- file / dir handles --------------------------------------------------------------
boolean VfsIsFile (void *h) { return h >= (void *) &s_File[0] && h < (void *) &s_File[MAX_VFILES]; }
boolean VfsIsDir (void *h)  { return h >= (void *) &s_Dir[0]  && h < (void *) &s_Dir[MAX_VDIRS]; }

void *VfsOpen (const char *pPath)
{
	TVFile *f = 0;
	for (unsigned i = 0; i < MAX_VFILES && f == 0; i++) if (!s_File[i].bUsed) f = &s_File[i];
	if (f == 0) return 0;
	u32 nSize = 0; unsigned nLen = 0;
	int fid = VfsCall (VFS_OP_OPEN, pPath, 0, 0, 0, 0, 0, 0, &nSize, sizeof nSize, &nLen);
	if (fid < 0) return 0;
	f->bUsed = TRUE; f->nFid = fid; f->nSize = nSize; f->nPos = 0;
	strncpy (f->Path, pPath, VFS_PATH_MAX - 1); f->Path[VFS_PATH_MAX - 1] = '\0';
	return f;
}

int VfsRead (void *h, void *pBuf, unsigned nLen)
{
	TVFile *f = (TVFile *) h;
	if (!f->bUsed) return -1;
	unsigned nDone = 0;
	while (nDone < nLen && f->nPos < f->nSize)
	{
		unsigned n = nLen - nDone; if (n > VFS_READ_MAX) n = VFS_READ_MAX;
		unsigned nGot = 0;
		int r = VfsCall (VFS_OP_READ, f->Path, 0, f->nFid, f->nPos, n, 0, 0, (u8 *) pBuf + nDone, n, &nGot);
		if (r <= 0) break;
		nDone += (unsigned) r; f->nPos += (unsigned) r;
	}
	return (int) nDone;
}

unsigned VfsSize (void *h) { return ((TVFile *) h)->bUsed ? ((TVFile *) h)->nSize : 0; }

void VfsClose (void *h)
{
	TVFile *f = (TVFile *) h;
	if (!f->bUsed) return;
	VfsCall (VFS_OP_CLOSE, f->Path, 0, f->nFid, 0, 0, 0, 0, 0, 0, 0);
	f->bUsed = FALSE;
}

void *VfsOpenDir (const char *pPath)
{
	TVDir *d = 0;
	for (unsigned i = 0; i < MAX_VDIRS && d == 0; i++) if (!s_Dir[i].bUsed) d = &s_Dir[i];
	if (d == 0) return 0;
	static u8 Buf[256 * 1024];					// one listing at a time
	unsigned nLen = 0;
	int n = VfsCall (VFS_OP_LIST, pPath, 0, 0, 0, 0, 0, 0, Buf, sizeof Buf, &nLen);
	if (n < 0) return 0;
	if (nLen > sizeof Buf) nLen = sizeof Buf;
	d->pList = new u8[nLen + 1];
	if (d->pList == 0) return 0;
	memcpy (d->pList, Buf, nLen);
	d->nLen = nLen; d->nPos = 0; d->bUsed = TRUE;
	return d;
}

int VfsReadDir (void *h, struct kapi_dirent *pEnt)
{
	TVDir *d = (TVDir *) h;
	if (!d->bUsed || d->nPos + 6 > d->nLen) return 0;
	const u8 *p = d->pList + d->nPos;
	pEnt->size = (unsigned) p[0] | ((unsigned) p[1] << 8) | ((unsigned) p[2] << 16) | ((unsigned) p[3] << 24);
	pEnt->is_dir = p[4] ? 1 : 0;
	unsigned i = 0, k = 5;
	while (d->nPos + k < d->nLen && p[k] != '\0') { if (i < sizeof pEnt->name - 1) pEnt->name[i++] = (char) p[k]; k++; }
	pEnt->name[i] = '\0';
	d->nPos += k + 1;
	return 1;
}

void VfsCloseDir (void *h)
{
	TVDir *d = (TVDir *) h;
	delete [] d->pList; d->pList = 0;
	d->bUsed = FALSE;
}
