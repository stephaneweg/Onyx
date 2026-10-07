//
// shm.cpp -- shared memory objects (kapi v76, WP-IPC: shm_create, shm_open, shm_unlink, shm_ctl,
// handle_close; shm_map is vm.cpp's). docs/POSIX-PLAN.md §14, docs/02 §8 "v76: IPC"; kern/lsock.h.
//
// An object (TShm) is a size and an array of 64 KB frames, each allocated from the app pool and
// zeroed at its first use (a fault in a mapping, a POPULATE, a WILLNEED), all freed when the last
// reference goes: the HANDLE_SHM entries (nKind: the access), the messages and spawn records
// carrying it (lsock.cpp), the name table (shm_open), the address spaces mapping it (ShmMapAttach:
// one each, held by vm.cpp until no region and no deferred zap can name the object). So a frame is
// never freed while a PTE maps it: an unmap only drops the PTE (not owned). Before a frame goes,
// its word waiters (wait_word on it) are woken. Seals as Linux's memfd: SEAL_SEAL (set unless
// created KAPI_SHM_ALLOW_SEALING), SHRINK, GROW, WRITE (only while nothing maps it).
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
#include <kern/lsock.h>
#include <kern/kapi_abi.h>
#include <kern/handle.h>
#include <kern/vm.h>			// VmPoolFree, VmCommitOK, VM_RESERVE, WordWaitsZap
#include <kern/layout.h>		// KPAGE_SIZE
#include <kern/uaccess.h>
#include <circle/alloc.h>		// palloc_high, pfree
#include <circle/new.h>
#include <circle/util.h>

#define SHM_MAGIC	0x53484D4Fu		// "SHMO": a live TShm

struct TShm
{
	u32	 nMagic;
	int	 nRefs;
	u64	 nSize;				// bytes (ftruncate's)
	u64	 nCap;				// entries of pFrame
	u64	*pFrame;			// physical addresses, 0: not yet
	unsigned nSeals;			// KAPI_SEAL_*
	unsigned nMaps;				// address spaces mapping it
	u32	 nId;
	boolean	 bNamed;			// in the name table
	char	 Name[KAPI_SHM_NAME_MAX + 1];
	TShm	*pNextNamed;
};

static TShm	*s_pNamed;			// shm_open's table
static u32	 s_nNextId = 1;
static unsigned	 s_nPages;			// frames held by every object
static unsigned	 s_nObjects;

static inline u64 PagesFor (u64 nSize)	{ return (nSize + KPAGE_SIZE - 1) / KPAGE_SIZE; }

boolean ShmIs (const void *p)		{ return p != 0 && ((const TShm *) p)->nMagic == SHM_MAGIC; }
u64 ShmSize (const void *p)		{ return ((const TShm *) p)->nSize; }
unsigned ShmSeals (const void *p)	{ return ((const TShm *) p)->nSeals; }
unsigned ShmMaps (const void *p)	{ return ((const TShm *) p)->nMaps; }
unsigned ShmPagesTotal (void)		{ return s_nPages; }
unsigned ShmObjectsTotal (void)		{ return s_nObjects; }

void ShmRef (void *p)
{
	if (ShmIs (p)) ((TShm *) p)->nRefs++;
}

// The frames from page nFirst on freed (nothing maps them).
static void FreeFrom (TShm *o, u64 nFirst)
{
	for (u64 i = nFirst; i < o->nCap; i++)
	{
		if (o->pFrame[i] != 0)
		{
			WordWaitsZap (o->pFrame[i], KPAGE_SIZE);
			pfree ((void *) (uintptr) o->pFrame[i]);
			o->pFrame[i] = 0;
			s_nPages--;
		}
	}
}

void ShmRelease (void *p, boolean bTeardown)
{
	(void) bTeardown;				// (nothing here waits)
	TShm *o = (TShm *) p;
	if (!ShmIs (o) || --o->nRefs > 0) return;
	FreeFrom (o, 0);
	o->nMagic = 0;
	delete [] o->pFrame;
	delete o;
	s_nObjects--;
}

int ShmFrame (void *p, u64 nPage, u64 *pPhys)
{
	TShm *o = (TShm *) p;
	if (!ShmIs (o) || nPage >= PagesFor (o->nSize) || nPage >= o->nCap) return -KAPI_EFAULT;
	if (o->pFrame[nPage] == 0)
	{
		if (VmPoolFree () < VM_RESERVE + 2 * KPAGE_SIZE) return -KAPI_ENOMEM;
		void *pFrame = palloc_high ();
		if (pFrame == 0) return -KAPI_ENOMEM;
		memset (pFrame, 0, KPAGE_SIZE);
#ifdef __aarch64__						// (the host test: none)
		asm volatile ("dsb ishst" ::: "memory");	// (the zeros before any PTE names it)
#endif
		o->pFrame[nPage] = (u64) (uintptr) pFrame;
		s_nPages++;
	}
	*pPhys = o->pFrame[nPage];
	return 0;
}

