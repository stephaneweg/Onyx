/*
 * posixtest-cxx.cpp -- /bin/posixtest-cxx: the C++ part of libonyxposix's conformance test
 * (docs/POSIX-PLAN.md §2.3 and "WP-TC resolutions"; docs/03 §5.4). Built with WP-TC's
 * aarch64-onyx-elf toolchain only (real gthreads, native TLS, newlib's errno per thread);
 * `posixtest cxx` runs it.
 *
 *   posixtest-cxx [group...]   groups: thread mutex cond tls static fs time future except errno
 *                              atomic sync (default: all)
 *   posixtest-cxx fs SD:/tmp   the filesystem group in another directory (default RAM:/ and SD:/tmp)
 *
 * Each check prints PASS, FAIL (with what was seen) or SKIP, then a summary; the exit status is
 * the number of failures (as /bin/posixtest).
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions: The above copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
 * IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cerrno>
#include <climits>
#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <latch>
#include <memory>
#include <mutex>
#include <semaphore>
#include <set>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
namespace fs = std::filesystem;
using steady = std::chrono::steady_clock;

static const char *s_group = "";
static int s_pass, s_fail, s_skip;

static void pass (const char *name)
{
	s_pass++;
	std::printf ("PASS  cxx.%s: %s\n", s_group, name);
}

static void fail (const char *name, const char *fmt, ...)
{
	char why[200];
	va_list ap;
	va_start (ap, fmt);
	std::vsnprintf (why, sizeof why, fmt, ap);
	va_end (ap);
	s_fail++;
	std::printf ("FAIL  cxx.%s: %s (%s)\n", s_group, name, why);
}

static void skip (const char *name, const char *why)
{
	s_skip++;
	std::printf ("SKIP  cxx.%s: %s (%s)\n", s_group, name, why);
}

#define CHECK(name, cond, ...) do { if (cond) pass (name); else fail (name, __VA_ARGS__); } while (0)

static long ms_since (steady::time_point t0)
{
	return (long) std::chrono::duration_cast<std::chrono::milliseconds> (steady::now () - t0).count ();
}

/* ============================================================================== thread == */
static void group_thread ()
{
	s_group = "thread";
	int v = 0;
	std::thread::id other;
	std::thread t ([&] { v = 42; other = std::this_thread::get_id (); });
	CHECK ("std::thread runs, joinable", t.joinable (), "not joinable");
	t.join ();
	CHECK ("join: the thread's work is seen", v == 42 && !t.joinable (), "v %d", v);
	CHECK ("get_id differs from main's", other != std::this_thread::get_id () && other != std::thread::id (), "same id");
	CHECK ("hardware_concurrency >= 1", std::thread::hardware_concurrency () >= 1, "%u", std::thread::hardware_concurrency ());

	std::atomic<int> done {0};
	std::thread d ([&] { std::this_thread::sleep_for (10ms); done = 1; });
	d.detach ();
	auto t0 = steady::now ();
	while (!done && ms_since (t0) < 3000)
		std::this_thread::sleep_for (1ms);
	CHECK ("detach: the thread still runs", done == 1, "never ran");

	std::vector<std::thread> ts;
	std::atomic<int> n {0};
	for (int i = 0; i < 16; i++)
		ts.emplace_back ([&, i] { n += i; std::this_thread::yield (); });
	for (auto &x : ts)
		x.join ();
	CHECK ("16 threads, yield, join", n == 120, "sum %d", n.load ());

	/* a thread given a move-only argument and a big stack frame */
	auto up = std::make_unique<int> (7);
	int got = 0;
	std::thread m ([&got] (std::unique_ptr<int> p) { volatile char big[200000]; big[0] = 1; got = *p + big[0]; }, std::move (up));
	m.join ();
	CHECK ("a move-only argument, a 200 KB frame", got == 8, "got %d", got);
}

