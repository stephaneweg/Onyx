//
// views.h -- Courier's window: the top bar (the app, New, Import, the environment chosen), the
// sidebar (rail.h), the open tabs, and the three editors a tab shows -- a request (its URL bar, its
// Params / Authorization / Headers / Body / Tests / Settings, its response below: Body / Cookies /
// Headers / Test Results / Console), an environment, a collection.
//
#ifndef _courier_views_h
#define _courier_views_h

#include "app.h"

namespace cr {

// ---- a container: its background, and its children placed by a function ------------------------------------------
class Pane : public Widget
{
public:
	unsigned bg;
	void (*lay) (Pane &);
	void (*paint) (Pane &);
	Pane (unsigned bg_, void (*lay_) (Pane &) = 0, void (*paint_) (Pane &) = 0) : Widget (0, 0, 16, 16), bg (bg_), lay (lay_), paint (paint_) {}
	unsigned bgColor () override { return bg; }
	void onDraw () override { canvas.clear (bg); if (paint) paint (*this); }
	void layout () override { if (lay) lay (*this); else Widget::layout (); }
};
static inline void place (Widget *w, int x, int y, int wd, int ht) { if (!w) return; w->left = x; w->top = y; w->resizeTo (imax (1, wd), imax (1, ht)); }
static inline void show (Widget *w, bool on) { if (w && w->hidden == on) { w->hidden = !on; if (w->parent) w->parent->invalidate (true); } }

// a paragraph wrapped into w px at (x, y); returns the y below it
static inline int text_wrap (Canvas &cv, int x, int y, int w, const char *s, unsigned c, int style = 0, int lineGap = 4)
{
	Str line; const char *p = s;
	int fh = wk_fh ();
	while (*p)
	{
		const char *e = p; const char *cut = 0;
		Str t;
		while (*e && *e != '\n')
		{
			t.set (p, (int) (e - p + 1));
			if (wk_tw (t.c (), style) > w && cut) break;
			if (*e == ' ') cut = e;
			e++;
		}
		const char *stop = (*e && *e != '\n' && cut) ? cut : e;
		line.set (p, (int) (stop - p));
		wk_text (cv, x, y, line.c (), c, style);
		y += fh + lineGap;
		p = stop;
		if (*p == ' ' || *p == '\n') p++;
	}
	return y;
}

// ---- the auth editor (a request's, a folder's, a collection's) ----------------------------------------------------------
class AuthPanel : public Pane
{
public:
	Auth *auth;				// the one edited
	bool forColl;				// a collection's: no "inherit"
	Choice *type, *where;
	LineEdit *f1, *f2;
	Action onChange;
	const char *inheritText;		// what a request inherits (written in the panel)
	Str m_inherit;
	AuthPanel (bool coll) : Pane (C_FIELD), auth (0), forColl (coll), type (0), where (0), f1 (0), f2 (0), onChange (0), inheritText (0)
	{
		lay = doLayout; paint = doPaint;
		type = new Choice (0, 0, 10, 10, forColl ? AUTH_NAMES + 1 : AUTH_NAMES, forColl ? AUTH_COUNT - 1 : AUTH_COUNT, 0);
		type->onChange = typeChanged; addChild (type);
		static const char *const WHERE[] = { "Header", "Query Params" };
		where = new Choice (0, 0, 10, 10, WHERE, 2, 0); where->onChange = whereChanged; addChild (where);
		f1 = new LineEdit (0, 0, 10, 10); f1->onChange = fieldChanged; addChild (f1);
		f2 = new LineEdit (0, 0, 10, 10); f2->onChange = fieldChanged; addChild (f2);
	}
	void bind (Auth *a)
	{
		auth = a;
		if (!a) return;
		type->sel = forColl ? imax (0, a->type - 1) : a->type;
		where->sel = a->inQuery ? 1 : 0;
		sync ();
	}
	void sync ()
	{
		if (!auth) return;
		int t = auth->type;
		show (f1, t == AUTH_BEARER || t == AUTH_BASIC || t == AUTH_APIKEY);
		show (f2, t == AUTH_BASIC || t == AUTH_APIKEY);
		show (where, t == AUTH_APIKEY);
		f2->password = t == AUTH_BASIC;
		if (t == AUTH_BEARER) { f1->setText (auth->token.c ()); f1->placeholder = "Token"; }
		else if (t == AUTH_BASIC) { f1->setText (auth->user.c ()); f1->placeholder = "Username"; f2->setText (auth->pass.c ()); f2->placeholder = "Password"; }
		else if (t == AUTH_APIKEY) { f1->setText (auth->key.c ()); f1->placeholder = "Key (e.g. X-API-Key)"; f2->setText (auth->value.c ()); f2->placeholder = "Value"; }
		type->invalidate (true);
		layout (); invalidate (true);
	}
	static void doLayout (Pane &p)
	{
		AuthPanel &a = (AuthPanel &) p;
		int lx = 20, lw = imin (260, p.width / 3), rx = lx + lw + 30, rw = imax (120, p.width - rx - 24);
		place (a.type, lx, 44, lw, 30);
		int y = 44 + wk_fh () + 8;
		place (a.f1, rx + 110, 44 - 0, rw - 110, 30);
		place (a.f2, rx + 110, 44 + 44, rw - 110, 30);
		place (a.where, rx + 110, 44 + 88, imin (200, rw - 110), 30);
		(void) y;
	}
	static void doPaint (Pane &p)
	{
		AuthPanel &a = (AuthPanel &) p;
		if (!a.auth) return;
		int lx = 20, lw = imin (260, p.width / 3), rx = lx + lw + 30, rw = imax (120, p.width - rx - 24);
		text_at (p.canvas, lx, 18, "Auth Type", c_dim (), 2);
		static const char *const HELP[] = {
			"The authorization header is made from its folder's or its collection's auth.",
			"This request is sent without authorization.",
			"The token is sent as the Authorization header: Bearer <token>.",
			"The username and the password are sent as the Authorization header (Basic, base64).",
			"The key and its value are sent as a header, or added to the URL's query.",
		};
		text_wrap (p.canvas, lx, 88, lw, HELP[a.auth->type], c_dim (), 0);
		p.canvas.fillRect (lx + lw + 14, 14, 1, p.height - 28, c_line ());
		int t = a.auth->type;
		if (t == AUTH_BEARER) text_v (p.canvas, rx, 44, 30, "Token", C_FIELD_TEXT);
		if (t == AUTH_BASIC) { text_v (p.canvas, rx, 44, 30, "Username", C_FIELD_TEXT); text_v (p.canvas, rx, 88, 30, "Password", C_FIELD_TEXT); }
		if (t == AUTH_APIKEY) { text_v (p.canvas, rx, 44, 30, "Key", C_FIELD_TEXT); text_v (p.canvas, rx, 88, 30, "Value", C_FIELD_TEXT); text_v (p.canvas, rx, 132, 30, "Add to", C_FIELD_TEXT); }
		if (t == AUTH_INHERIT) text_wrap (p.canvas, rx, 50, rw, a.inheritText ? a.inheritText : "", C_FIELD_TEXT);
		if (t == AUTH_NONE) text_wrap (p.canvas, rx, 50, rw, "This request does not use any authorization.", c_dim ());
		if (t == AUTH_BEARER || t == AUTH_BASIC || t == AUTH_APIKEY)
			text_wrap (p.canvas, rx, 44 + (t == AUTH_BEARER ? 48 : t == AUTH_BASIC ? 92 : 136) + 6, rw,
				   "Tip: keep secrets in variables -- {{token}} -- in an environment.", c_faint ());
	}
	static void typeChanged (Widget &w)
	{
		AuthPanel &a = *(AuthPanel *) w.parent;
		if (!a.auth) return;
		a.auth->type = a.forColl ? a.type->sel + 1 : a.type->sel;
		a.sync ();
		if (a.onChange) a.onChange (a);
	}
	static void whereChanged (Widget &w)
	{
		AuthPanel &a = *(AuthPanel *) w.parent;
		if (!a.auth) return;
		a.auth->inQuery = a.where->sel == 1;
		if (a.onChange) a.onChange (a);
	}
	static void fieldChanged (Widget &w)
	{
		AuthPanel &a = *(AuthPanel *) w.parent;
		if (!a.auth) return;
		int t = a.auth->type;
		if (t == AUTH_BEARER) a.auth->token = a.f1->text;
		else if (t == AUTH_BASIC) { a.auth->user = a.f1->text; a.auth->pass = a.f2->text; }
		else if (t == AUTH_APIKEY) { a.auth->key = a.f1->text; a.auth->value = a.f2->text; }
		if (a.onChange) a.onChange (a);
	}
};

// ---- the response's head: the status, the time, the size ----------------------------------------------------------------
class RespStatus : public Widget
{
public:
	RespStatus () : Widget (0, 0, 10, 10) {}
	unsigned bgColor () override { return C_FIELD; }
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		Tab *t = cur_tab ();
		if (!t || !t->resp || t->job) return;
		Response &r = *t->resp;
		int x = width - 8;
		char sz[32], tm[32], st[96];
		if (r.status < 0) snprintf (st, sizeof st, "Error"); else snprintf (st, sizeof st, "%d %s", r.status, r.reason.c ());
		human_size (sz, sizeof sz, r.bodyLen);
		snprintf (tm, sizeof tm, "%d ms", r.msTotal);
		int ws = wk_tw (sz), wt = wk_tw (tm), wst = wk_tw (st, 2);
		// what fits: the status always, then the time, then the size
		bool showT = wst + 16 + 22 + wt + 8 <= width, showS = showT && wst + 16 + 44 + wt + ws + 8 <= width;
		if (showS) { x -= ws; text_v (canvas, x, 0, height, sz, on_field (C_OK)); x -= 22; wk_glyph (canvas, WKG_DOT, x + 11, height / 2, 4, c_faint ()); }
		if (showT) { x -= wt; text_v (canvas, x, 0, height, tm, on_field (C_OK)); x -= 22; wk_glyph (canvas, WKG_DOT, x + 11, height / 2, 4, c_faint ()); }
		x -= wst + 16;
		if (x < 0) x = 0;
		unsigned sc = status_color (r.status);
		wk_rbox (canvas, x, (height - 24) / 2, wst + 16, 24, 6, wk_mix (C_FIELD, sc, 30), wk_mix (C_FIELD, sc, 30));
		text_v (canvas, x + 8, 0, height, st, sc, 2);
		m_tip.clear ();
		m_tip.addf ("Connect %d ms, first byte %d ms, total %d ms; %d bytes received (headers %d)%s%s", r.msConnect, r.msFirst, r.msTotal, r.wireLen, r.headLen,
			    r.redirects ? ", redirected" : "", r.truncated ? ", TRUNCATED" : "");
		tip = m_tip.c ();
	}
private:
	Str m_tip;
};

