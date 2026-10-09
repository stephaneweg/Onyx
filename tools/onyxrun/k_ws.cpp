//
// k_ws.cpp -- what the kernel gives the graphics server, Elegant (kern/wsrv.h, kernel/sys/wsrv.cpp; kapi_ws_ctl), in
// the runner: the role, the display (the server's rectangles copied into the PC's screen: display.cpp), the raw
// input ring (the PC's mouse and keyboard), one wait, the attached programs and their event queues, the windows'
// buffers (host memory mapped into the program at its slot's fixed place and into the server at USER_WS_BASE + n *
// USER_WS_SLOT), the programs' requests (WS_CALL: the caller waits until the server answers). Also the screen's
// calls a program makes (screen_size, screen_grab, get_modifiers, key_held).
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include "display.h"
#include <chrono>
#include <string.h>

typedef std::chrono::steady_clock Clock;

// The structures with a `long` (8 bytes on Onyx, 4 on a Windows host): their guest layout.
struct GWsCall { int op; unsigned in_len; u64 in, out; unsigned out_cap, out_len; long long a[4]; };
struct GWsReply { unsigned id, len; long long status; u64 data; };
struct GWsReqHead { unsigned id, pid; int op; unsigned in_len; long long a[4]; };
static_assert (sizeof (GWsCall) == 64 && sizeof (GWsReply) == 24 && sizeof (GWsReqHead) == 48, "ws structures (kern/kapi_abi.h)");
#ifndef _WIN32
static_assert (offsetof (struct kapi_ws_req, data) == sizeof (GWsReqHead), "kapi_ws_req's data");
#endif

#define WS_RING		256
#define WS_REQS		16

struct WsBuf { u8 *host = 0; u64 len = 0; int pid = 0, slot = 0; bool prog = false, srv = false; };
struct WsReq
{
	int state = 0;			// 0 free, 1 pending, 2 taken, 3 done
	unsigned id = 0, pid = 0;
	int op = 0;
	long long a[4] = {};
	std::vector<u8> in, out;
	long long status = 0;
};

static std::mutex s_M;
static std::condition_variable s_CV;		// the ring, a request, an answer
static int s_ServerPid = 0;
static bool s_Owned = false;
static std::deque<struct kapi_ws_input> s_Ring;
static std::map<int, std::vector<u8>> s_Clients;	// attached pids -> their kept state (empty: never set)
static WsBuf s_Buf[USER_WS_SLOTS];
static WsReq s_Req[WS_REQS];
static unsigned s_NextReq = 1;
static int s_FocusPid = 0;
static unsigned s_Mods = 0;
static std::map<int, bool> s_Held;		// logical key -> held

static int me (void) { Proc *P = cur (); return P ? P->pid : 0; }

static void push (const struct kapi_ws_input &e)		// (s_M held)
{
	if (s_Ring.size () >= WS_RING) s_Ring.pop_front ();
	s_Ring.push_back (e);
	s_CV.notify_all ();
}

// ---- the input (display.cpp) ------------------------------------------------------------------------------------
void ws_input_pointer (int x, int y, unsigned buttons, int wheel)
{
	std::lock_guard<std::mutex> L (s_M);
	if (!s_Owned) return;
	struct kapi_ws_input e; memset (&e, 0, sizeof e);
	e.type = KAPI_WS_IN_POINTER; e.x = x; e.y = y; e.buttons = buttons; e.a = wheel;
	push (e);
}

void ws_input_key (const char *keys)
{
	std::lock_guard<std::mutex> L (s_M);
	if (!s_Owned) return;
	size_t n = strlen (keys);
	for (size_t i = 0; i < n; )			// (keys[44]: a long string in pieces, never cutting a UTF-8 character)
	{
		size_t k = n - i < 40 ? n - i : 40;
		while (k > 0 && i + k < n && ((unsigned char) keys[i + k] & 0xC0) == 0x80) k--;
		struct kapi_ws_input e; memset (&e, 0, sizeof e);
		e.type = KAPI_WS_IN_KEY;
		memcpy (e.keys, keys + i, k);
		push (e);
		i += k;
	}
}

void ws_input_mods (unsigned mods)
{
	std::lock_guard<std::mutex> L (s_M);
	s_Mods = mods;
	if (!s_Owned) return;
	struct kapi_ws_input e; memset (&e, 0, sizeof e);
	e.type = KAPI_WS_IN_MODS; e.a = (int) mods;
	push (e);
}

