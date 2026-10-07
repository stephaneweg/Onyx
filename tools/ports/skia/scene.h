// scene.h -- the scene of Onyx's Skia smoke programs (skiatest: into a PNG, checked; skiademo: into a window),
// drawn the way WebKit draws with Skia: paths, gradients, a blur, a dash, images decoded by Skia's codecs,
// and text shaped by HarfBuzz (the face read from the SkTypeface's tables, ICU's Unicode functions, as WebKit's
// Skia port does) then drawn as an SkTextBlob of glyph ids and positions. Header-only, one translation unit.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is hereby granted, free
// of charge, to any person obtaining a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice
// and this permission notice shall be included in all copies or substantial portions of the Software. THE
// SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
#pragma once
#include "include/codec/SkCodec.h"
#include "include/codec/SkJpegDecoder.h"
#include "include/codec/SkPngDecoder.h"
#include "include/codec/SkWebpDecoder.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkImage.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkMilestone.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTextBlob.h"
#include "include/core/SkTypeface.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradient.h"
#include "include/effects/SkImageFilters.h"
#include "include/encode/SkJpegEncoder.h"
#include "include/encode/SkPngEncoder.h"
#include "include/encode/SkWebpEncoder.h"
#include "include/ports/SkFontMgr_onyx.h"
#include <hb.h>
#ifndef SCENE_NO_ICU
#include <hb-icu.h>		// ICU's Unicode functions, as WebKit (skiatest); skiademo: HarfBuzz's own, no ICU data
#endif
#include <math.h>
#include <string.h>
#include <string>
#include <vector>

