//
// printerkit/pdfprint.h -- printing through an app's PDF export code: PrintWriter is a pdfw::Writer
// (pdf/pdfwrite.h) whose pages are recorded into a print job (printerkit/printerkit.h) instead of a PDF. An app that
// has "Export as PDF" prints with the same drawing code:
//
//   PrintSetup s; print_setup_default (&s);
//   PrintDialogInfo di = { sizeof di, name, pages, current, PRINT_DLG_OWN_PAPER, pageW, pageH };
//   if (!print_dialog (&s, &di)) return;
//   PrintJob *j = print_begin (&s, name);
//   { PrintWriter w (j); export_pages (w); }      // the code that takes a pdfw::Writer &
//   print_end (j);
//
// The pages outside the range chosen in the dialog are dropped (their drawing is ignored); the app need
// not know. Links and bookmarks have no meaning on paper: ignored.
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
#ifndef ONYX_PRINT_PDFPRINT_H
#define ONYX_PRINT_PDFPRINT_H

#include "pdf/pdfwrite.h"
#include "printerkit/printerkit.h"

class PrintWriter : public pdfw::Writer
{
public:
	PrintJob *job;
	bool wanted;				// the page begun is one to print (an app may skip a costly page that is not)
	PrintWriter (PrintJob *j) : job (j), wanted (false) {}

	int  add_font (const unsigned char *ttf, unsigned len) override { return print_font_data (job, ttf, len); }
	void begin_page (float w, float h) override { wanted = print_page (job, w, h) != 0; }
	void fill_rect (float x, float y, float w, float h, unsigned rgb) override { print_rect (job, x, y, w, h, rgb); }
	void glyph (int f, float size, float x, float y, unsigned gid, unsigned unicode, unsigned rgb, bool bold = false, bool italic = false) override
	{
		print_glyph (job, f, size, x, y, gid, unicode, rgb, (bold ? PRINT_BOLD : 0) | (italic ? PRINT_ITALIC : 0));
	}
	void image (const unsigned *px, int pw, int ph, float x, float y, float w, float h, bool jpeg = false, int quality = 85) override
	{
		(void) quality;
		print_image (job, px, pw, ph, x, y, w, h, PRINT_IMG_ALPHA | (jpeg ? PRINT_IMG_PHOTO : 0));
	}
	void end_page () override {}
	bool page_wanted () const override { return wanted; }
};

// The usual sequence: the Print dialog for a document with its own page size, then a job -> 0: cancelled
// (or the queue cannot be written). The caller draws through PrintWriter, then print_end (job).
static inline PrintJob *print_ask (const char *title, int pages, int current, float pageW, float pageH, PrintSetup *setup = 0)
{
	PrintSetup local; PrintSetup *s = setup ? setup : &local;
	if (!setup || !s->size) print_setup_default (s);
	PrintDialogInfo di = { sizeof (PrintDialogInfo), title, pages, current, PRINT_DLG_OWN_PAPER, pageW, pageH };
	if (!print_dialog (s, &di)) return 0;
	return print_begin (s, title);
}

#endif
