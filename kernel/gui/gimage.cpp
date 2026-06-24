//
// gimage.cpp -- ported from SimpleOS graphics/gimage.bas (FreeBASIC -> C++).
//
#include <kern/gui/gimage.h>
#include <circle/util.h>		// memset
#include <circle/chargenerator.h>	// built-in bitmap font
#include <circle/new.h>

// One shared character generator (default console font). Constructed at static-init.
static CCharGenerator s_CharGen;

static inline void Fill32 (u32 *pDst, u32 nColor, int nCount)
{
	while (nCount-- > 0)
	{
		*pDst++ = nColor;
	}
}

GImage::GImage (void)
:	m_nWidth (0), m_nHeight (0), m_pBuffer (0), m_nBufferSize (0), m_bOwnsBuffer (FALSE)
{
}

GImage::GImage (u32 *pBuffer, int nWidth, int nHeight)
:	m_nWidth (nWidth), m_nHeight (nHeight), m_pBuffer (pBuffer),
	m_nBufferSize (0), m_bOwnsBuffer (FALSE)
{
}

GImage::~GImage (void)
{
	if (m_pBuffer != 0 && m_bOwnsBuffer)
	{
		delete [] m_pBuffer;
	}
	m_pBuffer = 0;
}

void GImage::Wrap (u32 *pBuffer, int nWidth, int nHeight)
{
	if (m_pBuffer != 0 && m_bOwnsBuffer)
	{
		delete [] m_pBuffer;
	}
	m_pBuffer = pBuffer;
	m_nWidth = nWidth;
	m_nHeight = nHeight;
	m_nBufferSize = 0;
	m_bOwnsBuffer = FALSE;
}

void GImage::SetSize (int nWidth, int nHeight)
{
	if (nWidth != m_nWidth || nHeight != m_nHeight)
	{
		m_nWidth = nWidth;
		m_nHeight = nHeight;
		CreateBuffer ();
	}
}

void GImage::CreateBuffer (void)
{
	unsigned nNewSize = (unsigned) (m_nWidth * m_nHeight) * sizeof (u32);
	if (nNewSize > m_nBufferSize)
	{
		if (m_pBuffer != 0 && m_bOwnsBuffer)
		{
			delete [] m_pBuffer;
		}
		m_pBuffer = new u32[m_nWidth * m_nHeight];
		m_nBufferSize = nNewSize;
		m_bOwnsBuffer = TRUE;
	}
}

void GImage::Clear (u32 nColor)
{
	if (m_pBuffer == 0)
	{
		return;
	}
	Fill32 (m_pBuffer, nColor, m_nWidth * m_nHeight);
}

void GImage::SetPixel (int x, int y, u32 nColor)
{
	if (m_pBuffer == 0)
	{
		return;
	}
	if (x >= 0 && y >= 0 && x < m_nWidth && y < m_nHeight)
	{
		m_pBuffer[y * m_nWidth + x] = nColor;
	}
}

u32 GImage::GetPixel (int x, int y) const
{
	if (m_pBuffer == 0 || x < 0 || y < 0 || x >= m_nWidth || y >= m_nHeight)
	{
		return 0;
	}
	return m_pBuffer[y * m_nWidth + x];
}

void GImage::DrawLine (int x1, int y1, int x2, int y2, u32 nColor)
{
	if (m_pBuffer == 0)
	{
		return;
	}

	// Bresenham, with fast paths for axis-aligned lines.
	if (x1 == x2)
	{
		if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
		for (int y = y1; y <= y2; y++) SetPixel (x1, y, nColor);
		return;
	}
	if (y1 == y2)
	{
		if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
		for (int x = x1; x <= x2; x++) SetPixel (x, y1, nColor);
		return;
	}

	int dx = x2 > x1 ? x2 - x1 : x1 - x2;
	int dy = y2 > y1 ? y2 - y1 : y1 - y2;
	int sx = x1 < x2 ? 1 : -1;
	int sy = y1 < y2 ? 1 : -1;
	int err = (dx > dy ? dx : -dy) / 2;

	for (;;)
	{
		SetPixel (x1, y1, nColor);
		if (x1 == x2 && y1 == y2) break;
		int e2 = err;
		if (e2 > -dx) { err -= dy; x1 += sx; }
		if (e2 <  dy) { err += dx; y1 += sy; }
	}
}

void GImage::DrawRectangle (int x1, int y1, int x2, int y2, u32 nColor)
{
	DrawLine (x1, y1, x2, y1, nColor);
	DrawLine (x1, y1, x1, y2, nColor);
	DrawLine (x2, y1, x2, y2, nColor);
	DrawLine (x1, y2, x2, y2, nColor);
}

void GImage::FillRectangle (int x1, int y1, int x2, int y2, u32 nColor)
{
	if (m_pBuffer == 0)
	{
		return;
	}

	if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
	if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }

	// Clip to bounds.
	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	if (x2 >= m_nWidth)  x2 = m_nWidth - 1;
	if (y2 >= m_nHeight) y2 = m_nHeight - 1;
	if (x1 > x2 || y1 > y2)
	{
		return;
	}

	int nRowPixels = x2 - x1 + 1;
	u32 *pRow = m_pBuffer + y1 * m_nWidth + x1;
	for (int y = y1; y <= y2; y++)
	{
		Fill32 (pRow, nColor, nRowPixels);
		pRow += m_nWidth;
	}
}

