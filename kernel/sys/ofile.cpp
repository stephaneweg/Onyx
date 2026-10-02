//
// ofile.cpp -- open files with POSIX semantics (HANDLE_OFILE): file_open / read / write / seek /
// truncate / sync / stat / close, path_stat / unlink / mkdir / rename / utime, dir_read,
// stream_write_nb (kapi v75 slots 207..221, docs/POSIX-PLAN.md §3.2).
//
// Owner: WP-FILE/PROC. This is the WP-0 skeleton: every entry returns -KAPI_ENOSYS and the hooks
// do nothing, until WP-FILE/PROC lands.
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
#include <kern/kapi_abi.h>

void OFileClose (void *pObj, boolean bTeardown)
{
	(void) pObj; (void) bTeardown;		// (no HANDLE_OFILE object is made yet)
}

extern "C" {

long long kapi_file_open (const char *pPath, unsigned nFlags, unsigned nMode)
{
	return -KAPI_ENOSYS;
}

long long kapi_file_read (long long h, void *pBuf, unsigned long long nLen, long long nOff)
{
	return -KAPI_ENOSYS;
}

long long kapi_file_write (long long h, const void *pBuf, unsigned long long nLen, long long nOff)
{
	return -KAPI_ENOSYS;
}

long long kapi_file_seek (long long h, long long nOff, int nWhence)
{
	return -KAPI_ENOSYS;
}

int kapi_file_truncate (long long h, long long nSize)
{
	return -KAPI_ENOSYS;
}

int kapi_file_sync (long long h)
{
	return -KAPI_ENOSYS;
}

int kapi_file_stat (long long h, struct kapi_stat *pOut)
{
	return -KAPI_ENOSYS;
}

int kapi_file_close (long long h)
{
	return -KAPI_ENOSYS;
}

int kapi_path_stat (const char *pPath, struct kapi_stat *pOut)
{
	return -KAPI_ENOSYS;
}

int kapi_path_unlink (const char *pPath, unsigned nFlags)
{
	return -KAPI_ENOSYS;
}

int kapi_path_mkdir (const char *pPath, unsigned nMode)
{
	return -KAPI_ENOSYS;
}

int kapi_path_rename (const char *pFrom, const char *pTo)
{
	return -KAPI_ENOSYS;
}

int kapi_path_utime (const char *pPath, long long nMTime)
{
	return -KAPI_ENOSYS;
}

int kapi_dir_read (void *hDir, struct kapi_dirent2 *pOut)
{
	return -KAPI_ENOSYS;
}

int kapi_stream_write_nb (void *h, const void *pBuf, unsigned nLen)
{
	return -KAPI_ENOSYS;
}

}
