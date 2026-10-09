//
// serve.cpp -- a graphics server's loop, common to Elegant and PocketUI (policy.h): the display and the raw
// input taken from the kernel (kapi v89, kws.h), the programs' windows served (the requests: ops.cpp; the
// protocols: uikit/port/elegant.h, uikit/port/pocket.h). It was Elegant's server.cpp until 2026-10-08
// (PocketUI's phase P3), the behaviour unchanged: what differs between the servers is their policy's.
//
// One loop, one thread: the raw input to the window manager; the programs' requests answered; the
// events the window manager queued for a program's window put in that program's queue (the
// kernel's: route.cpp); what changed composed and sent to the display; then the wait.
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
#include "kws.h"
#include "core.h"
#include "policy.h"

static const char *Name (void)			{ return g_pWsPolicy != 0 && g_pWsPolicy->name != 0 ? g_pWsPolicy->name : "server"; }

// ---- the windows' shared pixels (core.h) --------------------------------------------------------
// A buffer's address here says its number (kern/layout.h: USER_WS_BASE + number * USER_WS_SLOT).

#define WS_BASE		0xB00000000ULL		// 44 GB (v94; 52 GB with 128 slots before)
#define WS_SLOT		0x4000000ULL		// 64 MB
#define WS_SLOTS	256

void *el_shared_alloc (unsigned pid, int part, unsigned long bytes)
{
	struct kapi_ws_buf b;
	kapi_memset (&b, 0, sizeof b);
	b.pid = pid; b.slot = part; b.bytes = bytes;
	b.flags = KAPI_WS_BUF_ADOPT;		// (what a server before this one left the program: its pixels kept)
	if (kws_buf_map (&b) != 0) return 0;
	return (void *) b.addr;
}

int el_shared_is (const void *p)
{
	unsigned long long a = (unsigned long long) p;
	return a >= WS_BASE && a < WS_BASE + WS_SLOTS * WS_SLOT;
}

void el_shared_free (void *p)
{
	if (el_shared_is (p)) kws_buf_free ((unsigned) (((unsigned long long) p - WS_BASE) / WS_SLOT) + 1);
}

// ---- the programs' requests ---------------------------------------------------------------------

static struct kapi_ws_req s_Req;		// (4 KB: not on the stack)
static unsigned char s_Out[KAPI_WS_DATA_MAX];

static unsigned s_nIn, s_nReq, s_nFrames, s_nStatAt;	// (stats; the events sent: route.cpp's g_nWsPosted)

int el_sys_attach (unsigned pid)
{
	return kws_attach (pid) == 0;
}

int el_sys_state (unsigned pid, void *bytes, int set)
{
	return kapi_ws_ctl (KAPI_WS_STATE, (long) pid, (long) bytes, set) == 0;
}

int el_sys_clients (unsigned *pids, int max)
{
	long n = kapi_ws_ctl (KAPI_WS_CLIENTS, (long) pids, max, 0);
	return n > 0 ? (int) n : 0;
}

int el_sys_name (unsigned pid, char *buf, unsigned cap)
{
	long n = kapi_ws_ctl (KAPI_WS_PROC_NAME, (long) pid, (long) buf, (long) cap);
	return n > 0 ? (int) n : 0;
}

int el_sys_screen_grab (unsigned *dst, int w, int h)		// (a full-screen program's frames: rdpd reads its window)
{
	return kapi_screen_grab (dst, w, h);
}

static int s_bLostDisplay = 0;

