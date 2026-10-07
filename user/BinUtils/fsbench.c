//
// fsbench -- measure the file system on the SD card:
//   fsbench [big-file]      (default SD:/doom/freedoom1.wad)
//   1. sequential read of the big file, 1 MB at a time (MB/s);
//   2. opening + reading every app's app.txt (what the menu bar does): twice, the 2nd
//      time with the sector cache warm;
//   3. listing SD:/apps, twice;
//   4. writing a 4 MB file (SD:/fsbench.tmp, removed after), then reading it back.
// Compare with sdhs=1 / sdcache=0 in cmdline.txt (kmsg shows what is in use).
//
#include "appkit/appkit.h"

static unsigned char s_buf[1 << 20];

static unsigned long long now_us (void)
{
	unsigned long long c, f;
	__asm__ volatile ("mrs %0, cntpct_el0" : "=r" (c));
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (f));
	return f ? c * 1000000ull / f : 0;
}

static void put_num (unsigned long long v, int decimals)	// v in 1/10^decimals
{
	char t[32]; int k = 0;
	unsigned long long div = 1;
	for (int i = 0; i < decimals; i++) div *= 10;
	unsigned long long ip = v / div, fp = v % div;
	do { t[k++] = (char) ('0' + ip % 10); ip /= 10; } while (ip);
	char out[40]; int n = 0;
	while (k) out[n++] = t[--k];
	if (decimals)
	{
		out[n++] = '.';
		for (unsigned long long m = div / 10; m; m /= 10) out[n++] = (char) ('0' + (fp / m) % 10);
	}
	out[n] = 0;
	ax_puts (out);
}

static void rate (const char *what, unsigned long long bytes, unsigned long long us)
{
	ax_puts (what);
	put_num (us ? bytes * 100ull / us : 0, 2);			// bytes/us = MB/s
	ax_puts (" MB/s  (");
	put_num (bytes / 1024, 0); ax_puts (" KB in ");
	put_num (us / 100, 1); ax_putln (" ms)");
}

static void small_files (int pass)
{
	static char list[4096];
	int napps = kapi_list_apps (list, sizeof list);
	unsigned long long t0 = now_us ();
	int files = 0; unsigned bytes = 0;
	for (int i = 0; list[i]; )
	{
		char name[64]; int n = 0;
		while (list[i] && list[i] != '\n') { if (n < 63) name[n++] = list[i]; i++; }
		if (list[i] == '\n') i++;
		name[n] = 0;
		if (!n) continue;
		char path[128]; int p = 0;
		ax_strcat (path, sizeof path, &p, "SD:/apps/"); ax_strcat (path, sizeof path, &p, name);
		ax_strcat (path, sizeof path, &p, ".app/app.txt");
		void *f = kapi_open (path);
		if (!f) continue;
		unsigned sz = kapi_fsize (f);
		if (sz > sizeof s_buf) sz = sizeof s_buf;
		int r = kapi_read (f, s_buf, sz);
		kapi_close (f);
		if (r > 0) bytes += (unsigned) r;
		files++;
	}
	unsigned long long us = now_us () - t0;
	ax_puts (pass ? "  again (cache warm): " : "  app.txt of ");
	if (!pass) { put_num ((unsigned) napps, 0); ax_puts (" apps: "); }
	put_num (us / 100, 1); ax_puts (" ms, ");
	put_num (files ? us / (unsigned) files / 10 : 0, 2); ax_putln (" ms a file");
}

static void list_dir (int pass)
{
	unsigned long long t0 = now_us ();
	void *d = kapi_opendir ("SD:/apps");
	int n = 0;
	if (d)
	{
		struct kapi_dirent e;
		while (kapi_readdir (d, &e)) n++;
		kapi_closedir (d);
	}
	unsigned long long us = now_us () - t0;
	ax_puts (pass ? "  again: " : "  SD:/apps, ");
	if (!pass) { put_num ((unsigned) n, 0); ax_puts (" entries: "); }
	put_num (us / 100, 1); ax_putln (" ms");
}

int main (void)
{
	char path[256];
	kapi_get_args (path, sizeof path);
	if (!path[0]) { int p = 0; ax_strcat (path, sizeof path, &p, "SD:/doom/freedoom1.wad"); }

	ax_putln ("1. sequential read");
	void *f = kapi_open (path);
	if (!f) { ax_puts ("  cannot open "); ax_putln (path); }
	else
	{
		unsigned sz = kapi_fsize (f), done = 0;
		unsigned long long t0 = now_us ();
		while (done < sz)
		{
			unsigned k = sz - done > sizeof s_buf ? (unsigned) sizeof s_buf : sz - done;
			int r = kapi_read (f, s_buf, k);
			if (r <= 0) break;
			done += (unsigned) r;
		}
		kapi_close (f);
		rate ("  ", done, now_us () - t0);
	}

	ax_putln ("2. small files");
	small_files (0);
	small_files (1);

	ax_putln ("3. directory listing");
	list_dir (0);
	list_dir (1);

	ax_putln ("4. write + read back 4 MB (SD:/fsbench.tmp)");
	static unsigned char big[4 << 20];
	for (unsigned i = 0; i < sizeof big; i++) big[i] = (unsigned char) (i * 7 + (i >> 12));
	unsigned long long t0 = now_us ();
	int w = kapi_save_file ("SD:/fsbench.tmp", big, sizeof big);
	unsigned long long us = now_us () - t0;
	if (w < 0) ax_putln ("  write failed");
	else
	{
		rate ("  write ", sizeof big, us);
		f = kapi_open ("SD:/fsbench.tmp");
		unsigned ok = 1, done = 0;
		t0 = now_us ();
		while (f && done < sizeof big)
		{
			int r = kapi_read (f, s_buf, sizeof s_buf);
			if (r <= 0) break;
			for (int i = 0; i < r; i++) if (s_buf[i] != big[done + (unsigned) i]) ok = 0;
			done += (unsigned) r;
		}
		us = now_us () - t0;
		if (f) kapi_close (f);
		rate ("  read  ", done, us);
		ax_putln (ok && done == sizeof big ? "  content: identical (OK)" : "  content: DIFFERENT!");
		kapi_remove ("SD:/fsbench.tmp");
	}
	return 0;
}