/* =============================================================================== mutex == */
static void group_mutex ()
{
	s_group = "mutex";
	{
		std::mutex mu;
		long count = 0;
		std::vector<std::thread> ts;
		for (int i = 0; i < 8; i++)
			ts.emplace_back ([&] { for (int k = 0; k < 20000; k++) { std::lock_guard<std::mutex> g (mu); count++; } });
		for (auto &t : ts)
			t.join ();
		CHECK ("std::mutex: 8 threads x 20000 increments", count == 160000, "count %ld", count);
	}
	{
		std::recursive_mutex rm;
		bool ok = false;
		std::function<void (int)> rec = [&] (int d) { std::lock_guard<std::recursive_mutex> g (rm); if (d) rec (d - 1); else ok = true; };
		rec (5);
		CHECK ("std::recursive_mutex: locked 6 deep", ok, "");
	}
	{
		std::timed_mutex tm;
		tm.lock ();
		bool got = true;
		long ms = 0;
		std::thread t ([&] { auto t0 = steady::now (); got = tm.try_lock_for (100ms); ms = ms_since (t0); });
		t.join ();
		tm.unlock ();
		CHECK ("std::timed_mutex: try_lock_for (100 ms) times out", !got && ms >= 90 && ms < 3000, "got %d after %ld ms", got, ms);
		bool got2 = false;
		std::thread t2 ([&] { got2 = tm.try_lock_until (steady::now () + 100ms); if (got2) tm.unlock (); });
		t2.join ();
		CHECK ("std::timed_mutex: try_lock_until when free", got2, "not taken");
	}
	{
		std::shared_mutex sm;
		std::atomic<int> inside {0}, peak {0};
		std::vector<std::thread> ts;
		for (int i = 0; i < 4; i++)
			ts.emplace_back ([&] {
				std::shared_lock<std::shared_mutex> g (sm);
				int v = ++inside;
				int p = peak.load ();
				while (v > p && !peak.compare_exchange_weak (p, v)) {}
				std::this_thread::sleep_for (30ms);
				--inside;
			});
		for (auto &t : ts)
			t.join ();
		std::unique_lock<std::shared_mutex> w (sm);
		CHECK ("std::shared_mutex: readers together, then a writer", peak > 1 && inside == 0, "peak %d", peak.load ());
	}
	{
		std::mutex a, b;
		std::thread t ([&] { for (int i = 0; i < 2000; i++) { std::scoped_lock g (b, a); } });
		for (int i = 0; i < 2000; i++) { std::scoped_lock g (a, b); }
		t.join ();
		pass ("std::scoped_lock of two mutexes in opposite orders: no deadlock");
	}
}

/* ================================================================================ cond == */
static void group_cond ()
{
	s_group = "cond";
	{
		std::mutex mu;
		std::condition_variable cv;
		std::vector<int> q;
		bool end = false;
		long sum = 0;
		std::thread cons ([&] {
			std::unique_lock<std::mutex> g (mu);
			for (;;)
			{
				cv.wait (g, [&] { return !q.empty () || end; });
				while (!q.empty ()) { sum += q.back (); q.pop_back (); }
				if (end)
					break;
			}
		});
		for (int i = 1; i <= 1000; i++)
		{
			{ std::lock_guard<std::mutex> g (mu); q.push_back (i); }
			cv.notify_one ();
		}
		{ std::lock_guard<std::mutex> g (mu); end = true; }
		cv.notify_one ();
		cons.join ();
		CHECK ("producer / consumer: 1000 items", sum == 500500, "sum %ld", sum);
	}
	{
		std::mutex mu;
		std::condition_variable cv;
		std::unique_lock<std::mutex> g (mu);
		auto t0 = steady::now ();
		auto st = cv.wait_for (g, 150ms);
		long ms = ms_since (t0);
		CHECK ("wait_for (150 ms): timeout", st == std::cv_status::timeout && ms >= 140 && ms < 3000, "%ld ms", ms);
		t0 = steady::now ();
		bool r = cv.wait_until (g, steady::now () + 100ms, [] { return false; });
		ms = ms_since (t0);
		CHECK ("wait_until (steady_clock + 100 ms) with a predicate: false at the deadline", !r && ms >= 90 && ms < 3000, "%ld ms", ms);
		t0 = steady::now ();
		st = cv.wait_until (g, std::chrono::system_clock::now () + 100ms);
		ms = ms_since (t0);
		CHECK ("wait_until (system_clock + 100 ms): timeout", st == std::cv_status::timeout && ms >= 80 && ms < 3000, "%ld ms", ms);
	}
	{
		std::mutex mu;
		std::condition_variable cv;
		bool go = false;
		std::atomic<int> woke {0};
		std::vector<std::thread> ts;
		for (int i = 0; i < 6; i++)
			ts.emplace_back ([&] { std::unique_lock<std::mutex> g (mu); if (cv.wait_for (g, 5s, [&] { return go; })) woke++; });
		std::this_thread::sleep_for (50ms);
		{ std::lock_guard<std::mutex> g (mu); go = true; }
		auto t0 = steady::now ();
		cv.notify_all ();
		for (auto &t : ts)
			t.join ();
		CHECK ("notify_all wakes 6 waiters (before their timeout)", woke == 6 && ms_since (t0) < 4000, "%d woke", woke.load ());
	}
	{
		std::mutex mu;
		std::condition_variable_any cv;
		std::unique_lock<std::mutex> g (mu);
		auto t0 = steady::now ();
		auto st = cv.wait_for (g, 100ms);
		long ms = ms_since (t0);
		CHECK ("condition_variable_any::wait_for (100 ms): timeout", st == std::cv_status::timeout && ms >= 90 && ms < 3000, "%ld ms", ms);
	}
}