// ---- the test results --------------------------------------------------------------------------------------------------
class ResultsView : public Widget
{
public:
	int top;
	ResultsView () : Widget (0, 0, 10, 10), top (0) {}
	unsigned bgColor () override { return C_FIELD; }
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		Tab *t = cur_tab ();
		if (!t) return;
		int pass = 0, fail = 0, cap = 0;
		for (int i = 0; i < t->results.size (); i++) { if (t->results[i].capture) cap++; else if (t->results[i].pass) pass++; else fail++; }
		if (!t->results.size ())
		{
			empty_state (canvas, 0, 0, width, height, "No tests for this request", "Add checks in the request's Tests tab: status = 200, json.id exists...", C_FIELD);
			return;
		}
		char h[96]; snprintf (h, sizeof h, "%d / %d passed", pass, pass + fail);
		text_at (canvas, 16, 10, h, fail ? on_field (C_BAD) : on_field (C_OK), 2);
		if (cap) { char c[48]; snprintf (c, sizeof c, "%d variable%s set", cap, cap > 1 ? "s" : ""); text_at (canvas, 40 + wk_tw (h, 2), 10, c, c_dim ()); }
		int y = 38 - top * 32;
		for (int i = 0; i < t->results.size (); i++, y += 32)
		{
			if (y < 30) continue;
			if (y > height) break;
			TestResult &r = t->results[i];
			unsigned c = r.capture ? on_field (0x000053B8) : r.pass ? on_field (C_OK) : on_field (C_BAD);
			const char *tag = r.capture ? "SET" : r.pass ? "PASS" : "FAIL";
			int tw_ = wk_tw (tag, 2) + 14;
			wk_rbox (canvas, 16, y + 4, tw_, 22, 5, wk_mix (C_FIELD, c, 34), wk_mix (C_FIELD, c, 34));
			text_v (canvas, 23, y + 4, 22, tag, c, 2);
			text_fit (canvas, 30 + tw_, y + 4, 22, width / 2 - 40 - tw_, r.name.c (), C_FIELD_TEXT);
			text_fit (canvas, width / 2, y + 4, 22, width / 2 - 16, r.detail.c (), c_dim ());
			canvas.fillRect (16, y + 31, width - 32, 1, c_line ());
		}
	}
	bool onMouse (int mx, int, int, int, int, int wheel) override
	{
		if (mx < 0) return false;
		Tab *t = cur_tab ();
		if (wheel && t) { top = iclamp (top + wheel, 0, imax (0, t->results.size () - (height - 40) / 32)); invalidate (true); }
		return true;
	}
};