void ShmMapAttach (void *p)
{
	TShm *o = (TShm *) p;
	if (!ShmIs (o)) return;
	o->nRefs++;
	o->nMaps++;
}

void ShmMapDetach (void *p, boolean bTeardown)
{
	TShm *o = (TShm *) p;
	if (!ShmIs (o)) return;
	if (o->nMaps > 0) o->nMaps--;
	ShmRelease (o, bTeardown);
}

static TShm *ShmNew (unsigned nSeals)
{
	TShm *o = new TShm;
	if (o == 0) return 0;
	memset (o, 0, sizeof *o);
	o->nMagic = SHM_MAGIC;
	o->nRefs = 1;
	o->nSeals = nSeals;
	o->nId = s_nNextId++;
	s_nObjects++;
	return o;
}

// The size set (ftruncate) -> 0 / -errno.
static int SetSize (TShm *o, u64 nSize)
{
	if (nSize > IPC_SHM_MAX) return -KAPI_EFBIG;
	if (nSize == o->nSize) return 0;
	u64 nPages = PagesFor (nSize);
	if (nSize > o->nSize)
	{
		if (o->nSeals & KAPI_SEAL_GROW) return -KAPI_EPERM;
		if (!VmCommitOK (nSize - o->nSize)) return -KAPI_ENOMEM;	// (the heuristic overcommit)
		if (nPages > o->nCap)
		{
			u64 nCap = nPages;
			u64 *pNew = new u64[nCap];
			if (pNew == 0) return -KAPI_ENOMEM;
			for (u64 i = 0; i < nCap; i++) pNew[i] = i < o->nCap ? o->pFrame[i] : 0;
			delete [] o->pFrame;
			o->pFrame = pNew;
			o->nCap = nCap;
		}
		o->nSize = nSize;
		return 0;
	}
	if (o->nSeals & KAPI_SEAL_SHRINK) return -KAPI_EPERM;
	if (o->nMaps > 0) return -KAPI_EBUSY;			// (a mapping may hold those frames)
	FreeFrom (o, nPages);
	u64 nTail = nSize % KPAGE_SIZE;				// (a regrowth reads zeros there)
	if (nTail != 0 && nPages - 1 < o->nCap && o->pFrame[nPages - 1] != 0)
	{
		memset ((u8 *) (uintptr) o->pFrame[nPages - 1] + nTail, 0, KPAGE_SIZE - nTail);
	}
	o->nSize = nSize;
	return 0;
}

// A name for the table: one leading '/' dropped, 1..63 characters, no other '/'.
static int CleanName (const char *pIn, char *pOut)
{
	if (*pIn == '/') pIn++;
	unsigned n = 0;
	for (; pIn[n] != '\0'; n++)
	{
		if (pIn[n] == '/') return -KAPI_EINVAL;
		if (n >= KAPI_SHM_NAME_MAX) return -KAPI_ENAMETOOLONG;
		pOut[n] = pIn[n];
	}
	if (n == 0) return -KAPI_EINVAL;
	pOut[n] = '\0';
	return 0;
}

static TShm *FindNamed (const char *pName)
{
	for (TShm *o = s_pNamed; o != 0; o = o->pNextNamed) if (strcmp (o->Name, pName) == 0) return o;
	return 0;
}

static void *HandleOf (long long h)
{
	return h > 0 && h <= 0xFFFFFF ? (void *) (uintptr) (unsigned) h : (void *) (uintptr) 0x1000000;
}

// A new handle for o (its reference handed over; dropped on failure) -> the handle / -EMFILE.
static long long NewHandle (TShm *o, unsigned nAccess)
{
	CHandleTable *pTable = HandlesCurrent ();
	void *h = pTable != 0 ? pTable->Add (o, HANDLE_SHM, nAccess) : 0;
	if (h == 0)
	{
		ShmRelease (o, FALSE);
		return -KAPI_EMFILE;
	}
	return (long long) (uintptr) h;
}

