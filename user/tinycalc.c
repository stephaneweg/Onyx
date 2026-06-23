//
// tinycalc.c -- a scientific calculator. The apps build integer-only
// (-mgeneral-regs-only: no hardware float, and the kernel doesn't save FP state
// across task switches), so ALL math is fixed-point: Q32.32 in a signed 64-bit
// int (1.0 == 1<<32). Transcendental functions use range reduction + Taylor /
// Newton / atanh series. Input is by mouse (button grid) OR keyboard (digits and
// operators); the display is drawn with kapi_draw_text.
//
#include "kapi.h"

typedef long long          i64;
typedef unsigned long long u64;
typedef i64                fix;

#define SHIFT	32
#define ONE	(1LL << SHIFT)

// Constants (value * 2^32, rounded).
#define FIX_PI		13493037705LL
#define FIX_2PI		26986075410LL
#define FIX_HALFPI	6746518852LL
#define FIX_LN2		2977044472LL
#define FIX_LN10	9890190863LL
#define FIX_E		11674931555LL

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
	u64 hi, lo;
	umul ((u64) a, (u64) b, &hi, &lo);
	u64 r = (lo >> 32) | (hi << 32);		// (a*b) >> 32
	return neg ? -(fix) r : (fix) r;
}

static fix fdiv (fix a, fix b)
{
	if (b == 0) { g_error = 1; return 0; }
	int neg = 0;
	if (a < 0) { a = -a; neg ^= 1; }
	if (b < 0) { b = -b; neg ^= 1; }
	u64 ua = (u64) a, ub = (u64) b;
	u64 q = ua / ub, r = ua % ub;			// integer part (hardware udiv)
	u64 frac = 0;
	for (int i = 0; i < 32; i++)			// 32 fractional bits, long division
	{
		r <<= 1; frac <<= 1;
		if (r >= ub) { r -= ub; frac |= 1; }
	}
	u64 res = (q << 32) | frac;
	return neg ? -(fix) res : (fix) res;
}

// ---- functions --------------------------------------------------------------

static fix fsqrt (fix x)
{
	if (x < 0) { g_error = 1; return 0; }
	if (x == 0) return 0;
	fix r = x > ONE ? x : ONE;
	for (int i = 0; i < 40; i++)			// Newton: r = (r + x/r)/2
	{
		fix nr = (r + fdiv (x, r)) >> 1;
		if (nr == r) break;
		r = nr;
	}
	return r;
}

// Reduce an angle to [-PI, PI].
static fix reduce (fix x)
{
	fix k = fdiv (x, FIX_2PI);
	i64 n = (k + (k >= 0 ? ONE / 2 : -ONE / 2)) >> 32;	// round to nearest integer
	return x - (fix) n * FIX_2PI;
}

static fix fsin (fix x)
{
	x = reduce (x);
	if (x >  FIX_HALFPI) x =  FIX_PI - x;		// fold into [-PI/2, PI/2]
	if (x < -FIX_HALFPI) x = -FIX_PI - x;
	fix x2 = fmul (x, x);
	fix term = x, sum = x;
	for (int k = 1; k < 10; k++)			// Taylor: -x^2/((2k)(2k+1))
	{
		term = -fmul (term, x2) / ((2 * k) * (2 * k + 1));
		sum += term;
	}
	return sum;
}

static fix fcos (fix x) { return fsin (x + FIX_HALFPI); }

static fix ftan (fix x)
{
	fix c = fcos (x);
	if (c == 0) { g_error = 1; return 0; }
	return fdiv (fsin (x), c);
}

static fix fln (fix x)
{
	if (x <= 0) { g_error = 1; return 0; }
	int e = 0;
	while (x >= (ONE << 1)) { x >>= 1; e++; }	// x = m * 2^e, m in [1,2)
	while (x < ONE)         { x <<= 1; e--; }
	fix t = fdiv (x - ONE, x + ONE);		// ln(m) = 2*atanh(t)
	fix t2 = fmul (t, t);
	fix term = t, sum = t;
	for (int k = 1; k < 14; k++)
	{
		term = fmul (term, t2);
		sum += term / (2 * k + 1);
	}
	return (fix) e * FIX_LN2 + (sum << 1);
}

static fix fexp (fix x)
{
	i64 k = (fdiv (x, FIX_LN2) + (x >= 0 ? ONE / 2 : -ONE / 2)) >> 32;
	if (k > 30) { g_error = 1; return 0; }		// would overflow Q32.32
	fix r = x - (fix) k * FIX_LN2;			// x = k*ln2 + r
	fix term = ONE, sum = ONE;
	for (int i = 1; i < 14; i++)			// exp(r) = sum r^i/i!
	{
		term = fmul (term, r) / i;
		sum += term;
	}
	if (k >= 0) sum <<= k; else sum >>= (-k);	// * 2^k
	return sum;
}