/* ================================================================================= tls == */
static std::atomic<int> s_tlsCtor {0}, s_tlsDtor {0};
struct TlsObj
{
	int v = 5;
	std::string s = "initial";
	TlsObj () { s_tlsCtor++; }
	~TlsObj () { s_tlsDtor++; }
};
static thread_local TlsObj t_obj;
static thread_local int t_int = 11;
static __thread long t_raw;

static void group_tls ()
{
	s_group = "tls";
	t_int = 99;
	t_raw = 1234;
	int seen = -1;
	long seenRaw = -1;
	const void *a1 = &t_int, *a2 = 0;
	std::thread t ([&] { seen = t_int; seenRaw = t_raw; a2 = &t_int; t_int = 3; });
	t.join ();
	CHECK ("thread_local: a fresh copy per thread (initialiser 11)", seen == 11 && t_int == 99, "thread saw %d, main has %d", seen, t_int);
	CHECK ("__thread: zero in a new thread, main's kept", seenRaw == 0 && t_raw == 1234, "thread saw %ld, main %ld", seenRaw, t_raw);
	CHECK ("thread_local: a different address per thread", a1 != a2 && a2 != 0, "%p %p", a1, a2);

	unsigned long tp;
	__asm__ volatile ("mrs %0, tpidr_el0" : "=r" (tp));
	unsigned long ta = (unsigned long) &t_int;
	CHECK ("native TLS: the variable sits just above TPIDR_EL0 (variant 1)", ta >= tp + 16 && ta < tp + 65536, "tp %lx var %lx", tp, ta);

	int c0 = s_tlsCtor, d0 = s_tlsDtor;
	std::string got;
	std::thread u ([&] { t_obj.v = 6; got = t_obj.s; t_obj.s = "changed in the thread"; });
	u.join ();
	CHECK ("thread_local object: constructed in the thread", s_tlsCtor == c0 + 1 && got == "initial", "ctor %d -> %d, s '%s'", c0, s_tlsCtor.load (), got.c_str ());
	CHECK ("thread_local object: destroyed when the thread ends", s_tlsDtor == d0 + 1, "dtor %d -> %d", d0, s_tlsDtor.load ());

	std::vector<std::thread> ts;
	for (int i = 0; i < 8; i++)
		ts.emplace_back ([] { t_obj.v++; });
	for (auto &x : ts)
		x.join ();
	CHECK ("thread_local object: 8 threads, 8 constructions and destructions", s_tlsCtor == c0 + 9 && s_tlsDtor == d0 + 9, "ctor %d dtor %d", s_tlsCtor.load () - c0, s_tlsDtor.load () - d0);
}

/* ============================================================================== static == */
static std::atomic<int> s_staticBuilt {0};
struct Slow
{
	int v;
	Slow () { s_staticBuilt++; std::this_thread::sleep_for (60ms); v = 77; }
};
static int use_static ()
{
	static Slow s;			/* the guard: __cxa_guard_acquire */
	return s.v;
}

