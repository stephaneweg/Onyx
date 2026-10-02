//
// procx.cpp -- a process's POSIX side: spawn_ex / proc_wait, get_argv / get_env, getpid,
// clock_info, sleep_us (kapi v75 slots 222..228, docs/POSIX-PLAN.md §3.2, docs/02 §8 "v75: files
// and processes"). The model is in kern/procx.h.
//
// Owner: WP-FILE/PROC.
//
// ---------------------------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// ---------------------------------------------------------------------------------------------
//
#include <kern/procx.h>
#include <kern/ofile.h>			// ResolvePath, CurCwd, CHandlePin
#include <kern/addrspace.h>
#include <kern/applaunch.h>		// SpawnProcess
#include <kern/stream.h>		// CProcess
#include <kern/handle.h>
#include <kern/lsock.h>			// the handles given to a child (v76)
#include <kern/net.h>			// NetCurrentPid
#include <kern/iowait.h>
#include <kern/uaccess.h>
#include <kern/kapi_abi.h>
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/util.h>
#include <circle/new.h>
#include <fatfs/ff.h>

struct TInherit					// (v76) a handle spawn_ex2 gave the child
{
	TIpcXfer  X;				// the reference (until the child asks: get_handles)
	int	  nFd;				// the child's descriptor
	long long h;				// the child's handle (once given; -1: dropped)
};

struct TProcInfo
{
	char	 *pArgv;			// "a\0b\0\0"
	unsigned  nArgv;			// its bytes, the final NUL included
	char	 *pEnv;
	unsigned  nEnv;
	TInherit *pInherit;			// (v76) spawn_ex2's handles, 0: none
	unsigned  nInherit;
	boolean	  bGiven;			// get_handles put them in the child's table
};

#define ENV_FILE	"SD:/etc/environment"
#define ARGS_OLD	1024			// CAddressSpace::m_Args (kapi_get_args)

static const char s_BuiltinEnv[] = "HOME=SD:/home\0PATH=SD:/bin\0TMPDIR=RAM:/tmp\0LANG=C.UTF-8\0";
static char	*s_pDefEnv;			// the system default (ProcInfoBootInit), 0: the built-in one
static unsigned	 s_nDefEnv;

static CAddressSpace *CurrentAS (void)
{
	if (!CScheduler::IsActive ()) return 0;
	CTask *pTask = CScheduler::Get ()->GetCurrentTask ();
	return pTask != 0 ? (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER) : 0;
}

// ---- blocks ---------------------------------------------------------------------------------------

static char *Dup (const char *p, unsigned n)
{
	char *q = new char[n > 0 ? n : 1];
	if (q != 0 && n > 0) memcpy (q, p, n);
	return q;
}

// A block's size, its final empty string included ("a\0b\0\0" -> 5; "\0" -> 1)
static unsigned BlockLen (const char *p)
{
	unsigned n = 0;
	while (p[n] != '\0') { while (p[n] != '\0') n++; n++; }
	return n + 1;
}

// argv = pPath, then pArgs split as a shell does (blanks; "double quotes" group, removed)
static char *ArgvFrom (const char *pPath, const char *pArgs, unsigned *pLen)
{
	unsigned nPath = strlen (pPath), nArgs = pArgs != 0 ? strlen (pArgs) : 0;
	char *p = new char[nPath + nArgs + 3];
	if (p == 0) return 0;
	memcpy (p, pPath, nPath + 1);
	unsigned o = nPath + 1;
	for (unsigned i = 0; i < nArgs; )
	{
		while (i < nArgs && (pArgs[i] == ' ' || pArgs[i] == '\t')) i++;
		if (i >= nArgs) break;
		boolean bQuote = FALSE, bAny = FALSE;
		while (i < nArgs && (bQuote || (pArgs[i] != ' ' && pArgs[i] != '\t')))
		{
			if (pArgs[i] == '"') { bQuote = !bQuote; bAny = TRUE; i++; continue; }
			p[o++] = pArgs[i++];
			bAny = TRUE;
		}
		if (bAny) p[o++] = '\0';
	}
	p[o++] = '\0';
	*pLen = o;
	return p;
}

