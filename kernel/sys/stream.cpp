//
// stream.cpp -- pipe + file stream implementations (see stream.h).
//
#include <kern/stream.h>
#include <kern/ramfs.h>
#include <kern/iowait.h>		// (v75) a pipe's waits and wakes
#include <kern/image.h>			// (v77) ImageFileChanged: a program file written
#include <kern/volume.h>		// (v92) VolTrack
#include <circle/sched/scheduler.h>

// A pipe's waits: until the generation moves (kern/iowait.h: a pipe written, drained or closed
// calls IoWake), at most PIPE_WAIT_MS before the state is checked again.
#define PIPE_WAIT_MS	50

static void PipeWait (u32 nGen)
{
	if (CScheduler::IsActive ())
	{
		IoWait (nGen, PIPE_WAIT_MS);		// (the other end of the pipe runs meanwhile)
	}
}

// ---- CPipeStream ------------------------------------------------------------

CPipeStream::CPipeStream (void)
:	m_nHead (0), m_nTail (0), m_bWriteClosed (FALSE), m_nWriters (1)
{
}

unsigned CPipeStream::Put (const u8 *p, unsigned nLen)
{
	unsigned n = 0;
	while (n < nLen)
	{
		unsigned nNext = (m_nHead + 1) % PIPE_CAP;
		if (nNext == m_nTail) break;			// full
		m_Buf[m_nHead] = p[n++];
		m_nHead = nNext;
	}
	if (n > 0) IoWake ();				// (a reader may wait)
	return n;
}

unsigned CPipeStream::Get (u8 *p, unsigned nLen)
{
	unsigned n = 0;
	while (n < nLen && m_nTail != m_nHead)
	{
		p[n++] = m_Buf[m_nTail];
		m_nTail = (m_nTail + 1) % PIPE_CAP;
	}
	if (n > 0) IoWake ();				// (room: a writer may wait)
	return n;
}

int CPipeStream::Read (void *pBuf, unsigned nLen)
{
	// Block until at least one byte is available or the writer closed.
	for (;;)
	{
		u32 nGen = IoGen ();
		if (m_nHead != m_nTail || m_bWriteClosed) break;
		PipeWait (nGen);
	}
	return (int) Get ((u8 *) pBuf, nLen);		// 0 => EOF (empty + write closed)
}

int CPipeStream::Write (const void *pBuf, unsigned nLen)
{
	const u8 *p = (const u8 *) pBuf;
	unsigned n = 0;
	while (n < nLen)
	{
		u32 nGen = IoGen ();
		unsigned k = Put (p + n, nLen - n);
		n += k;
		if (k == 0) PipeWait (nGen);		// full: wait for the reader to drain
	}
	return (int) n;
}

int CPipeStream::WriteNonBlocking (const void *pBuf, unsigned nLen)
{
	if (nLen == 0) return 0;
	unsigned n = Put ((const u8 *) pBuf, nLen);
	return n > 0 ? (int) n : -2;			// (-2: full)
}

int CPipeStream::ReadNonBlocking (void *pBuf, unsigned nLen)
{
	if (m_nHead == m_nTail)
	{
		return m_bWriteClosed ? 0 : -1;		// EOF, or "no data yet"
	}
	return (int) Get ((u8 *) pBuf, nLen);
}

void CPipeStream::CloseWrite (void)
{
	if (m_nWriters > 1)				// (v76: a write end elsewhere still writes)
	{
		m_nWriters--;
		return;
	}
	m_bWriteClosed = TRUE;
	IoWake ();					// (a reader sees the end)
}

unsigned CPipeStream::PollMask (void)
{
	unsigned nMask = 0;
	if (m_nHead != m_nTail) nMask |= KAPI_POLLIN;
	if (m_bWriteClosed) nMask |= KAPI_POLLIN | KAPI_POLLHUP;
	if ((m_nHead + 1) % PIPE_CAP != m_nTail) nMask |= KAPI_POLLOUT;
	return nMask;
}

// ---- CFileStream ------------------------------------------------------------

CFileStream::CFileStream (const char *pPath, int nMode)
:	m_bOpen (FALSE)
{
	BYTE flags = (nMode == 0) ? FA_READ
		   : (nMode == 2) ? (FA_WRITE | FA_OPEN_APPEND)
				  : (FA_WRITE | FA_CREATE_ALWAYS);
	// (v77) A file opened for writing: the image of a program at that path loses its name now
	// (kern/image.h), and again at the close (one made from the half-written file meanwhile).
	unsigned n = 0;
	if (nMode != 0)
	{
		for (; pPath[n] != '\0' && n < sizeof m_Written - 1; n++) m_Written[n] = pPath[n];
		ImageFileChanged (pPath);
	}
	m_Written[n] = '\0';
	if (f_open (&m_File, pPath, flags) == FR_OK)
	{
		m_bOpen = TRUE;
		VolTrack (&m_File.obj, nMode != 0 ? &m_File : 0);	// (v92: an eject counts it)
	}
}

CFileStream::~CFileStream (void)
{
	if (m_bOpen)
	{
		f_close (&m_File);			// flushes pending writes
		VolUntrack (&m_File.obj);
	}
	if (m_Written[0] != '\0') ImageFileChanged (m_Written);
}

int CFileStream::Read (void *pBuf, unsigned nLen)
{
	if (!m_bOpen) return 0;
	UINT nRead = 0;
	if (f_read (&m_File, pBuf, nLen, &nRead) != FR_OK) return -1;
	return (int) nRead;				// 0 => EOF
}

int CFileStream::Write (const void *pBuf, unsigned nLen)
{
	if (!m_bOpen) return -1;
	UINT nWritten = 0;
	if (f_write (&m_File, pBuf, nLen, &nWritten) != FR_OK) return -1;
	return (int) nWritten;
}

// ---- CRamStream (RAM:, kern/ramfs.h) ---------------------------------------------

CRamStream::CRamStream (const char *pPath, int nMode)
:	m_pFile (RamFsStreamOpen (pPath, nMode))
{
}

CRamStream::~CRamStream (void)
{
	RamFsStreamClose (m_pFile);			// (a written file's last chunk fitted)
}

int CRamStream::Read (void *pBuf, unsigned nLen)
{
	return m_pFile != 0 ? RamFsStreamRead (m_pFile, pBuf, nLen) : 0;
}

int CRamStream::Write (const void *pBuf, unsigned nLen)
{
	return m_pFile != 0 ? RamFsStreamWrite (m_pFile, pBuf, nLen) : -1;
}
