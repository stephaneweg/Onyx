//
// route.cpp -- the routing every graphics server shares (policy.h): the keys to the server's policy, then to
// the window that has them; the events the window manager queued for the programs' windows put in the
// programs' queues (the kernel's); the windows' places kept in the kernel for a server started again; the
// program that has the keys told to the kernel. It was Elegant's server.cpp until 2026-10-08 (PocketUI's
// phase P3: user/Servers/common/), the behaviour unchanged.
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

unsigned g_nWsPosted;
static unsigned s_nExitAsked[EL_WINDOWS_MAX];	// the program (its pid) a closed window's exit was asked of
static unsigned s_nFocus = 0xFFFFFFFFu;

void ws_route_key (const char *keys, unsigned mods)
{
	if (g_pWsPolicy != 0 && g_pWsPolicy->key != 0 && g_pWsPolicy->key (keys, mods)) return;
	el_core_key (keys);
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
			k.handler = e.handler; k.value = e.value; k.event = e.event; k.mods = e.mods;
			k.sender = (unsigned long long) el_core_window_win (id);	// (v94) which of its windows (0: its first)
			long r = kws_post (pid, &k);
			if (r == 0) break;				// (its queue is full: later)
			if (r == 1) g_nWsPosted++;
			el_core_window_event_drop (id);			// (queued, or the program is gone)
		}
		int win = el_core_window_win (id);
		if (win > 0 && el_core_window_closing (id))	// (v94) a program's other window: told, it closes it
		{
			struct kapi_event k;
			kapi_memset (&k, 0, sizeof k);
			k.handler = el_core_window_pointer_handler (id); k.sender = (unsigned long long) win;
			k.event = GUI_EVENT_WINCTL; k.value = KAPI_FRAME_CLOSE;
			if (k.handler == 0 || kws_post (pid, &k) != 0) el_core_window_closing_clear (id);	// (full: at the next turn)
			continue;
		}
		if (el_core_window_closing (id) && s_nExitAsked[id] != pid)
		{
			kws_exit (pid);
			s_nExitAsked[id] = pid;
		}
	}
}

void ws_route_turn (unsigned self)
{
	if (g_pWsPolicy != 0 && g_pWsPolicy->tick != 0) g_pWsPolicy->tick (self);
	forward_events (self);
	el_core_save_states (self);				// (the windows' places, for a server started again)
	unsigned focus = el_core_focus_pid ();			// (kapi_key_held, the pads: who has the keys)
	if (g_pWsPolicy != 0 && g_pWsPolicy->focus != 0) focus = g_pWsPolicy->focus (focus);
	if (focus != s_nFocus) { s_nFocus = focus; kapi_ws_ctl (KAPI_WS_FOCUS, (long) focus, 0, 0); }
}
