//
// cmdparse.h -- cmd's command-line parser: a line split into pipeline stages, each an argv
// block plus its redirections. No kapi: the host test (tools/tests/cmd) includes it as is.
//
// The rules (docs/04 "Terminal & shell"):
//   - blanks separate words; | separates stages; < file, > file, >> file redirect a stage;
//   - "..." and '...' make one word of what they hold: blanks, < > | and the other quote are
//     plain characters there; quotes inside a word join it ("a"b'c' is abc);
//   - a backslash escapes a literal: outside quotes \" \' \\ \< \> \| \; \& \# and \<blank>; inside
//     double quotes \" and \\ only (any other backslash stays, so "\n" reaches the program as
//     \n); nothing in single quotes (POSIX: '...' cannot hold a ');
//   - an empty word ("" or '') is dropped: the argv block ("a\0b\0\0") cannot carry one.
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
#ifndef _bin_cmdparse_h
#define _bin_cmdparse_h

#define CMD_LINE	2048		// a command line, at most (the terminal's and telnetd's too)
#define CMD_MAXSTAGES	6
#define CMD_ARGV	CMD_LINE	// a stage's argv block: the whole line fits in one stage
#define CMD_PATH	256		// a redirection's file

struct CmdStage
{
	int  argc;			// argv[0] = the command word
	int  len;			// the block's bytes, its final '\0' aside
	char argv[CMD_ARGV];		// "cmd\0arg1\0...\0\0"
	char infile[CMD_PATH], outfile[CMD_PATH];
	int  append;			// >>
};

static inline int cmd_blank (char c) { return c == ' ' || c == '\t'; }

// argument i of a stage (0 = the command), or 0
static inline const char *cmd_arg (const struct CmdStage *st, int i)
{
	if (i < 0 || i >= st->argc) return 0;
	const char *p = st->argv;
	while (i-- > 0) { while (*p) p++; p++; }
	return p;
}

// The line -> its stages (0: an empty line), or -1 with *err set.
static inline int cmd_parse (const char *s, struct CmdStage *stg, int max, const char **err)
{
	int ns = 0, i = 0;
	struct CmdStage *st = 0;
	char word[CMD_ARGV];
	int redir = 0;			// the next word is: 1 the input file, 2 the output file
	*err = 0;
	for (;;)
	{
		while (cmd_blank (s[i])) i++;
		char c = s[i];
		if (c == '\0' || c == '|')
		{
			if (redir) { *err = "syntax error: a file name after < or >"; return -1; }
			if (c == '|' && (st == 0 || st->argc == 0)) { *err = "syntax error near |"; return -1; }
			if (c == '\0')
			{
				if (st != 0 && st->argc == 0) { *err = "syntax error: no command"; return -1; }
				return ns;
			}
			i++;
			while (cmd_blank (s[i])) i++;
			if (s[i] == '\0' || s[i] == '|') { *err = "syntax error near |"; return -1; }
			st = 0;				// (the next stage starts at its first word)
			continue;
		}
		if (st == 0)
		{
			if (ns >= max) { *err = "too many stages (6 at most)"; return -1; }
			st = &stg[ns++];
			st->argc = st->len = st->append = 0;
			st->argv[0] = st->argv[1] = '\0';
			st->infile[0] = st->outfile[0] = '\0';
		}
		if (c == '<' || c == '>')
		{
			if (redir) { *err = "syntax error: a file name after < or >"; return -1; }
			i++;
			if (c == '>') { st->append = 0; if (s[i] == '>') { st->append = 1; i++; } }
			redir = c == '<' ? 1 : 2;
			continue;
		}

		// a word: up to a blank, | < > or the end, outside quotes
		int w = 0;
		char q = 0;
		for (;;)
		{
			c = s[i];
			if (c == '\0')
			{
				if (q) { *err = q == '"' ? "unterminated \" quote" : "unterminated ' quote"; return -1; }
				break;
			}
			if (!q && (cmd_blank (c) || c == '|' || c == '<' || c == '>')) break;
			i++;
			if (!q && (c == '"' || c == '\'')) { q = c; continue; }
			if (q && c == q) { q = 0; continue; }
			if (c == '\\' && q != '\'')
			{
				char n = s[i];
				int esc = q == '"' ? (n == '"' || n == '\\')
						   : (n == '"' || n == '\'' || n == '\\' || n == '<' || n == '>' || n == '|'
						      || n == ';' || n == '&' || n == '#' || cmd_blank (n));
				if (esc) { c = n; i++; }
			}
			if (w >= (int) sizeof word - 1) { *err = "line too long"; return -1; }
			word[w++] = c;
		}
		word[w] = '\0';
		if (redir)
		{
			if (w == 0) { *err = "syntax error: a file name after < or >"; return -1; }
			if (w >= CMD_PATH) { *err = "file name too long"; return -1; }
			char *d = redir == 1 ? st->infile : st->outfile;
			for (int k = 0; k <= w; k++) d[k] = word[k];
			redir = 0;
			continue;
		}
		if (w == 0) continue;				// ("" or '': dropped)
		if (st->len + w + 2 > CMD_ARGV) { *err = "line too long"; return -1; }
		for (int k = 0; k <= w; k++) st->argv[st->len + k] = word[k];
		st->len += w + 1;
		st->argv[st->len] = '\0';
		st->argc++;
	}
}

