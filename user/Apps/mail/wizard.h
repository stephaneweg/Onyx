//
// Apps/mail/wizard.h -- adding an account, step by step: the name and the address; the provider recognised by the
// address's domain (accounts.h) -- Gmail, iCloud, Yahoo: an app password, the page that makes it opened in Web
// (Jet); Outlook.com / Hotmail: Microsoft's sign-in by a code (on a phone or a PC); another one: its password, its
// servers guessed or typed by hand (IMAP or POP3, the security, the ports); then the settings tried (the worker:
// J_CHECK) and the account kept. And the settings of the accounts there are (the name, the signature, how often to
// look for mail, the password, removal).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _mail_wizard_h
#define _mail_wizard_h

#include "Apps/mail/app.h"

namespace mailapp {

// The browser a page is opened in: Jet.
#define WIZ_WEB		"SD:/apps/jet.app/main"

class Wizard;
static Wizard *g_wizard;			// open: the worker's answers go to it (on_result)
static void wiz_btn (Widget &w);

class Wizard : public Modal
{
public:
	enum { P_START, P_APPPW, P_OUTLOOK, P_PASSWORD, P_MANUAL, P_CHECKING, P_DONE };
	int page, prevPage;
	Account a; int provider;
	char err[300];
	Textbox *tName, *tEmail, *tPw, *tInHost, *tInPort, *tInUser, *tOutHost, *tOutPort, *tOutUser, *tOutPw;
	Dropdown *dKind, *dInSec, *dOutSec; Checkbox *cKeep;
	Button *bBack, *bNext, *bCancel, *bExtra;
	DeviceCode dc; bool haveCode, waiting, copied;
	static const int W = 600, H = 470, PAD = 26;

