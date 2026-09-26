//
// basic/basscreen.h -- the BASIC screen, shared by the Onyx runtime (runtime.cpp) and the PC
// runtime (pc/obcore): a bas::Host that emulates QBasic's screen in memory -- the screen
// modes, up to 8 pages of 0x00RRGGBB pixels, the 256-colour palette, the text cells in the
// code page 437 fonts (basfont.h), PAINT, GET / PUT, VIEW, the key queue, the mouse, the
// PLAY "MB" note queue. A platform derives from it and supplies the window (open, resize,
// present), the events, the clock and the sound; the GUI controls and the files are its own.
//
#ifndef ONYX_BASSCREEN_H
#define ONYX_BASSCREEN_H

#include "basic/bas.h"
#include "basic/basfont.h"

namespace bas {

// Key codes the platform pushes (the values of Onyx's KEY_* in kapi.h).
enum { K_BACKSPACE = 8, K_TAB = 9, K_ENTER = 13, K_UP = 0x100, K_DOWN, K_LEFT, K_RIGHT, K_HOME, K_END,
	K_PGUP, K_PGDN, K_DEL, K_F1 = 0x110 };

// QBasic's screen modes: size, text cell height, pages, colours, display scale.
struct ModeInfo { int mode, w, h, cellH, pages, colors, sx, sy; };
static const ModeInfo MODES[] = {
	{ 0, 640, 400, 16, 8, 256, 1, 1 }, { 1, 320, 200, 8, 1, 4, 2, 2 }, { 2, 640, 200, 8, 1, 2, 1, 2 },
	{ 7, 320, 200, 8, 8, 16, 2, 2 }, { 8, 640, 200, 8, 4, 16, 1, 2 }, { 9, 640, 350, 14, 2, 16, 1, 1 },
	{ 10, 640, 350, 14, 2, 4, 1, 1 }, { 11, 640, 480, 16, 1, 2, 1, 1 }, { 12, 640, 480, 16, 1, 16, 1, 1 },
	{ 13, 320, 200, 8, 1, 256, 2, 2 }, { -1, 0, 0, 0, 0, 0, 0, 0 } };
static inline const ModeInfo *modeInfo (int m) { for (int i = 0; MODES[i].mode >= 0; i++) if (MODES[i].mode == m) return &MODES[i]; return 0; }

// The VGA default palette: the 16 EGA colours, 16 greys, 9 rings of 24 hues, black.
static inline void vgaPalette (unsigned *p)
{
	static const unsigned EGA16[16] = {
		0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
		0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF };
	for (int i = 0; i < 16; i++) p[i] = EGA16[i];
	static const int grey[16] = { 0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63 };
	for (int i = 0; i < 16; i++) { unsigned g = (unsigned) (grey[i] * 255 / 63); p[16 + i] = (g << 16) | (g << 8) | g; }
	static const int grp[9][5] = {	// high, low, mid1..3
		{ 63, 0, 16, 31, 47 }, { 63, 31, 39, 47, 55 }, { 63, 45, 49, 54, 58 },
		{ 28, 0, 7, 14, 21 }, { 28, 14, 17, 21, 24 }, { 28, 20, 22, 24, 26 },
		{ 16, 0, 4, 8, 12 }, { 16, 8, 10, 12, 14 }, { 16, 11, 12, 13, 15 } };
	for (int g = 0; g < 9; g++)
	{
		int H = grp[g][0], L = grp[g][1], m1 = grp[g][2], m2 = grp[g][3], m3 = grp[g][4];
		const int ring[24][3] = {
			{ L, L, H }, { m1, L, H }, { m2, L, H }, { m3, L, H }, { H, L, H }, { H, L, m3 }, { H, L, m2 }, { H, L, m1 },
			{ H, L, L }, { H, m1, L }, { H, m2, L }, { H, m3, L }, { H, H, L }, { m3, H, L }, { m2, H, L }, { m1, H, L },
			{ L, H, L }, { L, H, m1 }, { L, H, m2 }, { L, H, m3 }, { L, H, H }, { L, m3, H }, { L, m2, H }, { L, m1, H } };
		for (int k = 0; k < 24; k++)
			p[32 + g * 24 + k] = ((unsigned) (ring[k][0] * 255 / 63) << 16) | ((unsigned) (ring[k][1] * 255 / 63) << 8) | (unsigned) (ring[k][2] * 255 / 63);
	}
	for (int i = 248; i < 256; i++) p[i] = 0;
}

class ScreenHost : public Host
{
public:
	// ---- what the platform supplies --------------------------------------------------------------
	virtual bool openWindow (int w, int h) = 0;	// the window, w x h pixels (the page x the scale); `title`
	virtual void resizeWindow (int w, int h) = 0;
	virtual void pageChanged () {}			// the visible page, its size or its scale changed
	virtual void markDirty () {}			// the visible picture changed
	virtual void present (bool force) = 0;		// show it (a platform may skip frames unless forced)
	virtual void pumpEvents () = 0;			// input and window events -> pushKey / setMouse / pushEvent
	virtual bool stopRequested () = 0;		// the window was closed, the app must exit
	virtual unsigned nowMs () = 0;
	virtual void sleepRaw (int ms) = 0;
	virtual bool keyHeld (int key) { (void) key; return false; }	// K_* or a Latin-1 character
	virtual bool soundReady () { return true; }	// the sound output can be used (PLAY "MB")
	virtual void endSound () {}			// the program ended: release the output
	virtual void leaveFullscreen () {}