static fix fpow (fix a, fix b)			// a^b = exp(b*ln a), a>0
{
	if (a <= 0) { g_error = 1; return 0; }
	return fexp (fmul (b, fln (a)));
}

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
		i++;
		i64 fp = 0, scale = 1; int nd = 0;
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
	v += ONE / 200000000;				// round at the 8th fractional digit

	u64 ip = (u64) (v >> 32);
	u64 frac = (u64) (v & 0xffffffffULL);

	char tmp[24]; int n = 0;
	if (ip == 0) tmp[n++] = '0';
	while (ip > 0) { tmp[n++] = (char) ('0' + ip % 10); ip /= 10; }
	while (n > 0) *p++ = tmp[--n];

	char fd[12]; int fn = 0;
	for (int k = 0; k < 8 && frac != 0; k++)
	{
		frac *= 10;
		fd[fn++] = (char) ('0' + (int) (frac >> 32));
		frac &= 0xffffffffULL;
	}
	while (fn > 0 && fd[fn - 1] == '0') fn--;	// trim trailing zeros
	if (fn > 0) { *p++ = '.'; for (int k = 0; k < fn; k++) *p++ = fd[k]; }
	*p = 0;
}

// ---- calculator state -------------------------------------------------------

#define W	280
#define H	296
#define DISPX	8
#define DISPY	8
#define DISPW	(W - 72)	// leave room for the DEG/RAD toggle on the right
#define DISPH	36

enum {
	D0, D1, D2, D3, D4, D5, D6, D7, D8, D9,
	DOT, EQ, ADD, SUB, MUL, DIV, CLR, NEG, PCT, BKSP,
	F_SIN, F_COS, F_TAN, F_LN, F_LOG, F_SQRT, F_SQ, F_POW, F_INV, F_EXP, F_PI, DEG
};

static unsigned *fb;
static char  g_entry[40] = "0";
static int   g_entering = 0;
static fix   g_acc = 0;
static int   g_op = 0;			// pending operator code (0 = none)
static int   g_deg = 0;
static int   g_dirty = 1;
static int   g_fw = 8, g_fh = 16;
static unsigned long g_deg_btn = 0;

static void set_entry (fix v) { format_fix (v, g_entry); g_entering = 0; }

static fix apply_op (fix a, int op, fix b)
{
	switch (op)
	{
	case ADD:   return a + b;
	case SUB:   return a - b;
	case MUL:   return fmul (a, b);
	case DIV:   return fdiv (a, b);
	case F_POW: return fpow (a, b);		// x^y: x entered, then y, then '='
	}
	return b;
}

static void push_digit (int d)
{
	if (g_error) { g_entry[0] = '0'; g_entry[1] = 0; g_error = 0; g_entering = 0; }
	if (!g_entering) { g_entry[0] = 0; g_entering = 1; }
	int n = slen (g_entry);
	if (n == 1 && g_entry[0] == '0') n = 0;		// replace a lone leading zero
	if (n < 30) { g_entry[n] = (char) ('0' + d); g_entry[n + 1] = 0; }
}

static void push_dot (void)
{
	if (g_error || !g_entering) { g_entry[0] = '0'; g_entry[1] = 0; g_entering = 1; g_error = 0; }
	for (int i = 0; g_entry[i]; i++) if (g_entry[i] == '.') return;	// only one '.'
	int n = slen (g_entry);
	if (n < 30) { g_entry[n] = '.'; g_entry[n + 1] = 0; }
}

static void do_backspace (void)
{
	if (!g_entering || g_error) return;
	int n = slen (g_entry);
	if (n > 1) g_entry[n - 1] = 0;
	else { g_entry[0] = '0'; g_entry[1] = 0; g_entering = 0; }
}

static fix to_rad (fix v) { return g_deg ? fmul (v, FIX_PI) / 180 : v; }

static void do_op (int op)
{
	fix v = parse_fix (g_entry);
	if (g_op) g_acc = apply_op (g_acc, g_op, v); else g_acc = v;
	g_op = op;
	set_entry (g_acc);
}

