//
// printconf -- Printers, the Control Panel's applet (also a window of its own): the printers Onyx prints
// on -- the PDF printer, always there, and the network printers added by their address (IPP Everywhere /
// AirPrint: the printer is asked what it can do; SD:/etc/printers.ini) --, the default one, a test page,
// and the print queue (the jobs waiting, printing, done; a job cancelled).
//
// Everything goes through the print library (printerkit/printerkit.h -> the print service, printd).
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
#include <stdio.h>
#include <string.h>
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "fontkit/uikitface.h"		// FreeType's text (DejaVu Sans) for every widget
#include "printerkit/printerkit.h"

using namespace uikit;

#define W	700
#define H	470

static Root    *g_root;
static ListBox *g_list, *g_jobs;
static Label   *g_model, *g_addr, *g_state, *g_status;
static Textbox *g_newAddr, *g_newName;
static PrintPrinter g_pr[12]; static int g_npr;
static PrintJobInfo g_job[32]; static int g_njob;
static ListBox *g_found; static PrintFound g_fnd[16]; static int g_nfnd;

static void status (const char *s) { g_status->setText (s); }
// the window shown now (a request to the printer takes a moment)
static void show_now (void) { if (!g_root->valid) { g_root->draw (); uk_present (); } }

static void show_printer (void)
{
	int i = g_list->sel;
	if (i < 0 || i >= g_npr) { g_model->setText (""); g_addr->setText (""); g_state->setText (""); return; }
	const PrintPrinter &p = g_pr[i];
	char t[128];
	g_model->setText (p.model);
	if (!strcmp (p.kind, "pdf")) g_addr->setText (TR ("Asks for the file's name when you print."));
	else { snprintf (t, sizeof t, "%s", p.uri); g_addr->setText (t); }
	if (!strcmp (p.kind, "pdf")) g_state->setText ("");
	else
	{
		snprintf (t, sizeof t, TR ("%s, %d dpi, up to %d copies"), p.color ? TR ("Colour") : TR ("Black and white"), p.dpi, p.copies);
		g_state->setText (t);
	}
}
static void load_printers (const char *select)
{
	int keep = g_list->sel;
	g_npr = print_printers (g_pr, 12);
	g_list->clear ();
	for (int i = 0; i < g_npr; i++)
	{
		char t[96]; snprintf (t, sizeof t, "%.44s%s", g_pr[i].name, g_pr[i].is_default ? TR ("  (default)") : "");
		g_list->add (t);
		if (select && !strcmp (select, g_pr[i].name)) keep = i;
	}
	if (keep < 0 || keep >= g_npr) keep = 0;
	g_list->setSel (keep);
	show_printer ();
}
static const char *job_state (const PrintJobInfo &j)
{
	switch (j.state)
	{
	case PRINT_JOB_QUEUED:    return TR ("waiting");
	case PRINT_JOB_PREPARING: return TR ("preparing");
	case PRINT_JOB_SENDING:   return TR ("sending");
	case PRINT_JOB_PRINTING:  return TR ("printing");
	case PRINT_JOB_DONE:      return TR ("done");
	case PRINT_JOB_FAILED:    return TR ("failed");
	default:                  return TR ("cancelled");
	}
}
static void load_jobs (void)
{
	PrintJobInfo nw[32];
	int n = print_jobs (nw, 32);
	if (n == g_njob && !memcmp (nw, g_job, (size_t) n * sizeof nw[0])) return;	// (nothing new: no repaint)
	int sel = g_jobs->sel; unsigned id = sel >= 0 && sel < g_njob ? g_job[sel].id : 0;
	memcpy (g_job, nw, sizeof nw); g_njob = n;
	g_jobs->clear (); sel = -1;
	for (int i = 0; i < n; i++)
	{
		char t[96];
		if ((g_job[i].state == PRINT_JOB_PREPARING || g_job[i].state == PRINT_JOB_SENDING) && g_job[i].pages > 0)
			snprintf (t, sizeof t, "%.28s - %s %d / %d", g_job[i].title, job_state (g_job[i]), g_job[i].page, g_job[i].pages);
		else snprintf (t, sizeof t, "%.34s - %s (%.14s)", g_job[i].title, job_state (g_job[i]), g_job[i].printer);
		g_jobs->add (t);
		if (g_job[i].id == id) sel = i;
	}
	if (sel >= 0) g_jobs->setSel (sel);
}

