//
// imageview -- the Onyx image viewer: BMP, GIF (animated), PNG, JPEG, PCX and WebP
// (img/imgload.hpp: stb_image, simplewebp, our PCX decoder).
//
//   * Opens the file given as argument (File Viewer double-click via fileassoc.ini, the
//     Shelf), File > Open... (^O), or a file dropped on the window.
//   * Fit to the window by default (never enlarged); 1 = actual size, + / - (or the
//     wheel) zoom, 0 = fit again. When the image is larger than the window, drag it
//     with the mouse to pan.
//   * Left / Right (or Page Up / Page Down, Backspace / Space): previous / next image of
//     the same folder; Home / End: first / last.
//   * Transparent pixels are shown over a checkerboard; animated GIFs play.
//   * imageview --background [-tile] <image>: no window -- the image becomes the desktop
//     wallpaper (the kernel's shared buffer, kapi_wallpaper_buffer / commit), scaled to
//     cover the screen (proportions kept, the overflow cut), or with -tile repeated from
//     the top-left corner; then it exits (the wallpaper stays). E.g. in SD:/etc/autostart:
//     run imageview --background SD:/pictures/sky.jpg
//
#include "kapi.h"
#include "fsutil.h"
#include "img/imgload.hpp"
#include "wtk/wtk.h"

using namespace wtk;

#define W	720
#define H	540
#define ST_H	20			// status strip
#define VIEW_H	(H - ST_H)
#define MAXF	256			// images listed in the folder

static ImgFrames g_im;
static int  g_frame = 0;
static unsigned g_frameT = 0;		// ticks when the current frame started
static char g_path[256] = "";
static bool g_fit = true;
static int  g_zoom = 100;		// percent, when !g_fit
static int  g_panX = 0, g_panY = 0;	// image offset inside the view (zoomed mode)
static char g_err[160] = "";
static char g_files[MAXF][96]; static int g_nfiles = 0, g_cur = -1;
static Root *g_root = 0;

// ---- the folder's images (next / previous) -------------------------------------------------
static void list_folder (void)
{
	g_nfiles = 0; g_cur = -1;
	char dir[256]; fs_dirname (dir, sizeof dir, g_path);
	void *d = kapi_opendir (dir);
	if (d == 0) return;
	struct kapi_dirent e;
	while (g_nfiles < MAXF && kapi_readdir (d, &e))
		if (!e.is_dir && e.name[0] != '.' && img_is_image_name (e.name))
			fs_copy (g_files[g_nfiles++], e.name, sizeof g_files[0]);
	kapi_closedir (d);
	for (int i = 1; i < g_nfiles; i++)		// case-insensitive sort
	{
		char t[96]; fs_copy (t, g_files[i], sizeof t); int j = i - 1;
		while (j >= 0 && fs_ci_cmp (g_files[j], t) > 0) { fs_copy (g_files[j + 1], g_files[j], sizeof t); j--; }
		fs_copy (g_files[j + 1], t, sizeof t);
	}
	const char *base = fs_basename (g_path);
	for (int i = 0; i < g_nfiles; i++) if (fs_ci_cmp (g_files[i], base) == 0) g_cur = i;
}

static void open_image (const char *path)
{
	img_free (&g_im);
	fs_copy (g_path, path, sizeof g_path);
	g_frame = 0; g_frameT = kapi_get_ticks ();
	g_fit = true; g_zoom = 100; g_panX = g_panY = 0; g_err[0] = '\0';
	if (!img_load (path, &g_im))
	{
		const char *a = "Cannot read this image: ";
		int p = 0; for (int i = 0; a[i]; i++) g_err[p++] = a[i];
		for (int i = 0; path[i] && p < (int) sizeof g_err - 1; i++) g_err[p++] = path[i];
		g_err[p] = '\0';
	}
	list_folder ();
	if (g_root) g_root->invalidate (true);
}

static void step (int d)
{
	if (g_nfiles == 0) return;
	int i = g_cur < 0 ? 0 : g_cur + d;
	if (i < 0) i = g_nfiles - 1;
	if (i >= g_nfiles) i = 0;
	char dir[256], p[300];
	fs_dirname (dir, sizeof dir, g_path);
	fs_join (p, sizeof p, dir, g_files[i]);
	open_image (p);
}

