//
// handle.cpp -- per-process opaque handles (see kern/handle.h for the model and the rules).
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
#include <kern/handle.h>
#include <kern/stream.h>		// CStream, CProcess
#include <kern/ramfs.h>
#include <kern/vfs.h>
#include <kern/ofile.h>		// OFileClose (v75)
#include <kern/lsock.h>		// IpcLocalRelease, ShmRelease (v76)
#include <kern/addrspace.h>
#include <kern/volume.h>		// (v92) VolUntrack
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/util.h>
#include <circle/new.h>
#include <fatfs/ff.h>

// value = (generation << 16) | (index + 1)
#define HV_INDEX_BITS	16
#define HV_INDEX_MASK	0xFFFFu
#define HV_MAX		0xFFFFFFu

// ---- the objects ----------------------------------------------------------------------------

void ProcessRelease (CProcess *pProc)
{
	if (pProc != 0 && --pProc->nRef <= 0)
	{
		delete pProc;
	}
}

void HandleObjectClose (void *pObj, unsigned nType, unsigned nKind, boolean bTeardown)
{
	if (pObj == 0)
	{
		return;
	}
	switch (nType)
	{
	case HANDLE_FILE:
		if (nKind == HKIND_RAMFS)
		{
			// (a teardown: RamFsOnProcessGone, run just before, closed the process's RAM:
			// handles -- its slot may already be another process's)
			if (!bTeardown) RamFsClose (pObj);
		}
		else if (nKind == HKIND_VFS)
		{
			if (bTeardown) VfsDropFile (pObj);	// (no request: nothing may block here)
			else VfsClose (pObj);
		}
		else
		{
			FIL *pFile = (FIL *) pObj;
			// A read handle (FA_READ) and no FatFs share locks (FF_FS_LOCK 0): f_close
			// flushes nothing, it only takes the volume lock to invalidate the object -- left
			// out in a teardown, where nothing may wait for that lock.
			if (!bTeardown) f_close (pFile);
			VolUntrack (&pFile->obj);		// (v92, kern/volume.h)
			delete [] pFile->cltbl;			// (kapi_seek's fast-seek map)
			delete pFile;
		}
		break;

	case HANDLE_DIR:
		if (nKind == HKIND_RAMFS)
		{
			if (!bTeardown) RamFsCloseDir (pObj);
		}
		else if (nKind == HKIND_VFS)
		{
			VfsCloseDir (pObj);			// (a kernel copy of the listing: no request)
		}
		else
		{
			if (!bTeardown) f_closedir ((DIR *) pObj);	// (as a FIL: nothing else to do)
			VolUntrack (&((DIR *) pObj)->obj);
			delete (DIR *) pObj;
		}
		break;

	case HANDLE_STREAM:
		if ((nKind & HKIND_STREAM_WRITER) && !(nKind & HKIND_STREAM_EOF_DONE))
		{
			((CStream *) pObj)->CloseWrite ();	// (v76: a carried write end gone)
		}
		if (bTeardown) HandlesDeferRelease ((CStream *) pObj);
		else ((CStream *) pObj)->Release ();
		break;

	case HANDLE_PROCESS:
		ProcessRelease ((CProcess *) pObj);
		break;

	case HANDLE_OFILE:				// (v75: its node's reference dropped)
		OFileClose (pObj, bTeardown);
		break;

	case HANDLE_LSOCK:				// (v76: the end's reference dropped)
		IpcLocalRelease (pObj, bTeardown);
		break;

	case HANDLE_SHM:				// (v76: the object's reference dropped)
		ShmRelease (pObj, bTeardown);
		break;

	case HANDLE_RESERVED:				// (no object yet)

	default:
		break;
	}
}

// ---- deferred stream releases (a teardown's) ---------------------------------------------

struct TDeferred
{
	CStream	  *pStream;
	TDeferred *pNext;
};

static TDeferred *s_pDeferred = 0;

void HandlesDeferRelease (CStream *pStream)
{
	if (pStream == 0)
	{
		return;
	}
	if (pStream->GetRefs () > 1)			// not the last: nothing is closed, now
	{
		pStream->Release ();
		return;
	}
	TDeferred *p = new TDeferred;
	if (p == 0)
	{
		pStream->Release ();			// (out of memory: now, as before)
		return;
	}
	p->pStream = pStream;
	p->pNext = s_pDeferred;
	s_pDeferred = p;
}

