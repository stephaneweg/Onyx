//
// ofiletest -- the kernel's open files of kapi v75 (kernel/sys/ofile.cpp: file_* / path_* /
// dir_read, WP-FILE/PROC, docs/POSIX-PLAN.md §3.2) on the PC: the real ofile.cpp over the real
// FatFs (the Circle fork's ff.c + diskio.cpp, on a RAM disk: SD: FAT32, SD1: exFAT, as the card's
// partitions) and the real RAM: (ramfs.cpp, RAMFS_HOST_TEST). The kernel around them is stubbed
// (one flow, no scheduler; the app's pointers are plain pointers, the ones under 4 KB "bad").
// On each volume: the open-flag matrix, random pread / pwrite / append / truncate through three
// descriptions of one file against a model (then the file read back through the OLD path, plain
// FatFs, after the close), O_APPEND from two handles, stat (times through the time zone, ino),
// unlink of an open file (hidden, deleted at the close), rename (across folders, onto an open
// file, of an open file, of a folder holding an open file), rmdir, dir_read (long names, ino =
// stat's), utime, the teardown's deferred close, the boot cleanup of leftovers.
//   ofiletest <seed> <ops>
// run_ofile_test.sh builds and runs it.
//
// MIT License. Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
// do so, subject to the following conditions: the above copyright notice and this permission
// notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE IS
// PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
// EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
#include "ff.h"
#include "diskio.h"
#include <circle/devicenameservice.h>
#include <circle/timer.h>
#include <kern/ofile.h>
#include <kern/handle.h>
#include <kern/ramfs.h>
#include <kern/vfs.h>
#include <kern/uaccess.h>
#include <kern/stream.h>
#include <kern/kapi_abi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <set>

// ---- the RAM disk and FatFs' OS glue -----------------------------------------------------------

CDevice *CDeviceNameService::s_pDisk = 0;

class CRamDisk : public CDevice
{
public:
	CRamDisk (size_t n) : m_Data (n, 0) {}
	int Read (void *p, size_t n) override { if (m_Off + n > m_Data.size ()) return -1; memcpy (p, &m_Data[m_Off], n); m_Off += n; return (int) n; }
	int Write (const void *p, size_t n) override { if (m_Off + n > m_Data.size ()) return -1; memcpy (&m_Data[m_Off], p, n); m_Off += n; return (int) n; }
	u64 Seek (u64 o) override { m_Off = o; return o; }
	u64 GetSize (void) const override { return m_Data.size (); }
	std::vector<u8> m_Data; size_t m_Off = 0;
};

// The clock: UTC s_now, the zone +120 min (FAT keeps the local time)
static const long long s_now = 1790416800;		// 2026-09-26 10:00:00 UTC
static const int s_tz = 120;

extern "C" {
void *ff_memalloc (UINT n) { return malloc (n); }
void ff_memfree (void *p) { free (p); }
DWORD get_fattime (void)
{
	time_t t = (time_t) (s_now + s_tz * 60);
	struct tm tm;
	gmtime_r (&t, &tm);
	return ((DWORD) (tm.tm_year + 1900 - 1980) << 25) | ((DWORD) (tm.tm_mon + 1) << 21) | ((DWORD) tm.tm_mday << 16)
	     | ((DWORD) tm.tm_hour << 11) | ((DWORD) tm.tm_min << 5) | ((DWORD) tm.tm_sec / 2);
}
}

static CTimer s_Timer;
CTimer *CTimer::Get (void) { return &s_Timer; }
int CTimer::GetTimeZone (void) const { return s_tz; }
unsigned CTimer::GetUniversalTime (void) const { return (unsigned) s_now; }
unsigned CTimer::GetClockTicks (void) { return 0; }

// ---- RAM:'s platform (ramfs.cpp, RAMFS_HOST_TEST) -------------------------------------------------

static std::set<void *> s_pages;
void *RamPlatPageAlloc (void) { if (s_pages.size () >= 4096) return 0; void *p = aligned_alloc (65536, 65536); s_pages.insert (p); return p; }
void RamPlatPageFree (void *p) { s_pages.erase (p); free (p); }
u64 RamPlatPagesFree (void) { return (4096 - s_pages.size ()) * 65536ull; }
static int s_task;
void *RamPlatTask (void) { return &s_task; }
void RamPlatYield (void) {}
unsigned RamPlatPid (void) { return 7; }
s64 RamPlatTime (void) { return s_now; }

// ---- the kernel around ofile.cpp ----------------------------------------------------------------

