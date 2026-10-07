//
// filetest -- the open files of kapi v75 (WP-FILE/PROC, docs/POSIX-PLAN.md §3.2): file_open's flag
// matrix, pread / pwrite at random offsets through two descriptions of one file checked against
// a model, O_APPEND from two handles, truncate (grow with zeros, shrink), sync, stat (size, mode,
// times, ino), unlink of an open file, rename (across folders, onto an open file, of an open
// file), mkdir / rmdir (ENOTEMPTY), dir_read with 200-character names, utime, a big file streamed
// (MB/s), pipes (stream_write_nb -> EAGAIN on a full pipe, a blocking write drained by a thread,
// the end of the stream). Runs on SD: (SD:/tmp/filetest) and on RAM: (RAM:/filetest). One PASS /
// FAIL line per check; the exit code is the number of failures.
//
//   filetest [sd | ram | all] [ops N] [big MB]       (default: all, 4000 ops, 64 MB on SD: / 32 MB on RAM:)
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
#include "appkit/appkit.h"

static int s_pass, s_fail;

// ---- output ------------------------------------------------------------------------------------

static void put_num (long long v)
{
	char b[24]; int n = 0;
	if (v < 0) { kapi_stdout_write ("-", 1); v = -v; }
	do { b[n++] = (char) ('0' + v % 10); v /= 10; } while (v != 0);
	while (n > 0) kapi_stdout_write (&b[--n], 1);
}

static const char *s_vol = "";			// "SD" / "RAM": the lines' prefix

static void check (const char *what, int ok, long long got)
{
	ax_puts (ok ? "PASS " : "FAIL ");
	ax_puts (s_vol);
	ax_puts (": ");
	ax_puts (what);
	ax_puts (" (");
	put_num (got);
	ax_putln (")");
	if (ok) s_pass++; else s_fail++;
}

// ---- small helpers -------------------------------------------------------------------------------

static void cat (char *d, const char *a, const char *b)
{
	int p = 0;
	d[0] = '\0';
	ax_strcat (d, 512, &p, a);
	ax_strcat (d, 512, &p, b);
}

static int memeq (const void *a, const void *b, unsigned long long n)
{
	const unsigned char *p = a, *q = b;
	for (unsigned long long i = 0; i < n; i++) if (p[i] != q[i]) return 0;
	return 1;
}

static unsigned s_seed = 12345;
static unsigned rnd (void) { s_seed = s_seed * 1103515245u + 12345u; return (s_seed >> 8) & 0xFFFFFF; }

static unsigned long long now_us (void)
{
	unsigned long long c, f;
	__asm__ volatile ("isb\n\tmrs %0, cntpct_el0\n\tmrs %1, cntfrq_el0" : "=r" (c), "=r" (f));
	return c / (f / 1000000);				// (54 MHz: an exact divisor)
}

static long long put_file (const char *path, const char *data, unsigned n)
{
	long long h = kapi_file_open (path, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC, 0644);
	if (h < 0) return h;
	long long w = kapi_file_write (h, data, n, -1);
	kapi_file_close (h);
	return w;
}

static long long file_size (const char *path)
{
	struct kapi_stat st;
	int r = kapi_path_stat (path, &st);
	return r < 0 ? r : (long long) st.size;
}

// ---- the flag matrix ---------------------------------------------------------------------------