static void group_static ()
{
	s_group = "static";
	std::atomic<int> bad {0};
	std::vector<std::thread> ts;
	for (int i = 0; i < 8; i++)
		ts.emplace_back ([&] { if (use_static () != 77) bad++; });
	for (auto &t : ts)
		t.join ();
	CHECK ("a function-local static: built once, 8 threads all see it built", s_staticBuilt == 1 && bad == 0, "built %d, %d saw it unfinished", s_staticBuilt.load (), bad.load ());

	std::once_flag once;
	std::atomic<int> calls {0};
	ts.clear ();
	for (int i = 0; i < 8; i++)
		ts.emplace_back ([&] { std::call_once (once, [&] { std::this_thread::sleep_for (20ms); calls++; }); });
	for (auto &t : ts)
		t.join ();
	CHECK ("std::call_once: 8 threads, one call", calls == 1, "%d calls", calls.load ());
}

/* ================================================================================== fs == */
static void fs_in (const char *where)
{
	std::error_code ec;
	fs::path base = fs::path (where) / "pxcxx";
	fs::remove_all (base, ec);
	bool made = fs::create_directories (base / "a" / "b", ec);
	std::string what = std::string (where) + ": ";
	CHECK ((what + "create_directories a/b").c_str (), made && !ec && fs::is_directory (base / "a" / "b"), "%s", ec.message ().c_str ());
	if (!made)
		return;
	{
		std::ofstream f (base / "a" / "f.txt");
		f << "hello, filesystem\n";
	}
	CHECK ((what + "ofstream, exists, is_regular_file, file_size").c_str (),
	       fs::exists (base / "a" / "f.txt") && fs::is_regular_file (base / "a" / "f.txt") && fs::file_size (base / "a" / "f.txt", ec) == 18,
	       "size %ju %s", (uintmax_t) fs::file_size (base / "a" / "f.txt", ec), ec.message ().c_str ());
	std::set<std::string> names;
	for (auto &e : fs::directory_iterator (base / "a", ec))
		names.insert (e.path ().filename ().string () + (e.is_directory () ? "/" : ""));
	CHECK ((what + "directory_iterator: b/ and f.txt").c_str (), !ec && names == std::set<std::string> ({ "b/", "f.txt" }), "%zu entries %s", names.size (), ec.message ().c_str ());
	int rn = 0;
	for (auto it = fs::recursive_directory_iterator (base, ec); !ec && it != fs::recursive_directory_iterator (); it.increment (ec))
		rn++;
	CHECK ((what + "recursive_directory_iterator: 3 entries").c_str (), !ec && rn == 3, "%d %s", rn, ec.message ().c_str ());
	fs::copy_file (base / "a" / "f.txt", base / "a" / "g.txt", ec);
	CHECK ((what + "copy_file").c_str (), !ec && fs::file_size (base / "a" / "g.txt", ec) == 18, "%s", ec.message ().c_str ());
	fs::rename (base / "a" / "g.txt", base / "a" / "b" / "h.txt", ec);
	CHECK ((what + "rename into a subdirectory").c_str (), !ec && fs::exists (base / "a" / "b" / "h.txt") && !fs::exists (base / "a" / "g.txt"), "%s", ec.message ().c_str ());
	fs::resize_file (base / "a" / "b" / "h.txt", 4, ec);
	CHECK ((what + "resize_file (truncate) to 4").c_str (), !ec && fs::file_size (base / "a" / "b" / "h.txt", ec) == 4, "%s", ec.message ().c_str ());
	auto lw = fs::last_write_time (base / "a" / "f.txt", ec);
	CHECK ((what + "last_write_time").c_str (), !ec && lw != fs::file_time_type::min (), "%s", ec.message ().c_str ());
	CHECK ((what + "remove a file").c_str (), fs::remove (base / "a" / "f.txt", ec) && !fs::exists (base / "a" / "f.txt"), "%s", ec.message ().c_str ());
	auto n = fs::remove_all (base, ec);
	CHECK ((what + "remove_all: 4 entries").c_str (), !ec && n == 4 && !fs::exists (base), "%ju %s", (uintmax_t) n, ec.message ().c_str ());
}

static void group_fs (const char *dir)
{
	s_group = "fs";
	if (dir)
		fs_in (dir);
	else
	{
		fs_in ("RAM:");
		fs_in ("SD:/tmp");
	}
	std::error_code ec;
	fs::path cwd = fs::current_path (ec);
	CHECK ("current_path", !ec && !cwd.empty (), "%s", ec.message ().c_str ());
	fs::path tmp = fs::temp_directory_path (ec);
	CHECK ("temp_directory_path", !ec && !tmp.empty (), "%s", ec.message ().c_str ());
	CHECK ("a missing file: exists false, status not_found", !fs::exists ("RAM:/no/such/file", ec) && fs::status ("RAM:/no/such/file", ec).type () == fs::file_type::not_found, "");
	bool threw = false;
	try { (void) fs::file_size ("RAM:/no/such/file"); }
	catch (const fs::filesystem_error &e) { threw = e.code () == std::errc::no_such_file_or_directory; }
	CHECK ("filesystem_error (ENOENT) thrown", threw, "");
}

