//
// Apps/photos/share.h -- what is done to photos: favourite, an album (a new one, one made before), the trash (the
// File Viewer's: they can come back), a quarter turn (a JPEG's EXIF orientation changed: no pixel touched), Send by
// Mail (smaller copies, 1920 pixels, attached to a new message in Mail), copied (the Clipboard), the wallpaper, Open
// in Paint, a PDF (a contact sheet: an album or the photos chosen, three by four a page, their dates), the slideshow
// (the whole screen, one photo every few seconds melting into the next).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _photos_share_h
#define _photos_share_h

#include "Apps/photos/editor.h"
#include "systemkit/systemkit.h"
#include "pdf/pdfwrite.h"
#include "printerkit/printerkit.h"

namespace photos {

static void status_note (const char *s) { scpy (g_note, s, sizeof g_note); g_noteT = kapi_get_ticks (); if (g_grid) g_grid->invalidate (true); }

// ---- favourite ---------------------------------------------------------------------------------------------------------------------
static void act_favourite (int one)
{
	Vec<int> c; if (!chosen (c, one)) { free (c.a); return; }
	bool all = true; for (int i = 0; i < c.n; i++) if (!g_lib.ph[c[i]].fav) all = false;
	for (int i = 0; i < c.n; i++) g_lib.ph[c[i]].fav = !all;
	free (c.a);
	g_lib.save ();
	lib_changed ();
}

// ---- albums -------------------------------------------------------------------------------------------------------------------------
static void act_new_album ()
{
	NameBox nb (TR ("A new album"), TR ("Its name:"), ""); char n[120];
	if (!nb.ask (n, sizeof n)) return;
	if (strchr (n, '/') || strchr (n, ':')) { uk_messagebox (TR ("Photos"), TR ("An album's name cannot have a / or a :."), MB_OK); return; }
	int a = g_lib.album_new (n);
	// the photos chosen go in
	Vec<int> c; chosen (c);
	for (int i = 0; i < c.n; i++) g_lib.album_add (a, g_lib.ph[c[i]].path);
	free (c.a);
	if (c.n) { char s[200]; snprintf (s, sizeof s, TR ("%d added to %s"), c.n, n); status_note (s); sel_clear (); }
	lib_changed ();
	if (!c.n) show_source (SRC_ALBUM, g_lib.album_find (n));
}
static void act_add_to_album (int one, int x, int y)
{
	Vec<int> c; if (!chosen (c, one)) { free (c.a); return; }
	if (x < 0) { x = 200; y = 100; }
	PopupMenu m (x, y);
	int n = g_lib.albums.n < 13 ? g_lib.albums.n : 13;
	for (int a = 0; a < n; a++)
	{
		bool all = true; for (int i = 0; i < c.n; i++) if (!g_lib.album_has (a, g_lib.ph[c[i]].path)) all = false;
		char l[160]; snprintf (l, sizeof l, all ? "%s   \xE2\x9C\x93" : "%s", g_lib.albums[a].name);
		static char keep[13][160]; scpy (keep[a], l, sizeof keep[a]);
		m.add (keep[a], 10 + a);
	}
	if (n) m.separator ();
	m.add (TR ("New album..."), 1);
	int r = m.run ();
	if (r == 1) { if (one >= 0 && !g_selN) { sel_set (one, true); act_new_album (); sel_clear (); } else act_new_album (); free (c.a); refresh_all (); return; }
	if (r >= 10)
	{
		int a = r - 10;
		bool all = true; for (int i = 0; i < c.n; i++) if (!g_lib.album_has (a, g_lib.ph[c[i]].path)) all = false;
		for (int i = 0; i < c.n; i++) { if (all) g_lib.album_remove (a, g_lib.ph[c[i]].path); else g_lib.album_add (a, g_lib.ph[c[i]].path); }
		char s[200]; snprintf (s, sizeof s, all ? TR ("Taken out of %s") : TR ("Added to %s"), g_lib.albums[a].name); status_note (s);
		if (!all && g_selN) sel_clear ();
		lib_changed ();
	}
	free (c.a);
}

// ---- the trash -----------------------------------------------------------------------------------------------------------------------
static void act_delete (int one)
{
	Vec<int> c; if (!chosen (c, one)) { free (c.a); return; }
	char q[300];
	if (c.n == 1) snprintf (q, sizeof q, TR ("Move %s to the trash? (The File Viewer can bring it back.)"), base_name (g_lib.ph[c[0]].path));
	else snprintf (q, sizeof q, TR ("Move these %d photos to the trash? (The File Viewer can bring them back.)"), c.n);
	if (uk_messagebox (TR ("Photos"), q, MB_YESNO) != 1) { free (c.a); return; }
	int failed = 0;
	for (int i = 0; i < c.n; i++)
	{
		Photo &p = g_lib.ph[c[i]];
		if (!trash_move (p.path)) { failed++; continue; }
		g_th.forget (p.key ());
		g_lib.albums_path_changed (p.path, 0);
		p.alive = false;
	}
	// the viewer: on to the next one
	int vpos = g_view && !g_view->hidden ? g_view->pos : -1;
	free (c.a);
	sel_clear ();
	g_lib.compact (); g_lib.save ();
	lib_changed ();
	if (failed) uk_messagebox (TR ("Photos"), TR ("Some photos could not be moved to the trash."), MB_OK);
	if (vpos >= 0) { if (!g_list.n) close_viewer (); else open_viewer (vpos < g_list.n ? vpos : g_list.n - 1); }
}

// ---- a quarter turn to the left ----------------------------------------------------------------------------------------------------
// a JPEG with an EXIF orientation: that number changed (lossless); otherwise the pixels turned and written again
static bool set_exif_orientation (const char *path, int o)
{
	PicInfo pi; if (!pic_info (path, pi) || !pi.exifOff) return false;
	int len; char *b = file_read (path, &len); if (!b) return false;
	unsigned char *u = (unsigned char *) b; unsigned base = pi.exifOff + 10;
	bool done = false;
	if (base + 8 < (unsigned) len)
	{
		Tiff t { u + base, (unsigned) len - base, u[base] == 'I' };
		unsigned ifd0 = t.u32 (4), c = t.u16 (ifd0);
		for (unsigned i = 0; i < c && i < 400; i++)
		{
			unsigned e = ifd0 + 2 + i * 12;
			if (t.u16 (e) == 0x0112) { unsigned at = base + e + 8; if (t.le) { u[at] = (unsigned char) o; u[at + 1] = 0; } else { u[at] = 0; u[at + 1] = (unsigned char) o; } done = true; break; }
		}
	}
	if (done) done = kapi_save_file (path, b, (unsigned) len) >= 0;
	free (b);
	return done;
}
static void act_rotate (int one)
{
	Vec<int> c; if (!chosen (c, one)) { free (c.a); return; }
	static const int CCW[9] = { 0, 8, 0, 6, 0, 0, 1, 0, 3 };	// 1 -> 8 -> 3 -> 6 -> 1
	for (int i = 0; i < c.n; i++)
	{
		Photo &p = g_lib.ph[c[i]];
		int o = p.orient >= 1 && p.orient <= 8 ? p.orient : 1;
		bool ok = false;
		if (CCW[o]) ok = set_exif_orientation (p.path, CCW[o]);
		if (ok) { g_th.forget (p.key ()); p.orient = (unsigned char) CCW[o]; short t = p.w; p.w = p.h; p.h = t; continue; }
		// the pixels turned
		Pix full; if (!Thumbs::load_full (p.path, p.orient, full)) continue;
		rotate90 (full, 3); opaque (full);
		bool png = Editor::ends (p.path, ".png");
		unsigned n = 0; unsigned char *d = png ? pngsave::png_encode (full.px, full.w, full.h, false, &n) : (Editor::ends (p.path, ".jpg") || Editor::ends (p.path, ".jpeg")) ? pngsave::jpeg_encode (full.px, full.w, full.h, 94, &n) : 0;
		if (!d) { uk_messagebox (TR ("Photos"), TR ("This kind of picture cannot be turned here (open it in Paint)."), MB_OK); continue; }
		PicInfo pi; pic_info (p.path, pi);
		if (!png && pi.exifOff) { unsigned m; unsigned char *x = jpeg_with_exif (d, n, p.path, pi, &m); if (x) { delete[] d; d = x; n = m; } }
		g_th.forget (p.key ());
		if (kapi_save_file (p.path, d, n) >= 0) { p.size = n; p.orient = 1; p.w = (short) full.w; p.h = (short) full.h; }
		delete[] d;
	}
	free (c.a);
	g_lib.save ();
	if (g_view && !g_view->hidden) g_view->show (g_view->pos);
	lib_changed ();
}

// ---- Send by Mail ----------------------------------------------------------------------------------------------------------------------
static void act_mail (int one)
{
	Vec<int> c; if (!chosen (c, one)) { free (c.a); return; }
	if (c.n > 16) { uk_messagebox (TR ("Photos"), TR ("Mail takes 16 attachments at most: choose fewer photos."), MB_OK); free (c.a); return; }
	const char *dir = "RAM:/photos-mail";
	mkdirs (dir);
	if (!dir_exists (dir)) { dir = PH_DIR "/outbox"; mkdirs (dir); }
	char list[20000]; int k = 0;
	status_note (TR ("Making the copies for Mail..."));
	for (int i = 0; i < c.n; i++)
	{
		const Photo &p = g_lib.ph[c[i]];
		char out[400], name[200]; scpy (name, base_name (p.path), sizeof name); char *dot = strrchr (name, '.'); if (dot) *dot = 0;
		snprintf (out, sizeof out, "%s/%s.jpg", dir, name);
		Pix full, small;
		if (!Thumbs::load_full (p.path, p.orient, full)) continue;
		fit (full, small, 1920, 1920); full.free_ (); opaque (small);
		unsigned n = 0; unsigned char *d = pngsave::jpeg_encode (small.px, small.w, small.h, 85, &n);
		if (d && kapi_save_file (out, d, n) >= 0) k += snprintf (list + k, sizeof list - k, "%s\n", out);
		delete[] d;
	}
	free (c.a);
	char lp[200]; snprintf (lp, sizeof lp, "%s/attach.txt", dir);
	kapi_save_file (lp, list, (unsigned) k);
	char args[260]; snprintf (args, sizeof args, "--attach %s", lp);
	if (kapi_exec ("SD:apps/mail.app/main", args) < 0) uk_messagebox (TR ("Photos"), TR ("Mail could not be started."), MB_OK);
	else status_note (TR ("Opened in Mail."));
}

// ---- the wallpaper, the Clipboard, Paint --------------------------------------------------------------------------------------------
// the wallpaper's setting (SD:/etc/wallpaper.ini, the image mode) pointed at the photo. A photo not on SD: (another
// volume: not there at every start) is copied to SD:/res/wallpaper.<ext>; a photo the camera stored turned (its EXIF
// orientation: the wallpaper's painter does not read it) is written there turned, as SD:/res/wallpaper.jpg.
static void act_wallpaper (int pi)
{
	const Photo &p = g_lib.ph[pi];
	char img[200]; scpy (img, p.path, sizeof img);
	bool turned = p.orient > 1 && p.orient <= 8, away = !ipfx (p.path, "SD:/");
	if (turned || away)
	{
		const char *dot = strrchr (base_name (p.path), '.');
		char ext[16]; scpy (ext, dot ? dot + 1 : "jpg", sizeof ext); for (char *c = ext; *c; c++) *c = (char) lc (*c);
		// (the other wallpaper.* removed: one file only)
		static const char *const EX[] = { "jpg", "jpeg", "jpe", "png", "gif", "bmp", "webp", "pcx" };
		for (unsigned i = 0; i < sizeof EX / sizeof EX[0]; i++) { char o[60]; snprintf (o, sizeof o, "SD:/res/wallpaper.%s", EX[i]); kapi_remove (o); }
		bool ok = false;
		if (turned)
		{
			Pix full; if (Thumbs::load_full (p.path, p.orient, full)) { opaque (full); unsigned n = 0; unsigned char *d = pngsave::jpeg_encode (full.px, full.w, full.h, 92, &n);
				if (d) { ok = kapi_save_file ("SD:/res/wallpaper.jpg", d, n) >= 0; delete[] d; } }
			scpy (img, "SD:/res/wallpaper.jpg", sizeof img);
		}
		else
		{
			int len; char *b = file_read (p.path, &len);
			snprintf (img, sizeof img, "SD:/res/wallpaper.%s", ext);
			if (b) { ok = kapi_save_file (img, b, (unsigned) len) >= 0; free (b); }
		}
		if (!ok) { uk_messagebox (TR ("Photos"), TR ("The picture could not be copied to SD:/res for the wallpaper."), MB_OK); return; }
	}
	Wallpaper w; wp_load (w);
	w.mode = WP_IMAGE; scpy (w.image, img, sizeof w.image); w.tile = 0; w.tint = 0;
	wp_save (w);
	kapi_exec ("SD:apps/voronoy.app/main", "");
	status_note (TR ("The desktop's wallpaper is set."));
}
static void act_copy (int pi)
{
	Pix full; if (!Thumbs::load_full (g_lib.ph[pi].path, g_lib.ph[pi].orient, full)) return;
	Pix s; const Pix *p = &full;
	if (full.w > 4096 || full.h > 4096) { fit (full, s, 4096, 4096); p = &s; }
	opaque (*(Pix *) p);
	clip_set_image (p->px, p->w, p->h);
	status_note (TR ("Copied: the picture is on the Clipboard."));
}

static void act_share_menu (int one, int x, int y)
{
	Vec<int> c; int n = chosen (c, one); int first = n ? c[0] : -1; free (c.a);
	if (!n) return;
	PopupMenu m (x, y);
	m.add (TR ("Send by Mail..."), 1);
	m.add (TR ("Copy"), 2, n == 1);
	m.add (TR ("Set as the wallpaper"), 3, n == 1);
	m.add (TR ("Open in Paint"), 4, n == 1);
	m.add (TR ("Export as a PDF..."), 5);
	switch (m.run ())
	{
	case 1: act_mail (one); break;
	case 2: act_copy (first); break;
	case 3: act_wallpaper (first); break;
	case 4: kapi_exec ("SD:apps/paint.app/main", g_lib.ph[first].path); break;
	case 5: act_pdf (-1); break;
	}
}

// ---- a photo's own menu (a right click) -----------------------------------------------------------------------------------------------
static void photo_menu (int pi, int x, int y)
{
	bool many = g_selN > 1;
	const Photo &p = g_lib.ph[pi];
	PopupMenu m (x, y);
	if (!many) m.add (TR ("Open"), 1);
	m.add (TR ("Slideshow from here"), 2);
	m.separator ();
	m.add (p.fav && !many ? TR ("Remove from the favourites") : TR ("Add to the favourites"), 3);
	m.add (TR ("Add to an album..."), 4);
	m.add (TR ("Rotate to the left"), 5, true, "R");
	if (!many) m.add (TR ("Edit..."), 6, true, "E");
	m.separator ();
	m.add (TR ("Send by Mail..."), 7);
	if (!many) { m.add (TR ("Copy"), 8); m.add (TR ("Set as the wallpaper"), 9); m.add (TR ("Open in Paint"), 10); m.add (TR ("Show in the File Viewer"), 11); }
	m.separator ();
	m.add (TR ("Move to the trash"), 12, true, "Del");
	int one = g_selN ? -1 : pi;
	switch (m.run ())
	{
	case 1: for (int i = 0; i < g_list.n; i++) if (g_list[i] == pi) { open_viewer (i); break; } break;
	case 2: for (int i = 0; i < g_list.n; i++) if (g_list[i] == pi) { act_slideshow (i); break; } break;
	case 3: act_favourite (one); break;
	case 4: act_add_to_album (one, x, y); break;
	case 5: act_rotate (one); break;
	case 6: open_editor (pi); break;
	case 7: act_mail (one); break;
	case 8: act_copy (pi); break;
	case 9: act_wallpaper (pi); break;
	case 10: kapi_exec ("SD:apps/paint.app/main", p.path); break;
	case 11: kapi_exec ("SD:apps/fileviewer.app/main", p.path); break;
	case 12: act_delete (one); break;
	}
}

// ---- a PDF: a contact sheet ------------------------------------------------------------------------------------------------------------
// each page drawn as a picture (A4, 150 dpi): the title, 3 x 4 photos (whole, centred in their cell), their dates
static void act_pdf (int album)
{
	Vec<int> c;
	char title[160];
	if (album >= 0)
	{
		for (int k = 0; k < g_lib.albums[album].paths.n; k++) { int i = g_lib.find (g_lib.albums[album].paths[k]); if (i >= 0 && !g_lib.ph[i].offline) c.push (i); }
		// by date, the oldest first (a story)
		for (int i = 1; i < c.n; i++) { int t = c[i]; int j = i - 1; while (j >= 0 && g_lib.ph[c[j]].when () > g_lib.ph[t].when ()) { c[j + 1] = c[j]; j--; } c[j + 1] = t; }
		scpy (title, g_lib.albums[album].name, sizeof title);
	}
	else
	{
		chosen (c);
		if (!c.n) for (int i = 0; i < g_list.n; i++) c.push (g_list[i]);
		for (int i = 0; i < c.n / 2; i++) { int t = c[i]; c[i] = c[c.n - 1 - i]; c[c.n - 1 - i] = t; }
		scpy (title, src_title (), sizeof title);
	}
	if (!c.n) { free (c.a); uk_messagebox (TR ("Photos"), TR ("There is no photo to put in a PDF."), MB_OK); return; }
	char out[400], def[200]; snprintf (def, sizeof def, "%s.pdf", title);
	for (char *s = def; *s; s++) if (*s == '/' || *s == ':') *s = '-';
	if (!uk_file_save (out, sizeof out, "SD:/Documents", def, "PDF documents|*.pdf|All files|*")) { free (c.a); return; }
	if (!Editor::ends (out, ".pdf")) { int k = (int) strlen (out); if (k < 390) strcpy (out + k, ".pdf"); }
	status_note (TR ("Making the PDF..."));
	const int PW = 1240, PH = 1754, M = 90, COLS = 3, ROWS = 4;		// A4 at 150 dpi
	const float PTW = 595.3f, PTH = 841.9f;
	pdfw::Writer w;
	w.info (title, "", 0, "Photos (Onyx)");
	Pix page; page.alloc (PW, PH);
	if (!page.px) { free (c.a); return; }
	Canvas cv; cv.adopt (page.px, PW, PH);
	int per = COLS * ROWS, pages = (c.n + per - 1) / per;
	int cellW = (PW - 2 * M - (COLS - 1) * 30) / COLS, cellH = (PH - 2 * M - 140 - (ROWS - 1) * 36) / ROWS;
	for (int pg = 0; pg < pages; pg++)
	{
		cv.clear (0xFFFFFF);
		// the title (larger letters: drawn twice the size by the H1 face, on the page's own scale)
		text (cv, M, M - 10, title, 0x202428, F_H1, 1);
		char sub[80]; snprintf (sub, sizeof sub, c.n == 1 ? TR ("1 photo") : TR ("%d photos"), c.n);
		if (pages > 1) snprintf (sub + strlen (sub), sizeof sub - strlen (sub), TR ("  \xC2\xB7  page %d of %d"), pg + 1, pages);
		text (cv, M, M + 26, sub, 0x70757C, F_MID);
		cv.fillRect (M, M + 56, PW - 2 * M, 2, 0xD0D4D8);
		for (int k = 0; k < per; k++)
		{
			int i = pg * per + k; if (i >= c.n) break;
			int cx = M + (k % COLS) * (cellW + 30), cy = M + 90 + (k / COLS) * (cellH + 36);
			Pix full, f;
			const Photo &p = g_lib.ph[c[i]];
			if (Thumbs::load_full (p.path, p.orient, full)) { fit (full, f, cellW, cellH - 28, true); full.free_ (); }
			if (f.px) Thumbs::blit (cv, f.px, cx + (cellW - f.w) / 2, cy + (cellH - 28 - f.h) / 2, f.w, f.h);
			char d[100] = ""; if (p.when ()) fmt_day (p.when (), d, sizeof d); else scpy (d, base_name (p.path), sizeof d);
			text_c (cv, cx, cy + cellH - 24, cellW, 20, d, 0x50555C, F_SMALL);
		}
		opaque (page);
		w.begin_page (PTW, PTH);
		w.image (page.px, PW, PH, 0, 0, PTW, PTH, true, 88);
		w.end_page ();
	}
	free (c.a);
	unsigned n = 0; unsigned char *pdf = w.finish (&n);
	int ok = pdf ? kapi_save_file (out, pdf, n) : -1;
	delete[] pdf;
	if (ok < 0) uk_messagebox (TR ("Photos"), TR ("The PDF could not be written."), MB_OK);
	else { char s[300]; snprintf (s, sizeof s, TR ("Saved: %s"), base_name (out)); status_note (s); }
}

// ---- printing: each photo a page, as large as what the printer prints of the paper takes it, centred ----------------------------------
// (one: the photo shown in the viewer; else the photos selected). The Print dialog is the library's (printerkit/printerkit.h).
static void act_print (int one = -1)
{
	Vec<int> c;
	if (one >= 0) c.push (one); else chosen (c);
	if (!c.n) { free (c.a); uk_messagebox (TR ("Photos"), TR ("Select the photos to print first."), MB_OK); return; }
	char title[160];
	if (c.n == 1) scpy (title, base_name (g_lib.ph[c[0]].path), sizeof title); else snprintf (title, sizeof title, TR ("%d photos"), c.n);
	PrintSetup ps; print_setup_default (&ps);
	{ const Photo &p = g_lib.ph[c[0]]; bool turn = p.orient >= 5; if ((turn ? p.h : p.w) > (turn ? p.w : p.h)) print_setup_paper (&ps, 0, PRINT_LANDSCAPE); }
	PrintDialogInfo di = { sizeof di, title, c.n, 0, 0, 0, 0 };
	if (!print_dialog (&ps, &di)) { free (c.a); return; }
	PrintJob *j = print_begin (&ps, title);
	if (j)
	{
		status_note (TR ("Preparing the pages..."));
		float l = ps.margin_l > 18 ? ps.margin_l : 18, t = ps.margin_t > 18 ? ps.margin_t : 18;
		float r = ps.margin_r > 18 ? ps.margin_r : 18, b = ps.margin_b > 18 ? ps.margin_b : 18;
		float aw = ps.paper_w - l - r, ah = ps.paper_h - t - b;
		for (int i = 0; i < c.n; i++)
		{
			if (!print_page (j, 0, 0)) continue;			// (not one of the pages to print: not even read)
			const Photo &p = g_lib.ph[c[i]];
			Pix full; if (!Thumbs::load_full (p.path, p.orient, full)) continue;
			opaque (full);
			float k = aw / full.w < ah / full.h ? aw / full.w : ah / full.h;
			float w = full.w * k, h = full.h * k;
			print_image (j, full.px, full.w, full.h, l + (aw - w) / 2, t + (ah - h) / 2, w, h, PRINT_IMG_ALPHA | PRINT_IMG_PHOTO);
			full.free_ ();
		}
	}
	free (c.a);
	if (!j || print_end (j) < 0) uk_messagebox (TR ("Photos"), TR ("The photos could not be put in the print queue."), MB_OK);
	else status_note (TR ("In the print queue."));
}

// ---- the slideshow ---------------------------------------------------------------------------------------------------------------------------
static volatile long g_fsKey; static volatile int g_fsClick, g_fsMoved;
static void fs_key (unsigned long, int ev, gui_value v) { if (ev == GUI_EVENT_KEY) g_fsKey = (long) v; }
static void fs_ptr (unsigned long, int ev, gui_value v)
{
	if (ev == GUI_EVENT_PTR_MOVE) g_fsMoved = 1;
	if (ev == GUI_EVENT_PTR_DOWN && (GUI_PTR_CHANGED (v) & 1)) g_fsClick = 1;
}
static FullDone *g_ssCame;			// the slideshow's next photo, decoded
static void (*g_ssPrevOnFull) (FullDone *);
static void ss_full (FullDone *d) { if (g_ssCame) { g_ssCame->pix.free_ (); delete g_ssCame; } g_ssCame = new FullDone; g_ssCame->index = d->index; g_ssCame->failed = d->failed; g_ssCame->pix.take (d->pix); }

static void act_slideshow (int from)
{
	Vec<int> show;
	if (g_src == SRC_ALBUMS) { free (show.a); return; }
	if (g_selN > 1) { for (int i = 0; i < g_list.n; i++) if (selected (g_list[i])) show.push (g_list[i]); from = 0; }
	else for (int i = 0; i < g_list.n; i++) show.push (g_list[i]);
	if (!show.n) { free (show.a); return; }
	if (from < 0 || from >= show.n) from = 0;
	int W, H;
	unsigned *fb = uk_win_fullscreen_begin (&W, &H);
	if (!fb) { free (show.a); return; }
	uk_win_on_key (fs_key); uk_win_on_pointer (fs_ptr);
	g_fsKey = 0; g_fsClick = 0; g_fsMoved = 0;
	g_ssPrevOnFull = g_th.onFull; g_th.onFull = ss_full; g_ssCame = 0;
	Pix cur, curF, prevF; curF.alloc (W, H); prevF.alloc (W, H);
	if (!curF.px || !prevF.px) { uk_win_fullscreen_end (); g_th.onFull = g_ssPrevOnFull; free (show.a); g_root->attach (); return; }
	for (int i = 0; i < W * H; i++) fb[i] = curF.px[i] = prevF.px[i] = 0;
	kapi_present_fb ();
	int at = from; bool paused = false, want = true, fading = false, hudShown = false;
	unsigned shownT = 0, fadeT = 0, hudT = kapi_get_ticks ();
	const unsigned STAY = 450, FADE = 60, HUD = 250;	// ticks (10 ms): 4.5 s a photo, 0.6 s to melt, the help 2.5 s
	auto place = [&] (const Pix &p, unsigned *dst) {	// the photo fitted to the screen, black around
		for (int i = 0; i < W * H; i++) dst[i] = 0;
		if (!p.px) return;
		float s = (float) W / p.w; if (p.h * s > H) s = (float) H / p.h;
		int w = (int) (p.w * s), h = (int) (p.h * s); if (w < 1) w = 1; if (h < 1) h = 1;
		scale_into (p.px, p.w, p.h, 0, 0, p.w, p.h, dst + (long) ((H - h) / 2) * W + (W - w) / 2, W, w, h);
	};
	auto hud = [&] (unsigned *dst) {
		Canvas cv; cv.adopt (dst, W, H);
		char s[160]; snprintf (s, sizeof s, TR ("%d / %d%s   \xC2\xB7   Esc: back   \xC2\xB7   \xE2\x86\x90 \xE2\x86\x92   \xC2\xB7   Space: %s"), at + 1, show.n, paused ? TR ("  (paused)") : "", paused ? TR ("go on") : TR ("pause"));
		int w = tw (s) + 40;
		fill_round (cv, (W - w) / 2, H - 70, w, 36, 16, 0x000000, 170);
		text_c (cv, (W - w) / 2, H - 70, w, 36, s, 0xFFFFFF);
	};
	for (;;)
	{
		kapi_pump_wait (10);
		if (uk_quit ()) break;
		unsigned now = kapi_get_ticks ();
		long k = g_fsKey; g_fsKey = 0;
		if (k == 27 || g_fsClick) break;
		if (g_fsMoved) { g_fsMoved = 0; hudT = now; }
		bool redraw = false;
		if (k == ' ') { paused = !paused; hudT = now; redraw = true; shownT = now; }
		if (k == KEY_RIGHT || k == KEY_PGDN) { at = (at + 1) % show.n; want = true; hudT = now; }
		if (k == KEY_LEFT || k == KEY_PGUP) { at = (at + show.n - 1) % show.n; want = true; hudT = now; }
		if (want) { want = false; g_th.want_full (show[at]); }
		if (g_ssCame)
		{
			FullDone *d = g_ssCame; g_ssCame = 0;
			if (d->index == show[at])
			{
				unsigned *t = prevF.px; prevF.px = curF.px; curF.px = t;		// (the one shown melts away)
				cur.free_ (); if (!d->failed) cur.take (d->pix);
				place (cur, curF.px);
				fading = true; fadeT = now; shownT = now; redraw = true;
			}
			d->pix.free_ (); delete d;
		}
		if (!paused && cur.px && !fading && now - shownT > STAY) { at = (at + 1) % show.n; want = true; shownT = now; }
		bool hudOn = now - hudT < HUD;
		if (hudOn != hudShown) { hudShown = hudOn; redraw = true; }
		if (fading || redraw)
		{
			unsigned t = now - fadeT;
			if (fading && t >= FADE) fading = false;
			if (fading) { int a = (int) (t * 256 / FADE); for (int i = 0; i < W * H; i++) fb[i] = uk_mix (prevF.px[i], curF.px[i], a); }
			else memcpy (fb, curF.px, (size_t) W * H * 4);
			if (hudOn) hud (fb);
			kapi_present_fb ();
		}
	}
	uk_win_fullscreen_end ();
	g_th.onFull = g_ssPrevOnFull;
	if (g_ssCame) { g_ssCame->pix.free_ (); delete g_ssCame; g_ssCame = 0; }
	free (show.a);
	g_root->attach ();
	refresh_all ();
}

} // namespace photos

#endif
