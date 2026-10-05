//
// Apps/mail/read.h -- Mail's right column: a conversation. Its subject, its account and folder; its messages oldest
// first, the earlier ones folded (who, the first words, when; a click opens one), the last one open: who, to whom,
// when, Reply / Reply all / Forward, the text (an HTML message drawn by WebKit -- Web's web view: webview.h -- when
// Web is on the card, else by Mail's own renderer -- mail/html.h --; a text message: the plain text with its links
// found and its quoted lines greyed), the remote pictures held back ("Show the pictures"),
// the attachments (opened in their app, or saved); at the bottom a quick reply. A message not on the card yet is
// fetched (the worker) and shown when it comes.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _mail_read_h
#define _mail_read_h

#include "Apps/mail/app.h"
#include "Apps/mail/webview.h"

namespace mailapp {

// ---- the remote pictures fetched (shared by the messages; kept while Mail runs) ----------------------------------------------
struct Remote { char *url; unsigned *px; int w, h; bool failed, asked; };
static Remote g_remote[64]; static int g_nremote;
static Remote *remote_find (const char *url) { for (int i = 0; i < g_nremote; i++) if (!strcmp (g_remote[i].url, url)) return &g_remote[i]; return 0; }
static Remote *remote_add (const char *url)
{
	if (g_nremote == 64) { free (g_remote[0].url); delete [] g_remote[0].px; for (int i = 1; i < 64; i++) g_remote[i - 1] = g_remote[i]; g_nremote--; }
	Remote &r = g_remote[g_nremote++]; memset (&r, 0, sizeof r); r.url = sdup (url);
	return &r;
}

// ---- a message shown ------------------------------------------------------------------------------------------------------------
struct Shown;
struct MsgPictures : html::Pictures
{
	Shown *s;
	const unsigned *get (const char *src, int *w, int *h) override;
};
struct Inline { char cid[160]; unsigned *px; int w, h; bool tried; };
struct Shown
{
	Ref ref; bool open, loading, missing;
	char *raw; int rawLen;			// the message's bytes (its .eml)
	Mime *mime;
	html::Html *html; int layW;		// its text laid out at layW
	bool isHtml, remoteOk;
	int att[24]; int natt;			// its attachments' parts
	Inline inl[16]; int ninl;
	int y, h;				// its place in the pane (content coordinates)
	int bodyY;				// where its text starts
	unsigned *zpx; int zw, zh;		// a text wider than the pane: drawn once, scaled down to fit
	int webRemote;				// (the web view) content from the internet in its HTML: 0 not looked, 1 no, 2 yes
};
static void shown_free (Shown &s)
{
	free (s.raw); delete s.mime; delete s.html; free (s.zpx);
	for (int i = 0; i < s.ninl; i++) delete [] s.inl[i].px;
	memset (&s, 0, sizeof s);
}
static html::FtHost *g_host;
static MsgPictures g_pics;			// (one provider, pointed at the message laid out or drawn)
const unsigned *MsgPictures::get (const char *src, int *w, int *h)
{
	if (!s || !s->mime) return 0;
	if (istarts (src, "cid:"))
	{
		const char *cid = src + 4;
		for (int i = 0; i < s->ninl; i++) if (!strcmp (s->inl[i].cid, cid)) { if (!s->inl[i].px) return 0; *w = s->inl[i].w; *h = s->inl[i].h; return s->inl[i].px; }
		if (s->ninl == 16) return 0;
		Inline &in = s->inl[s->ninl++]; memset (&in, 0, sizeof in); scpy (in.cid, cid, sizeof in.cid); in.tried = true;
		int k = s->mime->by_cid (cid); if (k < 0) return 0;
		Buf b; s->mime->decoded (s->mime->parts[k], b);
		ImgFrames im; memset (&im, 0, sizeof im);
		if (!b.n || !img_load_mem (b.p, (unsigned) b.n, &im) || im.n < 1) return 0;
		in.px = im.px[0]; in.w = im.w; in.h = im.h; im.px[0] = 0; img_free (&im);
		*w = in.w; *h = in.h; return in.px;
	}
	if (!s->remoteOk) return 0;
	const char *url = src; char tmp[1024];
	if (istarts (src, "//")) { snprintf (tmp, sizeof tmp, "https:%s", src); url = tmp; }
	if (!istarts (url, "http://") && !istarts (url, "https://")) return 0;
	Remote *r = remote_find (url);
	if (!r)
	{	// asked of the worker; the pane laid out again when it comes
		r = remote_add (url); r->asked = true;
		Ref &f = s->ref; Job *j = g_m.job (J_PICTURE, f.acct); scpy (j->uids, url, sizeof j->uids); g_m.worker.push (j);
		return 0;
	}
	if (!r->px) return 0;
	*w = r->w; *h = r->h; return r->px;
}

// a message's parts (once): its attachments, HTML or not
static void shown_parse (Shown &s)
{
	if (s.mime || !s.raw) return;
	s.mime = new Mime; s.mime->parse (s.raw, s.rawLen);
	s.natt = s.mime->attachments (s.att, 24);
	int p = s.mime->body_part (true);
	s.isHtml = p >= 0 && ieq (s.mime->parts[p].sub, "html");
}

// a message's text: its HTML (or its plain text as HTML), laid out at w
static void shown_layout (Shown &s, int w)
{
	if (!s.raw || !s.open) return;
	shown_parse (s);
	if (!s.html)
	{
		s.html = new html::Html;
		s.html->textColour = 0xFF000000u | (C_FIELD_TEXT & 0xFFFFFF);
		int p = s.mime->body_part (true);
		Buf t; if (p >= 0) s.mime->text (s.mime->parts[p], t);
		s.isHtml = p >= 0 && ieq (s.mime->parts[p].sub, "html");
		if (s.isHtml) s.html->parse (t.c (), t.n); else s.html->parse_text (t.c (), t.n);
		s.layW = -1;
	}
	if (s.layW != w)
	{
		free (s.zpx); s.zpx = 0;
		g_pics.s = &s; g_host->pics = &g_pics;
		s.html->layout (*g_host, w);
		s.layW = w;
	}
}

// ---- the pane ---------------------------------------------------------------------------------------------------------------------
class QuickReply;
static void quick_send ();
class ReadPane : public Widget
{
public:
	Shown sh[60]; int nsh;
	int sy, contentH;			// the scroll, the content's height
	HitList hits;
	enum { H_EXPAND = 1, H_REPLY, H_REPLYALL, H_FORWARD, H_PICS, H_ATT_OPEN, H_ATT_SAVE, H_SAVEALL, H_LINK, H_STAR, H_SENDER, H_ACCOUNT, H_MORE };
	HintBox *quick; Button *qsend;
	bool barDrag; int pressY, pressSy;
	int hint; char hintText[220];
	static const int PAD = 24, QR_H = 58;