static void on_select (Widget &) { show_printer (); status (""); }
static void on_job (Widget &)
{
	int i = g_jobs->sel;
	if (i >= 0 && i < g_njob) status (g_job[i].message[0] ? g_job[i].message : job_state (g_job[i]));
}
static void on_default (Widget &)
{
	int i = g_list->sel; if (i < 0 || i >= g_npr) return;
	char n[64]; strcpy (n, g_pr[i].name);
	if (print_printer_default (n)) { load_printers (n); status (TR ("The default printer is set.")); }
}
static void on_remove (Widget &)
{
	int i = g_list->sel; if (i < 0 || i >= g_npr) return;
	if (!strcmp (g_pr[i].kind, "pdf")) { status (TR ("The PDF printer is part of Onyx: it stays.")); return; }
	char q[128]; snprintf (q, sizeof q, TR ("Remove the printer %.60s?"), g_pr[i].name);
	if (uk_messagebox (TR ("Printers"), q, MB_YESNO) != 1) return;
	print_printer_remove (g_pr[i].name);
	load_printers (0); status (TR ("Removed."));
}
static void on_check (Widget &)
{
	int i = g_list->sel; if (i < 0 || i >= g_npr) return;
	status (TR ("Asking the printer...")); show_now ();
	char t[160], n[64]; strcpy (n, g_pr[i].name);
	int ok = print_printer_status (n, t, sizeof t);
	char s[200]; snprintf (s, sizeof s, "%s%s", ok ? "" : TR ("Not ready: "), t);
	load_printers (n); status (s);
}
static void on_add (Widget &)
{
	if (!g_newAddr->text[0]) { status (TR ("Type the printer's address first (its IP address, as 192.168.0.14).")); return; }
	status (TR ("Asking the printer what it can do...")); show_now ();
	char err[160];
	if (print_printer_add (g_newName->text, g_newAddr->text, err, sizeof err))
	{
		char s[200]; snprintf (s, sizeof s, TR ("%s is added."), err);		// (the name it was given)
		load_printers (err); status (s);
		g_newAddr->setText (""); g_newName->setText ("");
	}
	else { char s[200]; snprintf (s, sizeof s, TR ("Not added: %s."), err); status (s); }
}
// Find: the network searched (the print service asks who prints, by mDNS, as AirPrint does); a printer found,
// clicked: its address and name in the fields -- Add adds it.
static void on_find (Widget &)
{
	status (TR ("Searching the network for printers...")); show_now ();
	g_nfnd = print_printers_find (g_fnd, 16);
	g_found->clear ();
	for (int i = 0; i < g_nfnd; i++)
	{
		char t[96]; snprintf (t, sizeof t, "%.30s  %s%s", g_fnd[i].model, g_fnd[i].address, g_fnd[i].usable ? "" : TR ("  (not usable)"));
		g_found->add (t);
	}
	char s[200];
	if (!g_nfnd) snprintf (s, sizeof s, "%s", TR ("No printer found on the network: is it on, and on the same network?"));
	else snprintf (s, sizeof s, g_nfnd == 1 ? TR ("%d printer found: click it, then Add.") : TR ("%d printers found: click one, then Add."), g_nfnd);
	status (s);
	if (g_nfnd == 1) { g_found->setSel (0); g_newAddr->setText (g_fnd[0].address); g_newName->setText (g_fnd[0].name); }
}
static void on_found (Widget &)
{
	int i = g_found->sel;
	if (i < 0 || i >= g_nfnd) return;
	g_newAddr->setText (g_fnd[i].address); g_newName->setText (g_fnd[i].name);
	status (g_fnd[i].usable ? TR ("Add adds this printer.") : TR ("This printer takes neither PWG Raster nor PDF: Onyx cannot print on it."));
}
static void on_cancel_job (Widget &)
{
	int i = g_jobs->sel;
	if (i < 0 || i >= g_njob) { status (TR ("Pick a job of the queue first.")); return; }
	print_job_cancel (g_job[i].id); load_jobs ();
}
static void on_forget (Widget &) { print_jobs_forget (); kapi_msleep (150); load_jobs (); }

