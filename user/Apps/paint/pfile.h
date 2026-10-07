//
// pfile.h -- Paint's files. Save writes the working format, **OpenRaster** (.ora -- the layered
// format GIMP, Krita and MyPaint share): a ZIP of each layer as a PNG, stack.xml (the layers from
// the top: name, visibility, opacity, blend mode -- composite-op svg:multiply, svg:screen, svg:plus,
// svg:lighten, svg:dst-in, svg:dst-out, krita:subtract; a clip mask: onyx:clip="true"), the merged
// picture and a thumbnail. Open reads it back, or any
// picture (PNG, JPEG, BMP, GIF, WebP, PCX: one layer). Export writes what is visible, flattened: PNG
// (its transparency kept), JPEG (quality 90), BMP, GIF (256 colours; the clear pixels transparent) --
// the format of the file's name.
//
#ifndef _paint_pfile_h
#define _paint_pfile_h

#include "imagekit/img/imgload.hpp"
#include "imagekit/img/pngsave.hpp"
#include "raster.h"

namespace pd {

static bool meq (const void *a, const void *b, int n) { for (int i = 0; i < n; i++) if (((const char *) a)[i] != ((const char *) b)[i]) return false; return true; }
static bool ends_with (const char *s, const char *e)
{
	int n = slen (s), k = slen (e);
	if (n < k) return false;
	for (int i = 0; i < k; i++) { int a = s[n - k + i], b = e[i]; if (a >= 'A' && a <= 'Z') a += 32; if (a != b) return false; }
	return true;
}

static unsigned char *read_all (const char *path, unsigned *len)
{
	void *f = kapi_open (path);
	if (!f) return 0;
	unsigned n = kapi_fsize (f);
	unsigned char *b = new unsigned char[n ? n : 1];
	int r = kapi_read (f, b, n);
	kapi_close (f);
	if (r < 0) { delete[] b; return 0; }
	*len = (unsigned) r;
	return b;
}

// The blend modes' names in OpenRaster (as GIMP and Krita write them).
static const char *const COMPOSITE_OPS[NBLENDS] = { "svg:src-over", "svg:multiply", "svg:screen", "svg:plus", "krita:subtract", "svg:lighten", "svg:dst-in", "svg:dst-out" };

// ---- stack.xml ----------------------------------------------------------------------------------------------
// An attribute's value in a tag [p, end) (entities &amp; &lt; &gt; &quot; &apos; read); false: none.
static bool xml_attr (const char *p, const char *end, const char *name, char *out, int cap)
{
	int nl = slen (name);
	for (const char *q = p; q + nl + 2 < end; q++)
	{
		if ((q == p || q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\n') && meq (q, name, nl) && q[nl] == '=' && (q[nl + 1] == '"' || q[nl + 1] == '\''))
		{
			char quote = q[nl + 1];
			const char *v = q + nl + 2;
			int n = 0;
			while (v < end && *v != quote && n < cap - 1)
			{
				if (*v == '&')
				{
					static const char *const ent[5] = { "&amp;", "&lt;", "&gt;", "&quot;", "&apos;" };
					static const char val[5] = { '&', '<', '>', '"', '\'' };
					int k = 0; for (; k < 5; k++) if (meq (v, ent[k], slen (ent[k]))) break;
					if (k < 5) { out[n++] = val[k]; v += slen (ent[k]); continue; }
				}
				out[n++] = *v++;
			}
			out[n] = 0;
			return true;
		}
	}
	return false;
}
static int parse_int (const char *s) { int v = 0; bool neg = *s == '-'; if (neg) s++; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return neg ? -v : v; }
// "0.75" -> 0..255
static int parse_opacity (const char *s)
{
	int ip = 0, frac = 0, div = 1;
	while (*s >= '0' && *s <= '9') ip = ip * 10 + (*s++ - '0');
	if (*s == '.') { s++; while (*s >= '0' && *s <= '9' && div < 10000) { frac = frac * 10 + (*s++ - '0'); div *= 10; } }
	int v = ((ip * div + frac) * 255 + div / 2) / div;
	return pclamp (v, 0, 255);
}

// ---- reading ---------------------------------------------------------------------------------------------------
// An OpenRaster file into the picture; false: not one.
static bool ora_load (const unsigned char *z, unsigned n)
{
	pngsave::ZipEntry e;
	if (!pngsave::zip_find (z, n, "stack.xml", &e)) return false;
	unsigned xl = 0;
	unsigned char *xml = e.method == 8 ? img_inflate (e.data, e.csize, false, &xl) : 0;
	if (e.method == 0) { xml = new unsigned char[e.usize + 1]; for (unsigned i = 0; i < e.usize; i++) xml[i] = e.data[i]; xl = e.usize; }
	if (!xml) return false;
	const char *x = (const char *) xml, *xe = x + xl;
	char v[256];
	const char *img = x; while (img + 6 < xe && !meq (img, "<image", 6)) img++;
	const char *imgEnd = img; while (imgEnd < xe && *imgEnd != '>') imgEnd++;
	int w = xml_attr (img, imgEnd, "w", v, sizeof v) ? parse_int (v) : 0, h = xml_attr (img, imgEnd, "h", v, sizeof v) ? parse_int (v) : 0;
	if (w <= 0 || h <= 0 || w > 16384 || h > 16384) { delete[] xml; return false; }
	// the layers, top first in the file: gathered, then put bottom first
	struct L { char name[32], src[128]; int x, y, op, blend; bool vis, clip; } *ls = new L[MAXLAYERS];
	int nl = 0;
	for (const char *q = imgEnd; q + 6 < xe && nl < MAXLAYERS; q++)
	{
		if (!meq (q, "<layer", 6) || (q[6] != ' ' && q[6] != '\t' && q[6] != '\n')) continue;
		const char *te = q; while (te < xe && *te != '>') te++;
		L &l = ls[nl];
		if (!xml_attr (q, te, "src", l.src, sizeof l.src)) { q = te; continue; }
		if (!xml_attr (q, te, "name", l.name, sizeof l.name)) scpy (l.name, "Layer", sizeof l.name);
		l.x = xml_attr (q, te, "x", v, sizeof v) ? parse_int (v) : 0;
		l.y = xml_attr (q, te, "y", v, sizeof v) ? parse_int (v) : 0;
		l.op = xml_attr (q, te, "opacity", v, sizeof v) ? parse_opacity (v) : 255;
		l.vis = !(xml_attr (q, te, "visibility", v, sizeof v) && meq (v, "hidden", 6));
		l.blend = GPC_B_NORMAL;
		if (xml_attr (q, te, "composite-op", v, sizeof v))
			for (int b = 0; b < NBLENDS; b++) if (slen (v) == slen (COMPOSITE_OPS[b]) && meq (v, COMPOSITE_OPS[b], slen (v))) l.blend = b;
		l.clip = xml_attr (q, te, "onyx:clip", v, sizeof v) && v[0] == 't';
		nl++;
		q = te;
	}
	delete[] xml;
	if (nl == 0) { delete[] ls; return false; }
	doc_new (w, h, false);
	D.n = 0;
	delete[] D.lay[0].px;
	for (int i = nl - 1; i >= 0; i--)
	{
		const L &l = ls[i];
		Layer &dl = D.lay[D.n++];
		layer_init (dl, l.name, new_px (w, h, 0));
		dl.visible = l.vis; dl.opacity = l.op; dl.blend = l.blend; dl.clip = l.clip;
		pngsave::ZipEntry pe;
		if (!pngsave::zip_find (z, n, l.src, &pe)) continue;
		unsigned pl = 0;
		unsigned char *png = pe.method == 8 ? img_inflate (pe.data, pe.csize, false, &pl) : 0;
		ImgFrames im;
		bool ok = png ? img_load_mem (png, pl, &im) : pe.method == 0 && img_load_mem (pe.data, pe.csize, &im);
		delete[] png;
		if (!ok) continue;
		for (int yy = 0; yy < im.h; yy++)
			for (int xx = 0; xx < im.w; xx++)
			{
				int X = l.x + xx, Y = l.y + yy;
				if (X >= 0 && Y >= 0 && X < w && Y < h) dl.px[(unsigned) Y * w + X] = im.px[0][(unsigned) yy * im.w + xx];
			}
		img_free (&im);
	}
	delete[] ls;
	D.cur = D.n - 1;
	return true;
}

// A picture (its first frame: one layer) or an OpenRaster file.
static bool doc_open (const char *path)
{
	unsigned n = 0;
	unsigned char *b = read_all (path, &n);
	if (!b) return false;
	bool ok = false;
	if (n > 4 && b[0] == 'P' && b[1] == 'K') ok = ora_load (b, n);
	else
	{
		ImgFrames im;
		if (img_load_mem (b, n, &im))
		{
			for (int i = 1; i < im.n; i++) delete[] im.px[i];
			doc_new (im.w, im.h, false);
			delete[] D.lay[0].px;
			D.lay[0].px = im.px[0];
			ok = true;
		}
	}
	delete[] b;
	if (ok) { undo_clear (); compose_all (); D.changes = 0; }
	return ok;
}

// ---- writing ---------------------------------------------------------------------------------------------------
static void xml_escaped (pngsave::Buf &o, const char *s)
{
	for (; *s; s++)
	{
		if (*s == '&') o.put ("&amp;", 5); else if (*s == '<') o.put ("&lt;", 4); else if (*s == '>') o.put ("&gt;", 4);
		else if (*s == '"') o.put ("&quot;", 6); else o.put ((unsigned char) *s);
	}
}
static void put_num (pngsave::Buf &o, int v) { char t[12]; int j = 0; if (v < 0) { o.put ('-'); v = -v; } do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j) o.put ((unsigned char) t[--j]); }
static void put_str (pngsave::Buf &o, const char *s) { o.put (s, (unsigned) slen (s)); }

// The visible layers flattened (a new buffer: the picture as it is shown, without what floats), with
// gpucomp's own blending: the file is what the screen showed.
static unsigned *flatten ()
{
	unsigned *o = new unsigned[(unsigned) D.w * D.h];
	for (int y = 0; y < D.h; y++) for (int x = 0; x < D.w; x++) o[(unsigned) y * D.w + x] = comp_px (x, y, false);
	return o;
}

// The picture as OpenRaster: the bytes (new []).
static unsigned char *ora_save (unsigned *len)
{
	pngsave::ZipOut z;
	z.add ("mimetype", "image/openraster", 16, false);
	pngsave::Buf x;
	put_str (x, "<?xml version='1.0' encoding='UTF-8'?>\n<image version=\"0.0.5\" w=\""); put_num (x, D.w);
	put_str (x, "\" h=\""); put_num (x, D.h); put_str (x, "\">\n <stack>\n");
	for (int k = D.n - 1; k >= 0; k--)
	{
		const Layer &l = D.lay[k];
		char src[40] = "data/layer"; int n = slen (src); int v = k; char t[8]; int j = 0; do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j) src[n++] = t[--j];
		src[n++] = '.'; src[n++] = 'p'; src[n++] = 'n'; src[n++] = 'g'; src[n] = 0;
		put_str (x, "  <layer name=\""); xml_escaped (x, l.name); put_str (x, "\" src=\""); put_str (x, src);
		put_str (x, "\" x=\"0\" y=\"0\" opacity=\"");
		int op = l.opacity * 1000 / 255; put_num (x, op / 1000); x.put ('.'); x.put ((unsigned char) ('0' + op / 100 % 10)); x.put ((unsigned char) ('0' + op / 10 % 10)); x.put ((unsigned char) ('0' + op % 10));
		put_str (x, "\" visibility=\""); put_str (x, l.visible ? "visible" : "hidden");
		put_str (x, "\" composite-op=\""); put_str (x, COMPOSITE_OPS[pclamp (l.blend, 0, NBLENDS - 1)]);
		if (l.clip) put_str (x, "\" onyx:clip=\"true");
		put_str (x, "\"/>\n");
		unsigned pl;
		unsigned char *png = pngsave::png_encode (l.px, D.w, D.h, true, &pl);
		z.add (src, png, pl, false);
		delete[] png;
	}
	put_str (x, " </stack>\n</image>\n");
	z.add ("stack.xml", x.b, x.n, true);
	// the merged picture, and a thumbnail (at most 256 px)
	unsigned *flat = flatten ();
	unsigned ml;
	unsigned char *merged = pngsave::png_encode (flat, D.w, D.h, true, &ml);
	z.add ("mergedimage.png", merged, ml, false);
	delete[] merged;
	int tw = D.w, th = D.h;
	if (tw > 256 || th > 256) { if (tw >= th) { th = pmax (1, th * 256 / tw); tw = 256; } else { tw = pmax (1, tw * 256 / th); th = 256; } }
	unsigned *small = scale (flat, D.w, D.h, tw, th, true);
	unsigned tl;
	unsigned char *thumb = pngsave::png_encode (small, tw, th, true, &tl);
	z.add ("Thumbnails/thumbnail.png", thumb, tl, false);
	delete[] thumb; delete[] small; delete[] flat;
	return z.finish (len);
}

// The visible picture in the format of the name's extension (.png, .jpg / .jpeg, .bmp, .gif).
static unsigned char *export_bytes (const char *path, unsigned *len)
{
	unsigned *flat = flatten ();
	unsigned char *b;
	if (ends_with (path, ".jpg") || ends_with (path, ".jpeg")) b = pngsave::jpeg_encode (flat, D.w, D.h, 90, len);
	else if (ends_with (path, ".bmp")) b = pngsave::bmp_encode (flat, D.w, D.h, len);
	else if (ends_with (path, ".gif")) b = pngsave::gif_encode (flat, D.w, D.h, len);
	else b = pngsave::png_encode (flat, D.w, D.h, true, len);
	delete[] flat;
	return b;
}

} // namespace pd

#endif
