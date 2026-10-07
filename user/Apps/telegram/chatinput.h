//
// chatinput.h -- where a message is written: a strip of tools (the emoticons, in Messenger's way), the text
// itself -- wrapped, its emoticons drawn as pictures while it is written, the caret moved by the arrows and
// the mouse, Ctrl+V pasting --, and Send. Enter sends, Shift+Enter starts a new line. The emoticons' picker:
// a grid of the pictures, a click puts one in the text.
//
// MIT licence.
//
#ifndef TG_CHATINPUT_H
#define TG_CHATINPUT_H

#include "chatview.h"
#include "systemkit/systemkit.h"

static void send_current ();
static void typed_something ();
static bool paste_picture ();		// (main.cpp: the clipboard's picture, or a picture file copied: attached)
static void choose_picture ();		// (main.cpp: the file dialog)

static void attach_clear () { delete [] g_att.jpg; delete [] g_att.thumb; memset (&g_att, 0, sizeof g_att); }

// ---- the text --------------------------------------------------------------------------------------------

class ChatEdit : public Widget
{
public:
	TgPane *m_pane = g_pane;		// (its pane: buddylist.h)
	enum { CAP = 4096 };
	char buf[CAP];
	int len, caret;

	ChatEdit (int l, int t, int w, int h) : Widget (l, t, w, h), len (0), caret (0), m_scroll (0), m_down (false), m_blink (0), m_rev (1), m_lw (-1)
	{
		buf[0] = 0; canFocus = true;
	}
	void clear () { len = caret = 0; buf[0] = 0; m_scroll = 0; changed (); }
	void insert (const char *s)
	{
		int n = (int) strlen (s);
		if (len + n >= CAP) n = CAP - 1 - len;
		if (n <= 0) return;
		memmove (buf + caret + n, buf + caret, (size_t) (len - caret + 1));
		memcpy (buf + caret, s, (size_t) n);
		len += n; caret += n;
		changed ();
	}
	void tickBlink () { tg_use (m_pane); if (hasFocus && ++m_blink % 30 == 0) invalidate (true); }

	void onDraw () override
	{
		tg_use (m_pane);
		Canvas &cv = canvas;
		cv.clear (0xFFFFFF);
		lay ();
		int x0 = 6, y0 = 4 - m_scroll;
		if (!len && !hasFocus) ftext (cv, g_face.ui, x0, 6, TR ("Type a message here"), TC_LIGHT, 1);
		rich_draw (cv, m_r, g_face.ui, buf, x0, y0, TC_INK);
		if (hasFocus && (m_blink / 30) % 2 == 0)
		{
			int cx, cl;
			caretXY (caret, &cx, &cl);
			cv.fillRect (x0 + cx, y0 + cl * m_r.lineH + 2, 1, m_r.lineH - 3, TC_INK);
		}
	}

	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		tg_use (m_pane);
		(void) br; (void) bm;
		if (mx < 0) { m_down = false; return false; }
		uk_cursor (KAPI_CURSOR_TEXT);
		if (wheel) { m_scroll -= wheel * m_r.lineH; clampScroll (); invalidate (true); return true; }
		if (bl && !m_down) { m_down = true; setFocus (); caret = offsetAt (mx - 6, my - 4 + m_scroll); m_blink = 0; invalidate (true); }
		else if (!bl) m_down = false;
		return true;
	}

	bool onKey (long k) override
	{
		tg_use (m_pane);
		m_blink = 0;
		unsigned mods = kapi_get_modifiers ();
		if (k == KEY_ENTER)
		{
			if (mods & MOD_SHIFT) { insert ("\n"); typed_something (); }
			else send_current ();
			return true;
		}
		if (k == UK_CTRL ('V'))
		{
			if (paste_picture ()) return true;
			static char b[CAP];
			if (clip_get_text (b, sizeof b)) { insert (b); typed_something (); }
			return true;
		}
		if (k == UK_CTRL ('A')) { caret = len; changed (); return true; }
		if (k == KEY_BACKSPACE) { if (caret > 0) { int s = unitBefore (caret); memmove (buf + s, buf + caret, (size_t) (len - caret + 1)); len -= caret - s; caret = s; changed (); typed_something (); } return true; }
		if (k == KEY_DEL) { if (caret < len) { int e = unitAfter (caret); memmove (buf + caret, buf + e, (size_t) (len - e + 1)); len -= e - caret; changed (); } return true; }
		if (k == KEY_LEFT) { caret = unitBefore (caret); invalidate (true); return true; }
		if (k == KEY_RIGHT) { caret = unitAfter (caret); invalidate (true); return true; }
		if (k == KEY_HOME) { int x, l; caretXY (caret, &x, &l); caret = offsetAt (0, l * m_r.lineH + 1); invalidate (true); return true; }
		if (k == KEY_END) { int x, l; caretXY (caret, &x, &l); caret = offsetAt (100000, l * m_r.lineH + 1); invalidate (true); return true; }
		if (k == KEY_UP || k == KEY_DOWN)
		{
			int x, l; caretXY (caret, &x, &l);
			l += k == KEY_UP ? -1 : 1;
			if (l < 0 || l >= m_r.lines) return false;
			caret = offsetAt (x, l * m_r.lineH + 1);
			ensureCaret (); invalidate (true);
			return true;
		}
		char u[8];
		int n = uk_u8_key (k, u);
		if (n) { u[n] = 0; insert (u); typed_something (); return true; }
		return false;
	}