void ws_input_held (int key, bool down)
{
	std::lock_guard<std::mutex> L (s_M);
	s_Held[key] = down;
	if (!s_Owned) return;
	struct kapi_ws_input e; memset (&e, 0, sizeof e);
	e.type = KAPI_WS_IN_HELD; e.a = key; e.buttons = down ? 1 : 0;
	push (e);
}

// The server's pid when it owns the display (main.cpp waits for it), else 0.
int ws_active (void) { std::lock_guard<std::mutex> L (s_M); return s_Owned ? s_ServerPid : 0; }

// ---- the buffers -------------------------------------------------------------------------------------------------
static u64 slot_va (int slot)
{
	static const u64 base[KAPI_WS_SLOT_MORE] = { KAPI_WS_VA_CANVAS, KAPI_WS_VA_FRAME, KAPI_WS_VA_FRAME_OFF, KAPI_WS_VA_WALLPAPER, KAPI_WS_VA_XFER };
	if (slot < KAPI_WS_SLOT_MORE) return base[slot];
	int w = (slot - KAPI_WS_SLOT_MORE) / 3 + 1, p = (slot - KAPI_WS_SLOT_MORE) % 3;
	return KAPI_WS_VA_WIN (w, p);
}

static void unmap_in (int pid, u64 va, u64 len)
{
	Proc *P = proc_find (pid);
	if (!P || P->ended) return;
	PLock L (P);
	P->mem.unmap (va, len);
}

static bool map_in (int pid, u64 va, u64 len, u8 *host, const char *what)
{
	Proc *P = proc_find (pid);
	if (!P || P->ended) return false;
	PLock L (P);
	if (P->mem.next (va) && P->mem.next (va)->va < va + len) P->mem.unmap (va, len);
	return P->mem.map_shared (va, len, MEM_R | MEM_W, what, host, KAPI_VMK_FIXED);
}

static void buf_free_if_unused (WsBuf &b)		// (s_M held)
{
	if (!b.host || b.prog || b.srv) return;
	host_shared_free (b.host, b.len);
	b = WsBuf ();
}

static long buf_map (u64 ub)
{
	struct kapi_ws_buf *gb = G<struct kapi_ws_buf> (ub, MEM_R | MEM_W);
	if (!gb) return -KAPI_EFAULT;
	struct kapi_ws_buf B = *gb;
	if (B.slot < 0 || B.slot >= KAPI_WS_SLOTS || B.bytes == 0 || B.bytes > USER_WS_SLOT) return -KAPI_EINVAL;
	std::unique_lock<std::mutex> L (s_M);
	if (!s_Clients.count ((int) B.pid)) return -KAPI_ESRCH;
	u64 len = ALIGN_UP (B.bytes);
	if (B.flags & KAPI_WS_BUF_ADOPT)
		for (int i = 0; i < USER_WS_SLOTS; i++)
		{
			WsBuf &o = s_Buf[i];
			if (!o.host || !o.prog || o.srv || o.pid != (int) B.pid || o.slot != B.slot || o.len != len) continue;
			u64 va = USER_WS_BASE + (u64) i * USER_WS_SLOT;
			L.unlock ();
			map_in (s_ServerPid, va, len, o.host, "a window's buffer (the server's)");
			L.lock ();
			o.srv = true;
			gb->id = (unsigned) i + 1; gb->addr = va;
			return 0;
		}
	int n = -1;
	for (int i = 0; i < USER_WS_SLOTS && n < 0; i++) if (!s_Buf[i].host) n = i;
	if (n < 0) return -KAPI_ENOMEM;
	u8 *h = host_shared_alloc (len);
	if (!h) return -KAPI_ENOMEM;
	for (int i = 0; i < USER_WS_SLOTS; i++)		// the slot's buffer so far leaves the program
	{
		WsBuf &o = s_Buf[i];
		if (!o.host || !o.prog || o.pid != (int) B.pid || o.slot != B.slot) continue;
		u64 olen = o.len;
		L.unlock ();
		unmap_in (o.pid, slot_va (B.slot), olen);
		L.lock ();
		o.prog = false;
		buf_free_if_unused (o);
	}
	WsBuf &b = s_Buf[n];
	b.host = h; b.len = len; b.pid = (int) B.pid; b.slot = B.slot; b.prog = true; b.srv = true;
	u64 sva = USER_WS_BASE + (u64) n * USER_WS_SLOT;
	int srv = s_ServerPid;
	L.unlock ();
	map_in ((int) B.pid, slot_va (B.slot), len, h, "a window's buffer");
	map_in (srv, sva, len, h, "a window's buffer (the server's)");
	gb->id = (unsigned) n + 1; gb->addr = sva;
	return 0;
}

