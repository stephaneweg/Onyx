//
// dialect_test.cpp -- Onyx BASIC's host words (bas::setDialect: statements, functions, aliases, REPEAT) and its
// statement hook (Host::lineHook / onStatement), on the PC. Run by tools/tests/run_basic_test.sh.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "basic/bas.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf ("FAIL %s:%d ", __FILE__, __LINE__); printf (__VA_ARGS__); printf ("\n"); } } while (0)

enum { W_STEP = 1, W_POS, W_NAME, W_BOOM };
static const bas::ExtWord WORDS[] = {
	{ "STEPIT", W_STEP, 's', "[N" }, { "POSX", W_POS, 'n', "" }, { "NAMEOF$", W_NAME, '$', "N" },
	{ "BOOM", W_BOOM, 's', "" }, { "COLOR", W_STEP, 's', "N" }, { 0, 0, 0, 0 } };
static const char *const ALIASES[] = { "AVANCE", "STEPIT", "SI", "IF", "ALORS", "THEN", "FIN", "END", "REPETE", "REPEAT", 0 };
static const bas::Dialect DIALECT = { WORDS, ALIASES, true };

struct H : bas::Host
{
	double x = 0; char log[4096] = ""; int stmts = 0, stopAt = -1; char name[16];
	void out (const char *s, int n) override { strncat (log, s, n); }
	int inputLine (char *, int) override { return -1; }
	bool ext (int id, const bas::ExtVal *a, int argc, bas::ExtVal *r, char *why, int cap) override
	{
		switch (id)
		{
		case W_STEP: x += argc ? a[0].n : 1; return true;
		case W_POS: r->str = false; r->n = x; return true;
		case W_NAME: snprintf (name, sizeof name, "n%d", (int) a[0].n); r->str = true; r->s = name; r->len = (int) strlen (name); return true;
		case W_BOOM: snprintf (why, cap, "boom"); return false;
		}
		return false;
	}
	bool onStatement (int line) override { stmts++; char b[16]; snprintf (b, sizeof b, "<%d>", line); strcat (log, b); return stmts != stopAt; }
};

static int run (H &h, const char *src, bas::Error *e)
{
	bas::Program *p = bas::compile (src, e);
	if (!p) return -2;
	int rc = bas::run (p, h, e);
	bas::destroy (p);
	return rc;
}

int main ()
{
	bas::setDialect (&DIALECT);
	bas::Error e;
	{ H h; int rc = run (h, "STEPIT 2\nAVANCE\nREPEAT 3\n  STEPIT\nEND REPEAT\nPRINT POSX; NAMEOF$ (7)\n", &e);
	  CHECK (rc == 0, "rc %d %s", rc, e.msg); CHECK (h.x == 6, "x %g", h.x); CHECK (strstr (h.log, " 6 n7") != 0, "log %s", h.log); }
	{ H h; run (h, "REPETE 2: STEPIT 5: FIN REPETE\nSI POSX () = 10 ALORS PRINT \"ten\"\n", &e); CHECK (strstr (h.log, "ten") != 0, "alias %s", h.log); }
	{ H h; int rc = run (h, "STEPIT\n\nBOOM\n", &e); CHECK (rc == -1 && e.line == 3 && !strcmp (e.msg, "boom"), "error rc %d line %d %s", rc, e.line, e.msg); }
	{ H h; bas::Program *p = bas::compile ("POSX = 3\n", &e); CHECK (!p, "a word is reserved"); bas::destroy (p); }
	{ H h; int rc = run (h, "COLOR 4\n", &e); CHECK (rc == 0 && h.x == 4, "COLOR is the dialect's"); }
	{ H h; bas::Program *p = bas::compile ("REPEAT 2\nSTEPIT\n", &e); CHECK (!p && strstr (e.msg, "END REPEAT"), "unclosed %s", e.msg); bas::destroy (p); }
	{ H h; h.lineHook = true; int rc = run (h, "STEPIT\nFOR i = 1 TO 3\n  STEPIT\nNEXT\n", &e);
	  CHECK (rc == 0 && !strcmp (h.log, "<1><2><3><3><3>"), "hook %s", h.log); }
	{ H h; h.lineHook = true; h.stopAt = 3; int rc = run (h, "STEPIT\nSTEPIT\nSTEPIT\nSTEPIT\n", &e); CHECK (rc == 0 && h.x == 2, "stopped x %g", h.x); }
	bas::setDialect (0);
	{ H h; bas::Program *p = bas::compile ("STEPIT = 3: PRINT STEPIT\n", &e); CHECK (p != 0, "plain BASIC: a variable %s", e.msg);
	  if (p) { bas::run (p, h, &e); bas::destroy (p); } CHECK (strstr (h.log, " 3") != 0, "%s", h.log); }
	printf (fails ? "dialect: %d failure(s)\n" : "ok   dialect (words, aliases, REPEAT, statement hook)\n", fails);
	return fails != 0;
}