	// ---- the state -----------------------------------------------------------------------------------
	bool win, windowCmd, textUsed;			// the window exists; WINDOW was used; text was shown
	int mode, W, H, cellH, sx, sy, ncol, npages;
	unsigned *pg[8]; int apage, vpage;
	unsigned pal[256];
	int cols, rows, crow, ccol, vpTop, vpBot;	// text cursor (0-based) and VIEW PRINT rows
	unsigned char *tchar, *tattr;			// the text cells (SCREEN () function)
	unsigned fg, bg, gfg;				// text colours, graphics default colour
	bool clipOn; int cx1, cy1, cx2, cy2;
	char title[64];
	enum { KEYQ = 64, EVQ = 64 };
	volatile long keys[KEYQ]; volatile int kh, kt;	// (one producer, one consumer)
	volatile int mx, my, mb;			// the mouse, in the program's pixels
	volatile bool stopKey;				// Ctrl+C (QBasic's Ctrl+Break)
	volatile int evq[EVQ]; volatile int eh, et;	// control events

	ScreenHost () : win (false), windowCmd (false), textUsed (false), mode (0), W (640), H (400), cellH (16), sx (1), sy (1),
		ncol (256), npages (8), apage (0), vpage (0), cols (80), rows (25), crow (0), ccol (0), vpTop (0), vpBot (24),
		tchar (0), tattr (0), fg (7), bg (0), gfg (15), clipOn (false), cx1 (0), cy1 (0), cx2 (0), cy2 (0),
		kh (0), kt (0), mx (0), my (0), mb (0), stopKey (false), eh (0), et (0)
	{
		title[0] = 0;
		for (int i = 0; i < 8; i++) pg[i] = 0;
		vgaPalette (pal);
	}
	virtual ~ScreenHost () { for (int i = 0; i < 8; i++) delete [] pg[i]; delete [] tchar; delete [] tattr; }

	// ---- input from the platform ------------------------------------------------------------------
	void pushKey (long k)
	{
		if (k == 3) { stopKey = true; return; }			// Ctrl+C
		int n = (kt + 1) % KEYQ;
		if (n != kh) { keys[kt] = k; kt = n; }
	}
	bool peekKey (long *k) { if (kh == kt) return false; *k = keys[kh]; return true; }
	bool popKey (long *k) { if (kh == kt) return false; *k = keys[kh]; kh = (kh + 1) % KEYQ; return true; }
	void setMouse (int x, int y, int b)				// x < 0: the buttons only
	{
		if (x >= 0) { mx = x < 0 ? 0 : x > W - 1 ? W - 1 : x; my = y < 0 ? 0 : y > H - 1 ? H - 1 : y; }
		mb = b;
	}
	void pushEvent (int id) { int n = (et + 1) % EVQ; if (n != eh) { evq[et] = id; et = n; } }
	bool stopped () { return stopKey || stopRequested (); }

