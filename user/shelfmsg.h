//
// shelfmsg.h -- tell the Shelf (IPC service "shelf") that a file or folder moved, so
// its references follow: shelf_moved ("SD:/a/x.txt", "SD:/b/x.txt"). Items inside a
// moved folder follow too. No-op if the Shelf is not running. Used by the File Viewer
// (drag & drop moves, Cut / Paste, Rename).
//
#ifndef _shelfmsg_h
#define _shelfmsg_h

#include "kapi.h"

#define SHELF_SERVICE		"shelf"
#define SHELF_MSG_MOVED		1	// payload: "<from>\0<to>\0"

static inline void shelf_moved (const char *from, const char *to)
{
	int pid = kapi_ipc_lookup (SHELF_SERVICE);
	if (pid == 0) return;
	char buf[512]; int p = 0;
	for (int i = 0; from[i] && p < 254; i++) buf[p++] = from[i];
	buf[p++] = '\0';
	for (int i = 0; to[i] && p < 510; i++) buf[p++] = to[i];
	buf[p++] = '\0';
	kapi_mailbox_send (pid, SHELF_MSG_MOVED, buf, (unsigned) p);
}

#endif
