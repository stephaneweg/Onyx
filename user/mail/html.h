//
// mail/html.h -- Mail's own HTML renderer (the user, 2026-10-02: "simple, not Jet, HTML 4 at least with CSS 2"): a
// message's HTML parsed (html_dom.h), styled (html_css.h), laid out at a width (html_layout.h) and painted through a
// Painter (html_ft.h: FreeType's text and wtk's canvas). Safe by construction: no scripts, no forms, no frames; the
// remote pictures shown only when the Host says so (Mail: "Show the pictures"); the links reported, never followed.
//
//   mail::html::Html h;
//   h.parse (utf8, n);                    the message's HTML (UTF-8: Mime::text)
//   h.layout (host, 600);                 laid out 600 px wide -> h.width (), h.height ()
//   h.paint (painter, ox, oy, clip...);   drawn (the document's (0, 0) at ox, oy)
//   const char *href = h.link_at (x, y);  the link under a point (document coordinates)
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See mail/util.h for the full notice.
//
#ifndef ONYX_MAIL_HTML_H
#define ONYX_MAIL_HTML_H

#include "mail/html_layout.h"

namespace mail {
namespace html {

struct Painter
{
	virtual ~Painter () {}
	virtual void fill (int x, int y, int w, int h, unsigned argb) = 0;
	virtual void text (int font, int x, int baseline, const char *s, int n, unsigned argb) = 0;
	virtual void picture (const char *src, int x, int y, int w, int h) = 0;
	virtual void clip (int x0, int y0, int x1, int y1) = 0;		// (what is drawn stays inside)
};

struct Html
{
	Doc doc;
	Styles styles;
	Layout *lay;
	int baseSize; unsigned textColour;
	Html () : lay (0), baseSize (14), textColour (0xFF202124) {}
	~Html () { delete lay; }
	Html (const Html &) = delete;
	Html &operator= (const Html &) = delete;