extern "C" {

long long kapi_shm_create (unsigned long long nSize, unsigned nFlags)
{
	if (nFlags & ~KAPI_SHM_ALLOW_SEALING) return -KAPI_EINVAL;
	TShm *o = ShmNew ((nFlags & KAPI_SHM_ALLOW_SEALING) ? 0 : KAPI_SEAL_SEAL);
	if (o == 0) return -KAPI_ENOMEM;
	unsigned nSeals = o->nSeals;
	o->nSeals = 0;
	int r = SetSize (o, nSize);
	o->nSeals = nSeals;
	if (r < 0)
	{
		ShmRelease (o, FALSE);
		return r;
	}
	return NewHandle (o, KAPI_O_RDWR);
}

long long kapi_shm_open (const char *pName, unsigned nOFlags, unsigned nMode)
{
	(void) nMode;					// (one user)
	CUserStr Name (pName, 256);
	if (!Name.OK ()) return -KAPI_EFAULT;
	char Clean[KAPI_SHM_NAME_MAX + 1];
	int r = CleanName (Name.Get (), Clean);
	if (r < 0) return r;
	unsigned nAcc = nOFlags & KAPI_O_ACCMODE;
	if (nAcc != KAPI_O_RDONLY && nAcc != KAPI_O_RDWR) return -KAPI_EINVAL;
	if (nOFlags & ~(KAPI_O_ACCMODE | KAPI_O_CREAT | KAPI_O_EXCL | KAPI_O_TRUNC)) return -KAPI_EINVAL;
	TShm *o = FindNamed (Clean);
	if (o != 0)
	{
		if ((nOFlags & KAPI_O_CREAT) && (nOFlags & KAPI_O_EXCL)) return -KAPI_EEXIST;
		if (nOFlags & KAPI_O_TRUNC)
		{
			if (nAcc != KAPI_O_RDWR) return -KAPI_EACCES;
			r = SetSize (o, 0);
			if (r < 0) return r;
		}
		o->nRefs++;
		return NewHandle (o, nAcc);
	}
	if (!(nOFlags & KAPI_O_CREAT)) return -KAPI_ENOENT;
	o = ShmNew (0);					// (shm_open's objects: seals allowed)
	if (o == 0) return -KAPI_ENOMEM;
	strcpy (o->Name, Clean);
	o->bNamed = TRUE;
	o->pNextNamed = s_pNamed;
	s_pNamed = o;
	o->nRefs++;					// (the table's)
	return NewHandle (o, nAcc);
}

int kapi_shm_unlink (const char *pName)
{
	CUserStr Name (pName, 256);
	if (!Name.OK ()) return -KAPI_EFAULT;
	char Clean[KAPI_SHM_NAME_MAX + 1];
	int r = CleanName (Name.Get (), Clean);
	if (r < 0) return r;
	for (TShm **pp = &s_pNamed; *pp != 0; pp = &(*pp)->pNextNamed)
	{
		TShm *o = *pp;
		if (strcmp (o->Name, Clean) == 0)
		{
			*pp = o->pNextNamed;
			o->bNamed = FALSE;
			ShmRelease (o, FALSE);
			return 0;
		}
	}
	return -KAPI_ENOENT;
}

long long kapi_shm_ctl (long long h, int nOp, unsigned long long nArg)
{
	CHandleTable *pTable = HandlesCurrent ();
	unsigned nAccess = 0;
	TShm *o = pTable != 0 ? (TShm *) pTable->Get (HandleOf (h), HANDLE_SHM, &nAccess) : 0;
	if (!ShmIs (o)) return -KAPI_EBADF;
	switch (nOp)
	{
	case KAPI_SHM_GET_SIZE:
		return (long long) o->nSize;

	case KAPI_SHM_SET_SIZE:
		if (nAccess != KAPI_O_RDWR) return -KAPI_EINVAL;	// (ftruncate of a read-only fd)
		return SetSize (o, nArg);

	case KAPI_SHM_ADD_SEALS:
		if (nArg & ~(u64) (KAPI_SEAL_SEAL | KAPI_SEAL_SHRINK | KAPI_SEAL_GROW | KAPI_SEAL_WRITE)) return -KAPI_EINVAL;
		if (nAccess != KAPI_O_RDWR || (o->nSeals & KAPI_SEAL_SEAL)) return -KAPI_EPERM;
		if ((nArg & KAPI_SEAL_WRITE) && o->nMaps > 0) return -KAPI_EBUSY;
		o->nSeals |= (unsigned) nArg;
		return 0;

	case KAPI_SHM_GET_SEALS:
		return o->nSeals;

	case KAPI_SHM_GET_ID:
		return o->nId;

	case KAPI_SHM_GET_ACCESS:
		return nAccess;

	default:
		return -KAPI_EINVAL;
	}
}

int kapi_handle_close (long long h)
{
	CHandleTable *pTable = HandlesCurrent ();
	if (pTable == 0) return -KAPI_EBADF;
	void *v = HandleOf (h);
	unsigned nType = pTable->TypeOf (v);
	switch (nType)
	{
	case HANDLE_SHM:
	case HANDLE_LSOCK:
	case HANDLE_OFILE:
	case HANDLE_STREAM:
		return pTable->Close (v, nType) ? 0 : -KAPI_EBADF;
	default:
		return -KAPI_EBADF;
	}
}

}
