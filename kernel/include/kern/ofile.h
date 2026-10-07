//
// ofile.h -- open files with POSIX semantics: one node per open file, open-file descriptions
// with their own 64-bit offset (HANDLE_OFILE), stat, unlink / rename of open files (kapi v75,
// docs/POSIX-PLAN.md §3.2, docs/02 §8 "v75: files and processes").
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
#ifndef _kern_ofile_h
#define _kern_ofile_h

#include <kern/handle.h>
#include <circle/types.h>
#include <fatfs/ff.h>

// Close a HANDLE_OFILE object (HandleObjectClose: file_close, the process's teardown).
// bTeardown: the reaper's teardown, the interrupts masked -- nothing may block or yield: the
// close is then queued and done by OFileRunDeferred.
void OFileClose (void *pObj, boolean bTeardown);

// The closes a teardown queued (the reaper's loop, HandlesRunDeferred: a task context).
void OFileRunDeferred (void);

// Boot (CKernel::StartAutostart): the files unlinked while open that a crash left behind
// (<volume>:/.~onyx-deleted/) removed from the card's volumes.
void OFileBootCleanup (void);

// kapi_opendir made pDir (a FatFs DIR) for the directory pAbs: kept so that dir_read can give
// each entry its ino (the FNV-1a hash of its upper-cased absolute path).
void OFileNoteDir (const void *pDir, const char *pAbs);

// A path an app gave that CUserStr refused -> -KAPI_EFAULT, or -KAPI_ENAMETOOLONG (no NUL in
// UPATH_MAX bytes).
int UserPathErr (const char *pUser);

// ---- shared with sys/kapi.cpp (defined there) ----------------------------------------------------

// An app's path made absolute and clean ("SD:/a/b", "RAM:/x", see kapi.cpp).
void ResolvePath (const char *pIn, char *pOut, unsigned nCap);
// The calling task's current directory ("SD:/" for a kernel task).
const char *CurCwd (void);
// FatFs transfers in 64 KB pieces with a Yield between them (in kapi.cpp's extern "C" part).
extern "C" FRESULT ChunkedRead (FIL *pFile, void *pBuf, unsigned nLen, UINT *pDone);
extern "C" FRESULT ChunkedWrite (FIL *pFile, const void *pBuf, unsigned nLen, UINT *pDone);

// A handle of the calling process pinned for the length of one call (a call that may yield:
// another thread closing it meanwhile only marks it; see kern/handle.h).
class CHandlePin
{
public:
	CHandlePin (void *h, unsigned nType)
	:	m_pTable (HandlesCurrent ()), m_pObj (0), m_nIdx (0), m_nKind (0)
	{
		if (m_pTable != 0) m_pObj = m_pTable->Pin (h, nType, &m_nIdx, &m_nKind);
	}
	~CHandlePin (void)
	{
		if (m_pObj != 0) m_pTable->Unpin (m_nIdx);
	}
	void *Obj (void) const		{ return m_pObj; }
	unsigned Kind (void) const	{ return m_nKind; }

private:
	CHandlePin (const CHandlePin &);
	CHandlePin &operator= (const CHandlePin &);

	CHandleTable *m_pTable;
	void	     *m_pObj;
	unsigned      m_nIdx;
	unsigned      m_nKind;
};

#endif // _kern_ofile_h
