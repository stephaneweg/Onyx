//
// signin.h -- the window before the account is there, Messenger's sign-in: the sky, the two buddies in their
// glass frame, then one step at a time -- the app's key (api_id, api_hash: my.telegram.org's, once), the
// phone number and "Sign in as" (Online / Appear offline), the code Telegram sent, the cloud password, a new
// account's name. The server's refusals in plain words.
//
// MIT licence.
//
#ifndef TG_SIGNIN_H
#define TG_SIGNIN_H

#include "chatinput.h"

static void save_api (int id, const char *hash);

// The server's error in the user's words.
static void friendly_error (const char *e, int wait, char *out, int cap)
{
	struct { const char *code, *text; } t[] = {
		{ "PHONE_NUMBER_INVALID", TRN ("This phone number is not valid. Give it with its country code: +33 6 12 34 56 78.") },
		{ "PHONE_CODE_INVALID", TRN ("This code is not the right one.") },
		{ "PHONE_CODE_EMPTY", TRN ("Type the code Telegram sent.") },
		{ "PHONE_CODE_EXPIRED", TRN ("This code has expired: ask for a new one (Use another number).") },
		{ "PASSWORD_HASH_INVALID", TRN ("This password is not the right one.") },
		{ "PHONE_NUMBER_BANNED", TRN ("Telegram has banned this phone number.") },
		{ "PHONE_NUMBER_FLOOD", TRN ("Too many codes asked for this number: try again later.") },
		{ "PHONE_PASSWORD_FLOOD", TRN ("Too many tries: try again later.") },
		{ "API_ID_INVALID", TRN ("This api_id and api_hash are not valid: check them at my.telegram.org.") },
		{ "API_ID_PUBLISHED_FLOOD", TRN ("This api_id cannot be used: make your own at my.telegram.org.") },
		{ "FIRSTNAME_INVALID", TRN ("This first name is not valid.") },
		{ "PAYMENT_REQUIRED", TRN ("Telegram asks for a payment to sign in with this number, which this app cannot do.") },
		{ "PASSWORD_PARAMS", TRN ("Telegram sent a kind of password this app does not know.") },
		{ "AUTH_KEY_UNREGISTERED", TRN ("You were signed out: sign in again.") },
		{ "SESSION_REVOKED", TRN ("This session was closed from another device: sign in again.") } };
	for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
		if (!strcmp (e, t[i].code)) { snprintf (out, (size_t) cap, "%s", TR (t[i].text)); return; }
	if (!strncmp (e, "FLOOD_WAIT", 10))
	{
		if (wait >= 120) snprintf (out, (size_t) cap, TR ("Too many tries: wait %d minutes."), (wait + 59) / 60);
		else snprintf (out, (size_t) cap, TR ("Too many tries: wait %d seconds."), wait);
		return;
	}
	snprintf (out, (size_t) cap, TR ("Telegram says: %s"), e);
}

class SignIn : public Widget
{
public:
	Textbox *f1, *f2;
	Button *go;
	int shown;			// the state the page was made for

	SignIn (int l, int t, int w, int h) : Widget (l, t, w, h), shown (-1), m_linkHot (false), m_statusHot (false), m_down (false)
	{
		f1 = new Textbox (0, 0, 300, 28, "");
		f2 = new Textbox (0, 0, 300, 28, "");
		go = new Button (0, 0, 140, 32, TR ("Sign in"), [] (Widget &) { extern SignIn *g_signin; g_signin->submit (); });
		f1->cb = enterKey; f2->cb = enterKey;
		f1->maxLen = 120; f2->maxLen = 120;
		addChild (f1); addChild (f2); addChild (go);
	}

	void tick ()
	{
		if (shown != (int) g_c.state) setup ();
		bool busy = g_c.busy;
		if (busy != m_busy) { m_busy = busy; go->disabled = busy; snprintf (go->text, sizeof go->text, "%s", busy ? TR ("Please wait...") : goLabel ()); go->invalidate (true); invalidate (true); }
		if (m_rev != g_c.rev) { m_rev = g_c.rev; invalidate (true); }
		if (busy && ++m_spin % 6 == 0) invalidate (true);
	}