// The old get_args string of a spawn_ex child: argv[1..] joined with blanks, an argument with a
// blank (or empty) in double quotes; cut at ARGS_OLD - 1.
static void ArgsFrom (const char *pArgv, char *pOut, unsigned nCap)
{
	unsigned o = 0;
	const char *p = pArgv + strlen (pArgv) + 1;			// (after argv[0])
	for (; *p != '\0'; p += strlen (p) + 1)
	{
		boolean bQuote = FALSE;
		for (const char *q = p; *q != '\0'; q++) if (*q == ' ' || *q == '\t') bQuote = TRUE;
		if (o > 0 && o < nCap - 1) pOut[o++] = ' ';
		if (bQuote && o < nCap - 1) pOut[o++] = '"';
		for (const char *q = p; *q != '\0' && o < nCap - 1; q++) pOut[o++] = *q;
		if (bQuote && o < nCap - 1) pOut[o++] = '"';
	}
	pOut[o] = '\0';
}

// An app's block -> a kernel copy (*ppOut, *pLen) / -errno (-EINVAL: over PROCX_BLOCK_MAX)
static int CopyInBlock (const char *pUser, char **ppOut, unsigned *pLen)
{
	u64 nAvail = UserRangeAvail (pUser);
	u64 nOff = 0;
	for (;;)
	{
		if (nOff >= PROCX_BLOCK_MAX) return -KAPI_EINVAL;
		if (nOff >= nAvail) return -KAPI_EFAULT;
		u64 nMax = PROCX_BLOCK_MAX - nOff;
		boolean bAvail = nAvail - nOff < nMax;
		if (bAvail) nMax = nAvail - nOff;
		long n = UAccessStrLen (pUser + nOff, nMax);
		if (n < 0) return -KAPI_EFAULT;
		if ((u64) n >= nMax) return bAvail ? -KAPI_EFAULT : -KAPI_EINVAL;
		nOff += (u64) n + 1;
		if (n == 0) break;
	}
	char *p = new char[nOff];
	if (p == 0) return -KAPI_ENOMEM;
	if (!UserCopyIn (p, pUser, nOff)) { delete [] p; return -KAPI_EFAULT; }
	p[nOff - 1] = '\0';					// (changed meanwhile: still a block)
	if (nOff >= 2) p[nOff - 2] = '\0';
	*ppOut = p;
	*pLen = BlockLen (p);					// (<= nOff)
	return 0;
}

static TProcInfo *InfoMake (char *pArgv, unsigned nArgv, char *pEnv, unsigned nEnv)
{
	TProcInfo *p = pArgv != 0 && pEnv != 0 ? new TProcInfo : 0;
	if (p == 0)
	{
		delete [] pArgv; delete [] pEnv;
		return 0;
	}
	p->pArgv = pArgv; p->nArgv = nArgv;
	p->pEnv = pEnv; p->nEnv = nEnv;
	p->pInherit = 0; p->nInherit = 0; p->bGiven = FALSE;
	return p;
}

// The environment a child inherits: the caller's initial one, else the system default
static char *EnvCopy (boolean bInherit, unsigned *pLen)
{
	CAddressSpace *pAS = bInherit ? CurrentAS () : 0;
	TProcInfo *pMe = pAS != 0 ? pAS->GetProcInfo () : 0;
	if (pMe != 0)
	{
		*pLen = pMe->nEnv;
		return Dup (pMe->pEnv, pMe->nEnv);
	}
	const char *p = s_pDefEnv != 0 ? s_pDefEnv : s_BuiltinEnv;
	*pLen = s_pDefEnv != 0 ? s_nDefEnv : sizeof s_BuiltinEnv;
	return Dup (p, *pLen);
}

// ---- the hooks (kern/procx.h) ----------------------------------------------------------------------

TProcInfo *ProcInfoNew (const char *pPath, const char *pArgs, boolean bInherit)
{
	unsigned nArgv = 0, nEnv = 0;
	char *pArgv = ArgvFrom (pPath != 0 ? pPath : "", pArgs, &nArgv);
	char *pEnv = EnvCopy (bInherit, &nEnv);
	return InfoMake (pArgv, nArgv, pEnv, nEnv);
}

void ProcInfoFree (TProcInfo *pInfo)
{
	if (pInfo == 0) return;
	delete [] pInfo->pArgv;
	delete [] pInfo->pEnv;
	for (unsigned i = 0; i < pInfo->nInherit; i++)		// (v76: never asked for: closed)
	{
		IpcXferDrop (&pInfo->pInherit[i].X, TRUE);	// (TRUE: from a teardown too)
	}
	delete [] pInfo->pInherit;
	delete pInfo;
}

