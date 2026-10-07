// tools/tests/json/test_json.cpp -- user/Include/json.hpp on the PC, under AddressSanitizer / LeakSanitizer:
// unit cases, then every file given on the command line parsed, written back (pretty and
// minified), parsed again and compared node by node.
//   sh tools/tests/json/run.sh [files...]
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "json.hpp"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static bool same (const json::Value &a, const json::Value &b)
{
	if (a.type != b.type) return false;
	switch (a.type)
	{
	case json::NUL: return true;
	case json::BOOL: return a.b == b.b;
	case json::NUM: return fabs (a.n - b.n) <= 1e-6 * (1 + fabs (a.n));
	case json::STR: return a.slen_ == b.slen_ && memcmp (a.s, b.s, a.slen_) == 0;
	default:
		if (a.count != b.count) return false;
		for (const json::Value *x = a.first (), *y = b.first (); x; x = x->next, y = y->next)
		{
			if (a.type == json::OBJ && strcmp (x->key, y->key)) return false;
			if (!same (*x, *y)) return false;
		}
		return true;
	}
}

static void units ()
{
	json::Doc d;
	CHECK (d.parse ("{\"a\":1,\"b\":[true,false,null],\"c\":\"x\\u00e9\\ud83c\\udfb5\",\"d\":-12.5e1}"));
	CHECK (d.root ()["a"].asInt () == 1);
	CHECK (d.root ()["A"].asInt () == 1);				// case-insensitive fallback
	CHECK (d.root ()["b"].size () == 3 && d.root ()["b"][0].asBool () && !d.root ()["b"][1].asBool (true));
	CHECK (d.root ()["b"][2].isNull ());
	CHECK (strcmp (d.root ()["c"].asStr (), "x\xc3\xa9\xf0\x9f\x8e\xb5") == 0);
	CHECK (d.root ()["d"].asDouble () == -125.0);
	CHECK (d.root ()["missing"]["deeper"][3].asInt (7) == 7);
	char l1[16]; json::latin1 (d.root ()["c"].asStr (), l1, sizeof l1);
	CHECK (strcmp (l1, "x\xe9?") == 0);
	CHECK (!d.parse ("{\"a\":1,}"));
	CHECK (d.errLine () == 1 && d.errCol () == 8);
	const char *llm = "Here is the JSON:\n```json\n{\"a\":[1,2,], /* c */ \"b\":\"t\"} // done\n```\n";
	CHECK (d.parse (llm, strlen (llm), json::TOLERANT));
	CHECK (d.root ()["a"].size () == 2 && strcmp (d.root ()["b"].asStr (), "t") == 0);
	CHECK (!d.parse ("[1,2", 4));
	CHECK (!d.parse ("\"abc", 4));
	CHECK (d.parse ("{\"n\":\"120\"}") && d.root ()["n"].asInt () == 120);
	CHECK (d.parse ("{\"m\":18446744073709551615}") && d.root ()["m"].asU64 () == 18446744073709551615ull);
	// the writer
	json::Writer w (true);
	w.beginObj (); w.key ("bpm"); w.num (120.0); w.key ("pi"); w.num (3.25);
	w.key ("notes"); w.beginArr ();
	for (int i = 0; i < 2; i++) { w.beginArr (true); w.num (48 + i); w.num (0); w.num (24); w.endArr (); }
	w.endArr (); w.key ("s"); w.str ("a\"b\n"); w.key ("e"); w.beginArr (); w.endArr (); w.key ("o"); w.beginObj (); w.endObj ();
	w.endObj ();
	const char *expect = "{\n  \"bpm\": 120,\n  \"pi\": 3.25,\n  \"notes\": [\n    [48,0,24],\n    [49,0,24]\n  ],\n  \"s\": \"a\\\"b\\n\",\n  \"e\": [],\n  \"o\": {}\n}";
	if (strcmp (w.data (), expect)) { printf ("writer gave:\n%s\n", w.data ()); g_fail++; }
	json::Writer m (false);
	m.beginArr (); m.num (1e20); m.num (-0.001); m.num (1.0 / 3); m.endArr ();
	CHECK (strcmp (m.data (), "[1e20,-0.001,0.333333]") == 0 || (printf ("got %s\n", m.data ()), false));
	// building a document
	json::Doc b; json::Value *o = b.newObj (); b.set (o, "k", b.newStr ("v")); b.set (o, "n", b.newNum (2)); b.setRoot (o);
	CHECK (strcmp (b.root ()["k"].asStr (), "v") == 0 && b.root ()["n"].asInt () == 2);
}

static char *slurp (const char *path, long *len)
{
	FILE *f = fopen (path, "rb"); if (!f) return 0;
	fseek (f, 0, SEEK_END); *len = ftell (f); fseek (f, 0, SEEK_SET);
	char *b = (char *) malloc (*len + 1); if (fread (b, 1, *len, f) != (size_t) *len) *len = 0; b[*len] = 0; fclose (f);
	return b;
}

int main (int argc, char **argv)
{
	units ();
	for (int i = 1; i < argc; i++)
	{
		long len; char *t = slurp (argv[i], &len);
		if (!t) { printf ("FAIL cannot read %s\n", argv[i]); g_fail++; continue; }
		json::Doc a;
		if (!a.parse (t, len)) { printf ("FAIL %s: %s at %d:%d\n", argv[i], a.error (), a.errLine (), a.errCol ()); g_fail++; free (t); continue; }
		for (int pretty = 0; pretty < 2; pretty++)
		{
			json::Writer w (pretty != 0); w.value (a.root ());
			json::Doc b;
			if (!b.parse (w.data (), w.size ())) { printf ("FAIL %s rewritten: %s\n", argv[i], b.error ()); g_fail++; }
			else if (!same (a.root (), b.root ())) { printf ("FAIL %s: round trip differs\n", argv[i]); g_fail++; }
		}
		printf ("ok %s (%ld bytes, arena %lu)\n", argv[i], len, a.bytes ());
		free (t);
	}
	printf (g_fail ? "json: %d FAILED\n" : "json: all passed\n", g_fail);
	return g_fail != 0;
}