	// ---- pages -------------------------------------------------------------------------------------
	unsigned rgb (int c, unsigned def)
	{
		if (c < 0) return def;
		if (c & 0x1000000) return (unsigned) c & 0xFFFFFF;
		return pal[(unsigned) c % (unsigned) ncol];
	}
	unsigned *db () { return pg[apage]; }
	const unsigned *visible () { return pg[vpage] ? pg[vpage] : pg[0]; }
	void allocPages ()
	{
		for (int i = 0; i < 8; i++) { delete [] pg[i]; pg[i] = 0; }
		pg[0] = new unsigned[W * H];
		for (int i = 0; i < W * H; i++) pg[0][i] = rgb (bg, 0);
		apage = vpage = 0;
		delete [] tchar; delete [] tattr;
		cols = W / 8; rows = H / cellH;
		tchar = new unsigned char[cols * rows]; tattr = new unsigned char[cols * rows];
		for (int i = 0; i < cols * rows; i++) { tchar[i] = ' '; tattr[i] = (unsigned char) ((bg << 4) | (fg & 15)); }
		vpTop = 0; vpBot = rows - 1; crow = ccol = 0;
	}
	unsigned *page (int i)
	{
		if (i < 0 || i >= npages) return 0;
		if (!pg[i]) { pg[i] = new unsigned[W * H]; for (int k = 0; k < W * H; k++) pg[i][k] = rgb (bg, 0); }
		return pg[i];
	}
	bool ensureWindow ()
	{
		if (win) return true;
		if (!openWindow (W * sx, H * sy)) return false;
		win = true;
		allocPages ();
		pageChanged ();
		markDirty ();
		present (true);
		return true;
	}
	void dirty () { if (win) markDirty (); }
	void pump ()
	{
		bgTick ();
		pumpEvents ();
		if (win) present (false);
	}
	void fillRect (int x, int y, int w, int h, unsigned c, bool clip = false)
	{
		if (!win) return;
		int l = 0, t = 0, r = W - 1, b = H - 1;
		if (clip && clipOn) { l = cx1; t = cy1; r = cx2; b = cy2; }
		if (x < l) { w -= l - x; x = l; }
		if (y < t) { h -= t - y; y = t; }
		if (x + w - 1 > r) w = r - x + 1;
		if (y + h - 1 > b) h = b - y + 1;
		unsigned *d = db ();
		for (int j = 0; j < h; j++) { unsigned *p = d + (long) (y + j) * W + x; for (int i = 0; i < w; i++) p[i] = c; }
	}