static inline bool Bad (const void *p) { return (uintptr) p < 4096; }
u64 UserRangeAvail (const void *p) { return Bad (p) ? 0 : ~(u64) 0; }
boolean UserRange (const void *p, u64 n) { return n == 0 || !Bad (p); }
boolean UserReadable (const void *p, u64 n) { return n == 0 || !Bad (p); }
boolean UserWritable (void *p, u64 n) { return n == 0 || !Bad (p); }
boolean UserCopyIn (void *k, const void *u, u64 n) { if (n && Bad (u)) return FALSE; memcpy (k, u, n); return TRUE; }
boolean UserCopyOut (void *u, const void *k, u64 n) { if (n && Bad (u)) return FALSE; memcpy (u, k, n); return TRUE; }
CUserStr::CUserStr (const char *pUser, u64 nMax, boolean)
:	m_pStr (0), m_pHeap (0), m_nLen (0), m_bNull (pUser == 0)
{
	if (pUser == 0 || Bad (pUser)) return;
	size_t n = strlen (pUser);
	if (n >= nMax) return;
	m_pHeap = new char[n + 1];
	memcpy (m_pHeap, pUser, n + 1);
	m_pStr = m_pHeap; m_nLen = n;
}
CUserStr::~CUserStr (void) { delete [] m_pHeap; }
extern "C" long UAccessStrLen (const char *p, u64 n) { u64 i = 0; while (i < n && p[i]) i++; return (long) i; }

boolean VfsHandles (const char *) { return FALSE; }
int VfsCall (int, const char *, const char *, long, long, long, const void *, unsigned, void *, unsigned, unsigned *) { return -1; }
int VfsReadDir (void *, struct kapi_dirent *) { return 0; }

const char *CurCwd (void) { return "SD:/"; }
// (v77) the program images' hook (kern/image.h): its own test is tools/tests/run_image_test.sh
unsigned g_nImageHooks;
void ImageFileChanged (const char *) { g_nImageHooks++; }

