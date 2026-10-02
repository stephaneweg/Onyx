//
// lsock.h -- IPC between processes (kapi v76, WP-IPC; docs/POSIX-PLAN.md §14, docs/02 §8 "v76: IPC"):
// local sockets (sys/lsock.cpp), handles carried from one process's table to another's (sys/lsock.cpp),
// shared memory objects (sys/shm.cpp, mapped by sys/vm.cpp).
//
// LOCAL SOCKETS. An end (TLsEnd, private to lsock.cpp) is a core-0 kernel object named by HANDLE_LSOCK
// entries: a socket number >= KAPI_SOCK_LOCAL_BASE is the handle's value in the caller's table. Each
// entry, each message carrying the end and each spawn record holds one reference; the last one gone,
// its peer sees the end. Its receive queue holds messages (data + carried handles). Nothing waits
// with the interrupts masked; a blocking call waits on the I/O generation (kern/iowait.h), and every
// change calls IoWake. bsdsock.cpp routes the sock_* calls and poll for local numbers here.
//
// CARRIED HANDLES (TIpcXfer). A sender's handle is turned into a reference on its object at once
// (IpcXferTake); the reference moves into the receiver's table later (IpcXferGive) or is dropped
// with the message (IpcXferDrop). The objects: open-file descriptions (HANDLE_OFILE, refcounted by
// ofile.cpp), streams (CStream), local socket ends, shared memory objects; IP sockets (0..255, one
// owner pid in sys/net.cpp) are carried by number and adopted by the receiver.
//
// SHARED MEMORY. A TShm (shm.cpp) is a size and an array of 64 KB frames allocated and zeroed on
// first use, freed when its last reference goes: handles (HANDLE_SHM, nKind = the access), carried
// handles, the name table (shm_open), and each address space mapping it (ShmMapAttach: one per space,
// vm.cpp). A mapping's PTEs are not owned (VM_PTE_SW_OWNED clear): an unmap never frees a frame.
//
// Core 0 only, as the handle tables: the kernel is not preempted, so no lock.
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
#ifndef _kern_lsock_h
#define _kern_lsock_h

#include <circle/types.h>
#include <kern/kapi_abi.h>

class CHandleTable;

#define IPC_LS_DEFAULT_BUF	(256 * 1024)	// SO_RCVBUF / SO_SNDBUF of a new end
#define IPC_LS_MIN_BUF		4096
#define IPC_LS_MAX_BUF		(16 * 1024 * 1024)
#define IPC_LS_TOTAL_MAX	(64ULL << 20)	// every local queue together (-ENOBUFS beyond)
#define IPC_SHM_MAX		(4ULL << 30)	// one object's size

// ---- carried handles --------------------------------------------------------------------------

struct TIpcXfer
{
	void	*pObj;			// the object (a reference held), 0: none (an IP socket, or dropped)
	int	 nSock;			// KAPI_HK_SOCKET: its number
	u32	 nPid;			// KAPI_HK_SOCKET: its owner when it was taken
	u32	 nTag;			// the sender's word
	u8	 nHK;			// KAPI_HK_*
	u8	 nType;			// HANDLE_*
	u8	 nKind;			// the handle entry's kind (HANDLE_SHM: the access)
	u8	 nPad;
};

// The object of pTable's handle In (In.kind, In.h) referenced into *pOut. nPid: the caller.
// -> 0 / -KAPI_EBADF (not such a handle) / -KAPI_EINVAL (kind).
int  IpcXferTake (CHandleTable *pTable, const struct kapi_handle_xfer &In, unsigned nPid, TIpcXfer *pOut);

// The reference dropped (a message discarded). bTeardown: no wait allowed (deferred closes).
void IpcXferDrop (TIpcXfer *p, boolean bTeardown);

// The reference moved into pTable (the receiver nPid's): pOut filled {h, kind, tag, fd -1}.
// bAdopt: an IP socket becomes nPid's (sendmsg); else it is passed by number (spawn: the child
// adopts it at its first use). FALSE: dropped (the table full; an IP socket gone: kind NONE, h -1).
boolean IpcXferGive (CHandleTable *pTable, TIpcXfer *p, unsigned nPid, boolean bAdopt,
		     struct kapi_handle_xfer *pOut);

// ---- local sockets (bsdsock.cpp's dispatch) ------------------------------------------------------

static inline boolean IpcIsLocal (int s)	{ return s >= KAPI_SOCK_LOCAL_BASE; }

void IpcLocalRelease (void *pEnd, boolean bTeardown);	// a reference dropped (HandleObjectClose)

unsigned  IpcLocalPoll (int s);				// KAPI_POLL* now (NVAL: not one of the caller's)
long long IpcLocalSend (int s, const void *pBuf, u64 nLen, unsigned nFlags);
long long IpcLocalRecv (int s, void *pBuf, u64 nLen, unsigned nFlags);
int  IpcLocalShutdown (int s, int nHow);
int  IpcLocalClose (int s);
int  IpcLocalGetOpt (int s, int nOpt, int *pValue);	// pValue: a kernel int
int  IpcLocalSetOpt (int s, int nOpt, int nValue);
int  IpcLocalName (int s, int nPeer);			// 0: an AF_UNIX name (empty) / -ENOTCONN / -EBADF

// ---- shared memory (shm.cpp) ----------------------------------------------------------------------

struct TShm;

boolean   ShmIs (const void *pObj);			// a live TShm
void      ShmRef (void *pShm);
void      ShmRelease (void *pShm, boolean bTeardown);	// the last one: its frames freed
u64       ShmSize (const void *pShm);
unsigned  ShmSeals (const void *pShm);
unsigned  ShmMaps (const void *pShm);			// address spaces mapping it

// The frame of page nPage (allocated, zeroed, at the first use) -> 0 + *pPhys / -KAPI_EFAULT (beyond
// the size) / -KAPI_ENOMEM (the app pool under VM_RESERVE). Never yields.
int       ShmFrame (void *pShm, u64 nPage, u64 *pPhys);

// A space starts / stops mapping the object (a reference each).
void      ShmMapAttach (void *pShm);
void      ShmMapDetach (void *pShm, boolean bTeardown);

// Frames held by every object (64 KB pages), objects alive.
unsigned  ShmPagesTotal (void);
unsigned  ShmObjectsTotal (void);

// ---- the open files (ofile.cpp) -------------------------------------------------------------------

// One more holder of the description (a carried handle; OFileClose drops one).
void OFileRef (void *pObj);

#endif // _kern_lsock_h
