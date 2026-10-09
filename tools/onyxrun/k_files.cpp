//
// k_files.cpp -- the files, the folders and the streams (onyxrun.h), on host folders: SD: is the runner's root (the
// repository's sdcard/ by default), RAM: a folder of its own. FAT's rules kept where a program sees them: names
// without case (an existing name found whatever its case), "SD:x" = "SD:/x", '\' = '/'.
//
//   - the v1..v59 calls (kernel/sys/kapi.cpp): open / read / fsize / seek / close (read-only files), save_file,
//     opendir / readdir / closedir, mkdir / remove / rename, chdir / getcwd, the streams (pipe, file_in, file_out,
//     stream_*, the console's);
//   - the v75 calls (kernel/sys/ofile.cpp, vfs.cpp): file_open / read / write / seek / truncate / sync / stat /
//     close, path_stat / unlink / mkdir / rename / utime, dir_read, stream_write_nb, handle_close.
// Every object is a handle of one table (a number >= 0x1000, given as a pointer to the old calls).
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include <filesystem>
#include <algorithm>
#include <condition_variable>
#include <map>
#include <memory>
#include <deque>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <sys/utime.h>
#else
#include <unistd.h>
#include <utime.h>
#endif

namespace fs = std::filesystem;

bool guest_str (const char *p, std::string &out, size_t max);
int str_out (char *buf, unsigned cap, const std::string &s);
template <class T> static void put (T *p, T v) { if (p && hm_ok (p, sizeof (T), MEM_W)) *p = v; }

static fs::path U8 (const std::string &s)
{
#if __cplusplus >= 202002L
	return fs::path (std::u8string (s.begin (), s.end ()));
#else
	return fs::u8path (s);
#endif
}

static std::string S8 (const fs::path &p)
{
#if __cplusplus >= 202002L
	auto u = p.u8string ();
	return std::string (u.begin (), u.end ());
#else
	return p.u8string ();
#endif
}

static bool ieq (const std::string &a, const std::string &b)
{
	if (a.size () != b.size ()) return false;
	for (size_t i = 0; i < a.size (); i++)
		if (tolower ((unsigned char) a[i]) != tolower ((unsigned char) b[i])) return false;
	return true;
}

// ---- the paths -------------------------------------------------------------------------------------------------
// An Onyx path made absolute and clean: "VOL:/a/b" (the volume upper case), "" if bad.
std::string onyx_abs (const char *p)
{
	std::string s = p ? p : "";
	for (auto &c : s) if (c == '\\') c = '/';
	std::string vol, rest;
	size_t colon = s.find (':');
	if (colon != std::string::npos && s.find ('/') > colon)
	{
		vol = s.substr (0, colon);
		rest = s.substr (colon + 1);
	}
	else
	{
		std::string cwd;
		{ std::lock_guard<std::mutex> L (g_Proc.lock); cwd = g_Proc.cwd; }
		size_t c = cwd.find (':');
		vol = cwd.substr (0, c);
		rest = s.size () && s[0] == '/' ? s : cwd.substr (c + 1) + "/" + s;
	}
	for (auto &c : vol) c = (char) toupper ((unsigned char) c);
	if (vol.empty ()) return "";
	std::vector<std::string> parts;
	size_t i = 0;
	while (i <= rest.size ())
	{
		size_t j = rest.find ('/', i);
		if (j == std::string::npos) j = rest.size ();
		std::string w = rest.substr (i, j - i);
		if (w == "..") { if (!parts.empty ()) parts.pop_back (); }
		else if (!w.empty () && w != ".") parts.push_back (w);
		i = j + 1;
	}
	std::string out = vol + ":/";
	for (size_t k = 0; k < parts.size (); k++) out += (k ? "/" : "") + parts[k];
	return out.size () > 1024 ? "" : out;
}

static std::string vol_root (const std::string &vol)
{
	if (vol == "SD") return g_Proc.root;
	if (vol == "RAM") return g_Proc.ramRoot;
	return "";
}

