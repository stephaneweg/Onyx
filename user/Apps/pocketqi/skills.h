//
// pocketqi/skills.h -- PocketQI: Pocket's skills, the small apps in Onyx BASIC the LLM may run (PocketQI's
// pocket_apps.cpp, ported). A skill is a folder <home>/apps/<id>/ of the app's data folder ("home",
// SD:/apps/pocketqi.app -- the skills' own "SD:", so that PocketQI's skills run unchanged):
//   main.bas   the program, compiled with <home>/lib/galo.bi after it (the program's line numbers unchanged);
//   app.txt    its manifest, "key = value": name, description, and one line per action the LLM may ask:
//              llm.<action> = <the arguments' types> : <what the action does>; cron = <the actions the cron may run>;
//   the rest   its settings and its data (agenda.txt...).
// The LLM asks for one with an outcome {"app": "agenda", "args": ["write", "2026-10-24", "14:00", "..."]}; it is checked
// against the manifest (a known action, the number of arguments, their types: date YYYY-MM-DD, heure HH:MM, nombre,
// texte) before anything runs, then run in Onyx BASIC's VM (Libs/basic) on the brain's thread: COMMAND$ = the
// arguments separated by tabs. Everything else goes through devices, as on PocketQI's other hosts:
//   SAY:        the lines written are said (shown in the conversation)
//   MOOD:       "60 40" (valence, arousal) or "joyeux 2" written: the face; read: "60 40 joyeux 2"
//   LLM:        a sentence for the LLM, handled as if typed once the skill ends (SendLLM)
//   ASKINFO:    FILEEXISTS: true when it runs for the LLM (askinfo: what it says goes back to the LLM)
//   CRON:       FILEEXISTS: true when the cron runs it; OPEN "CRON:STOP" ends its task
//   [METHOD ]http(s)://...   the web: FOR INPUT reads the answer (GET), FOR OUTPUT writes the body sent at CLOSE
//               (POST); HEADERS: the request's header lines, STATUS: the last HTTP code, RESPONSE: its body
//   LANG:       read: the system's language ("en", "fr") -- Onyx's own device
// LINE INPUT asks the person (the conversation's field): what is typed comes back. A skill is stopped after 10 s of its
// own work (the time waiting for an answer or the network does not count). MAIL:, PRINTER:, SCREEN: and TOUCH:
// (PocketQI's mail, printing and drawing) are not in this version: STATUS: says 501 and the screen is "0 0".
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
#ifndef POCKETQI_SKILLS_H
#define POCKETQI_SKILLS_H

#include "basic/bas.h"
#include "Apps/pocketqi/text.h"
#include "Apps/pocketqi/net.h"
#include "Apps/pocketqi/core/groq_proto.h"		// (C, extern "C" in their headers)
#include "Apps/pocketqi/core/pocket_humeur.h"

