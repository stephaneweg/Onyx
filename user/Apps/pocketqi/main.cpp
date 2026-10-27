//
// pocketqi/main.cpp -- PocketQI: Pocket, the family's desk assistant (the user's project PocketQI, github.com/
// stephaneweg/PocketQI, ported to Onyx). You type a sentence; an LLM (Mistral, Groq as the fallback, or a server of
// your own) decides what Pocket says and does; Pocket's face -- two big eyes -- shows its mood; small apps in Onyx
// BASIC, the skills (the agenda, the to-do list, the shopping list, the weather...), do the work. The window: the
// face on top, the conversation, the field to type in (and, while the conversation is empty, a few questions to try).
// Pocket's mind runs on a thread of its own (brain.h: the LLM, the outcomes, the skills -- skills.h -- and the
// network -- net.h); it tells this window what to show through kapi_post. Pocket speaks with Mistral's voices
// (voice.h), on a third thread: the brain goes on meanwhile, and waits for the voice only before listening.
// `pocketqi --tray`: Pocket in the background, its icon in the menu bar, its ear on the microphone -- "dis Pocket" --
// and its face in a small popup while it is spoken to (tray.h, listen.h). Not in this version: its drawings.
//
// Its files: SD:/apps/pocketqi.app/config.ini (the settings and the API keys: Pocket > Settings), etc/memoire.txt
// (what Pocket remembers), etc/cron.txt, etc/histoires.txt, apps/<skill>/ (the skills and their data), lib/galo.bi
// (the skills' library), prompt/<name>.txt (the system prompt's texts, changed without rebuilding).
//
// Keys: Enter sends, Page Up / Page Down scroll the conversation (Up / Down too when it has the focus), Esc skips a
// question Pocket asked, Ctrl+N a new conversation, Ctrl+Q quits. A pad: A sends, the D-pad moves
// between the field, the questions and the conversation.
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
#define ONYX_HOSTED_NEW 1		// (libstdc++'s operator new: the brain keeps its text in std::string and std::vector)
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "uikit/form.h"
#include "uikit/padhint.h"
#include "systemkit/systemkit.h"
#include "fontkit/uikitface.h"
#include "Apps/pocketqi/brain.h"
#include "Apps/pocketqi/voice.h"
#include "uikit/checkbox.h"
#include <stdio.h>
#include <string.h>

using namespace uikit;
using namespace pq;

enum { MIN_W = 420, MIN_H = 400 };

// ---- the state --------------------------------------------------------------------------------------------------
static Brain       g_brain;
static int         g_jobEv, g_ansEv;		// the brain's work to do; the person's answer to a question
static std::string g_job, g_answer;		// (written before the event is set: the brain reads them after it)
static volatile bool g_busy, g_quit;		// Pocket works on a sentence; the window closes
static bool        g_listening;			// Pocket waits for the answer to its question
static int         g_bgState = VISAGE_VEILLE;	// what Pocket does (the face), the speaking aside
static unsigned    g_speakUntil;		// ticks: the eyes bob while a sentence is "said" (no voice)
static volatile bool g_talking;			// the voice is heard (voice.h, the speaker's thread)
static std::string g_home;			// SD:apps/pocketqi.app

class FaceView;
class ChatView;
class Entry;
static Root     *g_root;
static FaceView *g_face;
static ChatView *g_chat;
static Entry    *g_entry;
static Button   *g_send;
static Label    *g_status;
static Button   *g_chips[3];
static bool      g_chipsShown = true;
static void layout ();
static void send_text (const char *t);
static void update_status ();

// ---- the face: PocketQI's two eyes (core/pocket_visage.c), drawn at ~25 images a second ----------------------------
class FaceView : public Widget
{
public:
	pocket_visage v;
	std::vector<uint32_t> img;
	unsigned last;
	FaceView (int l, int t, int w, int h) : Widget (l, t, w, h), last (kapi_get_ticks ())
	{
		pocket_visage_init (&v, kapi_get_ticks () * 2654435761u + 1);
		v.couleur = 0x38D6F5;				// (PocketQI's blue eyes on black)
		v.fond = 0x05080C;
	}
	void tick ()
	{
		unsigned now = kapi_get_ticks ();
		if (now - last < 4) return;			// (1/100 s ticks: 25 images a second at most)
		float dt = (float) (now - last) / 100.0f;
		last = now;
		pocket_visage_step (&v, dt > 0.25f ? 0.25f : dt);
		invalidate (true);
	}
	void onDraw () override
	{
		int w = canvas.w, h = canvas.h;
		if (w <= 0 || h <= 0) return;
		img.resize ((size_t) w * (size_t) h);
		pocket_visage_image_argb (&v, img.data (), w, h, w);
		for (int y = 0; y < h; y++)
		{
			unsigned *d = canvas.px + (size_t) y * (size_t) canvas.stride;
			const uint32_t *s = img.data () + (size_t) y * (size_t) w;
			for (int x = 0; x < w; x++) d[x] = s[x] & 0xFFFFFF;
		}
		// the rounded corners, in the window's face
		const int r = 12;
		unsigned bg = parent ? parent->bgColor () : C_BG;
		for (int y = 0; y < r; y++)
			for (int x = 0; x < r; x++)
			{
				int dx = r - x, dy = r - y;
				if (dx * dx + dy * dy <= r * r) continue;
				canvas.pixel (x, y, bg); canvas.pixel (w - 1 - x, y, bg);
				canvas.pixel (x, h - 1 - y, bg); canvas.pixel (w - 1 - x, h - 1 - y, bg);
			}
	}
};