// (kernel/sys/kapi.cpp's, as it is)
static unsigned VolumePrefix (const char *p)
{
	auto alpha = [] (char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
	if (!alpha (p[0])) return 0;
	unsigned i = 1;
	while (alpha (p[i]) || (p[i] >= '0' && p[i] <= '9')) i++;
	return p[i] == ':' ? i + 1 : 0;
}
void ResolvePath (const char *pIn, char *pOut, unsigned nCap)
{
	char raw[512]; unsigned r = 0;
	auto put = [&] (const char *p, unsigned n) { for (unsigned i = 0; i < n && p[i] != '\0' && r < sizeof (raw) - 1; i++) raw[r++] = p[i]; };
	auto putVolume = [&] (const char *p, unsigned n)
	{
		if (n == 4 && (p[0] == 'S' || p[0] == 's') && (p[1] == 'D' || p[1] == 'd') && p[2] == '0') { put ("SD:", 3); return; }
		for (unsigned i = 0; i < n && r < sizeof (raw) - 1; i++) raw[r++] = p[i] >= 'a' && p[i] <= 'z' ? (char) (p[i] - 32) : p[i];
	};
	const char *cwd = CurCwd ();
	unsigned nIn = VolumePrefix (pIn);
	if (nIn) { putVolume (pIn, nIn); put (pIn + nIn, ~0u); }
	else if (pIn[0] == '/') { unsigned n = VolumePrefix (cwd); if (n) put (cwd, n); else put ("SD:", 3); put (pIn, ~0u); }
	else { put (cwd, ~0u); if (r < sizeof (raw) - 1) raw[r++] = '/'; put (pIn, ~0u); }
	raw[r] = '\0';
	unsigned nVol = VolumePrefix (raw);
	unsigned starts[64]; int depth = 0; unsigned o = 0;
	for (unsigned k = 0; k < nVol && o < nCap - 1; k++) pOut[o++] = raw[k];
	unsigned i = nVol;
	while (raw[i] != '\0')
	{
		while (raw[i] == '/') i++;
		if (raw[i] == '\0') break;
		unsigned j = i;
		while (raw[j] != '\0' && raw[j] != '/') j++;
		unsigned len = j - i;
		if (len == 1 && raw[i] == '.') {}
		else if (len == 2 && raw[i] == '.' && raw[i + 1] == '.') { if (depth > 0) o = starts[--depth]; }
		else
		{
			if (depth < 64) starts[depth++] = o;
			if (o < nCap - 1) pOut[o++] = '/';
			for (unsigned k = i; k < j && o < nCap - 1; k++) pOut[o++] = raw[k];
		}
		i = j;
	}
	if (o == nVol && o < nCap - 1) pOut[o++] = '/';
	pOut[o] = '\0';
}

extern "C" FRESULT ChunkedRead (FIL *pFile, void *pBuf, unsigned nLen, UINT *pDone)
{
	*pDone = 0; u8 *p = (u8 *) pBuf;
	while (nLen > 0)
	{
		UINT k = nLen > 65536 ? 65536 : nLen, n = 0;
		FRESULT r = f_read (pFile, p, k, &n);
		if (r != FR_OK) return r;
		*pDone += n; p += n; nLen -= n;
		if (n < k) break;
	}
	return FR_OK;
}
extern "C" FRESULT ChunkedWrite (FIL *pFile, const void *pBuf, unsigned nLen, UINT *pDone)
{
	*pDone = 0; const u8 *p = (const u8 *) pBuf;
	while (nLen > 0)
	{
		UINT k = nLen > 65536 ? 65536 : nLen, n = 0;
		FRESULT r = f_write (pFile, p, k, &n);
		if (r != FR_OK) return r;
		*pDone += n; p += n; nLen -= n;
		if (n < k) break;
	}
	return FR_OK;
}

// A handle table (kern/handle.h's model, without the pins' subtleties: one flow)
struct TEnt { void *pObj; unsigned nType, nKind; };
static std::vector<TEnt> s_Ent;
CHandleTable::CHandleTable (void) : m_pEntry (0), m_nSize (0), m_nUsed (0) {}
CHandleTable::~CHandleTable (void) {}
void *CHandleTable::Add (void *pObj, unsigned nType, unsigned nKind)
{
	s_Ent.push_back ({ pObj, nType, nKind });
	return (void *) (uintptr) ((1u << 16) | s_Ent.size ());
}
int CHandleTable::Lookup (void *h, unsigned nType) const
{
	uintptr v = (uintptr) h;
	unsigned i = (unsigned) (v & 0xFFFF);
	if (v > 0xFFFFFF || (v >> 16) != 1 || i == 0 || i > s_Ent.size ()) return -1;
	const TEnt &e = s_Ent[i - 1];
	return e.pObj != 0 && e.nType == nType ? (int) i - 1 : -1;
}
void *CHandleTable::Pin (void *h, unsigned nType, unsigned *pIdx, unsigned *pKind)
{
	int i = Lookup (h, nType);
	if (i < 0) return 0;
	*pIdx = (unsigned) i;
	if (pKind) *pKind = s_Ent[i].nKind;
	return s_Ent[i].pObj;
}
void CHandleTable::Unpin (unsigned) {}
boolean CHandleTable::Close (void *h, unsigned nType)
{
	int i = Lookup (h, nType);
	if (i < 0) return FALSE;
	void *p = s_Ent[i].pObj;
	s_Ent[i].pObj = 0;
	HandleObjectClose (p, nType, s_Ent[i].nKind, FALSE);
	return TRUE;
}
static CHandleTable s_Table;
CHandleTable *HandlesCurrent (void) { return &s_Table; }
void HandleObjectClose (void *pObj, unsigned nType, unsigned nKind, boolean bTeardown)
{
	if (nType == HANDLE_OFILE) OFileClose (pObj, bTeardown);
	else if (nType == HANDLE_DIR)
	{
		if (nKind == HKIND_RAMFS) RamFsCloseDir (pObj);
		else { f_closedir ((DIR *) pObj); delete (DIR *) pObj; }
	}
}

// kapi_opendir, as kapi.cpp makes it
static void *OpenDir (const char *pPath)
{
	char abs[300];
	ResolvePath (pPath, abs, sizeof abs);
	if (RamFsHandles (abs)) { void *d = RamFsOpenDir (abs); return d ? s_Table.Add (d, HANDLE_DIR, HKIND_RAMFS) : 0; }
	DIR *pDir = new DIR;
	if (f_opendir (pDir, abs) != FR_OK) { delete pDir; return 0; }
	OFileNoteDir (pDir, abs);
	return s_Table.Add (pDir, HANDLE_DIR, HKIND_FATFS);
}

extern "C" {
long long kapi_file_open (const char *, unsigned, unsigned);
long long kapi_file_read (long long, void *, unsigned long long, long long);
long long kapi_file_write (long long, const void *, unsigned long long, long long);
long long kapi_file_seek (long long, long long, int);
int kapi_file_truncate (long long, long long);
int kapi_file_sync (long long);
int kapi_file_stat (long long, struct kapi_stat *);
int kapi_file_close (long long);
int kapi_path_stat (const char *, struct kapi_stat *);
int kapi_path_unlink (const char *, unsigned);
int kapi_path_mkdir (const char *, unsigned);
int kapi_path_rename (const char *, const char *);
int kapi_path_utime (const char *, long long);
int kapi_dir_read (void *, struct kapi_dirent2 *);
}

// ---- the checks -----------------------------------------------------------------------------------

static unsigned s_seed;
static unsigned rnd (void) { s_seed = s_seed * 1103515245u + 12345u; return (s_seed >> 8) & 0xFFFFFF; }
static int fails = 0, checks = 0;
static std::string s_vol;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; if (fails < 40) { printf ("FAIL %s: ", s_vol.c_str ()); printf (__VA_ARGS__); printf ("\n"); } } } while (0)