static void flags (const char *B)
{
	char f[512], d[512];
	char buf[64];
	cat (f, B, "/flags");
	kapi_path_unlink (f, 0);
	check ("open a missing file: ENOENT", kapi_file_open (f, KAPI_O_RDONLY, 0) == -KAPI_ENOENT, kapi_file_open (f, KAPI_O_RDONLY, 0));
	long long h = kapi_file_open (f, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_EXCL, 0644);
	check ("O_CREAT|O_EXCL creates", h > 0, h);
	check ("write 5 bytes", kapi_file_write (h, "hello", 5, -1) == 5, 0);
	check ("O_WRONLY: a read is EBADF", kapi_file_read (h, buf, 5, 0) == -KAPI_EBADF, 0);
	kapi_file_close (h);
	check ("O_CREAT|O_EXCL on an existing file: EEXIST",
	       kapi_file_open (f, KAPI_O_RDWR | KAPI_O_CREAT | KAPI_O_EXCL, 0) == -KAPI_EEXIST, 0);
	check ("size 5", file_size (f) == 5, file_size (f));
	h = kapi_file_open (f, KAPI_O_RDONLY, 0);
	check ("O_RDONLY: a write is EBADF", kapi_file_write (h, "x", 1, -1) == -KAPI_EBADF, 0);
	long long r = kapi_file_read (h, buf, sizeof buf, -1);
	check ("read the 5 bytes", r == 5 && memeq (buf, "hello", 5), r);
	check ("read at the end: 0", kapi_file_read (h, buf, sizeof buf, -1) == 0, 0);
	check ("O_RDONLY: truncate is EINVAL", kapi_file_truncate (h, 0) == -KAPI_EINVAL, 0);
	kapi_file_close (h);
	check ("a closed handle: EBADF", kapi_file_read (h, buf, 1, 0) == -KAPI_EBADF, 0);
	h = kapi_file_open (f, KAPI_O_RDWR | KAPI_O_APPEND, 0);
	kapi_file_write (h, "ab", 2, -1);
	kapi_file_seek (h, 0, KAPI_SEEK_SET);
	kapi_file_write (h, "cd", 2, -1);			// (O_APPEND: at the end anyway)
	r = kapi_file_read (h, buf, sizeof buf, 0);
	check ("O_APPEND writes at the end", r == 9 && memeq (buf, "helloabcd", 9), r);
	kapi_file_write (h, "XY", 2, 0);			// (an explicit offset: pwrite)
	r = kapi_file_read (h, buf, sizeof buf, 0);
	check ("pwrite at 0 with O_APPEND", r == 9 && memeq (buf, "XYlloabcd", 9), r);
	check ("seek END", kapi_file_seek (h, -2, KAPI_SEEK_END) == 7, kapi_file_seek (h, 0, KAPI_SEEK_CUR));
	check ("seek before 0: EINVAL", kapi_file_seek (h, -100, KAPI_SEEK_SET) == -KAPI_EINVAL, 0);
	check ("seek whence 9: EINVAL", kapi_file_seek (h, 0, 9) == -KAPI_EINVAL, 0);
	kapi_file_close (h);
	h = kapi_file_open (f, KAPI_O_WRONLY | KAPI_O_TRUNC, 0);
	check ("O_TRUNC empties it", h > 0 && file_size (f) == 0, file_size (f));
	kapi_file_close (h);
	h = kapi_file_open (f, KAPI_O_RDONLY | KAPI_O_TRUNC, 0);
	kapi_file_close (h);
	check ("O_RDONLY|O_TRUNC (read-only: no truncation)", h > 0, h);
	check ("open a folder: EISDIR", kapi_file_open (B, KAPI_O_RDONLY, 0) == -KAPI_EISDIR, kapi_file_open (B, KAPI_O_RDONLY, 0));
	cat (d, B, "/nope/x");
	check ("O_CREAT in a missing folder: ENOENT", kapi_file_open (d, KAPI_O_WRONLY | KAPI_O_CREAT, 0) == -KAPI_ENOENT,
	       kapi_file_open (d, KAPI_O_WRONLY | KAPI_O_CREAT, 0));
	check ("access mode 3: EINVAL", kapi_file_open (f, 3, 0) == -KAPI_EINVAL, 0);
	check ("a bad path pointer: EFAULT", kapi_file_open ((const char *) 0x10, 0, 0) == -KAPI_EFAULT, 0);
	h = kapi_file_open (f, KAPI_O_RDONLY | KAPI_O_CREAT, 0);
	check ("O_RDONLY|O_CREAT on an existing file", h > 0, h);
	kapi_file_close (h);
	kapi_path_unlink (f, 0);
	h = kapi_file_open (f, KAPI_O_RDONLY | KAPI_O_CREAT, 0);
	check ("O_RDONLY|O_CREAT creates", h > 0 && file_size (f) == 0, h);
	kapi_file_close (h);
	kapi_path_unlink (f, 0);
}

// ---- random pread / pwrite against a model -----------------------------------------------------

#define MODEL_MAX	(384 * 1024)
static unsigned char s_model[MODEL_MAX];
static unsigned char s_tmp[64 * 1024];
static unsigned char s_tmp2[64 * 1024];