namespace pq {

struct SkillAction
{
	std::string name;			// "write"
	std::vector<std::string> types;		// "date", "texte", "[heure]"
	std::string description;
};

struct Skill
{
	std::string id;				// the folder's name: "agenda"
	std::string name, description;
	std::string dir;
	std::vector<SkillAction> actions;
	std::vector<std::string> cron;		// the actions the cron may run again ("cron = status")
};

// What a running skill needs from the rest of the app (the brain): its words said, the person asked.
struct SkillIo
{
	virtual ~SkillIo () {}
	virtual void say (const std::string &utf8) = 0;
	virtual bool listen (std::string &answer) = 0;		// LINE INPUT: the person's answer (false: none in time)
	virtual void log (const std::string &utf8) = 0;		// a line of the skill's journal (PRINT, the web's requests)
	virtual const char *lang () = 0;			// "en", "fr"
};

// ---- the manifests ----------------------------------------------------------------------------------------------
static std::vector<Skill> skills_scan (const std::string &home)
{
	std::vector<Skill> v;
	for (auto &d : sub_dirs (home + "/apps"))
	{
		Skill a;
		a.id = lower (d);
		a.dir = home + "/apps/" + d;
		std::string txt;
		if (!read_file (a.dir + "/app.txt", txt) || !file_exists (a.dir + "/main.bas")) continue;
		for (auto &l0 : split (txt, '\n'))
		{
			std::string l = trim (l0);
			if (l.empty () || l[0] == '#') continue;
			size_t eq = l.find ('=');
			if (eq == std::string::npos) continue;
			std::string k = lower (trim (l.substr (0, eq))), val = trim (l.substr (eq + 1));
			if (k == "name") a.name = val;
			else if (k == "description") a.description = val;
			else if (k == "cron") { for (auto &c : split (val, ' ')) if (!c.empty ()) a.cron.push_back (lower (c)); }
			else if (k.compare (0, 4, "llm.") == 0 && k.size () > 4)
			{
				SkillAction act;
				act.name = k.substr (4);
				size_t c = val.find (':');
				for (auto &t : split (trim (val.substr (0, c)), ' ')) if (!t.empty ()) act.types.push_back (lower (t));
				if (c != std::string::npos) act.description = trim (val.substr (c + 1));
				a.actions.push_back (act);
			}
		}
		if (!a.actions.empty ()) v.push_back (a);
	}
	return v;
}

// the prompt's "available applications" section (groq_request.apps)
static std::string skills_prompt (const std::vector<Skill> &skills)
{
	std::string s;
	for (auto &a : skills)
	{
		s += a.id;
		if (!a.description.empty ()) s += " (" + a.description + ")";
		s += " : ";
		for (size_t i = 0; i < a.actions.size (); i++)
		{
			auto &act = a.actions[i];
			s += "[\"" + act.name + "\"";
			for (auto &t : act.types) s += ", " + t;
			s += "] " + act.description;
			for (auto &c : a.cron) if (c == act.name) { s += " (planifiable)"; break; }
			s += i + 1 < a.actions.size () ? " ; " : ". ";
		}
	}
	if (!s.empty ())
		s += "Types : date AAAA-MM-JJ (ou un marqueur de date tel quel, {anniversaire} : Pocket le convertit), "
		     "heure HH:MM, nombre en chiffres, texte de 60 caracteres au plus ; [entre crochets] : facultatif.";
	return s;
}

// ---- the checks -------------------------------------------------------------------------------------------------
static bool is_date (const std::string &d)
{
	if (d.size () != 10 || d[4] != '-' || d[7] != '-') return false;
	for (int i : { 0, 1, 2, 3, 5, 6, 8, 9 }) if (d[(size_t) i] < '0' || d[(size_t) i] > '9') return false;
	int y = atoi (d.c_str ()), m = atoi (d.c_str () + 5), j = atoi (d.c_str () + 8);
	static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (m < 1 || m > 12 || j < 1) return false;
	int max = days[m - 1] + (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 1 : 0);
	return j <= max && y >= 2000 && y <= 2100;
}

// "19 septembre", "le 1er mai 2027", "19/09" -> YYYY-MM-DD, the next time if the year is not said; "" otherwise (a
// date of the memory, "anniversaire = 19 septembre", reaches a skill as its value: the LLM only wrote the marker)
static std::string spoken_date (const std::string &v)
{
	std::string s = lower (v);
	for (char &c : s) if (c == '/' || c == '.' || c == ',') c = ' ';
	static const char *const months[] = { "janvier", "février", "mars", "avril", "mai", "juin", "juillet",
		"août", "septembre", "octobre", "novembre", "décembre" };
	static const char *const plain[] = { "janvier", "fevrier", "mars", "avril", "mai", "juin", "juillet",
		"aout", "septembre", "octobre", "novembre", "decembre" };
	static const char *const english[] = { "january", "february", "march", "april", "may", "june", "july",
		"august", "september", "october", "november", "december" };
	int j = 0, m = 0, y = 0, n = 0;
	for (auto &w : split (s, ' '))
	{
		if (w.empty () || w == "le" || w == "du" || w == "au" || w == "the" || w == "of") continue;
		if (w == "1er" || w == "premier" || w == "1st") { j = 1; continue; }
		int k = -1;
		for (int i = 0; i < 12; i++) if (w == months[i] || w == plain[i] || w == english[i]) k = i;
		if (k >= 0) { m = k + 1; continue; }
		std::string d = w;
		while (d.size () > 2 && (d.back () == 'h' || d.back () == 't' || d.back () == 'd' || d.back () == 's' || d.back () == 'n' || d.back () == 'r'))
			d.pop_back ();		// (2nd, 3rd, 4th)
		if (d.empty () || d.find_first_not_of ("0123456789") != std::string::npos) return "";
		int x = atoi (d.c_str ());
		if (n == 0 && !j) j = x; else if (!m) m = x; else y = x;
		n++;
	}
	if (j < 1 || j > 31 || m < 1 || m > 12) return "";
	Date t = now ();
	if (!y) { y = t.y; if (m < t.m || (m == t.m && j < t.d)) y++; }
	else if (y < 100) y += 2000;
	char b[16];
	snprintf (b, sizeof b, "%04d-%02d-%02d", y, m, j);
	return b;
}

static bool is_time (const std::string &h)
{
	if (h.size () != 5 || h[2] != ':') return false;
	for (int i : { 0, 1, 3, 4 }) if (h[(size_t) i] < '0' || h[(size_t) i] > '9') return false;
	return atoi (h.c_str ()) <= 23 && atoi (h.c_str () + 3) <= 59;
}

// the outcome a known skill with a known action and valid arguments? (else why says why; a spoken date is made
// YYYY-MM-DD)
static const Skill *skills_check (const std::vector<Skill> &skills, groq_outcome &o, std::string &why)
{
	const Skill *app = 0;
	for (auto &a : skills) if (a.id == o.app) app = &a;
	if (!app) { why = std::string ("unknown app: ") + o.app; return 0; }
	if (o.nargs == 0) { why = "no action"; return 0; }
	std::string action = lower (o.args[0]);
	const SkillAction *act = 0;
	for (auto &x : app->actions) if (x.name == action) act = &x;
	if (!act) { why = "unknown action of " + app->id + ": " + action; return 0; }
	size_t needed = 0;					// (the types in brackets, at the end, are optional)
	while (needed < act->types.size () && act->types[needed][0] != '[') needed++;
	if (o.nargs - 1 < needed || o.nargs - 1 > act->types.size ())
	{
		why = app->id + " " + action + " takes " + num ((long) needed)
		      + (needed < act->types.size () ? " to " + num ((long) act->types.size ()) : "")
		      + " argument(s), " + num ((long) o.nargs - 1) + " given";
		return 0;
	}
	for (size_t i = 0; i + 1 < o.nargs; i++)
	{
		std::string t = act->types[i];
		if (t[0] == '[') t = t.substr (1, t.size () - (t.back () == ']' ? 2 : 1));
		if (t == "date" && !is_date (o.args[i + 1]))
		{
			std::string d = spoken_date (o.args[i + 1]);
			if (!d.empty ()) snprintf (o.args[i + 1], sizeof o.args[i + 1], "%s", d.c_str ());
		}
		const std::string v = o.args[i + 1];
		if (v.find_first_of ("\t\r\n") != std::string::npos) { why = "a forbidden character in an argument"; return 0; }
		if (v.empty () && act->types[i][0] == '[') continue;	// (an optional argument may be empty)
		bool ok = t == "date" ? is_date (v) : t == "heure" ? is_time (v)
			: t == "nombre" ? (!v.empty () && v.find_first_not_of ("0123456789") == std::string::npos)
			: (!v.empty () && v.size () <= 120);
		if (!ok) { why = "argument " + num ((long) i + 1) + " (" + t + ") not valid: \"" + v + "\""; return 0; }
	}
	return app;
}

// ---- the VM's host ----------------------------------------------------------------------------------------------
class SkillHost : public bas::Host
{
public:
	std::string home, args;			// args in Latin-1 (the VM makes it CP437)
	SkillIo *io = 0;
	unsigned endTicks = 0;			// the run's limit (kapi_get_ticks: 1/100 s)
	bool overrun = false;
	bool askinfo = false;			// run for the LLM (outcome askinfo)
	bool *cronStop = 0;			// run by the cron: CRON: exists, CRON:STOP ends its task
	std::vector<std::string> llm;		// SendLLM: the sentences for the LLM (UTF-8), handed over at the end
	std::string headers, response, outLine;
	int status = 0;

