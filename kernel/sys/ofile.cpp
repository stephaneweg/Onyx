//
// ofile.cpp -- open files with POSIX semantics (HANDLE_OFILE): file_open / read / write / seek /
// truncate / sync / stat / close, path_stat / unlink / mkdir / rename / utime, dir_read,
// stream_write_nb (kapi v75 slots 207..221, docs/POSIX-PLAN.md §3.2, docs/02 §8 "v75: files and
// processes").
//
// The model:
//  - A FatFs file open through file_open has ONE node (TFNode), whatever the number of its
//    openers: one FIL, opened FA_READ, re-opened FA_READ | FA_WRITE when its first writer comes
//    (FatFs keeps no share locks, FF_FS_LOCK 0: two FILs writing one file would corrupt it, and
//    their sector buffers would not see each other's writes). Its key is its absolute path
//    (ResolvePath's), compared case-insensitively (FAT is).
//  - An open-file description (TOFile, the HANDLE_OFILE object) holds a node and its own 64-bit
//    offset, its access mode and O_APPEND; dup is user-space (libc counts its references).
//  - Each call that uses a node's FIL holds the node's lock across the call (FatFs calls yield
//    while the card works: OnyxDriverWait), so its f_lseek + f_read / f_write are not mixed with
//    another description's. The node table has a lock of its own (open, close, unlink, rename);
//    the order is table, then node -- never the other way round, so no deadlock. Both are
//    sleeping locks (the waiters yield, as sys/fslock.cpp's), and a call is in a no-kill
//    section from its start to its end (killed meanwhile, the task ends once it holds nothing).
//  - Unlink of an open file: the file is moved to <volume>:/.~onyx-deleted/<n> (hidden) and
//    deleted at its last close; a rename onto an open file hides the target the same way. A
//    file renamed (or hidden) while open has its FIL closed, the entry moved, then re-opened at
//    its new path: FatFs keeps the location of a file's directory entry in its FIL (dir_sect,
//    or exFAT's c_scl / c_ofs) and would write the size and time into the old one. Leftovers
//    (a crash before the last close) are removed at boot (OFileBootCleanup), or on a volume
//    mounted later the first time a file there is hidden.
//  - RAM: files go to sys/ramfs.cpp's node calls (RamFsNodeOpen...: the same semantics); a
//    provider's path (FTP:...) gets -ENOTSUP for file_open / path_stat / path_utime.
//  - The old kapis (open / read / save_file / file_in / file_out) keep their own FILs: mixing
//    them with file_* on one file is not coherent (docs/02).
//  - A process's descriptions still open at its end are closed by its teardown: queued there
//    (nothing may wait in a teardown), closed by the reaper (OFileRunDeferred).
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
#include <kern/ofile.h>
#include <kern/lsock.h>		// OFileRef (v76)
#include <kern/handle.h>
#include <kern/stream.h>
#include <kern/ramfs.h>
#include <kern/vfs.h>
#include <kern/volume.h>		// (v92) OFileVolume: an eject, a stick pulled out
#include <kern/uaccess.h>
#include <kern/kapi_abi.h>
#include <kern/image.h>			// (v77) ImageFileChanged: a program file removed, renamed, written
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/util.h>
#include <circle/new.h>
#include <fatfs/ff.h>

#define OF_PATH		300			// a resolved path (ResolvePath's buffers)
#define OF_MAGIC	0x4F46494Cu		// "OFIL": a TOFile
#define OF_IO_MAX	0x40000000ull		// one read / write moves at most 1 GB (a short count)
#define OF_ZERO		(64 * 1024)		// the zeros written into a gap, per piece
#define HIDE_DIR	".~onyx-deleted"	// <volume>:/.~onyx-deleted/<n>: unlinked while open

enum { OF_FAT = 0, OF_RAM = 1 };

static const char *const s_Volumes[] = { FF_VOLUME_STRS };	// "SD", "SD1"... (ffconf.h)
#define NVOLUMES	(sizeof s_Volumes / sizeof s_Volumes[0])

// ---- small helpers -----------------------------------------------------------------------------

static inline char Upper (char c) { return c >= 'a' && c <= 'z' ? (char) (c - 32) : c; }

static boolean SameName (const char *a, const char *b)	// case-insensitive (FAT)
{
	for (; *a != '\0' && *b != '\0'; a++, b++) if (Upper (*a) != Upper (*b)) return FALSE;
	return *a == *b;
}

static void Copy (char *pDst, const char *pSrc, unsigned nCap)
{
	unsigned i = 0;
	for (; pSrc[i] != '\0' && i < nCap - 1; i++) pDst[i] = pSrc[i];
	pDst[i] = '\0';
}

// FNV-1a 64 of the upper-cased string, continued from h (stat's ino)
#define FNV_BASIS	0xCBF29CE484222325ull
static u64 Fnv (u64 h, const char *p)
{
	for (; *p != '\0'; p++) { h ^= (u8) Upper (*p); h *= 0x100000001B3ull; }
	return h;
}

// The FatFs volume of an absolute path ("SD1:/x" -> 1), -1 if none
static int VolumeOf (const char *pAbs)
{
	unsigned n = 0;
	while (pAbs[n] != '\0' && pAbs[n] != ':' && pAbs[n] != '/') n++;
	if (pAbs[n] != ':') return -1;
	for (unsigned v = 0; v < NVOLUMES; v++)
	{
		const char *s = s_Volumes[v];
		unsigned k = 0;
		while (k < n && s[k] != '\0' && Upper (s[k]) == Upper (pAbs[k])) k++;
		if (k == n && s[k] == '\0') return (int) v;
	}
	return -1;
}

static boolean IsRoot (const char *pAbs)		// "SD:/"
{
	const char *p = pAbs;
	while (*p != '\0' && *p != ':') p++;
	return *p == ':' && (p[1] == '\0' || (p[1] == '/' && p[2] == '\0'));
}

static int FatErr (FRESULT r)
{
	switch (r)
	{
	case FR_OK:			return 0;
	case FR_NO_FILE:
	case FR_NO_PATH:		return -KAPI_ENOENT;
	case FR_EXIST:			return -KAPI_EEXIST;
	case FR_DENIED:			return -KAPI_EACCES;
	case FR_WRITE_PROTECTED:	return -KAPI_EROFS;
	case FR_INVALID_NAME:		return -KAPI_EINVAL;
	case FR_LOCKED:			return -KAPI_EBUSY;
	case FR_TOO_MANY_OPEN_FILES:	return -KAPI_EMFILE;
	case FR_NOT_ENOUGH_CORE:	return -KAPI_ENOMEM;
	case FR_INVALID_DRIVE:
	case FR_NOT_ENABLED:
	case FR_NO_FILESYSTEM:		return -KAPI_ENODEV;
	case FR_INVALID_OBJECT:		return -KAPI_EBADF;
	case FR_INVALID_PARAMETER:	return -KAPI_EINVAL;
	default:			return -KAPI_EIO;	// DISK_ERR, INT_ERR, NOT_READY, TIMEOUT...
	}
}

