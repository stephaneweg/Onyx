//
// server_sim.cpp -- a graphics server of Onyx run on the PC, with a real app as its client, in one process:
// the code the servers share (user/Servers/common/: the window manager and compositor, the routing, the requests'
// decoding) and a server's policy (PocketUI's user/Servers/pocketui/wm.cpp + band.cpp, or Elegant's -- none),
// built for the host; the app built against UIKit's WIRE port (UK_PORT_WIRE: the port's real client, the one
// SD:/lib/uikit.so or SD:/lib/pocket/uikit.so has on Onyx, -DUK_PORT_POCKET for PocketUI's) and the desktop
// simulator's stand-in kernel (tools/tests/desktop_sim/fakekapi.cpp, its server hooks). This file stands for the
// kernel's side of the graphics server (kernel/sys/wsrv.cpp): kapi_ws_ctl's operations -- the app's requests
// (KAPI_WS_CALL straight to the server's el_op, KAPI_WS_KICK), the shared buffers (two mappings of one memory: the
// server's at its own address, the app's at KAPI_WS_VA_*), the events queued for the app and run at its next
// turn, the windows' kept state --, and the input: the script's pointer and keys in SCREEN coordinates go to the
// server (which routes them as on the Pi). docs/03-DEVELOPER-GUIDE.md "PocketUI on the PC".
//
// The script (SIM, fakekapi.cpp's) gains, or takes over:
//   down X Y / up X Y / move X Y / rdown X Y / rup X Y / wheel X Y N   the pointer, SCREEN coordinates
//   key C           a key (a character, or a KEY_* number: arrows, Home, End, PgUp/PgDn, Del, F1-F12, Enter 13)
//   mods N          the modifiers held from now on (1 Ctrl, 2 Shift, 4 Alt): "mods 4;key 0x09;mods 0" is Alt+Tab
//   dump FILE       the composed SCREEN: "ELSM" w h 0 0, then w x h pixels (tools/tests/desktop_sim/shot.py makes it a PNG)
//   screen FILE     the screen as the turns composed it (only what was damaged, as on the Pi), the same format
//   other W H T [F [X Y]] another program's window (pid 101 + n, a plain coloured canvas titled T, flags F, at
//                   X, Y -- else placed by the server): the policy with two programs, the desktop's bands
//   otherpic W H T FILE SX SY [F [X Y]]   the same, its canvas painted from a dump (FILE.elsm, from SX, SY): a real app's look
//   othermenu SPEC  the latest "other" program's menus ('|' between the lines, '~' a tab): what the menu bar shows
//   owin W H T      a second window of the latest "other" program (a dialog)
//   oclose          the latest "other" program ended (its windows removed)
//   raise           the latest "other" program raised by its own request (EL_OP_WIN_RAISE)
//   place X Y       the latest "other" window moved by its own request (EL_OP_MOVE)
//   expect K V      a check, printed PASS / FAIL (the run's exit status counts the FAILs): K one of
//                   front (the program in front: app, other, none), kind (the app's first window: card, fill, popup),
//                   frame (1 the app's window has a frame, 0 none), area (the work area "x,y,w,h"),
//                   client (the app's client size "w,h"), pos (its window's top left "x,y"), aside (1 / 0: the app set aside),
//                   made (1 / 0: the latest "other" window was made, or refused), menu (the title of the menus the
//                   menu bar gets: EL_OP_MENU_GET, "-" none), bar (1 / 0: PocketUI has the global menu bar),
//                   band (1 / 0: PocketUI's own status band is there), opos (the latest "other" window's top left "x,y");
//                   (P5) home (1 / 0), shell (app, other, none), tasks (how many), kindN (the app's window N's kind),
//                   okind[N] (the latest "other" program's window N's), shownN (1 / 0: the app's window N on the
//                   screen), focus (who has the keys: app, other, none), keys (the GUI_EVENT_KEY the app received),
//                   matte (1 / 0: PocketUI's matte shown), full (1 / 0: the app has the full screen, as the list says),
//                   hidden (1 / 0: the app's window not shown as the list says -- set aside: KAPI_WIN_OFFDESK, rdpd's view)
//   (mods 8 is the Super key: the policy's mods hook gets it, the window manager not -- as serve.cpp does; the app's
//   uk_win_fullscreen_begin / _end tell the server as the kernel does, KAPI_WS_IN_FULLSCREEN)
// Everything else is fakekapi.cpp's (wait, quit, exit...).
//
// Environment: SIM_SCREEN=WxH (the screen; fakekapi's), SIM_MODE=console (PocketUI's console mode), SIM_APPNAME
// (the app's name, for the lists), SIM_SCREEN_STALE=WxH (the app's kapi_screen_size wrong: the server's screen kept),
// SIM_SERVER_LATE=N (PocketUI: the first N PK_OP_SERVER refused -- uk_win_server falls back on kapi_screen_size: a
// program started while the server comes up after a live switch).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#define _GNU_SOURCE 1
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <map>
#include <string>
#include <vector>
#include "appkit/appkit.h"
#include "uikit/port/elegant.h"
#include "core.h"
#ifndef SIM_OLD
#include "policy.h"
#endif
#ifdef SIM_POCKET
#include "pocketui.h"
#include "uikit/port/pocket.h"
#endif