// ---- the conversation -------------------------------------------------------------------------------------------
enum { E_USER, E_POCKET, E_NOTE, E_ERROR, E_GESTURE, E_INFO };
struct Line { int kind; std::string text; };

static unsigned mix (unsigned a, unsigned b, int t)	// t/256 of b over a
{
	unsigned r = (((a >> 16) & 255) * (unsigned) (256 - t) + ((b >> 16) & 255) * (unsigned) t) >> 8;
	unsigned g = (((a >> 8) & 255) * (unsigned) (256 - t) + ((b >> 8) & 255) * (unsigned) t) >> 8;
	unsigned bl = ((a & 255) * (unsigned) (256 - t) + (b & 255) * (unsigned) t) >> 8;
	return (r << 16) | (g << 8) | bl;
}

class ChatView : public Widget
{
public:
	std::vector<Line> lines;
	int scroll = 0, total = 0;			// px from the top; the content's height
	bool stick = true;				// follow the newest line
	UkBarDrag bar;
	enum { PAD = 10, GAP = 8, BPAD = 8, MAXL = 80 };
	ChatView (int l, int t, int w, int h) : Widget (l, t, w, h) { canFocus = true; }

	int lineH () { return uk_fh () + 2; }
	int bubbleW () { int w = (width - 2 * PAD - uk_scroll_gutter ()) * 4 / 5; return w < 120 ? 120 : w; }
	// the lines of an entry's text wrapped at w
	int wrap (const Line &e, int w, int *st, int *ln)
	{
		int n = uk_text_wrap (e.text.c_str (), (int) e.text.size (), w, MAXL, st, ln, 0, 0);
		return n < 1 ? 1 : n;
	}
	int entryH (const Line &e)
	{
		int st[MAXL], ln[MAXL];
		if (e.kind == E_USER || e.kind == E_POCKET) return wrap (e, bubbleW () - 2 * BPAD, st, ln) * lineH () + 2 * BPAD - 2;
		return wrap (e, width - 2 * PAD - uk_scroll_gutter () - 16, st, ln) * lineH ();
	}
	int measure ()
	{
		total = PAD;
		for (auto &e : lines) total += entryH (e) + GAP;
		return total;
	}
	void clampScroll ()
	{
		int most = total - height;
		if (most < 0) most = 0;
		if (stick || scroll > most) scroll = most;
		if (scroll < 0) scroll = 0;
		stick = scroll >= most;
	}
	void add (int kind, const std::string &t)
	{
		lines.push_back ({ kind, t });
		if (lines.size () > 400) lines.erase (lines.begin ());
		stick = true;
		invalidate (true);
	}
	void scrollBy (int dy) { measure (); scroll += dy; stick = false; clampScroll (); invalidate (true); }

	void onDraw () override
	{
		canvas.clear (C_FIELD);
		measure ();
		clampScroll ();
		int y = PAD - scroll, lh = lineH ();
		int bw = bubbleW (), gw = uk_scroll_gutter ();
		unsigned pocketBg = mix (C_FIELD, C_ACCENT, 34), userBg = C_ACCENT;
		for (auto &e : lines)
		{
			int h = entryH (e);
			if (y + h >= 0 && y < height)
			{
				int st[MAXL], ln[MAXL];
				if (e.kind == E_USER || e.kind == E_POCKET)
				{
					int n = wrap (e, bw - 2 * BPAD, st, ln), tw = 0;
					for (int i = 0; i < n; i++) { int k = uk_tw_n (e.text.c_str () + st[i], ln[i]); if (k > tw) tw = k; }
					int boxW = tw + 2 * BPAD, x = e.kind == E_USER ? width - PAD - gw - boxW : PAD;
					uk_rbox (canvas, x, y, boxW, h, 10, e.kind == E_USER ? userBg : pocketBg, e.kind == E_USER ? userBg : pocketBg);
					unsigned ink = e.kind == E_USER ? C_SEL_TEXT : C_FIELD_TEXT;
					for (int i = 0; i < n; i++)
					{
						std::string s = e.text.substr ((size_t) st[i], (size_t) ln[i]);
						uk_text (canvas, x + BPAD, y + BPAD - 1 + i * lh, s.c_str (), ink);
					}
				}
				else
				{
					int n = wrap (e, width - 2 * PAD - gw - 16, st, ln);
					unsigned ink = e.kind == E_ERROR ? 0xC0392B : C_DIS;
					for (int i = 0; i < n; i++)
					{
						std::string s = e.text.substr ((size_t) st[i], (size_t) ln[i]);
						int tw = uk_tw (s.c_str ());
						uk_text (canvas, (width - gw - tw) / 2, y + i * lh, s.c_str (), ink);
					}
				}
			}
			y += h + GAP;
		}
		UkThumb t = uk_thumb (total, height, scroll, height);
		if (t.show) uk_draw_vscroll (canvas, width - gw - (gw ? 0 : 4), 0, gw ? gw : 4, height, t, C_FIELD);
		if (hasFocus) uk_rline (canvas, 0, 0, width, height, 4, C_ACCENT);
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) br; (void) bm;
		long pos = scroll;
		int gw = uk_scroll_gutter () ? uk_scroll_gutter () : 6;
		if (bar.mouse (mx, my, bl, width - gw, gw, 0, height, total, height, &pos))
		{
			scroll = (int) pos; stick = false; clampScroll (); invalidate (true);
			return true;
		}
		if (wheel) { scrollBy (-wheel * 3 * lineH ()); return true; }
		return mx >= 0 && my >= 0 && mx < width && my < height;
	}
	bool onKey (long k) override
	{
		int most = total - height;
		if (k == KEY_UP && scroll > 0) { scrollBy (-3 * lineH ()); return true; }
		if (k == KEY_DOWN && scroll < most) { scrollBy (3 * lineH ()); return true; }
		if (k == KEY_PGUP) { scrollBy (-(height - 2 * lineH ())); return true; }
		if (k == KEY_PGDN) { scrollBy (height - 2 * lineH ()); return true; }
		if (k == KEY_HOME) { scroll = 0; stick = false; invalidate (true); return true; }
		if (k == KEY_END) { stick = true; invalidate (true); return true; }
		return false;
	}
	std::string transcript ()
	{
		std::string s;
		for (auto &e : lines)
			s += (e.kind == E_USER ? std::string ("> ") : e.kind == E_POCKET ? std::string ("Pocket: ") : std::string ("  (")) + e.text
			     + (e.kind == E_USER || e.kind == E_POCKET ? "\n" : ")\n");
		return s;
	}
};

