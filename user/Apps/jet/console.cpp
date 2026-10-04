//
// web/console.cpp -- Web's JavaScript console: a window of its own (View > Console), the browser
// program started again as `web --onyx-console` by the browser window (engine_spawn_self), its
// standard input and output two pipes to it. It lists what the page's console takes: console.log /
// info / warn / error / debug with all their arguments, the script errors nobody caught, the
// engine's own warnings -- each with its place (file:line:column), coloured by its level, the long
// ones wrapped. The browser empties it when a new page starts loading.
//
// The browser writes lines:  c  (clear) /  m<level> <where>\x02<text>  (a message: level '0' log,
// '1' warning, '2' error, '3' debug, '4' info; the text's own line ends are \x01) / an empty line
// (come to the front).  This window answers:  clear  (the Clear button: the browser forgets its
// copy too).  When the browser window closes, the window stays until the user closes it.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see LICENSE).
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "wtk/paint.h"
#include "wtk/toolbar.h"
#include "clipboard.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace wtk;

namespace {

enum { W = 760, H = 380, TB_H = 34, PAD = 8, SBAR = 14, MAXMSG = 1000 };
enum { L_LOG = 0, L_WARN, L_ERROR, L_DEBUG, L_INFO };

struct Msg
{
	int level;
	std::string where, text;
};
// A row on screen: a piece of a message's text (its first row also shows the place).
struct Row
{
	int msg, start, len;
	bool first;
};
std::vector<Msg> g_msgs;
std::vector<Row> g_rows;
int g_top;					// the first row shown
int g_laidW = -1;				// the width the rows were laid out for
unsigned g_laidN;				// the messages laid out so far
bool g_follow = true;				// the view stays at the end as messages come

class Lines;
Lines *g_lines;
Scrollbar *g_bar;
Label *g_count;

int row_h () { return wk_fh () + 4; }

// The text's part from `start` that fits `w` pixels: cut at a space when there is one.
int fit (const std::string &s, int start, int w)
{
	int n = (int) s.size () - start;
	if (n <= 0) return 0;
	// (one line of text: up to its own line end)
	int eol = 0;
	while (eol < n && s[(size_t) (start + eol)] != '\n') eol++;
	char buf[512];
	int lo = 1, hi = eol < (int) sizeof buf - 1 ? eol : (int) sizeof buf - 1, best = 1;
	if (hi < 1) return 0;
	memcpy (buf, s.data () + start, (size_t) hi); buf[hi] = 0;
	if (wk_text_w (buf) <= w) return hi;
	while (lo <= hi)
	{
		int mid = (lo + hi) / 2;
		// (never in the middle of a UTF-8 character)
		int cut = mid;
		while (cut > 1 && ((unsigned char) s[(size_t) (start + cut)] & 0xC0) == 0x80) cut--;
		memcpy (buf, s.data () + start, (size_t) cut); buf[cut] = 0;
		if (wk_text_w (buf) <= w) { best = cut; lo = mid + 1; } else hi = mid - 1;
	}
	for (int i = best; i > best / 2; i--)
		if (s[(size_t) (start + i)] == ' ') return i + 1;
	return best;
}

class Lines : public Widget
{
public:
	Lines (int l, int t, int w, int h) : Widget (l, t, w, h) { anchor = ANCHOR_FILL; }
	int visible () const { return height / row_h () > 0 ? height / row_h () : 1; }
	int textW () const { return width - 2 * PAD - 12; }
	// The rows of the messages not laid out yet (all of them after a resize).
	void layout_rows ()
	{
		if (g_laidW != width) { g_rows.clear (); g_laidN = 0; g_laidW = width; }
		for (; g_laidN < g_msgs.size (); g_laidN++)
		{
			const Msg &m = g_msgs[g_laidN];
			int start = 0, n = (int) m.text.size ();
			bool first = true;
			// (the first row leaves room for the place, at the right)
			int placeW = m.where.empty () ? 0 : wk_text_w (m.where.c_str ()) + 16;
			do
			{
				int w = textW () - (first && placeW < textW () / 2 ? placeW : 0);
				int len = fit (m.text, start, w);
				Row r = { (int) g_laidN, start, len, first };
				g_rows.push_back (r);
				start += len;
				if (start < n && m.text[(size_t) start] == '\n') start++;
				first = false;
			} while (start < n && g_rows.size () < 20000);
		}
		int maxTop = (int) g_rows.size () - visible ();
		if (maxTop < 0) maxTop = 0;
		if (g_follow || g_top > maxTop) g_top = maxTop;
		g_bar->vmax = maxTop < 1 ? 1 : maxTop;
		g_bar->value = g_top;
		g_bar->invalidate (true);
		char c[48];
		int errors = 0, warnings = 0;
		for (size_t i = 0; i < g_msgs.size (); i++) { errors += g_msgs[i].level == L_ERROR; warnings += g_msgs[i].level == L_WARN; }
		snprintf (c, sizeof c, "%d messages, %d errors, %d warnings", (int) g_msgs.size (), errors, warnings);
		g_count->setText (c);
	}
	void onDraw () override
	{
		layout_rows ();
		canvas.clear (C_FIELD);
		int rh = row_h ();
		if (g_msgs.empty ()) { wk_text_c (canvas, 0, 0, width, height, "The page's console is empty", C_DIS); return; }
		for (int i = 0; i < visible () + 1 && g_top + i < (int) g_rows.size (); i++)
		{
			const Row &r = g_rows[(size_t) (g_top + i)];
			const Msg &m = g_msgs[(size_t) r.msg];
			int y = i * rh;
			unsigned ink = C_FIELD_TEXT, band = 0;
			bool tint = false;
			if (m.level == L_ERROR) { ink = 0x00B01818; band = 0x00FCE8E6; tint = true; }
			else if (m.level == L_WARN) { ink = 0x00805800; band = 0x00FFF4D6; tint = true; }
			else if (m.level == L_DEBUG) ink = C_DIS;
			else if (m.level == L_INFO) ink = 0x00205090;
			if (tint) canvas.fillRect (0, y, width, rh, band);
			if (r.first && i > 0) canvas.fillRect (0, y, width, 1, tint ? 0x00E8D0C8 : 0x00E4E4E4);
			if (tint) canvas.fillRect (0, y, 3, rh, ink);
			char buf[512];
			int n = r.len < (int) sizeof buf - 1 ? r.len : (int) sizeof buf - 1;
			memcpy (buf, m.text.data () + r.start, (size_t) n); buf[n] = 0;
			for (int k = 0; k < n; k++) if ((unsigned char) buf[k] < 32) buf[k] = ' ';	// (tabs...)
			wk_text_l (canvas, PAD, y, rh, buf, ink);
			if (r.first && !m.where.empty ())
			{
				int ww = wk_text_w (m.where.c_str ());
				if (ww + 16 < textW () / 2) wk_text_l (canvas, width - PAD - ww, y, rh, m.where.c_str (), tint ? ink : C_DIS);
			}
		}
	}
	void scrollTo (int top)
	{
		int maxTop = (int) g_rows.size () - visible ();
		if (maxTop < 0) maxTop = 0;
		if (top > maxTop) top = maxTop;
		if (top < 0) top = 0;
		g_top = top;
		g_follow = top == maxTop;
		invalidate (true);
	}
	bool onMouse (int, int, int, int, int, int wheel) override
	{
		if (wheel) scrollTo (g_top - wheel * 3);
		return true;
	}
	bool onKey (long k) override
	{
		if (k == KEY_UP) scrollTo (g_top - 1);
		else if (k == KEY_DOWN) scrollTo (g_top + 1);
		else if (k == KEY_PGUP) scrollTo (g_top - visible ());
		else if (k == KEY_PGDN) scrollTo (g_top + visible ());
		else if (k == KEY_HOME) scrollTo (0);
		else if (k == KEY_END) scrollTo (1 << 30);
		else return false;
		return true;
	}
};

void cb_bar (Widget &) { g_lines->scrollTo (g_bar->value); }

void clear_all ()
{
	g_msgs.clear ();
	g_rows.clear ();
	g_laidN = 0;
	g_top = 0;
	g_follow = true;
	g_lines->invalidate (true);
}

void cb_clear (Widget &)
{
	clear_all ();
	(void) !write (1, "clear\n", 6);
}

static const char *const s_levelName[] = { "", "warning: ", "error: ", "debug: ", "info: " };

// Everything, as text, to the clipboard: one message a line (its own lines kept), its place after it.
void cb_copy (Widget &)
{
	std::string all;
	for (size_t i = 0; i < g_msgs.size (); i++)
	{
		const Msg &m = g_msgs[i];
		all += s_levelName[m.level];
		all += m.text;
		if (!m.where.empty ()) { all += "  ("; all += m.where; all += ")"; }
		all += "\n";
	}
	clip_set_text_n (all.c_str (), (int) all.size ());
}

// This window to the front (View > Console again): found among the windows by its owner.
void raise_self ()
{
	static kapi_win_info w[64];
	int n = kapi_win_list (w, 64);
	unsigned me = (unsigned) getpid ();
	for (int i = 0; i < n; i++)
		if (w[i].pid == me) { kapi_win_raise (w[i].id); return; }
}

void line (char *s)
{
	if (!*s) { raise_self (); return; }
	if (s[0] == 'c' && !s[1]) { clear_all (); return; }
	if (s[0] != 'm' || s[1] < '0' || s[1] > '4' || s[2] != ' ') return;
	Msg m;
	m.level = s[1] - '0';
	char *sep = strchr (s + 3, '\x02');
	if (!sep) return;
	m.where.assign (s + 3, (size_t) (sep - (s + 3)));
	m.text = sep + 1;
	for (size_t i = 0; i < m.text.size (); i++) if (m.text[i] == '\x01') m.text[i] = '\n';
	if (g_msgs.size () >= MAXMSG)					// (the oldest go)
	{
		g_msgs.erase (g_msgs.begin (), g_msgs.begin () + MAXMSG / 4);
		g_rows.clear ();
		g_laidN = 0;
	}
	g_msgs.push_back (m);
}

std::string g_in;
bool g_eof;

class ConRoot : public Root
{
public:
	ConRoot (int x, int y) : Root (x, y, W, H, "Console", 0) {}
	void onTick () override
	{
		if (g_eof) return;
		bool any = false;
		for (int pass = 0; pass < 16; pass++)			// (a burst of messages: several reads a tick)
		{
			char buf[4096];
			int n = (int) read (0, buf, sizeof buf);
			if (n == 0) { g_eof = true; break; }
			if (n < 0) break;
			g_in.append (buf, (size_t) n);
			size_t nl;
			while ((nl = g_in.find ('\n')) != std::string::npos)
			{
				std::string l = g_in.substr (0, nl);
				g_in.erase (0, nl + 1);
				line (&l[0]);
				any = true;
			}
			if (g_in.size () > 65536) g_in.clear ();		// (a line without an end: dropped)
		}
		if (any) g_lines->invalidate (true);
	}
	void onResized () override { g_lines->invalidate (true); }
};

} // namespace

