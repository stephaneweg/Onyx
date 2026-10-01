//
// ramfs.cpp -- RAM:, the kernel's RAM file system (see kern/ramfs.h, docs/02 *The RAM: volume*).
//
// The tree: TNode records (folders, files) on the kernel heap -- all the same size, so a freed
// one is reused as is (Circle's heap keeps freed blocks by size, it never merges them). A
// file's bytes are a list of extents (TExtent), each a chunk of a 64 KB page of the page
// allocator: a whole page, or one of the 256 B .. 32 KB chunks a slab page (TSlab) is cut into.
// Every extent but the last is full. A file written whole (save_file) is cut exactly -- whole
// pages, then the rest in falling powers of two (40 KB + 100 B: 32 KB, 8 KB, 256 B) --, a file
// written by a stream grows by doubling chunks and its last chunk is fitted when it is closed.
// The extent list itself lives in a chunk. A page is given back once nothing in it is used.
//
// Built for the PC too (RAMFS_HOST_TEST: tools/tests/ramfs, the operations checked against a
// model): the platform's few calls (pages, the scheduler, the current process) are below.
//
#include <kern/ramfs.h>
#include <kern/kapi_abi.h>		// struct kapi_dirent, struct kapi_vol_info

#ifndef RAMFS_HOST_TEST
#include <kern/addrspace.h>
#include <circle/alloc.h>		// palloc_high, pfree
#include <circle/memory.h>		// CMemorySystem: the free pages, the free heap
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/logger.h>
#include <circle/util.h>
#include <circle/new.h>

static inline void *PlatPageAlloc (void) { return palloc_high (); }
static inline void PlatPageFree (void *p) { pfree (p); }
// The page memory the apps draw from: the high zone (or, without one -- a 1 GB Pi -- the low
// pager, which palloc_high falls back to).
static u64 PlatPagesFree (void)
{
	if (CMemorySystem::GetHighZoneTotal () > 0)
		return (u64) CMemorySystem::GetPagerHighFreeSpace () + CMemorySystem::GetPagerHighFreeListSpace ();
	return (u64) CMemorySystem::GetPagerFreeSpace () + CMemorySystem::GetPagerFreeListSpace ();
}
// The heap never handed out yet (a record of a new size comes from there; out of it, Circle
// panics).
static u64 PlatHeapFree (void)
{
	CMemorySystem *pMem = CMemorySystem::Get ();
	return pMem != 0 ? (u64) pMem->GetHeapFreeSpace (HEAP_ANY) : 0;
}
static inline void *PlatTask (void)
{
	return CScheduler::IsActive () ? (void *) CScheduler::Get ()->GetCurrentTask () : 0;
}
static inline void PlatYield (void) { if (CScheduler::IsActive ()) CScheduler::Get ()->Yield (); }
static inline void PlatNoKill (boolean bEnter)
{
	if (!CScheduler::IsActive ()) return;
	if (bEnter) CScheduler::Get ()->EnterNoKill (); else CScheduler::Get ()->LeaveNoKill ();
}
static unsigned PlatPid (void)
{
	if (!CScheduler::IsActive ()) return 0;
	CAddressSpace *pAS = (CAddressSpace *) CScheduler::Get ()->GetCurrentTask ()->GetUserData (TASK_USER_DATA_USER);
	return pAS != 0 ? pAS->GetPid () : 0;
}
#define RamLog(...)	CLogger::Get ()->Write ("ramfs", LogNotice, __VA_ARGS__)
#define PAGE_BYTES	KPAGE_SIZE
#else
#include <string.h>
#include <stdio.h>
extern void *RamPlatPageAlloc (void);
extern void RamPlatPageFree (void *);
extern u64 RamPlatPagesFree (void);
extern void *RamPlatTask (void);
extern void RamPlatYield (void);
extern unsigned RamPlatPid (void);
#define PlatPageAlloc	RamPlatPageAlloc
#define PlatPageFree	RamPlatPageFree
#define PlatPagesFree	RamPlatPagesFree
#define PlatTask	RamPlatTask
#define PlatYield	RamPlatYield
#define PlatPid		RamPlatPid
static inline u64 PlatHeapFree (void) { return 1ull << 40; }
static inline void PlatNoKill (boolean) {}
#define RamLog(...)	(printf ("ramfs: " __VA_ARGS__), printf ("\n"))
#define PAGE_BYTES	65536u
#endif

#define CHUNK_MIN	256u			// the smallest chunk
#define NCLASS		8			// 256 B << 0 .. 7 = 32 KB; bigger: a whole page
#define HEAP_MARGIN	(8ull << 20)		// the heap never handed out, kept free
#define MAX_FILES	256			// open read handles
#define MAX_DIRS	64			// open folder listings