static void model (const char *B, unsigned ops)
{
	char f[512];
	cat (f, B, "/model");
	kapi_path_unlink (f, 0);
	long long a = kapi_file_open (f, KAPI_O_RDWR | KAPI_O_CREAT, 0644);
	long long b = kapi_file_open (f, KAPI_O_RDWR, 0);
	long long ap = kapi_file_open (f, KAPI_O_WRONLY | KAPI_O_APPEND, 0);
	check ("three descriptions of one file", a > 0 && b > 0 && ap > 0 && a != b, b);
	unsigned long long size = 0;
	int bad = 0;
	long long badv = 0;
	unsigned long long t0 = now_us ();
	for (unsigned i = 0; i < ops && !bad; i++)
	{
		long long h = (rnd () & 1) ? a : b;
		unsigned op = rnd () % 16;
		unsigned long long off = rnd () % (size + 20000);
		unsigned len = rnd () % 8192 + 1;
		if (off + len > MODEL_MAX) off = MODEL_MAX - len;
		if (op < 6)
		{
			for (unsigned k = 0; k < len; k++) s_tmp[k] = (unsigned char) rnd ();
			long long w = kapi_file_write (h, s_tmp, len, (long long) off);
			if (w != len) { bad = 1; badv = w; break; }
			if (off > size) for (unsigned long long k = size; k < off; k++) s_model[k] = 0;
			for (unsigned k = 0; k < len; k++) s_model[off + k] = s_tmp[k];
			if (off + len > size) size = off + len;
		}
		else if (op < 7 && size + 600 < MODEL_MAX)
		{
			unsigned n = rnd () % 500 + 1;
			for (unsigned k = 0; k < n; k++) s_tmp[k] = (unsigned char) rnd ();
			long long w = kapi_file_write (ap, s_tmp, n, -1);
			if (w != n) { bad = 2; badv = w; break; }
			for (unsigned k = 0; k < n; k++) s_model[size + k] = s_tmp[k];
			size += n;
		}
		else if (op < 8)
		{
			unsigned long long n = rnd () % (size + 30000);
			if (n > MODEL_MAX) n = MODEL_MAX;
			int r = kapi_file_truncate (h, (long long) n);
			if (r != 0) { bad = 3; badv = r; break; }
			for (unsigned long long k = size; k < n; k++) s_model[k] = 0;
			size = n;
		}
		else
		{
			long long r = kapi_file_read (h, s_tmp2, len, (long long) off);
			unsigned long long want = off >= size ? 0 : (size - off < len ? size - off : len);
			if (r != (long long) want || !memeq (s_tmp2, s_model + off, want)) { bad = 4; badv = r; break; }
		}
	}
	unsigned long long dt = now_us () - t0;
	check ("random pread / pwrite / append / truncate vs the model (0 = all ok)", bad == 0, bad ? bad * 1000000LL + badv : 0);
	struct kapi_stat st;
	kapi_file_stat (a, &st);
	check ("the size at the end", st.size == size, (long long) st.size);
	ax_puts ("     ");
	put_num (ops);
	ax_puts (" operations in ");
	put_num ((long long) (dt / 1000));
	ax_putln (" ms");
	// the whole file read through the 3rd description's node: = the model
	long long r = 0;
	unsigned long long pos = 0;
	int same = 1;
	while ((r = kapi_file_read (b, s_tmp2, sizeof s_tmp2, (long long) pos)) > 0)
	{
		if (!memeq (s_tmp2, s_model + pos, (unsigned long long) r)) same = 0;
		pos += (unsigned long long) r;
	}
	check ("the whole file = the model", same && pos == size, (long long) pos);
	kapi_file_close (a); kapi_file_close (b); kapi_file_close (ap);
	check ("reopened: the same size", file_size (f) == (long long) size, file_size (f));
	kapi_path_unlink (f, 0);
}

// ---- append from two handles, truncate, sync, stat -------------------------------------------------

