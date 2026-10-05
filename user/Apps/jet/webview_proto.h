//
// web/webview_proto.h -- Web's web view: a page shown INSIDE another app's window (roadmap step 5 of
// docs/08-WEBKIT-PORT.md, "a host window + a web view"; Mail's HTML messages first). The host starts the
// browser's one program as an applet (applet_proto.h):
//
//   SD:/apps/jet.app/main --applet <surface id> <host pid> <host's IPC service>
//
// (main.cpp hands that run to webview_main, webview.cpp). The view's uikit Root adopts the host's surface;
// the page fills its top-left w x h (WV_SIZE: the surface is made once, as big as the work area, so the
// host's box can grow without a new surface); JavaScript is off; a click on a link is not followed but
// told to the host (WV_LINK). The applet protocol's AP_* messages carry the pointer, the keys, the
// pictures (AP_PRESENT: copy the surface's w x h) and the end (AP_CLOSE / AP_EXIT); these are the web
// view's own, over the same mailboxes (<= 512 bytes; strings NUL-terminated):
//
//   host -> view   WV_SIZE     int w, h: the page's size (<= the surface's)
//                  WV_HTML     char path[]: a file of HTML (UTF-8) to show -- written by the host (e.g.
//                              RAM:/mailview-<pid>.html), read once, not removed: no size limit
//                  WV_URL      char url[]: load this address (http:, https:, file:)
//                  WV_COMMAND  char name[]: "Copy", "SelectAll" (the page's selection)
//                  WV_PING     -: (the host sees whether the view still runs: its send fails if not)
//   view -> host   WV_LINK     char url[]: a link clicked (or a window asked for): the host decides
//                  WV_STATUS   char text[]: the link under the pointer ("" none)
//                  WV_LOADED   -: the page is loaded (drawn): the host may show it
//                  WV_ENDED    -: the engine could not start, or its web process ended: the host shows
//                              the content its own way
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see LICENSE).
//
#ifndef _web_webview_proto_h
#define _web_webview_proto_h

enum
{
	WV_SIZE = 60, WV_HTML = 61, WV_URL = 62, WV_COMMAND = 63, WV_PING = 64,
	WV_LINK = 70, WV_STATUS = 71, WV_LOADED = 72, WV_ENDED = 73
};

#define WV_PROGRAM	"SD:/apps/jet.app/main"

#endif