	// ---- text screen ---------------------------------------------------------------------------------
	void scroll ()
	{
		int fh = cellH;
		unsigned *p = db ();
		for (int y = vpTop * fh; y < vpBot * fh; y++)
			for (int x = 0; x < W; x++) p[(long) y * W + x] = p[(long) (y + fh) * W + x];
		fillRect (0, vpBot * fh, W, fh, rgb (bg, 0));
		for (int r = vpTop; r < vpBot; r++) for (int c = 0; c < cols; c++) { tchar[r * cols + c] = tchar[(r + 1) * cols + c]; tattr[r * cols + c] = tattr[(r + 1) * cols + c]; }
		for (int c = 0; c < cols; c++) { tchar[vpBot * cols + c] = ' '; tattr[vpBot * cols + c] = (unsigned char) (bg << 4); }
	}
	void newline () { ccol = 0; if (++crow > vpBot) { scroll (); crow = vpBot; } }
	// A code page 437 character from the built-in fonts (basfont.h), plotted pixel by pixel:
	// 8 x 8, 8 x 14 or 8 x 16 after the mode's cell height (QBasic's). Transparent background.
	void glyph (int x, int y, unsigned char c, unsigned col, int h = 0)
	{
		if (!h) h = cellH <= 8 ? 8 : cellH <= 14 ? 14 : 16;
		const unsigned char *g = (h == 8 ? basFont8 : h == 14 ? basFont14 : basFont16) + c * h;
		unsigned *d = db ();
		for (int r = 0; r < h; r++)
		{
			int py = y + r;
			if (py < 0) continue;
			if (py >= H) break;
			unsigned bits = g[r];
			for (int i = 0; i < 8 && bits; i++, bits = (bits << 1) & 0xFF)
				if ((bits & 0x80) && x + i >= 0 && x + i < W) d[(long) py * W + x + i] = col;
		}
	}
	void putch (char c)
	{
		if (c == '\n') { newline (); return; }
		if (c == '\r') { ccol = 0; return; }
		if (c == '\t') { do putch (' '); while (ccol % 8); return; }
		if (ccol >= cols) newline ();
		int x = ccol * 8, y = crow * cellH;
		fillRect (x, y, 8, cellH, rgb (bg, 0));
		glyph (x, y, (unsigned char) c, rgb (fg, 0xAAAAAA));
		tchar[crow * cols + ccol] = (unsigned char) c; tattr[crow * cols + ccol] = (unsigned char) (((bg & 15) << 4) | (fg & 15));
		ccol++;
	}
	void out (const char *s, int n) override
	{
		if (!ensureWindow ()) return;
		textUsed = true;
		for (int i = 0; i < n; i++) putch (s[i]);
		dirty ();
	}
	int inputLine (char *buf, int cap) override
	{
		if (!ensureWindow ()) return -1;
		textUsed = true;
		int n = 0;
		unsigned blink = nowMs ();
		for (;;)
		{
			// caret: an underline at the cursor
			int cx = ccol * 8, cy = crow * cellH;
			bool on = ((nowMs () - blink) / 500) % 2 == 0;
			fillRect (cx, cy + cellH - 2, 8, 2, on ? rgb (fg, 0xAAAAAA) : rgb (bg, 0));
			dirty ();
			pump ();
			if (stopped ()) return -1;
			long k;
			while (popKey (&k))
			{
				fillRect (cx, cy + cellH - 2, 8, 2, rgb (bg, 0));
				if (k == K_ENTER || k == '\n') { buf[n] = 0; newline (); dirty (); return n; }
				if (k == K_BACKSPACE)
				{
					if (n > 0)
					{
						n--;
						if (ccol == 0 && crow > 0) { crow--; ccol = cols; }
						ccol--;
						fillRect (ccol * 8, crow * cellH, 8, cellH, rgb (bg, 0));
					}
				}
				else if (((k >= 32 && k < 127) || (k >= 0xA0 && k <= 0xFF)) && n < cap - 1)
				{ char c = (char) basLatin1To437[k]; buf[n++] = c; putch (c); }	// keys are Latin-1
				blink = nowMs ();
				cx = ccol * 8; cy = crow * cellH;
			}
			sleepRaw (10);
		}
	}
	// A key as INKEY$ gives it: 1 char, or 0 + a scan code for the special keys.
	static int keyString (long k, char *o)
	{
		int code = 0;
		switch (k)
		{
		case K_UP: code = 72; break; case K_DOWN: code = 80; break;
		case K_LEFT: code = 75; break; case K_RIGHT: code = 77; break;
		case K_HOME: code = 71; break; case K_END: code = 79; break;
		case K_PGUP: code = 73; break; case K_PGDN: code = 81; break;
		case K_DEL: code = 83; break;
		}
		if (k >= K_F1 && k <= K_F1 + 9) code = 59 + (int) (k - K_F1);
		if (k == K_F1 + 10) code = 133;
		if (k == K_F1 + 11) code = 134;
		if (code) { o[0] = 0; o[1] = (char) code; return 2; }
		if (k == K_BACKSPACE) { o[0] = 8; return 1; }
		if (k == K_ENTER) { o[0] = 13; return 1; }
		if (k > 0 && k < 256) { o[0] = (char) basLatin1To437[k]; return 1; }	// Latin-1 -> CP437
		return 0;
	}
	int inkey (char *o) override
	{
		if (!win) return 0;
		pump ();
		long k;
		while (popKey (&k)) { int n = keyString (k, o); if (n) return n; }
		return 0;
	}
	int keyPending (char *o) override
	{
		if (!win) return 0;
		long k;
		while (peekKey (&k)) { int n = keyString (k, o); if (n) return n; popKey (&k); }
		return 0;
	}
	// KEYDOWN(k$): k$ as INKEY$ gives it (CHR$(0) + "K" = Left, CHR$(27) = Esc, "a", " ") or a
	// name (LEFT RIGHT UP DOWN SPACE ENTER ESC ...). 0 without a window.
	bool keyDown (const char *k, int n) override
	{
		if (!win || n <= 0) return false;
		pump ();
		int key = 0;
		if (n == 2 && k[0] == 0)
		{
			switch (k[1])
			{
			case 72: key = K_UP; break; case 80: key = K_DOWN; break;
			case 75: key = K_LEFT; break; case 77: key = K_RIGHT; break;
			case 71: key = K_HOME; break; case 79: key = K_END; break;
			case 73: key = K_PGUP; break; case 81: key = K_PGDN; break;
			case 83: key = K_DEL; break;
			}
		}
		else if (n == 1) key = (unsigned char) k[0] == 13 ? (int) K_ENTER : (int) (unsigned char) k[0];
		else
		{
			static const struct { const char *name; int key; } names[] = {
				{ "LEFT", K_LEFT }, { "RIGHT", K_RIGHT }, { "UP", K_UP }, { "DOWN", K_DOWN },
				{ "SPACE", ' ' }, { "ENTER", K_ENTER }, { "ESC", 27 }, { "TAB", K_TAB },
				{ "BACKSPACE", K_BACKSPACE }, { "HOME", K_HOME }, { "END", K_END },
				{ "PGUP", K_PGUP }, { "PGDN", K_PGDN }, { "DEL", K_DEL }, { 0, 0 } };
			for (int i = 0; names[i].name && !key; i++)
			{
				const char *p = names[i].name; int j = 0;
				while (j < n && p[j] && (k[j] == p[j] || k[j] == p[j] + ('a' - 'A'))) j++;
				if (j == n && p[j] == 0) key = names[i].key;
			}
		}
		if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
		return key != 0 && keyHeld (key);
	}
	void cls (int m) override
	{
		if (!ensureWindow ()) return;
		if (m == 1 || (m < 0 && clipOn)) { fillRect (0, 0, W, H, rgb (bg, 0), true); dirty (); return; }
		if (m == 2)
		{
			fillRect (0, vpTop * cellH, W, (vpBot - vpTop + 1) * cellH, rgb (bg, 0));
			for (int r = vpTop; r <= vpBot; r++) for (int c = 0; c < cols; c++) tchar[r * cols + c] = ' ';
			crow = vpTop; ccol = 0; dirty (); return;
		}
		fillRect (0, 0, W, H, rgb (bg, 0));
		for (int i = 0; i < cols * rows; i++) tchar[i] = ' ';
		crow = vpTop; ccol = 0; dirty ();
	}
	void locate (int r, int c) override
	{
		if (r > 0) { crow = r - 1; if (crow < vpTop) crow = vpTop; if (crow > vpBot) crow = vpBot; }
		if (c > 0) ccol = c - 1 < cols ? c - 1 : cols - 1;
	}
	int column () override { return ccol + 1; }
	int row () override { return crow + 1; }
	void color (int f, int b) override { if (f >= 0) { fg = (unsigned) f; gfg = (unsigned) f; } if (b >= 0) bg = (unsigned) b; }
	int width () override { return cols; }
	int screenChar (int r, int c, bool colorf) override
	{
		if (!win || r < 1 || r > rows || c < 1 || c > cols) return 32;
		return colorf ? tattr[(r - 1) * cols + c - 1] : tchar[(r - 1) * cols + c - 1];
	}
	void viewPrint (int t, int b) override
	{
		if (!ensureWindow ()) return;
		if (t <= 0 || b <= 0) { vpTop = 0; vpBot = rows - 1; }
		else { vpTop = t - 1 < rows ? t - 1 : rows - 1; vpBot = b - 1 < rows ? b - 1 : rows - 1; if (vpBot < vpTop) vpBot = vpTop; }
		crow = vpTop; ccol = 0;
	}

