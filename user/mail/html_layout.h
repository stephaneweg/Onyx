//
// mail/html_layout.h -- the styled tree laid out at a width into a display list: CSS 2's block boxes (margins, the
// vertical ones collapsing between siblings, borders, padding, width / min / max, margin:auto), the inline flow (words
// broken into lines, white-space, text-align, vertical-align, line-height, the inline boxes' backgrounds and padding
// -- mail's "buttons" --, inline-blocks), floats (left / right, the lines beside them, clear), lists (their markers),
// tables (the automatic layout: the columns' min / max widths, colspan, rowspan, cellspacing / padding, borders,
// vertical-align in the cells, align="center"), pictures (their size from the attributes, CSS or the picture; blocked
// ones as a box). The display list -- rectangles, text runs, pictures, each with its link -- is drawn by the app
// (mail/html.h: Html::paint) and hit-tested for the links. Part of Mail's own HTML renderer (mail/html.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See mail/util.h for the full notice.
//
#ifndef ONYX_MAIL_HTML_LAYOUT_H
#define ONYX_MAIL_HTML_LAYOUT_H

#include "mail/html_css.h"

namespace mail {
namespace html {

// ---- what the app gives: the fonts' measures, the pictures' sizes -------------------------------------------------------------
struct Host
{
	virtual ~Host () {}
	virtual int font (int family, int px, bool bold, bool italic) = 0;	// a handle
	virtual int width (int font, const char *s, int n) = 0;		// the text's advance, px
	virtual void metrics (int font, int *ascent, int *descent) = 0;	// px
	// a picture's size (src: "cid:x", "https://..."): true when it will be drawn (its size given), false: not shown
	// (blocked, or not there)
	virtual bool image (const char *src, int *w, int *h) = 0;
};

// ---- the display list -------------------------------------------------------------------------------------------------------
enum { I_RECT, I_TEXT, I_IMAGE, I_BLOCKED, I_HLINE };
struct Item
{
	unsigned char kind, deco, dash;		// (dash: a rectangle's border style: BS_DASHED / BS_DOTTED)
	int font;
	int x, y, w, h;				// (a text: x its left, y its baseline, w its width, h its line's height)
	unsigned color, color2;			// (a text: its colour, its decoration's)
	const char *s; int n;			// a text's UTF-8 / a picture's src
	int link;				// the <a>'s node (-1: none)
	int node;
};
struct Float { int x, y, w, h; int side; };
struct Bfc { Vec<Float> floats; };

struct Layout
{
	const Doc &d; Styles &S; Host &host;
	Vec<Item> items;
	Arena text;
	int width, height;			// the document's laid out size
	int *minW, *maxW;			// the intrinsic widths, per node (-1: not yet)
	unsigned bg;				// the page's background (the root's / body's)
	// a list item's marker waiting for its first line
	struct Marker { bool on; int x; int node; } marker;

	Layout (const Doc &doc, Styles &st, Host &h) : d (doc), S (st), host (h), width (0), height (0), minW (0), maxW (0), bg (0) { marker.on = false; }
	~Layout () { free (minW); free (maxW); }
	const Style &st (int node) const { return S.s[node]; }

	// ---- the boxes' edges ----------------------------------------------------------------------------------------------------
	int pad (int node, int side, int cw) const { return resolve (st (node).padding[side], cw); }
	int bord (int node, int side) const { return st (node).borderW[side]; }
	int marg (int node, int side, int cw) const { const Len &l = st (node).margin[side]; return l.u == U_AUTO ? 0 : resolve (l, cw); }
	int edges_h (int node, int cw) const { return pad (node, 1, cw) + pad (node, 3, cw) + bord (node, 1) + bord (node, 3); }
	int edges_v (int node, int cw) const { return pad (node, 0, cw) + pad (node, 2, cw) + bord (node, 0) + bord (node, 2); }

	int font_of (const Style &s) { return host.font (s.family, s.fontSize, s.bold, s.italic); }
	int line_height (const Style &s, int asc, int desc) const
	{
		if (s.lineNum > 0) return (int) (s.lineNum * s.fontSize + 0.5f);
		if (s.lineHeight.u == U_PX) return (int) (s.lineHeight.v + 0.5f);
		return asc + desc;
	}
	bool is_block_level (int node) const
	{
		const Node &e = d.nodes[node];
		if (e.tag == T_TEXT) return false;
		int dp = st (node).display;
		if (dp == D_NONE || dp == D_INLINE || dp == D_INLINE_BLOCK || dp == D_INLINE_TABLE) return st (node).floatSide != FL_NONE && dp != D_NONE;
		return true;
	}
	// an inline element with a block inside (<a><div>...</div></a>): laid out as a block
	bool holds_block (int node, int depth = 0) const
	{
		if (depth > 30) return false;
		for (int c = d.nodes[node].first; c >= 0; c = d.nodes[c].next)
		{
			if (d.nodes[c].tag == T_TEXT) continue;
			int dp = st (c).display;
			if (dp == D_NONE) continue;
			if (is_block_level (c)) return true;
			if (dp == D_INLINE && holds_block (c, depth + 1)) return true;
		}
		return false;
	}
	bool block_like (int node) const { return is_block_level (node) || (st (node).display == D_INLINE && d.nodes[node].tag != T_TEXT && holds_block (node)); }

	// ---- items ------------------------------------------------------------------------------------------------------------------
	int rect (int x, int y, int w, int h, unsigned c, int node, int link = -1)
	{
		Item &it = items.push (); it.kind = I_RECT; it.x = x; it.y = y; it.w = w; it.h = h; it.color = c; it.node = node; it.link = link; it.font = -1;
		return items.n - 1;
	}
	// a box's background and borders, given its border box (bgItem: the rectangle reserved before its content)
	void box_paint (int node, int bgItem, int x, int y, int w, int h)
	{
		const Style &s = st (node);
		Item &b = items[bgItem]; b.x = x; b.y = y; b.w = w; b.h = h; b.color = s.bg;
		if (!(s.bg >> 24)) b.w = 0;
		if (!s.visible) b.w = 0;
		for (int side = 0; side < 4; side++)
		{
			int bw = s.borderW[side]; if (bw <= 0 || !s.visible) continue;
			unsigned c = s.borderC[side];
			if (s.borderS[side] == BS_INSET || s.borderS[side] == BS_GROOVE) c = side == 0 || side == 3 ? shade (c, -60) : shade (c, 50);
			if (s.borderS[side] == BS_OUTSET || s.borderS[side] == BS_RIDGE) c = side == 0 || side == 3 ? shade (c, 50) : shade (c, -60);
			int r;
			switch (side)
			{
			case 0: r = rect (x, y, w, bw, c, node); break;
			case 1: r = rect (x + w - bw, y, bw, h, c, node); break;
			case 2: r = rect (x, y + h - bw, w, bw, c, node); break;
			default: r = rect (x, y, bw, h, c, node); break;
			}
			items[r].dash = (s.borderS[side] == BS_DASHED || s.borderS[side] == BS_DOTTED) ? s.borderS[side] : 0;
		}
	}
	static unsigned shade (unsigned c, int k)
	{
		int r = (c >> 16 & 255) + k, g = (c >> 8 & 255) + k, b = (c & 255) + k;
		r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
		return (c & 0xFF000000u) | (unsigned) r << 16 | (unsigned) g << 8 | (unsigned) b;
	}
	void shift (int from, int to, int dx, int dy) { for (int i = from; i < to; i++) { items[i].x += dx; items[i].y += dy; } }

	// ---- floats ---------------------------------------------------------------------------------------------------------------
	// the room for a line at y (h high) between x0 and x1, the floats taken out
	void room (Bfc &bfc, int y, int h, int x0, int x1, int *l, int *r)
	{
		*l = x0; *r = x1;
		for (int i = 0; i < bfc.floats.n; i++)
		{
			const Float &f = bfc.floats[i];
			if (f.y >= y + (h > 0 ? h : 1) || f.y + f.h <= y) continue;
			if (f.side == FL_LEFT) { if (f.x + f.w > *l && f.x < x1) *l = f.x + f.w; }
			else if (f.x < *r && f.x + f.w > x0) *r = f.x;
		}
	}
	int floats_bottom (Bfc &bfc, int side)		// (side: 1 left, 2 right, 3 both)
	{
		int b = 0;
		for (int i = 0; i < bfc.floats.n; i++) if ((side & (bfc.floats[i].side == FL_LEFT ? 1 : 2)) && bfc.floats[i].y + bfc.floats[i].h > b) b = bfc.floats[i].y + bfc.floats[i].h;
		return b;
	}
	// a float placed at y or lower, between x0 and x1: laid out, moved there -> its place recorded
	void place_float (int node, int x0, int x1, int y, Bfc &bfc)
	{
		const Style &s = st (node);
		int cw = x1 - x0;
		int w = shrink_width (node, cw);
		int first = items.n;
		int h = layout_box (node, 0, 0, w, true);		// (at 0, 0: moved below)
		int mw = w + marg (node, 1, cw) + marg (node, 3, cw);
		int mh = h;
		int py = y;
		for (int tries = 0; tries < 200; tries++)
		{
			int l, r; room (bfc, py, mh, x0, x1, &l, &r);
			if (r - l >= mw || (l == x0 && r == x1)) { int px2 = s.floatSide == FL_LEFT ? l : r - mw; shift (first, items.n, px2 + marg (node, 3, cw), py); Float &f = bfc.floats.push (); f.x = px2; f.y = py; f.w = mw; f.h = mh; f.side = s.floatSide; return; }
			// lower: to the next float's bottom
			int next = 1 << 30;
			for (int i = 0; i < bfc.floats.n; i++) { int b = bfc.floats[i].y + bfc.floats[i].h; if (b > py && b < next) next = b; }
			if (next == 1 << 30) break;
			py = next;
		}
		shift (first, items.n, x0 + marg (node, 3, cw), py);
		Float &f = bfc.floats.push (); f.x = x0; f.y = py; f.w = mw; f.h = mh; f.side = s.floatSide;
	}

