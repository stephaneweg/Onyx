//
// ramfs.h -- RAM:, the kernel's RAM file system (docs/02 *The RAM: volume*).
//
// A volume in memory, reached by the same file kapis as the card (open / read / fsize /
// seek / close, save_file, file_in / file_out streams (append too), opendir / readdir /
// closedir, mkdir, remove, rename, chdir, vol_info): "RAM:/jet/cache/x". Nested folders,
// names of up to 127 characters (case-insensitive, case kept, as on the card's FAT). Its
// contents live until the Pi restarts -- never written anywhere, not tied to any app.
//
// Memory: the files' bytes are in 64 KB pages of the page allocator (palloc_high: the apps'
// pool), carved into chunks of 256 B .. 32 KB for the small files and the files' tails (a file
// takes its size rounded up to 256 B, not a whole page); a file's chunk list is in a chunk too.
// Only the folders' and files' records (one size: no fragmentation) are on the kernel heap.
// A page goes back to the allocator as soon as nothing in it is used (a file removed,
// truncated). Never in the BSS (the kernel image's 2 MB limit).
//
// Limits: the volume's size (system.ini "ramfs=": MB, or "N%" of the page memory free at boot;
// default 128 MB but at most a quarter of that free memory; "ramfs=0" = no RAM: volume), a
// reserve of free pages always left to the apps (RAMFS_RESERVE), RAMFS_MAX_NODES files and
// folders, a file of RAMFS_FILE_MAX at most.
//
// Locking: one re-entrant sleeping lock (its waiters yield, as the FatFs volume lock in
// sys/fslock.cpp); nothing yields while holding it. A big read or write is cut into slices of
// RAMFS_SLICE with a Yield between them (the lock free meanwhile, the caller kept from being
// killed in the middle: CScheduler::EnterNoKill).
//
#ifndef _kern_ramfs_h
#define _kern_ramfs_h

#include <circle/types.h>

#define RAMFS_PREFIX		"RAM:"
#define RAMFS_DEFAULT_MB	128
#define RAMFS_RESERVE		(32ull << 20)		// page memory always left to the apps
#define RAMFS_MAX_NODES		16384			// files + folders
#define RAMFS_FILE_MAX		(128ull << 20)		// one file (and the volume's size)
#define RAMFS_NAME_MAX		127
#define RAMFS_SLICE		(1u << 20)		// bytes copied between two yields

struct kapi_dirent;
struct kapi_vol_info;

// Boot: the volume's size from system.ini's "ramfs=" value (0: the default rule).
void RamFsInit (const char *pConfig);
boolean RamFsMounted (void);

// Is this (resolved, absolute: "RAM:/...") path on the RAM volume?
boolean RamFsHandles (const char *pAbs);

// ---- the file kapis on a RAM: path (pAbs: ResolvePath's) -------------------------------------
void   *RamFsOpen (const char *pAbs);			// read handle, 0 = none
boolean RamFsIsFile (void *pHandle);			// one of RamFsOpen's handles?
int     RamFsRead (void *pHandle, void *pBuf, unsigned nLen);
u64     RamFsSize (void *pHandle);
int     RamFsSeek (void *pHandle, u64 nPos);		// 0 / -1
void    RamFsClose (void *pHandle);

int     RamFsSave (const char *pAbs, const void *pBuf, unsigned nLen);	// bytes written, -1

void   *RamFsOpenDir (const char *pAbs);
boolean RamFsIsDir (void *pHandle);			// one of RamFsOpenDir's handles?
int     RamFsReadDir (void *pHandle, struct kapi_dirent *pEnt);	// 1, 0 = the end
void    RamFsCloseDir (void *pHandle);

int     RamFsMkdir (const char *pAbs);			// 0 / -1
int     RamFsRemove (const char *pAbs);			// a file or an empty folder: 0 / -1
int     RamFsRename (const char *pFrom, const char *pTo);	// both on RAM: -> 0 / -1
boolean RamFsIsDirPath (const char *pAbs);		// a folder there (chdir)

// ---- streams (kapi_file_in / kapi_file_out; CRamStream in sys/stream.cpp) ----------------------
// nMode 0 read, 1 write (created / truncated), 2 append (created if missing)
void   *RamFsStreamOpen (const char *pAbs, int nMode);
int     RamFsStreamRead (void *pStream, void *pBuf, unsigned nLen);		// 0 = the end
int     RamFsStreamWrite (void *pStream, const void *pBuf, unsigned nLen);	// bytes, -1
void    RamFsStreamClose (void *pStream);

// ---- the volume -------------------------------------------------------------------------------
// total = its size, used = the pages its files take (+ their records), free = what can still
// be written (also bounded by the free page memory less the reserve), files / folders.
void    RamFsInfo (u64 *pTotal, u64 *pUsed, u64 *pFree, unsigned *pFiles, unsigned *pDirs);

// A process ended: its open handles closed (the reaper, from IpcOnProcessGone).
void    RamFsOnProcessGone (unsigned nPid);

#endif