// ---- the field ---------------------------------------------------------------------------------------------------
class Entry : public Textbox
{
public:
	Entry (int l, int t, int w, int h) : Textbox (l, t, w, h, "") { maxLen = 500; }
	bool onKey (long k) override
	{
		if (k == KEY_ENTER) { send_text (text); return true; }
		if (k == KEY_PGUP || k == KEY_PGDN) return g_chat->onKey (k);
		if (k == 27 && g_listening) { send_text (""); return true; }	// (Esc: no answer to Pocket's question)
		return Textbox::onKey (k);
	}
	void onDraw () override
	{
		Textbox::onDraw ();
		if (len () == 0 && !hasFocus)
		{
			const char *h = g_listening ? TR ("Your answer...") : TR ("Ask Pocket something...");
			uk_text (canvas, 8, (height - uk_fh ()) / 2, h, C_DIS);
		}
	}
	int len () const { return (int) strlen (text); }
};

// ---- the brain's messages, posted to the main thread -------------------------------------------------------------
enum { M_SAY, M_NOTE, M_STATE, M_MOOD, M_LISTEN, M_DONE, M_LOG, M_TALK, M_WAKE, M_HIDE, M_EARERR };
struct Msg { int type, a, b; std::string text; };

static bool g_tray;				// `pocketqi --tray`: Pocket in the background (tray.h)
static void tray_msg (Msg *m);
static bool tray_listen (std::string &answer, int seconds);
static void tray_apply (const Config &c);
enum { TRAY_MSG_RELOAD = 1, TRAY_MSG_QUIT = 2 };
#define TRAY_SERVICE "pocketqi.tray"

static void face_state (int e)
{
	if (!g_face) return;
	if (e != VISAGE_PARLE) g_bgState = e;
	bool speaking = g_talking || (int) (g_speakUntil - kapi_get_ticks ()) > 0;
	pocket_visage_etat (&g_face->v, speaking ? VISAGE_PARLE : g_bgState);
}

static const char *gesture_word (const std::string &g)
{
	if (g == "salue") return TR ("(Pocket waves)");
	if (g == "rire") return TR ("(Pocket laughs)");
	if (g == "danse") return TR ("(Pocket dances)");
	if (g == "assis") return TR ("(Pocket sits down)");
	if (g == "debout") return TR ("(Pocket stands up)");
	return TR ("(Pocket stops)");
}

static void on_msg (void *ctx, long)
{
	Msg *m = (Msg *) ctx;
	if (g_tray) { tray_msg (m); delete m; return; }
	switch (m->type)
	{
	case M_SAY:
		g_chat->add (E_POCKET, m->text);
		if (!m->a)				// (no voice: the eyes bob a moment, as long as the sentence)
		{
			g_speakUntil = kapi_get_ticks () + (unsigned) (40 + (m->text.size () * 4 > 460 ? 460 : m->text.size () * 4));
			face_state (VISAGE_PARLE);
		}
		break;
	case M_TALK: g_talking = m->a != 0; face_state (g_bgState); break;
	case M_NOTE:
		g_chat->add (m->a == NOTE_ERROR ? E_ERROR : m->a == NOTE_GESTURE ? E_GESTURE : m->a == NOTE_INFO ? E_INFO : E_NOTE,
			     m->a == NOTE_GESTURE ? std::string (gesture_word (m->text)) : m->a == NOTE_APP ? "\xE2\x96\xB8 " + m->text : m->text);
		break;
	case M_STATE: face_state (m->a); update_status (); break;
	case M_MOOD:
	{
		float va, ev;
		if (pocket_humeur_get_va (&va, &ev)) pocket_visage_humeur_va (&g_face->v, va, ev);
		else pocket_visage_humeur (&g_face->v, m->a, m->b);
		break;
	}
	case M_LISTEN:
		g_listening = m->a != 0;
		if (g_listening) ((Widget *) g_entry)->setFocus ();
		g_entry->invalidate (true);
		update_status ();
		break;
	case M_DONE:
		g_busy = false;
		update_status ();
		break;
	case M_LOG: printf ("pocketqi: %s\n", m->text.c_str ()); break;
	}
	delete m;
}

