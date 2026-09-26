//
// basic/basvm.cpp -- the Onyx BASIC virtual machine: runs a compiled bas::Program.
//
// A stack machine over tagged values: numbers (double), strings (reference-counted,
// immutable), arrays (reference-counted, so array arguments are shared) and references
// (a by-ref argument points at the caller's variable slot). Each SUB / FUNCTION call gets
// a frame of locals. Everything outside goes through the bas::Host.
//
#include "basic/basint.h"
#include "basic/basnum.h"

namespace bas {

enum { VN = 0, VS, VA, VR };

struct Str { int ref; int len; char d[1]; };
struct V { int t; double n; void *p; };
struct Arr { int ref; int nd; int lo[4]; int cnt[4]; int total; bool isStr; V *e; };

static Str *snew (const char *s, int len)
{
	if (len <= 0) return 0;
	Str *x = (Str *) new char[sizeof (Str) + len];
	x->ref = 1; x->len = len; bmcpy (x->d, s, len); x->d[len] = 0;
	return x;
}
static void srel (Str *s) { if (s && --s->ref == 0) delete [] (char *) s; }
static void arel (Arr *a);
static inline void vclear (V &v)
{
	if (v.t == VS) srel ((Str *) v.p);
	else if (v.t == VA) arel ((Arr *) v.p);
	v.t = VN; v.n = 0; v.p = 0;
}
static void arel (Arr *a)
{
	if (!a || --a->ref > 0) return;
	for (int i = 0; i < a->total; i++) vclear (a->e[i]);
	delete [] a->e; delete a;
}
static inline void vretain (const V &v)
{
	if (v.t == VS && v.p) ((Str *) v.p)->ref++;
	else if (v.t == VA) ((Arr *) v.p)->ref++;
}
static inline void vset (V &dst, const V &src) { vretain (src); vclear (dst); dst = src; }
static inline const char *sdata (const V &v, int *len)
{
	if (v.t != VS || !v.p) { *len = 0; return ""; }
	*len = ((Str *) v.p)->len; return ((Str *) v.p)->d;
}

class VM
{
public:
	Program *P; Host &H; Error *err;
	enum { STACK = 2048, MAXFRAMES = 400, MAXGOSUB = 256, MAXFILES = 16 };
	V stack[STACK]; int sp;
	V *G;
	struct Frame { int ret; V *loc; int nloc; int proc; };
	Frame frames[MAXFRAMES]; int nf;
	struct GoSub { int ret; int frame; };
	GoSub gosubs[MAXGOSUB]; int ngs;
	int pc, opPc; bool failed, ended;
	unsigned rnd; double lastRnd;
	int dataPtr;
	int chan;				// current PRINT / INPUT channel (0 = screen)
	char inBuf[512]; int inPos;
	int lastX, lastY;
	struct File { bool open; int mode; char *buf; int len, cap, pos, col; char path[240]; };
	File files[MAXFILES];

	VM (Program *p, Host &h, Error *e) : P (p), H (h), err (e), sp (0), G (0), nf (0), ngs (0), pc (0), opPc (0),
		failed (false), ended (false), rnd (327680), lastRnd (0), dataPtr (0), chan (0), inPos (0), lastX (0), lastY (0)
	{
		inBuf[0] = 0;
		for (int i = 0; i < MAXFILES; i++) { files[i].open = false; files[i].buf = 0; }
		G = new V[P->nglobals > 0 ? P->nglobals : 1];
		for (int i = 0; i < P->nglobals; i++) initSlot (G[i], P->gkind[i]);
	}
	~VM ()
	{
		while (sp > 0) vclear (stack[--sp]);
		while (nf > 0) popFrame ();
		for (int i = 0; i < P->nglobals; i++) vclear (G[i]);
		delete [] G;
		for (int i = 0; i < MAXFILES; i++) delete [] files[i].buf;
	}
	static void initSlot (V &v, int kind) { v.t = kind == K_STR ? VS : VN; v.n = 0; v.p = 0; }

	// ---- errors --------------------------------------------------------------------------------
	void fail (const char *msg)
	{
		if (failed) return;
		failed = true;
		err->line = P->lineAt (opPc);
		bscpy (err->msg, msg, sizeof err->msg);
	}

	// ---- stack --------------------------------------------------------------------------------------
	void push (const V &v) { if (sp >= STACK) { fail ("Out of stack space"); return; } stack[sp++] = v; }
	void pushN (double n) { V v; v.t = VN; v.n = n; v.p = 0; push (v); }
	void pushS (const char *s, int len) { V v; v.t = VS; v.n = 0; v.p = snew (s, len); push (v); }
	void pushStr (Str *s) { V v; v.t = VS; v.n = 0; v.p = s; push (v); }
	V pop () { if (sp <= 0) { V v; v.t = VN; v.n = 0; v.p = 0; fail ("Stack underflow"); return v; } return stack[--sp]; }
	double popN () { V v = pop (); double n = v.n; if (v.t != VN) { vclear (v); fail ("Type mismatch"); } return n; }
	long long popI () { double d = popN (); return (long long) nfloor (d + 0.5); }