// The host path of an Onyx path ("" when its volume is not the runner's). Each part's case: the existing entry's.
std::string host_path (const char *onyx)
{
	std::string a = onyx_abs (onyx);
	if (a.empty ()) return "";
	size_t c = a.find (':');
	std::string root = vol_root (a.substr (0, c));
	if (root.empty ()) return "";
	fs::path cur = U8 (root);
	std::string rest = a.substr (c + 2);
	size_t i = 0;
	while (i < rest.size ())
	{
		size_t j = rest.find ('/', i);
		if (j == std::string::npos) j = rest.size ();
		std::string w = rest.substr (i, j - i);
		fs::path next = cur / U8 (w);
		std::error_code ec;
		if (!fs::exists (next, ec))
		{
			for (auto &e : fs::directory_iterator (cur, ec))
				if (ieq (S8 (e.path ().filename ()), w)) { next = e.path (); break; }
		}
		cur = next;
		i = j + 1;
	}
	return S8 (cur);
}

static FILE *host_fopen (const std::string &h, const char *mode)
{
#ifdef _WIN32
	std::wstring m (mode, mode + strlen (mode));
	return _wfopen (U8 (h).wstring ().c_str (), m.c_str ());
#else
	return fopen (h.c_str (), mode);
#endif
}

static int fseek64 (FILE *f, long long off, int whence)
{
#ifdef _WIN32
	return _fseeki64 (f, off, whence);
#else
	return fseeko (f, (off_t) off, whence);
#endif
}

static long long ftell64 (FILE *f)
{
#ifdef _WIN32
	return _ftelli64 (f);
#else
	return (long long) ftello (f);
#endif
}

static int err_of (const std::error_code &ec)
{
	if (ec == std::errc::no_such_file_or_directory) return -KAPI_ENOENT;
	if (ec == std::errc::file_exists) return -KAPI_EEXIST;
	if (ec == std::errc::directory_not_empty) return -KAPI_ENOTEMPTY;
	if (ec == std::errc::not_a_directory) return -KAPI_ENOTDIR;
	if (ec == std::errc::is_a_directory) return -KAPI_EISDIR;
	if (ec == std::errc::permission_denied) return -KAPI_EACCES;
	if (ec == std::errc::no_space_on_device) return -KAPI_ENOSPC;
	return -KAPI_EIO;
}

// ---- the handles -----------------------------------------------------------------------------------------------
enum Kind { H_FILE, H_DIR, H_PIPE, H_CONIN, H_CONOUT, H_FILEIN, H_FILEOUT };

struct Pipe
{
	std::mutex m;
	std::condition_variable cv;
	std::deque<u8> data;
	bool eof = false;
	int readers = 0, writers = 0;
};

struct Obj
{
	Kind kind;
	FILE *f = 0;				// H_FILE, H_FILEIN, H_FILEOUT
	unsigned oflags = 0;
	long long pos = 0;			// H_FILE's offset
	std::string onyx, host;
	std::vector<fs::directory_entry> entries;	// H_DIR
	size_t next = 0;
	std::shared_ptr<Pipe> pipe;		// H_PIPE
	int refs = 1;
	std::mutex m;
};

static std::mutex s_HM;
static std::map<u64, std::shared_ptr<Obj>> s_H;
static u64 s_NextH = 0x1000;

static u64 h_add (std::shared_ptr<Obj> o)
{
	std::lock_guard<std::mutex> L (s_HM);
	u64 h = s_NextH++;
	s_H[h] = o;
	return h;
}

static std::shared_ptr<Obj> h_get (u64 h, int kind = -1)
{
	std::lock_guard<std::mutex> L (s_HM);
	auto it = s_H.find (h);
	if (it == s_H.end () || (kind >= 0 && it->second->kind != kind)) return nullptr;
	return it->second;
}

static void obj_close (Obj &o)
{
	if (o.f) { fclose (o.f); o.f = 0; }
	if (o.pipe && o.kind == H_PIPE)
	{
		std::lock_guard<std::mutex> L (o.pipe->m);
		o.pipe->eof = true;		// (the last reference to a pipe stream: its readers see the end)
		o.pipe->cv.notify_all ();
	}
}