static long buf_free (unsigned id)
{
	std::unique_lock<std::mutex> L (s_M);
	if (id == 0 || id > USER_WS_SLOTS) return -KAPI_EINVAL;
	WsBuf &b = s_Buf[id - 1];
	if (!b.host || !b.srv) return -KAPI_EINVAL;
	int srv = s_ServerPid, pid = b.pid, slot = b.slot;
	bool prog = b.prog;
	u64 len = b.len;
	b.srv = false; b.prog = false;
	L.unlock ();
	unmap_in (srv, USER_WS_BASE + (u64) (id - 1) * USER_WS_SLOT, len);
	if (prog) unmap_in (pid, slot_va (slot), len);
	L.lock ();
	buf_free_if_unused (b);
	return 0;
}

// ---- the programs' requests ---------------------------------------------------------------------------------------
static bool req_pending (void) { for (auto &r : s_Req) if (r.state == 1) return true; return false; }

static long ws_call (u64 uc)
{
	GWsCall *gc = G<GWsCall> (uc, MEM_R | MEM_W);
	int pid = me ();
	if (!gc) return -KAPI_EFAULT;
	GWsCall C = *gc;
	if (C.in_len > KAPI_WS_DATA_MAX) return -KAPI_EINVAL;
	std::vector<u8> in (C.in_len);
	if (C.in_len)
	{
		u8 *h = GB (C.in, C.in_len, MEM_R);
		if (!h) return -KAPI_EFAULT;
		memcpy (in.data (), h, C.in_len);
	}
	std::unique_lock<std::mutex> L (s_M);
	if (!s_Owned || s_ServerPid == 0 || pid == s_ServerPid) return -KAPI_ESRCH;
	WsReq *r = 0;
	for (auto &q : s_Req) if (q.state == 0) { r = &q; break; }
	if (!r) return -KAPI_EAGAIN;
	r->state = 1;
	r->id = s_NextReq++; if (s_NextReq == 0) s_NextReq = 1;
	r->pid = (unsigned) pid; r->op = C.op;
	for (int i = 0; i < 4; i++) r->a[i] = C.a[i];
	r->in = in; r->out.clear (); r->status = -KAPI_EIO;
	unsigned id = r->id;
	int srv = s_ServerPid;
	s_CV.notify_all ();
	auto start = Clock::now ();
	while (!(r->state == 3 && r->id == id))
	{
		s_CV.wait_for (L, std::chrono::milliseconds (50));
		if (r->state == 3 && r->id == id) break;
		if (r->id != id || s_ServerPid != srv || !s_Owned || Clock::now () - start > std::chrono::seconds (10))
		{
			if (r->id == id) r->state = 0;
			return -KAPI_ESRCH;
		}
		L.unlock (); check_dying (); L.lock ();
	}
	long long st = r->status;
	std::vector<u8> out = r->out;
	r->state = 0;
	L.unlock ();
	if (!out.empty () && C.out && C.out_cap)
	{
		unsigned n = (unsigned) out.size () < C.out_cap ? (unsigned) out.size () : C.out_cap;
		u8 *h = GB (C.out, n, MEM_W);
		if (!h) return -KAPI_EFAULT;
		memcpy (h, out.data (), n);
	}
	gc = G<GWsCall> (uc, MEM_W);
	if (gc) gc->out_len = (unsigned) out.size ();
	return (long) st;
}

