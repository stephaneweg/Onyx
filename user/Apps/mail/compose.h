//
// Apps/mail/compose.h -- writing a message, in place of the list and the reading pane: From (the account), To / Cc
// / Bcc completed from the contacts and the addresses already written to as one types (Up / Down, Enter or Tab
// takes one), the subject, the text, the attachments (added from the card, or kept from a forwarded message);
// Send (Ctrl+Enter), Save as draft, Discard. A reply quotes the message ("On ..., X wrote:" and its lines with
// "> "), keeps its conversation (In-Reply-To, References); a forward carries its attachments. The message goes as
// plain text and as simple HTML (its paragraphs, its links, the quote with a bar), in UTF-8 (mail/mime.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _mail_compose_h
#define _mail_compose_h

#include "Apps/mail/app.h"

namespace mailapp {

class ComposePane;
// the suggestions under an address field
class Suggest : public Widget
{
public:
	Contacts::Match m[8]; int n, sel;
	class AddrBox *owner;			// the field it completes
	Suggest () : Widget (0, 0, 300, 10), n (0), sel (0), owner (0) { hidden = true; }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_popup (canvas, 0, 0, width, height, 6, C_FIELD);
		for (int i = 0; i < n; i++)
		{
			int y = 4 + i * 40;
			if (i == sel) uk_fill_round (canvas, 4, y, width - 8, 38, 5, col_sel ());
			avatar (canvas, 24, y + 19, 14, m[i].name[0] ? m[i].name : m[i].email, m[i].email);
			text (canvas, 46, y + 3, m[i].name[0] ? m[i].name : m[i].email, C_FIELD_TEXT, F_UI, 1, width - 56);
			text (canvas, 46, y + 21, m[i].email, col_dim (), F_SMALL, 0, width - 56);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override;
};

// an address field: its last address completed
class AddrBox : public Textbox
{
public:
	Suggest *sug; char last[512];
	AddrBox (int l, int t, int w, int h, Suggest *s) : Textbox (l, t, w, h, ""), sug (s) { last[0] = 0; maxLen = TEXT_CAP - 1; }
	// the address being typed: after the last comma
	const char *current () const { const char *c = strrchr (text, ','); c = c ? c + 1 : text; while (*c == ' ') c++; return c; }
	void take (int i)
	{
		if (i < 0 || i >= sug->n) return;
		char t[TEXT_CAP]; int keep = (int) (current () - text);
		memcpy (t, text, keep); t[keep] = 0;
		const Contacts::Match &m = sug->m[i];
		char one[300];
		if (m.name[0]) { bool q = strpbrk (m.name, ",;<>@\"") != 0; snprintf (one, sizeof one, q ? "\"%s\" <%s>, " : "%s <%s>, ", m.name, m.email); }
		else snprintf (one, sizeof one, "%s, ", m.email);
		if (keep && t[keep - 1] != ' ') scpy (t + keep, " ", sizeof t - keep);
		int k = (int) strlen (t); scpy (t + k, one, sizeof t - k);
		setText (t); scpy (last, text, sizeof last);
		sug->hidden = true; sug->invalidate (true);
		if (parent) parent->invalidate (false);
	}
	void tick ()
	{
		if (!hasFocus) { if (!sug->hidden && sug->owner == this) { sug->hidden = true; if (parent) parent->invalidate (true); } return; }
		if (!strcmp (last, text)) return;
		scpy (last, text, sizeof last);
		const char *c = current ();
		sug->n = strlen (c) >= 1 ? g_m.contacts.complete (c, sug->m, 6) : 0;
		sug->sel = 0;
		bool show = sug->n > 0;
		sug->owner = this;
		if (show) { sug->left = left; sug->top = top + height + 2; sug->resizeTo (width < 360 ? width : 360, 8 + sug->n * 40); sug->bringToFront (); }
		sug->hidden = !show;
		sug->invalidate (true);
		if (parent) parent->invalidate (true);
	}
	bool onKey (long k) override
	{
		if (!sug->hidden && sug->owner == this)
		{
			if (k == KEY_DOWN) { sug->sel = (sug->sel + 1) % sug->n; sug->invalidate (true); return true; }
			if (k == KEY_UP) { sug->sel = (sug->sel + sug->n - 1) % sug->n; sug->invalidate (true); return true; }
			if (k == KEY_ENTER || k == KEY_TAB) { take (sug->sel); return true; }
			if (k == 27) { sug->hidden = true; if (parent) parent->invalidate (true); return true; }
		}
		return Textbox::onKey (k);
	}
};

// the text: Ctrl+Enter sends
class BodyArea : public Textarea
{
public:
	BodyArea (int l, int t, int w, int h, int cap) : Textarea (l, t, w, h, cap) {}
	bool onKey (long k) override;
};

struct Attach { char name[200]; char type[80]; char *data; int n; };

static void compose_send ();
static void compose_draft ();
static void compose_discard ();
static void compose_attach ();
static void compose_ccbcc ();
class ComposePane : public Widget
{
public:
	Dropdown *from; AddrBox *to, *cc, *bcc; Textbox *subject; BodyArea *body; Button *send, *attach, *draft, *discard;
	Suggest *sug;
	const char *fromOpts[12]; char fromText[12][220];
	bool ccOn;
	int mode; Ref about; bool haveAbout;
	char inReplyTo[220], references[1100];
	Attach att[16]; int natt;
	HitList hits;
	char title[60];
	enum { H_CCBCC = 1, H_ATT_DEL };
	static const int PAD = 20, ROW = 38, LBL = 70;

