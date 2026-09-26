//
// vfs.h -- user-space file-system providers ("FUSE-like"), ABI v44.
//
// A provider app (e.g. /bin/ftpfs) registers a path prefix ("FTP:", "FTPS:"). The file
// kapis (open/read/fsize/close, save_file, opendir/readdir/closedir, mkdir/remove/rename)
// on a path with that prefix become REQUESTS: the calling task queues one in a slot, wakes
// the provider and blocks (event, timeout) until the provider answers with vfs_reply.
// Payloads (file data, listings) are copied through kernel buffers, since the two
// processes do not share memory. A few prefixes are auto-started on first use (their
// daemon is exec'd and given a moment to register). See sys/vfs.cpp.
//
#ifndef _kern_vfs_h
#define _kern_vfs_h

#include <circle/types.h>

#define VFS_OP_OPEN	1	// path -> status = fid (>= 0); out = u32 file size
#define VFS_OP_READ	2	// a0 = fid, a1 = offset, a2 = length -> out = data, status = n
#define VFS_OP_CLOSE	3	// a0 = fid
#define VFS_OP_LIST	4	// path -> out = entries (u32 size, u8 is_dir, name '\0')*, status = count
#define VFS_OP_SAVE	5	// path + in = data -> status = bytes written
#define VFS_OP_MKDIR	6	// path -> 0 / -1
#define VFS_OP_REMOVE	7	// path -> 0 / -1
#define VFS_OP_RENAME	8	// path -> path2: 0 / -1

#define VFS_PATH_MAX	300
#define VFS_READ_MAX	(64 * 1024)	// largest READ answer

// Is pPath served by a provider (registered, or auto-startable)?
boolean VfsHandles (const char *pPath);

// One request, from the calling task: blocks until answered (or the provider is gone /
// the timeout). Returns the provider's status (< 0 = error; -100 = no provider / timeout).
// The answer's data is copied to pOut (<= nOutCap), its full length in *pOutLen.
int VfsCall (int nOp, const char *pPath, const char *pPath2, long a0, long a1, long a2,
	     const void *pIn, unsigned nInLen, void *pOut, unsigned nOutCap, unsigned *pOutLen);

// Teardown hook (called by IpcOnProcessGone): forget a dead provider, fail its requests.
void VfsOnProcessGone (unsigned nPid);

// ---- file / dir handles backed by a provider (kapi_open / kapi_opendir) --------------
// kapi_open returns a pointer INTO these static tables for provider paths, so the other
// file kapis tell them apart from FatFs FIL / DIR objects by address.
boolean VfsIsFile (void *pHandle);
boolean VfsIsDir (void *pHandle);
void   *VfsOpen (const char *pPath);
int     VfsRead (void *pHandle, void *pBuf, unsigned nLen);
unsigned VfsSize (void *pHandle);
void    VfsClose (void *pHandle);
void   *VfsOpenDir (const char *pPath);
struct kapi_dirent;
int     VfsReadDir (void *pHandle, struct kapi_dirent *pEnt);
void    VfsCloseDir (void *pHandle);

#endif