static void post (int type, int a, int b, const std::string &t)
{
	Msg *m = new Msg { type, a, b, t };
	for (int i = 0; i < 50 && kapi_post (on_msg, m, 0) < 0; i++) kapi_msleep (20);	// (the queue full: wait a little)
}

// ---- the voice: Pocket's sentences said one after the other on a thread of their own (voice.h) ---------------------
// The brain goes on while Pocket speaks (its skills, the next piece); it waits for the voice only before listening.
struct Speech { std::string text, lang; };
static std::vector<Speech> g_speech;
static int g_speechMx = -1, g_speechEv = -1;
static volatile int g_speechLeft;		// sentences queued or being said

static bool voice_on () { return g_brain.cfg.voice && !g_brain.cfg.mistralKey.empty (); }

static void speak (const std::string &t, const std::string &lang)
{
	kapi_mutex_lock (g_speechMx, 0xFFFFFFFF);
	g_speech.push_back ({ t, lang });
	g_speechLeft++;
	kapi_mutex_unlock (g_speechMx);
	kapi_event_set (g_speechEv);
}
static void speech_clear ()
{
	kapi_mutex_lock (g_speechMx, 0xFFFFFFFF);
	g_speechLeft -= (int) g_speech.size ();
	g_speech.clear ();
	kapi_mutex_unlock (g_speechMx);
}
static void speech_wait () { for (int i = 0; i < 6000 && g_speechLeft > 0 && !g_quit; i++) kapi_msleep (10); }

static void first_sound (void *) { post (M_TALK, 1, 0, ""); }

static int speaker (void *)
{
	bool warned = false;
	while (!g_quit)
	{
		kapi_event_wait (g_speechEv, 500);
		for (;;)
		{
			kapi_mutex_lock (g_speechMx, 0xFFFFFFFF);
			if (g_speech.empty ()) { kapi_mutex_unlock (g_speechMx); break; }
			Speech sp = g_speech.front ();
			g_speech.erase (g_speech.begin ());
			kapi_mutex_unlock (g_speechMx);
			Config c = g_brain.cfg;
			int intensity = 0;
			std::string v = voice_id (sp.lang, c.voiceFr, c.voiceEn, pocket_humeur_get (&intensity)), err;
			if (!v.empty () && !g_quit)
			{
				if (!voice_say (c.mistralKey, v, sp.text, first_sound, 0, err))
				{
					post (M_LOG, 0, 0, "voice: " + err);
					if (!warned) { warned = true; post (M_NOTE, NOTE_ERROR, 0, std::string (TR ("Pocket's voice did not work:")) + " " + err); }
				}
				else warned = false;
				post (M_TALK, 0, 0, "");
			}
			g_speechLeft--;
		}
	}
	return 0;
}

// what the brain asks of the window (on the brain's thread)
struct Bridge : BrainUi
{
	void say (const std::string &t, const std::string &lang) override
	{
		bool v = voice_on ();
		post (M_SAY, v ? 1 : 0, 0, t);
		if (v) speak (t, lang);
	}
	void note (int kind, const std::string &t) override { post (M_NOTE, kind, 0, t); }
	void state (int e) override { post (M_STATE, e, 0, ""); }
	bool listen (std::string &answer, int seconds) override
	{
		if (g_tray) return tray_listen (answer, seconds);
		speech_wait ();				// (the question heard before the answer is awaited)
		kapi_event_reset (g_ansEv);
		post (M_LISTEN, 1, 0, "");
		int r = kapi_event_wait (g_ansEv, (unsigned) seconds * 1000u);
		post (M_LISTEN, 0, 0, "");
		if (r != 0 || g_quit) return false;
		answer = g_answer;
		return !answer.empty ();
	}
	const char *tr (const char *en) override { return uk_tr (en); }
	const char *lang () override { return uk_lang (); }
	void log (const std::string &t) override { post (M_LOG, 0, 0, t); }
};
static Bridge g_bridge;

static void mood_sink (pocket_humeur h, int intensite, void *) { post (M_MOOD, (int) h, intensite, ""); }