// Current scale in percent (fit: shrink to the view, never enlarge).
static int scale (void)
{
	if (!g_fit || g_im.n == 0) return g_zoom;
	int sx = W * 100 / g_im.w, sy = VIEW_H * 100 / g_im.h, s = sx < sy ? sx : sy;
	return s > 100 ? 100 : (s < 1 ? 1 : s);
}
static void clamp_pan (void)
{
	int s = scale (), dw = g_im.w * s / 100, dh = g_im.h * s / 100;
	if (dw <= W) g_panX = (W - dw) / 2; else { if (g_panX > 0) g_panX = 0; if (g_panX < W - dw) g_panX = W - dw; }
	if (dh <= VIEW_H) g_panY = (VIEW_H - dh) / 2; else { if (g_panY > 0) g_panY = 0; if (g_panY < VIEW_H - dh) g_panY = VIEW_H - dh; }
}
static void set_zoom (int z, bool keepCenter)
{
	if (g_im.n == 0) return;
	int old = scale ();
	if (z < 5) z = 5;
	if (z > 1600) z = 1600;
	// Keep the view centre on the same image point.
	int cx = (W / 2 - g_panX) * 100 / old, cy = (VIEW_H / 2 - g_panY) * 100 / old;
	g_fit = false; g_zoom = z;
	if (keepCenter) { g_panX = W / 2 - cx * z / 100; g_panY = VIEW_H / 2 - cy * z / 100; }
	clamp_pan ();
	g_root->invalidate (true);
}

// ---- menu commands ---------------------------------------------------------------------------
static void on_open ()     { char p[256]; if (wk_file_open (p, sizeof p, "SD:/")) open_image (p); }
static void on_next ()     { step (1); }
static void on_prev ()     { step (-1); }
static void on_fit ()      { g_fit = true; clamp_pan (); g_root->invalidate (true); }
static void on_actual ()   { set_zoom (100, true); }
static void on_zoom_in ()  { set_zoom (scale () * 5 / 4 + 1, true); }
static void on_zoom_out () { set_zoom (scale () * 4 / 5, true); }
static void on_paint ()    { if (g_path[0]) kapi_exec ("SD:apps/paint.app/main", g_path); }

class ViewRoot : public Root
{
public:
	bool dragging = false; int dragX = 0, dragY = 0;

	ViewRoot () : Root (W, H, "Image Viewer") {}