// ---- the sending overlay ---------------------------------------------------------------------------------------------------
class SendingView : public Pane
{
public:
	int phase; Btn *cancel;
	SendingView () : Pane (C_FIELD), phase (0)
	{
		lay = doLayout; paint = doPaint;
		cancel = new Btn (0, 0, 100, 32, "Cancel", BTN_SECONDARY, [] (Widget &) { app_cancel (); });
		addChild (cancel);
	}
	static void doLayout (Pane &p) { SendingView &s = (SendingView &) p; place (s.cancel, (p.width - 100) / 2, p.height / 2 + 40, 100, 32); }
	static void doPaint (Pane &p)
	{
		SendingView &s = (SendingView &) p;
		draw_spinner (p.canvas, p.width / 2, p.height / 2 - 28, 16, s.phase, C_ACCENT, C_FIELD);
		const char *t = "Sending request...";
		text_at (p.canvas, (p.width - wk_tw (t, 2)) / 2, p.height / 2 + 2, t, c_dim (), 2);
	}
};

// ================================================================================================================
// the widgets of the window (built once; the tabs are bound to them)
// ================================================================================================================
static struct UI
{
	Root *root;
	Pane *top, *main, *status;
	HSplitter *hsplit;
	DocTabs *docTabs;
	Pane *welcome;
	// the request
	Pane *reqView; VSplitter *vsplit; Pane *reqPane, *respPane;
	LineEdit *name; Btn *save, *code; Choice *method; LineEdit *url; Btn *send;
	TabBar *sections;
	Pane *secParams, *secAuth, *secHeaders, *secBody, *secTests, *secSettings;
	KVTable *params, *headers, *form, *urlenc, *tests, *captures;
	Btn *paramsBulk, *headersBulk, *bodyBulk;
	AuthPanel *auth;
	RadioRow *bodyMode; Choice *rawLang; Btn *beautify; CodeEdit *raw; Pane *binaryRow; Btn *binaryPick;
	ToggleSwitch *follow; LineEdit *timeout;
	// the response
	TabBar *respTabs; RespStatus *respStatus;
	Pane *respBody; SegmentedControl *bodyView; LineEdit *find; Btn *copyBody, *saveBody;
	CodeEdit *bodyText; ImageBox *bodyImage; Pane *respEmpty; SendingView *sending;
	KVTable *respHeaders, *respCookies; ResultsView *results; CodeEdit *console;
	KVList respHeadersRows, respCookieRows;
	// an environment
	Pane *envView; LineEdit *envName; Btn *envActive, *envExport; KVTable *envVars;
	// a collection
	Pane *collView; LineEdit *collName; Btn *collNewReq, *collExport; TabBar *collTabs; CodeEdit *collDesc; AuthPanel *collAuth; KVTable *collVars;
	// the top bar
	Choice *envPick; Btn *newBtn, *importBtn, *gear;
	const char *envNames[17];
	Str statusMsg; bool statusErr; unsigned statusT;
	bool binding;				// (the widgets being filled from a tab: their callbacks do not edit)
} ui;