// Print Screen (the USB keyboards' report: usage 0x46), at its press: the Screenshot app captures the
// screen -- with Alt, the window that has the keyboard. The running one is told through its service,
// else the app is started with what to capture ("--now" / "--window <id>"). (It was the kernel's: it
// no longer knows the windows.)
static void print_screen (const char *held, unsigned mods)
{
	static int s_bHeld = 0;
	int now = 0;
	for (int k = 0; k < 6; k++) if ((unsigned char) held[k] == 0x46) now = 1;
	int pressed = now && !s_bHeld;
	s_bHeld = now;
	if (!pressed) return;
	unsigned id = (mods & 4) ? el_core_active_id () : 0;		// (4: Alt)
	char msg[32], args[32], d[12];
	int n = 0, a = 0, i = 0;
	if (id != 0)
	{
		const char *w = "window "; for (; *w; w++) msg[n++] = *w;
		const char *g = "--window "; for (; *g; g++) args[a++] = *g;
		do { d[i++] = (char) ('0' + id % 10); id /= 10; } while (id != 0);
		while (i > 0) { msg[n++] = d[--i]; args[a++] = d[i]; }
	}
	else
	{
		const char *w = "now"; for (; *w; w++) msg[n++] = *w;
		const char *g = "--now"; for (; *g; g++) args[a++] = *g;
	}
	msg[n] = 0; args[a] = 0;
	int pid = kapi_ipc_lookup ("screenshot");
	if (pid <= 0 || !kapi_mailbox_send (pid, 1, msg, (unsigned) n + 1))
		kapi_exec_as ("SD:/apps/screenshot.app/main", args, "screenshot");
}

// F10, F11, F12 on a USB keyboard: Circle's key map gives them no string (its table: 0), so the cooked keys never
// carry them -- the shells' menu (F10 in the console's home), the emulators' full screen (F11) and speed (F12) only
// worked from a remote keyboard (2026-10-09). From the raw keys: a key newly held -> its xterm string, as typed.
static void function_keys (const char *held, unsigned mods)
{
	static const struct { unsigned char hid; const char *s; } FK[] = { { 0x43, "\x1b[21~" }, { 0x44, "\x1b[23~" }, { 0x45, "\x1b[24~" } };
	static unsigned s_was;
	unsigned now = 0;
	for (int k = 0; k < 6; k++)
		for (unsigned f = 0; f < sizeof FK / sizeof FK[0]; f++) if ((unsigned char) held[k] == FK[f].hid) now |= 1u << f;
	unsigned pressed = now & ~s_was;
	s_was = now;
	for (unsigned f = 0; f < sizeof FK / sizeof FK[0]; f++) if (pressed & (1u << f)) ws_route_key (FK[f].s, mods);
}

// The wheel's speed the Theme applet saved (SD:/etc/theme.txt: "wheelspeed=N").
static void wheel_speed (void)
{
	static char buf[4096];
	void *f = kapi_open ("SD:/etc/theme.txt");
	if (f == 0) return;
	int n = kapi_read (f, buf, sizeof buf - 1);
	kapi_close (f);
	if (n <= 0) return;
	buf[n] = 0;
	const char *key = "wheelspeed=";
	for (int i = 0; buf[i]; i++)
	{
		int k = 0;
		while (key[k] && buf[i + k] == key[k]) k++;
		if (key[k]) continue;
		int v = 0;
		for (const char *p = buf + i + k; *p >= '0' && *p <= '9'; p++) v = v * 10 + (*p - '0');
		el_core_wheel (v);
		return;
	}
}

// -> 0 shown; else not (a full-screen program has the display: the kernel shows its buffer).
static long present (unsigned *screen, int stride, int x, int y, int w, int h)
{
	struct kapi_ws_present p;
	kapi_memset (&p, 0, sizeof p);
	p.pixels = screen; p.stride = stride; p.x = x; p.y = y; p.w = w; p.h = h;
	long r = kws_present (&p);
	if (r == -KAPI_EPERM) s_bLostDisplay = 1;	// (the kernel took the display back: this server ends, another is started)
	return r;
}

static void say (const char *s) { ax_puts (s); }
static void say2 (const char *s)			// "<server>: ..."
{
	char line[128];
	int n = 0;
	for (const char *p = Name (); *p && n < 32; p++) line[n++] = *p;
	for (; *s && n < (int) sizeof line - 1; s++) line[n++] = *s;
	line[n] = 0;
	ax_puts (line);
}

// What the server did, one line every 5 s while something happens (the kernel's log: kmsg).