	ReadPane (int l, int t, int w, int h) : Widget (l, t, w, h), nsh (0), sy (0), contentH (0), barDrag (false), hint (0)
	{
		hintText[0] = 0;
		quick = new HintBox (PAD, h - QR_H + 12, w - 2 * PAD - 78, 34, "Reply...");
		addChild (quick);
		qsend = new Button (w - PAD - 70, h - QR_H + 12, 70, 34, "Send", [] (Widget &) { quick_send (); });
		addChild (qsend);
	}
	unsigned bgColor () override { return C_FIELD; }
	void clear () { for (int i = 0; i < nsh; i++) shown_free (sh[i]); nsh = 0; sy = 0; }
	void place ()
	{
		quick->left = PAD; quick->top = height - QR_H + 12; quick->resizeTo (width - 2 * PAD - 78, 34);
		qsend->left = width - PAD - 70; qsend->top = height - QR_H + 12;
		quick->hidden = qsend->hidden = !nsh;
	}
	// a conversation's messages: the unread ones and the last open
	void load (const Ref *refs, int n)
	{
		clear ();
		bool anyUnread = false;
		for (int i = 0; i < n && i < 60; i++)
		{
			Shown &s = sh[nsh++]; memset (&s, 0, sizeof s); s.ref = refs[i];
			const Msg &m = g_m.msg (refs[i]);
			s.open = !(m.flags & F_SEEN) || i == n - 1;
			if (!(m.flags & F_SEEN)) anyUnread = true;
		}
		(void) anyUnread;
		for (int i = 0; i < nsh; i++) if (sh[i].open) fetch (sh[i]);
		quick->setText ("");
		place ();
		invalidate (true);
	}
	void fetch (Shown &s)
	{
		if (s.raw) return;
		Store &st = *g_m.stores[s.ref.acct]; Folder &f = st.folders[s.ref.folder]; const Msg &m = f.msgs[s.ref.msg];
		s.raw = st.body (f, m.uid, &s.rawLen);
		if (!s.raw && !s.loading)
		{
			if (g_m.accts.a[s.ref.acct].kind == K_POP3) { s.missing = true; return; }
			s.loading = true; g_m.fetch_body (s.ref.acct, s.ref.folder, m.uid);
		}
	}
	// a body came (the worker): shown if it is one of these
	void body_came (const char *acctId, const char *folder, long uid)
	{
		for (int i = 0; i < nsh; i++)
		{
			Shown &s = sh[i];
			Store &st = *g_m.stores[s.ref.acct];
			if (strcmp (st.acct->id, acctId) || strcmp (st.folders[s.ref.folder].name, folder) || st.folders[s.ref.folder].msgs[s.ref.msg].uid != uid) continue;
			s.loading = false; fetch (s);
			invalidate (true);
		}
	}
	void relayout () { for (int i = 0; i < nsh; i++) if (sh[i].html) sh[i].layW = -1; invalidate (true); }

