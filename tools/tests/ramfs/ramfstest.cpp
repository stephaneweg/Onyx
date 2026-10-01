//
// ramfstest -- the kernel's RAM file system (kernel/sys/ramfs.cpp, RAM:) on the PC: thousands of
// random operations (save, append and truncate through streams, read whole / in pieces / after a
// seek, list, mkdir, remove, rename) checked against a model in memory; then the edges: a full
// volume (a save that does not fit leaves no half file), a file removed while open (read to its
// end, freed at the close), a dead process's handles, the names (case, length), every page given
// back when everything is removed.
//   ramfstest <seed> <ops>
// run_ramfs_test.sh runs it with a few seeds.
//
#include <kern/ramfs.h>
#include <kern/kapi_abi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

// ---- the platform (ramfs.cpp's RAMFS_HOST_TEST calls) ------------------------------------------
static std::set<void *> s_pages;
static unsigned long long s_poolPages = 16384;		// 1 GB of "page memory"
static unsigned s_pid = 7;
void *RamPlatPageAlloc (void)
{
	if (s_pages.size () >= s_poolPages) return 0;
	void *p = aligned_alloc (65536, 65536);
	memset (p, 0xA5, 65536);				// (stale bytes: nothing may read them)
	s_pages.insert (p);
	return p;
}
void RamPlatPageFree (void *p)
{
	if (!s_pages.erase (p)) { printf ("FAIL: a page freed twice / not ours\n"); exit (1); }
	free (p);
}
u64 RamPlatPagesFree (void) { return (s_poolPages - s_pages.size ()) * 65536ull; }
static int s_task;
void *RamPlatTask (void) { return &s_task; }
void RamPlatYield (void) {}
unsigned RamPlatPid (void) { return s_pid; }

static unsigned s_seed;
static unsigned rnd (void) { s_seed = s_seed * 1103515245u + 12345u; return (s_seed >> 8) & 0xFFFFFF; }
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 20) { printf ("FAIL: "); printf (__VA_ARGS__); printf ("\n"); } } } while (0)

// ---- the model ---------------------------------------------------------------------------------
struct MNode { bool dir; std::string data; };
static std::map<std::string, MNode> M;			// key: the lower-cased path below RAM:
static std::string low (std::string s) { for (auto &c : s) c = (char) tolower (c); return s; }
static std::string parent (const std::string &p) { size_t k = p.rfind ('/'); return k == 0 ? "/" : p.substr (0, k); }
static bool mexists (const std::string &p) { return p == "/" || M.count (low (p)); }
static bool misdir (const std::string &p) { return p == "/" || (M.count (low (p)) && M[low (p)].dir); }
static std::string ram (const std::string &p) { return "RAM:" + p; }
static std::vector<std::string> mchildren (const std::string &d)
{
	std::vector<std::string> v; std::string pre = low (d) == "/" ? "/" : low (d) + "/";
	for (auto &kv : M) if (kv.first.compare (0, pre.size (), pre) == 0 && kv.first.find ('/', pre.size ()) == std::string::npos) v.push_back (kv.first);
	return v;
}

static std::vector<std::string> s_dirs = { "/" };	// (the model's folders, for picking)
static std::string pick_dir (void) { return s_dirs[rnd () % s_dirs.size ()]; }
static std::string join (const std::string &d, const std::string &n) { return d == "/" ? "/" + n : d + "/" + n; }
static std::string rand_name (void)
{
	static const char *names[] = { "a", "B", "cache", "Index", "x.bc", "0001.d", "0001.m", "Long-Name_With.Dots.txt", "z" };
	std::string n = names[rnd () % 9];
	if (rnd () % 3 == 0) n += std::to_string (rnd () % 20);
	return n;
}
static std::string rand_data (unsigned n)
{
	std::string s (n, '\0');
	unsigned x = rnd ();
	for (unsigned i = 0; i < n; i++) { x = x * 1664525u + 1013904223u; s[i] = (char) (x >> 24); }
	return s;
}
static unsigned rand_size (void)
{
	switch (rnd () % 8)
	{
	case 0: return 0;
	case 1: return rnd () % 300;
	case 2: case 3: return rnd () % 9000;
	case 4: case 5: return rnd () % 70000;
	case 6: return rnd () % 400000;
	default: return rnd () % 4 == 0 ? rnd () % 3000000 : rnd () % 200000;
	}
}

