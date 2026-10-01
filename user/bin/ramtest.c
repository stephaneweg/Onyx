//
// ramtest -- the RAM: volume (the kernel's RAM file system, docs/02 *The RAM: volume*) on the Pi:
//   ramtest          the checks (each "ok" / "FAIL"), then "ALL PASS" or the failures' count;
//   ramtest full     also fills the volume (a save that does not fit must fail and leave no half
//                    file; what was written is given back after).
// Everything happens in RAM:/ramtest (removed at the end); vol_info's numbers before and after
// must be the same (the memory given back). Prints the write / read speed of a 16 MB file.
//
#include "kapi.h"
#include "applib.h"

#define BIG	(16u << 20)
static unsigned char s_a[BIG], s_b[BIG];
static int s_fails;

static unsigned long long now_us (void)
{
	unsigned long long c, f;
	__asm__ volatile ("mrs %0, cntpct_el0" : "=r" (c));
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (f));
	return f ? c * 1000000ull / f : 0;
}

static void put_u (unsigned long long v) { char t[24]; int k = 0; do { t[k++] = (char) ('0' + v % 10); v /= 10; } while (v); char o[24]; int n = 0; while (k) o[n++] = t[--k]; o[n] = 0; ax_puts (o); }

static void check (int ok, const char *what)
{
	ax_puts (ok ? "  ok    " : "  FAIL  ");
	ax_putln (what);
	if (!ok) s_fails++;
}

static void fill (unsigned char *p, unsigned n, unsigned seed)
{
	unsigned x = seed;
	for (unsigned i = 0; i < n; i++) { x = x * 1664525u + 1013904223u; p[i] = (unsigned char) (x >> 24); }
}

static int same (const unsigned char *a, const unsigned char *b, unsigned n)
{
	for (unsigned i = 0; i < n; i++) if (a[i] != b[i]) return 0;
	return 1;
}

// the whole file read back with open / read (in pieces of `piece`) -> its length, -1 none
static int read_back (const char *path, unsigned char *dst, unsigned cap, unsigned piece)
{
	void *h = kapi_open (path);
	if (h == 0) return -1;
	unsigned got = 0; int r;
	while (got < cap && (r = kapi_read (h, dst + got, cap - got < piece ? cap - got : piece)) > 0) got += (unsigned) r;
	kapi_close (h);
	return (int) got;
}

static int listed (const char *dir, const char *name, unsigned *size, int *is_dir)
{
	void *d = kapi_opendir (dir);
	if (d == 0) return 0;
	struct kapi_dirent e; int found = 0;
	while (kapi_readdir (d, &e))
		if (ax_streq (e.name, name)) { found = 1; if (size) *size = e.size; if (is_dir) *is_dir = e.is_dir; }
	kapi_closedir (d);
	return found;
}

