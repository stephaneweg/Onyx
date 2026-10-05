//
// ask.h -- a non-blocking Yes / No question (or a line of text) in its own small window
// (apps/ask), for apps too small to host a uikit modal dialog (the dock).
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

// A line of text asked the same way (a name: the dock's tabs), started from `text`; the answer
// comes back on a pipe (the window's stdout). One at a time:
//   if (ask_text_begin ("Rename tab", "Name:", "Rename", "Cancel", "Documents")) ...
//   ... each frame: int r = ask_text_poll (out, cap);  -1 = still open, 1 = OK (out = the text),
//                                                       0 = cancelled
static void *s_askProc, *s_askPipe;
static inline bool ask_text_begin (const char *title, const char *msg, const char *ok, const char *cancel,
				   const char *text)
{
	if (s_askProc != 0) return false;
	static char args[460];
	int p = 0;
	const char *part[5] = { title, msg, ok, cancel, text };
	for (int k = 0; k < 5; k++)
	{
		if (k) args[p++] = '|';
		if (k == 4) args[p++] = '=';
		for (int i = 0; part[k] && part[k][i] && p < (int) sizeof args - 2; i++)
			args[p++] = part[k][i] == '|' ? '/' : part[k][i];
	}
	args[p] = '\0';
	s_askPipe = kapi_pipe ();
	s_askProc = s_askPipe ? kapi_spawn ("SD:apps/ask.app/main", args, 0, s_askPipe) : 0;
	if (s_askProc == 0 && s_askPipe) { kapi_stream_close (s_askPipe); s_askPipe = 0; }
	return s_askProc != 0;
}

static inline int ask_text_poll (char *out, int cap)
{
	if (s_askProc == 0) return 0;
	if (!kapi_proc_done (s_askProc)) return -1;
	int r = kapi_wait (s_askProc) == 1 ? 1 : 0;
	int n = 0;
	for (;;)
	{
		int k = kapi_stream_read_nb (s_askPipe, out + n, (unsigned) (cap - 1 - n));
		if (k <= 0 || n + k >= cap - 1) { if (k > 0) n += k; break; }
		n += k;
	}
	out[n] = '\0';
	kapi_stream_close (s_askPipe);
	s_askProc = s_askPipe = 0;
	return r && n > 0 ? 1 : 0;
}

#endif