	// ---- graphics ----------------------------------------------------------------------------------
	void screen (int m, int ap, int vp) override
	{
		const ModeInfo *mi = modeInfo (m);
		if (!mi) mi = modeInfo (0);
		if (mi->mode != mode || !win)
		{
			mode = mi->mode; ncol = mi->colors; npages = mi->pages; cellH = mi->cellH;
			sx = mi->sx; sy = mi->sy;
			if (mode == 1) { pal[0] = 0; pal[1] = 0x55FFFF; pal[2] = 0xFF55FF; pal[3] = 0xFFFFFF; }
			else if (mode == 2 || mode == 11) { pal[0] = 0; pal[1] = 0xFFFFFF; }
			else if (mode == 10) { pal[0] = 0; pal[1] = 0xAAAAAA; pal[2] = 0xAAAAAA; pal[3] = 0xFFFFFF; }
			else vgaPalette (pal);
			gfg = ncol == 2 ? 1 : ncol == 4 ? 3 : 15;
			fg = mode == 0 ? 7 : gfg; bg = 0;
			clipOn = false;
			resize (mi->w, mi->h);
			ensureWindow ();
		}
		if (ap >= 0 && page (ap)) apage = ap;
		if (vp >= 0 && page (vp)) { vpage = vp; pageChanged (); dirty (); }
	}
	void screenSize (int *w, int *h) override { *w = W; *h = H; }
	void resize (int w, int h)
	{
		W = w; H = h;
		if (!win) return;
		allocPages ();
		resizeWindow (w * sx, h * sy);
		pageChanged ();
		dirty ();
	}
	void setClip (int x1, int y1, int x2, int y2) override
	{
		clipOn = x1 >= 0;
		cx1 = x1 < 0 ? 0 : x1; cy1 = y1 < 0 ? 0 : y1; cx2 = x2 >= W ? W - 1 : x2; cy2 = y2 >= H ? H - 1 : y2;
	}
	bool inClip (int x, int y) { return x >= 0 && y >= 0 && x < W && y < H && (!clipOn || (x >= cx1 && x <= cx2 && y >= cy1 && y <= cy2)); }
	void plot (int x, int y, unsigned c) { if (inClip (x, y)) db ()[(long) y * W + x] = c; }
	void pset (int x, int y, int c) override { if (!ensureWindow ()) return; plot (x, y, rgb (c, rgb (gfg, 0xFFFFFF))); dirty (); }
	int toIndex (unsigned p)
	{
		for (int i = 0; i < ncol; i++) if (pal[i] == p) return i;
		return -1;
	}
	int nearest (unsigned p)
	{
		int e = toIndex (p); if (e >= 0) return e;
		int best = 0; long bd = -1;
		for (int i = 0; i < ncol; i++)
		{
			long dr = (long) ((p >> 16) & 255) - (long) ((pal[i] >> 16) & 255), dg = (long) ((p >> 8) & 255) - (long) ((pal[i] >> 8) & 255), dbb = (long) (p & 255) - (long) (pal[i] & 255);
			long d = dr * dr + dg * dg + dbb * dbb;
			if (bd < 0 || d < bd) { bd = d; best = i; }
		}
		return best;
	}
	int point (int x, int y) override
	{
		if (!win || x < 0 || y < 0 || x >= W || y >= H) return -1;
		unsigned p = db ()[(long) y * W + x];
		int i = toIndex (p);
		return i >= 0 ? i : (int) (p | 0x1000000);
	}
	void line (int x1, int y1, int x2, int y2, int c, int box, int style) override
	{
		if (!ensureWindow ()) return;
		unsigned col = rgb (c, rgb (gfg, 0xFFFFFF));
		if (box)
		{
			int l = x1 < x2 ? x1 : x2, r = x1 < x2 ? x2 : x1, t = y1 < y2 ? y1 : y2, b = y1 < y2 ? y2 : y1;
			if (box == 2) fillRect (l, t, r - l + 1, b - t + 1, col, true);
			else { line (l, t, r, t, c, 0, style); line (r, t, r, b, c, 0, style); line (r, b, l, b, c, 0, style); line (l, b, l, t, c, 0, style); }
		}
		else
		{
			unsigned mask = style < 0 ? 0xFFFF : (unsigned) style & 0xFFFF; int bit = 15;
			int dx = x2 > x1 ? x2 - x1 : x1 - x2, dy = y2 > y1 ? y2 - y1 : y1 - y2;
			int sx2 = x1 < x2 ? 1 : -1, sy2 = y1 < y2 ? 1 : -1, e = dx - dy;
			for (int guard = 0; guard < 100000; guard++)
			{
				if ((mask >> bit) & 1) plot (x1, y1, col);
				bit = bit ? bit - 1 : 15;
				if (x1 == x2 && y1 == y2) break;
				int e2 = 2 * e;
				if (e2 > -dy) { e -= dy; x1 += sx2; }
				if (e2 < dx) { e += dx; y1 += sy2; }
			}
		}
		dirty ();
	}
	void circle (int cx, int cy, int r, int c, int fill) override
	{
		if (!ensureWindow () || r < 0) return;
		unsigned col = rgb (c, rgb (gfg, 0xFFFFFF));
		int x = r, y = 0, e = 1 - r;
		while (x >= y)
		{
			if (fill)
			{
				fillRect (cx - x, cy + y, 2 * x + 1, 1, col, true); fillRect (cx - x, cy - y, 2 * x + 1, 1, col, true);
				fillRect (cx - y, cy + x, 2 * y + 1, 1, col, true); fillRect (cx - y, cy - x, 2 * y + 1, 1, col, true);
			}
			else
			{
				plot (cx + x, cy + y, col); plot (cx - x, cy + y, col); plot (cx + x, cy - y, col); plot (cx - x, cy - y, col);
				plot (cx + y, cy + x, col); plot (cx - y, cy + x, col); plot (cx + y, cy - x, col); plot (cx - y, cy - x, col);
			}
			y++;
			if (e < 0) e += 2 * y + 1; else { x--; e += 2 * (y - x) + 1; }
		}
		dirty ();
	}
	// PAINT: fill from (x, y) up to the border colour (scanline flood fill).
	void paint (int x, int y, int c, int border) override
	{
		if (!ensureWindow () || !inClip (x, y)) return;
		unsigned fc = rgb (c, rgb (gfg, 0xFFFFFF)), bc = rgb (border, fc);
		unsigned *d = db ();
		int l = clipOn ? cx1 : 0, r = clipOn ? cx2 : W - 1, t = clipOn ? cy1 : 0, b = clipOn ? cy2 : H - 1;
		auto stopAt = [&] (int px, int py) { unsigned p = d[(long) py * W + px]; return p == bc || p == fc; };
		if (stopAt (x, y)) return;
		int cap = 4096, n = 0;
		int *stk = new int[cap * 2];
		stk[n * 2] = x; stk[n * 2 + 1] = y; n++;
		while (n > 0)
		{
			n--;
			int px = stk[n * 2], py = stk[n * 2 + 1];
			if (stopAt (px, py)) continue;
			int x0 = px, x1 = px;
			while (x0 > l && !stopAt (x0 - 1, py)) x0--;
			while (x1 < r && !stopAt (x1 + 1, py)) x1++;
			for (int i = x0; i <= x1; i++) d[(long) py * W + i] = fc;
			for (int dir = -1; dir <= 1; dir += 2)
			{
				int ny = py + dir;
				if (ny < t || ny > b) continue;
				bool in = false;
				for (int i = x0; i <= x1; i++)
				{
					bool open = !stopAt (i, ny);
					if (open && !in)
					{
						if (n >= cap) { int nc = cap * 2; int *ns = new int[nc * 2]; for (int k = 0; k < n * 2; k++) ns[k] = stk[k]; delete [] stk; stk = ns; cap = nc; }
						stk[n * 2] = i; stk[n * 2 + 1] = ny; n++;
					}
					in = open;
				}
			}
		}
		delete [] stk;
		dirty ();
	}
	void readRect (int x, int y, int w, int h, int *o, bool raw) override
	{
		unsigned *d = win ? db () : 0;
		for (int j = 0; j < h; j++) for (int i = 0; i < w; i++)
		{
			int px = x + i, py = y + j;
			unsigned p = d && px >= 0 && py >= 0 && px < W && py < H ? d[(long) py * W + px] : 0;
			o[j * w + i] = raw ? (int) p : nearest (p);
		}
	}
	void writeRect (int x, int y, int w, int h, const int *in, bool raw) override
	{
		if (!ensureWindow ()) return;
		for (int j = 0; j < h; j++) for (int i = 0; i < w; i++)
		{
			int v = in[j * w + i];
			plot (x + i, y + j, raw ? (unsigned) v & 0xFFFFFF : pal[(unsigned) v % (unsigned) ncol]);
		}
		dirty ();
	}
	// PALETTE: change a colour -- what is drawn with it changes too.
	void recolor (unsigned from, unsigned to)
	{
		if (from == to) return;
		for (int k = 0; k < 8; k++) if (pg[k]) for (int i = 0; i < W * H; i++) if (pg[k][i] == from) pg[k][i] = to;
	}
	void palette (int attr, int c) override
	{
		if (!ensureWindow ()) return;
		if (attr < 0)
		{
			unsigned def[256]; vgaPalette (def);
			for (int i = 0; i < ncol; i++) { recolor (pal[i], def[i]); pal[i] = def[i]; }
		}
		else if (attr < 256) { recolor (pal[attr], (unsigned) c & 0xFFFFFF); pal[attr] = (unsigned) c & 0xFFFFFF; }
		dirty ();
	}
	void pcopy (int s, int d) override
	{
		if (!ensureWindow ()) return;
		unsigned *a = page (s), *b = page (d);
		if (!a || !b || a == b) return;
		for (int i = 0; i < W * H; i++) b[i] = a[i];
		dirty ();
	}
	void drawText (int x, int y, const char *s, int c) override
	{
		if (!ensureWindow ()) return;
		unsigned col = rgb (c, rgb (gfg, 0xFFFFFF));
		for (; *s; s++, x += 8) glyph (x, y, (unsigned char) *s, col, 16);	// always 8 x 16
		dirty ();
	}
	int mouse (int what) override
	{
		if (!win) return 0;
		pump ();
		return what == 0 ? mx : what == 1 ? my : mb;
	}