// A name over 255 characters: -ENAMETOOLONG, not FatFs' INVALID_NAME
static int NameErr (const char *pAbs, FRESULT r)
{
	if (r == FR_INVALID_NAME)
	{
		unsigned n = 0;
		for (const char *p = pAbs; *p != '\0'; p++) { n = *p == '/' ? 0 : n + 1; if (n > FF_MAX_LFN) return -KAPI_ENAMETOOLONG; }
	}
	return FatErr (r);
}

// ---- time: FAT's local date and time <-> UTC seconds ----------------------------------------------

static s64 DaysFromCivil (int y, unsigned m, unsigned d)	// (H. Hinnant's algorithm)
{
	y -= m <= 2;
	const int era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = (unsigned) (y - era * 400);
	const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return (s64) era * 146097 + (s64) doe - 719468;
}

static int TimeZoneSeconds (void)
{
	return CTimer::Get () != 0 ? CTimer::Get ()->GetTimeZone () * 60 : 0;
}

static s64 FatToUtc (WORD fdate, WORD ftime)
{
	if (fdate == 0) return 0;
	s64 nDays = DaysFromCivil (1980 + (fdate >> 9), (fdate >> 5) & 15, fdate & 31);
	s64 t = nDays * 86400 + (ftime >> 11) * 3600 + ((ftime >> 5) & 63) * 60 + (ftime & 31) * 2;
	return t - TimeZoneSeconds ();
}

static void UtcToFat (s64 t, WORD *pDate, WORD *pTime)
{
	t += TimeZoneSeconds ();
	if (t < 315532800) t = 315532800;			// (1980-01-01: FAT's first day)
	s64 z = t / 86400 + 719468;				// (civil from days)
	unsigned nSec = (unsigned) (t % 86400);
	const s64 era = z / 146097;
	const unsigned doe = (unsigned) (z - era * 146097);
	const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const unsigned mp = (5 * doy + 2) / 153;
	const unsigned d = doy - (153 * mp + 2) / 5 + 1;
	const unsigned m = mp < 10 ? mp + 3 : mp - 9;
	s64 y = (s64) yoe + era * 400 + (m <= 2);
	if (y > 2107) y = 2107;
	*pDate = (WORD) (((y - 1980) << 9) | (m << 5) | d);
	*pTime = (WORD) (((nSec / 3600) << 11) | (((nSec / 60) % 60) << 5) | ((nSec % 60) / 2));
}

static s64 NowUtc (void)
{
	return CTimer::Get () != 0 ? (s64) CTimer::Get ()->GetUniversalTime () : 0;
}

// ---- locks and no-kill sections ------------------------------------------------------------------

static CTask *Me (void)
{
	CTask *p = CScheduler::IsActive () ? CScheduler::Get ()->GetCurrentTask () : 0;
	return p != 0 ? p : (CTask *) 1;			// (boot: one flow)
}

static void Yield (void)
{
	if (CScheduler::IsActive ()) CScheduler::Get ()->Yield ();
}

struct TSLock					// re-entrant, its waiters yield
{
	CTask *volatile pOwner;
	unsigned	nDepth;
};

static void LockTake (TSLock *l)
{
	CTask *pMe = Me ();
	for (;;)
	{
		if (l->pOwner == 0) { l->pOwner = pMe; l->nDepth = 1; return; }	// (core 0 only, not preempted)
		if (l->pOwner == pMe) { l->nDepth++; return; }
		Yield ();
	}
}

static void LockGive (TSLock *l)
{
	if (l->pOwner == Me () && l->nDepth > 0 && --l->nDepth == 0) l->pOwner = 0;
}

struct TLockGuard
{
	TSLock *m_p;
	TLockGuard (TSLock *p) : m_p (p) { LockTake (p); }
	~TLockGuard (void) { LockGive (m_p); }
};

// A call from its start to its end: killed meanwhile, the task ends at the end (holding nothing).
// Declared first in a call, so that it is destroyed last.
class CNoKill
{
public:
	CNoKill (void)	{ if (CScheduler::IsActive ()) CScheduler::Get ()->EnterNoKill (); }
	~CNoKill (void)	{ if (CScheduler::IsActive ()) CScheduler::Get ()->LeaveNoKill (); }
};

// ---- the nodes (FatFs) and the descriptions ------------------------------------------------------

struct TFNode
{
	char	 Path[OF_PATH];			// its absolute path (the hidden one once unlinked)
	FIL	*pFile;				// 0: lost (a re-open failed) -- every call -EIO
	boolean	 bWritable;			// pFile opened FA_READ | FA_WRITE
	boolean	 bDeleteOnClose;		// unlinked while open: deleted at the last close
	boolean	 bDirty;			// written since its last sync: nMTime is its time
	s64	 nMTime;
	unsigned nRefs;				// its descriptions
	TSLock	 Lock;
	TFNode	*pNext;
};

struct TOFile
{
	u32	 nMagic;
	u8	 nKind;				// OF_FAT, OF_RAM
	u8	 nAccess;			// KAPI_O_RDONLY / WRONLY / RDWR
	boolean	 bAppend;
	TFNode	*pNode;				// OF_FAT
	void	*pRam;				// OF_RAM: its RAM: node
	u64	 nOffset;
	TOFile	*pNextDeferred;
	unsigned nHolders;			// (v76) handles and carried handles naming it (OFileRef)
};

static TSLock	 s_Table;			// the node list, open / close / unlink / rename
static TFNode	*s_pNodes;
static TOFile	*s_pDeferred;			// closes a teardown queued
static u8	*s_pZero;			// OF_ZERO zeros (made at the first gap)
static unsigned	 s_nHidden;			// the hidden names' counter
static u32	 s_nCleaned;			// the volumes whose hidden folder was emptied this boot

static TFNode *FindNode (const char *pAbs)	// (the table locked)
{
	for (TFNode *n = s_pNodes; n != 0; n = n->pNext) if (SameName (n->Path, pAbs)) return n;
	return 0;
}

static boolean IsExFat (const FIL *f)
{
	return f->obj.fs != 0 && f->obj.fs->fs_type == FS_EXFAT;
}

static u64 MaxSize (const FIL *f)		// FAT32: 4 GB - 1
{
	return IsExFat (f) ? (1ull << 62) : 0xFFFFFFFFull;
}

// (the node locked) the FIL (re-)opened at n->Path, as it was (read, or read + write)
static FRESULT NodeReopen (TFNode *n)
{
	if (n->pFile == 0 || n->Path[0] == '\0')
	{
		return FR_INVALID_OBJECT;		// (lost -- its volume unmounted, v92 -- it stays lost)
	}
	memset (n->pFile, 0, sizeof (FIL));
	FRESULT r = f_open (n->pFile, n->Path, FA_READ | (n->bWritable ? FA_WRITE : 0) | FA_OPEN_EXISTING);
	if (r != FR_OK)
	{
		delete n->pFile;
		n->pFile = 0;
	}
	return r;
}