namespace scene {

// ---- gradients (Skia's SkGradient API: SkColor4f spans) ----
static SkGradient grad (std::vector<SkColor4f> &store, std::initializer_list<SkColor> cs)
{
	store.clear ();
	for (SkColor c : cs)
		store.push_back (SkColor4f::FromColor (c));
	return SkGradient (SkGradient::Colors (SkSpan<const SkColor4f> (store.data (), store.size ()), SkTileMode::kClamp), {});
}

static sk_sp<SkShader> linear (SkPoint a, SkPoint b, std::initializer_list<SkColor> cs)
{
	std::vector<SkColor4f> st;
	const SkPoint pts[2] = { a, b };
	return SkShaders::LinearGradient (pts, grad (st, cs));
}

static sk_sp<SkShader> radial (SkPoint c, float r, std::initializer_list<SkColor> cs)
{
	std::vector<SkColor4f> st;
	return SkShaders::RadialGradient (c, r, grad (st, cs));
}

static sk_sp<SkShader> sweep (SkPoint c, std::initializer_list<SkColor> cs)
{
	std::vector<SkColor4f> st;
	return SkShaders::SweepGradient (c, grad (st, cs));
}

// ---- text: HarfBuzz over an SkTypeface (WebKit's way: hb_face_create_for_tables + SkTypeface tables) ----
static hb_blob_t *table_of (hb_face_t *, hb_tag_t tag, void *user)
{
	SkTypeface *tf = (SkTypeface *) user;
	sk_sp<SkData> d = tf->copyTableData (tag);
	if (!d)
		return nullptr;
	SkData *raw = d.release ();
	return hb_blob_create ((const char *) raw->data (), (unsigned) raw->size (), HB_MEMORY_MODE_READONLY, raw,
			       [] (void *p) { ((SkData *) p)->unref (); });
}

struct Shaped {
	std::vector<SkGlyphID> glyphs;
	std::vector<SkPoint> pos;
	float width = 0;
	hb_direction_t dir = HB_DIRECTION_LTR;
	hb_script_t script = HB_SCRIPT_UNKNOWN;
	unsigned chars = 0;
};

static Shaped shape (const sk_sp<SkTypeface> &tf, float size, const char *utf8)
{
	Shaped s;
	hb_face_t *face = hb_face_create_for_tables (table_of, SkRef (tf.get ()), [] (void *p) { ((SkTypeface *) p)->unref (); });
	hb_face_set_upem (face, tf->getUnitsPerEm ());
	hb_font_t *font = hb_font_create (face);
	int scale = (int) (size * 64);
	hb_font_set_scale (font, scale, scale);
	hb_buffer_t *buf = hb_buffer_create ();
#ifndef SCENE_NO_ICU
	hb_buffer_set_unicode_funcs (buf, hb_icu_get_unicode_funcs ());
#endif
	hb_buffer_add_utf8 (buf, utf8, -1, 0, -1);
	s.chars = hb_buffer_get_length (buf);
	hb_buffer_guess_segment_properties (buf);
	s.dir = hb_buffer_get_direction (buf);
	s.script = hb_buffer_get_script (buf);
	hb_shape (font, buf, nullptr, 0);
	unsigned n;
	hb_glyph_info_t *info = hb_buffer_get_glyph_infos (buf, &n);
	hb_glyph_position_t *pos = hb_buffer_get_glyph_positions (buf, nullptr);
	float x = 0;
	for (unsigned i = 0; i < n; i++) {
		s.glyphs.push_back ((SkGlyphID) info[i].codepoint);
		s.pos.push_back (SkPoint::Make (x + pos[i].x_offset / 64.f, -pos[i].y_offset / 64.f));
		x += pos[i].x_advance / 64.f;
	}
	s.width = x;
	hb_buffer_destroy (buf);
	hb_font_destroy (font);
	hb_face_destroy (face);
	return s;
}

static void draw_text (SkCanvas *c, const sk_sp<SkTypeface> &tf, float size, const Shaped &s, float x, float y, SkColor color)
{
	SkFont font (tf, size);
	font.setEdging (SkFont::Edging::kAntiAlias);
	font.setSubpixel (true);
	SkTextBlobBuilder b;
	const auto &run = b.allocRunPos (font, (int) s.glyphs.size ());
	memcpy (run.glyphs, s.glyphs.data (), s.glyphs.size () * sizeof (SkGlyphID));
	memcpy (run.points (), s.pos.data (), s.pos.size () * sizeof (SkPoint));
	SkPaint p;
	p.setAntiAlias (true);
	p.setColor (color);
	c->drawTextBlob (b.make (), x, y, p);
}

// ---- images: a 96x96 test picture, encoded by Skia (PNG / JPEG / WebP), decoded back by Skia's codecs ----
static SkBitmap test_picture ()
{
	SkBitmap bm;
	bm.allocN32Pixels (96, 96);
	SkCanvas c (bm);
	c.clear (SK_ColorWHITE);
	SkPaint p;
	p.setAntiAlias (true);
	p.setShader (linear ({ 0, 0 }, { 96, 96 }, { 0xffe63946, 0xfff1fa8c, 0xff2a9d8f }));
	c.drawRect (SkRect::MakeWH (96, 96), p);
	p.setShader (nullptr);
	p.setColor (0xff1d3557);
	for (int i = 0; i < 4; i++)
		c.drawRect (SkRect::MakeXYWH (8 + i * 22, 8 + i * 22, 14, 14), p);
	p.setColor (0xccffffff);
	c.drawCircle (60, 36, 20, p);
	return bm;
}

struct Decoded {
	const char *name;
	size_t bytes = 0;
	sk_sp<SkImage> image;
	double meanDiff = -1;		// against the original, per channel
};

static double mean_diff (const SkBitmap &a, const SkPixmap &b)
{
	if (a.width () != b.width () || a.height () != b.height ())
		return 1e9;
	double sum = 0;
	for (int y = 0; y < a.height (); y++)
		for (int x = 0; x < a.width (); x++) {
			SkColor ca = a.getColor (x, y), cb = b.getColor (x, y);
			sum += abs ((int) SkColorGetR (ca) - (int) SkColorGetR (cb)) + abs ((int) SkColorGetG (ca) - (int) SkColorGetG (cb)) +
			       abs ((int) SkColorGetB (ca) - (int) SkColorGetB (cb));
		}
	return sum / (3.0 * a.width () * a.height ());
}

static Decoded round_trip (const SkBitmap &src, int kind)
{
	Decoded d;
	SkPixmap pm;
	src.peekPixels (&pm);
	sk_sp<SkData> data;
	std::unique_ptr<SkCodec> codec;
	if (kind == 0) {
		d.name = "PNG";
		data = SkPngEncoder::Encode (pm, {});
		if (data)
			codec = SkPngDecoder::Decode (data, nullptr);
	} else if (kind == 1) {
		d.name = "JPEG";
		SkJpegEncoder::Options o;
		o.fQuality = 90;
		data = SkJpegEncoder::Encode (pm, o);
		if (data)
			codec = SkJpegDecoder::Decode (data, nullptr);
	} else {
		d.name = "WebP";
		SkWebpEncoder::Options o;
		o.fCompression = SkWebpEncoder::Compression::kLossy;
		o.fQuality = 90;
		data = SkWebpEncoder::Encode (pm, o);
		if (data)
			codec = SkWebpDecoder::Decode (data, nullptr);
	}
	if (!data || !codec)
		return d;
	d.bytes = data->size ();
	SkImageInfo info = codec->getInfo ().makeColorType (kN32_SkColorType).makeAlphaType (kPremul_SkAlphaType);
	SkBitmap out;
	out.allocPixels (info);
	if (codec->getPixels (info, out.getPixels (), out.rowBytes ()) != SkCodec::kSuccess)
		return d;
	SkPixmap opm;
	out.peekPixels (&opm);
	d.meanDiff = mean_diff (src, opm);
	out.setImmutable ();
	d.image = out.asImage ();
	return d;
}

// ---- the scene ----
struct Result {
	int families = 0;
	bool haveSans = false, haveBold = false;
	Shaped latin, arabic, title;
	Decoded images[3];
};

static sk_sp<SkTypeface> find_face (const sk_sp<SkFontMgr> &mgr, const char *family, SkFontStyle style)
{
	sk_sp<SkTypeface> tf = mgr->matchFamilyStyle (family, style);
	return tf;
}

static Result draw (SkCanvas *c, int W, int H, const sk_sp<SkFontMgr> &mgr)
{
	Result r;
	r.families = mgr ? mgr->countFamilies () : 0;
	sk_sp<SkTypeface> sans = mgr ? find_face (mgr, "sans-serif", SkFontStyle::Normal ()) : nullptr;
	sk_sp<SkTypeface> bold = mgr ? find_face (mgr, "sans-serif", SkFontStyle::Bold ()) : nullptr;
	sk_sp<SkTypeface> serif = mgr ? find_face (mgr, "Times New Roman", SkFontStyle::Normal ()) : nullptr;
	r.haveSans = sans != nullptr;
	r.haveBold = bold != nullptr && bold->isBold ();
	if (!bold)
		bold = sans;
	if (!serif)
		serif = sans;

	// background: a diagonal linear gradient
	SkPaint bg;
	bg.setShader (linear ({ 0, 0 }, { (float) W, (float) H }, { 0xff1d3557, 0xff457b9d }));
	c->drawRect (SkRect::MakeWH (W, H), bg);

	float s = W / 800.f;		// the layout is made for 800 x 600, scaled
	c->save ();
	c->scale (s, s);

	// a card with a drop shadow (a blur image filter)
	SkPaint card;
	card.setAntiAlias (true);
	card.setColor (0xfff1faee);
	card.setImageFilter (SkImageFilters::DropShadow (0, 8, 10, 10, 0x80000000, nullptr));
	c->drawRRect (SkRRect::MakeRectXY (SkRect::MakeXYWH (30, 300, 740, 270), 18, 18), card);

	// a star, a radial gradient fill and a round-joined stroke
	SkPathBuilder star;
	for (int i = 0; i < 10; i++) {
		float a = (float) (M_PI / 2 + i * M_PI / 5), rad = (i & 1) ? 42.f : 100.f;
		SkPoint p = { 150 + rad * cosf (a), 180 - rad * sinf (a) };
		if (i == 0)
			star.moveTo (p);
		else
			star.lineTo (p);
	}
	star.close ();
	SkPath starPath = star.detach ();
	SkPaint fill;
	fill.setAntiAlias (true);
	fill.setShader (radial ({ 150, 180 }, 100, { 0xffffd166, 0xffef476f }));
	c->drawPath (starPath, fill);
	SkPaint stroke;
	stroke.setAntiAlias (true);
	stroke.setStyle (SkPaint::kStroke_Style);
	stroke.setStrokeWidth (5);
	stroke.setStrokeJoin (SkPaint::kRound_Join);
	stroke.setColor (SK_ColorWHITE);
	c->drawPath (starPath, stroke);

	// a colour wheel: a sweep gradient
	SkPaint wheel;
	wheel.setAntiAlias (true);
	wheel.setShader (sweep ({ 660, 170 }, { 0xffff0000, 0xffffff00, 0xff00ff00, 0xff00ffff, 0xff0000ff, 0xffff00ff, 0xffff0000 }));
	c->drawCircle (660, 170, 80, wheel);
	SkPaint hole;
	hole.setAntiAlias (true);
	hole.setColor (0xff1d3557);
	c->drawCircle (660, 170, 34, hole);

	// a dashed cubic curve
	SkPath curve = SkPathBuilder ().moveTo (290, 250).cubicTo (360, 60, 460, 300, 540, 90).detach ();
	SkPaint dash;
	dash.setAntiAlias (true);
	dash.setStyle (SkPaint::kStroke_Style);
	dash.setStrokeWidth (6);
	dash.setStrokeCap (SkPaint::kRound_Cap);
	dash.setColor (0xffa8dadc);
	const float iv[] = { 18, 12 };
	dash.setPathEffect (SkDashPathEffect::Make (iv, 0));
	c->drawPath (curve, dash);

	// the title
	if (bold) {
		r.title = shape (bold, 34, "Onyx · Skia m154 + HarfBuzz");
		draw_text (c, bold, 34, r.title, 400 - r.title.width / 2, 50, SK_ColorWHITE);
	}
	// text in the card: Latin (ligatures, kerning), Arabic (right to left, joined), Cyrillic / Greek
	if (sans) {
		r.latin = shape (sans, 30, "office, fluffy — AVAWAY Tokyo");	// DejaVu Sans: ffi / fl / ff ligatures
		draw_text (c, sans, 30, r.latin, 60, 350, 0xff1d3557);
		Shaped cg = shape (serif, 26, "Привет, мир · Γειά σου κόσμε");	// "Times New Roman": Liberation Serif
		draw_text (c, serif, 26, cg, 60, 392, 0xff1d3557);
		r.arabic = shape (sans, 30, "مرحبا بالعالم");
		draw_text (c, sans, 30, r.arabic, 740 - r.arabic.width, 350, 0xffe63946);
	}
	// images: PNG, JPEG, WebP round trips, drawn with their labels
	SkBitmap pic = test_picture ();
	for (int k = 0; k < 3; k++) {
		r.images[k] = round_trip (pic, k);
		float x = 60 + k * 130;
		if (r.images[k].image)
			c->drawImageRect (r.images[k].image, SkRect::MakeXYWH (x, 420, 96, 96), SkSamplingOptions (SkFilterMode::kLinear));
		if (sans) {
			Shaped lab = shape (sans, 16, r.images[k].name);
			draw_text (c, sans, 16, lab, x + 48 - lab.width / 2, 540, 0xff1d3557);
		}
	}
	// a blurred, translucent circle over the card's corner (a mask filter)
	SkPaint glow;
	glow.setAntiAlias (true);
	glow.setColor (0x806a4c93);
	glow.setMaskFilter (SkMaskFilter::MakeBlur (kNormal_SkBlurStyle, 12));
	c->drawCircle (620, 480, 60, glow);
	c->restore ();
	return r;
}

}  // namespace scene
