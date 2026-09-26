//
// basic/basvm.cpp -- the Onyx BASIC virtual machine: runs a compiled bas::Program.
//
// A stack machine over tagged values: numbers (double), strings (reference-counted,
// immutable), arrays (reference-counted, so array arguments are shared), records (user
// TYPEs: reference-counted but copied before a store when shared -- value semantics) and
// references (a by-ref argument, or the address of a field / element, points at a V).
// Each SUB / FUNCTION call gets a frame of locals. Everything outside goes through the
// bas::Host.
//
// Errors: a runtime error fails the current op; before the next one the VM looks for a
// "resume point" -- like setjmp / longjmp: ON ERROR GOTO sets one at the module level; the
// error unwinds the frames (releasing their values) and the value stack to it, and jumps
// to the handler, which can RESUME (the statement again), RESUME NEXT (the one after) or
// RESUME label. Without one the program stops with the message and the line.
//
#include "basic/basint.h"
#include "basic/basnum.h"

namespace bas {

enum { VN = 0, VS, VA, VR, VT };

struct Str { int ref; int len; char d[1]; };
struct V { int t; double n; void *p; };
struct Arr { int ref; int nd; int lo[4]; int cnt[4]; int total; int ek; int ext; V *e; };	// ek 0 num, 1 str, 2 record
struct Rec { int ref; int type; int nf; V f[1]; };

static Str *snew (const char *s, int len)
{
	if (len <= 0) return 0;
	Str *x = (Str *) new char[sizeof (Str) + len];
	x->ref = 1; x->len = len; bmcpy (x->d, s, len); x->d[len] = 0;
	return x;
}
static Str *sfill (int len, char c)
{
	if (len <= 0) return 0;
	Str *x = (Str *) new char[sizeof (Str) + len];
	x->ref = 1; x->len = len; for (int i = 0; i < len; i++) x->d[i] = c; x->d[len] = 0;
	return x;
}
static void srel (Str *s) { if (s && --s->ref == 0) delete [] (char *) s; }
static void arel (Arr *a);
static void rrel (Rec *r);
static inline void vclear (V &v)
{
	if (v.t == VS) srel ((Str *) v.p);
	else if (v.t == VA) arel ((Arr *) v.p);
	else if (v.t == VT) rrel ((Rec *) v.p);
	v.t = VN; v.n = 0; v.p = 0;
}
static void arel (Arr *a)
{
	if (!a || --a->ref > 0) return;
	for (int i = 0; i < a->total; i++) vclear (a->e[i]);
	delete [] a->e; delete a;
}
static void rrel (Rec *r)
{
	if (!r || --r->ref > 0) return;
	for (int i = 0; i < r->nf; i++) vclear (r->f[i]);
	delete [] (char *) r;
}
static inline void vretain (const V &v)
{
	if (v.t == VS && v.p) ((Str *) v.p)->ref++;
	else if (v.t == VA) ((Arr *) v.p)->ref++;
	else if (v.t == VT) ((Rec *) v.p)->ref++;
}
static inline const char *sdata (const V &v, int *len)
{
	if (v.t != VS || !v.p) { *len = 0; return ""; }
	*len = ((Str *) v.p)->len; return ((Str *) v.p)->d;
}
static Rec *recAlloc (int type, int nf)
{
	Rec *r = (Rec *) new char[sizeof (Rec) + (nf > 1 ? nf - 1 : 0) * sizeof (V)];
	r->ref = 1; r->type = type; r->nf = nf;
	for (int i = 0; i < nf; i++) { r->f[i].t = VN; r->f[i].n = 0; r->f[i].p = 0; }
	return r;
}
static Rec *recCopy (const Rec *s)
{
	Rec *r = recAlloc (s->type, s->nf);
	for (int i = 0; i < s->nf; i++)
	{
		const V &f = s->f[i];
		if (f.t == VT) { r->f[i].t = VT; r->f[i].p = recCopy ((const Rec *) f.p); }
		else { r->f[i] = f; vretain (f); }
	}
	return r;
}
// Before a store: a record shared with another variable gets its own copy.
static inline void own (V &v)
{
	if (v.t != VT) return;
	Rec *r = (Rec *) v.p;
	if (r->ref > 1) { Rec *c = recCopy (r); r->ref--; v.p = c; }
}

// Round half to even (QBasic's CINT / INTEGER stores).
static double roundEven (double x)
{
	double f = nfloor (x), d = x - f;
	if (d > 0.5) return f + 1;
	if (d < 0.5) return f;
	long long fi = (long long) f;
	return (fi & 1) ? f + 1 : f;
}

// ---- error codes (QBasic's) ------------------------------------------------------------------------
static const struct { int code; const char *msg; } ERRS[] = {
	{ 1, "NEXT without FOR" }, { 2, "Syntax error" }, { 3, "RETURN without GOSUB" }, { 4, "Out of DATA" },
	{ 5, "Illegal function call" }, { 6, "Overflow" }, { 7, "Out of memory" }, { 8, "Label not defined" },
	{ 9, "Subscript out of range" }, { 10, "Duplicate definition" }, { 11, "Division by zero" },
	{ 13, "Type mismatch" }, { 14, "Out of string space" }, { 19, "No RESUME" }, { 20, "RESUME without error" },
	{ 28, "Out of stack space" }, { 50, "FIELD overflow" }, { 51, "Internal error" }, { 52, "Bad file name or number" },
	{ 53, "File not found" }, { 54, "Bad file mode" }, { 55, "File already open" }, { 57, "Device I/O error" },
	{ 58, "File already exists" }, { 59, "Bad record length" }, { 61, "Disk full" }, { 62, "Input past end of file" },
	{ 63, "Bad record number" }, { 64, "Bad file name" }, { 67, "Too many files" }, { 70, "Permission denied" },
	{ 73, "Advanced feature unavailable" }, { 75, "Path/File access error" }, { 76, "Path not found" }, { 0, 0 } };
static const char *errMessage (int code)
{
	for (int i = 0; ERRS[i].msg; i++) if (ERRS[i].code == code) return ERRS[i].msg;
	return "Unprintable error";
}
static bool startsWith (const char *s, const char *p) { while (*p && *s == *p) { s++; p++; } return *p == 0; }
static int errCodeOf (const char *msg)
{
	static const struct { const char *pre; int code; } M[] = {
		{ "Illegal function call", 5 }, { "Overflow", 6 }, { "Out of memory", 7 }, { "Array too big", 7 },
		{ "Subscript out of range", 9 }, { "Wrong number of subscripts", 9 }, { "Too many subscripts", 9 },
		{ "Division by zero", 11 }, { "Type mismatch", 13 }, { "Out of stack space", 28 }, { "RETURN without GOSUB", 3 },
		{ "Out of DATA", 4 }, { "Bad file", 52 }, { "File not open", 52 }, { "File not found", 53 }, { "Bad file mode", 54 },
		{ "File already open", 55 }, { "Input past end of file", 62 }, { "Too many files", 67 }, { "Path/File access error", 75 },
		{ "Cannot write the file", 75 }, { "Path not found", 76 }, { "FIELD overflow", 50 }, { "Bad record number", 63 },
		{ "Bad record length", 59 }, { "RESUME without error", 20 }, { "Duplicate definition", 10 },
		{ "The audio output", 5 }, { "No such app", 53 }, { "Cannot run", 53 }, { 0, 0 } };
	for (int i = 0; M[i].pre; i++) if (startsWith (msg, M[i].pre)) return M[i].code;
	return 51;
}

// US scan codes of the ASCII keys (ON KEY user keys, KEY 15..25).
static int scanOf (unsigned char c)
{
	static const char *const rows[] = { "1234567890-=", "qwertyuiop[]", "asdfghjkl;'`", "\\zxcvbnm,./" };
	static const int first[] = { 2, 16, 30, 43 };
	if (c >= 'A' && c <= 'Z') c = (unsigned char) (c + 32);
	if (c == 27) return 1;
	if (c == 8) return 14;
	if (c == 9) return 15;
	if (c == 13) return 28;
	if (c == ' ') return 57;
	for (int r = 0; r < 4; r++) for (int i = 0; rows[r][i]; i++) if ((unsigned char) rows[r][i] == c) return first[r] + i;
	return 0;
}

class VM
{
public:
	Program *P; Host &H; Error *err;
	enum { STACK = 2048, MAXFRAMES = 400, MAXGOSUB = 256, MAXFILES = 16, MAXENV = 32, MAXBIND = 32 };
	V stack[STACK]; int sp;
	V *G;
	struct Frame { int ret; V *loc; int nloc; int proc; };
	Frame frames[MAXFRAMES]; int nf;
	struct GoSub { int ret; int frame; int chan; int ev; };
	GoSub gosubs[MAXGOSUB]; int ngs;
	int pc, opPc; bool failed, ended;
	unsigned rnd; double lastRnd;
	int dataPtr;
	int chan;				// current PRINT / INPUT channel (0 = screen)
	char inBuf[512]; int inPos;
	// errors
	int errCode, errLine, errPc, onErr; bool inErr; char errMsgSaved[120]; int errLineSaved;
	// files
	struct Bind { V *ptr; int off, w; };
	struct File
	{
		bool open; int mode; char *buf; int len, cap, pos, col; char path[240];
		int reclen; char *rec; int lastRec, nextRec; bool eof; Bind b[MAXBIND]; int nb;
	};
	File files[MAXFILES];
	char *env[MAXENV]; int nenv;
	// graphics
	int mode, scrW, scrH;
	bool viewOn, viewScreen; int vx1, vy1, vx2, vy2;
	bool winOn, winScreen; double wx1, wy1, wx2, wy2;
	double lastX, lastY;
	int drawAngle, drawTA, drawScale, drawColor;
	// events: 0 = TIMER, 1..31 = KEY(n)
	struct Ev { int target; int state; bool pending, busy; };
	Ev ev[32]; bool evAny, evKick; double timerInt, timerNext; int keyScan[32];
	// CHAIN / RUN / TRON
	char chainPath[240]; bool chainCommon; bool tron; int traceLine;
	// GET / PUT # serialisation buffer
	char *ser; int serLen, serCap;

