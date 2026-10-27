//
// pocketqi/tray.h -- PocketQI on Onyx: Pocket in the background, `pocketqi --tray` (included by main.cpp, whose brain,
// voice and messages it shares). Its icon in the menu bar's status area (uk_tray); the microphone always open on its
// ear (listen.h: "dis Pocket" spotted on the device, nothing sent before); when it is heard, a small popup at the
// bottom left of the screen -- Pocket's face alone -- while the sentence is said, sent to Voxtral, answered (the brain
// and the voice of the window); Pocket's question waits for a spoken answer; after the answer, 8 s to speak again
// without "dis Pocket"; then Pocket rests and the popup goes. While Pocket speaks or thinks, the microphone's sound is
// dropped (Pocket does not hear itself).
// The icon: a double click opens PocketQI's window; a right click its menu (in the popup): open, pause / listen
// again, stop. Started at every boot when Pocket > Settings says "listen in the background": the line
// `run pocketqi --tray` in SD:/etc/session/desktop and pocket (the interface's programs: restarted under the new
// graphics server when the mode changes); with the setting off, it ends at once. One at a time ("pocketqi.tray");
// the window tells it to read its settings again (TRAY_MSG_RELOAD) or to end (TRAY_MSG_QUIT).
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
#ifndef POCKETQI_TRAY_H
#define POCKETQI_TRAY_H

#include "Apps/pocketqi/listen.h"

enum { POP_W = 200, POP_H = 140, POP_MARGIN = 12 };
enum { EAR_ASLEEP, EAR_LISTEN, EAR_ANSWER, EAR_WAIT };	// the ear: "dis Pocket" awaited; a sentence; Pocket's answer; Pocket at work

static Root *g_pop;					// the popup (the program's first window: parked out of the screen)
static bool  g_popShown;
static volatile bool g_paused;				// the microphone closed by the menu
static volatile bool g_askPending;			// the brain waits for a spoken answer (Bridge::listen)
static volatile int  g_earMode = EAR_ASLEEP;
static std::string   g_earError;			// why the ear cannot listen ("": it listens)

static void tray_tip ()
{
	std::string tip = g_paused ? TR ("Pocket does not listen (paused)")
			: !g_earError.empty () ? std::string (TR ("Pocket cannot listen:")) + " " + g_earError
			: TR ("Pocket listens: say \"dis Pocket\"");
	uk_tray ((g_home + "/icon.bmp").c_str (), tip.c_str ());
}

static void pop_show (bool on)
{
	if (!g_pop || on == g_popShown) return;
	g_popShown = on;
	g_pop->winSelect ();
	struct kapi_win_geom g;
	int x = -POP_W - 50, y = 0;
	if (uk_win_geometry (&g) == 0 && g.aw > 0) y = g.ay + g.ah - POP_H - POP_MARGIN;
	else { int sw = 0, sh = 0; kapi_screen_size (&sw, &sh); y = sh - POP_H - 100; }
	if (on) x = (uk_win_geometry (&g) == 0 && g.aw > 0 ? g.ax : 0) + POP_MARGIN;
	uk_win_move (x, y);
	uk_win_alpha (on ? 255 : 0);
	if (on) { g_face->last = kapi_get_ticks (); g_pop->invalidate (true); }
}

// the window's messages (main.cpp's on_msg sends them here in the background)
static void tray_msg (Msg *m)
{
	switch (m->type)
	{
	case M_SAY:
		if (!m->a) { g_speakUntil = kapi_get_ticks () + (unsigned) (40 + (m->text.size () * 4 > 460 ? 460 : m->text.size () * 4)); face_state (VISAGE_PARLE); }
		break;
	case M_NOTE: if (m->a == NOTE_ERROR) printf ("pocketqi: %s\n", m->text.c_str ()); break;
	case M_STATE: face_state (m->a); break;
	case M_MOOD:
	{
		float va, ev;
		if (pocket_humeur_get_va (&va, &ev)) pocket_visage_humeur_va (&g_face->v, va, ev);
		else pocket_visage_humeur (&g_face->v, m->a, m->b);
		break;
	}
	case M_LISTEN: if (m->a) face_state (VISAGE_ECOUTE); break;
	case M_DONE: g_busy = false; break;
	case M_LOG: printf ("pocketqi: %s\n", m->text.c_str ()); break;
	case M_TALK: g_talking = m->a != 0; face_state (g_bgState); break;
	case M_WAKE: pop_show (true); face_state (VISAGE_ECOUTE); break;
	case M_HIDE: if (!g_busy && g_speechLeft <= 0) { pop_show (false); face_state (VISAGE_VEILLE); } break;
	case M_EARERR: g_earError = m->text; tray_tip (); break;
	}
}

// the brain's question: Pocket's sentence heard, then the ear listens for the answer (no "dis Pocket")
static bool tray_listen (std::string &answer, int seconds)
{
	speech_wait ();
	kapi_event_reset (g_ansEv);
	g_answer.clear ();
	g_askPending = true;
	post (M_LISTEN, 1, 0, "");
	int r = kapi_event_wait (g_ansEv, (unsigned) seconds * 1000u);
	g_askPending = false;
	post (M_LISTEN, 0, 0, "");
	if (r != 0 || g_quit) return false;
	answer = g_answer;
	return !answer.empty ();
}