// the brain's thread: a sentence when there is one, else the cron's tasks and the mood's way back to neutral
static int worker (void *)
{
	g_brain.start ();
	while (!g_quit)
	{
		int r = kapi_event_wait (g_jobEv, 500);
		if (g_quit) break;
		if (r == 0 && g_busy)
		{
			std::string s = g_job;
			g_brain.turn (s);
			post (M_DONE, 0, 0, "");
			continue;
		}
		pocket_humeur_tick ((unsigned long) kapi_get_ticks () * 10);
		if (!g_busy && g_brain.cronTick ()) post (M_DONE, 0, 0, "");
	}
	return 0;
}

// ---- the settings: SD:/apps/pocketqi.app/config.ini ---------------------------------------------------------------
static std::string cfg_path () { return g_home + "/config.ini"; }

static void cfg_load (Config &c)
{
	std::string t;
	if (!read_file (cfg_path (), t)) return;
	for (auto &l0 : split (t, '\n'))
	{
		std::string l = trim (l0);
		if (l.empty () || l[0] == '#' || l[0] == ';') continue;
		size_t eq = l.find ('=');
		if (eq == std::string::npos) continue;
		std::string k = lower (trim (l.substr (0, eq))), v = trim (l.substr (eq + 1));
		if (k == "provider") c.provider = lower (v);
		else if (k == "mistral_key") c.mistralKey = v;
		else if (k == "groq_key") c.groqKey = v;
		else if (k == "model") c.model = v;
		else if (k == "url") c.url = v;
		else if (k == "url_key") c.urlKey = v;
		else if (k == "character") c.character = atoi (v.c_str ());
		else if (k == "voice") c.voice = lower (v) != "off" && v != "0";
		else if (k == "voice_fr") c.voiceFr = v;
		else if (k == "voice_en") c.voiceEn = v;
		else if (k == "listen") c.listen = lower (v) == "on" || v == "1";
	}
}

static bool cfg_save (const Config &c)
{
	std::string t = "# PocketQI's settings (Pocket > Settings). It holds your API keys: keep it private.\n"
			"provider = " + c.provider + "\nmistral_key = " + c.mistralKey + "\ngroq_key = " + c.groqKey +
			"\nmodel = " + c.model + "\nurl = " + c.url + "\nurl_key = " + c.urlKey +
			"\ncharacter = " + num (c.character) + "\nvoice = " + (c.voice ? "on" : "off") +
			"\nvoice_fr = " + c.voiceFr + "\nvoice_en = " + c.voiceEn + "\nlisten = " + (c.listen ? "on" : "off") + "\n";
	return write_file (cfg_path (), t);
}

static bool busy_say ()
{
	if (!g_busy) return false;
	uk_messagebox ("PocketQI", TR ("Pocket is busy. Try again once it has answered."), MB_OK);
	return true;
}

static void open_settings ()
{
	if (busy_say ()) return;
	Config c = g_brain.cfg;
	static const char *const PROV[2] = { TRN ("Mistral"), TRN ("Groq") };
	static const char *const CHAR[5] = { TRN ("Serious"), TRN ("Calm"), TRN ("Warm"), TRN ("Playful"), TRN ("Mischievous") };
	const char *prov[2] = { TR (PROV[0]), TR (PROV[1]) }, *chr[5];
	for (int i = 0; i < 5; i++) chr[i] = TR (CHAR[i]);
	FormDialog f (TR ("Settings"), 460);
	Textbox *mk = new Textbox (0, 0, 280, 26, c.mistralKey.c_str ()); mk->password = true; mk->maxLen = 200;
	Textbox *gk = new Textbox (0, 0, 280, 26, c.groqKey.c_str ()); gk->password = true; gk->maxLen = 200;
	Dropdown *pv = new Dropdown (0, 0, 180, 26, prov, 2, c.provider == "groq" ? 1 : 0, 0);
	Textbox *md = new Textbox (0, 0, 280, 26, c.model.c_str ()); md->maxLen = 120;
	Textbox *ur = new Textbox (0, 0, 280, 26, c.url.c_str ()); ur->maxLen = 300;
	uk_set_input_type (ur, UK_IN_URL);
	Dropdown *ch = new Dropdown (0, 0, 180, 26, chr, 5, c.character < 0 || c.character > 4 ? 0 : c.character, 0);
	f.addSection (TR ("The language model"));
	f.addText (TR ("Pocket thinks with a large language model on the Internet. Get a free key at console.mistral.ai or console.groq.com."));
	f.addRow (TR ("Mistral key:"), mk);
	f.addRow (TR ("Groq key:"), gk);
	f.addRow (TR ("Ask first:"), pv);
	f.addRow (TR ("Model (optional):"), md);
	f.addRow (TR ("Server (optional):"), ur);
	f.addText (TR ("A server of your own: the address of an OpenAI-compatible chat/completions endpoint; it is then the only one asked."));
	f.addSection (TR ("Pocket"));
	f.addRow (TR ("Character:"), ch);
	f.addButton (TR ("Save"), UK_FB_DEFAULT, 1);
	f.addButton (TR ("Cancel"), UK_FB_CANCEL, 0);
	if (f.run () != 1) return;
	c.mistralKey = trim (mk->text); c.groqKey = trim (gk->text);
	c.provider = pv->sel == 1 ? "groq" : "mistral";
	c.model = trim (md->text); c.url = trim (ur->text);
	c.character = ch->sel;
	g_brain.cfg = c;
	if (!cfg_save (c)) uk_messagebox ("PocketQI", TR ("The settings could not be written on the card."), MB_OK);
	update_status ();
	int pid = kapi_ipc_lookup (TRAY_SERVICE);
	if (pid > 0) kapi_mailbox_send (pid, TRAY_MSG_RELOAD, "", 0);	// (Pocket in the background: the new keys)
}