struct TSlab					// a page cut into chunks of one size
{
	u8	*pPage;
	TSlab	*pPrev, *pNext;			// the class's pages with a free chunk
	u16	 nClass, nUsed, nChunks;
	u64	 Bits[4];			// the chunks used (256 at most: 256-byte chunks)
};

struct TExtent
{
	u8	*p;
	TSlab	*pSlab;				// 0: a whole page
	u32	 nCap;
};

struct TNode
{
	char	 Name[RAMFS_NAME_MAX + 1];
	TNode	*pParent, *pChild, *pNext;	// the folder's children: a list in creation order
	boolean	 bDir, bUnlinked;		// unlinked: removed while open (freed at the last close)
	u64	 nSize, nCapTotal;		// the bytes, the extents' room
	TExtent	*pExt;				// the extents (in the chunk ExtStore)
	TExtent	 ExtStore;
	unsigned nExt, nExtCap;
	unsigned nRefs;				// open handles and streams
	unsigned nGen;				// bumped when its extents change (a cursor walks again)
};

struct TCursor { unsigned nGen, nIdx; u64 nStart; };	// the extent of a read position

struct TRamFile { TNode *pNode; u64 nPos; TCursor Cur; unsigned nPid; boolean bUsed; };
struct TRamDir  { u8 *pList; unsigned nLen, nPos, nPid; boolean bUsed; };
struct TRamStream { TNode *pNode; u64 nPos; TCursor Cur; int nMode; };

static boolean	 s_bUp;
static TNode	*s_pRoot;
static u64	 s_nCapPages, s_nPages;		// the volume's size, the pages held
static unsigned	 s_nFiles, s_nDirs;
static TSlab	*s_pPartial[NCLASS];
static TRamFile	*s_pFile;			// (on the heap: not in the kernel's BSS)
static TRamDir	*s_pDir;

// ---- the lock: re-entrant, its waiters yield (nothing yields while holding it) ----------------

static void *volatile s_pOwner;
static unsigned s_nDepth;