static bool h_drop (u64 h)
{
	std::shared_ptr<Obj> o;
	{
		std::lock_guard<std::mutex> L (s_HM);
		auto it = s_H.find (h);
		if (it == s_H.end ()) return false;
		o = it->second;
		if (--o->refs > 0) return true;
		s_H.erase (it);
	}
	std::lock_guard<std::mutex> L (o->m);
	obj_close (*o);
	return true;
}

static u64 s_StdIn, s_StdOut;
void *stream_stdin (void)
{
	std::lock_guard<std::mutex> L (s_HM);
	if (!s_StdIn) { auto o = std::make_shared<Obj> (); o->kind = H_CONIN; o->refs = 1 << 30; s_StdIn = s_NextH++; s_H[s_StdIn] = o; }
	return (void *) s_StdIn;
}
void *stream_stdout (void)
{
	std::lock_guard<std::mutex> L (s_HM);
	if (!s_StdOut) { auto o = std::make_shared<Obj> (); o->kind = H_CONOUT; o->refs = 1 << 30; s_StdOut = s_NextH++; s_H[s_StdOut] = o; }
	return (void *) s_StdOut;
}

// ---- stat --------------------------------------------------------------------------------------------------------
static u64 fnv_upper (const std::string &s)
{
	u64 h = 0xcbf29ce484222325ULL;
	for (unsigned char c : s) { h ^= (u64) toupper (c); h *= 0x100000001b3ULL; }
	return h;
}

static bool host_stat (const std::string &h, u64 *size, long long *mtime, bool *dir)
{
#ifdef _WIN32
	struct _stat64 st;
	if (_wstat64 (U8 (h).wstring ().c_str (), &st) != 0) return false;
	*dir = (st.st_mode & _S_IFDIR) != 0;
#else
	struct stat st;
	if (stat (h.c_str (), &st) != 0) return false;
	*dir = S_ISDIR (st.st_mode);
#endif
	*size = *dir ? 0 : (u64) st.st_size;
	*mtime = (long long) st.st_mtime;
	return true;
}

static int vol_dev (const std::string &onyx) { return onyx.compare (0, 4, "RAM:") == 0 ? 9 : 0; }

static void fill_stat (struct kapi_stat *S, const std::string &onyx, u64 size, long long mtime, bool dir)
{
	memset (S, 0, sizeof *S);
	S->size = size;
	S->mtime = S->ctime = mtime;
	S->ino = fnv_upper (onyx);
	S->mode = dir ? (KAPI_S_IFDIR | 0777) : (KAPI_S_IFREG | 0666);
	S->dev = (unsigned) vol_dev (onyx);
	S->blksize = 32768;
	S->attr = dir ? 0x10 : 0x20;
	S->blocks = (size + 511) / 512;
}

// ---- the v1 calls: read-only files -----------------------------------------------------------------------------
static bool arg_path (const char *p, std::string &onyx, std::string &host)
{
	std::string s;
	if (!guest_str (p, s, 1024)) return false;
	onyx = onyx_abs (s.c_str ());
	host = host_path (s.c_str ());
	return !onyx.empty () && !host.empty ();
}

static void *k_open (const char *path)
{
	std::string onyx, host;
	if (!arg_path (path, onyx, host)) return 0;
	bool dir = false; u64 size; long long mt;
	if (!host_stat (host, &size, &mt, &dir) || dir) return 0;
	FILE *f = host_fopen (host, "rb");
	if (!f) return 0;
	auto o = std::make_shared<Obj> ();
	o->kind = H_FILE; o->f = f; o->onyx = onyx; o->host = host; o->oflags = KAPI_O_RDONLY;
	return (void *) h_add (o);
}

static int k_read (void *h, void *buf, unsigned n)
{
	auto o = h_get ((u64) h, H_FILE);
	if (!o || !hm_ok (buf, n, MEM_W)) return -1;
	std::lock_guard<std::mutex> L (o->m);
	fseek64 (o->f, o->pos, SEEK_SET);
	size_t r = fread (buf, 1, n, o->f);
	o->pos += (long long) r;
	return (int) r;
}

