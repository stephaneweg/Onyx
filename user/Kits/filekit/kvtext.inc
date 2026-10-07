//
// kvtext.inc -- the code of kvtext.h (FileKit: kvtext.cpp compiles it into filekit.so; a PC build has it inline).
// Valid C and C++; malloc / realloc / free; the files through AppKit (kapi_open / kapi_fsize / kapi_read,
// kapi_save_file).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include <stdlib.h>

#ifdef FK_KV_IMPL
#  ifdef __cplusplus
#    define FK_KV_FN	extern "C"
#  else
#    define FK_KV_FN
#  endif
#else
#  define FK_KV_FN	static inline
#endif

typedef struct fkkv_entry_ { char *key, *val; int block, line; } fkkv_entry_;
typedef struct fkkv_blk_ { char *name; int line; } fkkv_blk_;
struct fk_kv
{
	int flags;
	fkkv_entry_ *e; int n, cap;			// the entries, grouped by block in ascending order (the file's order)
	fkkv_blk_ *b; int nb, capb;			// b[0]: the part before any header; b[1...]: the "[...]" lines
	char *text;					// fk_kv_text's result
};

// ---- small helpers ----
static inline int fkkv_len_ (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline int fkkv_eq_ (const char *a, const char *b)
{
	if (!a) a = "";
	if (!b) b = "";
	while (*a && *a == *b) { a++; b++; }
	return *a == *b;
}
static inline char *fkkv_ndup_ (const char *s, int n)
{
	char *d = (char *) malloc ((size_t) n + 1);
	int i;
	if (!d) return 0;
	for (i = 0; i < n; i++) d[i] = s[i];
	d[n] = 0;
	return d;
}
static inline char *fkkv_dup_ (const char *s) { return fkkv_ndup_ (s ? s : "", fkkv_len_ (s)); }
static inline int fkkv_space_ (char c) { return c == ' ' || c == '\t'; }
// A value read with FK_KV_ESCAPES: "\n" -> a new line, "\\" -> '\' (in place; anything else kept)
static inline void fkkv_unescape_ (char *s)
{
	char *w = s, *r;
	for (r = s; *r; r++)
	{
		if (*r == '\\' && r[1] == 'n') { *w++ = '\n'; r++; }
		else if (*r == '\\' && r[1] == '\\') { *w++ = '\\'; r++; }
		else *w++ = *r;
	}
	*w = 0;
}
static inline int fkkv_grow_e_ (fk_kv *kv)
{
	if (kv->n < kv->cap) return 0;
	{
		int c = kv->cap ? kv->cap * 2 : 32;
		fkkv_entry_ *ne = (fkkv_entry_ *) realloc (kv->e, (size_t) c * sizeof (fkkv_entry_));
		if (!ne) return -1;
		kv->e = ne; kv->cap = c;
	}
	return 0;
}
// A new block (its name copied) -> its number, -1 no memory
static inline int fkkv_add_block_ (fk_kv *kv, const char *name, int nlen, int line)
{
	char *nm;
	if (kv->nb == kv->capb)
	{
		int c = kv->capb ? kv->capb * 2 : 16;
		fkkv_blk_ *nb = (fkkv_blk_ *) realloc (kv->b, (size_t) c * sizeof (fkkv_blk_));
		if (!nb) return -1;
		kv->b = nb; kv->capb = c;
	}
	nm = fkkv_ndup_ (name, nlen);
	if (!nm) return -1;
	kv->b[kv->nb].name = nm; kv->b[kv->nb].line = line;
	return kv->nb++;
}
// An entry put at index `at` (key / value taken, not copied) -> 0, -1 no memory (then they are freed)
static inline int fkkv_insert_ (fk_kv *kv, int at, char *key, char *val, int block, int line)
{
	int k;
	if (!key || !val || fkkv_grow_e_ (kv)) { free (key); free (val); return -1; }
	for (k = kv->n; k > at; k--) kv->e[k] = kv->e[k - 1];
	kv->e[at].key = key; kv->e[at].val = val; kv->e[at].block = block; kv->e[at].line = line;
	kv->n++;
	return 0;
}
// The first entry of (section, key) -> its index, -1 none
static inline int fkkv_find_ (const fk_kv *kv, const char *section, const char *key)
{
	int i;
	if (!kv) return -1;
	for (i = 0; i < kv->n; i++)
		if (fkkv_eq_ (kv->b[kv->e[i].block].name, section) && fkkv_eq_ (kv->e[i].key, key)) return i;
	return -1;
}

// ---- making and freeing ----
FK_KV_FN fk_kv *fk_kv_new (int flags)
{
	fk_kv *kv = (fk_kv *) malloc (sizeof (fk_kv));
	if (!kv) return 0;
	kv->flags = flags; kv->e = 0; kv->n = kv->cap = 0; kv->b = 0; kv->nb = kv->capb = 0; kv->text = 0;
	if (fkkv_add_block_ (kv, "", 0, 0) < 0) { free (kv); return 0; }
	return kv;
}
FK_KV_FN void fk_kv_free (fk_kv *kv)
{
	int i;
	if (!kv) return;
	for (i = 0; i < kv->n; i++) { free (kv->e[i].key); free (kv->e[i].val); }
	for (i = 0; i < kv->nb; i++) free (kv->b[i].name);
	free (kv->e); free (kv->b); free (kv->text); free (kv);
}
FK_KV_FN fk_kv *fk_kv_parse (const char *text, int flags)
{
	fk_kv *kv = fk_kv_new (flags);
	const char *p = text ? text : "";
	int line = 0, block = 0;
	int pipe = -1, piped = 0;			// the entry "|" lines go on (-1: none); its value already has "|" lines
	if (!kv) return 0;
	if ((unsigned char) p[0] == 0xEF && (unsigned char) p[1] == 0xBB && (unsigned char) p[2] == 0xBF) p += 3;	// (a UTF-8 BOM)
	while (*p)
	{
		const char *s = p, *e = p;
		int n;
		while (*e && *e != '\n') e++;
		p = *e ? e + 1 : e;
		line++;
		n = (int) (e - s);
		if (n && s[n - 1] == '\r') n--;
		while (n && fkkv_space_ (*s)) { s++; n--; }
		if ((flags & FK_KV_PIPES) && n && *s == '|')
		{
			char *v, *nv; int vl, ol;
			if (pipe < 0) continue;			// (a "|" line with nothing before it)
			s++; n--;
			if (n && *s == ' ') { s++; n--; }
			v = fkkv_ndup_ (s, n);
			if (!v) goto nomem;
			if (flags & FK_KV_ESCAPES) fkkv_unescape_ (v);
			vl = fkkv_len_ (v);
			if (!piped && !kv->e[pipe].val[0])	// "key =" empty: the value starts on this line
			{
				free (kv->e[pipe].val); kv->e[pipe].val = v; kv->e[pipe].line = line;
			}
			else
			{
				ol = fkkv_len_ (kv->e[pipe].val);
				nv = (char *) realloc (kv->e[pipe].val, (size_t) ol + vl + 2);
				if (!nv) { free (v); goto nomem; }
				nv[ol] = '\n';
				{ int k; for (k = 0; k <= vl; k++) nv[ol + 1 + k] = v[k]; }
				kv->e[pipe].val = nv; free (v);
			}
			piped = 1;
			continue;
		}
		pipe = -1; piped = 0;
		if (!n || *s == '#' || *s == ';') continue;
		if (*s == '[')
		{
			const char *nm = s + 1; int nl = 0;
			while (nl < n - 1 && nm[nl] != ']') nl++;
			while (nl && fkkv_space_ (*nm)) { nm++; nl--; }
			while (nl && fkkv_space_ (nm[nl - 1])) nl--;
			block = fkkv_add_block_ (kv, nm, nl, line);
			if (block < 0) goto nomem;
			continue;
		}
		{
			int eq = 0, kl, vs, vl;
			char *key, *val;
			while (eq < n && s[eq] != '=') eq++;
			if (eq == n) continue;			// (no '=': skipped)
			kl = eq; while (kl && fkkv_space_ (s[kl - 1])) kl--;
			if (!kl) continue;
			vs = eq + 1; while (vs < n && fkkv_space_ (s[vs])) vs++;
			vl = n; while (vl > vs && fkkv_space_ (s[vl - 1])) vl--;
			key = fkkv_ndup_ (s, kl); val = fkkv_ndup_ (s + vs, vl - vs);
			if (val && (flags & FK_KV_ESCAPES)) fkkv_unescape_ (val);
			if (fkkv_insert_ (kv, kv->n, key, val, block, line)) goto nomem;
			pipe = kv->n - 1;
		}
	}
	return kv;
nomem:
	fk_kv_free (kv);
	return 0;
}
FK_KV_FN fk_kv *fk_kv_load (const char *path, int flags)
{
	void *f = kapi_open (path);
	unsigned n, done = 0;
	char *buf;
	fk_kv *kv;
	if (!f) return 0;
	n = kapi_fsize (f);
	if (n == 0xFFFFFFFFu) { kapi_close (f); return 0; }
	buf = (char *) malloc ((size_t) n + 1);
	if (!buf) { kapi_close (f); return 0; }
	while (done < n)
	{
		int r = kapi_read (f, buf + done, n - done);
		if (r <= 0) break;
		done += (unsigned) r;
	}
	kapi_close (f);
	if (done < n) { free (buf); return 0; }
	buf[n] = 0;
	kv = fk_kv_parse (buf, flags);
	free (buf);
	return kv;
}

// ---- the entries ----
FK_KV_FN int fk_kv_count (const fk_kv *kv) { return kv ? kv->n : 0; }
FK_KV_FN const char *fk_kv_key (const fk_kv *kv, int i) { return kv && i >= 0 && i < kv->n ? kv->e[i].key : ""; }
FK_KV_FN const char *fk_kv_value (const fk_kv *kv, int i) { return kv && i >= 0 && i < kv->n ? kv->e[i].val : ""; }
FK_KV_FN const char *fk_kv_section (const fk_kv *kv, int i) { return kv && i >= 0 && i < kv->n ? kv->b[kv->e[i].block].name : ""; }
FK_KV_FN int fk_kv_block (const fk_kv *kv, int i) { return kv && i >= 0 && i < kv->n ? kv->e[i].block : 0; }
FK_KV_FN int fk_kv_line (const fk_kv *kv, int i) { return kv && i >= 0 && i < kv->n ? kv->e[i].line : 0; }

// ---- the blocks ----
FK_KV_FN int fk_kv_blocks (const fk_kv *kv) { return kv ? kv->nb - 1 : 0; }
FK_KV_FN const char *fk_kv_block_name (const fk_kv *kv, int b) { return kv && b > 0 && b < kv->nb ? kv->b[b].name : ""; }
FK_KV_FN int fk_kv_block_line (const fk_kv *kv, int b) { return kv && b > 0 && b < kv->nb ? kv->b[b].line : 0; }

// ---- looking up and changing ----
FK_KV_FN const char *fk_kv_get (const fk_kv *kv, const char *section, const char *key, const char *def)
{
	int i = fkkv_find_ (kv, section, key);
	return i >= 0 ? kv->e[i].val : def;
}
FK_KV_FN int fk_kv_set (fk_kv *kv, const char *section, const char *key, const char *value)
{
	int i, b, at;
	if (!kv || !key || !key[0]) return -1;
	i = fkkv_find_ (kv, section, key);
	if (i >= 0)
	{
		char *v = fkkv_dup_ (value);
		if (!v) return -1;
		free (kv->e[i].val); kv->e[i].val = v;
		return 0;
	}
	if (!section) section = "";
	b = -1;
	if (!section[0]) b = 0;
	else for (i = 1; i < kv->nb; i++) if (fkkv_eq_ (kv->b[i].name, section)) { b = i; break; }
	if (b < 0 && (b = fkkv_add_block_ (kv, section, fkkv_len_ (section), 0)) < 0) return -1;
	for (at = 0; at < kv->n && kv->e[at].block <= b; at++) ;	// (after the block's last entry)
	return fkkv_insert_ (kv, at, fkkv_dup_ (key), fkkv_dup_ (value), b, 0);
}
FK_KV_FN int fk_kv_remove (fk_kv *kv, const char *section, const char *key)
{
	int i = fkkv_find_ (kv, section, key), k;
	if (i < 0) return 0;
	free (kv->e[i].key); free (kv->e[i].val);
	for (k = i; k + 1 < kv->n; k++) kv->e[k] = kv->e[k + 1];
	kv->n--;
	return 1;
}

// ---- writing ----
typedef struct fkkv_out_ { char *d; int n, cap, bad; } fkkv_out_;
static inline void fkkv_put_ (fkkv_out_ *o, const char *s, int n)
{
	int i;
	if (o->bad) return;
	if (o->n + n + 1 > o->cap)
	{
		int c = o->cap ? o->cap : 1024;
		char *nd;
		while (o->n + n + 1 > c) c *= 2;
		nd = (char *) realloc (o->d, (size_t) c);
		if (!nd) { o->bad = 1; return; }
		o->d = nd; o->cap = c;
	}
	for (i = 0; i < n; i++) o->d[o->n++] = s[i];
	o->d[o->n] = 0;
}
static inline void fkkv_puts_ (fkkv_out_ *o, const char *s) { fkkv_put_ (o, s, fkkv_len_ (s)); }
static inline int fkkv_has_nl_ (const char *s) { for (; *s; s++) if (*s == '\n') return 1; return 0; }
static inline void fkkv_put_entry_ (fkkv_out_ *o, const fkkv_entry_ *e, int flags)
{
	const char *v = e->val;
	fkkv_puts_ (o, e->key);
	if (flags & FK_KV_ESCAPES)
	{
		fkkv_puts_ (o, " = ");
		for (; *v; v++)
		{
			if (*v == '\n') fkkv_put_ (o, "\\n", 2);
			else if (*v == '\\') fkkv_put_ (o, "\\\\", 2);
			else if (*v != '\r') fkkv_put_ (o, v, 1);
		}
		fkkv_put_ (o, "\n", 1);
	}
	else if ((flags & FK_KV_PIPES) && fkkv_has_nl_ (v))
	{
		fkkv_puts_ (o, " =\n");
		for (;;)
		{
			const char *q = v;
			while (*q && *q != '\n') q++;
			fkkv_put_ (o, "| ", 2); fkkv_put_ (o, v, (int) (q - v)); fkkv_put_ (o, "\n", 1);
			if (!*q) break;
			v = q + 1;
		}
	}
	else
	{
		fkkv_puts_ (o, v[0] ? " = " : " =");
		for (; *v; v++) fkkv_put_ (o, *v == '\n' ? " " : v, 1);
		fkkv_put_ (o, "\n", 1);
	}
}
FK_KV_FN const char *fk_kv_text (fk_kv *kv, const char *comment, int *len)
{
	fkkv_out_ o;
	int b, i = 0;
	if (len) *len = 0;
	if (!kv) return "";
	o.d = 0; o.n = o.cap = 0; o.bad = 0;
	fkkv_put_ (&o, "", 0);
	if (comment && comment[0])
	{
		if (comment[0] != '#') fkkv_puts_ (&o, "# ");
		for (; *comment && *comment != '\n'; comment++) fkkv_put_ (&o, comment, 1);
		fkkv_put_ (&o, "\n", 1);
	}
	for (b = 0; b < kv->nb; b++)
	{
		if (b > 0)
		{
			if (o.n) fkkv_put_ (&o, "\n", 1);
			fkkv_put_ (&o, "[", 1); fkkv_puts_ (&o, kv->b[b].name); fkkv_put_ (&o, "]\n", 2);
		}
		for (; i < kv->n && kv->e[i].block == b; i++) fkkv_put_entry_ (&o, &kv->e[i], kv->flags);
	}
	if (o.bad) { free (o.d); return ""; }
	free (kv->text); kv->text = o.d;
	if (len) *len = o.n;
	return kv->text;
}
FK_KV_FN int fk_kv_save (fk_kv *kv, const char *path, const char *comment)
{
	int n = 0;
	const char *t;
	if (!kv) return -1;
	t = fk_kv_text (kv, comment, &n);
	if (n == 0 && (kv->n > 0 || kv->nb > 1 || (comment && comment[0]))) return -1;	// (no memory for the text)
	return kapi_save_file (path, t, (unsigned) n) == n ? 0 : -1;
}
