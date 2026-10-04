//
// date -- print the date and the time (the system clock, local time).
//   usage: date [+FORMAT]
// Without a format: 2026-10-04 21:47:03 . In FORMAT: %Y year, %m month, %d day, %H hour,
// %M minute, %S second, %y the year's last two digits, %F = %Y-%m-%d, %T = %H:%M:%S,
// %n a line feed, %% a %; the rest is printed as is.   date +%H:%M    date "+%d/%m/%Y"
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see tool.h).
//
#define TOOL_NAME "date"
#include "tool.h"

static void two (int v) { t_putc ((char) ('0' + v / 10 % 10)); t_putc ((char) ('0' + v % 10)); }

int tool_main (int argc, char **argv)
{
	const char *fmt = "%F %T";
	if (argc > 1)
	{
		if (argv[1][0] != '+') { t_puts ("usage: date [+FORMAT]\n"); return 2; }
		fmt = argv[1] + 1;
	}
	int v[6];
	t_now (v);
	for (const char *p = fmt; *p; p++)
	{
		if (*p != '%' || p[1] == '\0') { t_putc (*p); continue; }
		switch (*++p)
		{
		case 'Y': t_putnum (v[0]); break;
		case 'y': two (v[0] % 100); break;
		case 'm': two (v[1]); break;
		case 'd': two (v[2]); break;
		case 'H': two (v[3]); break;
		case 'M': two (v[4]); break;
		case 'S': two (v[5]); break;
		case 'F': t_putnum (v[0]); t_putc ('-'); two (v[1]); t_putc ('-'); two (v[2]); break;
		case 'T': two (v[3]); t_putc (':'); two (v[4]); t_putc (':'); two (v[5]); break;
		case 'n': t_putc ('\n'); break;
		case '%': t_putc ('%'); break;
		default:  t_putc ('%'); t_putc (*p); break;
		}
	}
	t_putc ('\n');
	return 0;
}