	void out (const char *s, int n) override
	{
		// PRINT: the skill's journal (not said)
		outLine += from_cp437 (std::string (s, (size_t) n));
		size_t e;
		while ((e = outLine.find ('\n')) != std::string::npos)
		{
			if (io) io->log (outLine.substr (0, e));
			outLine.erase (0, e + 1);
		}
	}
	// LINE INPUT / INPUT: the person's answer; "" when nothing comes in time, or for askinfo and the cron
	int inputLine (char *buf, int cap) override
	{
		if (cap <= 0) return -1;
		buf[0] = 0;
		if (askinfo || cronStop || !io) return 0;
		unsigned t0 = kapi_get_ticks ();
		std::string r;
		bool ok = io->listen (r);
		endTicks += kapi_get_ticks () - t0;		// (the wait does not count)
		if (!ok) return 0;
		snprintf (buf, (size_t) cap, "%s", to_cp437 (r).c_str ());
		return (int) strlen (buf);
	}
	void sleepMs (int ms) override { kapi_msleep ((unsigned) (ms > 0 ? ms : 0)); }
	bool poll () override
	{
		if ((int) (kapi_get_ticks () - endTicks) > 0) { overrun = true; return false; }
		return true;
	}
	double timer () override { Date t = now (); return t.hh * 3600.0 + t.mm * 60 + t.ss; }
	void date (char *o) override { Date t = now (); snprintf (o, 11, "%02d-%02d-%04d", t.m, t.d, t.y); }
	void time (char *o) override { Date t = now (); snprintf (o, 9, "%02d:%02d:%02d", t.hh, t.mm, t.ss); }
	unsigned seed () override { return (unsigned) seconds_now () * 2654435761u ^ kapi_get_ticks (); }
	const char *command () override { return args.c_str (); }