	// ---- intrinsic widths (min-content / max-content), for tables and shrink-to-fit -----------------------------------------
	void intrinsic (int node, int *mn, int *mx)
	{
		if (minW[node] >= 0) { *mn = minW[node]; *mx = maxW[node]; return; }
		minW[node] = maxW[node] = 0;			// (a loop guard)
		const Style &s = st (node);
		int a = 0, b = 0;
		if (s.display == D_TABLE || s.display == D_INLINE_TABLE) table_intrinsic (node, &a, &b);
		else if (d.nodes[node].tag == T_IMG) { int w, h; img_size (node, 0, &w, &h); a = b = w; }
		else
		{
			// the children: block ones each on its own, inline runs measured whole
			measure_children (node, &a, &b);
		}
		int ed = edges_h (node, 0);
		if (s.width.u == U_PX && d.nodes[node].tag != T_IMG)
		{
			int w = (int) (s.width.v + 0.5f);
			if (s.display == D_TABLE || s.display == D_INLINE_TABLE || s.display == D_CELL) { if (w > a) a = w; b = w > a ? w : a; if (s.display == D_CELL) { a += ed; b += ed; } }
			else { a = b = w; a += ed; b += ed; }
			if (s.display == D_CELL) { a -= ed; b -= ed; }
		}
		if (s.maxWidth.u == U_PX && b > s.maxWidth.v) { b = (int) s.maxWidth.v; if (a > b) a = b; }
		if (!(s.width.u == U_PX && s.display != D_CELL && s.display != D_TABLE && s.display != D_INLINE_TABLE && d.nodes[node].tag != T_IMG)) { a += ed; b += ed; }
		else if (d.nodes[node].tag == T_IMG) { a += ed; b += ed; }
		int m = marg (node, 1, 0) + marg (node, 3, 0);
		if (s.display != D_CELL) { a += m; b += m; }
		if (b < a) b = a;
		minW[node] = *mn = a; maxW[node] = *mx = b;
	}
	void measure_children (int node, int *mn, int *mx)
	{
		int run = 0;
		bool ws = true;
		for (int c = d.nodes[node].first; c >= 0; c = d.nodes[c].next)
		{
			const Node &e = d.nodes[c];
			if (e.tag != T_TEXT && st (c).display == D_NONE) continue;
			if (e.tag != T_TEXT && block_like (c))
			{
				if (run > *mx) *mx = run; run = 0; ws = true;
				int a, b; intrinsic (c, &a, &b);
				if (a > *mn) *mn = a; if (b > *mx) *mx = b;
				continue;
			}
			measure_inline (c, mn, &run, mx, &ws);
		}
		if (run > *mx) *mx = run;
	}
	// an inline node's words: the widest (mn), the run's growing width (run; a <br> ends it into mx)
	void measure_inline (int node, int *mn, int *run, int *mx, bool *ws)
	{
		const Node &e = d.nodes[node];
		const Style &s = st (node);
		if (e.tag == T_TEXT)
		{
			int f = font_of (s);
			int sp = host.width (f, " ", 1);
			bool pre = s.whiteSpace == WS_PRE || s.whiteSpace == WS_PRE_WRAP;
			bool nowrap = s.whiteSpace == WS_NOWRAP || s.whiteSpace == WS_PRE;
			Buf t; transform (s, e.text, e.tn, t);
			const char *p = t.c (), *end = p + t.n;
			int word = 0;
			while (p < end)
			{
				if (*p == '\n' && (pre || s.whiteSpace == WS_PRE_LINE)) { *run += 0; if (*run > *mx) *mx = *run; *run = 0; if (!nowrap && word > *mn) *mn = word; word = 0; p++; *ws = true; continue; }
				if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
				{
					if (pre) { *run += sp; if (nowrap) word += sp; p++; continue; }
					if (!*ws) { *run += sp; if (nowrap) word += sp; }
					*ws = true;
					if (!nowrap) { if (word > *mn) *mn = word; word = 0; }
					p++; continue;
				}
				const char *w0 = p; while (p < end && !(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
				int ww = host.width (f, w0, (int) (p - w0));
				*run += ww; word += ww; *ws = false;
			}
			if (word > *mn) *mn = word;
			return;
		}
		if (e.tag == T_BR) { if (*run > *mx) *mx = *run; *run = 0; *ws = true; return; }
		int dp = s.display;
		if (dp == D_INLINE_BLOCK || dp == D_INLINE_TABLE || e.tag == T_IMG)
		{
			int a, b; intrinsic (node, &a, &b);
			if (a > *mn) *mn = a;
			*run += b; *ws = false;
			return;
		}
		int ed = edges_h (node, 0) + marg (node, 1, 0) + marg (node, 3, 0);
		*run += ed;
		for (int c = e.first; c >= 0; c = d.nodes[c].next) if (d.nodes[c].tag == T_TEXT || st (c).display != D_NONE) measure_inline (c, mn, run, mx, ws);
	}
	// shrink-to-fit: a float's, an inline-block's, a table's width at most cw
	int shrink_width (int node, int cw)
	{
		const Style &s = st (node);
		int ed = edges_h (node, cw);
		if (s.width.u != U_AUTO && s.width.u != U_NONE && d.nodes[node].tag != T_IMG) { int w = resolve (s.width, cw) + ed; return w; }
		if (d.nodes[node].tag == T_IMG) { int w, h; img_size (node, cw, &w, &h); return w + ed; }
		int a, b; intrinsic (node, &a, &b);
		int m = marg (node, 1, cw) + marg (node, 3, cw);
		a -= m; b -= m;
		int w = b < cw - m ? b : cw - m; if (w < a) w = a;
		return w;
	}
	void transform (const Style &s, const char *t, int n, Buf &o)
	{
		if (s.transform == TT_NONE) { o.add (t, n); return; }
		bool startW = true;
		for (int i = 0; i < n; i++)
		{
			unsigned char c = (unsigned char) t[i];
			bool up = s.transform == TT_UPPER || (s.transform == TT_CAPITALIZE && startW);
			if (up && c >= 'a' && c <= 'z') c -= 32;
			else if (s.transform == TT_LOWER && c >= 'A' && c <= 'Z') c += 32;
			else if (up && c == 0xC3 && i + 1 < n && (unsigned char) t[i + 1] >= 0xA0 && (unsigned char) t[i + 1] <= 0xBE && (unsigned char) t[i + 1] != 0xB7) { o.addc ((char) c); o.addc ((char) (t[i + 1] - 0x20)); i++; startW = false; continue; }
			startW = c == ' ' || c == '\n' || c == '\t' || c == '-';
			o.addc ((char) c);
		}
	}

	// ---- pictures ------------------------------------------------------------------------------------------------------------
	// its size: the CSS, the attributes, the picture's own (scaled to keep its shape), at most the container's width
	bool img_size (int node, int cw, int *w, int *h)
	{
		const Style &s = st (node);
		const char *src = d.attr (node, "src");
		int iw = 0, ih = 0;
		bool shown = src && *src && host.image (src, &iw, &ih);
		int W = -1, H = -1;
		if (s.width.u == U_PX) W = (int) (s.width.v + 0.5f); else if (s.width.u == U_PCT && cw > 0) W = (int) (s.width.v * cw / 100);
		if (s.height.u == U_PX) H = (int) (s.height.v + 0.5f);
		if (W < 0 && H < 0) { W = iw; H = ih; }
		else if (W < 0) W = ih > 0 ? iw * H / ih : (shown ? iw : H);
		else if (H < 0) H = iw > 0 ? ih * W / iw : (shown ? ih : 0);
		int mx = s.maxWidth.u == U_PX ? (int) s.maxWidth.v : s.maxWidth.u == U_PCT && cw > 0 ? (int) (s.maxWidth.v * cw / 100) : -1;
		if (mx >= 0 && W > mx) { if (W > 0) H = H * mx / W; W = mx; }
		if (cw > 0 && W > cw && s.maxWidth.u == U_PCT) { H = W > 0 ? H * cw / W : H; W = cw; }
		*w = W < 0 ? 0 : W; *h = H < 0 ? 0 : H;
		return shown;
	}

	// ---- the inline flow -------------------------------------------------------------------------------------------------------
	enum { K_WORD, K_SPACE, K_BREAK, K_ATOM, K_OPEN, K_CLOSE };
	struct Bit
	{
		unsigned char kind; int node;			// (K_WORD / K_SPACE: the text's node -- its parent's style)
		const char *s; int n; int w;			// the word, its width (an open / close: its edge's width)
		int font, asc, desc, lh;
		int atomFirst, atomLast, atomH, atomBase;	// an atom's items (laid out at 0, 0), its height, its baseline
	};
	Vec<Bit> bits;
	// the bits of an inline node (recursively)
	void collect (int node, bool *ws, int cw)
	{
		const Node &e = d.nodes[node];
		const Style &s = st (node);
		if (e.tag == T_TEXT)
		{
			int f = font_of (s); int asc, desc; host.metrics (f, &asc, &desc);
			int lh = line_height (s, asc, desc);
			bool pre = s.whiteSpace == WS_PRE || s.whiteSpace == WS_PRE_WRAP;
			bool keepNl = pre || s.whiteSpace == WS_PRE_LINE;
			bool nowrap = s.whiteSpace == WS_NOWRAP || s.whiteSpace == WS_PRE;
			Buf t; transform (s, e.text, e.tn, t);
			char *tt = text.alloc (t.n + 1); if (!tt) return;
			int tn = 0;
			if (pre)
			{	// the tabs to the next multiple of 8 (drawn as spaces)
				int tabs = 0; for (int k = 0; k < t.n; k++) if (t.p[k] == '\t') tabs++;
				if (tabs) { tt = text.alloc (t.n + tabs * 8 + 1); if (!tt) return; }
				int col = 0;
				for (int k = 0; k < t.n; k++)
				{
					char ch = t.p[k];
					if (ch == '\t') { do tt[tn++] = ' '; while (++col % 8); continue; }
					if (ch == '\r') continue;
					tt[tn++] = ch;
					if (ch == '\n') col = 0; else if ((ch & 0xC0) != 0x80) col++;
				}
				tt[tn] = 0;
			}
			else
			{	// the white space collapsed here (a run of it: one ' ', a newline kept for pre-line), so that a
				// line's words stay one text run
				bool sp = false;
				for (int k = 0; k < t.n; k++)
				{
					char ch = t.p[k];
					if (ch == '\r') continue;
					if (ch == '\n' && keepNl) { tt[tn++] = '\n'; sp = false; continue; }
					if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\f') { if (!sp) tt[tn++] = ' '; sp = true; continue; }
					tt[tn++] = ch; sp = false;
				}
				tt[tn] = 0;
			}
			const char *p = tt, *end = tt + tn;
			int sp = host.width (f, " ", 1);
			while (p < end)
			{
				if (keepNl && *p == '\n') { Bit &b = bits.push (); b.kind = K_BREAK; b.node = node; b.font = f; b.asc = asc; b.desc = desc; b.lh = lh; p++; *ws = true; continue; }
				if (*p == '\r') { p++; continue; }
				if (*p == ' ' || *p == '\t' || *p == '\n')
				{
					if (pre)
					{	// each space kept (a tab: to the next 8)
						const char *a = p; while (p < end && (*p == ' ' || *p == '\t')) p++;
						int n = (int) (p - a);
						Bit &b = bits.push (); b.kind = nowrap ? K_WORD : K_SPACE; b.node = node; b.s = a; b.n = (int) (p - a); b.w = sp * n; b.font = f; b.asc = asc; b.desc = desc; b.lh = lh;
						if (nowrap) b.kind = K_WORD;
						continue;
					}
					const char *a = p; p++;
					if (!*ws) { Bit &b = bits.push (); b.kind = nowrap ? K_WORD : K_SPACE; b.node = node; b.s = a; b.n = 1; b.w = sp; b.font = f; b.asc = asc; b.desc = desc; b.lh = lh; *ws = true; }
					continue;
				}
				const char *a = p; while (p < end && !(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
				Bit &b = bits.push (); b.kind = K_WORD; b.node = node; b.s = a; b.n = (int) (p - a); b.w = host.width (f, a, b.n); b.font = f; b.asc = asc; b.desc = desc; b.lh = lh;
				*ws = false;
			}
			return;
		}
		if (s.display == D_NONE) return;
		if (e.tag == T_BR) { int f = font_of (s); int asc, desc; host.metrics (f, &asc, &desc); Bit &b = bits.push (); b.kind = K_BREAK; b.node = node; b.font = f; b.asc = asc; b.desc = desc; b.lh = line_height (s, asc, desc); *ws = true; return; }
		if (e.tag == T_WBR) { Bit &b = bits.push (); b.kind = K_SPACE; b.node = node; b.s = ""; b.n = 0; b.w = 0; b.font = -1; return; }
		if (e.tag == T_IMG || s.display == D_INLINE_BLOCK || s.display == D_INLINE_TABLE)
		{
			Bit b; memset (&b, 0, sizeof b);
			b.kind = K_ATOM; b.node = node; b.font = -1;
			b.atomFirst = items.n;
			if (e.tag == T_IMG)
			{
				int w, h; bool shown = img_size (node, cw, &w, &h);
				const char *alt = d.attr (node, "alt");
				int ml = marg (node, 3, cw), mr = marg (node, 1, cw), mt = marg (node, 0, cw), mb = marg (node, 2, cw);
				int ed = edges_h (node, cw), ev = edges_v (node, cw);
				if (!shown && (w <= 1 || h <= 1))
				{	// blocked, no size (or a 1 px tracker): its alt text, else nothing
					if (alt && *alt && !(w == 1 || h == 1)) { Buf a; a.add (alt); Style &as = S.s[node]; (void) as; collect_alt (node, alt, ws); }
					return;
				}
				int bg = rect (0, 0, 0, 0, 0, node, s.link);
				Item &it = items.push (); it.kind = shown ? I_IMAGE : I_BLOCKED; it.x = ml + bord (node, 3) + pad (node, 3, cw); it.y = mt + bord (node, 0) + pad (node, 0, cw); it.w = w; it.h = h;
				it.s = d.attr (node, "src"); it.n = it.s ? (int) strlen (it.s) : 0; it.link = s.link; it.node = node; it.font = -1; it.color = 0xFFE8EAED; it.color2 = 0xFF9AA0A6;
				if (!shown && alt) { it.s = alt; it.n = (int) strlen (alt); }
				box_paint (node, bg, ml, mt, w + ed, h + ev);
				b.w = w + ed + ml + mr; b.atomH = h + ev + mt + mb; b.atomBase = b.atomH;
			}
			else
			{
				int w = shrink_width (node, cw);
				int h = layout_box (node, 0, 0, w, true);
				b.w = w + marg (node, 1, cw) + marg (node, 3, cw); b.atomH = h; b.atomBase = h;
				// its last line's baseline (an inline-block's), else its bottom
				if (lastBaseline >= 0 && s.display == D_INLINE_BLOCK) b.atomBase = lastBaseline;
			}
			b.atomLast = items.n;
			bits.push (b);
			*ws = false;
			return;
		}
		// an inline element: its edges, its children
		{ Bit &b = bits.push (); b.kind = K_OPEN; b.node = node; b.w = marg (node, 3, cw) + bord (node, 3) + pad (node, 3, cw); b.font = -1; }
		for (int c = e.first; c >= 0; c = d.nodes[c].next) collect (c, ws, cw);
		{ Bit &b = bits.push (); b.kind = K_CLOSE; b.node = node; b.w = marg (node, 1, cw) + bord (node, 1) + pad (node, 1, cw); b.font = -1; }
	}
	void collect_alt (int node, const char *alt, bool *ws)
	{
		const Style &s = st (node);
		int f = host.font (s.family, s.fontSize, s.bold, true); int asc, desc; host.metrics (f, &asc, &desc);
		int n = (int) strlen (alt); char *t = text.alloc (n + 1); if (!t) return; memcpy (t, alt, n + 1);
		const char *p = t, *end = t + n;
		while (p < end)
		{
			if (*p == ' ') { if (!*ws) { Bit &b = bits.push (); b.kind = K_SPACE; b.node = -node - 2; b.s = " "; b.n = 1; b.w = host.width (f, " ", 1); b.font = f; b.asc = asc; b.desc = desc; b.lh = asc + desc; *ws = true; } p++; continue; }
			const char *a = p; while (p < end && *p != ' ') p++;
			Bit &b = bits.push (); b.kind = K_WORD; b.node = -node - 2; b.s = a; b.n = (int) (p - a); b.w = host.width (f, a, b.n); b.font = f; b.asc = asc; b.desc = desc; b.lh = asc + desc; *ws = false;
		}
	}
	int lastBaseline;		// the last line's baseline of what was just laid out (an inline-block's)

	// the style a bit draws with (an alt text: its picture's, greyed)
	const Style &bit_style (const Bit &b) const { int n = b.node < -1 ? -b.node - 2 : b.node; return st (n); }

	bool wraps (const Bit &b) const { int ws = bit_style (b).whiteSpace; return ws != WS_NOWRAP && ws != WS_PRE; }
	bool keeps_spaces (const Bit &b) const { int ws = bit_style (b).whiteSpace; return ws == WS_PRE || ws == WS_PRE_WRAP; }
	// lines from bits[from .. to) (no K_BREAK inside), laid out in [x0, x1) from y; the block's style gives the strut and
	// the alignment -> the bottom. Long words are cut (bits grows: the caller sees bits.n change).
	int flow_lines (int from, int to, int x0, int x1, int y, Bfc &bfc, const Style &blk, int cw)
	{
		int f = font_of (blk); int sasc, sdesc; host.metrics (f, &sasc, &sdesc);
		int slh = line_height (blk, sasc, sdesc);
		int indent = resolve (blk.textIndent, cw);
		int i = from;
		bool firstLine = true;
		int open[32]; int nopen = 0;			// the inline boxes open across lines
		while (i < to)
		{
			while (i < to && bits[i].kind == K_SPACE && !keeps_spaces (bits[i])) i++;
			if (i >= to) break;
			int l, r; room (bfc, y, slh, x0, x1, &l, &r);
			if (firstLine) l += indent;
			int avail = r - l;
			// beside floats and the first thing does not fit: lower, under the next float's bottom
			if ((l > x0 || r < x1) && bfc.floats.n)
			{
				int fw = 0; for (int k = i; k < to && (bits[k].kind == K_OPEN || bits[k].kind == K_CLOSE || k == i); k++) fw += bits[k].w;
				for (int k = i; k < to; k++) if (bits[k].kind == K_WORD || bits[k].kind == K_ATOM) { fw = bits[k].w; break; }
				if (fw > avail)
				{
					int nb = 1 << 30; for (int k = 0; k < bfc.floats.n; k++) { int bt = bfc.floats[k].y + bfc.floats[k].h; if (bt > y && bt < nb) nb = bt; }
					if (nb != 1 << 30) { y = nb; continue; }
				}
			}
			// what fits: up to the last break opportunity before the overflow
			int j = i, w = 0, lastBreak = -1, end = -1;
			bool content = false;
			while (j < to)
			{
				const Bit &b = bits[j];
				if (b.kind == K_SPACE && wraps (b)) lastBreak = j;
				if ((b.kind == K_WORD || b.kind == K_ATOM) && w + b.w > avail && wraps (b))
				{
					if (lastBreak > i || (lastBreak == i && content)) { end = lastBreak; break; }
					if (!content)
					{
						if (b.kind == K_WORD && b.w > avail && avail > 0) { int n0 = bits.n; split_word (j, avail - w); to += bits.n - n0; }
						end = j + 1; break;
					}
				}
				if (b.kind == K_WORD || b.kind == K_ATOM) content = true;
				w += b.w; j++;
			}
			if (end < 0) end = j;
			// the trailing spaces not counted
			int last = end; while (last > i && bits[last - 1].kind == K_SPACE && !keeps_spaces (bits[last - 1])) last--;
			int lw = 0; for (int k = i; k < last; k++) lw += bits[k].w;
			// the line's height: the strut, the bits' boxes around the baseline
			int above = sasc + (slh - sasc - sdesc) / 2, below = slh - above;
			for (int k = i; k < last; k++)
			{
				const Bit &b = bits[k];
				if ((b.kind == K_WORD || b.kind == K_SPACE) && b.font >= 0)
				{
					int ha = b.asc + (b.lh - b.asc - b.desc) / 2, hb = b.lh - ha;
					int off = b.node >= 0 ? vshift_node (b.node, blk) : 0;
					if (ha - off > above) above = ha - off;
					if (hb + off > below) below = hb + off;
				}
				else if (b.kind == K_ATOM)
				{
					const Style &as = st (b.node);
					if (as.vAlign == VA_MIDDLE) { int half = b.atomH / 2, mid = sasc * 4 / 10; if (half + mid > above) above = half + mid; if (b.atomH - half - mid > below) below = b.atomH - half - mid; }
					else if (as.vAlign == VA_TOP || as.vAlign == VA_TEXT_TOP) { if (b.atomH - above > below) below = b.atomH - above; }
					else if (as.vAlign == VA_BOTTOM || as.vAlign == VA_TEXT_BOTTOM) { if (b.atomH - below > above) above = b.atomH - below; }
					else { if (b.atomBase > above) above = b.atomBase; if (b.atomH - b.atomBase > below) below = b.atomH - b.atomBase; }
				}
			}
			int lh = above + below;
			int base = y + above;
			// the alignment
			int ax = l, extra = avail - lw, spaces = 0;
			bool lastLine = end >= to;
			if (blk.textAlign == TA_CENTER && extra > 0) ax += extra / 2;
			else if (blk.textAlign == TA_RIGHT && extra > 0) ax += extra;
			else if (blk.textAlign == TA_JUSTIFY && extra > 0 && !lastLine) for (int k = i; k < last; k++) if (bits[k].kind == K_SPACE) spaces++;
			if (marker.on) emit_marker (base);
			// the inline boxes open from the line before: their fragments start here
			int x = ax;
			int fragStart[32], fragItem[32];
			for (int k = 0; k < nopen; k++) { fragStart[k] = x; fragItem[k] = frag_reserve (open[k]); }
			int sp = 0;
			for (int k = i; k < end; k++)
			{
				Bit &b = bits[k];
				switch (b.kind)
				{
				case K_OPEN:
					if (nopen < 32) { open[nopen] = b.node; fragStart[nopen] = x + marg (b.node, 3, cw); fragItem[nopen] = frag_reserve (b.node); nopen++; }
					x += b.w; break;
				case K_CLOSE:
				{
					int o = nopen - 1; while (o >= 0 && open[o] != b.node) o--;
					x += b.w;
					if (o >= 0)
					{
						frag_fill (fragItem[o], open[o], fragStart[o], x - marg (b.node, 1, cw), base, cw);
						for (int q = o; q < nopen - 1; q++) { open[q] = open[q + 1]; fragStart[q] = fragStart[q + 1]; fragItem[q] = fragItem[q + 1]; }
						nopen--;
					}
					break;
				}
				case K_SPACE:
					if (k >= last) break;
					if (spaces) { int add = extra / spaces + (sp < extra % spaces ? 1 : 0); sp++; emit_text (b, x, base, blk, b.w + add, false); x += b.w + add; }
					else x = emit_text (b, x, base, blk, b.w, true);
					break;
				case K_WORD: x = emit_text (b, x, base, blk, b.w, !spaces); break;
				case K_ATOM:
				{
					const Style &as = st (b.node);
					int ty;
					if (as.vAlign == VA_MIDDLE) ty = base - sasc * 4 / 10 - b.atomH / 2;
					else if (as.vAlign == VA_TOP || as.vAlign == VA_TEXT_TOP) ty = y;
					else if (as.vAlign == VA_BOTTOM || as.vAlign == VA_TEXT_BOTTOM) ty = y + lh - b.atomH;
					else ty = base - b.atomBase;
					shift (b.atomFirst, b.atomLast, x + (d.nodes[b.node].tag == T_IMG ? 0 : marg (b.node, 3, cw)), ty);
					x += b.w; break;
				}
				default: break;
				}
			}
			for (int k = 0; k < nopen; k++) frag_fill (fragItem[k], open[k], fragStart[k], x, base, cw);
			lastBaseline = base;
			y += lh;
			firstLine = false;
			i = end;
		}
		return y;
	}
	// an empty line (only a <br>): its height counted -- done by flow_lines's caller looking at the bits
	int vshift (const Style &s, const Style &blk) const
	{
		if (s.vAlign == VA_SUB) return blk.fontSize / 4;
		if (s.vAlign == VA_SUPER) return -blk.fontSize * 4 / 10;
		return 0;
	}
	// a text's shift: its inline ancestors' sub / super, summed
	int vshift_node (int node, const Style &blk) const
	{
		int k = 0;
		for (int p = node >= 0 ? d.nodes[node].parent : -1; p >= 0; p = d.nodes[p].parent)
		{
			const Style &s = st (p);
			if (s.display != D_INLINE) break;
			if (s.vAlign == VA_SUB) k += s.fontSize / 3; else if (s.vAlign == VA_SUPER) k -= blk.fontSize * 4 / 10;
		}
		return k;
	}
	int frag_reserve (int node)
	{
		const Style &s = st (node);
		bool any = (s.bg >> 24) != 0; for (int k = 0; k < 4; k++) if (s.borderW[k]) any = true;
		if (!any) return -1;
		int r = rect (0, 0, 0, 0, s.bg, node, s.link);
		if (!(s.bg >> 24)) items[r].w = 0;
		return r;
	}
	// an inline box's fragment on a line: from x0 to x1 (its border box), around the baseline
	void frag_fill (int item, int node, int x0, int x1, int base, int cw)
	{
		if (item < 0) return;
		const Style &s = st (node);
		int f = font_of (s); int asc, desc; host.metrics (f, &asc, &desc);
		int top = base - asc - pad (node, 0, cw) - bord (node, 0), bottom = base + desc + pad (node, 2, cw) + bord (node, 2);
		Item &bgI = items[item];
		bgI.x = x0; bgI.y = top; bgI.w = (s.bg >> 24) ? x1 - x0 : 0; bgI.h = bottom - top;
		// its borders (top, bottom; the sides at its ends)
		for (int side = 0; side < 4; side++)
		{
			int bw = s.borderW[side]; if (!bw) continue;
			unsigned c = s.borderC[side];
			if (side == 0) rect (x0, top, x1 - x0, bw, c, node, s.link);
			else if (side == 2) rect (x0, bottom - bw, x1 - x0, bw, c, node, s.link);
			else if (side == 3) rect (x0, top, bw, bottom - top, c, node, s.link);
			else rect (x1 - bw, top, bw, bottom - top, c, node, s.link);
		}
	}
	void split_word (int j, int room)
	{
		// the word cut where it fits (at least one character): the rest becomes a new word after it
		Bit b = bits[j];
		int k = 0, w = 0;
		while (k < b.n)
		{
			int l = 1; unsigned char c = (unsigned char) b.s[k]; if (c >= 0xF0) l = 4; else if (c >= 0xE0) l = 3; else if (c >= 0xC0) l = 2;
			int nw = host.width (b.font, b.s, k + l);
			if (nw > room && k > 0) break;
			k += l; w = nw;
		}
		if (k >= b.n) return;
		Bit rest = b; rest.s = b.s + k; rest.n = b.n - k; rest.w = host.width (b.font, rest.s, rest.n);
		bits[j].n = k; bits[j].w = w;
		// inserted after j, with a break before it
		bits.push (); for (int q = bits.n - 1; q > j + 1; q--) bits[q] = bits[q - 1];
		bits[j + 1] = rest;
	}
	// a word / space drawn at x -> where the next one goes (a run measured whole: its kerning, no rounding piled up)
	int emit_text (const Bit &b, int x, int base, const Style &blk, int w, bool join)
	{
		if (b.font < 0 || b.n == 0) return x + w;
		const Style &s = bit_style (b);
		if (!s.visible) return x + w;
		bool alt = b.node < -1;
		int y = base + (b.node >= 0 ? vshift_node (b.node, blk) : 0);
		// joined to the previous text run when it continues it (the same node, right after)
		if (items.n && join)
		{
			Item &p = items[items.n - 1];
			if (p.kind == I_TEXT && p.node == b.node && p.y == y && p.x + p.w == x && p.s + p.n == b.s && p.font == b.font)
			{ p.n += b.n; int nw = host.width (b.font, p.s, p.n); p.w = nw; return p.x + nw; }
		}
		Item &it = items.push (); it.kind = I_TEXT; it.x = x; it.y = y; it.w = w; it.h = b.lh; it.font = b.font;
		it.s = b.s; it.n = b.n; it.color = alt ? 0xFF80868B : s.color; it.color2 = s.decoColor; it.deco = alt ? 0 : s.decoration; it.link = s.link; it.node = b.node;
		// the decoration of the ancestors too (an <a> inside a <u>)
		for (int p = b.node >= 0 ? d.nodes[b.node].parent : -1; p >= 0 && !alt; p = d.nodes[p].parent)
		{
			if (st (p).display != D_INLINE && st (p).display != D_INLINE_BLOCK) break;
			if (st (p).decoration && !(it.deco & st (p).decoration)) { if (!it.deco) it.color2 = s.color; it.deco |= st (p).decoration; }
		}
		if (!it.color2) it.color2 = s.color;
		return x + w;
	}
	void emit_marker (int base)
	{
		marker.on = false;
		const Style &s = st (marker.node);
		if (s.listStyle == LS_NONE) return;
		char t[24]; t[0] = 0;
		int f = font_of (s);
		if (s.listStyle == LS_DISC) scpy (t, "\xE2\x80\xA2", sizeof t);
		else if (s.listStyle == LS_CIRCLE) scpy (t, "\xE2\x97\xA6", sizeof t);
		else if (s.listStyle == LS_SQUARE) scpy (t, "\xE2\x96\xAA", sizeof t);
		else
		{
			int n = list_number (marker.node);
			if (s.listStyle == LS_DECIMAL) snprintf (t, sizeof t, "%d.", n);
			else if (s.listStyle == LS_LOWER_ALPHA || s.listStyle == LS_UPPER_ALPHA) snprintf (t, sizeof t, "%c.", (char) ((s.listStyle == LS_LOWER_ALPHA ? 'a' : 'A') + (n - 1) % 26));
			else { roman (n, t, s.listStyle == LS_UPPER_ROMAN); strcat (t, "."); }
		}
		int n = (int) strlen (t);
		char *c = text.alloc (n + 1); if (!c) return; memcpy (c, t, n + 1);
		int w = host.width (f, c, n);
		Item &it = items.push (); it.kind = I_TEXT; it.x = marker.x - w - s.fontSize / 2; it.y = base; it.w = w; it.font = f; it.s = c; it.n = n; it.color = s.color; it.link = -1; it.node = marker.node;
	}
	int list_number (int li)
	{
		int par = d.nodes[li].parent;
		int n = 1;
		const char *st0 = par >= 0 ? d.attr (par, "start") : 0; if (st0) n = atoi (st0);
		for (int c = par >= 0 ? d.nodes[par].first : -1; c >= 0 && c != li; c = d.nodes[c].next)
			if (d.nodes[c].tag != T_TEXT && st (c).display == D_LIST_ITEM) { const char *v = d.attr (c, "value"); n = v ? atoi (v) + 1 : n + 1; }
		const char *v = d.attr (li, "value"); if (v) n = atoi (v);
		return n;
	}
	static void roman (int n, char *o, bool up)
	{
		static const int V[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
		static const char *const S[] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
		o[0] = 0; if (n <= 0 || n > 3999) { snprintf (o, 12, "%d", n); return; }
		for (int i = 0; i < 13; i++) while (n >= V[i]) { strcat (o, S[i]); n -= V[i]; }
		if (up) for (char *p = o; *p; p++) *p = (char) (*p - 32);
	}

	// ---- the block flow -----------------------------------------------------------------------------------------------------------
	// node's children laid out in its content box (x, y, w) -> the content's bottom
	int flow (int node, int x, int y, int w, Bfc &bfc)
	{
		const Style &ps = st (node);
		int pending = 0; bool havePending = false;			// the last block's bottom margin (collapsing)
		bool startedAny = false;
		for (int c = d.nodes[node].first; c >= 0; )
		{
			const Node &e = d.nodes[c];
			if (e.tag != T_TEXT && st (c).display == D_NONE) { c = e.next; continue; }
			if (e.tag != T_TEXT && block_like (c))
			{
				const Style &cs = st (c);
				if (cs.floatSide != FL_NONE)
				{
					place_float (c, x, x + w, y + (havePending ? pending : 0), bfc);
					c = e.next; continue;
				}
				if (cs.clearSide) { int b = floats_bottom (bfc, cs.clearSide); if (b > y + (havePending ? pending : 0)) { y = b; havePending = false; pending = 0; } }
				int mt = marg (c, 0, w);
				int top = havePending ? collapse (pending, mt) : (startedAny || !collapses_top (node) ? mt : mt);
				y += top;
				int h = layout_block (c, x, y, w, bfc);
				y += h;
				pending = marg (c, 2, w); havePending = true;
				startedAny = true;
				c = e.next;
				continue;
			}
			// an inline run: up to the next block
			if (havePending) { y += pending; pending = 0; havePending = false; }
			bits.clear ();
			bool ws = true;
			while (c >= 0 && (d.nodes[c].tag == T_TEXT || st (c).display == D_NONE || !block_like (c))) { collect (c, &ws, w); c = d.nodes[c].next; }
			// nothing but spaces: no line
			bool any = false; for (int k = 0; k < bits.n; k++) if (bits[k].kind != K_SPACE && bits[k].kind != K_OPEN && bits[k].kind != K_CLOSE) any = true; else if ((bits[k].kind == K_OPEN || bits[k].kind == K_CLOSE) && bits[k].w) any = true;
			if (!any) continue;
			int to = bits.n;
			int y0 = y;
			y = flow_lines_br (0, to, x, x + w, y, bfc, ps, w);
			if (y > y0) startedAny = true;
		}
		if (havePending) y += pending;
		return y;
	}
	// the lines of a run with its <br>s: an empty line for each <br> that ends nothing
	int flow_lines_br (int from, int to, int x0, int x1, int y, Bfc &bfc, const Style &blk, int cw)
	{
		int i = from;
		while (i < to)
		{
			int j = i; while (j < to && bits[j].kind != K_BREAK) j++;
			bool content = false; for (int k = i; k < j; k++) if (bits[k].kind == K_WORD || bits[k].kind == K_ATOM || (bits[k].kind == K_SPACE && (bit_style (bits[k]).whiteSpace == WS_PRE || bit_style (bits[k]).whiteSpace == WS_PRE_WRAP))) content = true;
			if (content) { int n0 = bits.n; y = flow_lines (i, j, x0, x1, y, bfc, blk, cw); int grew = bits.n - n0; to += grew; j += grew; }
			else if (j < to)
			{	// a <br> on its own line: the line's height (the <br>'s font)
				const Bit &b = bits[j];
				int lh = b.lh > 0 ? b.lh : 16;
				if (marker.on) emit_marker (y + b.asc);
				y += lh;
			}
			else
			{	// only spaces / empty inline boxes: their backgrounds (an empty <a> button: nothing)
			}
			i = j + 1;
		}
		return y;
	}
	static int collapse (int a, int b) { if (a >= 0 && b >= 0) return a > b ? a : b; if (a < 0 && b < 0) return a < b ? a : b; return a + b; }
	bool collapses_top (int node) const { (void) node; return false; }

	// a block-level box at (x, y): its margin's top already added -> its border box's height
	int layout_block (int node, int x, int y, int cw, Bfc &bfc)
	{
		const Style &s = st (node);
		if (s.display == D_TABLE || s.display == D_INLINE_TABLE) return table (node, x, y, cw, bfc);
		int ml = marg (node, 3, cw), mr = marg (node, 1, cw);
		int ed = edges_h (node, cw);
		int w;
		bool autoW = s.width.u == U_AUTO || s.width.u == U_NONE;
		if (d.nodes[node].tag == T_IMG) { int iw, ih; img_size (node, cw - ml - mr - ed, &iw, &ih); w = iw; autoW = false; }
		else if (autoW) w = cw - ml - mr - ed;
		else if (d.nodes[node].tag != T_IMG) w = resolve (s.width, cw);
		if (s.maxWidth.u != U_NONE && d.nodes[node].tag != T_IMG) { int m = resolve (s.maxWidth, cw); if (w > m) w = m; }
		if (s.minWidth.u != U_NONE) { int m = resolve (s.minWidth, cw); if (w < m) w = m; }
		if (w < 0) w = 0;
		// margin: auto (or a centred parent's align): centred
		int bx = x + ml;
		int used = w + ed + ml + mr;
		if (used < cw)
		{
			bool la = s.margin[3].u == U_AUTO, ra = s.margin[1].u == U_AUTO;
			int par = d.nodes[node].parent;
			bool centredParent = par >= 0 && !autoW && st (par).textAlign == TA_CENTER && centring_attr (par);
			if ((la && ra) || centredParent) bx = x + ml + (cw - used) / 2;
			else if (la) bx = x + cw - mr - w - ed;
		}
		return layout_at (node, bx, y, w, bfc);
	}
	// an element with align="center" / <center>: its blocks centred (HTML's habit, not CSS's)
	bool centring_attr (int node) const
	{
		for (int p = node; p >= 0; p = d.nodes[p].parent)
		{
			const Node &e = d.nodes[p];
			if (e.tag == T_CENTER) return true;
			const char *a = d.attr (p, "align"); if (a && (ieq (a, "center") || ieq (a, "middle"))) return true;
			if (st (p).textAlign != TA_CENTER) return false;
			if (st (p).display != D_INLINE && e.tag != T_TD && e.tag != T_TH && e.tag != T_DIV && e.tag != T_P) return false;
		}
		return false;
	}
	// a box laid out at (bx, y), its content w wide -> its border box's height (the items from the box's background)
	int layout_at (int node, int bx, int y, int w, Bfc &bfc)
	{
		const Style &s = st (node);
		if (d.nodes[node].tag == T_IMG) return picture_block (node, bx, y, w);
		int bg = rect (0, 0, 0, 0, 0, node, s.link);
		int cx = bx + bord (node, 3) + pad (node, 3, w), cy = y + bord (node, 0) + pad (node, 0, w);
		bool li = s.display == D_LIST_ITEM;
		Marker keep = marker;
		if (li) { marker.on = true; marker.x = cx; marker.node = node; }
		int end;
		if (d.nodes[node].tag == T_HR) end = cy;
		else end = flow (node, cx, cy, w, bfc);
		if (li && marker.on) { int f = font_of (s); int asc, desc; host.metrics (f, &asc, &desc); emit_marker (cy + asc); if (end < cy + asc + desc) end = cy + asc + desc; }
		if (li) marker = keep;
		int ch = end - cy;
		if (s.height.u == U_PX && ch < (int) s.height.v) ch = (int) s.height.v;
		if (s.minHeight.u == U_PX && ch < (int) s.minHeight.v) ch = (int) s.minHeight.v;
		int h = ch + edges_v (node, w);
		box_paint (node, bg, bx, y, w + edges_h (node, w), h);
		return h;
	}
	// a picture laid out as a block at (bx, y): its box -> its height (w: its content width, the picture's own)
	int picture_block (int node, int bx, int y, int w)
	{
		const Style &s = st (node);
		int iw, ih; bool shown = img_size (node, w, &iw, &ih);
		if (!shown && (iw <= 1 || ih <= 1)) return 0;
		int bg = rect (0, 0, 0, 0, 0, node, s.link);
		Item &it = items.push (); it.kind = shown ? I_IMAGE : I_BLOCKED; it.x = bx + bord (node, 3) + pad (node, 3, w); it.y = y + bord (node, 0) + pad (node, 0, w); it.w = iw; it.h = ih;
		it.s = d.attr (node, "src"); it.n = it.s ? (int) strlen (it.s) : 0; it.link = s.link; it.node = node; it.font = -1; it.color = 0xFFE8EAED; it.color2 = 0xFF9AA0A6;
		int h = ih + edges_v (node, w);
		box_paint (node, bg, bx, y, iw + edges_h (node, w), h);
		return h;
	}
	// a box laid out as the root of its own floats (a float, an inline-block, a cell): w its border box's width
	int layout_box (int node, int x, int y, int bw, bool own)
	{
		(void) own;
		const Style &s = st (node);
		if (s.display == D_TABLE || s.display == D_INLINE_TABLE) { Bfc b; return table (node, x, y, bw, b, true); }
		Bfc b;
		int w = bw - edges_h (node, bw); if (w < 0) w = 0;
		int h = layout_at (node, x, y, w, b);
		int fb = floats_bottom (b, 3) - y;
		if (fb > h) { h = fb; }
		return h;
	}

	// ---- tables ---------------------------------------------------------------------------------------------------------------
	struct Cell { int node; int row, col, cs, rs; int mn, mx; Len w; int first, last; int h; int y; int bg; };
	struct TRow { int node; int y, h; int bg; Len hh; };
	struct Grid
	{
		Vec<Cell> cells; Vec<TRow> rows; int ncols;
		Vec<int> colMin, colMax; Vec<int> colFixed, colPct, colW;
		int caption;
	};
	void table_rows (int node, Grid &g)
	{
		g.ncols = 0; g.caption = -1;
		// the row groups in order: thead, the bodies and rows, tfoot
		int groups[3][64]; int ng[3] = { 0, 0, 0 };
		for (int c = d.nodes[node].first; c >= 0; c = d.nodes[c].next)
		{
			if (d.nodes[c].tag == T_TEXT) continue;
			int dp = st (c).display;
			if (dp == D_NONE) continue;
			if (dp == D_CAPTION) { if (g.caption < 0) g.caption = c; continue; }
			int k = dp == D_HEADER_GROUP ? 0 : dp == D_FOOTER_GROUP ? 2 : 1;
			if (dp == D_ROW || dp == D_ROW_GROUP || dp == D_HEADER_GROUP || dp == D_FOOTER_GROUP) { if (ng[k] < 64) groups[k][ng[k]++] = c; }
		}
		// the occupied slots (rowspans)
		Vec<int> busy;					// per column: the last row it is taken by
		for (int k = 0; k < 3; k++)
			for (int gi = 0; gi < ng[k]; gi++)
			{
				int gnode = groups[k][gi];
				bool isRow = st (gnode).display == D_ROW;
				for (int r = isRow ? gnode : d.nodes[gnode].first; r >= 0; r = isRow ? -1 : d.nodes[r].next)
				{
					if (d.nodes[r].tag == T_TEXT || st (r).display != D_ROW) continue;
					TRow &R = g.rows.push (); R.node = r; R.hh = st (r).height;
					int ri = g.rows.n - 1;
					int col = 0;
					bool odd = false;
					for (int c = d.nodes[r].first; c >= 0; c = d.nodes[c].next) if (d.nodes[c].tag != T_TEXT && st (c).display != D_CELL && st (c).display != D_NONE) odd = true;
					if (odd)
					{
						Cell &C = g.cells.push (); C.node = r; C.row = ri; C.col = 0; C.cs = -1; C.rs = 1; C.w = autol ();
						continue;
					}
					for (int c = d.nodes[r].first; c >= 0; c = d.nodes[c].next)
					{
						if (d.nodes[c].tag == T_TEXT || st (c).display != D_CELL) continue;
						while (col < busy.n && busy[col] >= ri) col++;
						const char *a = d.attr (c, "colspan"); int cs = a ? atoi (a) : 1; if (cs < 1) cs = 1; if (cs > 50) cs = 50;
						a = d.attr (c, "rowspan"); int rs = a ? atoi (a) : 1; if (rs < 1) rs = 1; if (rs > 100) rs = 100;
						Cell &C = g.cells.push (); C.node = c; C.row = ri; C.col = col; C.cs = cs; C.rs = rs; C.w = st (c).width;
						while (busy.n < col + cs) busy.push (-1);
						for (int q = col; q < col + cs; q++) busy[q] = ri + rs - 1;
						col += cs;
						if (col > g.ncols) g.ncols = col;
					}
				}
			}
		// rowspans past the last row: cut; the rows made one cell: the whole width
		if (g.ncols == 0) for (int i = 0; i < g.cells.n; i++) if (g.cells[i].cs < 0) g.ncols = 1;
		for (int i = 0; i < g.cells.n; i++) { if (g.cells[i].cs < 0) g.cells[i].cs = g.ncols; if (g.cells[i].row + g.cells[i].rs > g.rows.n) g.cells[i].rs = g.rows.n - g.cells[i].row; }
	}
	int spacing (int node) const { return st (node).collapse ? 0 : st (node).borderSpacing; }
	void columns (int node, Grid &g)
	{
		int n = g.ncols;
		for (int i = 0; i < n; i++) { g.colMin.push (0); g.colMax.push (0); g.colFixed.push (-1); g.colPct.push (-1); g.colW.push (0); }
		// <col width>
		int ci = 0;
		for (int c = d.nodes[node].first; c >= 0; c = d.nodes[c].next)
		{
			if (d.nodes[c].tag == T_COL && ci < n) { const char *a = d.attr (c, "span"); int sp = a ? atoi (a) : 1; for (int k = 0; k < sp && ci < n; k++, ci++) { const Len &l = st (c).width; if (l.u == U_PX) g.colFixed[ci] = (int) l.v; else if (l.u == U_PCT) g.colPct[ci] = (int) l.v; } }
			else if (d.nodes[c].tag == T_COLGROUP) for (int cc = d.nodes[c].first; cc >= 0 && ci < n; cc = d.nodes[cc].next) if (d.nodes[cc].tag == T_COL) { const Len &l = st (cc).width; if (l.u == U_PX) g.colFixed[ci] = (int) l.v; ci++; }
		}
		for (int pass = 0; pass < 2; pass++)
			for (int i = 0; i < g.cells.n; i++)
			{
				Cell &C = g.cells[i];
				if ((pass == 0) != (C.cs == 1)) continue;
				intrinsic (C.node, &C.mn, &C.mx);
				int ed = edges_h (C.node, 0);
				// a cell's width: its border box (CSS: content + padding + border)
				int fixed = C.w.u == U_PX ? (int) (C.w.v + 0.5f) + ed : -1;
				if (fixed >= 0 && fixed < C.mn) fixed = C.mn;
				if (C.cs == 1)
				{
					int c = C.col;
					if (C.mn > g.colMin[c]) g.colMin[c] = C.mn;
					int mx = fixed >= 0 ? fixed : C.mx;
					if (fixed >= 0) { if (fixed > g.colFixed[c]) g.colFixed[c] = fixed; }
					if (mx > g.colMax[c]) g.colMax[c] = mx;
					if (C.w.u == U_PCT && (int) C.w.v > g.colPct[c]) g.colPct[c] = (int) C.w.v;
				}
				else
				{	// spread what the spanned columns lack
					int sp = spacing (node);
					int smin = 0, smax = 0; for (int k = C.col; k < C.col + C.cs; k++) { smin += g.colMin[k]; smax += g.colMax[k]; }
					smin += sp * (C.cs - 1); smax += sp * (C.cs - 1);
					int mx = fixed >= 0 ? fixed : C.mx;
					if (C.mn > smin) { int add = C.mn - smin; for (int k = C.col; k < C.col + C.cs; k++) g.colMin[k] += add / C.cs + (k - C.col < add % C.cs ? 1 : 0); }
					if (mx > smax) { int add = mx - smax; int base = 0; for (int k = C.col; k < C.col + C.cs; k++) base += g.colMax[k]; for (int k = C.col; k < C.col + C.cs; k++) g.colMax[k] += base > 0 ? (int) ((long long) add * g.colMax[k] / base) : add / C.cs; }
				}
			}
		for (int c = 0; c < n; c++)
		{
			if (g.colFixed[c] >= 0) { if (g.colFixed[c] < g.colMin[c]) g.colFixed[c] = g.colMin[c]; g.colMax[c] = g.colFixed[c]; }
			if (g.colMax[c] < g.colMin[c]) g.colMax[c] = g.colMin[c];
		}
	}
	void table_intrinsic (int node, int *mn, int *mx)
	{
		Grid g; table_rows (node, g); columns (node, g);
		int sp = spacing (node), ed = edges_h (node, 0);
		int a = sp * (g.ncols + 1) + ed, b = a;
		for (int c = 0; c < g.ncols; c++) { a += g.colMin[c]; b += g.colMax[c]; }
		const Style &s = st (node);
		if (s.width.u == U_PX) { int w = (int) s.width.v; if (w > a) a = w > a ? a : a; b = w > a ? w : a; }
		if (g.caption >= 0) { int ca, cb; intrinsic (g.caption, &ca, &cb); if (ca > a) a = ca; }
		*mn = a; *mx = b;
	}
	// the columns' widths for a table avail wide (its border box: its width, or fit to its content)
	int distribute (int node, Grid &g, int avail, bool shrink)
	{
		const Style &s = st (node);
		int n = g.ncols, sp = spacing (node), ed = edges_h (node, avail);
		int sumMin = 0, sumMax = 0; for (int c = 0; c < n; c++) { sumMin += g.colMin[c]; sumMax += g.colMax[c]; }
		int frame = sp * (n + 1) + ed;
		int target;
		bool given = s.width.u == U_PX || s.width.u == U_PCT;
		if (given) target = resolve (s.width, avail) + (s.width.u == U_PCT ? 0 : 0);
		else target = sumMax + frame < avail ? sumMax + frame : avail;
		if (s.maxWidth.u != U_NONE) { int m = resolve (s.maxWidth, avail); if (target > m) target = m; }
		if (target < sumMin + frame) target = sumMin + frame;
		(void) shrink;
		int room = target - frame;
		for (int c = 0; c < n; c++) g.colW[c] = g.colMin[c];
		int left = room - sumMin;
		if (left > 0)
		{
			// the fixed columns, then the percentages, then the others towards their max
			int want = 0; for (int c = 0; c < n; c++) if (g.colFixed[c] >= 0) want += g.colFixed[c] - g.colW[c];
			if (want > 0) { int give = want < left ? want : left; int done = 0; for (int c = 0; c < n; c++) if (g.colFixed[c] >= 0) { int k = (int) ((long long) give * (g.colFixed[c] - g.colW[c]) / want); g.colW[c] += k; done += k; } left -= done; }
			want = 0; for (int c = 0; c < n; c++) if (g.colFixed[c] < 0 && g.colPct[c] >= 0) { int t = room * g.colPct[c] / 100; if (t > g.colW[c]) want += t - g.colW[c]; }
			if (want > 0 && left > 0) { int give = want < left ? want : left; int done = 0; for (int c = 0; c < n; c++) if (g.colFixed[c] < 0 && g.colPct[c] >= 0) { int t = room * g.colPct[c] / 100; if (t > g.colW[c]) { int k = (int) ((long long) give * (t - g.colW[c]) / want); g.colW[c] += k; done += k; } } left -= done; }
			want = 0; for (int c = 0; c < n; c++) if (g.colFixed[c] < 0 && g.colPct[c] < 0) want += g.colMax[c] - g.colW[c];
			if (want > 0 && left > 0) { int give = want < left ? want : left; int done = 0; for (int c = 0; c < n; c++) if (g.colFixed[c] < 0 && g.colPct[c] < 0) { int k = (int) ((long long) give * (g.colMax[c] - g.colW[c]) / want); g.colW[c] += k; done += k; } left -= done; }
			// what remains: the auto columns (by their max), else all
			if (left > 0)
			{
				int base = 0, cnt = 0; for (int c = 0; c < n; c++) if (g.colFixed[c] < 0 && g.colPct[c] < 0) { base += g.colMax[c]; cnt++; }
				bool all = cnt == 0;
				if (all) for (int c = 0; c < n; c++) { base += g.colW[c]; cnt++; }
				int done = 0;
				for (int c = 0; c < n; c++) if (all || (g.colFixed[c] < 0 && g.colPct[c] < 0)) { int k = base > 0 ? (int) ((long long) left * (all ? g.colW[c] : g.colMax[c]) / base) : left / cnt; g.colW[c] += k; done += k; }
				// the rounding's rest: the last column
				for (int c = n - 1; c >= 0 && done < left; c--) { g.colW[c] += left - done; done = left; }
			}
		}
		return target;
	}
	// the table at (x, y) in a container cw wide -> its height (its margin box's, the bottom margin left out)
	int table (int node, int x, int y, int cw, Bfc &bfc, bool atZero = false)
	{
		(void) bfc;
		const Style &s = st (node);
		Grid g; table_rows (node, g); columns (node, g);
		int ml = marg (node, 3, cw), mr = marg (node, 1, cw);
		int avail = atZero ? cw : cw - ml - mr;
		int tw = distribute (node, g, avail, false);
		int tx = x + (atZero ? 0 : ml);
		if (!atZero && tw < avail)
		{
			bool la = s.margin[3].u == U_AUTO, ra = s.margin[1].u == U_AUTO;
			int par = d.nodes[node].parent;
			bool centred = (la && ra) || (par >= 0 && st (par).textAlign == TA_CENTER && centring_attr (par));
			const char *al = d.attr (node, "align");
			if (al && ieq (al, "center")) centred = true;
			if (centred) tx = x + ml + (avail - tw) / 2;
			else if (la) tx = x + cw - mr - tw;
			else if (par >= 0 && st (par).textAlign == TA_RIGHT && centring_attr_right (par)) tx = x + cw - mr - tw;
		}
		int sp = spacing (node);
		int bg = rect (0, 0, 0, 0, 0, node, s.link);
		int ty = y;
		// the caption, above
		if (g.caption >= 0) { Bfc b; int h = layout_box (g.caption, tx, ty, tw, true); ty += h; }
		int top = ty;
		int ix = tx + bord (node, 3) + pad (node, 3, cw), iy = ty + bord (node, 0) + pad (node, 0, cw);
		int n = g.ncols;
		Vec<int> cx; for (int c = 0; c <= n; c++) cx.push (0);
		cx[0] = ix + sp; for (int c = 0; c < n; c++) cx[c + 1] = cx[c] + g.colW[c] + sp;
		int ry = iy + sp;
		for (int r = 0; r < g.rows.n; r++)
		{
			TRow &R = g.rows[r];
			R.y = ry;
			const Style &rs = st (R.node);
			R.bg = (rs.bg >> 24) ? rect (cx[0], ry, cx[n] - sp - cx[0], 0, rs.bg, R.node) : -1;
			int rh = R.hh.u == U_PX ? (int) R.hh.v : 0;
			for (int i = 0; i < g.cells.n; i++)
			{
				Cell &C = g.cells[i];
				if (C.row != r) continue;
				int w = cx[C.col + C.cs] - sp - cx[C.col];
				C.bg = rect (0, 0, 0, 0, 0, C.node, st (C.node).link);
				C.first = items.n;
				Bfc b;
				int cwid = w - edges_h (C.node, w);
				int cyy = ry + bord (C.node, 0) + pad (C.node, 0, w);
				int end = flow (C.node, cx[C.col] + bord (C.node, 3) + pad (C.node, 3, w), cyy, cwid < 0 ? 0 : cwid, b);
				int fb = floats_bottom (b, 3); if (fb > end) end = fb;
				C.last = items.n;
				C.h = end - ry + pad (C.node, 2, w) + bord (C.node, 2);
				const Style &cs = st (C.node);
				if (cs.height.u == U_PX) { int hh = (int) cs.height.v + edges_v (C.node, w); if (hh > C.h) C.h = hh; }
				C.y = ry;
				if (C.rs == 1 && C.h > rh) rh = C.h;
			}
			R.h = rh;
			ry += rh + sp;
		}
		// the rowspans: their rows grown when they need it
		for (int i = 0; i < g.cells.n; i++)
		{
			Cell &C = g.cells[i]; if (C.rs <= 1) continue;
			int last = C.row + C.rs - 1;
			int span = g.rows[last].y + g.rows[last].h - g.rows[C.row].y;
			if (C.h > span) { int add = C.h - span; g.rows[last].h += add; for (int r = last + 1; r < g.rows.n; r++) { int dy = add; g.rows[r].y += dy; for (int k = 0; k < g.cells.n; k++) if (g.cells[k].row == r) { shift (g.cells[k].first, g.cells[k].last, 0, dy); g.cells[k].y += dy; } } ry += add; }
		}
		// the cells' backgrounds, borders, vertical alignment
		for (int r = 0; r < g.rows.n; r++) if (g.rows[r].bg >= 0) items[g.rows[r].bg].h = g.rows[r].h;
		for (int i = 0; i < g.cells.n; i++)
		{
			Cell &C = g.cells[i];
			int last = C.row + C.rs - 1;
			int h = g.rows[last].y + g.rows[last].h - C.y;
			int w = cx[C.col + C.cs] - sp - cx[C.col];
			const Style &cs = st (C.node);
			int extra = h - C.h;
			if (extra > 0)
			{
				int va = cs.vAlign;
				if (va == VA_MIDDLE) shift (C.first, C.last, 0, extra / 2);
				else if (va == VA_BOTTOM) shift (C.first, C.last, 0, extra);
			}
			if (s.collapse)
			{	// shared borders: the cell's right / bottom ones drawn over its neighbours' left / top
				box_paint (C.node, C.bg, cx[C.col] - (C.col ? bord (C.node, 3) / 2 : 0), C.y - (C.row ? bord (C.node, 0) / 2 : 0), w + (C.col ? bord (C.node, 3) / 2 : 0), h + (C.row ? bord (C.node, 0) / 2 : 0));
			}
			else box_paint (C.node, C.bg, cx[C.col], C.y, w, h);
		}
		int ih = ry - iy;
		int th = ih + edges_v (node, cw);
		if (s.height.u == U_PX && th < (int) s.height.v) th = (int) s.height.v;
		box_paint (node, bg, tx, top, tw, th);
		return th + (top - y);
	}
	bool centring_attr_right (int node) const { const char *a = d.attr (node, "align"); return a && ieq (a, "right"); }

	// ---- all of it ---------------------------------------------------------------------------------------------------------------
	void run (int w)
	{
		items.clear ();
		int n = d.nodes.n;
		minW = (int *) realloc (minW, sizeof (int) * (n ? n : 1)); maxW = (int *) realloc (maxW, sizeof (int) * (n ? n : 1));
		for (int i = 0; i < n; i++) minW[i] = maxW[i] = -1;
		lastBaseline = -1; marker.on = false;
		width = w;
		const Style &r = st (0);
		bg = r.bg;
		// the body's background is the page's: not drawn as a box
		Bfc b;
		int bgItem = rect (0, 0, 0, 0, 0, 0);
		int end = flow (0, pad (0, 3, w) + bord (0, 3), pad (0, 0, w), w - edges_h (0, w), b);
		int fb = floats_bottom (b, 3); if (fb > end) end = fb;
		height = end + pad (0, 2, w);
		(void) bgItem;
		// the widest item (a table wider than the view: scrolled sideways)
		for (int i = 0; i < items.n; i++) if (items[i].x + items[i].w > width && items[i].w) width = items[i].x + items[i].w;
	}
};

} // namespace html
} // namespace mail

#endif
