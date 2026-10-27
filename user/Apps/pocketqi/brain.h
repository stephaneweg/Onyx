//
// pocketqi/brain.h -- PocketQI: Pocket's mind, what happens between a sentence typed and what Pocket does (PocketQI's
// pocketqi_win.cpp, its chat () and run_outcomes (), ported). It runs on a thread of its own (main.cpp's worker); it
// tells the window what to show through a BrainUi, which posts to the main thread.
//
//   sentence -> the LLM (Mistral, Groq as the fallback; or any OpenAI-compatible server: config.ini url =), with the
//   system prompt (core/groq_proto.c), the last exchanges and the skills' manifests -> {"outcomes": [...]} run in
//   order: say, humeur (the face), geste, retenir (the memory), wiki (Wikipedia, then a second turn), askinfo (a skill
//   run for the LLM, then a second turn), ask (a question to the person, then a second turn), next / reprendre (a
//   long content in pieces), maketext, cron (a skill's action run again later), and the skills (skills.h).
//
// The memory (<home>/etc/memoire.txt, "name = value") never leaves the device: the LLM only sees markers ({nom}),
// replaced back before Pocket speaks (groq_mask / groq_unmask). The conversation is forgotten after 10 minutes.
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
#ifndef POCKETQI_BRAIN_H
#define POCKETQI_BRAIN_H

#include "Apps/pocketqi/skills.h"
#include "Apps/pocketqi/core/pocket_visage.h"
#include "uikit/lang.h"		// (TRN: the words the window translates, marked for tools/lang/check.py)

namespace pq {

enum { NOTE_APP, NOTE_INFO, NOTE_ERROR, NOTE_GESTURE };

// What the brain asks of the window (main.cpp); every call comes from the brain's thread.
struct BrainUi
{
	virtual ~BrainUi () {}
	virtual void say (const std::string &utf8, const std::string &lang) = 0;	// Pocket's words (lang: "fr", "en"... the voice's)
	virtual void note (int kind, const std::string &utf8) = 0;		// a small line: a skill run, an error...
	virtual void state (int etat) = 0;					// VISAGE_VEILLE, _ECOUTE, _REFLECHIT, _PARLE
	virtual bool listen (std::string &answer, int seconds) = 0;		// the person's next sentence (false: none)
	virtual const char *tr (const char *english) = 0;			// the system's language (uk_tr)
	virtual const char *lang () = 0;					// "en", "fr"
	virtual void log (const std::string &utf8) = 0;			// a journal line (the console)
};

struct Config
{
	std::string provider = "mistral";		// the first one asked
	std::string mistralKey, groqKey, model, url, urlKey;
	int character = 0;				// groq_set_caractere: 0 serious .. 4 mischievous
	bool voice = true;				// Pocket speaks (Mistral's voice: voice.h)
	std::string voiceFr, voiceEn;			// its voice in French, in English ("": voice.h's default)
	bool listen = false;				// Pocket listens for "dis Pocket" in the background (tray.h)
};

class Brain : public SkillIo
{
public:
	std::string home;				// the app's data folder (the skills' SD:)
	Config cfg;
	BrainUi *ui = 0;

	// ---- SkillIo (a skill's words) ---------------------------------------------------------------------
	void say (const std::string &t) override { saidAll (t); }
	bool listen (std::string &a) override { ui->state (VISAGE_ECOUTE); bool r = ui->listen (a, 60); ui->state (VISAGE_REFLECHIT); return r; }
	void log (const std::string &t) override { ui->log ("[skill] " + t); }
	const char *lang () override { return ui->lang (); }

	// ---- the memory -------------------------------------------------------------------------------------
	std::vector<std::pair<std::string, std::string>> mem;
	std::string memFile () { return home + "/etc/memoire.txt"; }
	void memLoad ()
	{
		mem.clear ();
		std::string t;
		if (!read_file (memFile (), t)) return;
		for (auto &l0 : split (t, '\n'))
		{
			std::string l = l0;
			if (!l.empty () && l.back () == '\r') l.pop_back ();
			size_t eq = l.find (" = ");
			if (eq == std::string::npos || l[0] == '#') continue;
			mem.push_back ({ l.substr (0, eq), l.substr (eq + 3) });
		}
	}
	void memSave ()
	{
		make_dirs (home + "/etc");
		std::string t = "# Pocket's memory (PocketQI): name = value; never sent to the LLM (only {name})\n";
		for (auto &kv : mem) t += kv.first + " = " + kv.second + "\n";
		write_file (memFile (), t);
	}
	void memSet (const std::string &name, const std::string &value)
	{
		for (auto &kv : mem) if (kv.first == name) { kv.second = value; memSave (); return; }
		mem.push_back ({ name, value });
		memSave ();
	}