void ProcInfoInstall (CAddressSpace *pAS, TProcInfo *pInfo)
{
	if (pAS == 0) { ProcInfoFree (pInfo); return; }
	if (pAS->GetProcInfo () != 0) ProcInfoFree (pAS->GetProcInfo ());
	pAS->SetProcInfo (pInfo);
	CProcess *pProc = pAS->GetProcess ();
	if (pProc != 0)
	{
		pProc->nPid = pAS->GetPid ();				// (spawn_ex waits for it)
		IoWake ();
	}
}

void ProcInfoTeardown (CAddressSpace *pAS)
{
	if (pAS == 0) return;
	int nReason = pAS->GetTermReason ();
	if (nReason != KAPI_PROC_EXITED) pAS->SetExitStatus (pAS->GetTermCode ());	// (-11, -9)
	CProcess *pProc = pAS->GetProcess ();
	if (pProc != 0)
	{
		pProc->nReason = nReason;
		pProc->nPid = pAS->GetPid ();
	}
	ProcInfoFree (pAS->GetProcInfo ());
	pAS->SetProcInfo (0);
	IoWake ();				// (proc_wait: the record is marked done right after this)
}

void ProcInfoBootInit (void)
{
	FIL File;
	if (f_open (&File, ENV_FILE, FA_READ) != FR_OK) return;	// (the built-in default)
	unsigned nSize = (unsigned) f_size (&File);
	if (nSize > PROCX_BLOCK_MAX - 2) nSize = PROCX_BLOCK_MAX - 2;
	char *pText = new char[nSize + 1];
	char *pEnv = new char[nSize + 2];
	UINT nRead = 0;
	if (pText == 0 || pEnv == 0 || f_read (&File, pText, nSize, &nRead) != FR_OK)
	{
		f_close (&File);
		delete [] pText; delete [] pEnv;
		return;
	}
	f_close (&File);
	pText[nRead] = '\0';
	unsigned o = 0, nVars = 0;
	for (char *p = pText; *p != '\0'; )
	{
		char *pLine = p;
		while (*p != '\0' && *p != '\n') p++;
		char *pEnd = p;
		if (*p == '\n') p++;
		while (pLine < pEnd && (*pLine == ' ' || *pLine == '\t')) pLine++;
		while (pEnd > pLine && (pEnd[-1] == '\r' || pEnd[-1] == ' ' || pEnd[-1] == '\t')) pEnd--;
		if (pLine == pEnd || *pLine == '#' || *pLine == '=') continue;	// (blank, a comment)
		boolean bEq = FALSE;
		for (char *q = pLine; q < pEnd; q++) if (*q == '=') bEq = TRUE;
		if (!bEq) continue;
		memcpy (pEnv + o, pLine, pEnd - pLine);
		o += pEnd - pLine;
		pEnv[o++] = '\0';
		nVars++;
	}
	pEnv[o++] = '\0';
	delete [] pText;
	delete [] s_pDefEnv;
	s_pDefEnv = pEnv;
	s_nDefEnv = o;
	CLogger::Get ()->Write ("proc", LogNotice, "%s: %u variables", ENV_FILE, nVars);
}

// ---- the kapis ----------------------------------------------------------------------------------

