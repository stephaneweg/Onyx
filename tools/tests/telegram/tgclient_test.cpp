//
// tgclient_test.cpp -- the Telegram app's client (user/Apps/telegram/client.h) against Telegram's TEST
// servers, end to end, over MTProto's HTTP transport (tgplat_host.h): two new accounts (the test
// servers' numbers 99966 X YYYY, X the data centre, whose code is X five times -- no SMS), A on data
// centre 2 and B on data centre 1 (B's sign-in starts on 2: PHONE_MIGRATE_1), then:
//
//   sign-up of both, the conversations' list, A finds B (contacts.importContacts), A -> B and B -> A
//   messages pushed as updates, typing, the history, the read receipts, B's profile photo (uploaded on
//   data centre 1) downloaded by A (exported authorization), a cloud password set on A then a sign-in
//   with it (SRP), the session file read back (signed in again without a code).
//
//   sh tools/tests/telegram/run_tgclient_test.sh		(needs the network; TG_API_ID / TG_API_HASH)
//
// The test servers refuse their own sign-in codes since late 2024 (PHONE_CODE_INVALID, tdlib/td#3083), so
// by default only what comes before the sign-in is tried live (the keys, the code sent, a wrong code, the
// migration, a production data centre); TG_TEST_LOGIN=1 tries the rest, for the day they accept it again.
//
// MIT licence.
//
#include "tgplat_host.h"
#include "client.h"
#include <unistd.h>
#include <sys/stat.h>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (c) printf ("  ok   " __VA_ARGS__); else { fails++; printf ("  FAIL " __VA_ARGS__); } printf ("\n"); } while (0)

static tg::Client *all[4];
static int nall = 0;

static void spin (int ms)
{
	long long end = tg_unix_ms () + ms;
	while (tg_unix_ms () < end) { for (int i = 0; i < nall; i++) all[i]->tick (); usleep (10000); }
}
template <class F> static bool waitFor (F cond, int ms)
{
	long long end = tg_unix_ms () + ms;
	while (tg_unix_ms () < end) { for (int i = 0; i < nall; i++) all[i]->tick (); if (cond ()) return true; usleep (10000); }
	return false;
}

struct Other { int req; bool done; int code; char err[96]; tl::Arena a; tl::Val v; };
static void otherCb (void *ctx, int req, const tl::Val &v, int code, const char *err)
{
	Other *o = (Other *) ctx;
	if (req != o->req) return;
	o->done = true; o->code = code;
	snprintf (o->err, sizeof o->err, "%s", err ? err : "");
	o->v = tl::copy (o->a, v);
}
static bool invoke (tg::Client &c, Other &o, const tl::Val &q, int dc = 0)
{
	o.done = false; o.code = 0; o.err[0] = 0; o.a.clear ();
	c.otherFn = otherCb; c.otherCtx = &o;
	o.req = c.invoke (q, dc);
	return o.req && waitFor ([&] { return o.done; }, 60000) && !o.code;
}

static tg::Client *newClient (const char *dir, int apiId, const char *hash, bool test = true)
{
	mkdir (dir, 0755);
	char cache[128]; snprintf (cache, sizeof cache, "%scache", dir); mkdir (cache, 0755);
	tg::Client *c = new tg::Client;
	snprintf (c->dir, sizeof c->dir, "%s", dir);
	c->apiId = apiId; snprintf (c->apiHash, sizeof c->apiHash, "%s", hash); c->test = test;
	all[nall++] = c;
	c->begin ();
	return c;
}

