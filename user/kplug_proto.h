//
// kplug_proto.h -- Koton's plugins as processes: the protocol between the HOST (the Koton app,
// user/Apps/koton/plug/plughost.*) and a PLUGIN (a program built on user/kplug.h), the way the
// Control Panel hosts its applets (applet_proto.h). APPEND-ONLY, like the kapi ABI: a message
// number, an event type, a field of KpShm never moves nor changes meaning; a new field takes a
// reserved word and KP_PROTO_VERSION grows. Plain C (a plugin may be written in C).
//
// A plugin is a folder SD:/koton/plugins/<id>/ with `main` (the program, no extension) and
// `plugin.json` ({ "name", "kind": "instrument" | "effect" | "generator", "params": [{ "id",
// "name", "min", "max", "default", "unit", "step", "choices" }], "aliases", "editor": {w, h} }):
// not under SD:/apps, so the dock does not show it.
//
// Starting: the host makes the SHARED REGION -- a surface (kapi_surface_create, KP_SHM_W x
// KP_SHM_H "pixels" = KP_SHM_BYTES) used as bytes: struct KpShm --, fills its identity (magic,
// version, kind, rate, lead, latency, the initial state in data[]), then runs the plugin with
// kapi_exec_as (".../main", "--kplug <shm id> <host pid>", "kp.<id>.<n>"). The plugin maps it,
// loads the state, writes its parameter list (JSON) into data[], sets `ready` and sends KP_HELLO.
// The host is the IPC service KP_SERVICE: a plugin whose host is gone ends by itself.
//
// REAL TIME WITHOUT A ROUND TRIP PER BLOCK: the engine runs on an app core (no kernel call) and
// cannot wake anybody; it is also the sequencer and knows the notes in advance. So it RENDERS
// AHEAD: every frame it outputs is counted on a STREAM CLOCK (64-bit frames, never reset); for an
// instrument it writes the timestamped note events `lead` frames ahead of the frame it plays into
// the EVENT RING and moves `want` (every event before it is in the ring); the plugin's render
// thread (real-time priority) sleeps on `kick` (kapi_wait_word: the kernel re-reads sleeping
// words at every 10 ms tick, so an app core's write wakes it) and renders the frames [done, want)
// into the OUTPUT RING; the engine reads frame P from it when it plays P (`readPos`). An EFFECT:
// the engine writes the dry block of frames [t, t + n) into the INPUT RING (want = t + n) and reads
// the processed frames [t - latency, ...) back -- the host's engine delays the other tracks by as
// much (plugin delay compensation). A frame not rendered in time is silence plus `underruns`:
// the engine never waits.
//
//   host --> plugin (mailbox)       plugin --> host (mailbox)
//   KP_SET_PARAM   KpParamMsg       KP_HELLO          KpHello: it is up
//   KP_STATE_GET   KpReq  \         KP_PARAM_CHANGED  KpParamMsg: its editor moved a parameter
//   KP_STATE_SET   KpReq   | the payload and the reply in data[] (the request channel: reqSeq /
//   KP_GENERATE    KpReq   | repSeq, below); the message is a doorbell -- the plugin also sees
//   KP_PARAMS      KpReq  /  reqSeq move by itself
//   KP_EDITOR      KpEditor: draw your editor into that surface, as an applet (AP_* of
//                  applet_proto.h, unchanged: AP_HELLO / AP_PRESENT / AP_EXIT back, AP_PTR /
//                  AP_KEY / AP_CLOSE in)
//   KP_BYE         (both ways) please end / I am ending
//   KP_DIRTY       plugin -> host: its state changed otherwise than by a parameter
//
#ifndef _kplug_proto_h
#define _kplug_proto_h

#define KP_PROTO_VERSION	1
#define KP_MAGIC		0x474C504Bu		/* "KPLG" */
#define KP_SERVICE		"kotonplug"		/* the host's IPC service (kapi_ipc_register) */
#define KP_DIR			"SD:/koton/plugins"	/* the plugins' folders */

enum { KP_INSTRUMENT = 1, KP_EFFECT = 2, KP_GENERATOR = 3 };

/* ---- the shared region's geometry ---------------------------------------------------------------- */
#define KP_SHM_W		512			/* a surface of 512 x 224 "pixels": 448 KB, 7 pages */
#define KP_SHM_H		224
#define KP_SHM_BYTES		(KP_SHM_W * KP_SHM_H * 4)
#define KP_EV_RING		1024			/* events (a power of two) */
#define KP_AU_RING		16384			/* frames per channel (a power of two): 371 ms */
#define KP_DATA_BYTES		(128 * 1024)		/* JSON exchanged: states, contexts, notes */
#define KP_MAX_PARAMS		64
#define KP_BLOCK		256			/* the most a plugin renders at once */
#define KP_LEAD_DEFAULT		4096			/* 92.9 ms at 44.1 kHz: > 2 ticks + a block, x 3 */
#define KP_LEAD_MIN		1024
#define KP_LEAD_MAX		8192

