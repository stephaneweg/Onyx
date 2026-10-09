/*
 * util.c -- the 3DS core's test programs: printing through svcOutputDebugString, and the checks' count. A check
 * that fails prints a line that starts with FAIL; summary () prints "<n> checks, <m> failed".
 *
 * MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
 */
#include "sys.h"

static int s_checks, s_failed;
static const char *s_section = "";

static int len (const char *s) { int n = 0; while (s[n]) n++; return n; }
void print (const char *s) { svcOutputDebugString (s, len (s)); }

void printHex (u32 v)
{
	char b[9];
	for (int i = 0; i < 8; i++) { u32 d = v >> (28 - 4 * i) & 15; b[i] = (char) (d < 10 ? '0' + d : 'a' + d - 10); }
	b[8] = 0;
	print (b);
}

void printDec (s32 v)
{
	char b[12]; int n = 11; b[n] = 0;
	u32 u = v < 0 ? (u32) -v : (u32) v;
	do { b[--n] = (char) ('0' + u % 10); u /= 10; } while (u);
	if (v < 0) b[--n] = '-';
	print (b + n);
}

void section (const char *name) { s_section = name; print (name); print ("\n"); }

void check (const char *what, int ok)
{
	s_checks++;
	if (ok) return;
	s_failed++;
	print ("FAIL "); print (s_section); print (": "); print (what); print ("\n");
}

void checkEq (const char *what, u32 got, u32 want)
{
	s_checks++;
	if (got == want) return;
	s_failed++;
	print ("FAIL "); print (s_section); print (": "); print (what); print (" = "); printHex (got); print (", expected "); printHex (want); print ("\n");
}

int summary (void)
{
	printDec (s_checks); print (" checks, "); printDec (s_failed); print (" failed\n");
	return s_failed;
}
