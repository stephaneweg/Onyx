// The authorization key with a test server, then help.getConfig -- the MTProto layer alone.
#include "tgplat_host.h"
#include <unistd.h>
struct L : mt::Listener {
	int done = 0; tl::Arena keep;
	void onResult (int req, const tl::Val &v) override { printf ("result %d: %s this_dc=%lld test=%d dc_options=%d\n", req, v.name (), v["this_dc"].i (), (int) v["test_mode"].b (), v["dc_options"].count ()); done = 1; }
	void onError (int req, int code, const char *m) override { printf ("error %d: %d %s\n", req, code, m); done = 2; }
	void onUpdates (const tl::Val &v) override { printf ("update: %s\n", v.name ()); }
	void onKeyReady () override { printf ("key ready\n"); }
};
int main (int argc, char **argv)
{
	if (!tl::load ()) { printf ("schema!\n"); return 1; }
	printf ("schema: %d combinators, layer %d\n", tl::schema ().n, tl::schema ().layer);
	tgc::rng_init ();
	mt::Session s; L l; s.L = &l;
	s.dc = 2; s.test = argc < 2; strcpy (s.host, argc < 2 ? "149.154.167.40" : "149.154.167.50");
	s.init.api_id = atoi (getenv ("TG_API_ID") ? getenv ("TG_API_ID") : "0");
	s.start (tg_new_transport ());
	tl::Arena a;
	s.call (*tl::make (a, "help.getConfig"));
	for (int i = 0; i < 400 && !l.done; i++) { s.poll (); if (s.broken ()) { printf ("broken\n"); return 1; } usleep (20000); }
	return l.done == 1 ? 0 : 1;
}
