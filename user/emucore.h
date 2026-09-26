//
// emucore.h -- run an emulator's machine on an app core (kapi v51), or inline if none.
//
// The app's main thread keeps the window, the input, the sound output and the pace; the
// machine runs on core 2 or 3 (kapi_core_acquire), undisturbed by the rest of the system:
//
//   main thread (core 0)                         machine (app core)
//     ec_request (n): n more frames      ->        frame (): run one frame, ec_publish ()
//     ec.btn = buttons                   ->          reads ec.btn
//     ec_take () / ec_front (): the image <-         ec_back (): where it draws
//     ec_audio_pop () -> kapi_sound_write <-         ec_audio_push ()
//     ec_hold () ... ec_resume (): menus touch the machine only while it waits between two
//                                  frames (reset, palette, the battery save...)
//
// With no free core, ec_pump () runs the requested frames on the main thread instead: the
// app code is the same. The machine's code on the app core makes no kapi call and does
// not allocate (all the buffers are made by ec_init).
//
#ifndef _user_emucore_h
#define _user_emucore_h

#include "kapi.h"

struct EmuCore;
typedef void (*ec_frame_fn) (EmuCore *ec);

struct EmuCore
{
	int core;				// app core, or -1 (inline)
	ec_frame_fn frame;
	// requests (main -> machine)
	volatile unsigned want;			// frames asked for so far
	volatile unsigned done;			// ... made
	volatile int btn;			// the buttons for the next frames
	volatile int stop, hold, held;
	// statistics (machine -> main)
	volatile unsigned long long emuUs;	// time spent in frames (sum)
	// the image: triple buffer (the machine draws in back, main shows front)
	int w, h;
	unsigned *slot[3];
	unsigned back, front;
	volatile unsigned mid;			// slot index | 4 when it holds a new image
	// the sound: s16 stereo ring (single producer / single consumer)
	short *ring;
	unsigned ringFrames;			// a power of two
	volatile unsigned head, tail;		// frames written / read
	unsigned char *stack;
};

static inline void ec_fence (void) { __asm__ volatile ("dmb ish" ::: "memory"); }
static inline void ec_sev (void)   { __asm__ volatile ("dsb ish; sev" ::: "memory"); }
static inline void ec_wfe (void)   { __asm__ volatile ("wfe" ::: "memory"); }
static inline unsigned long long ec_now_us (void)
{
	unsigned long long c, f;
	__asm__ volatile ("mrs %0, cntpct_el0" : "=r" (c));
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (f));
	return f ? c * 1000000ull / f : 0;
}
static inline unsigned ec_xchg (volatile unsigned *p, unsigned v)
{
	unsigned old, fail;
	__asm__ volatile ("1: ldaxr %w0, [%2]\n"
			  "   stlxr %w1, %w3, [%2]\n"
			  "   cbnz  %w1, 1b\n"
			  : "=&r" (old), "=&r" (fail) : "r" (p), "r" (v) : "memory");
	return old;
}

// ---- the machine's side ----------------------------------------------------------------------
static inline unsigned *ec_back (EmuCore *ec) { return ec->slot[ec->back]; }
static inline void ec_publish (EmuCore *ec)
{
	ec_fence ();
	ec->back = ec_xchg (&ec->mid, ec->back | 4) & 3;
}
static inline void ec_audio_push (EmuCore *ec, const short *lr, int n)
{
	unsigned h = ec->head, room = ec->ringFrames - (h - ec->tail);
	if ((unsigned) n > room) n = (int) room;			// (main is not reading: drop)
	for (int i = 0; i < n; i++)
	{
		unsigned k = ((h + (unsigned) i) & (ec->ringFrames - 1)) * 2;
		ec->ring[k] = lr[i * 2]; ec->ring[k + 1] = lr[i * 2 + 1];
	}
	ec_fence ();
	ec->head = h + (unsigned) n;
}
static inline void ec_one_frame (EmuCore *ec)
{
	unsigned long long t = ec_now_us ();
	ec->frame (ec);
	ec->emuUs += ec_now_us () - t;
	ec_fence ();
	ec->done++;
}
static void ec_thread (void *arg)			// runs on the app core
{
	EmuCore *ec = (EmuCore *) arg;
	while (!ec->stop)
	{
		if (ec->hold)
		{
			ec->held = 1; ec_sev ();
			while (ec->hold && !ec->stop) ec_wfe ();
			ec->held = 0;
			ec_fence ();			// (store held, then load hold: a full barrier, or
			continue;			//  main could still read held = 1 and go on)
		}
		if (ec->done != ec->want) { ec_one_frame (ec); continue; }
		ec_wfe ();					// (main sends an event with each request)
	}
}

