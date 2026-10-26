//
// pocketqi/natives.h -- PocketQI on Onyx: the native skills, Pocket's hands in Onyx's own apps. They are told to the
// LLM as the BASIC skills are (a manifest each, the same checks: skills.h), but run here, in C++, on the apps' own
// data through the apps' own models -- so that what Pocket notes is where the person looks for it:
//   agenda    the Calendar's events (calendar/model.h: SD:/apps/calendar.app/calendar.ics), then agenda.txt and
//             reminders.txt for the desktop's agenda widget (calendar/agendafiles.h); a Calendar open reads the file
//             again by itself. It takes the place of PocketQI's own agenda skill.
//   todo      the Calendar's tasks (its VTODO), in the place of PocketQI's to-do skill
//   notes     Notes' notes (notes/notesmodel.cpp: SD:/Notes), Stickies told to read them again
//   mail      a message prepared in Mail (`mail --compose <draft>`) for the person to check and send -- Pocket never
//             sends a mail itself; the address from the contacts
//   contacts  the address book Mail and Cardfile share (SD:/Documents/Contacts.card, cardfile/model.h), read only
//   reveil    the Clock's alarms (clock/alarms.cpp: SD:/apps/clock.app/alarms.txt), clockd told to read them again
//   imprimer  a note, the agenda, the tasks, the shopping list printed through PrinterKit (printerkit.h: the print
//             service printd sends it to the printer -- IPP --, or writes it as a PDF), on the default printer or the
//             one named; skills.h's PRINTER: device prints a skill's text file the same way
// The Clock and Mail keep their files while they run: open, Pocket asks to close them first. The native skills speak
// the window's language (TR); the BASIC ones PocketQI's French. Included by skills.h, after Skill and SkillIo.
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
#ifndef POCKETQI_NATIVES_H
#define POCKETQI_NATIVES_H

#include "uikit/lang.h"
#include "Apps/notes/notesmodel.h"
#include "Apps/notes/stickies_proto.h"
#include "Apps/clock/alarms.h"
#include "Apps/clock/clocktime.h"
#include "Apps/clock/clock_proto.h"
#include "Apps/cardfile/model.h"
#include "printerkit/printerkit.h"
namespace calm {				// (the Calendar's model: its names kept apart)
#include "Apps/calendar/model.h"
#include "Apps/calendar/agendafiles.h"
}