	// ---- variables ------------------------------------------------------------------------------------
	V &slot (bool global, int s)
	{
		V *v = global ? &G[s] : &frames[nf - 1].loc[s];
		if (v->t == VR) v = (V *) v->p;
		return *v;
	}
	int kindOf (bool global, int s)
	{
		if (global) return P->gkind[s];
		const ProcInfo &pi = P->procs[frames[nf - 1].proc];
		return P->lkind[pi.kindOff + s];
	}
	Arr *newArr (int nd, const int *lo, const int *hi, bool isStr)
	{
		Arr *a = new Arr; a->ref = 1; a->nd = nd; a->isStr = isStr; a->total = 1;
		for (int i = 0; i < nd; i++)
		{
			if (hi[i] < lo[i]) { delete a; fail ("Subscript out of range (DIM)"); return 0; }
			a->lo[i] = lo[i]; a->cnt[i] = hi[i] - lo[i] + 1; a->total *= a->cnt[i];
			if (a->total > 4000000) { delete a; fail ("Array too big"); return 0; }
		}
		a->e = new V[a->total];
		for (int i = 0; i < a->total; i++) { a->e[i].t = isStr ? VS : VN; a->e[i].n = 0; a->e[i].p = 0; }
		return a;
	}
	// The element for the nd indices on the stack (popped). Auto-DIMs 0..10.
	V *element (bool global, int s, int nd)
	{
		long long idx[4];
		if (nd > 4) { fail ("Too many subscripts"); return 0; }
		for (int i = nd - 1; i >= 0; i--) idx[i] = popI ();
		V &var = slot (global, s);
		if (var.t != VA)
		{
			int lo[4] = { 0, 0, 0, 0 }, hi[4] = { 10, 10, 10, 10 };
			Arr *a = newArr (nd, lo, hi, kindOf (global, s) == K_STRARR);
			if (!a) return 0;
			vclear (var); var.t = VA; var.p = a;
		}
		Arr *a = (Arr *) var.p;
		if (a->nd != nd) { fail ("Wrong number of subscripts"); return 0; }
		long long off = 0;
		for (int i = 0; i < nd; i++)
		{
			long long k = idx[i] - a->lo[i];
			if (k < 0 || k >= a->cnt[i]) { fail ("Subscript out of range"); return 0; }
			off = off * a->cnt[i] + k;
		}
		return &a->e[off];
	}

	void popFrame ()
	{
		Frame &f = frames[--nf];
		for (int i = 0; i < f.nloc; i++) if (f.loc[i].t != VR) vclear (f.loc[i]);
		delete [] f.loc;
		while (ngs > 0 && gosubs[ngs - 1].frame > nf) ngs--;
	}

	// ---- output ------------------------------------------------------------------------------------
	File *file (long long n, bool mustBeOpen = true)
	{
		if (n < 1 || n >= MAXFILES) { fail ("Bad file number"); return 0; }
		if (mustBeOpen && !files[n].open) { fail ("File not open"); return 0; }
		return &files[n];
	}
	void fappend (File *f, const char *s, int n)
	{
		if (f->len + n + 1 > f->cap)
		{
			int nc = (f->cap ? f->cap * 2 : 1024); while (nc < f->len + n + 1) nc *= 2;
			char *b = new char[nc]; bmcpy (b, f->buf, f->len); delete [] f->buf; f->buf = b; f->cap = nc;
		}
		bmcpy (f->buf + f->len, s, n); f->len += n;
		for (int i = 0; i < n; i++) { if (s[i] == '\n') f->col = 1; else f->col++; }
	}
	void out (const char *s, int n)
	{
		if (chan)
		{
			File *f = file (chan);
			if (!f) return;
			if (f->mode == 1) { fail ("Bad file mode (opened FOR INPUT)"); return; }
			fappend (f, s, n);
		}
		else H.out (s, n);
	}
	int column () { if (chan) { File *f = file (chan); return f ? f->col : 1; } return H.column (); }
	void spaces (int n) { char b[64]; for (int i = 0; i < 64; i++) b[i] = ' '; while (n > 0) { int k = n > 64 ? 64 : n; out (b, k); n -= k; } }

	void printValue (V &v, bool write)
	{
		if (v.t == VS) { int n; const char *s = sdata (v, &n); if (write) out ("\"", 1); out (s, n); if (write) out ("\"", 1); return; }
		char b[40]; int n = 0;
		if (!write && v.n >= 0) b[n++] = ' ';
		n += formatNum (v.n, b + n);
		if (!write) b[n++] = ' ';
		out (b, n);
	}

	// ---- input -----------------------------------------------------------------------------------------
	// Next comma-separated field of line s (at *pos): quoted string or bare text (trimmed).
	// num: a number field also ends at a blank (QBasic's INPUT #).
	static int field (const char *s, int n, int *pos, char *out, int cap, bool num = false)
	{
		int i = *pos, k = 0;
		while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
		if (i < n && s[i] == '"')
		{
			i++;
			while (i < n && s[i] != '"') { if (k < cap - 1) out[k++] = s[i]; i++; }
			if (i < n) i++;
			while (i < n && s[i] != ',' && s[i] != '\n') i++;
		}
		else
		{
			while (i < n && s[i] != ',' && s[i] != '\n' && s[i] != '\r' && !(num && (s[i] == ' ' || s[i] == '\t'))) { if (k < cap - 1) out[k++] = s[i]; i++; }
			while (k > 0 && (out[k - 1] == ' ' || out[k - 1] == '\t')) k--;
			while (num && i < n && (s[i] == ' ' || s[i] == '\t')) i++;
		}
		if (i < n && s[i] == '\r') i++;
		if (i < n && (s[i] == ',' || s[i] == '\n')) i++;
		*pos = i; out[k] = 0;
		return k;
	}
	bool readLine (char *buf, int cap, const char *prompt, int plen, bool question)
	{
		if (plen) H.out (prompt, plen);
		if (question) H.out ("? ", 2);
		int n = H.inputLine (buf, cap);
		if (n < 0) { ended = true; return false; }
		buf[n] = 0;
		return true;
	}

	// ---- builtins -----------------------------------------------------------------------------------------
	double rndNext () { rnd = rnd * 214013u + 2531011u; lastRnd = ((rnd >> 8) & 0xFFFFFF) / 16777216.0; return lastRnd; }