/* ================================================================================ time == */
static void group_time ()
{
	s_group = "time";
	CHECK ("steady_clock::is_steady", steady::is_steady, "");
	auto a = steady::now ();
	auto b = steady::now ();
	CHECK ("steady_clock: monotonic", b >= a, "");
	auto t0 = steady::now ();
	std::this_thread::sleep_for (100ms);
	long ms = ms_since (t0);
	CHECK ("sleep_for (100 ms) measured by steady_clock", ms >= 99 && ms < 2000, "%ld ms", ms);
	t0 = steady::now ();
	std::this_thread::sleep_until (steady::now () + 50ms);
	ms = ms_since (t0);
	CHECK ("sleep_until (steady + 50 ms)", ms >= 49 && ms < 2000, "%ld ms", ms);
	auto s1 = std::chrono::system_clock::now ();
	std::this_thread::sleep_for (20ms);
	auto s2 = std::chrono::system_clock::now ();
	CHECK ("system_clock advances", s2 > s1, "");
	auto h = std::chrono::high_resolution_clock::now ().time_since_epoch ().count ();
	CHECK ("high_resolution_clock", h != 0, "");
	/* resolution: two reads within a busy loop differ by less than 1 ms */
	auto r0 = steady::now (), r1 = r0;
	while (r1 == r0)
		r1 = steady::now ();
	CHECK ("steady_clock resolution < 1 ms", r1 - r0 < 1ms, "%lld ns", (long long) std::chrono::duration_cast<std::chrono::nanoseconds> (r1 - r0).count ());
}

/* ============================================================================== future == */
static void group_future ()
{
	s_group = "future";
	auto f = std::async (std::launch::async, [] { std::this_thread::sleep_for (20ms); return 6 * 7; });
	CHECK ("std::async (launch::async): the value", f.get () == 42, "");
	auto d = std::async (std::launch::deferred, [] { return std::this_thread::get_id (); });
	CHECK ("std::async (launch::deferred): runs in the caller", d.get () == std::this_thread::get_id (), "");
	auto e = std::async (std::launch::async, []() -> int { throw std::runtime_error ("from the task"); });
	std::string msg;
	try { e.get (); } catch (const std::runtime_error &x) { msg = x.what (); }
	CHECK ("std::async: an exception rethrown by get ()", msg == "from the task", "'%s'", msg.c_str ());

	std::promise<std::string> p;
	std::future<std::string> pf = p.get_future ();
	std::thread t ([&] { std::this_thread::sleep_for (30ms); p.set_value ("promised"); });
	auto st = pf.wait_for (1ms);
	std::string v = pf.get ();
	t.join ();
	CHECK ("promise / future across threads; wait_for before: timeout", v == "promised" && st == std::future_status::timeout, "'%s' %d", v.c_str (), (int) st);

	std::packaged_task<int (int)> task ([] (int x) { return x + 1; });
	auto tf = task.get_future ();
	std::thread tt (std::move (task), 9);
	tt.join ();
	CHECK ("packaged_task in a thread", tf.get () == 10, "");

	std::promise<void> go;
	std::shared_future<void> sf = go.get_future ().share ();
	std::atomic<int> n {0};
	std::vector<std::thread> ts;
	for (int i = 0; i < 4; i++)
		ts.emplace_back ([&, sf] { sf.wait (); n++; });
	go.set_value ();
	for (auto &x : ts)
		x.join ();
	CHECK ("shared_future: 4 waiters released", n == 4, "%d", n.load ());

	std::promise<int> broken;
	auto bf = broken.get_future ();
	{ std::promise<int> gone = std::move (broken); }
	bool isBroken = false;
	try { bf.get (); } catch (const std::future_error &x) { isBroken = x.code () == std::future_errc::broken_promise; }
	CHECK ("broken_promise", isBroken, "");
}

