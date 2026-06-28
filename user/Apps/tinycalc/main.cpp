//
// tinycalc/main.cpp -- scientific calculator (C++ port). This app builds integer-only
// (-mgeneral-regs-only) and keeps ALL math in fixed-point: Q32.32 in a signed 64-bit
// int (1.0 == 1<<32). (Hardware float is now available to apps -- the kernel saves the
// full FP state across switches, see kern/trapframe.h -- but tinycalc stays fixed-point
// by design.) Input is by mouse (button grid, via the wtk widget toolkit) OR keyboard
// (digits/operators, routed straight to the calculator through the Root's onKey).
//
#include "kapi.h"
#include "wtk/wtk.h"		// recursive widget toolkit
#include "embed.h"		// run embedded in the activity shell (surface + mailbox)

typedef long long          i64;
typedef unsigned long long u64;
typedef i64                fix;

#define SHIFT	32
#define ONE	(1LL << SHIFT)

#define FIX_PI		13493037705LL
#define FIX_2PI		26986075410LL
#define FIX_HALFPI	6746518852LL
#define FIX_LN2		2977044472LL
#define FIX_LN10	9890190863LL

static int g_error = 0;

// ---- fixed-point core (64-bit only; no libgcc 128-bit calls) ----------------
static void umul (u64 a, u64 b, u64 *hi, u64 *lo)
{
	u64 al = a & 0xffffffffULL, ah = a >> 32;
	u64 bl = b & 0xffffffffULL, bh = b >> 32;
	u64 ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
	u64 mid = (ll >> 32) + (lh & 0xffffffffULL) + (hl & 0xffffffffULL);
	*lo = (ll & 0xffffffffULL) | (mid << 32);
	*hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}
static fix fmul (fix a, fix b)
{
	int neg = 0;
	if (a < 0) { a = -a; neg ^= 1; }
	if (b < 0) { b = -b; neg ^= 1; }
	u64 hi, lo; umul ((u64) a, (u64) b, &hi, &lo);
	u64 r = (lo >> 32) | (hi << 32);
	return neg ? -(fix) r : (fix) r;
}
static fix fdiv (fix a, fix b)
{
	if (b == 0) { g_error = 1; return 0; }
	int neg = 0;
	if (a < 0) { a = -a; neg ^= 1; }
	if (b < 0) { b = -b; neg ^= 1; }
	u64 ua = (u64) a, ub = (u64) b;
	u64 q = ua / ub, r = ua % ub, frac = 0;
	for (int i = 0; i < 32; i++) { r <<= 1; frac <<= 1; if (r >= ub) { r -= ub; frac |= 1; } }
	u64 res = (q << 32) | frac;
	return neg ? -(fix) res : (fix) res;
}
static fix fsqrt (fix x)
{
	if (x < 0) { g_error = 1; return 0; }
	if (x == 0) return 0;
	fix r = x > ONE ? x : ONE;
	for (int i = 0; i < 40; i++) { fix nr = (r + fdiv (x, r)) >> 1; if (nr == r) break; r = nr; }
	return r;
}
static fix reduce (fix x)
{
	fix k = fdiv (x, FIX_2PI);
	i64 n = (k + (k >= 0 ? ONE / 2 : -ONE / 2)) >> 32;
	return x - (fix) n * FIX_2PI;
}
static fix fsin (fix x)
{
	x = reduce (x);
	if (x >  FIX_HALFPI) x =  FIX_PI - x;
	if (x < -FIX_HALFPI) x = -FIX_PI - x;
	fix x2 = fmul (x, x), term = x, sum = x;
	for (int k = 1; k < 10; k++) { term = -fmul (term, x2) / ((2 * k) * (2 * k + 1)); sum += term; }
	return sum;
}
static fix fcos (fix x) { return fsin (x + FIX_HALFPI); }
static fix ftan (fix x) { fix c = fcos (x); if (c == 0) { g_error = 1; return 0; } return fdiv (fsin (x), c); }
static fix fln (fix x)
{
	if (x <= 0) { g_error = 1; return 0; }
	int e = 0;
	while (x >= (ONE << 1)) { x >>= 1; e++; }
	while (x < ONE)         { x <<= 1; e--; }
	fix t = fdiv (x - ONE, x + ONE), t2 = fmul (t, t), term = t, sum = t;
	for (int k = 1; k < 14; k++) { term = fmul (term, t2); sum += term / (2 * k + 1); }
	return (fix) e * FIX_LN2 + (sum << 1);
}
static fix fexp (fix x)
{
	i64 k = (fdiv (x, FIX_LN2) + (x >= 0 ? ONE / 2 : -ONE / 2)) >> 32;
	if (k > 30) { g_error = 1; return 0; }
	fix r = x - (fix) k * FIX_LN2, term = ONE, sum = ONE;
	for (int i = 1; i < 14; i++) { term = fmul (term, r) / i; sum += term; }
	if (k >= 0) sum <<= k; else sum >>= (-k);
	return sum;
}
static fix fpow (fix a, fix b) { if (a <= 0) { g_error = 1; return 0; } return fexp (fmul (b, fln (a))); }