	VM (Program *p, Host &h, Error *e) : P (p), H (h), err (e), sp (0), G (0), nf (0), ngs (0), pc (0), opPc (0),
		failed (false), ended (false), rnd (327680), lastRnd (0), dataPtr (0), chan (0), inPos (0),
		errCode (0), errLine (0), errPc (0), onErr (-1), inErr (false), errLineSaved (0), nenv (0),
		mode (0), scrW (640), scrH (400), viewOn (false), viewScreen (false), vx1 (0), vy1 (0), vx2 (0), vy2 (0),
		winOn (false), winScreen (false), wx1 (0), wy1 (0), wx2 (1), wy2 (1), lastX (0), lastY (0),
		drawAngle (0), drawTA (0), drawScale (4), drawColor (-1), evAny (false), evKick (false), timerInt (0), timerNext (0),
		chainCommon (false), tron (false), traceLine (-1), ser (0), serLen (0), serCap (0)
	{
		inBuf[0] = 0; errMsgSaved[0] = 0; chainPath[0] = 0;
		for (int i = 0; i < MAXFILES; i++) { files[i].open = false; files[i].buf = 0; files[i].rec = 0; }
		for (int i = 0; i < 32; i++) { ev[i].target = -1; ev[i].state = 0; ev[i].pending = ev[i].busy = false; keyScan[i] = 0; }
		G = new V[P->nglobals > 0 ? P->nglobals : 1];
		for (int i = 0; i < P->nglobals; i++) initSlot (G[i], P->gkind[i], P->gext[i]);
		H.screenSize (&scrW, &scrH);
	}
	~VM ()
	{
		while (sp > 0) vclear (stack[--sp]);
		while (nf > 0) popFrame ();
		for (int i = 0; i < P->nglobals; i++) vclear (G[i]);
		delete [] G;
		for (int i = 0; i < MAXFILES; i++) { delete [] files[i].buf; delete [] files[i].rec; }
		for (int i = 0; i < nenv; i++) delete [] env[i];
		delete [] ser;
	}

	// ---- values -------------------------------------------------------------------------------------
	Rec *newRec (int type)
	{
		const TypeInfo &t = P->types[type];
		Rec *r = recAlloc (type, t.nf);
		for (int i = 0; i < t.nf; i++)
		{
			const FieldInfo &f = P->fields[t.first + i];
			V &v = r->f[i];
			if (f.kind == FK_VSTR) v.t = VS;
			else if (f.kind == FK_FSTR) { v.t = VS; v.p = sfill (f.len, ' '); }
			else if (f.kind == FK_REC) { v.t = VT; v.p = newRec (f.sub); }
		}
		return r;
	}
	void initSlot (V &v, int kind, int ext)
	{
		v.t = VN; v.n = 0; v.p = 0;
		if (kind == K_STR) { v.t = VS; if (ext > 0) v.p = sfill (ext, ' '); }
		else if (kind == K_REC) { v.t = VT; v.p = newRec (ext); }
	}

	// ---- errors --------------------------------------------------------------------------------
	void fail (const char *msg, int code = 0)
	{
		if (failed) return;
		failed = true;
		errCode = code ? code : errCodeOf (msg);
		err->line = P->lineAt (opPc);
		bscpy (err->msg, msg, sizeof err->msg);
	}
	// ERL: the line number label before the error, or its source line.
	int erlOf (int at)
	{
		if (P->numLabels.n == 0) return P->lineAt (at);
		int v = 0;
		for (int i = 0; i < P->numLabels.n; i++) if (P->numLabels[i].pc <= at) v = P->numLabels[i].value;
		return v;
	}
	// The resume point: an error goes to the ON ERROR handler (at the module level),
	// unwinding the SUB / FUNCTION frames and the value stack. False = fatal.
	bool trap ()
	{
		if (onErr < 0 || inErr || errCode == 51) return false;
		errLine = erlOf (opPc);
		errPc = nf > 0 ? frames[0].ret - 1 : opPc;		// the module statement that failed
		bscpy (errMsgSaved, err->msg, sizeof errMsgSaved); errLineSaved = err->line;
		while (nf > 0) popFrame ();
		while (sp > 0) vclear (stack[--sp]);
		chan = 0;
		inErr = true; failed = false; err->line = 0; err->msg[0] = 0;
		pc = onErr;
		return true;
	}
	// The innermost statement containing a pc.
	int stmtOf (int at, bool end)
	{
		int best = -1, bs = -1;
		for (int i = 0; i < P->stmts.n; i++)
		{
			const StmtRange &s = P->stmts[i];
			if (s.start <= at && at < s.end && s.start > bs) { bs = s.start; best = i; }
		}
		if (best < 0) return at;
		return end ? P->stmts[best].end : P->stmts[best].start;
	}