	// ---- time --------------------------------------------------------------------------------------
	void sleepMs (int ms) override
	{
		unsigned t0 = nowMs ();
		while ((int) (nowMs () - t0) < ms)
		{
			if (win) { pump (); if (stopped ()) return; } else bgTick ();
			int left = ms - (int) (nowMs () - t0);
			sleepRaw (left > 10 ? 10 : (left > 0 ? left : 1));
		}
		if (win) pump ();
	}
	bool poll () override
	{
		if (!win) { bgTick (); pumpEvents (); return !stopped (); }
		pump ();
		return !stopped ();
	}

	// ---- PLAY "MB": a 32-note queue on voice 0, advanced by bgTick () from pump () / sleepMs () ------
	struct BgNote { float freq; unsigned short on, off; unsigned char wave; };
	enum { BGQ = 32 };
	BgNote bgq[BGQ]; int bgHead = 0, bgCount = 0, bgPhase = 0; unsigned bgUntil = 0, bgGap = 0;	// phase 0 idle 1 on 2 off
	bool bgNote (double freq, int on, int off, int wave) override
	{
		if (freq > 0 && !soundReady ()) return false;
		while (bgCount >= BGQ)				// full: wait (QBasic does the same)
		{
			sleepRaw (5); bgTick (); if (win) pump ();
			if (stopped ()) return true;
		}
		BgNote &b = bgq[(bgHead + bgCount) % BGQ];
		b.freq = (float) freq; b.on = (unsigned short) (on < 0 ? 0 : on > 65535 ? 65535 : on);
		b.off = (unsigned short) (off < 0 ? 0 : off > 65535 ? 65535 : off); b.wave = (unsigned char) wave;
		bgCount++;
		bgTick ();
		return true;
	}
	int bgNotes () override { bgTick (); return bgCount + (bgPhase == 1 ? 1 : 0); }
	void bgTick ()
	{
		if (!bgPhase && !bgCount) return;
		unsigned now = nowMs ();
		for (int guard = 0; guard < BGQ + 2; guard++)
		{
			if (bgPhase && (int) (now - bgUntil) < 0) return;
			if (bgPhase == 1) { note (0, 0, 0, 0); bgPhase = 2; bgUntil += bgGap; continue; }
			unsigned start = bgPhase ? bgUntil : now;	// back to back, unless the queue ran dry
			if ((int) (now - start) > 100) start = now;	// (or we fell far behind)
			bgPhase = 0;
			if (!bgCount) return;
			BgNote b = bgq[bgHead]; bgHead = (bgHead + 1) % BGQ; bgCount--;
			bgGap = b.off;
			if (b.freq > 0 && b.on > 0) { note (0, b.freq, b.wave, 200); bgPhase = 1; bgUntil = start + b.on; }
			else { bgPhase = 2; bgUntil = start + b.on + b.off; }
		}
	}