static std::string read_all (const std::string &p, int how)
{
	std::string out;
	if (how == 2)						// a stream
	{
		void *s = RamFsStreamOpen (ram (p).c_str (), 0);
		if (!s) return "<none>";
		std::vector<char> b (rnd () % 5000 + 1); int n;
		while ((n = RamFsStreamRead (s, b.data (), (unsigned) b.size ())) > 0) out.append (b.data (), (size_t) n);
		RamFsStreamClose (s);
		return out;
	}
	void *h = RamFsOpen (ram (p).c_str ());
	if (!h) return "<none>";
	u64 n = RamFsSize (h);
	if (how == 0) { out.resize (n); int r = RamFsRead (h, &out[0], (unsigned) n); CHECK ((u64) r == n || n == 0, "read whole %s: %d of %llu", p.c_str (), r, (unsigned long long) n); }
	else
	{
		std::vector<char> b (rnd () % 70000 + 1); int r;
		while ((r = RamFsRead (h, b.data (), (unsigned) b.size ())) > 0) out.append (b.data (), (size_t) r);
	}
	RamFsClose (h);
	return out;
}

static void check_file (const std::string &p)
{
	const std::string &want = M[low (p)].data;
	int how = rnd () % 3;
	std::string got = read_all (p, how);
	CHECK (got == want, "%s: read (%d) %zu bytes, the model %zu", p.c_str (), how, got.size (), want.size ());
	if (want.size () > 10)					// a seek
	{
		void *h = RamFsOpen (ram (p).c_str ());
		unsigned off = rnd () % (unsigned) want.size (), n = rnd () % 5000 + 1;
		CHECK (RamFsSeek (h, off) == 0, "seek");
		std::string b (n, '\0'); int r = RamFsRead (h, &b[0], n);
		b.resize (r > 0 ? (size_t) r : 0);
		CHECK (b == want.substr (off, n), "%s: seek %u read %u", p.c_str (), off, n);
		RamFsClose (h);
	}
}

static void check_dir (const std::string &d)
{
	void *h = RamFsOpenDir (ram (d).c_str ());
	CHECK (h != 0, "opendir %s", d.c_str ());
	if (!h) return;
	std::set<std::string> got; kapi_dirent e;
	while (RamFsReadDir (h, &e) > 0)
	{
		std::string k = low (join (d, e.name));
		got.insert (k);
		CHECK (M.count (k) && M[k].dir == (e.is_dir != 0) && (e.is_dir || e.size == M[k].data.size ()),
		       "listing %s: %s (dir %d size %u)", d.c_str (), e.name, e.is_dir, e.size);
	}
	RamFsCloseDir (h);
	std::vector<std::string> want = mchildren (d);
	CHECK (got.size () == want.size (), "listing %s: %zu entries, the model %zu", d.c_str (), got.size (), want.size ());
}

static void drop_dir_model (const std::string &p) { for (size_t i = 0; i < s_dirs.size (); i++) if (low (s_dirs[i]) == low (p)) { s_dirs.erase (s_dirs.begin () + i); break; } }

