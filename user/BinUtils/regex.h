//
// regex.h -- the regular expressions of grep, sed and ed: the classic "basic" ones, matched
// straight from the pattern's text (no compile step, no memory). No kapi, no libc.
//
//   c        the character c            .        any character
//   [abc] [a-z] [^0-9]  one of / a range / none of      (a ] first in the set is itself)
//   x*       x zero or more times       x\+  one or more       x\?  zero or one
//            (x: one character, a . or a [set] -- the longest match is taken)
//   ^ at the start, $ at the end        the start / the end of the line
//   \( ... \)   a group, kept as \1 .. \9 (in the pattern too: a back-reference)
//   \.  \*  \[  \\  \/ ...   that character itself;  \t a tab,  \n a line feed
//
// No alternation (|) and no repeated group: enough for the tools, small and predictable.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
// hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
// do so, subject to the following conditions: The above copyright notice and this permission
// notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
// IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef _bin_regex_h
#define _bin_regex_h

#define RE_GROUPS	10		// \0 (the whole match) and \1 .. \9

struct re_match
{
	const char *sp[RE_GROUPS], *ep[RE_GROUPS];	// each group's start and end (0: not set)
};

struct re__ctx
{
	const char *bol, *end;		// the text's first character, and its end
	int icase;
	int ngroups;			// groups opened so far
	int open[RE_GROUPS], depth;	// the groups not closed yet
	struct re_match *m;
};

static inline int re__fold (int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

// the length of the atom at p (a character, an escape, a set); 0 at the pattern's end
static inline int re__atomlen (const char *p)
{
	if (*p == '\0') return 0;
	if (*p == '\\') return p[1] ? 2 : 1;
	if (*p == '[')
	{
		const char *q = p + 1;
		if (*q == '^') q++;
		if (*q == ']') q++;
		while (*q && *q != ']') q++;
		return *q ? (int) (q - p) + 1 : 1;		// (no closing ]: a plain [)
	}
	return 1;
}

// does the atom at p (alen long) match the character c?
static inline int re__atom (const struct re__ctx *x, const char *p, int alen, int c)
{
	if (alen == 1) return *p == '.' ? 1 : x->icase ? re__fold (*p) == re__fold (c) : *p == c;
	if (*p == '\\')
	{
		int e = p[1] == 't' ? '\t' : p[1] == 'n' ? '\n' : p[1];
		return x->icase ? re__fold (e) == re__fold (c) : e == c;
	}
	const char *q = p + 1, *e = p + alen - 1;		// a set
	int neg = 0, hit = 0;
	if (*q == '^') { neg = 1; q++; }
	int cc = x->icase ? re__fold (c) : c;
	while (q < e)
	{
		int lo = (unsigned char) *q, hi = lo;
		if (q + 2 < e && q[1] == '-') { hi = (unsigned char) q[2]; q += 3; } else q++;
		if (((unsigned char) c >= lo && (unsigned char) c <= hi)
		    || (x->icase && (unsigned char) cc >= re__fold (lo) && (unsigned char) cc <= re__fold (hi)))
			hit = 1;
	}
	return hit != neg;
}

// does the pattern at p match the text at t? -> the match's end, or 0
static inline const char *re__here (struct re__ctx *x, const char *p, const char *t)
{
	for (;;)
	{
		if (*p == '\0') return t;
		if (p[0] == '\\' && p[1] == '(')
		{
			if (x->ngroups >= RE_GROUPS - 1) return 0;
			int g = ++x->ngroups;
			const char *os = x->m->sp[g], *oe = x->m->ep[g];
			x->m->sp[g] = t; x->m->ep[g] = 0; x->open[x->depth++] = g;
			const char *r = re__here (x, p + 2, t);
			if (r == 0) { x->ngroups--; x->depth--; x->m->sp[g] = os; x->m->ep[g] = oe; }
			return r;
		}
		if (p[0] == '\\' && p[1] == ')')
		{
			if (x->depth == 0) return 0;
			int g = x->open[--x->depth];
			const char *oe = x->m->ep[g];
			x->m->ep[g] = t;
			const char *r = re__here (x, p + 2, t);
			if (r == 0) { x->m->ep[g] = oe; x->open[x->depth++] = g; }
			return r;
		}
		if (p[0] == '\\' && p[1] >= '1' && p[1] <= '9')		// a back-reference
		{
			int g = p[1] - '0';
			if (x->m->sp[g] == 0 || x->m->ep[g] == 0) return 0;
			for (const char *s = x->m->sp[g]; s < x->m->ep[g]; s++, t++)
				if (t >= x->end || (x->icase ? re__fold (*s) != re__fold (*t) : *s != *t)) return 0;
			p += 2;
			continue;
		}
		if (p[0] == '$' && (p[1] == '\0' || (p[1] == '\\' && p[2] == ')')))
		{
			if (t != x->end) return 0;
			p++;
			continue;
		}
		int alen = re__atomlen (p);
		const char *q = p + alen;
		int min = 1, max = 1, qlen = 0;
		if (*q == '*') { min = 0; max = -1; qlen = 1; }
		else if (q[0] == '\\' && q[1] == '+') { min = 1; max = -1; qlen = 2; }
		else if (q[0] == '\\' && q[1] == '?') { min = 0; max = 1; qlen = 2; }
		if (qlen == 0)
		{
			if (t >= x->end || !re__atom (x, p, alen, *t)) return 0;
			p = q; t++;
			continue;
		}
		int n = 0;
		while (t + n < x->end && (max < 0 || n < max) && re__atom (x, p, alen, t[n])) n++;
		for (; n >= min; n--)				// the longest first
		{
			const char *r = re__here (x, q + qlen, t + n);
			if (r) return r;
		}
		return 0;
	}
}

// The first match of pat in the text [text, end), looked for from `from` on (text: where the
// line starts, for ^). -> 1 and m (m->sp[0] .. m->ep[0]: the match; the groups), or 0.
static inline int re_search (const char *pat, const char *text, const char *end, const char *from,
			     int icase, struct re_match *m)
{
	struct re__ctx x;
	x.bol = text; x.end = end; x.icase = icase; x.m = m;
	int anchored = pat[0] == '^';
	if (anchored) pat++;
	for (const char *t = from; t <= end; t++)
	{
		if (anchored && t != text) break;
		for (int g = 0; g < RE_GROUPS; g++) m->sp[g] = m->ep[g] = 0;
		x.ngroups = 0; x.depth = 0;
		const char *r = re__here (&x, pat, t);
		if (r) { m->sp[0] = t; m->ep[0] = r; return 1; }
	}
	return 0;
}

#endif