#define SELF		1			// the server's pid (kapi_getpid answers it: both are this process)
#define APP_PID		100			// the app's
#define OTHER_PID	101			// the script's "other" programs: 101, 102...

// ---- what the stand-in kernel lacks for a server ---------------------------------------------------------
// (kapi_memset, kapi_memcpy, kapi_memmove: the C library's -- sim_mem.cpp, strong over appkit.h's weak ones)
extern "C" unsigned el_port_ticks (void)					{ return kapi_get_ticks (); }
unsigned g_nStubTicks = 1000;			// (tools/tests/desktop_sim/kstub/circle/timer.h: the window manager's clock)

#ifndef SIM_POCKET
#ifndef SIM_OLD
static const struct ws_policy s_Elegant = { "elegant" };	// (the rest 0: the common behaviour)	// (Elegant's: the common behaviour)
const struct ws_policy *g_pWsPolicy = &s_Elegant;
#endif
#endif

static int s_nFail, s_nPass;
static bool s_bInit, s_bExit;
static int s_nW, s_nH;
static unsigned *s_pScreen;
static unsigned s_nButtons, s_nMods;
static int s_nOthers;
static bool s_bOtherMade;
static std::map<unsigned, std::string> s_Names;	// the "other" programs' names (their title in lower case)
static int s_nKeys;				// the keys delivered to the app (GUI_EVENT_KEY events)

// ---- the shared buffers: one memory, mapped twice ---------------------------------------------------------
#define WS_BASE		0xB00000000ULL		// (user/Servers/common/serve.cpp: a buffer's address in the server says its number)
#define WS_SLOT		0x4000000ULL
struct TBuf { bool used; unsigned pid; int slot; unsigned long bytes; int fd; void *srv, *app; };
static TBuf s_Buf[256];

static unsigned long long SlotVa (int slot)
{
	switch (slot)
	{
	case KAPI_WS_SLOT_CANVAS:	return KAPI_WS_VA_CANVAS;
	case KAPI_WS_SLOT_FRAME:	return KAPI_WS_VA_FRAME;
	case KAPI_WS_SLOT_FRAME_OFF:	return KAPI_WS_VA_FRAME_OFF;
	case KAPI_WS_SLOT_WALLPAPER:	return KAPI_WS_VA_WALLPAPER;
	case KAPI_WS_SLOT_XFER:		return KAPI_WS_VA_XFER;
	}
	int w = (slot - KAPI_WS_SLOT_MORE) / 3 + 1, p = (slot - KAPI_WS_SLOT_MORE) % 3;
	return KAPI_WS_VA_WIN (w, p);
}

