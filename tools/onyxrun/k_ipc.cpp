//
// k_ipc.cpp -- the processes talking to each other (onyxrun.h), as kernel/sys/ipc.cpp and kapi.cpp do it: named
// services (ipc_register / ipc_lookup), each process's mailbox (32 messages of 512 bytes at most; mailbox_recv may
// sleep until one comes), the system's clipboard (one typed blob of 64 KB at most, a serial bumped by every set).
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include <chrono>
#include <string.h>
#include <strings.h>

#define MAILBOX_SLOTS	32
#define MAILBOX_MSG_MAX	512
#define IPC_NAME_MAX	32

struct Msg { int from, type; std::vector<u8> data; };

static std::mutex s_M;
static std::condition_variable s_CV;			// a message came
static std::map<std::string, int> s_Services;		// name (lower case) -> pid
static std::map<int, std::deque<Msg>> s_Box;		// pid -> its mailbox

static bool alive (int pid) { Proc *P = proc_find (pid); return P && !P->ended; }

static std::string key_of (const std::string &n)
{
	std::string k = n.substr (0, IPC_NAME_MAX - 1);
	for (auto &c : k) c = (char) tolower ((unsigned char) c);
	return k;
}

static int k_ipc_register (u64 name)
{
	std::string n;
	if (!gstr (name, n, 256) || n.empty ()) return 0;
	int me = cur ()->pid;
	std::lock_guard<std::mutex> L (s_M);
	auto it = s_Services.find (key_of (n));
	if (it != s_Services.end () && it->second != me && alive (it->second)) return 0;
	s_Services[key_of (n)] = me;
	return 1;
}

static int k_ipc_lookup (u64 name)
{
	std::string n;
	if (!gstr (name, n, 256)) return 0;
	std::lock_guard<std::mutex> L (s_M);
	auto it = s_Services.find (key_of (n));
	if (it == s_Services.end ()) return 0;
	if (!alive (it->second)) { s_Services.erase (it); return 0; }
	return it->second;
}

static int k_mailbox_send (int to, int type, u64 in, unsigned len)
{
	if (len > MAILBOX_MSG_MAX) return 0;
	u8 *h = len ? GB (in, len, MEM_R) : 0;
	if (len && in && !h) return 0;
	if (!alive (to)) return 0;
	std::lock_guard<std::mutex> L (s_M);
	auto &box = s_Box[to];
	if (box.size () >= MAILBOX_SLOTS - 1) return 0;
	Msg m;
	m.from = cur ()->pid; m.type = type;
	if (h) m.data.assign (h, h + len);
	box.push_back (m);
	s_CV.notify_all ();
	return 1;
}

static int k_mailbox_recv (u64 from, u64 type, u64 buf, unsigned cap, int blocking)
{
	int me = cur ()->pid;
	unsigned most = cap < MAILBOX_MSG_MAX ? cap : MAILBOX_MSG_MAX;
	if ((from && !G<int> (from, MEM_W)) || (type && !G<int> (type, MEM_W)) || (buf && most && !GB (buf, most, MEM_W))) return -1;
	std::unique_lock<std::mutex> L (s_M);
	for (;;)
	{
		auto &box = s_Box[me];
		if (!box.empty ())
		{
			Msg m = box.front ();
			box.pop_front ();
			L.unlock ();
			if (from) gput<int> (from, m.from);
			if (type) gput<int> (type, m.type);
			unsigned n = (unsigned) m.data.size () < cap ? (unsigned) m.data.size () : cap;
			if (buf && n) memcpy (GB (buf, n, MEM_W), m.data.data (), n);
			return (int) n;
		}
		if (!blocking) return -1;
		s_CV.wait_for (L, std::chrono::milliseconds (50));
		L.unlock (); check_dying (); L.lock ();
	}
}

// (proc.cpp) a process gone: its mailbox and its services
void ipc_proc_gone (Proc *P)
{
	std::lock_guard<std::mutex> L (s_M);
	s_Box.erase (P->pid);
	for (auto it = s_Services.begin (); it != s_Services.end (); )
		if (it->second == P->pid) it = s_Services.erase (it); else ++it;
}

// ---- the clipboard -------------------------------------------------------------------------------------------------
static std::mutex s_ClipM;
static std::vector<u8> s_Clip;
static int s_ClipType = 0;
static unsigned s_ClipSerial = 0;

static int k_clipboard_set (int type, u64 data, unsigned len)
{
	if (len > 64 * 1024) len = 64 * 1024;
	if (!data) len = 0;
	u8 *h = len ? GB (data, len, MEM_R) : 0;
	if (len && !h) return 0;
	std::lock_guard<std::mutex> L (s_ClipM);
	s_Clip.assign (h, h + len);
	s_ClipType = len ? type : 0;
	s_ClipSerial++;
	return 1;
}

static int k_clipboard_get (u64 type, u64 buf, unsigned cap, u64 serial)
{
	std::lock_guard<std::mutex> L (s_ClipM);
	unsigned n = (unsigned) s_Clip.size () < cap ? (unsigned) s_Clip.size () : cap;
	if ((type && !G<int> (type, MEM_W)) || (serial && !G<unsigned> (serial, MEM_W)) || (buf && n && !GB (buf, n, MEM_W))) return 0;
	if (type) gput<int> (type, s_ClipType);
	if (serial) gput<unsigned> (serial, s_ClipSerial);
	if (buf && n) memcpy (GB (buf, n, MEM_W), s_Clip.data (), n);
	return (int) s_Clip.size ();
}

KAPI (ipc_register, k_ipc_register);
KAPI (ipc_lookup, k_ipc_lookup);
KAPI (mailbox_send, k_mailbox_send);
KAPI (mailbox_recv, k_mailbox_recv);
KAPI (clipboard_set, k_clipboard_set);
KAPI (clipboard_get, k_clipboard_get);
