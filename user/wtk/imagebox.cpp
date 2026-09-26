//
// wtk/imagebox.cpp -- ImageBox (see imagebox.h). load () uses img/imgload.hpp (the
// codecs in wtk/imgload.cpp), so only apps that call it link them in.
//
#include "wtk/imagebox.h"
#include "img/imgload.hpp"

namespace wtk {

ImageBox::ImageBox (int l, int t, int w, int h, int stretch_, unsigned bg_)
  : Widget (l, t, w, h), stretch (stretch_), grow (false), bg (bg_), m_px (0), m_w (0), m_h (0) {}

ImageBox::~ImageBox () { delete [] m_px; }

void ImageBox::clearImage () { delete [] m_px; m_px = 0; m_w = m_h = 0; invalidate (true); }

bool ImageBox::load (const char *path)
{
	ImgFrames im;
	clearImage ();
	if (!img_load (path, &im)) return false;
	m_px = im.px[0]; m_w = im.w; m_h = im.h;		// keep the first frame
	for (int i = 1; i < im.n; i++) delete [] im.px[i];
	invalidate (true);
	return true;
}

void ImageBox::setPixels (const unsigned *px, int w, int h, bool hasAlpha)
{
	clearImage ();
	if (px == 0 || w <= 0 || h <= 0) return;
	m_px = new unsigned[(unsigned long) w * h];
	for (long i = 0; i < (long) w * h; i++) m_px[i] = hasAlpha ? px[i] : (px[i] | 0xFF000000u);
	m_w = w; m_h = h;
	invalidate (true);
}

void ImageBox::onDraw ()
{
	canvas.clear (bg);
	if (m_px == 0) return;
	int dw = m_w, dh = m_h;
	if (stretch == IMG_FILL) { dw = width; dh = height; }
	else if (stretch == IMG_FIT)
	{
		// Uniform: the largest size that fits (in 1/1024 steps), not beyond 1:1 unless grow.
		long sx = (long) width * 1024 / m_w, sy = (long) height * 1024 / m_h, s = sx < sy ? sx : sy;
		if (!grow && s > 1024) s = 1024;
		dw = (int) (m_w * s / 1024); dh = (int) (m_h * s / 1024);
		if (dw < 1) dw = 1;
		if (dh < 1) dh = 1;
	}
	int ox = (width - dw) / 2, oy = (height - dh) / 2;
	for (int y = 0; y < dh; y++)
	{
		int py = oy + y; if (py < 0 || py >= height) continue;
		int sy = (int) ((long) y * m_h / dh);
		for (int x = 0; x < dw; x++)
		{
			int px = ox + x; if (px < 0 || px >= width) continue;
			unsigned c = m_px[(long) sy * m_w + (long) x * m_w / dw], a = c >> 24;
			if (a == 0) continue;
			if (a != 255)
			{
				unsigned r = (((c >> 16) & 255) * a + ((bg >> 16) & 255) * (255 - a)) / 255;
				unsigned g = (((c >> 8) & 255) * a + ((bg >> 8) & 255) * (255 - a)) / 255;
				unsigned b = ((c & 255) * a + (bg & 255) * (255 - a)) / 255;
				c = (r << 16) | (g << 8) | b;
			}
			canvas.pixel (px, py, c & 0xFFFFFF);
		}
	}
}

} // namespace wtk