static long ws_next (u64 ureq)
{
	u8 *h = GB (ureq, sizeof (GWsReqHead) + KAPI_WS_DATA_MAX, MEM_W);
	if (!h) return 0;
	std::lock_guard<std::mutex> L (s_M);
	for (auto &r : s_Req)
	{
		if (r.state != 1) continue;
		GWsReqHead H;
		H.id = r.id; H.pid = r.pid; H.op = r.op; H.in_len = (unsigned) r.in.size ();
		for (int k = 0; k < 4; k++) H.a[k] = r.a[k];
		memcpy (h, &H, sizeof H);
		if (!r.in.empty ()) memcpy (h + sizeof H, r.in.data (), r.in.size ());
		r.state = 2;
		return 1;
	}
	return 0;
}

static long ws_reply (u64 urep)
{
	GWsReply *g = G<GWsReply> (urep, MEM_R);
	if (!g) return -KAPI_EFAULT;
	GWsReply R = *g;
	std::vector<u8> out;
	long long st = R.status;
	if (R.data && R.len)
	{
		u8 *h = R.len <= KAPI_WS_DATA_MAX ? GB (R.data, R.len, MEM_R) : 0;
		if (!h) st = -KAPI_EIO; else out.assign (h, h + R.len);
	}
	std::lock_guard<std::mutex> L (s_M);
	for (auto &r : s_Req)
	{
		if (r.state != 2 || r.id != R.id) continue;
		r.status = st; r.out = out; r.state = 3;
		s_CV.notify_all ();
		return 0;
	}
	return -KAPI_EINVAL;
}

// ---- the server's operations --------------------------------------------------------------------------------------
static long ws_register (void)
{
	Proc *P = cur ();
	std::lock_guard<std::mutex> L (s_M);
	if (s_ServerPid == P->pid) return 1;
	Proc *S = s_ServerPid ? proc_find (s_ServerPid) : 0;
	if (S && !S->ended) return 0;
	if (P->name != "elegant" && P->name != "pocketui") return -KAPI_EPERM;
	s_ServerPid = P->pid;
	s_Owned = false;
	return 1;
}

static long ws_display (long take, u64 out)
{
	std::lock_guard<std::mutex> L (s_M);
	if (!take) { s_Owned = false; return 0; }
	int w, h;
	display_size (&w, &h);
	if (out)
	{
		struct kapi_ws_display *d = G<struct kapi_ws_display> (out, MEM_W);
		if (!d) return -KAPI_EFAULT;
		memset (d, 0, sizeof *d);
		d->w = w; d->h = h;
	}
	s_Owned = true;
	return 0;
}

static long ws_present (u64 up)
{
	struct kapi_ws_present *gp = G<struct kapi_ws_present> (up, MEM_R);
	if (!gp) return -KAPI_EFAULT;
	struct kapi_ws_present P = *gp;
	{ std::lock_guard<std::mutex> L (s_M); if (!s_Owned) return -KAPI_EPERM; }
	int W, H;
	display_size (&W, &H);
	if (P.w <= 0 || P.h <= 0) { P.x = 0; P.y = 0; P.w = W; P.h = H; }
	if (P.x < 0 || P.y < 0 || P.x >= W || P.y >= H || P.stride < W) return -KAPI_EINVAL;
	if (P.w > W - P.x) P.w = W - P.x;
	if (P.h > H - P.y) P.h = H - P.y;
	const u32 *src = (const u32 *) GB ((u64) P.pixels, ((u64) (P.y + P.h - 1) * P.stride + P.x + P.w) * 4, MEM_R);
	if (!src) return -KAPI_EFAULT;
	display_present (src, P.stride, P.x, P.y, P.w, P.h);
	return 0;
}

static long ws_input (u64 out, long max)
{
	if (!out || max <= 0) return 0;
	struct kapi_ws_input *o = (struct kapi_ws_input *) GB (out, (u64) max * sizeof (struct kapi_ws_input), MEM_W);
	if (!o) return -KAPI_EFAULT;
	std::lock_guard<std::mutex> L (s_M);
	long n = 0;
	while (n < max && !s_Ring.empty ()) { o[n++] = s_Ring.front (); s_Ring.pop_front (); }
	return n;
}

static long ws_wait (long ms)
{
	if (ms > 1000) ms = 1000;
	std::unique_lock<std::mutex> L (s_M);
	auto until = Clock::now () + std::chrono::milliseconds (ms);
	while (s_Owned && s_Ring.empty () && !req_pending () && Clock::now () < until)
	{
		s_CV.wait_until (L, until);
		L.unlock (); check_dying (); L.lock ();
	}
	return (s_Ring.empty () ? 0 : KAPI_WS_PENDING_INPUT) | (req_pending () ? KAPI_WS_PENDING_CALL : 0);
}

