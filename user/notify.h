//
// notify.h -- desktop notifications. notify (title, text) sends an IPC message to the
// "notify" service (the notifyd app), which shows a bubble under the menu bar that fades
// in, stays ~4 s, then fades out. notifyd is launched on demand if it is not running.
//
//   #include "notify.h"
//   notify ("File Viewer", "3 items pasted");
//
#ifndef _notify_h
#define _notify_h
#include "kapi.h"

#define NOTIFY_SERVICE	"notify"
#define NOTIFY_MSG_SHOW	1		// payload: title '\0' text '\0'
#define NOTIFY_MAX	500		// payload bytes (IPC limit 512)

static inline int notify (const char *title, const char *text)
{
	int pid = kapi_ipc_lookup (NOTIFY_SERVICE);
	if (pid == 0)
	{
		kapi_launch ("notifyd");			// start the service, wait for it
		for (int i = 0; i < 40 && pid == 0; i++) { kapi_msleep (50); pid = kapi_ipc_lookup (NOTIFY_SERVICE); }
		if (pid == 0) return 0;
	}
	char msg[NOTIFY_MAX];
	int n = 0;
	for (int i = 0; title && title[i] && n < 80; i++) msg[n++] = title[i];
	msg[n++] = '\0';
	for (int i = 0; text && text[i] && n < NOTIFY_MAX - 1; i++) msg[n++] = text[i];
	msg[n++] = '\0';
	return kapi_mailbox_send (pid, NOTIFY_MSG_SHOW, msg, (unsigned) n);
}

#endif