// ---- decimal parse / format -------------------------------------------------
static int slen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static fix parse_fix (const char *s)
{
	int neg = 0, i = 0;
	if (s[i] == '-') { neg = 1; i++; } else if (s[i] == '+') i++;
	i64 ip = 0;
	while (s[i] >= '0' && s[i] <= '9') { if (ip < 2000000000) ip = ip * 10 + (s[i] - '0'); i++; }
	fix val = (fix) ip << 32;
	if (s[i] == '.')
	{
		i++; i64 fp = 0, scale = 1; int nd = 0;
		while (s[i] >= '0' && s[i] <= '9' && nd < 9) { fp = fp * 10 + (s[i] - '0'); scale *= 10; i++; nd++; }
		if (scale > 1) val += (fix) (((u64) fp << 32) / (u64) scale);
	}
	return neg ? -val : val;
}
static void format_fix (fix v, char *out)
{
	if (g_error) { const char *e = "Error"; int i = 0; while (e[i]) { out[i] = e[i]; i++; } out[i] = 0; return; }
	char *p = out;
	if (v < 0) { *p++ = '-'; v = -v; }
	v += ONE / 200000000;
	u64 ip = (u64) (v >> 32), frac = (u64) (v & 0xffffffffULL);
	char tmp[24]; int nn = 0;
	if (ip == 0) tmp[nn++] = '0';
	while (ip > 0) { tmp[nn++] = (char) ('0' + ip % 10); ip /= 10; }
	while (nn > 0) *p++ = tmp[--nn];
	char fd[12]; int fn = 0;
	for (int k = 0; k < 8 && frac != 0; k++) { frac *= 10; fd[fn++] = (char) ('0' + (int) (frac >> 32)); frac &= 0xffffffffULL; }
	while (fn > 0 && fd[fn - 1] == '0') fn--;
	if (fn > 0) { *p++ = '.'; for (int k = 0; k < fn; k++) *p++ = fd[k]; }
	*p = 0;
}

// ---- calculator state -------------------------------------------------------
#define W	280
#define H	296
#define DISPX	8
#define DISPY	8
#define DISPW	(W - 72)
#define DISPH	36

enum {
	D0, D1, D2, D3, D4, D5, D6, D7, D8, D9,
	DOT, EQ, ADD, SUB, MUL, DIV, CLR, NEG, PCT, BKSP,
	F_SIN, F_COS, F_TAN, F_LN, F_LOG, F_SQRT, F_SQ, F_POW, F_INV, F_EXP, F_PI, DEG
};

static char  g_entry[40] = "0";
static int   g_entering = 0;
static fix   g_acc = 0;
static int   g_op = 0;
static int   g_deg = 0;
static int   g_fw = 8, g_fh = 16;

static wtk::Widget *g_disp = 0;		// the display widget (invalidated when the entry changes)

