//
// basic/runtime.cpp -- /bin/basic, the Onyx BASIC runtime:  basic <program.bas> [args]
//
// Compiles the program (basic/bascomp.cpp) and runs it on the VM (basic/basvm.cpp) with an
// Onyx bas::Host:
//   * Started from a terminal (it has a stdout): PRINT / INPUT use the console -- until the
//     program opens its window (SCREEN, graphics, WINDOW, a control): then the window.
//   * Started as an app (a .app bundle with main.bas, the qbasic editor's Run, the File
//     Viewer): the first PRINT opens the window.
// The window is a QBasic-like screen (80 x 25 text cells of the kernel font = 640 x 400 by
// default; SCREEN 12 = 640 x 480, SCREEN 13 = 320 x 200) that text and graphics share, with
// wtk controls (BUTTON, TEXTBOX, ...) on top. The program's folder becomes the current
// directory, so it finds its files by relative names.
//
#include "kapi.h"
#include "applib.h"
#include "notify.h"
#include "wtk/wtk.h"
#include "basic/bas.h"

using namespace wtk;

static const unsigned EGA16[16] = {
	0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
	0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF };

static int slen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void scpy (char *d, const char *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }

// The VGA default palette: the 16 EGA colours, 16 greys, 9 rings of 24 hues, black.
static void vgaPalette (unsigned *p)
{
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

// QBasic's screen modes: size, text cell height, pages, colours, display scale.
struct ModeInfo { int mode, w, h, cellH, pages, colors, sx, sy; };
static const ModeInfo MODES[] = {
	{ 0, 640, 400, 16, 8, 256, 1, 1 }, { 1, 320, 200, 8, 1, 4, 2, 2 }, { 2, 640, 200, 8, 1, 2, 1, 2 },
	{ 7, 320, 200, 8, 8, 16, 2, 2 }, { 8, 640, 200, 8, 4, 16, 1, 2 }, { 9, 640, 350, 14, 2, 16, 1, 1 },
	{ 10, 640, 350, 14, 2, 4, 1, 1 }, { 11, 640, 480, 16, 1, 2, 1, 1 }, { 12, 640, 480, 16, 1, 16, 1, 1 },
	{ 13, 320, 200, 8, 1, 256, 2, 2 }, { -1, 0, 0, 0, 0, 0, 0, 0 } };
static const ModeInfo *modeInfo (int m) { for (int i = 0; MODES[i].mode >= 0; i++) if (MODES[i].mode == m) return &MODES[i]; return 0; }

class OnyxHost;
static OnyxHost *g_host = 0;
static void on_control (Widget &w);

// ---- the screen window ---------------------------------------------------------------------------
class ScreenRoot : public Root
{
public:
	const unsigned *vis; int vw, vh, sx, sy;	// the visible page and its display scale
	int fw, fh;
	enum { KEYQ = 64 };
	long keys[KEYQ]; int kh, kt;
	int mx, my, mb;
	bool stop;				// Ctrl+C (QBasic's Ctrl+Break): end the program
	bool fs; int fsOx, fsOy, fsW, fsH;	// full screen: where the picture is
	ScreenRoot (int w, int h, const char *title) : Root (w, h, title), vis (0), vw (w), vh (h), sx (1), sy (1), kh (0), kt (0),
		mx (0), my (0), mb (0), stop (false), fs (false), fsOx (0), fsOy (0), fsW (1), fsH (1)
	{
		fw = kapi_font_width (); if (fw < 1) fw = 8;
		fh = kapi_font_height (); if (fh < 1) fh = 16;
	}
	void onDraw () override
	{
		if (!vis) return;
		for (int y = 0; y < height; y++)
		{
			unsigned *d = canvas.px + (long) y * canvas.stride;
			const unsigned *s = vis + (long) (y / sy) * vw;
			if (sx == 1) for (int x = 0; x < width; x++) d[x] = s[x];
			else for (int x = 0; x < width; x++) d[x] = s[x / sx];
		}
	}
	bool onKey (long k) override
	{
		if (k == 3) { stop = true; return true; }			// Ctrl+C
		int n = (kt + 1) % KEYQ;
		if (n != kh) { keys[kt] = k; kt = n; }
		return true;
	}
	bool onMouse (int x, int y, int bl, int br, int bm, int) override
	{
		if (x >= 0)
		{
			// physical -> the program's pixels: / the zoom (320-wide modes are shown 2x) or, full
			// screen, x the virtual / physical size ratio; kept on the picture (not the black bars)
			if (fs) { mx = (x - fsOx) * vw / fsW; my = (y - fsOy) * vh / fsH; }
			else { mx = x / sx; my = y / sy; }
			mx = mx < 0 ? 0 : mx > vw - 1 ? vw - 1 : mx;
			my = my < 0 ? 0 : my > vh - 1 ? vh - 1 : my;
		}
		mb = (bl ? 1 : 0) | (br ? 2 : 0) | (bm ? 4 : 0);
		return true;
	}
	bool peekKey (long *k) { if (kh == kt) return false; *k = keys[kh]; return true; }
	bool popKey (long *k) { if (kh == kt) return false; *k = keys[kh]; kh = (kh + 1) % KEYQ; return true; }
};

// ---- the host --------------------------------------------------------------------------------------
class OnyxHost : public bas::Host
{
public:
	ScreenRoot *root; bool console, windowCmd, textUsed;
	int mode, W, H, cellH, sx, sy, ncol, npages;
	unsigned *pg[8]; int apage, vpage;
	unsigned pal[256];
	int cols, rows, crow, ccol, vpTop, vpBot;	// text cursor (0-based) and VIEW PRINT rows
	unsigned char *tchar, *tattr;			// the text cells (SCREEN () function)
	unsigned fg, bg, gfg;				// text colours, graphics default colour
	bool clipOn; int cx1, cy1, cx2, cy2;
	unsigned lastPresent;
	char title[64], args[256];
	double tBase; unsigned tTicks0;
	unsigned *fsBuf; int fsW, fsH;			// full screen back buffer
	enum { MAXCTL = 128 };
	Widget *ctl[MAXCTL]; int ctlKind[MAXCTL]; int nctl;
	char *ddItems[MAXCTL]; const char *ddPtr[MAXCTL][32];
	enum { EVQ = 64 };
	int evq[EVQ]; int eh, et;

	OnyxHost () : root (0), console (false), windowCmd (false), textUsed (false), mode (0), W (640), H (400), cellH (16), sx (1), sy (1),
		ncol (256), npages (8), apage (0), vpage (0), cols (80), rows (25), crow (0), ccol (0), vpTop (0), vpBot (24), tchar (0), tattr (0),
		fg (7), bg (0), gfg (15), clipOn (false), cx1 (0), cy1 (0), cx2 (0), cy2 (0), lastPresent (0), fsBuf (0), fsW (0), fsH (0), nctl (0), eh (0), et (0)
	{
		title[0] = 0; args[0] = 0;
		int h = 0, m = 0, s = 0;
		kapi_get_datetime (0, 0, 0, &h, &m, &s);
		tBase = h * 3600.0 + m * 60 + s; tTicks0 = kapi_get_ticks ();
		for (int i = 0; i < MAXCTL; i++) { ctl[i] = 0; ddItems[i] = 0; }
		for (int i = 0; i < 8; i++) pg[i] = 0;
		vgaPalette (pal);
	}

	unsigned rgb (int c, unsigned def)
	{
		if (c < 0) return def;
		if (c & 0x1000000) return (unsigned) c & 0xFFFFFF;
		return pal[(unsigned) c % (unsigned) ncol];
	}
	unsigned *db () { return pg[apage]; }

	// ---- the window: created on first need (text in app mode, graphics, controls, WINDOW) ------------
	void allocPages ()
	{
		for (int i = 0; i < 8; i++) { delete [] pg[i]; pg[i] = 0; }
		pg[0] = new unsigned[W * H];
		for (int i = 0; i < W * H; i++) pg[0][i] = rgb (bg, 0);
		apage = vpage = 0;
		delete [] tchar; delete [] tattr;
		cols = W / (root ? root->fw : 8); rows = H / cellH;
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
		if (root) return true;
		root = new ScreenRoot (W * sx, H * sy, title[0] ? title : "BASIC");
		if (root->canvas.px == 0) { delete root; root = 0; return false; }
		allocPages ();
		root->vis = pg[0]; root->vw = W; root->vh = H; root->sx = sx; root->sy = sy;
		root->attach ();				// (Root::run is not used: we pump ourselves)
		root->invalidate (true);
		present (true);
		return true;
	}
	bool windowText () { return root != 0 || !console; }

	void fillRect (int x, int y, int w, int h, unsigned c, bool clip = false)
	{
		if (!root) return;
		int l = 0, t = 0, r = W - 1, b = H - 1;
		if (clip && clipOn) { l = cx1; t = cy1; r = cx2; b = cy2; }
		if (x < l) { w -= l - x; x = l; }
		if (y < t) { h -= t - y; y = t; }
		if (x + w - 1 > r) w = r - x + 1;
		if (y + h - 1 > b) h = b - y + 1;
		unsigned *d = db ();
		for (int j = 0; j < h; j++) { unsigned *p = d + (long) (y + j) * W + x; for (int i = 0; i < w; i++) p[i] = c; }
	}
	void dirty () { if (root) root->invalidate (true); }
	void present (bool force = false)
	{
		if (!root) return;
		unsigned now = kapi_get_ticks ();
		if (!force && now - lastPresent < 2) return;	// ~50 Hz at most
		lastPresent = now;
		if (fsBuf) { blitFull (); kapi_present_fb (); return; }
		if (!root->valid) { root->draw (); kapi_present (); }
	}
	// Full screen: the visible page scaled into the display, proportions kept.
	void blitFull ()
	{
		const unsigned *v = pg[vpage] ? pg[vpage] : pg[0];
		int dw = W * sx, dh = H * sy;			// the displayed aspect
		int ow, oh;
		if ((long) fsW * dh <= (long) fsH * dw) { ow = fsW; oh = (int) ((long) fsW * dh / dw); }
		else { oh = fsH; ow = (int) ((long) fsH * dw / dh); }
		// A whole-number zoom when it fills nearly as much (1024 x 768: 320 x 200 at 3x, not
		// 3.2x): every pixel the same size, no uneven columns.
		int k = sx == sy ? ow / W : 0;
		if (k >= 1 && k * W * 100 >= ow * 85) { ow = k * W; oh = k * H; }
		int ox = (fsW - ow) / 2, oy = (fsH - oh) / 2;
		root->fsOx = ox; root->fsOy = oy; root->fsW = ow; root->fsH = oh; root->vw = W; root->vh = H;
		static int xmap[4096];
		for (int x = 0; x < ow && x < 4096; x++) xmap[x] = x * W / ow;
		for (int y = 0; y < oh; y++)
		{
			const unsigned *s = v + (long) (y * H / oh) * W;
			unsigned *d = fsBuf + (long) (oy + y) * fsW + ox;
			for (int x = 0; x < ow && x < 4096; x++) d[x] = s[xmap[x]];
		}
	}
	void pump ()
	{
		bgTick ();
		pump_events ();
		if (root) { if (!fsBuf) root->tooltipTick (); present (); }
	}
	bool stopped () { return (root && root->stop) || should_exit (); }

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
	void glyph (int x, int y, unsigned char c, unsigned col)
	{
		// the kernel font (fw x fh), squeezed to the mode's cell height
		static unsigned g[8 * 32];
		int fw = root->fw, fh = root->fh;
		if (fw > 8 || fh > 32) { char s[2] = { (char) c, 0 }; kapi_draw_text_buf (db (), W, H, x, y, s, col); return; }
		for (int i = 0; i < fw * fh; i++) g[i] = 0;
		char s[2] = { (char) c, 0 };
		kapi_draw_text_buf (g, fw, fh, 0, 0, s, 0xFFFFFF);
		unsigned *d = db ();
		for (int r = 0; r < cellH; r++)
		{
			int sr = r * fh / cellH;
			if (y + r >= H) break;
			for (int i = 0; i < fw && x + i < W; i++) if (g[sr * fw + i]) d[(long) (y + r) * W + x + i] = col;
		}
	}
	void putch (char c)
	{
		if (c == '\n') { newline (); return; }
		if (c == '\r') { ccol = 0; return; }
		if (c == '\t') { do putch (' '); while (ccol % 8); return; }
		if (ccol >= cols) newline ();
		int x = ccol * root->fw, y = crow * cellH;
		fillRect (x, y, root->fw, cellH, rgb (bg, 0));
		glyph (x, y, (unsigned char) c, rgb (fg, 0xAAAAAA));
		tchar[crow * cols + ccol] = (unsigned char) c; tattr[crow * cols + ccol] = (unsigned char) (((bg & 15) << 4) | (fg & 15));
		ccol++;
	}
	void out (const char *s, int n) override
	{
		if (!windowText ()) { kapi_stdout_write (s, (unsigned) n); for (int i = 0; i < n; i++) ccol = s[i] == '\n' ? 0 : ccol + 1; return; }
		if (!ensureWindow ()) return;
		textUsed = true;
		for (int i = 0; i < n; i++) putch (s[i]);
		dirty ();
	}
	int inputLine (char *buf, int cap) override
	{
		if (!windowText ())
		{
			int n = 0;
			for (;;)
			{
				char c;
				if (kapi_stdin_read (&c, 1) <= 0) return n ? n : -1;
				if (c == 4) return n ? n : -1;
				if (c == '\r') continue;
				if (c == '\n') break;
				if (n < cap - 1) buf[n++] = c;
			}
			buf[n] = 0; ccol = 0;
			return n;
		}
		if (!ensureWindow ()) return -1;
		textUsed = true;
		int n = 0;
		unsigned blink = 0;
		for (;;)
		{
			// caret: an underline at the cursor
			int cx = ccol * root->fw, cy = crow * cellH;
			bool on = ((kapi_get_ticks () - blink) / 50) % 2 == 0;
			fillRect (cx, cy + cellH - 2, root->fw, 2, on ? rgb (fg, 0xAAAAAA) : rgb (bg, 0));
			dirty ();
			pump ();
			if (stopped ()) return -1;
			long k;
			while (root->popKey (&k))
			{
				fillRect (cx, cy + cellH - 2, root->fw, 2, rgb (bg, 0));
				if (k == KEY_ENTER || k == '\n') { buf[n] = 0; newline (); dirty (); return n; }
				if (k == KEY_BACKSPACE || k == 8)
				{
					if (n > 0)
					{
						n--;
						if (ccol == 0 && crow > 0) { crow--; ccol = cols; }
						ccol--;
						fillRect (ccol * root->fw, crow * cellH, root->fw, cellH, rgb (bg, 0));
					}
				}
				else if (((k >= 32 && k < 127) || (k >= 0xA0 && k <= 0xFF)) && n < cap - 1) { buf[n++] = (char) k; putch ((char) k); }
				blink = kapi_get_ticks ();
				cx = ccol * root->fw; cy = crow * cellH;
			}
			kapi_msleep (10);
		}
	}
	// A key as INKEY$ gives it: 1 char, or 0 + a scan code for the special keys.
	static int keyString (long k, char *o)
	{
		int code = 0;
		switch (k)
		{
		case KEY_UP: code = 72; break; case KEY_DOWN: code = 80; break;
		case KEY_LEFT: code = 75; break; case KEY_RIGHT: code = 77; break;
		case KEY_HOME: code = 71; break; case KEY_END: code = 79; break;
		case KEY_PGUP: code = 73; break; case KEY_PGDN: code = 81; break;
		case KEY_DEL: code = 83; break;
		}
		if (k >= KEY_F1 && k <= KEY_F1 + 9) code = 59 + (int) (k - KEY_F1);
		if (k == KEY_F1 + 10) code = 133;
		if (k == KEY_F1 + 11) code = 134;
		if (code) { o[0] = 0; o[1] = (char) code; return 2; }
		if (k == KEY_BACKSPACE) { o[0] = 8; return 1; }
		if (k == KEY_ENTER) { o[0] = 13; return 1; }
		if (k > 0 && k < 256) { o[0] = (char) k; return 1; }
		return 0;
	}
	int inkey (char *o) override
	{
		if (!root)
		{
			if (!console) return 0;
			char c;
			if (kapi_kbd_ready () && kapi_stdin_read (&c, 1) > 0) { o[0] = c; return 1; }
			return 0;
		}
		pump ();
		long k;
		while (root->popKey (&k)) { int n = keyString (k, o); if (n) return n; }
		return 0;
	}
	int keyPending (char *o) override
	{
		if (!root) return 0;
		long k;
		while (root->peekKey (&k)) { int n = keyString (k, o); if (n) return n; root->popKey (&k); }
		return 0;
	}
	// KEYDOWN(k$): k$ as INKEY$ gives it (CHR$(0) + "K" = Left, CHR$(27) = Esc, "a", " ") or a
	// name (LEFT RIGHT UP DOWN SPACE ENTER ESC). Asks the kernel (ABI v48); 0 without a window.
	bool keyDown (const char *k, int n) override
	{
		if (!root || n <= 0) return false;
		pump ();
		int key = 0;
		if (n == 2 && k[0] == 0)
		{
			switch (k[1])
			{
			case 72: key = KEY_UP; break; case 80: key = KEY_DOWN; break;
			case 75: key = KEY_LEFT; break; case 77: key = KEY_RIGHT; break;
			case 71: key = KEY_HOME; break; case 79: key = KEY_END; break;
			case 73: key = KEY_PGUP; break; case 81: key = KEY_PGDN; break;
			case 83: key = KEY_DEL; break;
			}
		}
		else if (n == 1) key = (unsigned char) k[0] == 13 ? KEY_ENTER : (unsigned char) k[0];
		else
		{
			static const struct { const char *name; int key; } names[] = {
				{ "LEFT", KEY_LEFT }, { "RIGHT", KEY_RIGHT }, { "UP", KEY_UP }, { "DOWN", KEY_DOWN },
				{ "SPACE", ' ' }, { "ENTER", KEY_ENTER }, { "ESC", 27 }, { "TAB", KEY_TAB },
				{ "BACKSPACE", KEY_BACKSPACE }, { "HOME", KEY_HOME }, { "END", KEY_END },
				{ "PGUP", KEY_PGUP }, { "PGDN", KEY_PGDN }, { "DEL", KEY_DEL }, { 0, 0 } };
			for (int i = 0; names[i].name && !key; i++)
			{
				const char *p = names[i].name; int j = 0;
				while (j < n && p[j] && (k[j] == p[j] || k[j] == p[j] + ('a' - 'A'))) j++;
				if (j == n && p[j] == 0) key = names[i].key;
			}
		}
		return key != 0 && kapi_key_held (key) != 0;
	}
	void cls (int m) override
	{
		if (!windowText ()) { kapi_stdout_write ("\n", 1); return; }
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
		if (!root || r < 1 || r > rows || c < 1 || c > cols) return 32;
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
		if (mi->mode != mode || !root)
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
		if (vp >= 0 && page (vp)) { vpage = vp; if (root) root->vis = pg[vpage]; dirty (); }
	}
	void screenSize (int *w, int *h) override { *w = W; *h = H; }
	void resize (int w, int h)
	{
		W = w; H = h;
		if (!root) return;
		allocPages ();
		root->canvas.adopt (kapi_resize_window (w * sx, h * sy), w * sx, h * sy);
		root->width = w * sx; root->height = h * sy;
		root->vis = pg[0]; root->vw = W; root->vh = H; root->sx = sx; root->sy = sy;
		root->invalidate (true);
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
		if (!root || x < 0 || y < 0 || x >= W || y >= H) return -1;
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
		unsigned *d = root ? db () : 0;
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
		kapi_draw_text_buf (db (), W, H, x, y, s, rgb (c, rgb (gfg, 0xFFFFFF)));
		dirty ();
	}
	int mouse (int what) override
	{
		if (!root) return 0;
		pump ();
		return what == 0 ? root->mx : what == 1 ? root->my : root->mb;
	}
	void fullscreen (bool on) override
	{
		if (!ensureWindow ()) return;
		if (on && !fsBuf)
		{
			fsBuf = kapi_fullscreen_begin (&fsW, &fsH);
			if (!fsBuf) return;
			for (long i = 0; i < (long) fsW * fsH; i++) fsBuf[i] = 0;
			root->fs = true;
			present (true);
		}
		else if (!on && fsBuf)
		{
			kapi_fullscreen_end (); fsBuf = 0; root->fs = false;
			root->invalidate (true); present (true);
		}
	}

	// ---- time --------------------------------------------------------------------------------------
	void sleepMs (int ms) override
	{
		unsigned t0 = kapi_get_ticks ();
		while ((int) ((kapi_get_ticks () - t0) * 10) < ms)
		{
			if (root) { pump (); if (stopped ()) return; } else bgTick ();
			int left = ms - (int) ((kapi_get_ticks () - t0) * 10);
			kapi_msleep (left > 10 ? 10 : (left > 0 ? left : 1));
		}
		if (root) pump ();
	}
	bool poll () override
	{
		if (!root) { bgTick (); return !(console ? false : should_exit ()); }
		pump ();
		return !stopped ();
	}
	double timer () override
	{
		double t = tBase + (kapi_get_ticks () - tTicks0) / 100.0;
		while (t >= 86400) t -= 86400;
		return t;
	}
	static void two (char *p, int v) { p[0] = (char) ('0' + v / 10 % 10); p[1] = (char) ('0' + v % 10); }
	void date (char *o) override
	{
		int y = 0, mo = 0, d = 0; kapi_get_datetime (&y, &mo, &d, 0, 0, 0);
		two (o, mo); o[2] = '-'; two (o + 3, d); o[5] = '-'; two (o + 6, y / 100); two (o + 8, y % 100); o[10] = 0;
	}
	void time (char *o) override
	{
		int h = 0, m = 0, s = 0; kapi_get_datetime (0, 0, 0, &h, &m, &s);
		two (o, h); o[2] = ':'; two (o + 3, m); o[5] = ':'; two (o + 6, s); o[8] = 0;
	}
	unsigned seed () override { return kapi_get_ticks () * 2654435761u; }

	// ---- sound (ABI v46): the output is acquired on first use, released at exit ----------------------
	int audio = 0;					// 0 not asked, 1 ours, -1 unavailable
	int note (int voice, double freq, int wave, int vol) override
	{
		if (freq <= 0) { if (audio == 1) kapi_sound_stop (voice); return 0; }
		if (audio == 0) audio = kapi_sound_acquire () == 1 ? 1 : -1;
		if (audio != 1) return -1;
		return kapi_sound_start (voice, (unsigned) (freq * 1000), wave, vol) == 0 ? 0 : -1;
	}
	// PLAY "MB": a 32-note queue on voice 0, advanced by bgTick () from pump () / sleepMs ().
	struct BgNote { float freq; unsigned short on, off; unsigned char wave; };
	enum { BGQ = 32 };
	BgNote bgq[BGQ]; int bgHead = 0, bgCount = 0, bgPhase = 0; unsigned bgUntil = 0, bgGap = 0;	// phase 0 idle 1 on 2 off
	bool bgNote (double freq, int on, int off, int wave) override
	{
		if (freq > 0) { if (audio == 0) audio = kapi_sound_acquire () == 1 ? 1 : -1; if (audio != 1) return false; }
		while (bgCount >= BGQ)				// full: wait (QBasic does the same)
		{
			kapi_msleep (5); bgTick (); if (root) pump ();
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
		unsigned now = kapi_get_ticks () * 10;
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

	// ---- GUI -----------------------------------------------------------------------------------------
	void window (const char *t, int w, int h) override
	{
		windowCmd = true;
		if (!root) { scpy (title, t, sizeof title); if (w > 0 && h > 0) { W = w; H = h; } ensureWindow (); return; }
		if (w > 0 && h > 0 && (w != W || h != H)) { sx = sy = 1; resize (w, h); }
		fillRect (0, 0, W, H, rgb (bg, 0)); dirty ();
	}
	int control (int kind, int x, int y, int w, int h, const char *text, int val) override
	{
		if (!ensureWindow () || nctl >= MAXCTL - 1) return 0;
		int id = nctl + 1;
		Widget *wd = 0;
		switch (kind)
		{
		case bas::CTL_BUTTON:   wd = new Button (x, y, w, h, text, on_control); break;
		case bas::CTL_LABEL:    wd = new Label (x, y, w, h, text, C_TEXT, rgb (bg, 0)); break;
		case bas::CTL_TEXTBOX:  wd = new Textbox (x, y, w, h, text, on_control); break;
		case bas::CTL_CHECKBOX: wd = new Checkbox (x, y, w, h, text, val != 0, on_control, rgb (bg, 0)); break;
		case bas::CTL_PROGRESS: wd = new Progress (x, y, w, h, 0, 100, val); break;
		case bas::CTL_SLIDER:   wd = new Slider (x, y, w, h, 0, val > 0 ? val : 100, 0, on_control, rgb (bg, 0)); break;
		case bas::CTL_LISTBOX:
		{
			ListBox *lb = new ListBox (x, y, w, h, on_control, on_control);
			char item[128]; int n = 0;
			for (int i = 0; ; i++)
			{
				char c = text[i];
				if (c == '|' || c == 0) { item[n] = 0; if (n || c == '|') lb->add (item); n = 0; if (!c) break; continue; }
				if (n < 127) item[n++] = c;
			}
			wd = lb; break;
		}
		case bas::CTL_DROPDOWN:
		{
			int len = slen (text);
			char *copy = new char[len + 1]; scpy (copy, text, len + 1);
			ddItems[id] = copy;
			int n = 0; ddPtr[id][n++] = copy;
			for (int i = 0; i < len && n < 32; i++) if (copy[i] == '|') { copy[i] = 0; ddPtr[id][n++] = copy + i + 1; }
			wd = new Dropdown (x, y, w, h, ddPtr[id], n, 0, on_control);
			break;
		}
		default: return 0;
		}
		wd->tag = id;
		ctl[id] = wd; ctlKind[id] = kind; nctl++;
		root->addChild (wd);
		dirty ();
		return id;
	}
	Widget *get (int id) { return id > 0 && id <= nctl ? ctl[id] : 0; }
	void setText (int id, const char *s) override
	{
		Widget *w = get (id); if (!w) return;
		switch (ctlKind[id])
		{
		case bas::CTL_BUTTON: { Button *b = (Button *) w; scpy (b->text, s, sizeof b->text); b->invalidate (true); break; }
		case bas::CTL_LABEL: ((Label *) w)->setText (s); break;
		case bas::CTL_TEXTBOX: ((Textbox *) w)->setText (s); break;
		case bas::CTL_CHECKBOX: { Checkbox *c = (Checkbox *) w; scpy (c->text, s, sizeof c->text); c->invalidate (true); break; }
		case bas::CTL_LISTBOX: ((ListBox *) w)->add (s); break;		// SETTEXT on a list = add an item
		}
		dirty ();
	}
	int getText (int id, char *buf, int cap) override
	{
		Widget *w = get (id); buf[0] = 0; if (!w) return 0;
		switch (ctlKind[id])
		{
		case bas::CTL_BUTTON: scpy (buf, ((Button *) w)->text, cap); break;
		case bas::CTL_LABEL: scpy (buf, ((Label *) w)->text, cap); break;
		case bas::CTL_TEXTBOX: scpy (buf, ((Textbox *) w)->text, cap); break;
		case bas::CTL_CHECKBOX: scpy (buf, ((Checkbox *) w)->text, cap); break;
		case bas::CTL_LISTBOX: { ListBox *l = (ListBox *) w; scpy (buf, l->item (l->sel), cap); break; }
		case bas::CTL_DROPDOWN: { Dropdown *d = (Dropdown *) w; if (d->sel >= 0 && d->sel < d->nopts) scpy (buf, d->opts[d->sel], cap); break; }
		}
		return slen (buf);
	}
	int getValue (int id) override
	{
		Widget *w = get (id); if (!w) return 0;
		switch (ctlKind[id])
		{
		case bas::CTL_CHECKBOX: return ((Checkbox *) w)->checked ? -1 : 0;
		case bas::CTL_LISTBOX: return ((ListBox *) w)->sel;
		case bas::CTL_DROPDOWN: return ((Dropdown *) w)->sel;
		case bas::CTL_PROGRESS: return ((Progress *) w)->value;
		case bas::CTL_SLIDER: return ((Slider *) w)->value;
		}
		return 0;
	}
	void setValue (int id, int v) override
	{
		Widget *w = get (id); if (!w) return;
		switch (ctlKind[id])
		{
		case bas::CTL_CHECKBOX: ((Checkbox *) w)->checked = v != 0; w->invalidate (true); break;
		case bas::CTL_LISTBOX: ((ListBox *) w)->setSel (v); break;
		case bas::CTL_DROPDOWN: { Dropdown *d = (Dropdown *) w; if (v >= 0 && v < d->nopts) { d->sel = v; d->invalidate (true); } break; }
		case bas::CTL_PROGRESS: ((Progress *) w)->setValue (v); break;
		case bas::CTL_SLIDER: { Slider *s = (Slider *) w; s->value = v < s->vmin ? s->vmin : v > s->vmax ? s->vmax : v; s->invalidate (true); break; }
		}
		dirty ();
	}
	void pushEvent (int id) { int n = (et + 1) % EVQ; if (n != eh) { evq[et] = id; et = n; } }
	int event (bool wait) override
	{
		if (!ensureWindow ()) return -1;
		for (;;)
		{
			pump ();
			if (stopped ()) return -1;
			if (eh != et) { int id = evq[eh]; eh = (eh + 1) % EVQ; return id; }
			if (!wait) return 0;
			kapi_msleep (10);
		}
	}

	// ---- system -----------------------------------------------------------------------------------------
	void notify (const char *t, const char *m) override { ::notify (t, m); }
	int msgbox (const char *t, const char *m, int b) override
	{
		if (!ensureWindow ()) return 0;
		int r = wk_messagebox (t, m, b);
		dirty (); present (true);
		return r;
	}
	int clipboard (char *buf, int cap) override
	{
		int type = 0; unsigned serial = 0;
		int n = kapi_clipboard_get (&type, buf, (unsigned) cap - 1, &serial);
		if (n < 0 || type != CLIP_TEXT) n = 0;
		if (n > cap - 1) n = cap - 1;
		buf[n] = 0;
		return n;
	}
	void setClipboard (const char *s) override { kapi_clipboard_set (CLIP_TEXT, s, (unsigned) slen (s)); }
	bool fileDialog (bool save, const char *dir, char *o, int cap) override
	{
		if (!ensureWindow ()) return false;
		char name[128]; scpy (name, o, sizeof name);
		bool ok = save ? wk_file_save (o, (unsigned) cap, dir[0] ? dir : "SD:/", name[0] ? name : "untitled.txt")
			       : wk_file_open (o, (unsigned) cap, dir[0] ? dir : "SD:/");
		dirty (); present (true);
		return ok;
	}
	bool exec (const char *p, const char *a) override { return kapi_exec (p, a) != 0; }
	bool launch (const char *app) override { return kapi_launch (app) != 0; }
	bool chdir (const char *p) override { return kapi_chdir (p) != 0; }
	// SHELL "prog args": runs a /bin tool (or a path), its output on the screen; SHELL alone
	// opens a terminal.
	int shell (const char *cmd) override
	{
		int i = 0; while (cmd[i] == ' ') i++;
		if (!cmd[i]) return kapi_launch ("terminal") ? 0 : -1;
		char prog[200], path[220]; int n = 0;
		while (cmd[i] && cmd[i] != ' ' && n < 199) prog[n++] = cmd[i++];
		prog[n] = 0;
		while (cmd[i] == ' ') i++;
		bool hasPath = false; for (int k = 0; prog[k]; k++) if (prog[k] == '/' || prog[k] == ':') hasPath = true;
		if (hasPath) scpy (path, prog, sizeof path);
		else { scpy (path, "SD:/bin/", sizeof path); int k = slen (path); for (int j = 0; prog[j] && k < 218; j++) path[k++] = prog[j]; path[k] = 0; }
		if (!windowText ())				// the console: the tool writes there itself
		{
			void *pr = kapi_spawn (path, cmd + i, 0, kapi_stdout ());
			if (!pr) return -1;
			return kapi_wait (pr);
		}
		void *outp = kapi_pipe ();
		void *pr = kapi_spawn (path, cmd + i, 0, outp);
		if (!pr) { kapi_stream_close (outp); return -1; }
		char b[256];
		for (;;)
		{
			int r = kapi_stream_read_nb (outp, b, sizeof b);
			if (r > 0) { out (b, r); continue; }
			if (kapi_proc_done (pr)) { while ((r = kapi_stream_read_nb (outp, b, sizeof b)) > 0) out (b, r); break; }
			pump (); kapi_msleep (5);
		}
		int rc = kapi_wait (pr);
		kapi_stream_close (outp);
		return rc;
	}

	// ---- files ---------------------------------------------------------------------------------------------
	char *load (const char *path, int *len) override
	{
		*len = 0;
		void *f = kapi_open (path);
		if (!f) return 0;
		unsigned n = kapi_fsize (f);
		char *b = new char[n + 1];
		int r = kapi_read (f, b, n);
		kapi_close (f);
		*len = r > 0 ? r : 0; b[*len] = 0;
		return b;
	}
	bool save (const char *path, const char *d, int n) override { return kapi_save_file (path, d, (unsigned) n) >= 0; }
	bool remove (const char *p) override { return kapi_remove (p) == 0; }
	bool rename (const char *a, const char *b) override { return kapi_rename (a, b) == 0; }
	bool makeDir (const char *p) override { return kapi_mkdir (p) == 0; }
	bool exists (const char *p) override
	{
		void *f = kapi_open (p); if (f) { kapi_close (f); return true; }
		void *d = kapi_opendir (p); if (d) { kapi_closedir (d); return true; }
		return false;
	}
	int listDir (const char *dir, int index, char *o, int cap) override
	{
		o[0] = 0;
		void *d = kapi_opendir (dir[0] ? dir : ".");
		if (!d) return 0;
		struct kapi_dirent e; int i = 0;
		while (kapi_readdir (d, &e))
		{
			if (e.name[0] == '.') continue;
			if (i++ == index) { scpy (o, e.name, cap); if (e.is_dir) { int n = slen (o); if (n < cap - 1) { o[n] = '/'; o[n + 1] = 0; } } break; }
		}
		kapi_closedir (d);
		return slen (o);
	}
	const char *command () override { return args; }

	// End of the program: a text-only window program keeps its window until a key.
	void finished (bool error) override
	{
		while (!error && (bgCount || bgPhase) && !stopped ()) { kapi_msleep (10); bgTick (); if (root) pump (); }
		if (audio == 1) { kapi_sound_stop (-1); kapi_msleep (20); kapi_sound_release (); audio = 0; }
		if (fsBuf) fullscreen (false);
		if (!root || error || windowCmd || !textUsed || root->stop) return;
		const char *msg = "Press any key to continue";
		int save = fg; fg = 15;
		crow = rows - 1; ccol = 0;
		fillRect (0, crow * cellH, W, cellH, rgb (bg, 0));
		for (int i = 0; msg[i]; i++) putch (msg[i]);
		fg = (unsigned) save;
		dirty ();
		long k;
		while (!should_exit ()) { pump (); if (root->popKey (&k)) break; kapi_msleep (10); }
	}
};

static void on_control (Widget &w) { if (g_host) g_host->pushEvent (w.tag); }

int main (void)
{
	static char argbuf[512];
	kapi_get_args (argbuf, sizeof argbuf);
	char path[256], cwd[256] = ""; int i = 0, n = 0;
	bool ide = false;
	// Options: -d <dir> (current directory, default: the program's folder), -i (report a
	// syntax / runtime error to the qbasic editor over IPC: service "qbasic").
	for (;;)
	{
		while (argbuf[i] == ' ') i++;
		if (argbuf[i] == '-' && argbuf[i + 1] == 'i' && (argbuf[i + 2] == ' ' || !argbuf[i + 2])) { ide = true; i += 2; continue; }
		if (argbuf[i] == '-' && argbuf[i + 1] == 'd' && argbuf[i + 2] == ' ')
		{
			i += 3; while (argbuf[i] == ' ') i++;
			int k = 0;
			if (argbuf[i] == '"') { i++; while (argbuf[i] && argbuf[i] != '"' && k < 255) cwd[k++] = argbuf[i++]; if (argbuf[i]) i++; }
			else while (argbuf[i] && argbuf[i] != ' ' && k < 255) cwd[k++] = argbuf[i++];
			cwd[k] = 0;
			continue;
		}
		break;
	}
	if (argbuf[i] == '"') { i++; while (argbuf[i] && argbuf[i] != '"' && n < 255) path[n++] = argbuf[i++]; if (argbuf[i]) i++; }
	else while (argbuf[i] && argbuf[i] != ' ' && n < 255) path[n++] = argbuf[i++];
	path[n] = 0;
	while (argbuf[i] == ' ') i++;

	static OnyxHost host;
	g_host = &host;
	host.console = kapi_stdout () != 0;
	scpy (host.args, argbuf + i, sizeof host.args);
	if (!path[0])
	{
		ax_putln ("usage: basic <program.bas> [arguments]");
		return 1;
	}

	// Window title: the app name (apps/<name>.app/main.bas) or the file name.
	{
		int e = slen (path), s = e; while (s > 0 && path[s - 1] != '/' && path[s - 1] != ':') s--;
		const char *base = path + s;
		if ((base[0] == 'm' || base[0] == 'M') && s >= 5 && path[s - 5] == '.')		// "<name>.app/main.bas"
		{
			int ps = s - 1; while (ps > 0 && path[ps - 1] != '/' && path[ps - 1] != ':') ps--;
			int k = 0; for (int j = ps; j < s - 5 && k < 63; j++) host.title[k++] = path[j];
			host.title[k] = 0;
		}
		else scpy (host.title, base, sizeof host.title);
	}

	int len = 0;
	char *src = host.load (path, &len);
	if (!src)
	{
		if (host.console) { ax_puts ("basic: cannot read "); ax_putln (path); }
		else notify ("BASIC", "Cannot read the program file.");
		return 1;
	}
	// The program's folder becomes the current directory (relative file names).
	if (cwd[0]) kapi_chdir (cwd);
	else
	{
		char dir[256]; scpy (dir, path, sizeof dir);
		int e = slen (dir); while (e > 0 && dir[e - 1] != '/') e--;
		if (e > 0) { dir[e - 1 > 0 && dir[e - 2] != ':' ? e - 1 : e] = 0; kapi_chdir (dir); }
	}

	bas::Error err;
	bas::Program *prog = bas::compile (src, &err);
	delete [] src;
	char msg[200];
	auto report = [&] (const char *kind)
	{
		int k = 0;
		for (int j = 0; kind[j] && k < 190; j++) msg[k++] = kind[j];
		char num[12]; int nl = bas::formatNum (err.line, num);
		for (int j = 0; j < nl && k < 190; j++) msg[k++] = num[j];
		msg[k++] = ':'; msg[k++] = ' ';
		for (int j = 0; err.msg[j] && k < 198; j++) msg[k++] = err.msg[j];
		msg[k] = 0;
		if (ide)						// tell the editor: line \0 message
		{
			int pid = kapi_ipc_lookup ("qbasic");
			if (pid)
			{
				char m[180]; int q = bas::formatNum (err.line, m); m[q++] = 0;
				for (int j = 0; err.msg[j] && q < 178; j++) m[q++] = err.msg[j];
				m[q++] = 0;
				kapi_mailbox_send (pid, 1, m, (unsigned) q);
			}
		}
		if (host.fsBuf) host.fullscreen (false);
		if (host.root) { wk_messagebox ("BASIC", msg, MB_OK); }
		else if (host.console) ax_putln (msg);
		else notify (host.title, msg);
	};
	if (!prog) { report ("Syntax error in line "); return 2; }
	int r = bas::run (prog, host, &err);
	if (r) report ("Error in line ");
	bas::destroy (prog);
	return r ? 3 : 0;
}