// ---- command lists and script arguments (cmd's scripts; docs/04 "Scripts") ------------------
//
//   - a line is a list of commands separated by ;  &&  || : `a ; b` runs both, `a && b` runs b
//     only if a ended with the exit code 0, `a || b` only if it did not;
//   - a # at the start of a word begins a comment, up to the end of the line;
//   - $1 .. $9 are the script's arguments ($0 its name), $# their count, $* all of them, $? the
//     exit code of the last command; nothing is replaced inside '...', and \$ is a plain $.

// The next command of the list s, from *pos on, copied into out (quotes and escapes kept as
// written) -> 1, and *op = what follows it: 0 the end, ';', '&' (&&) or '|' (||); 0 when the
// line is over. A command longer than cap - 1 is cut (the caller's buffer holds a whole line).
static inline int cmd_next (const char *s, int *pos, char *out, int cap, int *op)
{
	int i = *pos, o = 0, start = 1;		// start: at the start of a word
	char q = 0;
	*op = 0;
	if (s[i] == '\0') return 0;
	for (; s[i]; i++)
	{
		char c = s[i];
		if (q) { if (c == q) q = 0; else if (c == '\\' &&q == '"' && s[i + 1]) { if (o < cap - 1) out[o++] = c; c = s[++i]; } }
		else if (c == '"' || c == '\'') q = c;
		else if (c == '\\' &&s[i + 1]) { if (o < cap - 1) out[o++] = c; c = s[++i]; }
		else if (c == '#' && start) { while (s[i]) i++; break; }
		else if (c == ';') { *op = ';'; i++; break; }
		else if (c == '&' && s[i + 1] == '&') { *op = '&'; i += 2; break; }
		else if (c == '|' && s[i + 1] == '|') { *op = '|'; i += 2; break; }
		start = !q && cmd_blank (c);
		if (o < cap - 1) out[o++] = c;
	}
	out[o] = '\0';
	*pos = i;
	return 1;
}

struct CmdVars
{
	int argc;			// the script's arguments: argv[0] its name, argv[1] = $1 ...
	const char *const *argv;
	int status;			// $?
};

static inline int cmd__put (char *out, int cap, int o, const char *s)
{
	while (*s) { if (o >= cap - 1) return -1; out[o++] = *s++; }
	return o;
}

// The command with its $ variables replaced -> its length, or -1: too long for cap.
static inline int cmd_expand (const char *s, char *out, int cap, const struct CmdVars *v)
{
	int o = 0;
	char q = 0;
	for (int i = 0; s[i]; i++)
	{
		char c = s[i];
		if (q == '\'') { if (c == '\'') q = 0; }
		else if (c == '\\' &&s[i + 1] == '$') { c = '$'; i++; }
		else if (c == '\\' &&s[i + 1]) { if (o >= cap - 1) return -1; out[o++] = c; c = s[++i]; }
		else if (c == '"') q = q ? 0 : '"';
		else if (c == '\'' && !q) q = c;
		else if (c == '$')
		{
			char n = s[i + 1], num[12];
			if (n >= '0' && n <= '9')
			{
				if (n - '0' < v->argc && (o = cmd__put (out, cap, o, v->argv[n - '0'])) < 0) return -1;
				i++; continue;
			}
			if (n == '#' || n == '?')
			{
				int x = n == '#' ? (v->argc > 0 ? v->argc - 1 : 0) : v->status, k = 0, neg = x < 0;
				char t[12]; int tn = 0;
				unsigned u = neg ? 0u - (unsigned) x : (unsigned) x;
				do { t[tn++] = (char) ('0' + u % 10); u /= 10; } while (u);
				if (neg) num[k++] = '-';
				while (tn) num[k++] = t[--tn];
				num[k] = '\0';
				if ((o = cmd__put (out, cap, o, num)) < 0) return -1;
				i++; continue;
			}
			if (n == '*' || n == '@')
			{
				for (int a = 1; a < v->argc; a++)
				{
					if (a > 1 && (o = cmd__put (out, cap, o, " ")) < 0) return -1;
					if ((o = cmd__put (out, cap, o, v->argv[a])) < 0) return -1;
				}
				i++; continue;
			}
		}
		if (o >= cap - 1) return -1;
		out[o++] = c;
	}
	out[o] = '\0';
	return o;
}

#endif
