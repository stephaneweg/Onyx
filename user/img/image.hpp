//
// image.hpp -- decode PNG / JPEG image data into a 32-bit pixel buffer for Onyx apps.
//
// Reusable, like http.hpp / onyx_tls.hpp. Decodes from a MEMORY buffer (no files) into a
// freshly malloc'd array of 0xAARRGGBB pixels (8-bit alpha in the top byte; the Onyx
// canvas ignores the top byte when blitting, so opaque images display as-is, and alpha is
// preserved for code that wants it). Caller frees the result with free().
//
// This is a NEWLIB app component: it uses libpng + libjpeg (+ zlib + libm) and malloc. Link
// them in (see user/img/Makefile / README). For the magenta-keyed icon path keep bmp.hpp;
// this is for full-colour web images (PNG/JPEG). GIF/SVG come later via NetSurf's own libs.
//
//   int w, h;
//   unsigned *px = onyximg::decode(data, len, &w, &h);   // 0 on failure / unknown format
//   if (px) { /* blit w*h pixels of 0xAARRGGBB ... */ free(px); }
//
#ifndef ONYX_IMAGE_HPP
#define ONYX_IMAGE_HPP

#include <stdlib.h>		// malloc / free
#include <setjmp.h>
#include <png.h>
#include <jpeglib.h>

namespace onyximg
{
	// ---- PNG (libpng) ------------------------------------------------------
	namespace detail
	{
		struct PngSrc { const unsigned char *p; int len, pos; };

		inline void png_mem_read (png_structp png, png_bytep out, png_size_t n)
		{
			PngSrc *s = (PngSrc *) png_get_io_ptr (png);
			png_size_t avail = (png_size_t) (s->len - s->pos);
			if (n > avail) n = avail;
			for (png_size_t i = 0; i < n; i++) out[i] = s->p[s->pos + i];
			s->pos += (int) n;
		}
	}

	inline unsigned *decode_png (const unsigned char *data, int len, int *ow, int *oh)
	{
		if (len < 8 || png_sig_cmp ((png_bytep) data, 0, 8)) return 0;

		png_structp png = png_create_read_struct (PNG_LIBPNG_VER_STRING, 0, 0, 0);
		if (!png) return 0;
		png_infop info = png_create_info_struct (png);
		if (!info) { png_destroy_read_struct (&png, 0, 0); return 0; }

		unsigned *out = 0;
		png_bytep row = 0;
		if (setjmp (png_jmpbuf (png)))			// libpng error -> here
		{ free (out); free (row); png_destroy_read_struct (&png, &info, 0); return 0; }

		detail::PngSrc src = { data, len, 0 };
		png_set_read_fn (png, &src, detail::png_mem_read);
		png_read_info (png, info);

		int w = (int) png_get_image_width (png, info);
		int h = (int) png_get_image_height (png, info);
		int ct = png_get_color_type (png, info);
		int bd = png_get_bit_depth (png, info);

		// Normalise everything to 8-bit RGBA.
		if (bd == 16) png_set_strip_16 (png);
		if (ct == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb (png);
		if (ct == PNG_COLOR_TYPE_GRAY && bd < 8) png_set_expand_gray_1_2_4_to_8 (png);
		if (png_get_valid (png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha (png);
		if (ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb (png);
		png_set_filler (png, 0xFF, PNG_FILLER_AFTER);	// force 4 bytes/pixel
		png_read_update_info (png, info);

		out = (unsigned *) malloc ((size_t) w * h * 4);
		if (!out) longjmp (png_jmpbuf (png), 1);
		row = (png_bytep) malloc (png_get_rowbytes (png, info));
		if (!row) longjmp (png_jmpbuf (png), 1);

		for (int y = 0; y < h; y++)
		{
			png_read_row (png, row, 0);
			for (int x = 0; x < w; x++)
			{
				unsigned char r = row[x*4+0], g = row[x*4+1], b = row[x*4+2], a = row[x*4+3];
				out[y*w+x] = ((unsigned)a<<24)|((unsigned)r<<16)|((unsigned)g<<8)|b;
			}
		}
		free (row);
		png_read_end (png, 0);
		png_destroy_read_struct (&png, &info, 0);
		*ow = w; *oh = h;
		return out;
	}

	// ---- JPEG (libjpeg) ----------------------------------------------------
	namespace detail
	{
		struct JpgErr { struct jpeg_error_mgr pub; jmp_buf jb; };
		inline void jpg_error_exit (j_common_ptr c) { longjmp (((JpgErr *) c->err)->jb, 1); }
	}

	inline unsigned *decode_jpeg (const unsigned char *data, int len, int *ow, int *oh)
	{
		if (len < 2 || data[0] != 0xFF || data[1] != 0xD8) return 0;	// SOI

		struct jpeg_decompress_struct ci;
		detail::JpgErr jerr;
		ci.err = jpeg_std_error (&jerr.pub);
		jerr.pub.error_exit = detail::jpg_error_exit;		// don't exit() on bad input

		unsigned *out = 0;
		unsigned char *row = 0;
		if (setjmp (jerr.jb))					// libjpeg error -> here
		{ free (out); free (row); jpeg_destroy_decompress (&ci); return 0; }

		jpeg_create_decompress (&ci);
		jpeg_mem_src (&ci, (unsigned char *) data, (unsigned long) len);
		if (jpeg_read_header (&ci, TRUE) != JPEG_HEADER_OK) longjmp (jerr.jb, 1);
		ci.out_color_space = JCS_RGB;
		jpeg_start_decompress (&ci);

		int w = (int) ci.output_width, h = (int) ci.output_height;
		out = (unsigned *) malloc ((size_t) w * h * 4);
		if (!out) longjmp (jerr.jb, 1);
		row = (unsigned char *) malloc ((size_t) w * ci.output_components);
		if (!row) longjmp (jerr.jb, 1);

		while ((int) ci.output_scanline < h)
		{
			int y = (int) ci.output_scanline;
			JSAMPROW rp = row;
			jpeg_read_scanlines (&ci, &rp, 1);
			for (int x = 0; x < w; x++)
			{
				unsigned char r = row[x*3+0], g = row[x*3+1], b = row[x*3+2];
				out[y*w+x] = 0xFF000000u | ((unsigned)r<<16)|((unsigned)g<<8)|b;
			}
		}
		free (row);
		jpeg_finish_decompress (&ci);
		jpeg_destroy_decompress (&ci);
		*ow = w; *oh = h;
		return out;
	}

	// ---- dispatcher: sniff the format by magic bytes -----------------------
	inline unsigned *decode (const unsigned char *data, int len, int *w, int *h)
	{
		if (len >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
			return decode_png (data, len, w, h);
		if (len >= 2 && data[0] == 0xFF && data[1] == 0xD8)
			return decode_jpeg (data, len, w, h);
		return 0;	// GIF / BMP / SVG: TODO (NetSurf libnsgif/libnsbmp, or bmp.hpp for files)
	}
}

#endif // ONYX_IMAGE_HPP