	void onDraw () override
	{
		int fh = kapi_font_height (); if (fh < 1) fh = 16;
		unsigned *px = canvas.px; int stride = canvas.stride;
		if (g_im.n == 0)
		{
			canvas.fillRect (0, 0, W, VIEW_H, 0x00181C22);
			canvas.text (16, 16, g_err[0] ? g_err : "Open an image: File > Open... (^O), or drop one here.", 0x00A0A8B4);
		}
		else
		{
			clamp_pan ();
			int s = scale ();
			const unsigned *src = g_im.px[g_frame];
			unsigned stepFx = (unsigned) ((100u << 16) / (unsigned) s);	// 16.16 src step per dest px
			for (int y = 0; y < VIEW_H; y++)
			{
				unsigned *row = px + (long) y * stride;
				int iy = y - g_panY;
				int sy = iy >= 0 ? (int) (((unsigned long) iy * stepFx) >> 16) : -1;
				for (int x = 0; x < W; x++)
				{
					int ix = x - g_panX;
					int sx = ix >= 0 ? (int) (((unsigned long) ix * stepFx) >> 16) : -1;
					if (sx < 0 || sy < 0 || sx >= g_im.w || sy >= g_im.h) { row[x] = 0x00181C22; continue; }
					unsigned c = src[(long) sy * g_im.w + sx], a = c >> 24;
					if (a == 255) { row[x] = c & 0xFFFFFF; continue; }
					unsigned bg = (((x >> 3) + (y >> 3)) & 1) ? 0x00C8C8C8 : 0x00989898;	// checkerboard
					unsigned r = (((c >> 16) & 255) * a + ((bg >> 16) & 255) * (255 - a)) / 255;
					unsigned g = (((c >> 8) & 255) * a + ((bg >> 8) & 255) * (255 - a)) / 255;
					unsigned b = ((c & 255) * a + (bg & 255) * (255 - a)) / 255;
					row[x] = (r << 16) | (g << 8) | b;
				}
			}
		}
		// Status strip: name, size, format, zoom, position in the folder.
		canvas.fillRect (0, VIEW_H, W, ST_H, 0x00303D4D);
		char st[200]; int p = 0;
		auto put = [&] (const char *t) { for (int i = 0; t[i] && p < (int) sizeof st - 1; i++) st[p++] = t[i]; };
		auto num = [&] (int v) { char b[12]; int n = 0; if (v == 0) b[n++] = '0'; while (v > 0) { b[n++] = (char) ('0' + v % 10); v /= 10; } while (n) st[p++] = b[--n]; };
		if (g_path[0]) put (fs_basename (g_path));
		if (g_im.n)
		{
			put ("   "); num (g_im.w); put (" x "); num (g_im.h); put ("   "); put (g_im.format);
			if (g_im.n > 1) { put (" ("); num (g_im.n); put (" frames)"); }
			put ("   "); num (scale ()); put ("%"); if (g_fit) put (" (fit)");
		}
		if (g_nfiles > 1 && g_cur >= 0) { put ("   "); num (g_cur + 1); put ("/"); num (g_nfiles); }
		st[p] = '\0';
		canvas.text (8, VIEW_H + (ST_H - fh) / 2, st, 0x00E0E6EE);
	}

	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx < 0) { dragging = false; return false; }
		if (wheel) { set_zoom (wheel > 0 ? scale () * 5 / 4 + 1 : scale () * 4 / 5, true); return true; }
		if (bl && !dragging && my < VIEW_H) { dragging = true; dragX = mx; dragY = my; return true; }
		if (bl && dragging)
		{
			g_panX += mx - dragX; g_panY += my - dragY; dragX = mx; dragY = my;
			if (g_fit) { g_fit = false; g_zoom = scale (); }
			clamp_pan ();
			invalidate (true);
			return true;
		}
		if (!bl) dragging = false;
		return true;
	}

	bool onKey (long k) override
	{
		switch (k)
		{
		case KEY_RIGHT: case KEY_PGDN: case ' ':          step (1);  return true;
		case KEY_LEFT:  case KEY_PGUP: case KEY_BACKSPACE: step (-1); return true;
		case KEY_HOME: if (g_nfiles) { g_cur = 0; step (0); } return true;
		case KEY_END:  if (g_nfiles) { g_cur = g_nfiles - 1; step (0); } return true;
		case '+': case '=': on_zoom_in ();  return true;
		case '-':           on_zoom_out (); return true;
		case '0':           on_fit ();      return true;
		case '1':           on_actual ();   return true;
		}
		return false;
	}

	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		if (type != DND_FILES) return;
		char p[256]; int n = 0;
		while (data[n] && data[n] != '\n' && n < (int) sizeof p - 1) { p[n] = data[n]; n++; }
		p[n] = '\0';
		if (n) open_image (p);
	}

	void onTick () override				// animated GIF
	{
		if (g_im.n < 2) return;
		unsigned now = kapi_get_ticks ();
		if ((now - g_frameT) * 10 >= (unsigned) g_im.delay[g_frame])
		{
			g_frame = (g_frame + 1) % g_im.n;
			g_frameT = now;
			invalidate (true);
		}
	}
};

// --background: the image into the wallpaper buffer. 0 = done.
static unsigned over_black (unsigned c) { unsigned a = c >> 24; if (a == 255) return c & 0xFFFFFF;
	return ((((c >> 16) & 255) * a / 255) << 16) | ((((c >> 8) & 255) * a / 255) << 8) | ((c & 255) * a / 255); }
