//
// lineedit.h -- the line being typed in a console, with its cursor and the history of the lines
// sent before. Shared by the terminal (user/Apps/terminal) and the remote shell (telnetd): they
// do the keyboard and the display, this holds the text. No kapi, no libc (C and C++): the PC
// test tools/tests/cmd/lineedit_test.c includes it as is.
//
//   Left / Right, Home / End     move the cursor in the line
//   a character                  inserted at the cursor; Backspace / Delete remove one
//   Up / Down                    the previous / next line of the history (the line being typed
//                                is kept, and comes back below the newest one)
//   Enter                        the line is sent: le_commit adds it to the history
//
// The history is a ring of text (LE_HIST bytes: a hundred ordinary lines); the oldest lines
// leave when it is full. An empty line, or the same line as the one before, is not added.
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
#ifndef _lineedit_h
#define _lineedit_h

#define LE_LINE		2048		// a line, its '\0' included (cmd takes 2047 characters)
#define LE_HIST		4096		// the history's text

struct LineEdit
{
	char buf[LE_LINE];		// the line (always '\0'-terminated)
	int  len, cur;			// its length; the cursor (0 .. len)
	char hist[LE_HIST];		// the lines sent, oldest first, each followed by a '\0'
	int  hlen;			// bytes used there
	int  hpos;			// 0: the line being typed; n: the nth line back in the history
	char saved[LE_LINE];		// the line being typed, while the history is shown
	int  savedlen;
};

static inline void le_init (struct LineEdit *e)
{
	e->len = e->cur = e->hlen = e->hpos = e->savedlen = 0;
	e->buf[0] = e->saved[0] = '\0';
}

// Every call below returns 1 when the line or the cursor changed (the display is to refresh).

static inline int le_insert (struct LineEdit *e, char c)
{
	if (e->len >= LE_LINE - 1) return 0;
	for (int i = e->len; i > e->cur; i--) e->buf[i] = e->buf[i - 1];
	e->buf[e->cur++] = c;
	e->buf[++e->len] = '\0';
	return 1;
}
static inline int le_delete (struct LineEdit *e)		// the character at the cursor
{
	if (e->cur >= e->len) return 0;
	for (int i = e->cur; i < e->len; i++) e->buf[i] = e->buf[i + 1];
	e->len--;
	return 1;
}
static inline int le_backspace (struct LineEdit *e)		// the character before it
{
	if (e->cur == 0) return 0;
	e->cur--;
	return le_delete (e);
}
static inline int le_left (struct LineEdit *e)  { if (e->cur == 0) return 0; e->cur--; return 1; }
static inline int le_right (struct LineEdit *e) { if (e->cur >= e->len) return 0; e->cur++; return 1; }
static inline int le_home (struct LineEdit *e)  { if (e->cur == 0) return 0; e->cur = 0; return 1; }
static inline int le_end (struct LineEdit *e)   { if (e->cur == e->len) return 0; e->cur = e->len; return 1; }
static inline int le_kill (struct LineEdit *e)			// from the cursor to the end
{
	if (e->cur >= e->len) return 0;
	e->len = e->cur; e->buf[e->len] = '\0';
	return 1;
}
static inline int le_clear (struct LineEdit *e)			// the whole line
{
	if (e->len == 0) return 0;
	e->len = e->cur = 0; e->buf[0] = '\0';
	return 1;
}

// how many lines the history holds
static inline int le_count (const struct LineEdit *e)
{
	int n = 0;
	for (int i = 0; i < e->hlen; i++) if (e->hist[i] == '\0') n++;
	return n;
}
// the nth line back (1: the newest), or 0
static inline const char *le_entry (const struct LineEdit *e, int n)
{
	int total = le_count (e);
	if (n < 1 || n > total) return 0;
	int skip = total - n, i = 0;
	while (skip > 0) { while (e->hist[i]) i++; i++; skip--; }
	return &e->hist[i];
}
static inline void le__load (struct LineEdit *e, const char *s, int n)
{
	for (int i = 0; i < n; i++) e->buf[i] = s[i];
	e->buf[n] = '\0'; e->len = e->cur = n;
}
static inline int le_up (struct LineEdit *e)
{
	const char *s = le_entry (e, e->hpos + 1);
	if (s == 0) return 0;
	if (e->hpos == 0)
	{
		for (int i = 0; i <= e->len; i++) e->saved[i] = e->buf[i];
		e->savedlen = e->len;
	}
	e->hpos++;
	int n = 0; while (s[n]) n++;
	le__load (e, s, n);
	return 1;
}
static inline int le_down (struct LineEdit *e)
{
	if (e->hpos == 0) return 0;
	e->hpos--;
	if (e->hpos == 0) { le__load (e, e->saved, e->savedlen); return 1; }
	const char *s = le_entry (e, e->hpos);
	int n = 0; while (s[n]) n++;
	le__load (e, s, n);
	return 1;
}

// The line is sent: added to the history (when `remember`), and the editor is empty again.
// The caller reads e->buf / e->len BEFORE this call.
static inline void le_commit (struct LineEdit *e, int remember)
{
	const char *last = le_entry (e, 1);
	int same = 0;
	if (last) { int i = 0; while (last[i] && last[i] == e->buf[i]) i++; same = last[i] == e->buf[i]; }
	if (remember && e->len > 0 && !same && e->len + 1 <= LE_HIST)
	{
		int drop = 0;					// the oldest lines leave until it fits
		while (e->hlen - drop + e->len + 1 > LE_HIST) { while (e->hist[drop]) drop++; drop++; }
		if (drop) { for (int i = drop; i < e->hlen; i++) e->hist[i - drop] = e->hist[i]; e->hlen -= drop; }
		for (int i = 0; i <= e->len; i++) e->hist[e->hlen + i] = e->buf[i];
		e->hlen += e->len + 1;
	}
	e->len = e->cur = e->hpos = e->savedlen = 0;
	e->buf[0] = e->saved[0] = '\0';
}

#endif
