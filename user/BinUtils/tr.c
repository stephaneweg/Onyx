//
// tr -- translate or delete characters of stdin.
//   usage: tr <set1> <set2>      each character of set1 replaced by its match in set2
//          tr -d <set1>          the characters of set1 deleted
//          tr -s <set1> [set2]   (and) runs of the same character squeezed into one
// A set: characters and ranges (a-z A-Z 0-9), \n \t \r \\ ; set2 shorter than set1 is
// continued with its last character.   tr a-z A-Z    tr -d '\r'    tr -s ' '
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see tool.h).
//
#define TOOL_NAME "tr"
#include "tool.h"

static int expand (const char *s, unsigned char *out)
{
	int n = 0;
	while (*s && n < 256)
	{
		int c = (unsigned char) *s++;
		if (c == '\\' && *s)
		{
			c = *s == 'n' ? '\n' : *s == 't' ? '\t' : *s == 'r' ? '\r' : (unsigned char) *s;
			s++;
		}
		if (s[0] == '-' && s[1] && s[1] != '\\' && (unsigned char) s[1] >= c)
		{
			for (int k = c; k <= (unsigned char) s[1] && n < 256; k++) out[n++] = (unsigned char) k;
			s += 2;
		}
		else out[n++] = (unsigned char) c;
	}
	return n;
}

int tool_main (int argc, char **argv)
{
	int del = 0, squeeze = 0, a = 1;
	for (; a < argc && argv[a][0] == '-' && argv[a][1] && argv[a][2] == '\0' && (argv[a][1] == 'd' || argv[a][1] == 's'); a++)
		if (argv[a][1] == 'd') del = 1; else squeeze = 1;
	int nsets = argc - a;
	if (nsets < 1 || nsets > 2 || (nsets == 1 && !del && !squeeze))
	{
		t_puts ("usage: tr <set1> <set2>\n       tr -d <set1>\n       tr -s <set1> [set2]\n");
		return 2;
	}
	static unsigned char s1[256], s2[256];
	int n1 = expand (argv[a], s1), n2 = nsets == 2 ? expand (argv[a + 1], s2) : 0;
	int map[256], drop[256], sq[256];
	for (int i = 0; i < 256; i++) { map[i] = i; drop[i] = sq[i] = 0; }
	if (del) for (int i = 0; i < n1; i++) drop[s1[i]] = 1;
	else if (n2 > 0) for (int i = 0; i < n1; i++) map[s1[i]] = s2[i < n2 ? i : n2 - 1];
	if (squeeze)
	{
		if (n2 > 0) for (int i = 0; i < n2; i++) sq[s2[i]] = 1;
		else for (int i = 0; i < n1; i++) sq[s1[i]] = 1;
	}
	struct t_file *f = t_open (0);
	int c, last = -1;
	while ((c = t_getc (f)) >= 0)
	{
		if (drop[c]) continue;
		c = map[c];
		if (sq[c] && c == last) continue;
		t_putc ((char) c); last = c;
		if (c == '\n') t_flush ();
	}
	t_close (f);
	return 0;
}