	// ---- stack --------------------------------------------------------------------------------------
	void push (const V &v) { if (sp >= STACK) { fail ("Out of stack space"); V x = v; vclear (x); return; } stack[sp++] = v; }
	void pushN (double n) { V v; v.t = VN; v.n = n; v.p = 0; push (v); }
	void pushS (const char *s, int len) { V v; v.t = VS; v.n = 0; v.p = snew (s, len); push (v); }
	void pushStr (Str *s) { V v; v.t = VS; v.n = 0; v.p = s; push (v); }
	void pushRef (V *target) { V r; r.t = VR; r.n = 0; r.p = target; push (r); }
	V pop () { if (sp <= 0) { V v; v.t = VN; v.n = 0; v.p = 0; fail ("Stack underflow", 51); return v; } return stack[--sp]; }
	double popN () { V v = pop (); double n = v.n; if (v.t != VN) { vclear (v); fail ("Type mismatch"); } return n; }
	long long popI () { double d = popN (); return (long long) nfloor (d + 0.5); }
	V *popRef () { V v = pop (); if (v.t != VR) { vclear (v); fail ("Internal error (reference)", 51); return 0; } return (V *) v.p; }

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
	int extOf (bool global, int s)
	{
		if (global) return P->gext[s];
		const ProcInfo &pi = P->procs[frames[nf - 1].proc];
		return P->lext[pi.kindOff + s];
	}
	Arr *newArr (int nd, const int *lo, const int *hi, int ek, int ext)
	{
		Arr *a = new Arr; a->ref = 1; a->nd = nd; a->ek = ek; a->ext = ext; a->total = 1;
		for (int i = 0; i < nd; i++)
		{
			if (hi[i] < lo[i]) { delete a; fail ("Subscript out of range (DIM)"); return 0; }
			a->lo[i] = lo[i]; a->cnt[i] = hi[i] - lo[i] + 1; a->total *= a->cnt[i];
			if (a->total > 4000000) { delete a; fail ("Array too big"); return 0; }
		}
		a->e = new V[a->total];
		for (int i = 0; i < a->total; i++)
		{
			V &v = a->e[i]; v.t = VN; v.n = 0; v.p = 0;
			if (ek == 1) { v.t = VS; if (ext > 0) v.p = sfill (ext, ' '); }
			else if (ek == 2) { v.t = VT; v.p = newRec (ext); }
		}
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
			int k = kindOf (global, s);
			Arr *a = newArr (nd, lo, hi, k == K_RECARR ? 2 : k == K_STRARR ? 1 : 0, extOf (global, s));
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
	void store (V &d, V v) { own (v); vclear (d); d = v; }

	void popFrame ()
	{
		Frame &f = frames[--nf];
		for (int i = 0; i < f.nloc; i++) if (f.loc[i].t != VR) vclear (f.loc[i]);
		delete [] f.loc;
		while (ngs > 0 && gosubs[ngs - 1].frame > nf) popGosub ();
	}
	void popGosub ()
	{
		GoSub &g = gosubs[--ngs];
		if (g.ev >= 0) { ev[g.ev].busy = false; chan = g.chan; }
	}

	// ---- output ------------------------------------------------------------------------------------
	File *file (long long n, bool mustBeOpen = true)
	{
		if (n < 1 || n >= MAXFILES) { fail ("Bad file name or number"); return 0; }
		if (mustBeOpen && !files[n].open) { fail ("Bad file name or number (not open)"); return 0; }
		return &files[n];
	}
	void fensure (File *f, int need)
	{
		if (need + 1 <= f->cap) return;
		int nc = f->cap ? f->cap * 2 : 1024; while (nc < need + 1) nc *= 2;
		char *b = new char[nc];
		bmcpy (b, f->buf, f->len);
		for (int i = f->len; i < nc; i++) b[i] = 0;
		delete [] f->buf; f->buf = b; f->cap = nc;
	}
	void fwriteAt (File *f, int at, const char *s, int n)
	{
		fensure (f, at + n);
		for (int i = f->len; i < at; i++) f->buf[i] = 0;
		bmcpy (f->buf + at, s, n);
		if (at + n > f->len) f->len = at + n;
	}
	void fappend (File *f, const char *s, int n)
	{
		fwriteAt (f, f->len, s, n);
		for (int i = 0; i < n; i++) { if (s[i] == '\n') f->col = 1; else f->col++; }
	}
	void out (const char *s, int n)
	{
		if (chan)
		{
			File *f = file (chan);
			if (!f) return;
			if (f->mode == 1) { fail ("Bad file mode (opened FOR INPUT)"); return; }
			if (f->mode == 5) { fwriteAt (f, f->pos, s, n); f->pos += n; return; }
			if (f->mode == 4) { fail ("Bad file mode (a RANDOM file: use PUT)"); return; }
			fappend (f, s, n);
		}
		else H.out (s, n);
	}
	void outs (const char *s) { out (s, bslen (s)); }
	int column () { if (chan) { File *f = file (chan); return f ? f->col : 1; } return H.column (); }
	void spaces (int n) { char b[64]; for (int i = 0; i < 64; i++) b[i] = ' '; while (n > 0) { int k = n > 64 ? 64 : n; out (b, k); n -= k; } }

	void printValue (V &v, bool write, bool dbl)
	{
		if (v.t == VS) { int n; const char *s = sdata (v, &n); if (write) out ("\"", 1); out (s, n); if (write) out ("\"", 1); return; }
		if (v.t != VN) { fail ("Type mismatch"); return; }
		char b[48]; int n = 0;
		if (!write && v.n >= 0) b[n++] = ' ';
		n += formatNum (v.n, b + n, dbl);
		if (!write) b[n++] = ' ';
		out (b, n);
	}

	// ---- PRINT USING -------------------------------------------------------------------------------
	// A numeric field at f[i]: parse it (advancing i) and format v into o (returns length).
	int usingNum (double v, const char *f, int fl, int &i, char *o)
	{
		bool plusLead = false, star = false, dollar = false, comma = false, dot = false, expo = false;
		int ipos = 0, dec = 0; char trail = 0;
		if (f[i] == '+') { plusLead = true; i++; }
		if (i + 2 < fl && f[i] == '*' && f[i + 1] == '*' && f[i + 2] == '$') { star = dollar = true; ipos += 3; i += 3; }
		else if (i + 1 < fl && f[i] == '*' && f[i + 1] == '*') { star = true; ipos += 2; i += 2; }
		else if (i + 1 < fl && f[i] == '$' && f[i + 1] == '$') { dollar = true; ipos += 2; i += 2; }
		while (i < fl && (f[i] == '#' || f[i] == ',')) { if (f[i] == ',') comma = true; ipos++; i++; }
		if (i < fl && f[i] == '.') { dot = true; i++; while (i < fl && f[i] == '#') { dec++; i++; } }
		if (i + 3 < fl && f[i] == '^' && f[i + 1] == '^' && f[i + 2] == '^' && f[i + 3] == '^') { expo = true; i += 4; if (i < fl && f[i] == '^') i++; }
		if (i < fl && (f[i] == '+' || f[i] == '-') && !plusLead) { trail = f[i]; i++; }
		int width = (plusLead ? 1 : 0) + ipos + (dot ? 1 + dec : 0) + (trail ? 1 : 0) + (expo ? 4 : 0);
		bool neg = v < 0; double a = neg ? -v : v;
		char body[80]; int bl = 0;
		double scale = 1; for (int k = 0; k < dec; k++) scale *= 10;
		char num[64]; int nl = 0;
		if (expo)
		{
			int digits = ipos - (dollar ? 1 : 0) - (!plusLead && !trail ? 1 : 0);	// one position for the sign
			if (digits < 0) digits = 0;
			int e = 0;
			if (a != 0)
			{
				double m = a;
				while (m >= 10) { m /= 10; e++; }
				while (m < 1) { m *= 10; e--; }
				e -= digits > 0 ? digits - 1 : -1;
			}
			double m = a; for (int k = 0; k < (e > 0 ? e : -e); k++) m = e > 0 ? m / 10 : m * 10;
			long long sc = (long long) (m * scale + 0.5);
			long long lim = 1; for (int k = 0; k < digits; k++) lim *= 10;
			if (sc >= lim * (long long) scale && digits > 0) { sc = (long long) (m / 10 * scale + 0.5); e++; }
			long long ip = sc / (long long) scale, fp = sc % (long long) scale;
			char t[24]; int tn = 0;
			if (ip == 0 && digits > 0) t[tn++] = '0';
			while (ip) { t[tn++] = (char) ('0' + ip % 10); ip /= 10; }
			while (tn) num[nl++] = t[--tn];
			if (dot) { num[nl++] = '.'; for (int k = dec - 1; k >= 0; k--) { long long p10 = 1; for (int q = 0; q < k; q++) p10 *= 10; num[nl++] = (char) ('0' + fp / p10 % 10); } }
			num[nl++] = 'E'; num[nl++] = e < 0 ? '-' : '+';
			int ae = e < 0 ? -e : e;
			num[nl++] = (char) ('0' + ae / 10 % 10); num[nl++] = (char) ('0' + ae % 10);
		}
		else
		{
			if (a >= 1e15) { nl = formatNum (a, num); }
			else
			{
				long long sc = (long long) (a * scale + 0.5);
				long long ip = sc / (long long) scale, fp = sc % (long long) scale;
				char t[40]; int tn = 0, cnt = 0;
				if (ip == 0) { if (ipos - (dollar ? 1 : 0) > (neg && !plusLead && !trail ? 1 : 0)) t[tn++] = '0'; }
				while (ip) { if (comma && cnt && cnt % 3 == 0) t[tn++] = ','; t[tn++] = (char) ('0' + ip % 10); ip /= 10; cnt++; }
				while (tn) num[nl++] = t[--tn];
				if (dot) { num[nl++] = '.'; for (int k = dec - 1; k >= 0; k--) { long long p10 = 1; for (int q = 0; q < k; q++) p10 *= 10; num[nl++] = (char) ('0' + fp / p10 % 10); } }
			}
			if (sc0 (a, scale) == 0) neg = false;
		}
		if (plusLead) body[bl++] = neg ? '-' : '+';
		else if (neg && !trail) body[bl++] = '-';
		if (dollar) body[bl++] = '$';
		for (int k = 0; k < nl; k++) body[bl++] = num[k];
		char tr[2] = { 0, 0 };
		if (trail == '+') tr[0] = neg ? '-' : '+';
		else if (trail == '-') tr[0] = neg ? '-' : ' ';
		int total = bl + (tr[0] ? 1 : 0);
		int n = 0;
		if (total > width) o[n++] = '%';
		else
		{
			int pad = width - total;
			// the sign / $ float next to the digits: padding first
			for (int k = 0; k < pad; k++) o[n++] = star ? '*' : ' ';
		}
		for (int k = 0; k < bl; k++) o[n++] = body[k];
		if (tr[0]) o[n++] = tr[0];
		return n;
	}
	static long long sc0 (double a, double scale) { return (long long) (a * scale + 0.5); }
	static bool numStart (const char *f, int fl, int i)
	{
		char c = f[i], d = i + 1 < fl ? f[i + 1] : 0;
		if (c == '#') return true;
		if (c == '.' && d == '#') return true;
		if ((c == '*' && d == '*') || (c == '$' && d == '$')) return true;
		if (c == '+' && i + 1 < fl) return numStart (f, fl, i + 1);
		return false;
	}
	// Fields: # . , + - ** $$ **$ ^^^^ (numbers), ! \  \ & (strings), _x (a literal x).
	void printUsing (int n)
	{
		V items[64];
		if (n > 64) { fail ("Too many items for PRINT USING"); return; }
		for (int k = n - 1; k >= 0; k--) items[k] = pop ();
		V fv = pop ();
		int fl; const char *f = sdata (fv, &fl);
		if (fv.t != VS) { fail ("Type mismatch (PRINT USING format)"); }
		char o[160];
		int i = 0, item = 0; bool anyField = false;
		while (!failed)
		{
			if (i >= fl)
			{
				if (item >= n || !anyField) break;
				i = 0;
			}
			char c = f[i];
			if (c == '_' && i + 1 < fl) { out (f + i + 1, 1); i += 2; continue; }
			bool isStrField = c == '!' || c == '&' || c == '\\';
			bool isNum = !isStrField && numStart (f, fl, i);
			if (!isStrField && !isNum) { out (&c, 1); i++; continue; }
			if (item >= n) break;					// the literals after the last item
			anyField = true;
			V &v = items[item++];
			if (isStrField)
			{
				if (v.t != VS) { fail ("Type mismatch (PRINT USING)"); break; }
				int sl; const char *s = sdata (v, &sl);
				if (c == '!') { out (sl ? s : " ", 1); i++; }
				else if (c == '&') { out (s, sl); i++; }
				else
				{
					int j = i + 1; while (j < fl && f[j] == ' ') j++;
					if (j >= fl || f[j] != '\\') { out ("\\", 1); i++; item--; continue; }
					int w = j - i + 1;
					out (s, sl < w ? sl : w); if (sl < w) spaces (w - sl);
					i = j + 1;
				}
			}
			else
			{
				if (v.t != VN) { fail ("Type mismatch (PRINT USING)"); break; }
				int k = usingNum (v.n, f, fl, i, o);
				out (o, k);
			}
		}
		for (int k = 0; k < n; k++) vclear (items[k]);
		vclear (fv);
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

	// ---- binary records (GET / PUT #, MKx$ / CVx) --------------------------------------------------------
	void sput (const void *p, int n)
	{
		if (serLen + n > serCap)
		{
			int nc = serCap ? serCap * 2 : 256; while (nc < serLen + n) nc *= 2;
			char *b = new char[nc]; bmcpy (b, ser, serLen); delete [] ser; ser = b; serCap = nc;
		}
		bmcpy (ser + serLen, p, n); serLen += n;
	}
	void serNum (double v, int kind)
	{
		unsigned char b[8];
		if (kind == LK_INT || kind == LK_LNG)
		{
			double r = roundEven (v);
			long long x = (long long) r;
			int n = kind == LK_INT ? 2 : 4;
			for (int i = 0; i < n; i++) b[i] = (unsigned char) (x >> (8 * i));
			sput (b, n);
		}
		else if (kind == LK_SNG) { union { float f; unsigned u; } c; c.f = (float) v; for (int i = 0; i < 4; i++) b[i] = (unsigned char) (c.u >> (8 * i)); sput (b, 4); }
		else { union { double d; unsigned long long u; } c; c.d = v; for (int i = 0; i < 8; i++) b[i] = (unsigned char) (c.u >> (8 * i)); sput (b, 8); }
	}
	static double deNum (const unsigned char *b, int kind)
	{
		if (kind == LK_INT) return (double) (short) (b[0] | (b[1] << 8));
		if (kind == LK_LNG) return (double) (int) ((unsigned) b[0] | ((unsigned) b[1] << 8) | ((unsigned) b[2] << 16) | ((unsigned) b[3] << 24));
		if (kind == LK_SNG) { union { float f; unsigned u; } c; c.u = (unsigned) b[0] | ((unsigned) b[1] << 8) | ((unsigned) b[2] << 16) | ((unsigned) b[3] << 24); return c.f; }
		union { double d; unsigned long long u; } c; c.u = 0;
		for (int i = 0; i < 8; i++) c.u |= (unsigned long long) b[i] << (8 * i);
		return c.d;
	}
	static int kindSize (int kind) { return kind == LK_INT ? 2 : kind == LK_DBL ? 8 : 4; }
	void serialize (const V &v, int kind, int ext, bool random)
	{
		switch (kind)
		{
		case LK_SNG: case LK_INT: case LK_LNG: case LK_DBL: serNum (v.n, kind); break;
		case LK_VSTR:
		{
			int n; const char *s = sdata (v, &n);
			if (random) { unsigned char l[2] = { (unsigned char) n, (unsigned char) (n >> 8) }; sput (l, 2); }
			sput (s, n); break;
		}
		case LK_FSTR:
		{
			int n; const char *s = sdata (v, &n);
			if (n > ext) n = ext;
			sput (s, n);
			for (int i = n; i < ext; i++) sput (" ", 1);
			break;
		}
		case LK_REC:
		{
			if (v.t != VT) { fail ("Type mismatch (record)"); return; }
			const Rec *r = (const Rec *) v.p;
			const TypeInfo &t = P->types[r->type];
			for (int i = 0; i < t.nf; i++)
			{
				const FieldInfo &f = P->fields[t.first + i];
				serialize (r->f[i], f.kind, f.kind == FK_REC ? f.sub : f.len, random);
			}
			break;
		}
		}
	}
	// Read a value of `kind` from b[*at..n) (missing bytes read as zeros).
	void deserialize (V &v, int kind, int ext, const char *b, int n, int *at, bool random)
	{
		auto get = [&] (unsigned char *o, int k) { for (int i = 0; i < k; i++) o[i] = *at + i < n ? (unsigned char) b[*at + i] : 0; *at += k; };
		switch (kind)
		{
		case LK_SNG: case LK_INT: case LK_LNG: case LK_DBL:
		{
			unsigned char t[8]; get (t, kindSize (kind));
			vclear (v); v.t = VN; v.n = deNum (t, kind); break;
		}
		case LK_VSTR: case LK_FSTR:
		{
			int len;
			if (kind == LK_FSTR) len = ext;
			else if (random) { unsigned char l[2]; get (l, 2); len = l[0] | (l[1] << 8); }
			else sdata (v, &len);					// binary: as long as the variable is
			if (len < 0) len = 0;
			Str *s = sfill (len, 0);
			for (int i = 0; i < len; i++) s->d[i] = *at + i < n ? b[*at + i] : 0;
			*at += len;
			vclear (v); v.t = VS; v.p = s; break;
		}
		case LK_REC:
		{
			if (v.t != VT) { fail ("Type mismatch (record)"); return; }
			own (v);
			Rec *r = (Rec *) v.p;
			const TypeInfo &t = P->types[r->type];
			for (int i = 0; i < t.nf; i++)
			{
				const FieldInfo &f = P->fields[t.first + i];
				deserialize (r->f[i], f.kind, f.kind == FK_REC ? f.sub : f.len, b, n, at, random);
			}
			break;
		}
		}
	}
	int sizeOf (const V &v, int kind, int ext)
	{
		serLen = 0; serialize (v, kind, ext, false);
		return serLen;
	}
	void refreshFields (File *f)
	{
		for (int i = 0; i < f->nb; i++)
		{
			Bind &b = f->b[i];
			vclear (*b.ptr); b.ptr->t = VS; b.ptr->p = snew (f->rec + b.off, b.w);
		}
	}
	void syncField (V *target)
	{
		for (int k = 1; k < MAXFILES; k++)
			if (files[k].open)
				for (int i = 0; i < files[k].nb; i++)
					if (files[k].b[i].ptr == target)
					{
						int n; const char *s = sdata (*target, &n);
						Bind &b = files[k].b[i];
						for (int j = 0; j < b.w; j++) files[k].rec[b.off + j] = j < n ? s[j] : ' ';
					}
	}

	// ---- graphics coordinates (VIEW / WINDOW) ------------------------------------------------------
	void viewBox (int &ox, int &oy, int &w, int &h)
	{
		if (viewOn) { ox = vx1; oy = vy1; w = vx2 - vx1 + 1; h = vy2 - vy1 + 1; }
		else { ox = 0; oy = 0; w = scrW; h = scrH; }
	}
	void toPhys (double x, double y, int &px, int &py)
	{
		int ox, oy, w, h; viewBox (ox, oy, w, h);
		if (winOn)
		{
			double fx = (x - wx1) / (wx2 - wx1) * (w - 1);
			double fy = winScreen ? (y - wy1) / (wy2 - wy1) * (h - 1) : (wy2 - y) / (wy2 - wy1) * (h - 1);
			px = ox + (int) nfloor (fx + 0.5); py = oy + (int) nfloor (fy + 0.5);
			return;
		}
		if (viewOn && viewScreen) { ox = 0; oy = 0; }
		px = (int) nfloor (x + 0.5) + ox; py = (int) nfloor (y + 0.5) + oy;
	}
	double pmap (double v, int n)
	{
		int ox, oy, w, h; viewBox (ox, oy, w, h);
		if (!winOn) return v;
		switch (n)
		{
		case 0: return nfloor ((v - wx1) / (wx2 - wx1) * (w - 1) + 0.5);
		case 1: return nfloor ((winScreen ? (v - wy1) / (wy2 - wy1) : (wy2 - v) / (wy2 - wy1)) * (h - 1) + 0.5);
		case 2: return wx1 + v / (w - 1) * (wx2 - wx1);
		case 3: return winScreen ? wy1 + v / (h - 1) * (wy2 - wy1) : wy2 - v / (h - 1) * (wy2 - wy1);
		}
		return v;
	}
	double xScale () { if (!winOn) return 1; int ox, oy, w, h; viewBox (ox, oy, w, h); double d = wx2 - wx1; return (w - 1) / (d < 0 ? -d : d); }
	int bppOf () { return mode == 13 ? 8 : (mode >= 1 && mode <= 12) ? 4 : 32; }
	int ncolors () { return mode == 13 ? 256 : mode == 0 ? 256 : 16; }

	void circleArc (double cx, double cy, double r, int c, double st, double en, double aspect, bool hasSE, bool fill)
	{
		double xr = r * xScale (), yr = xr;
		if (aspect > 0) { if (aspect < 1) yr = xr * aspect; else { yr = xr; xr = xr / aspect; } }
		int pcx, pcy; toPhys (cx, cy, pcx, pcy);
		const double TWO_PI = 6.283185307179586;
		bool lineS = false, lineE = false;
		if (hasSE)
		{
			if (st < 0) { st = -st; lineS = true; }
			if (en < 0) { en = -en; lineE = true; }
		}
		else { st = 0; en = TWO_PI; }
		if (en <= st) en += TWO_PI;
		if (fill && !hasSE)					// a filled ellipse: horizontal spans
		{
			int ry = (int) (yr + 0.5);
			for (int dy = -ry; dy <= ry; dy++)
			{
				double t = yr > 0 ? (double) dy / yr : 0;
				double half = xr * nsqrt (1 - t * t > 0 ? 1 - t * t : 0);
				int hx = (int) (half + 0.5);
				H.line (pcx - hx, pcy + dy, pcx + hx, pcy + dy, c, 0, -1);
			}
			return;
		}
		int steps = (int) ((en - st) / TWO_PI * (xr + yr) * 2) + 8;
		if (steps > 720) steps = 720;
		int lx = 0, ly = 0;
		for (int k = 0; k <= steps; k++)
		{
			double t = st + (en - st) * k / steps;
			int x = pcx + (int) nfloor (xr * ncos (t) + 0.5), y = pcy - (int) nfloor (yr * nsin (t) + 0.5);
			if (k) H.line (lx, ly, x, y, c, 0, -1);
			lx = x; ly = y;
		}
		if (lineS) H.line (pcx, pcy, pcx + (int) (xr * ncos (st)), pcy - (int) (yr * nsin (st)), c, 0, -1);
		if (lineE) H.line (pcx, pcy, pcx + (int) (xr * ncos (en)), pcy - (int) (yr * nsin (en)), c, 0, -1);
	}

	// DRAW: U D L R E F G H n, M [+-]x,y, B (move only), N (come back), A n, TA n, C n, S n, P c,b.
	void draw (const char *s, int n)
	{
		int i = 0;
		int px, py; toPhys (lastX, lastY, px, py);
		double cx = px, cy = py;
		auto skip = [&] () { while (i < n && (s[i] == ' ' || s[i] == ';')) i++; };
		auto num = [&] (bool *has) -> double
		{
			skip ();
			bool neg = false; if (i < n && (s[i] == '-' || s[i] == '+')) { neg = s[i] == '-'; i++; }
			double v = 0; *has = false;
			while (i < n && s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i++] - '0'); *has = true; }
			return neg ? -v : v;
		};
		while (i < n && !failed)
		{
			skip ();
			if (i >= n) break;
			char c = s[i++]; if (c >= 'a' && c <= 'z') c = (char) (c - 32);
			bool blank = false, back = false;
			while (c == 'B' || c == 'N')
			{
				if (c == 'B') blank = true; else back = true;
				skip (); if (i >= n) return;
				c = s[i++]; if (c >= 'a' && c <= 'z') c = (char) (c - 32);
			}
			bool has;
			double dx = 0, dy = 0; bool move = true, rel = true;
			switch (c)
			{
			case 'U': { double v = num (&has); if (!has) v = 1; dy = -v; break; }
			case 'D': { double v = num (&has); if (!has) v = 1; dy = v; break; }
			case 'L': { double v = num (&has); if (!has) v = 1; dx = -v; break; }
			case 'R': { double v = num (&has); if (!has) v = 1; dx = v; break; }
			case 'E': { double v = num (&has); if (!has) v = 1; dx = v; dy = -v; break; }
			case 'F': { double v = num (&has); if (!has) v = 1; dx = v; dy = v; break; }
			case 'G': { double v = num (&has); if (!has) v = 1; dx = -v; dy = v; break; }
			case 'H': { double v = num (&has); if (!has) v = 1; dx = -v; dy = -v; break; }
			case 'M':
			{
				skip ();
				rel = i < n && (s[i] == '+' || s[i] == '-');
				dx = num (&has); skip (); if (i < n && s[i] == ',') i++; dy = num (&has);
				break;
			}
			case 'A': drawAngle = (int) num (&has) & 3; move = false; break;
			case 'T': { skip (); if (i < n && (s[i] == 'A' || s[i] == 'a')) i++; drawTA = (int) num (&has); move = false; break; }
			case 'C': drawColor = (int) num (&has); move = false; break;
			case 'S': drawScale = (int) num (&has); if (drawScale < 1) drawScale = 1; move = false; break;
			case 'P':
			{
				int pc2 = (int) num (&has); skip (); if (i < n && s[i] == ',') i++; int bc = (int) num (&has);
				H.paint ((int) cx, (int) cy, pc2, bc); move = false; break;
			}
			case 'X': fail ("Illegal function call (DRAW X is not supported)"); return;
			default: fail ("Illegal function call (DRAW string)"); return;
			}
			if (!move) continue;
			double nx, ny;
			if (c == 'M' && !rel) { int ax, ay; toPhys (dx, dy, ax, ay); nx = ax; ny = ay; }
			else
			{
				double sc = drawScale / 4.0;
				dx *= sc; dy *= sc;
				if (c != 'M')
				{
					double ang = (drawAngle * 90 + drawTA) * 3.14159265358979 / 180;
					double co = ncos (ang), si = nsin (ang);
					double rx = dx * co + dy * si, ry = -dx * si + dy * co;
					dx = rx; dy = ry;
				}
				nx = cx + dx; ny = cy + dy;
			}
			if (!blank) H.line ((int) nfloor (cx + 0.5), (int) nfloor (cy + 0.5), (int) nfloor (nx + 0.5), (int) nfloor (ny + 0.5), drawColor, 0, -1);
			if (!back) { cx = nx; cy = ny; }
		}
		// back to logical coordinates
		int ox, oy, w, h; viewBox (ox, oy, w, h);
		if (winOn) { lastX = pmap (cx - ox, 2); lastY = pmap (cy - oy, 3); }
		else { lastX = cx - (viewOn && !viewScreen ? ox : 0); lastY = cy - (viewOn && !viewScreen ? oy : 0); }
	}