enum { SEC_PARAMS, SEC_AUTH, SEC_HEADERS, SEC_BODY, SEC_TESTS, SEC_SETTINGS };
enum { RS_BODY, RS_COOKIES, RS_HEADERS, RS_TESTS, RS_CONSOLE };
enum { BV_PRETTY, BV_RAW, BV_PREVIEW };

// ---- the params <-> the URL's query ----------------------------------------------------------------------------------------
static void params_from_url (Request &r)
{
	KVList off, all;
	for (int i = 0; i < r.params.size (); i++) if (!r.params[i].on && !r.params[i].blank ()) off.push (r.params[i]);
	const char *qm = strchr (r.url.c (), '?');
	if (qm)
		for (const char *p = qm + 1; *p; )
		{
			const char *e = p; while (*e && *e != '&' && *e != '#') e++;
			if (e > p)
			{
				const char *eq = p; while (eq < e && *eq != '=') eq++;
				KV kv; kv.key.set (p, (int) (eq - p)); if (eq < e) kv.value.set (eq + 1, (int) (e - eq - 1));
				for (int i = 0; i < r.params.size (); i++) if (r.params[i].on && r.params[i].key.eq (kv.key.c ())) { kv.desc = r.params[i].desc; break; }
				all.push (kv);
			}
			if (*e != '&') break;
			p = e + 1;
		}
	for (int i = 0; i < off.size (); i++) all.push (off[i]);
	r.params = all;
}
static void url_from_params (Request &r)
{
	const char *u = r.url.c ();
	const char *qm = strchr (u, '?');
	const char *hash = strchr (u, '#');
	Str base; base.set (u, qm ? (int) (qm - u) : hash ? (int) (hash - u) : r.url.len ());
	Str q;
	for (int i = 0; i < r.params.size (); i++)
	{
		const KV &kv = r.params[i];
		if (!kv.on || kv.blank ()) continue;
		if (!q.empty ()) q.add ('&');
		q.add (kv.key.c ());
		if (!kv.value.empty () || strchr (u, '=')) { q.add ('='); q.add (kv.value.c ()); }
	}
	if (!q.empty ()) { base.add ('?'); base.add (q.c ()); }
	if (hash) base.add (hash);
	r.url = base;
}

