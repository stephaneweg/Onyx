//
// print/dialog.cpp -- the Print dialog, the same in every app (print_dialog, print/print.h): the printer,
// the pages (all, the current one, a range), the copies, colour or black and white, the quality, the paper
// and its orientation. What it shows follows the printer chosen -- a printer without colour has no colour
// choice, the PDF printer has no copies nor quality and asks for the file to write.
//
// A wtk modal dialog in the app's window: wtk is the shared library wtk.so, reached through its import
// stubs (print/print.cpp opens it); wtk's variables (the palette, the text face) are the program's.
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
#include "wtk/wtk.h"
#include "print/print.h"
#include "print/priv.h"

using namespace wtk;
using namespace pprt;

namespace {

enum { R_CANCEL = 0, R_PRINT = 1, R_PRINTER = 2 };		// the dialog's results (R_PRINTER: another printer: rebuilt)
enum { LX = 18, CX = 132, CW = 250, ROW = 36, DW = 410 };

struct State							// what the user set, kept while the dialog is rebuilt
{
	int printer, pagesMode, from, to, copies, mono, quality, orientation;
	char media[64];
};

class PrintDlg : public Modal
{
public:
	List &list; const PrintDialogInfo &info; State &st;
	const Printer &pr;
	const char *names[MAXPRINTERS];
	char mediaName[40][64], mediaLabel[40][40]; const char *mediaOpt[40]; int nmedia;
	const char *qualOpt[3]; int qualVal[3]; int nqual;
	const char *colOpt[2], *oriOpt[2];
	Dropdown *dPrinter, *dColor, *dQual, *dMedia, *dOri;
	RadioButton *rAll, *rCur, *rRange; NumericUpDown *nFrom, *nTo, *nCopies;
	int yPages, yCopies, yColor, yQual, yMedia, yOri, yInfo;

	static int height_for (const Printer &p, const PrintDialogInfo &i)
	{
		bool ipp = !seq (p.kind, "pdf");
		int q = 0; for (int k = 3; k <= 5; k++) if (p.quality & (1u << k)) q++;
		int n = 1 + (ipp ? 1 : 0) + (ipp && p.color ? 1 : 0) + (ipp && q > 1 ? 1 : 0) + (i.flags & PRINT_DLG_OWN_PAPER ? 0 : 2);
		return Modal::titleH () + 14 + n * ROW + 24 + (i.current > 0 ? 3 : 2) * 28 + 10 + 54;
	}