/* ---- events (the engine -> an instrument), stamped on the stream clock ---------------------------- */
enum
{
	KPE_NOTE_ON = 1,	/* a = note (MIDI), b = velocity 1..127 */
	KPE_NOTE_OFF = 2,	/* a = note */
	KPE_ALL_OFF = 3,	/* every note released (a loop's end, a new song) */
	KPE_RESET = 4,		/* every voice cut at once (play, seek, stop) */
	KPE_CC = 5,		/* a = controller, b = value */
	KPE_PARAM = 6,		/* a = parameter index, v = value (sample-accurate automation) */
	KPE_BEND = 7		/* v = -1..+1 */
};
typedef struct KpEvent
{
	unsigned long long at;			/* the stream frame */
	unsigned char type, a, b, c;
	float v;
} KpEvent;				/* 16 bytes */

/* ---- `ready` ------------------------------------------------------------------------------------- */
enum { KP_STARTING = 0, KP_READY = 1, KP_ENDING = 2, KP_FAILED = 3 };

/* ---- request replies (`repStatus`) --------------------------------------------------------------- */
enum { KP_OK = 0, KP_ERR_UNSUPPORTED = 1, KP_ERR_BAD = 2, KP_ERR_TOOBIG = 3 };

/* ---- the shared region ----------------------------------------------------------------------------- */
typedef struct KpShm
{
	/* 0: its identity -- the host, before starting the plugin */
	unsigned magic, version, size, kind;
	unsigned rate;				/* the sample rate */
	unsigned lead;				/* an instrument: the frames the engine sends its events ahead */
	unsigned latency;			/* an effect: the frames it gives its output back late */
	unsigned instance;			/* the host's number for it */
	unsigned initLen;			/* the initial state (JSON) in data[] at the start, 0: the defaults */
	unsigned reserved0[7];
	/* 64: the plugin's side */
	volatile unsigned ready;		/* KP_STARTING / READY / ENDING / FAILED */
	volatile unsigned pluginVersion;	/* the protocol it speaks */
	volatile unsigned nparams;
	volatile unsigned paramsLen;		/* its parameter list (JSON) in data[] when it became ready */
	volatile unsigned beat;			/* its main thread's heartbeat */
	volatile unsigned dspBeat;		/* its render thread's */
	volatile unsigned dspUs;		/* the cost of its last render pass, microseconds */
	volatile unsigned dspMaxUs;		/* ... the most in the last second */
	volatile unsigned lateFrames;		/* frames it skipped because it came too late */
	volatile unsigned reserved1[7];
	/* 128: the render clock (want, readPos: the engine's; done: the plugin's) */
	volatile unsigned long long want;	/* instrument: every event before it is in the ring;
						   effect: the input ring holds the audio before it */
	volatile unsigned long long readPos;	/* the engine reads the output from there */
	volatile unsigned long long done;	/* the output ring holds [done - KP_AU_RING, done) */
	volatile unsigned kick;			/* (unsigned) want: the word the render thread sleeps on */
	volatile unsigned evWr, evRd;		/* the event ring's counts (the engine / the plugin) */
	volatile unsigned evDropped;		/* events lost: the ring was full */
	volatile unsigned underruns;		/* blocks the engine read before they were there */
	volatile unsigned active;		/* 1: connected to the engine */
	volatile unsigned reserved2[20];
	/* 256: the request channel (JSON in data[]) */
	volatile unsigned reqSeq, reqType, reqLen;	/* the host: the type (KP_STATE_GET...), the payload */
	volatile unsigned repSeq, repStatus, repLen;	/* the plugin: repSeq = reqSeq once the reply is there */
	volatile unsigned reserved3[10];
	/* 320: the parameters' values now (the plugin writes, the host reads) */
	volatile float param[KP_MAX_PARAMS];
	unsigned pad[(4096 - 320 - 4 * KP_MAX_PARAMS) / 4];
	/* 4096 */
	KpEvent ev[KP_EV_RING];
	float outL[KP_AU_RING], outR[KP_AU_RING];
	float inL[KP_AU_RING], inR[KP_AU_RING];
	char data[KP_DATA_BYTES];
} KpShm;

#ifdef __cplusplus
static_assert (sizeof (KpShm) <= KP_SHM_BYTES, "KpShm must fit the surface");
static_assert (__builtin_offsetof (KpShm, want) == 128 && __builtin_offsetof (KpShm, reqSeq) == 256
	       && __builtin_offsetof (KpShm, param) == 320 && __builtin_offsetof (KpShm, ev) == 4096, "KpShm's layout is fixed");
#endif

