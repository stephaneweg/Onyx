//
// voronoy -- the desktop's wallpaper painter, a userland app. It asks the kernel for the shared
// wallpaper buffer (kapi_wallpaper_buffer), paints the wallpaper SD:/etc/wallpaper.ini asks for
// (wallpaper.h: a toroidal Voronoi field -- ported from the old kernel GenerateWallpaper --, a
// gradient, bubbles over a gradient, a plain colour; a picture file: apps/imageview --background
// paints it), commits it as the live background, then exits. The buffer's frames are
// kernel-owned, so the wallpaper persists after this app is gone.
//
// It runs from autostart, and again when the Control Panel's Theme applet applies a wallpaper;
// run it from the terminal to reshuffle the cells. Without wallpaper.ini: its own config.ini's
// base colour and points (as before the Theme applet had them).
//
#include "kapi.h"
#include "applib.h"
#include "wallpaper.h"

static void yield (void) { kapi_yield (); }

int main (void)
{
	Wallpaper wp;
	wp_load (wp);
	if (wp.mode == WP_IMAGE && wp.image[0])
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
	unsigned *bg = kapi_wallpaper_buffer (&w, &h);
	if (bg == 0 || w <= 0 || h <= 0) return 1;
	wp_paint (bg, w, h, w, wp, kapi_get_ticks () | 1u, 2, yield);
	kapi_wallpaper_commit ();		// make it the live desktop background
	return 0;				// exit; the wallpaper persists
}
