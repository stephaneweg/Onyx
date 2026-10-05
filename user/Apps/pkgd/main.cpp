//
// pkgd -- the update daemon (docs/pkg/README.md): no window; started by etc/autostart. Once the network
// and the time (NTP) are there, then once a day: the repository's index read again (its signature
// checked), the packages set "auto" updated (pkg/pkglib.h, as `pkg upgrade`; an app running is left
// for the next round, the system's update staged for the next boot), and a notification -- the
// updates installed, those waiting for you (a click opens the Package Manager on its Updates).
//
//   pkgd            the daemon (one at a time: the IPC service "pkgd")
//   pkgd --once     one round now, then it ends (the check of the day is not counted)
//
// SD:/etc/pkg/pkg.ini: check = daily (the default) or never (the daemon then only ends). The day of
// the last round: SD:/var/pkg/lastcheck (yyyymmdd).
//
#include "appkit/appkit.h"
#include "notify.h"
#include "pkg/pkglib.h"

using namespace pkg;

struct Quiet : Report {};			// (pkgd says nothing but its notification)

static int today (void)
{
	int y = 0, mo = 0, d = 0;
	kapi_get_datetime (&y, &mo, &d, 0, 0, 0);
	return y * 10000 + mo * 100 + d;
}
static bool time_set (void) { return today () / 10000 >= 2025; }	// (NTP has answered)

static void round (bool count_day)
{
	static Manager *mp; if (!mp) mp = new Manager; else mp->db.load ();
	Manager &m = *mp;
	Quiet q;
	if (m.refresh (q) != OK || !m.verified) return;		// no index, or not checked: next time
	static bool synced; if (!synced) { synced = true; m.sync_meta (); }	// (fileassoc.ini, runners.ini: what the packages installed open and run)
	if (count_day) { char t[16]; snprintf (t, sizeof t, "%d\n", today ()); write_file (PKG_VAR "/lastcheck", t, strlen (t)); }
	char done[300] = "", wait[300] = ""; int nd = 0, nw = 0; bool staged = false;
	for (int i = 0; i < m.db.n; i++)
	{
		Inst &in = *m.db.v[i];
		const Pkg *u = m.update_for (in);
		if (!u || m.staged (in.name) || eq (in.mode (), "never")) continue;
		const char *title = in.ini.get ("package", "title", in.name);
		bool kapiOk = true;
		Need nd2[16]; int nn = parse_needs (u->needs, nd2, 16);
		for (int k = 0; k < nn; k++) if (eq (nd2[k].name, "kapi") && kapi_level () < atoi (nd2[k].ver)) kapiOk = false;
		if (eq (in.mode (), "auto") && kapiOk)
		{
			const Pkg *list[16]; int err = 0, n = 0;
			// its new needs first
			for (int k = 0; k < nn; k++)
			{
				if (eq (nd2[k].name, "kapi")) continue;
				Inst *have = m.db.find (nd2[k].name);
				if (have && vcmp (have->version (), nd2[k].ver) >= 0) continue;
				n += m.resolve (nd2[k].name, list + n, 16 - n, &err, q);
			}
			bool ok = true;
			for (int k = 0; k < n && ok; k++) ok = m.install (*list[k], q) == OK;
			int rc = ok ? m.install (*u, q) : E_NEEDS;
			if (rc == OK)
			{
				if (u->restart) staged = true;
				if (nd++ < 4) { if (done[0]) strncat (done, ", ", sizeof done - strlen (done) - 1); strncat (done, title, sizeof done - strlen (done) - 1); }
				continue;
			}
		}
		if (nw++ < 4) { if (wait[0]) strncat (wait, ", ", sizeof wait - strlen (wait) - 1); strncat (wait, title, sizeof wait - strlen (wait) - 1); }
	}
	m.refresh_desktop ();				// (an app updated: the dock started again)
	if (!nd && !nw) return;
	char text[480] = "";
	if (nw) snprintf (text, sizeof text, "%s%s", wait, nw > 4 ? "..." : "");
	if (nd)
	{
		char t[360]; snprintf (t, sizeof t, "%sUpdated: %s%s%s", nw ? ". " : "", done, nd > 4 ? "..." : "", staged ? " (the system at the next restart)" : "");
		strncat (text, t, sizeof text - strlen (text) - 1);
	}
	char title[64];
	if (nw) snprintf (title, sizeof title, "%d update%s available", nw, nw == 1 ? "" : "s");
	else snprintf (title, sizeof title, "%d package%s updated", nd, nd == 1 ? "" : "s");
	notify_action (title, text, "control pkgman");
}

int main (void)
{
	char args[64]; int an = kapi_get_args (args, sizeof args); args[an > 0 && an < 64 ? an : 0] = 0;
	bool once = strstr (args, "--once") != 0;
	if (!once && !kapi_ipc_register ("pkgd")) return 0;	// another pkgd is running
	Ini c; c.load (PKG_CONF);
	if (!once && eq (c.get ("", "check", "daily"), "never")) return 0;
	for (;;)
	{
		if (kapi_net_status (0, 0) && time_set ())
		{
			char *t = read_file (PKG_VAR "/lastcheck");
			int was = t ? atoi (t) : 0; free (t);
			if (once || was != today ()) round (!once);
			if (once) return 0;
		}
		else if (once) { kapi_msleep (2000); continue; }
		kapi_msleep (15 * 60 * 1000);			// a look every quarter of an hour (the day changes)
	}
}
