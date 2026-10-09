//
// kws.h -- a graphics server's door to the kernel (Elegant's, PocketUI's): the graphics server's mechanisms
// (kapi v89, kern/wsrv.h), each one a kapi_ws_ctl operation (kern/kapi_abi.h KAPI_WS_*).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef ELEGANT_KWS_H
#define ELEGANT_KWS_H

#include "appkit/appkit.h"

static inline long kws_register (void)					{ return kapi_ws_ctl (KAPI_WS_REGISTER, 0, 0, 0); }
static inline long kws_display (int take, struct kapi_ws_display *out)	{ return kapi_ws_ctl (KAPI_WS_DISPLAY, take, (long) out, 0); }
static inline long kws_present (const struct kapi_ws_present *p)	{ return kapi_ws_ctl (KAPI_WS_PRESENT, (long) p, 0, 0); }
static inline long kws_input (struct kapi_ws_input *out, int max)	{ return kapi_ws_ctl (KAPI_WS_INPUT, (long) out, max, 0); }
static inline long kws_wait (unsigned ms)				{ return kapi_ws_ctl (KAPI_WS_WAIT, (long) ms, 0, 0); }
static inline long kws_attach (unsigned pid)				{ return kapi_ws_ctl (KAPI_WS_ATTACH, (long) pid, 0, 0); }
static inline long kws_post (unsigned pid, const struct kapi_event *e)	{ return kapi_ws_ctl (KAPI_WS_POST, (long) pid, (long) e, 0); }
static inline long kws_exit (unsigned pid)				{ return kapi_ws_ctl (KAPI_WS_EXIT, (long) pid, 0, 0); }
static inline long kws_buf_map (struct kapi_ws_buf *b)			{ return kapi_ws_ctl (KAPI_WS_BUF_MAP, (long) b, 0, 0); }
static inline long kws_buf_free (unsigned id)				{ return kapi_ws_ctl (KAPI_WS_BUF_FREE, (long) id, 0, 0); }
static inline long kws_next (struct kapi_ws_req *out)			{ return kapi_ws_ctl (KAPI_WS_NEXT, (long) out, 0, 0); }
static inline long kws_reply (unsigned id, long status, const void *data, unsigned len)
{
	struct kapi_ws_reply r;
	r.id = id; r.len = len; r.status = status; r.data = data;
	return kapi_ws_ctl (KAPI_WS_REPLY, (long) &r, 0, 0);
}

#endif
