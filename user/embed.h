//
// embed.h -- helper for an app embedded in the activity shell. It registers with the
// shell, waits for the shell to assign a surface (its viewport), maps it, and runs a
// wtk widget tree on that surface driven by the shell's forwarded input. The app builds
// a normal wtk tree whose ROOT canvas is the surface (Surface::root adopts it); the
// shell composites it. Pure mouse/key come in over the mailbox; the app redraws and
// posts SH_PRESENT when its tree changes. Standalone (no shell) is detected so the app
// can fall back to its own window.
//
#ifndef _embed_h
#define _embed_h

#include "kapi.h"
#include "applib.h"		// should_exit, msleep
#include "shell_proto.h"
#include "wtk/wtk.h"

namespace embed {

struct Host
{
	int       surface_id;
	int       w, h;		// current logical (viewport) size
	int       stride;	// real buffer row width (>= w; the surface is over-allocated)
	unsigned *pixels;	// the mapped surface (== the root's canvas, adopt with stride)
};

// Announce this app to the shell and block until it assigns a surface. Returns false if
// there is no shell registered (caller should fall back to a window) or on failure.
static inline bool attach (Host *pHost, int nRole, const char *pName)
{
	ShRegister Reg;
	Reg.role = nRole;
	int i = 0; for (; pName && pName[i] && i < 31; i++) Reg.name[i] = pName[i];
	Reg.name[i] = '\0';
	if (kapi_shell_request (SH_REGISTER, &Reg, sizeof Reg) < 0)
	{
		return false;			// no shell
	}
	for (;;)				// wait for our surface assignment
	{
		int nFrom, nType; unsigned char Buf[128];
		int n = kapi_mailbox_recv (&nFrom, &nType, Buf, sizeof Buf, 1);	// blocking
		if (n < 0) return false;
		if (nType == SH_SURFACE)
		{
			ShSurface *pS = (ShSurface *) Buf;
			pHost->surface_id = pS->surface_id;
			pHost->w = pS->w; pHost->h = pS->h; pHost->stride = pS->stride;
			pHost->pixels = kapi_surface_map (pS->surface_id);
			return pHost->pixels != 0;
		}
		// ignore any other message until we have a surface
	}
}

typedef void (*KeyFn) (int key);	// optional app key handler (calc digits, etc.)

// Run the embedded event loop: draw the initial frame, then service forwarded input
// until the shell asks us to close. `pRoot` is a wtk widget whose canvas is the surface.
static inline void run (Host *pHost, wtk::Widget *pRoot, KeyFn pOnKey = 0)
{
	pRoot->invalidate (true);
	pRoot->draw ();
	kapi_shell_request (SH_PRESENT, &pHost->surface_id, sizeof (int));

	bool bRunning = true;
	while (bRunning)
	{
		int nFrom, nType; unsigned char Buf[128];
		int n = kapi_mailbox_recv (&nFrom, &nType, Buf, sizeof Buf, 1);	// blocking
		if (n < 0) { if (should_exit ()) break; continue; }
		switch (nType)
		{
		case SH_PTR:
		{
			ShPtr *p = (ShPtr *) Buf;	// p->x < 0 => pointer-leave (wtk convention)
			pRoot->handleMouse (p->x, p->y, p->buttons & 1,
					    (p->buttons >> 1) & 1, (p->buttons >> 2) & 1, p->wheel);
			break;
		}
		case SH_KEY:
			if (pOnKey != 0) pOnKey (*(int *) Buf);
			break;
		case SH_RESIZE:
		{
			ShResize *r = (ShResize *) Buf;		// viewport resized: re-fit our tree
			pRoot->setBounds (r->w, r->h);		// (anchors cascade; no canvas realloc)
			break;
		}
		case SH_CLOSE:
			bRunning = false;
			break;
		}
		if (!pRoot->valid)
		{
			pRoot->draw ();
			kapi_shell_request (SH_PRESENT, &pHost->surface_id, sizeof (int));
		}
	}
}

} // namespace embed

#endif // _embed_h