// what is left of the wake word after pocket_ecoute_strip, when Voxtral heard it otherwise ("Deep Pocket, ...",
// "10 pockets, ..."): "pocket" as the first or the second word -> the text after it
static std::string wake_trim (const std::string &t)
{
	std::string l = lower (t);
	size_t p = l.find ("pocket");
	if (p == std::string::npos || p > 16) return t;
	int words = 0;
	for (size_t i = 0; i < p; i++) if (l[i] == ' ' && i > 0 && l[i - 1] != ' ') words++;
	if (words > 1) return t;
	size_t e = p + 6;
	if (e < l.size () && l[e] == 's') e++;
	while (e < l.size () && strchr (" ,.;:!?-", l[e])) e++;
	std::string r = t.substr (e);
	if (!r.empty () && (unsigned char) r[0] < 0x80) r[0] = (char) toupper ((unsigned char) r[0]);
	return r;
}

// ---- the ear's thread: the microphone, "dis Pocket", the sentence, Voxtral, the brain --------------------------------
static int ear_thread (void *)
{
	static Ear ear;
	if (!ear.start (g_home)) { post (M_EARERR, 0, 0, TR ("its speech model is missing")); post (M_LOG, 0, 0, "ear: " + ear.err); return 0; }
	pocket_ecoute &e = ear.e;
	short blk[GEC_BLOCK];
	int have = 0;
	bool open = false, deaf = false;
	std::string last;					// the last sentence heard ("au revoir": no more listening)
	bool woke = false;					// the sentence began with "dis Pocket" (not a sentence said again)
	while (!g_quit)
	{
		if (g_paused) { if (open) { ak_in_close (); open = false; } kapi_msleep (200); continue; }
		if (!open)
		{
			int r = ak_in_open (16000);
			if (r <= 0)
			{
				post (M_EARERR, 0, 0, r == 0 ? TR ("another program records") : TR ("no microphone (the Sound applet chooses one)"));
				for (int i = 0; i < 50 && !g_quit && !g_paused; i++) kapi_msleep (100);
				continue;
			}
			open = true; have = 0;
			post (M_EARERR, 0, 0, "");
		}
		int n = ak_in_read (blk + have, GEC_BLOCK - have);
		if (n < 0) { ak_in_close (); open = false; continue; }		// (the device gone)
		if (n == 0) { kapi_msleep (8); continue; }
		have += n;
		if (have < GEC_BLOCK) continue;
		have = 0;
		// Pocket at work: what the microphone hears is dropped (Pocket speaking, the sentence being answered)
		if (g_earMode == EAR_WAIT)
		{
			if (g_askPending && g_speechLeft <= 0)
			{
				pocket_ecoute_reprise (&e); pocket_ecoute_relance (&e);
				g_earMode = EAR_ANSWER;
			}
			else if (!g_busy && g_speechLeft <= 0)
			{
				pocket_ecoute_reprise (&e);
				if (pocket_ecoute_fin_dite (last.c_str ())) { pocket_ecoute_veille (&e); g_earMode = EAR_ASLEEP; post (M_HIDE, 0, 0, ""); }
				else { pocket_ecoute_relance (&e); g_earMode = EAR_LISTEN; post (M_STATE, VISAGE_ECOUTE, 0, ""); }
			}
			deaf = true;
			continue;
		}
		if (g_speechLeft > 0) { deaf = true; continue; }		// (a sentence said by the window's own turn)
		if (deaf) { deaf = false; pocket_ecoute_reprise (&e); }
		int ev = pocket_ecoute_block (&e, blk);
		if (ev == GEC_REVEIL)
		{
			g_earMode = EAR_LISTEN;
			woke = true;
			pocket_humeur_set (HUMEUR_CURIEUX, 1);
			post (M_WAKE, 0, 0, "");
		}
		else if (ev == GEC_PHRASE_PRETE)
		{
			post (M_STATE, VISAGE_REFLECHIT, 0, "");
			std::string text, err;
			int st = voxtral_transcribe (g_brain.cfg.mistralKey, e.pcm, e.n, uk_lang (), text, err);
			if (st != 200) post (M_LOG, 0, 0, "voxtral: " + err);
			std::vector<char> t (text.begin (), text.end ());
			t.push_back (0);
			pocket_ecoute_strip (&t[0]);
			text = trim (std::string (&t[0]));
			if (woke) text = trim (wake_trim (text));
			woke = false;
			last = text;
			if (g_earMode == EAR_ANSWER)
			{
				g_answer = text;
				kapi_event_set (g_ansEv);
				g_earMode = EAR_WAIT;
			}
			else if (text.empty () || g_busy)
			{
				pocket_ecoute_veille (&e); g_earMode = EAR_ASLEEP;
				post (M_HIDE, 0, 0, "");
			}
			else
			{
				post (M_LOG, 0, 0, "heard: " + text);
				g_job = text;
				g_busy = true;
				g_earMode = EAR_WAIT;
				kapi_event_set (g_jobEv);
			}
		}
		else if (ev == GEC_FIN)
		{
			if (g_earMode == EAR_ANSWER) { g_answer.clear (); kapi_event_set (g_ansEv); g_earMode = EAR_WAIT; }
			else { pocket_ecoute_veille (&e); g_earMode = EAR_ASLEEP; post (M_HIDE, 0, 0, ""); }
		}
	}
	if (open) ak_in_close ();
	return 0;
}