static void set_entry (fix v) { format_fix (v, g_entry); g_entering = 0; }
static fix apply_op (fix a, int op, fix b)
{
	switch (op)
	{
	case ADD:   return a + b;
	case SUB:   return a - b;
	case MUL:   return fmul (a, b);
	case DIV:   return fdiv (a, b);
	case F_POW: return fpow (a, b);
	}
	return b;
}
static void push_digit (int d)
{
	if (g_error) { g_entry[0] = '0'; g_entry[1] = 0; g_error = 0; g_entering = 0; }
	if (!g_entering) { g_entry[0] = 0; g_entering = 1; }
	int n = slen (g_entry);
	if (n == 1 && g_entry[0] == '0') n = 0;
	if (n < 30) { g_entry[n] = (char) ('0' + d); g_entry[n + 1] = 0; }
}
static void push_dot (void)
{
	if (g_error || !g_entering) { g_entry[0] = '0'; g_entry[1] = 0; g_entering = 1; g_error = 0; }
	for (int i = 0; g_entry[i]; i++) if (g_entry[i] == '.') return;
	int n = slen (g_entry);
	if (n < 30) { g_entry[n] = '.'; g_entry[n + 1] = 0; }
}
static void do_backspace (void)
{
	if (!g_entering || g_error) return;
	int n = slen (g_entry);
	if (n > 1) g_entry[n - 1] = 0; else { g_entry[0] = '0'; g_entry[1] = 0; g_entering = 0; }
}
static fix to_rad (fix v) { return g_deg ? fmul (v, FIX_PI) / 180 : v; }
static void do_op (int op) { fix v = parse_fix (g_entry); if (g_op) g_acc = apply_op (g_acc, g_op, v); else g_acc = v; g_op = op; set_entry (g_acc); }
static void do_equals (void) { fix v = parse_fix (g_entry); if (g_op) { g_acc = apply_op (g_acc, g_op, v); g_op = 0; } else g_acc = v; set_entry (g_acc); }
static void do_unary (int code)
{
	fix v = parse_fix (g_entry), r = 0;
	switch (code)
	{
	case F_SIN:  r = fsin (to_rad (v)); break;
	case F_COS:  r = fcos (to_rad (v)); break;
	case F_TAN:  r = ftan (to_rad (v)); break;
	case F_LN:   r = fln (v); break;
	case F_LOG:  r = fdiv (fln (v), FIX_LN10); break;
	case F_SQRT: r = fsqrt (v); break;
	case F_SQ:   r = fmul (v, v); break;
	case F_INV:  r = fdiv (ONE, v); break;
	case F_EXP:  r = fexp (v); break;
	case NEG:    r = -v; break;
	case PCT:    r = fdiv (v, 100LL << 32); break;
	}
	set_entry (r);
}
static void action (int code)
{
	if (code >= D0 && code <= D9) { push_digit (code - D0); if (g_disp) g_disp->invalidate (true); return; }
	switch (code)
	{
	case DOT:  push_dot (); break;
	case BKSP: do_backspace (); break;
	case CLR:  g_entry[0] = '0'; g_entry[1] = 0; g_entering = 0; g_acc = 0; g_op = 0; g_error = 0; break;
	case ADD: case SUB: case MUL: case DIV: do_op (code); break;
	case EQ:   do_equals (); break;
	case F_PI: set_entry (FIX_PI); break;
	case F_POW: do_op (F_POW); break;
	case DEG:  g_deg ^= 1; break;
	default:   do_unary (code); break;
	}
	if (g_disp) g_disp->invalidate (true);
}

// ---- UI (wtk) ----------------------------------------------------------------
using namespace wtk;

// The right-aligned numeric display (its own canvas).
class Display : public Widget
{
public:
	Display (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (0x00101820);
		const char *s = g_entry;
		int tw = slen (s) * g_fw, tx = width - tw - 8; if (tx < 4) tx = 4;
		canvas.text (tx, (height - g_fh) / 2, s, g_error ? 0x00ff6060 : 0x0060ff90);
	}
};

