//
// server.cpp -- Elegant as the graphics server: the display and the raw input taken from the
// kernel (kapi v89, kws.h), the programs' windows served (the protocol: appkit/elegant.h).
//
// One loop, one thread: the raw input to the window manager; the programs' requests answered; the
// events the window manager queued for a program's window put in that program's queue (the
// kernel's); what changed composed and sent to the display; then the wait.
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
#include "appkit/elegant.h"
#include "core.h"

int el_demo_scene (int w, int h);		// (main.cpp) the demonstration's windows -> how many
int el_demo_closed (unsigned self);		// ... those closed since the last call, removed

// ---- the windows' shared pixels (core.h) --------------------------------------------------------
// A buffer's address here says its number (kern/layout.h: USER_WS_BASE + number * USER_WS_SLOT).

#define WS_BASE		0xD00000000ULL		// 52 GB
#define WS_SLOT		0x4000000ULL		// 64 MB
#define WS_SLOTS	128

void *el_shared_alloc (unsigned pid, int part, unsigned long bytes)
{
	struct kapi_ws_buf b;
	kapi_memset (&b, 0, sizeof b);
	b.pid = pid; b.slot = part; b.bytes = bytes;
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
static unsigned s_nExitAsked[EL_WINDOWS_MAX];	// the program (its pid) a closed window's exit was asked of

static unsigned s_nFocus = 0xFFFFFFFFu;
static unsigned s_nIn, s_nReq, s_nPosted, s_nFrames, s_nStatAt;	// (stats)

int el_sys_attach (unsigned pid)
{
	return kws_attach (pid) == 0;
}

int el_sys_name (unsigned pid, char *buf, unsigned cap)
{
	long n = kapi_ws_ctl (KAPI_WS_PROC_NAME, (long) pid, (long) buf, (long) cap);
	return n > 0 ? (int) n : 0;
}

// The events the window manager queued for the programs' windows -> the programs' queues; a
// program whose window was closed is told to end (once).
static void forward_events (unsigned self)
{
	for (int id = 0; id < EL_WINDOWS_MAX; id++)
	{
		unsigned pid = el_core_window_pid (id);
		if (pid == 0 || pid == self) continue;
		struct el_core_event e;
		while (el_core_window_event_peek (id, &e))
		{
			struct kapi_event k;
			kapi_memset (&k, 0, sizeof k);
			k.handler = e.handler; k.sender = e.sender; k.value = e.value; k.event = e.event; k.mods = e.mods;
			long r = kws_post (pid, &k);
			if (r == 0) break;				// (its queue is full: later)
			if (r == 1) s_nPosted++;
			el_core_window_event_drop (id);			// (queued, or the program is gone)
		}
		if (el_core_window_closing (id) && s_nExitAsked[id] != pid)
		{
			kws_exit (pid);
			s_nExitAsked[id] = pid;
		}
	}
}

static void present (unsigned *screen, int stride, int x, int y, int w, int h)
{
	struct kapi_ws_present p;
	kapi_memset (&p, 0, sizeof p);
	p.pixels = screen; p.stride = stride; p.x = x; p.y = y; p.w = w; p.h = h;
	kws_present (&p);
}

static void say (const char *s) { ax_puts (s); }

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
	if (s_nIn + s_nReq + s_nPosted + s_nFrames == 0) return;
	char line[96], *p = line;
	put_num (p, "elegant 5s: input ", s_nIn); put_num (p, ", requests ", s_nReq);
	put_num (p, ", events sent ", s_nPosted); put_num (p, ", frames ", s_nFrames);
	*p++ = '\n'; *p = 0;
	say (line);
	s_nIn = s_nReq = s_nPosted = s_nFrames = 0;
}

int el_serve (int demo)
{
	long r = kws_register ();
	if (r != 1)
	{
		say (r == 0 ? "elegant: another graphics server runs\n" : "elegant: this kernel has no graphics server role (kapi v89)\n");
		return 1;
	}
	struct kapi_ws_display D;
	if (kws_display (1, &D) != 0) { say ("elegant: the display is busy (a full-screen program)\n"); return 1; }
	int w = D.w, h = D.h;
	unsigned self = (unsigned) kapi_getpid (0);
	unsigned *screen = new unsigned[(unsigned long) w * h];
	int nDemo = 0, ok = screen != 0;
	if (ok && demo) { nDemo = el_demo_scene (w, h); ok = nDemo > 0; }
	else if (ok) ok = el_core_start (w, h);
	if (!ok) { kws_display (0, 0); say ("elegant: no memory\n"); return 1; }

	unsigned nStart = kapi_get_ticks ();
	int quit = 0;
	int rects[EL_RECTS_MAX * 4];
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
					el_core_pointer (in[i].x, in[i].y, in[i].buttons, in[i].a);
					break;
				case KAPI_WS_IN_KEY:
					if (demo && in[i].keys[0] == 27 && in[i].keys[1] == 0) quit = 1;
					else el_core_key (in[i].keys);
					break;
				case KAPI_WS_IN_MODS:
					el_core_modifiers ((unsigned) in[i].a);
					break;
				case KAPI_WS_IN_KICK:
					el_core_window_present (el_core_window_of ((unsigned) in[i].a));
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

		if (demo) nDemo -= el_demo_closed (self);
		forward_events (self);
		unsigned focus = el_core_focus_pid ();			// (kapi_key_held, the pads: who has the keys)
		if (focus != s_nFocus) { s_nFocus = focus; kapi_ws_ctl (KAPI_WS_FOCUS, (long) focus, 0, 0); }

		int k = el_core_compose (screen, w, h, rects);
		if (k < 0) present (screen, w, 0, 0, 0, 0);
		for (int i = 0; i < k; i++) present (screen, w, rects[i * 4], rects[i * 4 + 1], rects[i * 4 + 2], rects[i * 4 + 3]);
		if (k != 0) s_nFrames++;
		stats ();
		if (k == 0) kws_wait (100);
	}
	kws_display (0, 0);
	return 0;
}