// (the node locked) its FIL now read + write
static int NodeMakeWritable (TFNode *n)
{
	if (n->bWritable) return 0;
	if (n->pFile == 0) return -KAPI_EIO;
	FIL *p = new FIL;
	if (p == 0) return -KAPI_ENOMEM;
	memset (p, 0, sizeof (FIL));
	FRESULT r = f_open (p, n->Path, FA_READ | FA_WRITE | FA_OPEN_EXISTING);
	if (r != FR_OK)
	{
		delete p;
		return FatErr (r);			// (DENIED: a read-only file -> -EACCES)
	}
	f_close (n->pFile);				// (read only: nothing to flush)
	delete n->pFile;
	n->pFile = p;
	n->bWritable = TRUE;
	return 0;
}

static int WriteZeros (FIL *f, u64 nLen)		// at f's pointer -> 0 / -errno
{
	if (s_pZero == 0)
	{
		s_pZero = new u8[OF_ZERO];
		if (s_pZero == 0) return -KAPI_ENOMEM;
		memset (s_pZero, 0, OF_ZERO);
	}
	while (nLen > 0)
	{
		UINT k = nLen > OF_ZERO ? OF_ZERO : (UINT) nLen, w = 0;
		FRESULT r = f_write (f, s_pZero, k, &w);
		if (r != FR_OK) return FatErr (r);
		if (w < k) return -KAPI_ENOSPC;
		nLen -= w;
		if (nLen > 0) Yield ();
	}
	return 0;
}

// ---- the hidden folder (unlinked while open) -----------------------------------------------------

static void HideDirOf (const char *pAbs, char *pOut, unsigned nCap)	// "SD1:/x" -> "SD1:/.~onyx-deleted"
{
	unsigned o = 0;
	for (const char *p = pAbs; *p != '\0' && *p != ':' && o < nCap - 1; p++) pOut[o++] = *p;
	for (const char *p = ":/" HIDE_DIR; *p != '\0' && o < nCap - 1; p++) pOut[o++] = *p;
	pOut[o] = '\0';
}

// Every file in the volume's hidden folder deleted, then the folder (FatFs; no node there).
static void CleanHidden (const char *pDir)
{
	for (unsigned nGuard = 0; nGuard < 100000; nGuard++)
	{
		DIR Dir;
		FILINFO Info;
		if (f_opendir (&Dir, pDir) != FR_OK) return;
		FRESULT r = f_readdir (&Dir, &Info);
		f_closedir (&Dir);
		if (r != FR_OK || Info.fname[0] == '\0') break;
		char Path[OF_PATH];
		Copy (Path, pDir, sizeof Path);
		unsigned n = strlen (Path);
		if (n + 2 + strlen (Info.fname) >= sizeof Path) break;
		Path[n++] = '/';
		Copy (Path + n, Info.fname, sizeof Path - n);
		if (f_unlink (Path) != FR_OK) break;
	}
	f_unlink (pDir);				// (empty now)
}

void OFileBootCleanup (void)
{
	CNoKill NK;
	TLockGuard G (&s_Table);
	for (unsigned v = 0; v < 4 && v < NVOLUMES; v++)	// SD:, SD1:..SD3: (the card's)
	{
		char Dir[OF_PATH];
		HideDirOf (s_Volumes[v], Dir, sizeof Dir);
		FILINFO Info;
		if (f_stat (Dir, &Info) == FR_OK)
		{
			CLogger::Get ()->Write ("ofile", LogNotice, "removing %s (files unlinked while open)", Dir);
			CleanHidden (Dir);
		}
		s_nCleaned |= 1u << v;
	}
}

// (the table and the node locked) the node's file moved to the hidden folder, deleted at its
// last close
static int NodeHide (TFNode *n)
{
	int v = VolumeOf (n->Path);
	if (v < 0) return -KAPI_EINVAL;
	char Dir[OF_PATH], Hidden[OF_PATH];
	HideDirOf (n->Path, Dir, sizeof Dir);
	if (!(s_nCleaned & (1u << v)))			// (a volume mounted after the boot)
	{
		CleanHidden (Dir);
		s_nCleaned |= 1u << v;
	}
	FRESULT r = f_mkdir (Dir);
	if (r == FR_OK) f_chmod (Dir, AM_HID, AM_HID);
	else if (r != FR_EXIST) return FatErr (r);
	FILINFO Info;
	for (;;)
	{
		unsigned o = strlen (Dir);
		Copy (Hidden, Dir, sizeof Hidden);
		Hidden[o++] = '/';
		unsigned x = ++s_nHidden;
		char Num[12]; int k = 0;
		do { Num[k++] = "0123456789abcdef"[x & 15]; x >>= 4; } while (x != 0);
		while (k > 0 && o < sizeof Hidden - 1) Hidden[o++] = Num[--k];
		Hidden[o] = '\0';
		if (f_stat (Hidden, &Info) != FR_OK) break;	// (a free name)
	}
	if (n->pFile != 0) f_close (n->pFile);		// (flushed: the entry is moved next)
	r = f_rename (n->Path, Hidden);
	if (r == FR_OK) Copy (n->Path, Hidden, sizeof n->Path);
	FRESULT r2 = NodeReopen (n);
	if (r2 != FR_OK)
	{
		CLogger::Get ()->Write ("ofile", LogWarning, "%s: cannot re-open (%d)", n->Path, r2);
	}
	if (r != FR_OK) return FatErr (r);
	n->bDeleteOnClose = TRUE;
	return 0;
}

// ---- open, close -----------------------------------------------------------------------------------

static void CloseNow (TOFile *of)
{
	CNoKill NK;
	if (of->nKind == OF_RAM)
	{
		RamFsNodeClose (of->pRam);
	}
	else
	{
		TLockGuard G (&s_Table);
		TFNode *n = of->pNode;
		if (n != 0 && n->nRefs > 0 && --n->nRefs == 0)
		{
			{
				TLockGuard GN (&n->Lock);		// (nobody else uses it now)
				if (n->pFile != 0)
				{
					f_close (n->pFile);		// (flushes what was written)
					delete n->pFile;
					n->pFile = 0;
				}
				if (n->bDeleteOnClose)
				{
					f_unlink (n->Path);
					char Dir[OF_PATH];
					HideDirOf (n->Path, Dir, sizeof Dir);
					f_unlink (Dir);			// (if it is empty now)
				}
			}
			// (v77) a written file closed: an image made from it while it was written is stale
			if (n->bWritable) ImageFileChanged (n->Path);
			for (TFNode **pp = &s_pNodes; *pp != 0; pp = &(*pp)->pNext)
			{
				if (*pp == n) { *pp = n->pNext; break; }
			}
			delete n;
		}
	}
	of->nMagic = 0;
	delete of;
}

void OFileClose (void *pObj, boolean bTeardown)
{
	TOFile *of = (TOFile *) pObj;
	if (of == 0 || of->nMagic != OF_MAGIC)
	{
		return;
	}
	if (of->nHolders > 1)				// (v76: another process still holds it)
	{
		of->nHolders--;
		return;
	}
	if (bTeardown)					// (nothing may wait here: the reaper closes it)
	{
		of->pNextDeferred = s_pDeferred;
		s_pDeferred = of;
		return;
	}
	CloseNow (of);
}

