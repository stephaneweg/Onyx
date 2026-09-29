//
// applet_proto.h -- the Control Panel's applets (apps/control): an applet is a wtk app shown
// INSIDE the Control Panel's window instead of in a window of its own. The host (apps/control)
// makes a shared surface (kapi v35) the size of its pane and starts the applet with the
// arguments "--applet <surface id> <host pid>"; the applet's wtk::Root then draws into that
// surface (wtk/root.cpp, the applet mode); the host copies it into its window when told and
// sends it the pointer and the keys. Messages over the kernel's mailboxes (kapi v35 / v40,
// <= 512 bytes), from / to the pids:
//
//   applet -> host   AP_HELLO    int w, h: the applet is up (its Root the surface's size)
//                    AP_PRESENT  int x, y, w, h: it drew (0 0 0 0: all of it) -- copy it
//                    AP_EXIT     it is ending: stop reading the surface
//                    AP_THEME    it applied a new theme (SD:/etc/theme.txt): the host takes it
//                                and starts the applet again (in the new colours)
//   host -> applet   AP_PTR      struct ApPtr: the pointer (a GUI_EVENT_PTR_* event, pane
//                                coordinates; x < 0: it left the pane)
//                    AP_KEY      struct ApKey: a key typed (a character or a KEY_* code)
//                    AP_CLOSE    please end (the user went back to the applets' list)
//
// The host is the IPC service AP_SERVICE: an applet whose host is gone ends by itself. The
// surface's frames live as long as either process maps it (kernel v65: its users).
//
#ifndef _applet_proto_h
#define _applet_proto_h

#define AP_SERVICE	"control"

enum
{
	AP_HELLO = 40, AP_PRESENT = 41, AP_EXIT = 42, AP_THEME = 43,
	AP_PTR = 50, AP_KEY = 51, AP_CLOSE = 52
};

struct ApPtr { int event, x, y, buttons, changed, wheel; };
struct ApKey { int key; unsigned mods; };

#endif
