//
// fstest -- the Onyx sector cache (circle/addon/fatfs/diskio.cpp) under FatFs, on the PC:
// a RAM disk, a FAT32 volume, then thousands of random file operations (create, write,
// append, overwrite, read back, delete, rename, list) checked against a model in memory.
//   fstest <seed> <ops> <cache 0|1> <image-out>
// run_fs_test.sh runs it with the cache on and off: both must pass, give the same image
// byte for byte, and the cache must save device reads.
//
#include "ff.h"
#include "diskio.h"
#include <circle/devicenameservice.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

CDevice *CDeviceNameService::s_pDisk = 0;

class CRamDisk : public CDevice
{
public:
	CRamDisk (size_t n) : m_Data (n, 0) {}
	int Read (void *p, size_t n) override { if (m_Off + n > m_Data.size ()) return -1; memcpy (p, &m_Data[m_Off], n); m_Off += n; reads++; readBytes += n; return (int) n; }
	int Write (const void *p, size_t n) override { if (m_Off + n > m_Data.size ()) return -1; memcpy (&m_Data[m_Off], p, n); m_Off += n; writes++; return (int) n; }
	u64 Seek (u64 o) override { m_Off = o; return o; }
	u64 GetSize (void) const override { return m_Data.size (); }
	std::vector<u8> m_Data; size_t m_Off = 0;
	unsigned long reads = 0, writes = 0, readBytes = 0;
};

// FatFs OS glue (the real one, ffsystem.cpp, uses Circle's locks: not needed here)
extern "C" {
void *ff_memalloc (UINT n) { return malloc (n); }
void ff_memfree (void *p) { free (p); }
DWORD get_fattime (void) { return ((DWORD) (2026 - 1980) << 25) | (9 << 21) | (26 << 16); }
}

static unsigned s_seed;
static unsigned rnd (void) { s_seed = s_seed * 1103515245u + 12345u; return (s_seed >> 8) & 0xFFFFFF; }
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 20) { printf ("FAIL: "); printf (__VA_ARGS__); printf ("\n"); } } } while (0)

// The SD card's layout for Onyx (ffconf.h FF_MULTI_PARTITION, diskio.cpp VolToPart): an MBR,
// partition 1 FAT32 (the boot one: SD:), partition 2 exFAT (SD1:); both mounted, a file on each.
static int parts (void)
{
	CRamDisk *disk = new CRamDisk (160u << 20);
	CDeviceNameService::s_pDisk = disk;
	static BYTE work[FF_MAX_SS * 8];
	LBA_t plist[] = { 40, 60, 0 };				// (percentages of the disk)
	CHECK (f_fdisk (0, plist, work) == FR_OK, "fdisk");
	VolToPart[0].pt = 1;					// (f_mkfs on "SD:" = partition 1, not a new table)
	MKFS_PARM fat = { FM_FAT32, 0, 0, 0, 512 }, ex = { FM_EXFAT, 0, 0, 0, 4096 };
	CHECK (f_mkfs ("SD:", &fat, work, sizeof work) == FR_OK, "mkfs SD: (FAT32)");
	VolToPart[0].pt = 0;					// back to Onyx's: the card's first FAT volume
	CHECK (f_mkfs ("SD1:", &ex, work, sizeof work) == FR_OK, "mkfs SD1: (exFAT)");
	FATFS fs0, fs1, fs2;
	CHECK (f_mount (&fs0, "SD:", 1) == FR_OK, "mount SD:");
	CHECK (f_mount (&fs1, "SD1:", 1) == FR_OK, "mount SD1:");
	CHECK (f_mount (&fs2, "SD2:", 1) != FR_OK, "SD2: (no partition 3) mounted");
	CHECK (fs0.fs_type == FS_FAT32 && fs1.fs_type == FS_EXFAT, "types %d %d", fs0.fs_type, fs1.fs_type);
	const char *files[2] = { "SD:/kernel8-rpi4.img", "SD1:/roms.txt" };
	for (int i = 0; i < 2; i++)
	{
		FIL f; UINT n;
		CHECK (f_open (&f, files[i], FA_WRITE | FA_CREATE_ALWAYS) == FR_OK, "create %s", files[i]);
		f_write (&f, files[i], (UINT) strlen (files[i]), &n); f_close (&f);
	}
	for (int i = 0; i < 2; i++)
	{
		FIL f; UINT n; char b[64] = "";
		CHECK (f_open (&f, files[i], FA_READ) == FR_OK, "open %s", files[i]);
		f_read (&f, b, sizeof b - 1, &n); b[n] = 0; f_close (&f);
		CHECK (strcmp (b, files[i]) == 0, "read back %s: %s", files[i], b);
	}
	FILINFO fi;
	CHECK (f_stat ("SD1:/kernel8-rpi4.img", &fi) == FR_NO_FILE, "SD: file seen on SD1:");
	CHECK (f_stat ("SD:/roms.txt", &fi) == FR_NO_FILE, "SD1: file seen on SD:");
	printf ("%s partitions: SD: FAT32, SD1: exFAT, SD2: none\n", fails ? "FAIL" : "ok  ");
	return fails ? 1 : 0;
}

