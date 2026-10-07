//
// solution.cpp -- Critters' solutions and replays (solution.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "solution.h"
#include <stdio.h>
#include <string.h>

namespace critters {

static const char *const E_STEP = "bad step";
static const char *const E_ACTION = "bad action";
static const char *const E_CREATURE = "bad creature";
static const char *const E_ACTIONS = "too many actions (max 1024)";

static bool sp (char c) { return c == ' ' || c == '\t' || c == '\r'; }
static bool digit (char c) { return c >= '0' && c <= '9'; }

static bool fail (LoadError &e, int line, const char *fmt)
{
	e.line = line; e.fmt = fmt; e.arg[0][0] = e.arg[1][0] = 0;
	snprintf (e.reason, sizeof e.reason, "%s", fmt);
	return false;
}
// A word of the line into w (at most cap - 1 characters; longer: cut, and false)
static bool word (const char *&p, const char *end, char *w, int cap)
{
	while (p < end && sp (*p)) p++;
	int n = 0;
	bool ok = true;
	while (p < end && !sp (*p)) { if (n < cap - 1) w[n++] = *p; else ok = false; p++; }
	w[n] = 0;
	return ok;
}
// A whole non-negative number (at most 7 digits) -> -1 if it does not read
static int number (const char *w)
{
	if (!*w) return -1;
	int v = 0, n = 0;
	for (; *w; w++) { if (!digit (*w) || ++n > 7) return -1; v = v * 10 + (*w - '0'); }
	return v;
}

bool load_solution (const char *text, Solution &s, LoadError &e)
{
	s.n = 0;
	memset (&e, 0, sizeof e);
	e.fmt = "";
	int line = 0, last = 0;
	const char *p = text ? text : "";
	while (*p)
	{
		line++;
		const char *eol = p;
		while (*eol && *eol != '\n') eol++;
		const char *end = p;
		while (end < eol && *end != '#') end++;		// a "#" ends the line
		char w1[16], w2[16], w3[16], w4[16];
		const char *q = p;
		bool ok1 = word (q, end, w1, sizeof w1), ok2 = word (q, end, w2, sizeof w2), ok3 = word (q, end, w3, sizeof w3);
		word (q, end, w4, sizeof w4);
		p = *eol ? eol + 1 : eol;
		if (!w1[0]) continue;				// a blank or comment line
		int step = ok1 ? number (w1) : -1;
		if (step < 0 || step < last) { s.n = 0; return fail (e, line, E_STEP); }
		if (s.n >= MAXACT) { s.n = 0; return fail (e, line, E_ACTIONS); }
		Action &a = s.a[s.n];
		a.step = step; a.line = line; a.role = -1; a.arg = 0;
		int role = -1;
		for (int r = 0; r < NROLES && ok2; r++) if (!strcmp (w2, ROLE_WORD[r])) role = r;
		if (role >= 0)
		{
			int who = ok3 ? number (w3) : -1;
			if (who < 0 || who >= MAXCRIT || w4[0]) { s.n = 0; return fail (e, line, E_CREATURE); }
			a.kind = A_ROLE; a.role = (int8_t) role; a.arg = (int16_t) who;
		}
		else if (ok2 && !strcmp (w2, "rate"))
		{
			int r = ok3 ? number (w3) : -1;
			if (r < 1 || r > 99 || w4[0]) { s.n = 0; return fail (e, line, E_ACTION); }
			a.kind = A_RATE; a.arg = (int16_t) r;
		}
		else if (ok2 && (!strcmp (w2, "nuke") || !strcmp (w2, "pause") || !strcmp (w2, "fast")))
		{
			if (w3[0]) { s.n = 0; return fail (e, line, E_ACTION); }
			a.kind = w2[0] == 'n' ? A_NUKE : w2[0] == 'p' ? A_PAUSE : A_FAST;
		}
		else { s.n = 0; return fail (e, line, E_ACTION); }
		last = step;
		s.n++;
	}
	return true;
}

void Replay::apply_due (World &w)
{
	if (!s) return;
	while (next < s->n && s->a[next].step < w.step) next++;	// (late: never applied)
	for (; next < s->n && s->a[next].step == w.step; next++)
	{
		const Action &a = s->a[next];
		switch (a.kind)
		{
		case A_ROLE: if (w.assign (a.arg, a.role) != OK) refused++; break;
		case A_RATE: w.set_rate (a.arg); break;
		case A_NUKE: w.nuke (); break;
		default: break;
		}
	}
}

void Recorder::add (int step, int kind, int role, int arg)
{
	if (s.n >= MAXACT) return;
	Action &a = s.a[s.n++];
	a.step = step; a.kind = (uint8_t) kind; a.role = (int8_t) role; a.arg = (int16_t) arg; a.line = 0;
}

int Recorder::text (char *out, int cap) const
{
	if (cap <= 0) return 0;
	int n = 0;
	out[0] = 0;
	for (int i = 0; i < s.n && n < cap - 1; i++)
	{
		const Action &a = s.a[i];
		int k;
		switch (a.kind)
		{
		case A_ROLE: k = snprintf (out + n, (size_t) (cap - n), "%d %s %d\n", a.step, ROLE_WORD[a.role & 7], a.arg); break;
		case A_RATE: k = snprintf (out + n, (size_t) (cap - n), "%d rate %d\n", a.step, a.arg); break;
		case A_NUKE: k = snprintf (out + n, (size_t) (cap - n), "%d nuke\n", a.step); break;
		case A_PAUSE: k = snprintf (out + n, (size_t) (cap - n), "%d pause\n", a.step); break;
		default: k = snprintf (out + n, (size_t) (cap - n), "%d fast\n", a.step); break;
		}
		if (k < 0) break;
		n += k;
		if (n > cap - 1) n = cap - 1;
	}
	return n;
}

RunResult run_solution (const Level &lv, const Solution *s, void (*perStep) (int step, uint64_t sum, void *u), void *u, World *wp)
{
	RunResult r;
	memset (&r, 0, sizeof r);
	World *own = wp ? 0 : new World;
	World &w = wp ? *wp : *own;
	if (!w.reset (lv)) { delete own; r.result = LOST; return r; }
	Replay rp;
	rp.start (s);
	while (w.result == PLAYING)
	{
		rp.apply_due (w);
		w.tick ();
		if (perStep) perStep (w.step, w.checksum (), u);
	}
	r.result = w.result; r.saved = w.saved; r.needed = lv.save; r.endStep = w.endStep; r.refused = rp.refused;
	r.lastChecksum = w.checksum ();
	r.hashOk = w.t.hash == w.t.full_hash ();
	delete own;
	return r;
}

}