// Pocket > Voice and Listening: Mistral's voices (voice.h), the listening in the background (tray.h)
static void open_voice ()
{
	if (busy_say ()) return;
	Config c = g_brain.cfg;
	// the voices: their names, a family "follows Pocket's mood"
	static std::vector<std::string> fv, ev;
	fv.clear (); ev.clear ();
	auto label = [] (const VoiceChoice &v) { return std::string (v.name) + (v.moods ? std::string (" \xE2\x80\x93 ") + TR ("follows Pocket's mood") : std::string ()); };
	int fs = 0, es = 0, nf = (int) (sizeof VOICES_FR / sizeof VOICES_FR[0]), ne = (int) (sizeof VOICES_EN / sizeof VOICES_EN[0]);
	for (int i = 0; i < nf; i++) { fv.push_back (label (VOICES_FR[i])); if (c.voiceFr == VOICES_FR[i].id) fs = i; }
	for (int i = 0; i < ne; i++) { ev.push_back (label (VOICES_EN[i])); if (c.voiceEn == VOICES_EN[i].id) es = i; }
	const char *fvp[8], *evp[16];
	for (int i = 0; i < nf; i++) fvp[i] = fv[i].c_str ();
	for (int i = 0; i < ne; i++) evp[i] = ev[i].c_str ();
	Checkbox *vo = new Checkbox (0, 0, 280, 24, TR ("Pocket speaks aloud"), c.voice, 0, C_FACE);
	Dropdown *vf = new Dropdown (0, 0, 280, 26, fvp, nf, fs, 0);
	Dropdown *ve = new Dropdown (0, 0, 280, 26, evp, ne, es, 0);
	FormDialog f (TR ("Voice and Listening"), 460);
	f.addSection (TR ("The voice"));
	f.addText (TR ("Pocket speaks with Mistral's voices (Voxtral): your Mistral key (Pocket > Settings) is needed."));
	f.addRow (0, vo);
	f.addRow (TR ("In French:"), vf);
	f.addRow (TR ("In English:"), ve);
	Checkbox *li = new Checkbox (0, 0, 380, 24, TR ("Listen for \"dis Pocket\" in the background"), c.listen, 0, C_FACE);
	f.addSection (TR ("Listening"));
	f.addText (TR ("Pocket waits in the menu bar; say \"dis Pocket\", then your question: its face shows at the bottom left. "
		       "Nothing leaves the device before \"dis Pocket\". A microphone is needed (the Sound applet)."));
	f.addRow (0, li);
	f.addButton (TR ("Save"), UK_FB_DEFAULT, 1);
	f.addButton (TR ("Cancel"), UK_FB_CANCEL, 0);
	if (f.run () != 1) return;
	c.voice = vo->checked;
	c.voiceFr = VOICES_FR[vf->sel].id;
	c.voiceEn = VOICES_EN[ve->sel].id;
	c.listen = li->checked;
	if (!c.voice) speech_clear ();
	g_brain.cfg = c;
	if (!cfg_save (c)) uk_messagebox ("PocketQI", TR ("The settings could not be written on the card."), MB_OK);
	tray_apply (c);
}

static void open_memory ()
{
	if (busy_say ()) return;
	FormDialog f (TR ("What Pocket Remembers"), 420);
	if (g_brain.mem.empty ()) f.addText (TR ("Nothing yet. Tell Pocket your name, your town, the names of your family: it remembers them here, on this device. The language model only ever sees their names in braces, never their values."));
	else
	{
		std::string t;
		for (auto &kv : g_brain.mem) t += kv.first + " = " + kv.second + "\n";
		f.addText (t.c_str ());
		f.addText (TR ("Kept on this device only: the language model never sees these values."));
	}
	if (!g_brain.mem.empty ()) f.addButton (TR ("Forget Everything"), UK_FB_DESTRUCTIVE, 2);
	f.addButton (TR ("Close"), UK_FB_DEFAULT, 1);
	if (f.run () == 2 && uk_messagebox ("PocketQI", TR ("Forget everything Pocket remembers?"), MB_YESNO) == 1)
	{
		g_brain.mem.clear ();
		g_brain.memSave ();
		g_chat->add (E_INFO, TR ("Pocket has forgotten everything."));
	}
}

static void open_skills ()
{
	if (busy_say ()) return;
	g_brain.skillsReload ();
	FormDialog f (TR ("Pocket's Skills"), 460);
	f.addText (TR ("Small apps in Onyx BASIC that Pocket runs when you ask (SD:/apps/pocketqi.app/apps). Their words are in French for now."));
	for (auto &s : g_brain.skills)
	{
		std::string t = (s.name.empty () ? s.id : s.name) + " -- " + s.description;
		f.addText (t.c_str ());
	}
	f.addButton (TR ("Close"), UK_FB_DEFAULT, 1);
	f.run ();
}