static std::string P (const std::string &s) { return s_vol + ":/t" + s; }
static long long Size (const std::string &p) { kapi_stat st; int r = kapi_path_stat (p.c_str (), &st); return r < 0 ? r : (long long) st.size; }
static long long Put (const std::string &p, const std::string &d)
{
	long long h = kapi_file_open (p.c_str (), KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC, 0644);
	if (h < 0) return h;
	long long w = kapi_file_write (h, d.data (), d.size (), -1);
	kapi_file_close (h);
	return w;
}
static std::string Get (long long h, long long off = 0)
{
	std::string s (1 << 20, '\0');
	long long n = kapi_file_read (h, &s[0], s.size (), off);
	s.resize (n > 0 ? (size_t) n : 0);
	return s;
}
static std::string GetPath (const std::string &p)
{
	long long h = kapi_file_open (p.c_str (), KAPI_O_RDONLY, 0);
	std::string s = h > 0 ? Get (h) : "<none>";
	if (h > 0) kapi_file_close (h);
	return s;
}
static std::string Rand (size_t n) { std::string s (n, 0); for (auto &c : s) c = (char) rnd (); return s; }
static bool Listed (const std::string &dir, const std::string &name, kapi_dirent2 *pOut = 0)
{
	void *d = OpenDir (dir.c_str ());
	bool found = false;
	kapi_dirent2 e;
	while (d && kapi_dir_read (d, &e) == 1) if (name == e.name) { found = true; if (pOut) *pOut = e; }
	if (d) s_Table.Close (d, HANDLE_DIR);
	return found;
}

static void flags (void)
{
	std::string f = P ("/flags");
	char b[64];
	CHECK (kapi_file_open (f.c_str (), KAPI_O_RDONLY, 0) == -KAPI_ENOENT, "ENOENT");
	long long h = kapi_file_open (f.c_str (), KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_EXCL, 0);
	CHECK (h > 0 && kapi_file_write (h, "hello", 5, -1) == 5, "create + write");
	CHECK (kapi_file_read (h, b, 5, 0) == -KAPI_EBADF, "WRONLY read");
	CHECK (kapi_file_open (f.c_str (), KAPI_O_RDWR | KAPI_O_CREAT | KAPI_O_EXCL, 0) == -KAPI_EEXIST, "EXCL while open");
	kapi_file_close (h);
	CHECK (kapi_file_open (f.c_str (), KAPI_O_RDWR | KAPI_O_CREAT | KAPI_O_EXCL, 0) == -KAPI_EEXIST, "EXCL");
	CHECK (Size (f) == 5, "size %lld", Size (f));
	h = kapi_file_open (f.c_str (), KAPI_O_RDWR | KAPI_O_APPEND, 0);
	kapi_file_write (h, "ab", 2, -1);
	kapi_file_seek (h, 0, KAPI_SEEK_SET);
	kapi_file_write (h, "cd", 2, -1);
	CHECK (Get (h) == "helloabcd", "append: %s", Get (h).c_str ());
	CHECK (kapi_file_seek (h, -2, KAPI_SEEK_END) == 7 && kapi_file_seek (h, 3, KAPI_SEEK_CUR) == 10, "seek");
	CHECK (kapi_file_seek (h, -1, KAPI_SEEK_SET) == -KAPI_EINVAL, "seek < 0");
	long long h2 = kapi_file_open (f.c_str (), KAPI_O_WRONLY | KAPI_O_TRUNC, 0);
	CHECK (Size (f) == 0 && Get (h) == "", "TRUNC seen by the other description");
	kapi_file_close (h); kapi_file_close (h2);
	CHECK (kapi_file_open ((s_vol + ":/t").c_str (), KAPI_O_RDONLY, 0) == -KAPI_EISDIR, "EISDIR");
	CHECK (kapi_file_open ((s_vol + ":/").c_str (), KAPI_O_RDONLY, 0) == -KAPI_EISDIR, "EISDIR root");
	CHECK (kapi_file_open (P ("/no/x").c_str (), KAPI_O_WRONLY | KAPI_O_CREAT, 0) == -KAPI_ENOENT, "ENOENT folder");
	CHECK (kapi_file_open (f.c_str (), 3, 0) == -KAPI_EINVAL, "mode 3");
	CHECK (kapi_file_open ((const char *) 8, 0, 0) == -KAPI_EFAULT, "EFAULT");
	CHECK (kapi_file_read (12345, b, 1, 0) == -KAPI_EBADF && kapi_file_close (12345) == -KAPI_EBADF, "EBADF");
	if (s_vol != "RAM")
	{
		std::string longn = P ("/") + std::string (256, 'x');
		CHECK (kapi_file_open (longn.c_str (), KAPI_O_WRONLY | KAPI_O_CREAT, 0) == -KAPI_ENAMETOOLONG, "256 characters: %lld",
		       kapi_file_open (longn.c_str (), KAPI_O_WRONLY | KAPI_O_CREAT, 0));
	}
	kapi_path_unlink (f.c_str (), 0);
}