// (v76, kern/lsock.h) One more holder: a handle carried to another process names the same
// description (its offset shared, as a dup'd descriptor).
void OFileRef (void *pObj)
{
	TOFile *of = (TOFile *) pObj;
	if (of != 0 && of->nMagic == OF_MAGIC) of->nHolders++;
}

void OFileRunDeferred (void)
{
	while (s_pDeferred != 0)
	{
		TOFile *of = s_pDeferred;			// (one at a time: a close may yield)
		s_pDeferred = of->pNextDeferred;
		CloseNow (of);
	}
}

// ---- FatFs stat ------------------------------------------------------------------------------------

static unsigned s_nBlk[NVOLUMES];		// a volume's cluster size (bytes), 0 = not known yet

static unsigned ClusterBytes (int v, const FATFS *pFs)
{
	if (v < 0 || v >= (int) NVOLUMES) return 512;
	if (pFs != 0 && pFs->csize != 0) s_nBlk[v] = pFs->csize * FF_MAX_SS;
	if (s_nBlk[v] == 0)
	{
		char Root[16];
		unsigned o = 0;
		for (const char *p = s_Volumes[v]; *p != '\0' && o < 12; p++) Root[o++] = *p;
		Root[o++] = ':'; Root[o++] = '/'; Root[o] = '\0';
		DIR Dir;
		if (f_opendir (&Dir, Root) == FR_OK)
		{
			if (Dir.obj.fs != 0) s_nBlk[v] = Dir.obj.fs->csize * FF_MAX_SS;
			f_closedir (&Dir);
		}
	}
	return s_nBlk[v] != 0 ? s_nBlk[v] : 512;
}

// (v92, kern/volume.h) The open files of FatFs volume nVol: counted, synced, or dropped (an eject, a
// format, a stick pulled out: each FIL closed -- flushed if the device is there -- and the node lost:
// every later call on it -EIO; the descriptions stay until the program closes them).
int OFileVolume (int nVol, int nOp)
{
	CNoKill NK;
	TLockGuard G (&s_Table);
	int n = 0;
	for (TFNode *nd = s_pNodes; nd != 0; nd = nd->pNext)
	{
		if (VolumeOf (nd->Path) != nVol) continue;
		if (nOp == OFV_COUNT) { n++; continue; }
		TLockGuard GN (&nd->Lock);
		if (nd->pFile == 0) continue;
		if (nOp == OFV_SYNC)
		{
			if (!nd->bWritable) continue;
			if (f_sync (nd->pFile) == FR_OK) nd->bDirty = FALSE;
			else n++;
		}
		else
		{
			f_close (nd->pFile);
			delete nd->pFile;
			nd->pFile = 0;
			nd->bDeleteOnClose = FALSE;		// (its hidden file is on that volume: gone with it)
			nd->Path[0] = '\0';			// (no later open finds it: a new stick, a new node)
		}
	}
	if (nOp == OFV_DROP && nVol >= 0 && nVol < (int) NVOLUMES) s_nBlk[nVol] = 0;	// (a format may change it)
	return n;
}

static void FillFat (struct kapi_stat *st, const char *pAbs, u64 nSize, BYTE nAttr, s64 nMTime, const FATFS *pFs)
{
	memset (st, 0, sizeof *st);
	int v = VolumeOf (pAbs);
	boolean bDir = (nAttr & AM_DIR) != 0;
	st->size = bDir ? 0 : nSize;
	st->mtime = st->ctime = nMTime;
	st->ino = Fnv (FNV_BASIS, pAbs);
	st->mode = (bDir ? KAPI_S_IFDIR | 0755 : KAPI_S_IFREG | 0644) & ((nAttr & AM_RDO) ? ~0222u : ~0u);
	st->dev = v >= 0 ? (unsigned) v + 1 : 0;
	st->blksize = ClusterBytes (v, pFs);
	st->attr = nAttr;
	st->blocks = (st->size + st->blksize - 1) / st->blksize * st->blksize / 512;
}

// (the node locked) an open file's stat: its FIL's size, its entry's attributes and time (the
// time of its last write while it has not been synced)
static int NodeStat (TFNode *n, struct kapi_stat *st)
{
	if (n->pFile == 0) return -KAPI_EIO;
	FILINFO Info;
	BYTE nAttr = AM_ARC;
	s64 nMTime = n->nMTime;
	if (f_stat (n->Path, &Info) == FR_OK)
	{
		nAttr = Info.fattrib;
		if (!n->bDirty) nMTime = FatToUtc (Info.fdate, Info.ftime);
	}
	FillFat (st, n->Path, f_size (n->pFile), nAttr, nMTime, n->pFile->obj.fs);
	if (n->bDeleteOnClose) st->ino = Fnv (FNV_BASIS, n->Path);	// (the hidden name's)
	return 0;
}

// ---- the app's arguments -------------------------------------------------------------------------

static void *HandleOf (long long h)
{
	return h > 0 && h <= 0xFFFFFF ? (void *) (uintptr) h : (void *) (uintptr) 0x1000000;	// (never a handle)
}

// ---- the kapis ----------------------------------------------------------------------------------

int UserPathErr (const char *pUser)
{
	u64 n = pUser != 0 ? UserRangeAvail (pUser) : 0;
	if (n == 0) return -KAPI_EFAULT;
	if (n > UPATH_MAX) n = UPATH_MAX;
	long k = UAccessStrLen (pUser, n);
	return k >= UPATH_MAX ? -KAPI_ENAMETOOLONG : -KAPI_EFAULT;	// (no NUL in UPATH_MAX: too long)
}

