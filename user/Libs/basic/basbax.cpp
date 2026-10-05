//
// basic/basbax.cpp -- compiled BASIC programs (.bax): a bas::Program written as bytes, so a
// program runs without being parsed again (the qbasic editor's Run > Make .bax, basic -c,
// the PC editor). Portable like the rest of the core: no libc.
//
// Layout (little-endian): "OBAX", the format version, the counts of opcodes / built-in
// functions / statements of the VM that wrote it (a .bax from another VM is refused, it
// would run the wrong instructions), then every table of the Program in order.
//
#include "basic/basint.h"

namespace bas {

enum { BAX_FORMAT = 4 };			// (2: the classes' tables; 3: the program's flags -- managed; 4: the kits; older files still load)

namespace {
struct Out
{
	char *b; int n, cap;
	Out () : b (0), n (0), cap (0) {}
	void byte (int c)
	{
		if (n == cap) { int nc = cap ? cap * 2 : 4096; char *nb = new char[nc]; bmcpy (nb, b, n); delete [] b; b = nb; cap = nc; }
		b[n++] = (char) c;
	}
	void i32 (int v) { unsigned u = (unsigned) v; for (int i = 0; i < 4; i++) byte ((int) ((u >> (i * 8)) & 0xFF)); }
	void f64 (double d) { unsigned char r[8]; bmcpy (r, &d, 8); for (int i = 0; i < 8; i++) byte (r[i]); }
	void bytes (const char *s, int len) { for (int i = 0; i < len; i++) byte ((unsigned char) s[i]); }
	void name (const char *s) { int len = bslen (s); i32 (len); bytes (s, len); }
};
struct In
{
	const unsigned char *b; int n, at; bool bad;
	int i32 () { if (at + 4 > n) { bad = true; return 0; } unsigned u = 0; for (int i = 0; i < 4; i++) u |= (unsigned) b[at + i] << (i * 8); at += 4; return (int) u; }
	double f64 () { double d = 0; if (at + 8 > n) { bad = true; return 0; } bmcpy (&d, b + at, 8); at += 8; return d; }
	int count () { int c = i32 (); if (c < 0 || c > n) { bad = true; return 0; } return c; }
	char *str (int *len)
	{
		int l = count (); if (bad || at + l > n) { bad = true; l = 0; }
		char *s = new char[l + 1]; if (l) bmcpy (s, b + at, l); s[l] = 0; at += l;
		if (len) *len = l;
		return s;
	}
	void name (char *out, int cap) { int l; char *s = str (&l); bscpy (out, s, cap); delete [] s; }
};
}

bool isBax (const char *buf, int len) { return len >= 4 && buf[0] == 'O' && buf[1] == 'B' && buf[2] == 'A' && buf[3] == 'X'; }

int saveBax (const Program *p, char **out)
{
	Out o;
	o.bytes ("OBAX", 4);
	o.i32 (BAX_FORMAT); o.i32 (OP_COUNT_); o.i32 (B_LAST); o.i32 (S_LAST);
	o.i32 (p->nglobals);
	o.i32 (p->code.n); for (int i = 0; i < p->code.n; i++) o.i32 (p->code[i]);
	o.i32 (p->nums.n); for (int i = 0; i < p->nums.n; i++) o.f64 (p->nums[i]);
	o.i32 (p->strs.n); for (int i = 0; i < p->strs.n; i++) { o.i32 (p->strl[i]); o.bytes (p->strs[i], p->strl[i]); }
	o.i32 (p->lines.n); for (int i = 0; i < p->lines.n; i++) { o.i32 (p->lines[i].pc); o.i32 (p->lines[i].line); }
	o.i32 (p->data.n); for (int i = 0; i < p->data.n; i++) { o.i32 (p->data[i].isStr ? 1 : 0); o.name (p->data[i].text ? p->data[i].text : ""); }
	o.i32 (p->procs.n);
	for (int i = 0; i < p->procs.n; i++)
	{
		const ProcInfo &r = p->procs[i];
		o.name (r.name); o.i32 (r.isFunc ? 1 : 0); o.i32 (r.retTy); o.i32 (r.nparams); o.i32 (r.entry); o.i32 (r.nlocals); o.i32 (r.kindOff);
	}
	o.i32 (p->gkind.n); for (int i = 0; i < p->gkind.n; i++) o.byte (p->gkind[i]);
	o.i32 (p->lkind.n); for (int i = 0; i < p->lkind.n; i++) o.byte (p->lkind[i]);
	o.i32 (p->gext.n); for (int i = 0; i < p->gext.n; i++) o.i32 (p->gext[i]);
	o.i32 (p->lext.n); for (int i = 0; i < p->lext.n; i++) o.i32 (p->lext[i]);
	o.i32 (p->types.n);
	for (int i = 0; i < p->types.n; i++) { o.name (p->types[i].name); o.i32 (p->types[i].first); o.i32 (p->types[i].nf); o.i32 (p->types[i].size); }
	o.i32 (p->fields.n); for (int i = 0; i < p->fields.n; i++) { o.i32 (p->fields[i].kind); o.i32 (p->fields[i].len); o.i32 (p->fields[i].sub); }
	o.i32 (p->stmts.n); for (int i = 0; i < p->stmts.n; i++) { o.i32 (p->stmts[i].start); o.i32 (p->stmts[i].end); }
	o.i32 (p->numLabels.n); for (int i = 0; i < p->numLabels.n; i++) { o.i32 (p->numLabels[i].pc); o.i32 (p->numLabels[i].value); }
	o.i32 (p->common.n); for (int i = 0; i < p->common.n; i++) o.i32 (p->common[i]);
	for (int i = 0; i < p->types.n; i++)
	{
		const TypeInfo &t = p->types[i];
		o.i32 (t.kind); o.i32 (t.parent); o.i32 (t.vt); o.i32 (t.nvt); o.i32 (t.it); o.i32 (t.nit); o.i32 (t.dtor);
	}
	o.i32 (p->vtab.n); for (int i = 0; i < p->vtab.n; i++) o.i32 (p->vtab[i]);
	o.i32 (p->itab.n); for (int i = 0; i < p->itab.n; i++) o.i32 (p->itab[i]);
	o.i32 (p->managed ? 1 : 0);
	o.i32 (p->kits.n); for (int i = 0; i < p->kits.n; i++) { o.name (p->kits[i].name); o.i32 (p->kits[i].minVer); }
	o.i32 (p->kfns.n);
	for (int i = 0; i < p->kfns.n; i++)
	{
		const KitFn &f = p->kfns[i];
		o.i32 (f.kit); o.i32 (f.slot); o.byte (f.ret); o.name (f.args); o.name (f.name);
	}
	o.bytes ("END.", 4);
	*out = o.b;
	return o.n;
}

static void setErr (Error *e, const char *m) { if (e) { e->line = 0; bscpy (e->msg, m, sizeof e->msg); } }

Program *loadBax (const char *buf, int len, Error *err)
{
	if (!isBax (buf, len)) { setErr (err, "Not a compiled program (.bax)"); return 0; }
	In in; in.b = (const unsigned char *) buf; in.n = len; in.at = 4; in.bad = false;
	// (the enums only grow: a .bax from an older compiler loads)
	int fmt = in.i32 ();
	if (fmt < 1 || fmt > BAX_FORMAT || in.i32 () > OP_COUNT_ || in.i32 () > B_LAST || in.i32 () > S_LAST)
	{ setErr (err, "This .bax was made by another BASIC version: compile the .bas again"); return 0; }
	Program *p = new Program;
	p->nglobals = in.i32 ();
	int c = in.count (); for (int i = 0; i < c && !in.bad; i++) p->code.push (in.i32 ());
	c = in.count (); for (int i = 0; i < c && !in.bad; i++) p->nums.push (in.f64 ());
	c = in.count (); for (int i = 0; i < c && !in.bad; i++) { int l; char *s = in.str (&l); p->strs.push (s); p->strl.push (l); }
	c = in.count (); for (int i = 0; i < c && !in.bad; i++) { LineMark m; m.pc = in.i32 (); m.line = in.i32 (); p->lines.push (m); }
	c = in.count (); for (int i = 0; i < c && !in.bad; i++) { DataItem d; d.isStr = in.i32 () != 0; d.text = in.str (0); p->data.push (d); }
	c = in.count ();
	for (int i = 0; i < c && !in.bad; i++)
	{
		ProcInfo r; in.name (r.name, sizeof r.name); r.isFunc = in.i32 () != 0; r.retTy = in.i32 (); r.nparams = in.i32 ();
		r.entry = in.i32 (); r.nlocals = in.i32 (); r.kindOff = in.i32 ();
		p->procs.push (r);
	}
	c = in.count (); for (int i = 0; i < c && !in.bad; i++) { if (in.at >= in.n) { in.bad = true; break; } p->gkind.push (in.b[in.at++]); }
	c = in.count (); for (int i = 0; i < c && !in.bad; i++) { if (in.at >= in.n) { in.bad = true; break; } p->lkind.push (in.b[in.at++]); }
	c = in.count (); for (int i = 0; i < c && !in.bad; i++) p->gext.push (in.i32 ());
	c = in.count (); for (int i = 0; i < c && !in.bad; i++) p->lext.push (in.i32 ());
	c = in.count ();
	for (int i = 0; i < c && !in.bad; i++) { TypeInfo t; in.name (t.name, sizeof t.name); t.first = in.i32 (); t.nf = in.i32 (); t.size = in.i32 (); p->types.push (t); }
	c = in.count (); for (int i = 0; i < c && !in.bad; i++) { FieldInfo f; f.kind = in.i32 (); f.len = in.i32 (); f.sub = in.i32 (); p->fields.push (f); }
	c = in.count (); for (int i = 0; i < c && !in.bad; i++) { StmtRange s; s.start = in.i32 (); s.end = in.i32 (); p->stmts.push (s); }
	c = in.count (); for (int i = 0; i < c && !in.bad; i++) { NumLabel l; l.pc = in.i32 (); l.value = in.i32 (); p->numLabels.push (l); }
	c = in.count (); for (int i = 0; i < c && !in.bad; i++) p->common.push (in.i32 ());
	if (fmt >= 2)
	{
		for (int i = 0; i < p->types.n && !in.bad; i++)
		{
			TypeInfo &t = p->types[i];
			t.kind = in.i32 (); t.parent = in.i32 (); t.vt = in.i32 (); t.nvt = in.i32 (); t.it = in.i32 (); t.nit = in.i32 (); t.dtor = in.i32 ();
		}
		c = in.count (); for (int i = 0; i < c && !in.bad; i++) p->vtab.push (in.i32 ());
		c = in.count (); for (int i = 0; i < c && !in.bad; i++) p->itab.push (in.i32 ());
	}
	if (fmt >= 3) p->managed = (in.i32 () & 1) != 0;
	if (fmt >= 4)
	{
		c = in.count (); for (int i = 0; i < c && !in.bad; i++) { KitRef k; in.name (k.name, sizeof k.name); k.minVer = in.i32 (); p->kits.push (k); }
		c = in.count ();
		for (int i = 0; i < c && !in.bad; i++)
		{
			KitFn f; f.kit = in.i32 (); f.slot = in.i32 ();
			if (in.at >= in.n) { in.bad = true; break; }
			f.ret = (char) in.b[in.at++];
			in.name (f.args, sizeof f.args); in.name (f.name, sizeof f.name);
			if (f.kit < 0 || f.kit >= p->kits.n || f.slot < 0) in.bad = true;
			p->kfns.push (f);
		}
	}
	if (in.bad || in.at + 4 > in.n || in.b[in.at] != 'E' || in.b[in.at + 3] != '.'
	    || p->gkind.n != p->nglobals || p->gext.n != p->nglobals)
	{ delete p; setErr (err, "The .bax file is damaged"); return 0; }
	return p;
}

int attachBax (const char *runtime, int rlen, const char *bax, int blen, char **out)
{
	// (a runtime that already carries a program: the new one takes its place)
	unsigned o, l;
	if (rlen >= BAX_TRAILER && attachedBax (runtime + rlen - BAX_TRAILER, (unsigned) rlen, &o, &l)) rlen = (int) o;
	char *b = new char[rlen + blen + BAX_TRAILER];
	bmcpy (b, runtime, rlen); bmcpy (b + rlen, bax, blen);
	char *t = b + rlen + blen;
	bmcpy (t, "OBAXAPP1", 8);
	for (int i = 0; i < 4; i++) { t[8 + i] = (char) (((unsigned) rlen >> (8 * i)) & 0xFF); t[12 + i] = (char) (((unsigned) blen >> (8 * i)) & 0xFF); }
	*out = b;
	return rlen + blen + BAX_TRAILER;
}
bool attachedBax (const char *t, unsigned fileSize, unsigned *off, unsigned *len)
{
	const char *m = "OBAXAPP1";
	for (int i = 0; i < 8; i++) if (t[i] != m[i]) return false;
	unsigned o = 0, l = 0;
	for (int i = 0; i < 4; i++) { o |= (unsigned) (unsigned char) t[8 + i] << (8 * i); l |= (unsigned) (unsigned char) t[12 + i] << (8 * i); }
	if (l == 0 || o > fileSize || l > fileSize || o + l + BAX_TRAILER != fileSize) return false;
	*off = o; *len = l;
	return true;
}

void setManaged (Program *p, bool on) { if (p) p->managed = on; }
bool isManaged (const Program *p) { return p && p->managed; }

// A program from a file's bytes: a .bax as it is, else BASIC source (compiled).
Program *load (const char *buf, int len, Error *err)
{
	if (isBax (buf, len)) return loadBax (buf, len, err);
	char *z = new char[len + 1]; bmcpy (z, buf, len); z[len] = 0;
	Program *p = compile (z, err);
	delete [] z;
	return p;
}

} // namespace bas