// ---- the request's editor: filling it from a tab, and its edits going to the tab ----------------------------------------
static void req_counts ()
{
	Tab *t = cur_tab ();
	if (!t || t->kind != TAB_REQUEST) return;
	Request &r = t->req;
	ui.sections->setCount (SEC_PARAMS, ui.params->count () ? ui.params->count () : -1);
	ui.sections->setCount (SEC_HEADERS, ui.headers->count () ? ui.headers->count () : -1);
	ui.sections->setDot (SEC_AUTH, r.auth.type != AUTH_INHERIT && r.auth.type != AUTH_NONE);
	ui.sections->setDot (SEC_BODY, r.bodyMode != BODY_NONE);
	int nt = 0; for (int i = 0; i < r.tests.size (); i++) if (r.tests[i].on && !r.tests[i].key.empty ()) nt++;
	for (int i = 0; i < r.captures.size (); i++) if (r.captures[i].on && !r.captures[i].key.empty ()) nt++;
	ui.sections->setCount (SEC_TESTS, nt ? nt : -1);
	ui.sections->setDot (SEC_SETTINGS, !r.followRedirects || r.timeoutMs);
}
static void edited ()
{
	if (ui.binding) return;
	Tab *t = cur_tab ();
	if (!t) return;
	t->dirty = true;
	req_counts ();
	app_tab_changed ();
}
static void show_section (int s)
{
	Pane *secs[] = { ui.secParams, ui.secAuth, ui.secHeaders, ui.secBody, ui.secTests, ui.secSettings };
	for (int i = 0; i < 6; i++) show (secs[i], i == s);
	Tab *t = cur_tab (); if (t) t->reqSection = s;
}
static void body_mode_shown ()
{
	Tab *t = cur_tab (); if (!t) return;
	int m = t->req.bodyMode;
	show (ui.form, m == BODY_FORM); show (ui.urlenc, m == BODY_URLENC); show (ui.raw, m == BODY_RAW);
	show (ui.binaryRow, m == BODY_BINARY); show (ui.rawLang, m == BODY_RAW); show (ui.beautify, m == BODY_RAW && t->req.rawLang == RAW_JSON);
	show (ui.bodyBulk, m == BODY_FORM || m == BODY_URLENC);
	ui.raw->lang = t->req.rawLang == RAW_JSON ? LANG_JSON : t->req.rawLang == RAW_XML ? LANG_XML : t->req.rawLang == RAW_HTML ? LANG_HTML : t->req.rawLang == RAW_JS ? LANG_JS : LANG_TEXT;
	ui.raw->invalidate (true);
	ui.secBody->layout (); ui.secBody->invalidate (true);
}
static void inherit_text ()
{
	Tab *t = cur_tab (); if (!t) return;
	Collection *c = 0; Item *it = tab_item (t, &c);
	ui.auth->m_inherit.clear ();
	if (!c) ui.auth->m_inherit.add ("This request is not in a collection: it inherits nothing (sent without authorization). Save it in a collection, or choose a type.");
	else
	{
		const Auth &a = effective_auth (t->req, it, c);
		const char *from = c->name.c ();
		for (const Item *p = it ? it->parent : 0; p && p->parent; p = p->parent) if (p->auth.type != AUTH_INHERIT) { from = p->name.c (); break; }
		ui.auth->m_inherit.addf ("This request is using %s from \"%s\".", a.type == AUTH_NONE ? "No Auth" : AUTH_NAMES[a.type], from);
	}
	ui.auth->inheritText = ui.auth->m_inherit.c ();
	ui.auth->invalidate (true);
}