	void submit ()
	{
		if (g_c.busy) return;
		switch (g_c.state)
		{
		case tg::AS_NEED_API:
		{
			int id = atoi (f1->text);
			const char *h = f2->text;
			int hl = (int) strlen (h);
			if (id <= 0 || hl != 32) { snprintf (m_local, sizeof m_local, "%s", TR ("The api_id is a number, the api_hash 32 letters and digits.")); invalidate (true); return; }
			m_local[0] = 0;
			save_api (id, h);
			break;
		}
		case tg::AS_PHONE:
			if (strlen (f1->text) < 6) { snprintf (m_local, sizeof m_local, "%s", TR ("Type your phone number with its country code.")); invalidate (true); return; }
			m_local[0] = 0;
			g_c.sendCode (f1->text);
			break;
		case tg::AS_CODE: if (f1->text[0]) g_c.signIn (f1->text); break;
		case tg::AS_PASSWORD: if (f1->text[0]) g_c.checkPassword (f1->text); break;
		case tg::AS_SIGNUP: if (f1->text[0]) g_c.signUp (f1->text, f2->text); break;
		default: break;
		}
		invalidate (true);
	}

	void onDraw () override
	{
		Canvas &cv = canvas;
		sky (cv, 0, 0, width, height, false);
		// a second, larger swoosh low on the page
		VPath p;
		p.ellipse (V (width / 2), V (height + height / 5), V (width), V (height / 3)); p.fill (cv, 0xFFFFFF, 120);
		int cx = width / 2, s = 112;
		avatar_default (cv, cx - s / 2, m_top, s, g_c.state == tg::AS_PHONE && !m_offline ? TC_ONLINE : 0x3A8EE6);
		int y = m_top + s + 22;
		const char *title = TR ("Sign in to Telegram");
		switch (g_c.state)
		{
		case tg::AS_NEED_API: title = TR ("Welcome to Telegram"); break;
		case tg::AS_CODE: title = TR ("Enter the code"); break;
		case tg::AS_PASSWORD: title = TR ("Your cloud password"); break;
		case tg::AS_SIGNUP: title = TR ("Your name"); break;
		default: break;
		}
		int tw = ftw (g_face.huge, title);
		ftext (cv, g_face.huge, cx - tw / 2, y, title, TC_TITLE);
		y += g_face.huge->height () + 8;
		// the explanation
		char ex[600];
		explain (ex, sizeof ex);
		Rich r;
		rich_layout (r, g_face.ui, ex, COLW, 16, false);
		rich_draw (cv, r, g_face.ui, ex, cx - COLW / 2, y, TC_GREY);
		// the fields' labels
		const char *l1 = 0, *l2 = 0;
		labels (&l1, &l2);
		if (l1) ftext (cv, g_face.ui, f1->left, f1->top - g_face.ui->height () - 3, l1, TC_INK, 2);
		if (l2 && !f2->hidden) ftext (cv, g_face.ui, f2->left, f2->top - g_face.ui->height () - 3, l2, TC_INK, 2);
		// "Sign in as: Online"
		if (g_c.state == tg::AS_PHONE)
		{
			int sy = f1->top + f1->height + 12;
			ftext (cv, g_face.ui, f1->left, sy, TR ("Sign in as:"), TC_INK);
			int sx = f1->left + ftw (g_face.ui, TR ("Sign in as:")) + 8;
			const char *st = m_offline ? TR ("Appear offline") : TR ("Online");
			int sw = ftw (g_face.ui, st) + 40;
			if (m_statusHot) { uk_rbox (cv, sx - 4, sy - 3, sw, 22, 3, TC_HOT_TOP, TC_HOT_BOT); uk_rline (cv, sx - 4, sy - 3, sw, 22, 3, TC_HOT_RIM); }
			buddy (cv, sx, sy, 15, m_offline ? TC_OFFLINE : TC_ONLINE);
			ftext (cv, g_face.ui, sx + 20, sy, st, TC_LINK);
			uk_glyph (cv, WKG_CHEV_DOWN, sx + sw - 12, sy + 8, 7, TC_GREY);
			m_statusBox[0] = sx - 4; m_statusBox[1] = sy - 3; m_statusBox[2] = sw; m_statusBox[3] = 22;
		}
		// the error
		char err[300] = "";
		if (m_local[0]) snprintf (err, sizeof err, "%s", m_local);
		else if (g_c.error[0]) friendly_error (g_c.error, g_c.floodWait, err, sizeof err);
		if (err[0])
		{
			Rich er; rich_layout (er, g_face.ui, err, COLW, 16, false);
			int ey = go->top - er.height () - 8;
			rich_draw (cv, er, g_face.ui, err, cx - COLW / 2, ey, TC_BUSY);
		}
		// the busy spinner
		if (g_c.busy)
		{
			int sx = go->left + go->width + 16, sy = go->top + go->height / 2;
			for (int i = 0; i < 8; i++)
			{
				int a = i * 45 - (m_spin / 6) * 45;
				VPath d; d.circle (V (sx) + uk_cos (a) * 8 * 16 / 16384, V (sy) - uk_sin (a) * 8 * 16 / 16384, V (2));
				d.fill (cv, 0x3A8EE6, 60 + i * 24);
			}
		}
		// the link under the button
		const char *link = linkText ();
		if (link)
		{
			int lw = ftw (g_face.ui, link), lx = cx - lw / 2, ly = go->top + go->height + 14;
			ftext (cv, g_face.ui, lx, ly, link, TC_LINK);
			if (m_linkHot) cv.fillRect (lx, ly + g_face.ui->ascent () + 2, lw, 1, TC_LINK);
			m_linkBox[0] = lx; m_linkBox[1] = ly; m_linkBox[2] = lw; m_linkBox[3] = g_face.ui->height ();
		}
		// the foot
		const char *foot = TR ("Telegram for Onyx - an unofficial client, made on Telegram's open API.");
		int fw = ftw (g_face.small, foot);
		ftext (cv, g_face.small, cx - fw / 2, height - 24, foot, TC_GREY, 1);
	}

	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) br; (void) bm; (void) wheel;
		if (mx < 0) { m_linkHot = m_statusHot = false; invalidate (true); return false; }
		bool lh = linkText () && mx >= m_linkBox[0] && mx < m_linkBox[0] + m_linkBox[2] && my >= m_linkBox[1] && my < m_linkBox[1] + m_linkBox[3];
		bool sh = g_c.state == tg::AS_PHONE && mx >= m_statusBox[0] && mx < m_statusBox[0] + m_statusBox[2] && my >= m_statusBox[1] && my < m_statusBox[1] + m_statusBox[3];
		if (lh != m_linkHot || sh != m_statusHot) { m_linkHot = lh; m_statusHot = sh; invalidate (true); }
		uk_cursor (lh || sh ? KAPI_CURSOR_HAND : KAPI_CURSOR_ARROW);
		if (bl && !m_down)
		{
			if (lh) linkClicked ();
			if (sh) { m_offline = !m_offline; invalidate (true); }
		}
		m_down = bl != 0;
		return true;
	}
	void resizeTo (int w, int h) override { Widget::resizeTo (w, h); place (); }
	bool offline () const { return m_offline; }

