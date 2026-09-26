//
// wtk/imagebox.h -- ImageBox: shows an image (WPF Image). load (path) reads BMP / GIF /
// PNG / JPEG / PCX / WebP (img/imgload.hpp -- first frame), or setPixels () takes
// 0x(AA)RRGGBB pixels (copied). stretch: IMG_NONE (1:1, centred, clipped), IMG_FIT
// (uniform, never enlarged beyond 1:1 unless `grow`), IMG_FILL (stretched to the box).
// Transparent pixels show the box background.
//
#ifndef _wtk_imagebox_h
#define _wtk_imagebox_h

#include "wtk/widget.h"

namespace wtk {

enum { IMG_NONE, IMG_FIT, IMG_FILL };

class ImageBox : public Widget
{
public:
	int stretch; bool grow; unsigned bg;
	ImageBox (int l, int t, int w, int h, int stretch_ = IMG_FIT, unsigned bg_ = C_BG);
	~ImageBox ();
	bool load (const char *path);		// false: unreadable (the box shows nothing)
	void setPixels (const unsigned *px, int w, int h, bool hasAlpha = true);
	void clearImage ();
	int  imageWidth () const { return m_w; }
	int  imageHeight () const { return m_h; }
	void onDraw () override;
private:
	unsigned *m_px; int m_w, m_h;
};

} // namespace wtk

#endif