static unsigned long long k_fsize64 (void *h)
{
	auto o = h_get ((u64) h, H_FILE);
	if (!o) return 0;
	std::lock_guard<std::mutex> L (o->m);
	long long here = ftell64 (o->f);
	fseek64 (o->f, 0, SEEK_END);
	long long n = ftell64 (o->f);
	fseek64 (o->f, here, SEEK_SET);
	return n < 0 ? 0 : (unsigned long long) n;
}
static unsigned k_fsize (void *h) { u64 n = k_fsize64 (h); return n > 0xFFFFFFFFull ? 0xFFFFFFFFu : (unsigned) n; }

static int k_seek (void *h, unsigned long long pos)
{
	auto o = h_get ((u64) h, H_FILE);
	if (!o) return -1;
	std::lock_guard<std::mutex> L (o->m);
	o->pos = (long long) pos;
	return 0;
}

static void k_close (void *h) { h_drop ((u64) h); }

static int k_save_file (const char *path, const void *data, unsigned n)
{
	std::string onyx, host;
	if (!arg_path (path, onyx, host) || !hm_ok (data, n, MEM_R)) return 0;
	FILE *f = host_fopen (host, "wb");
	if (!f) return 0;
	bool ok = fwrite (data, 1, n, f) == n;
	ok = fclose (f) == 0 && ok;
	return ok ? 1 : 0;
}

// ---- folders ----------------------------------------------------------------------------------------------------
static void *k_opendir (const char *path)
{
	std::string onyx, host;
	if (!arg_path (path, onyx, host)) return 0;
	std::error_code ec;
	if (!fs::is_directory (U8 (host), ec)) return 0;
	auto o = std::make_shared<Obj> ();
	o->kind = H_DIR; o->onyx = onyx; o->host = host;
	for (auto &e : fs::directory_iterator (U8 (host), ec)) o->entries.push_back (e);
	std::sort (o->entries.begin (), o->entries.end (), [] (const fs::directory_entry &a, const fs::directory_entry &b)
		{
			std::string x = S8 (a.path ().filename ()), y = S8 (b.path ().filename ());
			for (auto &c : x) c = (char) tolower ((unsigned char) c);
			for (auto &c : y) c = (char) tolower ((unsigned char) c);
			return x < y;
		});
	return (void *) h_add (o);
}

static bool dir_next (Obj &o, std::string &name, u64 *size, long long *mtime, bool *dir)
{
	while (o.next < o.entries.size ())
	{
		const fs::directory_entry &e = o.entries[o.next++];
		name = S8 (e.path ().filename ());
		if (host_stat (S8 (e.path ()), size, mtime, dir)) return true;
	}
	return false;
}

static int k_readdir (void *h, struct kapi_dirent *ent)
{
	auto o = h_get ((u64) h, H_DIR);
	if (!o || !hm_ok (ent, sizeof *ent, MEM_W)) return 0;
	std::lock_guard<std::mutex> L (o->m);
	std::string name; u64 size; long long mt; bool dir;
	if (!dir_next (*o, name, &size, &mt, &dir)) return 0;
	memset (ent, 0, sizeof *ent);
	strncpy (ent->name, name.c_str (), sizeof ent->name - 1);
	ent->size = size > 0xFFFFFFFFull ? 0xFFFFFFFFu : (unsigned) size;
	ent->is_dir = dir ? 1 : 0;
	return 1;
}

static int k_dir_read (void *h, struct kapi_dirent2 *out)
{
	auto o = h_get ((u64) h, H_DIR);
	if (!o) return -KAPI_EBADF;
	if (!hm_ok (out, sizeof *out, MEM_W)) return -KAPI_EFAULT;
	std::lock_guard<std::mutex> L (o->m);
	std::string name; u64 size; long long mt; bool dir;
	if (!dir_next (*o, name, &size, &mt, &dir)) return 0;
	memset (out, 0, sizeof *out);
	strncpy (out->name, name.c_str (), sizeof out->name - 1);
	out->size = size;
	out->mtime = mt;
	out->mode = dir ? (KAPI_S_IFDIR | 0777) : (KAPI_S_IFREG | 0666);
	out->attr = dir ? 0x10 : 0x20;
	out->ino = fnv_upper (o->onyx + "/" + name);
	return 1;
}

static void k_closedir (void *h) { h_drop ((u64) h); }

