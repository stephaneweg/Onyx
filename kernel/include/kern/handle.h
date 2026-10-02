//
// handle.h -- per-process opaque handles for the kernel objects apps hold (ABI unchanged).
//
// The kapis that hand an app a kernel object -- open, opendir, pipe, file_in, file_out,
// spawn, stdin_stream, stdout_stream -- return a small integer cast to the ABI's pointer type
// (never 0), not the object's address; the kapis that take one (read, fsize, fsize64, seek,
// close, readdir, closedir, stream_*, spawn's streams, wait, proc_done) look it up in the
// CALLING process's table, by its expected type, and fail cleanly on anything else. A forged
// or stale value can no longer make the kernel read, write or delete arbitrary memory.
//
// A handle's value: (generation << 16) | (index + 1), at most 0xFFFFFF -- a slot reused gets
// another generation, so a stale handle does not name the new object (for a while: 255
// generations). The table grows by doubling, up to HANDLES_MAX entries per process.
//
// Owner: the process's CAddressSpace (threads share it); the kernel's own tasks (no address
// space) use one kernel table. A process's handles still open when it ends are closed by its
// teardown (CloseAll), in the reaper: files and directories freed at once (the FatFs ones are
// read-only: nothing to flush), a provider's file slot dropped without a request, the RAM:
// volume's left to RamFsOnProcessGone, streams released by the reaper task a moment later
// (HandlesRunDeferred: a file stream's last release writes to the card, which must not happen
// with the interrupts masked), a process handle's reference dropped.
//
// Shared objects are reference-counted, and each table entry holds one reference:
//  - a CStream (a pipe, a file stream): the entry's ref + one per spawned child that has it as
//    stdin / stdout (its CAddressSpace's) + the stdio aliases (stdin_stream / stdout_stream
//    give a handle with its own ref, made once and reused while it is open);
//  - a CProcess (spawn): the spawner's entry + the child (its task, then its address space,
//    which sets done / status and drops it at its end). Whichever ends last frees it: the
//    waiter that died first no longer leaks it, and the child never writes into freed memory.
//
// Concurrency: the tables are only touched on core 0, in kapis and in the reaper; kernel code
// is not preempted, so a lookup is atomic. A kapi that may yield with the object (a read, a
// pipe's wait, kapi_wait) PINS the entry: a close from another thread of the process meanwhile
// only marks it, and the last unpin closes the object -- never freed under a running call.
// App cores (kapi_core_run) make no kapi call; the network core does not see these objects.
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
#ifndef _kern_handle_h
#define _kern_handle_h

#include <circle/types.h>

#define HANDLES_FIRST	16		// entries at the first use
#define HANDLES_MAX	4096		// per process (an app opening in a loop: its own limit)

// An entry's type (what the kapi expects) ...
enum THandleType
{
	HANDLE_FREE	= 0,
	HANDLE_FILE	= 1,		// open: a read handle
	HANDLE_DIR	= 2,		// opendir
	HANDLE_STREAM	= 3,		// pipe, file_in, file_out, stdin_stream, stdout_stream: CStream
	HANDLE_PROCESS	= 4,		// spawn: CProcess
	HANDLE_RESERVED	= 5,		// taken, its object not there yet (no kapi finds it)
	HANDLE_OFILE	= 6,		// (v75) file_open: an open-file description (kern/ofile.h)
	HANDLE_LSOCK	= 7,		// (v76) a local socket's end (kern/lsock.h): its value is the socket number
	HANDLE_SHM	= 8		// (v76) a shared memory object (kern/lsock.h); nKind: KAPI_O_RDONLY / RDWR
};

// ... and, for a file or a directory, the volume behind it.
enum THandleKind
{
	HKIND_FATFS	= 0,		// FIL / DIR (the card, USB)
	HKIND_RAMFS	= 1,		// RAM: (kern/ramfs.h)
	HKIND_VFS	= 2		// a user-space provider's (kern/vfs.h)
};

struct THandleEntry
{
	void	*pObj;			// the kernel object; 0: a free slot
	u8	 nType;			// THandleType
	u8	 nKind;			// THandleKind (files, directories)
	u8	 nGen;			// 1..255: the generation in the handle's value
	u8	 bClosing;		// closed while pinned: closed by the last Unpin
	u16	 nPins;			// calls in flight with the object (they may yield)
};

class CHandleTable
{
public:
	CHandleTable (void);
	~CHandleTable (void);			// (CloseAll (TRUE) first: the owner's teardown)

	// A new entry for pObj (its reference handed over to the table). Returns the handle (never
	// 0), or 0 if the table is full or out of memory -- the caller then closes pObj itself.
	void *Add (void *pObj, unsigned nType, unsigned nKind = 0);

	// The object of handle h if it is an open entry of type nType (*pKind its kind), else 0.
	// Pin: also pins it; the caller passes *pIdx to Unpin when its call is over.
	void *Get (void *h, unsigned nType, unsigned *pKind = 0);
	void *Pin (void *h, unsigned nType, unsigned *pIdx, unsigned *pKind = 0);
	void  Unpin (unsigned nIdx);

	// Close handle h (of type nType): its object closed now, or by the last Unpin if a call
	// is using it. FALSE: not an open handle of that type.
	boolean Close (void *h, unsigned nType);

	// A handle taken before its object exists (kapi_spawn: the child must not start when no
	// handle can name it); Fill gives it its object (FALSE: h is not a reserved entry), Close
	// (h, HANDLE_RESERVED) frees it.
	void *Reserve (void)			{ return Add ((void *) this, HANDLE_RESERVED); }
	boolean Fill (void *h, void *pObj, unsigned nType, unsigned nKind = 0);

	// The handle already naming pObj (type nType), 0 if none.
	void *Find (const void *pObj, unsigned nType) const;

	// (v76) The type of open handle h (HANDLE_FREE: none).
	unsigned TypeOf (void *h) const;

	// The owner ends: every entry closed, pins ignored (no task of the process runs any more).
	// bTeardown: the reaper's teardown, the interrupts masked -- nothing may block or yield.
	void CloseAll (boolean bTeardown);

	unsigned GetCount (void) const		{ return m_nUsed; }

private:
	int  Lookup (void *h, unsigned nType) const;	// the entry's index, -1 if none
	void Free (unsigned nIdx);

private:
	THandleEntry *m_pEntry;
	unsigned      m_nSize;			// entries allocated
	unsigned      m_nUsed;			// entries holding an object
};

// The calling task's table: its process's, or the kernel's (a kernel task, no process; 0 only
// if that one could not be made: out of memory).
CHandleTable *HandlesCurrent (void);

// Close an object of type nType the way its kapi does (kapi_close, kapi_closedir,
// kapi_stream_close, kapi_wait's end). bTeardown: see CloseAll.
void HandleObjectClose (void *pObj, unsigned nType, unsigned nKind, boolean bTeardown);

// Drop a reference to a stream from a teardown (interrupts masked): now if it is not the last,
// else later, by the reaper task (HandlesRunDeferred) -- the last release of a file stream
// flushes it to the card, which may wait for the volume lock.
class CStream;
void HandlesDeferRelease (CStream *pStream);
void HandlesRunDeferred (void);			// the reaper's loop

// A spawned-process record (struct CProcess, kern/stream.h): drop a reference.
struct CProcess;
void ProcessRelease (CProcess *pProc);

#endif // _kern_handle_h
