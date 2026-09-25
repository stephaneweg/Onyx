//
// ask.h -- a non-blocking Yes / No question in its own small window (apps/ask), for
// apps too small to host a wtk modal dialog (the Shelf).
//   void *h = ask_begin ("Title", "Message", "Yes", "No");	// 0 = could not start
//   ... each frame: int r = ask_poll (h);  -1 = still open, else 1 = Yes / 0 = No
//                                           (the handle is freed once answered)
//
#ifndef _ask_h
#define _ask_h

#include "kapi.h"

static inline void *ask_begin (const char *title, const char *msg, const char *yes, const char *no)
{
	static char args[400];
	int p = 0;
	const char *part[4] = { title, msg, yes, no };
	for (int k = 0; k < 4; k++)
	{
		if (k) args[p++] = '|';
		for (int i = 0; part[k] && part[k][i] && p < (int) sizeof args - 2; i++)
			args[p++] = part[k][i] == '|' ? '/' : part[k][i];
	}
	args[p] = '\0';
	return kapi_spawn ("SD:apps/ask.app/main", args, 0, 0);
}

static inline int ask_poll (void *h)
{
	if (h == 0) return 0;
	if (!kapi_proc_done (h)) return -1;
	return kapi_wait (h) == 1 ? 1 : 0;
}

#endif
