//
// print/pdfsink.h -- a print job's pages as a PDF (print/job.h replayed into pdf/pdfwrite.h): what the PDF
// printer does. The text stays text (the fonts embedded as subsets), the shapes stay shapes.
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
#ifndef ONYX_PRINT_PDFSINK_H
#define ONYX_PRINT_PDFSINK_H

#include "print/job.h"
#include "pdf/pdfwrite.h"
#include "img/imgload.hpp"

namespace pjob {

class PdfSink : public Sink
{
public:
	pdfw::Writer w;
	PdfSink () { for (int i = 0; i < 64; i++) m_font[i] = -1; }

	void font (int id, const unsigned char *ttf, unsigned len) { if (id >= 0 && id < 64) m_font[id] = w.add_font (ttf, len); }
	void begin_page (float pw, float ph) { w.begin_page (pw, ph); }
	void rect (float x, float y, float rw, float rh, unsigned rgb) { w.fill_rect (x, y, rw, rh, rgb); }
	void glyphs (int font, unsigned style, float size, unsigned rgb, const Glyph *g, int n)
	{
		int f = font >= 0 && font < 64 ? m_font[font] : -1;
		for (int i = 0; i < n; i++) w.glyph (f, size, g[i].x, g[i].y, g[i].gid, g[i].uni, rgb, (style & ST_BOLD) != 0, (style & ST_ITALIC) != 0);
	}
	void image (float x, float y, float iw, float ih, int pw, int ph, int kind, const unsigned char *data, unsigned len)
	{
		if (kind == IMG_JPEG) { w.image_jpeg (data, len, pw, ph, x, y, iw, ih); return; }
		unsigned n = 0;
		unsigned char *raw = img_inflate (data, len, true, &n);
		if (raw && n >= (unsigned) pw * ph * 4) w.image ((const unsigned *) raw, pw, ph, x, y, iw, ih);
		delete[] raw;
	}
	void path (const PathPt *pt, int n, unsigned rgb, float strokeW)
	{
		float *xy = new float[n * 2]; unsigned char *v = new unsigned char[n];
		for (int i = 0; i < n; i++) { xy[i * 2] = pt[i].x; xy[i * 2 + 1] = pt[i].y; v[i] = (unsigned char) pt[i].verb; }
		// (job.h marks each of a curve's three points; pdfwrite wants the mark on the first)
		w.path (xy, v, n, rgb, strokeW);
		delete[] xy; delete[] v;
	}
	void end_page () { w.end_page (); }

private:
	int m_font[64];
};

} // namespace pjob

#endif