// the response part, for the tab
static void resp_body_fill ()
{
	Tab *t = cur_tab ();
	if (!t || !t->resp) return;
	Response &r = *t->resp;
	const char *ct = r.header ("Content-Type"); if (!ct) ct = "";
	int ctl = (int) strlen (ct);
	bool json = s_findi (ct, ctl, "json") >= 0, xml = s_findi (ct, ctl, "xml") >= 0, html = s_findi (ct, ctl, "html") >= 0;
	bool image = s_findi (ct, ctl, "image/") >= 0;
	const char *b = r.body ? r.body : ""; int n = r.bodyLen;
	if (!json && !xml && !html && n) { int i = 0; while (i < n && is_space (b[i])) i++; if (i < n && (b[i] == '{' || b[i] == '[')) json = true; else if (i < n && b[i] == '<') xml = true; }
	int view = t->bodyView;
	show (ui.bodyImage, view == BV_PREVIEW && image);
	show (ui.bodyText, !(view == BV_PREVIEW && image));
	ui.bodyText->lang = json ? LANG_JSON : html ? LANG_HTML : xml ? LANG_XML : LANG_TEXT;
	// a binary body: its bytes as hexadecimal, a line a 16
	bool binary = false;
	for (int i = 0; i < n && i < 2048; i++) { unsigned char c = (unsigned char) b[i]; if (c == 0 || (c < 8)) { binary = true; break; } }
	Str out;
	if (view == BV_PREVIEW && image)
	{
		ImgFrames im;
		if (img_load_mem (b, (unsigned) n, &im)) { ui.bodyImage->setPixels (im.px[0], im.w, im.h, true); img_free (&im); }
		else ui.bodyImage->clearImage ();
		return;
	}
	if (binary)
	{
		ui.bodyText->lang = LANG_TEXT;
		int m = imin (n, 64 * 1024);
		for (int i = 0; i < m; i += 16)
		{
			out.addf ("%08x  ", i);
			for (int k = 0; k < 16; k++) { if (i + k < m) out.addf ("%02x ", (unsigned char) b[i + k]); else out.add ("   "); if (k == 7) out.add (' '); }
			out.add (' ');
			for (int k = 0; k < 16 && i + k < m; k++) { unsigned char c = (unsigned char) b[i + k]; out.add (c >= 32 && c < 127 ? (char) c : '.'); }
			out.add ('\n');
		}
		if (n > m) out.addf ("... (%d bytes in all)\n", n);
	}
	else if (view == BV_PRETTY && json && pretty_json (out, b, n)) {}
	else if (view == BV_PRETTY && xml && !html && pretty_xml (out, b, n)) {}
	else if (view == BV_PREVIEW && html) { html_text (out, b, n); ui.bodyText->lang = LANG_TEXT; }
	else out.set (b, n);
	ui.bodyText->lineNumbers = view != BV_PREVIEW;
	ui.bodyText->adopt (out);
}
static void resp_shown ()
{
	Tab *t = cur_tab ();
	if (!t) return;
	bool busy = t->job != 0, has = t->resp != 0 && !busy;
	int s = t->respSection;
	show (ui.sending, busy);
	show (ui.respEmpty, !busy && (!has || t->resp->status < 0) && s != RS_CONSOLE);
	bool ok = has && t->resp->status >= 0;
	show (ui.respBody, ok && s == RS_BODY);
	show (ui.respCookies, ok && s == RS_COOKIES);
	show (ui.respHeaders, ok && s == RS_HEADERS);
	show (ui.results, ok && s == RS_TESTS);
	show (ui.console, !busy && s == RS_CONSOLE);
	ui.respTabs->select (s, false);
	ui.respStatus->invalidate (true);
	ui.respPane->invalidate (true);
}
static void resp_fill ()
{
	Tab *t = cur_tab ();
	ui.respHeadersRows.clear (); ui.respCookieRows.clear ();
	if (t && t->resp)
	{
		Response &r = *t->resp;
		for (int i = 0; i < r.headers.size (); i++) ui.respHeadersRows.push (r.headers[i]);
		for (int i = 0; i < r.setCookies.size (); i++)
		{
			const Cookie &c = r.setCookies[i];
			KV kv (c.name.c (), c.value.c ());
			kv.desc.addf ("%s%s  path %s%s%s", c.hostOnly ? "" : ".", c.domain.c (), c.path.c (), c.secure ? "  secure" : "", c.httpOnly ? "  HttpOnly" : "");
			if (c.expires) { Str e; iso_time (e, c.expires); kv.desc.add ("  expires "); kv.desc.add (e.c ()); }
			ui.respCookieRows.push (kv);
		}
		ui.console->setText (r.console.c ());
		ui.respTabs->setCount (RS_HEADERS, r.headers.size () ? r.headers.size () : -1);
		ui.respTabs->setCount (RS_COOKIES, r.setCookies.size () ? r.setCookies.size () : -1);
		int pass = 0, tot = 0;
		for (int i = 0; i < t->results.size (); i++) if (!t->results[i].capture) { tot++; if (t->results[i].pass) pass++; }
		ui.respTabs->badge[RS_TESTS] = pass == tot ? on_field (C_OK) : on_field (C_BAD);
		ui.respTabs->setCount (RS_TESTS, tot ? pass : -1);
		ui.bodyView->select (t->bodyView, false);
		resp_body_fill ();
	}
	else
	{
		ui.console->setText ("");
		ui.respTabs->setCount (RS_HEADERS, -1); ui.respTabs->setCount (RS_COOKIES, -1); ui.respTabs->setCount (RS_TESTS, -1);
	}
	ui.respHeaders->setRows (&ui.respHeadersRows);
	ui.respCookies->setRows (&ui.respCookieRows);
	ui.results->top = 0; ui.results->invalidate (true);
	ui.respPane->layout ();
	resp_shown ();
}

// a request tab into the editor
static void bind_request (Tab *t)
{
	ui.binding = true;
	Request &r = t->req;
	ui.name->setText (r.name.c ());
	ui.method->sel = method_index (r.method.c ()); ui.method->invalidate (true);
	ui.url->setText (r.url.c ());
	ui.params->setRows (&r.params);
	ui.headers->setRows (&r.headers);
	ui.form->setRows (&r.form);
	ui.urlenc->setRows (&r.urlenc);
	ui.tests->setRows (&r.tests);
	ui.captures->setRows (&r.captures);
	ui.auth->bind (&r.auth);
	inherit_text ();
	ui.bodyMode->sel = r.bodyMode; ui.bodyMode->invalidate (true);
	ui.rawLang->sel = r.rawLang; ui.rawLang->invalidate (true);
	ui.raw->setText (r.raw.c ());
	ui.follow->setOn (r.followRedirects);
	{ char b[16]; if (r.timeoutMs) snprintf (b, sizeof b, "%d", r.timeoutMs); else b[0] = 0; ui.timeout->setText (b); }
	body_mode_shown ();
	ui.sections->select (t->reqSection, false);
	show_section (t->reqSection);
	if (t->split > 0) ui.vsplit->setSplit (t->split);
	req_counts ();
	resp_fill ();
	ui.binding = false;
	ui.binaryRow->invalidate (true);
	ui.reqPane->invalidate (true);
}

