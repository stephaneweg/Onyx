// tools/tests/koton/plug_host_test.cpp -- the plugin host (user/Apps/koton/plug/plughost.cpp) and the
// plugins' kernel side (user/Include/kplug.h: kplug_main, its render thread, its editor) on the PC: the
// desktop simulator's stand-in kernel (tools/tests/desktop_sim/fakekapi.cpp, used as it is) with a
// few of its entries replaced here by a small multi-process one -- processes are threads (exec_as runs
// a plugin's real main in one), each its mailbox, several shared surfaces, named services, the process
// list, kill, threads, word waits that an "app core" wakes at the next 10 ms tick, real time.
//
// The engine renders on its own thread (the app core) in real time; the host, on the main thread
// (the UI), makes a track's instrument (kp_fm2) and insert (kp_delay) from a project's track
// (syncTrack), plays a song through them, sets a parameter, gets / sets a state, opens the
// instrument's editor in a uikit panel (the plugin draws its knobs into the surface) and drags its
// first knob (the host hears of it), renders a generator module (kp_arp) through the hook (then from
// its cache), sees a plugin killed (onCrash) and starts it again, then ends everything (shutdown).
//   sh tools/tests/koton/plug_host_run.sh
#include "uikit/uikit.h"
#include "plug/plughost.h"
#include "engine/theory.h"
#include "applet_proto.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

using namespace kt;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

int kp_fm2_main (), kp_delay_main (), kp_arp_main ();		// (the plugins' main, renamed)

// ---- the stand-in kernel's processes -----------------------------------------------------------------------
struct Msg { int from, type, len; unsigned char data[512]; };
enum { MAXP = 16, MBOX = 64 };
struct Proc
{
	int pid; char name[40], args[128];
	int (*entry) ();
	volatile int alive, killed;
	Msg box[MBOX]; int head, tail;
};
static Proc g_proc[MAXP];
static int g_nproc;
static pthread_mutex_t g_k = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static thread_local int t_pid = 1;			// the host (the main thread) is 1
static char g_service[32]; static int g_servicePid;
struct Surf { unsigned *px; int w, h; };
static Surf g_surf[64]; static int g_nsurf;
static double t0;