static void do_equals (void)
{
	fix v = parse_fix (g_entry);
	if (g_op) { g_acc = apply_op (g_acc, g_op, v); g_op = 0; }
	else g_acc = v;
	set_entry (g_acc);
}

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
	if (code >= D0 && code <= D9) { push_digit (code - D0); g_dirty = 1; return; }
	switch (code)
	{
	case DOT:  push_dot (); break;
	case BKSP: do_backspace (); break;
	case CLR:  g_entry[0] = '0'; g_entry[1] = 0; g_entering = 0; g_acc = 0; g_op = 0; g_error = 0; break;
	case ADD: case SUB: case MUL: case DIV: do_op (code); break;
	case EQ:   do_equals (); break;
	case F_PI: set_entry (FIX_PI); break;
	case F_POW:							// x^y: y is the next entry
		do_op (F_POW); break;
	case DEG:
		g_deg ^= 1;
		kapi_widget_set_text (g_deg_btn, g_deg ? "DEG" : "RAD");
		break;
	default: do_unary (code); break;
	}
	g_dirty = 1;
}

// ---- UI ---------------------------------------------------------------------

#define NB	40
static unsigned long g_bh[NB];
static int           g_bc[NB];
static int           g_nb = 0;

static void on_btn (unsigned long sender, int ev, long v)
{
	(void) ev; (void) v;
	for (int i = 0; i < g_nb; i++)
		if (g_bh[i] == sender) { action (g_bc[i]); return; }
}

static void on_deg (unsigned long sender, int ev, long v)
{
	(void) sender; (void) ev; (void) v;
	action (DEG);
}

static void on_key (unsigned long sender, int ev, long key)
{
	(void) sender;
	if (ev != GUI_EVENT_KEY) return;
	if (key >= '0' && key <= '9') action (D0 + (int) (key - '0'));
	else if (key == '.') action (DOT);
	else if (key == '+') action (ADD);
	else if (key == '-') action (SUB);
	else if (key == '*') action (MUL);
	else if (key == '/') action (DIV);
	else if (key == '%') action (PCT);
	else if (key == '=' || key == KEY_ENTER) action (EQ);
	else if (key == KEY_BACKSPACE) action (BKSP);
	else if (key == 27 || key == 'c' || key == 'C') action (CLR);
}

static void fill_rect (int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h && yy < H; yy++)
		for (int xx = x; xx < x + w && xx < W; xx++)
			if (xx >= 0 && yy >= 0) fb[yy * W + xx] = c;
}

static void draw_display (void)
{
	fill_rect (DISPX, DISPY, DISPW, DISPH, 0x00101820);
	const char *s = g_entry;
	int tw = slen (s) * g_fw;
	int tx = DISPX + DISPW - tw - 8;
	if (tx < DISPX + 4) tx = DISPX + 4;
	int ty = DISPY + (DISPH - g_fh) / 2;
	kapi_draw_text (tx, ty, s, g_error ? 0x00ff6060 : 0x0060ff90);
}

// Button grid: 5 columns x 6 rows, plus the DEG/RAD toggle on the display row.
static const char *g_lbl[30] = {
	"sin","cos","tan","ln","log",
	"sqrt","x^2","x^y","1/x","e^x",
	"7","8","9","/","C",
	"4","5","6","*","+/-",
	"1","2","3","-","%",
	"0",".","pi","=","+"
};
static const int g_code[30] = {
	F_SIN,F_COS,F_TAN,F_LN,F_LOG,
	F_SQRT,F_SQ,F_POW,F_INV,F_EXP,
	D7,D8,D9,DIV,CLR,
	D4,D5,D6,MUL,NEG,
	D1,D2,D3,SUB,PCT,
	D0,DOT,F_PI,EQ,ADD
};

int main (void)
{
	fb = kapi_create_window (W, H, "tinycalc");
	if (fb == 0) return 1;

	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;

	fill_rect (0, 0, W, H, 0x00283038);		// window background

	// DEG/RAD toggle on the display row (right).
	g_deg_btn = kapi_add_button (W - 56, DISPY, 48, DISPH, "RAD", on_deg);

	// 5x6 button grid.
	const int x0 = 8, y0 = 52, bw = 48, bh = 34, sx = 54, sy = 40;
	for (int r = 0; r < 6; r++)
	{
		for (int c = 0; c < 5; c++)
		{
			int idx = r * 5 + c;
			unsigned long h = kapi_add_button (x0 + c * sx, y0 + r * sy, bw, bh,
							   g_lbl[idx], on_btn);
			if (g_nb < NB) { g_bh[g_nb] = h; g_bc[g_nb] = g_code[idx]; g_nb++; }
		}
	}

	kapi_set_key_handler (on_key);

	while (!should_exit ())
	{
		pump_events ();
		if (g_dirty) { draw_display (); g_dirty = 0; }
		msleep (16);
	}
	return 0;
}