// ---- the popup and the icon ------------------------------------------------------------------------------------------
enum { TM_OPEN = 1, TM_PAUSE, TM_STOP };

class TrayRoot : public Root
{
public:
	// (made away from the screen's edges: a topmost window on the top or the bottom edge is a bar -- PocketUI refuses
	// it, Elegant takes it for a dock)
	TrayRoot () : Root (-POP_W - 50, 120, POP_W, POP_H, "Pocket", WIN_FLAG_BORDERLESS | WIN_FLAG_TOPMOST | WIN_FLAG_SYSTEM | WIN_FLAG_ALPHA) {}
	void onTick () override
	{
		if (g_popShown && g_face) g_face->tick ();
		if (g_speakUntil && (int) (kapi_get_ticks () - g_speakUntil) >= 0) { g_speakUntil = 0; face_state (g_bgState); }
		int from = 0, type = 0;
		char b[64];
		while (kapi_mailbox_recv (&from, &type, b, sizeof b, 0) >= 0)
		{
			if (type == TRAY_MSG_RELOAD) { Config c; cfg_load (c); g_brain.cfg = c; if (!c.listen) kapi_exit (0); }
			else if (type == TRAY_MSG_QUIT) kapi_exit (0);
		}
	}
	void onTray (int kind) override
	{
		if (kind == KAPI_TRAY_OPEN) { lx_launch ("pocketqi", ""); return; }
		bool was = g_popShown;
		pop_show (true);
		PopupMenu m (8, 8);
		m.add (TR ("Open PocketQI"), TM_OPEN);
		m.add (g_paused ? TR ("Listen Again") : TR ("Pause Listening"), TM_PAUSE);
		m.separator ();
		m.add (TR ("Stop Listening in the Background"), TM_STOP);
		int r = m.run ();
		if (!was) pop_show (false);
		if (r == TM_OPEN) lx_launch ("pocketqi", "");
		else if (r == TM_PAUSE) { g_paused = !g_paused; tray_tip (); }
		else if (r == TM_STOP)
		{
			Config c = g_brain.cfg;
			c.listen = false;
			cfg_save (c);
			kapi_exit (0);
		}
	}
};

static int tray_main ()
{
	if (!g_brain.cfg.listen) return 0;			// (the setting off: the boot's line does nothing)
	if (!kapi_ipc_register (TRAY_SERVICE)) return 0;	// (one at a time)
	TrayRoot root;
	if (root.canvas.px == 0) return 1;
	g_pop = &root;
	g_root = &root;
	root.setBg (0xFF000000);				// (the face's rounded corners: see-through)
	g_face = new FaceView (0, 0, POP_W, POP_H);
	root.addChild (g_face);
	uk_win_alpha (0);
	tray_tip ();
	g_jobEv = kapi_event_create (0, 0);
	g_ansEv = kapi_event_create (0, 0);
	g_speechMx = kapi_mutex_create ();
	g_speechEv = kapi_event_create (0, 0);
	int tid = kapi_thread_create (worker, 0, 1024 * 1024, "brain");
	int stid = kapi_thread_create (speaker, 0, 256 * 1024, "voice");
	int etid = kapi_thread_create (ear_thread, 0, 1024 * 1024, "ear");
	root.run ();
	g_quit = true;
	kapi_event_set (g_jobEv); kapi_event_set (g_ansEv); kapi_event_set (g_speechEv);
	speech_clear ();
	if (tid > 0) kapi_thread_join (tid, 3000, 0);
	if (stid > 0) kapi_thread_join (stid, 3000, 0);
	if (etid > 0) kapi_thread_join (etid, 3000, 0);
	return 0;
}

// the window's settings: the background listening started (its boot line in the sessions' files) or stopped
static void tray_apply (const Config &c)
{
	int pid = kapi_ipc_lookup (TRAY_SERVICE);
	if (!c.listen) { if (pid > 0) kapi_mailbox_send (pid, TRAY_MSG_QUIT, "", 0); return; }
	static const char *const FILES[2] = { "SD:/etc/session/desktop", "SD:/etc/session/pocket" };
	for (const char *f : FILES)
	{
		std::string t;
		if (!read_file (f, t) || t.find ("run pocketqi --tray") != std::string::npos) continue;
		if (!t.empty () && t.back () != '\n') t += "\n";
		t += "# PocketQI: Pocket listens for \"dis Pocket\" in the background (Pocket > Settings)\nrun pocketqi --tray\n";
		write_file (f, t);
	}
	if (pid > 0) kapi_mailbox_send (pid, TRAY_MSG_RELOAD, "", 0);
	else lx_launch ("pocketqi", "--tray");
}

#endif