static double now () { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static Proc *proc (int pid) { for (int i = 0; i < g_nproc; i++) if (g_proc[i].pid == pid) return &g_proc[i]; return 0; }
static void checkKilled ()			// (a killed process's threads end at their next kernel call)
{
	Proc *p = proc (t_pid);
	if (p && p->killed) pthread_exit (0);
}

static unsigned k_ticks () { return (unsigned) ((now () - t0) * 100); }
static void k_msleep (unsigned ms) { checkKilled (); usleep (ms * 1000); }
static void k_yield () { checkKilled (); sched_yield (); }
static int k_args (char *b, unsigned n) { Proc *p = proc (t_pid); snprintf (b, n, "%s", p ? p->args : ""); return (int) strlen (b); }

static int k_register (const char *n) { if (g_servicePid && strcmp (n, g_service)) return 0; snprintf (g_service, sizeof g_service, "%s", n); g_servicePid = t_pid; return 1; }
static int k_lookup (const char *n) { checkKilled (); return g_servicePid && !strcmp (n, g_service) ? g_servicePid : 0; }

static int k_surfCreate (int w, int h)
{
	if (g_nsurf >= 64) return 0;
	void *p = 0; if (posix_memalign (&p, 65536, (size_t) w * h * 4)) return 0;
	memset (p, 0, (size_t) w * h * 4);
	g_surf[g_nsurf].px = (unsigned *) p; g_surf[g_nsurf].w = w; g_surf[g_nsurf].h = h;
	return ++g_nsurf;
}
static unsigned *k_surfMap (int id) { return id >= 1 && id <= g_nsurf ? g_surf[id - 1].px : 0; }
static int k_surfSize (int id, int *w, int *h) { if (id < 1 || id > g_nsurf) return 0; *w = g_surf[id - 1].w; *h = g_surf[id - 1].h; return 1; }
static int k_surfDestroy (int id) { return id >= 1 && id <= g_nsurf; }		// (kept: freed at the end)

static int k_send (int to, int type, const void *in, unsigned len)
{
	pthread_mutex_lock (&g_k);
	Proc *p = to == 1 ? &g_proc[0] : proc (to);
	int ok = 0;
	if (p && p->alive && !p->killed && len <= 512 && (p->head + 1) % MBOX != p->tail)
	{
		Msg &m = p->box[p->head]; m.from = t_pid; m.type = type; m.len = (int) len; if (len) memcpy (m.data, in, len);
		p->head = (p->head + 1) % MBOX; ok = 1;
	}
	pthread_cond_broadcast (&g_cv);
	pthread_mutex_unlock (&g_k);
	return ok;
}
static int k_recv (int *from, int *type, void *buf, unsigned cap, int)
{
	checkKilled ();
	pthread_mutex_lock (&g_k);
	Proc *p = t_pid == 1 ? &g_proc[0] : proc (t_pid);
	int n = -1;
	if (p && p->tail != p->head)
	{
		Msg &m = p->box[p->tail]; *from = m.from; *type = m.type;
		n = m.len < (int) cap ? m.len : (int) cap; if (n) memcpy (buf, m.data, n);
		p->tail = (p->tail + 1) % MBOX;
	}
	pthread_mutex_unlock (&g_k);
	return n;
}

// word waits: a wake_word at once; a word written without it (the engine, "on its app core") is seen
// at the next 10 ms tick
static int k_wait (volatile unsigned *a, unsigned v, unsigned ms)
{
	checkKilled ();
	double end = now () + ms / 1000.0;
	pthread_mutex_lock (&g_k);
	int r = 0;
	for (;;)
	{
		if (*a != v) break;
		double t = now ();
		if (t >= end) { r = 1; break; }
		double tick = floor (t * 100 + 1) / 100.0, until = tick < end ? tick : end;
		struct timespec ts; clock_gettime (CLOCK_REALTIME, &ts);
		double d = until - t; long ns = ts.tv_nsec + (long) (d * 1e9);
		ts.tv_sec += ns / 1000000000; ts.tv_nsec = ns % 1000000000;
		pthread_cond_timedwait (&g_cv, &g_k, &ts);
		Proc *p = proc (t_pid);
		if (p && p->killed) { pthread_mutex_unlock (&g_k); pthread_exit (0); }
	}
	pthread_mutex_unlock (&g_k);
	return r;
}
static int k_wake (volatile unsigned *) { pthread_mutex_lock (&g_k); pthread_cond_broadcast (&g_cv); pthread_mutex_unlock (&g_k); return 1; }

// threads of a process
struct ThreadArg { int (*fn) (void *); void *arg; int pid; };
static pthread_t g_thr[64]; static int g_nthr;
static void *threadMain (void *a) { ThreadArg t = *(ThreadArg *) a; delete (ThreadArg *) a; t_pid = t.pid; long c = t.fn (t.arg); return (void *) c; }
static int k_threadCreate (int (*fn) (void *), void *arg, unsigned, const char *)
{
	ThreadArg *t = new ThreadArg; t->fn = fn; t->arg = arg; t->pid = t_pid;
	pthread_mutex_lock (&g_k); int id = g_nthr++; pthread_mutex_unlock (&g_k);
	pthread_create (&g_thr[id], 0, threadMain, t);
	return id + 2;
}
static int k_threadJoin (int tid, unsigned ms, int *code)
{
	struct timespec ts; clock_gettime (CLOCK_REALTIME, &ts);
	long ns = ts.tv_nsec + (long) ms * 1000000L; ts.tv_sec += ns / 1000000000; ts.tv_nsec = ns % 1000000000;
	void *r = 0;
	if (pthread_timedjoin_np (g_thr[tid - 2], &r, &ts)) return -1;
	if (code) *code = (int) (long) r;
	return 0;
}
static int k_prio (int, int) { return 0; }

// processes
static pthread_t g_pthr[MAXP];
static void *procMain (void *a)
{
	Proc *p = (Proc *) a;
	t_pid = p->pid;
	p->entry ();
	p->alive = 0;
	return 0;
}
static int k_execAs (const char *path, const char *args, const char *name)
{
	int (*e) () = strstr (path, "/fm2/") ? kp_fm2_main : strstr (path, "/delay/") ? kp_delay_main : strstr (path, "/arp/") ? kp_arp_main : 0;
	if (!e || g_nproc >= MAXP) return 0;
	pthread_mutex_lock (&g_k);
	Proc *p = &g_proc[g_nproc];
	memset (p, 0, sizeof *p);
	p->pid = 100 + g_nproc; snprintf (p->name, sizeof p->name, "%s", name); snprintf (p->args, sizeof p->args, "%s", args);
	p->entry = e; p->alive = 1;
	int i = g_nproc++;
	pthread_mutex_unlock (&g_k);
	pthread_create (&g_pthr[i], 0, procMain, p);
	return 1;
}
static int k_list (char *b, unsigned n)
{
	int k = snprintf (b, n, "1 a R 10 koton\n"), c = 1;
	pthread_mutex_lock (&g_k);
	for (int i = 1; i < g_nproc; i++)
		if (g_proc[i].alive && !g_proc[i].killed) { k += snprintf (b + k, n - k, "%d a S 8 %s\n", g_proc[i].pid, g_proc[i].name); c++; }
	pthread_mutex_unlock (&g_k);
	return c;
}
static int k_kill (int pid, int) { Proc *p = proc (pid); if (!p || !p->alive) return 0; p->killed = 1; k_wake (0); return 1; }

static void installKernel ()
{
	TKApiTable *T = (TKApiTable *) KAPI_TABLE_VA;	// (fakekapi's, writable)
	t0 = now ();
	T->get_ticks = k_ticks; T->msleep = k_msleep; T->yield = k_yield; T->get_args = k_args;
	T->ipc_register = k_register; T->ipc_lookup = k_lookup;
	T->surface_create = k_surfCreate; T->surface_map = k_surfMap; T->surface_size = k_surfSize; T->surface_destroy = k_surfDestroy;
	T->mailbox_send = k_send; T->mailbox_recv = k_recv; T->wait_word = k_wait; T->wake_word = k_wake;
	T->thread_create = k_threadCreate; T->thread_join = k_threadJoin; T->thread_priority = k_prio;
	T->exec_as = k_execAs; T->list_procs = k_list; T->kill_pid = k_kill;
	g_proc[0].pid = 1; g_proc[0].alive = 1; g_nproc = 1;
}

// ---- the engine on its "app core" ------------------------------------------------------------------------
struct Core { Engine *e; volatile int stop; float *L, *R; int cap, done; };
static void *coreMain (void *a)
{
	Core *c = (Core *) a;
	double start = now ();
	float l[256], r[256];
	while (!c->stop)
	{
		c->e->render (l, r, 256);
		if (c->done + 256 <= c->cap) { memcpy (c->L + c->done, l, sizeof l); memcpy (c->R + c->done, r, sizeof r); c->done += 256; }
		double due = start + (c->done ? c->done : 1) / 44100.0, t = now ();
		if (due > t) usleep ((useconds_t) ((due - t) * 1e6));
	}
	return 0;
}

// ---- the host's notifications ----
static int g_crashes, g_params, g_lastParam = -1;
static void onCrash (PlugInstance *p, void *) { g_crashes++; printf ("  onCrash: %s -- %s\n", p->info ().id.c (), p->error ()); }
static void onParam (PlugInstance *, int i, float, void *) { g_params++; g_lastParam = i; }

// KPLUG_SHOT=prefix: an editor as drawn -> prefix-<name>.ppm (the user guide's picture)
static void shot (uikit::Widget &w, int x0, int y0, int ew, int eh, const char *name)
{
	const char *pre = getenv ("KPLUG_SHOT");
	if (!pre) return;
	char path[256]; snprintf (path, sizeof path, "%s-%s.ppm", pre, name);
	FILE *f = fopen (path, "wb");
	if (!f) return;
	fprintf (f, "P6\n%d %d\n255\n", ew, eh);
	for (int y = y0; y < y0 + eh; y++)
		for (int x = x0; x < x0 + ew; x++)
		{
			unsigned c = w.canvas.px[y * w.canvas.stride + x];
			unsigned char rgb[3] = { (unsigned char) (c >> 16), (unsigned char) (c >> 8), (unsigned char) c };
			fwrite (rgb, 1, 3, f);
		}
	fclose (f);
}

static void pollFor (PlugHost &h, double sec) { double end = now () + sec; while (now () < end) { h.poll (); usleep (16000); } }

static void addChord (Project &p, int deg, int beats)
{
	PatternModule *m = new PatternModule;
	RootQ c = diatonicChord (p.key, deg, 0, 0, 0);
	m->degree = deg; m->root = c.root; m->quality = c.quality; m->beatsPerBar = beats;
	p.tracks[p.chordTrackIndex ()].items.push (Item (0, m));
}

int main ()
{
	installKernel ();
	uikit::init ();
	// the card: SD:/koton/plugins/{fm2,delay,arp} in SIM_WRITES (the manifests; a file standing for the program)
	CHECK (getenv ("SIM_WRITES") != 0);
	PlugHost h;
	h.onCrash = onCrash; h.onParam = onParam;
	CHECK (h.init (44100));
	CHECK (h.scan () == 3 && h.find ("fm2") && h.find ("koton.arpeggiator") && h.countKind (KP_EFFECT) == 1);
	printf ("host: up, %d plugins: %s, %s, %s\n", h.count (), h.info (0).name.c (), h.info (1).name.c (), h.info (2).name.c ());
	// the engine, on its core
	Engine e; e.init (0, 44100);
	h.attach (&e);
	Core core = { &e, 0, 0, 0, 44100 * 8, 0 };
	core.L = (float *) calloc (core.cap, sizeof (float)); core.R = (float *) calloc (core.cap, sizeof (float));
	pthread_t ct; pthread_create (&ct, 0, coreMain, &core);
	// a track: fm2 as its instrument, the delay as its insert
	Track tr; tr.name = "Synth";
	tr.instrumentPlugin.id = "fm2"; tr.instrumentPlugin.state = "{\"params\":{\"index\":5}}";
	PluginSlot fx; fx.id = "delay"; fx.state = "{\"params\":{\"mix\":0.5,\"time\":120}}"; tr.inserts.push (fx);
	CHECK (h.syncTrack (0, tr));
	PlugInstance *fm = h.trackInstrument (0), *dl = h.trackInsert (0, 0);
	CHECK (fm && dl && fm->track () == 0 && dl->slot () == 0);
	pollFor (h, 0.5);
	CHECK (fm->ready () && dl->ready () && fm->pid () > 0 && dl->pid () > 0);
	CHECK (fabsf (fm->param (fm->paramIndex ("index")) - 5) < 1e-4f);		// its initial state
	// a song
	CompiledSong *s = new CompiledSong;
	s->sampleRate = 44100; s->totalSlices = 800; s->sliceSample.resize (801);
	for (int i = 0; i <= 800; i++) s->sliceSample[i] = (long long) i * 200;
	CTrack &t = s->tracks.add (); t.srcIndex = 0; t.channel = 0; t.program = 0; t.bank = 0; t.drum = false; t.silent = false;
	for (int k = 0; k < 16; k++)
	{
		CEvent on; memset (&on, 0, sizeof on); on.slice = k * 40; on.kind = EV_ON; on.note = (unsigned char) (48 + (k * 5) % 24); on.vel = 100;
		CEvent off = on; off.kind = EV_OFF; off.slice = k * 40 + 20;
		t.events.push (on); t.events.push (off);
	}
	e.post (CMD_SONG, 0, 0, 0, 0, s);
	e.post (CMD_PLAY, 0);
	pollFor (h, 1.0);
	// a parameter, a state
	int ix = fm->paramIndex ("ratio");
	h.setParam (fm, ix, 3.5f);
	pollFor (h, 0.1);
	CHECK (fabsf (fm->param (ix) - 3.5f) < 1e-4f);
	Str st;
	CHECK (h.getState (fm, st) && strstr (st.c (), "\"ratio\":3.5"));
	CHECK (h.setState (fm, "{\"params\":{\"ratio\":1,\"volume\":-12}}") && fabsf (fm->param (ix) - 1) < 1e-4f);
	h.saveTrack (0, tr);
	CHECK (strstr (tr.inserts[0].state.c (), "\"time\":120") && strstr (tr.instrumentPlugin.state.c (), "\"volume\":-12"));
	printf ("host: parameters and states through the plugin: %s\n", tr.instrumentPlugin.state.c ());
	// the editor in a panel: the plugin draws its knobs (in the host's colours: Koton's dark ones
	// here); a drag on the first one reaches the host
	{ uikit::UkTheme th; uikit::uk_theme_get (th); th.window = 0x21252D; th.button = 0x363C48; th.field = 0x14171C; th.accent = 0x49B0C4; uikit::uk_theme_set (th); }
	uikit::Panel panel (0, 0, 640, 400, 0x00123456);
	int ew = 0, eh = 0; h.editorSize (fm, &ew, &eh);
	PlugEditorView *v = h.openEditor (fm, panel, 10, 10, ew, eh);
	CHECK (v != 0);
	pollFor (h, 0.3);
	panel.draw ();
	int inked = 0;
	for (int y = 10; y < 10 + eh; y++) for (int x = 10; x < 10 + ew; x++) if ((panel.canvas.px[y * panel.canvas.stride + x] & 0xFFFFFF) != (uikit::C_BG & 0xFFFFFF)) inked++;
	CHECK (inked > 2000);
	shot (panel, 10, 10, ew, eh, "fm2");
	float r0 = fm->param (0);
	int kx = 10 + 10 + 40, ky = 10 + uikit::uk_fh () + 18 + 40;		// the first knob's dial
	panel.handleMouse (kx, ky, 0, 0, 0, 0); pollFor (h, 0.05);
	panel.handleMouse (kx, ky, 1, 0, 0, 0); pollFor (h, 0.05);
	panel.handleMouse (kx, ky - 60, 1, 0, 0, 0); pollFor (h, 0.05);
	panel.handleMouse (kx, ky - 60, 0, 0, 0, 0); pollFor (h, 0.2);
	CHECK (g_params > 0 && g_lastParam == 0 && fm->param (0) > r0);
	printf ("host: the editor %d x %d, %d pixels drawn; a drag on its first knob: ratio %.2f -> %.2f (%d notices)\n", ew, eh, inked, r0, fm->param (0), g_params);
	h.closeEditor (v);
	pollFor (h, 0.1);
	// a generator module through the hook, then from the cache
	Project p;
	p.key.tonicLetter = 2; p.key.mode = 0;			// E major
	Track lead; p.tracks.push (lead);
	Track ch; ch.type = TRACK_CHORD; p.tracks.push (ch);
	addChord (p, 0, 4); addChord (p, 3, 4);
	GeneratorModule *g = new GeneratorModule; g->id = "g1"; g->generatorId = "koton.arpeggiator"; g->durationBeats = 8;
	setModuleStateJson (*g, "{\"v\":1,\"params\":{\"pattern\":2,\"notes_per_beat\":4}}");
	p.tracks[0].items.push (Item (0, g));
	h.installGeneratorHook ();
	int carry[9] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
	double t1 = now ();
	Riff r = renderModule (*g, p, 0, carry);
	double t2 = now ();
	CHECK (r.notes.size () == 32);
	PlugInstance *gi = h.generator ("arp");
	CHECK (gi && gi->ready ());
	Riff r2 = renderModule (*g, p, 0, carry);
	double t3 = now ();
	CHECK (r2.notes.size () == 32);
	for (int i = 0; i < r.notes.size () && i < r2.notes.size (); i++) CHECK (r.notes[i].note == r2.notes[i].note && r.notes[i].start == r2.notes[i].start);
	printf ("host: a generator block rendered by kp_arp: %d notes in %.1f ms (its process started), %.2f ms again (the cache)\n",
		r.notes.size (), (t2 - t1) * 1000, (t3 - t2) * 1000);
	// its editor: a parameter moved there goes back into the module (found by its id in the project)
	uikit::Panel gp (0, 0, 640, 400, 0x00123456);
	int gw = 0, gh = 0; h.editorSize (gi, &gw, &gh);
	PlugEditorView *gv = h.openGeneratorEditor (*g, gp, 0, 0, gw, gh);
	CHECK (gv && gi->isGenerator () && !strcmp (gi->editedModule (), "g1"));
	pollFor (h, 0.2);
	gp.draw (); shot (gp, 0, 0, gw, gh, "arp");
	h.setParam (gi, gi->paramIndex ("notes_per_beat"), 2);		// (as its editor would)
	pollFor (h, 0.1);
	CHECK (h.pullGeneratorState (gi, p));
	Str gs; moduleStateJson (*g, gs);
	CHECK (strstr (gs.c (), "\"notes_per_beat\":2") && strstr (gs.c (), "\"pattern\":2"));
	Riff r3 = renderModule (*g, p, 0, carry);
	CHECK (r3.notes.size () == 16);
	h.closeEditor (gv);
	CHECK (!gi->editedModule ()[0]);
	// a plugin killed: noticed, its track silent, started again
	unsigned u0 = e.pluginUnderruns;
	kapi_kill_pid (dl->pid (), 1);
	pollFor (h, 1.2);
	CHECK (g_crashes == 1 && dl->state () == PLUG_CRASHED);
	CHECK (e.pluginUnderruns > u0 && dl->track () < 0 && e.pdcFrames == 0);	// (taken out: the track dry)
	unsigned ud = e.pluginUnderruns;
	pollFor (h, 0.3);
	CHECK (e.pluginUnderruns == ud);
	CHECK (h.restart (dl));
	pollFor (h, 0.6);
	CHECK (dl->ready () && dl->track () == 0 && dl->slot () == 0);
	pollFor (h, 0.3);
	unsigned u1 = e.pluginUnderruns;
	pollFor (h, 0.5);
	CHECK (e.pluginUnderruns == u1);
	printf ("host: the delay killed -> onCrash, %u blocks silent until then, the track dry; started again, in its slot, no underrun since\n", ud - u0);
	// the end
	core.stop = 1; pthread_join (ct, 0);
	h.attach (0);
	h.shutdown ();
	usleep (100000);
	int alive = 0; for (int i = 1; i < g_nproc; i++) if (g_proc[i].alive && !g_proc[i].killed) alive++;
	CHECK (alive == 0);
	for (int i = 1; i < g_nproc; i++) pthread_join (g_pthr[i], 0);
	double rms = 0; bool nan = false;
	for (int i = 0; i < core.done; i++) { if (core.L[i] != core.L[i]) nan = true; rms += core.L[i] * core.L[i]; }
	rms = sqrt (rms / (core.done ? core.done : 1));
	printf ("host: %.1f s played, rms %.4f, engine underruns %u; every plugin process ended\n", core.done / 44100.0, rms, e.pluginUnderruns);
	CHECK (!nan && rms > 0.005);
	for (CompiledSong *rt; (rt = e.retired ()); ) delete rt;
	free (core.L); free (core.R);
	for (int i = 0; i < g_nsurf; i++) free (g_surf[i].px);
	printf (g_fail ? "koton plugin host: %d FAILED\n" : "koton plugin host: all passed\n", g_fail);
	fflush (stdout);
	_exit (g_fail != 0);		// (fakekapi's own state is not torn down)
}
