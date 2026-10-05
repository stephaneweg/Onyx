//
// plug/plughost.cpp -- Koton's plugins as processes: the host (plughost.h).
//
#include "uikit/uikit.h"			// (first: its onyxpp.hpp gives placement new -- kbase.h then leaves <new> out)
#include "plughost.h"
#include "appkit/appkit.h"
#include "systemkit/applet_proto.h"
#include "json.hpp"

namespace kt {

// ---- small helpers ---------------------------------------------------------------------------------------
static unsigned ticks () { return kapi_get_ticks (); }			// 10 ms
static void cat (char *d, int cap, const char *s) { int n = (int) strlen (d); while (*s && n + 1 < cap) d[n++] = *s++; d[n] = 0; }
static void catNum (char *d, int cap, int v)
{
	char t[16]; int k = 0; bool neg = v < 0; unsigned u = neg ? (unsigned) -v : (unsigned) v;
	do { t[k++] = (char) ('0' + u % 10); u /= 10; } while (u);
	char o[18]; int j = 0; if (neg) o[j++] = '-';
	while (k) o[j++] = t[--k];
	o[j] = 0; cat (d, cap, o);
}
static void setErr (char *e, const char *s) { e[0] = 0; cat (e, 96, s); }

// a whole (small) file -> text (0: none); the caller deletes []
static char *readFile (const char *path, unsigned max, unsigned *len)
{
	void *f = kapi_open (path);
	if (!f) return 0;
	unsigned n = kapi_fsize (f);
	if (n > max) n = max;
	char *b = new char[n + 1];
	int got = kapi_read (f, b, n);
	kapi_close (f);
	if (got < 0) got = 0;
	b[got] = 0;
	if (len) *len = (unsigned) got;
	return b;
}

// ---- PlugInstance ------------------------------------------------------------------------------------------
PlugInstance::PlugInstance () : user (0), m_number (0), m_state (PLUG_STARTING), m_pid (0), m_sid (0), m_shm (0), m_pool (-1),
	m_src (0), m_fx (0), m_track (-1), m_slot (-1), m_wasTrack (-1), m_wasSlot (-1), m_t0 (0), m_beat (0), m_beatT (0), m_dspBeat (0), m_dspT (0), m_dspWant (0),
	m_xlock (0), m_pending (0), m_editor (0), m_edSid (0), m_edW (0), m_edH (0), m_edPx (0), m_dying (false), m_generator (false),
	m_dieT (0), m_usedT (0)
{
	m_proc[0] = m_err[0] = 0;
	for (int i = 0; i < KP_MAX_PARAMS; i++) m_pendingVal[i] = 0;
}

int PlugInstance::paramIndex (const char *id) const
{
	for (int i = 0; i < m_info.params.size (); i++) if (m_info.params[i].id == id) return i;
	return -1;
}

float PlugInstance::param (int i) const
{
	if (i < 0 || i >= m_info.params.size ()) return 0;
	if (m_pending & (1ull << i)) return m_pendingVal[i];
	return m_shm && m_state == PLUG_READY ? m_shm->param[i] : m_info.params[i].def;
}

// ---- PlugHost: setting up, the catalogue --------------------------------------------------------------------
PlugHost::PlugHost () : foreign (0), onCrash (0), onParam (0), onDirty (0), ctx (0), m_ok (false), m_rate (44100),
	m_lead (KP_LEAD_DEFAULT), m_latency (KP_LEAD_DEFAULT), m_self (0), m_next (1), m_engine (0), m_lock (0), m_lastProcs (0),
	m_npids (0), m_procText (0) {}

PlugHost::~PlugHost () { shutdown (); delete [] m_procText; }

bool PlugHost::init (int sampleRate)
{
	m_rate = sampleRate > 0 ? sampleRate : 44100;
	if (m_ok) return true;
	if (kapi_abi_version () < 68) return false;			// (word waits, priorities: kapi v68)
	if (!kapi_ipc_register (KP_SERVICE)) return false;	// (another Koton hosts plugins)
	m_self = kapi_ipc_lookup (KP_SERVICE);
	if (m_self <= 0) return false;
	if (!m_procText) m_procText = new char[16384];
	m_inst.reserve (MAX_INSTANCES);			// (never moved: the generator hook's thread reads it too)
	m_ok = true;
	return true;
}

void PlugHost::setLead (int f) { m_lead = f < KP_LEAD_MIN ? KP_LEAD_MIN : f > KP_LEAD_MAX ? KP_LEAD_MAX : f; }
void PlugHost::setLatency (int f) { m_latency = f < KP_LEAD_MIN ? KP_LEAD_MIN : f > KP_LEAD_MAX ? KP_LEAD_MAX : f; }

int PlugHost::scan (const char *dir)
{
	m_infos.clear ();
	void *d = kapi_opendir (dir);
	if (!d) return 0;
	struct kapi_dirent e;
	while (kapi_readdir (d, &e) > 0)
	{
		if (!e.is_dir || e.name[0] == '.') continue;
		char path[256]; path[0] = 0; cat (path, sizeof path, dir); cat (path, sizeof path, "/"); cat (path, sizeof path, e.name);
		char mf[300]; mf[0] = 0; cat (mf, sizeof mf, path); cat (mf, sizeof mf, "/plugin.json");
		char exe[300]; exe[0] = 0; cat (exe, sizeof exe, path); cat (exe, sizeof exe, "/main");
		unsigned len = 0;
		char *text = readFile (mf, 64 * 1024, &len);
		if (!text) continue;
		void *x = kapi_open (exe);
		if (!x) { delete [] text; continue; }
		kapi_close (x);
		json::Doc doc;
		if (doc.parse (text, len, json::TOLERANT) && doc.root ().isObj ())
		{
			const json::Value &r = doc.root ();
			PlugInfo &pi = m_infos.add ();
			pi.id = e.name; pi.exe = exe;
			pi.name = r["name"].asStr (e.name); pi.text = r["text"].asStr (""); pi.vendor = r["vendor"].asStr ("");
			const char *k = r["kind"].asStr ("");
			pi.kind = json::seqi (k, "instrument") ? KP_INSTRUMENT : json::seqi (k, "effect") ? KP_EFFECT : json::seqi (k, "generator") ? KP_GENERATOR : 0;
			for (const json::Value *a = r["aliases"].first (); a; a = a->next) if (a->isStr ()) pi.aliases.push (Str (a->asStr ()));
			pi.editorW = r["editor"]["w"].asInt (0); pi.editorH = r["editor"]["h"].asInt (0);
			for (const json::Value *p = r["params"].first (); p && pi.params.size () < KP_MAX_PARAMS; p = p->next)
			{
				PlugParam &q = pi.params.add ();
				q.id = (*p)["id"].asStr (""); q.name = (*p)["name"].asStr (q.id.c ()); q.unit = (*p)["unit"].asStr ("");
				q.min = (*p)["min"].asFloat (0); q.max = (*p)["max"].asFloat (1); q.def = (*p)["default"].asFloat (q.min); q.step = (*p)["step"].asFloat (0);
				for (const json::Value *c = (*p)["choices"].first (); c; c = c->next) q.choices.push (Str (c->asStr ("")));
			}
			if (!pi.kind) m_infos.pop ();
		}
		delete [] text;
	}
	kapi_closedir (d);
	m_infos.sort ([] (const PlugInfo &a, const PlugInfo &b) { return strcmp (a.name.c (), b.name.c ()) < 0; });
	return m_infos.size ();
}

const PlugInfo *PlugHost::find (const char *id) const
{
	if (!id || !id[0]) return 0;
	for (int i = 0; i < m_infos.size (); i++) if (json::seqi (m_infos[i].id.c (), id)) return &m_infos[i];
	for (int i = 0; i < m_infos.size (); i++)
		for (int a = 0; a < m_infos[i].aliases.size (); a++) if (json::seqi (m_infos[i].aliases[a].c (), id)) return &m_infos[i];
	return 0;
}

int PlugHost::countKind (int kind) const { int n = 0; for (int i = 0; i < m_infos.size (); i++) if (m_infos[i].kind == kind) n++; return n; }

// ---- the processes ------------------------------------------------------------------------------------------
// the list of processes (kapi_list_procs: "<pid> <a|k> <state> <pages> <name>" lines)
void PlugHost::procs ()
{
	m_npids = 0;
	if (!m_procText) return;
	int n = kapi_list_procs (m_procText, 16384);
	(void) n;
	for (const char *p = m_procText; *p && m_npids < 256; )
	{
		int pid = 0; while (*p >= '0' && *p <= '9') pid = pid * 10 + (*p++ - '0');
		if (pid > 0) m_pids[m_npids++] = pid;
		while (*p && *p != '\n') p++;
		if (*p) p++;
	}
	m_lastProcs = ticks ();
}

bool PlugHost::pidAlive (int pid) const { for (int i = 0; i < m_npids; i++) if (m_pids[i] == pid) return true; return false; }

int PlugHost::pidByName (const char *name) const
{
	if (!m_procText) return 0;
	for (const char *p = m_procText; *p; )
	{
		int pid = 0; while (*p >= '0' && *p <= '9') pid = pid * 10 + (*p++ - '0');
		for (int f = 0; f < 3 && *p && *p != '\n'; f++) { while (*p == ' ') p++; while (*p && *p != ' ' && *p != '\n') p++; }
		while (*p == ' ') p++;
		const char *s = p; while (*p && *p != '\n') p++;
		int len = (int) (p - s);
		if (pid > 0 && len == (int) strlen (name) && !strncmp (s, name, len)) return pid;
		if (*p) p++;
	}
	return 0;
}

int PlugHost::takeRegion ()
{
	for (int i = 0; i < m_regions.size (); i++) if (!m_regions[i].used) { m_regions[i].used = true; return i; }
	int sid = kapi_surface_create (KP_SHM_W, KP_SHM_H), w = 0, h = 0;
	if (sid <= 0) return -1;
	KpShm *s = (KpShm *) kapi_surface_map (sid);
	if (!s || !kapi_surface_size (sid, &w, &h) || (unsigned) (w * h * 4) < sizeof (KpShm)) { kapi_surface_destroy (sid); return -1; }
	Region r = { sid, s, true };
	m_regions.push (r);
	return m_regions.size () - 1;
}

// the process of p on its region, with that initial state. fresh: the region made new; else (a
// restart) the engine's side of it -- the clock, the rings' counts -- is left as it is (it plays on)
bool PlugHost::start (PlugInstance *p, const char *stateJson, bool fresh)
{
	KpShm *s = p->m_shm;
	if (fresh) kp_shm_init (s, p->m_info.kind, m_rate, m_lead, m_latency, p->m_number);
	else
	{
		s->ready = KP_STARTING; s->pluginVersion = 0; s->nparams = 0; s->paramsLen = 0; s->beat = s->dspBeat = 0;
		s->dspUs = s->dspMaxUs = 0; s->lateFrames = 0; s->initLen = 0;
		s->reqSeq = s->repSeq = 0; s->reqType = s->reqLen = s->repStatus = s->repLen = 0;
	}
	unsigned len = stateJson ? (unsigned) strlen (stateJson) : 0;
	if (len && len <= KP_DATA_BYTES) { memcpy (s->data, stateJson, len); s->initLen = len; }
	p->m_proc[0] = 0; cat (p->m_proc, sizeof p->m_proc, "kp."); cat (p->m_proc, sizeof p->m_proc, p->m_info.id.c ());
	cat (p->m_proc, sizeof p->m_proc, "."); catNum (p->m_proc, sizeof p->m_proc, p->m_number);
	char args[64]; args[0] = 0; cat (args, sizeof args, "--kplug "); catNum (args, sizeof args, p->m_sid);
	cat (args, sizeof args, " "); catNum (args, sizeof args, m_self);
	p->m_pid = 0; p->m_state = PLUG_STARTING; p->m_t0 = p->m_beatT = p->m_dspT = ticks (); p->m_beat = p->m_dspBeat = 0;
	p->m_err[0] = 0;
	if (!kapi_exec_as (p->m_info.exe.c (), args, p->m_proc)) { p->m_state = PLUG_FAILED; setErr (p->m_err, "its program could not be started"); return false; }
	return true;
}

PlugInstance *PlugHost::create (const char *id, const char *stateJson, bool wait)
{
	if (!m_ok || m_inst.size () >= MAX_INSTANCES) return 0;
	const PlugInfo *info = find (id);
	if (!info) return 0;
	PlugInstance *p = new PlugInstance;
	p->m_info = *info;
	kapi_lock (&m_lock);
	int r = takeRegion ();
	if (r < 0) { kapi_unlock (&m_lock); delete p; return 0; }
	p->m_pool = r; p->m_sid = m_regions[r].sid; p->m_shm = m_regions[r].shm;
	p->m_number = m_next++;
	p->m_lastState = stateJson ? stateJson : "";
	m_inst.push (p);
	kapi_unlock (&m_lock);
	start (p, stateJson, true);
	if (wait) waitReady (p, 3000);
	return p;
}

bool PlugHost::restart (PlugInstance *p)
{
	if (!p || p->m_dying || p->m_state == PLUG_READY || p->m_state == PLUG_STARTING) return false;
	if (p->m_pid > 0) kapi_kill_pid (p->m_pid, 1);
	Str st (p->m_lastState);			// (the region is kept: its engine side goes on reading it)
	if (!start (p, st.c (), false)) return false;
	if (p->m_wasTrack >= 0 && !p->m_fx) connectEffect (p, p->m_wasTrack, p->m_wasSlot);	// (taken out at its crash)
	p->m_wasTrack = p->m_wasSlot = -1;
	return true;
}

// up (its region says so), its pid known
bool PlugHost::waitReady (PlugInstance *p, int timeoutMs)
{
	if (!p || !p->m_shm) return false;
	unsigned t0 = ticks ();
	for (;;)
	{
		unsigned r = kp_ld32 (&p->m_shm->ready);
		if (r == KP_FAILED) { if (p->m_state == PLUG_STARTING) { p->m_state = PLUG_FAILED; setErr (p->m_err, "it refused to start (another kind, protocol)"); } return false; }
		if (r == KP_READY && p->m_pid <= 0)
		{
			kapi_lock (&m_lock); procs (); p->m_pid = pidByName (p->m_proc); kapi_unlock (&m_lock);
		}
		if (r == KP_READY && p->m_pid > 0)
		{
			if (p->m_state == PLUG_STARTING) p->m_state = PLUG_READY;
			return p->m_state == PLUG_READY;
		}
		if (p->m_state != PLUG_STARTING && p->m_state != PLUG_READY) return false;
		if ((int) (ticks () - t0) * 10 >= timeoutMs) return false;
		kapi_wait_word (&p->m_shm->ready, r, 20);
	}
}

void PlugHost::destroy (PlugInstance *p)
{
	if (!p || p->m_dying) return;
	if (p->m_editor) closeEditor (p->m_editor);
	disconnect (p);
	for (int t = 0; t < m_tracks.size (); t++)
	{
		if (m_tracks[t].instr == p) m_tracks[t].instr = 0;
		for (int s = 0; s < 4; s++) if (m_tracks[t].fx[s] == p) m_tracks[t].fx[s] = 0;
	}
	if (p->m_pid > 0) kapi_mailbox_send (p->m_pid, KP_BYE, 0, 0);
	p->m_dying = true; p->m_dieT = ticks ();
	if (p->m_state == PLUG_READY || p->m_state == PLUG_STARTING) p->m_state = PLUG_ENDING;
}

PlugInstance *PlugHost::byPid (int pid) const
{
	if (pid <= 0) return 0;
	for (int i = 0; i < m_inst.size (); i++) if (m_inst[i]->m_pid == pid) return m_inst[i];
	return 0;
}

void PlugHost::lose (PlugInstance *p, const char *why)
{
	if (p->m_dying || p->m_state == PLUG_CRASHED || p->m_state == PLUG_FAILED) return;
	bool wasStarting = p->m_state == PLUG_STARTING;
	p->m_state = wasStarting ? PLUG_FAILED : PLUG_CRASHED;
	setErr (p->m_err, why);
	if (p->m_pid > 0) kapi_kill_pid (p->m_pid, 1);
	if (p->m_editor) { p->m_editor->m_up = false; p->m_editor->invalidate (true); }
	// an effect taken out of its track: the track goes on dry (restart puts it back); an instrument
	// stays (silent: its track has nothing else to play)
	if (p->m_fx) { int t = p->m_track, sl = p->m_slot; disconnect (p); p->m_wasTrack = t; p->m_wasSlot = sl; }
	if (onCrash) onCrash (p, ctx);
	if (p->m_generator && !p->m_editor) { p->m_dying = true; p->m_dieT = ticks (); }	// (the next request starts another)
}

// ---- the engine ---------------------------------------------------------------------------------------------
void PlugHost::attach (Engine *e)
{
	if (!e && m_engine)			// (the engine was deleted: its sides are no one's any more)
		for (int i = 0; i < m_retired.size (); i++) { delete m_retired[i].src; delete m_retired[i].fx; }
	if (!e) m_retired.clear ();
	m_engine = e;
}

void PlugHost::retire (ShmSource *s, ShmEffect *f)
{
	if (!s && !f) return;
	if (!m_engine) { delete s; delete f; return; }
	Retired r = { s, f, __atomic_load_n (&m_engine->renders, __ATOMIC_ACQUIRE) };
	m_retired.push (r);
}

bool PlugHost::connectInstrument (PlugInstance *p, int track)
{
	if (!p || !m_engine || p->m_dying || p->m_info.kind != KP_INSTRUMENT || track < 0 || track >= ENGINE_MAX_TRACKS) return false;
	disconnect (p);
	while (m_tracks.size () <= track) { TrackPlugs t = { 0, { 0, 0, 0, 0 } }; m_tracks.push (t); }
	// the track's other instrument plugin, if any, gives its place
	for (int i = 0; i < m_inst.size (); i++) if (m_inst[i] != p && m_inst[i]->m_src && m_inst[i]->m_track == track) disconnect (m_inst[i]);
	ShmSource *src = new ShmSource (p->m_shm, (int) p->m_shm->lead);
	if (!m_engine->post (CMD_EXTERNAL, 0, 0, track, 0, src)) { delete src; return false; }
	p->m_src = src; p->m_track = track; p->m_slot = -1;
	p->m_shm->active = 1;
	return true;
}

bool PlugHost::connectEffect (PlugInstance *p, int track, int slot)
{
	if (!p || !m_engine || p->m_dying || p->m_info.kind != KP_EFFECT || track < 0 || track >= ENGINE_MAX_TRACKS || slot < 0 || slot >= 4) return false;
	disconnect (p);
	for (int i = 0; i < m_inst.size (); i++) if (m_inst[i] != p && m_inst[i]->m_fx && m_inst[i]->m_track == track && m_inst[i]->m_slot == slot) disconnect (m_inst[i]);
	ShmEffect *fx = new ShmEffect (p->m_shm, (int) p->m_shm->latency);
	m_engine->setInsert (track, slot, fx);
	p->m_wasTrack = p->m_wasSlot = -1;
	p->m_fx = fx; p->m_track = track; p->m_slot = slot;
	p->m_shm->active = 1;
	return true;
}

void PlugHost::disconnect (PlugInstance *p)
{
	if (!p) return;
	if (p->m_src)
	{
		if (m_engine && !m_engine->post (CMD_EXTERNAL, 0, 0, p->m_track, 0, (void *) 0))
		{
			// (the command ring is full: try again at the next tick -- the source stays connected meanwhile)
			return;
		}
		retire (p->m_src, 0); p->m_src = 0;
	}
	if (p->m_fx)
	{
		if (m_engine) m_engine->setInsert (p->m_track, p->m_slot, 0);
		retire (0, p->m_fx); p->m_fx = 0;
	}
	p->m_track = p->m_slot = -1;
	if (p->m_shm) p->m_shm->active = 0;
}

// ---- parameters, states, requests ---------------------------------------------------------------------------------
void PlugHost::setParam (PlugInstance *p, int i, float v)
{
	if (!p || i < 0 || i >= p->m_info.params.size () || i >= KP_MAX_PARAMS) return;
	p->m_pendingVal[i] = v;
	p->m_pending |= 1ull << i;
	if (p->m_state == PLUG_READY && p->m_pid > 0)
	{
		KpParamMsg m = { i, v };
		if (kapi_mailbox_send (p->m_pid, KP_SET_PARAM, &m, sizeof m) > 0) p->m_pending &= ~(1ull << i);
	}
}

// A request through the region's channel (the payload and the reply in data[]); the mailbox message
// is the doorbell, the word the plugin sleeps on another. Its lock: one request at a time.
bool PlugHost::request (PlugInstance *p, unsigned type, const char *payload, unsigned len, Str *reply, int timeoutMs)
{
	if (!p || p->m_dying || len > KP_DATA_BYTES || !waitReady (p, timeoutMs)) return false;
	kapi_lock (&p->m_xlock);
	KpShm *s = p->m_shm;
	unsigned seq = kp_req_post (s, type, payload, len);
	KpReq r = { seq };
	kapi_mailbox_send (p->m_pid, (int) type, &r, sizeof r);
	kapi_wake_word (&s->reqSeq);
	unsigned t0 = ticks ();
	bool ok = false;
	for (;;)
	{
		unsigned rs = kp_ld32 (&s->repSeq);
		if (rs == seq)
		{
			ok = s->repStatus == KP_OK;
			if (ok && reply) reply->set (s->data, (int) (s->repLen <= KP_DATA_BYTES ? s->repLen : 0));
			break;
		}
		if ((int) (ticks () - t0) * 10 >= timeoutMs || kp_ld32 (&s->ready) != KP_READY) break;
		kapi_wait_word (&s->repSeq, rs, 20);
	}
	kapi_unlock (&p->m_xlock);
	return ok;
}

// the parameters not sent yet (a full mailbox, a plugin starting)
void PlugHost::flushParams (PlugInstance *p)
{
	if (!p->m_pending || p->m_state != PLUG_READY || p->m_pid <= 0) return;
	for (int k = 0; k < KP_MAX_PARAMS; k++)
		if (p->m_pending & (1ull << k))
		{
			KpParamMsg m = { k, p->m_pendingVal[k] };
			if (kapi_mailbox_send (p->m_pid, KP_SET_PARAM, &m, sizeof m) > 0) p->m_pending &= ~(1ull << k);
			else break;
		}
}

bool PlugHost::getState (PlugInstance *p, Str &json, int timeoutMs)
{
	if (!p) return false;
	waitReady (p, timeoutMs);
	for (int k = 0; k < 50 && p->m_pending && p->m_state == PLUG_READY; k++) { flushParams (p); if (p->m_pending) kapi_msleep (2); }
	if (!request (p, KP_STATE_GET, 0, 0, &json, timeoutMs)) return false;
	p->m_lastState = json;
	return true;
}

bool PlugHost::setState (PlugInstance *p, const char *json, int timeoutMs)
{
	if (!p || !json) return false;
	p->m_pending = 0;
	if (!request (p, KP_STATE_SET, json, (unsigned) strlen (json), 0, timeoutMs)) return false;
	p->m_lastState = json;
	return true;
}

// ---- a project's tracks --------------------------------------------------------------------------------------
PlugInstance *PlugHost::trackInstrument (int t) const { return t >= 0 && t < m_tracks.size () ? m_tracks[t].instr : 0; }
PlugInstance *PlugHost::trackInsert (int t, int s) const { return t >= 0 && t < m_tracks.size () && s >= 0 && s < 4 ? m_tracks[t].fx[s] : 0; }

bool PlugHost::syncTrack (int track, const Track &t)
{
	if (!m_ok || track < 0 || track >= ENGINE_MAX_TRACKS) return false;
	while (m_tracks.size () <= track) { TrackPlugs tp = { 0, { 0, 0, 0, 0 } }; m_tracks.push (tp); }
	TrackPlugs &tp = m_tracks[track];
	bool ok = true;
	// the instrument
	const PluginSlot &is = t.instrumentPlugin;
	bool want = !is.id.empty () && t.type != TRACK_CHORD;
	const PlugInfo *wi = want ? find (is.id) : 0;
	if (tp.instr && (!want || !wi || !(tp.instr->m_info.id == wi->id))) { PlugInstance *o = tp.instr; tp.instr = 0; destroy (o); }
	if (want && wi && !tp.instr)
	{
		tp.instr = create (is.id, is.state.c (), false);
		if (!tp.instr) ok = false;
	}
	if (want && !wi) ok = false;
	if (tp.instr)
	{
		bool up = tp.instr->m_state == PLUG_READY || tp.instr->m_state == PLUG_STARTING;	// (a crashed one: restart ())
		if (is.enabled && up && tp.instr->m_track != track) ok = connectInstrument (tp.instr, track) && ok;
		else if (!is.enabled && tp.instr->m_src) disconnect (tp.instr);
	}
	// the inserts, slots 0..3
	for (int s = 0; s < 4; s++)
	{
		const PluginSlot *ps = s < t.inserts.size () ? &t.inserts[s] : 0;
		const PlugInfo *fi = ps && !ps->id.empty () ? find (ps->id) : 0;
		if (fi && fi->kind != KP_EFFECT) fi = 0;
		if (tp.fx[s] && (!fi || !(tp.fx[s]->m_info.id == fi->id))) { PlugInstance *o = tp.fx[s]; tp.fx[s] = 0; destroy (o); }
		if (fi && !tp.fx[s])
		{
			tp.fx[s] = create (ps->id, ps->state.c (), false);
			if (!tp.fx[s]) ok = false;
		}
		if (ps && !ps->id.empty () && !fi) ok = false;
		if (tp.fx[s])
		{
			bool up = tp.fx[s]->m_state == PLUG_READY || tp.fx[s]->m_state == PLUG_STARTING;
			if (ps->enabled && up && (tp.fx[s]->m_track != track || tp.fx[s]->m_slot != s)) ok = connectEffect (tp.fx[s], track, s) && ok;
			else if (!ps->enabled && tp.fx[s]->m_fx) disconnect (tp.fx[s]);
		}
	}
	return ok;
}

void PlugHost::saveTrack (int track, Track &t)
{
	if (track < 0 || track >= m_tracks.size ()) return;
	TrackPlugs &tp = m_tracks[track];
	Str st;
	if (tp.instr && tp.instr->m_info.id == t.instrumentPlugin.id && getState (tp.instr, st)) t.instrumentPlugin.state = st;
	for (int s = 0; s < 4 && s < t.inserts.size (); s++)
		if (tp.fx[s] && tp.fx[s]->m_info.id == t.inserts[s].id && getState (tp.fx[s], st)) t.inserts[s].state = st;
}

void PlugHost::releaseTracks (int from)
{
	for (int t = from < 0 ? 0 : from; t < m_tracks.size (); t++)
	{
		TrackPlugs &tp = m_tracks[t];
		if (tp.instr) { PlugInstance *o = tp.instr; tp.instr = 0; destroy (o); }
		for (int s = 0; s < 4; s++) if (tp.fx[s]) { PlugInstance *o = tp.fx[s]; tp.fx[s] = 0; destroy (o); }
	}
	if (from >= 0 && from < m_tracks.size ()) m_tracks.resize (from);
}

// ---- generators ------------------------------------------------------------------------------------------------
static PlugHost *s_hookHost;
static bool generatorHook (const GeneratorModule &m, const Project &p, double startBeat, Riff &out)
{
	return s_hookHost && s_hookHost->generate (m, p, startBeat, out);
}
void PlugHost::installGeneratorHook () { s_hookHost = this; g_generatorHook = generatorHook; }

PlugInstance *PlugHost::generator (const char *id)
{
	const PlugInfo *info = find (id);
	if (!m_ok || !info || info->kind != KP_GENERATOR) return 0;
	PlugInstance *g = 0;
	kapi_lock (&m_lock);
	for (int i = 0; i < m_inst.size () && !g; i++)
	{
		PlugInstance *p = m_inst[i];
		if (p->m_generator && !p->m_dying && p->m_info.id == info->id && (p->m_state == PLUG_READY || p->m_state == PLUG_STARTING)) g = p;
	}
	kapi_unlock (&m_lock);
	if (g) { g->m_usedT = ticks (); return g; }
	g = create (info->id, 0, true);
	if (g) { g->m_generator = true; g->m_usedT = ticks (); }
	return g;
}

// The song is compiled again at every edit: a module whose request (its state, the chords under it, the
// key...) did not change gets the notes its process gave last time, without asking again.
static unsigned fnv (const char *s, unsigned long n, unsigned h = 2166136261u)
{
	for (unsigned long i = 0; i < n; i++) { h ^= (unsigned char) s[i]; h *= 16777619u; }
	return h;
}

bool PlugHost::generate (const GeneratorModule &m, const Project &p, double startBeat, Riff &out, int timeoutMs)
{
	PlugInstance *g = generator (m.generatorId);
	if (!g) return false;
	Str st; moduleStateJson (m, st);
	json::Writer w (false);
	genContext (m, p, startBeat, st.c (), w);
	if (!w.ok ()) return false;
	unsigned h = fnv (w.data (), w.size (), fnv (g->m_info.id.c (), g->m_info.id.len ()));
	kapi_lock (&m_lock);
	for (int i = 0; i < m_genCache.size (); i++)
		if (m_genCache[i].hash == h && m_genCache[i].len == (unsigned) w.size () && m_genCache[i].number == g->m_number)
		{
			out.notes = m_genCache[i].notes;
			kapi_unlock (&m_lock);
			g->m_usedT = ticks ();
			return true;
		}
	kapi_unlock (&m_lock);
	Str reply;
	g->m_usedT = ticks ();
	if (!request (g, KP_GENERATE, w.data (), (unsigned) w.size (), &reply, timeoutMs)) return false;
	g->m_usedT = ticks ();
	if (!genReply (reply.c (), (unsigned long) reply.len (), out)) return false;
	kapi_lock (&m_lock);
	if (m_genCache.size () >= GEN_CACHE) m_genCache.removeAt (0);
	GenCached &c = m_genCache.add ();
	c.hash = h; c.len = (unsigned) w.size (); c.number = g->m_number; c.notes = out.notes;
	kapi_unlock (&m_lock);
	return true;
}

PlugEditorView *PlugHost::openGeneratorEditor (const GeneratorModule &m, uikit::Widget &parent, int x, int y, int w, int h)
{
	PlugInstance *g = generator (m.generatorId);
	if (!g) return 0;
	Str st; moduleStateJson (m, st);
	setState (g, st.empty () ? "{}" : st.c ());
	g->m_editModule = m.id;
	return openEditor (g, parent, x, y, w, h);
}

bool PlugHost::pullGeneratorState (PlugInstance *p, Project &pr)
{
	if (!p || !p->m_generator || p->m_editModule.empty ()) return false;
	for (int t = 0; t < pr.tracks.size (); t++)
		for (int i = 0; i < pr.tracks[t].items.size (); i++)
		{
			Module *m = pr.tracks[t].items[i].module;
			if (m && m->kind == M_GENERATOR && m->id == p->m_editModule) return pullGeneratorState (p, *(GeneratorModule *) m);
		}
	return false;
}

bool PlugHost::pullGeneratorState (PlugInstance *p, GeneratorModule &m)
{
	Str st, old;
	if (!getState (p, st)) return false;
	moduleStateJson (m, old);
	if (old == st) return false;
	setModuleStateJson (m, st.c ());
	return true;
}

// ---- editors ------------------------------------------------------------------------------------------------------
void PlugHost::editorSize (const PlugInstance *p, int *w, int *h) const { editorSize (p ? &p->m_info : 0, w, h); }
void PlugHost::editorSize (const PlugInfo *info, int *w, int *h) const
{
	int W = 480, H = 240;
	if (info && info->editorW > 0 && info->editorH > 0) { W = info->editorW; H = info->editorH; }
	else if (info)
	{
		// (as kplug.h lays its knobs out: 96 px a knob, 170 a drop-down, 150 a check box, rows of 600 px)
		int fh = uikit::uk_fh (), x = 10, rows = 1, rowW = 600;
		for (int i = 0; i < info->params.size (); i++)
		{
			const PlugParam &q = info->params[i];
			bool tog = q.choices.size () == 2 && q.choices[0] == "Off" && q.choices[1] == "On";
			int cw = q.choices.size () && !tog ? 170 : tog ? 150 : 96;
			if (x + cw > rowW - 6 && x > 10) { x = 10; rows++; }
			x += cw;
		}
		W = rowW; H = fh + 18 + rows * (2 * fh + 58 + 8) + 6;
	}
	if (w) *w = W;
	if (h) *h = H;
}

PlugEditorView *PlugHost::openEditor (PlugInstance *p, uikit::Widget &parent, int x, int y, int w, int h)
{
	if (!p || p->m_dying || w < 32 || h < 32) return 0;
	if (p->m_editor) closeEditor (p->m_editor);
	if (!waitReady (p, 3000)) return 0;
	if (!p->m_edSid || p->m_edW < w || p->m_edH < h)
	{
		if (p->m_edSid) { kapi_surface_destroy (p->m_edSid); p->m_edSid = 0; p->m_edPx = 0; }	// (never read again)
		int sid = kapi_surface_create (w, h), sw = 0, sh = 0;
		unsigned *px = sid > 0 ? kapi_surface_map (sid) : 0;
		if (!px || !kapi_surface_size (sid, &sw, &sh)) { if (sid > 0) kapi_surface_destroy (sid); return 0; }
		p->m_edSid = sid; p->m_edPx = px; p->m_edW = sw; p->m_edH = sh;
	}
	if (w > p->m_edW) w = p->m_edW;
	if (h > p->m_edH) h = p->m_edH;
	for (int r = 0; r < h; r++) for (int c = 0; c < w; c++) p->m_edPx[r * p->m_edW + c] = uikit::C_BG;
	PlugEditorView *v = new PlugEditorView (this, p, x, y, w, h);
	p->m_editor = v;
	parent.addChild (v);
	// the app's colours now (its palette: what it applied, not the theme file's) -- the plugin draws with them
	KpEditor e = { p->m_edSid, w, h, 1, uikit::C_BG, uikit::C_BUTTON, uikit::C_FIELD, uikit::C_ACCENT };
	kapi_mailbox_send (p->m_pid, KP_EDITOR, &e, sizeof e);
	return v;
}

void PlugHost::closeEditor (PlugEditorView *v)
{
	if (!v) return;
	if (v->parent) v->parent->removeChild (v);
	delete v;					// (its destructor tells the plugin: editorGone)
}

void PlugHost::editorGone (PlugEditorView *v)
{
	PlugInstance *p = v->m_inst;
	if (!p || p->m_editor != v) return;
	p->m_editor = 0;
	p->m_editModule = "";
	if (p->m_pid > 0 && (p->m_state == PLUG_READY)) kapi_mailbox_send (p->m_pid, AP_CLOSE, 0, 0);
}

PlugEditorView::PlugEditorView (PlugHost *h, PlugInstance *p, int x, int y, int w, int hh)
	: Widget (x, y, w, hh), m_host (h), m_inst (p), m_up (false), m_closed (false), m_inside (false), m_buttons (0)
{
	canFocus = true;
}

PlugEditorView::~PlugEditorView () { if (m_host) m_host->editorGone (this); }

void PlugEditorView::onDraw ()
{
	using namespace uikit;
	PlugInstance *p = m_inst;
	if (!p || !m_up || !p->m_edPx || p->m_state != PLUG_READY)
	{
		canvas.clear (C_BG);
		const char *s = !p ? "No plugin" : p->m_state == PLUG_CRASHED || p->m_state == PLUG_FAILED ? p->m_err
			: p->m_state == PLUG_STARTING ? "Starting..." : m_closed ? "The editor closed." : "Opening the editor...";
		uk_text_c (canvas, 0, 0, width, height, s[0] ? s : "The plugin stopped.", C_DIS);
		return;
	}
	for (int y = 0; y < height && y < p->m_edH; y++)
	{
		unsigned *d = canvas.px + (long) y * canvas.stride;
		const unsigned *s = p->m_edPx + (long) y * p->m_edW;
		for (int x = 0; x < width && x < p->m_edW; x++) d[x] = s[x] & 0x00FFFFFFu;
	}
}

void PlugEditorView::send (int ev, int x, int y, int btn, int changed, int wheel)
{
	if (!m_inst || m_inst->m_pid <= 0) return;
	ApPtr p = { ev, x, y, btn, changed, wheel };
	kapi_mailbox_send (m_inst->m_pid, AP_PTR, &p, sizeof p);
}

bool PlugEditorView::onMouse (int mx, int my, int bl, int br, int bm, int wheel)
{
	if (!m_inst || !m_up) return false;
	int btn = (bl ? 1 : 0) | (br ? 2 : 0) | (bm ? 4 : 0);
	bool in = mx >= 0 && my >= 0 && mx < width && my < height;
	if (!in && !m_buttons)				// (a drag begun here stays here)
	{
		if (m_inside) send (GUI_EVENT_PTR_LEAVE, -1, -1, 0, 0, 0);
		m_inside = false; catchOutside = false;
		return false;
	}
	int x = mx < 0 ? 0 : mx >= width ? width - 1 : mx, y = my < 0 ? 0 : my >= height ? height - 1 : my;
	if (!m_inside) { m_inside = true; send (GUI_EVENT_PTR_ENTER, x, y, btn, 0, 0); }
	if (wheel) send (GUI_EVENT_PTR_WHEEL, x, y, btn, 0, wheel);
	else if (btn != m_buttons)
	{
		for (int b = 1; b <= 4; b <<= 1)
			if ((btn ^ m_buttons) & b) send ((btn & b) ? GUI_EVENT_PTR_DOWN : GUI_EVENT_PTR_UP, x, y, btn, b, 0);
	}
	else send (GUI_EVENT_PTR_MOVE, x, y, btn, 0, 0);
	if (btn && !m_buttons) setFocus ();
	m_buttons = btn;
	catchOutside = btn != 0;
	return true;
}

bool PlugEditorView::onKey (long k)
{
	if (!m_inst || !m_up || m_inst->m_pid <= 0) return false;
	ApKey key = { (int) k, kapi_get_modifiers () };
	kapi_mailbox_send (m_inst->m_pid, AP_KEY, &key, sizeof key);
	return true;
}

// ---- the loop ---------------------------------------------------------------------------------------------------
bool PlugHost::handleMessage (int from, int type, const void *data, int len)
{
	if (!m_ok) return false;
	PlugInstance *p = byPid (from);
	if (!p && type == KP_HELLO && len >= (int) sizeof (KpHello))		// (a new one: by its region)
	{
		KpHello h; memcpy (&h, data, sizeof h);
		for (int i = 0; i < m_inst.size () && !p; i++) if (m_inst[i]->m_sid == h.shm && m_inst[i]->m_pid <= 0) p = m_inst[i];
		if (p) p->m_pid = from;
	}
	if (!p) return false;
	switch (type)
	{
	case KP_HELLO: if (p->m_state == PLUG_STARTING && kp_ld32 (&p->m_shm->ready) == KP_READY) p->m_state = PLUG_READY; break;
	case KP_PARAM_CHANGED:
		if (len >= (int) sizeof (KpParamMsg))
		{
			KpParamMsg m; memcpy (&m, data, sizeof m);
			if (m.index >= 0 && m.index < KP_MAX_PARAMS) p->m_pending &= ~(1ull << m.index);	// (its editor won)
			if (onParam && !p->m_dying) onParam (p, m.index, m.value, ctx);
		}
		break;
	case KP_DIRTY: if (onDirty && !p->m_dying) onDirty (p, ctx); break;
	case KP_BYE: if (!p->m_dying) lose (p, "The plugin ended."); break;
	case AP_HELLO: if (p->m_editor) { p->m_editor->m_up = true; p->m_editor->m_closed = false; p->m_editor->invalidate (true); } break;
	case AP_PRESENT: if (p->m_editor) p->m_editor->invalidate (true); break;
	case AP_EXIT: if (p->m_editor) { p->m_editor->m_up = false; p->m_editor->m_closed = true; p->m_editor->invalidate (true); } break;
	default: break;
	}
	return true;
}

void PlugHost::poll ()
{
	if (!m_ok) return;
	int from = 0, type = 0, n;
	unsigned char buf[512];
	while ((n = kapi_mailbox_recv (&from, &type, buf, sizeof buf, 0)) >= 0)
		if (!handleMessage (from, type, buf, n) && foreign) foreign (from, type, buf, n, ctx);
	tick ();
}

void PlugHost::tick ()
{
	if (!m_ok) return;
	unsigned now = ticks ();
	// what the engine let go (two blocks after: it is no longer inside them)
	for (int i = 0; i < m_retired.size (); )
	{
		if (!m_engine || m_engine->renders - m_retired[i].renders >= 2) { delete m_retired[i].src; delete m_retired[i].fx; m_retired.removeAt (i); }
		else i++;
	}
	// the parameters not sent yet (a full mailbox, a plugin starting)
	for (int i = 0; i < m_inst.size (); i++) flushParams (m_inst[i]);
	// the source of an instrument taken out while the command ring was full: again
	for (int i = 0; i < m_inst.size (); i++) if (m_inst[i]->m_dying && m_inst[i]->m_src) disconnect (m_inst[i]);
	// twice a second: which processes still run
	if (now - m_lastProcs < 50) return;
	kapi_lock (&m_lock);
	procs ();
	for (int i = 0; i < m_inst.size (); i++)
	{
		PlugInstance *p = m_inst[i];
		KpShm *s = p->m_shm;
		if (p->m_dying) continue;
		if (p->m_pid <= 0) p->m_pid = pidByName (p->m_proc);
		if (p->m_state == PLUG_STARTING)
		{
			unsigned r = kp_ld32 (&s->ready);
			if (r == KP_READY && p->m_pid > 0) p->m_state = PLUG_READY;
			else if (r == KP_FAILED) { p->m_state = PLUG_FAILED; setErr (p->m_err, "It refused to start."); if (onCrash) onCrash (p, ctx); }
			else if (now - p->m_t0 > 800) { kapi_unlock (&m_lock); lose (p, "It did not start."); kapi_lock (&m_lock); }
			continue;
		}
		if (p->m_state != PLUG_READY) continue;
		if (p->m_pid > 0 && !pidAlive (p->m_pid)) { kapi_unlock (&m_lock); lose (p, "The plugin stopped (it crashed?)."); kapi_lock (&m_lock); continue; }
		// hung: its main thread stopped beating for 3 s, or its render thread while it has frames to render
		if (s->beat != p->m_beat) { p->m_beat = s->beat; p->m_beatT = now; }
		unsigned long long want = s->want;
		if (s->dspBeat != p->m_dspBeat || want == p->m_dspWant || s->done >= want) { p->m_dspBeat = s->dspBeat; p->m_dspT = now; }
		p->m_dspWant = want;
		bool busy = p->m_xlock != 0;			// (a request in flight has its own timeout)
		if (busy) { p->m_beatT = now; }
		if (now - p->m_beatT > 300 || (p->m_info.kind != KP_GENERATOR && now - p->m_dspT > 300))
		{
			kapi_unlock (&m_lock); lose (p, "The plugin stopped answering: ended."); kapi_lock (&m_lock);
			continue;
		}
		// a generator unused for a minute (no editor open, no request): ended
		if (p->m_generator && !p->m_editor && p->m_xlock == 0 && now - p->m_usedT > 6000) { p->m_dying = true; p->m_dieT = now; kapi_mailbox_send (p->m_pid, KP_BYE, 0, 0); }
	}
	// the ending ones: gone (or killed a second after their KP_BYE), their region free again once the
	// engine let their side go
	for (int i = 0; i < m_inst.size (); )
	{
		PlugInstance *p = m_inst[i];
		if (!p->m_dying) { i++; continue; }
		bool gone = p->m_pid <= 0 || !pidAlive (p->m_pid);
		if (!gone && now - p->m_dieT > 100) { kapi_kill_pid (p->m_pid, 1); gone = true; }
		bool held = p->m_src != 0;
		for (int k = 0; k < m_retired.size () && !held; k++)
			if ((m_retired[k].src && m_retired[k].src->shm () == p->m_shm) || (m_retired[k].fx && m_retired[k].fx->shm () == p->m_shm)) held = true;
		if (gone && !held)
		{
			if (p->m_pool >= 0 && p->m_pool < m_regions.size ()) m_regions[p->m_pool].used = false;
			if (p->m_edSid) kapi_surface_destroy (p->m_edSid);	// (the plugin, its user, is gone: freed)
			m_inst.removeAt (i);
			delete p;
		}
		else i++;
	}
	kapi_unlock (&m_lock);
}

void PlugHost::shutdown ()
{
	if (!m_ok) return;
	if (s_hookHost == this) { g_generatorHook = 0; s_hookHost = 0; }
	for (int i = 0; i < m_inst.size (); i++)
	{
		PlugInstance *p = m_inst[i];
		if (p->m_editor) { p->m_editor->m_host = 0; p->m_editor->m_inst = 0; p->m_editor->m_up = false; p->m_editor = 0; }
		disconnect (p);
		for (int k = 0; k < 50 && p->m_src; k++) { kapi_msleep (2); disconnect (p); }	// (the engine's command ring was full)
		if (p->m_src) { retire (p->m_src, 0); p->m_src = 0; }
		if (p->m_pid > 0) kapi_mailbox_send (p->m_pid, KP_BYE, 0, 0);
		p->m_dying = true;
	}
	// a second for them to end by themselves, then killed
	unsigned t0 = ticks ();
	for (;;)
	{
		procs ();
		bool any = false;
		for (int i = 0; i < m_inst.size (); i++) if (m_inst[i]->m_pid > 0 && pidAlive (m_inst[i]->m_pid)) any = true;
		if (!any || ticks () - t0 > 100) break;
		kapi_msleep (20);
	}
	for (int i = 0; i < m_inst.size (); i++)
	{
		PlugInstance *p = m_inst[i];
		if (p->m_pid > 0 && pidAlive (p->m_pid)) kapi_kill_pid (p->m_pid, 1);
		if (p->m_edSid) kapi_surface_destroy (p->m_edSid);
		delete p;
	}
	m_inst.clear ();
	m_tracks.clear ();
	m_genCache.clear ();
	// the engine's sides: once it rendered two blocks since (it has let them go), or 0.2 s went by
	// without a block (it is not rendering: stopped, as it must be by now)
	if (m_engine) { unsigned r = m_engine->renders; for (int k = 0; k < 20 && m_engine->renders - r < 2; k++) kapi_msleep (10); }
	for (int i = 0; i < m_retired.size (); i++) { delete m_retired[i].src; delete m_retired[i].fx; }
	m_retired.clear ();
	for (int i = 0; i < m_regions.size (); i++) kapi_surface_destroy (m_regions[i].sid);	// (no one maps them now)
	m_regions.clear ();
	m_ok = false;
}

} // namespace kt