static void put_num (char *&p, const char *label, unsigned v)
{
	while (*label) *p++ = *label++;
	char d[12]; int i = 0;
	do { d[i++] = (char) ('0' + v % 10); v /= 10; } while (v != 0);
	while (i > 0) *p++ = d[--i];
}

static void stats (void)
{
	unsigned now = kapi_get_ticks ();
	if (now - s_nStatAt < 500) return;
	s_nStatAt = now;
	el_core_shot_tick ();
	if (s_nIn + s_nReq + g_nWsPosted + s_nFrames == 0) return;
	char line[112], *p = line;
	for (const char *n = Name (); *n && p < line + 16; n++) *p++ = *n;
	put_num (p, " 5s: input ", s_nIn); put_num (p, ", requests ", s_nReq);
	put_num (p, ", events sent ", g_nWsPosted); put_num (p, ", frames ", s_nFrames);
	*p++ = '\n'; *p = 0;
	say (line);
	s_nIn = s_nReq = g_nWsPosted = s_nFrames = 0;
}

int el_serve (int demo, int restart)
{
	long r = kws_register ();
	if (r != 1)
	{
		say2 (r == 0 ? ": another graphics server runs\n" : ": this kernel has no graphics server role (kapi v89)\n");
		return 1;
	}
	if (g_pWsPolicy != 0 && g_pWsPolicy->registered != 0) g_pWsPolicy->registered (restart);	// (before the display: init's programs come after it)
	kapi_thread_priority (0, 1);		// what the pointer does is shown at once: before the programs' turns
	struct kapi_ws_display D;
	long taken = kws_display (1, &D);
	for (int t = 0; taken != 0 && restart && t < 600; t++)	// (started again under a full-screen program: when it ends)
	{
		kapi_msleep (500);
		taken = kws_display (1, &D);
	}
	if (taken != 0) { say2 (": the display is busy (a full-screen program)\n"); return 1; }
	int w = D.w, h = D.h;
	unsigned self = (unsigned) kapi_getpid (0);
	unsigned *screen = new unsigned[(unsigned long) w * h];
	int nDemo = 0, ok = screen != 0;
	if (ok && demo) { nDemo = g_pWsPolicy != 0 && g_pWsPolicy->demo_scene != 0 ? g_pWsPolicy->demo_scene (w, h) : 0; ok = nDemo > 0; }
	else if (ok) ok = el_core_start (w, h);
	if (!ok) { kws_display (0, 0); say2 (": no memory\n"); return 1; }

	if (restart) el_core_restore_all ();			// the programs' windows, as the server before had them
	if (g_pWsPolicy != 0 && g_pWsPolicy->start != 0) g_pWsPolicy->start (w, h, restart);	// (Elegant: the wallpaper painted again)
	wheel_speed ();
	unsigned mods = 0;
	unsigned nStart = kapi_get_ticks ();
	int quit = 0;
	int rects[EL_RECTS_MAX * 4];
	unsigned frame_us = kapi_clock_us () - 16000u;
	while (!quit)
	{
		if (demo && (nDemo <= 0 || kapi_get_ticks () - nStart >= 60 * 100)) break;

		struct kapi_ws_input in[32];
		long n;
		while ((n = kws_input (in, 32)) > 0)
			for (long i = 0; i < n; i++)
			{
				s_nIn++;
				switch (in[i].type)
				{
				case KAPI_WS_IN_POINTER:
					if (g_pWsPolicy == 0 || g_pWsPolicy->pointer == 0
					    || !g_pWsPolicy->pointer (in[i].x, in[i].y, in[i].buttons, in[i].a))
						el_core_pointer (in[i].x, in[i].y, in[i].buttons, in[i].a);
					break;
				case KAPI_WS_IN_KEY:
					if (demo && in[i].keys[0] == 27 && in[i].keys[1] == 0) quit = 1;
					else ws_route_key (in[i].keys, mods);
					break;
				case KAPI_WS_IN_MODS:			// (the window manager: Ctrl, Shift, Alt -- the policy: Super too)
					mods = (unsigned) in[i].a;
					el_core_modifiers (mods & ~(unsigned) KAPI_WS_MOD_SUPER);
					if (g_pWsPolicy != 0 && g_pWsPolicy->mods != 0) g_pWsPolicy->mods (mods);
					break;
				case KAPI_WS_IN_HELD_USB:
					print_screen (in[i].keys, mods);
					function_keys (in[i].keys, mods);
					break;
				case KAPI_WS_IN_KICK:
					el_core_window_present (el_core_window_of_win ((unsigned) in[i].a, in[i].x));
					break;
				case KAPI_WS_IN_SCREEN:
					if (in[i].x > 0 && in[i].y > 0 && (in[i].x != w || in[i].y != h))
					{
						unsigned *bigger = new unsigned[(unsigned long) in[i].x * in[i].y];
						delete [] screen;
						screen = bigger; w = in[i].x; h = in[i].y;
						el_core_screen (w, h);
						if (g_pWsPolicy != 0 && g_pWsPolicy->screen != 0) g_pWsPolicy->screen (w, h);
					}
					break;
				case KAPI_WS_IN_FULLSCREEN:
					el_core_fullscreen ((unsigned) in[i].a, (int) in[i].buttons);
					break;
				case KAPI_WS_IN_QUIT:			// (v97) the kernel switches the server: end
					quit = 1;
					break;
				case KAPI_WS_IN_GONE:
					{
						int id;
						while ((id = el_core_window_of ((unsigned) in[i].a)) >= 0) el_core_window_remove (id);
						el_core_program_gone ((unsigned) in[i].a);
					}
					break;
				}
			}
		if (n < 0) break;					// (the display was taken back)

		while (kws_next (&s_Req) == 1)
		{
			unsigned len = 0;
			long st = el_op (s_Req.pid, s_Req.op, s_Req.a, s_Req.data, s_Req.in_len, s_Out, &len);
			s_nReq++;
			kws_reply (s_Req.id, st, s_Out, len);
		}

		if (demo && g_pWsPolicy != 0 && g_pWsPolicy->demo_closed != 0) nDemo -= g_pWsPolicy->demo_closed (self);
		ws_route_turn (self);					// (the events to the programs: route.cpp)

		// One frame every 16 ms at most: what the programs changed meanwhile (each of them presents
		// by itself, often 60 times a second) is composed and sent together, not one by one.
		unsigned now = kapi_clock_us ();
		if (now - frame_us < 16000u)
		{
			stats ();
			kws_wait ((16000u - (now - frame_us)) / 1000u + 1);
			continue;
		}
		int k = el_core_compose (screen, w, h, rects);
		int drew = k != 0;
		if (drew) frame_us = now;
		long shown = 0;
		if (k < 0) shown = present (screen, w, 0, 0, 0, 0);
		if (k > 1)		// several rectangles that nearly make one (a window dragged: where it was, where it is): one send
		{
			int x0 = rects[0], y0 = rects[1], x1 = x0 + rects[2], y1 = y0 + rects[3];
			long sum = 0;
			for (int i = 0; i < k; i++)
			{
				int *r = &rects[i * 4];
				if (r[0] < x0) x0 = r[0];
				if (r[1] < y0) y0 = r[1];
				if (r[0] + r[2] > x1) x1 = r[0] + r[2];
				if (r[1] + r[3] > y1) y1 = r[1] + r[3];
				sum += (long) r[2] * r[3];
			}
			if ((long) (x1 - x0) * (y1 - y0) <= sum + sum / 2)
			{
				shown = present (screen, w, x0, y0, x1 - x0, y1 - y0);
				k = 0;
			}
		}
		for (int i = 0; i < k; i++) shown |= present (screen, w, rects[i * 4], rects[i * 4 + 1], rects[i * 4 + 2], rects[i * 4 + 3]);
		if (shown != 0) { el_core_redraw (); kws_wait (20); }	// (not shown: the whole screen at the next turn)
		if (s_bLostDisplay) break;
		if (drew) s_nFrames++;
		stats ();
		if (!drew) kws_wait (100);
	}
	kws_display (0, 0);
	return 0;
}
