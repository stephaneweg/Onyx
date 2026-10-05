//
// shell_proto.h -- the activity-shell IPC protocol (message types + payloads) shared by
// the shell (compositor) and the apps it hosts. Carried over the kernel message router
// (kapi_register_shell / kapi_shell_request / kapi_mailbox_send / kapi_mailbox_recv,
// ABI v35): each message is {from_pid, type, payload-bytes}; the kernel is agnostic to
// `type` -- these constants give it meaning.
//
// Flow:
//   app  --SH_REGISTER-->  shell        (announce role + name)
//   shell --SH_SURFACE-->  app          (here is your surface id + size; map + draw it)
//   app  --SH_PRESENT-->   shell        (I drew; please recomposite)
//   shell --SH_PTR/SH_KEY/SH_RESIZE/SH_CLOSE--> app   (forwarded input / geometry / exit)
//
#ifndef _shell_proto_h
#define _shell_proto_h

// Reserve type 0 and negatives for the kernel/system; app<->shell types are >= 1.
enum {
	// app -> shell (via kapi_shell_request)
	SH_REGISTER = 1,	// payload = ShRegister  : announce this app to the shell
	SH_PRESENT  = 2,	// payload = int surface_id : app finished a frame, recomposite

	// shell -> app (via kapi_mailbox_send)
	SH_SURFACE  = 10,	// payload = ShSurface : assigned surface (id + size); map + draw it
	SH_RESIZE   = 11,	// payload = ShResize  : viewport resized (SAME surface id), reclip
	SH_PTR      = 12,	// payload = ShPtr     : pointer event, viewport-local coords
	SH_KEY      = 13,	// payload = int key   : key event (ASCII or KEY_* code)
	SH_CLOSE    = 14,	// payload = (none)    : please exit cleanly
};

// App role -- which section of the shell hosts it.
enum {
	ROLE_PRINCIPAL = 1,	// main section (editor, browser, ...)
	ROLE_SECONDARY = 2,	// secondary section (terminal, calculator, ...)
	ROLE_APPLET    = 3,	// applet zone (clock, wifi, ...) -- reserved for later
};

struct ShRegister { int role; char name[32]; };		// SH_REGISTER
struct ShSurface  { int surface_id, w, h, stride; };	// SH_SURFACE (stride = real buffer row width)
struct ShResize   { int w, h; };			// SH_RESIZE (same surface/stride, new logical size)
struct ShPtr      { int event, x, y, buttons, changed, wheel; };	// SH_PTR (GUI_EVENT_PTR_*)

#endif // _shell_proto_h
