//
// subst.h -- what sed and ed share: a growing string, a /delimited/ piece read from a command,
// and the substitution s/re/replacement/ on one line (regex.h). Included after tool.h.
//
// The replacement: & is the whole match, \1 .. \9 the groups, \n a line feed, \t a tab, and
// \& \\ \/ those characters themselves.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see tool.h).
//
#ifndef _bin_subst_h
#define _bin_subst_h

#include "regex.h"

struct t_sb { char *p; long n, cap; };

static inline void sb_put (struct t_sb *b, const char *s, long n)
{
	if (b->n + n + 1 > b->cap)
	{
		b->cap = (b->n + n + 1) * 2 + 64;
		b->p = (char *) t_realloc (b->p, b->cap);
	}
	t_memcpy (b->p + b->n, s, n);
	b->n += n; b->p[b->n] = '\0';
}
static inline void sb_putc (struct t_sb *b, char c) { sb_put (b, &c, 1); }
static inline void sb_reset (struct t_sb *b) { b->n = 0; if (b->p) b->p[0] = '\0'; else sb_put (b, "", 0); }

// The piece of *pp up to the delimiter d (a \d does not end it; kept as written) -> a copy (to
// free); *pp after the delimiter, or at the end of the text when there is none (*closed = 0).
static inline char *re_piece (const char **pp, char d, int *closed)
{
	const char *s = *pp, *q = s;
	while (*q && *q != d && *q != '\n')
	{
		if (*q == '\\' && q[1]) q++;
		q++;
	}
	char *r = t_strndup (s, q - s);
	if (closed) *closed = *q == d;
	*pp = *q == d ? q + 1 : q;
	return r;
}

// s/re/repl/ on the line s (len bytes), the result added to out. g: every match, else only the
// nth one (1: the first). -> how many replacements were made.
static inline int re_subst (const char *re, const char *repl, int icase, int g, int nth,
			    const char *s, long len, struct t_sb *out)
{
	const char *end = s + len, *pos = s, *prev_end = 0;
	struct re_match m;
	int count = 0, made = 0;
	if (nth < 1) nth = 1;
	while (pos <= end && re_search (re, s, end, pos, icase, &m))
	{
		int empty = m.sp[0] == m.ep[0];
		if (empty && m.sp[0] == prev_end)		// (an empty match right after a match: not one)
		{
			if (m.sp[0] >= end) break;
			sb_put (out, pos, m.sp[0] + 1 - pos); pos = m.sp[0] + 1;
			continue;
		}
		count++;
		sb_put (out, pos, m.sp[0] - pos);
		if (count >= nth && (g || count == nth))
		{
			made++;
			for (const char *r = repl; *r; r++)
			{
				if (*r == '&') sb_put (out, m.sp[0], m.ep[0] - m.sp[0]);
				else if (*r == '\\' && r[1])
				{
					r++;
					if (*r >= '1' && *r <= '9')
					{
						int k = *r - '0';
						if (m.sp[k] && m.ep[k]) sb_put (out, m.sp[k], m.ep[k] - m.sp[k]);
					}
					else sb_putc (out, *r == 'n' ? '\n' : *r == 't' ? '\t' : *r);
				}
				else sb_putc (out, *r);
			}
		}
		else sb_put (out, m.sp[0], m.ep[0] - m.sp[0]);
		pos = prev_end = m.ep[0];
		if (empty)
		{
			if (pos >= end) break;
			sb_putc (out, *pos++);
		}
		if (!g && count >= nth) break;
	}
	if (pos < end) sb_put (out, pos, end - pos);
	return made;
}

#endif