static int k_mkdir (const char *path)
{
	std::string onyx, host;
	if (!arg_path (path, onyx, host)) return -1;
	std::error_code ec;
	return fs::create_directory (U8 (host), ec) ? 0 : -1;
}

static int k_remove (const char *path)
{
	std::string onyx, host;
	if (!arg_path (path, onyx, host)) return -1;
	std::error_code ec;
	return fs::remove (U8 (host), ec) ? 0 : -1;
}

static int k_rename (const char *from, const char *to)
{
	std::string o1, h1, o2, h2;
	if (!arg_path (from, o1, h1) || !arg_path (to, o2, h2)) return -1;
	std::error_code ec;
	fs::rename (U8 (h1), U8 (h2), ec);
	return ec ? -1 : 0;
}

static int k_chdir (const char *path)
{
	std::string onyx, host;
	if (!arg_path (path, onyx, host)) return 0;
	std::error_code ec;
	if (!fs::is_directory (U8 (host), ec)) return 0;
	std::lock_guard<std::mutex> L (g_Proc.lock);
	g_Proc.cwd = onyx;
	return 1;
}

static int k_getcwd (char *buf, unsigned cap)
{
	std::string c;
	{ std::lock_guard<std::mutex> L (g_Proc.lock); c = g_Proc.cwd; }
	return str_out (buf, cap, c);
}

// ---- streams -----------------------------------------------------------------------------------------------------
static void *k_pipe (void)
{
	auto o = std::make_shared<Obj> ();
	o->kind = H_PIPE;
	o->pipe = std::make_shared<Pipe> ();
	return (void *) h_add (o);
}

static void *k_file_in (const char *path)
{
	std::string onyx, host;
	if (!arg_path (path, onyx, host)) return 0;
	FILE *f = host_fopen (host, "rb");
	if (!f) return 0;
	auto o = std::make_shared<Obj> ();
	o->kind = H_FILEIN; o->f = f; o->onyx = onyx;
	return (void *) h_add (o);
}

static void *k_file_out (const char *path, int append)
{
	std::string onyx, host;
	if (!arg_path (path, onyx, host)) return 0;
	FILE *f = host_fopen (host, append ? "ab" : "wb");
	if (!f) return 0;
	auto o = std::make_shared<Obj> ();
	o->kind = H_FILEOUT; o->f = f; o->onyx = onyx;
	return (void *) h_add (o);
}

// -> bytes, 0 the end, -1 an error; nb: -1 would block (as stream_read_nb)
static int stream_read (u64 h, void *buf, unsigned n, bool nb)
{
	auto o = h_get (h);
	if (!o || !hm_ok (buf, n, MEM_W)) return -1;
	switch (o->kind)
	{
	case H_CONIN:
		if (nb) return -1;		// (the console: a blocking read only)
		return (int) fread (buf, 1, n, stdin);
	case H_FILEIN: case H_FILE:
	{
		std::lock_guard<std::mutex> L (o->m);
		return (int) fread (buf, 1, n, o->f);
	}
	case H_PIPE:
	{
		Pipe &P = *o->pipe;
		std::unique_lock<std::mutex> L (P.m);
		while (P.data.empty () && !P.eof)
		{
			if (nb) return -1;
			P.cv.wait (L);
		}
		unsigned k = 0;
		while (k < n && !P.data.empty ()) { ((u8 *) buf)[k++] = P.data.front (); P.data.pop_front (); }
		P.cv.notify_all ();
		return (int) k;
	}
	default: return -1;
	}
}

static int stream_write (u64 h, const void *buf, unsigned n, bool nb)
{
	auto o = h_get (h);
	if (!o || !hm_ok (buf, n, MEM_R)) return -1;
	switch (o->kind)
	{
	case H_CONOUT:
		fwrite (buf, 1, n, stdout);
		fflush (stdout);
		return (int) n;
	case H_FILEOUT: case H_FILE:
	{
		std::lock_guard<std::mutex> L (o->m);
		return (int) fwrite (buf, 1, n, o->f);
	}
	case H_PIPE:
	{
		Pipe &P = *o->pipe;
		std::unique_lock<std::mutex> L (P.m);
		const size_t CAP = 64 * 1024;
		if (nb && P.data.size () >= CAP) return -KAPI_EAGAIN;
		unsigned k = 0;
		while (k < n)
		{
			while (P.data.size () >= CAP && !nb) P.cv.wait (L);
			if (P.data.size () >= CAP) break;
			P.data.push_back (((const u8 *) buf)[k++]);
		}
		P.cv.notify_all ();
		return (int) k;
	}
	default: return -1;
	}
}

