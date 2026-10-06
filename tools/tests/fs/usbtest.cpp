//
// usbtest -- the USB volumes' FatFs side on the PC (kapi v93, kernel/sys/volume.cpp; the fork's
// patches: ff.c's checks after the volume lock, ffsystem.cpp's mutex kept, FF_USE_MKFS / LABEL):
//   - a stick formatted as Windows does (an MBR, one partition) in FAT32 and exFAT, with a label,
//     found by USB1:'s auto search; a "superfloppy" (no MBR) too;
//   - a stick pulled out while a file is written: the calls fail, nothing crashes, the volume
//     lock is never left held, the old file objects stay invalid after another stick is mounted;
//   - an unmount while a task waits for the volume lock (another task's eject): the waiter gets
//     FR_INVALID_OBJECT / FR_NOT_ENABLED and gives the lock back (the fork's re-check).
// Built with FF_FS_REENTRANT 1 and a mutex of its own (the kernel's is a sleeping lock).
//
#include "ff.h"
#include "diskio.h"
#include <circle/devicenameservice.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

CDevice *CDeviceNameService::s_pDisk = 0;

class CRamDisk : public CDevice
{
public:
	CRamDisk (size_t n) : m_Data (n, 0) {}
	int Read (void *p, size_t n) override { if (m_bGone || m_Off + n > m_Data.size ()) return -1; memcpy (p, &m_Data[m_Off], n); m_Off += n; return (int) n; }
	int Write (const void *p, size_t n) override { if (m_bGone || m_Off + n > m_Data.size ()) return -1; memcpy (&m_Data[m_Off], p, n); m_Off += n; return (int) n; }
	u64 Seek (u64 o) override { m_Off = o; return o; }
	u64 GetSize (void) const override { return m_Data.size (); }
	std::vector<u8> m_Data; size_t m_Off = 0; bool m_bGone = false;
};

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf ("FAIL: "); printf (__VA_ARGS__); printf ("\n"); } } while (0)

// The OS glue: a volume lock that counts (it must be free at every check), and a hook that runs
// "another task's" unmount while the caller waits for the lock.
static int s_nDepth[FF_VOLUMES + 1];
static bool s_bUnmountWhileWaiting = false;
extern "C" {
void *ff_memalloc (UINT n) { return malloc (n); }
void ff_memfree (void *p) { free (p); }
DWORD get_fattime (void) { return ((DWORD) (2026 - 1980) << 25) | (10 << 21) | (6 << 16); }
int ff_mutex_create (int) { return 1; }
void ff_mutex_delete (int) {}
int ff_mutex_take (int vol)
{
	if (s_bUnmountWhileWaiting && vol == 4)	// (USB1)
	{
		s_bUnmountWhileWaiting = false;
		f_mount (0, "USB1:", 0);			// (the other task, which held the lock, unmounted)
	}
	s_nDepth[vol]++;
	return 1;
}
void ff_mutex_give (int vol) { s_nDepth[vol]--; }
}

static bool LockFree (void) { for (int v = 0; v <= FF_VOLUMES; v++) if (s_nDepth[v] != 0) return false; return true; }

static BYTE work[FF_MAX_SS * 64];

static CRamDisk *Plug (size_t nBytes)
{
	CRamDisk *d = new CRamDisk (nBytes);
	CDeviceNameService::s_pUsb = d;
	return d;
}
static void Pull (CRamDisk *d)			// (Circle: the device deleted, its removed handlers called)
{
	d->m_bGone = true;
	CDeviceNameService::s_pUsb = 0;
	d->Remove ();
}