	// ---- controls' events; WINDOW -----------------------------------------------------------------
	int event (bool wait) override
	{
		if (!ensureWindow ()) return -1;
		for (;;)
		{
			pump ();
			if (stopped ()) return -1;
			if (eh != et) { int id = evq[eh]; eh = (eh + 1) % EVQ; return id; }
			if (!wait) return 0;
			sleepRaw (10);
		}
	}
	void window (const char *t, int w, int h) override
	{
		windowCmd = true;
		if (!win)
		{
			int i = 0; for (; t[i] && i < (int) sizeof title - 1; i++) title[i] = t[i];
			title[i] = 0;
			if (w > 0 && h > 0) { W = w; H = h; }
			ensureWindow ();
			return;
		}
		if (w > 0 && h > 0 && (w != W || h != H)) { sx = sy = 1; resize (w, h); }
		fillRect (0, 0, W, H, rgb (bg, 0)); dirty ();
	}

	// End of the program: the background music plays out; a text-only window program keeps its
	// window until a key.
	void finished (bool error) override
	{
		while (!error && (bgCount || bgPhase) && !stopped ()) { sleepRaw (10); bgTick (); if (win) pump (); }
		endSound ();
		leaveFullscreen ();
		if (!win || error || windowCmd || !textUsed || stopped ()) return;
		const char *msg = "Press any key to continue";
		unsigned save = fg; fg = 15;
		crow = rows - 1; ccol = 0;
		fillRect (0, crow * cellH, W, cellH, rgb (bg, 0));
		for (int i = 0; msg[i]; i++) putch (msg[i]);
		fg = save;
		dirty ();
		long k;
		while (!stopRequested ()) { pump (); if (popKey (&k)) break; sleepRaw (10); }
	}
};

}	// namespace bas

#endif
