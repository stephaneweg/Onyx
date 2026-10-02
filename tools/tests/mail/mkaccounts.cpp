//
// mkaccounts.cpp -- the accounts (and their encrypted passwords) and the contacts Mail's screenshots start with,
// written by Mail's own code (accounts.h, contacts.h) into the stand-in card (SIM_WRITES):
//   mkaccounts "label|email|name|imap or pop3|host|in port|out port|password|#colour|provider" ...
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#include "Apps/mail/contacts.h"

using namespace mail;
using namespace mailapp;

int main (int argc, char **argv)
{
	Accounts A;
	for (int i = 1; i < argc && A.n < 12; i++)
	{
		char *f[10]; int k = 0; char buf[1000]; scpy (buf, argv[i], sizeof buf);
		f[k++] = buf; for (char *p = buf; *p && k < 10; p++) if (*p == '|') { *p = 0; f[k++] = p + 1; }
		if (k < 9) { printf ("mkaccounts: %s: 9 fields\n", argv[i]); return 1; }
		Account &a = A.a[A.n]; account_defaults (a);
		A.new_id (a.id, sizeof a.id); A.n++;
		scpy (a.label, f[0], sizeof a.label); scpy (a.email, f[1], sizeof a.email); scpy (a.name, f[2], sizeof a.name);
		a.kind = !strcmp (f[3], "pop3") ? K_POP3 : K_IMAP;
		scpy (a.inHost, f[4], sizeof a.inHost); scpy (a.outHost, f[4], sizeof a.outHost);
		a.inPort = atoi (f[5]); a.outPort = atoi (f[6]); a.inSec = a.outSec = SEC_NONE;
		scpy (a.inUser, a.email, sizeof a.inUser); scpy (a.inSecret, f[7], sizeof a.inSecret);
		a.colour = (unsigned) strtoul (f[8] + (f[8][0] == '#'), 0, 16);
		if (k > 9) a.provider = !strcmp (f[9], "gmail") ? PV_GMAIL : !strcmp (f[9], "outlook") ? PV_OUTLOOK : PV_OTHER;
		a.checkMinutes = 0;
	}
	if (!A.save () || !A.save_secrets ()) { printf ("mkaccounts: not written\n"); return 1; }
	// the contacts
	Contacts c; c.make_form ();
	static const char *const P[][5] = {
		{ "Marie Dubois", "marie.dubois@example.org", "+32 475 12 34 56", "Atelier Lumen", "14 avenue des Tilleuls\n1180 Uccle" },
		{ "Jonas Peeters", "jonas@brouwerij-peeters.example", "+32 15 21 43 65", "Brouwerij Peeters", "Kerkstraat 3\n2800 Mechelen" },
		{ "Sofia Rinaldi", "sofia.rinaldi@example.it", "+39 347 123 4567", "", "Via Roma 21\n10121 Torino" },
		{ "Anna Lefèvre", "anna@example.org", "+32 486 98 76 54", "", "" },
		{ "Björn Müller", "bjorn@example.de", "+49 30 1234567", "Müller Design", "Torstraße 10\n10119 Berlin" },
		{ "Printshop Mechelen", "hello@printshop.example", "+32 15 11 22 33", "Printshop Mechelen", "" } };
	for (unsigned i = 0; i < sizeof P / sizeof P[0]; i++)
	{
		Contact x; memset (&x, 0, sizeof x);
		scpy (x.name, P[i][0], sizeof x.name); scpy (x.email, P[i][1], sizeof x.email); scpy (x.mobile, P[i][2], sizeof x.mobile);
		scpy (x.company, P[i][3], sizeof x.company); scpy (x.address, P[i][4], sizeof x.address);
		if (i == 0) { scpy (x.birthday, "14/03/1984", sizeof x.birthday); scpy (x.notes, "Designer; the labels for the brewery.", sizeof x.notes); }
		c.add (x);
	}
	if (!c.save ()) { printf ("mkaccounts: contacts not written\n"); return 1; }
	return 0;
}