// One callback for the whole grid (dispatch by tag). The DEG/RAD button relabels itself.
static void on_btn (Widget &w)
{
	action (w.tag);
	if (w.tag == DEG)
	{
		Button &b = (Button &) w;
		const char *t = g_deg ? "DEG" : "RAD";
		int i = 0; for (; t[i]; i++) b.text[i] = t[i]; b.text[i] = '\0';
		b.invalidate (true);
	}
}

// Keyboard -> calculator. Shared by the standalone window (Root::onKey) and the embedded
// loop (embed::run forwards SH_KEY here). Returns true if the key was consumed.
static bool calc_key (int key)
{
	if (key >= '0' && key <= '9') action (D0 + (key - '0'));
	else if (key == '.') action (DOT);
	else if (key == '+') action (ADD);
	else if (key == '-') action (SUB);
	else if (key == '*') action (MUL);
	else if (key == '/') action (DIV);
	else if (key == '%') action (PCT);
	else if (key == '=' || key == KEY_ENTER) action (EQ);
	else if (key == KEY_BACKSPACE) action (BKSP);
	else if (key == 27 || key == 'c' || key == 'C') action (CLR);
	else return false;
	return true;
}
static void calc_key_v (int key) { calc_key (key); }	// embed::KeyFn adapter

// A Root subclass so the keyboard drives the calculator directly (buttons never steal
// focus, so unfocused keys fall through to the Root's onKey).
class Calc : public Root
{
public:
	Calc (int w, int h, const char *t) : Root (w, h, t) {}
	bool onKey (long key) override { return calc_key ((int) key); }
};

static const char *g_lbl[30] = {
	"sin","cos","tan","ln","log", "sqrt","x^2","x^y","1/x","e^x",
	"7","8","9","/","C", "4","5","6","*","+/-",
	"1","2","3","-","%", "0",".","pi","=","+"
};
static const int g_code[30] = {
	F_SIN,F_COS,F_TAN,F_LN,F_LOG, F_SQRT,F_SQ,F_POW,F_INV,F_EXP,
	D7,D8,D9,DIV,CLR, D4,D5,D6,MUL,NEG, D1,D2,D3,SUB,PCT, D0,DOT,F_PI,EQ,ADD
};

#define CALC_BG	0x00283038

// Build the calculator UI into a 5-column / 7-row UniformGridLayout filling `root`:
// row 0 = display (colSpan 4) + DEG/RAD toggle; rows 1-6 = the 30 keys (reading order).
// Cell height = root height / 7; a small gap between cells.
static void build_ui (Widget *root, int w, int h)
{
	UniformGridLayout *grid = new UniformGridLayout (0, 0, w, h, 5, 0, CALC_BG, 4, 4);
	grid->anchor = ANCHOR_FILL;		// fills the root; resizing the root re-flows the keys
	grid->setRows (7);

	g_disp = new Display (0, 0, 10, 10);
	g_disp->colSpan = 4;			// display spans 4 of the 5 columns
	grid->addChild (g_disp);

	Button *deg = new Button (0, 0, 10, 10, "RAD", on_btn); deg->tag = DEG;	// DEG/RAD toggle (col 5)
	grid->addChild (deg);

	for (int i = 0; i < 30; i++)		// rows 1..6: the keypad + functions
	{
		Button *b = new Button (0, 0, 10, 10, g_lbl[i], on_btn);
		b->tag = g_code[i];
		grid->addChild (b);
	}
	root->addChild (grid);
}

int main (void)
{
	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;

	// Embedded in the activity shell? Register, get a surface viewport, run on it.
	embed::Host host;
	if (embed::attach (&host, ROLE_SECONDARY, "calc"))
	{
		Panel *root = new Panel (0, 0, host.w, host.h, CALC_BG);
		root->canvas.adopt (host.pixels, host.w, host.h, host.stride);	// draw into the surface sub-rect
		build_ui (root, host.w, host.h);
		embed::run (&host, root, calc_key_v);
		return 0;
	}

	// Standalone fallback: our own window.
	Calc root (W, H, "tinycalc");
	if (root.canvas.px == 0) return 1;
	root.setBg (CALC_BG);
	build_ui (&root, W, H);
	root.run ();
	return 0;
}
