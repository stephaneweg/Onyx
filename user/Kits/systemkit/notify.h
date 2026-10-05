//
// notify.h -- desktop notifications. notify (title, text) sends an IPC message to the
// "notify" service (the notifyd app), which shows a bubble under the menu bar that fades
// in, stays ~4 s, then fades out. notifyd is launched on demand if it is not running.
//
//   #include "notify.h"
//   notify ("File Viewer", "3 items pasted");
//   notify_action ("Updates", "3 updates available", "control pkgman");   // a click: that app, with its
//                                                                         // arguments (a longer stay)
//
#ifndef _notify_h
#define _notify_h
#include "appkit/appkit.h"
#include "sk_api.h"

#define NOTIFY_SERVICE	"notify"
#define NOTIFY_MSG_SHOW	1		// payload: title '\0' text '\0' [action '\0': "app args", run on a click]
#define NOTIFY_MAX	500		// payload bytes (IPC limit 512)
SK_API int notify_action (const char *title, const char *text, const char *action);
SK_API int notify (const char *title, const char *text);

#if defined (SK_BODIES_INLINE) && !defined (SK_IMPL)
#include "notify.inc"
#endif

#endif