	// GET (x1,y1)-(x2,y2), a(i): a(i) = width, a(i+1) = height, then the pixels packed in
	// 16-bit words (8 bits per pixel in SCREEN 13, 4 in the 16-colour modes, 32 = RGB in 2
	// words otherwise) -- QBasic-sized arrays are big enough.
	bool arrStart (V &av, double idx, Arr **pa, int *start)
	{
		if (av.t != VA) { fail ("Illegal function call (the array is not dimensioned)"); return false; }
		Arr *a = (Arr *) av.p;
		if (a->ek != 0) { fail ("Type mismatch (a numeric array is needed)"); return false; }
		int st = 0;
		if (idx >= 0 || idx < -1) { st = (int) idx - a->lo[0]; if (st < 0 || st >= a->total) { fail ("Subscript out of range"); return false; } }
		*pa = a; *start = st;
		return true;
	}
	void gget (double x1, double y1, double x2, double y2, int f, V &av, double idx)
	{
		if (f & 1) { x1 += lastX; y1 += lastY; }
		if (f & 2) { x2 += x1; y2 += y1; }
		int ax, ay, bx, by; toPhys (x1, y1, ax, ay); toPhys (x2, y2, bx, by);
		if (ax > bx) { int t = ax; ax = bx; bx = t; }
		if (ay > by) { int t = ay; ay = by; by = t; }
		int w = bx - ax + 1, h = by - ay + 1;
		Arr *a; int st;
		if (!arrStart (av, idx, &a, &st)) return;
		int bpp = bppOf ();
		long long bits = (long long) w * h * bpp;
		long long need = 2 + (bits + 15) / 16;
		if (st + need > a->total) { fail ("Illegal function call (the array is too small for GET)"); return; }
		int *px = new int[w * h];
		H.readRect (ax, ay, w, h, px, bpp == 32);
		V *e = a->e + st;
		e[0].n = w; e[1].n = h;
		unsigned acc = 0; int nb = 0; long long k = 2;
		auto emitWord = [&] (unsigned wv) { e[k++].n = (double) (wv >= 32768 ? (int) wv - 65536 : (int) wv); };
		for (int i = 0; i < w * h; i++)
		{
			unsigned v = (unsigned) px[i];
			if (bpp == 32) { emitWord (v & 0xFFFF); emitWord ((v >> 16) & 0xFFFF); continue; }
			acc |= (v & ((1u << bpp) - 1)) << nb; nb += bpp;
			if (nb == 16) { emitWord (acc); acc = 0; nb = 0; }
		}
		if (nb) emitWord (acc);
		delete [] px;
		lastX = x2; lastY = y2;
	}
	void gput (double x, double y, int f, V &av, double idx, int act)
	{
		if (f & 1) { x += lastX; y += lastY; }
		int px, py; toPhys (x, y, px, py);
		Arr *a; int st;
		if (!arrStart (av, idx, &a, &st)) return;
		V *e = a->e + st;
		if (st + 2 > a->total) { fail ("Illegal function call (PUT)"); return; }
		int w = (int) e[0].n, h = (int) e[1].n;
		int bpp = bppOf ();
		if (w <= 0 || h <= 0 || w > 4096 || h > 4096) { fail ("Illegal function call (PUT: not an image)"); return; }
		long long need = 2 + ((long long) w * h * bpp + 15) / 16;
		if (st + need > a->total) { fail ("Illegal function call (PUT: the array is too small)"); return; }
		int *src = new int[w * h], *dst = act != 0 ? new int[w * h] : 0;
		long long k = 2; unsigned acc = 0; int nb = 0;
		auto word = [&] () -> unsigned { int v = (int) e[k++].n; return (unsigned) v & 0xFFFF; };
		for (int i = 0; i < w * h; i++)
		{
			if (bpp == 32) { unsigned lo = word (), hi = word (); src[i] = (int) (lo | (hi << 16)); continue; }
			if (nb == 0) { acc = word (); nb = 16; }
			src[i] = (int) (acc & ((1u << bpp) - 1)); acc >>= bpp; nb -= bpp;
		}
		if (dst)
		{
			H.readRect (px, py, w, h, dst, bpp == 32);
			unsigned mask = bpp == 32 ? 0xFFFFFFu : (1u << bpp) - 1;
			for (int i = 0; i < w * h; i++)
			{
				unsigned s = (unsigned) src[i], d = (unsigned) dst[i], r = s;
				switch (act)
				{
				case 1: r = ~s & mask; break;
				case 2: r = s & d; break;
				case 3: r = s | d; break;
				case 4: r = s ^ d; break;
				}
				src[i] = (int) (r & mask);
			}
		}
		H.writeRect (px, py, w, h, src, bpp == 32);
		delete [] src; delete [] dst;
		lastX = x; lastY = y;
	}
	// A PALETTE colour: VGA (blue * 65536 + green * 256 + red, 0..63 each), EGA 0..63 in the
	// EGA modes, or an RGB () value.
	int palColor (double v)
	{
		long long c = (long long) v;
		if (c & 0x1000000) return (int) (c & 0xFFFFFF);
		if (mode == 7 || mode == 8 || mode == 9)
		{
			int r = ((c >> 2) & 1) * 170 + ((c >> 5) & 1) * 85, g = ((c >> 1) & 1) * 170 + ((c >> 4) & 1) * 85, b = (c & 1) * 170 + ((c >> 3) & 1) * 85;
			return (r << 16) | (g << 8) | b;
		}
		int r = (int) (c & 63), g = (int) ((c >> 8) & 63), b = (int) ((c >> 16) & 63);
		return ((r * 255 / 63) << 16) | ((g * 255 / 63) << 8) | (b * 255 / 63);
	}