private:
	Rich m_r;
	int m_scroll;
	bool m_down;
	int m_blink;
	unsigned m_rev, m_laid = 0;
	int m_lw;

	void changed () { m_rev++; lay (); ensureCaret (); invalidate (true); }
	void lay ()
	{
		if (m_laid == m_rev && m_lw == width) return;
		rich_layout (m_r, g_face.ui, buf, width - 14, 19);
		if (!m_r.lines) m_r.lines = 1;
		m_laid = m_rev; m_lw = width;
	}
	void clampScroll ()
	{
		int most = m_r.lines * m_r.lineH + 8 - height;
		if (m_scroll > most) m_scroll = most;
		if (m_scroll < 0) m_scroll = 0;
	}
	void ensureCaret ()
	{
		int x, l; caretXY (caret, &x, &l);
		int y = l * m_r.lineH;
		if (y < m_scroll) m_scroll = y;
		if (y + m_r.lineH + 8 > m_scroll + height) m_scroll = y + m_r.lineH + 8 - height;
		clampScroll ();
	}
	// the emoticon sequence (or the character) ending at / starting at i
	int unitBefore (int i)
	{
		if (i <= 0) return 0;
		for (int k = 0; k < m_r.n; k++) if (m_r.p[k].kind == RP_EMO && m_r.p[k].off + m_r.p[k].len == i) return m_r.p[k].off;
		return uk_u8_prev (buf, i);
	}
	int unitAfter (int i)
	{
		if (i >= len) return len;
		for (int k = 0; k < m_r.n; k++) if (m_r.p[k].kind == RP_EMO && m_r.p[k].off == i) return i + m_r.p[k].len;
		return uk_u8_next (buf, i, len);
	}
	// the caret's place for byte offset o: x (px) and line
	void caretXY (int o, int *x, int *line)
	{
		lay ();
		int px = 0, ln = 0, from = 0;
		int space = g_face.ui->widthN (" ", 1, 0);
		for (int k = 0; k < m_r.n; k++)
		{
			RichPiece &pc = m_r.p[k];
			if (pc.off > o) break;
			if (o < pc.off + pc.len)
			{
				*line = pc.line;
				*x = pc.x + (pc.kind == RP_EMO ? 0 : g_face.ui->widthN (buf + pc.off, o - pc.off, 0));
				return;
			}
			px = pc.x + pc.w + (pc.kind == RP_EMO ? 1 : 0); ln = pc.line; from = pc.off + pc.len;
		}
		// after the last piece before o: the spaces and line breaks between
		for (int i = from; i < o; i++)
		{
			if (buf[i] == '\n') { ln++; px = 0; }
			else if (buf[i] == ' ' && !(px == 0 && i > 0 && buf[i - 1] == '\n')) px += space;
		}
		*x = px; *line = ln;
	}
	// the byte offset nearest (x, y) in the text
	int offsetAt (int x, int y)
	{
		lay ();
		int line = y / (m_r.lineH ? m_r.lineH : 16);
		if (line < 0) line = 0;
		int best = 0, bestD = 1 << 30;
		for (int o = 0; o <= len; o = o < len ? unitAfter (o) : len + 1)
		{
			int cx, cl; caretXY (o, &cx, &cl);
			if (cl == line)
			{
				int d = cx > x ? cx - x : x - cx;
				if (d < bestD) { bestD = d; best = o; }
			}
			else if (cl > line && bestD == 1 << 30) { best = o; break; }
			if (o == len) break;
		}
		if (bestD == 1 << 30 && line >= m_r.lines) best = len;
		return best;
	}
};