void HandlesRunDeferred (void)
{
	// (a release may yield -- a file stream's flush: take the list first, so a teardown
	// meanwhile starts a new one)
	TDeferred *p = s_pDeferred;
	s_pDeferred = 0;
	while (p != 0)
	{
		TDeferred *pNext = p->pNext;
		p->pStream->Release ();
		delete p;
		p = pNext;
	}
	OFileRunDeferred ();				// (v75: the open files a teardown left, kern/ofile.h)
}

// ---- CHandleTable ---------------------------------------------------------------------------

CHandleTable::CHandleTable (void)
:	m_pEntry (0),
	m_nSize (0),
	m_nUsed (0)
{
}

CHandleTable::~CHandleTable (void)
{
	CloseAll (TRUE);
	delete [] m_pEntry;
	m_pEntry = 0;
	m_nSize = 0;
}

void *CHandleTable::Add (void *pObj, unsigned nType, unsigned nKind)
{
	if (pObj == 0 || nType == HANDLE_FREE)
	{
		return 0;
	}
	unsigned i = 0;
	for (; i < m_nSize; i++)
	{
		if (m_pEntry[i].pObj == 0)
		{
			break;
		}
	}
	if (i == m_nSize)				// full: grow (doubling)
	{
		if (m_nSize >= HANDLES_MAX)
		{
			return 0;
		}
		unsigned nNew = m_nSize == 0 ? HANDLES_FIRST : m_nSize * 2;
		if (nNew > HANDLES_MAX) nNew = HANDLES_MAX;
		THandleEntry *pNew = new THandleEntry[nNew];
		if (pNew == 0)
		{
			return 0;
		}
		if (m_nSize > 0) memcpy (pNew, m_pEntry, m_nSize * sizeof (THandleEntry));
		memset (pNew + m_nSize, 0, (nNew - m_nSize) * sizeof (THandleEntry));
		delete [] m_pEntry;
		m_pEntry = pNew;
		m_nSize = nNew;
	}

	THandleEntry *e = &m_pEntry[i];
	e->pObj = pObj;
	e->nType = (u8) nType;
	e->nKind = (u8) nKind;
	e->nGen = (u8) (e->nGen >= 255 ? 1 : e->nGen + 1);	// (1..255, another one each use)
	e->bClosing = 0;
	e->nPins = 0;
	m_nUsed++;
	return (void *) (uintptr) (((uintptr) e->nGen << HV_INDEX_BITS) | (i + 1));
}

int CHandleTable::Lookup (void *h, unsigned nType) const
{
	uintptr v = (uintptr) h;
	if (v == 0 || v > HV_MAX)
	{
		return -1;
	}
	unsigned nIdx = (unsigned) (v & HV_INDEX_MASK);
	unsigned nGen = (unsigned) (v >> HV_INDEX_BITS);
	if (nIdx == 0 || nIdx > m_nSize)
	{
		return -1;
	}
	const THandleEntry *e = &m_pEntry[nIdx - 1];
	if (e->pObj == 0 || e->bClosing || e->nGen != nGen || e->nType != nType)
	{
		return -1;
	}
	return (int) (nIdx - 1);
}

void *CHandleTable::Get (void *h, unsigned nType, unsigned *pKind)
{
	int i = Lookup (h, nType);
	if (i < 0)
	{
		return 0;
	}
	if (pKind != 0) *pKind = m_pEntry[i].nKind;
	return m_pEntry[i].pObj;
}

void *CHandleTable::Pin (void *h, unsigned nType, unsigned *pIdx, unsigned *pKind)
{
	int i = Lookup (h, nType);
	if (i < 0 || m_pEntry[i].nPins == 0xFFFF)
	{
		return 0;
	}
	m_pEntry[i].nPins++;
	*pIdx = (unsigned) i;
	if (pKind != 0) *pKind = m_pEntry[i].nKind;
	return m_pEntry[i].pObj;
}