static void model (unsigned ops)
{
	std::string f = P ("/model");
	long long a = kapi_file_open (f.c_str (), KAPI_O_RDWR | KAPI_O_CREAT, 0);
	long long b = kapi_file_open (f.c_str (), KAPI_O_RDWR, 0);
	long long ap = kapi_file_open (f.c_str (), KAPI_O_WRONLY | KAPI_O_APPEND, 0);
	long long ro = kapi_file_open (f.c_str (), KAPI_O_RDONLY, 0);
	std::string m;
	u64 ofsA = 0;
	for (unsigned i = 0; i < ops; i++)
	{
		long long h = (rnd () & 1) ? a : b;
		unsigned op = rnd () % 20;
		u64 off = rnd () % (m.size () + 200000);
		unsigned len = rnd () % 70000 + 1;
		if (op < 6)
		{
			std::string d = Rand (len);
			long long w = kapi_file_write (h, d.data (), len, (long long) off);
			CHECK (w == len, "pwrite %llu+%u: %lld", (unsigned long long) off, len, w);
			if (m.size () < off + len) m.resize (off + len, '\0');
			memcpy (&m[off], d.data (), len);
		}
		else if (op < 7)
		{
			std::string d = Rand (len % 3000);
			CHECK (kapi_file_write (ap, d.data (), d.size (), -1) == (long long) d.size (), "append");
			m += d;
		}
		else if (op < 8)
		{
			u64 n = rnd () % (m.size () + 100000);
			CHECK (kapi_file_truncate (h, (long long) n) == 0, "truncate");
			m.resize (n, '\0');
		}
		else if (op < 10)					// the offset of a's description
		{
			std::string d = Rand (len % 5000);
			long long w = kapi_file_write (a, d.data (), d.size (), -1);
			CHECK (w == (long long) d.size (), "write at the offset");
			if (m.size () < ofsA + d.size ()) m.resize (ofsA + d.size (), '\0');
			memcpy (&m[ofsA], d.data (), d.size ());
			ofsA += d.size ();
			CHECK (kapi_file_seek (a, 0, KAPI_SEEK_CUR) == (long long) ofsA, "the offset advanced");
			if (rnd () % 4 == 0) { ofsA = rnd () % (m.size () + 1000); kapi_file_seek (a, (long long) ofsA, KAPI_SEEK_SET); }
		}
		else if (op < 11)
		{
			CHECK (kapi_file_sync (h) == 0, "sync");
		}
		else
		{
			std::string got (len, '\x55');
			long long r = kapi_file_read ((rnd () & 1) ? ro : h, &got[0], len, (long long) off);
			u64 want = off >= m.size () ? 0 : std::min<u64> (m.size () - off, len);
			CHECK (r == (long long) want && memcmp (got.data (), m.data () + (off < m.size () ? off : 0), want) == 0,
			       "pread %llu+%u: %lld (want %llu)", (unsigned long long) off, len, r, (unsigned long long) want);
		}
		if (fails > 20) break;
	}
	kapi_stat st;
	CHECK (kapi_file_stat (ro, &st) == 0 && st.size == m.size (), "fstat size %llu / %zu", (unsigned long long) st.size, m.size ());
	kapi_file_close (a); kapi_file_close (b); kapi_file_close (ap); kapi_file_close (ro);
	CHECK (Size (f) == (long long) m.size (), "size after the close");
	if (s_vol != "RAM")						// (the old path: plain FatFs)
	{
		FIL fil; UINT n = 0;
		std::string got (m.size (), '\0');
		CHECK (f_open (&fil, f.c_str (), FA_READ) == FR_OK, "f_open");
		f_read (&fil, &got[0], (UINT) got.size (), &n);
		f_close (&fil);
		CHECK (n == m.size () && got == m, "FatFs reads the same after the close");
	}
	else CHECK (GetPath (f) == m, "read back");
	kapi_path_unlink (f.c_str (), 0);
}