static long ws_attach (int pid)
{
	Proc *P = proc_find (pid);
	if (!P || P->ended) return -KAPI_ESRCH;
	std::lock_guard<std::mutex> L (s_M);
	if (!s_Clients.count (pid)) s_Clients[pid] = std::vector<u8> ();
	return 0;
}

bool ws_has_window (Proc *P)
{
	std::lock_guard<std::mutex> L (s_M);
	return s_Clients.count (P->pid) != 0;
}

static long ws_post (int pid, u64 uev)
{
	struct kapi_event *e = G<struct kapi_event> (uev, MEM_R);
	if (!e) return -KAPI_EFAULT;
	{ std::lock_guard<std::mutex> L (s_M); if (!s_Clients.count (pid)) return -KAPI_ESRCH; }
	Proc *P = proc_find (pid);
	if (!P || P->ended) return -KAPI_ESRCH;
	std::lock_guard<std::mutex> L (P->m);
	if (P->events.size () >= 63) return 0;
	P->events.push_back (*e);
	P->pumpCV.notify_all ();
	return 1;
}

static long ws_exit (int pid)
{
	{ std::lock_guard<std::mutex> L (s_M); if (!s_Clients.count (pid)) return -KAPI_ESRCH; }
	Proc *P = proc_find (pid);
	if (!P || P->ended) return -KAPI_ESRCH;
	std::lock_guard<std::mutex> L (P->m);
	P->exitAsked = true;
	P->pumpCV.notify_all ();
	return 0;
}

static long ws_state (int pid, u64 bytes, long set)
{
	u8 *h = GB (bytes, KAPI_WS_STATE_BYTES, set ? MEM_R : MEM_W);
	std::lock_guard<std::mutex> L (s_M);
	auto it = s_Clients.find (pid);
	if (it == s_Clients.end ()) return -KAPI_ESRCH;
	if (!h) return -KAPI_EFAULT;
	if (set) { it->second.assign (h, h + KAPI_WS_STATE_BYTES); return 0; }
	if (it->second.empty ()) return -KAPI_ENOENT;
	memcpy (h, it->second.data (), KAPI_WS_STATE_BYTES);
	return 0;
}

static long ws_clients (u64 out, long max)
{
	std::lock_guard<std::mutex> L (s_M);
	long n = 0;
	for (auto &c : s_Clients)
	{
		if (n >= max) break;
		unsigned *o = G<unsigned> (out + (u64) n * 4, MEM_W);
		if (!o) return -KAPI_EFAULT;
		*o = (unsigned) c.first;
		n++;
	}
	return n;
}

static long ws_proc_name (int pid, u64 buf, unsigned cap)
{
	Proc *P = proc_find (pid);
	if (!P || P->ended || cap == 0) return -KAPI_ESRCH;
	std::string n = P->name.substr (0, 63);
	gstr_out (buf, cap, n);
	return (long) (n.size () < cap ? n.size () : cap - 1);
}

static long ws_kick (long window)
{
	int pid = me ();
	std::lock_guard<std::mutex> L (s_M);
	if (!s_Owned || !s_Clients.count (pid)) return -KAPI_ESRCH;
	struct kapi_ws_input e; memset (&e, 0, sizeof e);
	e.type = KAPI_WS_IN_KICK; e.a = pid;
	e.x = window > 0 && window <= KAPI_WS_WINDOWS_MORE ? (int) window : 0;
	push (e);
	return s_ServerPid;
}

// A process is gone (proc.cpp): the server's role, its programs' buffers and requests.
void ws_proc_gone (Proc *P)
{
	std::lock_guard<std::mutex> L (s_M);
	if (P->pid == s_ServerPid)
	{
		s_ServerPid = 0;
		s_Owned = false;
		s_Ring.clear ();
		for (auto &b : s_Buf) { b.srv = false; buf_free_if_unused (b); }
		s_CV.notify_all ();
		return;
	}
	if (!s_Clients.count (P->pid)) return;
	s_Clients.erase (P->pid);
	for (auto &b : s_Buf) if (b.host && b.pid == P->pid) { b.prog = false; buf_free_if_unused (b); }
	for (auto &r : s_Req) if ((int) r.pid == P->pid && r.state == 1) r.state = 0;
	struct kapi_ws_input e; memset (&e, 0, sizeof e);
	e.type = KAPI_WS_IN_GONE; e.a = P->pid;
	push (e);
}