static int set_background (const char *path, bool tile)
{
	ImgFrames im;
	if (!img_load (path, &im) || im.w <= 0 || im.h <= 0) return 2;
	int sw = 0, sh = 0;
	unsigned *bg = kapi_wallpaper_buffer (&sw, &sh);
	if (!bg || sw <= 0 || sh <= 0) { img_free (&im); return 3; }
	const unsigned *src = im.px[0];
	if (tile)
		for (int y = 0; y < sh; y++)
		{
			const unsigned *s = src + (long) (y % im.h) * im.w;
			for (int x = 0; x < sw; x++) bg[(long) y * sw + x] = over_black (s[x % im.w]);
		}
	else
	{
		// cover: the larger of the two scales, centred; bilinear (16.16 fixed point)
		long long kx = ((long long) im.w << 16) / sw, ky = ((long long) im.h << 16) / sh;
		long long k = kx < ky ? kx : ky;			// source pixels per screen pixel
		long long ox = (((long long) im.w << 16) - k * sw) / 2, oy = (((long long) im.h << 16) - k * sh) / 2;
		for (int y = 0; y < sh; y++)
		{
			long long fy = oy + k * y + k / 2 - 32768; if (fy < 0) fy = 0;
			int y0 = (int) (fy >> 16), y1 = y0 + 1 < im.h ? y0 + 1 : y0; unsigned wy = (unsigned) ((fy >> 8) & 255);
			for (int x = 0; x < sw; x++)
			{
				long long fx = ox + k * x + k / 2 - 32768; if (fx < 0) fx = 0;
				int x0 = (int) (fx >> 16), x1 = x0 + 1 < im.w ? x0 + 1 : x0; unsigned wx = (unsigned) ((fx >> 8) & 255);
				if (x0 >= im.w) x0 = x1 = im.w - 1;
				if (y0 >= im.h) y0 = y1 = im.h - 1;
				unsigned a = over_black (src[(long) y0 * im.w + x0]), b = over_black (src[(long) y0 * im.w + x1]);
				unsigned c = over_black (src[(long) y1 * im.w + x0]), d = over_black (src[(long) y1 * im.w + x1]);
				unsigned o = 0;
				for (int sft = 0; sft < 24; sft += 8)
				{
					unsigned top = (((a >> sft) & 255) * (256 - wx) + ((b >> sft) & 255) * wx) >> 8;
					unsigned bot = (((c >> sft) & 255) * (256 - wx) + ((d >> sft) & 255) * wx) >> 8;
					o |= ((top * (256 - wy) + bot * wy) >> 8) << sft;
				}
				bg[(long) y * sw + x] = o;
			}
		}
	}
	img_free (&im);
	kapi_wallpaper_commit ();
	return 0;
}

int main (void)
{
	char args[256] = "";
	kapi_get_args (args, sizeof args);
	{
		// --background [-tile] <image>: set the wallpaper, no window
		bool bgMode = false, tile = false; int i = 0;
		for (;;)
		{
			while (args[i] == ' ') i++;
			if (args[i] != '-') break;
			char opt[20]; int n = 0;
			while (args[i] && args[i] != ' ' && n < 19) opt[n++] = args[i++];
			opt[n] = 0;
			bool bgo = true, tlo = true;
			const char *b1 = "--background", *t1 = "-tile", *t2 = "--tile";
			for (int k = 0; b1[k] || opt[k]; k++) if (b1[k] != opt[k]) { bgo = false; break; }
			bool m1 = true, m2 = true;
			for (int k = 0; t1[k] || opt[k]; k++) if (t1[k] != opt[k]) { m1 = false; break; }
			for (int k = 0; t2[k] || opt[k]; k++) if (t2[k] != opt[k]) { m2 = false; break; }
			tlo = m1 || m2;
			if (bgo) bgMode = true; else if (tlo) tile = true; else break;
		}
		if (bgMode)
		{
			char path[256]; int n = 0;
			if (args[i] == '"') { i++; while (args[i] && args[i] != '"' && n < 255) path[n++] = args[i++]; }
			else while (args[i] && n < 255) path[n++] = args[i++];
			while (n > 0 && path[n - 1] == ' ') n--;
			path[n] = 0;
			return path[0] ? set_background (path, tile) : 1;
		}
	}

	ViewRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;

	static Menu menu;
	menu.menu ("File");
	menu.item ("Open...",        "^O", WK_CTRL ('O'), on_open);
	menu.item ("Next image",     "->", 0,             on_next);
	menu.item ("Previous image", "<-", 0,             on_prev);
	menu.separator ();
	menu.item ("Edit in Paint",  "",   0,             on_paint);
	menu.menu ("View");
	menu.item ("Fit to window",  "0",  0,             on_fit);
	menu.item ("Actual size",    "1",  0,             on_actual);
	menu.item ("Zoom in",        "+",  0,             on_zoom_in);
	menu.item ("Zoom out",       "-",  0,             on_zoom_out);
	menu.publish ();

	if (args[0]) open_image (args);
	root.run ();
	return 0;
}