	PrintDlg (List &l, const PrintDialogInfo &i, State &s)
		: Modal (DW, height_for (l.p[s.printer], i)), list (l), info (i), st (s), pr (l.p[s.printer]),
		  dColor (0), dQual (0), dMedia (0), dOri (0), rCur (0), nCopies (0)
	{
		Root *r = Root::current ();
		int RW = r ? r->width : width, RH = r ? r->height : height;
		left = RW > width ? (RW - width) / 2 : 0; top = RH > height ? (RH - height) / 2 : 0;
		bool ipp = !seq (pr.kind, "pdf");
		int y = titleH () + 14;

		for (int k = 0; k < list.n; k++) names[k] = list.p[k].name;
		dPrinter = new Dropdown (CX, y, CW, 26, names, list.n, st.printer, onPrinter); addChild (dPrinter);
		yInfo = y + 30; y += ROW + 24;

		// the pages
		yPages = y;
		rAll = new RadioButton (CX, y, 120, 24, TR ("All"), 1, st.pagesMode == 0, 0, C_FACE); addChild (rAll); y += 28;
		if (info.current > 0) { rCur = new RadioButton (CX, y, 160, 24, TR ("Current page"), 1, st.pagesMode == 1, 0, C_FACE); addChild (rCur); y += 28; }
		else if (st.pagesMode == 1) st.pagesMode = 0;
		rRange = new RadioButton (CX, y, 76, 24, TR ("From"), 1, st.pagesMode == 2, 0, C_FACE); addChild (rRange);
		int most = info.pages > 0 ? info.pages : 9999;
		nFrom = new NumericUpDown (CX + 78, y - 1, 66, 26, 1, most, st.from, 1, onRange); addChild (nFrom);
		nTo = new NumericUpDown (CX + 78 + 66 + 34, y - 1, 66, 26, 1, most, st.to, 1, onRange); addChild (nTo);
		if (st.pagesMode == 0) rAll->checked = true;
		y += 28 + 10;

		if (ipp)
		{
			yCopies = y;
			nCopies = new NumericUpDown (CX, y, 66, 26, 1, pr.copies > 1 ? pr.copies : 1, st.copies, 1, 0); addChild (nCopies);
			y += ROW;
		}
		if (ipp && pr.color)
		{
			yColor = y;
			colOpt[0] = TR ("Colour"); colOpt[1] = TR ("Black and white");
			dColor = new Dropdown (CX, y, CW, 26, colOpt, 2, st.mono ? 1 : 0, 0); addChild (dColor);
			y += ROW;
		}
		nqual = 0;
		static const char *const QN[3] = { "Draft", "Normal", "High" };
		for (int k = 3; k <= 5; k++) if (pr.quality & (1u << k)) { qualOpt[nqual] = TR (QN[k - 3]); qualVal[nqual++] = k; }
		if (ipp && nqual > 1)
		{
			yQual = y;
			int sel = 0; for (int k = 0; k < nqual; k++) if (qualVal[k] == st.quality) sel = k;
			dQual = new Dropdown (CX, y, CW, 26, qualOpt, nqual, sel, 0); addChild (dQual);
			y += ROW;
		}
		nmedia = 0;
		if (!(info.flags & PRINT_DLG_OWN_PAPER))
		{
			int sel = -1, def = 0;
			while (nmedia < 40 && list_item (pr.media, nmedia, mediaName[nmedia], 64))
			{
				media_label (mediaName[nmedia], mediaLabel[nmedia], 40); mediaOpt[nmedia] = mediaLabel[nmedia];
				if (seq (mediaName[nmedia], st.media)) sel = nmedia;
				if (seq (mediaName[nmedia], pr.media_default)) def = nmedia;
				nmedia++;
			}
			if (sel < 0 && info.paper_w > 0)
			{	// the document's own paper, if the printer has it (upright or lying)
				float w = info.paper_w < info.paper_h ? info.paper_w : info.paper_h, h = info.paper_w < info.paper_h ? info.paper_h : info.paper_w;
				for (int k = 0; k < nmedia && sel < 0; k++)
				{
					float mw, mh;
					if (media_size (mediaName[k], &mw, &mh) && mw > w - 2 && mw < w + 2 && mh > h - 2 && mh < h + 2) sel = k;
				}
			}
			if (sel < 0) sel = def;
			yMedia = y;
			dMedia = new Dropdown (CX, y, CW, 26, mediaOpt, nmedia, sel, 0); addChild (dMedia);
			y += ROW;
			yOri = y;
			oriOpt[0] = TR ("Portrait"); oriOpt[1] = TR ("Landscape");
			dOri = new Dropdown (CX, y, CW, 26, oriOpt, 2, st.orientation ? 1 : 0, 0); addChild (dOri);
			y += ROW;
		}
		Button *b = new Button (width - 196, height - 42, 86, 28, TR ("Print"), onBtn); b->tag = R_PRINT; addChild (b);
		b = new Button (width - 102, height - 42, 86, 28, TR ("Cancel"), onBtn); b->tag = R_CANCEL; addChild (b);
	}