static void random_ops (unsigned nOps)
{
	for (unsigned op = 0; op < nOps; op++)
	{
		std::string d = pick_dir (), p = join (d, rand_name ());
		switch (rnd () % 12)
		{
		case 0:	// mkdir
		{
			int r = RamFsMkdir (ram (p).c_str ());
			bool ok = !mexists (p);
			CHECK ((r == 0) == ok, "mkdir %s -> %d", p.c_str (), r);
			if (r == 0) { M[low (p)] = { true, "" }; s_dirs.push_back (p); }
			break;
		}
		case 1: case 2:	// save
		{
			std::string data = rand_data (rand_size ());
			int r = RamFsSave (ram (p).c_str (), data.data (), (unsigned) data.size ());
			bool ok = !misdir (p);
			CHECK ((r == (int) data.size ()) == ok, "save %s (%zu) -> %d", p.c_str (), data.size (), r);
			if (ok) M[low (p)] = { false, data };
			break;
		}
		case 3:	// a stream: written (truncated) or appended, in pieces
		{
			int mode = 1 + rnd () % 2;
			void *s = RamFsStreamOpen (ram (p).c_str (), mode);
			bool ok = !misdir (p);
			CHECK ((s != 0) == ok, "stream %d %s", mode, p.c_str ());
			if (!s) break;
			std::string &m = M[low (p)].data;
			if (!M[low (p)].dir && mode == 1) m.clear ();
			M[low (p)].dir = false;
			unsigned n = rnd () % 6;
			for (unsigned i = 0; i < n; i++)
			{
				std::string piece = rand_data (rnd () % 3 == 0 ? rnd () % 200000 : rnd () % 600);
				int w = RamFsStreamWrite (s, piece.data (), (unsigned) piece.size ());
				CHECK (w == (int) piece.size () || (piece.empty () && w == 0), "stream write %d of %zu", w, piece.size ());
				m += piece;
			}
			RamFsStreamClose (s);
			break;
		}
		case 4: case 5: case 6:	// read a file back
		{
			std::vector<std::string> files;
			for (auto &kv : M) if (!kv.second.dir) files.push_back (kv.first);
			if (files.empty ()) break;
			check_file (files[rnd () % files.size ()]);
			break;
		}
		case 7:	// list
			check_dir (d);
			break;
		case 8:	// remove
		{
			int r = RamFsRemove (ram (p).c_str ());
			bool ok = mexists (p) && (!misdir (p) || mchildren (p).empty ());
			CHECK ((r == 0) == ok, "remove %s -> %d", p.c_str (), r);
			if (r == 0) { if (misdir (p)) drop_dir_model (p); M.erase (low (p)); }
			break;
		}
		case 9:	// rename (to a new name, maybe another folder)
		{
			std::string to = join (pick_dir (), rand_name ());
			int r = RamFsRename (ram (p).c_str (), ram (to).c_str ());
			bool into = misdir (p) && (low (to) + "/").compare (0, low (p).size () + 1, low (p) + "/") == 0;
			bool ok = mexists (p) && (!mexists (to) || low (to) == low (p)) && !into;
			CHECK ((r == 0) == ok, "rename %s -> %s: %d", p.c_str (), to.c_str (), r);
			if (r == 0 && low (to) != low (p))
			{
				std::string lp = low (p), lt = low (to);
				std::map<std::string, MNode> n2;
				for (auto &kv : M)
					if (kv.first == lp || kv.first.compare (0, lp.size () + 1, lp + "/") == 0) n2[lt + kv.first.substr (lp.size ())] = kv.second;
					else n2[kv.first] = kv.second;
				M = n2;
				for (auto &s : s_dirs) if (low (s) == lp || low (s).compare (0, lp.size () + 1, lp + "/") == 0) s = lt + low (s).substr (lp.size ());
			}
			break;
		}
		default:	// open / opendir of what is not there, or of the wrong kind
		{
			void *h = RamFsOpen (ram (p).c_str ());
			CHECK ((h != 0) == (mexists (p) && !misdir (p)), "open %s", p.c_str ());
			if (h) RamFsClose (h);
			void *dh = RamFsOpenDir (ram (p).c_str ());
			CHECK ((dh != 0) == misdir (p), "opendir %s", p.c_str ());
			if (dh) RamFsCloseDir (dh);
			CHECK (RamFsIsDirPath (ram (p).c_str ()) == misdir (p), "isdir %s", p.c_str ());
			break;
		}
		}
	}
	for (auto &kv : M) if (kv.second.dir) check_dir (kv.first); else check_file (kv.first);
	check_dir ("/");
}

static void remove_all (const std::string &d)
{
	for (auto &c : mchildren (d)) { if (M[c].dir) remove_all (c); CHECK (RamFsRemove (ram (c).c_str ()) == 0, "remove %s", c.c_str ()); M.erase (c); }
}

static u64 used (void) { u64 t, u, f; unsigned nf, nd; RamFsInfo (&t, &u, &f, &nf, &nd); return u; }