static void FormatAndCheck (const char *what, BYTE fmt, size_t nBytes, BYTE wantType, bool sfd)
{
	CRamDisk *d = Plug (nBytes);
	MKFS_PARM opt = { (BYTE) (fmt | (sfd ? FM_SFD : 0)), 2, 0, 0, 0 };
	FRESULT r = f_mkfs ("USB1:", &opt, work, sizeof work);
	CHECK (r == FR_OK, "%s: mkfs %d", what, (int) r);
	bool mbr = d->m_Data[510] == 0x55 && d->m_Data[511] == 0xAA && d->m_Data[0x1BE + 4] != 0;
	CHECK (sfd ? !mbr || d->m_Data[0x1BE + 4] == 0 || d->m_Data[3] != 0 : mbr, "%s: partition table %s", what, mbr ? "present" : "absent");
	FATFS fs;
	r = f_mount (&fs, "USB1:", 1);
	CHECK (r == FR_OK && fs.fs_type == wantType, "%s: mount %d, type %d", what, (int) r, fs.fs_type);
	CHECK (f_setlabel ("USB1:ONYX STICK") == FR_OK, "%s: setlabel", what);
	char label[36] = ""; DWORD vsn = 0;
	CHECK (f_getlabel ("USB1:", label, &vsn) == FR_OK && strcmp (label, "ONYX STICK") == 0, "%s: label '%s'", what, label);
	CHECK (f_setlabel ("USB1:BAD*NAME") != FR_OK, "%s: a bad label taken", what);
	FIL f; UINT n;
	CHECK (f_open (&f, "USB1:/hello.txt", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK && f_write (&f, "hello", 5, &n) == FR_OK && f_close (&f) == FR_OK, "%s: write", what);
	f_mount (0, "USB1:", 0);
	CHECK (f_mount (&fs, "USB1:", 1) == FR_OK, "%s: mounted again", what);
	char b[8] = "";
	CHECK (f_open (&f, "USB1:/hello.txt", FA_READ) == FR_OK && f_read (&f, b, 5, &n) == FR_OK && n == 5 && memcmp (b, "hello", 5) == 0, "%s: read back", what);
	f_close (&f);
	f_mount (0, "USB1:", 0);
	CHECK (LockFree (), "%s: a volume lock left held", what);
	Pull (d);
	delete d;
	printf ("%s %s\n", fails ? "FAIL" : "ok  ", what);
}

int main (void)
{
	FormatAndCheck ("USB stick, FAT32, MBR (as Windows)", FM_FAT32, 96u << 20, FS_FAT32, false);
	FormatAndCheck ("USB stick, exFAT, MBR", FM_EXFAT, 64u << 20, FS_EXFAT, false);
	FormatAndCheck ("USB stick, FAT32, superfloppy (no MBR)", FM_FAT32, 96u << 20, FS_FAT32, true);
	FormatAndCheck ("USB stick, FAT16 (auto, small)", FM_ANY, 16u << 20, FS_FAT16, false);

	// pulled out while a file is being written
	int f0 = fails;
	CRamDisk *d = Plug (96u << 20);
	MKFS_PARM opt = { FM_FAT32, 2, 0, 0, 0 };
	f_mkfs ("USB1:", &opt, work, sizeof work);
	static FATFS fs;
	CHECK (f_mount (&fs, "USB1:", 1) == FR_OK, "pull: mount");
	FIL w, r2; DIR dir; UINT n;
	CHECK (f_open (&w, "USB1:/big.bin", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK, "pull: create");
	CHECK (f_write (&w, work, 20000, &n) == FR_OK, "pull: write");
	CHECK (f_open (&r2, "USB1:/big.bin", FA_READ) == FR_OK, "pull: open to read");
	CHECK (f_opendir (&dir, "USB1:/") == FR_OK, "pull: opendir");
	Pull (d);
	FRESULT r = f_write (&w, work, 20000, &n);
	CHECK (r != FR_OK, "pull: a write after the pull succeeded");
	CHECK (f_close (&w) != FR_OK, "pull: close after the pull succeeded");
	FILINFO fi;
	CHECK (f_readdir (&dir, &fi) != FR_OK, "pull: readdir after the pull succeeded");
	CHECK (f_open (&w, "USB1:/other", FA_READ) != FR_OK, "pull: an open after the pull succeeded");
	CHECK (LockFree (), "pull: a volume lock left held");
	f_mount (0, "USB1:", 0);				// (VolPoll: the volume unregistered)
	CHECK (f_open (&w, "USB1:/big.bin", FA_READ) == FR_NOT_ENABLED, "pull: unmounted volume opened");
	// another stick in the same place, mounted with the same FATFS object: the old objects stay invalid
	CRamDisk *d2 = Plug (96u << 20);
	f_mkfs ("USB1:", &opt, work, sizeof work);
	CHECK (f_mount (&fs, "USB1:", 1) == FR_OK, "pull: the second stick mounted");
	char b[16];
	CHECK (f_read (&r2, b, 16, &n) == FR_INVALID_OBJECT, "pull: an old file reads the new stick");
	CHECK (f_readdir (&dir, &fi) == FR_INVALID_OBJECT, "pull: an old folder reads the new stick");
	CHECK (LockFree (), "pull: a volume lock left held (2)");
	printf ("%s pulled out while writing: errors, no lock held, old objects invalid on the next stick\n", fails > f0 ? "FAIL" : "ok  ");

	// an unmount while waiting for the lock (another task ejects meanwhile)
	f0 = fails;
	FIL g;
	CHECK (f_open (&g, "USB1:/x.txt", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK, "wait: create");
	s_bUnmountWhileWaiting = true;
	r = f_write (&g, "abc", 3, &n);
	CHECK (r == FR_INVALID_OBJECT, "wait: a write on an unmounted volume: %d", (int) r);
	CHECK (LockFree (), "wait: the lock left held after the write");
	CHECK (f_mount (&fs, "USB1:", 1) == FR_OK, "wait: mounted again");
	s_bUnmountWhileWaiting = true;
	r = f_open (&g, "USB1:/y.txt", FA_WRITE | FA_CREATE_ALWAYS);
	CHECK (r == FR_NOT_ENABLED, "wait: an open on an unmounted volume: %d", (int) r);
	CHECK (LockFree (), "wait: the lock left held after the open");
	CHECK (f_stat ("USB1:/y.txt", &fi) == FR_NOT_ENABLED, "wait: the volume came back by itself");
	printf ("%s unmounted while a call waited for the volume lock: FR_INVALID_OBJECT / FR_NOT_ENABLED, lock given back\n", fails > f0 ? "FAIL" : "ok  ");
	f_mount (0, "USB1:", 0);
	Pull (d2);

	// a device with two partitions (kernel/sys/volume.cpp: USB1P1:, USB1P2:), each its own file system
	f0 = fails;
	CRamDisk *d3 = Plug (160u << 20);
	LBA_t plist[] = { 50, 50, 0 };
	CHECK (f_fdisk (1, plist, work) == FR_OK, "parts: fdisk");
	MKFS_PARM fat = { FM_FAT32, 2, 0, 0, 512 }, ex = { FM_EXFAT, 1, 0, 0, 0 };
	CHECK ((r = f_mkfs ("USB1P1:", &fat, work, sizeof work)) == FR_OK, "parts: mkfs USB1P1: %d", (int) r);
	CHECK ((r = f_mkfs ("USB1P2:", &ex, work, sizeof work)) == FR_OK, "parts: mkfs USB1P2: %d", (int) r);
	static FATFS p1, p2;
	CHECK (f_mount (&p1, "USB1P1:", 1) == FR_OK && p1.fs_type == FS_FAT32, "parts: USB1P1: FAT32");
	CHECK (f_mount (&p2, "USB1P2:", 1) == FR_OK && p2.fs_type == FS_EXFAT, "parts: USB1P2: exFAT");
	CHECK (f_open (&g, "USB1P1:/one.txt", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK && f_close (&g) == FR_OK, "parts: a file on P1");
	CHECK (f_stat ("USB1P2:/one.txt", &fi) == FR_NO_FILE, "parts: P1's file seen on P2");
	CHECK (f_stat ("USB1P1:/one.txt", &fi) == FR_OK, "parts: P1's file lost");
	CHECK (d3->m_Data[510] == 0x55 && d3->m_Data[0x1BE + 4] != 0 && d3->m_Data[0x1CE + 4] != 0, "parts: the MBR's two entries");
	f_mount (0, "USB1P1:", 0); f_mount (0, "USB1P2:", 0);
	// the whole device formatted again (USB1:): one partition, P2 gone
	MKFS_PARM one = { FM_FAT32, 2, 0, 0, 0 };
	CHECK (f_mkfs ("USB1:", &one, work, sizeof work) == FR_OK, "parts: the whole device formatted");
	CHECK (d3->m_Data[0x1CE + 4] == 0, "parts: a second partition left");
	CHECK (f_mount (&fs, "USB1:", 1) == FR_OK, "parts: USB1: mounted after");
	f_mount (0, "USB1:", 0);
	CHECK (LockFree (), "parts: a volume lock left held");
	printf ("%s two partitions: USB1P1: FAT32 and USB1P2: exFAT apart, the whole device made one again\n", fails > f0 ? "FAIL" : "ok  ");
	Pull (d3);
	delete d; delete d2; delete d3;
	return fails ? 1 : 0;
}
