//
// chart.h -- a chart drawn from its range: the series (a column or a row each: the first row / column
// may name them and the categories), the axes with round steps, the grid, the legend, the title; column,
// bar (horizontal), line, area (stacked or not), pie (the first series), scatter (the first column the x
// values). Anti-aliased shapes (wtk/vpaint.h), texts from the card's fonts.
//
#ifndef _sheet_chart_h
#define _sheet_chart_h

#include "render.h"

namespace ss {

static const unsigned SERIES[10] = { 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47, 0x264478, 0x9E480E, 0x636363, 0x997300 };
static const char *const CHART_NAMES[CH_COUNT] = { "Column", "Bar", "Line", "Area", "Pie", "Scatter" };

struct ChartData
{
	int ns, nc;						// series, categories
	char names[16][48];
	char cats[200][32];
	double v[16][200];
	bool ok[16][200];
	double xs[200];						// (scatter: the x values)
};
static void chart_data (Book &b, Chart *c, ChartData &d)
{
	memset (&d, 0, sizeof d);
	Sheet *s = book_sheet_by_id (b, c->srcSheet);
	if (!s || c->src.r1 < c->src.r0) return;
	Rect r = c->src;
	sheet_bounds (s);
	r.r1 = imin (r.r1, imax (r.r0, s->maxR)); r.c1 = imin (r.c1, imax (r.c0, s->maxC));
	bool rows = c->byRows;
	int s0 = rows ? r.r0 + (c->head ? 1 : 0) : r.c0 + (c->side ? 1 : 0), s1 = rows ? r.r1 : r.c1;
	int v0 = rows ? r.c0 + (c->side ? 1 : 0) : r.r0 + (c->head ? 1 : 0), v1 = rows ? r.c1 : r.r1;
	if (c->type == CH_SCATTER && !(rows ? c->head : c->side)) { if (rows) { v0 = r.c0; } else { v0 = r.r0; } }
	d.ns = imin (16, imax (0, s1 - s0 + 1));
	d.nc = imin (200, imax (0, v1 - v0 + 1));
	auto cellText = [&] (int rr, int cc, char *o, int cap) {
		Shown sh; cell_shown (b, s, s->cells.get (rr, cc), sh, 16); scpy (o, sh.text, cap); };
	for (int k = 0; k < d.ns; k++)
	{
		int sidx = s0 + k;
		if (rows ? c->side : c->head) cellText (rows ? sidx : r.r0, rows ? r.c0 : sidx, d.names[k], sizeof d.names[k]);
		else snprintf (d.names[k], sizeof d.names[k], "Series %d", k + 1);
		for (int i = 0; i < d.nc; i++)
		{
			Cell *x = rows ? s->cells.get (sidx, v0 + i) : s->cells.get (v0 + i, sidx);
			if (x && (x->vt == V_NUM || x->vt == V_BOOL)) { d.v[k][i] = x->num; d.ok[k][i] = true; }
		}
	}
	bool hasCats = rows ? c->head : c->side;
	for (int i = 0; i < d.nc; i++)
	{
		if (hasCats) cellText (rows ? r.r0 : v0 + i, rows ? v0 + i : r.c0, d.cats[i], sizeof d.cats[i]);
		else snprintf (d.cats[i], sizeof d.cats[i], "%d", i + 1);
		d.xs[i] = i + 1;
		if (hasCats) { Cell *x = rows ? s->cells.get (r.r0, v0 + i) : s->cells.get (v0 + i, r.c0); if (x && x->vt == V_NUM) d.xs[i] = x->num; }
	}
	if (c->type == CH_SCATTER && !hasCats && d.ns > 1)		// (no x column named: the first series gives the x)
	{
		for (int i = 0; i < d.nc; i++) { d.xs[i] = d.v[0][i]; snprintf (d.cats[i], sizeof d.cats[i], "%g", d.v[0][i]); }
		for (int k = 1; k < d.ns; k++) { memcpy (d.v[k - 1], d.v[k], sizeof d.v[k]); memcpy (d.ok[k - 1], d.ok[k], sizeof d.ok[k]); memcpy (d.names[k - 1], d.names[k], sizeof d.names[k]); }
		d.ns--;
	}
}

// Round steps for an axis from lo to hi: ~n ticks.
static void nice_axis (double lo, double hi, int n, double *a0, double *a1, double *step)
{
	if (hi <= lo) { hi = lo + 1; }
	double span = hi - lo, raw = span / imax (1, n);
	double mag = pow (10.0, floor (log10 (raw)));
	double f = raw / mag;
	double st = f <= 1 ? 1 : f <= 2 ? 2 : f <= 2.5 ? 2.5 : f <= 5 ? 5 : 10;
	st *= mag;
	*step = st;
	*a0 = floor (lo / st) * st;
	*a1 = ceil (hi / st) * st;
	if (*a1 <= *a0) *a1 = *a0 + st;
}
static void axis_label (double v, char *o, int cap)
{
	double a = fabs (v);
	if (a >= 1e6 || (a < 1e-3 && a > 0)) { snprintf (o, cap, "%.3g", v); return; }
	char t[40]; snprintf (t, sizeof t, "%.6f", v);
	int n = (int) strlen (t); while (n > 0 && t[n - 1] == '0') t[--n] = 0; if (n > 0 && t[n - 1] == '.') t[--n] = 0;
	if (!strcmp (t, "-0")) scpy (t, "0", sizeof t);
	// thousands' separators
	char *dot = strchr (t, '.'); int il = dot ? (int) (dot - t) : (int) strlen (t);
	int neg = t[0] == '-';
	Buf b; if (neg) b.put ('-');
	for (int i = neg; i < il; i++) { b.put (t[i]); int left = il - i - 1; if (left > 0 && left % 3 == 0) b.put (','); }
	if (dot) b.puts (dot);
	scpy (o, b.str (), cap);
}

static void text_at (Canvas &cv, fnt::Font *f, int x, int y, const char *s, unsigned c, int align, const Rect &clip)	// align 0 left, 1 centre, 2 right
{
	int n = (int) strlen (s), w = u8_width (f, s, n);
	int x64 = align == 1 ? x * 64 - w / 2 : align == 2 ? x * 64 - w : x * 64;
	u8_draw (cv, f, x64, y, s, n, c, clip.c0, clip.r0, clip.c1 + 1, clip.r1 + 1);
}

// The chart in the box (x, y, w, h) of the canvas, clipped to clip.
static void draw_chart (Canvas &cv, Book &b, Chart *c, int x, int y, int w, int h, int z, Rect clip)
{
	Rect cl = { imax (clip.r0, y), imax (clip.c0, x), imin (clip.r1, y + h - 1), imin (clip.c1, x + w - 1) };
	if (cl.r1 < cl.r0 || cl.c1 < cl.c0) return;
	// the frame
	for (int yy = cl.r0; yy <= cl.r1; yy++) for (int xx = cl.c0; xx <= cl.c1; xx++) cv.px[yy * cv.stride + xx] = 0xFFFFFF;
	hline (cv, x, x + w - 1, y, 0xBFBFBF, BS_THIN, cl); hline (cv, x, x + w - 1, y + h - 1, 0xBFBFBF, BS_THIN, cl);
	vline (cv, x, y, y + h - 1, 0xBFBFBF, BS_THIN, cl); vline (cv, x + w - 1, y, y + h - 1, 0xBFBFBF, BS_THIN, cl);
	int fam = g_famSans >= 0 ? g_famSans : 0;
	fnt::Font *ft = fnt::get (fam, fnt::BOLD, imax (8, 15 * z / 100) * 64), *fl = fnt::get (fam, 0, imax (7, 11 * z / 100) * 64);
	static ChartData d;
	chart_data (b, c, d);
	int pad = imax (6, 10 * z / 100);
	int top = y + pad, bottom = y + h - pad, left = x + pad, right = x + w - pad;
	unsigned ink = 0x404040, faint = 0x7F7F7F;
	if (c->title[0]) { text_at (cv, ft, x + w / 2, top + (ft->ascent >> 6), c->title, 0x262626, 1, cl); top += (ft->height >> 6) + pad / 2; }
	if (d.ns == 0 || d.nc == 0)
	{
		text_at (cv, fl, x + w / 2, y + h / 2, "(no data)", faint, 1, cl);
		return;
	}
	int lh = (fl->height >> 6);
	// the legend
	bool pie = c->type == CH_PIE;
	int nleg = pie ? d.nc : d.ns;
	if (c->legend != LG_NONE && nleg > 0)
	{
		int sw = imax (6, 9 * z / 100);
		if (c->legend == LG_RIGHT)
		{
			int lw = 0;
			for (int i = 0; i < nleg; i++) { const char *t = pie ? d.cats[i] : d.names[i]; lw = imax (lw, u8_width (fl, t, (int) strlen (t)) >> 6); }
			lw = imin (lw + sw + 8, w / 3);
			int ly = top + (bottom - top - nleg * (lh + 2)) / 2;
			for (int i = 0; i < nleg; i++)
			{
				unsigned col = SERIES[i % 10];
				int yy = ly + i * (lh + 2);
				VPath p; p.rect (V (right - lw), V (yy + (lh - sw) / 2), V (sw), V (sw)); p.fill (cv, col);
				text_at (cv, fl, right - lw + sw + 5, yy + (fl->ascent >> 6), pie ? d.cats[i] : d.names[i], ink, 0, Rect { cl.r0, cl.c0, cl.r1, right });
			}
			right -= lw + pad;
		}
		else
		{
			int tot = 0;
			for (int i = 0; i < nleg; i++) { const char *t = pie ? d.cats[i] : d.names[i]; tot += (u8_width (fl, t, (int) strlen (t)) >> 6) + sw + 14; }
			int lx = x + imax (pad, (w - tot) / 2);
			int yy = c->legend == LG_TOP ? top : bottom - lh;
			for (int i = 0; i < nleg; i++)
			{
				const char *t = pie ? d.cats[i] : d.names[i];
				VPath p; p.rect (V (lx), V (yy + (lh - sw) / 2), V (sw), V (sw)); p.fill (cv, SERIES[i % 10]);
				text_at (cv, fl, lx + sw + 4, yy + (fl->ascent >> 6), t, ink, 0, cl);
				lx += (u8_width (fl, t, (int) strlen (t)) >> 6) + sw + 14;
			}
			if (c->legend == LG_TOP) top += lh + pad / 2; else bottom -= lh + pad / 2;
		}
	}
	if (pie)
	{
		double tot = 0; for (int i = 0; i < d.nc; i++) if (d.ok[0][i] && d.v[0][i] > 0) tot += d.v[0][i];
		int cx = (left + right) / 2, cy = (top + bottom) / 2, rad = imax (4, imin (right - left, bottom - top) / 2 - 4);
		if (tot <= 0) return;
		double a = 90;						// (from 12 o'clock, clockwise)
		for (int i = 0; i < d.nc; i++)
		{
			if (!d.ok[0][i] || d.v[0][i] <= 0) continue;
			double sweep = d.v[0][i] / tot * 360;
			VPath p;
			int pts[2 * 100]; int np = 0;
			pts[np++] = V (cx); pts[np++] = V (cy);
			int steps = imax (2, (int) (sweep / 4));
			for (int k = 0; k <= steps && np < 198; k++)
			{
				double ang = (a - sweep * k / steps) * M_PI / 180;
				pts[np++] = V (cx) + (int) (cos (ang) * rad * 16); pts[np++] = V (cy) - (int) (sin (ang) * rad * 16);
			}
			p.poly (pts, np / 2); p.fill (cv, SERIES[i % 10]);
			p.clear (); p.polyline (pts, np / 2, 20, true); p.fill (cv, 0xFFFFFF);
			// the share, in the slice
			if (sweep > 12)
			{
				double mid = (a - sweep / 2) * M_PI / 180;
				char t[16]; snprintf (t, sizeof t, "%.0f%%", d.v[0][i] / tot * 100);
				text_at (cv, fl, cx + (int) (cos (mid) * rad * 0.65), cy - (int) (sin (mid) * rad * 0.65) + (fl->ascent >> 7), t, 0xFFFFFF, 1, cl);
			}
			a -= sweep;
		}
		return;
	}
	// the value range (stacked: the sums)
	double lo = 0, hi = 0; bool any = false;
	bool stack = c->stacked && c->type != CH_SCATTER;
	for (int i = 0; i < d.nc; i++)
	{
		double pos = 0, neg = 0;
		for (int k = 0; k < d.ns; k++)
		{
			if (!d.ok[k][i]) continue;
			double v = d.v[k][i];
			if (stack) { if (v >= 0) pos += v; else neg += v; }
			else { if (!any || v < lo) lo = v; if (!any || v > hi) hi = v; any = true; }
		}
		if (stack) { if (!any || neg < lo) lo = neg; if (!any || pos > hi) hi = pos; any = true; }
	}
	if (lo > 0) lo = 0;
	if (hi < 0) hi = 0;
	if (lo < 0 && -lo < (hi - lo) * 1e-9) lo = 0;		// (a sum's rounding left, -3.6E-12: not below 0)
	if (hi > 0 && hi < (hi - lo) * 1e-9) hi = 0;
	double a0, a1, step;
	nice_axis (lo, hi, 5, &a0, &a1, &step);
	bool horiz = c->type == CH_BAR;
	// the x values of a scatter
	double xlo = 0, xhi = 1, xs0 = 0, xs1 = 1, xstep = 1;
	if (c->type == CH_SCATTER)
	{
		xlo = xhi = d.xs[0];
		for (int i = 0; i < d.nc; i++) { if (d.xs[i] < xlo) xlo = d.xs[i]; if (d.xs[i] > xhi) xhi = d.xs[i]; }
		nice_axis (xlo, xhi, 5, &xs0, &xs1, &xstep);
	}
	// the labels' room
	char lab[48];
	int vw = 0;
	for (double v = a0; v <= a1 + step / 2; v += step) { axis_label (v, lab, sizeof lab); vw = imax (vw, u8_width (fl, lab, (int) strlen (lab)) >> 6); }
	int catH = lh + 4;
	int px0, py0, px1, py1;					// the plot area
	if (!horiz) { px0 = left + vw + 6; px1 = right; py0 = top + lh / 2; py1 = bottom - catH; }
	else
	{
		int cw = 0; for (int i = 0; i < d.nc; i++) cw = imax (cw, u8_width (fl, d.cats[i], (int) strlen (d.cats[i])) >> 6);
		cw = imin (cw, (right - left) / 3);
		px0 = left + cw + 6; px1 = right - vw / 2; py0 = top; py1 = bottom - catH;
	}
	if (px1 - px0 < 20 || py1 - py0 < 20) return;
	auto vpos = [&] (double v) -> int {			// a value's place along the value axis
		double t = (v - a0) / (a1 - a0);
		return horiz ? px0 + (int) (t * (px1 - px0)) : py1 - (int) (t * (py1 - py0)); };
	// the grid and the value axis' labels
	for (double v = a0; v <= a1 + step / 2; v += step)
	{
		int p = vpos (v);
		axis_label (v, lab, sizeof lab);
		if (!horiz)
		{
			if (c->grid || v == 0) hline (cv, px0, px1, p, v == 0 ? 0xA6A6A6 : 0xE3E3E3, BS_THIN, cl);
			text_at (cv, fl, px0 - 4, p + (fl->ascent >> 7), lab, faint, 2, cl);
		}
		else
		{
			if (c->grid || v == 0) vline (cv, p, py0, py1, v == 0 ? 0xA6A6A6 : 0xE3E3E3, BS_THIN, cl);
			text_at (cv, fl, p, py1 + 2 + (fl->ascent >> 6), lab, faint, 1, cl);
		}
	}
	if (c->type == CH_SCATTER)
	{
		for (double v = xs0; v <= xs1 + xstep / 2; v += xstep)
		{
			int p = px0 + (int) ((v - xs0) / (xs1 - xs0) * (px1 - px0));
			axis_label (v, lab, sizeof lab);
			if (c->grid) vline (cv, p, py0, py1, 0xE3E3E3, BS_THIN, cl);
			text_at (cv, fl, p, py1 + 2 + (fl->ascent >> 6), lab, faint, 1, cl);
		}
	}
	// the category axis
	if (!horiz) hline (cv, px0, px1, py1, 0xA6A6A6, BS_THIN, cl); else vline (cv, px0, py0, py1, 0xA6A6A6, BS_THIN, cl);
	int n = d.nc;
	double band = (double) (horiz ? py1 - py0 : px1 - px0) / n;
	if (c->type != CH_SCATTER)
	{
		int every = 1;
		int maxw = 0; for (int i = 0; i < n; i++) maxw = imax (maxw, u8_width (fl, d.cats[i], (int) strlen (d.cats[i])) >> 6);
		if (!horiz) while (every < n && band * every < maxw + 6) every++;
		else while (every < n && band * every < lh) every++;
		for (int i = 0; i < n; i += every)
		{
			int mid = (int) ((horiz ? py0 : px0) + band * (i + 0.5));
			if (!horiz) text_at (cv, fl, mid, py1 + 2 + (fl->ascent >> 6), d.cats[i], faint, 1, cl);
			else text_at (cv, fl, px0 - 4, mid + (fl->ascent >> 7), d.cats[i], faint, 2, Rect { cl.r0, x + 2, cl.r1, px0 - 2 });
		}
	}
	// the series
	VPath p;
	if (c->type == CH_COLUMN || c->type == CH_BAR)
	{
		double gw = band * 0.7;
		for (int i = 0; i < n; i++)
		{
			double pos = 0, neg = 0;
			for (int k = 0; k < d.ns; k++)
			{
				if (!d.ok[k][i]) continue;
				double v = d.v[k][i], from, to;
				if (stack) { if (v >= 0) { from = pos; pos += v; to = pos; } else { from = neg; neg += v; to = neg; } }
				else { from = 0; to = v; }
				int e0 = vpos (from), e1 = vpos (to);
				double slot = stack ? gw : gw / d.ns;
				double s0 = (horiz ? py0 : px0) + band * i + (band - gw) / 2 + (stack ? 0 : slot * k);
				int a = (int) (s0 * 16), bl = imax (16, (int) (slot * 16) - (stack || d.ns == 1 ? 0 : 16));
				p.clear ();
				if (!horiz) p.rect (a, V (imin (e0, e1)), bl, V (imax (1, abs (e1 - e0))));
				else p.rect (V (imin (e0, e1)), a, V (imax (1, abs (e1 - e0))), bl);
				p.fill (cv, SERIES[k % 10]);
			}
		}
	}
	else if (c->type == CH_LINE || c->type == CH_AREA || c->type == CH_SCATTER)
	{
		static double base[200];
		for (int i = 0; i < n; i++) base[i] = 0;
		for (int k = 0; k < d.ns; k++)
		{
			int pts[2 * 202]; int np = 0;
			for (int i = 0; i < n; i++)
			{
				if (!d.ok[k][i] && c->type != CH_AREA) continue;
				double v = (d.ok[k][i] ? d.v[k][i] : 0) + (stack ? base[i] : 0);
				int px = c->type == CH_SCATTER ? px0 + (int) ((d.xs[i] - xs0) / (xs1 - xs0) * (px1 - px0)) : (int) (px0 + band * (i + 0.5));
				pts[np++] = V (px); pts[np++] = V (vpos (v));
			}
			if (np < 2) continue;
			unsigned col = SERIES[k % 10];
			if (c->type == CH_AREA)
			{
				int poly[2 * 404]; int m = 0;
				for (int i = 0; i < np; i++) poly[m++] = pts[i];
				for (int i = n - 1; i >= 0; i--) { poly[m++] = V ((int) (px0 + band * (i + 0.5))); poly[m++] = V (vpos (stack ? base[i] : 0)); }
				p.clear (); p.poly (poly, m / 2); p.fill (cv, col, stack ? 230 : 150);
			}
			if (c->type != CH_SCATTER) { p.clear (); p.polyline (pts, np / 2, imax (24, 36 * z / 100)); p.fill (cv, col); }
			if (c->type != CH_AREA) { p.clear (); for (int i = 0; i < np; i += 2) p.circle (pts[i], pts[i + 1], V (imax (2, 3 * z / 100))); p.fill (cv, col); }
			if (stack) for (int i = 0; i < n; i++) if (d.ok[k][i]) base[i] += d.v[k][i];
		}
	}
}

} // namespace ss

#endif