static int k_stream_read (void *h, void *buf, unsigned n)	{ return stream_read ((u64) h, buf, n, false); }
static int k_stream_read_nb (void *h, void *buf, unsigned n)	{ return stream_read ((u64) h, buf, n, true); }
static int k_stream_write (void *h, const void *buf, unsigned n){ return stream_write ((u64) h, buf, n, false); }
static int k_stream_write_nb (void *h, const void *buf, unsigned n)
{
	if (!h_get ((u64) h)) return -KAPI_EBADF;
	return stream_write ((u64) h, buf, n, true);
}
static void k_stream_close (void *h)				{ h_drop ((u64) h); }
static void k_stream_eof (void *h)
{
	auto o = h_get ((u64) h, H_PIPE);
	if (!o) return;
	std::lock_guard<std::mutex> L (o->pipe->m);
	o->pipe->eof = true;
	o->pipe->cv.notify_all ();
}

// ---- the v75 files ---------------------------------------------------------------------------------------------
static long long k_file_open (const char *path, unsigned flags, unsigned mode)
{
	(void) mode;
	std::string s;
	if (!guest_str (path, s, 1024)) return -KAPI_EFAULT;
	std::string onyx = onyx_abs (s.c_str ()), host = host_path (s.c_str ());
	if (onyx.empty ()) return -KAPI_ENAMETOOLONG;
	if (host.empty ()) return -KAPI_ENOENT;
	bool dir = false, exists; u64 size; long long mt;
	exists = host_stat (host, &size, &mt, &dir);
	if (exists && dir) return (flags & KAPI_O_ACCMODE) == KAPI_O_RDONLY ? -KAPI_EISDIR : -KAPI_EISDIR;
	if (exists && (flags & KAPI_O_CREAT) && (flags & KAPI_O_EXCL)) return -KAPI_EEXIST;
	if (!exists && !(flags & KAPI_O_CREAT)) return -KAPI_ENOENT;
	unsigned acc = flags & KAPI_O_ACCMODE;
	const char *m;
	if (!exists || (flags & KAPI_O_TRUNC)) m = acc == KAPI_O_RDONLY ? "wb+" : "wb+";
	else m = acc == KAPI_O_RDONLY ? "rb" : "rb+";
	if (!exists)
	{
		std::error_code ec;
		if (!fs::is_directory (U8 (host).parent_path (), ec)) return -KAPI_ENOENT;
	}
	FILE *f = host_fopen (host, m);
	if (!f) return -KAPI_EACCES;
	auto o = std::make_shared<Obj> ();
	o->kind = H_FILE; o->f = f; o->onyx = onyx; o->host = host; o->oflags = flags;
	return (long long) h_add (o);
}

static long long k_file_read (long long h, void *buf, unsigned long long n, long long off)
{
	auto o = h_get ((u64) h, H_FILE);
	if (!o) return -KAPI_EBADF;
	if ((o->oflags & KAPI_O_ACCMODE) == KAPI_O_WRONLY) return -KAPI_EBADF;
	if (!hm_ok (buf, n, MEM_W)) return -KAPI_EFAULT;
	std::lock_guard<std::mutex> L (o->m);
	long long at = off >= 0 ? off : o->pos;
	fseek64 (o->f, at, SEEK_SET);
	size_t r = fread (buf, 1, (size_t) n, o->f);
	if (off < 0) o->pos = at + (long long) r;
	return (long long) r;
}