// The test page: what the printer can do, seen on paper -- text, the margins, colours, greys, fine lines.
static void on_test (Widget &)
{
	int i = g_list->sel; if (i < 0 || i >= g_npr) return;
	PrintSetup s; print_setup_default (&s);
	strcpy (s.printer, g_pr[i].name);
	print_setup_paper (&s, 0, PRINT_PORTRAIT);
	if (!strcmp (g_pr[i].kind, "pdf")) strcpy (s.output, "SD:/docs/Test page.pdf");
	PrintJob *j = print_begin (&s, "Onyx test page");
	if (!j) { status (TR ("The print queue cannot be written (SD:/var/spool/print).")); return; }
	int f = print_font (j, "DejaVu Sans", 0), fb = print_font (j, "DejaVu Sans", PRINT_BOLD), fs = print_font (j, "DejaVu Serif", PRINT_ITALIC);
	print_page (j, 0, 0);
	float m = 56, w = s.paper_w - 2 * m, y = m;
	// the printable area's edge (what is outside it, the printer cannot print)
	print_frame (j, s.margin_l, s.margin_t, s.paper_w - s.margin_l - s.margin_r, s.paper_h - s.margin_t - s.margin_b, 0.5f, 0xB0B0B0);
	print_text (j, fb, 26, m, y + 26, "Onyx test page", 0x202020); y += 44;
	print_line (j, m, y, m + w, y, 1.5f, 0x4992A7); y += 22;
	char t[200];
	snprintf (t, sizeof t, "Printer: %s (%s)", g_pr[i].name, g_pr[i].model); print_text (j, f, 11, m, y, t, 0); y += 16;
	snprintf (t, sizeof t, "Paper: %s, %d x %d points.", s.media, (int) s.paper_w, (int) s.paper_h);
	print_text (j, f, 11, m, y, t, 0); y += 16;
	print_text (j, f, 11, m, y, "The grey frame is the edge of what this printer prints.", 0); y += 30;
	static const float SZ[5] = { 8, 10, 12, 16, 24 };
	for (int k = 0; k < 5; k++) { y += SZ[k] * 1.1f; snprintf (t, sizeof t, "%d pt - The quick brown fox jumps over the lazy dog", (int) SZ[k]); print_text (j, f, SZ[k], m, y, t, 0); y += SZ[k] * 0.4f; }
	y += 20; print_text (j, fs, 14, m, y, "Serif italic: Portez ce vieux whisky au juge blond qui fume.", 0x303030); y += 30;
	static const unsigned COL[8] = { 0xE03030, 0xE08020, 0xE0D020, 0x30A040, 0x20A0C0, 0x3050C0, 0x8040B0, 0x000000 };
	static const char *const CN[8] = { "red", "orange", "yellow", "green", "cyan", "blue", "violet", "black" };
	float cw = w / 8;
	for (int k = 0; k < 8; k++) { print_rect (j, m + k * cw, y, cw - 6, 40, COL[k]); print_text (j, f, 8, m + k * cw, y + 52, CN[k], 0x404040); }
	y += 76;
	for (int k = 0; k < 16; k++) { unsigned g = (unsigned) (k * 17); print_rect (j, m + k * w / 16, y, w / 16, 24, g << 16 | g << 8 | g); }
	print_frame (j, m, y, w, 24, 0.5f, 0); y += 50;
	static const float LW[5] = { 0.25f, 0.5f, 1, 2, 4 };
	for (int k = 0; k < 5; k++) { print_line (j, m, y, m + w * 0.45f, y, LW[k], 0); snprintf (t, sizeof t, "%d.%02d pt", (int) LW[k], (int) (LW[k] * 100) % 100); print_text (j, f, 8, m + w * 0.47f, y + 3, t, 0); y += 14; }
	// shapes: a disc, a triangle, a slanted line
	float cx = m + w * 0.80f, cy = y - 34, r = 30, kq = 0.5523f * r;
	print_path_move (j, cx + r, cy);
	print_path_curve (j, cx + r, cy + kq, cx + kq, cy + r, cx, cy + r); print_path_curve (j, cx - kq, cy + r, cx - r, cy + kq, cx - r, cy);
	print_path_curve (j, cx - r, cy - kq, cx - kq, cy - r, cx, cy - r); print_path_curve (j, cx + kq, cy - r, cx + r, cy - kq, cx + r, cy);
	print_path_close (j); print_path_fill (j, 0x4992A7);
	print_path_move (j, cx - 90, cy + r); print_path_line (j, cx - 40, cy + r); print_path_line (j, cx - 65, cy - r); print_path_close (j);
	print_path_stroke (j, 1.5f, 0xC04040);
	print_line (j, m, s.paper_h - m - 30, m + w, s.paper_h - m, 0.75f, 0x808080);
	print_text (j, f, 9, m, s.paper_h - m - 40, "Printed by Onyx's print service (printd).", 0x606060);
	int id = print_end (j);
	if (id < 0) status (TR ("The test page could not be queued (is the print service installed?)."));
	else status (!strcmp (g_pr[i].kind, "pdf") ? TR ("The test page is being saved as SD:/docs/Test page.pdf.") : TR ("The test page is in the queue."));
	load_jobs ();
}