	// ---- events (ON TIMER / ON KEY) -----------------------------------------------------------------
	int keyIndex (const char *k, int n)
	{
		if (n <= 0) return 0;
		int scan;
		if (k[0] == 0 && n > 1)
		{
			unsigned char c = (unsigned char) k[1];
			if (c >= 59 && c <= 68 && ev[c - 58].target >= 0) return c - 58;	// F1..F10
			if (c == 133 && ev[30].target >= 0) return 30;
			if (c == 134 && ev[31].target >= 0) return 31;
			if (c == 72 && ev[11].target >= 0) return 11;
			if (c == 75 && ev[12].target >= 0) return 12;
			if (c == 77 && ev[13].target >= 0) return 13;
			if (c == 80 && ev[14].target >= 0) return 14;
			scan = c;
		}
		else scan = scanOf ((unsigned char) k[0]);
		for (int i = 15; i <= 25; i++) if (keyScan[i] && keyScan[i] == scan && ev[i].target >= 0) return i;
		return 0;
	}
	void fire (int i)
	{
		if (ngs >= MAXGOSUB) return;
		gosubs[ngs].ret = pc; gosubs[ngs].frame = nf; gosubs[ngs].chan = chan; gosubs[ngs].ev = i; ngs++;
		chan = 0;
		ev[i].busy = true; ev[i].pending = false;
		pc = ev[i].target;
	}
	void checkEvents ()
	{
		if (ev[0].target >= 0 && ev[0].state != 0 && timerInt > 0)
		{
			double t = H.timer ();
			if (t < timerNext - 43200) timerNext -= 86400;		// midnight
			if (t >= timerNext) { ev[0].pending = true; timerNext = t + timerInt; }
		}
		char k[4];
		int n = H.keyPending (k);
		if (n > 0)
		{
			int i = keyIndex (k, n);
			if (i && ev[i].state != 0) { H.inkey (k); ev[i].pending = true; }
		}
		for (int i = 0; i < 32; i++)
			if (ev[i].pending && ev[i].state == 1 && !ev[i].busy && ev[i].target >= 0) { fire (i); return; }
	}
	bool eventDue ()
	{
		if (!evAny) return false;
		if (ev[0].target >= 0 && ev[0].state == 1 && timerInt > 0 && H.timer () >= timerNext) return true;
		char k[4]; int n = H.keyPending (k);
		return n > 0 && keyIndex (k, n) != 0;
	}