static void append_trunc_stat (const char *B)
{
	char f[512];
	cat (f, B, "/app");
	kapi_path_unlink (f, 0);
	long long h1 = kapi_file_open (f, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_APPEND, 0644);
	long long h2 = kapi_file_open (f, KAPI_O_WRONLY | KAPI_O_APPEND, 0);
	for (int i = 0; i < 20; i++)
	{
		kapi_file_write (h1, "AAAAAAAAAA", 10, -1);
		kapi_file_write (h2, "BBBBBBBBBB", 10, -1);
	}
	kapi_file_close (h1); kapi_file_close (h2);
	long long r = kapi_file_open (f, KAPI_O_RDONLY, 0);
	long long n = kapi_file_read (r, s_tmp, sizeof s_tmp, 0);
	int ok = n == 400;
	for (int i = 0; ok && i < 40; i++)
	{
		unsigned char c = (i & 1) ? 'B' : 'A';
		for (int k = 0; k < 10; k++) if (s_tmp[i * 10 + k] != c) ok = 0;
	}
	check ("O_APPEND from two handles: 400 bytes, interleaved whole", ok, n);
	kapi_file_close (r);

	long long h = kapi_file_open (f, KAPI_O_RDWR, 0);
	check ("truncate grows (+100000)", kapi_file_truncate (h, 100400) == 0 && file_size (f) == 100400, file_size (f));
	int zeros = 1;
	for (long long off = 400; off < 100400; off += sizeof s_tmp)
	{
		long long k = kapi_file_read (h, s_tmp, sizeof s_tmp, off);
		for (long long j = 0; j < k; j++) if (s_tmp[j] != 0) zeros = 0;
	}
	check ("the growth reads as zeros", zeros, 0);
	check ("truncate shrinks (10)", kapi_file_truncate (h, 10) == 0 && file_size (f) == 10, file_size (f));
	n = kapi_file_read (h, s_tmp, sizeof s_tmp, 0);
	check ("10 bytes left, the first ones", n == 10 && memeq (s_tmp, "AAAAAAAAAA", 10), n);
	check ("a write far past the end", kapi_file_write (h, "Z", 1, 70000) == 1 && file_size (f) == 70001, file_size (f));
	n = kapi_file_read (h, s_tmp, 100, 69950);
	check ("the gap reads as zeros", n == 51 && s_tmp[0] == 0 && s_tmp[49] == 0 && s_tmp[50] == 'Z', n);
	check ("sync", kapi_file_sync (h) == 0, kapi_file_sync (h));

	struct kapi_stat st, ps;
	struct kapi_clock_info ci;
	kapi_clock_info (&ci);
	kapi_file_stat (h, &st);
	kapi_path_stat (f, &ps);
	check ("fstat: a regular file, its size", (st.mode & KAPI_S_IFMT) == KAPI_S_IFREG && st.size == 70001, (long long) st.size);
	check ("stat = fstat (size, ino, dev)", ps.size == st.size && ps.ino == st.ino && ps.dev == st.dev && ps.ino != 0, (long long) ps.dev);
	long long nowS = ci.utc_us / 1000000;
	if (ci.flags & KAPI_CLOCK_REALTIME_VALID)
	{
		long long d = st.mtime - nowS;
		check ("mtime within 3 s of clock_info", d >= -3 && d <= 3, d);
	}
	else
	{
		ax_putln ("     (no real date yet: the mtime check skipped)");
	}
	check ("blksize, blocks", st.blksize >= 512 && st.blocks * 512 >= st.size, (long long) st.blksize);
	kapi_file_close (h);
	check ("utime", kapi_path_utime (f, 1000000000LL) == 0 && kapi_path_stat (f, &ps) == 0 && ps.mtime == 1000000000LL, ps.mtime);
	struct kapi_stat ds;
	check ("stat a folder", kapi_path_stat (B, &ds) == 0 && (ds.mode & KAPI_S_IFMT) == KAPI_S_IFDIR, (long long) ds.mode);
	char root[8]; int k = 0;
	while (B[k] != ':' && k < 6) { root[k] = B[k]; k++; }
	root[k++] = ':'; root[k++] = '/'; root[k] = '\0';
	check ("stat the volume's root", kapi_path_stat (root, &ds) == 0 && (ds.mode & KAPI_S_IFMT) == KAPI_S_IFDIR, (long long) ds.mode);
	check ("stat a missing file: ENOENT", kapi_path_stat ("nope-nope", &ds) == -KAPI_ENOENT, 0);
	kapi_path_unlink (f, 0);
}

// ---- unlink, rename, mkdir / rmdir, dir_read ---------------------------------------------------------

static int listed (const char *dir, const char *name, struct kapi_dirent2 *out)
{
	static struct kapi_dirent2 e;
	void *d = kapi_opendir (dir);
	int found = 0;
	if (d == 0) return -1;
	while (kapi_dir_read (d, &e) == 1)
	{
		if (ax_streq (e.name, name)) { found = 1; if (out) *out = e; }
	}
	kapi_closedir (d);
	return found;
}