static void Lock (void)
{
	void *pMe = PlatTask ();
	if (pMe == 0) return;				// (boot: no tasks yet)
	for (;;)
	{
		void *pFree = 0;
		if (__atomic_compare_exchange_n (&s_pOwner, &pFree, pMe, FALSE, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		{
			s_nDepth = 1;
			return;
		}
		if (s_pOwner == pMe) { s_nDepth++; return; }
		PlatYield ();
	}
}

static void Unlock (void)
{
	void *pMe = PlatTask ();
	if (pMe == 0 || s_pOwner != pMe || s_nDepth == 0) return;
	if (--s_nDepth == 0) __atomic_store_n (&s_pOwner, (void *) 0, __ATOMIC_RELEASE);
}

struct TGuard { TGuard (void) { Lock (); } ~TGuard (void) { Unlock (); } };

// ---- pages and chunks --------------------------------------------------------------------------

static u8 *PageGet (void)
{
	if (s_nPages >= s_nCapPages || PlatPagesFree () < RAMFS_RESERVE + PAGE_BYTES) return 0;
	u8 *p = (u8 *) PlatPageAlloc ();
	if (p != 0) s_nPages++;
	return p;
}

static void PagePut (u8 *p)
{
	PlatPageFree (p);
	s_nPages--;
}

static void SlabLink (TSlab *s)
{
	s->pPrev = 0; s->pNext = s_pPartial[s->nClass];
	if (s->pNext != 0) s->pNext->pPrev = s;
	s_pPartial[s->nClass] = s;
}

static void SlabUnlink (TSlab *s)
{
	if (s->pPrev != 0) s->pPrev->pNext = s->pNext; else s_pPartial[s->nClass] = s->pNext;
	if (s->pNext != 0) s->pNext->pPrev = s->pPrev;
	s->pPrev = s->pNext = 0;
}

// A chunk of at least nBytes (<= a page): a slab's chunk (<= 32 KB) or a whole page.
static boolean ChunkAlloc (u32 nBytes, TExtent *pOut)
{
	if (nBytes > PAGE_BYTES / 2)
	{
		u8 *p = PageGet ();
		if (p == 0) return FALSE;
		pOut->p = p; pOut->pSlab = 0; pOut->nCap = PAGE_BYTES;
		return TRUE;
	}
	unsigned c = 0;
	while ((CHUNK_MIN << c) < nBytes) c++;
	u32 nSize = CHUNK_MIN << c;
	TSlab *s = s_pPartial[c];
	if (s == 0)
	{
		if (PlatHeapFree () < HEAP_MARGIN) return FALSE;
		u8 *p = PageGet ();
		if (p == 0) return FALSE;
		s = new TSlab;
		if (s == 0) { PagePut (p); return FALSE; }
		memset (s, 0, sizeof *s);
		s->pPage = p; s->nClass = (u16) c; s->nChunks = (u16) (PAGE_BYTES / nSize);
		for (unsigned i = s->nChunks; i < 256; i++) s->Bits[i / 64] |= 1ull << (i % 64);	// (none there)
		SlabLink (s);
	}
	unsigned nIdx = 0;
	for (unsigned w = 0; w < 4; w++)
		if (~s->Bits[w] != 0)
		{
			nIdx = w * 64 + (unsigned) __builtin_ctzll (~s->Bits[w]);
			break;
		}
	s->Bits[nIdx / 64] |= 1ull << (nIdx % 64);
	if (++s->nUsed == s->nChunks) SlabUnlink (s);
	pOut->p = s->pPage + nIdx * nSize; pOut->pSlab = s; pOut->nCap = nSize;
	return TRUE;
}

static void ChunkFree (const TExtent &e)
{
	if (e.p == 0) return;
	TSlab *s = e.pSlab;
	if (s == 0) { PagePut (e.p); return; }
	unsigned nIdx = (unsigned) ((e.p - s->pPage) / (CHUNK_MIN << s->nClass));
	u64 nBit = 1ull << (nIdx % 64);
	if (!(s->Bits[nIdx / 64] & nBit)) return;		// (not used: never)
	if (s->nUsed == s->nChunks) SlabLink (s);		// (full until now: free chunks again)
	s->Bits[nIdx / 64] &= ~nBit;
	if (--s->nUsed == 0)
	{
		SlabUnlink (s);
		PagePut (s->pPage);
		delete s;
	}
}

// ---- a file's bytes ----------------------------------------------------------------------------

static boolean ExtReserve (TNode *n, unsigned nNeed)
{
	if (nNeed <= n->nExtCap) return TRUE;
	unsigned nCap = n->nExtCap ? n->nExtCap * 2 : 8;
	while (nCap < nNeed) nCap *= 2;
	u64 nBytes = (u64) nCap * sizeof (TExtent);
	if (nBytes > PAGE_BYTES) nBytes = PAGE_BYTES;
	if (nBytes / sizeof (TExtent) < nNeed) return FALSE;	// (a page of extents: ~2700, 170 MB)
	TExtent Store;
	if (!ChunkAlloc ((u32) nBytes, &Store)) return FALSE;
	if (n->nExt > 0) memcpy (Store.p, n->pExt, n->nExt * sizeof (TExtent));
	ChunkFree (n->ExtStore);
	n->ExtStore = Store;
	n->pExt = (TExtent *) Store.p;
	n->nExtCap = Store.nCap / sizeof (TExtent);
	return TRUE;
}

static void FreeData (TNode *n)
{
	for (unsigned i = 0; i < n->nExt; i++) ChunkFree (n->pExt[i]);
	ChunkFree (n->ExtStore);
	n->ExtStore.p = 0; n->ExtStore.pSlab = 0; n->ExtStore.nCap = 0;
	n->pExt = 0; n->nExt = n->nExtCap = 0;
	n->nSize = n->nCapTotal = 0;
	n->nGen++;
}

// The size of the next extent for `want` bytes still to write: exact (a whole write: a page, or
// the biggest chunk that fits, 256 B at least) or a stream's (a whole page; its tail is cut
// exactly when it is closed, Trim).
static u32 NextExtent (u64 nWant, u64 nSize, boolean bExact)
{
	if (nWant >= PAGE_BYTES) return PAGE_BYTES;
	if (bExact)
	{
		u32 n = PAGE_BYTES / 2;
		while (n > CHUNK_MIN && n > nWant) n /= 2;
		return n;
	}
	(void) nSize;
	return PAGE_BYTES;					// (a stream: whole pages, Trim at its close)
}

// (the lock held) nLen bytes at the end of the file -> how many (fewer: the volume is full)
static u64 Append (TNode *n, const u8 *pSrc, u64 nLen, boolean bExact)
{
	u64 nDone = 0;
	while (nDone < nLen && n->nSize < RAMFS_FILE_MAX)
	{
		TExtent *pLast = n->nExt > 0 ? &n->pExt[n->nExt - 1] : 0;
		u64 nFill = pLast != 0 ? n->nSize - (n->nCapTotal - pLast->nCap) : 0;
		if (pLast != 0 && nFill < pLast->nCap)
		{
			u64 k = pLast->nCap - nFill;
			if (k > nLen - nDone) k = nLen - nDone;
			if (k > RAMFS_FILE_MAX - n->nSize) k = RAMFS_FILE_MAX - n->nSize;
			memcpy (pLast->p + nFill, pSrc + nDone, (size_t) k);
			nDone += k; n->nSize += k;
			continue;
		}
		TExtent e;
		if (!ExtReserve (n, n->nExt + 1) || !ChunkAlloc (NextExtent (nLen - nDone, n->nSize, bExact), &e))
			break;
		n->pExt[n->nExt++] = e;
		n->nCapTotal += e.nCap;
	}
	return nDone;
}

static u64 ReadAt (TNode *n, u64 nPos, u8 *pDst, u64 nLen, TCursor *c);

// (the lock held) a stream's file closed: its tail -- the chunks after its last whole page, and
// the page a stream writes into -- cut again exactly (whole pages, then falling powers of two)
static void Trim (TNode *n)
{
	if (n->nExt == 0) return;
	unsigned k = n->nExt;					// the tail: extents k .. nExt - 1
	u64 nStart = n->nCapTotal;
	while (k > 0 && (k == n->nExt || n->pExt[k - 1].pSlab != 0))
	{
		k--;
		nStart -= n->pExt[k].nCap;
	}
	u64 nTail = n->nSize - nStart;
	if (n->nCapTotal - n->nSize < CHUNK_MIN && n->nExt - k <= NCLASS) return;	// (tight already)
	TExtent New[16]; unsigned nNew = 0; u64 nLeft = nTail;
	while (nLeft > 0 && nNew < 16)
	{
		if (!ChunkAlloc (NextExtent (nLeft, 0, TRUE), &New[nNew])) break;
		nLeft -= New[nNew].nCap < nLeft ? New[nNew].nCap : nLeft;
		nNew++;
	}
	if (nLeft > 0 || !ExtReserve (n, k + nNew))
	{
		for (unsigned i = 0; i < nNew; i++) ChunkFree (New[i]);
		return;						// (no room: kept as it is)
	}
	TCursor c = { n->nGen - 1, 0, 0 };
	u64 nOff = 0;
	for (unsigned i = 0; i < nNew; i++)
	{
		u64 m = nTail - nOff < New[i].nCap ? nTail - nOff : New[i].nCap;
		ReadAt (n, nStart + nOff, New[i].p, m, &c);
		nOff += m;
	}
	for (unsigned i = k; i < n->nExt; i++) { n->nCapTotal -= n->pExt[i].nCap; ChunkFree (n->pExt[i]); }
	n->nExt = k;
	for (unsigned i = 0; i < nNew; i++) { n->pExt[n->nExt++] = New[i]; n->nCapTotal += New[i].nCap; }
	n->nGen++;
}

// (the lock held) up to nLen bytes from nPos
static u64 ReadAt (TNode *n, u64 nPos, u8 *pDst, u64 nLen, TCursor *c)
{
	if (nPos >= n->nSize) return 0;
	if (nLen > n->nSize - nPos) nLen = n->nSize - nPos;
	if (c->nGen != n->nGen || c->nIdx >= n->nExt || nPos < c->nStart)
	{
		c->nGen = n->nGen; c->nIdx = 0; c->nStart = 0;
	}
	u64 nDone = 0;
	while (nDone < nLen && c->nIdx < n->nExt)
	{
		const TExtent &e = n->pExt[c->nIdx];
		if (nPos + nDone >= c->nStart + e.nCap) { c->nStart += e.nCap; c->nIdx++; continue; }
		u64 nOff = nPos + nDone - c->nStart;
		u64 k = e.nCap - nOff;
		if (k > nLen - nDone) k = nLen - nDone;
		memcpy (pDst + nDone, e.p + nOff, (size_t) k);
		nDone += k;
	}
	return nDone;
}

// ---- the tree ----------------------------------------------------------------------------------

static inline char Lower (char c) { return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c; }

static boolean NameEq (const char *a, const char *b, unsigned nb)
{
	for (unsigned i = 0; i < nb; i++) if (a[i] == '\0' || Lower (a[i]) != Lower (b[i])) return FALSE;
	return a[nb] == '\0';
}

static TNode *FindChild (TNode *d, const char *pName, unsigned nLen)
{
	for (TNode *c = d->pChild; c != 0; c = c->pNext) if (NameEq (c->Name, pName, nLen)) return c;
	return 0;
}

static void LinkChild (TNode *d, TNode *n)
{
	n->pParent = d; n->pNext = 0;
	TNode **pp = &d->pChild;
	while (*pp != 0) pp = &(*pp)->pNext;
	*pp = n;
}

static void UnlinkChild (TNode *n)
{
	if (n->pParent == 0) return;
	for (TNode **pp = &n->pParent->pChild; *pp != 0; pp = &(*pp)->pNext)
		if (*pp == n) { *pp = n->pNext; break; }
	n->pParent = 0; n->pNext = 0;
}

static TNode *NewNode (TNode *d, const char *pName, unsigned nLen, boolean bDir)
{
	if (nLen == 0 || nLen > RAMFS_NAME_MAX || s_nFiles + s_nDirs >= RAMFS_MAX_NODES || PlatHeapFree () < HEAP_MARGIN)
		return 0;
	TNode *n = new TNode;
	if (n == 0) return 0;
	memset (n, 0, sizeof *n);
	memcpy (n->Name, pName, nLen);
	n->bDir = bDir;
	if (bDir) s_nDirs++; else s_nFiles++;
	LinkChild (d, n);
	return n;
}

static void FreeNode (TNode *n)
{
	FreeData (n);
	if (n->bDir) s_nDirs--; else s_nFiles--;
	delete n;
}

// (the lock held) a node out of the tree; freed now, or at its last close
static void Drop (TNode *n)
{
	UnlinkChild (n);
	n->bUnlinked = TRUE;
	if (n->nRefs == 0) FreeNode (n);
}

static void Release (TNode *n)
{
	if (n->nRefs > 0) n->nRefs--;
	if (n->nRefs == 0 && n->bUnlinked) FreeNode (n);
}

// "RAM:/a/b/c" -> the node of "/a/b/c" (bParent: of "/a/b", *ppLeaf / *pnLeaf "c"), 0 = none.
static TNode *Walk (const char *pAbs, boolean bParent, const char **ppLeaf = 0, unsigned *pnLeaf = 0)
{
	if (!s_bUp || pAbs == 0) return 0;
	const char *p = pAbs + 4;				// (after "RAM:")
	TNode *n = s_pRoot;
	for (;;)
	{
		while (*p == '/') p++;
		if (*p == '\0') return bParent ? 0 : n;		// (the root has no parent)
		const char *s = p;
		while (*p != '\0' && *p != '/') p++;
		unsigned nLen = (unsigned) (p - s);
		const char *q = p;
		while (*q == '/') q++;
		if (bParent && *q == '\0')			// the last name
		{
			if (nLen > RAMFS_NAME_MAX) return 0;
			if (ppLeaf != 0) *ppLeaf = s;
			if (pnLeaf != 0) *pnLeaf = nLen;
			return n;
		}
		if (!n->bDir) return 0;
		n = FindChild (n, s, nLen);
		if (n == 0) return 0;
	}
}

// ---- the API -----------------------------------------------------------------------------------

boolean RamFsMounted (void) { return s_bUp; }

boolean RamFsHandles (const char *pAbs)
{
	return pAbs != 0 && pAbs[0] == 'R' && pAbs[1] == 'A' && pAbs[2] == 'M' && pAbs[3] == ':';
}

void RamFsInit (const char *pConfig)
{
	if (s_bUp) return;
	u64 nFree = PlatPagesFree ();
	u64 nCap = (u64) RAMFS_DEFAULT_MB << 20;
	if (nCap > nFree / 4) nCap = nFree / 4;
	if (pConfig != 0 && *pConfig != '\0')			// "128" (MB), "10%", "0"
	{
		u64 v = 0; const char *p = pConfig;
		while (*p >= '0' && *p <= '9') v = v * 10 + (u64) (*p++ - '0');
		if (p != pConfig)
			nCap = *p == '%' ? nFree / 100 * (v > 90 ? 90 : v) : v << 20;
	}
	if (nFree > RAMFS_RESERVE && nCap > nFree - RAMFS_RESERVE) nCap = nFree - RAMFS_RESERVE;
	if (nCap > RAMFS_FILE_MAX * 64) nCap = RAMFS_FILE_MAX * 64;
	s_nCapPages = nCap / PAGE_BYTES;
	if (s_nCapPages < 16)					// (under 1 MB: none)
	{
		RamLog ("no RAM: volume (ramfs=%s)", pConfig != 0 ? pConfig : "");
		return;
	}
	s_pRoot = new TNode;
	s_pFile = new TRamFile[MAX_FILES];
	s_pDir = new TRamDir[MAX_DIRS];
	if (s_pRoot == 0 || s_pFile == 0 || s_pDir == 0) return;
	memset (s_pRoot, 0, sizeof *s_pRoot);
	s_pRoot->bDir = TRUE;
	memset (s_pFile, 0, sizeof (TRamFile) * MAX_FILES);
	memset (s_pDir, 0, sizeof (TRamDir) * MAX_DIRS);
	s_bUp = TRUE;
	RamLog ("RAM: volume, up to %u MB (%u MB of page memory free)", (unsigned) (s_nCapPages * PAGE_BYTES >> 20),
		(unsigned) (nFree >> 20));
}

// -- read handles

void *RamFsOpen (const char *pAbs)
{
	TGuard G;
	TNode *n = Walk (pAbs, FALSE);
	if (n == 0 || n->bDir) return 0;
	for (unsigned i = 0; i < MAX_FILES; i++)
		if (!s_pFile[i].bUsed)
		{
			TRamFile *f = &s_pFile[i];
			memset (f, 0, sizeof *f);
			f->bUsed = TRUE; f->pNode = n; f->nPid = PlatPid ();
			f->Cur.nGen = n->nGen - 1;			// (walked at the first read)
			n->nRefs++;
			return f;
		}
	return 0;
}

boolean RamFsIsFile (void *pHandle)
{
	return s_pFile != 0 && (TRamFile *) pHandle >= s_pFile && (TRamFile *) pHandle < s_pFile + MAX_FILES;
}

int RamFsRead (void *pHandle, void *pBuf, unsigned nLen)
{
	TRamFile *f = (TRamFile *) pHandle;
	if (!RamFsIsFile (pHandle) || pBuf == 0) return -1;
	u64 nDone = 0;
	for (;;)
	{
		u64 nWant = nLen - nDone > RAMFS_SLICE ? RAMFS_SLICE : nLen - nDone, k;
		{
			TGuard G;
			if (!f->bUsed) return -1;
			k = ReadAt (f->pNode, f->nPos, (u8 *) pBuf + nDone, nWant, &f->Cur);
			f->nPos += k;
		}
		nDone += k;
		if (k < nWant || nDone >= nLen) break;
		PlatYield ();					// (a big read: the others' turn)
	}
	return (int) nDone;
}

u64 RamFsSize (void *pHandle)
{
	TGuard G;
	TRamFile *f = (TRamFile *) pHandle;
	return RamFsIsFile (pHandle) && f->bUsed ? f->pNode->nSize : 0;
}

int RamFsSeek (void *pHandle, u64 nPos)
{
	TGuard G;
	TRamFile *f = (TRamFile *) pHandle;
	if (!RamFsIsFile (pHandle) || !f->bUsed) return -1;
	f->nPos = nPos;						// (past the end: reads give 0)
	return 0;
}

void RamFsClose (void *pHandle)
{
	TGuard G;
	TRamFile *f = (TRamFile *) pHandle;
	if (!RamFsIsFile (pHandle) || !f->bUsed) return;
	f->bUsed = FALSE;
	Release (f->pNode);
}

// -- a file created / truncated, nothing in it yet; pinned (nRefs) -> 0: not possible
static TNode *CreateFile (const char *pAbs, boolean bTruncate)
{
	const char *pLeaf; unsigned nLeaf;
	TNode *d = Walk (pAbs, TRUE, &pLeaf, &nLeaf);
	if (d == 0 || !d->bDir) return 0;
	TNode *n = FindChild (d, pLeaf, nLeaf);
	if (n == 0) n = NewNode (d, pLeaf, nLeaf, FALSE);
	else if (n->bDir) return 0;
	else if (bTruncate) FreeData (n);
	if (n != 0) n->nRefs++;
	return n;
}

int RamFsSave (const char *pAbs, const void *pBuf, unsigned nLen)
{
	if (pBuf == 0 && nLen > 0) return -1;
	TNode *n;
	{
		TGuard G;
		n = CreateFile (pAbs, TRUE);
		if (n == 0) return -1;
		// (too big for the room left -- the file's old bytes are free already: fail now)
		u64 nRoom = (s_nCapPages - s_nPages) * PAGE_BYTES, nFree = PlatPagesFree ();
		nFree = nFree > RAMFS_RESERVE ? nFree - RAMFS_RESERVE : 0;
		if (nFree < nRoom) nRoom = nFree;
		if (nLen > PAGE_BYTES && (u64) nLen > nRoom + PAGE_BYTES)
		{
			Drop (n); Release (n);
			return -1;
		}
	}
	PlatNoKill (TRUE);					// (killed between two slices: once done)
	u64 nDone = 0;
	boolean bOK = TRUE;
	while (nDone < nLen)
	{
		u64 nWant = nLen - nDone > RAMFS_SLICE ? RAMFS_SLICE : nLen - nDone, k;
		{
			TGuard G;
			k = Append (n, (const u8 *) pBuf + nDone, nWant, TRUE);
		}
		nDone += k;
		if (k < nWant) { bOK = FALSE; break; }		// (the volume is full)
		if (nDone < nLen) PlatYield ();
	}
	{
		TGuard G;
		if (!bOK && !n->bUnlinked) Drop (n);		// (no half file left)
		Release (n);
	}
	PlatNoKill (FALSE);
	return bOK ? (int) nLen : -1;
}

// -- folders

void *RamFsOpenDir (const char *pAbs)
{
	TGuard G;
	TNode *d = Walk (pAbs, FALSE);
	if (d == 0 || !d->bDir) return 0;
	unsigned nSlot = MAX_DIRS;
	for (unsigned i = 0; i < MAX_DIRS && nSlot == MAX_DIRS; i++) if (!s_pDir[i].bUsed) nSlot = i;
	if (nSlot == MAX_DIRS) return 0;
	unsigned nLen = 0;					// (a copy: u32 size, u8 is_dir, name '\0')
	for (TNode *c = d->pChild; c != 0; c = c->pNext) nLen += 5 + (unsigned) strlen (c->Name) + 1;
	if (PlatHeapFree () < HEAP_MARGIN + nLen) return 0;
	u8 *pList = new u8[nLen > 0 ? nLen : 1];
	if (pList == 0) return 0;
	unsigned o = 0;
	for (TNode *c = d->pChild; c != 0; c = c->pNext)
	{
		u32 nSize = c->bDir ? 0 : (c->nSize > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32) c->nSize);
		memcpy (pList + o, &nSize, 4); pList[o + 4] = c->bDir ? 1 : 0;
		unsigned k = (unsigned) strlen (c->Name) + 1;
		memcpy (pList + o + 5, c->Name, k);
		o += 5 + k;
	}
	TRamDir *r = &s_pDir[nSlot];
	r->pList = pList; r->nLen = nLen; r->nPos = 0; r->nPid = PlatPid (); r->bUsed = TRUE;
	return r;
}

boolean RamFsIsDir (void *pHandle)
{
	return s_pDir != 0 && (TRamDir *) pHandle >= s_pDir && (TRamDir *) pHandle < s_pDir + MAX_DIRS;
}

int RamFsReadDir (void *pHandle, struct kapi_dirent *pEnt)
{
	TGuard G;
	TRamDir *r = (TRamDir *) pHandle;
	if (!RamFsIsDir (pHandle) || !r->bUsed || pEnt == 0 || r->nPos + 5 > r->nLen) return 0;
	u8 *p = r->pList + r->nPos;
	u32 nSize; memcpy (&nSize, p, 4);
	const char *pName = (const char *) p + 5;
	unsigned k = (unsigned) strlen (pName);
	unsigned c = k < sizeof pEnt->name - 1 ? k : sizeof pEnt->name - 1;
	memcpy (pEnt->name, pName, c); pEnt->name[c] = '\0';
	pEnt->size = nSize; pEnt->is_dir = p[4];
	r->nPos += 5 + k + 1;
	return 1;
}

void RamFsCloseDir (void *pHandle)
{
	TGuard G;
	TRamDir *r = (TRamDir *) pHandle;
	if (!RamFsIsDir (pHandle) || !r->bUsed) return;
	delete [] r->pList;
	r->pList = 0; r->bUsed = FALSE;
}

int RamFsMkdir (const char *pAbs)
{
	TGuard G;
	const char *pLeaf; unsigned nLeaf;
	TNode *d = Walk (pAbs, TRUE, &pLeaf, &nLeaf);
	if (d == 0 || !d->bDir || FindChild (d, pLeaf, nLeaf) != 0) return -1;
	return NewNode (d, pLeaf, nLeaf, TRUE) != 0 ? 0 : -1;
}

int RamFsRemove (const char *pAbs)
{
	TGuard G;
	TNode *n = Walk (pAbs, FALSE);
	if (n == 0 || n == s_pRoot || (n->bDir && n->pChild != 0)) return -1;
	Drop (n);
	return 0;
}

int RamFsRename (const char *pFrom, const char *pTo)
{
	TGuard G;
	TNode *n = Walk (pFrom, FALSE);
	const char *pLeaf; unsigned nLeaf;
	TNode *d = Walk (pTo, TRUE, &pLeaf, &nLeaf);
	if (n == 0 || n == s_pRoot || d == 0 || !d->bDir || nLeaf == 0) return -1;
	TNode *pOld = FindChild (d, pLeaf, nLeaf);
	if (pOld != 0 && pOld != n) return -1;			// (there already: as FatFs)
	for (TNode *a = d; a != 0; a = a->pParent) if (a == n) return -1;	// (into itself)
	if (d != n->pParent) { UnlinkChild (n); LinkChild (d, n); }
	memset (n->Name, 0, sizeof n->Name);
	memcpy (n->Name, pLeaf, nLeaf);
	return 0;
}

boolean RamFsIsDirPath (const char *pAbs)
{
	TGuard G;
	TNode *n = Walk (pAbs, FALSE);
	return n != 0 && n->bDir;
}

// -- streams

void *RamFsStreamOpen (const char *pAbs, int nMode)
{
	TGuard G;
	TNode *n;
	if (nMode == 0)
	{
		n = Walk (pAbs, FALSE);
		if (n == 0 || n->bDir) return 0;
		n->nRefs++;
	}
	else if ((n = CreateFile (pAbs, nMode == 1)) == 0) return 0;
	TRamStream *s = new TRamStream;
	if (s == 0) { Release (n); return 0; }
	memset (s, 0, sizeof *s);
	s->pNode = n; s->nMode = nMode; s->Cur.nGen = n->nGen - 1;
	return s;
}

int RamFsStreamRead (void *pStream, void *pBuf, unsigned nLen)
{
	TRamStream *s = (TRamStream *) pStream;
	if (s == 0 || pBuf == 0) return -1;
	u64 nDone = 0;
	for (;;)
	{
		u64 nWant = nLen - nDone > RAMFS_SLICE ? RAMFS_SLICE : nLen - nDone, k;
		{
			TGuard G;
			k = ReadAt (s->pNode, s->nPos, (u8 *) pBuf + nDone, nWant, &s->Cur);
			s->nPos += k;
		}
		nDone += k;
		if (k < nWant || nDone >= nLen) break;
		PlatYield ();
	}
	return (int) nDone;
}

int RamFsStreamWrite (void *pStream, const void *pBuf, unsigned nLen)
{
	TRamStream *s = (TRamStream *) pStream;
	if (s == 0 || s->nMode == 0 || (pBuf == 0 && nLen > 0)) return -1;
	u64 nDone = 0;
	for (;;)
	{
		u64 nWant = nLen - nDone > RAMFS_SLICE ? RAMFS_SLICE : nLen - nDone, k;
		{
			TGuard G;
			k = Append (s->pNode, (const u8 *) pBuf + nDone, nWant, FALSE);
		}
		nDone += k;
		if (k < nWant || nDone >= nLen) break;
		PlatYield ();
	}
	return nDone == 0 && nLen > 0 ? -1 : (int) nDone;
}

void RamFsStreamClose (void *pStream)
{
	TRamStream *s = (TRamStream *) pStream;
	if (s == 0) return;
	{
		TGuard G;
		if (s->nMode != 0) Trim (s->pNode);
		Release (s->pNode);
	}
	delete s;
}

// -- the volume

void RamFsInfo (u64 *pTotal, u64 *pUsed, u64 *pFree, unsigned *pFiles, unsigned *pDirs)
{
	TGuard G;
	u64 nTotal = s_nCapPages * PAGE_BYTES, nUsed = s_nPages * PAGE_BYTES;
	u64 nFree = nTotal - nUsed, nPages = PlatPagesFree ();
	nPages = nPages > RAMFS_RESERVE ? nPages - RAMFS_RESERVE : 0;
	if (nFree > nPages) nFree = nPages / PAGE_BYTES * PAGE_BYTES;
	if (pTotal != 0) *pTotal = nTotal;
	if (pUsed != 0) *pUsed = nUsed;
	if (pFree != 0) *pFree = s_bUp ? nFree : 0;
	if (pFiles != 0) *pFiles = s_nFiles;
	if (pDirs != 0) *pDirs = s_nDirs;
}

void RamFsOnProcessGone (unsigned nPid)
{
	if (!s_bUp || nPid == 0) return;
	TGuard G;
	for (unsigned i = 0; i < MAX_FILES; i++)
		if (s_pFile[i].bUsed && s_pFile[i].nPid == nPid)
		{
			s_pFile[i].bUsed = FALSE;
			Release (s_pFile[i].pNode);
		}
	for (unsigned i = 0; i < MAX_DIRS; i++)
		if (s_pDir[i].bUsed && s_pDir[i].nPid == nPid)
		{
			delete [] s_pDir[i].pList;
			s_pDir[i].pList = 0; s_pDir[i].bUsed = FALSE;
		}
}
