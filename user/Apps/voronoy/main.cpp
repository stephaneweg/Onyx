//
// voronoy -- the desktop's wallpaper painter, a userland app. It asks the kernel for the shared
// wallpaper buffer (uk_win_wallpaper_buffer), paints the wallpaper SD:/etc/wallpaper.ini asks for
// (wallpaper.h: a toroidal Voronoi field -- ported from the old kernel GenerateWallpaper --, a
// gradient, bubbles over a gradient, a plain colour, a grey pattern -- SD:/wallpapers -- coloured
// by the gradient (multiplied); a picture file: apps/imageview --background paints it), commits
// it as the live background, then exits. The buffer's frames are kernel-owned, so the wallpaper
// persists after this app is gone.
//
// It runs from autostart, and again when the Control Panel's Theme applet applies a wallpaper;
// run it from the terminal to reshuffle the cells. Without wallpaper.ini: its own config.ini's
// base colour and points (as before the Theme applet had them).
//
#include "appkit/appkit.h"
#include "uikit/win.h"		// the window API (UIKit's: uk_win_*)
#include "onyxpp.hpp"			// operator new / delete (the picture's decoder)
#include "systemkit/systemkit.h"
#include "imagekit/img/imgload.hpp"

static void yield (void) { kapi_yield (); }

int main (void)
{
	Wallpaper wp;
	wp_load (wp);
	if (wp.mode == WP_IMAGE && wp.image[0] && !wp.tint)		// (a tinted one: painted below)
	{
		char args[260]; int n = 0;
		ax_strcat (args, sizeof args, &n, "--background ");
		if (wp.tile) ax_strcat (args, sizeof args, &n, "-tile ");
		bool sp = false; for (int i = 0; wp.image[i]; i++) if (wp.image[i] == ' ') sp = true;
		if (sp) ax_strcat (args, sizeof args, &n, "\"");
		ax_strcat (args, sizeof args, &n, wp.image);
		if (sp) ax_strcat (args, sizeof args, &n, "\"");
		if (kapi_exec ("SD:apps/imageview.app/main", args)) return 0;
		wp.mode = WP_SOLID;					// (no viewer: its colour)
	}
	int w = 0, h = 0;
	unsigned *bg = uk_win_wallpaper_buffer (&w, &h);
	if (bg == 0 || w <= 0 || h <= 0) return 1;
	wp_paint (bg, w, h, w, wp, kapi_get_ticks () | 1u, 2, yield);
	// a pattern: its grey multiplies the gradient; a tinted picture: its grey multiplies the colour
	const char *grey = wp.mode == WP_PATTERN ? wp.pattern : wp.mode == WP_IMAGE && wp.tint ? wp.image : 0;
	if (grey && grey[0])
	{
		ImgFrames im;
		if (img_load (grey, &im) && im.w > 0 && im.h > 0)
		{
			unsigned char *g = new unsigned char[(long) w * h];
			if (g)
			{
				if (wp.mode == WP_IMAGE && wp.tile) wp_grey_tile (im.px[0], im.w, im.h, g, w, h, w, h);
				else wp_grey_cover (im.px[0], im.w, im.h, g, w, h);
				wp_multiply (bg, w, h, w, g);
				delete [] g;
			}
			img_free (&im);
		}
	}
	uk_win_wallpaper_commit ();		// make it the live desktop background
	return 0;				// exit; the wallpaper persists
}