void GImage::PutOtherRaw (const u32 *pSrc, int nSrcW, int nSrcH, int x, int y)
{
	if (m_pBuffer == 0 || pSrc == 0 || nSrcW <= 0 || nSrcH <= 0)
	{
		return;
	}

	int sx = 0, sy = 0;		// source origin offset after clipping
	int dx = x, dy = y;		// destination origin
	int w = nSrcW, h = nSrcH;

	if (x >= m_nWidth || y >= m_nHeight || x + w <= 0 || y + h <= 0)
	{
		return;
	}
	if (x < 0) { sx = -x; dx = 0; w += x; }
	if (y < 0) { sy = -y; dy = 0; h += y; }
	if (dx + w > m_nWidth)  w = m_nWidth - dx;
	if (dy + h > m_nHeight) h = m_nHeight - dy;

	const u32 *pSrcRow = pSrc + sy * nSrcW + sx;
	u32 *pDstRow = m_pBuffer + dy * m_nWidth + dx;
	for (int row = 0; row < h; row++)
	{
		memcpy (pDstRow, pSrcRow, (size_t) w * sizeof (u32));
		pSrcRow += nSrcW;
		pDstRow += m_nWidth;
	}
}

void GImage::PutOther (const GImage *pSrc, int x, int y, boolean bTransparent)
{
	if (pSrc == 0 || !pSrc->IsValid ())
	{
		return;
	}

	if (!bTransparent)
	{
		PutOtherRaw (pSrc->Buffer (), pSrc->Width (), pSrc->Height (), x, y);
		return;
	}

	// Transparent blit: skip GIMAGE_TRANSPARENT pixels, copy the rest per pixel.
	if (m_pBuffer == 0)
	{
		return;
	}

	int nSrcW = pSrc->Width ();
	int nSrcH = pSrc->Height ();
	int sx = 0, sy = 0, dx = x, dy = y, w = nSrcW, h = nSrcH;

	if (x >= m_nWidth || y >= m_nHeight || x + w <= 0 || y + h <= 0)
	{
		return;
	}
	if (x < 0) { sx = -x; dx = 0; w += x; }
	if (y < 0) { sy = -y; dy = 0; h += y; }
	if (dx + w > m_nWidth)  w = m_nWidth - dx;
	if (dy + h > m_nHeight) h = m_nHeight - dy;

	const u32 *pSrcBuf = pSrc->Buffer ();
	for (int row = 0; row < h; row++)
	{
		const u32 *pS = pSrcBuf + (sy + row) * nSrcW + sx;
		u32 *pD = m_pBuffer + (dy + row) * m_nWidth + dx;
		for (int col = 0; col < w; col++)
		{
			u32 c = pS[col];
			if ((c & 0xFFFFFF) != GIMAGE_TRANSPARENT)
			{
				pD[col] = c;
			}
		}
	}
}

void GImage::PutOtherPart (const GImage *pSrc, int dstX, int dstY,
			   int srcX, int srcY, int w, int h, boolean bTransparent)
{
	if (m_pBuffer == 0 || pSrc == 0 || !pSrc->IsValid () || w <= 0 || h <= 0)
	{
		return;
	}

	int nSrcW = pSrc->Width ();
	int nSrcH = pSrc->Height ();

	// Clip the source rectangle to the source image.
	if (srcX < 0) { w += srcX; dstX -= srcX; srcX = 0; }
	if (srcY < 0) { h += srcY; dstY -= srcY; srcY = 0; }
	if (srcX + w > nSrcW) w = nSrcW - srcX;
	if (srcY + h > nSrcH) h = nSrcH - srcY;
	if (w <= 0 || h <= 0) return;

	// Clip the destination rectangle to this image.
	if (dstX < 0) { w += dstX; srcX -= dstX; dstX = 0; }
	if (dstY < 0) { h += dstY; srcY -= dstY; dstY = 0; }
	if (dstX + w > m_nWidth)  w = m_nWidth - dstX;
	if (dstY + h > m_nHeight) h = m_nHeight - dstY;
	if (w <= 0 || h <= 0) return;

	const u32 *pSrcBuf = pSrc->Buffer ();
	for (int row = 0; row < h; row++)
	{
		const u32 *pS = pSrcBuf + (srcY + row) * nSrcW + srcX;
		u32 *pD = m_pBuffer + (dstY + row) * m_nWidth + dstX;
		if (bTransparent)
		{
			for (int col = 0; col < w; col++)
			{
				u32 c = pS[col];
				if ((c & 0xFFFFFF) != GIMAGE_TRANSPARENT)
				{
					pD[col] = c;
				}
			}
		}
		else
		{
			for (int col = 0; col < w; col++)
			{
				pD[col] = pS[col];
			}
		}
	}
}


// ---- text ------------------------------------------------------------------

int GImage::FontWidth (void)	{ return (int) s_CharGen.GetCharWidth (); }
int GImage::FontHeight (void)	{ return (int) s_CharGen.GetCharHeight (); }

int GImage::TextWidth (const char *pText)
{
	int n = 0;
	while (pText != 0 && *pText++ != '\0')
	{
		n++;
	}
	return n * FontWidth ();
}

void GImage::DrawChar (int x, int y, char chAscii, u32 nColor)
{
	unsigned nW = s_CharGen.GetCharWidth ();
	unsigned nH = s_CharGen.GetCharHeight ();
	for (unsigned cy = 0; cy < nH; cy++)
	{
		for (unsigned cx = 0; cx < nW; cx++)
		{
			if (s_CharGen.GetPixel (chAscii, cx, cy))
			{
				SetPixel (x + (int) cx, y + (int) cy, nColor);
			}
		}
	}
}

void GImage::DrawText (int x, int y, const char *pText, u32 nColor)
{
	if (pText == 0)
	{
		return;
	}
	int nW = FontWidth ();
	for (; *pText != '\0'; pText++)
	{
		if ((unsigned char) *pText >= ' ')
		{
			DrawChar (x, y, *pText, nColor);
		}
		x += nW;
	}
}