void ws_quit_request (void)
{
	std::lock_guard<std::mutex> L (s_M);
	struct kapi_ws_input e; memset (&e, 0, sizeof e);
	e.type = KAPI_WS_IN_QUIT;
	push (e);
}

static long k_ws_ctl (int op, long long a0, long long a1, long long a2)
{
	if (op == KAPI_WS_ACTIVE) return ws_active ();
	if (op == KAPI_WS_REGISTER) return ws_register ();
	if (op == KAPI_WS_CALL) return ws_call ((u64) a0);
	if (op == KAPI_WS_KICK) return ws_kick ((long) a0);
	if (op == KAPI_WS_SWITCH) return -KAPI_EBUSY;
	{ std::lock_guard<std::mutex> L (s_M); if (me () != s_ServerPid) return -KAPI_EPERM; }
	switch (op)
	{
	case KAPI_WS_DISPLAY:	return ws_display ((long) a0, (u64) a1);
	case KAPI_WS_PRESENT:	return ws_present ((u64) a0);
	case KAPI_WS_INPUT:	return ws_input ((u64) a0, (long) a1);
	case KAPI_WS_WAIT:	return ws_wait ((long) a0);
	case KAPI_WS_ATTACH:	return ws_attach ((int) a0);
	case KAPI_WS_POST:	return ws_post ((int) a0, (u64) a1);
	case KAPI_WS_EXIT:	return ws_exit ((int) a0);
	case KAPI_WS_BUF_MAP:	return buf_map ((u64) a0);
	case KAPI_WS_BUF_FREE:	return buf_free ((unsigned) a0);
	case KAPI_WS_NEXT:	return ws_next ((u64) a0);
	case KAPI_WS_REPLY:	return ws_reply ((u64) a0);
	case KAPI_WS_FOCUS:	{ std::lock_guard<std::mutex> L (s_M); s_FocusPid = (int) a0; } return 0;
	case KAPI_WS_PROC_NAME:	return ws_proc_name ((int) a0, (u64) a1, (unsigned) a2);
	case KAPI_WS_STATE:	return ws_state ((int) a0, (u64) a1, (long) a2);
	case KAPI_WS_CLIENTS:	return ws_clients ((u64) a0, (long) a1);
	}
	return -KAPI_ENOSYS;
}

// ---- the screen's calls of any program ---------------------------------------------------------------------------
static void k_screen_size (u64 w, u64 h)
{
	int W, H;
	display_size (&W, &H);
	gput<int> (w, W); gput<int> (h, H);
}

static int k_screen_native (u64 w, u64 h) { k_screen_size (w, h); return 1; }

static int k_screen_grab (u64 dst, int w, int h)
{
	u32 *d = (u32 *) GB (dst, (u64) w * h * 4, MEM_W);
	return d && display_grab (d, w, h) ? 1 : 0;
}

static unsigned k_get_modifiers (void)
{
	Proc *P = cur ();
	{ std::lock_guard<std::mutex> L (P->m); if (P->evMods != 0xFFFFFFFFu) return P->evMods; }
	std::lock_guard<std::mutex> L (s_M);
	return s_Mods & 7;
}

static int k_key_held (int key)
{
	std::lock_guard<std::mutex> L (s_M);
	if (s_FocusPid && s_FocusPid != me ()) return 0;
	if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
	auto it = s_Held.find (key);
	return it != s_Held.end () && it->second ? 1 : 0;
}

static int k_font_width (void) { return 8; }
static int k_font_height (void) { return 16; }
static int k_set_cursor (int) { return 0; }

KAPI (ws_ctl, k_ws_ctl);
KAPI (screen_size, k_screen_size);
KAPI (screen_native, k_screen_native);
KAPI (screen_grab, k_screen_grab);
KAPI (get_modifiers, k_get_modifiers);
KAPI (key_held, k_key_held);
KAPI (font_width, k_font_width);
KAPI (font_height, k_font_height);
KAPI (set_cursor, k_set_cursor);
