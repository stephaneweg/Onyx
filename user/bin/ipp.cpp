//
// ipp -- ask a network printer what it can do (IPP: printerkit/ipp.h; the Printers applet's diagnostic).
//
//   ipp 192.168.0.14            the printer's model, the formats it takes, its papers, its ink
//   ipp ipp://host:631/ipp/print
//   ipp <address> validate      ... and whether it would take a job from Onyx (nothing is printed)
//
// A printer Onyx can print on answers here and takes PWG Raster (IPP Everywhere, AirPrint) or PDF.
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
#include "kapi.h"
#include "onyxpp.hpp"
#pragma GCC diagnostic ignored "-Wunused-function"	// (the headers' other functions)
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "printerkit/ippnet.h"

static void outs (const char *s) { int n = 0; while (s[n]) n++; kapi_stdout_write (s, (unsigned) n); }
static void outi (int v) { char t[16]; ipp::sint (t, sizeof t, v); outs (t); }
static void line (const char *k, const char *v) { outs (k); outs (v); outs ("\n"); }

int main (void)
{
	char a[256];
	int n = kapi_get_args (a, sizeof a - 1); a[n > 0 ? n : 0] = 0;
	int s = 0; while (a[s] == ' ') s++;
	int e = s; while (a[e] && a[e] != ' ') e++;
	bool validate = false;
	if (a[e]) { a[e++] = 0; while (a[e] == ' ') e++; validate = a[e] == 'v'; }
	if (!a[s]) { outs ("usage: ipp <printer address> [validate]\n"); return 1; }
	if (!kapi_net_status (0, 0)) { outs ("ipp: no network\n"); return 1; }

	ipp::Caps *c = new ipp::Caps; char err[96];
	if (!ipp::get_caps (&ipp::KAPI_NET, a + s, *c, err, sizeof err)) { outs ("ipp: "); outs (err); outs ("\n"); return 1; }
	line ("printer   ", c->model);
	line ("name      ", c->name);
	line ("formats   ", c->formats);
	outs ("Onyx      "); outs (c->pwg ? "prints on it (PWG Raster" : c->pdf ? "prints on it (PDF" : "cannot print on it (neither PWG Raster nor PDF");
	if (c->pwg) { outs (", "); outi (c->dpi); outs (" dpi"); }
	outs (")\n");
	outs ("colour    "); outs (c->color ? "yes" : "no"); outs ("\n");
	outs ("quality   "); if (c->quality & 8) outs ("draft "); if (c->quality & 16) outs ("normal "); if (c->quality & 32) outs ("high"); outs ("\n");
	outs ("copies    1-"); outi (c->copies); outs ("\n");
	outs ("two-sided "); outs (c->duplex ? "yes" : "no"); outs ("\n");
	line ("paper     ", c->media);
	line ("default   ", c->media_default);
	outs ("margins   left "); outi (c->margin[0]); outs (", top "); outi (c->margin[1]); outs (", right "); outi (c->margin[2]); outs (", bottom "); outi (c->margin[3]); outs (" (1/100 mm)\n");
	outs ("state     "); outs (c->state == 3 ? "idle" : c->state == 4 ? "printing" : c->state == 5 ? "stopped" : "?");
	if (c->state_reasons[0]) { outs (" ("); outs (c->state_reasons); outs (")"); }
	outs ("\n");
	for (int i = 0; i < c->ninks; i++) { outs ("ink       "); outs (c->ink_name[i]); outs (" "); outi (c->ink[i]); outs (" %\n"); }
	if (validate)
	{
		ipp::Send *v = new ipp::Send;
		ipp::JobSetup js = { "Onyx validation", "onyx", c->pwg ? "image/pwg-raster" : "application/pdf", c->media_default, 1, 4, false };
		bool ok = v->begin (&ipp::KAPI_NET, a + s, js, true) && v->end () >= 0;
		outs ("validate  "); outs (ok ? "the printer would take the job" : v->err); outs ("\n");
		return ok ? 0 : 1;
	}
	return c->pwg || c->pdf ? 0 : 1;
}
