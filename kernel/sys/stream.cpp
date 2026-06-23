//
// stream.cpp -- pipe + file stream implementations (see stream.h).
//
#include <kern/stream.h>
#include <circle/sched/scheduler.h>

static void StreamYield (void)
{
	if (CScheduler::IsActive ())
	{
		CScheduler::Get ()->Yield ();		// let the other end of the pipe run
	}
}

// ---- CPipeStream ------------------------------------------------------------

CPipeStream::CPipeStream (void)
:	m_nHead (0), m_nTail (0), m_bWriteClosed (FALSE)
{
}

int CPipeStream::Read (void *pBuf, unsigned nLen)
{
	// Block until at least one byte is available or the writer closed.
	while (m_nHead == m_nTail && !m_bWriteClosed)
	{
		StreamYield ();
	}

	u8 *p = (u8 *) pBuf;
	unsigned n = 0;
	while (n < nLen && m_nTail != m_nHead)
	{
		p[n++] = m_Buf[m_nTail];
		m_nTail = (m_nTail + 1) % PIPE_CAP;
	}
	return (int) n;					// 0 => EOF (empty + write closed)
}

int CPipeStream::Write (const void *pBuf, unsigned nLen)
{
	const u8 *p = (const u8 *) pBuf;
	unsigned n = 0;
	while (n < nLen)
	{
		unsigned nNext = (m_nHead + 1) % PIPE_CAP;
		if (nNext == m_nTail)			// full: wait for the reader to drain
		{
			StreamYield ();
			continue;
		}
		m_Buf[m_nHead] = p[n++];
		m_nHead = nNext;
	}
	return (int) n;
}

void CPipeStream::CloseWrite (void)
{
	m_bWriteClosed = TRUE;
}

// ---- CFileStream ------------------------------------------------------------

CFileStream::CFileStream (const char *pPath, int nMode)
:	m_bOpen (FALSE)
{
	BYTE flags = (nMode == 0) ? FA_READ
		   : (nMode == 2) ? (FA_WRITE | FA_OPEN_APPEND)
				  : (FA_WRITE | FA_CREATE_ALWAYS);
	if (f_open (&m_File, pPath, flags) == FR_OK)
	{
		m_bOpen = TRUE;
	}
}

CFileStream::~CFileStream (void)
{
	if (m_bOpen)
	{
		f_close (&m_File);			// flushes pending writes
	}
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
