//
// web/engine_mock.cpp -- engine.h without an engine, for the desktop simulator (tools/tests/desktop_sim:
// the window tried and photographed on the PC): the page is a picture, SD:/wktest/page.png (a page
// WebKit painted: tools/webkit/wk2test), else a white page; the address and the title are the URL's.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see LICENSE).
//
#include "kapi.h"
#include "img/imgload.hpp"
#include "engine.h"
#include <string.h>

static const EngineClient *s_client;
static unsigned *s_px; static int s_pw, s_ph;
static int s_w, s_h;
static double s_zoom = 1;
static bool s_told;

bool engine_is_auxiliary (int, char **) { return false; }
int  engine_auxiliary_main (int, char **) { return 1; }

bool engine_init (const EngineClient *c, int w, int h)
{
	s_client = c; s_w = w; s_h = h;
	ImgFrames im;
	if (img_load ("SD:/wktest/page.png", &im)) { s_px = im.px[0]; s_pw = im.w; s_ph = im.h; }
	return true;
}

void engine_new_window (const char *url) { kapi_exec ("SD:/apps/jet.app/main", url); }

void engine_cycle ()
{
	if (s_told) return;
	s_told = true;
	s_client->titleChanged ("Koton Studio");
	s_client->urlChanged ("https://kotonstudio.com/");
	s_client->historyChanged (true, false);
	s_client->loadingChanged (false, 1);
	s_client->needsDisplay (0, 0, s_w, s_h);
}

void engine_resize (int w, int h) { s_w = w; s_h = h; }

void engine_paint (unsigned *px, int stride, int x, int y, int w, int h)
{
	for (int j = y; j < y + h; j++)
		for (int i = x; i < x + w; i++)
			px[j * stride + i] = s_px && i < s_pw && j < s_ph ? s_px[j * s_pw + i] & 0x00FFFFFFu : 0x00FFFFFFu;
}

void engine_set_active (bool) {}
void engine_set_compositing (bool) {}
void engine_load (const char *) { s_told = false; }
void engine_back () {}
void engine_forward () {}
void engine_reload () {}
void engine_stop () {}
void engine_zoom (double f) { s_zoom = f; }
double engine_zoom_factor () { return s_zoom; }
void engine_command (const char *) {}
void engine_mouse (int, int, int, int, unsigned) {}
void engine_wheel (int, int, int, unsigned) {}
void engine_key (long, unsigned) {}
void engine_mouse_leave () {}
void engine_popup_select (int) {}
void engine_find (const char *, bool) { s_client->findResult (3); }
void engine_find_done () {}
void engine_hit (char *link, int, char *image, int) { strcpy (link, "https://kotonstudio.com/fr/#features"); image[0] = 0; }
void engine_download_url (const char *, const char *) {}
void engine_download_cancel (int) {}
int engine_cursor () { return 0; }
void engine_set_clipboard (void (*) (const char *, unsigned long), unsigned long (*) (char *, unsigned long), unsigned (*) ()) {}
void engine_load_html (const char *, const char *) { s_told = false; }
void engine_set_scripts (bool) {}
void engine_set_link_handler (void (*) (const char *)) {}
int  engine_spawn_self (const char *, int *, int *) { return 0; }