static long long k_file_write (long long h, const void *buf, unsigned long long n, long long off)
{
	auto o = h_get ((u64) h, H_FILE);
	if (!o) return -KAPI_EBADF;
	if ((o->oflags & KAPI_O_ACCMODE) == KAPI_O_RDONLY) return -KAPI_EBADF;
	if (!hm_ok (buf, n, MEM_R)) return -KAPI_EFAULT;
	std::lock_guard<std::mutex> L (o->m);
	long long at = off >= 0 ? off : o->pos;
	if (off < 0 && (o->oflags & KAPI_O_APPEND)) { fseek64 (o->f, 0, SEEK_END); at = ftell64 (o->f); }
	fseek64 (o->f, at, SEEK_SET);
	size_t w = fwrite (buf, 1, (size_t) n, o->f);
	fflush (o->f);
	if (off < 0) o->pos = at + (long long) w;
	return w == n ? (long long) w : (w ? (long long) w : -KAPI_ENOSPC);
}

static long long k_file_seek (long long h, long long off, int whence)
{
	auto o = h_get ((u64) h, H_FILE);
	if (!o) return -KAPI_EBADF;
	std::lock_guard<std::mutex> L (o->m);
	long long base = 0;
	if (whence == KAPI_SEEK_CUR) base = o->pos;
	else if (whence == KAPI_SEEK_END) { fseek64 (o->f, 0, SEEK_END); base = ftell64 (o->f); }
	else if (whence != KAPI_SEEK_SET) return -KAPI_EINVAL;
	if (base + off < 0) return -KAPI_EINVAL;
	o->pos = base + off;
	return o->pos;
}

static int k_file_truncate (long long h, long long size)
{
	auto o = h_get ((u64) h, H_FILE);
	if (!o) return -KAPI_EBADF;
	if (size < 0) return -KAPI_EINVAL;
	std::lock_guard<std::mutex> L (o->m);
	fflush (o->f);
	std::error_code ec;
	fs::resize_file (U8 (o->host), (uintmax_t) size, ec);
	return ec ? err_of (ec) : 0;
}

static int k_file_sync (long long h) { auto o = h_get ((u64) h, H_FILE); if (!o) return -KAPI_EBADF; fflush (o->f); return 0; }

static int k_file_stat (long long h, struct kapi_stat *out)
{
	auto o = h_get ((u64) h);
	if (!o) return -KAPI_EBADF;
	if (!hm_ok (out, sizeof *out, MEM_W)) return -KAPI_EFAULT;
	if (o->kind != H_FILE) { struct kapi_stat S; fill_stat (&S, "", 0, 0, false); S.mode = 0010000 | 0666; *out = S; return 0; }
	std::lock_guard<std::mutex> L (o->m);
	fflush (o->f);
	u64 size; long long mt; bool dir;
	if (!host_stat (o->host, &size, &mt, &dir)) return -KAPI_EIO;
	struct kapi_stat S;
	fill_stat (&S, o->onyx, size, mt, dir);
	*out = S;
	return 0;
}

static int k_file_close (long long h)	{ return h_get ((u64) h, H_FILE) && h_drop ((u64) h) ? 0 : -KAPI_EBADF; }
static int k_handle_close (long long h)	{ return h_drop ((u64) h) ? 0 : -KAPI_EBADF; }

static int k_path_stat (const char *path, struct kapi_stat *out)
{
	std::string s;
	if (!guest_str (path, s, 1024) || !hm_ok (out, sizeof *out, MEM_W)) return -KAPI_EFAULT;
	std::string onyx = onyx_abs (s.c_str ()), host = host_path (s.c_str ());
	if (host.empty ()) return -KAPI_ENOENT;
	u64 size; long long mt; bool dir;
	if (onyx.size () > 0 && onyx.back () == '/')	// a volume's root
	{
		struct kapi_stat S; fill_stat (&S, onyx, 0, 0, true); *out = S; return 0;
	}
	if (!host_stat (host, &size, &mt, &dir)) return -KAPI_ENOENT;
	struct kapi_stat S;
	fill_stat (&S, onyx, size, mt, dir);
	*out = S;
	return 0;
}

static int k_path_unlink (const char *path, unsigned flags)
{
	std::string onyx, host;
	if (!arg_path (path, onyx, host)) return -KAPI_ENOENT;
	std::error_code ec;
	bool isdir = fs::is_directory (U8 (host), ec);
	if (!fs::exists (U8 (host), ec)) return -KAPI_ENOENT;
	if (flags & 1) { if (!isdir) return -KAPI_ENOTDIR; }	// (KAPI_UNLINK_DIR)
	else if (isdir) return -KAPI_EISDIR;
	if (isdir && !fs::is_empty (U8 (host), ec)) return -KAPI_ENOTEMPTY;
	fs::remove (U8 (host), ec);
	return ec ? err_of (ec) : 0;
}