static bool signInFresh (tg::Client &c, const char *phone, const char *code, const char *first, const char *password = 0)
{
	c.sendCode (phone);
	if (!waitFor ([&] { return c.state == tg::AS_CODE || c.error[0]; }, 60000)) { printf ("  (no code state)\n"); return false; }
	if (c.error[0]) { printf ("  (sendCode: %s)\n", c.error); return false; }
	char full[16]; int k = 0;
	for (; k < c.codeLength && k < 15; k++) full[k] = code[0];	// (the test servers' code: the data centre's digit, as many as asked)
	full[k] = 0;
	printf ("  (the code: %s, by %s)\n", full, c.codeType);
	c.signIn (k ? full : code);
	if (!waitFor ([&] { return c.state == tg::AS_READY || c.state == tg::AS_SIGNUP || c.state == tg::AS_PASSWORD || (c.error[0] && !c.busy); }, 60000)) return false;
	if (c.state == tg::AS_SIGNUP) { c.signUp (first, "Test"); waitFor ([&] { return c.state == tg::AS_READY || (c.error[0] && !c.busy); }, 60000); }
	if (c.state == tg::AS_PASSWORD && password) { c.checkPassword (password); waitFor ([&] { return c.state == tg::AS_READY || (c.error[0] && !c.busy); }, 90000); }
	if (c.error[0]) printf ("  (sign-in: %s)\n", c.error);
	return c.state == tg::AS_READY;
}