static void names (void)
{
	kapi_stat st, st2;
	std::string d1 = P ("/d1"), d2 = P ("/d2");
	CHECK (kapi_path_mkdir (d1.c_str (), 0) == 0 && kapi_path_mkdir (d1.c_str (), 0) == -KAPI_EEXIST, "mkdir");
	CHECK (kapi_path_mkdir (P ("/no/pe").c_str (), 0) == -KAPI_ENOENT, "mkdir ENOENT");
	kapi_path_mkdir (d2.c_str (), 0);

	// unlink while open
	std::string f = d1 + "/open";
	Put (f, "still here");
	long long h = kapi_file_open (f.c_str (), KAPI_O_RDWR, 0);
	CHECK (kapi_path_unlink (f.c_str (), 0) == 0 && kapi_path_stat (f.c_str (), &st) == -KAPI_ENOENT, "unlink while open");
	CHECK (!Listed (d1, "open"), "not listed");
	CHECK (Get (h) == "still here" && kapi_file_write (h, "!", 1, 10) == 1, "still readable, writable");
	CHECK (kapi_file_stat (h, &st) == 0 && st.size == 11, "fstat after the unlink");
	CHECK (Put (f, "new") == 3 && GetPath (f) == "new", "a new file of that name");
	CHECK (Get (h) == "still here!", "the old one: %s", Get (h).c_str ());
	if (s_vol != "RAM")
	{
		FILINFO fi;
		std::string hid = s_vol + ":/.~onyx-deleted";
		CHECK (f_stat (hid.c_str (), &fi) == FR_OK && (fi.fattrib & AM_HID), "the hidden folder");
		CHECK (!Listed (s_vol + ":/", ".~onyx-deleted"), "not listed by dir_read");
		kapi_file_close (h);
		CHECK (f_stat (hid.c_str (), &fi) == FR_NO_FILE, "deleted at the close, the folder too");
	}
	else kapi_file_close (h);
	kapi_path_unlink (f.c_str (), 0);

	// rename
	std::string a = d1 + "/a", b = d2 + "/b";
	Put (a, "AAA");
	CHECK (kapi_path_rename (a.c_str (), b.c_str ()) == 0 && GetPath (b) == "AAA" && Size (a) == -KAPI_ENOENT, "across folders");
	Put (a, "NEW!");
	long long hb = kapi_file_open (b.c_str (), KAPI_O_RDONLY, 0);
	CHECK (kapi_path_rename (a.c_str (), b.c_str ()) == 0 && GetPath (b) == "NEW!", "onto an open file");
	CHECK (Get (hb) == "AAA", "the replaced one readable: %s", Get (hb).c_str ());
	kapi_file_close (hb);
	long long hw = kapi_file_open (b.c_str (), KAPI_O_RDWR, 0);
	CHECK (kapi_path_rename (b.c_str (), a.c_str ()) == 0, "an open file");
	CHECK (kapi_file_write (hw, "++", 2, -1) == 2 && kapi_file_write (hw, "xyz", 3, 10) == 3, "written after");
	kapi_file_close (hw);
	CHECK (GetPath (a) == std::string ("++W!\0\0\0\0\0\0xyz", 13), "at its new path: %s", GetPath (a).c_str ());
	CHECK (kapi_path_rename (a.c_str (), d2.c_str ()) == -KAPI_EISDIR, "file onto folder");
	CHECK (kapi_path_rename (d2.c_str (), a.c_str ()) == -KAPI_ENOTDIR, "folder onto file");
	CHECK (kapi_path_rename (d1.c_str (), (d1 + "/in").c_str ()) == -KAPI_EINVAL, "into itself");
	CHECK (kapi_path_rename (a.c_str (), (s_vol == "RAM" ? "SD:/x" : "RAM:/x")) == -KAPI_EXDEV, "EXDEV");
	CHECK (kapi_path_rename (a.c_str (), a.c_str ()) == 0, "onto itself");
	std::string c = d2 + "/c", d3 = P ("/d3");
	Put (c, "in d2");
	h = kapi_file_open (c.c_str (), KAPI_O_RDWR, 0);
	CHECK (kapi_path_rename (d2.c_str (), d3.c_str ()) == 0, "a folder holding an open file");
	CHECK (kapi_file_seek (h, 0, KAPI_SEEK_END) == 5 && kapi_file_write (h, "!", 1, -1) == 1, "written after");
	CHECK (kapi_file_stat (h, &st) == 0 && kapi_path_stat ((d3 + "/c").c_str (), &st2) == 0 && st.ino == st2.ino, "its ino = its new path's");
	kapi_file_close (h);
	CHECK (GetPath (d3 + "/c") == "in d2!", "in the renamed folder: %s", GetPath (d3 + "/c").c_str ());
	CHECK (kapi_path_rename (d3.c_str (), d2.c_str ()) == 0, "back");
	// a folder replacing an empty one; not a full one
	std::string e1 = P ("/e1"), e2 = P ("/e2");
	kapi_path_mkdir (e1.c_str (), 0); kapi_path_mkdir (e2.c_str (), 0);
	CHECK (kapi_path_rename (e1.c_str (), e2.c_str ()) == 0 && kapi_path_stat (e1.c_str (), &st) == -KAPI_ENOENT, "a folder onto an empty one");
	CHECK (kapi_path_rename (e2.c_str (), d2.c_str ()) == -KAPI_ENOTEMPTY, "onto a full one");
	kapi_path_unlink (e2.c_str (), KAPI_UNLINK_DIR);
	// the case of a name
	CHECK (kapi_path_rename (a.c_str (), (d1 + "/A").c_str ()) == 0 && Listed (d1, "A") && !Listed (d1, "a"), "the case changed");
	a = d1 + "/A";

	// rmdir
	CHECK (kapi_path_unlink (d1.c_str (), KAPI_UNLINK_DIR) == -KAPI_ENOTEMPTY, "ENOTEMPTY");
	CHECK (kapi_path_unlink (d1.c_str (), 0) == -KAPI_EISDIR, "EISDIR");
	CHECK (kapi_path_unlink (a.c_str (), KAPI_UNLINK_DIR) == -KAPI_ENOTDIR, "ENOTDIR");
	CHECK (kapi_path_unlink ((d1 + "/zz").c_str (), 0) == -KAPI_ENOENT, "ENOENT");

	// stat, utime, dir_read
	CHECK (kapi_path_stat (a.c_str (), &st) == 0 && (st.mode & KAPI_S_IFMT) == KAPI_S_IFREG && st.size == 13, "stat");
	CHECK (st.mtime >= s_now - 2 && st.mtime <= s_now, "mtime through the zone: %lld (now %lld)", (long long) st.mtime, s_now);
	CHECK (kapi_path_utime (a.c_str (), 1000000000LL) == 0 && kapi_path_stat (a.c_str (), &st) == 0 && st.mtime == 1000000000LL,
	       "utime: %lld", (long long) st.mtime);
	h = kapi_file_open (a.c_str (), KAPI_O_RDWR, 0);
	kapi_file_write (h, "w", 1, 0);
	CHECK (kapi_file_stat (h, &st) == 0 && st.mtime == s_now, "written: mtime now (%lld)", (long long) st.mtime);
	CHECK (kapi_path_utime (a.c_str (), 1200000000LL) == 0 && kapi_file_stat (h, &st) == 0 && st.mtime == 1200000000LL,
	       "utime of an open written file: %lld", (long long) st.mtime);
	kapi_file_close (h);
	CHECK (kapi_path_stat (a.c_str (), &st) == 0 && st.mtime == 1200000000LL, "kept after its close: %lld", (long long) st.mtime);
	int len = s_vol == "RAM" ? 127 : 200;
	std::string longn;
	for (int i = 0; i < len; i++) longn += (char) ('a' + i % 26);
	Put (d1 + "/" + longn, "12345");
	kapi_dirent2 de;
	CHECK (Listed (d1, longn, &de) && de.size == 5, "dir_read: %d characters", len);
	CHECK (kapi_path_stat ((d1 + "/" + longn).c_str (), &st) == 0 && st.ino == de.ino && st.mtime == de.mtime, "dir_read's ino = stat's");
	CHECK (kapi_path_stat ((s_vol + ":/").c_str (), &st) == 0 && (st.mode & KAPI_S_IFMT) == KAPI_S_IFDIR, "the root");
	kapi_path_unlink ((d1 + "/" + longn).c_str (), 0);
	kapi_path_unlink (a.c_str (), 0);
	kapi_path_unlink (c.c_str (), 0);
	CHECK (kapi_path_unlink (d1.c_str (), KAPI_UNLINK_DIR) == 0 && kapi_path_unlink (d2.c_str (), KAPI_UNLINK_DIR) == 0, "rmdir");
}