	// the controls' values kept (the dialog is rebuilt for another printer; the caller reads them at the end)
	void keep ()
	{
		st.printer = dPrinter->sel;
		st.pagesMode = rRange->checked ? 2 : rCur && rCur->checked ? 1 : 0;
		st.from = nFrom->value; st.to = nTo->value;
		if (st.to < st.from) st.to = st.from;
		if (nCopies) st.copies = nCopies->value;
		if (dColor) st.mono = dColor->sel;
		if (dQual) st.quality = qualVal[dQual->sel];
		if (dMedia && nmedia) scpy (st.media, sizeof st.media, mediaName[dMedia->sel]);
		if (dOri) st.orientation = dOri->sel;
	}
	static PrintDlg *of (Widget &w) { Widget *p = w.parent; while (p && !p->modal) p = p->parent; return (PrintDlg *) p; }
	static void onBtn (Widget &w) { PrintDlg *d = of (w); if (d) d->onButton (w.tag); }
	static void onPrinter (Widget &w) { PrintDlg *d = of (w); if (d && d->dPrinter->sel != d->st.printer) d->onButton (R_PRINTER); }
	static void onRange (Widget &w) { PrintDlg *d = of (w); if (d && !d->rRange->checked) d->rRange->select (); }
	void onButton (int tag) override { keep (); close (tag); }
	bool onKey (long k) override
	{
		if (k == 27) { close (R_CANCEL); return true; }
		if (k == KEY_ENTER) { onButton (R_PRINT); return true; }
		return false;
	}
	void onDraw () override
	{
		char t[160]; scpy (t, sizeof t, TR ("Print"));
		if (info.title && info.title[0]) { scat (t, sizeof t, " - "); scat (t, sizeof t, info.title); }
		drawBox (t);
		int ty = (26 - wk_fh ()) / 2;
		canvas.text (LX, titleH () + 14 + ty, TR ("Printer"), C_TEXT);
		canvas.text (CX, yInfo, pr.model, C_DIS);
		canvas.text (LX, yPages + 4, TR ("Pages"), C_TEXT);
		canvas.text (nFrom->left + nFrom->width + 10, nFrom->top + ty, TR ("to"), C_TEXT);
		if (info.pages > 0)
		{
			char n[48]; pngsave::Buf b; putint (b, info.pages); b.put ((unsigned char) 0);
			scpy (n, sizeof n, "("); scat (n, sizeof n, (const char *) b.b); scat (n, sizeof n, info.pages == 1 ? " page)" : " pages)");
			canvas.text (rAll->left + 124, yPages + 4, n, C_DIS);
		}
		if (nCopies) canvas.text (LX, yCopies + ty, TR ("Copies"), C_TEXT);
		if (dColor) canvas.text (LX, yColor + ty, TR ("Colour"), C_TEXT);
		if (dQual) canvas.text (LX, yQual + ty, TR ("Quality"), C_TEXT);
		if (dMedia) { canvas.text (LX, yMedia + ty, TR ("Paper"), C_TEXT); canvas.text (LX, yOri + ty, TR ("Orientation"), C_TEXT); }
	}
};

} // namespace

extern "C" int print_dialog (PrintSetup *s, const PrintDialogInfo *info)
{
	if (!s || !print__need_wtk () || Root::current () == 0) return 0;
	PrintDialogInfo di; for (unsigned i = 0; i < sizeof di; i++) ((char *) &di)[i] = 0;
	if (info) { unsigned n = info->size < sizeof di ? info->size : sizeof di; for (unsigned i = 0; i < n; i++) ((char *) &di)[i] = ((const char *) info)[i]; }
	if (s->size == 0) print_setup_default (s);

	List *l = new List; load (*l);
	State st;
	st.printer = 0;
	for (int i = 0; i < l->n; i++) if (seq (l->p[i].name, s->printer[0] ? s->printer : l->def)) st.printer = i;
	st.pagesMode = s->from > 0 ? 2 : 0;
	st.from = s->from > 0 ? s->from : 1; st.to = s->to > 0 ? s->to : di.pages > 0 ? di.pages : 1;
	st.copies = s->copies > 0 ? s->copies : 1; st.mono = s->mono; st.quality = s->quality ? s->quality : PRINT_NORMAL;
	st.orientation = s->orientation;
	if (di.paper_w > di.paper_h * 1.02f && s->media[0] == 0) st.orientation = PRINT_LANDSCAPE;
	scpy (st.media, sizeof st.media, di.paper_w > 0 ? "" : s->media);

	int r;
	for (;;)
	{
		PrintDlg *d = new PrintDlg (*l, di, st);
		r = d->run ();
		delete d;
		if (r != R_PRINTER) break;
	}
	int ok = 0;
	if (r == R_PRINT)
	{
		const Printer &p = l->p[st.printer];
		bool pdf = seq (p.kind, "pdf");
		scpy (s->printer, sizeof s->printer, p.name);
		s->copies = pdf ? 1 : st.copies; s->mono = pdf ? 0 : st.mono; s->quality = st.quality;
		s->from = st.pagesMode == 2 ? st.from : st.pagesMode == 1 ? di.current : 0;
		s->to = st.pagesMode == 2 ? st.to : st.pagesMode == 1 ? di.current : 0;
		print_setup_paper (s, st.media[0] ? st.media : 0, st.orientation);
		ok = 1;
		if (pdf)
		{	// the PDF printer: the file to write
			char def[96]; scpy (def, sizeof def, di.title && di.title[0] ? di.title : "Document");
			for (int i = slen (def) - 1; i > 0; i--) if (def[i] == '.') { def[i] = 0; break; } else if (def[i] == '/') break;
			const char *base = def; for (const char *q = def; *q; q++) if (*q == '/' || *q == ':') base = q + 1;
			char name[96]; scpy (name, sizeof name, base); scat (name, sizeof name, ".pdf");
			ok = wk_file_save (s->output, sizeof s->output, "SD:/docs", name) ? 1 : 0;
		}
	}
	delete l;
	return ok;
}
