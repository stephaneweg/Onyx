//
// rich.h -- a message's text laid out: the words wrapped to a width, the emoticons (emoticons.h) as little
// pictures in the line, the links (http://, https://, www., t.me/) in the link colour, the line breaks kept.
// One layout a message, kept until the width changes; drawn at any place.
//
// MIT licence.
//
#ifndef TG_RICH_H
#define TG_RICH_H

#include <stdlib.h>
#include <string.h>
#include "emoticons.h"
#include "look.h"

enum { RP_TEXT, RP_EMO, RP_LINK };

struct RichPiece { int x, line, w; short kind, emo; int off, len; };

struct Rich
{
	RichPiece *p; int n, cap;
	int lines, width, lineH, emo;		// emo: the emoticons' size
	int widest;				// the widest line, px
	Rich () : p (0), n (0), cap (0), lines (0), width (0), lineH (16), emo (18), widest (0) {}
	~Rich () { free (p); }
	void clear () { n = 0; lines = 0; widest = 0; }
	RichPiece &add ()
	{
		if (n == cap) { cap = cap ? cap * 2 : 16; p = (RichPiece *) realloc (p, sizeof (RichPiece) * (size_t) cap); }
		memset (&p[n], 0, sizeof p[n]);
		return p[n++];
	}
	int height () const { return lines * lineH; }
};

static bool rich_link_at (const char *s, int n)
{
	static const char *pre[] = { "http://", "https://", "www.", "t.me/" };
	for (int i = 0; i < 4; i++)
	{
		int l = (int) strlen (pre[i]);
		if (n >= l && !strncmp (s, pre[i], (size_t) l)) return true;
	}
	return false;
}

// The width of n bytes of s in the face.
static int rich_w (FtTextFace *f, const char *s, int n, int style = 0) { return n > 0 ? f->widthN (s, n, style) : 0; }

// text laid out in width px (emo: the emoticons' size; typed: ":)" made a picture too).
static void rich_layout (Rich &r, FtTextFace *f, const char *text, int width, int emo, bool typed = true)
{
	r.clear ();
	r.width = width; r.emo = emo;
	int fh = f->height ();
	r.lineH = (emo > fh ? emo : fh) + 3;
	int n = (int) strlen (text), i = 0, x = 0, line = 0;
	int space = rich_w (f, " ", 1);
	bool any = false;
	while (i < n)
	{
		char c = text[i];
		if (c == '\n') { line++; x = 0; i++; continue; }
		if (c == ' ' || c == '\t') { if (x > 0) x += space; i++; continue; }
		if (c == '\r') { i++; continue; }
		int el;
		int id = emo_at (text + i, n - i, &el, typed);
		if (id >= 0)
		{
			if (x > 0 && x + emo > width) { line++; x = 0; }
			RichPiece &pc = r.add ();
			pc.kind = RP_EMO; pc.emo = (short) id; pc.x = x; pc.line = line; pc.w = emo; pc.off = i; pc.len = el;
			x += emo + 1; i += el; any = true;
			if (x > r.widest) r.widest = x;
			continue;
		}
		// a word: up to a space, a line break or an emoticon
		int j = i;
		bool link = rich_link_at (text + i, n - i);
		while (j < n && text[j] != ' ' && text[j] != '\n' && text[j] != '\t')
		{
			int el2;
			if (j > i && emo_at (text + j, n - j, &el2, typed && !link) >= 0) break;
			j = uk_u8_next (text, j, n);
		}
		int ww = rich_w (f, text + i, j - i);
		if (x > 0 && x + ww > width) { line++; x = 0; }
		if (ww > width)				// a word wider than the line: cut by characters
		{
			int k = i;
			while (k < j)
			{
				int e = k, wpart = 0;
				while (e < j)
				{
					int e2 = uk_u8_next (text, e, n);
					int w2 = rich_w (f, text + k, e2 - k);
					if (w2 > width - x && e > k) break;
					e = e2; wpart = w2;
				}
				RichPiece &pc = r.add ();
				pc.kind = link ? RP_LINK : RP_TEXT; pc.x = x; pc.line = line; pc.w = wpart; pc.off = k; pc.len = e - k;
				if (x + wpart > r.widest) r.widest = x + wpart;
				k = e;
				if (k < j) { line++; x = 0; } else x += wpart;
			}
		}
		else
		{
			RichPiece &pc = r.add ();
			pc.kind = link ? RP_LINK : RP_TEXT; pc.x = x; pc.line = line; pc.w = ww; pc.off = i; pc.len = j - i;
			x += ww;
			if (x > r.widest) r.widest = x;
		}
		i = j; any = true;
	}
	r.lines = any || n ? line + 1 : 0;
	// the spaces between pieces of a line run on: the text pieces of a line drawn with their spaces is
	// not needed -- each piece has its own x.
}

// Drawn at (x, y): text in ink, links in TC_LINK underlined. big: a message of emoticons alone.
static void rich_draw (Canvas &cv, const Rich &r, FtTextFace *f, const char *text, int x, int y, unsigned ink, int style = 0)
{
	char buf[1024];
	int fh = f->height ();
	for (int i = 0; i < r.n; i++)
	{
		const RichPiece &pc = r.p[i];
		int ly = y + pc.line * r.lineH;
		if (ly > cv.h || ly + r.lineH < 0) continue;
		if (pc.kind == RP_EMO) { emo_draw (cv, pc.emo, x + pc.x, ly + (r.lineH - r.emo) / 2, r.emo); continue; }
		int len = pc.len < (int) sizeof buf - 1 ? pc.len : (int) sizeof buf - 1;
		memcpy (buf, text + pc.off, (size_t) len); buf[len] = 0;
		int ty = ly + (r.lineH - fh) / 2;
		unsigned c = pc.kind == RP_LINK ? TC_LINK : ink;
		f->draw (cv, x + pc.x, ty, buf, c, style);
		if (pc.kind == RP_LINK) cv.fillRect (x + pc.x, ty + f->ascent () + 2, pc.w, 1, c);
	}
}

// The link under (px, py) (relative to where the text is drawn) into out -> true.
static bool rich_link (const Rich &r, const char *text, int px, int py, char *out, int cap)
{
	for (int i = 0; i < r.n; i++)
	{
		const RichPiece &pc = r.p[i];
		if (pc.kind != RP_LINK) continue;
		int ly = pc.line * r.lineH;
		if (px >= pc.x && px < pc.x + pc.w && py >= ly && py < ly + r.lineH)
		{
			int len = pc.len < cap - 1 ? pc.len : cap - 1;
			memcpy (out, text + pc.off, (size_t) len); out[len] = 0;
			return true;
		}
	}
	return false;
}

// Only emoticons (and spaces), and few: a message drawn big.
static bool rich_only_emo (const char *text)
{
	int n = (int) strlen (text), i = 0, count = 0;
	while (i < n)
	{
		if (text[i] == ' ' || text[i] == '\n') { i++; continue; }
		int el;
		if (emo_at (text + i, n - i, &el, false) < 0) return false;
		i += el; count++;
	}
	return count > 0 && count <= 3;
}

#endif