static void growth (void)
{
	std::string f = P ("/grow");
	long long h = kapi_file_open (f.c_str (), KAPI_O_RDWR | KAPI_O_CREAT | KAPI_O_TRUNC, 0);
	CHECK (kapi_file_write (h, "AB", 2, -1) == 2, "write");
	CHECK (kapi_file_truncate (h, 300000) == 0 && Size (f) == 300000, "truncate grows");
	std::string s = Get (h);
	CHECK (s.size () == 300000 && s[0] == 'A' && s.find_first_not_of ('\0', 2) == std::string::npos, "zeros");
	CHECK (kapi_file_write (h, "Z", 1, 1000000) == 1 && Size (f) == 1000001, "a write far past the end");
	s = Get (h);
	CHECK (s.size () == 1000001 && s[1000000] == 'Z' && s.find_first_not_of ('\0', 2) == 1000000, "the gap: zeros");
	CHECK (kapi_file_truncate (h, 1) == 0 && Get (h) == "A", "shrink");
	CHECK (kapi_file_truncate (h, -1) == -KAPI_EINVAL, "EINVAL");
	// teardown: the close deferred, done by the reaper
	OFileClose (s_Ent[(size_t) ((h & 0xFFFF) - 1)].pObj, TRUE);
	s_Ent[(size_t) ((h & 0xFFFF) - 1)].pObj = 0;
	OFileRunDeferred ();
	CHECK (Size (f) == 1, "the deferred close");
	// append from two handles
	long long h1 = kapi_file_open (f.c_str (), KAPI_O_WRONLY | KAPI_O_APPEND | KAPI_O_TRUNC, 0);
	long long h2 = kapi_file_open (f.c_str (), KAPI_O_WRONLY | KAPI_O_APPEND, 0);
	std::string want;
	for (int i = 0; i < 20; i++)
	{
		kapi_file_write (h1, "AAAAAAAAAA", 10, -1); kapi_file_write (h2, "BBBBBBBBBB", 10, -1);
		want += "AAAAAAAAAABBBBBBBBBB";
	}
	kapi_file_close (h1); kapi_file_close (h2);
	CHECK (GetPath (f) == want, "append from two handles");
	kapi_path_unlink (f.c_str (), 0);
}