private:
	enum { COLW = 340 };
	bool m_linkHot, m_statusHot, m_down;
	bool m_busy = false, m_offline = false;
	int m_top = 40;
	int m_linkBox[4] = { 0, 0, 0, 0 }, m_statusBox[4] = { 0, 0, 0, 0 };
	char m_local[200] = "";
	unsigned m_rev = 0;
	int m_spin = 0;

	static void enterKey (Widget &) { extern SignIn *g_signin; g_signin->submit (); }

	const char *goLabel ()
	{
		switch (g_c.state)
		{
		case tg::AS_NEED_API: return TR ("Continue");
		case tg::AS_PHONE: return TR ("Sign in");
		case tg::AS_SIGNUP: return TR ("Create my account");
		default: return TR ("Next");
		}
	}
	const char *linkText ()
	{
		switch (g_c.state)
		{
		case tg::AS_CODE: case tg::AS_PASSWORD: case tg::AS_SIGNUP: return TR ("Use another number");
		default: return 0;
		}
	}
	void linkClicked () { g_c.backToPhone (); m_local[0] = 0; }

	void labels (const char **l1, const char **l2)
	{
		*l1 = *l2 = 0;
		switch (g_c.state)
		{
		case tg::AS_NEED_API: *l1 = "api_id"; *l2 = "api_hash"; break;
		case tg::AS_PHONE: *l1 = TR ("Phone number"); break;
		case tg::AS_CODE: *l1 = TR ("Code"); break;
		case tg::AS_PASSWORD: *l1 = TR ("Password"); break;
		case tg::AS_SIGNUP: *l1 = TR ("First name"); *l2 = TR ("Last name (optional)"); break;
		default: break;
		}
	}
	void explain (char *out, int cap)
	{
		out[0] = 0;
		switch (g_c.state)
		{
		case tg::AS_NEED_API:
			snprintf (out, (size_t) cap, "%s", TR ("Telegram asks every app for its own key. Once only: sign in at my.telegram.org with your phone, open \"API development tools\", create an app (any name), then copy its api_id and api_hash here."));
			break;
		case tg::AS_PHONE:
			snprintf (out, (size_t) cap, "%s", TR ("Type your phone number with its country code. Telegram will send you a code, in the Telegram app on your phone or by SMS."));
			break;
		case tg::AS_CODE:
			if (!strcmp (g_c.codeType, "app")) snprintf (out, (size_t) cap, TR ("Telegram sent a code to your Telegram app on another device (+%s)."), g_c.phone[0] == '+' ? g_c.phone + 1 : g_c.phone);
			else if (!strcmp (g_c.codeType, "sms")) snprintf (out, (size_t) cap, TR ("Telegram sent a code by SMS to +%s."), g_c.phone[0] == '+' ? g_c.phone + 1 : g_c.phone);
			else if (!strcmp (g_c.codeType, "call")) snprintf (out, (size_t) cap, TR ("Telegram is calling +%s to tell you a code."), g_c.phone[0] == '+' ? g_c.phone + 1 : g_c.phone);
			else snprintf (out, (size_t) cap, "%s", TR ("Telegram sent you a code."));
			break;
		case tg::AS_PASSWORD:
			if (g_c.passwordHint[0]) snprintf (out, (size_t) cap, TR ("Your account is protected by a password (two-step verification). Its hint: %s"), g_c.passwordHint);
			else snprintf (out, (size_t) cap, "%s", TR ("Your account is protected by a password (two-step verification)."));
			break;
		case tg::AS_SIGNUP:
			snprintf (out, (size_t) cap, "%s", TR ("This number has no Telegram account yet. Your name makes one: it is what your contacts will see."));
			break;
		default: break;
		}
	}
	// the fields for the state
	void setup ()
	{
		shown = (int) g_c.state;
		f1->setText (""); f2->setText ("");
		f1->password = g_c.state == tg::AS_PASSWORD;
		f2->hidden = !(g_c.state == tg::AS_NEED_API || g_c.state == tg::AS_SIGNUP);
		if (g_c.state == tg::AS_PHONE && g_c.phone[0]) f1->setText (g_c.phone);
		m_local[0] = 0;
		snprintf (go->text, sizeof go->text, "%s", goLabel ());
		place ();
		f1->setFocus ();
		invalidate (true);
	}
	void place ()
	{
		int cx = width / 2;
		m_top = height > 640 ? 50 : 26;
		int y = m_top + 112 + 22 + g_face.huge->height () + 8;
		char ex[600]; explain (ex, sizeof ex);
		Rich r; rich_layout (r, g_face.ui, ex, COLW, 16, false);
		y += r.height () + 34;
		f1->left = cx - COLW / 2; f1->top = y; f1->resizeTo (COLW, 28);
		y += 28;
		if (!f2->hidden) { y += 32; f2->left = cx - COLW / 2; f2->top = y; f2->resizeTo (COLW, 28); y += 28; }
		if (g_c.state == tg::AS_PHONE) y += 36;
		y += 52;
		go->left = cx - 70; go->top = y; go->resizeTo (140, 32);
		invalidate (true);
	}
};
SignIn *g_signin;

#endif