static void names (const char *B)
{
	char f[512], g[512], d1[512], d2[512], x[512];
	cat (d1, B, "/d1"); cat (d2, B, "/d2");
	kapi_path_mkdir (d1, 0755);
	check ("mkdir an existing folder: EEXIST", kapi_path_mkdir (d1, 0755) == -KAPI_EEXIST, 0);
	cat (x, B, "/no/pe");
	check ("mkdir without its parent: ENOENT", kapi_path_mkdir (x, 0755) == -KAPI_ENOENT, kapi_path_mkdir (x, 0755));
	kapi_path_mkdir (d2, 0755);

	// unlink an open file: still readable, gone from the listing, nothing left after the close
	cat (f, d1, "/open");
	put_file (f, "still here", 10);
	long long h = kapi_file_open (f, KAPI_O_RDWR, 0);
	check ("unlink an open file", kapi_path_unlink (f, 0) == 0, 0);
	struct kapi_stat st;
	check ("gone: stat ENOENT", kapi_path_stat (f, &st) == -KAPI_ENOENT, 0);
	check ("gone from the listing", listed (d1, "open", 0) == 0, 0);
	long long n = kapi_file_read (h, s_tmp, 64, 0);
	check ("still readable through its handle", n == 10 && memeq (s_tmp, "still here", 10), n);
	check ("and writable", kapi_file_write (h, "!", 1, 10) == 1, 0);
	check ("a new file of that name", put_file (f, "new", 3) == 3 && file_size (f) == 3, file_size (f));
	n = kapi_file_read (h, s_tmp, 64, 0);
	check ("the old one unchanged", n == 11, n);
	kapi_file_close (h);
	if (B[0] == 'S')					// (the hidden folder: empty, gone)
	{
		check ("nothing left after the close", kapi_path_stat ("SD:/.~onyx-deleted", &st) == -KAPI_ENOENT, 0);
	}
	kapi_path_unlink (f, 0);

	// rename across folders, onto an open file, of an open file
	cat (f, d1, "/a"); cat (g, d2, "/b");
	put_file (f, "AAA", 3);
	kapi_path_unlink (g, 0);
	check ("rename across folders", kapi_path_rename (f, g) == 0 && file_size (g) == 3 && file_size (f) == -KAPI_ENOENT, file_size (g));
	put_file (f, "NEW!", 4);
	long long hg = kapi_file_open (g, KAPI_O_RDONLY, 0);
	check ("rename onto an open file (replaced)", kapi_path_rename (f, g) == 0 && file_size (g) == 4, file_size (g));
	n = kapi_file_read (hg, s_tmp, 64, 0);
	check ("the replaced one still readable", n == 3 && memeq (s_tmp, "AAA", 3), n);
	kapi_file_close (hg);
	long long hw = kapi_file_open (g, KAPI_O_RDWR, 0);
	check ("rename an open file", kapi_path_rename (g, f) == 0, 0);
	check ("written after its rename", kapi_file_write (hw, "++", 2, -1) == 2, 0);
	kapi_file_close (hw);
	h = kapi_file_open (f, KAPI_O_RDONLY, 0);
	n = kapi_file_read (h, s_tmp, 64, 0);
	check ("found at its new path, with the write", n == 4 && memeq (s_tmp, "++W!", 4), n);
	kapi_file_close (h);
	check ("a file onto a folder: EISDIR", kapi_path_rename (f, d2) == -KAPI_EISDIR, kapi_path_rename (f, d2));
	cat (x, d1, "/inside");
	check ("a folder into itself: EINVAL", kapi_path_rename (d1, x) == -KAPI_EINVAL, kapi_path_rename (d1, x));
	check ("across volumes: EXDEV", kapi_path_rename (f, B[0] == 'S' ? "RAM:/x" : "SD:/x") == -KAPI_EXDEV, 0);
	// a folder renamed while a file in it is open
	cat (x, B, "/d3");
	cat (g, d2, "/c");
	put_file (g, "in d2", 5);
	h = kapi_file_open (g, KAPI_O_RDWR, 0);
	check ("rename a folder with an open file", kapi_path_rename (d2, x) == 0, 0);
	check ("the file written after", kapi_file_seek (h, 0, KAPI_SEEK_END) == 5 && kapi_file_write (h, "!", 1, -1) == 1, 0);
	kapi_file_close (h);
	cat (g, x, "/c");
	check ("found in the renamed folder", file_size (g) == 6, file_size (g));
	check ("rename it back", kapi_path_rename (x, d2) == 0, 0);
	cat (g, d2, "/c");
	kapi_path_unlink (g, 0);

	// rmdir
	check ("rmdir a folder not empty: ENOTEMPTY", kapi_path_unlink (d1, KAPI_UNLINK_DIR) == -KAPI_ENOTEMPTY,
	       kapi_path_unlink (d1, KAPI_UNLINK_DIR));
	check ("unlink a folder: EISDIR", kapi_path_unlink (d1, 0) == -KAPI_EISDIR, 0);
	check ("rmdir a file: ENOTDIR", kapi_path_unlink (f, KAPI_UNLINK_DIR) == -KAPI_ENOTDIR, 0);
	check ("unlink a missing file: ENOENT", kapi_path_unlink (x, 0) == -KAPI_ENOENT, 0);

	// dir_read: a long name (200 characters on SD:, RAM: keeps 127), its ino = stat's
	char name[256];
	int len = B[0] == 'S' ? 200 : 127;
	for (int i = 0; i < len; i++) name[i] = (char) ('a' + i % 26);
	name[len] = '\0';
	cat (x, d1, "/");
	cat (g, x, name);
	put_file (g, "12345", 5);
	struct kapi_dirent2 e;
	int found = listed (d1, name, &e);
	struct kapi_stat gs;
	kapi_path_stat (g, &gs);
	check ("dir_read: a long name", found == 1 && ax_strlen (e.name) == len && e.size == 5, len);
	check ("dir_read: its mode, mtime and ino = stat's", (e.mode & KAPI_S_IFMT) == KAPI_S_IFREG && e.ino == gs.ino && e.mtime == gs.mtime,
	       (long long) (e.ino == gs.ino));
	check ("dir_read: a bad handle: EBADF", kapi_dir_read ((void *) 0x7777, &e) == -KAPI_EBADF, 0);
	kapi_path_unlink (g, 0);
	kapi_path_unlink (f, 0);
	check ("rmdir", kapi_path_unlink (d1, KAPI_UNLINK_DIR) == 0 && kapi_path_unlink (d2, KAPI_UNLINK_DIR) == 0, 0);
}