	void parse (const char *s, int n) { delete lay; lay = 0; doc.parse (s, n); }
	// a plain text shown the same way (its lines kept, its links found)
	void parse_text (const char *s, int n)
	{
		Buf h; h.add ("<div style=\"white-space:pre-wrap\">");
		const char *e = s + n;
		while (s < e)
		{
			// a link: http(s)://... or www.
			if ((e - s > 8 && (!memcmp (s, "https://", 8) || !memcmp (s, "http://", 7))) || (e - s > 4 && !memcmp (s, "www.", 4) && (s == e - n || !is_word (s[-1]))))
			{
				const char *a = s; while (s < e && !strchr (" \t\r\n<>\"'", *s)) s++;
				while (s > a && strchr (".,;:!?)]", s[-1])) s--;
				Buf u; u.add (a, (int) (s - a));
				h.add ("<a href=\""); if (*a == 'w') h.add ("https://"); escape (h, u.c (), u.n); h.add ("\">"); escape (h, u.c (), u.n); h.add ("</a>");
				continue;
			}
			// a quoted line ("> ..."): greyed with a bar
			if ((s == e - n || s[-1] == '\n') && *s == '>')
			{
				const char *le = (const char *) memchr (s, '\n', e - s); if (!le) le = e;
				h.add ("<span style=\"color:#5f6368\">"); escape (h, s, (int) (le - s)); h.add ("</span>");
				s = le; continue;
			}
			const char *a = s; while (s < e && *s != 'h' && *s != 'w' && *s != '>') s++;
			if (s == a) s++;
			escape (h, a, (int) (s - a));
		}
		h.add ("</div>");
		parse (h.c (), h.n);
	}
	static bool is_word (char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
	static void escape (Buf &o, const char *s, int n)
	{
		for (int i = 0; i < n; i++) { char c = s[i]; if (c == '<') o.add ("&lt;"); else if (c == '>') o.add ("&gt;"); else if (c == '&') o.add ("&amp;"); else if (c == '"') o.add ("&quot;"); else o.addc (c); }
	}
	void layout (Host &host, int w)
	{
		cascade (doc, styles, w, baseSize, textColour);
		delete lay; lay = new Layout (doc, styles, host);
		lay->run (w);
	}
	int width () const { return lay ? lay->width : 0; }
	int height () const { return lay ? lay->height : 0; }
	unsigned background () const { return lay ? lay->bg : 0; }
	const char *title () const { return doc.title; }

	// drawn: the document's (0, 0) at (ox, oy); only what meets [cx0, cx1) x [cy0, cy1)
	void paint (Painter &p, int ox, int oy, int cx0, int cy0, int cx1, int cy1)
	{
		if (!lay) return;
		p.clip (cx0, cy0, cx1, cy1);
		for (int i = 0; i < lay->items.n; i++)
		{
			const Item &it = lay->items[i];
			int x = it.x + ox, y = it.y + oy;
			switch (it.kind)
			{
			case I_RECT:
				if (it.w <= 0 || it.h <= 0 || !(it.color >> 24)) break;
				if (x >= cx1 || y >= cy1 || x + it.w <= cx0 || y + it.h <= cy0) break;
				if (it.dash)
				{	// dashed / dotted: segments along its long side
					int seg = it.dash == BS_DOTTED ? (it.w < it.h ? it.w : it.h) : 3 * (it.w < it.h ? it.w : it.h) + 2;
					if (seg < 1) seg = 1;
					if (it.w >= it.h) for (int k = 0; k < it.w; k += seg * 2) p.fill (x + k, y, k + seg > it.w ? it.w - k : seg, it.h, it.color);
					else for (int k = 0; k < it.h; k += seg * 2) p.fill (x, y + k, it.w, k + seg > it.h ? it.h - k : seg, it.color);
				}
				else p.fill (x, y, it.w, it.h, it.color);
				break;
			case I_TEXT:
			{
				if (x >= cx1 || x + it.w <= cx0 || y - it.h * 2 >= cy1 || y + it.h <= cy0) break;
				p.text (it.font, x, y, it.s, it.n, it.color);
				if (it.deco)
				{
					int asc, desc; lay->host.metrics (it.font, &asc, &desc);
					int th = asc / 14 > 1 ? asc / 14 : 1;
					// trailing spaces not underlined
					int w = it.w; int n = it.n; while (n > 0 && it.s[n - 1] == ' ') n--; if (n < it.n) w = lay->host.width (it.font, it.s, n);
					if (it.deco & TD_UNDERLINE) p.fill (x, y + desc / 3 + 1, w, th, it.color2);
					if (it.deco & TD_LINE_THROUGH) p.fill (x, y - asc * 3 / 10, w, th, it.color2);
					if (it.deco & TD_OVERLINE) p.fill (x, y - asc, w, th, it.color2);
				}
				break;
			}
			case I_IMAGE:
				if (x >= cx1 || y >= cy1 || x + it.w <= cx0 || y + it.h <= cy0) break;
				p.picture (it.s, x, y, it.w, it.h);
				break;
			case I_BLOCKED:
			{
				if (x >= cx1 || y >= cy1 || x + it.w <= cx0 || y + it.h <= cy0) break;
				// a blocked picture: a light box, a thin frame, a small picture sign when there is room
				p.fill (x, y, it.w, it.h, it.color);
				p.fill (x, y, it.w, 1, it.color2); p.fill (x, y + it.h - 1, it.w, 1, it.color2); p.fill (x, y, 1, it.h, it.color2); p.fill (x + it.w - 1, y, 1, it.h, it.color2);
				if (it.w >= 24 && it.h >= 20)
				{
					int cx = x + it.w / 2 - 8, cy = y + it.h / 2 - 6;
					p.fill (cx, cy, 16, 12, it.color2); p.fill (cx + 1, cy + 1, 14, 10, it.color);
					p.fill (cx + 3, cy + 7, 3, 3, it.color2); p.fill (cx + 6, cy + 5, 3, 5, it.color2); p.fill (cx + 9, cy + 3, 4, 7, it.color2);
				}
				break;
			}
			default: break;
			}
		}
	}
	// the link under a point (document coordinates) -> its href (0: none)
	const char *link_at (int x, int y) const
	{
		if (!lay) return 0;
		for (int i = lay->items.n - 1; i >= 0; i--)
		{
			const Item &it = lay->items[i];
			if (it.link < 0) continue;
			int top = it.kind == I_TEXT ? it.y - it.h * 3 / 4 : it.y, h = it.kind == I_TEXT ? it.h : it.h;
			if (x >= it.x && x < it.x + it.w && y >= top && y < top + h) return doc.attr (it.link, "href");
		}
		return 0;
	}
	// the document's text (search, copy, the reply's quote): its lines in order
	void plain_text (Buf &o) const
	{
		if (!lay) return;
		int lastY = -1 << 30, lastEnd = 0;
		for (int i = 0; i < lay->items.n; i++)
		{
			const Item &it = lay->items[i];
			if (it.kind != I_TEXT) continue;
			if (lastY != (-1 << 30) && it.y != lastY) { o.addc ('\n'); if (it.y - lastY > it.h * 3 / 2) o.addc ('\n'); }
			else if (lastY == it.y && it.x > lastEnd + 2 && o.n && o.p[o.n - 1] != ' ') o.addc (' ');
			o.add (it.s, it.n);
			lastY = it.y; lastEnd = it.x + it.w;
		}
	}
	// the pictures it would show (remote ones: is there one? -- "Show the pictures")
	int remote_pictures () const
	{
		int k = 0;
		for (int i = 0; i < doc.nodes.n; i++)
		{
			if (doc.nodes[i].tag != T_IMG) continue;
			const char *s = doc.attr (i, "src");
			if (s && (istarts (s, "http://") || istarts (s, "https://") || istarts (s, "//"))) k++;
		}
		return k;
	}
};

} // namespace html
} // namespace mail

#endif