extern "C" {

// spawn_ex / spawn_ex2: pInherit (nInherit, referenced) handed to the child's record, or dropped.
static long long SpawnCommon (const struct kapi_spawn_attr *pUserAttr, TInherit *pInherit, unsigned nInherit)
{
	struct TDrop					// (until the child's record holds them)
	{
		TInherit *p; unsigned n;
		~TDrop (void) { for (unsigned i = 0; i < n; i++) IpcXferDrop (&p[i].X, FALSE); delete [] p; }
	} Drop = { pInherit, nInherit };
	struct kapi_spawn_attr A;
	if (pUserAttr == 0 || !UserGet (&A, pUserAttr)) return -KAPI_EFAULT;
	CUserStr Path (A.path, UPATH_MAX);
	if (!Path.OK ()) return UserPathErr (A.path);
	if (Path.Length () == 0) return -KAPI_ENOENT;
	char abs[300], cwd[300];
	ResolvePath (Path.Get (), abs, sizeof abs);
	if (A.cwd != 0)
	{
		CUserStr Cwd (A.cwd, UPATH_MAX);
		if (!Cwd.OK ()) return -KAPI_EFAULT;
		ResolvePath (Cwd.Get (), cwd, sizeof cwd);
	}
	else
	{
		strncpy (cwd, CurCwd (), sizeof cwd - 1);
		cwd[sizeof cwd - 1] = '\0';
	}
	FILINFO Info;						// (the program: there, a file)
	FRESULT r = f_stat (abs, &Info);
	if (r == FR_NO_FILE || r == FR_NO_PATH || r == FR_INVALID_NAME || r == FR_INVALID_DRIVE) return -KAPI_ENOENT;
	if (r != FR_OK) return -KAPI_EIO;
	if (Info.fattrib & AM_DIR) return -KAPI_EACCES;

	char *pArgv = 0, *pEnv = 0;
	unsigned nArgv = 0, nEnv = 0;
	int nErr = 0;
	if (A.argv != 0) nErr = CopyInBlock (A.argv, &pArgv, &nArgv);
	if (nErr == 0 && (pArgv == 0 || nArgv <= 1))		// (none: argv[0] = the path)
	{
		delete [] pArgv;
		pArgv = ArgvFrom (abs, 0, &nArgv);
	}
	if (nErr == 0 && A.envp != 0) nErr = CopyInBlock (A.envp, &pEnv, &nEnv);
	else if (nErr == 0) pEnv = EnvCopy (TRUE, &nEnv);
	if (nErr != 0) { delete [] pArgv; delete [] pEnv; return nErr; }
	char *pArgs = new char[ARGS_OLD];
	if (pArgs == 0 || pArgv == 0 || pEnv == 0) { delete [] pArgs; delete [] pArgv; delete [] pEnv; return -KAPI_ENOMEM; }
	ArgsFrom (pArgv, pArgs, ARGS_OLD);
	TProcInfo *pInfo = InfoMake (pArgv, nArgv, pEnv, nEnv);
	if (pInfo == 0) { delete [] pArgs; return -KAPI_ENOMEM; }
	pInfo->pInherit = Drop.p;			// (the record's from here: ProcInfoFree drops them)
	pInfo->nInherit = Drop.n;
	Drop.p = 0;
	Drop.n = 0;

	// (pinned: SpawnProcess may yield before the child takes its refs on them)
	CHandlePin In (A.in, HANDLE_STREAM), Out (A.out, HANDLE_STREAM);
	if ((A.in != 0 && In.Obj () == 0) || (A.out != 0 && Out.Obj () == 0))
	{
		ProcInfoFree (pInfo); delete [] pArgs;
		return -KAPI_EBADF;
	}
	CHandleTable *pTable = HandlesCurrent ();
	void *h = pTable != 0 ? pTable->Reserve () : 0;
	if (h == 0) { ProcInfoFree (pInfo); delete [] pArgs; return -KAPI_EMFILE; }
	CAddressSpace *pAS = CurrentAS ();
	CProcess *pProc = SpawnProcess (abs, pArgs, (CStream *) In.Obj (), (CStream *) Out.Obj (), cwd,
					pAS != 0 ? pAS->GetPid () : 0, pInfo);	// (pInfo: its own now)
	delete [] pArgs;
	if (pProc == 0)
	{
		pTable->Close (h, HANDLE_RESERVED);
		return -KAPI_ENOMEM;
	}
	if (!pTable->Fill (h, pProc, HANDLE_PROCESS))
	{
		ProcessRelease (pProc);
		return -KAPI_ENOMEM;
	}
	{
		// Until the child has its pid (its first time slice makes its space): proc_wait
		// (NOHANG | KEEP) then gives it at once. At most a second.
		CHandlePin Use (h, HANDLE_PROCESS);
		CProcess *p = (CProcess *) Use.Obj ();
		unsigned nStart = CTimer::GetClockTicks ();
		while (p != 0 && p->nPid == 0 && !p->bDone && CTimer::GetClockTicks () - nStart < 1000000)
		{
			CScheduler::Get ()->Yield ();
		}
	}
	return (long long) (uintptr) h;
}

long long kapi_spawn_ex (const struct kapi_spawn_attr *pUserAttr)
{
	return SpawnCommon (pUserAttr, 0, 0);
}

// (v76) spawn_ex, and n of the caller's handles referenced for the child (get_handles).
long long kapi_spawn_ex2 (const struct kapi_spawn_attr *pUserAttr, const struct kapi_handle_xfer *pUser, unsigned n)
{
	if (n == 0) return SpawnCommon (pUserAttr, 0, 0);
	if (n > KAPI_IPC_HANDLES_MAX) return -KAPI_EINVAL;
	struct kapi_handle_xfer *pIn = new struct kapi_handle_xfer[n];
	TInherit *pInh = new TInherit[n];
	if (pIn == 0 || pInh == 0) { delete [] pIn; delete [] pInh; return -KAPI_ENOMEM; }
	if (pUser == 0 || !UserCopyIn (pIn, pUser, n * sizeof *pIn))
	{
		delete [] pIn; delete [] pInh;
		return -KAPI_EFAULT;
	}
	CHandleTable *pTable = HandlesCurrent ();
	unsigned nPid = NetCurrentPid ();
	for (unsigned i = 0; i < n; i++)
	{
		int r = pIn[i].fd < 0 ? -KAPI_EBADF : IpcXferTake (pTable, pIn[i], nPid, &pInh[i].X);
		if (r < 0)
		{
			for (unsigned k = 0; k < i; k++) IpcXferDrop (&pInh[k].X, FALSE);
			delete [] pIn; delete [] pInh;
			return r;
		}
		pInh[i].nFd = pIn[i].fd;
		pInh[i].h = -1;
	}
	delete [] pIn;
	return SpawnCommon (pUserAttr, pInh, n);		// (pInh: its own now)
}

// (v76) The handles the spawner gave: in this process's table at the first call.
int kapi_get_handles (struct kapi_handle_xfer *pOut, unsigned nCap)
{
	CAddressSpace *pAS = CurrentAS ();
	TProcInfo *pInfo = pAS != 0 ? pAS->GetProcInfo () : 0;
	if (pInfo == 0 || pInfo->nInherit == 0) return 0;
	if (nCap > pInfo->nInherit) nCap = pInfo->nInherit;
	if (nCap > 0 && (pOut == 0 || !UserWritable (pOut, (u64) nCap * sizeof *pOut))) return -KAPI_EFAULT;
	if (!pInfo->bGiven)
	{
		pInfo->bGiven = TRUE;
		CHandleTable *pTable = HandlesCurrent ();
		unsigned nPid = pAS->GetPid ();
		for (unsigned i = 0; i < pInfo->nInherit; i++)
		{
			struct kapi_handle_xfer X;
			TInherit &I = pInfo->pInherit[i];
			int nKind = I.X.nHK;
			I.h = IpcXferGive (pTable, &I.X, nPid, FALSE, &X) ? X.h : -1;
			I.X.nHK = (u8) (I.h >= 0 ? nKind : KAPI_HK_NONE);	// (kept to report it)
			I.X.pObj = 0;
		}
	}
	for (unsigned i = 0; i < nCap; i++)
	{
		const TInherit &I = pInfo->pInherit[i];
		struct kapi_handle_xfer X;
		memset (&X, 0, sizeof X);
		X.h = I.h;
		X.kind = I.X.nHK;
		X.tag = I.X.nTag;
		X.fd = I.nFd;
		if (!UserCopyOut (&pOut[i], &X, sizeof X)) return -KAPI_EFAULT;
	}
	return (int) pInfo->nInherit;
}

int kapi_proc_wait (void *hProc, unsigned nFlags, struct kapi_proc_status *pOut)
{
	if (pOut != 0 && !UserRange (pOut, sizeof *pOut)) return -KAPI_EFAULT;
	struct kapi_proc_status St;
	memset (&St, 0, sizeof St);
	{
		CHandlePin Use (hProc, HANDLE_PROCESS);		// (pinned while it sleeps)
		CProcess *p = (CProcess *) Use.Obj ();
		if (p == 0) return -KAPI_EBADF;
		for (;;)
		{
			u32 nGen = IoGen ();
			if (p->bDone) break;
			if (nFlags & KAPI_WAIT_NOHANG)			// running: its pid, reason -1
			{
				St.reason = -1;
				St.pid = (int) p->nPid;
				if (pOut != 0) UserPut (pOut, St);
				return 0;
			}
			IoWait (nGen, 200);	// (the child's end wakes it; 200 ms: a start that failed)
		}
		St.code = p->nStatus;
		St.reason = p->nReason;
		St.pid = (int) p->nPid;
	}
	if (!(nFlags & KAPI_WAIT_KEEP))
	{
		CHandleTable *pTable = HandlesCurrent ();
		if (pTable != 0) pTable->Close (hProc, HANDLE_PROCESS);
	}
	if (pOut != 0 && !UserPut (pOut, St)) return -KAPI_EFAULT;
	return 1;
}

static int BlockOut (char *pBuf, unsigned nCap, const char *pBlock, unsigned nLen)
{
	if (pBuf != 0 && nCap > 0)
	{
		unsigned n = nCap < nLen ? nCap : nLen;
		if (!UserRange (pBuf, n) || !UserCopyOut (pBuf, pBlock, n)) return -KAPI_EFAULT;
	}
	return (int) nLen;
}

int kapi_get_argv (char *pBuf, unsigned nCap)
{
	CAddressSpace *pAS = CurrentAS ();
	TProcInfo *pInfo = pAS != 0 ? pAS->GetProcInfo () : 0;
	if (pInfo != 0) return BlockOut (pBuf, nCap, pInfo->pArgv, pInfo->nArgv);
	unsigned nLen = 0;					// (none: made from the task's name)
	char *p = ArgvFrom (CScheduler::Get ()->GetCurrentTask ()->GetName (), pAS != 0 ? pAS->GetArgs () : 0, &nLen);
	if (p == 0) return -KAPI_ENOMEM;
	int n = BlockOut (pBuf, nCap, p, nLen);
	delete [] p;
	return n;
}

int kapi_get_env (char *pBuf, unsigned nCap)
{
	CAddressSpace *pAS = CurrentAS ();
	TProcInfo *pInfo = pAS != 0 ? pAS->GetProcInfo () : 0;
	if (pInfo != 0) return BlockOut (pBuf, nCap, pInfo->pEnv, pInfo->nEnv);
	unsigned nLen = 0;
	char *p = EnvCopy (FALSE, &nLen);
	if (p == 0) return -KAPI_ENOMEM;
	int n = BlockOut (pBuf, nCap, p, nLen);
	delete [] p;
	return n;
}

int kapi_getpid (int nWhich)
{
	CAddressSpace *pAS = CurrentAS ();
	if (nWhich == 0) return pAS != 0 ? (int) pAS->GetPid () : 0;
	if (nWhich == 1) return pAS != 0 ? (int) pAS->GetParentPid () : 0;
	return -KAPI_EINVAL;
}

static u64 s_nBootCnt;					// CNTPCT at boot (computed once)

int kapi_clock_info (struct kapi_clock_info *pOut)
{
	if (pOut == 0 || !UserRange (pOut, sizeof *pOut)) return -KAPI_EFAULT;
	struct kapi_clock_info C;
	memset (&C, 0, sizeof C);
	CTimer *pTimer = CTimer::Get ();
	u64 nFreq, nCnt;
	asm volatile ("mrs %0, cntfrq_el0" : "=r" (nFreq));
	unsigned nSec = 0, nUs = 0;
	boolean bValid = pTimer->GetUniversalTime (&nSec, &nUs);	// (the last tick's: +-10 ms)
	asm volatile ("isb; mrs %0, cntpct_el0" : "=r" (nCnt) :: "memory");
	if (s_nBootCnt == 0)
	{
		unsigned nUpSec = 0, nUpUs = 0;
		pTimer->GetUptime (&nUpSec, &nUpUs);
		unsigned __int128 nUp = (unsigned __int128) ((u64) nUpSec * 1000000 + nUpUs) * nFreq / 1000000;
		s_nBootCnt = nCnt > (u64) nUp ? nCnt - (u64) nUp : 1;
	}
	C.cnt = nCnt;
	C.freq = nFreq;
	C.utc_us = bValid ? (long long) nSec * 1000000 + nUs : 0;
	C.tz_minutes = pTimer->GetTimeZone ();
	C.flags = bValid && nSec > 365u * 24 * 3600 ? KAPI_CLOCK_REALTIME_VALID : 0;	// (> ~1 year: a real date)
	C.boot_cnt = s_nBootCnt;
	return UserPut (pOut, C) ? 0 : -KAPI_EFAULT;
}

int kapi_sleep_us (unsigned long long nMicros)
{
	if (!CScheduler::IsActive ()) return 0;
	CScheduler *pSched = CScheduler::Get ();
	if (nMicros < 1000)					// (under the scheduler's resolution)
	{
		u64 nStart = CTimer::GetClockTicks64 ();
		do pSched->Yield (); while (CTimer::GetClockTicks64 () - nStart < nMicros);
		return 0;
	}
	while (nMicros > 0)
	{
		unsigned n = nMicros > 1000000000ull ? 1000000000u : (unsigned) nMicros;
		pSched->usSleep (n);
		nMicros -= n;
	}
	return 0;
}

}