// ---- sending -------------------------------------------------------------------------------------------------------
static void chips_show (bool on)
{
	if (g_chipsShown == on) return;
	g_chipsShown = on;
	layout ();
}

static void send_text (const char *t0)
{
	std::string t = trim (t0 ? t0 : "");
	if (g_listening)
	{
		// the answer to Pocket's question (empty: none)
		if (!t.empty ()) g_chat->add (E_USER, t);
		g_answer = t;
		g_listening = false;
		g_entry->setText ("");
		kapi_event_set (g_ansEv);
		update_status ();
		return;
	}
	if (t.empty ()) return;
	if (g_busy) { g_status->setText (TR ("Pocket is still busy with your last sentence...")); return; }
	g_chat->add (E_USER, t);
	g_entry->setText ("");
	chips_show (false);
	g_job = t;
	g_busy = true;
	update_status ();
	kapi_event_set (g_jobEv);
}

static void update_status ()
{
	if (!g_status) return;
	const char *s = "";
	if (g_listening) s = TR ("Pocket is waiting for your answer (Esc: no answer).");
	else if (g_busy && g_bgState == VISAGE_REFLECHIT) s = TR ("Pocket is thinking...");
	else if (g_busy) s = TR ("Pocket is busy...");
	else if (!g_brain.haveKey ()) s = TR ("No API key yet: Pocket > Settings.");
	g_status->setText (s);
	g_send->disabled = g_busy && !g_listening;
	g_send->invalidate (true);
}

static void chip (Widget &w) { send_text (((Button &) w).text); }

// ---- the menus -----------------------------------------------------------------------------------------------------
static void m_new ()
{
	if (busy_say ()) return;
	g_brain.forget ();
	speech_clear ();
	g_chat->lines.clear ();
	g_chat->invalidate (true);
	pocket_humeur_set (HUMEUR_NEUTRE, 1);
	chips_show (true);
	((Widget *) g_entry)->setFocus ();
}
static void m_settings () { open_settings (); }
static void m_voice () { open_voice (); }
static void m_memory () { open_memory (); }
static void m_skills () { open_skills (); }
static void m_quit () { kapi_exit (0); }
static void m_copy_last ()
{
	for (size_t i = g_chat->lines.size (); i-- > 0;)
		if (g_chat->lines[i].kind == E_POCKET) { clip_set_text (g_chat->lines[i].text.c_str ()); return; }
}
static void m_copy_all () { clip_set_text (g_chat->transcript ().c_str ()); }
static void m_about ()
{
	uk_messagebox (TR ("About PocketQI"), TR ("PocketQI -- Pocket, the family's desk assistant, on Onyx.\n"
		       "A port of PocketQI (github.com/stephaneweg/PocketQI). MIT License.\n\n"
		       "The language model answers through Mistral or Groq; what Pocket remembers stays on this device."), MB_OK);
}

static Menu g_menu;
static void build_menu ()
{
	g_menu.menu (TR ("Pocket"));
	g_menu.item (TR ("New Conversation"), "^N", UK_CTRL ('N'), m_new);
	g_menu.item (TR ("What Pocket Remembers..."), "", 0, m_memory);
	g_menu.item (TR ("Skills..."), "", 0, m_skills);
	g_menu.item (TR ("Settings..."), "", 0, m_settings);
	g_menu.item (TR ("Voice and Listening..."), "", 0, m_voice);
	g_menu.separator ();
	g_menu.item (TR ("Quit"), "^Q", UK_CTRL ('Q'), m_quit);
	g_menu.menu (TR ("Edit"));
	g_menu.item (TR ("Copy Last Answer"), "", 0, m_copy_last);
	g_menu.item (TR ("Copy Conversation"), "", 0, m_copy_all);
	g_menu.menu (TR ("Help"));
	g_menu.item (TR ("About PocketQI"), "", 0, m_about);
	g_menu.publish ();
}

// ---- the layout ----------------------------------------------------------------------------------------------------
static int g_laidW, g_laidH;			// the size the window was laid out for