static long BufMap (struct kapi_ws_buf *b)
{
	int i = 1;
	while (i < 256 && s_Buf[i].used) i++;
	if (i == 256 || b->bytes == 0 || b->bytes > WS_SLOT) return -KAPI_ENOMEM;
	unsigned long n = (b->bytes + 0xFFFF) & ~0xFFFFUL;
	int fd = memfd_create ("ws", 0);
	if (fd < 0 || ftruncate (fd, (off_t) n) != 0) return -KAPI_ENOMEM;
	void *srv = mmap ((void *) (WS_BASE + i * WS_SLOT), n, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
	if (srv == MAP_FAILED) { close (fd); return -KAPI_ENOMEM; }
	void *app = 0;
	if (b->pid == APP_PID)				// (the app's view at its slot's place: what was there replaced)
	{
		for (int k = 1; k < 256; k++)
			if (s_Buf[k].used && s_Buf[k].pid == APP_PID && s_Buf[k].slot == b->slot && s_Buf[k].app != 0) s_Buf[k].app = 0;
		app = mmap ((void *) SlotVa (b->slot), n, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
		if (app == MAP_FAILED) app = 0;
	}
	s_Buf[i] = { true, b->pid, b->slot, n, fd, srv, app };
	b->id = (unsigned) i;
	b->addr = (unsigned long long) srv;
	return 0;
}

static long BufFree (unsigned id)
{
	if (id == 0 || id >= 256 || !s_Buf[id].used) return -KAPI_EINVAL;
	TBuf &B = s_Buf[id];
	munmap (B.srv, B.bytes);
	if (B.app != 0) mmap (B.app, B.bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	close (B.fd);
	B = TBuf ();
	return 0;
}

// ---- the kernel's kapi_ws_ctl, for the server and for the app ----------------------------------------------
static std::vector<struct kapi_event> s_Ev;		// queued for the app (run at its next turn)
static std::map<unsigned, std::vector<unsigned char> > s_State;
static bool s_bAttached;
static unsigned char s_Out[KAPI_WS_DATA_MAX];

static void Init (void);

extern "C" long sim_ws_ctl (int op, long a0, long a1, long a2)
{
	Init ();
	switch (op)
	{
	case KAPI_WS_ACTIVE:	return SELF;
	case KAPI_WS_CALL:
		{
			struct kapi_ws_call *c = (struct kapi_ws_call *) a0;
			unsigned len = 0;
#ifdef SIM_POCKET
			static int s_nLate = getenv ("SIM_SERVER_LATE") ? atoi (getenv ("SIM_SERVER_LATE")) : 0;
			if ((c->op & 0xFFFF) == PK_OP_SERVER && s_nLate > 0)	// (the server's answer not there yet: the first n asked)
			{
				s_nLate--;
				c->out_len = 0;
				return EL_E_BADOP;
			}
#endif
			long st = el_op (APP_PID, c->op, c->a, (const unsigned char *) c->in, c->in_len, s_Out, &len);
			if (getenv ("SIM_TRACE")) fprintf (stderr, "server_sim: request %d (window %d) %ld %ld %ld %ld -> %ld\n", c->op & 0xFFFF, c->op >> 16, c->a[0], c->a[1], c->a[2], c->a[3], st);
			if (c->out != 0) memcpy (c->out, s_Out, len < c->out_cap ? len : c->out_cap);
			c->out_len = len;
			return st;
		}
	case KAPI_WS_KICK:
		el_core_window_present (el_core_window_of_win (APP_PID, (int) a0));
		return SELF;
	case KAPI_WS_ATTACH:	s_bAttached = true; return 0;
	case KAPI_WS_POST:
		if ((unsigned) a0 == APP_PID)
		{
			s_Ev.push_back (*(const struct kapi_event *) a1);
			if (((const struct kapi_event *) a1)->event == GUI_EVENT_KEY) s_nKeys++;
		}
		return 1;
	case KAPI_WS_EXIT:	if ((unsigned) a0 == APP_PID) s_bExit = true; return 0;
	case KAPI_WS_BUF_MAP:	return BufMap ((struct kapi_ws_buf *) a0);
	case KAPI_WS_BUF_FREE:	return BufFree ((unsigned) a0);
	case KAPI_WS_FOCUS:	return 0;
	case KAPI_WS_PROC_NAME:
		{
			const char *n = (unsigned) a0 == APP_PID ? (getenv ("SIM_APPNAME") ? getenv ("SIM_APPNAME") : "app")
				      : s_Names.count ((unsigned) a0) ? s_Names[(unsigned) a0].c_str () : "other";
			snprintf ((char *) a1, (size_t) a2, "%s", n);
			return (long) strlen ((char *) a1);
		}
	case KAPI_WS_STATE:
		{
			std::vector<unsigned char> &S = s_State[(unsigned) a0];
			if (a2) { S.assign ((const unsigned char *) a1, (const unsigned char *) a1 + KAPI_WS_STATE_BYTES); return 0; }
			if (S.empty ()) return -KAPI_ENOENT;
			memcpy ((void *) a1, S.data (), KAPI_WS_STATE_BYTES);
			return 0;
		}
	case KAPI_WS_CLIENTS:
		if (!s_bAttached || a1 < 1) return 0;
		((unsigned *) a0)[0] = APP_PID;
		return 1;
	}
	return -KAPI_ENOSYS;
}

// ---- the server's door to the kernel (core.h: user/Servers/common/serve.cpp's, which is not linked here) --------
void *el_shared_alloc (unsigned pid, int part, unsigned long bytes)
{
	struct kapi_ws_buf b;
	memset (&b, 0, sizeof b);
	b.pid = pid; b.slot = part; b.bytes = bytes;
	return sim_ws_ctl (KAPI_WS_BUF_MAP, (long) &b, 0, 0) == 0 ? (void *) b.addr : 0;
}
int el_shared_is (const void *p)
{
	unsigned long long a = (unsigned long long) p;
	return a >= WS_BASE && a < WS_BASE + 256 * WS_SLOT;
}
void el_shared_free (void *p)
{
	if (el_shared_is (p)) sim_ws_ctl (KAPI_WS_BUF_FREE, (long) (((unsigned long long) p - WS_BASE) / WS_SLOT), 0, 0);
}
int el_sys_attach (unsigned pid)				{ return sim_ws_ctl (KAPI_WS_ATTACH, pid, 0, 0) == 0; }
int el_sys_state (unsigned pid, void *bytes, int set)		{ return sim_ws_ctl (KAPI_WS_STATE, pid, (long) bytes, set) == 0; }
int el_sys_clients (unsigned *pids, int max)			{ long n = sim_ws_ctl (KAPI_WS_CLIENTS, (long) pids, max, 0); return n > 0 ? (int) n : 0; }
int el_sys_name (unsigned pid, char *buf, unsigned cap)	{ long n = sim_ws_ctl (KAPI_WS_PROC_NAME, pid, (long) buf, cap); return n > 0 ? (int) n : 0; }
int el_sys_screen_grab (unsigned *dst, int w, int h)		{ return kapi_screen_grab (dst, w, h); }
#ifdef SIM_POCKET
void pk_main_registered (int)					{}	// (PocketUI's main.cpp: its UIKit's alias -- none on the PC)
#endif

// ---- the kernel's table, patched where a server needs more than the desktop simulator gives -------------------
static int (*s_pShouldExit) (void);
static int SimShouldExit (void)			{ return s_bExit || (s_pShouldExit != 0 && s_pShouldExit ()); }
static int SimGetpid (int)			{ return SELF; }
// The full screen: the kernel tells the server (KAPI_WS_IN_FULLSCREEN), as kernel/sys/kapi.cpp does
static unsigned *(*s_pFsBegin) (int *, int *);
static void (*s_pFsEnd) (void);
static bool s_bFull;
static unsigned *SimFsBegin (int *w, int *h)
{
	unsigned *p = s_pFsBegin != 0 ? s_pFsBegin (w, h) : 0;
	if (p != 0) { el_core_fullscreen (APP_PID, 1); s_bFull = true; }
	return p;
}
static void SimFsEnd (void)
{
	if (s_pFsEnd != 0) s_pFsEnd ();
	el_core_fullscreen (APP_PID, 0); s_bFull = false;
}

// SIM_SCREEN_STALE=WxH: the program's kapi_screen_size answers that size -- not the server's screen (the Pi's report
// after a live switch from the desktop: pocketshell laid out small); the server keeps SIM_SCREEN's.
static void SimStaleScreen (int *w, int *h)
{
	int sw = 800, sh = 480;
	sscanf (getenv ("SIM_SCREEN_STALE"), "%dx%d", &sw, &sh);
	if (w) *w = sw;
	if (h) *h = sh;
}

static void Init (void)
{
	if (s_bInit) return;
	s_bInit = true;
	TKApiTable *T = (TKApiTable *) KAPI_TABLE_VA;
	s_pShouldExit = T->should_exit; T->should_exit = SimShouldExit;
	T->getpid = SimGetpid;
	s_pFsBegin = T->fullscreen_begin; T->fullscreen_begin = SimFsBegin;
	s_pFsEnd = T->fullscreen_end; T->fullscreen_end = SimFsEnd;
	kapi_screen_size (&s_nW, &s_nH);
	if (getenv ("SIM_SCREEN_STALE")) T->screen_size = SimStaleScreen;	// (the app's kapi_screen_size: stale from now on)
	s_pScreen = new unsigned[(size_t) s_nW * s_nH];
#ifdef SIM_POCKET
	const char *m = getenv ("SIM_MODE");
	g_nPkMode = m != 0 && strcmp (m, "console") == 0 ? PK_MODE_CONSOLE : PK_MODE_POCKET;
#endif
	el_core_start (s_nW, s_nH);
#ifndef SIM_OLD
	if (g_pWsPolicy != 0 && g_pWsPolicy->start != 0) g_pWsPolicy->start (s_nW, s_nH, 0);
#endif
	fprintf (stderr, "server_sim: %s on a %d x %d screen\n",
#ifdef SIM_POCKET
		 g_nPkMode == PK_MODE_CONSOLE ? "PocketUI (console)" : "PocketUI (pocket)",
#elif defined (SIM_OLD)
		 "Elegant (as it was before the extraction)",
#else
		 "Elegant",
#endif
		 s_nW, s_nH);
}

// ---- a turn ---------------------------------------------------------------------------------------------
#ifdef SIM_OLD
// (the routing as Elegant's server.cpp had it before user/Servers/common/route.cpp: the events forwarded, the
// windows' places saved)
static unsigned s_nExitAsked[EL_WINDOWS_MAX];
static void ws_route_turn (unsigned self)
{
	for (int id = 0; id < EL_WINDOWS_MAX; id++)
	{
		unsigned pid = el_core_window_pid (id);
		if (pid == 0 || pid == self) continue;
		struct el_core_event e;
		while (el_core_window_event_peek (id, &e))
		{
			struct kapi_event k;
			memset (&k, 0, sizeof k);
			k.handler = e.handler; k.value = e.value; k.event = e.event; k.mods = e.mods;
			k.sender = (unsigned long long) el_core_window_win (id);
			long r = sim_ws_ctl (KAPI_WS_POST, pid, (long) &k, 0);
			if (r == 0) break;
			el_core_window_event_drop (id);
		}
		if (el_core_window_closing (id) && s_nExitAsked[id] != pid) { sim_ws_ctl (KAPI_WS_EXIT, pid, 0, 0); s_nExitAsked[id] = pid; }
	}
	el_core_save_states (self);
}
static void ws_route_key (const char *keys, unsigned) { el_core_key (keys); }
#endif

extern "C" void sim_server_turn (void)
{
	Init ();
	g_nStubTicks = kapi_get_ticks ();
	ws_route_turn (SELF);
	std::vector<struct kapi_event> q;
	q.swap (s_Ev);
	static const bool bTrace = getenv ("SIM_TRACE") != 0;
	for (auto &e : q)
	{
		if (bTrace) fprintf (stderr, "server_sim: event %d value 0x%llx to window %llu\n", e.event, (unsigned long long) e.value, (unsigned long long) e.sender);
		if (e.handler != 0) ((gui_handler) e.handler) ((unsigned long) e.sender, e.event, (gui_value) e.value);
	}
	el_core_compose (s_pScreen, s_nW, s_nH, 0);
}

// ---- the script's steps -------------------------------------------------------------------------------------
static void Dump (const char *file, bool bWhole = true)
{
	if (bWhole) el_core_redraw ();
	el_core_compose (s_pScreen, s_nW, s_nH, 0);
	FILE *f = fopen (file, "wb");
	if (f == 0) { perror (file); return; }
	int hdr[5] = { 0x4D534C45, s_nW, s_nH, 0, 0 };
	fwrite (hdr, 4, 5, f);
	std::vector<unsigned> px ((size_t) s_nW * s_nH);
	for (size_t i = 0; i < px.size (); i++) px[i] = s_pScreen[i] & 0x00FFFFFFu;
	fwrite (px.data (), 4, px.size (), f);
	fclose (f);
	fprintf (stderr, "server_sim: dumped the screen %dx%d -> %s\n", s_nW, s_nH, file);
}

static void KeyString (long k, char *out)
{
	static const struct { long code; const char *seq; } Map[] = {
		{ KEY_UP, "\x1b[A" }, { KEY_DOWN, "\x1b[B" }, { KEY_RIGHT, "\x1b[C" }, { KEY_LEFT, "\x1b[D" },
		{ KEY_HOME, "\x1b[H" }, { KEY_END, "\x1b[F" }, { KEY_PGUP, "\x1b[5~" }, { KEY_PGDN, "\x1b[6~" }, { KEY_DEL, "\x1b[3~" },
		{ KEY_ENTER, "\n" }, { KEY_BACKSPACE, "\x7f" } };
	for (auto &m : Map) if (m.code == k) { strcpy (out, m.seq); return; }
	if (k >= KEY_F1 && k <= KEY_F12) { static const int n[] = { 11, 12, 13, 14, 15, 17, 18, 19, 20, 21, 23, 24 }; sprintf (out, "\x1b[%d~", n[k - KEY_F1]); return; }
	out[0] = (char) k; out[1] = 0;
}

static int AppId (void)				{ return el_core_window_of_win (APP_PID, 0); }

static void Check (const char *what, bool ok, const std::string &got)
{
	(ok ? s_nPass : s_nFail)++;
	fprintf (stderr, "server_sim: %s %s%s%s\n", ok ? "PASS" : "FAIL", what, ok ? "" : " -- got ", ok ? "" : got.c_str ());
}

static void Expect (const char *key, const char *want)
{
	int id = AppId ();
	char got[64] = "";
	struct el_core_frame F;
	memset (&F, 0, sizeof F);
	if (id >= 0) el_core_window_frame_info (id, &F);
	if (!strcmp (key, "frame")) snprintf (got, sizeof got, "%d", F.frame_w > 0 ? 1 : 0);
	else if (!strcmp (key, "made")) snprintf (got, sizeof got, "%d", s_bOtherMade ? 1 : 0);
	else if (!strcmp (key, "opos"))
	{
		struct kapi_win_geom G;
		memset (&G, 0, sizeof G);
		unsigned len = 0;
		long r[4] = { 0, 0, 0, 0 };
		el_op (OTHER_PID + (unsigned) s_nOthers - 1, EL_OP_GEOMETRY, r, 0, 0, s_Out, &len);
		memcpy (&G, s_Out, sizeof G);
		snprintf (got, sizeof got, "%d,%d", G.x, G.y);
	}
	else if (!strcmp (key, "menu"))
	{
		struct el_menu M;
		memset (&M, 0, sizeof M);
		unsigned len = 0;
		long a[4] = { 0, 0, 0, 0 };
		long serial = el_op (OTHER_PID + 50, EL_OP_MENU_GET, a, 0, 0, s_Out, &len);	// (as the menu bar asks it)
		memcpy (&M, s_Out, len < sizeof M ? len : sizeof M);
		snprintf (got, sizeof got, "%s", serial > 0 && M.title[0] ? M.title : "-");
	}
	else if (!strcmp (key, "client")) snprintf (got, sizeof got, "%d,%d", F.content_w, F.content_h);
	else if (!strcmp (key, "keys")) snprintf (got, sizeof got, "%d", s_nKeys);
	else if (!strcmp (key, "full"))					// the app has the full screen (the server's state, the list's)
	{
		struct kapi_win_info L[64];
		long a[4] = { 64, 0, 0, 0 };
		unsigned len = 0;
		long n = el_op (APP_PID, EL_OP_WIN_LIST, a, 0, 0, s_Out, &len);
		memcpy (L, s_Out, len < sizeof L ? len : sizeof L);
		int on = 0;
		for (long i = 0; i < n && i < 64; i++)
			if (L[i].pid == APP_PID && (L[i].state & KAPI_WIN_FULLSCREEN)) on = L[i].ow == 0 && L[i].x == 0 && L[i].w == s_nW ? 1 : 9;	// (9: framed or not the screen)
		snprintf (got, sizeof got, "%d%s", on, on == (s_bFull ? 1 : 0) ? "" : " (the kernel's: not the same)");
	}
	else if (!strcmp (key, "focus"))
	{
		unsigned p = el_core_focus_pid ();
		snprintf (got, sizeof got, "%s", p == APP_PID ? "app" : p > APP_PID ? "other" : "none");
	}
	else if (!strncmp (key, "shown", 5))					// shownN: the app's window N on the screen (1 / 0)
	{
		struct kapi_win_info L[64];
		long a[4] = { 64, 0, 0, 0 };
		unsigned len = 0;
		long n = el_op (APP_PID, EL_OP_WIN_LIST, a, 0, 0, s_Out, &len);
		memcpy (L, s_Out, len < sizeof L ? len : sizeof L);
		int id = el_core_window_of_win (APP_PID, key[5] ? key[5] - '0' : 0), on = 0;
		struct el_core_frame F2;
		memset (&F2, 0, sizeof F2);
		if (id >= 0) el_core_window_frame_info (id, &F2);
		for (long i = 0; i < n && i < 64; i++)
			if (L[i].pid == APP_PID && !strcmp (L[i].title, F2.title)) on = L[i].x >= 0 && L[i].x < s_nW && !(L[i].state & KAPI_WIN_MINIMISED) ? 1 : 0;
		snprintf (got, sizeof got, "%d", on);
	}
#ifdef SIM_POCKET
	else if (!strcmp (key, "front"))
	{
		unsigned p = pk_front_pid ();
		snprintf (got, sizeof got, "%s", p == APP_PID ? "app" : p > APP_PID ? "other" : "none");
	}
	else if (!strcmp (key, "kind"))
	{
		static const char *K[] = { "none", "own", "popup", "card", "fill", "bar", "home", "shell", "centre" };
		int k = pk_kind (id);
		snprintf (got, sizeof got, "%s", k >= 0 && k <= 8 ? K[k] : "?");
	}
	else if (!strcmp (key, "bar")) snprintf (got, sizeof got, "%d", pk_bar_id () >= 0 ? 1 : 0);
	else if (!strcmp (key, "home")) snprintf (got, sizeof got, "%d", pk_home ());
	else if (!strcmp (key, "matte")) snprintf (got, sizeof got, "%d", pk_matte ());
	else if (!strcmp (key, "shell")) snprintf (got, sizeof got, "%s", pk_shell_pid () == APP_PID ? "app" : pk_shell_pid () ? "other" : "none");
	else if (!strcmp (key, "tasks"))
	{
		long a[4] = { 32, 0, 0, 0 };
		unsigned len = 0;
		snprintf (got, sizeof got, "%ld", el_op (APP_PID, PK_OP_TASKS, a, 0, 0, s_Out, &len));
	}
	else if (!strncmp (key, "kind", 4) && key[4] >= '0' && key[4] <= '9')	// kindN: the app's window N
	{
		static const char *K[] = { "none", "own", "popup", "card", "fill", "bar", "home", "shell", "centre" };
		int k = pk_kind (el_core_window_of_win (APP_PID, key[4] - '0'));
		snprintf (got, sizeof got, "%s", k >= 0 && k <= 8 ? K[k] : "?");
	}
	else if (!strncmp (key, "okind", 5))				// okind[N]: the latest "other" program's window N's kind
	{
		static const char *K[] = { "none", "own", "popup", "card", "fill", "bar", "home", "shell", "centre" };
		int k = pk_kind (el_core_window_of_win (OTHER_PID + (unsigned) s_nOthers - 1, key[5] ? key[5] - '0' : 0));
		snprintf (got, sizeof got, "%s", k >= 0 && k <= 8 ? K[k] : "?");
	}
	else if (!strcmp (key, "band")) snprintf (got, sizeof got, "%d", pk_band_id () >= 0 ? 1 : 0);
#endif
	else
	{
		struct kapi_win_geom G;				// (as the app asks it: EL_OP_GEOMETRY)
		memset (&G, 0, sizeof G);
		unsigned len = 0;
		long a[4] = { 0, 0, 0, 0 };
		el_op (APP_PID, EL_OP_GEOMETRY, a, 0, 0, s_Out, &len);
		memcpy (&G, s_Out, sizeof G);
		if (!strcmp (key, "area")) snprintf (got, sizeof got, "%d,%d,%d,%d", G.ax, G.ay, G.aw, G.ah);
		else if (!strcmp (key, "pos")) snprintf (got, sizeof got, "%d,%d", G.x, G.y);
		else if (!strcmp (key, "aside")) snprintf (got, sizeof got, "%d", (G.state & KAPI_WIN_KEYS) ? 0 : 1);
		else if (!strcmp (key, "hidden")) snprintf (got, sizeof got, "%d", (G.state & (KAPI_WIN_OFFDESK | KAPI_WIN_MINIMISED)) ? 1 : 0);
		else snprintf (got, sizeof got, "(unknown check %s)", key);
	}
	std::string what = std::string (key) + " = " + want;
	Check (what.c_str (), !strcmp (got, want), got);
}

static std::string s_Pic; static int s_nPicX, s_nPicY;	// (otherpic: the canvas from that dump, from that point)

static void Other (int w, int h, const char *title, unsigned flags, int x, int y)	// another program's window, painted
{
	unsigned pid = OTHER_PID + (unsigned) s_nOthers++;
	{ std::string n (title); for (auto &c : n) c = (char) tolower (c); s_Names[pid] = n; }
	struct el_create C;
	memset (&C, 0, sizeof C);
	C.flags = flags;
	snprintf (C.title, sizeof C.title, "%s", title);
	long a[4] = { -1, -1, w, h };
	unsigned len = 0;
	if (x >= 0 && y >= 0) { a[0] = x; a[1] = y; }
	s_bOtherMade = el_op (pid, EL_OP_CREATE, a, (const unsigned char *) &C, sizeof C, s_Out, &len) == 1;
	if (!s_bOtherMade) { fprintf (stderr, "server_sim: other: refused\n"); return; }
	int id = el_core_window_of_win (pid, 0), cw = 0, ch = 0;
	unsigned *p = el_core_window_canvas (id, &cw, &ch);
	for (int y = 0; p != 0 && y < ch; y++)
		for (int x = 0; x < cw; x++) p[(size_t) y * cw + x] = ((x / 24 + y / 24) & 1) ? 0x00C8D4E4 : 0x00B8C6DA;
	if (!s_Pic.empty () && p != 0)				// a picture: a part of a screen dumped before (an app's look)
	{
		FILE *f = fopen (s_Pic.c_str (), "rb");
		int hdr[5] = { 0, 0, 0, 0, 0 };
		if (f != 0 && fread (hdr, 4, 5, f) == 5 && hdr[0] == 0x4D534C45)
		{
			std::vector<unsigned> px ((size_t) hdr[1] * hdr[2]);
			if (fread (px.data (), 4, px.size (), f) == px.size ())
				for (int y = 0; y < ch && s_nPicY + y < hdr[2]; y++)
					for (int x = 0; x < cw && s_nPicX + x < hdr[1]; x++) p[(size_t) y * cw + x] = px[(size_t) (s_nPicY + y) * hdr[1] + s_nPicX + x];
		}
		if (f != 0) fclose (f);
		s_Pic.clear ();
	}
	for (int k = 0; k < 2; k++)				// (its frame: a plain title band, as a program's UIKit would draw one)
	{
		int fw = 0, fh = 0;
		unsigned *f = el_core_window_frame (id, k, &fw, &fh);
		for (int y = 0; f != 0 && y < fh; y++)
			for (int x = 0; x < fw; x++) f[(size_t) y * fw + x] = y < KAPI_FRAME_TITLE_H ? (k == 0 ? 0x00E8E8EC : 0x00D0D0D4) : 0x00E2E2E4;
	}
	el_core_window_present (id);
}

// The pointer as serve.cpp gives it: the policy's hook first (PocketUI's viewport, P6), then the window manager.
static void Pointer (int x, int y, unsigned b, int wheel)
{
#ifdef WS_POLICY_HAS_POINTER
	if (g_pWsPolicy != 0 && g_pWsPolicy->pointer != 0 && g_pWsPolicy->pointer (x, y, b, wheel)) return;
#endif
	el_core_pointer (x, y, b, wheel);
}

extern "C" int sim_server_step (const char *st)
{
	Init ();
	char cmd[32] = "", arg[256] = "", arg2[64] = "";
	int a = 0, b = 0, c = 0;
	sscanf (st, "%31s", cmd);
	if (!strcmp (cmd, "down") || !strcmp (cmd, "rdown") || !strcmp (cmd, "up") || !strcmp (cmd, "rup") || !strcmp (cmd, "move"))
	{
		sscanf (st, "%*s %d %d", &a, &b);
		unsigned bit = cmd[0] == 'r' ? 2 : 1;
		if (!strcmp (cmd, "down") || !strcmp (cmd, "rdown")) s_nButtons |= bit;
		else if (strcmp (cmd, "move")) s_nButtons &= ~bit;
		Pointer (a, b, s_nButtons, 0);
		return 1;
	}
	if (!strcmp (cmd, "wheel")) { sscanf (st, "%*s %d %d %d", &a, &b, &c); Pointer (a, b, s_nButtons, c); return 1; }
	if (!strcmp (cmd, "key"))
	{
		sscanf (st, "%*s %255s", arg);
		long k = arg[1] ? strtol (arg, 0, 0) : arg[0];
		char keys[16];
		KeyString (k, keys);
		ws_route_key (keys, s_nMods);
		return 1;
	}
	if (!strcmp (cmd, "mods"))				// (8: Super -- the policy's only, as serve.cpp does)
	{
		sscanf (st, "%*s %d", &a); s_nMods = (unsigned) a; el_core_modifiers (s_nMods & 7);
#ifdef WS_POLICY_HAS_MODS
		if (g_pWsPolicy != 0 && g_pWsPolicy->mods != 0) g_pWsPolicy->mods (s_nMods);
#endif
		return 1;
	}
	if (!strcmp (cmd, "dump") && getenv ("SIM_APPLET")) return 0;	// (an applet: its surface, the stand-in kernel's dump)
	if (!strcmp (cmd, "dump")) { sscanf (st, "%*s %255s", arg); Dump (arg); return 1; }
	if (!strcmp (cmd, "screen")) { sscanf (st, "%*s %255s", arg); Dump (arg, false); return 1; }
	if (!strcmp (cmd, "otherpic"))				// otherpic W H T FILE SX SY [F [X Y]]: its canvas from a dump
	{
		unsigned f = 0;
		int x = -1, y = -1;
		char file[256] = "";
		sscanf (st, "%*s %d %d %255s %255s %d %d %i %d %d", &a, &b, arg, file, &s_nPicX, &s_nPicY, (int *) &f, &x, &y);
		s_Pic = file;
		Other (a, b, arg, f, x, y);
		return 1;
	}
	if (!strcmp (cmd, "othermenu"))				// othermenu SPEC ('|' between the lines): the latest other's menus
	{
		std::string m (st + 10);
		for (auto &c : m) if (c == '|') c = '\n'; else if (c == '~') c = '\t';
		long r[4] = { 0, 0, 0, 0 };
		unsigned len = 0;
		el_op (OTHER_PID + (unsigned) s_nOthers - 1, EL_OP_MENU_SET, r, (const unsigned char *) m.c_str (), (unsigned) m.size () + 1, s_Out, &len);
		return 1;
	}
	if (!strcmp (cmd, "owin"))				// owin W H T: a second window of the latest "other" program (a dialog)
	{
		unsigned pid = OTHER_PID + (unsigned) s_nOthers - 1;
		sscanf (st, "%*s %d %d %255s", &a, &b, arg);
		struct el_create C;
		memset (&C, 0, sizeof C);
		snprintf (C.title, sizeof C.title, "%s", arg);
		long r[4] = { -1, -1, a, b };
		unsigned len = 0;
		s_bOtherMade = el_op (pid, EL_OP_CREATE | (1 << EL_OP_WINDOW_SHIFT), r, (const unsigned char *) &C, sizeof C, s_Out, &len) == 1;
		int id = el_core_window_of_win (pid, 1);
		if (id >= 0) el_core_window_present (id);
		return 1;
	}
	if (!strcmp (cmd, "oclose"))				// the latest "other" program ended (its windows gone)
	{
		unsigned pid = OTHER_PID + (unsigned) s_nOthers - 1;
		int id;
		while ((id = el_core_window_of (pid)) >= 0) el_core_window_remove (id);
		el_core_program_gone (pid);
		s_Names[pid] = "";
		return 1;
	}
	if (!strcmp (cmd, "other"))
	{
		unsigned f = 0;
		int x = -1, y = -1;
		sscanf (st, "%*s %d %d %255s %i %d %d", &a, &b, arg, (int *) &f, &x, &y);
		Other (a, b, arg, f, x, y);
		return 1;
	}
	if (!strcmp (cmd, "place"))
	{
		sscanf (st, "%*s %d %d", &a, &b);
		long r[4] = { a, b, 0, 0 };
		unsigned len = 0;
		el_op (OTHER_PID + (unsigned) s_nOthers - 1, EL_OP_MOVE, r, 0, 0, s_Out, &len);
		return 1;
	}
	if (!strcmp (cmd, "raise"))
	{
		long r[4] = { 0, 0, 0, 0 };
		unsigned len = 0;
		el_op (OTHER_PID + (unsigned) s_nOthers - 1, EL_OP_WIN_RAISE, r, 0, 0, s_Out, &len);
		return 1;
	}
	if (!strcmp (cmd, "expect")) { sscanf (st, "%*s %255s %63s", arg, arg2); Expect (arg, arg2); return 1; }
	if (!strcmp (cmd, "exit") || !strcmp (cmd, "quit"))
	{
		if (s_nPass + s_nFail > 0) fprintf (stderr, "server_sim: %d passed, %d failed\n", s_nPass, s_nFail);
		if (s_nFail > 0) exit (1);
		return 0;					// (fakekapi's: the end)
	}
	return 0;
}