// ---- a big file, streamed ---------------------------------------------------------------------------

#define BIG_CHUNK	(1024 * 1024)
static unsigned char s_big[BIG_CHUNK];

static void fill_big (unsigned i)
{
	unsigned *p = (unsigned *) s_big;
	for (unsigned k = 0; k < BIG_CHUNK / 4; k++) p[k] = i * 0x9E3779B9u + k;
}

static void big (const char *B, unsigned mb)
{
	char f[512];
	cat (f, B, "/big");
	kapi_path_unlink (f, 0);
	long long h = kapi_file_open (f, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC, 0644);
	unsigned long long t0 = now_us ();
	int ok = h > 0;
	for (unsigned i = 0; ok && i < mb; i++)
	{
		fill_big (i);
		if (kapi_file_write (h, s_big, BIG_CHUNK, -1) != BIG_CHUNK) ok = 0;
	}
	kapi_file_sync (h);
	kapi_file_close (h);
	unsigned long long tw = now_us () - t0;
	check ("big file written (MB)", ok && file_size (f) == (long long) mb * BIG_CHUNK, mb);
	h = kapi_file_open (f, KAPI_O_RDONLY, 0);
	t0 = now_us ();
	int same = h > 0;
	unsigned long long tf = 0;
	for (unsigned i = 0; same && i < mb; i++)
	{
		if (kapi_file_read (h, s_big, BIG_CHUNK, -1) != BIG_CHUNK) { same = 0; break; }
		unsigned long long t1 = now_us ();
		unsigned *p = (unsigned *) s_big;
		for (unsigned k = 0; k < BIG_CHUNK / 4; k++) if (p[k] != i * 0x9E3779B9u + k) { same = 0; break; }
		tf += now_us () - t1;
	}
	unsigned long long tr = now_us () - t0 - tf;
	kapi_file_close (h);
	check ("big file read back, the same", same, mb);
	ax_puts ("     write ");
	put_num ((long long) (mb * 1000000ull / (tw ? tw : 1)));
	ax_puts (" MB/s, read ");
	put_num ((long long) (mb * 1000000ull / (tr ? tr : 1)));
	ax_putln (" MB/s");
	kapi_path_unlink (f, 0);
}