int main (int argc, char **argv)
{
	if (argc > 1 && strcmp (argv[1], "parts") == 0) return parts ();
	s_seed = argc > 1 ? (unsigned) atoi (argv[1]) : 1;
	int ops = argc > 2 ? atoi (argv[2]) : 3000;
	int cache = argc > 3 ? atoi (argv[3]) : 1;
	CRamDisk *disk = new CRamDisk (96u << 20);
	CDeviceNameService::s_pDisk = disk;
	disk_cache_enable (cache);

	static BYTE work[FF_MAX_SS * 8];
	bool exfat = argc > 6 && strcmp (argv[6], "exfat") == 0;	// (the user partitions' format)
	MKFS_PARM opt = { (BYTE) (exfat ? FM_EXFAT : FM_FAT32), 0, 0, 0, (UINT) (argc > 5 ? atoi (argv[5]) : 1024) };	// (FAT32 needs >= 65526 clusters: 1 KB ones on 96 MB)
	{ FRESULT mr = f_mkfs ("SD:", &opt, work, sizeof work); CHECK (mr == FR_OK, "mkfs %d", (int) mr); }
	FATFS fs;
	CHECK (f_mount (&fs, "SD:", 1) == FR_OK, "mount");

	// directories: a big one (like SD:/apps), a few small ones
	const char *dirs[] = { "SD:/apps", "SD:/docs", "SD:/roms", "SD:/roms/gb" };
	for (auto d : dirs) CHECK (f_mkdir (d) == FR_OK, "mkdir %s", d);
	std::map<std::string, std::string> model;
	auto path = [&] (int i) {
		char p[96]; int d = i % 4;
		snprintf (p, sizeof p, "%s/file with a long name %04d.dat", dirs[d], i);
		return std::string (p);
	};
	auto writeFile = [&] (const std::string &p, const std::string &data, bool append) {
		FIL f; UINT n;
		if (f_open (&f, p.c_str (), append ? (FA_WRITE | FA_OPEN_APPEND) : (FA_WRITE | FA_CREATE_ALWAYS)) != FR_OK) return false;
		bool ok = f_write (&f, data.data (), (UINT) data.size (), &n) == FR_OK && n == data.size ();
		return f_close (&f) == FR_OK && ok;
	};
	auto readFile = [&] (const std::string &p, std::string &out) {
		FIL f; UINT n;
		if (f_open (&f, p.c_str (), FA_READ) != FR_OK) return false;
		out.resize (f_size (&f));
		bool ok = out.empty () || (f_read (&f, &out[0], (UINT) out.size (), &n) == FR_OK && n == out.size ());
		f_close (&f);
		return ok;
	};
	auto payload = [&] () {
		unsigned sizes[] = { 0, 17, 511, 512, 513, 4096, 30000, 131072, 200001 };
		unsigned n = sizes[rnd () % 9] + rnd () % 64;
		std::string s (n, 0); unsigned x = rnd ();
		for (unsigned i = 0; i < n; i++) { x = x * 1664525u + 1013904223u; s[i] = (char) (x >> 24); }
		return s;
	};

	for (int op = 0; op < ops; op++)
	{
		int i = (int) (rnd () % 400);
		std::string p = path (i);
		unsigned what = rnd () % 100;
		bool exists = model.count (p) != 0;
		if (what < 35)						// create / overwrite
		{
			std::string d = payload ();
			CHECK (writeFile (p, d, false), "write %s", p.c_str ());
			model[p] = d;
		}
		else if (what < 45 && exists)				// append
		{
			std::string d = payload ().substr (0, 3000);
			CHECK (writeFile (p, d, true), "append %s", p.c_str ());
			model[p] += d;
		}
		else if (what < 75 && exists)				// read back
		{
			std::string got;
			CHECK (readFile (p, got) && got == model[p], "read %s", p.c_str ());
		}
		else if (what < 85 && exists)				// delete
		{
			CHECK (f_unlink (p.c_str ()) == FR_OK, "unlink %s", p.c_str ());
			model.erase (p);
		}
		else if (what < 90 && exists)				// rename (to a free name)
		{
			std::string q = path (i + 400 * (1 + (int) (rnd () % 3)));
			if (!model.count (q))
			{
				CHECK (f_rename (p.c_str (), q.c_str ()) == FR_OK, "rename %s", p.c_str ());
				model[q] = model[p]; model.erase (p);
			}
		}
		else if (what < 95)					// list a folder: exactly the model's files
		{
			const char *d = dirs[rnd () % 4];
			DIR dir; FILINFO fi; int n = 0, want = 0;
			for (auto &kv : model) { size_t s = kv.first.rfind ('/'); if (kv.first.substr (0, s) == d) want++; }
			CHECK (f_opendir (&dir, d) == FR_OK, "opendir %s", d);
			while (f_readdir (&dir, &fi) == FR_OK && fi.fname[0])
			{
				if (fi.fattrib & AM_DIR) continue;
				std::string full = std::string (d) + "/" + fi.fname;
				CHECK (model.count (full) && model[full].size () == fi.fsize, "list %s: %s", d, fi.fname);
				n++;
			}
			f_closedir (&dir);
			CHECK (n == want, "list %s: %d files, want %d", d, n, want);
		}
		else if (what < 97)					// remount (the cache must forget the volume)
		{
			f_unmount ("SD:");
			CHECK (f_mount (&fs, "SD:", 1) == FR_OK, "remount");
		}
	}
	// everything back, after a remount
	f_unmount ("SD:");
	CHECK (f_mount (&fs, "SD:", 1) == FR_OK, "final mount");
	for (auto &kv : model) { std::string got; CHECK (readFile (kv.first, got) && got == kv.second, "final %s", kv.first.c_str ()); }
	f_unmount ("SD:");

	unsigned hits = 0, misses = 0; disk_cache_stats (&hits, &misses);
	printf ("cache %d: %d ops, %zu files, device reads %lu (%lu KB), writes %lu, cache hits %u misses %u, %s\n",
		cache, ops, model.size (), disk->reads, disk->readBytes / 1024, disk->writes, hits, misses, fails ? "FAILED" : "ok");
	if (argc > 4) { FILE *o = fopen (argv[4], "wb"); fwrite (disk->m_Data.data (), 1, disk->m_Data.size (), o); fclose (o); }
	return fails ? 1 : 0;
}
