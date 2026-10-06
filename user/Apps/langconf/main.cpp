//
// langconf -- the Control Panel's Language & Region applet (applet_proto.h; alone, a window of its
// own). The language of the programs' words: the ones Onyx speaks (systemkit/locale.h), one taken
// when clicked and kept in SD:/etc/system.ini ("language=") -- a program takes it when it starts
// (uikit/lang.h's TR ()): the ones already open keep theirs until they are started again. The region:
// the time zone (the clock's offset at once, the summer time counted; kept in system.ini: "zone=",
// "timezone=").
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <stdio.h>
#include <string.h>
#include "appkit/appkit.h"
#include "systemkit/systemkit.h"
#include "uikit/uikit.h"
#include "fontkit/uikitface.h"		// FreeType's text (DejaVu Sans) for every widget

using namespace uikit;

#define W	700
#define H	470

static GroupBox *g_gbLang, *g_gbZone;
static ListBox *g_lbLang, *g_lbZone;
static Label *g_lLang[3], *g_lZone[2], *g_status;

// "Brussels  (UTC+2, summer time)" -- the cities are the kit's, in English: the ones with a name of their own
// in another language are words of the catalogue.
// TR: Brussels
// TR: Vienna
// TR: Warsaw
// TR: London
// TR: Lisbon
// TR: Athens
static void zone_label (int z, char *out, int cap)
{
	char utc[16]; locale_zone_utc (z, utc, sizeof utc);
	const char *city = TR (locale_zone_city (z));
	if (!strcmp (locale_zone_city (z), "UTC")) snprintf (out, cap, "UTC  (%s)", TR ("no summer time"));
	else if (locale_zone_summer (z)) snprintf (out, cap, "%s  (%s, %s)", city, utc, TR ("summer time"));
	else snprintf (out, cap, "%s  (%s)", city, utc);
}

// Every text, in the language in use (again when it changes).
static void texts (void)
{
	snprintf (g_gbLang->title, sizeof g_gbLang->title, "%s", TR ("Language"));
	snprintf (g_gbZone->title, sizeof g_gbZone->title, "%s", TR ("Time zone"));
	g_lLang[0]->setText (TR ("Click a language: the programs started"));
	g_lLang[1]->setText (TR ("from now on speak it. The ones already"));
	g_lLang[2]->setText (TR ("open keep theirs until started again."));
	g_lZone[0]->setText (TR ("Click a city of your time zone: the clock"));
	g_lZone[1]->setText (TR ("follows at once."));
	int sel = g_lbZone->sel, top = g_lbZone->top;
	g_lbZone->clear ();
	for (int z = 0; z < locale_zone_count (); z++) { char s[64]; zone_label (z, s, sizeof s); g_lbZone->add (s); }
	if (sel >= 0) g_lbZone->setSel (sel);
	g_lbZone->top = top;
	g_gbLang->invalidate (true); g_gbZone->invalidate (true);
}

static void on_lang (Widget &)
{
	int i = g_lbLang->sel;
	if (i < 0 || i >= locale_language_count ()) return;
	const char *code = locale_language_code (i);
	if (!locale_set_language (code)) { g_status->setText (TR ("The language could not be written (SD:/etc/system.ini).")); return; }
	uk_lang_load (code);				// (this applet's own words: at once)
	texts ();
	g_status->setText (TR ("Language taken: the programs you start now speak it."));
}

static void on_zone (Widget &)
{
	int z = g_lbZone->sel;
	if (z < 0 || z >= locale_zone_count ()) return;
	g_status->setText (locale_set_zone (z) ? TR ("Time zone taken, and set at every boot.") : TR ("Time zone taken (system.ini not written)."));
}

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);		// (before the widgets; false: the bitmap font)
	uk_lang_init ();
	Root root (W, H, TR ("Language & Region"));
	if (root.canvas.px == 0) return 1;
	int X = root.width > W ? (root.width - W) / 2 : 0;

	g_gbLang = new GroupBox (X + 10, 8, W - 20, 150, "");
	root.addChild (g_gbLang);
	int ct = g_gbLang->contentTop () + 6;
	g_lbLang = new ListBox (14, ct, 280, 100, on_lang); g_gbLang->addChild (g_lbLang);
	for (int i = 0; i < locale_language_count (); i++) g_lbLang->add (locale_language_name (i));	// (each in its own language)
	g_lbLang->setSel (locale_language_index ());
	for (int k = 0; k < 3; k++) { g_lLang[k] = new Label (310, ct + k * 20, 360, 20, "", C_TEXT, g_gbLang->bg); g_gbLang->addChild (g_lLang[k]); }

	g_gbZone = new GroupBox (X + 10, 168, W - 20, 226, "");
	root.addChild (g_gbZone);
	ct = g_gbZone->contentTop () + 6;
	g_lbZone = new ListBox (14, ct, 280, 176, on_zone); g_gbZone->addChild (g_lbZone);
	for (int k = 0; k < 2; k++) { g_lZone[k] = new Label (310, ct + k * 20, 360, 20, "", C_TEXT, g_gbZone->bg); g_gbZone->addChild (g_lZone[k]); }
	texts ();
	g_lbZone->setSel (locale_zone ());

	g_status = new Label (X + 12, 404, W - 24, 22, "", C_DIS, root.bg);
	root.addChild (g_status);
	root.run ();
	return 0;
}