namespace pq {

// ---- the words ------------------------------------------------------------------------------------------------
static std::string fmt (const char *f, const std::string &a, const std::string &b = "", const std::string &c = "")
{
	char o[1400];
	snprintf (o, sizeof o, f, a.c_str (), b.c_str (), c.c_str ());
	return o;
}

// a day said: "today", "tomorrow", "yesterday", else "Monday 3 November" ("lundi 3 novembre")
static std::string spoken_day (long day)
{
	static const char *const WD[7] = { TRN ("Sunday"), TRN ("Monday"), TRN ("Tuesday"), TRN ("Wednesday"), TRN ("Thursday"), TRN ("Friday"), TRN ("Saturday") };
	static const char *const MO[12] = { TRN ("January"), TRN ("February"), TRN ("March"), TRN ("April"), TRN ("May"), TRN ("June"),
					    TRN ("July"), TRN ("August"), TRN ("September"), TRN ("October"), TRN ("November"), TRN ("December") };
	Date t = now ();
	long d0 = day_number (t.y, t.m, t.d);
	if (day == d0) return TR ("today");
	if (day == d0 + 1) return TR ("tomorrow");
	if (day == d0 - 1) return TR ("yesterday");
	int y, m, d;
	from_day_number (day, &y, &m, &d);
	std::string s = std::string (TR (WD[week_day (day)])) + " " + num (d) + " " + TR (MO[m - 1]);
	if (y != t.y) s += " " + num (y);
	return s;
}

static std::string hm (int minutes) { char b[8]; snprintf (b, sizeof b, "%d:%02d", minutes / 60 % 24, minutes % 60); return b; }
static int parse_hm (const std::string &h) { return h.size () == 5 && h[2] == ':' ? atoi (h.c_str ()) * 60 + atoi (h.c_str () + 3) : -1; }
static long parse_day (const std::string &d) { return d.size () == 10 ? day_number (atoi (d.c_str ()), atoi (d.c_str () + 5), atoi (d.c_str () + 8)) : -1; }
static std::string arg (const groq_outcome &o, unsigned k) { return k < o.nargs ? trim (o.args[k]) : std::string (); }

// how well a title matches the words said: the words of 3 letters and more found in it (case ignored)
static int match_score (const std::string &title, const std::string &words)
{
	std::string t = lower (title);
	int sc = 0;
	for (auto &w0 : split (lower (words), ' '))
	{
		std::string w = trim (w0);
		while (!w.empty () && (w.back () == ',' || w.back () == '.')) w.pop_back ();
		if (w.size () >= 3 && t.find (w) != std::string::npos) sc++;
	}
	return sc;
}

// an app running? (kapi_list_tasks: "<state><kind> <name>" lines)
static bool app_running (const char *name)
{
	static char b[4096];
	int n = kapi_list_tasks (b, sizeof b);
	if (n <= 0) return false;
	for (auto &l : split (b, '\n'))
		if (l.size () > 3 && l[1] == 'a' && trim (l.substr (3)) == name) return true;
	return false;
}

// ---- the manifests (in French, as the rest of the prompt: PocketQI's) -----------------------------------------
static const char *const NATIVE_MANIFESTS[] = {
	"agenda",
	"name = Agenda\n"
	"description = l'agenda d'Onyx (le Calendrier et le widget du bureau)\n"
	"llm.write = date texte [heure] [heure] [texte] : ajouter un rendez-vous ou un evenement (texte tel que dit : dentiste, anniversaire de Paul ; "
	"l'heure de debut HH:MM des qu'une heure est dite, 14h -> 14:00, sinon rien : toute la journee ; puis l'heure de fin si elle est dite ; "
	"puis le lieu s'il est dit ; si le jour n'est pas dit, ne l'invente pas : demande-le avec ask) ; des que la personne annonce quelque chose "
	"de prevu, meme sans demander de le noter\n"
	"llm.read = date [date] : dire les rendez-vous d'un jour, ou d'une periode (les deux jours inclus ; cette semaine : lundi a dimanche)\n"
	"llm.delete = texte [date] : supprimer un rendez-vous (les mots qui le designent ; la date seulement si elle est dite)\n"
	"llm.edit = texte date [heure] : deplacer un rendez-vous (les mots qui le designent, le nouveau jour, la nouvelle heure HH:MM si elle est dite)\n",
	"todo",
	"name = Taches\n"
	"description = les taches a faire du Calendrier d'Onyx\n"
	"llm.add = texte [date] : ajouter une chose a faire (je dois appeler le plombier ; la date seulement si un delai est dit : avant vendredi) ; une chose par appel\n"
	"llm.done = texte : cocher une tache faite (les mots qui la designent)\n"
	"llm.delete = texte : supprimer une tache qui n'est plus a faire\n"
	"llm.list = : dire les taches a faire, avec leur echeance\n",
	"notes",
	"name = Notes\n"
	"description = les notes d'Onyx (l'app Notes, les notes epinglees sur le bureau)\n"
	"llm.write = texte long : creer une note (son titre, puis son texte : une chose a ne pas oublier, une idee, une liste ; une ligne par chose)\n"
	"llm.add = texte texte : ajouter une ligne a une note (les mots de son titre, la ligne) ; la note est creee si elle n'existe pas\n"
	"llm.read = texte : lire une note (les mots de son titre)\n"
	"llm.list = : dire les titres des notes\n",
	"mail",
	"name = Mail\n"
	"description = preparer un mail dans Mail d'Onyx, que la personne relit puis envoie elle-meme\n"
	"llm.write = texte texte long : preparer un mail (le destinataire : un prenom du carnet d'adresses ou une adresse ; le sujet ; le texte, "
	"redige par toi, signe {nom} si ce marqueur est connu) ; Pocket ouvre le message dans Mail, pas de say qui le repete\n",
	"contacts",
	"name = Contacts\n"
	"description = le carnet d'adresses d'Onyx (Mail et Cardfile)\n"
	"llm.find = texte : les coordonnees d'une personne (telephone, e-mail, adresse, anniversaire) ; avec askinfo pour une question, sinon les dire\n",
	"reveil",
	"name = Reveil\n"
	"description = les reveils (alarmes) de l'Horloge d'Onyx\n"
	"llm.set = heure [texte] [date] : regler un reveil (l'heure HH:MM ; son libelle s'il est dit ; le jour seulement s'il est dit, sinon la prochaine fois)\n"
	"llm.list = : dire les reveils regles\n"
	"llm.delete = heure : supprimer le reveil de cette heure\n",
	"imprimer",
	"name = Imprimer\n"
	"description = imprimer sur une imprimante d'Onyx (l'imprimante par defaut, ou celle qui est nommee)\n"
	"llm.note = texte [texte] : imprimer une note de Notes (les mots de son titre ; l'imprimante seulement si elle est nommee)\n"
	"llm.agenda = date [date] [texte] : imprimer les rendez-vous d'un jour ou d'une periode (cette semaine : lundi a dimanche ; l'imprimante si elle est nommee)\n"
	"llm.taches = [texte] : imprimer les taches a faire (l'imprimante si elle est nommee)\n"
	"llm.courses = [texte] : imprimer la liste de courses (l'imprimante si elle est nommee)\n"
	"llm.list = : dire les imprimantes d'Onyx et celle par defaut\n",
	0
};

// ---- agenda and todo: the Calendar ------------------------------------------------------------------------------
static int cal_today () { Date t = now (); return calm::days_from_civil (t.y, t.m, t.d); }

static void cal_load ()
{
	calm::g_utf8 = true;			// (PocketQI's text is UTF-8)
	calm::g_nev = calm::g_ntk = calm::g_ncat = 0;
	char *t = calm::cal_read_file (CAL_ICS, 0);
	if (t) { calm::ics_read (t, false); delete [] t; }
	if (calm::g_ncat == 0)		// (the Calendar's own, the first time)
	{
		calm::cat_add ("Work", 0x003B82F6); calm::cat_add ("Personal", 0x0010B981); calm::cat_add ("Family", 0x00F59E0B);
		calm::cat_add ("Sport", 0x008B5CF6); calm::cat_add ("Birthdays", 0x00EC4899);
	}
}

static bool cal_save ()
{
	int len = 0;
	char *t = calm::ics_write (&len);
	kapi_mkdir ("SD:/apps/calendar.app");
	bool ok = kapi_save_file (CAL_ICS, t, (unsigned) len) == len;
	delete [] t;
	calm::cal_write_agenda (cal_today ());
	return ok;
}

// the event the words name (on that day; -1: any from today on), -1 none, -2 several as good
static int cal_find (const std::string &words, long day)
{
	int best = -1, bs = 0, ties = 0, today = cal_today ();
	for (int i = 0; i < calm::g_nev; i++)
	{
		const calm::Event &e = calm::g_ev[i];
		int d = e.start / 1440;
		if (day >= 0 ? d != day : (d < today && e.freq == calm::RP_NONE)) continue;
		int sc = match_score (e.title, words);
		if (sc > bs) { bs = sc; best = i; ties = 0; }
		else if (sc == bs && sc > 0) ties++;
	}
	return bs == 0 ? -1 : ties ? -2 : best;
}

static std::string event_said (const calm::Event &e, int start)
{
	std::string s = e.title;
	if (!e.allDay) s += TR (" at ") + hm (start % 1440);
	if (e.place[0]) s += std::string (", ") + e.place;
	return s;
}

static void do_agenda (const groq_outcome &o, SkillIo *io)
{
	std::string act = lower (arg (o, 0));
	cal_load ();
	if (act == "write")
	{
		long d = parse_day (arg (o, 1));
		int h1 = parse_hm (arg (o, 3)), h2 = parse_hm (arg (o, 4));
		calm::Event &e = calm::ev_new ();
		snprintf (e.title, sizeof e.title, "%s", arg (o, 2).c_str ());
		snprintf (e.place, sizeof e.place, "%s", arg (o, 5).c_str ());
		e.cat = calm::cat_find ("Personal");
		if (h1 >= 0) { e.start = (int) d * 1440 + h1; e.end = h2 > h1 ? (int) d * 1440 + h2 : e.start + 60; e.remind = 15; }
		else { e.allDay = true; e.start = (int) d * 1440; e.end = e.start + 1440; }
		if (!cal_save ()) { io->say (TR ("The agenda could not be written on the card.")); return; }
		io->say (fmt (TR ("Noted in the agenda: %s, %s."), event_said (e, e.start), spoken_day (d)));
	}
	else if (act == "read")
	{
		long d1 = parse_day (arg (o, 1)), d2 = arg (o, 2).empty () ? d1 : parse_day (arg (o, 2));
		static calm::Occ occ[200];
		int n = calm::occurrences ((int) d1, (int) d2 + 1, occ, 200, true);
		if (n == 0) { io->say (d1 == d2 ? fmt (TR ("Nothing in the agenda %s."), spoken_day (d1)) : std::string (TR ("Nothing in the agenda for those days."))); return; }
		io->say (n == 1 ? std::string (TR ("One appointment:")) : fmt (TR ("%s appointments:"), num (n)));
		for (int i = 0; i < n; i++)
			io->say (spoken_day (occ[i].start / 1440) + ": " + event_said (calm::g_ev[occ[i].ev], occ[i].start) + ".");
	}
	else if (act == "delete" || act == "edit")
	{
		long d = act == "delete" && !arg (o, 2).empty () ? parse_day (arg (o, 2)) : -1;
		int i = cal_find (arg (o, 1), d);
		if (i == -1) { io->say (TR ("I find no appointment like that in the agenda.")); return; }
		if (i == -2) { io->say (TR ("Several appointments match: which one?")); return; }
		calm::Event e = calm::g_ev[i];
		if (act == "delete")
		{
			calm::ev_delete (i);
			if (!cal_save ()) { io->say (TR ("The agenda could not be written on the card.")); return; }
			io->say (fmt (TR ("Deleted from the agenda: %s, %s."), event_said (e, e.start), spoken_day (e.start / 1440)));
			return;
		}
		long nd = parse_day (arg (o, 2));
		int h = parse_hm (arg (o, 3)), len = e.end - e.start;
		calm::Event &m = calm::g_ev[i];
		if (h >= 0) { m.allDay = false; m.start = (int) nd * 1440 + h; m.end = m.start + (e.allDay ? 60 : len); if (m.remind < 0) m.remind = 15; }
		else { m.start = (int) nd * 1440 + (e.allDay ? 0 : e.start % 1440); m.end = m.start + len; }
		if (!cal_save ()) { io->say (TR ("The agenda could not be written on the card.")); return; }
		io->say (fmt (TR ("Moved: %s, %s."), event_said (m, m.start), spoken_day (nd)));
	}
}

static void do_todo (const groq_outcome &o, SkillIo *io)
{
	std::string act = lower (arg (o, 0));
	cal_load ();
	auto find = [] (const std::string &words) {
		int best = -1, bs = 0;
		for (int i = 0; i < calm::g_ntk; i++)
		{
			int sc = match_score (calm::g_tk[i].title, words) * 2 + (calm::g_tk[i].done ? 0 : 1);
			if (match_score (calm::g_tk[i].title, words) > 0 && sc > bs) { bs = sc; best = i; }
		}
		return best;
	};
	if (act == "add")
	{
		calm::Task &t = calm::tk_new ();
		snprintf (t.title, sizeof t.title, "%s", arg (o, 1).c_str ());
		t.due = arg (o, 2).empty () ? 0 : (int) parse_day (arg (o, 2));
		if (!cal_save ()) { io->say (TR ("The tasks could not be written on the card.")); return; }
		io->say (t.due ? fmt (TR ("Added to your tasks: %s, by %s."), t.title, spoken_day (t.due)) : fmt (TR ("Added to your tasks: %s."), t.title));
	}
	else if (act == "done" || act == "delete")
	{
		int i = find (arg (o, 1));
		if (i < 0) { io->say (TR ("I find no task like that.")); return; }
		std::string title = calm::g_tk[i].title;
		if (act == "done") calm::g_tk[i].done = true; else calm::tk_delete (i);
		if (!cal_save ()) { io->say (TR ("The tasks could not be written on the card.")); return; }
		io->say (fmt (act == "done" ? TR ("Done: %s.") : TR ("Deleted from your tasks: %s."), title));
	}
	else if (act == "list")
	{
		std::vector<int> v;
		for (int i = 0; i < calm::g_ntk; i++) if (!calm::g_tk[i].done) v.push_back (i);
		for (size_t i = 1; i < v.size (); i++)		// (those with a date first, the soonest first)
			for (size_t j = i; j > 0; j--)
			{
				int a = calm::g_tk[v[j - 1]].due, b = calm::g_tk[v[j]].due;
				if ((a == 0 && b != 0) || (a && b && b < a)) std::swap (v[j - 1], v[j]); else break;
			}
		if (v.empty ()) { io->say (TR ("Nothing left to do in your tasks.")); return; }
		io->say (v.size () == 1 ? std::string (TR ("One task to do:")) : fmt (TR ("%s tasks to do:"), num ((long) v.size ())));
		for (int i : v)
		{
			const calm::Task &t = calm::g_tk[i];
			io->say (t.due ? fmt (TR ("%s, by %s."), t.title, spoken_day (t.due)) : std::string (t.title) + ".");
		}
	}
}

// ---- notes: Notes --------------------------------------------------------------------------------------------------
static Notes s_notes;				// (512 notes' entries: static)

static void notes_told ()
{
	int pid = kapi_ipc_lookup (STICKIES_SERVICE);
	if (pid > 0) kapi_mailbox_send (pid, STK_MSG_RELOAD, "", 0);
}

static int notes_find_words (const std::string &words)
{
	int best = -1, bs = 0;
	for (int i = 0; i < s_notes.count; i++)
	{
		int sc = match_score (s_notes.n[i].title, words);
		if (sc > bs) { bs = sc; best = i; }
	}
	return best;
}

static bool notes_new (const std::string &text, std::string &title)
{
	int i = notes_add_new (s_notes, notes_now ());
	if (i < 0) return false;
	char t[NOTE_TITLE];
	notes_title (text.c_str (), (int) text.size (), t, sizeof t);
	title = t;
	if (notes_write (s_notes, s_notes.n[i].file, text.c_str (), (int) text.size (), notes_now ()) != 0) return false;
	notes_save_ini (s_notes);
	notes_told ();
	return true;
}

static void do_notes (const groq_outcome &o, SkillIo *io)
{
	std::string act = lower (arg (o, 0));
	notes_scan (s_notes);
	static char buf[NOTE_MAX_BYTES + 1];
	if (act == "write")
	{
		std::string text = arg (o, 1) + "\n" + arg (o, 2) + "\n", title;
		if (!notes_new (text, title)) { io->say (TR ("The note could not be written on the card.")); return; }
		io->say (fmt (TR ("I wrote the note \"%s\" in Notes."), title));
	}
	else if (act == "add")
	{
		int i = notes_find_words (arg (o, 1));
		std::string title;
		if (i < 0)
		{
			if (!notes_new (arg (o, 1) + "\n" + arg (o, 2) + "\n", title)) { io->say (TR ("The note could not be written on the card.")); return; }
			io->say (fmt (TR ("I made the note \"%s\" in Notes, with: %s."), title, arg (o, 2)));
			return;
		}
		int n = notes_read (s_notes.n[i].file, buf, sizeof buf);
		if (n < 0) { io->say (TR ("That note cannot be read.")); return; }
		std::string text (buf, (size_t) n);
		if (!text.empty () && text.back () != '\n') text += "\n";
		text += arg (o, 2) + "\n";
		title = s_notes.n[i].title;
		if (notes_write (s_notes, s_notes.n[i].file, text.c_str (), (int) text.size (), notes_now ()) != 0) { io->say (TR ("The note could not be written on the card.")); return; }
		notes_save_ini (s_notes);
		notes_told ();
		io->say (fmt (TR ("Added to the note \"%s\": %s."), title, arg (o, 2)));
	}
	else if (act == "read")
	{
		int i = notes_find_words (arg (o, 1));
		if (i < 0) { io->say (TR ("I find no note like that.")); return; }
		int n = notes_read (s_notes.n[i].file, buf, sizeof buf);
		if (n < 0) { io->say (TR ("That note cannot be read.")); return; }
		int said = 0;
		for (auto &l : split (std::string (buf, (size_t) n), '\n'))
			if (!trim (l).empty () && said++ < 30) io->say (trim (l));
	}
	else if (act == "list")
	{
		if (s_notes.count == 0) { io->say (TR ("There is no note yet.")); return; }
		std::string t;
		for (int i = 0; i < s_notes.count && i < 12; i++) t += (i ? ", " : "") + std::string (s_notes.n[i].title[0] ? s_notes.n[i].title : "?");
		io->say (fmt (TR ("Your notes: %s."), t));
	}
}

// ---- contacts: the address book -------------------------------------------------------------------------------
struct Person { std::string name, email, phone, mobile, address, birthday; };

// the person the words name in SD:/Documents/Contacts.card (Latin-1); false: none
static bool contact_find (const std::string &words, Person &p)
{
	std::string b;
	if (!read_file ("SD:/Documents/Contacts.card", b)) return false;
	cf::Doc d;
	cf::doc_init (d);
	if (!cf::doc_read (d, &b[0], (int) b.size (), 0)) { cf::doc_clear (d); return false; }
	auto col = [&] (const char *c) { for (int k = 0; k < d.nf; k++) if (!strcmp (d.f[k].column, c)) return k; return -1; };
	int cn = col ("name"), ce = col ("email"), cp = col ("phone"), cm = col ("mobile"), ca = col ("address"), cb = col ("birthday");
	if (cn < 0) cn = 0;
	int best = -1, bs = 0;
	std::string w = to_latin1 (words);
	for (int i = 0; i < d.nr; i++)
	{
		int sc = match_score (d.r[i][cn], w);
		if (sc > bs) { bs = sc; best = i; }
	}
	bool ok = best >= 0;
	if (ok)
	{
		auto v = [&] (int k) { return k >= 0 ? from_latin1 (d.r[best][k]) : std::string (); };
		p.name = v (cn); p.email = v (ce); p.phone = v (cp); p.mobile = v (cm); p.address = v (ca); p.birthday = v (cb);
	}
	cf::doc_clear (d);
	return ok;
}

static void do_contacts (const groq_outcome &o, SkillIo *io)
{
	Person p;
	if (!contact_find (arg (o, 1), p)) { io->say (fmt (TR ("I find no %s in your contacts."), arg (o, 1))); return; }
	std::string s = p.name;
	if (!p.phone.empty ()) s += fmt (TR (", phone %s"), p.phone);
	if (!p.mobile.empty ()) s += fmt (TR (", mobile %s"), p.mobile);
	if (!p.email.empty ()) s += fmt (TR (", e-mail %s"), p.email);
	if (!p.address.empty ()) { std::string a = p.address; for (char &c : a) if (c == '\n') c = ' '; s += fmt (TR (", address %s"), a); }
	if (!p.birthday.empty ()) s += fmt (TR (", birthday %s"), p.birthday);
	io->say (s + ".");
}

// ---- mail: a message prepared in Mail ---------------------------------------------------------------------------
static void do_mail (const groq_outcome &o, SkillIo *io, const std::string &home)
{
	std::string who = arg (o, 1), subject = arg (o, 2), text = arg (o, 3), to;
	if (app_running ("mail")) { io->say (TR ("Mail is open: close it, then ask me again, and I will open the message in it.")); return; }
	Person p;
	if (who.find ('@') != std::string::npos) to = who;
	else if (contact_find (who, p) && !p.email.empty ()) to = p.name + " <" + p.email + ">";
	make_dirs (home + "/tmp");
	std::string path = home + "/tmp/mail-draft.txt";
	if (!write_file (path, "To: " + to + "\nSubject: " + subject + "\n\n" + text + "\n")) { io->say (TR ("The message could not be written on the card.")); return; }
	if (lx_launch ("mail", ("--compose " + path).c_str ()) <= 0) { io->say (TR ("Mail could not be started.")); return; }
	if (to.empty ()) io->say (fmt (TR ("Your message is ready in Mail, but I did not find %s's address in your contacts: type it, then press Send."), who));
	else io->say (fmt (TR ("Your message to %s is ready in Mail: read it, then press Send."), p.name.empty () ? who : p.name));
}

// ---- reveil: the Clock's alarms ---------------------------------------------------------------------------------
static void do_reveil (const groq_outcome &o, SkillIo *io)
{
	std::string act = lower (arg (o, 0));
	if (act != "list" && app_running ("clock")) { io->say (TR ("The Clock is open: close it, then ask me again.")); return; }
	static AlarmSet s;
	static bool init;
	if (!init) { alarms_init (s); init = true; }
	alarms_free (s);
	alarms_load (s, ALARMS_PATH);
	Date t = now ();
	long nowMin = clk_minute (t.y, t.m, t.d, t.hh, t.mm);
	auto told = [] { int pid = kapi_ipc_lookup (CLOCKD_SERVICE); if (pid > 0) kapi_mailbox_send (pid, CLOCKD_MSG_RELOAD, "", 0); };
	if (act == "set")
	{
		int h = parse_hm (arg (o, 1));
		Alarm a;
		alarm_new (s, a);
		a.hh = h / 60; a.mm = h % 60;
		alarm_set_label (a, arg (o, 2).c_str ());
		long d = parse_day (arg (o, 3));
		a.date = d >= 0 ? clk_days (atoi (arg (o, 3).c_str ()), atoi (arg (o, 3).c_str () + 5), atoi (arg (o, 3).c_str () + 8)) : alarm_once_day (a.hh, a.mm, nowMin);
		if (alarm_add (s, a) < 0) { io->say (TR ("The Clock has 20 alarms already.")); return; }
		kapi_mkdir ("SD:/apps/clock.app");
		if (alarms_save (s, ALARMS_PATH) != 0) { io->say (TR ("The alarms could not be written on the card.")); return; }
		told ();
		int y, m, dd;
		clk_civil (a.date, &y, &m, &dd);
		io->say (fmt (TR ("Alarm set for %s, %s."), hm (h), spoken_day (day_number (y, m, dd))));
	}
	else if (act == "list")
	{
		std::string l;
		int n = 0;
		for (int i = 0; i < s.n; i++)
		{
			const Alarm &a = s.a[i];
			if (!a.on || !a.valid) continue;
			l += (n++ ? ", " : "") + hm (a.hh * 60 + a.mm);
			if (a.label[0]) l += std::string (" (") + a.label + ")";
			if (!a.days && a.date >= 0) { int y, m, dd; clk_civil (a.date, &y, &m, &dd); l += " " + spoken_day (day_number (y, m, dd)); }
			else if (a.days) l += TR (" every week");
		}
		io->say (n ? fmt (TR ("Your alarms: %s."), l) : std::string (TR ("No alarm is set.")));
	}
	else if (act == "delete")
	{
		int h = parse_hm (arg (o, 1)), found = 0;
		for (int i = s.n; i-- > 0;)
			if (s.a[i].valid && s.a[i].hh * 60 + s.a[i].mm == h) { alarm_remove (s, s.a[i].id); found++; }
		if (!found) { io->say (fmt (TR ("There is no alarm at %s."), hm (h))); return; }
		if (alarms_save (s, ALARMS_PATH) != 0) { io->say (TR ("The alarms could not be written on the card.")); return; }
		told ();
		io->say (fmt (TR ("The alarm at %s is deleted."), hm (h)));
	}
}

// (the Calendar's words in reminders.txt, agendafiles.h: written in this window's language)
static const char *const CAL_WORDS[] __attribute__ ((unused)) = { TRN ("(No title)"), TRN (" (tomorrow)"), TRN (" (today)") };

// ---- imprimer: PrinterKit -------------------------------------------------------------------------------------
// A document of lines (a heading in bold), printed through PrinterKit -- the print service printd makes of it what the
// printer takes (IPP for a network printer, a PDF file for the PDF printer); Pocket does not wait for the printer.
struct PrintLine { std::string text; bool bold; };

// the printer the words name (else the default one) -> its setup; false: no printer
static bool printer_setup (const std::string &words, PrintSetup &s)
{
	memset (&s, 0, sizeof s);
	s.size = sizeof s;
	print_setup_default (&s);
	if (!words.empty ())
	{
		static PrintPrinter pr[16];
		int n = print_printers (pr, 16), best = -1, bs = 0;
		for (int i = 0; i < n; i++)
		{
			int sc = match_score (std::string (pr[i].name) + " " + pr[i].model, words);
			if (sc > bs) { bs = sc; best = i; }
		}
		if (best >= 0)
		{
			snprintf (s.printer, sizeof s.printer, "%s", pr[best].name);
			if (pr[best].media_default[0]) print_setup_paper (&s, pr[best].media_default, PRINT_PORTRAIT);
		}
	}
	return s.printer[0] != 0;
}

// the document printed: a title, the day, its lines wrapped to the page -> the job's number (> 0), else < 0
static int print_lines (PrintSetup &s, const std::string &title, std::vector<PrintLine> lines)
{
	while (!lines.empty () && trim (lines.back ().text).empty ()) lines.pop_back ();	// (a file's last new line)
	PrintJob *j = print_begin (&s, title.c_str ());
	if (!j) return -1;
	int f = print_font (j, "DejaVu Sans", 0), fb = print_font (j, "DejaVu Sans", PRINT_BOLD);
	if (fb < 0) fb = f;
	float ml = s.margin_l + 48, mr = s.margin_r + 48, mt = s.margin_t + 48, mb = s.margin_b + 48;
	float w = s.paper_w - ml - mr, y = 0, size = 11, lh = 15;
	auto page = [&] { print_page (j, 0, 0); y = mt; };
	page ();
	y += 18;
	print_text (j, fb, 16, ml, y, title.c_str (), 0x000000);
	y += 14;
	Date t = now ();
	print_text (j, f, 9, ml, y, (spoken_day (day_number (t.y, t.m, t.d)) + ", " + hm (t.hh * 60 + t.mm) + " -- PocketQI").c_str (), 0x707070);
	y += 8;
	print_line (j, ml, y, ml + w, y, 0.75f, 0xA0A0A0);
	y += 10;
	for (auto &l : lines)
	{
		int font = l.bold ? fb : f;
		if (l.bold) y += 6;
		// the line's words wrapped at the page's width
		std::string cur;
		std::vector<std::string> out;
		for (auto &wd : split (l.text, ' '))
		{
			std::string t2 = cur.empty () ? wd : cur + " " + wd;
			if (!cur.empty () && print_text_width (j, font, size, t2.c_str ()) > w) { out.push_back (cur); cur = wd; }
			else cur = t2;
		}
		out.push_back (cur);
		for (auto &o : out)
		{
			if (y + lh > s.paper_h - mb) page ();
			y += lh;
			print_text (j, font, size, ml, y, o.c_str (), 0x000000);
		}
	}
	return print_end (j);
}

static void printed (SkillIo *io, int job, const std::string &what, const PrintSetup &s)
{
	if (job > 0) io->say (fmt (TR ("I am printing %s on %s."), what, !strcmp (s.printer, "PDF") ? std::string (TR ("the PDF printer")) : std::string (s.printer)));
	else io->say (TR ("The printing could not start: is the print service running?"));
}

static void do_imprimer (const groq_outcome &o, SkillIo *io, const std::string &home)
{
	std::string act = lower (arg (o, 0));
	PrintSetup s;
	if (act == "list")
	{
		static PrintPrinter pr[16];
		int n = print_printers (pr, 16);
		if (n <= 0) { io->say (TR ("No printer is set up: the Control Panel's Printers applet adds one.")); return; }
		std::string l;
		for (int i = 0; i < n; i++) l += (i ? ", " : "") + std::string (!strcmp (pr[i].name, "PDF") ? TR ("the PDF printer") : pr[i].name) + (pr[i].is_default ? TR (" (by default)") : "");
		io->say (fmt (TR ("Your printers: %s."), l));
		return;
	}
	std::string printer = act == "note" ? arg (o, 2) : act == "agenda" ? arg (o, 3) : arg (o, 1);
	if (!printer_setup (printer, s)) { io->say (TR ("No printer is set up: the Control Panel's Printers applet adds one.")); return; }
	std::vector<PrintLine> lines;
	if (act == "note")
	{
		notes_scan (s_notes);
		int i = notes_find_words (arg (o, 1));
		if (i < 0) { io->say (TR ("I find no note like that.")); return; }
		static char buf[NOTE_MAX_BYTES + 1];
		int n = notes_read (s_notes.n[i].file, buf, sizeof buf);
		if (n < 0) { io->say (TR ("That note cannot be read.")); return; }
		bool first = true;
		for (auto &l : split (std::string (buf, (size_t) n), '\n'))
		{
			if (first && !trim (l).empty ()) { first = false; continue; }	// (its title: the document's)
			lines.push_back ({ l, false });
		}
		std::string title = s_notes.n[i].title;
		printed (io, print_lines (s, title, lines), "\"" + title + "\"", s);
	}
	else if (act == "agenda")
	{
		cal_load ();
		long d1 = parse_day (arg (o, 1)), d2 = arg (o, 2).empty () ? d1 : parse_day (arg (o, 2));
		static calm::Occ occ[400];
		int n = calm::occurrences ((int) d1, (int) d2 + 1, occ, 400, true);
		long day = -1;
		for (int i = 0; i < n; i++)
		{
			long d = occ[i].start / 1440;
			if (d != day) { day = d; std::string h = spoken_day (d); if (!h.empty ()) h[0] = (char) toupper ((unsigned char) h[0]); lines.push_back ({ h, true }); }
			const calm::Event &e = calm::g_ev[occ[i].ev];
			lines.push_back ({ (e.allDay ? std::string (TR ("all day")) : hm (occ[i].start % 1440)) + "   " + e.title + (e.place[0] ? std::string (", ") + e.place : std::string ()), false });
		}
		if (!n) lines.push_back ({ TR ("Nothing in the agenda for those days."), false });
		printed (io, print_lines (s, TR ("Agenda"), lines), TR ("the agenda"), s);
	}
	else if (act == "taches")
	{
		cal_load ();
		for (int i = 0; i < calm::g_ntk; i++)
		{
			const calm::Task &t = calm::g_tk[i];
			if (t.done) continue;
			lines.push_back ({ std::string ("\xE2\x98\x90  ") + t.title + (t.due ? " (" + spoken_day (t.due) + ")" : std::string ()), false });
		}
		if (lines.empty ()) lines.push_back ({ TR ("Nothing left to do in your tasks."), false });
		printed (io, print_lines (s, TR ("Tasks"), lines), TR ("your tasks"), s);
	}
	else if (act == "courses")
	{
		std::string t;
		read_file (home + "/apps/courses/liste.txt", t);
		for (auto &l : split (from_cp437_if_needed (t), '\n'))
		{
			std::vector<std::string> c = split (trim (l), '\t');
			if (c.empty () || c[0].empty ()) continue;
			lines.push_back ({ std::string ("\xE2\x98\x90  ") + c[0] + (c.size () > 1 && !c[1].empty () ? " (" + c[1] + ")" : std::string ()), false });
		}
		if (lines.empty ()) { io->say (TR ("The shopping list is empty.")); return; }
		printed (io, print_lines (s, TR ("Shopping list"), lines), TR ("the shopping list"), s);
	}
}

// ---- the list and the run -------------------------------------------------------------------------------------
static std::vector<Skill> native_skills ()
{
	std::vector<Skill> v;
	for (int i = 0; NATIVE_MANIFESTS[i]; i += 2)
	{
		Skill s;
		s.id = NATIVE_MANIFESTS[i];
		s.native = true;
		skill_parse (s, NATIVE_MANIFESTS[i + 1]);
		v.push_back (s);
	}
	return v;
}

static int native_run (const std::string &home, const Skill &app, const groq_outcome &o, SkillIo *io)
{
	if (app.id == "agenda") do_agenda (o, io);
	else if (app.id == "todo") do_todo (o, io);
	else if (app.id == "notes") do_notes (o, io);
	else if (app.id == "mail") do_mail (o, io, home);
	else if (app.id == "contacts") do_contacts (o, io);
	else if (app.id == "reveil") do_reveil (o, io);
	else if (app.id == "imprimer") do_imprimer (o, io, home);
	return 0;
}

} // namespace pq

#endif
