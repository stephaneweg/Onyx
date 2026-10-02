//
// stream.h -- byte streams for the stdio system. A task has a stdin + stdout stream
// (default 0 = none); the terminal wires children's streams to pipes or files for
// redirection / pipelines. Pipes block cooperatively when empty/full (v75: they sleep in IoWait,
// kern/iowait.h, and every change of a pipe's state -- written, drained, closed -- calls IoWake).
//
// Refcounted + shared: a pipe is held by both the writer (a child's stdout) and the
// reader (the terminal). The WRITER signals EOF via CloseWrite(); the last Release()
// frees it.
//
#ifndef _kern_stream_h
#define _kern_stream_h

#include <circle/types.h>
#include <fatfs/ff.h>
#include <kern/kapi_abi.h>		// KAPI_POLL* (v75)

#define PIPE_CAP	8192		// pipe ring-buffer size (bytes)

class CStream
{
public:
	CStream (void) : m_nRef (1) {}
	virtual ~CStream (void) {}

	// Read up to nLen bytes. Blocks (cooperatively) until >=1 byte or EOF; returns
	// the count, or 0 at EOF, or -1 on error.
	virtual int Read (void *pBuf, unsigned nLen) = 0;
	// Non-blocking read: >0 bytes, 0 = EOF, -1 = would block (no data yet). Default
	// just blocks (fine for files, which never "would block").
	virtual int ReadNonBlocking (void *pBuf, unsigned nLen) { return Read (pBuf, nLen); }
	// Write nLen bytes (may block until space). Returns bytes written, or -1.
	virtual int Write (const void *pBuf, unsigned nLen) = 0;
	// (v75) Non-blocking write: what fits now (> 0), -2 = would block (full), -1 = error. Default:
	// Write (a file never "would block").
	virtual int WriteNonBlocking (const void *pBuf, unsigned nLen) { return Write (pBuf, nLen); }
	// Signal "no more data will be written" so readers see EOF.
	virtual void CloseWrite (void) {}
	// (v75) The poll bits ready now (KAPI_POLLIN / OUT / HUP...): poll asks it without blocking.
	// Default: always readable and writable (a file). A pipe answers for itself (WP-FILE/PROC).
	virtual unsigned PollMask (void) { return KAPI_POLLIN | KAPI_POLLOUT; }

	void AddRef (void)	{ m_nRef++; }
	void Release (void)	{ if (--m_nRef <= 0) delete this; }
	int GetRefs (void) const { return m_nRef; }

protected:
	int m_nRef;
};

// In-memory FIFO between a writer task and a reader task.
class CPipeStream : public CStream
{
public:
	CPipeStream (void);
	int Read (void *pBuf, unsigned nLen) override;
	int ReadNonBlocking (void *pBuf, unsigned nLen) override;
	int Write (const void *pBuf, unsigned nLen) override;
	int WriteNonBlocking (const void *pBuf, unsigned nLen) override;	// (v75)
	void CloseWrite (void) override;
	unsigned PollMask (void) override;	// (v75) IN: data (or HUP | IN: closed), OUT: room

private:
	unsigned Put (const u8 *p, unsigned nLen);	// what fits, copied in
	unsigned Get (u8 *p, unsigned nLen);		// what is there, copied out

private:
	u8 m_Buf[PIPE_CAP];
	volatile unsigned m_nHead;	// next write slot
	volatile unsigned m_nTail;	// next read slot
	volatile boolean  m_bWriteClosed;
};

// A FatFs file as a stream. nMode: 0 = read, 1 = write (truncate), 2 = append.
class CFileStream : public CStream
{
public:
	CFileStream (const char *pPath, int nMode);
	~CFileStream (void);
	boolean IsValid (void) const { return m_bOpen; }
	int Read (void *pBuf, unsigned nLen) override;
	int Write (const void *pBuf, unsigned nLen) override;

private:
	FIL     m_File;
	boolean m_bOpen;
};

// A file of the RAM volume (kern/ramfs.h) as a stream. nMode as CFileStream's.
class CRamStream : public CStream
{
public:
	CRamStream (const char *pPath, int nMode);
	~CRamStream (void);
	boolean IsValid (void) const { return m_pFile != 0; }
	int Read (void *pBuf, unsigned nLen) override;
	int Write (const void *pBuf, unsigned nLen) override;

private:
	void *m_pFile;
};

// Spawned-process record: the child sets bDone/nStatus on exit; the waiter polls it (v75:
// proc_wait sleeps in IoWait, the child's end calls IoWake). nReason: KAPI_PROC_* and nPid, set
// when the child's address space exists / ends (sys/procx.cpp).
// Outlives the task, so it never dangles on the reaped CTask. Refcounted (kern/handle.h):
// one ref for the spawner's handle, one for the child (its task, then its address space);
// ProcessRelease frees it with the last, so a spawner that dies first no longer leaks it.
struct CProcess
{
	volatile boolean bDone;
	int              nStatus;
	int              nRef;
	int              nReason;		// (v75) KAPI_PROC_EXITED / FAULT / KILLED / OOM
	volatile unsigned nPid;			// (v75) the child's pid (0 until its space exists)
};

#endif // _kern_stream_h