	// SD:/x and a relative path: in the app's data folder
	std::string path (const char *p)
	{
		std::string s = p;
		if (s.size () >= 3 && (s[0] == 'S' || s[0] == 's') && (s[1] == 'D' || s[1] == 'd') && s[2] == ':') s = s.substr (3);
		while (!s.empty () && (s[0] == '/' || s[0] == '\\')) s.erase (0, 1);
		for (char &c : s) if (c == '\\') c = '/';
		return home + "/" + s;
	}
	static bool is_say (const char *p) { return starts_ci (p, "SAY:"); }

	// "[METHOD ]http(s)://..." -> method and URL; false when it is not a web address
	static bool web_path (const char *p, std::string &method, std::string &url)
	{
		std::string s = p;
		size_t sp = s.find (' ');
		method.clear ();
		if (sp != std::string::npos && sp < 8)
		{
			method = s.substr (0, sp);
			for (char &c : method) if (c >= 'a' && c <= 'z') c = (char) (c - 32);
			s = s.substr (sp + 1);
		}
		if (!starts_ci (s.c_str (), "https://") && !starts_ci (s.c_str (), "http://")) return false;
		url = s;
		return true;
	}

	// the request: the VM's strings are Latin-1 (the address) and CP437 (what is written to a file); everything goes
	// out in UTF-8, the answer comes back in CP437. Content-Type: JSON when the body starts with { or [.
	std::string request (const std::string &method, const std::string &url8859, const std::string &bodyCp437)
	{
		std::string u = from_latin1 (url8859), enc;
		static const char hex[] = "0123456789ABCDEF";
		for (unsigned char c : u)
			if (c <= ' ' || c >= 0x7F) { enc += '%'; enc += hex[c >> 4]; enc += hex[c & 15]; }
			else enc += (char) c;
		std::string body = from_cp437 (bodyCp437), h = headers;
		while (!body.empty () && (body.back () == '\n' || body.back () == '\r')) body.pop_back ();	// (PRINT # ends the line)
		if (!body.empty () && lower (h).find ("content-type:") == std::string::npos)
			h += (body[0] == '{' || body[0] == '[') ? "Content-Type: application/json; charset=utf-8\r\n" : "Content-Type: text/plain; charset=utf-8\r\n";
		unsigned t0 = kapi_get_ticks ();
		std::string resp;
		int st = http_request (method.c_str (), enc, h, body.empty () ? 0 : &body, resp, 20000);
		endTicks += kapi_get_ticks () - t0;		// (the network's time does not count)
		status = st > 0 ? st : 0;
		response = st > 0 ? to_cp437 (resp) : std::string ();
		if (io)
		{
			size_t q = enc.find ('?');		// (the journal never shows the query nor the headers: a key may be there)
			io->log (method + " " + (q == std::string::npos ? enc : enc.substr (0, q) + "?...") + " : " + (st > 0 ? "HTTP " + num (st) : "no answer (" + num (st) + ")"));
		}
		return response;
	}

