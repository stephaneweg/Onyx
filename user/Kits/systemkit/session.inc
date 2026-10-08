//
// session.inc -- the code of session.h (SystemKit: compiled into systemkit.so; a PC build, or a program that defines
// SK_INLINE -- pkg, which splits the autostart at boot without the library --, has it inline).
// C as well as C++ (/bin/session includes it on the PC): no bool, no references.
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

#define SS_AUTOSTART	"SD:/etc/autostart"
#define SS_MAX		16384			// a bigger autostart is left alone (never cut)
#define SS_HELD		"#setup:"		// Setup's held-back lines: "#setup: <line>"

static inline const char *ss_name_ (int m)
{
	return m == SESSION_DESKTOP ? "desktop" : m == SESSION_POCKET ? "pocket" : m == SESSION_CONSOLE ? "console" : "";
}
static inline int ss_blank_ (char c) { return c == ' ' || c == '\t' || c == '\r'; }
static inline int ss_len_ (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline void ss_put_ (char *o, int *k, int cap, const char *s, int n)
{
	for (int i = 0; i < n; i++) { if (*k < cap) o[*k] = s[i]; (*k)++; }	// (*k past cap: the caller sees it did not fit)
}
// A file into buf (cap - 1 bytes at most) -> its length, 0 no file (or empty), -1 bigger than cap - 1.
static inline int ss_read_ (const char *path, char *buf, int cap)
{
	void *f = kapi_open (path);
	if (f == 0) { buf[0] = 0; return 0; }
	unsigned size = kapi_fsize (f);
	int n = 0;
	if (size < (unsigned) cap)
		while (n < (int) size) { int k = kapi_read (f, buf + n, size - (unsigned) n); if (k <= 0) break; n += k; }
	kapi_close (f);
	if (size >= (unsigned) cap) { buf[0] = 0; return -1; }
	buf[n] = 0;
	return n;
}
// The command of the line [s, e): its start (blanks and Setup's "#setup:" skipped; *held 1 for the latter), -1 a
// comment or a blank line.
static inline int ss_command_ (const char *b, int s, int e, int *held)
{
	*held = 0;
	while (s < e && ss_blank_ (b[s])) s++;
	if (s < e && b[s] == '#')
	{
		int k = 0;
		while (SS_HELD[k] && s + k < e && b[s + k] == SS_HELD[k]) k++;
		if (SS_HELD[k]) return -1;
		s += k; *held = 1;
		while (s < e && ss_blank_ (b[s])) s++;
	}
	return s < e ? s : -1;
}
// The word at s (up to a blank or e) is w? -> its end, -1 not.
static inline int ss_word_ (const char *b, int s, int e, const char *w)
{
	int k = 0;
	for (; w[k]; k++) if (s + k >= e || b[s + k] != w[k]) return -1;
	if (s + k < e && !ss_blank_ (b[s + k])) return -1;
	return s + k;
}
static inline int ss_skip_ (const char *b, int s, int e) { while (s < e && ss_blank_ (b[s])) s++; return s; }
static inline int ss_word_end_ (const char *b, int s, int e) { while (s < e && !ss_blank_ (b[s])) s++; return s; }

// The program a command [c, e) starts, as its process is named: `run <app> ...` -> the app (a program file's path:
// its file's name without its extension), `wait <command>` -> that command's, `sleep` -> none, else the /bin tool
// -> 1 (name in out), 0 none.
static inline int ss_program_ (const char *b, int c, int e, char *out, int cap)
{
	int w = ss_word_end_ (b, c, e);
	if (ss_word_ (b, c, e, "sleep") >= 0) return 0;
	if (ss_word_ (b, c, e, "wait") >= 0) { c = ss_skip_ (b, w, e); w = ss_word_end_ (b, c, e); if (c >= e) return 0; }
	if (ss_word_ (b, c, e, "run") >= 0) { c = ss_skip_ (b, w, e); w = ss_word_end_ (b, c, e); if (c >= e) return 0; }
	int s = c;
	for (int i = c; i < w; i++) if (b[i] == '/' || b[i] == ':') s = i + 1;	// (a path: its file's name)
	int t = w;
	if (s != c) for (int i = s; i < w; i++) if (b[i] == '.') { t = i; break; }	// (... without its extension)
	if (t <= s || cap < 2) return 0;
	int o = 0;
	for (int i = s; i < t && o < cap - 1; i++) out[o++] = b[i];
	out[o] = 0;
	return 1;
}

SK_FN int session_modes (void) { return 3; }
SK_FN const char *session_mode_name (int m) { return ss_name_ (m); }
SK_FN int session_mode_find (const char *name)
{
	if (name == 0) return -1;
	while (*name == ' ' || *name == '\t') name++;
	for (int m = 0; m < 3; m++)
	{
		const char *w = ss_name_ (m);
		int k = 0;
		while (w[k] && (name[k] == w[k] || name[k] == w[k] - 'a' + 'A')) k++;
		if (!w[k] && (name[k] == 0 || name[k] == ' ' || name[k] == '\t' || name[k] == '\r' || name[k] == '\n')) return m;
	}
	return -1;
}
SK_FN int session_mode (void)
{
	char v[32];
	if (!locale_ini_get ("shell", v, sizeof v)) return SESSION_DESKTOP;
	int m = session_mode_find (v);
	return m < 0 ? SESSION_DESKTOP : m;
}
SK_FN int session_set_mode (int m)
{
	if (m < 0 || m > 2) return 0;
	return locale_ini_set ("shell", ss_name_ (m)) ? 1 : 0;
}
SK_FN int session_file (int m, char *out, int cap)
{
	if (m < 0 || m > 2 || out == 0 || cap <= 0) return 0;
	int k = 0;
	ss_put_ (out, &k, cap, SESSION_DIR "/", ss_len_ (SESSION_DIR) + 1);
	ss_put_ (out, &k, cap, ss_name_ (m), ss_len_ (ss_name_ (m)));
	if (k >= cap) { out[0] = 0; return 0; }
	out[k] = 0;
	return k;
}
// The programs of a session file's text [0, n), one a line into out -> how many.
static inline int ss_programs_ (const char *in, int n, char *out, int cap)
{
	int count = 0, o = 0;
	if (cap > 0) out[0] = 0;
	for (int i = 0; i < n; )
	{
		int s = i, held;
		while (i < n && in[i] != '\n') i++;
		int e = i;
		if (i < n) i++;
		int c = ss_command_ (in, s, e, &held);
		char name[64];
		if (c < 0 || !ss_program_ (in, c, e, name, sizeof name)) continue;
		int l = ss_len_ (name);
		if (o + l + 2 > cap) break;
		ss_put_ (out, &o, cap, name, l);
		ss_put_ (out, &o, cap, "\n", 1);
		out[o] = 0;
		count++;
	}
	return count;
}
SK_FN int session_programs (int m, char *out, int cap)
{
	static char in[SS_MAX + 1];
	char path[64];
	if (out == 0 || cap <= 0) return 0;
	out[0] = 0;
	if (!session_file (m, path, sizeof path)) return 0;
	int n = ss_read_ (path, in, sizeof in);
	return n > 0 ? ss_programs_ (in, n, out, cap) : 0;
}
SK_FN void *session_switch_start (int m, int flags, int keep_pid)
{
	char args[96];
	int k = 0;
	if (m < 0 || m > 2) return 0;
	ss_put_ (args, &k, (int) sizeof args, "switch ", 7);
	ss_put_ (args, &k, (int) sizeof args, ss_name_ (m), ss_len_ (ss_name_ (m)));
	if (flags & SESSION_FORCE) ss_put_ (args, &k, (int) sizeof args, " --force", 8);
	if (flags & SESSION_NO_ASK) ss_put_ (args, &k, (int) sizeof args, " --no-ask", 9);
	if (keep_pid > 0)
	{
		char d[16]; int nd = 0, v = keep_pid;
		while (v > 0 && nd < 15) { d[nd++] = (char) ('0' + v % 10); v /= 10; }
		ss_put_ (args, &k, (int) sizeof args, " --keep ", 8);
		while (nd > 0) { char c = d[--nd]; ss_put_ (args, &k, (int) sizeof args, &c, 1); }
	}
	if (k >= (int) sizeof args) return 0;
	args[k] = 0;
	return kapi_spawn (SESSION_TOOL, args, 0, 0);
}
SK_FN int session_switch (int m, int flags)
{
	void *h = session_switch_start (m, flags, 0);
	if (h == 0) return m < 0 || m > 2 ? SESSION_BAD : SESSION_FAILED;
	return kapi_wait (h);
}
SK_FN int session_waiting (char *out, int cap)
{
	if (out == 0 || cap <= 0) return 0;
	int n = ss_read_ (SESSION_WAITING, out, cap), count = 0;
	if (n <= 0) { out[0] = 0; return 0; }
	for (int i = 0; i < n; i++) if (out[i] == '\n') count++;
	return out[n - 1] == '\n' ? count : count + 1;
}

// ---- the autostart split (session_migrate) ----
// The desktop's programs: their lines move into the desktop's session file.
static inline int ss_desktop_line_ (const char *b, int s, int e)
{
	static const char *const APPS[] = { "voronoy", "setup", "menubar", "notifyd", "dock", "agenda", "stickies", "wifimenu", "imageview", 0 };
	int held, c = ss_command_ (b, s, e, &held);
	if (c < 0) return 0;
	int w = ss_word_ (b, c, e, "run");
	if (w < 0) return 0;
	c = ss_skip_ (b, w, e);
	for (int k = 0; APPS[k]; k++) if (ss_word_ (b, c, e, APPS[k]) >= 0) return 1;
	return 0;
}
// Setup's comment lines ("#setup# ..."): Setup's own, with its line.
static inline int ss_setup_comment_ (const char *b, int s, int e)
{
	s = ss_skip_ (b, s, e);
	return e - s >= 7 && b[s] == '#' && b[s + 1] == 's' && b[s + 2] == 'e' && b[s + 3] == 't' && b[s + 4] == 'u' && b[s + 5] == 'p' && b[s + 6] == '#';
}
// A plain comment line ("# ...", not Setup's held-back or comment lines).
static inline int ss_comment_ (const char *b, int s, int e)
{
	int held;
	s = ss_skip_ (b, s, e);
	if (s >= e || b[s] != '#' || ss_setup_comment_ (b, s, e)) return 0;
	return ss_command_ (b, s, e, &held) < 0;
}
// The line `session` (with arguments, held back by Setup, or turned off as "#session": the split was done).
static inline int ss_session_line_ (const char *b, int s, int e)
{
	int held, c = ss_command_ (b, s, e, &held);
	if (c < 0)
	{
		s = ss_skip_ (b, s, e);
		if (s + 1 >= e || b[s] != '#' || b[s + 1] != 's') return 0;
		c = s + 1;
	}
	return ss_word_ (b, c, e, "session") >= 0;
}
// A `preload` line: the session's line goes before it when no desktop line is found.
static inline int ss_preload_line_ (const char *b, int s, int e)
{
	int held, c = ss_command_ (b, s, e, &held);
	return c >= 0 && ss_word_ (b, c, e, "preload") >= 0;
}

#define SS_SESSION_BLOCK \
	"# The session: the programs of the interface's mode (SD:/etc/system.ini \"shell =\": desktop, pocket or\n" \
	"# console) are in SD:/etc/session/<mode>, which /bin/session runs (and switches: the Control Panel's Mode).\n" \
	"session\n"
#define SS_DESKTOP_HEAD \
	"# SD:/etc/session/desktop -- the desktop session's programs: run by /bin/session (the autostart's line\n" \
	"# `session`) when the mode is the desktop (SD:/etc/system.ini \"shell =\" desktop, or no line), ended when it\n" \
	"# is switched to another. The same syntax as SD:/etc/autostart. (Moved here from the autostart, once.)\n"

// The autostart's text [0, n) split: the system's part into sys (*ns bytes), the desktop's file into desk (*nd)
// -> 1 split, 0 it has a `session` line already, -1 it does not fit.
static inline int ss_split_ (const char *in, int n, char *sys, int scap, int *ns, char *desk, int dcap, int *nd)
{
	int anyDesktop = 0, o = 0, d = 0, done = 0, ps = -1;	// ps: the comment block waiting (its start), -1 none
	*ns = *nd = 0;
	for (int i = 0; i < n; )
	{
		int s = i;
		while (i < n && in[i] != '\n') i++;
		int e = i;
		if (i < n) i++;
		if (ss_session_line_ (in, s, e)) return 0;
		if (ss_desktop_line_ (in, s, e) || ss_setup_comment_ (in, s, e)) anyDesktop = 1;
	}
	ss_put_ (desk, &d, dcap, SS_DESKTOP_HEAD, ss_len_ (SS_DESKTOP_HEAD));
	for (int i = 0; i < n; )
	{
		int s = i;
		while (i < n && in[i] != '\n') i++;
		int e = i;
		if (i < n) i++;
		int nl = i > e ? 0 : 1;				// (the last line without its '\n': one added)
		if (ss_comment_ (in, s, e)) { if (ps < 0) ps = s; continue; }
		if (ss_desktop_line_ (in, s, e) || ss_setup_comment_ (in, s, e))
		{
			if (!done) { ss_put_ (sys, &o, scap, SS_SESSION_BLOCK, ss_len_ (SS_SESSION_BLOCK)); done = 1; }
			if (ps >= 0) { ss_put_ (desk, &d, dcap, in + ps, s - ps); ps = -1; }
			ss_put_ (desk, &d, dcap, in + s, i - s);
			if (nl) ss_put_ (desk, &d, dcap, "\n", 1);
			continue;
		}
		if (!done && !anyDesktop && ss_preload_line_ (in, s, e))
		{
			ss_put_ (sys, &o, scap, SS_SESSION_BLOCK, ss_len_ (SS_SESSION_BLOCK)); done = 1;
		}
		if (ps >= 0) { ss_put_ (sys, &o, scap, in + ps, s - ps); ps = -1; }
		ss_put_ (sys, &o, scap, in + s, i - s);
		if (nl) ss_put_ (sys, &o, scap, "\n", 1);
	}
	if (ps >= 0) ss_put_ (sys, &o, scap, in + ps, n - ps);
	if (o > 0 && o <= scap && sys[o - 1] != '\n') ss_put_ (sys, &o, scap, "\n", 1);
	if (!done) ss_put_ (sys, &o, scap, SS_SESSION_BLOCK, ss_len_ (SS_SESSION_BLOCK));
	if (o >= scap || d >= dcap) return -1;
	sys[o] = 0; desk[d] = 0;
	*ns = o; *nd = d;
	return 1;
}

SK_FN int session_migrate (void)
{
	static char in[SS_MAX + 1], sys[SS_MAX + 1024], desk[SS_MAX + 512];
	int n = ss_read_ (SS_AUTOSTART, in, sizeof in), ns = 0, nd = 0;
	if (n == 0) return 0;					// (no autostart: nothing to split)
	if (n < 0) return -1;
	int r = ss_split_ (in, n, sys, (int) sizeof sys, &ns, desk, (int) sizeof desk, &nd);
	if (r <= 0) return r;
	kapi_mkdir (SESSION_DIR);				// (there already on a card with the new files)
	// The desktop's file first: if the autostart cannot be written after it, the next try makes it anew.
	if (kapi_save_file (SESSION_DIR "/desktop", desk, (unsigned) nd) != nd) return -1;
	if (kapi_save_file (SS_AUTOSTART ".old", in, (unsigned) n) != n) return -1;
	if (kapi_save_file (SS_AUTOSTART, sys, (unsigned) ns) != ns) return -1;
	return 1;
}

#undef SS_SESSION_BLOCK
#undef SS_DESKTOP_HEAD
#undef SS_HELD
#undef SK_FN