class PrintRoot : public Root
{
public:
	unsigned last = 0;
	PrintRoot () : Root (W, H, TR ("Printers")) {}
	void onTick () override
	{
		unsigned now = kapi_get_ticks ();
		if (now - last >= 100) { last = now; load_jobs (); }		// (a second)
	}
};

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);		// (before the widgets; false: the bitmap font)
	uk_lang_init ();				// the words in the system's language (before the widgets)
	PrintRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	int X = root.width > W ? (root.width - W) / 2 : 0;

	GroupBox *gp = new GroupBox (X + 10, 8, 330, 250, TR ("Printers"));
	root.addChild (gp);
	int ct = gp->contentTop () + 4;
	g_list = new ListBox (10, ct, 310, 110, on_select); gp->addChild (g_list);
	g_model = new Label (10, ct + 116, 310, 20, "", C_TEXT, gp->bg); gp->addChild (g_model);
	g_addr = new Label (10, ct + 136, 310, 20, "", C_DIS, gp->bg); gp->addChild (g_addr);
	g_state = new Label (10, ct + 156, 310, 20, "", C_DIS, gp->bg); gp->addChild (g_state);
	int by = ct + 182;
	gp->addChild (new Button (10, by, 78, 28, TR ("Default"), on_default));	// (the widths: the French words fit too)
	gp->addChild (new Button (94, by, 92, 28, TR ("Test page"), on_test));
	gp->addChild (new Button (192, by, 60, 28, TR ("Check"), on_check));
	gp->addChild (new Button (258, by, 62, 28, TR ("Remove"), on_remove));

	GroupBox *ga = new GroupBox (X + 350, 8, W - 360, 250, TR ("Add a network printer"));
	root.addChild (ga);
	ga->addChild (new Label (10, ct, W - 380, 20, TR ("Find searches the network; or type a printer's"), C_TEXT, ga->bg));
	ga->addChild (new Label (10, ct + 20, W - 380, 20, TR ("address (IPP Everywhere / AirPrint: no driver)."), C_TEXT, ga->bg));
	ga->addChild (new Label (10, ct + 52, 70, 20, TR ("Address"), C_TEXT, ga->bg));
	g_newAddr = new Textbox (84, ct + 48, W - 380 - 74, 26, ""); ga->addChild (g_newAddr);
	g_newAddr->tip = TR ("The printer's IP address (192.168.0.14; its network page or its screen shows it), or ipp://host:631/ipp/print");
	ga->addChild (new Label (10, ct + 86, 70, 20, TR ("Name"), C_TEXT, ga->bg));
	g_newName = new Textbox (84, ct + 82, W - 380 - 74, 26, ""); ga->addChild (g_newName);
	g_newName->tip = TR ("The name shown in the Print dialog (empty: the printer's own name)");
	ga->addChild (new Button (84, ct + 118, 86, 28, TR ("Add"), on_add));
	ga->addChild (new Button (176, ct + 118, 86, 28, TR ("Find"), on_find));
	g_found = new ListBox (10, ct + 154, W - 380, 62, on_found); ga->addChild (g_found);
	g_found->tip = TR ("Find searches the network: the printers found are listed here");

	GroupBox *gq = new GroupBox (X + 10, 264, W - 20, 170, TR ("Print queue"));
	root.addChild (gq);
	int qt = gq->contentTop () + 4;
	g_jobs = new ListBox (10, qt, W - 170, 170 - qt - 10, on_job); gq->addChild (g_jobs);
	gq->addChild (new Button (W - 150, qt, 120, 28, TR ("Cancel job"), on_cancel_job));
	gq->addChild (new Button (W - 150, qt + 34, 120, 28, TR ("Clear done"), on_forget));

	g_status = new Label (X + 12, H - 28, W - 24, 22, "", C_DIS, root.bg); root.addChild (g_status);

	load_printers (0);
	load_jobs ();
	root.run ();
	return 0;
}