	Wizard () : Modal (W, H), page (P_START), prevPage (P_START), provider (-1), haveCode (false), waiting (false), copied (false)
	{
		account_defaults (a); err[0] = 0;
		tName = tEmail = tPw = tInHost = tInPort = tInUser = tOutHost = tOutPort = tOutUser = tOutPw = 0;
		dKind = dInSec = dOutSec = 0; cKeep = 0; bBack = bNext = bCancel = bExtra = 0;
		build ();
	}
	~Wizard () { g_wizard = 0; free_dead (); }
	// A page's widgets are rebuilt from a button's own callback (Button::onMouse still uses itself after it): the old ones
	// are kept, out of the window, until the next rebuild or the end -- never deleted under the button being clicked.
	Widget *dead[48]; int ndead = 0;
	void free_dead () { while (ndead) delete dead[--ndead]; }
	void clear_children () { free_dead (); while (firstChild) { Widget *c = firstChild; removeChild (c); if (ndead < 48) dead[ndead++] = c; else delete c; } tName = tEmail = tPw = tInHost = tInPort = tInUser = tOutHost = tOutPort = tOutUser = tOutPw = 0; dKind = dInSec = dOutSec = 0; cKeep = 0; bExtra = 0; }
	Textbox *field (int x, int y, int w, const char *v, bool pw = false) { Textbox *t = new Textbox (x, y, w, 30, v); t->password = pw; t->maxLen = 200; addChild (t); return t; }
	Button *button (int x, int y, int w, const char *l, int tag) { Button *b = new Button (x, y, w, 34, l, wiz_btn); b->tag = tag; addChild (b); return b; }
	void build ()
	{
		// the values typed so far kept across pages
		clear_children ();
		int y0 = titleH () + 20;
		switch (page)
		{
		case P_START:
			tName = field (PAD + 150, y0 + 70, W - 2 * PAD - 150, a.name);
			tEmail = field (PAD + 150, y0 + 112, W - 2 * PAD - 150, a.email);
			tName->setFocus ();
			break;
		case P_APPPW: case P_PASSWORD:
			tPw = field (PAD + 150, page == P_APPPW ? y0 + 190 : y0 + 90, W - 2 * PAD - 150, a.inSecret, true); tPw->setFocus ();
			if (page == P_APPPW) bExtra = button (PAD, y0 + 140, 250, "Open the page in Jet", 10);
			else
			{
				bExtra = button (PAD, y0 + 150, 200, "Settings by hand...", 11);
				// (a work or school address whose mail Microsoft 365 or Google Workspace hosts)
				button (PAD, y0 + 228, 260, "Microsoft 365 (Outlook)", 15); button (PAD + 272, y0 + 228, 260, "Google Workspace (Gmail)", 16);
			}
			break;
		case P_OUTLOOK:
			if (!haveCode) bExtra = button (PAD, y0 + 150, 200, "Get a code", 12);
			else { bExtra = button (PAD, y0 + 310, 250, "Open the page in Jet", 13); button (PAD + 262, y0 + 310, 180, "Copy the code", 14); }
			break;
		case P_MANUAL:
		{
			static const char *const KIND[2] = { "IMAP", "POP3" };
			static const char *const SEC[3] = { "SSL/TLS", "STARTTLS", "None" };
			int x = PAD + 110, w2 = 140;
			dKind = new Dropdown (x, y0 + 4, 120, 30, KIND, 2, a.kind, wiz_btn); dKind->tag = 20; addChild (dKind);
			char port[12];
			tInHost = field (x, y0 + 44, 230, a.inHost); snprintf (port, sizeof port, "%d", a.inPort); tInPort = field (x + 240, y0 + 44, 60, port);
			dInSec = new Dropdown (x + 310, y0 + 44, w2, 30, SEC, 3, a.inSec, wiz_btn); dInSec->tag = 21; addChild (dInSec);
			tInUser = field (x, y0 + 84, 230, a.inUser);
			tPw = field (x + 240, y0 + 84, W - PAD - x - 240, a.inSecret, true);
			tOutHost = field (x, y0 + 160, 230, a.outHost); snprintf (port, sizeof port, "%d", a.outPort); tOutPort = field (x + 240, y0 + 160, 60, port);
			dOutSec = new Dropdown (x + 310, y0 + 160, w2, 30, SEC, 3, a.outSec, wiz_btn); dOutSec->tag = 22; addChild (dOutSec);
			tOutUser = field (x, y0 + 200, 230, a.outUser);
			tOutPw = field (x + 240, y0 + 200, W - PAD - x - 240, a.outSecret, true);
			cKeep = new Checkbox (x, y0 + 244, 330, 26, "Leave the messages on the server (POP3)", a.popKeep, 0, bgColor ()); addChild (cKeep);
			cKeep->hidden = a.kind != K_POP3;
			break;
		}
		default: break;
		}
		int by = H - 50;
		bCancel = button (PAD, by, 100, "Cancel", 1);
		if (page != P_START && page != P_CHECKING && page != P_DONE) bBack = button (W - PAD - 230, by, 100, "Back", 2); else bBack = 0;
		const char *nl = page == P_DONE ? "Finish" : page == P_APPPW || page == P_PASSWORD || page == P_MANUAL ? "Sign in" : "Next";
		bNext = page == P_CHECKING || (page == P_OUTLOOK) ? 0 : button (W - PAD - 120, by, 120, nl, 3);
		if (page == P_DONE) { bCancel->hidden = true; }
		invalidate (true);
	}
	void read_fields ()
	{
		if (tName) scpy (a.name, tName->text, sizeof a.name);
		if (tEmail) { char e[160]; const char *s = tEmail->text; while (*s == ' ') s++; scpy (e, s, sizeof e); int n = (int) strlen (e); while (n && e[n - 1] == ' ') e[--n] = 0; scpy (a.email, e, sizeof a.email); }
		if (tPw) scpy (a.inSecret, tPw->text, sizeof a.inSecret);
		if (page == P_MANUAL)
		{
			a.kind = dKind->sel == 1 ? K_POP3 : K_IMAP;
			scpy (a.inHost, tInHost->text, sizeof a.inHost); a.inPort = atoi (tInPort->text); a.inSec = dInSec->sel; scpy (a.inUser, tInUser->text, sizeof a.inUser);
			scpy (a.outHost, tOutHost->text, sizeof a.outHost); a.outPort = atoi (tOutPort->text); a.outSec = dOutSec->sel; scpy (a.outUser, tOutUser->text, sizeof a.outUser);
			scpy (a.outSecret, tOutPw->text, sizeof a.outSecret);
			a.popKeep = cKeep->checked;
		}
	}
	void go (int p) { prevPage = page; page = p; err[0] = 0; build (); }
	void check ()
	{
		go (P_CHECKING);
		Job *j = new Job; memset (j, 0, sizeof *j); j->kind = J_CHECK; j->acct = a;
		g_m.worker.cancel = 0; g_m.worker.push (j);
	}
	void onButton (int tag) override
	{
		read_fields ();
		if (tag == 1) { g_m.worker.cancel = 1; close (0); return; }
		if (tag == 2) { go (page == P_MANUAL ? (prevPage == P_MANUAL ? P_START : prevPage) : P_START); return; }
		if (tag == 10) { const char *url = (a.provider == PV_GMAIL ? "https://myaccount.google.com/apppasswords" : a.provider == PV_ICLOUD ? "https://account.apple.com" : a.provider == PV_YAHOO ? "https://login.yahoo.com/account/security" : "https://www.fastmail.com/settings/security/devicekeys"); kapi_exec (WIZ_WEB, url); return; }
		if (tag == 11) { go (P_MANUAL); return; }
		if (tag == 15) { account_hosted (a, PV_OUTLOOK); a.inSecret[0] = 0; haveCode = false; go (P_OUTLOOK); return; }
		if (tag == 16) { account_hosted (a, PV_GMAIL); a.inSecret[0] = 0; provider = provider_of ("x@gmail.com"); go (P_APPPW); return; }
		// (Outlook: Microsoft's page opened here, the code copied to be pasted in it)
		if (tag == 13 && haveCode) { kapi_exec (WIZ_WEB, dc.verifyUri[0] ? dc.verifyUri : "https://login.microsoft.com/device"); return; }
		if (tag == 14 && haveCode) { ::clip_set_text (dc.userCode); copied = true; invalidate (true); return; }
		if (tag == 12) { waiting = true; Job *j = new Job; memset (j, 0, sizeof *j); j->kind = J_OAUTH_START; j->acct = a; g_m.worker.cancel = 0; g_m.worker.push (j); invalidate (true); return; }
		if (tag == 20) { int k = dKind->sel; bool pop = k == 1; if (pop != (a.kind == K_POP3)) { a.kind = pop ? K_POP3 : K_IMAP; char host[120]; const char *at = strrchr (a.email, '@'); snprintf (host, sizeof host, pop ? "pop.%s" : "imap.%s", at ? at + 1 : ""); if (provider < 0) { scpy (a.inHost, host, sizeof a.inHost); } a.inPort = pop ? 995 : 993; a.inSec = SEC_TLS; build (); } return; }
		if (tag == 21 || tag == 22) { if (tag == 21) { a.inSec = dInSec->sel; a.inPort = a.kind == K_POP3 ? (a.inSec == SEC_TLS ? 995 : 110) : (a.inSec == SEC_TLS ? 993 : 143); } else { a.outSec = dOutSec->sel; a.outPort = a.outSec == SEC_TLS ? 465 : 587; } build (); return; }
		if (tag != 3) return;
		switch (page)
		{
		case P_START:
		{
			if (!strchr (a.email, '@') || strchr (a.email, ' ')) { scpy (err, "Type your whole e-mail address (name@example.com).", sizeof err); invalidate (true); return; }
			char nm[120]; scpy (nm, a.name, sizeof nm); char em[160]; scpy (em, a.email, sizeof em);
			int keepKind = a.kind;
			account_defaults (a); scpy (a.name, nm, sizeof a.name); a.kind = keepKind; account_guess (a, em);
			provider = provider_of (em);
			if (provider >= 0 && PROVIDERS[provider].auth == AU_OAUTH) go (P_OUTLOOK);
			else if (provider >= 0 && PROVIDERS[provider].note[0]) go (P_APPPW);
			else go (P_PASSWORD);
			return;
		}
		case P_APPPW: case P_PASSWORD: case P_MANUAL:
			if (!a.inSecret[0]) { scpy (err, "Type the password.", sizeof err); invalidate (true); return; }
			// (Gmail's app password is shown in groups: the spaces off)
			if (page == P_APPPW) { char t[200]; int k = 0; for (const char *p = a.inSecret; *p; p++) if (*p != ' ') t[k++] = *p; t[k] = 0; scpy (a.inSecret, t, sizeof a.inSecret); }
			check (); return;
		case P_DONE: finish (); close (1); return;
		}
	}
	// the worker's answers
	void answer (Result *r)
	{
		if (r->kind == J_OAUTH_START)
		{
			waiting = false;
			if (!r->ok) { scpy (err, r->err, sizeof err); invalidate (true); return; }
			dc = r->dc; haveCode = true; copied = false; build ();
			Job *j = new Job; memset (j, 0, sizeof *j); j->kind = J_OAUTH_POLL; j->acct = a; j->dc = dc; g_m.worker.push (j);
			return;
		}
		if (r->kind == J_OAUTH_POLL)
		{
			if (!r->ok || r->oauthState != 1) { haveCode = false; scpy (err, r->err[0] ? r->err : "The sign-in did not finish.", sizeof err); build (); return; }
			scpy (a.access, r->access, sizeof a.access); scpy (a.refresh, r->refresh, sizeof a.refresh); a.expires = r->expires;
			check ();
			return;
		}
		if (r->kind == J_CHECK)
		{
			if (r->ok) { if (r->tokens) { scpy (a.access, r->access, sizeof a.access); scpy (a.refresh, r->refresh, sizeof a.refresh); a.expires = r->expires; } go (P_DONE); return; }
			int back = prevPage == P_CHECKING ? P_PASSWORD : prevPage;
			if (back == P_OUTLOOK) { haveCode = false; }
			// a password refused stays on its page; the rest (a server not found, a certificate...) goes to the servers
			bool pw = ifind (r->err, "refused") || ifind (r->err, "credentials") || ifind (r->err, "password") || ifind (r->err, "AUTH");
			go (pw || back == P_OUTLOOK ? back : P_MANUAL);
			scpy (err, r->err, sizeof err); invalidate (true);
		}
	}
	void finish ()
	{
		if (g_m.accts.n >= 12) return;
		g_m.accts.new_id (a.id, sizeof a.id);
		if (!a.label[0]) scpy (a.label, "Mail", sizeof a.label);
		a.colour = ACCOUNT_COLOURS[g_m.accts.n % 8];
		if (a.provider == PV_GMAIL) a.colour = 0xD93025; else if (a.provider == PV_OUTLOOK) a.colour = 0x0F6CBD;
		g_m.accts.a[g_m.accts.n++] = a;
		g_m.accts.save (); g_m.accts.save_secrets ();
		g_m.open_store (g_m.accts.n - 1);
		g_m.sync (g_m.accts.n - 1);
	}
	void para (int x, int y, int w, const char *s, unsigned c, int f = F_UI)
	{
		// a paragraph wrapped at w (UTF-8 words)
		UkFaceScope sc (f == F_UI ? 0 : g_face[f]);
		char line[400]; line[0] = 0; int ln = 0; const char *p = s; int lh = uk_fh () + 3;
		while (*p)
		{
			const char *ws = p; while (*p && *p != ' ' && *p != '\n') p++;
			char word[200]; int wn = (int) (p - ws); if (wn > 190) wn = 190; memcpy (word, ws, wn); word[wn] = 0;
			char test[600]; snprintf (test, sizeof test, "%s%s%s", line, ln ? " " : "", word);
			if (ln && uk_tw (test, 0) > w) { uk_text (canvas, x, y, line, c, 0); y += lh; scpy (line, word, sizeof line); }
			else scpy (line, test, sizeof line);
			ln = (int) strlen (line);
			if (*p == '\n') { uk_text (canvas, x, y, line, c, 0); y += lh; line[0] = 0; ln = 0; }
			if (*p) p++;
		}
		if (ln) uk_text (canvas, x, y, line, c, 0);
	}
	void onDraw () override
	{
		drawBox ("Add a mail account");
		int y0 = titleH () + 20; int w = W - 2 * PAD;
		unsigned dim = uk_mix (C_BG, C_TEXT, 150);
		switch (page)
		{
		case P_START:
			text (canvas, PAD, y0, "Your mail account", C_TEXT, F_H2, 1);
			para (PAD, y0 + 30, w, "Gmail, Outlook.com, iCloud, Yahoo and others are recognised by their address: their servers are filled in for you.", dim, F_SMALL);
			text_v (canvas, PAD, y0 + 70, 30, "Your name", C_TEXT);
			text_v (canvas, PAD, y0 + 112, 30, "E-mail address", C_TEXT);
			para (PAD + 150, y0 + 148, w - 150, "Your name is what the people you write to see.", dim, F_SMALL);
			break;
		case P_APPPW:
		{
			const Provider &v = PROVIDERS[provider];
			char t[120]; snprintf (t, sizeof t, "%s: an app password", v.label);
			text (canvas, PAD, y0, t, C_TEXT, F_H2, 1);
			para (PAD, y0 + 32, w, v.note, C_TEXT);
			para (PAD, y0 + 84, w, "Make one on the page below (in Jet), then type its letters here. Mail keeps it encrypted on this card.", dim, F_SMALL);
			text_v (canvas, PAD, y0 + 190, 30, "App password", C_TEXT);
			break;
		}
		case P_PASSWORD:
		{
			text (canvas, PAD, y0, a.email, C_TEXT, F_H2, 1, w);
			char t[300]; snprintf (t, sizeof t, "Mail will try %s (IMAP) and %s (SMTP).", a.inHost, a.outHost);
			para (PAD, y0 + 32, w, t, dim, F_SMALL);
			text_v (canvas, PAD, y0 + 90, 30, "Password", C_TEXT);
			para (PAD, y0 + 200, w, "A work or school address whose mail Microsoft 365 or Google Workspace keeps? Choose it:", dim, F_SMALL);
			break;
		}
		case P_OUTLOOK:
			text (canvas, PAD, y0, provider < 0 ? "Microsoft 365: sign in at Microsoft" : "Outlook.com: sign in at Microsoft", C_TEXT, F_H2, 1);
			para (PAD, y0 + 32, w, "Microsoft does not take passwords from mail apps: you sign in on its own page, on a phone or a PC, with a code Mail gets for you.", C_TEXT);
			if (waiting) text (canvas, PAD, y0 + 160, "Asking Microsoft for a code...", dim);
			if (haveCode)
			{
				char t[300]; snprintf (t, sizeof t, "1. On a phone or a PC, open  %s", dc.verifyUri);
				text (canvas, PAD, y0 + 100, t, C_TEXT, F_UI, 1, w);
				text (canvas, PAD, y0 + 126, "2. Type this code:", C_TEXT, F_UI, 1);
				uk_fill_round (canvas, PAD, y0 + 154, w, 64, 8, C_FIELD);
				text_c (canvas, PAD, y0 + 154, w, 64, dc.userCode, C_ACCENT, F_H1, 1);
				text (canvas, PAD, y0 + 230, "3. Sign in with your Microsoft account and allow \"Onyx Mail\".", C_TEXT, F_UI, 1, w);
				para (PAD, y0 + 258, w, copied ? "The code is in the clipboard: paste it in Microsoft's page. Mail goes on by itself as soon as it is done." : "Mail goes on by itself as soon as it is done.", dim, F_SMALL);
			}
			break;
		case P_MANUAL:
		{
			int x = PAD + 110;
			text_v (canvas, PAD, y0 + 4, 30, "Incoming", C_TEXT, F_UI, 1);
			text_v (canvas, PAD, y0 + 44, 30, "Server, port", C_TEXT);
			text_v (canvas, PAD, y0 + 84, 30, "User, password", C_TEXT);
			text_v (canvas, PAD, y0 + 126, 30, "Outgoing (SMTP)", C_TEXT, F_UI, 1);
			text_v (canvas, PAD, y0 + 160, 30, "Server, port", C_TEXT);
			text_v (canvas, PAD, y0 + 200, 30, "User, password", C_TEXT);
			text (canvas, x, y0 + 232, "(empty: the same as for the incoming mail)", dim, F_TINY);
			(void) x;
			break;
		}
		case P_CHECKING:
		{
			text (canvas, PAD, y0, "Checking the settings...", C_TEXT, F_H2, 1);
			char t[200]; snprintf (t, sizeof t, "%s", g_m.worker.current[0] ? g_m.worker.current : "Connecting...");
			text (canvas, PAD, y0 + 40, t, dim);
			break;
		}
		case P_DONE:
			icon (canvas, I_CHECK, PAD, y0, 40, 0x188038);
			text (canvas, PAD + 54, y0 + 6, "Your account is ready", C_TEXT, F_H2, 1);
			{ char t[300]; snprintf (t, sizeof t, "%s  (%s)", a.email, a.kind == K_POP3 ? "POP3" : "IMAP"); text (canvas, PAD + 54, y0 + 36, t, dim); }
			para (PAD, y0 + 80, w, "Mail now fetches your messages. New mail is looked for every 5 minutes (Accounts and settings).", C_TEXT);
			break;
		}
		if (err[0])
		{
			int ey = H - 110;
			uk_fill_round (canvas, PAD, ey, w, 46, 6, uk_mix (C_BG, 0xD93025, 40));
			icon (canvas, I_WARN, PAD + 10, ey + 12, 20, 0xC5221F);
			para (PAD + 40, ey + 6, w - 50, err, C_TEXT, F_SMALL);
		}
	}
};
static void wiz_btn (Widget &w) { if (g_wizard) g_wizard->onButton (w.tag); }

// ---- the accounts' settings ---------------------------------------------------------------------------------------------------------
class SettingsBox;
static SettingsBox *g_settings;
static void set_btn (Widget &w);
class SettingsBox : public Modal
{
public:
	int cur;
	Textbox *tName, *tLabel, *tPw; Textarea *tSig; Dropdown *dEvery;
	HitList hits;
	static const int W = 680, H = 500, LW = 200, PAD = 20;
	SettingsBox () : Modal (W, H), cur (0) { tName = tLabel = tPw = 0; tSig = 0; dEvery = 0; build (); }
	~SettingsBox () { g_settings = 0; free_dead (); }
	// (rebuilt from a button's callback too: the old widgets kept until the next rebuild, as the Wizard's)
	Widget *dead[48]; int ndead = 0;
	void free_dead () { while (ndead) delete dead[--ndead]; }
	void build ()
	{
		free_dead ();
		while (firstChild) { Widget *c = firstChild; removeChild (c); if (ndead < 48) dead[ndead++] = c; else delete c; }
		int x = LW + PAD, y = titleH () + 16, w = W - x - PAD;
		Button *b;
		b = new Button (PAD, H - 48, LW - PAD, 32, "Add an account...", set_btn); b->tag = 1; addChild (b);
		b = new Button (W - PAD - 100, H - 48, 100, 32, "Close", set_btn); b->tag = 2; addChild (b);
		if (cur >= g_m.accts.n) { tName = tLabel = tPw = 0; tSig = 0; dEvery = 0; invalidate (true); return; }
		Account &a = g_m.accts.a[cur];
		tName = new Textbox (x + 130, y + 40, w - 130, 30, a.name); addChild (tName);
		tLabel = new Textbox (x + 130, y + 78, w - 130, 30, a.label); addChild (tLabel);
		static const char *const EV[6] = { "By hand only", "Every minute", "Every 5 minutes", "Every 15 minutes", "Every 30 minutes", "Every hour" };
		static const int EVM[6] = { 0, 1, 5, 15, 30, 60 };
		int sel = 2; for (int i = 0; i < 6; i++) if (EVM[i] == a.checkMinutes) sel = i;
		dEvery = new Dropdown (x + 130, y + 116, 200, 30, EV, 6, sel, 0); addChild (dEvery);
		tSig = new Textarea (x, y + 182, w, 90, 600); tSig->setContent (a.signature); addChild (tSig);
		if (a.auth == AU_PASSWORD) { tPw = new Textbox (x + 130, y + 290, w - 130, 30, ""); tPw->password = true; addChild (tPw); } else tPw = 0;
		b = new Button (W - PAD - 220, H - 48, 110, 32, "Remove...", set_btn); b->tag = 3; addChild (b);
		invalidate (true);
	}
	void save_cur ()
	{
		if (cur >= g_m.accts.n || !tName) return;
		Account &a = g_m.accts.a[cur];
		scpy (a.name, tName->text, sizeof a.name);
		if (tLabel->text[0]) scpy (a.label, tLabel->text, sizeof a.label);
		static const int EVM[6] = { 0, 1, 5, 15, 30, 60 };
		a.checkMinutes = EVM[dEvery->sel];
		scpy (a.signature, tSig->content (), sizeof a.signature);
		if (tPw && tPw->text[0]) { scpy (a.inSecret, tPw->text, sizeof a.inSecret); tPw->setText (""); g_m.accts.save_secrets (); }
		g_m.accts.save ();
	}
	void onButton (int tag) override
	{
		if (tag == 2) { save_cur (); close (0); return; }
		if (tag == 1) { save_cur (); close (2); return; }
		if (tag == 3 && cur < g_m.accts.n)
		{
			char q[300]; snprintf (q, sizeof q, "Remove %s from Mail? Its messages stay on the server; Mail's copy on this card goes.", g_m.accts.a[cur].email);
			if (uk_messagebox ("Mail", q, MB_YESNO) != 1) return;
			delete g_m.stores[cur];
			for (int i = cur; i < g_m.accts.n - 1; i++) { g_m.accts.a[i] = g_m.accts.a[i + 1]; g_m.stores[i] = g_m.stores[i + 1]; g_m.stores[i]->acct = &g_m.accts.a[i]; }
			g_m.accts.n--; g_m.stores[g_m.accts.n] = 0;
			g_m.accts.save (); g_m.accts.save_secrets ();
			if (cur >= g_m.accts.n) cur = g_m.accts.n - 1; if (cur < 0) cur = 0;
			build ();
		}
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		static bool was; bool down = bl && !was; was = bl;
		if (down) { const Hit *h = hits.at (mx, my); if (h) { save_cur (); cur = h->a; build (); return true; } }
		return Modal::onMouse (mx, my, bl, br, bm, wheel);
	}
	void onDraw () override
	{
		drawBox ("Accounts and settings");
		hits.clear ();
		int y = titleH () + 16;
		uk_fill_round (canvas, PAD, y, LW - PAD, H - y - 64, 6, C_FIELD);
		for (int i = 0; i < g_m.accts.n; i++)
		{
			int ry = y + 4 + i * 46;
			if (i == cur) uk_fill_round (canvas, PAD + 4, ry, LW - PAD - 8, 42, 5, col_sel ());
			uk_fill_round (canvas, PAD + 10, ry + 12, 18, 18, 4, 0xFF000000u | g_m.accts.a[i].colour);
			text (canvas, PAD + 36, ry + 4, g_m.accts.a[i].label, C_FIELD_TEXT, F_UI, 1, LW - PAD - 46);
			text (canvas, PAD + 36, ry + 22, g_m.accts.a[i].email, col_dim (), F_SMALL, 0, LW - PAD - 46);
			hits.add (PAD, ry, LW - PAD, 42, 1, i);
		}
		if (!g_m.accts.n) { text (canvas, LW + PAD, y + 10, "No account yet.", C_TEXT); return; }
		Account &a = g_m.accts.a[cur];
		int x = LW + PAD; unsigned dim = uk_mix (C_BG, C_TEXT, 150);
		text (canvas, x, y, a.email, C_TEXT, F_H2, 1, W - x - PAD);
		text_v (canvas, x, y + 40, 30, "Your name", C_TEXT);
		text_v (canvas, x, y + 78, 30, "Shown as", C_TEXT);
		text_v (canvas, x, y + 116, 30, "New mail", C_TEXT);
		text (canvas, x, y + 160, "Signature (added to what you write)", C_TEXT);
		if (a.auth == AU_PASSWORD) text_v (canvas, x, y + 290, 30, "New password", C_TEXT);
		char srv[400];
		snprintf (srv, sizeof srv, "%s %s:%d (%s)  \xC2\xB7  SMTP %s:%d (%s)%s", a.kind == K_POP3 ? "POP3" : "IMAP", a.inHost, a.inPort, a.inSec == SEC_TLS ? "SSL/TLS" : a.inSec == SEC_STARTTLS ? "STARTTLS" : "plain",
			a.outHost, a.outPort, a.outSec == SEC_TLS ? "SSL/TLS" : a.outSec == SEC_STARTTLS ? "STARTTLS" : "plain", a.auth == AU_OAUTH ? "  \xC2\xB7  Microsoft sign-in" : "");
		text (canvas, x, y + 334, srv, dim, F_SMALL, 0, W - x - PAD);
	}
};
static void set_btn (Widget &w) { if (g_settings) g_settings->onButton (w.tag); }

} // namespace mailapp

#endif