	// ---- builtins -----------------------------------------------------------------------------------------
	double rndNext () { rnd = rnd * 214013u + 2531011u; lastRnd = ((rnd >> 8) & 0xFFFFFF) / 16777216.0; return lastRnd; }
	static void cstr (const V &v, char *out, int cap) { int n; const char *s = sdata (v, &n); if (n > cap - 1) n = cap - 1; bmcpy (out, s, n); out[n] = 0; }
	void mk (double v, int kind) { serLen = 0; serNum (v, kind); pushS (ser, serLen); }
	void cv (const V &s, int kind)
	{
		int n; const char *d = sdata (s, &n);
		int need = kindSize (kind);
		if (n < need) { fail ("Illegal function call (CVx: the string is too short)"); return; }
		pushN (deNum ((const unsigned char *) d, kind));
	}
	const char *envGet (const char *name)
	{
		int ln = bslen (name);
		for (int i = 0; i < nenv; i++)
		{
			int j = 0; while (j < ln && bup (env[i][j]) == bup (name[j])) j++;
			if (j == ln && env[i][j] == '=') return env[i] + j + 1;
		}
		return "";
	}

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
		case B_STR: case B_STRD: { int n = 0; if (a[0].n >= 0) tmp[n++] = ' '; n += formatNum (a[0].n, tmp + n, id == B_STRD); pushS (tmp, n); break; }
		case B_VAL: { int n = l0 < 255 ? l0 : 255; bmcpy (tmp, s0, n); tmp[n] = 0; pushN (parseNum (tmp, 0)); break; }
		case B_SPACE: case B_STRING:
		{
			long long n = (long long) a[0].n; if (n < 0 || n > 1000000) { fail ("Illegal function call"); break; }
			char c = ' ';
			if (id == B_STRING) { if (a[1].t == VN) c = (char) (long long) a[1].n; else if (l1 > 0) c = s1[0]; else { fail ("Illegal function call (STRING$)"); break; } }
			pushStr (sfill ((int) n, c)); break;
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
		case B_CINT: { double r = roundEven (a[0].n); if (r < -32768 || r > 32767) fail ("Overflow"); else pushN (r); break; }
		case B_CLNG: { double r = roundEven (a[0].n); if (r < -2147483648.0 || r > 2147483647.0) fail ("Overflow"); else pushN (r); break; }
		case B_CDBL: pushN (a[0].n); break;
		case B_MIN: pushN (a[0].n < a[1].n ? a[0].n : a[1].n); break;
		case B_MAX: pushN (a[0].n > a[1].n ? a[0].n : a[1].n); break;
		case B_TIMER: pushN (H.timer ()); break;
		case B_TICKS: pushN (nfloor (H.timer () * 1000)); break;
		case B_DATE: { char d[16]; H.date (d); pushS (d, bslen (d)); break; }
		case B_TIME: { char t[16]; H.time (t); pushS (t, bslen (t)); break; }
		case B_INKEY: { char k[4]; int n = H.inkey (k); pushS (k, n); if (!H.poll ()) ended = true; evKick = true; break; }
		case B_COMMAND: { const char *c = H.command (); pushS (c, bslen (c)); break; }
		case B_POS: pushN (H.column ()); break;
		case B_CSRLIN: pushN (H.row ()); break;
		case B_POINT: { int px, py; toPhys (a[0].n, a[1].n, px, py); pushN (H.point (px, py)); break; }
		case B_POINT1:
		{
			int px, py; toPhys (lastX, lastY, px, py);
			int k = (int) a[0].n;
			pushN (k == 0 ? px : k == 1 ? py : k == 2 ? lastX : lastY); break;
		}
		case B_PMAP: pushN (pmap (a[0].n, (int) a[1].n)); break;
		case B_SCREEN: pushN (H.screenChar ((int) a[0].n, (int) a[1].n, argc > 2 && a[2].n != 0)); break;
		case B_RGB:
		{
			int r = (int) a[0].n, g = (int) a[1].n, b = (int) a[2].n;
			r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
			pushN ((double) (0x1000000 | (r << 16) | (g << 8) | b)); break;
		}
		case B_EOF:
		{
			File *f = file ((long long) a[0].n); if (!f) break;
			bool e = f->mode == 4 ? (f->lastRec == 0 ? f->len == 0 : f->eof) : f->pos >= f->len;
			pushN (e ? -1 : 0); break;
		}
		case B_LOF: { File *f = file ((long long) a[0].n); if (f) pushN (f->len); break; }
		case B_LOC: { File *f = file ((long long) a[0].n); if (f) pushN (f->mode == 4 ? f->lastRec : f->mode == 5 ? f->pos : f->pos / 128); break; }
		case B_SEEK: { File *f = file ((long long) a[0].n); if (f) pushN (f->mode == 4 ? f->nextRec : f->pos + 1); break; }
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
		case B_PLAYN: pushN (H.bgNotes ()); break;
		case B_KEYDOWN: pushN (H.keyDown (s0, l0) ? -1 : 0); if (!H.poll ()) ended = true; break;
		case B_ERR: pushN (inErr ? errCode : 0); break;
		case B_ERL: pushN (inErr ? errLine : 0); break;
		case B_MKI: if (a[0].n < -32768 || a[0].n > 32767) fail ("Overflow"); else mk (a[0].n, LK_INT); break;
		case B_MKL: mk (a[0].n, LK_LNG); break;
		case B_MKS: mk (a[0].n, LK_SNG); break;
		case B_MKD: mk (a[0].n, LK_DBL); break;
		case B_CVI: cv (a[0], LK_INT); break;
		case B_CVL: cv (a[0], LK_LNG); break;
		case B_CVS: cv (a[0], LK_SNG); break;
		case B_CVD: cv (a[0], LK_DBL); break;
		case B_INPUTS:
		{
			long long n = (long long) a[0].n;
			if (n < 0 || n > 32767) { fail ("Illegal function call (INPUT$)"); break; }
			if (argc > 1)
			{
				File *f = file ((long long) a[1].n); if (!f) break;
				if (f->mode != 1 && f->mode != 5) { fail ("Bad file mode (INPUT$)"); break; }
				if (f->pos + n > f->len) { fail ("Input past end of file"); break; }
				pushS (f->buf + f->pos, (int) n); f->pos += (int) n;
				break;
			}
			Str *x = sfill ((int) n, 0); int got = 0;
			while (got < n)
			{
				char k[4]; int kn = H.inkey (k);
				if (kn > 0) { x->d[got++] = k[kn - 1]; continue; }
				if (!H.poll ()) { ended = true; break; }
				H.sleepMs (10);
			}
			pushStr (x); break;
		}
		case B_ENVIRON:
		{
			if (a[0].t == VS) { cstr (a[0], tmp, sizeof tmp); const char *v = envGet (tmp); pushS (v, bslen (v)); }
			else { int k = (int) a[0].n; if (k < 1) { fail ("Illegal function call (ENVIRON$)"); break; } if (k <= nenv) pushS (env[k - 1], bslen (env[k - 1])); else pushS ("", 0); }
			break;
		}
		case B_FRE:
		{
			double v = 60000;
			if (a[0].t == VN && a[0].n == -1) v = 400000;
			else if (a[0].t == VN && a[0].n == -2) v = (STACK - sp) * 16.0;
			pushN (v); break;
		}
		default: fail ("Unknown function", 51);
		}
		for (int i = 0; i < argc; i++) vclear (a[i]);
	}

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
		case S_CLS: H.cls (N (0, -1)); break;
		case S_LOCATE: H.locate (N (0, -1), N (1, -1)); break;
		case S_COLOR: H.color (N (0, -1), N (1, -1)); break;
		case S_SCREEN:
		{
			int m = N (0, -1); if (m < 0) m = mode;
			H.screen (m, N (2, -1), N (3, -1));
			if (m != mode) { mode = m; viewOn = winOn = false; H.setClip (-1, -1, -1, -1); }
			H.screenSize (&scrW, &scrH);
			break;
		}
		case S_PSET:
		{
			double x = a[0].n, y = a[1].n;
			if (N (3, 0)) { x += lastX; y += lastY; }
			int px, py; toPhys (x, y, px, py);
			int c = N (2, -1); if (c == -2) c = 0;
			H.pset (px, py, c);
			lastX = x; lastY = y;
			break;
		}
		case S_LINE:					// x1 y1 x2 y2 c style box flags
		{
			int fl = N (7, 0);
			double x1 = a[0].n, y1 = a[1].n, x2 = a[2].n, y2 = a[3].n;
			if (fl & 4) { x1 = lastX; y1 = lastY; }
			else if (fl & 1) { x1 += lastX; y1 += lastY; }
			if (fl & 2) { x2 += x1; y2 += y1; }
			int ax, ay, bx, by; toPhys (x1, y1, ax, ay); toPhys (x2, y2, bx, by);
			H.line (ax, ay, bx, by, N (4, -1), N (6, 0), N (5, -1));
			lastX = x2; lastY = y2;
			break;
		}
		case S_CIRCLE:					// x y r c start end aspect flags
		{
			int fl = N (7, 0);
			double x = a[0].n, y = a[1].n;
			if (fl & 1) { x += lastX; y += lastY; }
			if ((fl & 28) == 0)
			{
				int px, py; toPhys (x, y, px, py);
				H.circle (px, py, (int) (a[2].n * xScale () + 0.5), N (3, -1), (fl & 2) ? 1 : 0);
			}
			else circleArc (x, y, a[2].n, N (3, -1), a[4].n, (fl & 8) ? a[5].n : 6.283185307179586, (fl & 16) ? a[6].n : 0, (fl & 12) != 0, (fl & 2) != 0);
			lastX = x; lastY = y;
			break;
		}
		case S_PAINT:
		{
			double x = a[0].n, y = a[1].n;
			if (N (4, 0) & 1) { x += lastX; y += lastY; }
			int px, py; toPhys (x, y, px, py);
			int c = N (2, -1), b = N (3, -1);
			H.paint (px, py, c, b < 0 ? c : b);
			lastX = x; lastY = y;
			break;
		}
		case S_DRAW: { int n; const char *s = sdata (a[0], &n); draw (s, n); break; }
		case S_GGET: gget (a[0].n, a[1].n, a[2].n, a[3].n, N (4, 0), a[5], a[6].n); break;
		case S_GPUT: gput (a[0].n, a[1].n, N (2, 0), a[3], a[4].n, N (5, 4)); break;
		case S_VIEW:
			if (argc == 0) { viewOn = false; H.setClip (-1, -1, -1, -1); break; }
			{
				int x1 = N (0, 0), y1 = N (1, 0), x2 = N (2, 0), y2 = N (3, 0);
				if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
				if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
				H.setClip (-1, -1, -1, -1);
				if (N (5, -1) >= 0) H.line (x1 - 1, y1 - 1, x2 + 1, y2 + 1, N (5, -1), 1, -1);
				if (N (4, -1) >= 0) H.line (x1, y1, x2, y2, N (4, -1), 2, -1);
				viewOn = true; viewScreen = N (6, 0) != 0; vx1 = x1; vy1 = y1; vx2 = x2; vy2 = y2;
				H.setClip (x1, y1, x2, y2);
			}
			break;
		case S_VIEWPRINT: H.viewPrint (N (0, 0), N (1, 0)); break;
		case S_GWINDOW:
			if (argc == 0) { winOn = false; break; }
			if (a[0].n == a[2].n || a[1].n == a[3].n) { fail ("Illegal function call (WINDOW)"); break; }
			winOn = true; winScreen = N (4, 0) != 0;
			wx1 = a[0].n < a[2].n ? a[0].n : a[2].n; wx2 = a[0].n < a[2].n ? a[2].n : a[0].n;
			wy1 = a[1].n < a[3].n ? a[1].n : a[3].n; wy2 = a[1].n < a[3].n ? a[3].n : a[1].n;
			break;
		case S_PALETTE:
			if (argc == 0) H.palette (-1, 0);
			else { int at = N (0, 0); if (at < 0 || at >= ncolors ()) { fail ("Illegal function call (PALETTE attribute)"); break; } H.palette (at, palColor (a[1].n)); }
			break;
		case S_PALUSING:
		{
			Arr *ar; int st;
			if (!arrStart (a[0], a[1].n, &ar, &st)) break;
			for (int i = 0; i < ncolors () && st + i < ar->total; i++)
				if (ar->e[st + i].n != -1) H.palette (i, palColor (ar->e[st + i].n));
			break;
		}
		case S_PCOPY: H.pcopy (N (0, 0), N (1, 0)); break;
		case S_FULLSCREEN: H.fullscreen (N (0, 1) != 0); break;
		case S_KEYDEF:
		{
			int k = N (0, 0); int n; const char *s = sdata (a[1], &n);
			if (k >= 15 && k <= 25) keyScan[k] = n >= 2 ? (unsigned char) s[1] : n == 1 ? scanOf ((unsigned char) s[0]) : 0;
			else if (k < 1 || (k > 10 && k != 30 && k != 31)) fail ("Illegal function call (KEY)");
			break;
		}
		case S_ERROR:
		{
			int c = N (0, 0);
			if (c < 1 || c > 255) { fail ("Illegal function call (ERROR)"); break; }
			fail (errMessage (c), c);
			break;
		}
		case S_RESET: closeAll (); break;
		case S_FILES:
		{
			char pat[256] = "";
			if (argc) cstr (a[0], pat, sizeof pat);
			char dir[256] = ""; const char *mask = pat;
			int e = bslen (pat); while (e > 0 && pat[e - 1] != '/' && pat[e - 1] != ':') e--;
			if (e > 0) { bscpy (dir, pat, e + 1); mask = pat + e; }
			if (!*mask) mask = "*.*";
			int col = 0, w = H.width ();
			for (int i = 0; ; i++)
			{
				char name[200]; int n = H.listDir (dir, i, name, sizeof name);
				if (n <= 0) break;
				char base[200]; bscpy (base, name, sizeof base);
				int bl = bslen (base); if (bl && base[bl - 1] == '/') base[bl - 1] = 0;
				if (!wild (mask, base)) continue;
				if (col + 16 > w) { outs ("\n"); col = 0; }
				outs (name); spaces (16 - n > 0 ? 16 - n : 1); col += 16;
			}
			outs ("\n");
			break;
		}
		case S_CHDIR: cstr (a[0], t1, sizeof t1); if (!H.chdir (t1)) fail ("Path not found"); break;
		case S_ENVIRON:
		{
			cstr (a[0], t1, sizeof t1);
			int eq = 0; while (t1[eq] && t1[eq] != '=') eq++;
			if (!t1[eq] || eq == 0) { fail ("Illegal function call (ENVIRON: NAME=value)"); break; }
			for (int i = 0; i < eq; i++) t1[i] = bup (t1[i]);
			int k = 0;
			for (; k < nenv; k++) { int j = 0; while (j < eq && env[k][j] == t1[j]) j++; if (j == eq && env[k][j] == '=') break; }
			if (k == nenv) { if (nenv >= MAXENV) { fail ("Out of memory (ENVIRON)"); break; } nenv++; }
			else delete [] env[k];
			int n = bslen (t1); env[k] = new char[n + 1]; bscpy (env[k], t1, n + 1);
			break;
		}
		case S_SHELL: cstr (a[0], t1, sizeof t1); if (argc == 0) t1[0] = 0; H.shell (t1); break;
		case S_DRAWTEXT: { cstr (a[2], t1, sizeof t1); int px, py; toPhys (a[0].n, a[1].n, px, py); H.drawText (px, py, t1, N (3, -1)); break; }
		case S_SLEEP:
		{
			int secs = N (0, 0);
			char k[4];
			double t0 = H.timer ();
			for (;;)
			{
				if (eventDue ()) break;
				if (secs <= 0) { if (H.inkey (k)) break; }
				else
				{
					if (H.inkey (k)) break;
					double el = H.timer () - t0; if (el < 0) el += 86400;
					if (el >= secs) break;
				}
				if (!H.poll ()) { ended = true; break; }
				H.sleepMs (20);
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
			if (playBg && H.bgNote (a[0].n, (int) (d * 1000 / 18.2), 0, 1)) break;
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
		case S_WINDOW: cstr (a[0], t1, sizeof t1); H.window (t1, N (1, 0), N (2, 0)); H.screenSize (&scrW, &scrH); break;
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
		default: fail ("Unknown statement", 51);
		}
		for (int i = 0; i < argc; i++) vclear (a[i]);
	}
	// '*' / '?' wildcards, case-insensitive; "*.*" matches every name.
	static bool wild (const char *m, const char *s)
	{
		if (bseq (m, "*.*") || bseq (m, "*")) return true;
		if (*m == 0) return *s == 0;
		if (*m == '*') { for (;; s++) { if (wild (m + 1, s)) return true; if (!*s) return false; } }
		if (*s && (*m == '?' || bup (*m) == bup (*s))) return wild (m + 1, s + 1);
		return false;
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
	// (normal 7/8, legato, staccato 3/4), MF / MB (foreground / background: MB queues the notes
	// in the host, which plays them while the program goes on; PLAY(n) = notes still queued).
	int playOct = 4, playLen = 4, playTempo = 120, playStyle = 0; bool playBg = false;
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
				else if (m == 'F') playBg = false; else if (m == 'B') playBg = true;
				continue;
			}
			else { fail ("Illegal function call (PLAY string)"); return; }
			if (len < 1) len = 1;
			double ms = 240000.0 / playTempo / len;			// whole note = 4 beats
			double dot = ms;
			while (i < n && s[i] == '.') { dot /= 2; ms += dot; i++; }
			if (midi < 0) { if (!playBg || !H.bgNote (0, 0, (int) ms, 0)) H.sleepMs ((int) ms); continue; }
			double on = playStyle == 1 ? ms : playStyle == 2 ? ms * 3 / 4 : ms * 7 / 8;
			if (playBg && H.bgNote (midiFreq (midi), (int) on, (int) (ms - on), 2)) { if (!H.poll ()) ended = true; continue; }
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
		delete [] f.buf; f.buf = 0; delete [] f.rec; f.rec = 0; f.open = false; f.nb = 0;
	}
	void closeAll () { for (int i = 1; i < MAXFILES; i++) if (files[i].open) closeFile (i); }
	void openFile (int mode, long long n, const char *path, long long reclen)
	{
		File *f = file (n, false);
		if (!f) return;
		if (f->open) { fail ("File already open"); return; }
		if (reclen < 0) reclen = 128;
		if (reclen < 1 || reclen > 32767) { fail ("Bad record length"); return; }
		f->mode = mode; f->len = f->cap = f->pos = 0; f->col = 1; f->buf = 0; f->rec = 0;
		f->reclen = (int) reclen; f->lastRec = 0; f->nextRec = 1; f->eof = false; f->nb = 0;
		bscpy (f->path, path, sizeof f->path);
		if (mode != 2)
		{
			int len = 0; char *b = H.load (path, &len);
			if (!b && mode == 1) { fail ("File not found"); return; }
			f->buf = b; f->len = len; f->cap = b ? len + 1 : 0;
		}
		if (mode == 4) { f->rec = new char[f->reclen]; for (int i = 0; i < f->reclen; i++) f->rec[i] = 0; }
		f->open = true;
	}
	// GET / PUT #: records of a RANDOM file, bytes of a BINARY one.
	void getPut (bool get, int hasVar, int kind, int ext)
	{
		V *target = hasVar ? popRef () : 0;
		long long rec = popI (), n = popI ();
		if (failed) return;
		File *f = file (n); if (!f) return;
		if (f->mode == 4)
		{
			long long r = rec >= 1 ? rec : rec == -1 ? f->nextRec : -1;
			if (r < 1) { fail ("Bad record number"); return; }
			int off = (int) ((r - 1) * f->reclen);
			if (get)
			{
				f->eof = off >= f->len;
				char *tmp = new char[f->reclen];
				for (int i = 0; i < f->reclen; i++) tmp[i] = off + i < f->len ? f->buf[off + i] : 0;
				if (target) { int at = 0; deserialize (*target, kind, ext, tmp, f->reclen, &at, true); }
				else { bmcpy (f->rec, tmp, f->reclen); refreshFields (f); }
				delete [] tmp;
			}
			else
			{
				serLen = 0;
				if (target) serialize (*target, kind, ext, true); else sput (f->rec, f->reclen);
				if (serLen > f->reclen) { fail ("FIELD overflow"); return; }
				while (serLen < f->reclen) { char z = 0; sput (&z, 1); }
				fwriteAt (f, off, ser, serLen);
			}
			f->lastRec = (int) r; f->nextRec = (int) r + 1;
			return;
		}
		if (f->mode == 5)
		{
			if (!target) { fail ("Bad file mode (BINARY GET / PUT need a variable)"); return; }
			if (rec >= 1) f->pos = (int) rec - 1;
			if (get)
			{
				int size = sizeOf (*target, kind, ext);
				int at = f->pos;
				deserialize (*target, kind, ext, f->buf ? f->buf : "", f->len, &at, false);
				f->eof = f->pos + size > f->len;
				f->pos += size;
			}
			else { serLen = 0; serialize (*target, kind, ext, false); fwriteAt (f, f->pos, ser, serLen); f->pos += serLen; }
			return;
		}
		fail ("Bad file mode (GET / PUT need RANDOM or BINARY)");
	}

	// ---- CHAIN / RUN / CLEAR ------------------------------------------------------------------------------------
	void resetGlobals ()
	{
		for (int i = 0; i < P->nglobals; i++) { vclear (G[i]); initSlot (G[i], P->gkind[i], P->gext[i]); }
	}
	void restart ()
	{
		while (nf > 0) popFrame ();
		while (sp > 0) vclear (stack[--sp]);
		ngs = 0; chan = 0; dataPtr = 0; onErr = -1; inErr = false;
		closeAll ();
		resetGlobals ();
		for (int i = 0; i < 32; i++) { ev[i].target = -1; ev[i].state = 0; ev[i].pending = ev[i].busy = false; }
		evAny = false;
	}
	// A CHAINed program takes the COMMON values (in order) and the open files.
	void importFrom (VM &o, bool commons)
	{
		if (commons)
			for (int i = 0; i < P->common.n && i < o.P->common.n; i++)
			{
				int ns = P->common[i], os = o.P->common[i];
				int nk = P->gkind[ns], ok = o.P->gkind[os];
				if ((nk & 1) != (ok & 1) || (nk >= K_NUMARR) != (ok >= K_NUMARR)) continue;
				vclear (G[ns]); G[ns] = o.G[os]; o.G[os].t = VN; o.G[os].p = 0;
			}
		for (int i = 1; i < MAXFILES; i++)
			if (o.files[i].open) { files[i] = o.files[i]; o.files[i].open = false; o.files[i].buf = 0; o.files[i].rec = 0; o.files[i].nb = 0; }
		for (int i = 0; i < o.nenv; i++) env[nenv++] = o.env[i];
		o.nenv = 0;
		mode = o.mode; scrW = o.scrW; scrH = o.scrH;
	}

	// ---- the loop ---------------------------------------------------------------------------------------------
	int run ()
	{
		const int *code = P->code.d;
		unsigned count = 0;
		while (!ended)
		{
			if (failed) { if (!trap ()) break; continue; }
			++count;
			if ((count & 4095) == 0 && !H.poll ()) { ended = true; break; }
			if (evAny && (evKick || (count & 31) == 0)) { evKick = false; checkEvents (); if (failed) continue; }
			if (tron)
			{
				int l = P->lineAt (pc);
				if (l != traceLine) { traceLine = l; char b[16]; int n = 0; b[n++] = '['; n += formatNum (l, b + n); b[n++] = ']'; H.out (b, n); }
			}
			opPc = pc;
			int op = code[pc++];
			switch (op)
			{
			case OP_NUM: pushN (P->nums[code[pc++]]); break;
			case OP_STR: { int k = code[pc++]; pushS (P->strs[k], P->strl[k]); break; }
			case OP_LDG: case OP_LDL: { V v = slot (op == OP_LDG, code[pc++]); vretain (v); push (v); break; }
			case OP_STG: case OP_STL: { V v = pop (); V &d = slot (op == OP_STG, code[pc++]); store (d, v); break; }
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
				if (e) store (*e, v); else vclear (v);
				break;
			}
			case OP_AADDRG: case OP_AADDRL:
			{
				int s = code[pc++], nd = code[pc++];
				V *e = element (op == OP_AADDRG, s, nd);
				if (e) pushRef (e);
				break;
			}
			case OP_DIMG: case OP_DIML:
			{
				int s = code[pc++], nd = code[pc++], ek = code[pc++], ext = code[pc++];
				int lo[4], hi[4];
				for (int i = nd - 1; i >= 0; i--) { hi[i] = (int) popI (); lo[i] = (int) popI (); }
				if (failed) break;
				Arr *a = newArr (nd, lo, hi, ek, ext);
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
				pushRef (target);
				break;
			}
			case OP_FADDR:
			{
				int i = code[pc++];
				V *r = popRef (); if (!r) break;
				if (r->t != VT) { fail ("Type mismatch (not a record)"); break; }
				own (*r);
				pushRef (&((Rec *) r->p)->f[i]);
				break;
			}
			case OP_FLD:
			{
				int i = code[pc++];
				V rv = pop ();
				if (rv.t != VT) { vclear (rv); fail ("Type mismatch (not a record)"); break; }
				V f = ((Rec *) rv.p)->f[i]; vretain (f);
				vclear (rv); push (f);
				break;
			}
			case OP_STREF: { V v = pop (); V *r = popRef (); if (r) store (*r, v); else vclear (v); break; }
			case OP_LDREF: { V *r = popRef (); if (r) { V v = *r; vretain (v); push (v); } break; }
			case OP_CONV:
			{
				int nt = code[pc++];
				double v = popN (), r = roundEven (v);
				if (nt == NT_INT && (r < -32768 || r > 32767)) { fail ("Overflow"); break; }
				if (nt == NT_LNG && (r < -2147483648.0 || r > 2147483647.0)) { fail ("Overflow"); break; }
				pushN (r);
				break;
			}
			case OP_FIXSTR:
			{
				int len = code[pc++];
				V v = pop ();
				int n; const char *s = sdata (v, &n);
				if (n != len)
				{
					Str *x = sfill (len, ' ');
					if (x) bmcpy (x->d, s, n < len ? n : len);
					vclear (v); v.t = VS; v.p = x;
				}
				push (v);
				break;
			}
			case OP_MIDSET:
			{
				V rv = pop (); long long len = popI (), st = popI (); V *t = popRef ();
				if (!t || failed) { vclear (rv); break; }
				int tl, rl; const char *ts = sdata (*t, &tl), *rs = sdata (rv, &rl);
				if (st < 1 || st > tl) { vclear (rv); fail ("Illegal function call (MID$)"); break; }
				long long n = rl; if (len >= 0 && len < n) n = len; if (st - 1 + n > tl) n = tl - (st - 1);
				Str *x = snew (ts, tl);
				bmcpy (x->d + st - 1, rs, (int) n);
				vclear (*t); t->t = VS; t->p = x;
				vclear (rv);
				syncField (t);
				break;
			}
			case OP_LSET: case OP_RSET:
			{
				V rv = pop (); V *t = popRef ();
				if (!t) { vclear (rv); break; }
				int tl, rl; sdata (*t, &tl); const char *rs = sdata (rv, &rl);
				Str *x = sfill (tl, ' ');
				if (x)
				{
					int n = rl < tl ? rl : tl;
					if (op == OP_LSET) bmcpy (x->d, rs, n); else bmcpy (x->d + tl - n, rs, n);
				}
				vclear (*t); t->t = VS; t->p = x;
				vclear (rv);
				syncField (t);
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
				for (int i = 0; i < f.nloc; i++)
				{
					f.loc[i].t = VN; f.loc[i].n = 0; f.loc[i].p = 0;
					if (i < pr.nlocals) initSlot (f.loc[i], P->lkind[pr.kindOff + i], P->lext[pr.kindOff + i]);
				}
				int first = pr.isFunc ? 1 : 0;
				for (int i = argc - 1; i >= 0; i--) { V v = pop (); own (v); vclear (f.loc[first + i]); f.loc[first + i] = v; }
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
				gosubs[ngs].ret = pc + 1; gosubs[ngs].frame = nf; gosubs[ngs].chan = chan; gosubs[ngs].ev = -1; ngs++;
				pc = code[pc];
				break;
			case OP_RETSUB:
				if (ngs == 0 || gosubs[ngs - 1].frame != nf) { fail ("RETURN without GOSUB"); break; }
				pc = gosubs[ngs - 1].ret;
				popGosub ();
				break;
			case OP_POP: { V v = pop (); vclear (v); break; }
			case OP_BI: { int id = code[pc++], argc = code[pc++]; builtin (id, argc); break; }
			case OP_ST: { int id = code[pc++], argc = code[pc++]; statement (id, argc); evKick = true; break; }
			case OP_PRINT: { int w = code[pc++]; V v = pop (); printValue (v, (w & 1) != 0, (w & 2) != 0); vclear (v); break; }
			case OP_USING: printUsing (code[pc++]); break;
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
				long long reclen = popI ();
				char path[240];
				if (mode == 9)					// OPEN mode$, n, path$ [, reclen]
				{
					V pv = pop (); cstr (pv, path, sizeof path); vclear (pv);
					long long n = popI ();
					V mv = pop (); int ml; const char *ms = sdata (mv, &ml);
					char m = ml ? bup (ms[0]) : 0; vclear (mv);
					int md = m == 'I' ? 1 : m == 'O' ? 2 : m == 'A' ? 3 : m == 'R' ? 4 : m == 'B' ? 5 : 0;
					if (!md) { fail ("Bad file mode"); break; }
					openFile (md, n, path, reclen);
				}
				else
				{
					long long n = popI ();
					V pv = pop (); cstr (pv, path, sizeof path); vclear (pv);
					if (failed) break;
					openFile (mode, n, path, reclen);
				}
				break;
			}
			case OP_CLOSE:
			{
				long long n = popI ();
				if (n == 0) closeAll ();
				else { File *f = file (n, false); if (f) closeFile ((int) n); }
				break;
			}
			case OP_FGET: case OP_FPUT:
			{
				int hv = code[pc++], kind = code[pc++], ext = code[pc++];
				getPut (op == OP_FGET, hv, kind, ext);
				break;
			}
			case OP_FIELD:
			{
				int n = code[pc++];
				V *refs[MAXBIND]; int w[MAXBIND];
				if (n > MAXBIND) { fail ("Too many FIELD variables"); break; }
				for (int i = n - 1; i >= 0; i--) { refs[i] = popRef (); w[i] = (int) popI (); }
				long long fn = popI ();
				if (failed) break;
				File *f = file (fn); if (!f) break;
				if (f->mode != 4) { fail ("Bad file mode (FIELD needs a RANDOM file)"); break; }
				int off = 0;
				for (int i = 0; i < n; i++)
				{
					if (w[i] < 0 || off + w[i] > f->reclen) { fail ("FIELD overflow"); break; }
					f->b[i].ptr = refs[i]; f->b[i].off = off; f->b[i].w = w[i]; off += w[i];
				}
				if (failed) break;
				f->nb = n;
				refreshFields (f);
				break;
			}
			case OP_SEEK:
			{
				long long p = popI (), n = popI ();
				File *f = file (n); if (!f) break;
				if (p < 1) { fail ("Bad record number"); break; }
				if (f->mode == 4) f->nextRec = (int) p; else f->pos = (int) p - 1;
				break;
			}
			case OP_ONERR:
			{
				int t = code[pc++];
				if (t < 0 && inErr)					// ON ERROR GOTO 0 in a handler: stop with the error
				{
					inErr = false; failed = true; err->line = errLineSaved; bscpy (err->msg, errMsgSaved, sizeof err->msg);
					break;
				}
				onErr = t;
				break;
			}
			case OP_RESUME:
			{
				int m = code[pc++], t = code[pc++];
				if (!inErr) { fail ("RESUME without error"); break; }
				inErr = false; errCode = 0;
				pc = m == 2 ? t : stmtOf (errPc, m == 1);
				break;
			}
			case OP_ONEVENT:
			{
				int kind = code[pc++], t = code[pc++];
				long long n = popI ();
				if (kind == 0)
				{
					if (n < 1 || n > 86400) { fail ("Illegal function call (ON TIMER)"); break; }
					timerInt = (double) n; timerNext = H.timer () + timerInt; ev[0].target = t;
				}
				else
				{
					if (n < 1 || n > 31 || (n > 25 && n < 30)) { fail ("Illegal function call (ON KEY)"); break; }
					ev[n].target = t;
				}
				evAny = true;
				break;
			}
			case OP_EVSTATE:
			{
				int kind = code[pc++], st = code[pc++];
				long long n = popI ();
				int i = kind == 0 ? 0 : (int) n;
				if (i < 0 || i > 31) { fail ("Illegal function call (KEY)"); break; }
				if (kind == 1 && n == 0) { for (int k = 1; k < 32; k++) { ev[k].state = st; if (!st) ev[k].pending = false; } break; }
				ev[i].state = st;
				if (st == 0) ev[i].pending = false;
				if (i == 0 && st == 1) timerNext = H.timer () + timerInt;
				evAny = true;
				break;
			}
			case OP_CHAIN:
			{
				V pv = pop (); cstr (pv, chainPath, sizeof chainPath); vclear (pv);
				chainCommon = true; ended = true;
				break;
			}
			case OP_RUN:
			{
				int m = code[pc++], t = code[pc++];
				if (m == 2) { V pv = pop (); cstr (pv, chainPath, sizeof chainPath); vclear (pv); chainCommon = false; closeAll (); ended = true; break; }
				restart ();
				pc = m == 1 ? t : 0;
				break;
			}
			case OP_CLEAR:
				if (nf > 0) { fail ("Illegal function call (CLEAR in a SUB)"); break; }
				while (sp > 0) vclear (stack[--sp]);
				ngs = 0; closeAll (); resetGlobals ();
				break;
			case OP_TRON: tron = code[pc++] != 0; traceLine = -1; break;
			case OP_END: case OP_STOP: ended = true; break;
			case OP_NOP: break;
			default: fail ("Bad bytecode", 51); break;
			}
		}
		bool wasFailed = failed;
		failed = false;
		chan = 0;
		if (!(chainPath[0] && chainCommon)) closeAll ();		// (a CHAIN keeps the files open)
		if (failed && !wasFailed) wasFailed = true;
		return wasFailed ? -1 : 0;
	}
};

