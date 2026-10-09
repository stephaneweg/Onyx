//
// pkg/pkgjob.h -- a package job run in a thread, the program's window alive: the index read again (and the packages'
// icons fetched), packages installed (their needs resolved; the apps first, the system's last), removed, their updates
// mode set. What it says (the package at work, its percentage, the message, the ones that failed or were staged) is in
// g_job, read by the window each frame; g_job.done at its end (then kapi_thread_join). The package manager (pkgman)
// and the console's Packages page share it. Include after pkg/pkglib.h; one translation unit.
//
//   g_jobM = &manager; job_start (J_INSTALL, names, n);   ... each frame: g_job.cur, g_job.pct ... g_job.done
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _pkg_pkgjob_h
#define _pkg_pkgjob_h

namespace pkg {
static Manager *g_jobM;					// the manager the job works with (set before job_start)

enum { J_CHECK, J_INSTALL, J_REMOVE, J_MODE };
struct Job
{
	int kind; char names[64][40]; int n; char mode[8];
	volatile bool running, done, cancel;
	int tid;
	// what it says (the window reads)
	char cur[40]; volatile int pct; char msg[200]; volatile int rc;
	char failed[64][40]; int nfailed;
	char staged[64][40]; int nstaged;
	char finished[96][40]; volatile int nfinished;	// done, in their order
};
static Job g_job;
struct JobReport : Report
{
	void say (const char *s) override
	{
		cpy (g_job.msg, s, sizeof g_job.msg);
		const char *st = strstr (s, "staged");
		if (st) { char nm[40]; int k = 0; while (s[k] && s[k] != ' ' && k < 39) { nm[k] = s[k]; k++; } nm[k] = 0; if (g_job.nstaged < 64) cpy (g_job.staged[g_job.nstaged++], nm, 40); }
	}
	void step (const char *what, u64 done, u64 total) override
	{
		(void) what;
		g_job.pct = total ? (int) (done * 100 / total) : 0;
		if (g_job.pct > 100) g_job.pct = 100;
	}
	bool cancelled () override { return g_job.cancel; }
};

static void fetch_icons (Report &r)
{
	kapi_mkdir (PKG_VAR "/icons");
	for (int i = 0; i < g_jobM->index.n && !g_job.cancel; i++)
	{
		const Pkg &p = g_jobM->index.p[i];
		if (!p.icon[0]) continue;
		char path[200]; snprintf (path, sizeof path, PKG_VAR "/icons/%s.bmp", p.name);
		char app[200]; snprintf (app, sizeof app, "SD:/apps/%s.app/icon.bmp", p.name);
		if (exists (path) || exists (app)) continue;
		u64 n = 0; char *b = g_jobM->fetch (p.icon, &n, 0, r);
		if (b) { write_file (path, b, n); free (b); }
	}
}

static int job_thread (void *)
{
	JobReport r;
	Manager &m = *g_jobM;
	int rc = OK;
	if (g_job.kind == J_CHECK)
	{
		cpy (g_job.cur, "index", sizeof g_job.cur);
		rc = m.refresh (r);
		if (m.haveIndex) fetch_icons (r);
	}
	else if (g_job.kind == J_INSTALL)
	{
		const Pkg *list[96]; int n = 0;
		for (int i = 0; i < g_job.n && !g_job.cancel; i++)
		{
			Inst *in = m.db.find (g_job.names[i]);
			const Pkg *p = m.index.find (g_job.names[i]);
			if (!p) continue;
			if (in) { list[n++] = p; continue; }
			int err = 0; const Pkg *sub[32];
			int k = m.resolve (g_job.names[i], sub, 32, &err, r);
			for (int j = 0; j < k && n < 96; j++) { bool dup = false; for (int q = 0; q < n; q++) if (list[q] == sub[j]) dup = true; if (!dup) list[n++] = sub[j]; }
			if (!k) { rc = err; if (g_job.nfailed < 64) cpy (g_job.failed[g_job.nfailed++], g_job.names[i], 40); }
		}
		for (int i = 1; i < n; i++)			// the apps first, the system's packages last
			for (int j = i; j > 0 && list[j - 1]->restart && !list[j]->restart; j--) { const Pkg *t = list[j]; list[j] = list[j - 1]; list[j - 1] = t; }
		for (int i = 0; i < n && !g_job.cancel; i++)
		{
			cpy (g_job.cur, list[i]->name, sizeof g_job.cur); g_job.pct = 0;
			int x = m.install (*list[i], r);
			if (x != OK) { rc = x; if (g_job.nfailed < 64) cpy (g_job.failed[g_job.nfailed++], list[i]->name, 40); }
			if (g_job.nfinished < 96) { cpy (g_job.finished[g_job.nfinished], list[i]->name, 40); g_job.nfinished++; }
		}
		if (g_job.cancel && rc == OK) rc = E_CANCEL;
	}
	else if (g_job.kind == J_REMOVE)
	{
		for (int i = 0; i < g_job.n && !g_job.cancel; i++)		// (the ticked ones: each in turn)
		{
			cpy (g_job.cur, g_job.names[i], sizeof g_job.cur);
			int x = m.remove (g_job.names[i], false, r);
			if (x != OK) rc = x;
		}
	}
	else if (g_job.kind == J_MODE)
	{
		if (!m.set_mode (g_job.names[0], g_job.mode)) rc = E_NOTINST;
	}
	m.refresh_desktop ();				// (an app installed or removed: the dock started again)
	g_job.rc = rc;
	g_job.cur[0] = 0;
	g_job.done = true;
	return 0;
}
// A job started -> false: one is running, or no thread.
static bool job_start (int kind, const char *const *names, int n, const char *mode = 0)
{
	if (g_job.running) return false;
	memset (&g_job, 0, sizeof g_job);
	g_job.kind = kind; g_job.running = true;
	for (int i = 0; i < n && i < 64; i++) cpy (g_job.names[g_job.n++], names[i], 40);
	if (mode) cpy (g_job.mode, mode, sizeof g_job.mode);
	g_job.tid = kapi_thread_create (job_thread, 0, 0, "pkgjob");
	if (g_job.tid < 0) { g_job.running = false; return false; }
	return true;
}
} // namespace pkg

#endif