int console_main ()
{
	fcntl (0, F_SETFL, fcntl (0, F_GETFL) | O_NONBLOCK);
	fcntl (1, F_SETFL, fcntl (1, F_GETFL) | O_NONBLOCK);		// (a browser that no longer reads never holds this window)
	int sw = 1920, sh = 1080;
	kapi_screen_size (&sw, &sh);
	ConRoot root (sw - W - 24, sh - H - 140);			// (bottom right, above the dock)
	if (root.canvas.px == 0) return 1;

	ToolBar *tb = new ToolBar (0, 0, W, TB_H);
	tb->line = true;
	tb->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	tb->add (new Button (0, 0, 70, 24, "Clear", cb_clear), 6);
	tb->add (new Button (0, 0, 70, 24, "Copy", cb_copy), 6);
	root.addChild (tb);
	g_count = new Label (tb->next () + 14, 9, W - tb->next () - 24, 16, "", C_TEXT, C_BG);
	g_count->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	root.addChild (g_count);

	g_lines = new Lines (0, TB_H, W - SBAR, H - TB_H);
	root.addChild (g_lines);
	g_bar = new Scrollbar (W - SBAR, TB_H, SBAR, H - TB_H, true, 1, 0, cb_bar);
	g_bar->anchor = ANCHOR_TOP | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	root.addChild (g_bar);
	root.setResizable (true);
	g_lines->setFocus ();
	root.run ();
	return 0;
}