// ---- the request's layouts ---------------------------------------------------------------------------------------------------
static void lay_req_pane (Pane &p)
{
	int W = p.width, H = p.height;
	int x = 16;
	int sw = 96, cw = 40;
	place (ui.code, W - 16 - cw, 8, cw, 30);
	place (ui.save, W - 16 - cw - 8 - 86, 8, 86, 30);
	place (ui.name, x - 6, 8, imax (60, W - 16 - cw - 8 - 86 - 16 - x + 6), 30);
	int y = 46;
	place (ui.method, x, y, 116, 38);
	place (ui.send, W - 16 - sw, y, sw, 38);
	place (ui.url, x + 116 + 6, y, W - 16 - sw - 8 - (x + 116 + 6), 38);
	y += 38 + 8;
	place (ui.sections, 0, y, W, 36);
	y += 36;
	Pane *secs[] = { ui.secParams, ui.secAuth, ui.secHeaders, ui.secBody, ui.secTests, ui.secSettings };
	for (int i = 0; i < 6; i++) place (secs[i], 0, y, W, H - y);
}
static void paint_req_pane (Pane &p) { (void) p; }
// a section with a title line (and a Bulk Edit button) above its table
static void lay_table_sec (Pane &p)
{
	KVTable *t = 0; Btn *b = 0;
	for (Widget *c = p.firstChild; c; c = c->nextSib) { if (c == ui.params || c == ui.headers) t = (KVTable *) c; if (c == ui.paramsBulk || c == ui.headersBulk) b = (Btn *) c; }
	place (b, p.width - 16 - 96, 8, 96, 26);
	place (t, 16, 40, p.width - 32, p.height - 40 - 12);
}
static void paint_params (Pane &p) { text_v (p.canvas, 18, 8, 26, "Query Params", c_dim (), 2); }
static void paint_headers (Pane &p)
{
	text_v (p.canvas, 18, 8, 26, "Headers", c_dim (), 2);
	const char *h = "Sent too: User-Agent, Accept, Accept-Encoding, Host, Connection, the cookies of the jar";
	text_fit (p.canvas, 30 + wk_tw ("Headers", 2), 8, 26, p.width - 170 - wk_tw ("Headers", 2), h, c_faint ());
}
static void lay_body (Pane &p)
{
	int W = p.width, H = p.height;
	int rw = ui.bodyMode ? ui.bodyMode->itemX (5) : 520;
	place (ui.bodyMode, 16, 8, imin (W - 32, rw), 28);
	// the raw's language and Beautify: on the radios' line when it has room, else below them
	bool two = 16 + rw + 16 + 130 + 8 + 96 + 16 > W;
	int oy = two ? 42 : 8;
	place (ui.bodyBulk, W - 16 - 96, 8, 96, 26);
	place (ui.beautify, W - 16 - 96, oy, 96, 26);
	place (ui.rawLang, two ? 12 : 16 + rw + 16, oy - 1, 130, 28);
	Tab *t = cur_tab ();
	int y = two && t && t->req.bodyMode == BODY_RAW ? 76 : 44;
	place (ui.form, 16, y, W - 32, H - y - 12);
	place (ui.urlenc, 16, y, W - 32, H - y - 12);
	place (ui.raw, 16, y, W - 32, H - y - 12);
	place (ui.binaryRow, 16, y, W - 32, 60);
}
static void paint_body (Pane &p)
{
	Tab *t = cur_tab ();
	if (t && t->req.bodyMode == BODY_NONE)
	{
		const char *s = "This request does not have a body";
		text_at (p.canvas, (p.width - wk_tw (s)) / 2, 44 + (p.height - 44) / 2 - 10, s, c_dim ());
	}
}
static void lay_binary (Pane &p) { place (ui.binaryPick, 0, 12, 130, 32); }
static void paint_binary (Pane &p)
{
	Tab *t = cur_tab (); if (!t) return;
	const char *s = t->req.binary.empty () ? "No file chosen: its bytes are sent as the body, its type from its name." : t->req.binary.c ();
	text_fit (p.canvas, 146, 12, 32, p.width - 150, s, t->req.binary.empty () ? c_dim () : C_FIELD_TEXT);
}
static void lay_tests (Pane &p)
{
	int W = p.width, H = p.height;
	int half = (H - 60) / 2;
	place (ui.tests, 16, 58, W - 32, half - 20);
	place (ui.captures, 16, 58 + half + 26, W - 32, H - (58 + half + 26) - 12);
}
static void paint_tests (Pane &p)
{
	int H = p.height, half = (H - 60) / 2;
	text_v (p.canvas, 18, 6, 22, "Tests", c_dim (), 2);
	text_fit (p.canvas, 18, 26, 26, p.width - 36, "Checked on each response:  status = 200,  json.data.id exists,  header.Content-Type contains json,  time < 800,  body contains ok", c_faint ());
	text_v (p.canvas, 18, 58 + half - 16, 22, "Captures", c_dim (), 2);
	text_fit (p.canvas, 30 + wk_tw ("Captures", 2), 58 + half - 16, 22, p.width - 60,
		  "variables set from the response, in the environment chosen (else the globals):  token  <-  json.access_token", c_faint ());
}
static void lay_settings (Pane &p)
{
	place (ui.follow, 20, 20, imin (p.width - 40, 420), 30);
	place (ui.timeout, 20 + 250, 66, 120, 30);
}
static void paint_settings (Pane &p)
{
	text_v (p.canvas, 20, 66, 30, "Request timeout (ms)", C_FIELD_TEXT);
	text_v (p.canvas, 20 + 250 + 132, 66, 30, "empty: 30000", c_faint ());
	int y = 116;
	y = text_wrap (p.canvas, 20, y, p.width - 40, "HTTPS: the server's certificate is not verified yet (Onyx has no certificate authority bundle): "
		       "the connection is encrypted, not authenticated.", c_faint ());
	text_wrap (p.canvas, 20, y + 6, p.width - 40, "The redirects followed keep the cookies they set; 301 / 302 / 303 go on as GET, 307 / 308 keep the method and the body.", c_faint ());
}
static void lay_resp_pane (Pane &p)
{
	int W = p.width, H = p.height;
	place (ui.respTabs, 0, 0, W, 38);
	int tabsEnd = ui.respTabs->tabX (ui.respTabs->n) + 8;
	int sx = imax (tabsEnd, W - 330);
	place (ui.respStatus, sx, 1, imax (1, W - 8 - sx), 34);
	int y = 38;
	Widget *all[] = { ui.respBody, ui.respCookies, ui.respHeaders, ui.results, ui.console, ui.respEmpty, ui.sending };
	for (int i = 0; i < 7; i++)
	{
		if (all[i] == ui.respCookies || all[i] == ui.respHeaders) place (all[i], 16, y + 10, W - 32, H - y - 20);
		else if (all[i] == ui.console) place (all[i], 12, y + 8, W - 24, H - y - 16);
		else place (all[i], 0, y, W, H - y);
	}
}
static void paint_resp_pane (Pane &p) { p.canvas.fillRect (0, 0, p.width, 1, c_line ()); }
static void lay_resp_body (Pane &p)
{
	int W = p.width, H = p.height;
	place (ui.bodyView, 16, 8, 230, 28);
	place (ui.saveBody, W - 16 - 34, 8, 34, 28);
	place (ui.copyBody, W - 16 - 34 - 4 - 34, 8, 34, 28);
	place (ui.find, W - 16 - 34 - 4 - 34 - 8 - 190, 8, 190, 28);
	place (ui.bodyText, 12, 44, W - 24, H - 44 - 8);
	place (ui.bodyImage, 12, 44, W - 24, H - 44 - 8);
}
static void paint_resp_body (Pane &p)
{
	Tab *t = cur_tab ();
	if (!t || !t->resp) return;
	const char *ct = t->resp->header ("Content-Type");
	if (ct) { char b[80]; s_copy (b, ct, sizeof b); char *semi = strchr (b, ';'); if (semi) *semi = 0; text_fit (p.canvas, 262, 8, 28, p.width - 262 - 300, b, c_dim ()); }
}
static void paint_resp_empty (Pane &p)
{
	Tab *t = cur_tab ();
	if (t && t->resp && t->resp->status < 0)
	{
		// the error, large
		int cy = p.height / 2 - 40;
		const char *h = t->resp->cancelled ? "The request was cancelled" : "Could not send the request";
		unsigned red = t->resp->cancelled ? c_dim () : on_field (C_BAD);
		wk_glyph (p.canvas, t->resp->cancelled ? WKG_CLOSE : WKG_RING, p.width / 2, cy - 18, 22, red);
		text_at (p.canvas, (p.width - wk_tw (h, 2)) / 2, cy + 6, h, red, 2);
		int w = imin (p.width - 60, 560);
		text_wrap (p.canvas, (p.width - w) / 2, cy + 34, w, t->resp->error.c (), c_dim ());
		return;
	}
	empty_state (p.canvas, 0, 0, p.width, p.height, "Enter the URL and click Send to get a response", "Ctrl+Enter sends too; {{variables}} come from the environment chosen, top right.", C_FIELD);
}

} // namespace cr

#endif
