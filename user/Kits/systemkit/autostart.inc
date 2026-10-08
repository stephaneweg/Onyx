//
// autostart.inc -- the code of autostart.h (SystemKit: compiled into systemkit.so; a PC build has it inline).
// C as well as C++ (a C program includes it on the PC): no bool, no references.
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
#ifdef SK_IMPL
#define SK_FN	extern "C"
#else
#define SK_FN	static inline
#endif

#define AS_HELD		"#setup:"		// (Setup's held-back lines: "#setup: <line>")

static inline int as_blank_ (char c) { return c == ' ' || c == '\t' || c == '\r'; }
// The files looked in: the autostart (the system's part), then the sessions' (SD:/etc/session/<mode>).
static inline const char *as_file_ (int k)
{
	static const char *const F[] = { AUTOSTART_PATH, AUTOSTART_SESSIONS "desktop", AUTOSTART_SESSIONS "pocket",
					 AUTOSTART_SESSIONS "console", 0 };
	return k >= 0 && k < 4 ? F[k] : 0;
}
// The file into buf (cap - 1 bytes at most) -> its length, 0 no file, -1 bigger than cap - 1.
static inline int as_read_ (const char *path, char *buf, int cap)
{
	void *f = kapi_open (path);
	if (f == 0) return 0;
	unsigned size = kapi_fsize (f);
	int n = 0;
	if (size < (unsigned) cap)
		while (n < (int) size) { int k = kapi_read (f, buf + n, size - (unsigned) n); if (k <= 0) break; n += k; }
	kapi_close (f);
	if (size >= (unsigned) cap) return -1;
	buf[n] = '\0';
	return n;
}
// The command of the line [s, e): its start (blanks and Setup's "#setup:" skipped; *held 1 for the latter),
// -1 a comment or a blank line.
static inline int as_command_ (const char *b, int s, int e, int *held)
{
	*held = 0;
	while (s < e && as_blank_ (b[s])) s++;
	if (s < e && b[s] == '#')
	{
		int k = 0;
		while (AS_HELD[k] && s + k < e && b[s + k] == AS_HELD[k]) k++;
		if (AS_HELD[k]) return -1;				// (a comment; "#setup#" too)
		s += k; *held = 1;
		while (s < e && as_blank_ (b[s])) s++;
	}
	return s < e ? s : -1;
}
// Do the words of [s, e) begin with the words of w? (blanks between words: any number)
static inline int as_starts_ (const char *b, int s, int e, const char *w)
{
	int i = 0;
	while (w[i] == ' ') i++;
	if (!w[i]) return 0;
	while (w[i])
	{
		while (w[i] && w[i] != ' ' && s < e && b[s] == w[i]) { s++; i++; }
		if (w[i] && w[i] != ' ') return 0;			// (the word differs)
		if (s < e && !as_blank_ (b[s])) return 0;		// (the line's word goes on)
		while (w[i] == ' ') i++;
		while (s < e && as_blank_ (b[s])) s++;
	}
	return 1;
}
// The first line whose command starts with the words w: its end (past its '\n'), *held its Setup's mark;
// -1 none. before = 1: its start instead.
static inline int as_find_ (const char *b, int n, const char *w, int before, int *held)
{
	for (int i = 0; i < n; )
	{
		int s = i;
		while (i < n && b[i] != '\n') i++;
		int e = i;
		if (i < n) i++;
		int c = as_command_ (b, s, e, held);
		if (c >= 0 && as_starts_ (b, c, e, w)) return before ? s : i;
	}
	*held = 0;
	return -1;
}
static inline void as_put_ (char *o, int *k, int cap, const char *s, int n)
{
	for (int i = 0; i < n && *k < cap; i++) o[(*k)++] = s[i];
}
static inline int as_len_ (const char *s) { int n = 0; while (s && s[n]) n++; return n; }

SK_FN int autostart_has (const char *cmd)
{
	static char in[AUTOSTART_MAX + 1];
	if (cmd == 0) return 0;
	for (int k = 0; as_file_ (k); k++)
	{
		int n = as_read_ (as_file_ (k), in, sizeof in), held;
		if (n > 0 && as_find_ (in, n, cmd, 0, &held) >= 0) return 1;
	}
	return 0;
}

SK_FN int autostart_ensure (const char *cmd, const char *after, const char *comment)
{
	static char in[AUTOSTART_MAX + 1], out[AUTOSTART_MAX + 1024];
	if (cmd == 0 || !*cmd) return 0;
	if (autostart_has (cmd)) return 1;
	// the file whose line `after` is (the autostart first, then the sessions'); else the autostart
	int n = -1, held = 0, at = -1;
	const char *path = AUTOSTART_PATH;
	for (int k = 0; after && *after && as_file_ (k) && at < 0; k++)
	{
		n = as_read_ (as_file_ (k), in, sizeof in);
		if (n > 0) at = as_find_ (in, n, after, 0, &held);
		if (at >= 0) path = as_file_ (k);
	}
	if (at < 0)
	{
		n = as_read_ (AUTOSTART_PATH, in, sizeof in);
		if (n < 0) return 0;					// (too big: left alone)
		at = as_find_ (in, n, "preload", 1, &held); held = 0;
		if (at < 0) at = n;
	}
	int o = 0, cap = (int) sizeof out;
	as_put_ (out, &o, cap, in, at);
	if (o > 0 && out[o - 1] != '\n') as_put_ (out, &o, cap, "\n", 1);	// (a last line without its '\n')
	if (comment && *comment) { as_put_ (out, &o, cap, comment, as_len_ (comment)); as_put_ (out, &o, cap, "\n", 1); }
	if (held) as_put_ (out, &o, cap, AS_HELD " ", 8);
	as_put_ (out, &o, cap, cmd, as_len_ (cmd));
	as_put_ (out, &o, cap, "\n", 1);
	as_put_ (out, &o, cap, in + at, n - at);
	if (o >= cap) return 0;
	return kapi_save_file (path, out, (unsigned) o) == o ? 2 : 0;
}

#undef AS_HELD
#undef SK_FN