/* ============================================================================== except == */
static void group_except ()
{
	s_group = "except";
	std::exception_ptr ep;
	std::thread t ([&] { try { throw std::logic_error ("across"); } catch (...) { ep = std::current_exception (); } });
	t.join ();
	std::string w;
	try { if (ep) std::rethrow_exception (ep); } catch (const std::logic_error &x) { w = x.what (); }
	CHECK ("exception_ptr from a thread, rethrown in main", w == "across", "'%s'", w.c_str ());

	std::atomic<int> caught {0};
	std::vector<std::thread> ts;
	for (int i = 0; i < 8; i++)
		ts.emplace_back ([&, i] {
			for (int k = 0; k < 500; k++)
			{
				try { if (k >= 0) throw i * 1000 + k; }
				catch (int v) { if (v == i * 1000 + k) caught++; }
			}
		});
	for (auto &x : ts)
		x.join ();
	CHECK ("8 threads x 500 throw / catch at once (the unwinder, the exception globals)", caught == 4000, "%d", caught.load ());

	int unc = -1;
	std::thread u ([&] {
		struct D { int *p; ~D () { *p = std::uncaught_exceptions (); } };
		try { D d { &unc }; throw 1; } catch (int) {}
	});
	u.join ();
	CHECK ("uncaught_exceptions per thread", unc == 1 && std::uncaught_exceptions () == 0, "%d", unc);

	bool nested = false;
	try
	{
		try { throw std::runtime_error ("inner"); }
		catch (...) { std::throw_with_nested (std::logic_error ("outer")); }
	}
	catch (const std::logic_error &x)
	{
		try { std::rethrow_if_nested (x); } catch (const std::runtime_error &y) { nested = !std::strcmp (y.what (), "inner"); }
	}
	CHECK ("nested exceptions", nested, "");
}

/* =============================================================================== errno == */
static void group_errno ()
{
	s_group = "errno";
	errno = 0;
	int theirs = 0, theirs2 = 0;
	std::thread t ([&] {
		errno = 0;
		long v = std::strtol ("99999999999999999999999", nullptr, 10);	/* newlib sets ERANGE through its _reent */
		theirs = (v == LONG_MAX) ? errno : -1;
		errno = 0;
		std::FILE *f = std::fopen ("RAM:/no/such/dir/x", "r");
		theirs2 = f ? -1 : errno;
	});
	t.join ();
	CHECK ("strtol's ERANGE in a thread (newlib's _reent errno), main's errno untouched", theirs == ERANGE && errno == 0, "thread %d, main %d", theirs, errno);
	CHECK ("fopen's ENOENT in a thread", theirs2 == ENOENT || theirs2 == ENOTDIR, "%d", theirs2);
	std::atomic<int> bad {0};
	std::vector<std::thread> ts;
	for (int i = 0; i < 8; i++)
		ts.emplace_back ([&, i] { for (int k = 0; k < 2000; k++) { errno = i + 1; std::this_thread::yield (); if (errno != i + 1) bad++; } });
	for (auto &x : ts)
		x.join ();
	CHECK ("errno: 8 threads, each keeps its own", bad == 0, "%d mismatches", bad.load ());
	/* stdio from several threads (newlib's locks, its thread-local _reent) */
	char buf[8][32];
	ts.clear ();
	for (int i = 0; i < 8; i++)
		ts.emplace_back ([&, i] { for (int k = 0; k < 200; k++) std::snprintf (buf[i], sizeof buf[i], "t%d-%d-%.1f", i, k, k * 0.5); });
	for (auto &x : ts)
		x.join ();
	CHECK ("snprintf (with floats) from 8 threads", !std::strcmp (buf[3], "t3-199-99.5") && !std::strcmp (buf[7], "t7-199-99.5"), "'%s' '%s'", buf[3], buf[7]);
}

/* ============================================================================== atomic == */
static void group_atomic ()
{
	s_group = "atomic";
	std::atomic<long long> c {0};
	std::vector<std::thread> ts;
	for (int i = 0; i < 8; i++)
		ts.emplace_back ([&] { for (int k = 0; k < 50000; k++) c.fetch_add (1, std::memory_order_relaxed); });
	for (auto &x : ts)
		x.join ();
	CHECK ("atomic<long long>::fetch_add: 8 x 50000", c == 400000, "%lld", c.load ());

	auto sp = std::make_shared<std::string> ("shared");
	ts.clear ();
	for (int i = 0; i < 8; i++)
		ts.emplace_back ([sp] { for (int k = 0; k < 20000; k++) { auto copy = sp; (void) copy; } });
	for (auto &x : ts)
		x.join ();
	CHECK ("shared_ptr copied by 8 threads: use_count back to 1 (libstdc++ knows it is threaded)", sp.use_count () == 1, "%ld", sp.use_count ());

	std::atomic<int> flag {0};
	std::thread w ([&] { std::this_thread::sleep_for (30ms); flag.store (1); flag.notify_one (); });
	flag.wait (0);
	w.join ();
	CHECK ("atomic::wait / notify_one", flag == 1, "");
}