	void builtin (int id, int argc)
	{
		V a[8];
		for (int i = 0; i < 8; i++) { a[i].t = VN; a[i].n = 0; a[i].p = 0; }
		if (argc > 8) { fail ("Too many arguments"); return; }
		for (int i = argc - 1; i >= 0; i--) a[i] = pop ();
		if (failed) { for (int i = 0; i < argc; i++) vclear (a[i]); return; }
		int l0, l1; const char *s0 = sdata (a[0], &l0), *s1 = argc > 1 ? sdata (a[1], &l1) : ""; if (argc < 2) l1 = 0;
		char tmp[260];
		switch (id)
		{
		case B_LEN: pushN (l0); break;
		case B_ASC: if (l0 == 0) fail ("Illegal function call (ASC of an empty string)"); else pushN ((unsigned char) s0[0]); break;
		case B_CHR: { long long c = (long long) a[0].n; if (c < 0 || c > 255) { fail ("Illegal function call (CHR$)"); break; } char ch = (char) c; pushS (&ch, 1); break; }
		case B_LEFT: { long long n = (long long) a[1].n; if (n < 0) { fail ("Illegal function call"); break; } if (n > l0) n = l0; pushS (s0, (int) n); break; }
		case B_RIGHT: { long long n = (long long) a[1].n; if (n < 0) { fail ("Illegal function call"); break; } if (n > l0) n = l0; pushS (s0 + l0 - n, (int) n); break; }
		case B_MID:
		{
			long long st = (long long) a[1].n, n = argc > 2 ? (long long) a[2].n : l0;
			if (st < 1 || n < 0) { fail ("Illegal function call (MID$)"); break; }
			if (st > l0) { pushS ("", 0); break; }
			if (st - 1 + n > l0) n = l0 - (st - 1);
			pushS (s0 + st - 1, (int) n); break;
		}
		case B_INSTR:
		{
			int k = 0; long long st = 1;
			if (argc == 3) { if (a[0].t != VN) { fail ("Type mismatch (INSTR)"); break; } st = (long long) a[0].n; k = 1; }
			if (a[k].t != VS || a[k + 1].t != VS) { fail ("Type mismatch (INSTR)"); break; }
			int hl, nl; const char *h = sdata (a[k], &hl), *nd = sdata (a[k + 1], &nl);
			if (st < 1) { fail ("Illegal function call (INSTR)"); break; }
			long long r = 0;
			if (nl == 0) r = st <= hl ? st : 0;
			else for (long long i = st - 1; i + nl <= hl; i++) { int j = 0; while (j < nl && h[i + j] == nd[j]) j++; if (j == nl) { r = i + 1; break; } }
			pushN ((double) r); break;
		}
		case B_UCASE: case B_LCASE:
		{
			Str *x = snew (s0, l0);
			if (x) for (int i = 0; i < l0; i++) { char c = x->d[i]; if (id == B_UCASE && c >= 'a' && c <= 'z') x->d[i] = (char) (c - 32); if (id == B_LCASE && c >= 'A' && c <= 'Z') x->d[i] = (char) (c + 32); }
			pushStr (x); break;
		}
		case B_LTRIM: case B_RTRIM: case B_TRIM:
		{
			int st = 0, e = l0;
			if (id != B_RTRIM) while (st < e && s0[st] == ' ') st++;
			if (id != B_LTRIM) while (e > st && s0[e - 1] == ' ') e--;
			pushS (s0 + st, e - st); break;
		}
		case B_STR: { int n = 0; if (a[0].n >= 0) tmp[n++] = ' '; n += formatNum (a[0].n, tmp + n); pushS (tmp, n); break; }
		case B_VAL: { int n = l0 < 255 ? l0 : 255; bmcpy (tmp, s0, n); tmp[n] = 0; pushN (parseNum (tmp, 0)); break; }
		case B_SPACE: case B_STRING:
		{
			long long n = (long long) a[0].n; if (n < 0 || n > 1000000) { fail ("Illegal function call"); break; }
			char c = ' ';
			if (id == B_STRING) { if (a[1].t == VN) c = (char) (long long) a[1].n; else if (l1 > 0) c = s1[0]; else { fail ("Illegal function call (STRING$)"); break; } }
			Str *x = 0;
			if (n > 0) { x = (Str *) new char[sizeof (Str) + n]; x->ref = 1; x->len = (int) n; for (long long i = 0; i < n; i++) x->d[i] = c; x->d[n] = 0; }
			pushStr (x); break;
		}
		case B_HEX: case B_OCT:
		{
			unsigned long long v = (unsigned long long) (long long) nfloor (a[0].n + 0.5);
			if (a[0].n < 0) v &= 0xFFFFFFFFull;
			int base = id == B_HEX ? 16 : 8, n = 0; char t[40];
			if (v == 0) t[n++] = '0';
			while (v) { t[n++] = "0123456789ABCDEF"[v % base]; v /= base; }
			for (int i = 0; i < n; i++) tmp[i] = t[n - 1 - i];
			pushS (tmp, n); break;
		}
		case B_ABS: pushN (a[0].n < 0 ? -a[0].n : a[0].n); break;
		case B_SGN: pushN (a[0].n > 0 ? 1 : a[0].n < 0 ? -1 : 0); break;
		case B_INT: pushN (nfloor (a[0].n)); break;
		case B_FIX: pushN (a[0].n < 0 ? -nfloor (-a[0].n) : nfloor (a[0].n)); break;
		case B_SQR: if (a[0].n < 0) fail ("Illegal function call (SQR of a negative number)"); else pushN (nsqrt (a[0].n)); break;
		case B_SIN: pushN (nsin (a[0].n)); break;
		case B_COS: pushN (ncos (a[0].n)); break;
		case B_TAN: pushN (ntan (a[0].n)); break;
		case B_ATN: pushN (natan (a[0].n)); break;
		case B_EXP: pushN (nexp (a[0].n)); break;
		case B_LOG: if (a[0].n <= 0) fail ("Illegal function call (LOG of 0 or less)"); else pushN (nlog (a[0].n)); break;
		case B_RND:
			if (argc && a[0].n == 0) pushN (lastRnd);
			else { if (argc && a[0].n < 0) rnd = (unsigned) (long long) a[0].n; pushN (rndNext ()); }
			break;
		case B_CINT: case B_CLNG: pushN (nfloor (a[0].n + 0.5)); break;
		case B_CDBL: pushN (a[0].n); break;
		case B_MIN: pushN (a[0].n < a[1].n ? a[0].n : a[1].n); break;
		case B_MAX: pushN (a[0].n > a[1].n ? a[0].n : a[1].n); break;
		case B_TIMER: pushN (H.timer ()); break;
		case B_TICKS: pushN (nfloor (H.timer () * 1000)); break;
		case B_DATE: { char d[16]; H.date (d); pushS (d, bslen (d)); break; }
		case B_TIME: { char t[16]; H.time (t); pushS (t, bslen (t)); break; }
		case B_INKEY: { char k[4]; int n = H.inkey (k); pushS (k, n); if (!H.poll ()) ended = true; break; }
		case B_COMMAND: { const char *c = H.command (); pushS (c, bslen (c)); break; }
		case B_POS: pushN (H.column ()); break;
		case B_CSRLIN: pushN (H.row ()); break;
		case B_POINT: pushN (H.point ((int) a[0].n, (int) a[1].n)); break;
		case B_RGB:
		{
			int r = (int) a[0].n, g = (int) a[1].n, b = (int) a[2].n;
			r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
			pushN ((double) (0x1000000 | (r << 16) | (g << 8) | b)); break;
		}
		case B_EOF: { File *f = file ((long long) a[0].n); if (f) pushN (f->pos >= f->len ? -1 : 0); break; }
		case B_LOF: { File *f = file ((long long) a[0].n); if (f) pushN (f->len); break; }
		case B_FREEFILE: { int n = 1; while (n < MAXFILES && files[n].open) n++; if (n >= MAXFILES) fail ("Too many files"); else pushN (n); break; }
		case B_FILEEXISTS: { cstr (a[0], tmp, sizeof tmp); pushN (H.exists (tmp) ? -1 : 0); break; }
		case B_DIR:
		{
			cstr (a[0], tmp, sizeof tmp); char o[200];
			int n = H.listDir (tmp, argc > 1 ? (int) a[1].n : 0, o, sizeof o);
			pushS (o, n); break;
		}
		case B_BUTTON: case B_LABEL: case B_TEXTBOX: case B_CHECKBOX: case B_LISTBOX: case B_DROPDOWN: case B_PROGRESS: case B_SLIDER:
		{
			static const int kinds[] = { CTL_BUTTON, CTL_LABEL, CTL_TEXTBOX, CTL_CHECKBOX, CTL_LISTBOX, CTL_DROPDOWN, CTL_PROGRESS, CTL_SLIDER };
			int kind = kinds[id - B_BUTTON];
			char t[512] = ""; int val = 0;
			if (argc > 4) { if (a[4].t == VS) cstr (a[4], t, sizeof t); else val = (int) a[4].n; }
			if (argc > 5) val = (int) a[5].n;
			if (kind == CTL_SLIDER && argc < 5) val = 100;
			pushN (H.control (kind, (int) a[0].n, (int) a[1].n, (int) a[2].n, (int) a[3].n, t, val));
			break;
		}
		case B_GETTEXT: { char t[1024]; int n = H.getText ((int) a[0].n, t, sizeof t); pushS (t, n); break; }
		case B_VALUE: pushN (H.getValue ((int) a[0].n)); break;
		case B_EVENT: { int e = H.event (false); if (e < 0) ended = true; pushN (e); break; }
		case B_WAITEVENT: { int e = H.event (true); if (e < 0) ended = true; pushN (e); break; }
		case B_MSGBOX:
		{
			char t[128], m[512]; cstr (a[0], t, sizeof t); cstr (a[1], m, sizeof m);
			pushN (H.msgbox (t, m, argc > 2 ? (int) a[2].n : 0) ? -1 : 0); break;
		}
		case B_CLIPBOARD: { char t[2048]; int n = H.clipboard (t, sizeof t); pushS (t, n); break; }
		case B_OPENFILE: case B_SAVEFILE:
		{
			char d[200] = "", o[256] = "";
			if (argc > 0) cstr (a[0], d, sizeof d);
			if (argc > 1) cstr (a[1], o, sizeof o);
			if (!H.fileDialog (id == B_SAVEFILE, d, o, sizeof o)) o[0] = 0;
			pushS (o, bslen (o)); break;
		}
		case B_LBOUND: case B_UBOUND:
		{
			int d = (int) a[1].n;
			if (a[0].t != VA) { if (a[0].t == VN) { pushN (id == B_LBOUND ? 0 : 10); break; } fail ("Type mismatch (LBOUND / UBOUND)"); break; }
			Arr *ar = (Arr *) a[0].p;
			if (d < 1 || d > ar->nd) { fail ("Subscript out of range (dimension)"); break; }
			pushN (id == B_LBOUND ? ar->lo[d - 1] : ar->lo[d - 1] + ar->cnt[d - 1] - 1);
			break;
		}
		case B_MOUSEX: pushN (H.mouse (0)); break;
		case B_MOUSEY: pushN (H.mouse (1)); break;
		case B_MOUSEB: pushN (H.mouse (2)); break;
		default: fail ("Unknown function");
		}
		for (int i = 0; i < argc; i++) vclear (a[i]);
	}
	static void cstr (const V &v, char *out, int cap) { int n; const char *s = sdata (v, &n); if (n > cap - 1) n = cap - 1; bmcpy (out, s, n); out[n] = 0; }