void CHandleTable::Unpin (unsigned nIdx)
{
	// (by index: the table may have grown -- moved -- while the call yielded)
	if (nIdx >= m_nSize || m_pEntry[nIdx].pObj == 0 || m_pEntry[nIdx].nPins == 0)
	{
		return;
	}
	THandleEntry *e = &m_pEntry[nIdx];
	if (--e->nPins == 0 && e->bClosing)		// closed meanwhile: closed now
	{
		void *pObj = e->pObj;
		unsigned nType = e->nType, nKind = e->nKind;
		Free (nIdx);
		HandleObjectClose (pObj, nType, nKind, FALSE);
	}
}

boolean CHandleTable::Close (void *h, unsigned nType)
{
	int i = Lookup (h, nType);
	if (i < 0)
	{
		return FALSE;
	}
	THandleEntry *e = &m_pEntry[i];
	if (e->nPins > 0)
	{
		e->bClosing = 1;			// (no longer found; the last Unpin closes it)
		return TRUE;
	}
	void *pObj = e->pObj;
	unsigned nKind = e->nKind;
	Free ((unsigned) i);				// out of the table first: closing may yield
	HandleObjectClose (pObj, nType, nKind, FALSE);
	return TRUE;
}

boolean CHandleTable::Fill (void *h, void *pObj, unsigned nType, unsigned nKind)
{
	int i = Lookup (h, HANDLE_RESERVED);
	if (i < 0 || pObj == 0 || nType == HANDLE_FREE || nType == HANDLE_RESERVED)
	{
		return FALSE;
	}
	m_pEntry[i].pObj = pObj;
	m_pEntry[i].nType = (u8) nType;
	m_pEntry[i].nKind = (u8) nKind;
	return TRUE;
}

void *CHandleTable::Find (const void *pObj, unsigned nType) const
{
	if (pObj == 0)
	{
		return 0;
	}
	for (unsigned i = 0; i < m_nSize; i++)
	{
		const THandleEntry *e = &m_pEntry[i];
		if (e->pObj == pObj && e->nType == nType && !e->bClosing)
		{
			return (void *) (uintptr) (((uintptr) e->nGen << HV_INDEX_BITS) | (i + 1));
		}
	}
	return 0;
}

boolean CHandleTable::SetKind (void *h, unsigned nType, unsigned nKind)
{
	int i = Lookup (h, nType);
	if (i < 0) return FALSE;
	m_pEntry[i].nKind = (u8) nKind;
	return TRUE;
}

unsigned CHandleTable::TypeOf (void *h) const
{
	uintptr v = (uintptr) h;
	unsigned nIdx = (unsigned) (v & HV_INDEX_MASK);
	if (v == 0 || v > HV_MAX || nIdx == 0 || nIdx > m_nSize) return HANDLE_FREE;
	const THandleEntry *e = &m_pEntry[nIdx - 1];
	if (e->pObj == 0 || e->bClosing || e->nGen != (unsigned) (v >> HV_INDEX_BITS)) return HANDLE_FREE;
	return e->nType;
}

void CHandleTable::CloseAll (boolean bTeardown)
{
	for (unsigned i = 0; i < m_nSize; i++)
	{
		THandleEntry *e = &m_pEntry[i];
		if (e->pObj == 0)
		{
			continue;
		}
		void *pObj = e->pObj;
		unsigned nType = e->nType, nKind = e->nKind;
		Free (i);
		HandleObjectClose (pObj, nType, nKind, bTeardown);
	}
}

void CHandleTable::Free (unsigned nIdx)
{
	THandleEntry *e = &m_pEntry[nIdx];
	e->pObj = 0;
	e->nType = HANDLE_FREE;
	e->bClosing = 0;
	e->nPins = 0;					// (nGen kept: the next use takes another one)
	if (m_nUsed > 0) m_nUsed--;
}

// ---- the calling task's table ---------------------------------------------------------------

static CHandleTable *s_pKernelHandles = 0;	// the kernel's own tasks' (made on first use, kept)

static CHandleTable *KernelHandles (void)
{
	if (s_pKernelHandles == 0)
	{
		s_pKernelHandles = new CHandleTable;	// (0: out of memory -- the kapis then fail)
	}
	return s_pKernelHandles;
}

CHandleTable *HandlesCurrent (void)
{
	if (!CScheduler::IsActive ())
	{
		return KernelHandles ();
	}
	CTask *pTask = CScheduler::Get ()->GetCurrentTask ();
	CAddressSpace *pAS = pTask != 0 ? (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER) : 0;
	return pAS != 0 ? pAS->GetHandles () : KernelHandles ();
}