int main (void)
{
	char args[64];
	kapi_get_args (args, sizeof args);
	int full = args[0] == 'f';
	struct kapi_vol_info v0, v1;

	if (kapi_vol_info ("RAM:", &v0) != 0)
	{
		ax_putln ("ramtest: no RAM: volume (an older kernel, or ramfs=0 in SD:/etc/system.ini)");
		return 1;
	}
	ax_puts ("RAM: "); put_u (v0.total >> 20); ax_puts (" MB, "); put_u (v0.used >> 10); ax_puts (" KB used, ");
	put_u (v0.free >> 20); ax_puts (" MB free, "); put_u (v0.files); ax_puts (" files, "); put_u (v0.dirs); ax_putln (" folders");
	kapi_remove ("RAM:/ramtest/sub/deeper"); kapi_remove ("RAM:/ramtest/sub"); kapi_remove ("RAM:/ramtest");	// (a past run's)

	// folders
	check (kapi_mkdir ("RAM:/ramtest") == 0, "mkdir RAM:/ramtest");
	check (kapi_mkdir ("ram:/RamTest") != 0, "the same name in another case: there already");
	check (kapi_mkdir ("RAM:/ramtest/sub") == 0 && kapi_mkdir ("RAM:/ramtest/sub/deeper") == 0, "nested folders");
	check (kapi_mkdir ("RAM:/nowhere/x") != 0, "mkdir without its parent fails");

	// a whole file: save_file, read back whole and in small pieces
	fill (s_a, 300000, 1);
	check (kapi_save_file ("RAM:/ramtest/a.bin", s_a, 300000) == 300000, "save_file 300000 B (answers the bytes written)");
	check (read_back ("RAM:/ramtest/a.bin", s_b, BIG, BIG) == 300000 && same (s_a, s_b, 300000), "read back whole");
	check (read_back ("ram:/RAMTEST/A.BIN", s_b, BIG, 777) == 300000 && same (s_a, s_b, 300000), "read back in 777-byte pieces, the name in another case");
	{
		void *h = kapi_open ("RAM:/ramtest/a.bin");
		int ok = h != 0 && kapi_fsize (h) == 300000 && kapi_fsize64 (h) == 300000 && kapi_seek (h, 123456) == 0
			 && kapi_read (h, s_b, 1000) == 1000 && same (s_a + 123456, s_b, 1000);
		if (h) kapi_close (h);
		check (ok, "fsize, fsize64, seek then read");
	}
	unsigned sz = 0; int isdir = -1;
	check (listed ("RAM:/ramtest", "a.bin", &sz, &isdir) && sz == 300000 && isdir == 0, "readdir: the file and its size");
	check (listed ("RAM:/ramtest", "sub", 0, &isdir) && isdir == 1, "readdir: the folder");
	check (kapi_save_file ("RAM:/ramtest/empty", "", 0) == 0 && read_back ("RAM:/ramtest/empty", s_b, BIG, BIG) == 0, "an empty file");

	// streams: written, appended (cp, redirections), read
	{
		void *o = kapi_file_out ("RAM:/ramtest/s.txt", 0);
		int ok = o != 0;
		for (int i = 0; ok && i < 100; i++) ok = kapi_stream_write (o, s_a + i * 500, 500) == 500;
		if (o) kapi_stream_close (o);
		void *ap = kapi_file_out ("RAM:/ramtest/s.txt", 1);
		ok = ok && ap != 0 && kapi_stream_write (ap, s_a + 50000, 1234) == 1234;
		if (ap) kapi_stream_close (ap);
		check (ok && read_back ("RAM:/ramtest/s.txt", s_b, BIG, BIG) == 51234 && same (s_a, s_b, 51234), "file_out: written, then appended");
		void *in = kapi_file_in ("RAM:/ramtest/s.txt");
		unsigned got = 0; int r;
		while (in && (r = kapi_stream_read (in, s_b + got, 4096)) > 0) got += (unsigned) r;
		if (in) kapi_stream_close (in);
		check (got == 51234 && same (s_a, s_b, got), "file_in: read through");
	}

	// rename, remove; the current folder
	check (kapi_rename ("RAM:/ramtest/s.txt", "RAM:/ramtest/sub/t.txt") == 0 && listed ("RAM:/ramtest/sub", "t.txt", &sz, 0)
	       && sz == 51234 && !listed ("RAM:/ramtest", "s.txt", 0, 0), "rename into a folder");
	check (kapi_rename ("RAM:/ramtest/a.bin", "SD:/ramtest-a.bin") != 0, "rename across volumes fails (copy, then remove)");
	check (kapi_rename ("RAM:/ramtest/sub", "RAM:/ramtest/sub/deeper/x") != 0, "a folder into itself fails");
	check (kapi_remove ("RAM:/ramtest/sub") != 0, "remove a folder not empty fails");
	{
		char cwd[128];
		int ok = kapi_chdir ("RAM:/ramtest/sub") == 1 && kapi_getcwd (cwd, sizeof cwd) > 0 && ax_streq (cwd, "RAM:/ramtest/sub")
			 && read_back ("t.txt", s_b, BIG, BIG) == 51234 && read_back ("/ramtest/a.bin", s_b, BIG, BIG) == 300000;
		kapi_chdir ("SD:/");
		check (ok, "chdir RAM:/ramtest/sub, relative paths");
	}
	{	// a file removed while open: still read to its end
		void *h = kapi_open ("RAM:/ramtest/a.bin");
		int ok = h != 0 && kapi_remove ("RAM:/ramtest/a.bin") == 0 && kapi_open ("RAM:/ramtest/a.bin") == 0
			 && kapi_read (h, s_b, BIG) == 300000 && same (s_a, s_b, 300000);
		if (h) kapi_close (h);
		check (ok, "a file removed while open: read to its end");
	}

	// speed: 16 MB written and read
	{
		fill (s_a, BIG, 7);
		unsigned long long t0 = now_us ();
		int w = kapi_save_file ("RAM:/ramtest/big", s_a, BIG);
		unsigned long long t1 = now_us ();
		int r = read_back ("RAM:/ramtest/big", s_b, BIG, BIG);
		unsigned long long t2 = now_us ();
		check (w == (int) BIG && r == (int) BIG && same (s_a, s_b, BIG), "a 16 MB file written and read back");
		ax_puts ("  16 MB: written in "); put_u ((t1 - t0) / 1000); ax_puts (" ms, read in "); put_u ((t2 - t1) / 1000); ax_putln (" ms");
		struct kapi_vol_info vb;
		check (kapi_vol_info ("RAM:/ramtest", &vb) == 0 && vb.used >= v0.used + BIG && (vb.flags & KAPI_VOL_RAM)
		       && ax_streq (vb.type, "RAM"), "vol_info: the 16 MB counted");
		kapi_remove ("RAM:/ramtest/big");
	}

	if (full)
	{
		static char name[64];
		int n = 0;
		for (;;)
		{
			int k = 0; const char *pre = "RAM:/ramtest/full"; while (pre[k]) { name[k] = pre[k]; k++; }
			k += ax_itoa (n, name + k); name[k] = 0;
			if (kapi_save_file (name, s_a, BIG) != (int) BIG) break;
			n++;
		}
		struct kapi_vol_info vf;
		kapi_vol_info ("RAM:", &vf);
		ax_puts ("  full after "); put_u ((unsigned) n); ax_puts (" files of 16 MB, "); put_u (vf.free >> 10); ax_putln (" KB free");
		check (n > 0 && kapi_open (name) == 0, "full: the save that did not fit left no half file");
		check (kapi_save_file ("RAM:/ramtest/small", "hello", 5) == 5 || vf.free < 65536, "a small file still fits");
		kapi_remove ("RAM:/ramtest/small");
		for (int i = 0; i < n; i++)
		{
			int k = 0; const char *pre = "RAM:/ramtest/full"; while (pre[k]) { name[k] = pre[k]; k++; }
			k += ax_itoa (i, name + k); name[k] = 0;
			kapi_remove (name);
		}
	}

	// everything removed: the memory given back
	kapi_remove ("RAM:/ramtest/empty"); kapi_remove ("RAM:/ramtest/sub/t.txt");
	kapi_remove ("RAM:/ramtest/sub/deeper"); kapi_remove ("RAM:/ramtest/sub");
	check (kapi_remove ("RAM:/ramtest") == 0, "the test's folder removed");
	kapi_vol_info ("RAM:", &v1);
	check (v1.used == v0.used && v1.files == v0.files && v1.dirs == v0.dirs, "the memory given back (vol_info as before)");
	if (s_fails == 0) ax_putln ("ALL PASS");
	else { put_u ((unsigned) s_fails); ax_putln (" FAILED"); }
	return s_fails ? 1 : 0;
}