// The program file of a CHAIN / RUN "file" (".bas" added when the name has no extension).
static char *loadProgram (Host &h, const char *path, int *len)
{
	char *src = h.load (path, len);
	if (src) return src;
	int n = bslen (path), dot = 0;
	for (int i = 0; i < n; i++) { if (path[i] == '.') dot = 1; if (path[i] == '/' || path[i] == ':') dot = 0; }
	if (dot) return 0;
	char p2[260]; bscpy (p2, path, 250); int k = bslen (p2);
	p2[k++] = '.'; p2[k++] = 'b'; p2[k++] = 'a'; p2[k++] = 's'; p2[k] = 0;
	return h.load (p2, len);
}

int run (Program *p, Host &host, Error *err)
{
	err->line = 0; err->msg[0] = 0;
	Program *cur = p, *owned = 0;
	VM *prev = 0; bool prevCommons = false;
	int r = 0;
	for (;;)
	{
		VM *vm = new VM (cur, host, err);
		if (prev)
		{
			vm->importFrom (*prev, prevCommons);
			Program *old = prev->P;
			delete prev; prev = 0;
			if (old != p && old != cur) delete old;
		}
		r = vm->run ();
		if (r == 0 && vm->chainPath[0])
		{
			int len = 0;
			char *src = loadProgram (host, vm->chainPath, &len);
			if (!src)
			{
				err->line = 0; bscpy (err->msg, "File not found (CHAIN)", sizeof err->msg);
				r = -1; delete vm; break;
			}
			char *z = new char[len + 1]; bmcpy (z, src, len); z[len] = 0; delete [] src;
			Error e2;
			Program *np = compile (z, &e2);
			delete [] z;
			if (!np)
			{
				*err = e2;
				char m[120]; int k = 0;
				const char *pre = "CHAIN: ";
				for (int i = 0; pre[i]; i++) m[k++] = pre[i];
				for (int i = 0; e2.msg[i] && k < 118; i++) m[k++] = e2.msg[i];
				m[k] = 0; bscpy (err->msg, m, sizeof err->msg);
				r = -1; delete vm; break;
			}
			prev = vm; prevCommons = vm->chainCommon;
			owned = np; cur = np;
			continue;
		}
		delete vm;
		break;
	}
	if (owned && owned != p) delete owned;
	host.finished (r != 0);
	return r;
}

} // namespace bas