	enum { MAX_PRED = 64 };
	char heure[32], dateTxt[64];
	// the memory's values as the protocol's predicates, then {heure} and {date} (in words: the prompt is French)
	unsigned preds (groq_predicate *p)
	{
		static const char *const days[] = { "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi" };
		static const char *const months[] = { "janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août",
						      "septembre", "octobre", "novembre", "décembre" };
		Date t = now ();
		snprintf (heure, sizeof heure, "%d heures %02d", t.hh, t.mm);
		snprintf (dateTxt, sizeof dateTxt, "%s %d%s %s", days[week_day (day_number (t.y, t.m, t.d))], t.d, t.d == 1 ? "er" : "", months[t.m - 1]);
		unsigned n = 0;
		for (auto &kv : mem)
			if (n < MAX_PRED - 2 && !kv.second.empty ()) { p[n].name = kv.first.c_str (); p[n].value = kv.second.c_str (); n++; }
		p[n].name = "heure"; p[n].value = heure; n++;
		p[n].name = "date"; p[n].value = dateTxt; n++;
		return n;
	}

	// ---- the conversation (the last exchanges, in markers) ---------------------------------------------------
	struct Exchange { std::string user, pocket, lang, apps; };
	std::vector<Exchange> hist;
	std::string appsRun;				// the skills run in the answer being made (JSON)
	std::string said;				// what Pocket said in this answer
	std::string speakLang;				// the language of what Pocket says now (the answer's; a skill's own)
	long lastTurn = 0;
	void remember (const std::string &u, const std::string &g, const std::string &l, const std::string &a)
	{
		if (hist.size () == GROQ_MAX_HISTORY) hist.erase (hist.begin ());
		hist.push_back ({ u, g, l, a });
	}
	void forget () { hist.clear (); }

	// ---- the skills ------------------------------------------------------------------------------------------
	std::vector<Skill> skills;
	std::string skillsPrompt, skillsDirs;
	void skillsCheck ()
	{
		std::string d;
		for (auto &x : sub_dirs (home + "/apps")) d += x + "/";
		if (d == skillsDirs && !skills.empty ()) return;
		skillsDirs = d;
		skills = skills_scan (home);
		skillsPrompt = skills_prompt (skills);
	}
	void skillsReload () { skillsDirs.clear (); skills.clear (); skillsCheck (); }
	void noteApp (const groq_outcome &o)
	{
		std::string a = "{\"app\": " + json_str (o.app) + ", \"args\": [";
		for (unsigned k = 0; k < o.nargs; k++) a += (k ? ", " : "") + json_str (o.args[k]);
		appsRun += a + "]}, ";
	}

	// ---- saying -----------------------------------------------------------------------------------------------
	void saidAll (const std::string &t)
	{
		if (t.empty ()) return;
		ui->state (VISAGE_PARLE);
		ui->say (t, speakLang.empty () ? std::string (ui->lang ()) : speakLang);
		if (!said.empty ()) said += " ";
		said += t;
	}
	void sayTr (const char *english) { saidAll (ui->tr (english)); }

	// ---- the LLM --------------------------------------------------------------------------------------------
	// today in words, then a calendar to read rather than compute (LLMs get date arithmetic wrong)
	std::string todayTxt;
	const char *today ()
	{
		static const char *const days[] = { "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi" };
		static const char *const months[] = { "janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août",
						      "septembre", "octobre", "novembre", "décembre" };
		Date t = now ();
		long d0 = day_number (t.y, t.m, t.d);
		char b[200];
		snprintf (b, sizeof b, "%s %d %s %d (%s), %d h %02d %s (%02d:%02d)", days[week_day (d0)], t.d, months[t.m - 1], t.y,
			  iso (d0).c_str (), t.hh, t.mm, groq_moment (t.hh), t.hh, t.mm);
		todayTxt = b;
		todayTxt += ". Calendrier :";
		for (int k = 0; k <= 14; k++)
		{
			todayTxt += std::string (" ") + days[week_day (d0 + k)] + " " + iso (d0 + k);
			if (k == 0) todayTxt += " (aujourd'hui)"; else if (k == 1) todayTxt += " (demain)"; else if (k == 2) todayTxt += " (apres-demain)";
			todayTxt += k < 14 ? "," : "";
		}
		long monday = d0 - (week_day (d0) + 6) % 7;
		todayTxt += ". Cette semaine : du " + iso (monday) + " au " + iso (monday + 6) + " ; la semaine prochaine : du "
			    + iso (monday + 7) + " au " + iso (monday + 13)
			    + ". \"Mardi prochain\" est le premier mardi apres aujourd'hui. Prends les dates dans ce calendrier";
		return todayTxt.c_str ();
	}