static void layout ()
{
	Root &r = *g_root;
	g_laidW = r.width; g_laidH = r.height;
	static const UkPadHint H[] = { { "A", TRN ("send") }, { "+", TRN ("move") }, { "[PgUp]", TRN ("scroll") } };
	UkPadHint hn[3];
	for (int i = 0; i < 3; i++) { hn[i].glyphs = H[i].glyphs; hn[i].word = TR (H[i].word); }
	int foot = uk_hint_line (&r, hn, 3);
	int W = r.width, Hh = r.height - foot, pad = uk_compact () ? 6 : 10;
	int fieldH = uk_metrics ().field > 24 ? uk_metrics ().field : 28, btnW = uk_tw (TR ("Send")) + 30;
	int faceH = Hh * 28 / 100;
	if (faceH < 90) faceH = 90;
	if (faceH > 220) faceH = 220;
	int statusH = uk_fh () + 4, chipsH = g_chipsShown ? fieldH + 6 : 0;
	int y = pad;
	g_face->left = pad; g_face->top = y; g_face->resizeTo (W - 2 * pad, faceH);
	y += faceH + pad;
	int chatH = Hh - y - pad - fieldH - statusH - chipsH - 4;
	g_chat->left = pad; g_chat->top = y; g_chat->resizeTo (W - 2 * pad, chatH < 40 ? 40 : chatH);
	y += (chatH < 40 ? 40 : chatH) + 2;
	g_status->left = pad + 2; g_status->top = y; g_status->resizeTo (W - 2 * pad - 4, statusH);
	y += statusH;
	if (g_chipsShown)
	{
		// the questions to try: as many as fit
		int x = pad;
		for (int i = 0; i < 3; i++)
		{
			int w = uk_tw (g_chips[i]->text) + 24;
			bool fits = x + w <= W - pad;
			g_chips[i]->hidden = !fits;
			if (fits) { g_chips[i]->left = x; g_chips[i]->top = y; g_chips[i]->resizeTo (w, fieldH); x += w + 6; }
		}
		y += chipsH;
	}
	else for (int i = 0; i < 3; i++) g_chips[i]->hidden = true;
	g_entry->left = pad; g_entry->top = y; g_entry->resizeTo (W - 3 * pad - btnW, fieldH);
	g_send->left = W - pad - btnW; g_send->top = y; g_send->resizeTo (btnW, fieldH);
	r.invalidate (true);
}

class PocketRoot : public Root
{
public:
	PocketRoot (int w, int h) : Root (w, h, "PocketQI") {}
	void onTick () override
	{
		if (width != g_laidW || height != g_laidH) layout ();	// (a size taken by a way that told nobody: PocketUI's fill)
		if (g_face) g_face->tick ();
		if (g_speakUntil && (int) (kapi_get_ticks () - g_speakUntil) >= 0) { g_speakUntil = 0; face_state (g_bgState); }
	}
	void onResized () override { layout (); }
	void onSizeClass (int) override { layout (); }
};

static void on_send (Widget &) { send_text (g_entry->text); }

#include "Apps/pocketqi/tray.h"

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);
	uk_lang_init ();
	char d[160] = "";
	kapi_app_dir (d, sizeof d);
	g_home = d;
	while (!g_home.empty () && g_home.back () == '/') g_home.pop_back ();
	if (g_home.empty ()) g_home = "SD:apps/pocketqi.app";

	g_brain.home = g_home;
	g_brain.ui = &g_bridge;
	cfg_load (g_brain.cfg);
	pocket_humeur_set_sink (mood_sink, 0);
	pocket_humeur_set_delai (12000);
	char args[64] = "";
	kapi_get_args (args, sizeof args);
	if (strstr (args, "--tray")) { g_tray = true; return tray_main (); }

	// the desktop: a window of its own size; pocket and console: the screen's (fitted to the work area below --
	// PocketUI fills a smaller one only later)
	int ww = 600, wh = 640, sw = 0, sh = 0;
	kapi_screen_size (&sw, &sh);
	if (uk_compact () && sw > 0 && sh > 0) { ww = sw; wh = sh; }
	PocketRoot root (ww, wh);
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	root.setResizable (true);
	root.setMinSize (MIN_W, MIN_H);

	g_face = new FaceView (10, 10, 580, 170);
	root.addChild (g_face);
	g_chat = new ChatView (10, 190, 580, 330);
	root.addChild (g_chat);
	g_status = new Label (12, 524, 576, 20, "", C_DIS, C_BG);
	root.addChild (g_status);
	static const char *const CHIPS[3] = { TRN ("What's the weather today?"), TRN ("What is on my agenda this week?"), TRN ("Tell me a short story") };
	for (int i = 0; i < 3; i++)
	{
		g_chips[i] = new Button (0, 0, 100, 28, TR (CHIPS[i]), chip);
		root.addChild (g_chips[i]);
	}
	g_entry = new Entry (10, 600, 480, 28);
	root.addChild (g_entry);
	g_send = new Button (500, 600, 90, 28, TR ("Send"), on_send);
	root.addChild (g_send);
	build_menu ();

	g_chat->add (E_POCKET, TR ("Hello! I am Pocket. Ask me a question, or tell me what to note in your agenda or your lists."));
	layout ();
	((Widget *) g_entry)->setFocus ();
	root.fitWorkArea ();
	layout ();

	g_jobEv = kapi_event_create (0, 0);
	g_ansEv = kapi_event_create (0, 0);
	g_speechMx = kapi_mutex_create ();
	g_speechEv = kapi_event_create (0, 0);
	int tid = kapi_thread_create (worker, 0, 1024 * 1024, "brain");
	int stid = kapi_thread_create (speaker, 0, 256 * 1024, "voice");
	if (tid < 0) g_chat->add (E_ERROR, TR ("Pocket could not start its thread."));
	update_status ();

	root.run ();

	g_quit = true;
	kapi_event_set (g_jobEv);
	kapi_event_set (g_ansEv);
	kapi_event_set (g_speechEv);
	speech_clear ();
	if (tid > 0) kapi_thread_join (tid, 3000, 0);
	if (stid > 0) kapi_thread_join (stid, 3000, 0);
	return 0;
}