/* ================================================================================ sync == */
static void group_sync ()
{
	s_group = "sync";
	std::latch l (4);
	std::atomic<int> n {0};
	std::vector<std::thread> ts;
	for (int i = 0; i < 4; i++)
		ts.emplace_back ([&] { n++; l.count_down (); });
	l.wait ();
	CHECK ("std::latch", n == 4, "%d", n.load ());
	for (auto &x : ts)
		x.join ();

	std::atomic<int> phases {0};
	std::barrier bar (3, [&] () noexcept { phases++; });
	ts.clear ();
	for (int i = 0; i < 3; i++)
		ts.emplace_back ([&] { for (int k = 0; k < 5; k++) bar.arrive_and_wait (); });
	for (auto &x : ts)
		x.join ();
	CHECK ("std::barrier: 3 threads, 5 phases", phases == 5, "%d", phases.load ());

	std::counting_semaphore<4> sem (0);
	auto t0 = steady::now ();
	bool got = sem.try_acquire_for (80ms);
	long ms = ms_since (t0);
	CHECK ("counting_semaphore::try_acquire_for (80 ms): timeout", !got && ms >= 70 && ms < 3000, "%d after %ld ms", got, ms);
	std::thread rel ([&] { std::this_thread::sleep_for (20ms); sem.release (2); });
	sem.acquire ();
	got = sem.try_acquire ();
	rel.join ();
	CHECK ("counting_semaphore: release (2) from a thread", got, "");

	std::atomic<bool> stopped {false};
	{
		std::jthread j ([&] (std::stop_token st) { while (!st.stop_requested ()) std::this_thread::sleep_for (2ms); stopped = true; });
		std::this_thread::sleep_for (20ms);
	}
	CHECK ("std::jthread: stop requested and joined by its destructor", stopped, "");
}

int main (int argc, char **argv)
{
	std::setvbuf (stdout, nullptr, _IOLBF, 0);
	std::printf ("posixtest-cxx: C++ on libonyxposix (aarch64-onyx-elf, GCC %d.%d, libstdc++ %d)\n",
		     __GNUC__, __GNUC_MINOR__, (int) __GLIBCXX__);
	static const char *all[] = { "thread", "mutex", "cond", "tls", "static", "fs", "time", "future", "except", "errno", "atomic", "sync", nullptr };
	const char *const *groups = all;
	const char *sel[16];
	const char *dir = nullptr;
	if (argc > 1)
	{
		int n = 0;
		for (int i = 1; i < argc && n < 15; i++)
		{
			if (std::strchr (argv[i], ':') || argv[i][0] == '/')
				dir = argv[i];
			else
				sel[n++] = argv[i];
		}
		sel[n] = nullptr;
		if (n)
			groups = sel;
	}
	for (int i = 0; groups[i]; i++)
	{
		std::string g = groups[i];
		try
		{
			if (g == "thread") group_thread ();
			else if (g == "mutex") group_mutex ();
			else if (g == "cond") group_cond ();
			else if (g == "tls") group_tls ();
			else if (g == "static") group_static ();
			else if (g == "fs") group_fs (dir);
			else if (g == "time") group_time ();
			else if (g == "future") group_future ();
			else if (g == "except") group_except ();
			else if (g == "errno") group_errno ();
			else if (g == "atomic") group_atomic ();
			else if (g == "sync") group_sync ();
			else std::printf ("posixtest-cxx: no group '%s'\n", g.c_str ());
		}
		catch (const std::exception &x)
		{
			fail ("an unexpected exception", "%s", x.what ());
		}
	}
	(void) skip;
	std::printf ("posixtest-cxx: %d passed, %d failed, %d skipped\n", s_pass, s_fail, s_skip);
	return s_fail;
}