static void boot_cleanup (void)
{
	s_vol = "boot";
	f_mkdir ("SD:/.~onyx-deleted");
	FIL fil; UINT n;
	f_open (&fil, "SD:/.~onyx-deleted/1f", FA_WRITE | FA_CREATE_ALWAYS); f_write (&fil, "x", 1, &n); f_close (&fil);
	f_open (&fil, "SD:/.~onyx-deleted/20", FA_WRITE | FA_CREATE_ALWAYS); f_write (&fil, "y", 1, &n); f_close (&fil);
	f_mkdir ("SD1:/.~onyx-deleted");
	OFileBootCleanup ();
	FILINFO fi;
	CHECK (f_stat ("SD:/.~onyx-deleted", &fi) == FR_NO_FILE && f_stat ("SD1:/.~onyx-deleted", &fi) == FR_NO_FILE, "leftovers removed");
}

int main (int argc, char **argv)
{
	unsigned nSeed = argc > 1 ? (unsigned) atoi (argv[1]) : 1;
	s_seed = nSeed;
	unsigned ops = argc > 2 ? (unsigned) atoi (argv[2]) : 2000;
	CRamDisk *disk = new CRamDisk (200u << 20);
	CDeviceNameService::s_pDisk = disk;
	static BYTE work[FF_MAX_SS * 8];
	LBA_t plist[] = { 50, 50, 0 };
	if (f_fdisk (0, plist, work) != FR_OK) { printf ("fdisk\n"); return 1; }
	VolToPart[0].pt = 1;
	MKFS_PARM fat = { FM_FAT32, 0, 0, 0, 512 }, ex = { FM_EXFAT, 0, 0, 0, 4096 };
	if (f_mkfs ("SD:", &fat, work, sizeof work) != FR_OK) { printf ("mkfs SD:\n"); return 1; }
	VolToPart[0].pt = 0;
	if (f_mkfs ("SD1:", &ex, work, sizeof work) != FR_OK) { printf ("mkfs SD1:\n"); return 1; }
	static FATFS fs0, fs1;
	if (f_mount (&fs0, "SD:", 1) != FR_OK || f_mount (&fs1, "SD1:", 1) != FR_OK) { printf ("mount\n"); return 1; }
	RamFsInit ("64");
	DWORD nFree0[2], nFree1[2];
	FATFS *pFs;
	f_getfree ("SD:", &nFree0[0], &pFs); f_getfree ("SD1:", &nFree0[1], &pFs);
	boot_cleanup ();
	const char *vols[] = { "SD", "SD1", "RAM" };
	for (const char *v : vols)
	{
		s_vol = v;
		kapi_path_mkdir ((s_vol + ":/t").c_str (), 0);
		flags ();
		model (ops);
		names ();
		growth ();
		CHECK (kapi_path_unlink ((s_vol + ":/t").c_str (), KAPI_UNLINK_DIR) == 0, "the test folder removed (all closed)");
	}
	// every cluster back (a FIL writing through a stale directory entry would leak some): the
	// volumes mounted again, their free space counted from the FAT / the bitmap (FF_FS_NOFSINFO 3)
	f_unmount ("SD:"); f_unmount ("SD1:");
	f_mount (&fs0, "SD:", 1); f_mount (&fs1, "SD1:", 1);
	f_getfree ("SD:", &nFree1[0], &pFs); f_getfree ("SD1:", &nFree1[1], &pFs);
	s_vol = "end";
	CHECK (nFree1[0] == nFree0[0] && nFree1[1] == nFree0[1], "free clusters: SD: %u -> %u, SD1: %u -> %u",
	       (unsigned) nFree0[0], (unsigned) nFree1[0], (unsigned) nFree0[1], (unsigned) nFree1[1]);
	printf ("seed %u: %d checks, %d failures (SD: FAT32, SD1: exFAT, RAM:)\n", nSeed, checks, fails);
	return fails ? 1 : 0;
}