static int k_path_mkdir (const char *path, unsigned mode)
{
	(void) mode;
	std::string onyx, host;
	if (!arg_path (path, onyx, host)) return -KAPI_ENOENT;
	std::error_code ec;
	if (fs::exists (U8 (host), ec)) return -KAPI_EEXIST;
	if (!fs::is_directory (U8 (host).parent_path (), ec)) return -KAPI_ENOENT;
	fs::create_directory (U8 (host), ec);
	return ec ? err_of (ec) : 0;
}

static int k_path_rename (const char *from, const char *to)
{
	std::string o1, h1, o2, h2;
	if (!arg_path (from, o1, h1) || !arg_path (to, o2, h2)) return -KAPI_ENOENT;
	if (o1.substr (0, o1.find (':')) != o2.substr (0, o2.find (':'))) return -KAPI_EXDEV;
	std::error_code ec;
	if (!fs::exists (U8 (h1), ec)) return -KAPI_ENOENT;
	fs::rename (U8 (h1), U8 (h2), ec);
	return ec ? err_of (ec) : 0;
}

static int k_path_utime (const char *path, long long mtime)
{
	std::string onyx, host;
	if (!arg_path (path, onyx, host)) return -KAPI_ENOENT;
#ifdef _WIN32
	struct __utimbuf64 t = { (__time64_t) mtime, (__time64_t) mtime };
	return _wutime64 (U8 (host).wstring ().c_str (), &t) == 0 ? 0 : -KAPI_ENOENT;
#else
	struct utimbuf t = { (time_t) mtime, (time_t) mtime };
	return utime (host.c_str (), &t) == 0 ? 0 : -KAPI_ENOENT;
#endif
}

static int k_vol_info (const char *path, struct kapi_vol_info *out)
{
	std::string s;
	if (!guest_str (path, s, 1024) || !hm_ok (out, sizeof *out, MEM_W)) return -1;
	std::string onyx = onyx_abs (s.c_str ());
	std::string vol = onyx.substr (0, onyx.find (':'));
	if (vol_root (vol).empty ()) return -1;
	memset (out, 0, sizeof *out);
	return 0;
}

KAPI (open, k_open);
KAPI (read, k_read);
KAPI (fsize, k_fsize);
KAPI (fsize64, k_fsize64);
KAPI (seek, k_seek);
KAPI (close, k_close);
KAPI (save_file, k_save_file);
KAPI (opendir, k_opendir);
KAPI (readdir, k_readdir);
KAPI (closedir, k_closedir);
KAPI (dir_read, k_dir_read);
KAPI (mkdir, k_mkdir);
KAPI (remove, k_remove);
KAPI (rename, k_rename);
KAPI (chdir, k_chdir);
KAPI (getcwd, k_getcwd);
KAPI (pipe, k_pipe);
KAPI (file_in, k_file_in);
KAPI (file_out, k_file_out);
KAPI (stream_read, k_stream_read);
KAPI (stream_read_nb, k_stream_read_nb);
KAPI (stream_write, k_stream_write);
KAPI (stream_write_nb, k_stream_write_nb);
KAPI (stream_close, k_stream_close);
KAPI (stream_eof, k_stream_eof);
KAPI (file_open, k_file_open);
KAPI (file_read, k_file_read);
KAPI (file_write, k_file_write);
KAPI (file_seek, k_file_seek);
KAPI (file_truncate, k_file_truncate);
KAPI (file_sync, k_file_sync);
KAPI (file_stat, k_file_stat);
KAPI (file_close, k_file_close);
KAPI (handle_close, k_handle_close);
KAPI (path_stat, k_path_stat);
KAPI (path_unlink, k_path_unlink);
KAPI (path_mkdir, k_path_mkdir);
KAPI (path_rename, k_path_rename);
KAPI (path_utime, k_path_utime);
KAPI (vol_info, k_vol_info);
