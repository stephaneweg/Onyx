//
// uikit/imgload.cpp -- UIKit's img_load / img_load_mem / img_free / img_inflate / img_is_image_name
// (img/imgload.hpp), which every app that shows a picture calls.
//
// In the shared library (SD:/lib/uikit.so) they are RELAYS to ImageKit (SD:/lib/imagekit.so, opened
// when a picture is first read): one copy of the decoders in the system, and a format ImageKit learns
// is one every app shows. (Until 2026-10-05 the decoders were compiled here too.)
// In a static build of the toolkit (the PC builds: the simulator, Koton for Windows) there is no
// shared library: the decoders are compiled here, as before.
//
#ifndef ONYX_LIB_BUILD

#define IMGLOAD_IMPLEMENTATION
#include "img/imgload.hpp"

#else

#include "appkit/appkit.h"
#include "lib.h"
#include "img/imgload.hpp"
#include "imagekit/imagekit.h"

extern "C" {
const void *onyx_imagekit_table;			// the import stubs' table (lib/imagekit_stubs.o, linked in)
const TLibImports *onyx_lib_imports (void);		// (librt.cpp: what the program gave this library)
}
static bool need_imagekit (void)
{
	if (onyx_imagekit_table == 0)
	{
		int err = 0;
		const TLibHeader *t = (const TLibHeader *) kapi_lib_open ("imagekit", 44, &err);
		if (t != 0)
		{
			TLibImports imp = *onyx_lib_imports ();		// (the program's allocator: one heap; none of UIKit's variables)
			imp.size = sizeof imp; imp.ndata = 0; imp.data = 0;
			if (t->init (&imp) >= 0) onyx_imagekit_table = t;
		}
	}
	return onyx_imagekit_table != 0;
}

void img_free (ImgFrames *im)
{
	for (int i = 0; i < im->n; i++) { delete [] im->px[i]; im->px[i] = 0; }
	im->n = 0;
}
static bool frames_out (ik_frames *f, ImgFrames *im)
{
	if (f == 0) return false;
	im->w = ik_frames_width (f); im->h = ik_frames_height (f);
	im->format = ik_load_format ();				// (a constant of the library's)
	im->n = ik_frames_take (f, im->px, im->delay, IMG_MAX_FRAMES);
	ik_frames_free (f);
	return im->n > 0;
}
bool img_load (const char *path, ImgFrames *im)
{
	im->n = 0; im->w = im->h = 0; im->format = "";
	return path != 0 && need_imagekit () && frames_out (ik_frames_load (path), im);
}
bool img_load_mem (const void *data, unsigned len, ImgFrames *im)
{
	im->n = 0; im->w = im->h = 0; im->format = "";
	return data != 0 && len != 0 && need_imagekit () && frames_out (ik_frames_load_mem (data, len), im);
}
unsigned char *img_inflate (const void *data, unsigned len, bool zlib, unsigned *outLen)
{
	return need_imagekit () ? ik_inflate (data, len, zlib ? 1 : 0, outLen) : 0;
}
bool img_is_image_name (const char *name)
{
	return name != 0 && need_imagekit () && ik_is_image_name (name) != 0;
}

// The WebP decoder's own functions were in this library's table by accident (every global function of
// its objects is): a table's entry is never removed, so they stay as stubs that fail. Nobody called
// them: a WebP picture is read through img_load, as every other.
extern "C" {
long simplewebp_close_input (void) { return -1; }
long simplewebp_decode (void) { return -1; }
long simplewebp_decode_yuva (void) { return -1; }
long simplewebp_get_dimensions (void) { return -1; }
long simplewebp_get_error_text (void) { return 0; }
long simplewebp_input_from_memory (void) { return -1; }
long simplewebp_is_lossless (void) { return 0; }
long simplewebp_load (void) { return -1; }
long simplewebp_load_from_memory (void) { return -1; }
long simplewebp_unload (void) { return -1; }
long simplewebp_version (void) { return 0; }
}

#endif