	char *load (const char *p, int *len) override
	{
		*len = 0;
		if (starts_ci (p, "CRON:STOP")) { if (cronStop) *cronStop = true; return new char[1]; }
		if (is_say (p)) return 0;
		bool ok = true;
		std::string d, method, url;
		if (starts_ci (p, "MOOD:")) { char m[64]; pocket_humeur_texte (m, sizeof m); d = std::string (m) + "\r\n"; }
		else if (starts_ci (p, "LANG:")) d = std::string (io ? io->lang () : "en") + "\r\n";
		else if (starts_ci (p, "SCREEN:")) d = "0 0 landscape notouch\r\n";
		else if (starts_ci (p, "TOUCH:")) d = "\r\n";
		else if (starts_ci (p, "RESPONSE:")) d = response;
		else if (starts_ci (p, "STATUS:")) d = num (status) + "\r\n";
		else if (starts_ci (p, "HEADERS:")) d = headers;
		else if (starts_ci (p, "MAIL:") || starts_ci (p, "PRINTER:"))
		{
			status = 501;
			response = to_cp437 ("Ce service n'est pas disponible sur Onyx.");
		}
		else if (web_path (p, method, url))
		{
			d = request (method.empty () ? "GET" : method, url, std::string ());
			if (status < 200 || status > 299) d.clear ();		// (as a file not found: RESPONSE: keeps it)
		}
		else { std::string f; ok = read_file (path (p), f); d = utf8_file_to_cp437 (f); }
		if (!ok) return 0;
		char *b = new char[d.size () + 1];
		memcpy (b, d.data (), d.size ());
		*len = (int) d.size ();
		return b;
	}
	bool save (const char *p, const char *data, int len) override
	{
		std::string text (data, (size_t) len), method, url;
		if (starts_ci (p, "CRON:STOP")) { if (cronStop) *cronStop = true; return true; }
		if (is_say (p))
		{
			for (auto &l : split (text, '\n'))
			{
				std::string s = trim (l);
				if (s.empty () || !io) continue;
				io->say (from_cp437 (s));
				// the time to read it, as PocketQI's voice takes the time to say it (the face's moods between two
				// sentences are seen); not counted in the skill's limit
				if (askinfo) continue;
				unsigned ms = 250 + 25 * (unsigned) s.size ();
				if (ms > 1800) ms = 1800;
				kapi_msleep (ms);
				endTicks += ms / 10;
			}
			return true;
		}
		if (starts_ci (p, "LLM:"))
		{
			std::string t = from_cp437 (text);
			for (char &c : t) if (c == '\r' || c == '\n' || c == '\t') c = ' ';
			t = trim (t);
			if (!askinfo && !t.empty ()) llm.push_back (t);
			return true;
		}
		if (starts_ci (p, "MOOD:"))
		{
			if (!askinfo) pocket_humeur_commande (from_cp437 (text).c_str ());
			return true;
		}
		if (starts_ci (p, "SCREEN:")) return true;
		if (starts_ci (p, "MAIL:") || starts_ci (p, "PRINTER:"))
		{
			status = 501;
			response = to_cp437 ("Ce service n'est pas disponible sur Onyx.");
			return true;
		}
		if (starts_ci (p, "HEADERS:"))
		{
			headers.clear ();
			for (auto &l : split (from_cp437 (text), '\n'))
			{
				std::string s = l;
				if (!s.empty () && s.back () == '\r') s.pop_back ();
				if (s.find (':') != std::string::npos) headers += s + "\r\n";
			}
			return true;
		}
		if (web_path (p, method, url)) { request (method.empty () ? "POST" : method, url, text); return true; }
		std::string f = path (p);
		size_t slash = f.rfind ('/');
		if (slash != std::string::npos) make_dirs (f.substr (0, slash));
		return write_file (f, text);
	}
	bool exists (const char *p) override
	{
		if (is_say (p)) return true;
		if (starts_ci (p, "ASKINFO:")) return askinfo;
		if (starts_ci (p, "CRON:")) return cronStop != 0;
		if (starts_ci (p, "RESPONSE:") || starts_ci (p, "STATUS:") || starts_ci (p, "HEADERS:") || starts_ci (p, "MOOD:")
		    || starts_ci (p, "LLM:") || starts_ci (p, "LANG:")) return true;
		if (starts_ci (p, "MAIL:") || starts_ci (p, "PRINTER:") || starts_ci (p, "SCREEN:") || starts_ci (p, "TOUCH:")) return false;
		return file_exists (path (p));
	}
	bool remove (const char *p) override { return kapi_remove (path (p).c_str ()) == 0; }
	bool rename (const char *a, const char *b) override { return kapi_rename (path (a).c_str (), path (b).c_str ()) == 0; }
	bool makeDir (const char *p) override { make_dirs (path (p)); return file_exists (path (p)); }
};

// WITH HEADERS ... END WITH (the headers of the web requests that follow): made galo.bi's calls before compiling,
// a line for a line (the errors' line numbers stay right); the VM does not change.
//   WITH HEADERS                            ->  WebHeader ""                       (the previous ones cleared)
//       Authorization = "Bearer " + key$    ->  WebHeader "Authorization: " + ("Bearer " + key$)
//   END WITH                                ->  REM
static std::string with_headers (const std::string &src)
{
	std::string o;
	bool in = false;
	for (auto &l0 : split (src, '\n'))
	{
		std::string l = l0;
		bool cr = !l.empty () && l.back () == '\r';
		if (cr) l.pop_back ();
		std::string t = trim (l), u = lower (t);
		size_t ns = l.find_first_not_of (" \t");
		std::string ind = l.substr (0, ns == std::string::npos ? 0 : ns);
		if (!in && u == "with headers") { l = ind + "WebHeader \"\""; in = true; }
		else if (in && u == "end with") { l = ind + "REM (END WITH)"; in = false; }
		else if (in && !t.empty () && t[0] != '\'' && u.compare (0, 3, "rem") != 0)
		{
			size_t eq = t.find ('=');
			if (eq != std::string::npos) l = ind + "WebHeader \"" + trim (t.substr (0, eq)) + ": \" + (" + trim (t.substr (eq + 1)) + ")";
		}
		o += l + "\n";
	}
	return o;
}

// The compiled skills kept (the last few: compiled once, then only loadBax), keyed by main.bas's and galo.bi's sizes.
struct BaxKept { std::string dir; long key; std::string bax; unsigned used; };
static std::vector<BaxKept> s_bax;
static unsigned s_baxUse;

static bas::Program *skill_program (const std::string &home, const Skill &app, std::string &err)
{
	std::string src, bi;
	if (!read_file (app.dir + "/main.bas", src)) { err = "main.bas not readable"; return 0; }
	if (!read_file (home + "/lib/galo.bi", bi)) { err = "lib/galo.bi not found"; return 0; }
	long key = (long) src.size () * 1000003L + (long) bi.size ();
	for (auto &k : s_bax)
		if (k.dir == app.dir && k.key == key)
		{
			bas::Error e = { 0, "" };
			bas::Program *p = bas::loadBax (k.bax.data (), (int) k.bax.size (), &e);
			if (p) { k.used = ++s_baxUse; return p; }
		}
	// the program then galo.bi (the program's line numbers unchanged), in Latin-1
	std::string all = to_latin1 (with_headers (src));
	if (!all.empty () && all.back () != '\n') all += "\n";
	all += to_latin1 (bi);
	bas::setDialect (0);
	bas::Error e = { 0, "" };
	bas::Program *p = bas::compile (all.c_str (), &e);
	if (!p) { err = "line " + num (e.line) + ": " + e.msg; return 0; }
	char *out = 0;
	int n = bas::saveBax (p, &out);
	if (n > 0)
	{
		BaxKept *k = 0;
		for (auto &x : s_bax) if (x.dir == app.dir) k = &x;
		if (!k && s_bax.size () < 6) { s_bax.push_back (BaxKept ()); k = &s_bax.back (); }
		if (!k) { k = &s_bax[0]; for (auto &x : s_bax) if (x.used < k->used) k = &x; }
		k->dir = app.dir; k->key = key; k->bax.assign (out, (size_t) n); k->used = ++s_baxUse;
	}
	delete [] out;
	return p;
}

// The skill run: 0 when all went well, else -1 and err. llmOut: the sentences of its SendLLM.
static int skill_run (const std::string &home, const Skill &app, const groq_outcome &o, SkillIo *io, std::string &err,
		      std::vector<std::string> *llmOut, bool askinfo = false, bool *cronStop = 0, unsigned maxMs = 10000)
{
	bas::Program *p = skill_program (home, app, err);
	if (!p) return -1;
	SkillHost h;
	h.home = home;
	h.io = io;
	h.askinfo = askinfo;
	h.cronStop = cronStop;
	std::string a;
	for (unsigned i = 0; i < o.nargs; i++) { if (i) a += '\t'; a += o.args[i]; }
	h.args = to_latin1 (a);
	h.endTicks = kapi_get_ticks () + maxMs / 10;
	bas::Error re = { 0, "" };
	bas::run (p, h, &re);
	bas::destroy (p);
	if (!h.outLine.empty () && io) io->log (h.outLine);
	if (llmOut) for (auto &t : h.llm) llmOut->push_back (t);
	if (h.overrun) { err = "stopped after " + num ((long) (maxMs / 1000)) + " s"; return -1; }
	if (re.msg[0]) { err = "line " + num (re.line) + ": " + re.msg; return -1; }
	return 0;
}

} // namespace pq

#endif
