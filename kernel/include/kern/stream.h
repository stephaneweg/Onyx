//
// stream.h -- byte streams for the stdio system. A task has a stdin + stdout stream
// (default 0 = none); the terminal wires children's streams to pipes or files for
// redirection / pipelines. Pipes block cooperatively (Yield) when empty/full.
//
// Refcounted + shared: a pipe is held by both the writer (a child's stdout) and the
// reader (the terminal). The WRITER signals EOF via CloseWrite(); the last Release()
// frees it.
//
#ifndef _kern_stream_h
#define _kern_stream_h

#include <circle/types.h>
#include <fatfs/ff.h>

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
	// Signal "no more data will be written" so readers see EOF.
	virtual void CloseWrite (void) {}

	void AddRef (void)	{ m_nRef++; }
	void Release (void)	{ if (--m_nRef <= 0) delete this; }

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
	void CloseWrite (void) override;

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

// Spawned-process handle: the child sets bDone/nStatus on exit; the waiter polls it.
// Outlives the task (the waiter frees it), so it never dangles on the reaped CTask.
struct CProcess
{
	volatile boolean bDone;
	int              nStatus;
};

#endif // _kern_stream_h