extern "C" {

long long kapi_file_open (const char *pUserPath, unsigned nFlags, unsigned nMode)
{
	(void) nMode;					// (FAT has no permissions: the read-only bit is kept)
	CNoKill NK;
	CUserStr Path (pUserPath, UPATH_MAX);
	if (!Path.OK ()) return UserPathErr (pUserPath);
	if (Path.Length () == 0) return -KAPI_ENOENT;
	unsigned nAcc = nFlags & KAPI_O_ACCMODE;
	if (nAcc == KAPI_O_ACCMODE) return -KAPI_EINVAL;
	boolean bWrite = nAcc != KAPI_O_RDONLY;
	if (VfsHandles (Path.Get ())) return -KAPI_ENOTSUP;	// (a provider's path: v1)
	char abs[OF_PATH];
	ResolvePath (Path.Get (), abs, sizeof abs);
	CHandleTable *pTable = HandlesCurrent ();
	if (pTable == 0) return -KAPI_ENOMEM;

	TOFile *of = new TOFile;
	if (of == 0) return -KAPI_ENOMEM;
	memset (of, 0, sizeof *of);
	of->nMagic = OF_MAGIC;
	of->nHolders = 1;
	of->nAccess = (u8) nAcc;
	of->bAppend = (nFlags & KAPI_O_APPEND) != 0;

	if (RamFsHandles (abs))
	{
		int nErr = 0;
		unsigned nRamFlags = nFlags & ~(bWrite ? 0u : (unsigned) KAPI_O_TRUNC);
		of->nKind = OF_RAM;
		of->pRam = RamFsNodeOpen (abs, nRamFlags, &nErr);
		if (of->pRam == 0) { delete of; return nErr != 0 ? nErr : -KAPI_EIO; }
	}
	else
	{
		of->nKind = OF_FAT;
		if (VolumeOf (abs) < 0) { delete of; return -KAPI_ENODEV; }
		if (IsRoot (abs)) { delete of; return -KAPI_EISDIR; }
		// (v77) Opened for writing, created or truncated: the image of a program at that path
		// loses its name (kern/image.h: its key is the path, the file is not looked at again).
		if (bWrite || (nFlags & (KAPI_O_CREAT | KAPI_O_TRUNC))) ImageFileChanged (abs);
		TLockGuard G (&s_Table);
		TFNode *n = FindNode (abs);
		if (n != 0)					// open already: its node
		{
			if ((nFlags & KAPI_O_CREAT) && (nFlags & KAPI_O_EXCL)) { delete of; return -KAPI_EEXIST; }
			TLockGuard GN (&n->Lock);
			int nErr = n->pFile == 0 ? -KAPI_EIO : 0;
			if (nErr == 0 && bWrite) nErr = NodeMakeWritable (n);
			if (nErr == 0 && bWrite && (nFlags & KAPI_O_TRUNC) && f_size (n->pFile) > 0)
			{
				FRESULT r = f_lseek (n->pFile, 0);
				if (r == FR_OK) r = f_truncate (n->pFile);
				nErr = FatErr (r);
				n->bDirty = TRUE; n->nMTime = NowUtc ();
			}
			if (nErr != 0) { delete of; return nErr; }
			n->nRefs++;
		}
		else
		{
			FILINFO Info;
			FRESULT r = f_stat (abs, &Info);
			if (r == FR_OK)
			{
				if (Info.fattrib & AM_DIR) { delete of; return -KAPI_EISDIR; }
				if ((nFlags & KAPI_O_CREAT) && (nFlags & KAPI_O_EXCL)) { delete of; return -KAPI_EEXIST; }
			}
			else if (r != FR_NO_FILE || !(nFlags & KAPI_O_CREAT))
			{
				delete of;
				return NameErr (abs, r);
			}
			n = new TFNode;
			if (n == 0) { delete of; return -KAPI_ENOMEM; }
			memset (n, 0, sizeof *n);
			Copy (n->Path, abs, sizeof n->Path);
			n->pFile = new FIL;
			if (n->pFile == 0) { delete n; delete of; return -KAPI_ENOMEM; }
			memset (n->pFile, 0, sizeof (FIL));
			BYTE nMode = FA_READ | (bWrite ? FA_WRITE : 0);
			if (r != FR_NO_FILE) nMode |= FA_OPEN_EXISTING;
			else nMode |= (nFlags & KAPI_O_EXCL) ? FA_CREATE_NEW : FA_OPEN_ALWAYS;
			r = f_open (n->pFile, abs, nMode);
			if (r == FR_OK && bWrite && (nFlags & KAPI_O_TRUNC) && f_size (n->pFile) > 0)
			{
				r = f_truncate (n->pFile);		// (at 0: just opened)
			}
			if (r != FR_OK)
			{
				f_close (n->pFile);
				delete n->pFile; delete n; delete of;
				return NameErr (abs, r);
			}
			n->bWritable = bWrite;
			n->nMTime = NowUtc ();
			n->bDirty = bWrite;
			n->nRefs = 1;
			n->pNext = s_pNodes;
			s_pNodes = n;
		}
		of->pNode = n;
	}

	void *h = pTable->Add (of, HANDLE_OFILE);
	if (h == 0)
	{
		CloseNow (of);
		return -KAPI_EMFILE;
	}
	return (long long) (uintptr) h;
}

long long kapi_file_read (long long h, void *pBuf, unsigned long long nLen, long long nOff)
{
	CNoKill NK;
	CHandlePin Use (HandleOf (h), HANDLE_OFILE);
	TOFile *of = (TOFile *) Use.Obj ();
	if (of == 0 || of->nAccess == KAPI_O_WRONLY) return -KAPI_EBADF;
	if (nOff < -1) return -KAPI_EINVAL;
	if (nLen > OF_IO_MAX) nLen = OF_IO_MAX;
	if (of->nKind == OF_RAM)
	{
		u64 nPos = nOff >= 0 ? (u64) nOff : of->nOffset;
		if (nLen == 0) return 0;
		struct kapi_stat st;
		RamFsNodeStat (of->pRam, &st);
		u64 nFill = nPos >= st.size ? 0 : (st.size - nPos < nLen ? st.size - nPos : nLen);
		if (nFill > 0 && !UserWritable (pBuf, nFill)) return -KAPI_EFAULT;
		s64 r = RamFsPRead (of->pRam, nPos, pBuf, nLen > nFill ? nFill : nLen);
		if (r > 0 && nOff < 0) of->nOffset = nPos + (u64) r;
		return r;
	}
	TFNode *n = of->pNode;
	TLockGuard GN (&n->Lock);
	if (n->pFile == 0) return -KAPI_EIO;
	u64 nPos = nOff >= 0 ? (u64) nOff : of->nOffset;
	u64 nSize = f_size (n->pFile);
	u64 nFill = nPos >= nSize ? 0 : (nSize - nPos < nLen ? nSize - nPos : nLen);
	if (nFill == 0) return 0;
	if (!UserWritable (pBuf, nFill)) return -KAPI_EFAULT;
	FRESULT r = f_lseek (n->pFile, (FSIZE_t) nPos);
	UINT nDone = 0;
	if (r == FR_OK) r = ChunkedRead (n->pFile, pBuf, (unsigned) nFill, &nDone);
	if (r != FR_OK && nDone == 0) return FatErr (r);
	if (nOff < 0) of->nOffset = nPos + nDone;
	return (long long) nDone;
}

long long kapi_file_write (long long h, const void *pBuf, unsigned long long nLen, long long nOff)
{
	CNoKill NK;
	CHandlePin Use (HandleOf (h), HANDLE_OFILE);
	TOFile *of = (TOFile *) Use.Obj ();
	if (of == 0 || of->nAccess == KAPI_O_RDONLY) return -KAPI_EBADF;
	if (nOff < -1) return -KAPI_EINVAL;
	if (nLen > OF_IO_MAX) nLen = OF_IO_MAX;
	if (nLen > 0 && !UserReadable (pBuf, nLen)) return -KAPI_EFAULT;
	if (of->nKind == OF_RAM)
	{
		boolean bEnd = nOff < 0 && of->bAppend;
		u64 nEnd = 0;
		s64 r = RamFsPWrite (of->pRam, nOff >= 0 ? (u64) nOff : of->nOffset, pBuf, nLen, bEnd, &nEnd);
		if (r >= 0 && nOff < 0) of->nOffset = nEnd;
		return r;
	}
	if (nLen == 0) return 0;
	TFNode *n = of->pNode;
	TLockGuard GN (&n->Lock);
	FIL *f = n->pFile;
	if (f == 0) return -KAPI_EIO;
	u64 nSize = f_size (f);
	u64 nPos = nOff >= 0 ? (u64) nOff : (of->bAppend ? nSize : of->nOffset);
	u64 nMax = MaxSize (f);
	if (nPos >= nMax) return -KAPI_EFBIG;
	if (nLen > nMax - nPos) nLen = nMax - nPos;
	int nErr = 0;
	if (nPos > nSize)					// a gap: zeros (FatFs leaves it undefined)
	{
		FRESULT r = f_lseek (f, (FSIZE_t) nSize);
		nErr = r != FR_OK ? FatErr (r) : WriteZeros (f, nPos - nSize);
	}
	else
	{
		nErr = FatErr (f_lseek (f, (FSIZE_t) nPos));
	}
	UINT nDone = 0;
	if (nErr == 0)
	{
		FRESULT r = ChunkedWrite (f, pBuf, (unsigned) nLen, &nDone);
		if (r != FR_OK) nErr = FatErr (r);
		else if (nDone == 0) nErr = -KAPI_ENOSPC;
	}
	if (f_size (f) != nSize || nDone > 0) { n->bDirty = TRUE; n->nMTime = NowUtc (); }
	if (nDone == 0) return nErr;
	if (nOff < 0) of->nOffset = nPos + nDone;
	return (long long) nDone;
}

long long kapi_file_seek (long long h, long long nOff, int nWhence)
{
	CNoKill NK;
	CHandlePin Use (HandleOf (h), HANDLE_OFILE);
	TOFile *of = (TOFile *) Use.Obj ();
	if (of == 0) return -KAPI_EBADF;
	s64 nBase;
	switch (nWhence)
	{
	case KAPI_SEEK_SET:	nBase = 0; break;
	case KAPI_SEEK_CUR:	nBase = (s64) of->nOffset; break;
	case KAPI_SEEK_END:
		if (of->nKind == OF_RAM)
		{
			struct kapi_stat st;
			RamFsNodeStat (of->pRam, &st);
			nBase = (s64) st.size;
		}
		else
		{
			TLockGuard GN (&of->pNode->Lock);
			if (of->pNode->pFile == 0) return -KAPI_EIO;
			nBase = (s64) f_size (of->pNode->pFile);
		}
		break;
	default:		return -KAPI_EINVAL;
	}
	if ((nOff > 0 && nBase > (s64) (~0ull >> 1) - nOff) || nBase + nOff < 0) return -KAPI_EINVAL;
	of->nOffset = (u64) (nBase + nOff);
	return (long long) of->nOffset;
}

int kapi_file_truncate (long long h, long long nSize)
{
	CNoKill NK;
	CHandlePin Use (HandleOf (h), HANDLE_OFILE);
	TOFile *of = (TOFile *) Use.Obj ();
	if (of == 0) return -KAPI_EBADF;
	if (of->nAccess == KAPI_O_RDONLY || nSize < 0) return -KAPI_EINVAL;
	if (of->nKind == OF_RAM) return RamFsTruncate (of->pRam, (u64) nSize);
	TFNode *n = of->pNode;
	TLockGuard GN (&n->Lock);
	FIL *f = n->pFile;
	if (f == 0) return -KAPI_EIO;
	u64 nCur = f_size (f);
	if ((u64) nSize > MaxSize (f)) return -KAPI_EFBIG;
	int nErr = 0;
	if ((u64) nSize < nCur)
	{
		FRESULT r = f_lseek (f, (FSIZE_t) nSize);
		if (r == FR_OK) r = f_truncate (f);
		nErr = FatErr (r);
	}
	else if ((u64) nSize > nCur)					// grown: zeros
	{
		FRESULT r = f_lseek (f, (FSIZE_t) nCur);
		nErr = r != FR_OK ? FatErr (r) : WriteZeros (f, (u64) nSize - nCur);
		if (nErr != 0 && f_lseek (f, (FSIZE_t) nCur) == FR_OK) f_truncate (f);	// (no half growth)
	}
	n->bDirty = TRUE; n->nMTime = NowUtc ();
	return nErr;
}

int kapi_file_sync (long long h)
{
	CNoKill NK;
	CHandlePin Use (HandleOf (h), HANDLE_OFILE);
	TOFile *of = (TOFile *) Use.Obj ();
	if (of == 0) return -KAPI_EBADF;
	if (of->nKind == OF_RAM) return 0;
	TFNode *n = of->pNode;
	TLockGuard GN (&n->Lock);
	if (n->pFile == 0) return -KAPI_EIO;
	if (!n->bWritable) return 0;
	FRESULT r = f_sync (n->pFile);
	if (r == FR_OK) n->bDirty = FALSE;			// (the entry has the size and the time now)
	return FatErr (r);
}

int kapi_file_stat (long long h, struct kapi_stat *pOut)
{
	CNoKill NK;
	if (pOut == 0 || !UserRange (pOut, sizeof *pOut)) return -KAPI_EFAULT;
	CHandlePin Use (HandleOf (h), HANDLE_OFILE);
	TOFile *of = (TOFile *) Use.Obj ();
	if (of == 0) return -KAPI_EBADF;
	struct kapi_stat st;
	if (of->nKind == OF_RAM)
	{
		RamFsNodeStat (of->pRam, &st);
	}
	else
	{
		TLockGuard GN (&of->pNode->Lock);
		int nErr = NodeStat (of->pNode, &st);
		if (nErr != 0) return nErr;
	}
	return UserPut (pOut, st) ? 0 : -KAPI_EFAULT;
}

int kapi_file_close (long long h)
{
	CHandleTable *pTable = HandlesCurrent ();
	return pTable != 0 && pTable->Close (HandleOf (h), HANDLE_OFILE) ? 0 : -KAPI_EBADF;
}

int kapi_path_stat (const char *pUserPath, struct kapi_stat *pOut)
{
	CNoKill NK;
	if (pOut == 0 || !UserRange (pOut, sizeof *pOut)) return -KAPI_EFAULT;
	CUserStr Path (pUserPath, UPATH_MAX);
	if (!Path.OK ()) return UserPathErr (pUserPath);
	if (Path.Length () == 0) return -KAPI_ENOENT;
	if (VfsHandles (Path.Get ())) return -KAPI_ENOTSUP;
	char abs[OF_PATH];
	ResolvePath (Path.Get (), abs, sizeof abs);
	struct kapi_stat st;
	if (RamFsHandles (abs))
	{
		int nErr = RamFsStat (abs, &st);
		if (nErr != 0) return nErr;
		return UserPut (pOut, st) ? 0 : -KAPI_EFAULT;
	}
	int v = VolumeOf (abs);
	if (v < 0) return -KAPI_ENODEV;
	if (IsRoot (abs))					// (FatFs has no entry for it)
	{
		DIR Dir;
		FRESULT r = f_opendir (&Dir, abs);
		if (r != FR_OK) return FatErr (r);
		f_closedir (&Dir);
		FillFat (&st, abs, 0, AM_DIR, 0, 0);
		return UserPut (pOut, st) ? 0 : -KAPI_EFAULT;
	}
	{
		TLockGuard G (&s_Table);
		TFNode *n = FindNode (abs);
		if (n != 0)					// open: its FIL knows the size
		{
			TLockGuard GN (&n->Lock);
			int nErr = NodeStat (n, &st);
			if (nErr != 0) return nErr;
			st.ino = Fnv (FNV_BASIS, abs);
			return UserPut (pOut, st) ? 0 : -KAPI_EFAULT;
		}
	}
	FILINFO Info;
	FRESULT r = f_stat (abs, &Info);
	if (r != FR_OK) return NameErr (abs, r);
	FillFat (&st, abs, Info.fsize, Info.fattrib, FatToUtc (Info.fdate, Info.ftime), 0);
	return UserPut (pOut, st) ? 0 : -KAPI_EFAULT;
}

int kapi_path_unlink (const char *pUserPath, unsigned nFlags)
{
	CNoKill NK;
	CUserStr Path (pUserPath, UPATH_MAX);
	if (!Path.OK ()) return UserPathErr (pUserPath);
	if (Path.Length () == 0) return -KAPI_ENOENT;
	boolean bDir = (nFlags & KAPI_UNLINK_DIR) != 0;
	if (VfsHandles (Path.Get ()))
	{
		return VfsCall (VFS_OP_REMOVE, Path.Get (), 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0 ? 0 : -KAPI_EIO;
	}
	char abs[OF_PATH];
	ResolvePath (Path.Get (), abs, sizeof abs);
	if (RamFsHandles (abs)) return RamFsUnlink (abs, bDir);
	if (VolumeOf (abs) < 0) return -KAPI_ENODEV;
	if (IsRoot (abs)) return -KAPI_EBUSY;
	ImageFileChanged (abs);					// (v77: a program's image loses its name)
	TLockGuard G (&s_Table);
	FILINFO Info;
	FRESULT r = f_stat (abs, &Info);
	if (r != FR_OK) return NameErr (abs, r);
	if (Info.fattrib & AM_DIR)
	{
		if (!bDir) return -KAPI_EISDIR;
		r = f_unlink (abs);
		if (r == FR_DENIED) return (Info.fattrib & AM_RDO) ? -KAPI_EACCES : -KAPI_ENOTEMPTY;
		return FatErr (r);
	}
	if (bDir) return -KAPI_ENOTDIR;
	TFNode *n = FindNode (abs);
	if (n != 0)						// open: hidden, deleted at its last close
	{
		if (Info.fattrib & AM_RDO) return -KAPI_EACCES;
		TLockGuard GN (&n->Lock);
		return NodeHide (n);
	}
	return FatErr (f_unlink (abs));
}

int kapi_path_mkdir (const char *pUserPath, unsigned nMode)
{
	(void) nMode;
	CNoKill NK;
	CUserStr Path (pUserPath, UPATH_MAX);
	if (!Path.OK ()) return UserPathErr (pUserPath);
	if (Path.Length () == 0) return -KAPI_ENOENT;
	if (VfsHandles (Path.Get ()))
	{
		return VfsCall (VFS_OP_MKDIR, Path.Get (), 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0 ? 0 : -KAPI_EIO;
	}
	char abs[OF_PATH];
	ResolvePath (Path.Get (), abs, sizeof abs);
	if (RamFsHandles (abs)) return RamFsMkdirEx (abs);
	if (VolumeOf (abs) < 0) return -KAPI_ENODEV;
	if (IsRoot (abs)) return -KAPI_EEXIST;
	FRESULT r = f_mkdir (abs);
	return r == FR_NO_PATH ? -KAPI_ENOENT : NameErr (abs, r);
}

int kapi_path_rename (const char *pUserFrom, const char *pUserTo)
{
	CNoKill NK;
	CUserStr From (pUserFrom, UPATH_MAX), To (pUserTo, UPATH_MAX);
	if (!From.OK () || !To.OK ()) return UserPathErr (!From.OK () ? pUserFrom : pUserTo);
	if (From.Length () == 0 || To.Length () == 0) return -KAPI_ENOENT;
	boolean bVf = VfsHandles (From.Get ()), bVt = VfsHandles (To.Get ());
	if (bVf || bVt)						// (both on one provider)
	{
		if (!bVf || !bVt) return -KAPI_EXDEV;
		return VfsCall (VFS_OP_RENAME, From.Get (), To.Get (), 0, 0, 0, 0, 0, 0, 0, 0) == 0 ? 0 : -KAPI_EIO;
	}
	char absF[OF_PATH], absT[OF_PATH];
	ResolvePath (From.Get (), absF, sizeof absF);
	ResolvePath (To.Get (), absT, sizeof absT);
	boolean bRf = RamFsHandles (absF), bRt = RamFsHandles (absT);
	if (bRf || bRt) return bRf && bRt ? RamFsRenameEx (absF, absT) : -KAPI_EXDEV;
	int vf = VolumeOf (absF), vt = VolumeOf (absT);
	if (vf < 0 || vt < 0) return -KAPI_ENODEV;
	if (vf != vt) return -KAPI_EXDEV;			// (f_rename would stay in the source volume)
	if (IsRoot (absF) || IsRoot (absT)) return -KAPI_EBUSY;
	if (strcmp (absF, absT) == 0) return 0;
	ImageFileChanged (absF);				// (v77: the images of both names, and of
	ImageFileChanged (absT);				//  the programs under a renamed folder)

	TLockGuard G (&s_Table);
	FILINFO InfoF, InfoT;
	FRESULT r = f_stat (absF, &InfoF);
	if (r != FR_OK) return NameErr (absF, r);
	boolean bDir = (InfoF.fattrib & AM_DIR) != 0;
	unsigned nF = strlen (absF);
	if (bDir && strlen (absT) > nF && absT[nF] == '/')	// (a folder into itself)
	{
		char c = absT[nF]; absT[nF] = '\0';
		boolean bIn = SameName (absT, absF);
		absT[nF] = c;
		if (bIn) return -KAPI_EINVAL;
	}
	if (!SameName (absF, absT))				// (else: the case of its name changes)
	{
		r = f_stat (absT, &InfoT);
		if (r == FR_OK)					// replaced
		{
			boolean bDirT = (InfoT.fattrib & AM_DIR) != 0;
			if (bDir && !bDirT) return -KAPI_ENOTDIR;
			if (!bDir && bDirT) return -KAPI_EISDIR;
			if (bDirT)
			{
				r = f_unlink (absT);
				if (r == FR_DENIED) return (InfoT.fattrib & AM_RDO) ? -KAPI_EACCES : -KAPI_ENOTEMPTY;
				if (r != FR_OK) return FatErr (r);
			}
			else
			{
				TFNode *t = FindNode (absT);
				if (t != 0)
				{
					TLockGuard GT (&t->Lock);
					int nErr = NodeHide (t);
					if (nErr != 0) return nErr;
				}
				else if ((r = f_unlink (absT)) != FR_OK) return FatErr (r);
			}
		}
		else if (r != FR_NO_FILE) return NameErr (absT, r);
	}
	TFNode *s = bDir ? 0 : FindNode (absF);
	if (s != 0)						// open: closed, moved, re-opened
	{
		TLockGuard GS (&s->Lock);
		if (s->pFile != 0) f_close (s->pFile);
		r = f_rename (absF, absT);
		if (r == FR_OK) Copy (s->Path, absT, sizeof s->Path);
		FRESULT r2 = NodeReopen (s);
		if (r2 != FR_OK) CLogger::Get ()->Write ("ofile", LogWarning, "%s: cannot re-open (%d)", s->Path, r2);
	}
	else
	{
		r = f_rename (absF, absT);
	}
	if (r != FR_OK) return NameErr (absT, r);
	if (bDir)						// the open files inside: their new paths
	{
		unsigned nT = strlen (absT);
		for (TFNode *n = s_pNodes; n != 0; n = n->pNext)
		{
			if (strlen (n->Path) <= nF || n->Path[nF] != '/') continue;
			char c = n->Path[nF]; n->Path[nF] = '\0';
			boolean bIn = SameName (n->Path, absF);
			n->Path[nF] = c;
			if (!bIn || nT + strlen (n->Path + nF) >= sizeof n->Path) continue;
			char New[OF_PATH];
			Copy (New, absT, sizeof New);
			Copy (New + nT, n->Path + nF, sizeof New - nT);
			Copy (n->Path, New, sizeof n->Path);
		}
	}
	return 0;
}

int kapi_path_utime (const char *pUserPath, long long nMTime)
{
	CNoKill NK;
	CUserStr Path (pUserPath, UPATH_MAX);
	if (!Path.OK ()) return UserPathErr (pUserPath);
	if (Path.Length () == 0) return -KAPI_ENOENT;
	if (VfsHandles (Path.Get ())) return -KAPI_ENOTSUP;
	char abs[OF_PATH];
	ResolvePath (Path.Get (), abs, sizeof abs);
	if (RamFsHandles (abs)) return RamFsUtime (abs, nMTime);
	if (VolumeOf (abs) < 0) return -KAPI_ENODEV;
	if (IsRoot (abs)) return -KAPI_EPERM;
	TLockGuard G (&s_Table);
	TFNode *n = FindNode (abs);
	if (n != 0)						// open: flushed first (its close would stamp it)
	{
		TLockGuard GN (&n->Lock);
		if (n->pFile != 0 && n->bWritable) f_sync (n->pFile);
		n->bDirty = FALSE;
	}
	FILINFO Info;
	memset (&Info, 0, sizeof Info);
	UtcToFat (nMTime, &Info.fdate, &Info.ftime);
	return NameErr (abs, f_utime (abs, &Info));
}

}	// extern "C"

// ---- dir_read ----------------------------------------------------------------------------------

#define DIR_TAGS	64

struct TDirTag { const void *pDir; u64 nHash; };	// FNV of "<path>/", upper-cased
static TDirTag s_DirTag[DIR_TAGS];
static unsigned s_nDirTagNext;

void OFileNoteDir (const void *pDir, const char *pAbs)
{
	if (pDir == 0 || pAbs == 0) return;
	u64 h = Fnv (FNV_BASIS, pAbs);
	unsigned n = strlen (pAbs);
	if (n == 0 || pAbs[n - 1] != '/') h = Fnv (h, "/");
	unsigned i = 0;
	for (; i < DIR_TAGS; i++) if (s_DirTag[i].pDir == pDir) break;	// (a DIR reused)
	if (i == DIR_TAGS) i = s_nDirTagNext++ % DIR_TAGS;
	s_DirTag[i].pDir = pDir;
	s_DirTag[i].nHash = h;
}

static u64 DirHash (const void *pDir)
{
	for (unsigned i = 0; i < DIR_TAGS; i++) if (s_DirTag[i].pDir == pDir) return s_DirTag[i].nHash;
	return FNV_BASIS;
}

extern "C" {

int kapi_dir_read (void *hDir, struct kapi_dirent2 *pOut)
{
	CNoKill NK;
	if (pOut == 0 || !UserRange (pOut, sizeof *pOut)) return -KAPI_EFAULT;	// (before an entry is read)
	CHandlePin Use (hDir, HANDLE_DIR);
	if (Use.Obj () == 0) return -KAPI_EBADF;
	struct kapi_dirent2 *pEnt = new struct kapi_dirent2;	// (288 bytes: not on the kernel stack)
	if (pEnt == 0) return -KAPI_ENOMEM;
	memset (pEnt, 0, sizeof *pEnt);
	int nRet = 1;
	if (Use.Kind () == HKIND_RAMFS)
	{
		nRet = RamFsReadDir2 (Use.Obj (), pEnt);
	}
	else if (Use.Kind () == HKIND_VFS)
	{
		struct kapi_dirent Old;
		memset (&Old, 0, sizeof Old);
		nRet = VfsReadDir (Use.Obj (), &Old);
		if (nRet > 0)
		{
			Copy (pEnt->name, Old.name, sizeof pEnt->name);
			pEnt->size = Old.is_dir ? 0 : Old.size;
			pEnt->mode = Old.is_dir ? KAPI_S_IFDIR | 0755 : KAPI_S_IFREG | 0644;
			pEnt->attr = Old.is_dir ? AM_DIR : AM_ARC;
			pEnt->ino = Fnv (FNV_BASIS, Old.name);
		}
	}
	else
	{
		FILINFO *pInfo = new FILINFO;
		if (pInfo == 0) { delete pEnt; return -KAPI_ENOMEM; }
		for (;;)
		{
			FRESULT r = f_readdir ((DIR *) Use.Obj (), pInfo);
			if (r != FR_OK) { nRet = FatErr (r); break; }
			if (pInfo->fname[0] == '\0') { nRet = 0; break; }
			if (SameName (pInfo->fname, HIDE_DIR)) continue;	// (ours: files unlinked while open)
			Copy (pEnt->name, pInfo->fname, sizeof pEnt->name);
			boolean bDir = (pInfo->fattrib & AM_DIR) != 0;
			pEnt->size = bDir ? 0 : pInfo->fsize;
			pEnt->mtime = FatToUtc (pInfo->fdate, pInfo->ftime);
			pEnt->mode = (bDir ? KAPI_S_IFDIR | 0755 : KAPI_S_IFREG | 0644) & ((pInfo->fattrib & AM_RDO) ? ~0222u : ~0u);
			pEnt->attr = pInfo->fattrib;
			pEnt->ino = Fnv (DirHash (Use.Obj ()), pInfo->fname);
			break;
		}
		delete pInfo;
	}
	if (nRet > 0 && !UserCopyOut (pOut, pEnt, sizeof *pEnt)) nRet = -KAPI_EFAULT;
	delete pEnt;
	return nRet;
}

// ---- stream_write_nb ---------------------------------------------------------------------------

int kapi_stream_write_nb (void *h, const void *pBuf, unsigned nLen)
{
	CHandlePin Use (h, HANDLE_STREAM);
	CStream *pStream = (CStream *) Use.Obj ();
	if (pStream == 0) return -KAPI_EBADF;
	if (nLen == 0) return 0;
	if (!UserReadable (pBuf, nLen)) return -KAPI_EFAULT;
	int n = pStream->WriteNonBlocking (pBuf, nLen);
	if (n == -2) return -KAPI_EAGAIN;			// (full)
	return n < 0 ? -KAPI_EIO : n;
}

}