	// the prompt's texts changed without rebuilding: <home>/prompt/<name>.txt (groq_set_lire_texte)
	static std::string &promptDir () { static std::string d; return d; }
	static size_t readPromptText (const char *name, char *buf, size_t cap)
	{
		std::string t;
		if (!read_file (promptDir () + "/" + name + ".txt", t)) return 0;
		size_t n = t.size () < cap ? t.size () : cap;
		memcpy (buf, t.data (), n);
		return n;
	}

	bool haveKey () const { return !cfg.url.empty () || !cfg.mistralKey.empty () || !cfg.groqKey.empty (); }

	// one turn with the LLM (the provider chosen, then the other one); true when r is filled
	bool ask (const char *text, const char *tname, const char *targ, const char *tres, groq_predicate *p, unsigned np,
		  groq_reply &r, char *sent, size_t nsent)
	{
		std::vector<groq_exchange> h (hist.size ());
		for (size_t i = 0; i < hist.size (); i++)
		{
			h[i].user = hist[i].user.c_str (); h[i].galo = hist[i].pocket.c_str ();
			h[i].langue = hist[i].lang.c_str (); h[i].apps = hist[i].apps.c_str ();
		}
		promptDir () = home + "/prompt";
		groq_set_lire_texte (readPromptText);
		groq_set_caractere (cfg.character);
		groq_set_ecran (0, 0, 0);			// (no drawing in this version: draw is not offered)
		static char *body;
		if (!body) body = new char[96 * 1024];
		// who is asked, in order: a server of one's own (url), else the provider chosen, then the other one
		struct Try { std::string name, url, key, model; };
		std::vector<Try> tries;
		if (!cfg.url.empty ()) tries.push_back ({ "server", cfg.url, cfg.urlKey, cfg.model.empty () ? "default" : cfg.model });
		else
		{
			const char *order[2] = { cfg.provider == "groq" ? "groq" : "mistral", cfg.provider == "groq" ? "mistral" : "groq" };
			for (int k = 0; k < 2; k++)
			{
				const groq_provider *pv = groq_provider_find (order[k]);
				const std::string &key = std::string (order[k]) == "groq" ? cfg.groqKey : cfg.mistralKey;
				if (pv && !key.empty ()) tries.push_back ({ pv->name, pv->url, key, k == 0 && !cfg.model.empty () ? cfg.model : pv->default_model });
			}
		}
		for (size_t k = 0; k < tries.size (); k++)
		{
			Try &t = tries[k];
			groq_request q = { t.model.c_str (), 0, p, np, h.data (), (unsigned) h.size (), text, 0, tname, targ, tres, today (),
					   skillsPrompt.empty () ? 0 : skillsPrompt.c_str () };
			size_t n = groq_build_request (&q, body, 96 * 1024, sent, nsent);
			if (!n) { ui->note (NOTE_ERROR, ui->tr (TRN ("The question is too long."))); return false; }
			if (k > 0) ui->log ("-> fallback: " + t.name);
			std::string req (body, n), resp, hdr = "Content-Type: application/json\r\n";
			if (!t.key.empty ()) hdr += "Authorization: Bearer " + t.key + "\r\n";
			unsigned t0 = kapi_get_ticks ();
			int st = http_request ("POST", t.url, hdr, &req, resp, 60000);
			if (st <= 0) { ui->log (t.name + ": no answer (" + num (st) + ")"); lastError = st == -3 ? 1 : 2; continue; }
			if (groq_parse_response (resp.c_str (), p, np, &r) && st == 200)
			{
				ui->log (t.name + " " + t.model + ": " + num ((long) (kapi_get_ticks () - t0) * 10) + " ms");
				return true;
			}
			ui->log (t.name + ": HTTP " + num (st) + ": " + r.erreur);
			lastError = st == 401 || st == 403 ? 3 : 2;
		}
		return false;
	}
	int lastError = 0;				// why the last ask failed: 1 no network, 2 no answer, 3 the key refused

	// ---- wiki -------------------------------------------------------------------------------------------------
	void wiki (const char *topic, std::string &out)
	{
		std::string host = std::string ("https://") + (strcmp (ui->lang (), "fr") == 0 ? "fr" : "en") + ".wikipedia.org", q, k, resp;
		url_encode (topic, q);
		static char key[256], *ex;
		if (!ex) ex = new char[4096];
		out = std::string ("rien trouve sur Wikipedia pour ") + topic;
		if (http_request ("GET", host + "/w/rest.php/v1/search/page?q=" + q + "&limit=1", "", 0, resp, 20000) != 200
		    || !groq_json_string (resp.c_str (), "key", key, sizeof key)) return;
		url_encode (key, k);
		if (http_request ("GET", host + "/api/rest_v1/page/summary/" + k, "", 0, resp, 20000) != 200
		    || !groq_json_string (resp.c_str (), "extract", ex, 4096)) return;
		out = ex;
		if (out.size () > 1200) out = out.substr (0, 1197) + "...";
		ui->log (std::string ("wiki \"") + topic + "\" -> " + key);
	}