// ---- the main thread's side ------------------------------------------------------------------
// w x h: the image; frame: makes one frame (see above). FALSE if out of memory.
static inline bool ec_init (EmuCore *ec, int w, int h, ec_frame_fn frame, bool useCore = true)
{
	ec->core = -1; ec->frame = frame;
	ec->want = ec->done = 0; ec->btn = 0; ec->stop = ec->hold = ec->held = 0; ec->emuUs = 0;
	ec->w = w; ec->h = h;
	for (int i = 0; i < 3; i++)
	{
		ec->slot[i] = new unsigned[(long) w * h];
		if (!ec->slot[i]) return false;
		for (long k = 0; k < (long) w * h; k++) ec->slot[i][k] = 0;
	}
	ec->back = 0; ec->front = 1; ec->mid = 2;
	ec->ringFrames = 16384;
	ec->ring = new short[ec->ringFrames * 2];
	ec->head = ec->tail = 0;
	ec->stack = new unsigned char[256 * 1024];
	if (!ec->ring || !ec->stack) return false;
	if (useCore)
	{
		ec->core = kapi_core_acquire ();
		if (ec->core >= 0 && kapi_core_run (ec->core, ec_thread, ec, ec->stack + 256 * 1024) != 0)
		{
			kapi_core_release (ec->core);
			ec->core = -1;
		}
	}
	return true;
}
static inline bool ec_on_core (EmuCore *ec) { return ec->core >= 0; }
static inline unsigned ec_pending (EmuCore *ec) { return ec->want - ec->done; }
static inline void ec_request (EmuCore *ec, unsigned n)
{
	ec->want += n;
	ec_sev ();
}
// Inline mode: make the requested frames now (on the app core: nothing to do).
static inline void ec_pump (EmuCore *ec)
{
	if (ec->core >= 0)
	{
		if (kapi_core_state (ec->core) == KAPI_CORE_FAULT) { kapi_core_release (ec->core); ec->core = -1; }
		else return;
	}
	while (ec->done != ec->want) ec_one_frame (ec);
}
// A new image since the last call? Then ec_front () is it.
static inline bool ec_take (EmuCore *ec)
{
	if (!(ec->mid & 4)) return false;
	ec->front = ec_xchg (&ec->mid, ec->front) & 3;
	ec_fence ();
	return true;
}
static inline const unsigned *ec_front (EmuCore *ec) { return ec->slot[ec->front]; }
static inline unsigned ec_audio_count (EmuCore *ec) { return ec->head - ec->tail; }
static inline int ec_audio_pop (EmuCore *ec, short *lr, int max)
{
	unsigned t = ec->tail, n = ec->head - t;
	ec_fence ();
	if (n > (unsigned) max) n = (unsigned) max;
	for (unsigned i = 0; i < n; i++)
	{
		unsigned k = ((t + i) & (ec->ringFrames - 1)) * 2;
		lr[i * 2] = ec->ring[k]; lr[i * 2 + 1] = ec->ring[k + 1];
	}
	ec_fence ();
	ec->tail = t + n;
	return (int) n;
}
// Stop the machine between two frames (the main thread may then touch it), then resume.
static inline void ec_hold (EmuCore *ec)
{
	if (ec->core < 0) return;
	ec->hold = 1; ec_sev ();
	unsigned long long t = ec_now_us ();
	while (!ec->held && ec_now_us () - t < 500000)
		if (kapi_core_state (ec->core) != KAPI_CORE_RUNNING) break;
	ec_fence ();
}
static inline void ec_resume (EmuCore *ec)
{
	if (ec->core < 0) return;
	ec_fence ();
	ec->hold = 0; ec_sev ();
}
// Stop for good and give the core back (the kernel does it too when the app ends).
static inline void ec_shutdown (EmuCore *ec)
{
	if (ec->core < 0) return;
	ec->stop = 1; ec_sev ();
	unsigned long long t = ec_now_us ();
	while (kapi_core_state (ec->core) == KAPI_CORE_RUNNING && ec_now_us () - t < 500000) ec_sev ();
	kapi_core_release (ec->core);
	ec->core = -1;
}

#endif
