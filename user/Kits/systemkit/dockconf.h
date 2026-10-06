//
// dockconf.h -- the dock's settings (SD:/etc/dock.ini), shared by the dock (apps/dock) and the
// Control Panel's Panel applet (apps/dockconf), which writes them and tells the dock to read them
// again (IPC service "dock", DOCK_MSG_RELOAD). The file, one setting a line:
//
//     drawer   = Productivity, tinypad    a drawer: the apps' group (the "category" of their
//                                         app.txt) and its main app (its icon is the drawer's; a
//                                         click on it starts the app, the strip above opens the
//                                         drawer). In this order, left to right.
//     launcher = terminal                 a quick launcher after the drawers (a click starts it,
//                                         or brings it back)
//     desk     = Main                     a workspace (virtual desktop) and its name: 1 to
//                                         DOCK_MAXDESKS of them
//
#ifndef _dockconf_h
#define _dockconf_h
#include "appkit/appkit.h"
#include "sk_api.h"


#define DOCK_INI		"SD:/etc/dock.ini"
#define DOCK_SERVICE		"dock"
#define DOCK_MSG_RELOAD		1		// (no payload) read dock.ini and theme.txt again
#define DOCK_MAXDRAWERS		8
#define DOCK_MAXLAUNCHERS	6
#define DOCK_MAXDESKS		6

struct DockDrawer { char cat[24]; char app[32]; };
struct DockConf
{
	DockDrawer drawer[DOCK_MAXDRAWERS]; int ndrawers;
	char launcher[DOCK_MAXLAUNCHERS][32]; int nlaunchers;
	char desk[DOCK_MAXDESKS][24]; int ndesks;
};
SK_API void dc_copy (char *d, const char *s, int cap);	// s (0: "") copied into d, cut at cap - 1 characters, NUL-terminated
SK_API void dockconf_defaults (DockConf &c);	// the dock without a file: 6 drawers (Productivity .. Demos), the terminal and fileviewer launchers, desks "1" .. "4"
// A value's two parts: "Productivity, tinypad" -> "Productivity" and "tinypad".
SK_API void dc_split (const char *v, char *a, int acap, char *b, int bcap);
// -> false: no file (the defaults)
SK_API bool dockconf_load (DockConf &c);
SK_API int dc_put (char *o, int p, int cap, const char *s);
SK_API bool dockconf_save (const DockConf &c);
// The dock takes its settings (and the theme) again: it is started again -- a dock that only read
// dock.ini again (DOCK_MSG_RELOAD) showed a new drawer or launcher but did not always act on it
// (a click started nothing, a new group was not seen); a new one has them all. Nothing when no
// dock runs (the user may have taken it out of etc/autostart).
SK_API int dock_running (void);
SK_API void dock_reload (void);

#if defined (SK_BODIES_INLINE) && !defined (SK_IMPL)
#include "dockconf.inc"
#endif

#endif