// ---- the emoticons' picker ----------------------------------------------------------------------------------

class EmoPicker : public Widget
{
public:
	TgPane *m_pane = g_pane;		// (its pane: buddylist.h)
	enum { COLS = 8, CELL = 30 };
	ChatEdit *target;
	EmoPicker (ChatEdit *t) : Widget (0, 0, COLS * CELL + 12, ((EMO_COUNT + COLS - 1) / COLS) * CELL + 36), target (t), m_hot (-1), m_down (true)
	{ hidden = true; catchOutside = true; transparent = true; }
	void show (int x, int y) { left = x; top = y; hidden = false; m_hot = -1; m_down = true; bringToFront (); invalidate (true); if (parent) parent->invalidate (false); }
	void hide () { hidden = true; if (parent) parent->invalidate (true); }
	void onDraw () override
	{
		tg_use (m_pane);
		Canvas &cv = canvas;
		cv.clear (UK_TRANSPARENT_KEY);
		uk_rbox (cv, 0, 0, width, height, 6, 0xFFFFFF, 0xEEF4FA);
		uk_rline (cv, 0, 0, width, height, 6, TC_SEL_RIM);
		ftext (cv, g_face.small, 8, 5, m_hot >= 0 ? TR (emo_info[m_hot].name) : TR ("Emoticons"), TC_GREY, m_hot >= 0 ? 2 : 0);
		if (m_hot >= 0 && emo_info[m_hot].text) ftext (cv, g_face.small, width - 8 - ftw (g_face.small, emo_info[m_hot].text), 5, emo_info[m_hot].text, TC_LIGHT);
		for (int i = 0; i < EMO_COUNT; i++)
		{
			int x = 6 + (i % COLS) * CELL, y = 26 + (i / COLS) * CELL;
			if (i == m_hot) { uk_rbox (cv, x, y, CELL, CELL, 4, TC_HOT_TOP, TC_SEL_BOT); uk_rline (cv, x, y, CELL, CELL, 4, TC_SEL_RIM); }
			emo_draw (cv, i, x + 4, y + 4, CELL - 8);
		}
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		tg_use (m_pane);
		(void) br; (void) bm; (void) wheel;
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (!in) { if (bl && !m_down) hide (); m_down = bl != 0; if (m_hot != -1) { m_hot = -1; invalidate (true); } return bl != 0; }
		int c = (mx - 6) / CELL, r = (my - 26) / CELL, i = my >= 26 && mx >= 6 && c < COLS ? r * COLS + c : -1;
		if (i >= EMO_COUNT) i = -1;
		if (i != m_hot) { m_hot = i; invalidate (true); }
		if (bl && !m_down && i >= 0)
		{
			char u[8]; int n = uk_u8_put (u, emo_info[i].cp); u[n] = 0;
			if (target) { target->insert (u); target->setFocus (); }
			hide ();
		}
		m_down = bl != 0;
		return true;
	}
	bool onKey (long k) override { tg_use (m_pane); if (k == 27) { hide (); return true; } return false; }
private:
	int m_hot;
	bool m_down;
};

// ---- the strip, the text, Send ------------------------------------------------------------------------------