	// ---- the long contents in pieces: next [mode, summary] and reprendre [words] ------------------------------
	// A story, a long explanation, a recipe: the LLM gives a paragraph (say) then next; the brain asks for the next
	// piece (a second turn "next": the request, the summary the LLM keeps, the last passage). Modes: enchainer (a
	// story: "Shall I go on?" every 3 pieces), proposer (an explanation: "Shall I tell you more?"), attendre (steps:
	// one, then the person asks for the next: the LLM does reprendre). An interrupted content is kept in
	// <home>/etc/histoires.txt (10 at most): "go on with yesterday's story" gives reprendre.
	struct NextState { bool active = false, again = false; std::string mode, summary, last; };
	NextState next;
	std::string storiesFile () { return home + "/etc/histoires.txt"; }
	static std::string noTabs (std::string v) { for (auto &c : v) if (c == '\t' || c == '\n' || c == '\r') c = ' '; return v; }
	static std::string titleOf (const std::string &r)
	{
		std::string t = r;
		size_t k = t.find ("itre :");
		if (k != std::string::npos) t = t.substr (k + 6);
		t = trim (t);
		size_t e = t.find_first_of (";.,(");
		if (e != std::string::npos) t = t.substr (0, e);
		if (t.size () > 50) t = t.substr (0, 50);
		return trim (t);
	}
	struct Story { long when; std::string mode, title, summary, last; };
	std::vector<Story> storiesLoad ()
	{
		std::vector<Story> v;
		std::string t;
		read_file (storiesFile (), t);
		for (auto &l0 : split (t, '\n'))
		{
			std::string l = l0;
			if (!l.empty () && l.back () == '\r') l.pop_back ();
			if (l.empty () || l[0] == '#') continue;
			std::vector<std::string> c = split (l, '\t');
			if (c.size () >= 5) v.push_back ({ atol (c[0].c_str ()), c[1], c[2], c[3], c[4] });
		}
		return v;
	}
	void storiesSave (const std::vector<Story> &v)
	{
		make_dirs (home + "/etc");
		std::string t = "# interrupted contents (next): date, mode, title, summary, last passage\n";
		for (auto &s : v) t += num (s.when) + "\t" + s.mode + "\t" + s.title + "\t" + s.summary + "\t" + s.last + "\n";
		write_file (storiesFile (), t);
	}
	void storyKeep (const NextState &st)
	{
		std::vector<Story> v = storiesLoad ();
		std::string t = titleOf (st.summary);
		for (size_t i = 0; i < v.size (); i++) if (v[i].title == t) { v.erase (v.begin () + (long) i); break; }
		v.push_back ({ seconds_now (), st.mode, noTabs (t), noTabs (st.summary), noTabs (st.last) });
		while (v.size () > 10) v.erase (v.begin ());
		storiesSave (v);
	}
	void storyDrop (const std::string &summary)
	{
		std::vector<Story> v = storiesLoad ();
		std::string t = titleOf (summary);
		for (size_t i = 0; i < v.size (); i++) if (v[i].title == t) { v.erase (v.begin () + (long) i); storiesSave (v); return; }
	}
	bool storyResume (const std::string &words, NextState &st)
	{
		std::vector<Story> v = storiesLoad ();
		if (v.empty ()) return false;
		size_t best = v.size () - 1; int bs = 0;
		std::vector<std::string> m;
		for (auto &w : split (lower (words), ' ')) if (w.size () >= 4) m.push_back (w);
		for (size_t i = 0; i < v.size (); i++)
		{
			std::string t = lower (v[i].title + " " + v[i].summary);
			int sc = 0;
			for (auto &x : m) if (t.find (x) != std::string::npos) sc++;
			if (sc > bs) { bs = sc; best = i; }
		}
		st.active = true; st.again = true;
		st.mode = v[best].mode; st.summary = v[best].summary; st.last = v[best].last;
		return true;
	}
	static bool isYes (const std::string &r)
	{
		std::string t = " ";
		for (char c : r) t += (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (unsigned char) c >= 0x80 ? (char) lower (std::string (1, c))[0] : ' ';
		t += " ";
		for (const char *n : { " non ", " stop ", " arr", " pas ", " no ", " not ", " enough " })
			if (t.find (n) != std::string::npos) return false;
		for (const char *o : { " oui ", " ouais ", " vas y ", " continue", " suite ", " encore ", " ok ", " d accord ", " allez ",
				       " volontiers ", " bien s", " yes ", " yeah ", " sure ", " go on ", " more ", " please ", " ja ", " si " })
			if (t.find (o) != std::string::npos) return true;
		return false;
	}

	// ---- the cron: a skill's action run again later ("tell me when the washing machine is done") ---------------
	// Outcome cron [every, minutes, app, action, args...] / [at, HH:MM, app, action, args...] / [list] / [cancel, app].
	// Only the actions a manifest lists ("cron = status"); kept in <home>/etc/cron.txt; an "every" task ends by itself
	// after 24 h. Run while PocketQI is open, when Pocket is free.
	struct CronJob { std::string app; std::vector<std::string> args; long every, next, expires; };
	std::vector<CronJob> cron;
	std::string cronFile () { return home + "/etc/cron.txt"; }
	void cronSave ()
	{
		make_dirs (home + "/etc");
		std::string t = "# Pocket's cron: next run, period (s), end, app, action, arguments (tabs)\n";
		for (auto &j : cron)
		{
			t += num (j.next) + "\t" + num (j.every) + "\t" + num (j.expires) + "\t" + j.app;
			for (auto &a : j.args) t += "\t" + a;
			t += "\n";
		}
		write_file (cronFile (), t);
	}
	void cronLoad ()
	{
		cron.clear ();
		std::string t;
		read_file (cronFile (), t);
		for (auto &l0 : split (t, '\n'))
		{
			std::string l = l0;
			if (!l.empty () && l.back () == '\r') l.pop_back ();
			if (l.empty () || l[0] == '#') continue;
			std::vector<std::string> c = split (l, '\t');
			if (c.size () < 5) continue;
			CronJob j;
			j.next = atol (c[0].c_str ()); j.every = atol (c[1].c_str ()); j.expires = atol (c[2].c_str ());
			j.app = c[3];
			j.args.assign (c.begin () + 4, c.end ());
			cron.push_back (j);
		}
	}
	const Skill *cronCheck (const std::string &app, const std::vector<std::string> &args, groq_outcome &o, std::string &why)
	{
		static const Skill sayApp = { "say", "", "", "", {}, {} };
		memset (&o, 0, sizeof o);
		if (app == "say")
		{
			snprintf (o.app, sizeof o.app, "say");
			if (args.empty () || args[0].empty ()) { why = "a reminder without text"; return 0; }
			snprintf (o.args[0], sizeof o.args[0], "%s", args[0].c_str ());
			o.nargs = 1;
			return &sayApp;
		}
		snprintf (o.app, sizeof o.app, "%s", app.c_str ());
		for (auto &a : args) if (o.nargs < GROQ_MAX_ARGS) snprintf (o.args[o.nargs++], sizeof o.args[0], "%s", a.c_str ());
		const Skill *s = skills_check (skills, o, why);
		if (!s) return 0;
		std::string act = args.empty () ? "" : lower (args[0]);
		for (auto &c : s->cron) if (c == act) return s;
		why = "the action " + act + " of " + app + " cannot be scheduled";
		return 0;
	}
	std::string cronSay (const CronJob &j)
	{
		std::string s = j.app;
		for (auto &a : j.args) s += " " + a;
		char b[80];
		if (j.every) snprintf (b, sizeof b, ui->tr (TRN (", every %ld minutes")), j.every / 60);
		else { long t = j.next % 86400; snprintf (b, sizeof b, ui->tr (TRN (", at %02ld:%02ld")), t / 3600, t / 60 % 60); }
		return s + b;
	}
	bool cronOutcome (const groq_outcome &o)
	{
		std::string verb = o.nargs > 0 ? lower (o.args[0]) : "";
		long t = seconds_now ();
		if (verb == "list")
		{
			if (cron.empty ()) { sayTr (TRN ("I am not watching anything for now.")); return true; }
			std::string s = ui->tr (TRN ("I am watching: "));
			for (size_t i = 0; i < cron.size (); i++) s += (i ? "; " : "") + cronSay (cron[i]);
			saidAll (s + ".");
			return true;
		}
		if (verb == "cancel")
		{
			std::string app = o.nargs > 1 ? o.args[1] : "";
			for (size_t i = cron.size (); i-- > 0;) if (app.empty () || cron[i].app == app) cron.erase (cron.begin () + (long) i);
			cronSave ();
			return false;
		}
		if ((verb == "every" || verb == "at") && o.nargs >= 4)
		{
			CronJob j;
			j.app = o.args[2];
			for (unsigned k = 3; k < o.nargs; k++) j.args.push_back (o.args[k]);
			groq_outcome c; std::string why;
			if (!cronCheck (j.app, j.args, c, why)) { ui->log ("cron refused: " + why); sayTr (TRN ("I cannot watch that.")); return true; }
			j.args.clear ();
			for (unsigned k = 0; k < c.nargs; k++) j.args.push_back (c.args[k]);
			if (verb == "every")
			{
				long m = atol (o.args[1]);
				j.every = (m < 1 ? 1 : m) * 60;
				j.next = t + j.every;
				j.expires = t + 24 * 3600;
			}
			else
			{
				int hh = 0, mm = 0;
				if (sscanf (o.args[1], "%d:%d", &hh, &mm) != 2 || hh > 23 || mm > 59) { sayTr (TRN ("I did not understand the time.")); return true; }
				j.next = t - t % 86400 + hh * 3600L + mm * 60L;
				if (j.next <= t) j.next += 86400;
				j.every = 0;
				j.expires = j.next + 3600;
			}
			if (cron.size () >= 16) { sayTr (TRN ("I am already watching too many things.")); return true; }
			cron.push_back (j);
			cronSave ();
			ui->log ("cron: " + cronSay (j));
			return false;
		}
		return false;
	}
	// one due task, run (from the worker's loop, when Pocket is free) -> true if one ran
	bool cronTick ()
	{
		long t = seconds_now ();
		for (size_t i = 0; i < cron.size (); i++)
		{
			if (t > cron[i].expires) { cron.erase (cron.begin () + (long) i); cronSave (); return false; }
			if (cron[i].next > t) continue;
			CronJob j = cron[i];
			groq_outcome o; std::string why, err;
			skillsCheck ();
			const Skill *s = cronCheck (j.app, j.args, o, why);
			bool stop = !s;
			said.clear ();
			ui->log ("cron -> " + cronSay (j));
			if (s && j.app == "say") saidAll (o.args[0]);
			else if (s)
			{
				ui->state (VISAGE_REFLECHIT);
				if (skill_run (home, *s, o, this, err, 0, false, &stop) != 0) ui->note (NOTE_ERROR, j.app + ": " + err);
			}
			ui->state (VISAGE_VEILLE);
			if (stop || j.every == 0) cron.erase (cron.begin () + (long) i);
			else cron[i].next = t + j.every;
			cronSave ();
			return true;
		}
		return false;
	}

	// ---- the outcomes, in order ---------------------------------------------------------------------------------
	std::vector<std::string> llmQueue;		// the skills' SendLLM, handled once the answer is done

	void runOutcomes (const char *text, groq_reply &r, groq_predicate *p, unsigned &np, char *sent, size_t nsent, int depth)
	{
		// a skill run speaks for itself: the say of the same answer are kept aside, and said only when they do not
		// repeat what the skills said (the model often adds "Appointment added", a double of the skill's own)
		bool appRun = false, remembered = false, appSpoke = false;
		std::vector<std::string> later;
		speakLang = r.langue[0] ? r.langue : ui->lang ();
		static const char *const base[] = { "say", "humeur", "geste", "retenir", "wiki", "askinfo", "ask", "cron", "next",
						    "reprendre", "maketext", "draw", 0 };
		for (unsigned i = 0; i < r.noutcomes; i++)
		{
			bool b = false;
			for (int k = 0; base[k]; k++) if (!strcmp (r.outcomes[i].app, base[k])) b = true;
			if (!b) appRun = true;
		}
		for (unsigned i = 0; i < r.noutcomes; i++)
		{
			groq_outcome &o = r.outcomes[i];
			// an app's arguments' markers ({ville} -> Lyon): replaced here, on the device
			if (strcmp (o.app, "say") && strcmp (o.app, "retenir"))
				for (unsigned k = 0; k < o.nargs; k++)
					if (strchr (o.args[k], '{'))
					{
						static char u[sizeof o.args[0]];
						groq_unmask (o.args[k], p, np, u, sizeof u);
						snprintf (o.args[k], sizeof o.args[k], "%s", u);
					}
			if (!strcmp (o.app, "say"))
			{
				if (appRun) later.push_back (o.args[0]);
				else
				{
					std::string keep = speakLang;
					if (o.nargs > 1 && o.args[1][0] && strlen (o.args[1]) <= 3) speakLang = o.args[1];
					saidAll (o.args[0]);
					speakLang = keep;
				}
			}
			else if (!strcmp (o.app, "humeur")) pocket_humeur_set (r.humeur, r.intensite);
			else if (!strcmp (o.app, "geste")) ui->note (NOTE_GESTURE, o.nargs ? o.args[0] : "");
			else if (!strcmp (o.app, "retenir"))
			{
				if (!remembered)
					for (unsigned k = 0; k < r.nretenir; k++)
					{
						memSet (r.retenir[k].name, r.retenir[k].value);
						ui->note (NOTE_INFO, std::string (ui->tr (TRN ("Remembered: "))) + r.retenir[k].name + " = " + r.retenir[k].value);
					}
				remembered = true;
			}
			else if (!strcmp (o.app, "wiki"))
			{
				if (depth & 2) continue;
				std::string topic = o.nargs > 0 ? o.args[0] : "", result;
				ui->note (NOTE_APP, std::string ("wiki: ") + topic);
				wiki (topic.c_str (), result);
				np = preds (p);
				static groq_reply r2;
				if (!ask (text, "wiki", topic.c_str (), result.c_str (), p, np, r2, sent, nsent)) { sayTr (TRN ("I cannot find the answer.")); return; }
				runOutcomes (text, r2, p, np, sent, nsent, depth | 2);
				return;			// (the second answer takes the place of the rest of this one)
			}
			else if (!strcmp (o.app, "cron")) { noteApp (o); cronOutcome (o); }
			else if (!strcmp (o.app, "next"))
			{
				noteApp (o);
				next.active = o.nargs >= 2;
				next.again = false;
				next.mode = o.nargs > 0 ? lower (o.args[0]) : "enchainer";
				next.summary = o.nargs > 1 ? o.args[1] : "";
				next.last = r.texte;
			}
			else if (!strcmp (o.app, "reprendre"))
			{
				noteApp (o);
				if (!storyResume (o.nargs > 0 ? o.args[0] : "", next)) sayTr (TRN ("I have nothing in progress to go on with."));
			}
			else if (!strcmp (o.app, "askinfo"))
			{
				// a skill run for the LLM: what it would say goes back as data, then a second turn
				if (depth & 2) continue;
				static groq_outcome o2;
				memset (&o2, 0, sizeof o2);
				snprintf (o2.app, sizeof o2.app, "%s", o.nargs > 0 ? o.args[0] : "");
				for (unsigned k = 1; k < o.nargs; k++) memcpy (o2.args[o2.nargs++], o.args[k], sizeof o.args[k]);
				std::string why, err, info, what;
				for (unsigned k = 0; k < o.nargs; k++) what += (k ? " " : "") + std::string (o.args[k]);
				skillsCheck ();
				const Skill *s = skills_check (skills, o2, why);
				if (!s) info = "(impossible : " + why + ")";
				else
				{
					ui->note (NOTE_APP, what);
					struct Capture : SkillIo
					{
						Brain *b; std::string *info;
						void say (const std::string &t) override { if (!info->empty ()) *info += " "; *info += t; }
						bool listen (std::string &) override { return false; }
						void log (const std::string &t) override { b->log (t); }
						const char *lang () override { return b->lang (); }
					} cap;
					cap.b = this; cap.info = &info;
					if (skill_run (home, *s, o2, &cap, err, 0, true) != 0) info = "(erreur de l'app : " + err + ")";
				}
				np = preds (p);
				static groq_reply r2;
				if (!ask (text, "askinfo", what.c_str (), info.c_str (), p, np, r2, sent, nsent)) { sayTr (TRN ("I cannot find the answer.")); return; }
				runOutcomes (text, r2, p, np, sent, nsent, depth | 2);
				return;
			}
			else if (!strcmp (o.app, "maketext"))
			{
				static char file[48], draft[sizeof o.args[0] + 128];
				if (!groq_maketext (o.nargs > 0 ? o.args[0] : "", o.nargs > 1 ? o.args[1] : "", file, sizeof file, draft, sizeof draft)) continue;
				make_dirs (home + "/tmp");
				write_file (home + "/tmp/" + file, draft);
				ui->log (std::string ("maketext: SD:/tmp/") + file);
			}
			else if (!strcmp (o.app, "draw")) ui->log ("draw: no screen to draw on in this version");
			else if (!strcmp (o.app, "ask"))
			{
				// a question to the person: said, then the answer typed goes to the LLM in a second turn
				if (depth & 1) continue;
				std::string question = o.args[0], answer;
				saidAll (question);
				if (!listen (answer)) answer.clear ();
				np = preds (p);
				static groq_reply r2;
				if (!ask (text, "ask", question.c_str (), answer.c_str (), p, np, r2, sent, nsent)) { sayTr (TRN ("I cannot find the answer.")); return; }
				runOutcomes (text, r2, p, np, sent, nsent, depth | 1);
				return;
			}
			else
			{
				std::string why, args;
				skillsCheck ();
				const Skill *s = skills_check (skills, o, why);
				for (unsigned k = 0; k < o.nargs; k++) args += (k ? " " : "") + std::string (o.args[k]);
				if (!s) { ui->log ("app refused: " + why); sayTr (TRN ("I cannot do that.")); continue; }
				ui->note (NOTE_APP, s->id + " " + args);
				noteApp (o);
				std::string err;
				size_t before = said.size ();
				ui->state (VISAGE_REFLECHIT);
				std::string keepLang = speakLang;
				speakLang = s->native ? ui->lang () : "fr";		// (the native skills speak the system's language, the BASIC ones French)
				int rc = skill_run (home, *s, o, this, err, &llmQueue);
				speakLang = keepLang;
				if (rc != 0)
				{
					ui->note (NOTE_ERROR, s->id + ": " + err);
					char b[160];
					snprintf (b, sizeof b, ui->tr (TRN ("The %s skill has a problem.")), s->id.c_str ());
					saidAll (b);
				}
				if (said.size () != before) appSpoke = true;
			}
		}
		// a say kept aside is dropped only when it repeats what the skills said (half its words of 4 letters and more)
		auto words = [] (const std::string &t) {
			std::vector<std::string> v; std::string w;
			for (size_t i = 0; i <= t.size (); i++)
			{
				unsigned char c = i < t.size () ? (unsigned char) t[i] : ' ';
				if (c >= 0x80 || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) w += (char) (c >= 'A' && c <= 'Z' ? c + 32 : c);
				else { if (w.size () >= 4) v.push_back (w); w.clear (); }
			}
			return v;
		};
		std::vector<std::string> done = words (said);
		for (auto &d : later)
		{
			bool repeat = false;
			if (appSpoke)
			{
				std::vector<std::string> m = words (d);
				size_t seen = 0;
				for (auto &x : m) for (auto &y : done) if (x == y) { seen++; break; }
				repeat = m.empty () || seen * 2 >= m.size ();
			}
			if (!repeat) saidAll (d);
		}
	}

	// ---- a sentence typed -> what Pocket does and says ----------------------------------------------------------
	void chat (const std::string &text)
	{
		long t = seconds_now ();
		if (!hist.empty () && t - lastTurn > 600) forget ();	// (10 minutes without a word)
		skillsCheck ();
		ui->state (VISAGE_REFLECHIT);
		static groq_predicate p[MAX_PRED];
		unsigned np = preds (p);
		static groq_reply r;
		static char sent[1024];
		said.clear ();
		appsRun.clear ();
		if (!haveKey ())
		{
			ui->state (VISAGE_VEILLE);
			ui->note (NOTE_ERROR, ui->tr (TRN ("No API key yet: open Pocket > Settings to give your Mistral or Groq key.")));
			pocket_humeur_set (HUMEUR_PERPLEXE, 2);
			return;
		}
		if (!ask (text.c_str (), 0, 0, 0, p, np, r, sent, sizeof sent))
		{
			ui->state (VISAGE_VEILLE);
			ui->note (NOTE_ERROR, ui->tr (lastError == 1 ? TRN ("No network: is the Wi-Fi or the cable connected?")
						      : lastError == 3 ? TRN ("The API key was refused: check it in Pocket > Settings.")
						      : TRN ("The LLM did not answer. Try again in a moment.")));
			pocket_humeur_set (HUMEUR_TRISTE, 1);
			return;
		}
		next = NextState ();
		runOutcomes (text.c_str (), r, p, np, sent, sizeof sent, 0);
		// a long content: the pieces that follow
		for (int piece = 1; next.active && piece < 200; piece++)
		{
			NextState st = next;
			next.active = false;
			const char *q = 0;
			if (st.again) ;
			else if (st.mode == "attendre") { storyKeep (st); break; }
			else if (st.mode == "proposer" && piece == 1) q = TRN ("Shall I tell you more?");
			else if (piece % 3 == 0) q = TRN ("Shall I go on?");
			if (q)
			{
				sayTr (q);
				std::string a;
				if (!listen (a) || !isYes (a))
				{
					storyKeep (st);
					sayTr (st.mode == "proposer" ? TRN ("All right.") : TRN ("All right, let us stop here. You can ask me for the rest later."));
					break;
				}
			}
			static groq_reply rn;
			np = preds (p);
			std::string arg = st.mode + " : " + st.summary;
			if (!ask (text.c_str (), "next", arg.c_str (), st.last.c_str (), p, np, rn, sent, sizeof sent)) { storyKeep (st); break; }
			runOutcomes (text.c_str (), rn, p, np, sent, sizeof sent, 1);
			if (!next.active) storyDrop (st.summary);
			else if (next.mode.empty ()) next.mode = st.mode;
		}
		// the history in markers (the memory up to date): the sentence and what Pocket said, its skills too
		np = preds (p);
		static char um[1024], gm[2048];
		if (!groq_mask (sent, p, np, um, sizeof um)) snprintf (um, sizeof um, "%s", sent);
		if (!groq_mask (said.c_str (), p, np, gm, sizeof gm)) snprintf (gm, sizeof gm, "%s", said.c_str ());
		remember (um, gm[0] ? gm : "(fait)", r.langue[0] ? r.langue : "fr", appsRun);
		lastTurn = seconds_now ();
		ui->state (VISAGE_VEILLE);
	}

	// a sentence, then the SendLLM of its skills (at most 3 in a row)
	void turn (const std::string &text)
	{
		chat (text);
		for (int n = 0; !llmQueue.empty (); n++)
		{
			std::string s = llmQueue.front ();
			llmQueue.erase (llmQueue.begin ());
			if (n >= 3) { llmQueue.clear (); break; }
			ui->note (NOTE_APP, "SendLLM: " + s);
			chat (s);
		}
	}

	void start ()
	{
		make_dirs (home + "/etc");
		memLoad ();
		cronLoad ();
		skillsCheck ();
	}
};

} // namespace pq

#endif