	void statement (int id, int argc)
	{
		V a[8];
		for (int i = 0; i < 8; i++) { a[i].t = VN; a[i].n = 0; a[i].p = 0; }
		if (argc > 8) { fail ("Too many arguments"); return; }
		for (int i = argc - 1; i >= 0; i--) a[i] = pop ();
		if (failed) { for (int i = 0; i < argc; i++) vclear (a[i]); return; }
		char t1[512], t2[512];
		auto N = [&] (int i, int def) { return i < argc ? (int) a[i].n : def; };
		switch (id)
		{
		case S_CLS: H.cls (); break;
		case S_LOCATE: H.locate (N (0, -1), N (1, -1)); break;
		case S_COLOR: H.color (N (0, -1), N (1, -1)); break;
		case S_SCREEN: H.screen (N (0, 0)); break;
		case S_PSET: H.pset (N (0, 0), N (1, 0), N (2, -1)); lastX = N (0, 0); lastY = N (1, 0); break;
		case S_LINE:
		{
			int x1 = N (0, 0), y1 = N (1, 0);
			if (x1 == -32768 && y1 == -32768) { x1 = lastX; y1 = lastY; }
			H.line (x1, y1, N (2, 0), N (3, 0), N (4, -1), N (5, 0));
			lastX = N (2, 0); lastY = N (3, 0);
			break;
		}
		case S_CIRCLE: H.circle (N (0, 0), N (1, 0), N (2, 0), N (3, -1), N (4, 0)); lastX = N (0, 0); lastY = N (1, 0); break;
		case S_DRAWTEXT: cstr (a[2], t1, sizeof t1); H.drawText (N (0, 0), N (1, 0), t1, N (3, -1)); break;
		case S_SLEEP:
		{
			int secs = N (0, 0);
			char k[4];
			if (secs <= 0) { while (!H.inkey (k)) { if (!H.poll ()) { ended = true; break; } H.sleepMs (20); } }
			else
			{
				double t0 = H.timer ();
				for (;;)
				{
					if (H.inkey (k) || !H.poll ()) break;
					double el = H.timer () - t0; if (el < 0) el += 86400;
					if (el >= secs) break;
					H.sleepMs (20);
				}
			}
			break;
		}
		case S_PAUSE: H.sleepMs (N (0, 0)); if (!H.poll ()) ended = true; break;
		case S_WIDTH: break;
		case S_BEEP: tone (0, 880, 250, 1); break;
		case S_SOUND:					// SOUND freq, duration (1/18.2 s ticks)
		{
			double d = a[1].n;
			if (d <= 0) { H.note (0, 0, 0, 0); break; }
			if (a[0].n < 37 || a[0].n > 32767) { fail ("Illegal function call (SOUND frequency 37..32767)"); break; }
			tone (0, a[0].n, (int) (d * 1000 / 18.2), 1);
			break;
		}
		case S_NOTEON:
			if (H.note (N (0, 0), a[1].n, N (2, 0), N (3, 200)) < 0) fail ("The audio output is not available (another program uses it?)");
			break;
		case S_NOTEOFF:
			if (argc == 0) for (int v = 0; v < 16; v++) H.note (v, 0, 0, 0);
			else H.note (N (0, 0), 0, 0, 0);
			break;
		case S_PLAY: { int n; const char *s = sdata (a[0], &n); play (s, n); break; }
		case S_RANDOMIZE: rnd = argc ? (unsigned) (long long) a[0].n : H.seed (); break;
		case S_WINDOW: cstr (a[0], t1, sizeof t1); H.window (t1, N (1, 0), N (2, 0)); break;
		case S_SETTEXT: cstr (a[1], t1, sizeof t1); H.setText (N (0, 0), t1); break;
		case S_SETVALUE: H.setValue (N (0, 0), N (1, 0)); break;
		case S_NOTIFY: cstr (a[0], t1, sizeof t1); cstr (a[1], t2, sizeof t2); H.notify (t1, t2); break;
		case S_SETCLIPBOARD: cstr (a[0], t1, sizeof t1); H.setClipboard (t1); break;
		case S_EXEC: cstr (a[0], t1, sizeof t1); if (argc > 1) cstr (a[1], t2, sizeof t2); else t2[0] = 0; if (!H.exec (t1, t2)) fail ("Cannot run that program"); break;
		case S_LAUNCH: cstr (a[0], t1, sizeof t1); if (!H.launch (t1)) fail ("No such app"); break;
		case S_KILL: cstr (a[0], t1, sizeof t1); if (!H.remove (t1)) fail ("File not found"); break;
		case S_RMDIR: cstr (a[0], t1, sizeof t1); if (!H.remove (t1)) fail ("Path not found"); break;
		case S_MKDIR: cstr (a[0], t1, sizeof t1); if (!H.makeDir (t1)) fail ("Path/File access error"); break;
		case S_NAME: cstr (a[0], t1, sizeof t1); cstr (a[1], t2, sizeof t2); if (!H.rename (t1, t2)) fail ("File not found"); break;
		default: fail ("Unknown statement");
		}
		for (int i = 0; i < argc; i++) vclear (a[i]);
	}