class InputBar : public Widget
{
public:
	TgPane *m_pane = g_pane;		// (its pane: buddylist.h)
	enum { STRIP = 30 };
	ChatEdit *edit;
	Button *sendBtn;
	InputBar (int l, int t, int w, int h) : Widget (l, t, w, h), m_hot (-1), m_down (false)
	{
		edit = new ChatEdit (10, STRIP + 2, w - 110, h - STRIP - 10);
		edit->anchor = ANCHOR_FILL;
		addChild (edit);
		sendBtn = new Button (w - 92, STRIP + 2, 82, 30, TR ("Send"), [] (Widget &w) { tg_use (((InputBar *) w.parent)->m_pane); send_current (); });
		sendBtn->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
		addChild (sendBtn);
	}
	void onDraw () override
	{
		tg_use (m_pane);
		Canvas &cv = canvas;
		fill_grad (cv, 0, 0, width, height, 0xF2F7FC, TC_INPUT_BG);
		cv.fillRect (0, 0, width, 1, TC_LINE);
		// the strip's tools: the emoticons
		int x = 8;
		if (m_hot == 0) { uk_rbox (cv, x - 2, 3, 42, 24, 4, TC_HOT_TOP, TC_HOT_BOT); uk_rline (cv, x - 2, 3, 42, 24, 4, TC_HOT_RIM); }
		emo_draw (cv, EMO_SMILE, x + 2, 6, 18);
		uk_glyph (cv, WKG_CHEV_DOWN, x + 30, 15, 7, TC_GREY);
		// the picture button
		int px = x + 46;
		if (m_hot == 1) { uk_rbox (cv, px - 2, 3, 30, 24, 4, TC_HOT_TOP, TC_HOT_BOT); uk_rline (cv, px - 2, 3, 30, 24, 4, TC_HOT_RIM); }
		pictureIcon (cv, px + 4, 7);
		int hx = px + 36;
		if (g_att.jpg)						// the picture attached: its view, its name, the cross
		{
			int cw = width - hx - 8;
			uk_rbox (cv, hx, 3, cw, 24, 12, 0xFFFFFF, 0xEAF2FA);
			uk_rline (cv, hx, 3, cw, 24, 12, TC_SEL_RIM);
			if (g_att.thumb) blit_rect_round (cv, g_att.thumb, hx + 4, 5, g_att.tw, g_att.th, 3);
			char lab[160];
			snprintf (lab, sizeof lab, TR ("Picture: %s (%d x %d) - sent with the next message"), g_att.name, g_att.w, g_att.h);
			ftext (cv, g_face.small, hx + 8 + g_att.tw, 9, lab, TC_INK, 0, cw - g_att.tw - 34);
			int cx = hx + cw - 14;
			uk_glyph (cv, WKG_CLOSE, cx, 15, 9, m_hot == 2 ? TC_BUSY : TC_GREY);
			m_closeX = cx;
		}
		else ftext (cv, g_face.small, hx, 9, TR ("Enter: send - Shift+Enter: a new line - Ctrl+V: a picture too"), TC_LIGHT, 1, width - hx - 10);
		// the text's frame
		Widget *e = edit;
		uk_rbox (cv, e->left - 3, e->top - 3, e->width + 6, e->height + 6, 4, TC_LINE, TC_LINE);
		uk_rbox (cv, e->left - 2, e->top - 2, e->width + 4, e->height + 4, 3, 0xFFFFFF, 0xFFFFFF);
		if (e->hasFocus) uk_rline (cv, e->left - 3, e->top - 3, e->width + 6, e->height + 6, 4, 0x6FA8E6);
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		tg_use (m_pane);
		(void) br; (void) bm; (void) wheel;
		if (mx < 0) { if (m_hot != -1) { m_hot = -1; invalidate (true); } return false; }
		int hot = my < STRIP && mx >= 6 && mx < 48 ? 0 : my < STRIP && mx >= 52 && mx < 82 ? 1 :
			  my < STRIP && g_att.jpg && mx >= m_closeX - 10 && mx < m_closeX + 10 ? 2 : -1;
		if (hot != m_hot) { m_hot = hot; invalidate (true); }
		if (hot >= 1) uk_cursor (KAPI_CURSOR_HAND);
		if (bl && !m_down && hot == 1) choose_picture ();
		if (bl && !m_down && hot == 2) { attach_clear (); invalidate (true); }
		if (bl && !m_down && hot == 0)
		{
			// the picker over the strip
			int ax = 0, ay = 0;
			for (Widget *w = this; w && w->parent; w = w->parent) { ax += w->left; ay += w->top; }
			g_picker->show (ax + 6, ay - g_picker->height - 2);
		}
		m_down = bl != 0;
		return my < STRIP;
	}
	static void pictureIcon (Canvas &cv, int x, int y)
	{
		uk_rbox (cv, x, y, 20, 16, 3, 0x9FD0F5, 0x4A8FD0);
		uk_rline (cv, x, y, 20, 16, 3, 0x2E6EAE);
		VPath p;
		p.circle (V (x + 6), V (y + 5), V (2)); p.fill (cv, 0xFFE27A);
		p.clear (); { int t[6] = { V (x + 2), V (y + 14), V (x + 9), V (y + 6), V (x + 15), V (y + 14) }; p.poly (t, 3); } p.fill (cv, 0x2E7D32);
		p.clear (); { int t[6] = { V (x + 10), V (y + 14), V (x + 14), V (y + 9), V (x + 19), V (y + 14) }; p.poly (t, 3); } p.fill (cv, 0x4CAF50);
	}
private:
	int m_hot;
	bool m_down;
	int m_closeX = 0;
};

#endif