/* ---- mailbox messages (types beyond the applets' AP_* 40..52) ------------------------------------ */
enum
{
	KP_HELLO = 100,		/* plugin -> host: KpHello */
	KP_SET_PARAM = 101,	/* host -> plugin: KpParamMsg */
	KP_PARAM_CHANGED = 102,	/* plugin -> host: KpParamMsg (its editor) */
	KP_STATE_GET = 103,	/* host -> plugin: KpReq; reply: its state (JSON) */
	KP_STATE_SET = 104,	/* host -> plugin: KpReq; request: a state (JSON) */
	KP_GENERATE = 105,	/* host -> plugin: KpReq; request: a context (JSON), reply: notes (JSON) */
	KP_PARAMS = 106,	/* host -> plugin: KpReq; reply: its parameter list (JSON) */
	KP_EDITOR = 107,	/* host -> plugin: KpEditor */
	KP_DIRTY = 108,		/* plugin -> host: its state changed (not a parameter) */
	KP_BYE = 109		/* both ways */
};
typedef struct KpHello { unsigned version, kind, nparams; int shm; } KpHello;
typedef struct KpParamMsg { int index; float value; } KpParamMsg;
typedef struct KpReq { unsigned seq; } KpReq;
typedef struct KpEditor
{
	int surface, w, h;
	unsigned themed;			/* 1: draw with the host's colours below (its uikit theme), 0: the system's */
	unsigned window, button, field, accent;
} KpEditor;

/* ---- the rings: single producer, single consumer, no lock ------------------------------------------ */
static inline unsigned long long kp_ld64 (volatile unsigned long long *p) { return __atomic_load_n (p, __ATOMIC_ACQUIRE); }
static inline void kp_st64 (volatile unsigned long long *p, unsigned long long v) { __atomic_store_n (p, v, __ATOMIC_RELEASE); }
static inline unsigned kp_ld32 (volatile unsigned *p) { return __atomic_load_n (p, __ATOMIC_ACQUIRE); }
static inline void kp_st32 (volatile unsigned *p, unsigned v) { __atomic_store_n (p, v, __ATOMIC_RELEASE); }

/* the engine: one event in (0: the ring is full -- counted, dropped) */
static inline int kp_ev_push (KpShm *s, const KpEvent *e)
{
	unsigned w = s->evWr, r = kp_ld32 (&s->evRd);
	if (w - r >= KP_EV_RING) { s->evDropped = s->evDropped + 1; return 0; }
	s->ev[w & (KP_EV_RING - 1)] = *e;
	kp_st32 (&s->evWr, w + 1);
	return 1;
}
/* the plugin: the oldest event, not taken (0: none) */
static inline const KpEvent *kp_ev_peek (KpShm *s)
{
	unsigned r = s->evRd;
	if (r == kp_ld32 (&s->evWr)) return 0;
	return &s->ev[r & (KP_EV_RING - 1)];
}
static inline void kp_ev_pop (KpShm *s) { kp_st32 (&s->evRd, s->evRd + 1); }

/* frames [at, at + n) of a ring pair (KP_AU_RING frames per channel), in / out of two blocks */
static inline void kp_au_write (float *ringL, float *ringR, unsigned long long at, const float *l, const float *r, int n)
{
	unsigned i = (unsigned) (at & (KP_AU_RING - 1));
	int first = (int) (KP_AU_RING - i) < n ? (int) (KP_AU_RING - i) : n, k;
	for (k = 0; k < first; k++) { ringL[i + k] = l[k]; ringR[i + k] = r[k]; }
	for (k = first; k < n; k++) { ringL[k - first] = l[k]; ringR[k - first] = r[k]; }
}
static inline void kp_au_read (const float *ringL, const float *ringR, unsigned long long at, float *l, float *r, int n)
{
	unsigned i = (unsigned) (at & (KP_AU_RING - 1));
	int first = (int) (KP_AU_RING - i) < n ? (int) (KP_AU_RING - i) : n, k;
	for (k = 0; k < first; k++) { l[k] = ringL[i + k]; r[k] = ringR[i + k]; }
	for (k = first; k < n; k++) { l[k] = ringL[k - first]; r[k] = ringR[k - first]; }
}

/* the host: a request in data[] (its payload, len bytes, may be 0) -> its sequence number; the reply
   is there once kp_req_answered (the plugin: repStatus, repLen bytes in data[]) */
static inline unsigned kp_req_post (KpShm *s, unsigned type, const char *payload, unsigned len)
{
	unsigned k, seq;
	if (len > KP_DATA_BYTES) len = KP_DATA_BYTES;
	for (k = 0; payload && k < len; k++) s->data[k] = payload[k];
	s->reqType = type; s->reqLen = len;
	seq = s->reqSeq + 1; if (seq == 0) seq = 1;
	kp_st32 (&s->reqSeq, seq);
	return seq;
}
static inline int kp_req_answered (KpShm *s, unsigned seq) { return kp_ld32 (&s->repSeq) == seq; }

/* the host: a fresh region (all zero but its identity) */
static inline void kp_shm_init (KpShm *s, int kind, int rate, int lead, int latency, int instance)
{
	unsigned char *b = (unsigned char *) s;
	unsigned long k;
	for (k = 0; k < sizeof (KpShm); k++) b[k] = 0;
	s->magic = KP_MAGIC; s->version = KP_PROTO_VERSION; s->size = (unsigned) sizeof (KpShm);
	s->kind = (unsigned) kind; s->rate = (unsigned) rate; s->lead = (unsigned) lead;
	s->latency = (unsigned) latency; s->instance = (unsigned) instance;
}

#endif
