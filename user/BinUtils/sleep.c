//
// sleep -- wait.
//   usage: sleep <seconds>          (a decimal part is fine: sleep 0.5)
// For scripts: a pause between two commands.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see tool.h).
//
#define TOOL_NAME "sleep"
#include "tool.h"

int tool_main (int argc, char **argv)
{
	const char *s = argc > 1 ? argv[1] : "";
	long sec = 0, ms = 0;
	int ok = t_isdigit (*s) || (*s == '.' && t_isdigit (s[1]));
	while (t_isdigit (*s)) sec = sec * 10 + (*s++ - '0');
	if (*s == '.')
	{
		long scale = 100;
		for (s++; t_isdigit (*s); s++) { ms += (*s - '0') * scale; scale /= 10; }
	}
	if (!ok || *s) { t_puts ("usage: sleep <seconds>\n"); return 2; }
	for (; sec > 0; sec--) t_sleep_ms (1000);
	if (ms) t_sleep_ms ((unsigned) ms);
	return 0;
}