// ---- pipes -------------------------------------------------------------------------------------

static void *s_pipe;
static volatile int s_drained;

static int drainer (void *arg)
{
	(void) arg;
	kapi_msleep (50);
	static unsigned char b[4096];
	int n, total = 0;
	while ((n = kapi_stream_read (s_pipe, b, sizeof b)) > 0) total += n;
	s_drained = total;
	return 0;
}

static void pipes (void)
{
	s_vol = "pipe";
	void *p = kapi_pipe ();
	int total = 0, r;
	while ((r = kapi_stream_write_nb (p, s_tmp, 1000)) > 0) total += r;
	check ("write_nb fills the pipe, then EAGAIN", r == -KAPI_EAGAIN && total > 4000, total);
	check ("write_nb: a bad handle: EBADF", kapi_stream_write_nb ((void *) 0x7777, s_tmp, 1) == -KAPI_EBADF, 0);
	int got = 0;
	while ((r = kapi_stream_read_nb (p, s_tmp2, sizeof s_tmp2)) > 0) got += r;
	check ("drained without blocking", got == total && r == -1, got);
	kapi_stream_close (p);

	s_pipe = kapi_pipe ();
	s_drained = -1;
	int tid = kapi_thread_create (drainer, 0, 0, "drainer");
	unsigned long long t0 = now_us ();
	for (int i = 0; i < 20; i++) kapi_stream_write (s_pipe, s_tmp, 1000);	// (blocks: the pipe holds 8 KB)
	unsigned long long dt = now_us () - t0;
	check ("a blocking write waits for the reader", dt >= 40000, (long long) (dt / 1000));
	kapi_stream_eof (s_pipe);
	int code = 0;
	kapi_thread_join (tid, 5000, &code);
	check ("the reader got everything, then the end", s_drained == 20000, s_drained);
	char c;
	check ("read after the end: 0", kapi_stream_read (s_pipe, &c, 1) == 0, 0);
	kapi_stream_close (s_pipe);
}

// ---- main ------------------------------------------------------------------------------------------

static int argnum (const char *a, const char *key, unsigned *out)
{
	int k = ax_strlen (key);
	const char *p = a;
	for (; *p != '\0'; p++)
	{
		int i = 0;
		while (i < k && p[i] == key[i]) i++;
		if (i == k && p[k] == ' ')
		{
			unsigned v = 0;
			for (p += k + 1; *p >= '0' && *p <= '9'; p++) v = v * 10 + (unsigned) (*p - '0');
			*out = v;
			return 1;
		}
	}
	return 0;
}

static void run (const char *vol, const char *B, unsigned ops, unsigned mb)
{
	s_vol = vol;
	kapi_path_mkdir (B, 0755);
	struct kapi_stat st;
	if (kapi_path_stat (B, &st) != 0) { check ("the test folder", 0, kapi_path_stat (B, &st)); return; }
	flags (B);
	model (B, ops);
	append_trunc_stat (B);
	names (B);
	big (B, mb);
	check ("the test folder removed", kapi_path_unlink (B, KAPI_UNLINK_DIR) == 0, kapi_path_unlink (B, KAPI_UNLINK_DIR));
}

int main (void)
{
	char a[256];
	kapi_get_args (a, sizeof a);
	if (kapi_file_open ("SD:/", 0, 0) == -KAPI_ENOSYS)
	{
		ax_putln ("filetest: this kernel has no v75 files (file_open: ENOSYS)");
		return 1;
	}
	unsigned ops = 4000, mb = 0;
	argnum (a, "ops", &ops);
	argnum (a, "big", &mb);
	int sd = 1, ram = 1;
	if (a[0] == 's' && a[1] == 'd') ram = 0;
	if (a[0] == 'r' && a[1] == 'a' && a[2] == 'm') sd = 0;
	if (sd)
	{
		kapi_path_mkdir ("SD:/tmp", 0755);
		run ("SD", "SD:/tmp/filetest", ops, mb ? mb : 64);
	}
	if (ram)
	{
		run ("RAM", "RAM:/filetest", ops, mb ? mb : 32);
	}
	pipes ();
	ax_puts ("filetest: ");
	put_num (s_pass);
	ax_puts (" passed, ");
	put_num (s_fail);
	ax_putln (" failed");
	return s_fail;
}