static void edges (void)
{
	u64 t, u, f; unsigned nf, nd;
	// names: case kept, found in any case; too long
	CHECK (RamFsMkdir ("RAM:/Jet") == 0 && RamFsMkdir ("RAM:/jet") != 0, "mkdir case");
	CHECK (RamFsMkdir ("RAM:/JET/cache") == 0 && RamFsIsDirPath ("RAM:/jet/CACHE"), "nested");
	CHECK (RamFsMkdir ("RAM:/nope/x") != 0, "mkdir without its parent");
	std::string longn (127, 'n'), toolong (128, 'n');
	CHECK (RamFsSave (("RAM:/jet/" + longn).c_str (), "x", 1) == 1, "127 characters");
	CHECK (RamFsSave (("RAM:/jet/" + toolong).c_str (), "x", 1) == -1, "128 characters");
	void *dh = RamFsOpenDir ("RAM:/");
	kapi_dirent e; CHECK (RamFsReadDir (dh, &e) == 1 && strcmp (e.name, "Jet") == 0 && e.is_dir == 1, "the case kept");
	RamFsCloseDir (dh);
	CHECK (RamFsRemove ("RAM:/jet") != 0, "a folder not empty");
	CHECK (RamFsRename ("RAM:/jet", "RAM:/jet/cache/in") != 0, "a folder into itself");
	CHECK (RamFsRemove ("RAM:/") != 0, "the root");
	CHECK (RamFsSave ("RAM:/jet/cache", "x", 1) == -1, "a file over a folder");

	// a file removed while open: read to its end, then freed
	std::string big = rand_data (1000000);
	CHECK (RamFsSave ("RAM:/jet/big", big.data (), (unsigned) big.size ()) == (int) big.size (), "save 1 MB");
	u64 u0 = used ();
	void *h = RamFsOpen ("RAM:/jet/big");
	CHECK (RamFsRemove ("RAM:/jet/big") == 0 && RamFsOpen ("RAM:/jet/big") == 0, "removed while open");
	std::string got (big.size (), '\0');
	CHECK (RamFsRead (h, &got[0], (unsigned) got.size ()) == (int) big.size () && got == big, "read after the remove");
	CHECK (used () == u0, "kept while open");
	RamFsClose (h);
	CHECK (used () + 15 * 65536 <= u0, "freed at the close (%llu -> %llu)", (unsigned long long) u0, (unsigned long long) used ());

	// a dead process's handles: closed (and an open removed file freed)
	RamFsSave ("RAM:/jet/p", big.data (), 300000);
	s_pid = 9; void *h9 = RamFsOpen ("RAM:/jet/p"); void *d9 = RamFsOpenDir ("RAM:/jet"); s_pid = 7;
	RamFsRemove ("RAM:/jet/p");
	u64 u1 = used ();
	RamFsOnProcessGone (9);
	CHECK (used () < u1 && !RamFsIsFile (0), "a dead process's handles closed");
	(void) h9; (void) d9;

	// a small file takes its size rounded to 256 B, not a page
	RamFsRemove (("RAM:/jet/" + longn).c_str ());
	u64 ub = used ();
	for (int i = 0; i < 64; i++) RamFsSave (("RAM:/jet/s" + std::to_string (i)).c_str (), big.data (), 1000);
	CHECK (used () - ub <= 2 * 65536, "64 files of 1000 B: %llu KB of pages", (unsigned long long) (used () - ub) / 1024);
	for (int i = 0; i < 64; i++) RamFsRemove (("RAM:/jet/s" + std::to_string (i)).c_str ());
	// a streamed file: its last chunk fitted at the close
	void *s = RamFsStreamOpen ("RAM:/jet/st", 1);
	for (int i = 0; i < 100; i++) RamFsStreamWrite (s, big.data () + i * 700, 700);
	RamFsStreamClose (s);
	CHECK (read_all ("/jet/st", 0) == big.substr (0, 70000), "the streamed file");
	CHECK (used () - ub <= 3 * 65536, "70000 B streamed: %llu KB of pages", (unsigned long long) (used () - ub) / 1024);
	std::string want = big.substr (0, 70000);
	for (int i = 0; i < 300; i++)				// (a log: opened, one line appended, closed)
	{
		s = RamFsStreamOpen ("RAM:/jet/st", 2);
		RamFsStreamWrite (s, big.data () + i * 50, 50);
		RamFsStreamClose (s);
		want += big.substr (i * 50, 50);
	}
	CHECK (read_all ("/jet/st", 1) == want, "appended");
	CHECK (used () - ub <= 6 * 65536, "%zu B appended 300 times: %llu KB of pages", want.size (), (unsigned long long) (used () - ub) / 1024);
	RamFsRemove ("RAM:/jet/st");

	// the volume full: a save that does not fit fails, no half file; what fits still does
	RamFsInfo (&t, &u, &f, &nf, &nd);
	unsigned nOK = 0; int r;
	std::string chunk = rand_data (3 << 20);
	while ((r = RamFsSave (("RAM:/jet/cache/f" + std::to_string (nOK)).c_str (), chunk.data (), (unsigned) chunk.size ())) > 0) nOK++;
	CHECK (RamFsOpen (("RAM:/jet/cache/f" + std::to_string (nOK)).c_str ()) == 0, "no half file");
	RamFsInfo (&t, &u, &f, &nf, &nd);
	CHECK (nOK > 0 && f < (3u << 20) + 65536, "full: %u files of 3 MB, %llu KB free of %llu", nOK, (unsigned long long) f / 1024, (unsigned long long) t / 1024);
	CHECK (u <= t, "used <= total");
	s = RamFsStreamOpen ("RAM:/jet/cache/s", 1);
	int w = 0, all = 0;
	while ((w = RamFsStreamWrite (s, chunk.data (), 1 << 20)) == (1 << 20)) all += w;
	RamFsStreamClose (s);
	RamFsInfo (&t, &u, &f, &nf, &nd);
	CHECK (f == 0 && all > 0, "the stream filled it: %llu free", (unsigned long long) f);
	RamFsRemove (("RAM:/jet/cache/f" + std::to_string (nOK - 1)).c_str ());
	CHECK (RamFsSave ("RAM:/jet/small", "hello", 5) == 5, "a small file fits again");
	for (unsigned i = 0; i < nOK; i++) RamFsRemove (("RAM:/jet/cache/f" + std::to_string (i)).c_str ());
	RamFsRemove ("RAM:/jet/cache/s"); RamFsRemove ("RAM:/jet/small");
	CHECK (RamFsSave ("RAM:/jet/again", chunk.data (), (unsigned) chunk.size ()) == (int) chunk.size (), "room again after the removes");
	RamFsRemove ("RAM:/jet/again");
	RamFsRemove ("RAM:/jet/cache"); RamFsRemove ("RAM:/jet");
	RamFsInfo (&t, &u, &f, &nf, &nd);
	CHECK (u == 0 && nf == 0 && nd == 0 && s_pages.empty (), "everything given back: %llu bytes, %u files, %u folders, %zu pages",
	       (unsigned long long) u, nf, nd, s_pages.size ());
}

