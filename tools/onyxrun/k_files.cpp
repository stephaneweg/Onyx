//
// k_files.cpp -- the files, the folders and the streams (onyxrun.h, kobj.h), on host folders: SD: is the runner's
// root (the repository's sdcard/ by default), RAM: a folder of its own. FAT's rules kept where a program sees them:
// names without case (an existing name found whatever its case), "SD:x" = "SD:/x", '\' = '/'.
//
//   - the v1..v59 calls (kernel/sys/kapi.cpp): open / read / fsize / seek / close (read-only files), save_file,
//     opendir / readdir / closedir, mkdir / remove / rename, chdir / getcwd, the streams (pipe, file_in, file_out,
//     stream_*, the console's);
//   - the v75 calls (kernel/sys/ofile.cpp, vfs.cpp): file_open / read / write / seek / truncate / sync / stat /
//     close, path_stat / unlink / mkdir / rename / utime, dir_read, stream_write_nb, handle_close.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include "kobj.h"
#include <filesystem>
#include <algorithm>
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

static fs::path U8 (const std::string &s)
{
	return fs::u8path (s);
}

static std::string S8 (const fs::path &p)
{
	return p.u8string ();
}

static bool ieq (const std::string &a, const std::string &b)
{
	if (a.size () != b.size ()) return false;
	for (size_t i = 0; i < a.size (); i++)
		if (tolower ((unsigned char) a[i]) != tolower ((unsigned char) b[i])) return false;
	return true;
}

// ---- the paths -------------------------------------------------------------------------------------------------
std::string onyx_abs (const char *p, const std::string &cwd)
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
		size_t c = cwd.find (':');
		if (c == std::string::npos) return "";
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
	if (vol == "SD") return g_Run.root;
	if (vol == "RAM") return g_Run.ramRoot;
	return "";
}