int main ()
{
	setvbuf (stdout, 0, _IONBF, 0);
	const char *ids = getenv ("TG_API_ID"), *hash = getenv ("TG_API_HASH");
	if (!ids || !hash) { printf ("TG_API_ID / TG_API_HASH not set: skipped\n"); return 0; }
	int apiId = atoi (ids);
	tg_quiet = getenv ("TG_VERBOSE") == 0;
	srand ((unsigned) time (0) ^ (unsigned) getpid ());
	char phoneA[16], phoneB[16];
	snprintf (phoneA, sizeof phoneA, "999662%04d", rand () % 10000);
	snprintf (phoneB, sizeof phoneB, "999661%04d", rand () % 10000);
	if (system ("rm -rf /tmp/tgA /tmp/tgB /tmp/tgA2")) {}
	printf ("Telegram client against the test servers: A %s (dc 2), B %s (dc 1)\n", phoneA, phoneB);

	tg::Client &A = *newClient ("/tmp/tgA/", apiId, hash);
	tg::Client &B = *newClient ("/tmp/tgB/", apiId, hash);
	CHECK (A.state == tg::AS_PHONE && B.state == tg::AS_PHONE, "no session yet: the phone number is asked");

	// What the test servers still allow (their sign-in codes are refused since late 2024 -- PHONE_CODE_INVALID
	// for every client, tdlib/td#3083): the code sent, a wrong code refused, the migration to the number's
	// data centre; then a production data centre refusing a number that cannot exist.
	A.sendCode (phoneA);
	CHECK (waitFor ([&] { return A.state == tg::AS_CODE || A.error[0]; }, 60000) && A.state == tg::AS_CODE, "A: the code sent (by %s, %d digits)", A.codeType, A.codeLength);
	A.signIn ("00000");
	CHECK (waitFor ([&] { return !A.busy; }, 60000) && !strcmp (A.error, "PHONE_CODE_INVALID") && A.state == tg::AS_CODE, "a wrong code refused, the code asked again (%s)", A.error);
	B.sendCode (phoneB);
	CHECK (waitFor ([&] { return B.state == tg::AS_CODE || B.error[0]; }, 60000) && B.state == tg::AS_CODE && B.homeDc () == 1, "B: PHONE_MIGRATE_1, the code sent from data centre 1 (%d)", B.homeDc ());
	{
		if (system ("rm -rf /tmp/tgP")) {}
		tg::Client &P = *newClient ("/tmp/tgP/", apiId, hash, false);
		CHECK (P.state == tg::AS_PHONE, "production: the phone number asked");
		P.sendCode ("+1000");
		CHECK (waitFor ([&] { return P.error[0] && !P.busy; }, 60000) && P.state == tg::AS_PHONE, "production: an impossible number refused (%s)", P.error);
	}
	if (!getenv ("TG_TEST_LOGIN"))
	{
		printf ("(TG_TEST_LOGIN unset: the sign-in and what follows it are not tried -- tgmodel_test.cpp covers them offline)\n");
		printf ("%d checks, %d failed\n", checks, fails);
		return fails ? 1 : 0;
	}
	A.backToPhone (); B.backToPhone ();

	CHECK (signInFresh (A, phoneA, "22222", "Alice"), "A signed up on data centre 2");
	CHECK (signInFresh (B, phoneB, "11111", "Bob"), "B signed up (PHONE_MIGRATE to data centre 1)");
	CHECK (B.homeDc () == 1, "B's home is data centre 1 (%d)", B.homeDc ());
	CHECK (A.selfId && B.selfId && A.self () && B.self (), "both know themselves (%lld, %lld)", A.selfId, B.selfId);
	CHECK (waitFor ([&] { return A.dialogsLoaded && B.dialogsLoaded && A.contactsLoaded; }, 60000), "the conversations and the contacts loaded");

	// A finds B by the phone number
	Other o;
	{
		tl::Arena a;
		tl::Val *pc = tl::make (a, "inputPhoneContact");
		pc->set ("client_id", tl::L (1)); pc->set ("phone", tl::S (a, phoneB)); pc->set ("first_name", tl::S (a, "Bob")); pc->set ("last_name", tl::S (a, ""));
		tl::Val v = tl::Vec (a, 1); v.v[0] = *pc;
		tl::Val *q = tl::make (a, "contacts.importContacts");
		q->set ("contacts", v);
		CHECK (invoke (A, o, *q), "contacts.importContacts (%s)", o.err);
		A.addUsers (o.v["users"]);
	}
	long long peerB = tg::pkey (tg::P_USER, B.selfId), peerA = tg::pkey (tg::P_USER, A.selfId);
	CHECK (A.user (B.selfId) && A.user (B.selfId)->access, "A knows B and its access hash");

	A.send (peerB, "Hello Bob, it is Alice! \xc3\xa9t\xc3\xa9 \xf0\x9f\x98\x80");
	CHECK (waitFor ([&] { tg::Conv *c = A.conv (peerB); return c && c->n && !c->m[c->n - 1].pending; }, 60000), "A's message sent (the server gave its id)");
	CHECK (waitFor ([&] { tg::Conv *c = B.conv (peerA); return c && c->n; }, 60000), "B got A's message (pushed)");
	{
		tg::Conv *c = B.conv (peerA);
		CHECK (c && c->n && !strcmp (c->m[c->n - 1].text, "Hello Bob, it is Alice! \xc3\xa9t\xc3\xa9 \xf0\x9f\x98\x80"), "its text, UTF-8 and emoji whole");
		CHECK (c && c->unread == 1 && B.ninbox == 1, "one unread, one notification (%d, %d)", c ? c->unread : -1, B.ninbox);
		CHECK (B.user (A.selfId) != 0, "B knows A now");
	}

	B.typing (peerA);
	CHECK (waitFor ([&] { tg::Conv *c = A.conv (peerB); return c && c->typingUser == peerB; }, 30000), "A sees B typing");

	B.open (peerA);
	CHECK (waitFor ([&] { tg::Conv *c = B.conv (peerA); return c && c->loaded && !c->loading; }, 60000), "B opened the conversation: its history");
	CHECK (waitFor ([&] { tg::Conv *c = A.conv (peerB); return c && c->n && c->readOutMax >= c->m[c->n - 1].id; }, 30000), "A sees its message read");

	B.send (peerA, "Hi Alice");
	CHECK (waitFor ([&] { tg::Conv *c = A.conv (peerB); return c && c->n >= 2 && !strcmp (c->m[c->n - 1].text, "Hi Alice") && !c->m[c->n - 1].out; }, 60000), "A got B's answer");
	A.dialogs ();
	CHECK (A.norder >= 1 && A.order[0] == peerB, "B's conversation is at the top of A's list");

	// B's profile photo (on data centre 1), A downloads it from there
	{
		int n = 0;
		unsigned char *jpg = tg_load ("tools/tests/telegram/avatar.jpg", &n);
		CHECK (jpg && n > 0, "the test photo");
		tl::Arena a;
		long long fid = tgc::random64 ();
		tl::Val *sp = tl::make (a, "upload.saveFilePart");
		sp->set ("file_id", tl::L (fid)); sp->set ("file_part", tl::I (0)); sp->set ("bytes", tl::S (a, jpg, n));
		CHECK (invoke (B, o, *sp), "B: upload.saveFilePart (%s)", o.err);
		tl::Val *f = tl::make (a, "inputFile");
		f->set ("id", tl::L (fid)); f->set ("parts", tl::I (1)); f->set ("name", tl::S (a, "avatar.jpg")); f->set ("md5_checksum", tl::S (a, ""));
		tl::Val *up = tl::make (a, "photos.uploadProfilePhoto");
		up->set ("file", *f);
		CHECK (invoke (B, o, *up), "B: photos.uploadProfilePhoto (%s)", o.err);
		free (jpg);
		// A sees B again (users.getUsers): the photo's id and data centre
		tl::Val *gu = tl::make (a, "users.getUsers");
		tl::Val ids = tl::Vec (a, 1);
		tl::Val *iu = tl::make (a, "inputUser");
		iu->set ("user_id", tl::L (B.selfId)); iu->set ("access_hash", tl::L (A.user (B.selfId)->access));
		ids.v[0] = *iu;
		gu->set ("id", ids);
		CHECK (invoke (A, o, *gu), "A: users.getUsers (%s)", o.err);
		A.addUsers (o.v);
		tg::User *ub = A.user (B.selfId);
		CHECK (ub && ub->photoId && ub->photoDc == 1, "B's photo is on data centre 1 (%d)", ub ? ub->photoDc : -1);
		char path[160];
		A.photo (peerB, path, sizeof path);
		CHECK (waitFor ([&] { char p[160]; return A.photo (peerB, p, sizeof p); }, 90000), "A downloaded it from data centre 1 (exported authorization)");
		int m = 0;
		unsigned char *got = tg_load (path, &m);
		CHECK (got && m > 100 && got[0] == 0xff && got[1] == 0xd8, "a JPEG, %d bytes", m);
		free (got);
	}

	// a cloud password on A, then a new sign-in with it
	{
		tl::Arena a;
		CHECK (invoke (A, o, *tl::make (a, "account.getPassword")), "A: account.getPassword (%s)", o.err);
		const tl::Val &na = o.v["new_algo"];
		unsigned char salt1[64], p[256], v[256];
		int s1 = na["salt1"].len ();
		memcpy (salt1, na["salt1"].s, (size_t) s1);
		tgc::random (salt1 + s1, 32); s1 += 32;
		memcpy (p, na["p"].s, 256);
		int g = (int) na["g"].i ();
		CHECK (tgc::srp_verifier ("onyx secret", salt1, s1, (const unsigned char *) na["salt2"].s, na["salt2"].len (), g, p, v), "the verifier");
		tl::Val *algo = tl::make (a, "passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow");
		algo->set ("salt1", tl::S (a, salt1, s1)); algo->set ("salt2", tl::S (a, na["salt2"].s, na["salt2"].len ()));
		algo->set ("g", tl::I (g)); algo->set ("p", tl::S (a, p, 256));
		tl::Val *ns = tl::make (a, "account.passwordInputSettings");
		ns->set ("new_algo", *algo); ns->set ("new_password_hash", tl::S (a, v, 256)); ns->set ("hint", tl::S (a, "onyx"));
		tl::Val *q = tl::make (a, "account.updatePasswordSettings");
		q->set ("password", tl::obj (a, "inputCheckPasswordEmpty"));
		q->set ("new_settings", *ns);
		bool set = invoke (A, o, *q);
		CHECK (set || !strcmp (o.err, "EMAIL_UNCONFIRMED"), "A: a cloud password set (%s)", o.err);
	}
	tg::Client &A2 = *newClient ("/tmp/tgA2/", apiId, hash);
	CHECK (signInFresh (A2, phoneA, "22222", "Alice", "onyx secret"), "A signed in again elsewhere with its password (SRP)");
	CHECK (!strcmp (A2.passwordHint, "onyx"), "the password's hint (%s)", A2.passwordHint);

	// the session read back
	A.end ();
	tg::Client &A3 = *newClient ("/tmp/tgA/", apiId, hash);
	CHECK (A3.state == tg::AS_READY, "A's session read back: signed in without a code");
	CHECK (waitFor ([&] { return A3.dialogsLoaded; }, 60000) && A3.conv (peerB) && A3.conv (peerB)->n, "its conversations again");

	printf ("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
