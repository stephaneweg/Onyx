//
// tgunit_test.cpp -- the Telegram app's core without the network: the schema read, TL encoded and decoded
// back (flags, vectors, bare types, gzip_packed), AES-256-IGE against its known vector, the factoring of
// MTProto's documented pq, the RSA keys' fingerprints, the random generator.
//
// MIT licence.
//
#include "tgplat_host.h"
#include "client.h"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (c) printf ("  ok   " __VA_ARGS__); else { fails++; printf ("  FAIL " __VA_ARGS__); } printf ("\n"); } while (0)

static void hex (const char *h, unsigned char *o) { for (int i = 0; h[2 * i]; i++) { unsigned x; sscanf (h + 2 * i, "%2x", &x); o[i] = (unsigned char) x; } }

int main ()
{
	tg_quiet = true;
	CHECK (tl::load (), "the schema read: %d combinators, layer %d", tl::schema ().n, tl::schema ().layer);
	CHECK (tl::by_id (0xbe7e8ef1) && !strcmp (tl::by_id (0xbe7e8ef1)->name, "req_pq_multi"), "req_pq_multi by its id");
	CHECK (tl::by_name ("messages.sendMessage") && tl::by_name ("messages.sendMessage")->fn, "messages.sendMessage, a function");

	// a request with flags, a vector, a nested object
	tl::Arena a;
	tl::Val *q = tl::make (a, "messages.sendMessage");
	q->set ("peer", tl::obj (a, "inputPeerSelf"));
	q->set ("message", tl::S (a, "Hello \xe2\x98\xba"));
	q->set ("random_id", tl::L (0x1122334455667788ll));
	q->set ("silent", tl::T ());
	tl::Buf b;
	CHECK (tl::encode (b, *q), "encoded: %d bytes", b.n);
	CHECK (b.n % 4 == 0 && b.d[0] == 0x62 && b.d[3] == 0xfe, "its id first (fef48f62)");
	unsigned flags = b.d[4] | (b.d[5] << 8) | (b.d[6] << 16) | ((unsigned) b.d[7] << 24);
	CHECK (flags == (1u << 5), "the flags made from the fields: silent only (%x)", flags);
	tl::Val back;
	CHECK (tl::decode (a, b.d, b.n, back) && back.is ("messages.sendMessage"), "decoded back");
	CHECK (!strcmp (back["message"].str (), "Hello \xe2\x98\xba") && back["random_id"].i () == 0x1122334455667788ll && back["silent"].b () && !back["noforwards"].b (), "its fields");
	CHECK (back["peer"].is ("inputPeerSelf") && !back["reply_to"].ok (), "the nested object, an absent field");

	// a long string (254 bytes and more), a vector of longs
	char longs[600]; memset (longs, 'x', sizeof longs); longs[599] = 0;
	tl::Val *ack = tl::make (a, "msgs_ack");
	tl::Val ids = tl::Vec (a, 3); ids.v[0] = tl::L (1); ids.v[1] = tl::L (-2); ids.v[2] = tl::L (1ll << 40);
	ack->set ("msg_ids", ids);
	tl::Buf b2; tl::encode (b2, *ack);
	tl::Val back2;
	CHECK (tl::decode (a, b2.d, b2.n, back2) && back2["msg_ids"].count () == 3 && back2["msg_ids"][1].i () == -2 && back2["msg_ids"][2].i () == (1ll << 40), "Vector<long>");
	tl::Val *err = tl::make (a, "error");
	err->set ("code", tl::I (-7)); err->set ("text", tl::S (a, longs));
	tl::Buf b3; tl::encode (b3, *err);
	tl::Val back3;
	CHECK (tl::decode (a, b3.d, b3.n, back3) && back3["text"].len () == 599 && back3["code"].i () == -7, "a 599-byte string, a negative int");

	// gzip_packed around an object
	{
		unsigned char gz[1024]; z_stream z; memset (&z, 0, sizeof z);
		deflateInit2 (&z, 6, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
		z.next_in = b3.d; z.avail_in = (unsigned) b3.n; z.next_out = gz; z.avail_out = sizeof gz;
		deflate (&z, Z_FINISH); int gn = (int) (sizeof gz - z.avail_out); deflateEnd (&z);
		tl::Buf p; p.u32 (0x3072cfa1u); p.str (gz, gn);
		tl::Val back4;
		CHECK (tl::decode (a, p.d, p.n, back4) && back4.is ("error") && back4["text"].len () == 599, "gzip_packed unpacked");
	}
	// a bare vector of bare objects (future_salts)
	{
		tl::Buf p; p.u32 (0xae500895u); p.i64 (99); p.u32 (1234); p.u32 (2);
		for (int i = 0; i < 2; i++) { p.u32 (10 + i); p.u32 (20 + i); p.i64 (777 + i); }
		tl::Val fs;
		CHECK (tl::decode (a, p.d, p.n, fs) && fs["salts"].count () == 2 && fs["salts"][1]["salt"].i () == 778, "vector<future_salt>: bare");
	}

	// AES-IGE: the known answer (OpenSSL's test vector: AES-128), and AES-256 both ways
	{
		unsigned char key[32], iv[32], pt[32] = { 0 }, ct[32], want[32], dec[32];
		for (int i = 0; i < 32; i++) { key[i] = (unsigned char) i; iv[i] = (unsigned char) i; }
		hex ("1a8519a6557be652e9da8e43da4ef4453cf456b4ca488aa383c79c98b34797cb", want);
		tgc::aes_ige (true, key, iv, pt, ct, 32, 128);
		CHECK (!memcmp (ct, want, 32), "AES-IGE encrypts as the vector says");
		tgc::aes_ige (false, key, iv, ct, dec, 32, 128);
		CHECK (!memcmp (dec, pt, 32), "... and decrypts back");
		tgc::aes_ige (true, key, iv, want, ct, 32);
		tgc::aes_ige (false, key, iv, ct, dec, 32);
		CHECK (!memcmp (dec, want, 32) && memcmp (ct, want, 32), "AES-256-IGE: there and back");
	}
	// pq: MTProto's documented example
	{
		uint64_t p = 0, qq = 0;
		CHECK (tgc::factor (0x17ED48941A08F981ull, &p, &qq) && p == 0x494C553Bull && qq == 0x53911073ull, "pq 17ED48941A08F981 = 494C553B x 53911073");
		CHECK (tgc::factor (1000000016000000063ull, &p, &qq) && p == 1000000007ull && qq == 1000000009ull, "a 60-bit pq = 1000000007 x 1000000009");
	}
	{
		int n = 0;
		tgc::RsaKey *k = tgc::rsa_keys (&n);
		CHECK (n == 2 && (unsigned long long) k[0].fp == 0xd09d1d85de64fd85ull && (unsigned long long) k[1].fp == 0xb25898df208d2603ull, "the RSA keys' fingerprints");
	}
	{
		tgc::rng_init ();
		unsigned char x[32], y[32];
		tgc::random (x, 32); tgc::random (y, 32);
		CHECK (memcmp (x, y, 32) != 0, "the generator gives different bytes");
	}
	CHECK (tg::pid (tg::pkey (tg::P_CHANNEL, 1234567890123ll)) == 1234567890123ll && tg::ptype (tg::pkey (tg::P_CHANNEL, 5)) == tg::P_CHANNEL, "peer keys");

	printf ("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