std::string host_path (const char *onyx, const std::string &cwd)
{
	std::string a = onyx_abs (onyx, cwd);
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
Obj::~Obj ()
{
	if (f) fclose (f);
	if (pipe && kind == H_PIPE)
	{
		std::lock_guard<std::mutex> L (pipe->m);
		pipe->eof = true;		// (the last reference to a pipe: its readers see the end)
		pipe->cv.notify_all ();
	}
}

u64 h_add (Proc *P, std::shared_ptr<Obj> o)
{
	std::lock_guard<std::mutex> L (P->m);
	u64 h = P->nextHandle++;
	P->handles[h] = o;
	return h;
}

std::shared_ptr<Obj> h_get (Proc *P, u64 h, int kind)
{
	std::lock_guard<std::mutex> L (P->m);
	auto it = P->handles.find (h);
	if (it == P->handles.end () || (kind >= 0 && it->second->kind != kind)) return nullptr;
	return it->second;
}

bool h_drop (Proc *P, u64 h)
{
	std::shared_ptr<Obj> o;
	{
		std::lock_guard<std::mutex> L (P->m);
		auto it = P->handles.find (h);
		if (it == P->handles.end ()) return false;
		o = it->second;
		P->handles.erase (it);
	}
	return true;					// (the object goes when its last holder lets it go)
}

void h_drop_all (Proc *P)
{
	std::map<u64, std::shared_ptr<Obj>> all;
	{
		std::lock_guard<std::mutex> L (P->m);
		all.swap (P->handles);
	}
}

std::shared_ptr<Obj> obj_console_in (void)
{
	static std::shared_ptr<Obj> o = [] { auto x = std::make_shared<Obj> (); x->kind = H_CONIN; return x; } ();
	return o;
}
std::shared_ptr<Obj> obj_console_out (void)
{
	static std::shared_ptr<Obj> o = [] { auto x = std::make_shared<Obj> (); x->kind = H_CONOUT; return x; } ();
	return o;
}
std::shared_ptr<Obj> obj_pipe (void)
{
	auto o = std::make_shared<Obj> ();
	o->kind = H_PIPE;
	o->pipe = std::make_shared<Pipe> ();
	return o;
}

void obj_eof (Obj &o)
{
	if (!o.pipe) return;
	std::lock_guard<std::mutex> L (o.pipe->m);
	o.pipe->eof = true;
	o.pipe->cv.notify_all ();
}

int obj_read (Obj &o, void *buf, unsigned n, bool nb)
{
	switch (o.kind)
	{
	case H_CONIN:
		if (nb) return -1;		// (the host's console: a blocking read only)
		return (int) fread (buf, 1, n, stdin);
	case H_FILEIN: case H_FILE:
	{
		std::lock_guard<std::mutex> L (o.m);
		return (int) fread (buf, 1, n, o.f);
	}
	case H_PIPE:
	{
		Pipe &P = *o.pipe;
		std::unique_lock<std::mutex> L (P.m);
		while (P.data.empty () && !P.eof)
		{
			if (nb) return -1;
			P.cv.wait_for (L, std::chrono::milliseconds (50));
			if (P.data.empty () && !P.eof) { L.unlock (); check_dying (); L.lock (); }
		}
		unsigned k = 0;
		while (k < n && !P.data.empty ()) { ((u8 *) buf)[k++] = P.data.front (); P.data.pop_front (); }
		P.cv.notify_all ();
		return (int) k;
	}
	default: return -1;
	}
}

int obj_write (Obj &o, const void *buf, unsigned n, bool nb)
{
	switch (o.kind)
	{
	case H_CONOUT:
		fwrite (buf, 1, n, stdout);
		fflush (stdout);
		return (int) n;
	case H_FILEOUT: case H_FILE:
	{
		std::lock_guard<std::mutex> L (o.m);
		return (int) fwrite (buf, 1, n, o.f);
	}
	case H_PIPE:
	{
		Pipe &P = *o.pipe;
		std::unique_lock<std::mutex> L (P.m);
		const size_t CAP = 64 * 1024;
		if (nb && P.data.size () >= CAP) return -KAPI_EAGAIN;
		unsigned k = 0;
		while (k < n)
		{
			while (P.data.size () >= CAP && !nb)
			{
				P.cv.wait_for (L, std::chrono::milliseconds (50));
				if (P.data.size () >= CAP) { L.unlock (); check_dying (); L.lock (); }
			}
			if (P.data.size () >= CAP) break;
			P.data.push_back (((const u8 *) buf)[k++]);
		}
		P.cv.notify_all ();
		return (int) k;
	}
	default: return -1;
	}
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

static void fill_stat (struct kapi_stat *S, const std::string &onyx, u64 size, long long mtime, bool dir)
{
	memset (S, 0, sizeof *S);
	S->size = size;
	S->mtime = S->ctime = mtime;
	S->ino = fnv_upper (onyx);
	S->mode = dir ? (KAPI_S_IFDIR | 0777) : (KAPI_S_IFREG | 0666);
	S->dev = onyx.compare (0, 4, "RAM:") == 0 ? 9 : 0;
	S->blksize = 32768;
	S->attr = dir ? 0x10 : 0x20;
	S->blocks = (size + 511) / 512;
}

// ---- the v1 calls: read-only files -----------------------------------------------------------------------------
static bool arg_path (u64 p, std::string &onyx, std::string &host)
{
	std::string s;
	if (!gstr (p, s, 1024)) return false;
	Proc *P = cur ();
	std::string cwd;
	{ std::lock_guard<std::mutex> L (P->m); cwd = P->cwd; }
	onyx = onyx_abs (s.c_str (), cwd);
	host = host_path (s.c_str (), cwd);
	return !onyx.empty () && !host.empty ();
}

static u64 k_open (u64 path)
{
	std::string onyx, host;
	if (!arg_path (path, onyx, host)) return 0;
	bool dir = false; u64 size; long long mt;
	if (!host_stat (host, &size, &mt, &dir) || dir) return 0;
	FILE *f = host_fopen (host, "rb");
	if (!f) return 0;
	auto o = std::make_shared<Obj> ();
	o->kind = H_FILE; o->f = f; o->onyx = onyx; o->host = host; o->oflags = KAPI_O_RDONLY;
	return h_add (cur (), o);
}

static int k_read (u64 h, u64 buf, unsigned n)
{
	auto o = h_get (cur (), h, H_FILE);
	u8 *b = GB (buf, n, MEM_W);
	if (!o || !b) return -1;
	std::lock_guard<std::mutex> L (o->m);
	fseek64 (o->f, o->pos, SEEK_SET);
	size_t r = fread (b, 1, n, o->f);
	o->pos += (long long) r;
	return (int) r;
}

static unsigned long long k_fsize64 (u64 h)
{
	auto o = h_get (cur (), h, H_FILE);
	if (!o) return 0;
	std::lock_guard<std::mutex> L (o->m);
	fseek64 (o->f, 0, SEEK_END);
	long long n = ftell64 (o->f);
	return n < 0 ? 0 : (unsigned long long) n;
}
static unsigned k_fsize (u64 h) { u64 n = k_fsize64 (h); return n > 0xFFFFFFFFull ? 0xFFFFFFFFu : (unsigned) n; }

static int k_seek (u64 h, unsigned long long pos)
{
	auto o = h_get (cur (), h, H_FILE);
	if (!o) return -1;
	std::lock_guard<std::mutex> L (o->m);
	o->pos = (long long) pos;
	return 0;
}

static void k_close (u64 h) { h_drop (cur (), h); }

static int k_save_file (u64 path, u64 data, unsigned n)
{
	std::string onyx, host;
	u8 *d = GB (data, n, MEM_R);
	if (!arg_path (path, onyx, host) || (!d && n)) return 0;
	FILE *f = host_fopen (host, "wb");
	if (!f) return 0;
	bool ok = n == 0 || fwrite (d, 1, n, f) == n;
	ok = fclose (f) == 0 && ok;
	return ok ? 1 : 0;
}

// ---- folders ----------------------------------------------------------------------------------------------------
static u64 k_opendir (u64 path)
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
	return h_add (cur (), o);
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

static int k_readdir (u64 h, u64 ent)
{
	auto o = h_get (cur (), h, H_DIR);
	struct kapi_dirent *e = G<struct kapi_dirent> (ent, MEM_W);
	if (!o || !e) return 0;
	std::lock_guard<std::mutex> L (o->m);
	std::string name; u64 size; long long mt; bool dir;
	if (!dir_next (*o, name, &size, &mt, &dir)) return 0;
	memset (e, 0, sizeof *e);
	strncpy (e->name, name.c_str (), sizeof e->name - 1);
	e->size = size > 0xFFFFFFFFull ? 0xFFFFFFFFu : (unsigned) size;
	e->is_dir = dir ? 1 : 0;
	return 1;
}

static int k_dir_read (u64 h, u64 out)
{
	auto o = h_get (cur (), h, H_DIR);
	if (!o) return -KAPI_EBADF;
	struct kapi_dirent2 *e = G<struct kapi_dirent2> (out, MEM_W);
	if (!e) return -KAPI_EFAULT;
	std::lock_guard<std::mutex> L (o->m);
	std::string name; u64 size; long long mt; bool dir;
	if (!dir_next (*o, name, &size, &mt, &dir)) return 0;
	memset (e, 0, sizeof *e);
	strncpy (e->name, name.c_str (), sizeof e->name - 1);
	e->size = size;
	e->mtime = mt;
	e->mode = dir ? (KAPI_S_IFDIR | 0777) : (KAPI_S_IFREG | 0666);
	e->attr = dir ? 0x10 : 0x20;
	e->ino = fnv_upper (o->onyx + "/" + name);
	return 1;
}

static void k_closedir (u64 h) { h_drop (cur (), h); }

static int k_mkdir (u64 path)
{
	std::string onyx, host;
	if (!arg_path (path, onyx, host)) return -1;
	std::error_code ec;
	return fs::create_directory (U8 (host), ec) ? 0 : -1;
}

static int k_remove (u64 path)
{
	std::string onyx, host;
	if (!arg_path (path, onyx, host)) return -1;
	std::error_code ec;
	return fs::remove (U8 (host), ec) ? 0 : -1;
}

static int k_rename (u64 from, u64 to)
{
	std::string o1, h1, o2, h2;
	if (!arg_path (from, o1, h1) || !arg_path (to, o2, h2)) return -1;
	std::error_code ec;
	fs::rename (U8 (h1), U8 (h2), ec);
	return ec ? -1 : 0;
}

static int k_chdir (u64 path)
{
	std::string onyx, host;
	if (!arg_path (path, onyx, host)) return 0;
	std::error_code ec;
	if (!fs::is_directory (U8 (host), ec)) return 0;
	Proc *P = cur ();
	std::lock_guard<std::mutex> L (P->m);
	P->cwd = onyx;
	return 1;
}

static int k_getcwd (u64 buf, unsigned cap)
{
	Proc *P = cur ();
	std::string c;
	{ std::lock_guard<std::mutex> L (P->m); c = P->cwd; }
	return gstr_out (buf, cap, c);
}

// ---- streams -----------------------------------------------------------------------------------------------------
static u64 k_pipe (void) { return h_add (cur (), obj_pipe ()); }

static u64 k_file_in (u64 path)
{
	std::string onyx, host;
	if (!arg_path (path, onyx, host)) return 0;
	FILE *f = host_fopen (host, "rb");
	if (!f) return 0;
	auto o = std::make_shared<Obj> ();
	o->kind = H_FILEIN; o->f = f; o->onyx = onyx;
	return h_add (cur (), o);
}

static u64 k_file_out (u64 path, int append)
{
	std::string onyx, host;
	if (!arg_path (path, onyx, host)) return 0;
	FILE *f = host_fopen (host, append ? "ab" : "wb");
	if (!f) return 0;
	auto o = std::make_shared<Obj> ();
	o->kind = H_FILEOUT; o->f = f; o->onyx = onyx;
	return h_add (cur (), o);
}

static int k_stream_read (u64 h, u64 buf, unsigned n)
{
	auto o = h_get (cur (), h);
	u8 *b = GB (buf, n, MEM_W);
	return o && b ? obj_read (*o, b, n, false) : -1;
}
static int k_stream_read_nb (u64 h, u64 buf, unsigned n)
{
	auto o = h_get (cur (), h);
	u8 *b = GB (buf, n, MEM_W);
	return o && b ? obj_read (*o, b, n, true) : -1;
}
static int k_stream_write (u64 h, u64 buf, unsigned n)
{
	auto o = h_get (cur (), h);
	u8 *b = GB (buf, n, MEM_R);
	return o && b ? obj_write (*o, b, n, false) : -1;
}
static int k_stream_write_nb (u64 h, u64 buf, unsigned n)
{
	auto o = h_get (cur (), h);
	if (!o) return -KAPI_EBADF;
	u8 *b = GB (buf, n, MEM_R);
	return b ? obj_write (*o, b, n, true) : -KAPI_EFAULT;
}
static void k_stream_close (u64 h)	{ h_drop (cur (), h); }
static void k_stream_eof (u64 h)	{ auto o = h_get (cur (), h, H_PIPE); if (o) obj_eof (*o); }

// ---- the v75 files ---------------------------------------------------------------------------------------------
static long long k_file_open (u64 path, unsigned flags, unsigned mode)
{
	(void) mode;
	std::string onyx, host;
	std::string s;
	if (!gstr (path, s, 1024)) return -KAPI_EFAULT;
	if (!arg_path (path, onyx, host)) return onyx.empty () ? -KAPI_ENAMETOOLONG : -KAPI_ENOENT;
	bool dir = false, exists; u64 size; long long mt;
	exists = host_stat (host, &size, &mt, &dir);
	if (exists && dir) return -KAPI_EISDIR;
	if (exists && (flags & KAPI_O_CREAT) && (flags & KAPI_O_EXCL)) return -KAPI_EEXIST;
	if (!exists && !(flags & KAPI_O_CREAT)) return -KAPI_ENOENT;
	unsigned acc = flags & KAPI_O_ACCMODE;
	const char *m;
	if (!exists || (flags & KAPI_O_TRUNC)) m = "wb+";
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
	return (long long) h_add (cur (), o);
}

static long long k_file_read (long long h, u64 buf, unsigned long long n, long long off)
{
	auto o = h_get (cur (), (u64) h, H_FILE);
	if (!o) return -KAPI_EBADF;
	if ((o->oflags & KAPI_O_ACCMODE) == KAPI_O_WRONLY) return -KAPI_EBADF;
	u8 *b = GB (buf, n, MEM_W);
	if (!b && n) return -KAPI_EFAULT;
	std::lock_guard<std::mutex> L (o->m);
	long long at = off >= 0 ? off : o->pos;
	fseek64 (o->f, at, SEEK_SET);
	size_t r = n ? fread (b, 1, (size_t) n, o->f) : 0;
	if (off < 0) o->pos = at + (long long) r;
	return (long long) r;
}

static long long k_file_write (long long h, u64 buf, unsigned long long n, long long off)
{
	auto o = h_get (cur (), (u64) h, H_FILE);
	if (!o) return -KAPI_EBADF;
	if ((o->oflags & KAPI_O_ACCMODE) == KAPI_O_RDONLY) return -KAPI_EBADF;
	u8 *b = GB (buf, n, MEM_R);
	if (!b && n) return -KAPI_EFAULT;
	std::lock_guard<std::mutex> L (o->m);
	long long at = off >= 0 ? off : o->pos;
	if (off < 0 && (o->oflags & KAPI_O_APPEND)) { fseek64 (o->f, 0, SEEK_END); at = ftell64 (o->f); }
	fseek64 (o->f, at, SEEK_SET);
	size_t w = n ? fwrite (b, 1, (size_t) n, o->f) : 0;
	fflush (o->f);
	if (off < 0) o->pos = at + (long long) w;
	return w == n ? (long long) w : (w ? (long long) w : -KAPI_ENOSPC);
}

static long long k_file_seek (long long h, long long off, int whence)
{
	auto o = h_get (cur (), (u64) h, H_FILE);
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
	auto o = h_get (cur (), (u64) h, H_FILE);
	if (!o) return -KAPI_EBADF;
	if (size < 0) return -KAPI_EINVAL;
	std::lock_guard<std::mutex> L (o->m);
	fflush (o->f);
	std::error_code ec;
	fs::resize_file (U8 (o->host), (uintmax_t) size, ec);
	return ec ? err_of (ec) : 0;
}

static int k_file_sync (long long h) { auto o = h_get (cur (), (u64) h, H_FILE); if (!o) return -KAPI_EBADF; fflush (o->f); return 0; }

static int k_file_stat (long long h, u64 out)
{
	auto o = h_get (cur (), (u64) h);
	if (!o) return -KAPI_EBADF;
	struct kapi_stat *S = G<struct kapi_stat> (out, MEM_W);
	if (!S) return -KAPI_EFAULT;
	if (o->kind != H_FILE) { fill_stat (S, "", 0, 0, false); S->mode = 0010000 | 0666; return 0; }	// (a FIFO)
	std::lock_guard<std::mutex> L (o->m);
	fflush (o->f);
	u64 size; long long mt; bool dir;
	if (!host_stat (o->host, &size, &mt, &dir)) return -KAPI_EIO;
	fill_stat (S, o->onyx, size, mt, dir);
	return 0;
}

static int k_file_close (long long h)	{ return h_get (cur (), (u64) h, H_FILE) && h_drop (cur (), (u64) h) ? 0 : -KAPI_EBADF; }
static int k_handle_close (long long h)	{ return h_drop (cur (), (u64) h) ? 0 : -KAPI_EBADF; }

static int k_path_stat (u64 path, u64 out)
{
	std::string onyx, host;
	struct kapi_stat *S = G<struct kapi_stat> (out, MEM_W);
	if (!S) return -KAPI_EFAULT;
	if (!arg_path (path, onyx, host)) return -KAPI_ENOENT;
	if (onyx.back () == '/') { fill_stat (S, onyx, 0, 0, true); return 0; }	// (a volume's root)
	u64 size; long long mt; bool dir;
	if (!host_stat (host, &size, &mt, &dir)) return -KAPI_ENOENT;
	fill_stat (S, onyx, size, mt, dir);
	return 0;
}

static int k_path_unlink (u64 path, unsigned flags)
{
	std::string onyx, host;
	if (!arg_path (path, onyx, host)) return -KAPI_ENOENT;
	std::error_code ec;
	if (!fs::exists (U8 (host), ec)) return -KAPI_ENOENT;
	bool isdir = fs::is_directory (U8 (host), ec);
	if (flags & 1) { if (!isdir) return -KAPI_ENOTDIR; }	// (KAPI_UNLINK_DIR)
	else if (isdir) return -KAPI_EISDIR;
	if (isdir && !fs::is_empty (U8 (host), ec)) return -KAPI_ENOTEMPTY;
	fs::remove (U8 (host), ec);
	return ec ? err_of (ec) : 0;
}

static int k_path_mkdir (u64 path, unsigned mode)
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

static int k_path_rename (u64 from, u64 to)
{
	std::string o1, h1, o2, h2;
	if (!arg_path (from, o1, h1) || !arg_path (to, o2, h2)) return -KAPI_ENOENT;
	if (o1.substr (0, o1.find (':')) != o2.substr (0, o2.find (':'))) return -KAPI_EXDEV;
	std::error_code ec;
	if (!fs::exists (U8 (h1), ec)) return -KAPI_ENOENT;
	fs::rename (U8 (h1), U8 (h2), ec);
	return ec ? err_of (ec) : 0;
}

static int k_path_utime (u64 path, long long mtime)
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

static int k_vol_info (u64 path, u64 out)
{
	std::string onyx, host;
	struct kapi_vol_info *o = G<struct kapi_vol_info> (out, MEM_W);
	if (!o || !arg_path (path, onyx, host)) return -1;
	memset (o, 0, sizeof *o);
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