	int textW () const { return width - 2 * PAD - 10; }
	int viewH () const { return nsh ? height - QR_H : height; }

	void onDraw () override
	{
		canvas.clear (C_FIELD);
		hits.clear ();
		g_wv.shown = false;
		if (!nsh)
		{
			int cy = height / 2 - 40;
			icon (canvas, I_INBOX, width / 2 - 24, cy - 30, 48, col_faint ());
			text_c (canvas, 0, cy + 30, width, 24, g_m.view.convs.n ? "Choose a conversation to read it." : "Nothing here.", col_dim (), F_MID);
			return;
		}
		int W = width - 10, y = PAD - sy;
		const Msg &last = g_m.msg (sh[nsh - 1].ref);
		// the subject, the chips
		const char *subj = last.subject && last.subject[0] ? last.subject : "(no subject)";
		{
			UkFaceScope sc (g_face[F_H1]);
			// (a long subject over two lines)
			char a[400], b[400]; scpy (a, subj, sizeof a); b[0] = 0;
			if (uk_tw (a, 2) > W - 2 * PAD)
			{
				int cut = (int) strlen (a);
				while (cut > 0) { while (cut > 0 && a[cut] != ' ') cut--; char t = a[cut]; a[cut] = 0; int ww = uk_tw (a, 2); a[cut] = t; if (ww <= W - 2 * PAD || cut == 0) break; cut--; }
				if (cut > 0) { scpy (b, a + cut + 1, sizeof b); a[cut] = 0; }
			}
			text (canvas, PAD, y, a, C_FIELD_TEXT, F_H1, 1, W - 2 * PAD); y += fh (F_H1) + 2;
			if (b[0]) { text (canvas, PAD, y, b, C_FIELD_TEXT, F_H1, 1, W - 2 * PAD); y += fh (F_H1) + 2; }
		}
		y += 4;
		{
			Account &a = g_m.accts.a[sh[nsh - 1].ref.acct];
			const Folder &f = g_m.stores[sh[nsh - 1].ref.acct]->folders[sh[nsh - 1].ref.folder];
			int x = PAD;
			const char *chips[2] = { a.label, folder_label (f) };
			for (int k = 0; k < 2; k++)
			{
				int w = tw (chips[k], F_SMALL, 1) + 16;
				uk_fill_round (canvas, x, y, w, 20, 10, k ? col_line () : uk_mix (C_FIELD, a.colour, 200));
				text_c (canvas, x, y, w, 20, chips[k], k ? col_dim () : 0xFFFFFF, F_SMALL, 1);
				x += w + 6;
			}
			y += 32;
		}
		// the HTML message the web view shows (one view: the last open HTML message; the others by Mail's renderer)
		int webIdx = -1;
		if (wv_usable ())
			for (int i = nsh - 1; i >= 0 && webIdx < 0; i--)
			{
				Shown &s = sh[i];
				if (!s.open || !s.raw || s.missing) continue;
				shown_parse (s);
				if (s.isHtml) webIdx = i;
			}
		// the messages
		for (int i = 0; i < nsh; i++)
		{
			Shown &s = sh[i];
			const Msg &m = g_m.msg (s.ref);
			s.y = y + sy;
			char name[160], date[60]; who (m.from, name, sizeof name); if (!name[0]) scpy (name, "(unknown)", sizeof name);
			char em[160]; first_email (m.from, em, sizeof em);
			if (i) { canvas.fillRect (PAD, y, W - 2 * PAD, 1, col_line ()); y += 1; }
			if (!s.open)
			{	// folded: who, the first words, when
				int rh = 50;
				if (y + rh > 0 && y < height)
				{
					avatar (canvas, PAD + 16, y + rh / 2, 16, name, em);
					fmt_short (m.date, date, sizeof date);
					int dw = tw (date, F_SMALL);
					text (canvas, PAD + 42, y + 8, name, C_FIELD_TEXT, F_UI, (m.flags & F_SEEN) ? 0 : 1, W - 2 * PAD - 60 - dw);
					text (canvas, W - PAD - dw, y + 9, date, col_dim (), F_SMALL);
					text (canvas, PAD + 42, y + 27, m.preview ? m.preview : "", col_dim (), F_SMALL, 0, W - 2 * PAD - 50);
				}
				hits.add (0, y, W, rh, H_EXPAND, i);
				y += rh; s.h = rh;
				continue;
			}
			// the header
			int hy = y + 12;
			avatar (canvas, PAD + 20, hy + 20, 20, name, em);
			int nx = PAD + 52;
			fmt_long (m.date, date, sizeof date);
			int dw = tw (date, F_SMALL);
			text (canvas, nx, hy + 1, name, C_FIELD_TEXT, F_MID, 1, W - nx - dw - PAD - 90);
			hits.add (nx, hy, tw (name, F_MID, 1), 20, H_SENDER, i);
			char toLine[300]; char tos[240]; who (m.to, tos, sizeof tos, true);
			char myEmail[160]; scpy (myEmail, g_m.accts.a[s.ref.acct].email, sizeof myEmail);
			bool toMe = m.to && ifind (m.to, myEmail) && !strchr (m.to, ',');
			snprintf (toLine, sizeof toLine, "%s  \xC2\xB7  to %s", em, toMe ? "me" : tos[0] ? tos : "(nobody)");
			text (canvas, nx, hy + 22, toLine, col_dim (), F_SMALL, 0, W - nx - PAD - 100);
			text (canvas, W - PAD - dw, hy + 2, date, col_dim (), F_SMALL);
			// reply, reply all, forward, star
			int ix = W - PAD - 4 * 26;
			static const int IC[4] = { I_REPLY, I_REPLYALL, I_FORWARD, I_STAR };
			static const int HK[4] = { H_REPLY, H_REPLYALL, H_FORWARD, H_STAR };
			for (int k = 0; k < 4; k++)
			{
				bool on = k == 3 && (m.flags & F_FLAGGED);
				icon (canvas, k == 3 ? (on ? I_STAR : I_STAR_O) : IC[k], ix + k * 26 + 3, hy + 21, 18, on ? 0xF2A600 : col_dim ());
				hits.add (ix + k * 26, hy + 18, 26, 24, HK[k], i);
			}
			y = hy + 52;
			// the text
			if (s.missing) { text (canvas, PAD, y, "This message is not on the card any more.", col_dim ()); y += 30; }
			else if (!s.raw) { text (canvas, PAD, y, s.loading ? "Getting the message..." : "", col_dim ()); y += 30; }
			else if (i == webIdx)
			{	// the web view: the remote content held back (a bar), the page in a box filling the pane
				if (!s.webRemote) { Buf t; int p = s.mime->body_part (true); s.mime->text (s.mime->parts[p], t); s.webRemote = wv_has_remote (t.c (), t.n) ? 2 : 1; }
				if (!s.remoteOk && s.webRemote == 2) pictures_bar (y, W, i);
				s.bodyY = y + sy;
				// its height: the rest of the pane when that is most of it, else nearly the pane (scrolled to)
				int vh = viewH ();
				int tail = s.natt ? 26 + ((s.natt + 1) / 2) * 52 : 0;
				int fill = vh - (y + sy) - tail - 34;
				int bh = fill >= vh * 6 / 10 ? fill : vh - 40;
				if (bh < 160) bh = 160;
				char key[240];
				Store &st = *g_m.stores[s.ref.acct];
				snprintf (key, sizeof key, "%s/%s/%ld/%d", st.acct->id, st.folders[s.ref.folder].name, (long) st.folders[s.ref.folder].msgs[s.ref.msg].uid, s.remoteOk ? 1 : 0);
				wv_give (key, *s.mime, s.mime->body_part (true), s.remoteOk);
				if (g_wv.state != WS_OFF) wv_draw (canvas, PAD - 8, y, textW () + 16, bh, viewH ());
				if (g_wv.state == WS_OFF) { invalidate (true); return; }	// (it failed: Mail's renderer, next time)
				y += (g_wv.bh > 0 ? g_wv.bh : bh) + 14;
				attachments (s, y, W, i);
			}
			else
			{
				shown_layout (s, textW ());
				// the remote pictures held back: a bar
				if (!s.remoteOk && s.html->remote_pictures ()) pictures_bar (y, W, i);
				s.bodyY = y + sy;
				int bh = s.html->height ();
				unsigned bg = s.html->background ();
				bool hasBg = bg >> 24 && (bg & 0xFFFFFF) != (C_FIELD & 0xFFFFFF);
				if (s.html->width () > textW () + 2)
				{	// wider than the pane: drawn at its own width once, scaled down
					if (!s.zpx) zoom (s, hasBg ? bg : C_FIELD);
					bh = s.zh;
					if (s.zpx && y < viewH () && y + bh > 0)
						for (int j = 0; j < s.zh; j++) { int yy = y + j; if (yy < 0 || yy >= viewH ()) continue; memcpy (canvas.px + yy * canvas.stride + PAD, s.zpx + j * s.zw, sizeof (unsigned) * s.zw); }
				}
				else if (y < viewH () && y + bh > 0)
				{
					if (hasBg) canvas.fillRect (PAD - 8, y - 8, textW () + 16, bh + 16, bg);
					g_host->cv = &canvas; g_pics.s = &s; g_host->pics = &g_pics;
					s.html->paint (*g_host, PAD, y, 0, 0, width - 10, viewH ());
				}
				y += bh + 14;
				attachments (s, y, W, i);
			}
			y += 10;
			s.h = y + sy - s.y;
		}
		contentH = y + sy + 20;
		// the scroll bar
		int vh = viewH ();
		UkThumb t = uk_thumb (contentH, vh, sy, vh);
		if (t.show) uk_draw_vscroll (canvas, width - UK_SBW, 0, UK_SBW, vh, t, C_FIELD, barDrag);
		// the quick reply's band
		if (nsh)
		{
			canvas.fillRect (0, height - QR_H, width, QR_H, C_FIELD);
			canvas.fillRect (0, height - QR_H, width, 1, col_line ());
			char who2[160]; who (last.from, who2, sizeof who2);
			if (!quick->text[0] && !quick->hasFocus) { char ph[220]; snprintf (ph, sizeof ph, "Reply to %s...", who2); quick->setHint (ph); }
		}
	}
	// the remote pictures held back: a bar ("Show the pictures") at y, which it moves down
	void pictures_bar (int &y, int W, int i)
	{
		int bw = W - 2 * PAD;
		uk_fill_round (canvas, PAD, y, bw, 32, 6, uk_mix (C_FIELD, 0xF2A600, 40));
		icon (canvas, I_PICTURE, PAD + 10, y + 7, 18, uk_mix (C_FIELD, 0x8A6000, 220));
		text_v (canvas, PAD + 36, y, 32, "Pictures from the web are hidden.", C_FIELD_TEXT, F_SMALL, 0, bw - 190);
		const char *lb = "Show the pictures"; int lw = tw (lb, F_SMALL, 1);
		text_v (canvas, PAD + bw - lw - 12, y, 32, lb, C_ACCENT, F_SMALL, 1);
		hits.add (PAD + bw - lw - 20, y, lw + 20, 32, H_PICS, i);
		y += 42;
	}
	// a message's attachments at y (moved down past them)
	void attachments (Shown &s, int &y, int W, int i)
	{
		if (!s.natt) return;
		long total = 0; for (int k = 0; k < s.natt; k++) total += s.mime->size_of (s.mime->parts[s.att[k]]);
		char sz[40]; fmt_size (total, sz, sizeof sz);
		char l[120]; snprintf (l, sizeof l, "%d attachment%s  \xC2\xB7  %s  \xC2\xB7  ", s.natt, s.natt > 1 ? "s" : "", sz);
		text (canvas, PAD, y, l, col_dim (), F_SMALL, 1);
		int lx = PAD + tw (l, F_SMALL, 1);
		text (canvas, lx, y, s.natt > 1 ? "Save all" : "Save", C_ACCENT, F_SMALL, 1);
		hits.add (lx - 4, y - 2, tw ("Save all", F_SMALL, 1) + 8, 18, H_SAVEALL, i);
		y += 22;
		int cw = (W - 2 * PAD - 10) / 2; if (cw > 260) cw = 260;
		for (int k = 0; k < s.natt; k++)
		{
			int cx = PAD + (k % 2) * (cw + 10), cy = y + (k / 2) * 52;
			const Part &p = s.mime->parts[s.att[k]];
			uk_fill_round (canvas, cx, cy, cw, 44, 6, col_line ());
			uk_fill_round (canvas, cx + 1, cy + 1, cw - 2, 42, 5, C_FIELD);
			bool pic = ieq (p.type, "image");
			bool pdf = ieq (p.sub, "pdf");
			icon (canvas, pic ? I_PICTURE : I_FILE, cx + 10, cy + 10, 24, pic ? 0x3C8DA8 : pdf ? 0xC83C32 : 0x7B8794);
			const char *nm = p.name[0] ? p.name : pic ? "picture" : "attachment";
			text (canvas, cx + 42, cy + 6, nm, C_FIELD_TEXT, F_SMALL, 1, cw - 50);
			char ps[40]; fmt_size (s.mime->size_of (p), ps, sizeof ps);
			text (canvas, cx + 42, cy + 23, ps, col_dim (), F_SMALL);
			hits.add (cx, cy, cw, 44, H_ATT_OPEN, i, k);
		}
		y += ((s.natt + 1) / 2) * 52 + 4;
	}
	// a text drawn at its width W, then averaged down to the pane's (zw x zh)
	void zoom (Shown &s, unsigned bg)
	{
		int W = s.html->width (), H = s.html->height (), w = textW ();
		if (W <= 0 || H <= 0 || w <= 0) return;
		if (H > 6000) H = 6000;
		Canvas tmp; if (!tmp.alloc (W, H)) return;
		tmp.clear (0xFF000000u | bg);
		g_host->cv = &tmp; g_pics.s = &s; g_host->pics = &g_pics;
		s.html->paint (*g_host, 0, 0, 0, 0, W, H);
		int h = (int) ((long long) H * w / W); if (h < 1) h = 1;
		s.zpx = (unsigned *) malloc (sizeof (unsigned) * w * h); if (!s.zpx) return;
		s.zw = w; s.zh = h;
		for (int j = 0; j < h; j++)
		{
			int y0 = (int) ((long long) j * H / h), y1 = (int) ((long long) (j + 1) * H / h); if (y1 <= y0) y1 = y0 + 1;
			for (int i = 0; i < w; i++)
			{
				int x0 = (int) ((long long) i * W / w), x1 = (int) ((long long) (i + 1) * W / w); if (x1 <= x0) x1 = x0 + 1;
				unsigned r = 0, g = 0, b = 0, n = 0;
				for (int yy = y0; yy < y1 && yy < H; yy++) for (int xx = x0; xx < x1 && xx < W; xx++) { unsigned c = tmp.px[yy * tmp.stride + xx]; r += c >> 16 & 255; g += c >> 8 & 255; b += c & 255; n++; }
				s.zpx[j * w + i] = n ? 0xFF000000u | (r / n) << 16 | (g / n) << 8 | (b / n) : 0xFF000000u | bg;
			}
		}
	}
	void scroll_to (int v) { int mx = contentH - viewH (); if (v > mx) v = mx; if (v < 0) v = 0; if (v != sy) { sy = v; invalidate (true); } }
	// The link of a message's text under a point of the pane, 0 when there is none.
	const char *link_under (int mx, int my)
	{
		for (int i = 0; i < nsh; i++)
		{
			Shown &s = sh[i]; if (!s.html || !s.open) continue;
			int dy = s.bodyY - sy;
			int lx = mx - PAD, ly = my - dy;
			if (s.zpx && s.zw > 0) { lx = (int) ((long long) lx * s.html->width () / s.zw); ly = (int) ((long long) ly * s.html->width () / s.zw); }
			const char *href = s.html->link_at (lx, ly);
			if (href) return href;
		}
		return 0;
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		static bool was;
		// the wheel over the web view's box while the box is not all in sight: the pane scrolls first (the
		// box is nearly the pane's height: its page scrolls once the box is in view)
		if (wheel && !barDrag && g_wv.shown && g_wv.state == WS_UP
		    && ((wheel < 0 && g_wv.by + g_wv.bh > viewH () && sy < contentH - viewH ()) || (wheel > 0 && g_wv.by < 0 && sy > 0)))
		{
			int by = g_wv.by, to = wheel < 0 ? sy + (by + g_wv.bh - viewH ()) : sy + by;	// (the box's edge, no further)
			int step = sy - wheel * 48;
			scroll_to (wheel < 0 ? (step < to ? step : to) : (step > to ? step : to));
			return true;
		}
		// over the web view's box: its (the keys too, once clicked there)
		if (!barDrag && wv_mouse (mx, my, bl, br, bm, wheel)) { if (bl && !was) setFocus (); was = bl; return true; }
		if (wheel) { scroll_to (sy - wheel * 48); return true; }
		bool down = bl && !was; was = bl;
		if (barDrag)
		{
			if (!bl) { barDrag = false; invalidate (true); return true; }
			int vh = viewH (); UkThumb t = uk_thumb (contentH, vh, sy, vh);
			scroll_to ((int) uk_thumb_pos (my, vh, contentH, vh, t.h)); return true;
		}
		if (my < viewH () && mx < width - UK_SBW && link_under (mx, my)) uk_cursor (KAPI_CURSOR_HAND);
		if (!down) return my < viewH ();
		if (mx >= width - UK_SBW && my < viewH ()) { barDrag = true; int vh = viewH (); UkThumb t = uk_thumb (contentH, vh, sy, vh); scroll_to ((int) uk_thumb_pos (my, vh, contentH, vh, t.h)); return true; }
		if (my >= viewH ()) return false;
		const Hit *h = hits.at (mx, my);
		if (h) { act (*h, mx, my); return true; }
		// a link in a text
		for (int i = 0; i < nsh; i++)
		{
			Shown &s = sh[i]; if (!s.html || !s.open) continue;
			int dy = s.bodyY - sy;
			int lx = mx - PAD, ly = my - dy;
			if (s.zpx && s.zw > 0) { lx = (int) ((long long) lx * s.html->width () / s.zw); ly = (int) ((long long) ly * s.html->width () / s.zw); }
			const char *href = s.html->link_at (lx, ly);
			if (href) { open_link (href); return true; }
		}
		return true;
	}
	void open_link (const char *href)
	{
		if (istarts (href, "mailto:"))
		{
			char a[200]; scpy (a, href + 7, sizeof a); char *q = strchr (a, '?'); if (q) *q = 0;
			Buf d; html::decode_text (d, a, (int) strlen (a));
			compose_to ("", d.c ());
			return;
		}
		if (istarts (href, "http://") || istarts (href, "https://"))
		{
			// asked first: a link in a mail may not go where it says
			char q[700]; snprintf (q, sizeof q, "Open this link in Jet?\n\n%.600s", href);
			if (uk_messagebox ("Mail", q, MB_YESNO) != 1) return;
			kapi_exec (WV_PROGRAM, href);
		}
	}
	void act (const Hit &h, int mx, int my)
	{
		(void) mx; (void) my;
		Shown &s = sh[h.a];
		switch (h.kind)
		{
		case H_EXPAND: s.open = true; fetch (s); if (!(g_m.msg (s.ref).flags & F_SEEN)) g_m.set_flag (&s.ref, 1, F_SEEN, true); invalidate (true); break;
		case H_REPLY: compose_new (1, &s.ref); break;
		case H_REPLYALL: compose_new (2, &s.ref); break;
		case H_FORWARD: compose_new (3, &s.ref); break;
		case H_STAR: { bool on = !(g_m.msg (s.ref).flags & F_FLAGGED); g_m.set_flag (&s.ref, 1, F_FLAGGED, on); refresh_all (); break; }
		case H_PICS: s.remoteOk = true; s.layW = -1; invalidate (true); break;
		case H_SENDER: sender_menu (s, h.x + left, h.y + h.h + top); break;
		case H_ATT_OPEN: attachment_menu (s, h.b, h.x + left + 20, h.y + top + 30); break;
		case H_SAVEALL: save_all (s); break;
		}
	}
	void sender_menu (Shown &s, int x, int y)
	{
		const Msg &m = g_m.msg (s.ref);
		Addr a[2]; int n = parse_addrs (m.from ? m.from : "", a, 2); if (!n) return;
		bool known = g_m.contacts.find (a[0].email) >= 0;
		PopupMenu pm (x, y);
		pm.add ("Write to them", 1, true); pm.add (known ? "Show in the contacts" : "Add to the contacts", 2, true); pm.add ("Copy the address", 3, true);
		int r = pm.run ();
		if (r == 1) compose_to (a[0].name, a[0].email);
		else if (r == 2)
		{
			if (!known) { Contact c; memset (&c, 0, sizeof c); scpy (c.name, a[0].name[0] ? a[0].name : a[0].email, sizeof c.name); scpy (c.email, a[0].email, sizeof c.email); g_m.contacts.add (c); g_m.contacts.save (); status_note ("Added to the contacts."); }
			show_contacts (true);
		}
		else if (r == 3) ::clip_set_text (a[0].email);
	}
	// the attachment written out to the card (SD:/Downloads, or where the user says)
	bool write_att (Shown &s, int k, const char *path)
	{
		Buf b; s.mime->decoded (s.mime->parts[s.att[k]], b);
		return kapi_save_file (path, b.c (), (unsigned) b.n) >= 0;
	}
	static void safe_name (const char *in, char *out, int cap)
	{
		int k = 0; for (const char *p = in; *p && k < cap - 1; p++) out[k++] = strchr ("/\\:*?\"<>|", *p) ? '_' : *p;
		out[k] = 0; if (!k) scpy (out, "attachment", cap);
	}
	void attachment_menu (Shown &s, int k, int x, int y)
	{
		const Part &p = s.mime->parts[s.att[k]];
		char nm[200]; safe_name (p.name[0] ? p.name : "attachment", nm, sizeof nm);
		char app[40]; char tmp[300]; snprintf (tmp, sizeof tmp, "SD:/tmp/mail/%s", nm);
		bool can = fa_app_for (tmp, app, sizeof app);
		PopupMenu pm (x, y);
		char ol[80]; snprintf (ol, sizeof ol, can ? "Open (%s)" : "Open", app);
		pm.add (ol, 1, can); pm.add ("Save as...", 2, true);
		int r = pm.run ();
		if (r == 1) { kapi_mkdir ("SD:/tmp"); kapi_mkdir ("SD:/tmp/mail"); if (write_att (s, k, tmp)) fa_open (tmp); else uk_messagebox ("Mail", "The attachment could not be written to the card.", MB_OK); }
		else if (r == 2)
		{
			char out[300]; kapi_mkdir ("SD:/Downloads");
			if (uk_file_save (out, sizeof out, "SD:/Downloads", nm) && !write_att (s, k, out)) uk_messagebox ("Mail", "The attachment could not be written.", MB_OK);
		}
	}
	void save_all (Shown &s)
	{
		kapi_mkdir ("SD:/Downloads");
		int ok = 0;
		for (int k = 0; k < s.natt; k++)
		{
			char nm[200]; safe_name (s.mime->parts[s.att[k]].name, nm, sizeof nm);
			char path[300]; snprintf (path, sizeof path, "SD:/Downloads/%s", nm);
			if (write_att (s, k, path)) ok++;
		}
		char m[120]; snprintf (m, sizeof m, "%d of %d saved in SD:/Downloads.", ok, s.natt);
		status_note (m);
	}
	bool onKey (long k) override
	{
		if (wv_key (k)) return true;			// (the web view was clicked last: its page's)
		if (k == KEY_PGDN || k == ' ') { scroll_to (sy + viewH () - 40); return true; }
		if (k == KEY_PGUP) { scroll_to (sy - viewH () + 40); return true; }
		return false;
	}
};

// the web view's calls (webview.h)
static void wv_repaint () { if (g_read) g_read->invalidate (true); }
static void wv_link (const char *url) { if (g_read) g_read->open_link (url); }

} // namespace mailapp

#endif