int main (int argc, char **argv)
{
	unsigned seed = argc > 1 ? (unsigned) atoi (argv[1]) : 1;
	s_seed = seed;
	unsigned nOps = argc > 2 ? (unsigned) atoi (argv[2]) : 3000;
	RamFsInit ("512");					// 512 MB (of the 1 GB pool)
	random_ops (nOps);
	u64 t, u, f; unsigned nf, nd;
	RamFsInfo (&t, &u, &f, &nf, &nd);
	unsigned long long logical = 0; for (auto &kv : M) logical += kv.second.data.size ();
	printf ("seed %u: %zu files and folders, %llu KB in files, %llu KB of pages (%zu)\n", seed, M.size (),
		logical / 1024, (unsigned long long) u / 1024, s_pages.size ());
	CHECK (u == s_pages.size () * 65536ull, "the pages counted");
	remove_all ("/");
	RamFsInfo (&t, &u, &f, &nf, &nd);
	CHECK (u == 0 && nf == 0 && nd == 0 && s_pages.empty (), "all removed: %llu bytes, %u files, %u dirs, %zu pages", (unsigned long long) u, nf, nd, s_pages.size ());
	edges ();
	if (fails) { printf ("seed %u: %d FAILURES\n", seed, fails); return 1; }
	printf ("seed %u: ok\n", seed);
	return 0;
}