	// ---- sound ------------------------------------------------------------------------------------------------
	// A note of ms milliseconds (then silence), blocking.
	void tone (int voice, double freq, int ms, int wave)
	{
		if (H.note (voice, freq, wave, 200) < 0) { fail ("The audio output is not available (another program uses it?)"); return; }
		H.sleepMs (ms);
		H.note (voice, 0, 0, 0);
		if (!H.poll ()) ended = true;
	}
	// PLAY: QBasic's music macro language -- A..G (+ # or - after), a length (1 = whole .. 64)
	// and dots, O octave (0..6, default 4; O3 A = 440 Hz), < >, L length, T tempo (quarter
	// notes per minute, default 120), P / R pause, N note (1..84, 0 = pause), MN / ML / MS
	// (normal 7/8, legato, staccato 3/4), MF / MB (ignored: always in the foreground).
	int playOct = 4, playLen = 4, playTempo = 120, playStyle = 0;
	static double midiFreq (int midi)
	{
		double f = 440; int d = midi - 69;
		static const double semi[12] = { 1, 1.0594630943592953, 1.122462048309373, 1.189207115002721, 1.2599210498948732,
			1.3348398541700344, 1.4142135623730951, 1.4983070768766815, 1.5874010519681994, 1.681792830507429,
			1.7817974362806785, 1.8877486253633868 };
		while (d >= 12) { f *= 2; d -= 12; }
		while (d < 0) { f /= 2; d += 12; }
		return f * semi[d];
	}
	void play (const char *s, int n)
	{
		int i = 0;
		auto num = [&] (int def) { if (i >= n || s[i] < '0' || s[i] > '9') return def; int v = 0; while (i < n && s[i] >= '0' && s[i] <= '9') v = v * 10 + (s[i++] - '0'); return v; };
		auto up = [] (char c) { return (c >= 'a' && c <= 'z') ? (char) (c - 32) : c; };
		static const int semis[7] = { 9, 11, 0, 2, 4, 5, 7 };		// A B C D E F G
		while (i < n && !failed && !ended)
		{
			char c = up (s[i++]);
			if (c == ' ' || c == ';' || c == ',') continue;
			int midi = -1, len = playLen;
			if (c >= 'A' && c <= 'G')
			{
				int k = semis[c - 'A'];
				if (i < n && (s[i] == '#' || s[i] == '+')) { k++; i++; }
				else if (i < n && s[i] == '-') { k--; i++; }
				midi = 12 * (playOct + 2) + k;
				len = num (playLen);
			}
			else if (c == 'N') { int v = num (0); if (v > 0) midi = v + 23; len = playLen; }
			else if (c == 'P' || c == 'R') { len = num (playLen); }
			else if (c == 'O') { playOct = num (4); if (playOct > 6) playOct = 6; continue; }
			else if (c == '<') { if (playOct > 0) playOct--; continue; }
			else if (c == '>') { if (playOct < 6) playOct++; continue; }
			else if (c == 'L') { playLen = num (4); if (playLen < 1) playLen = 1; continue; }
			else if (c == 'T') { playTempo = num (120); if (playTempo < 32) playTempo = 32; if (playTempo > 255) playTempo = 255; continue; }
			else if (c == 'M')
			{
				char m = i < n ? up (s[i++]) : 0;
				if (m == 'N') playStyle = 0; else if (m == 'L') playStyle = 1; else if (m == 'S') playStyle = 2;
				continue;
			}
			else { fail ("Illegal function call (PLAY string)"); return; }
			if (len < 1) len = 1;
			double ms = 240000.0 / playTempo / len;			// whole note = 4 beats
			double dot = ms;
			while (i < n && s[i] == '.') { dot /= 2; ms += dot; i++; }
			if (midi < 0) { H.sleepMs ((int) ms); continue; }
			double on = playStyle == 1 ? ms : playStyle == 2 ? ms * 3 / 4 : ms * 7 / 8;
			if (H.note (0, midiFreq (midi), 2, 200) < 0) { fail ("The audio output is not available (another program uses it?)"); return; }
			H.sleepMs ((int) on);
			H.note (0, 0, 0, 0);
			if (ms > on) H.sleepMs ((int) (ms - on));
			if (!H.poll ()) ended = true;
		}
	}