	ComposePane (int l, int t, int w, int h) : Widget (l, t, w, h), ccOn (false), mode (0), haveAbout (false), natt (0)
	{
		hidden = true; inReplyTo[0] = references[0] = 0; title[0] = 0;
		sug = new Suggest;
		for (int i = 0; i < 12; i++) fromOpts[i] = "";
		from = new Dropdown (PAD + LBL, 0, 300, 30, fromOpts, 1, 0, 0); addChild (from);
		to = new AddrBox (PAD + LBL, 0, 300, 30, sug); addChild (to);
		cc = new AddrBox (PAD + LBL, 0, 300, 30, sug); addChild (cc);
		bcc = new AddrBox (PAD + LBL, 0, 300, 30, sug); addChild (bcc);
		subject = new Textbox (PAD + LBL, 0, 300, 30, ""); subject->maxLen = 300; addChild (subject);
		body = new BodyArea (PAD, 0, 300, 200, 256 * 1024); addChild (body);
		send = new Button (0, 0, 100, 34, TR ("Send"), [] (Widget &) { compose_send (); }); addChild (send);
		attach = new Button (0, 0, 110, 34, TR ("Attach..."), [] (Widget &) { compose_attach (); }); addChild (attach);
		draft = new Button (0, 0, 120, 34, TR ("Save draft"), [] (Widget &) { compose_draft (); }); addChild (draft);
		discard = new Button (0, 0, 100, 34, TR ("Discard"), [] (Widget &) { compose_discard (); }); addChild (discard);
		addChild (sug);
	}
	unsigned bgColor () override { return C_FIELD; }
	void accounts_changed ()
	{
		int n = g_m.accts.n < 12 ? g_m.accts.n : 12;
		for (int i = 0; i < n; i++) { snprintf (fromText[i], sizeof fromText[i], "%s <%s>", g_m.accts.a[i].name[0] ? g_m.accts.a[i].name : g_m.accts.a[i].label, g_m.accts.a[i].email); fromOpts[i] = fromText[i]; }
		from->setOptions (fromOpts, n ? n : 1, from->sel < n ? from->sel : 0);
	}
	int attY () const { return height - 54 - (natt ? 34 : 0); }
	void place ()
	{
		int w = width - 2 * PAD - LBL, y = 56;
		from->left = PAD + LBL; from->top = y; from->resizeTo (w, 30); y += ROW;
		to->left = PAD + LBL; to->top = y; to->resizeTo (w - (ccOn ? 0 : 70), 30); y += ROW;
		cc->hidden = bcc->hidden = !ccOn;
		if (ccOn) { cc->left = PAD + LBL; cc->top = y; cc->resizeTo (w, 30); y += ROW; bcc->left = PAD + LBL; bcc->top = y; bcc->resizeTo (w, 30); y += ROW; }
		subject->left = PAD + LBL; subject->top = y; subject->resizeTo (w, 30); y += ROW + 6;
		body->Widget::left = PAD; body->Widget::top = y; body->resizeTo (width - 2 * PAD, attY () - y - 8);
		int by = height - 46;
		send->left = PAD; send->top = by;
		attach->left = PAD + 110; attach->top = by;
		draft->left = PAD + 230; draft->top = by;
		discard->left = width - PAD - 100; discard->top = by;
	}
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		hits.clear ();
		text (canvas, PAD, 16, title, C_FIELD_TEXT, F_H2, 1);
		int y = 56;
		const char *lab[5] = { TR ("From"), TR ("To"), TR ("Cc"), TR ("Bcc"), TR ("Subject") };
		int rows[5] = { y, y + ROW, ccOn ? y + 2 * ROW : -1, ccOn ? y + 3 * ROW : -1, y + (ccOn ? 4 : 2) * ROW };
		for (int i = 0; i < 5; i++) if (rows[i] >= 0) { text_v (canvas, PAD, rows[i], 30, lab[i], col_dim ()); canvas.fillRect (PAD, rows[i] + 34, width - 2 * PAD, 1, col_line ()); }
		if (!ccOn)
		{
			int x = width - PAD - 60;
			text_v (canvas, x, y + ROW, 30, TR ("Cc Bcc"), C_ACCENT, F_SMALL, 1);
			hits.add (x - 4, y + ROW, 68, 30, H_CCBCC);
		}
		// the attachments
		if (natt)
		{
			int x = PAD, ay = attY ();
			for (int i = 0; i < natt; i++)
			{
				char sz[30]; fmt_size (att[i].n, sz, sizeof sz);
				char l[260]; snprintf (l, sizeof l, "%s (%s)", att[i].name, sz);
				int w = tw (l, F_SMALL) + 54; if (w > 260) w = 260;
				if (x + w > width - PAD) break;
				uk_fill_round (canvas, x, ay, w, 28, 14, col_line ());
				icon (canvas, I_CLIP, x + 8, ay + 6, 16, col_dim ());
				text_v (canvas, x + 28, ay, 28, l, C_FIELD_TEXT, F_SMALL, 0, w - 54);
				icon (canvas, I_CLOSE, x + w - 22, ay + 7, 14, col_dim ());
				hits.add (x + w - 26, ay, 26, 28, H_ATT_DEL, i);
				x += w + 8;
			}
		}
		canvas.fillRect (0, height - 56, width, 1, col_line ());
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		static bool was; bool down = bl && !was; was = bl;
		if (!down) return false;
		const Hit *h = hits.at (mx, my);
		if (!h) return false;
		if (h->kind == H_CCBCC) compose_ccbcc ();
		else if (h->kind == H_ATT_DEL) { free (att[h->a].data); for (int i = h->a; i < natt - 1; i++) att[i] = att[i + 1]; natt--; place (); invalidate (true); }
		return true;
	}
	void tick () { if (hidden) return; to->tick (); if (ccOn) { cc->tick (); bcc->tick (); } }
	void reset ()
	{
		for (int i = 0; i < natt; i++) free (att[i].data);
		natt = 0; ccOn = false; haveAbout = false; inReplyTo[0] = references[0] = 0;
		to->setText (""); cc->setText (""); bcc->setText (""); subject->setText (""); body->setContent ("");
		sug->hidden = true;
	}
	bool empty () const { return !to->text[0] && !subject->text[0] && !body->content ()[0] && !natt; }
};
bool BodyArea::onKey (long k)
{
	if (k == KEY_ENTER && (kapi_get_modifiers () & MOD_CTRL)) { compose_send (); return true; }
	return Textarea::onKey (k);
}

bool Suggest::onMouse (int mx, int my, int bl, int, int, int)
{
	(void) mx;
	static bool was; bool down = bl && !was; was = bl;
	int i = (my - 4) / 40; if (i < 0 || i >= n) return true;
	if (sel != i) { sel = i; invalidate (true); }
	if (down && owner) { owner->take (i); owner->setFocus (); }
	return true;
}

// ---- the message made from the pane -------------------------------------------------------------------------------------------
static const char *mime_type_of (const char *name)
{
	static const struct { const char *e, *t; } T[] = { { "pdf", "application/pdf" }, { "png", "image/png" }, { "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" },
		{ "gif", "image/gif" }, { "webp", "image/webp" }, { "txt", "text/plain" }, { "html", "text/html" }, { "htm", "text/html" }, { "zip", "application/zip" },
		{ "docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document" }, { "odt", "application/vnd.oasis.opendocument.text" },
		{ "xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" }, { "ods", "application/vnd.oasis.opendocument.spreadsheet" },
		{ "csv", "text/csv" }, { "mp3", "audio/mpeg" }, { "ogg", "audio/ogg" }, { "wav", "audio/wav" }, { "mp4", "video/mp4" }, { "eml", "message/rfc822" },
		{ "rtf", "application/rtf" }, { "card", "text/plain" }, { "bas", "text/plain" } };
	const char *dot = strrchr (name, '.');
	if (dot) for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) if (ieq (dot + 1, T[i].e)) return T[i].t;
	return "application/octet-stream";
}
// the text as simple HTML: its paragraphs and line breaks, its links, the quoted lines with a bar
static void text_to_html (const char *t, Buf &o)
{
	o.add ("<!DOCTYPE html><html><head><meta charset=\"utf-8\"></head><body><div style=\"font-family:Arial,Helvetica,sans-serif;font-size:14px;line-height:1.45\">");
	bool inQuote = false;
	const char *p = t;
	while (*p)
	{
		const char *e = strchr (p, '\n'); int n = e ? (int) (e - p) : (int) strlen (p);
		bool q = n && *p == '>';
		if (q && !inQuote) { o.add ("<blockquote style=\"margin:0 0 0 4px;border-left:3px solid #c8ccd0;padding-left:10px;color:#5f6368\">"); inQuote = true; }
		if (!q && inQuote) { o.add ("</blockquote>"); inQuote = false; }
		const char *l = p; int ln = n;
		if (q) { l++; ln--; if (ln && *l == ' ') { l++; ln--; } }
		// the line, its links made links
		for (int i = 0; i < ln; )
		{
			if ((ln - i > 8 && !memcmp (l + i, "https://", 8)) || (ln - i > 7 && !memcmp (l + i, "http://", 7)))
			{
				int j = i; while (j < ln && !strchr (" <>\"", l[j])) j++;
				while (j > i && strchr (".,;:!?)", l[j - 1])) j--;
				o.add ("<a href=\""); html::Html::escape (o, l + i, j - i); o.add ("\">"); html::Html::escape (o, l + i, j - i); o.add ("</a>");
				i = j; continue;
			}
			int j = i; while (j < ln && !(l[j] == 'h' && (j + 7 < ln) && (!memcmp (l + j, "http://", 7) || !memcmp (l + j, "https://", 8)))) j++;
			if (j == i) j = i + 1;
			html::Html::escape (o, l + i, j - i);
			i = j;
		}
		o.add ("<br>\n");
		if (!e) break;
		p = e + 1;
	}
	if (inQuote) o.add ("</blockquote>");
	o.add ("</div></body></html>\n");
}

} // namespace mailapp

#endif