	// ---- files ----------------------------------------------------------------------------------------------
	void closeFile (int n)
	{
		File &f = files[n];
		if (!f.open) return;
		if (f.mode != 1 && !H.save (f.path, f.buf ? f.buf : "", f.len)) fail ("Cannot write the file");
		delete [] f.buf; f.buf = 0; f.open = false;
	}
	void closeAll () { for (int i = 1; i < MAXFILES; i++) if (files[i].open) closeFile (i); }

	// ---- the loop ---------------------------------------------------------------------------------------------
	int run ()
	{
		const int *code = P->code.d;
		unsigned count = 0;
		while (!failed && !ended)
		{
			if ((++count & 4095) == 0 && !H.poll ()) { ended = true; break; }
			opPc = pc;
			int op = code[pc++];
			switch (op)
			{
			case OP_NUM: pushN (P->nums[code[pc++]]); break;
			case OP_STR: { int k = code[pc++]; pushS (P->strs[k], P->strl[k]); break; }
			case OP_LDG: case OP_LDL: { V v = slot (op == OP_LDG, code[pc++]); vretain (v); push (v); break; }
			case OP_STG: case OP_STL: { V v = pop (); V &d = slot (op == OP_STG, code[pc++]); vclear (d); d = v; break; }
			case OP_ALDG: case OP_ALDL:
			{
				int s = code[pc++], nd = code[pc++];
				V *e = element (op == OP_ALDG, s, nd);
				if (e) { V v = *e; vretain (v); push (v); }
				break;
			}
			case OP_ASTG: case OP_ASTL:
			{
				int s = code[pc++], nd = code[pc++];
				V v = pop ();
				V *e = element (op == OP_ASTG, s, nd);
				if (e) { vclear (*e); *e = v; } else vclear (v);
				break;
			}
			case OP_DIMG: case OP_DIML:
			{
				int s = code[pc++], nd = code[pc++], isStr = code[pc++];
				int lo[4], hi[4];
				for (int i = nd - 1; i >= 0; i--) { hi[i] = (int) popI (); lo[i] = (int) popI (); }
				if (failed) break;
				Arr *a = newArr (nd, lo, hi, isStr != 0);
				if (!a) break;
				V &d = slot (op == OP_DIMG, s); vclear (d); d.t = VA; d.p = a;
				break;
			}
			case OP_ERASEG: case OP_ERASEL: { V &d = slot (op == OP_ERASEG, code[pc++]); vclear (d); break; }
			case OP_REFG: case OP_REFL:
			{
				int s = code[pc++];
				V *target = op == OP_REFG ? &G[s] : &frames[nf - 1].loc[s];
				if (target->t == VR) target = (V *) target->p;
				V r; r.t = VR; r.n = 0; r.p = target; push (r);
				break;
			}
			case OP_ADD: { double b = popN (), a = popN (); pushN (a + b); break; }
			case OP_SUB: { double b = popN (), a = popN (); pushN (a - b); break; }
			case OP_MUL: { double b = popN (), a = popN (); pushN (a * b); break; }
			case OP_DIV: { double b = popN (), a = popN (); if (b == 0) fail ("Division by zero"); else pushN (a / b); break; }
			case OP_IDIV: { long long b = popI (), a = popI (); if (b == 0) fail ("Division by zero"); else pushN ((double) (a / b)); break; }
			case OP_MOD: { long long b = popI (), a = popI (); if (b == 0) fail ("Division by zero"); else pushN ((double) (a % b)); break; }
			case OP_POW: { double b = popN (), a = popN (); bool ok; double r = npow (a, b, &ok); if (!ok) fail ("Illegal function call (power)"); else pushN (r); break; }
			case OP_NEG: { double a = popN (); pushN (-a); break; }
			case OP_CAT:
			{
				V b = pop (), a = pop ();
				int la, lb; const char *sa = sdata (a, &la), *sb = sdata (b, &lb);
				Str *x = 0;
				if (la + lb > 0)
				{
					x = (Str *) new char[sizeof (Str) + la + lb]; x->ref = 1; x->len = la + lb;
					bmcpy (x->d, sa, la); bmcpy (x->d + la, sb, lb); x->d[la + lb] = 0;
				}
				vclear (a); vclear (b); pushStr (x);
				break;
			}
			case OP_EQ: { double b = popN (), a = popN (); pushN (a == b ? -1 : 0); break; }
			case OP_NE: { double b = popN (), a = popN (); pushN (a != b ? -1 : 0); break; }
			case OP_LT: { double b = popN (), a = popN (); pushN (a < b ? -1 : 0); break; }
			case OP_GT: { double b = popN (), a = popN (); pushN (a > b ? -1 : 0); break; }
			case OP_LE: { double b = popN (), a = popN (); pushN (a <= b ? -1 : 0); break; }
			case OP_GE: { double b = popN (), a = popN (); pushN (a >= b ? -1 : 0); break; }
			case OP_SEQ: case OP_SNE: case OP_SLT: case OP_SGT: case OP_SLE: case OP_SGE:
			{
				V b = pop (), a = pop ();
				int la, lb; const char *sa = sdata (a, &la), *sb = sdata (b, &lb);
				int c = 0, i = 0;
				while (i < la && i < lb && sa[i] == sb[i]) i++;
				if (i < la && i < lb) c = (unsigned char) sa[i] < (unsigned char) sb[i] ? -1 : 1;
				else c = la < lb ? -1 : la > lb ? 1 : 0;
				bool r = op == OP_SEQ ? c == 0 : op == OP_SNE ? c != 0 : op == OP_SLT ? c < 0 : op == OP_SGT ? c > 0 : op == OP_SLE ? c <= 0 : c >= 0;
				vclear (a); vclear (b); pushN (r ? -1 : 0);
				break;
			}
			case OP_AND: { long long b = popI (), a = popI (); pushN ((double) (a & b)); break; }
			case OP_OR:  { long long b = popI (), a = popI (); pushN ((double) (a | b)); break; }
			case OP_XOR: { long long b = popI (), a = popI (); pushN ((double) (a ^ b)); break; }
			case OP_EQV: { long long b = popI (), a = popI (); pushN ((double) ~(a ^ b)); break; }
			case OP_IMP: { long long b = popI (), a = popI (); pushN ((double) (~a | b)); break; }
			case OP_NOT: { long long a = popI (); pushN ((double) ~a); break; }
			case OP_JMP: pc = code[pc]; break;
			case OP_JZ:  { int t = code[pc++]; if (popN () == 0) pc = t; break; }
			case OP_JNZ: { int t = code[pc++]; if (popN () != 0) pc = t; break; }
			case OP_CALL:
			{
				int pi = code[pc++], argc = code[pc++];
				const ProcInfo &pr = P->procs[pi];
				if (nf >= MAXFRAMES) { fail ("Out of stack space (too deep recursion)"); break; }
				Frame &f = frames[nf];
				f.ret = pc; f.proc = pi; f.nloc = pr.nlocals > 0 ? pr.nlocals : 1;
				f.loc = new V[f.nloc];
				for (int i = 0; i < f.nloc; i++) initSlot (f.loc[i], i < pr.nlocals ? (int) P->lkind[pr.kindOff + i] : (int) K_NUM);
				int first = pr.isFunc ? 1 : 0;
				for (int i = argc - 1; i >= 0; i--) { V v = pop (); vclear (f.loc[first + i]); f.loc[first + i] = v; }
				nf++;
				pc = pr.entry;
				break;
			}
			case OP_RET: if (nf == 0) { fail ("RETURN outside a SUB"); break; } pc = frames[nf - 1].ret; popFrame (); break;
			case OP_RETF:
			{
				if (nf == 0) { fail ("RETURN outside a FUNCTION"); break; }
				V r = frames[nf - 1].loc[0]; frames[nf - 1].loc[0].t = VN; frames[nf - 1].loc[0].p = 0;
				pc = frames[nf - 1].ret; popFrame (); push (r);
				break;
			}
			case OP_GOSUB:
				if (ngs >= MAXGOSUB) { fail ("Out of stack space (GOSUB)"); break; }
				gosubs[ngs].ret = pc + 1; gosubs[ngs].frame = nf; ngs++;
				pc = code[pc];
				break;
			case OP_RETSUB:
				if (ngs == 0 || gosubs[ngs - 1].frame != nf) { fail ("RETURN without GOSUB"); break; }
				pc = gosubs[--ngs].ret;
				break;
			case OP_POP: { V v = pop (); vclear (v); break; }
			case OP_BI: { int id = code[pc++], argc = code[pc++]; builtin (id, argc); break; }
			case OP_ST: { int id = code[pc++], argc = code[pc++]; statement (id, argc); break; }
			case OP_PRINT: { int w = code[pc++]; V v = pop (); printValue (v, w != 0); vclear (v); break; }
			case OP_PRSEP:
			{
				int k = code[pc++];
				if (k == 2) out ("\n", 1);
				else if (k == 3) out (",", 1);
				else
				{
					int col = column (), next = ((col - 1) / 14 + 1) * 14 + 1;
					if (!chan && next > H.width ()) out ("\n", 1); else spaces (next - col);
				}
				break;
			}
			case OP_PRTAB:
			{
				long long n = popI (); int col = column ();
				if (n < 1) n = 1;
				if (n < col) { out ("\n", 1); col = 1; }
				spaces ((int) n - col);
				break;
			}
			case OP_PRSPC: { long long n = popI (); if (n > 0) spaces ((int) n); break; }
			case OP_CHAN: { long long n = popI (); if (n != 0 && !file (n)) break; chan = (int) n; break; }
			case OP_INPUT:
			{
				int pk = code[pc++], flags = code[pc++];
				if (!readLine (inBuf, sizeof inBuf, pk >= 0 ? P->strs[pk] : "", pk >= 0 ? P->strl[pk] : 0, flags & 1)) break;
				inPos = 0;
				break;
			}
			case OP_INFIELD:
			{
				int ty = code[pc++]; char f[512]; int n;
				if (chan)
				{
					File *fl = file (chan);
					if (!fl) break;
					if (fl->mode != 1) { fail ("Bad file mode (not opened FOR INPUT)"); break; }
					if (fl->pos >= fl->len) { fail ("Input past end of file"); break; }
					n = field (fl->buf, fl->len, &fl->pos, f, sizeof f, ty == TY_NUM);
				}
				else n = field (inBuf, bslen (inBuf), &inPos, f, sizeof f);
				if (ty == TY_STR) pushS (f, n); else pushN (parseNum (f, 0));
				break;
			}
			case OP_LINPUT:
			{
				int pk = code[pc++];
				if (chan)
				{
					File *fl = file (chan);
					if (!fl) break;
					if (fl->mode != 1) { fail ("Bad file mode (not opened FOR INPUT)"); break; }
					if (fl->pos >= fl->len) { fail ("Input past end of file"); break; }
					int st = fl->pos; while (fl->pos < fl->len && fl->buf[fl->pos] != '\n') fl->pos++;
					int e = fl->pos; if (fl->pos < fl->len) fl->pos++;
					if (e > st && fl->buf[e - 1] == '\r') e--;
					pushS (fl->buf + st, e - st);
				}
				else
				{
					char line[512];
					if (!readLine (line, sizeof line, pk >= 0 ? P->strs[pk] : "", pk >= 0 ? P->strl[pk] : 0, false)) break;
					pushS (line, bslen (line));
				}
				break;
			}
			case OP_READ:
			{
				int ty = code[pc++];
				if (dataPtr >= P->data.n) { fail ("Out of DATA"); break; }
				const DataItem &d = P->data[dataPtr++];
				if (ty == TY_STR) pushS (d.text, bslen (d.text));
				else { if (d.isStr) { fail ("Type mismatch (READ a string into a number)"); break; } pushN (parseNum (d.text, 0)); }
				break;
			}
			case OP_RESTORE: dataPtr = code[pc++]; break;
			case OP_OPEN:
			{
				int mode = code[pc++];
				long long n = popI ();
				V pv = pop ();
				char path[240]; cstr (pv, path, sizeof path); vclear (pv);
				File *f = file (n, false);
				if (!f) break;
				if (f->open) { fail ("File already open"); break; }
				f->mode = mode; f->len = f->cap = f->pos = 0; f->col = 1; f->buf = 0;
				bscpy (f->path, path, sizeof f->path);
				if (mode == 1 || mode == 3)
				{
					int len = 0; char *b = H.load (path, &len);
					if (!b && mode == 1) { fail ("File not found"); break; }
					f->buf = b; f->len = len; f->cap = b ? len : 0;
				}
				f->open = true;
				break;
			}
			case OP_CLOSE:
			{
				long long n = popI ();
				if (n == 0) closeAll ();
				else { File *f = file (n, false); if (f) closeFile ((int) n); }
				break;
			}
			case OP_END: case OP_STOP: ended = true; break;
			case OP_NOP: break;
			default: fail ("Bad bytecode"); break;
			}
		}
		bool wasFailed = failed;
		failed = false;
		chan = 0;
		closeAll ();
		if (failed && !wasFailed) wasFailed = true;
		H.finished (wasFailed);
		return wasFailed ? -1 : 0;
	}
};

int run (Program *p, Host &host, Error *err)
{
	err->line = 0; err->msg[0] = 0;
	VM *vm = new VM (p, host, err);
	int r = vm->run ();
	delete vm;
	return r;
}

} // namespace bas
