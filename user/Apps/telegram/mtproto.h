//
// mtproto.h -- one MTProto 2.0 session with one of Telegram's data centres: the authorization key
// made with the server (the Diffie-Hellman exchange: req_pq_multi, req_DH_params, set_client_DH_params),
// then the encrypted messages both ways (msg_key, AES-256-IGE), the msg_ids and seq_nos, the server
// salt, the acknowledgements, the containers, rpc_result / rpc_error, the service messages
// (bad_server_salt, bad_msg_notification, new_session_created...), the pushed updates.
//
// Non-blocking: poll () each tick reads what the transport has and sends what waits. A request
// (call ()) waits in the queue until the key is there; its answer comes to the Listener with the id
// call () gave. The first request on a connection is wrapped in invokeWithLayer (initConnection
// (...)). The transport is the platform's (tgplat.h): TCP's "intermediate" framing on Onyx, HTTP
// POSTs in the PC's tests.
//
// MIT licence.
//
#ifndef TG_MTPROTO_H
#define TG_MTPROTO_H

#include <stdio.h>
#include "tl.h"
#include "tgcrypto.h"

// From the platform (tgplat.h)
long long tg_unix_ms ();				// the wall clock, UTC, in ms
void tg_log (const char *fmt, ...);

namespace mt {

// A connection to a data centre, packets in and out (the framing is the transport's).
class Transport
{
public:
	virtual ~Transport () {}
	virtual bool open (const char *host, int port) = 0;
	virtual bool send (const unsigned char *p, int n) = 0;
	// one packet -> 1 (in out), 0 nothing yet, -1 the connection is gone; a 4-byte packet is the
	// server's error code (a negative int: -404 no such key, -429 too many connections...).
	virtual int recv (tl::Buf &out) = 0;
	virtual void close () = 0;
	virtual bool http () const { return false; }		// (HTTP: the answers come to requests)
};

// What the app is told.
class Listener
{
public:
	virtual ~Listener () {}
	virtual void onResult (int req, const tl::Val &result) = 0;
	virtual void onError (int req, int code, const char *message) = 0;
	virtual void onUpdates (const tl::Val &updates) = 0;	// (pushed: Updates, updateShort*...)
	virtual void onKeyReady () {}				// the authorization key was made
	virtual void onNewSession () {}				// (updates may have been missed: getDifference)
};

struct InitInfo					// initConnection's fields
{
	int api_id;
	const char *device, *system, *app, *lang;
};

enum { MAXREQ = 128 };

class Session
{
public:
	int dc;
	bool test;				// Telegram's test servers (their key, dc + 10000 in p_q_inner_data_dc)
	char host[64];
	int port;
	unsigned char key[256];
	long long keyId;
	bool haveKey;
	long long salt;
	long long timeOffset;			// (server - us), ms
	Listener *L;
	InitInfo init;
	tl::Arena scratch;			// (each packet's decode; cleared after it)

	Session () : dc (0), test (false), port (443), keyId (0), haveKey (false), salt (0), timeOffset (0), L (0),
		     m_t (0), m_state (S_IDLE), m_sessionId (0), m_lastMsgId (0), m_seq (0), m_nextReq (1),
		     m_inited (false), m_nack (0), m_lastSend (0), m_lastRecv (0), m_lastPing (0), m_retryAt (0), m_fails (0),
		     m_hsStart (0)
	{
		host[0] = 0;
		memset (key, 0, sizeof key);
		memset (m_req, 0, sizeof m_req);
		init.api_id = 0; init.device = "Raspberry Pi 4"; init.system = "Onyx"; init.app = "1.0"; init.lang = "en";
	}
	~Session () { stop (); for (int i = 0; i < MAXREQ; i++) free (m_req[i].body); }

	void setKey (const unsigned char k[256], long long s)
	{
		memcpy (key, k, 256);
		unsigned char h[20];
		tgc::sha1 (h, tgc::Part { key, 256 });
		memcpy (&keyId, h + 12, 8);
		haveKey = true;
		salt = s;
	}

	// Connects (the key made first if there is none). The transport is the session's from now on.
	void start (Transport *t)
	{
		stop ();
		m_t = t;
		m_inited = false;
		if (!m_sessionId) newSession ();
		if (!m_t->open (host, port)) { fail ("connect"); return; }
		m_lastRecv = m_lastSend = m_lastPing = now ();
		if (haveKey) { m_state = S_READY; resendAll (); }
		else beginHandshake ();
	}
	void stop ()
	{
		if (m_t) { m_t->close (); delete m_t; m_t = 0; }
		m_state = S_IDLE;
	}
	bool ready () const { return m_state == S_READY; }
	bool connected () const { return m_t != 0 && m_state != S_IDLE; }
	bool busy () const { for (int i = 0; i < MAXREQ; i++) if (m_req[i].used) return true; return false; }
	// (the transport broke: the app makes a new one and calls start () again after a while)
	bool broken () const { return m_state == S_BROKEN; }
	long long retryAt () const { return m_retryAt; }

	// A request (an object of a function, made with tl::make) -> its id (>0), 0 if the queue is full.
	int call (const tl::Val &query)
	{
		int slot = -1;
		for (int i = 0; i < MAXREQ; i++) if (!m_req[i].used) { slot = i; break; }
		if (slot < 0) return 0;
		tl::Buf b;
		if (!tl::encode (b, query)) return 0;
		Req &r = m_req[slot];
		r.used = true; r.sent = false; r.id = m_nextReq++; r.msgId = 0; r.fn = query.c; r.wrapped = false; r.reinit = 0;
		free (r.body);
		r.n = b.n; r.body = b.take ();
		if (m_state == S_READY) sendReq (r);
		return r.id;
	}
	// The same with a request already encoded (fn: its function, how its answer is read).
	int callRaw (const unsigned char *body, int n, const tl::Ctor *fn)
	{
		int slot = -1;
		for (int i = 0; i < MAXREQ; i++) if (!m_req[i].used) { slot = i; break; }
		if (slot < 0 || n <= 0) return 0;
		Req &r = m_req[slot];
		r.used = true; r.sent = false; r.id = m_nextReq++; r.msgId = 0; r.fn = fn; r.wrapped = false; r.reinit = 0;
		free (r.body);
		r.body = (unsigned char *) malloc ((size_t) n);
		if (!r.body) { r.used = false; return 0; }
		memcpy (r.body, body, (size_t) n); r.n = n;
		if (m_state == S_READY) sendReq (r);
		return r.id;
	}
	void cancel (int id) { for (int i = 0; i < MAXREQ; i++) if (m_req[i].used && m_req[i].id == id) drop (m_req[i]); }

	// Each tick: what the transport has, the acks, the pings, the time-outs.
	void poll ()
	{
		if (!m_t) return;
		for (int k = 0; k < 64 && m_t; k++)
		{
			tl::Buf pk;
			int r = m_t->recv (pk);
			if (r < 0) { fail ("connection closed"); return; }
			if (r == 0) break;
			m_lastRecv = now ();
			if (pk.n == 4)
			{
				int code = (int) (pk.d[0] | (pk.d[1] << 8) | (pk.d[2] << 16) | ((unsigned) pk.d[3] << 24));
				tg_log ("mtproto: dc%d transport error %d", dc, code);
				if (code == -404 && m_state == S_READY)		// (the server does not know our key)
				{
					haveKey = false;
					beginHandshake ();
					continue;
				}
				fail ("transport error");
				return;
			}
			if (m_state == S_READY) gotEncrypted (pk.d, pk.n);
			else if (m_state >= S_PQ && m_state <= S_DHGEN) gotPlain (pk.d, pk.n);
			scratch.clear ();
		}
		if (!m_t) return;
		long long t = now ();
		if (m_state == S_READY)
		{
			if (m_nack >= 16 || (m_nack > 0 && t - m_lastSend > 300)) sendAcks ();
			if (!m_t->http () && t - m_lastPing > 30000) ping ();
			if (m_t->http () && t - m_lastSend > (busy () ? 200 : 10000)) httpWait ();	// (HTTP: the answers fetched)
			if (!m_t->http () && t - m_lastRecv > 90000) fail ("timeout");
		}
		else if (m_state != S_IDLE && m_state != S_BROKEN && t - m_hsStart > 30000) fail ("key exchange timeout");
	}

	long long now () const { return tg_unix_ms (); }
	int serverTime () const { return (int) ((tg_unix_ms () + timeOffset) / 1000); }

private:
	enum State { S_IDLE, S_PQ, S_DH, S_DHGEN, S_READY, S_BROKEN };
	struct Req { bool used, sent; int id; long long msgId; const tl::Ctor *fn; unsigned char *body; int n; bool wrapped; int reinit; };

	Transport *m_t;
	State m_state;
	long long m_sessionId, m_lastMsgId;
	int m_seq;
	int m_nextReq;
	bool m_inited;
	Req m_req[MAXREQ];
	long long m_ack[64];
	int m_nack;
	long long m_lastSend, m_lastRecv, m_lastPing, m_retryAt;
	int m_fails;
	long long m_hsStart;
	// the key exchange
	unsigned char m_nonce[16], m_snonce[16], m_newNonce[32];
	unsigned char m_tmpKey[32], m_tmpIv[32];
	unsigned char m_b[256];
	unsigned char m_dhPrime[256];
	int m_g;
	long long m_retryId;
	unsigned char m_gaBuf[256];

	void fail (const char *why)
	{
		tg_log ("mtproto: dc%d %s", dc, why);
		if (m_t) { m_t->close (); delete m_t; m_t = 0; }
		m_state = S_BROKEN;
		m_fails++;
		int back = m_fails < 6 ? 1000 << (m_fails - 1) : 30000;
		m_retryAt = now () + back;
		for (int i = 0; i < MAXREQ; i++) m_req[i].sent = false;
	}

	void newSession ()
	{
		tgc::random (&m_sessionId, 8);
		m_seq = 0;
		m_inited = false;
	}

	long long msgId ()
	{
		long long ms = tg_unix_ms () + timeOffset;
		long long id = ((ms / 1000) << 32) | ((long long) ((ms % 1000) * 4294967 / 1000) & 0xFFFFFFFCll);
		if (id <= m_lastMsgId) id = m_lastMsgId + 4;
		m_lastMsgId = id;
		return id;
	}
	int seqNo (bool content) { int s = m_seq * 2 + (content ? 1 : 0); if (content) m_seq++; return s; }

	// ---- the key exchange (plain messages) --------------------------------------------------

	void sendPlain (const tl::Val &q)
	{
		tl::Buf body, pk;
		tl::encode (body, q);
		pk.i64 (0);
		pk.i64 (msgId ());
		pk.u32 ((uint32_t) body.n);
		pk.put (body.d, body.n);
		if (m_t && !m_t->send (pk.d, pk.n)) fail ("send");
	}

	void beginHandshake ()
	{
		m_state = S_PQ;
		m_hsStart = now ();
		m_retryId = 0;
		tgc::reseed ();
		tgc::random (m_nonce, 16);
		tl::Arena a;
		tl::Val *q = tl::make (a, "req_pq_multi");
		q->set ("nonce", tl::S (a, m_nonce, 16));
		sendPlain (*q);
	}

	void gotPlain (const unsigned char *d, int n)
	{
		if (n < 20) return;
		long long kid; memcpy (&kid, d, 8);
		int len = (int) (d[16] | (d[17] << 8) | (d[18] << 16) | ((unsigned) d[19] << 24));
		if (kid != 0 || len > n - 20 || len < 4) return;
		tl::Val v;
		if (!tl::decode (scratch, d + 20, len, v)) { fail ("bad plain message"); return; }
		if (v.is ("resPQ")) gotResPQ (v);
		else if (v.is ("server_DH_params_ok")) gotDHParams (v);
		else if (v.is ("dh_gen_ok") || v.is ("dh_gen_retry") || v.is ("dh_gen_fail")) gotDHGen (v);
		else fail ("key exchange refused");
	}

	bool nonceOk (const tl::Val &v, bool server = true)
	{
		const tl::Val &a = v["nonce"];
		if (a.len () != 16 || memcmp (a.s, m_nonce, 16)) return false;
		if (server) { const tl::Val &b = v["server_nonce"]; if (b.len () != 16 || memcmp (b.s, m_snonce, 16)) return false; }
		return true;
	}

	void gotResPQ (const tl::Val &v)
	{
		if (m_state != S_PQ || !nonceOk (v, false)) return;
		memcpy (m_snonce, v["server_nonce"].s, 16);
		const tl::Val &pq = v["pq"];
		if (pq.len () > 8) { fail ("pq"); return; }
		uint64_t n = 0;
		for (int i = 0; i < pq.len (); i++) n = (n << 8) | (unsigned char) pq.s[i];
		uint64_t p, q;
		if (!tgc::factor (n, &p, &q)) { fail ("pq factor"); return; }
		int nk = 0;
		tgc::RsaKey *keys = tgc::rsa_keys (&nk);
		const tgc::RsaKey *key = 0;
		const tl::Val &fps = v["server_public_key_fingerprints"];
		for (int i = 0; i < fps.count () && !key; i++)
			for (int k = 0; k < nk; k++) if (keys[k].fp == fps[i].l) { key = &keys[k]; break; }
		if (!key) { fail ("no known RSA key"); return; }
		unsigned char pb[8], qb[8];
		int pn = be (p, pb), qn = be (q, qb);
		tgc::random (m_newNonce, 32);
		tl::Arena a;
		tl::Val *in = tl::make (a, "p_q_inner_data_dc");
		in->set ("pq", tl::S (a, pq.s, pq.n));
		in->set ("p", tl::S (a, pb, pn));
		in->set ("q", tl::S (a, qb, qn));
		in->set ("nonce", tl::S (a, m_nonce, 16));
		in->set ("server_nonce", tl::S (a, m_snonce, 16));
		in->set ("new_nonce", tl::S (a, m_newNonce, 32));
		in->set ("dc", tl::I (test ? 10000 + dc : dc));
		tl::Buf data;
		tl::encode (data, *in);
		unsigned char enc[256];
		if (!tgc::rsa_pad (data.d, data.n, *key, enc)) { fail ("rsa"); return; }
		tl::Val *q2 = tl::make (a, "req_DH_params");
		q2->set ("nonce", tl::S (a, m_nonce, 16));
		q2->set ("server_nonce", tl::S (a, m_snonce, 16));
		q2->set ("p", tl::S (a, pb, pn));
		q2->set ("q", tl::S (a, qb, qn));
		q2->set ("public_key_fingerprint", tl::L (key->fp));
		q2->set ("encrypted_data", tl::S (a, enc, 256));
		m_state = S_DH;
		sendPlain (*q2);
	}

	static int be (uint64_t x, unsigned char *o)
	{
		unsigned char t[8]; int n = 0;
		while (x) { t[n++] = (unsigned char) x; x >>= 8; }
		for (int i = 0; i < n; i++) o[i] = t[n - 1 - i];
		return n;
	}

	void gotDHParams (const tl::Val &v)
	{
		if (m_state != S_DH || !nonceOk (v)) return;
		// tmp_aes_key = SHA1(new_nonce + server_nonce) + SHA1(server_nonce + new_nonce)[0:12]
		// tmp_aes_iv = SHA1(server_nonce + new_nonce)[12:20] + SHA1(new_nonce + new_nonce) + new_nonce[0:4]
		unsigned char h1[20], h2[20], h3[20];
		tgc::sha1 (h1, tgc::Part { m_newNonce, 32 }, tgc::Part { m_snonce, 16 });
		tgc::sha1 (h2, tgc::Part { m_snonce, 16 }, tgc::Part { m_newNonce, 32 });
		tgc::sha1 (h3, tgc::Part { m_newNonce, 32 }, tgc::Part { m_newNonce, 32 });
		memcpy (m_tmpKey, h1, 20); memcpy (m_tmpKey + 20, h2, 12);
		memcpy (m_tmpIv, h2 + 12, 8); memcpy (m_tmpIv + 8, h3, 20); memcpy (m_tmpIv + 28, m_newNonce, 4);
		const tl::Val &ea = v["encrypted_answer"];
		if (ea.len () < 32 || ea.len () % 16) { fail ("dh answer"); return; }
		unsigned char *ans = (unsigned char *) scratch.alloc ((unsigned) ea.len ());
		tgc::aes_ige (false, m_tmpKey, m_tmpIv, (const unsigned char *) ea.s, ans, ea.len ());
		tl::Val in;
		tl::Reader rd (ans + 20, ea.len () - 20);
		if (!tl::decode_obj (scratch, rd, in, 0) || !in.is ("server_DH_inner_data")) { fail ("dh inner"); return; }
		int used = (int) (rd.p - (ans + 20));
		unsigned char hh[20];
		tgc::sha1 (hh, tgc::Part { ans + 20, used });
		if (memcmp (hh, ans, 20)) { fail ("dh inner hash"); return; }
		if (!nonceOk (in)) { fail ("dh nonce"); return; }
		m_g = (int) in["g"].i ();
		const tl::Val &prime = in["dh_prime"], &ga = in["g_a"];
		if (prime.len () != 256 || ga.len () > 256 || !tgc::check_dh ((const unsigned char *) prime.s, 256, m_g)) { fail ("dh prime"); return; }
		memcpy (m_dhPrime, prime.s, 256);
		timeOffset = (long long) in["server_time"].i () * 1000 - tg_unix_ms ();
		memset (m_gaBuf, 0, 256);
		memcpy (m_gaBuf + 256 - ga.len (), ga.s, (size_t) ga.len ());
		tgc::Mpi P (m_dhPrime, 256), GA (m_gaBuf, 256);
		if (!tgc::check_g (GA, P)) { fail ("g_a"); return; }
		sendClientDH ();
	}

	void sendClientDH ()
	{
		tgc::Mpi P (m_dhPrime, 256), G, B, GB;
		mbedtls_mpi_lset (&G.m, m_g);
		for (int tries = 0; ; tries++)
		{
			tgc::random (m_b, 256);
			B.set (m_b, 256);
			tgc::powmod (GB, G, B, P);
			if (tgc::check_g (GB, P)) break;
			if (tries > 8) { fail ("g_b"); return; }
		}
		unsigned char gb[256];
		GB.out (gb, 256);
		tl::Arena a;
		tl::Val *in = tl::make (a, "client_DH_inner_data");
		in->set ("nonce", tl::S (a, m_nonce, 16));
		in->set ("server_nonce", tl::S (a, m_snonce, 16));
		in->set ("retry_id", tl::L (m_retryId));
		in->set ("g_b", tl::S (a, gb, 256));
		tl::Buf data;
		tl::encode (data, *in);
		int tot = 20 + data.n;
		int padded = (tot + 15) & ~15;
		unsigned char *plain = (unsigned char *) a.alloc ((unsigned) padded), *enc = (unsigned char *) a.alloc ((unsigned) padded);
		tgc::sha1 (plain, tgc::Part { data.d, data.n });
		memcpy (plain + 20, data.d, (size_t) data.n);
		tgc::random (plain + tot, padded - tot);
		tgc::aes_ige (true, m_tmpKey, m_tmpIv, plain, enc, padded);
		tl::Val *q = tl::make (a, "set_client_DH_params");
		q->set ("nonce", tl::S (a, m_nonce, 16));
		q->set ("server_nonce", tl::S (a, m_snonce, 16));
		q->set ("encrypted_data", tl::S (a, enc, padded));
		m_state = S_DHGEN;
		sendPlain (*q);
	}

	void gotDHGen (const tl::Val &v)
	{
		if (m_state != S_DHGEN || !nonceOk (v)) return;
		tgc::Mpi P (m_dhPrime, 256), GA (m_gaBuf, 256), B (m_b, 256), K;
		tgc::powmod (K, GA, B, P);
		unsigned char k[256];
		K.out (k, 256);
		unsigned char kh[20];
		tgc::sha1 (kh, tgc::Part { k, 256 });
		// new_nonce_hash{1,2,3} = SHA1(new_nonce + {1,2,3} + auth_key_aux_hash (kh[0:8]))[4:20]
		int which = v.is ("dh_gen_ok") ? 1 : v.is ("dh_gen_retry") ? 2 : 3;
		unsigned char tag = (unsigned char) which, nh[20];
		tgc::sha1 (nh, tgc::Part { m_newNonce, 32 }, tgc::Part { &tag, 1 }, tgc::Part { kh, 8 });
		const char *field = which == 1 ? "new_nonce_hash1" : which == 2 ? "new_nonce_hash2" : "new_nonce_hash3";
		const tl::Val &got = v[field];
		if (got.len () != 16 || memcmp (got.s, nh + 4, 16)) { fail ("new_nonce_hash"); return; }
		if (which == 2) { memcpy (&m_retryId, kh, 8); sendClientDH (); return; }
		if (which == 3) { fail ("dh_gen_fail"); return; }
		long long s = 0;
		for (int i = 0; i < 8; i++) ((unsigned char *) &s)[i] = m_newNonce[i] ^ m_snonce[i];
		setKey (k, s);
		memset (m_b, 0, sizeof m_b);
		tg_log ("mtproto: dc%d authorization key made", dc);
		m_state = S_READY;
		m_fails = 0;
		newSession ();
		if (L) L->onKeyReady ();
		resendAll ();
	}

	// ---- encrypted messages -----------------------------------------------------------------

	// MTProto 2.0's msg_key and AES key / iv (x = 0 client to server, 8 server to client)
	void kdf (const unsigned char msgKey[16], int x, unsigned char aesKey[32], unsigned char aesIv[32])
	{
		unsigned char a[32], b[32];
		tgc::sha256 (a, tgc::Part { msgKey, 16 }, tgc::Part { key + x, 36 });
		tgc::sha256 (b, tgc::Part { key + 40 + x, 36 }, tgc::Part { msgKey, 16 });
		memcpy (aesKey, a, 8); memcpy (aesKey + 8, b + 8, 16); memcpy (aesKey + 24, a + 24, 8);
		memcpy (aesIv, b, 8); memcpy (aesIv + 8, a + 8, 16); memcpy (aesIv + 24, b + 24, 8);
	}

	// One message (its body encoded) -> its msg_id.
	long long sendEncrypted (const unsigned char *body, int n, bool content, long long forceId = 0)
	{
		if (!m_t) return 0;
		long long id = forceId ? forceId : msgId ();
		int len = 32 + n;
		int pad = 16 - (len % 16);
		if (pad < 12) pad += 16;
		int tot = len + pad;
		unsigned char *pl = (unsigned char *) malloc ((size_t) tot), *pk = (unsigned char *) malloc ((size_t) tot + 24);
		if (!pl || !pk) { free (pl); free (pk); return 0; }
		memcpy (pl, &salt, 8);
		memcpy (pl + 8, &m_sessionId, 8);
		memcpy (pl + 16, &id, 8);
		int seq = seqNo (content);
		memcpy (pl + 24, &seq, 4);
		memcpy (pl + 28, &n, 4);
		memcpy (pl + 32, body, (size_t) n);
		tgc::random (pl + len, pad);
		unsigned char big[32];
		tgc::sha256 (big, tgc::Part { key + 88, 32 }, tgc::Part { pl, tot });
		unsigned char *msgKey = big + 8, ak[32], ai[32];
		kdf (msgKey, 0, ak, ai);
		memcpy (pk, &keyId, 8);
		memcpy (pk + 8, msgKey, 16);
		tgc::aes_ige (true, ak, ai, pl, pk + 24, tot);
		bool ok = m_t->send (pk, tot + 24);
		free (pl); free (pk);
		if (!ok) { fail ("send"); return 0; }
		m_lastSend = now ();
		return id;
	}

	void sendReq (Req &r)
	{
		if (!m_t || m_state != S_READY) return;
		const unsigned char *body = r.body;
		int n = r.n;
		tl::Buf wrapped;
		// invokeWithLayer (initConnection (query)) -- EVERY request until the server has answered a wrapped one: the
		// first message of a connection is often refused before it is read (bad_server_salt: the salt is not known
		// yet; bad_msg_notification 16 / 17: the Pi's clock) and sent again -- sent bare then, as "the connection is
		// initialised" was noted at the first send, it got CONNECTION_NOT_INITED (the user's report, 2026-10-09: no
		// sign-in). m_inited is now set by an answer (gotMessage, rpc_result).
		r.wrapped = !m_inited;
		if (!m_inited)
		{
			tl::Arena a;
			tl::Val *ic = tl::make (a, "initConnection");
			ic->set ("api_id", tl::I (init.api_id));
			ic->set ("device_model", tl::S (a, init.device));
			ic->set ("system_version", tl::S (a, init.system));
			ic->set ("app_version", tl::S (a, init.app));
			ic->set ("system_lang_code", tl::S (a, init.lang));
			ic->set ("lang_pack", tl::S (a, ""));
			ic->set ("lang_code", tl::S (a, init.lang));
			ic->set ("query", tl::Raw (a, r.body, r.n));
			tl::Val *wl = tl::make (a, "invokeWithLayer");
			wl->set ("layer", tl::I (tl::schema ().layer));
			wl->set ("query", *ic);
			tl::encode (wrapped, *wl);
			body = wrapped.d; n = wrapped.n;
		}
		r.msgId = sendEncrypted (body, n, true);
		r.sent = r.msgId != 0;
		tg_log ("mtproto: dc%d -> %s (%d bytes, msg %llx)", dc, r.fn ? r.fn->name : "?", n, r.msgId);
	}

	void resendAll ()
	{
		m_inited = false;
		for (int i = 0; i < MAXREQ; i++) if (m_req[i].used) { m_req[i].sent = false; sendReq (m_req[i]); }
	}

	void drop (Req &r) { r.used = false; free (r.body); r.body = 0; r.n = 0; }

	Req *byMsg (long long id)
	{
		for (int i = 0; i < MAXREQ; i++) if (m_req[i].used && m_req[i].msgId == id) return &m_req[i];
		return 0;
	}

	void ack (long long id)
	{
		if (m_nack < 64) m_ack[m_nack++] = id;
	}
	void sendAcks ()
	{
		if (!m_nack) return;
		tl::Buf b;
		b.u32 (0x62d6b459u); b.u32 (0x1cb5c415u); b.u32 ((uint32_t) m_nack);
		for (int i = 0; i < m_nack; i++) b.i64 (m_ack[i]);
		m_nack = 0;
		sendEncrypted (b.d, b.n, false);
	}
	void ping ()
	{
		tl::Buf b;
		b.u32 (0xf3427b8cu);				// ping_delay_disconnect ping_id disconnect_delay
		b.i64 (tgc::random64 ()); b.u32 (75);
		m_lastPing = now ();
		sendEncrypted (b.d, b.n, true);
	}
	void httpWait ()
	{
		tl::Buf b;
		b.u32 (0x9299359fu); b.u32 (0); b.u32 (0); b.u32 (1500);	// http_wait max_delay wait_after max_wait
		if (m_nack) { sendAcks (); }
		sendEncrypted (b.d, b.n, false);
	}

	void gotEncrypted (const unsigned char *d, int n)
	{
		if (n < 24 + 32 || (n - 24) % 16) return;
		long long kid; memcpy (&kid, d, 8);
		if (kid != keyId) { tg_log ("mtproto: dc%d another key's packet", dc); return; }
		unsigned char ak[32], ai[32];
		kdf (d + 8, 8, ak, ai);
		int tot = n - 24;
		unsigned char *pl = (unsigned char *) scratch.alloc ((unsigned) tot);
		if (!pl) return;
		tgc::aes_ige (false, ak, ai, d + 24, pl, tot);
		unsigned char big[32];
		tgc::sha256 (big, tgc::Part { key + 96, 32 }, tgc::Part { pl, tot });
		if (memcmp (big + 8, d + 8, 16)) { tg_log ("mtproto: dc%d msg_key mismatch", dc); return; }
		m_fails = 0;
		long long sid; memcpy (&sid, pl + 8, 8);
		if (sid != m_sessionId) { tg_log ("mtproto: dc%d another session's packet", dc); return; }
		long long mid; memcpy (&mid, pl + 16, 8);
		int seq, len;
		memcpy (&seq, pl + 24, 4);
		memcpy (&len, pl + 28, 4);
		if (len < 4 || len > tot - 32 || tot - 32 - len < 12 || tot - 32 - len > 1024) { tg_log ("mtproto: dc%d bad length", dc); return; }
		gotMessage (mid, seq, pl + 32, len);
	}

	static uint32_t rd32 (const unsigned char *p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
	static long long rd64 (const unsigned char *p) { long long x; memcpy (&x, p, 8); return x; }

	void gotMessage (long long mid, int seq, const unsigned char *b, int n)
	{
		if (seq & 1) ack (mid);
		if (n < 4) return;
		uint32_t id = rd32 (b);
		switch (id)
		{
		case 0x73f1f8dcu:				// msg_container
		{
			int cnt = (int) rd32 (b + 4), o = 8;
			for (int i = 0; i < cnt && o + 16 <= n; i++)
			{
				long long m = rd64 (b + o);
				int s = (int) rd32 (b + o + 8), l = (int) rd32 (b + o + 12);
				if (l < 0 || o + 16 + l > n) break;
				gotMessage (m, s, b + o + 16, l);
				o += 16 + l;
			}
			return;
		}
		case 0xf35c6d01u:				// rpc_result req_msg_id:long result:Object
		{
			if (n < 16) return;
			long long req = rd64 (b + 4);
			Req *r = byMsg (req);
			const unsigned char *res = b + 12;
			int rn = n - 12;
			if (rd32 (res) == 0x2144ca19u)		// rpc_error error_code:int error_message:string
			{
				tl::Val e;
				tl::decode (scratch, res, rn, e);
				int code = (int) e["error_code"].i ();
				const char *msg = e["error_message"].str ();
				if (!r) return;
				if (msg && !strcmp (msg, "CONNECTION_NOT_INITED") && r->reinit < 3)	// (sent bare too early: wrapped again)
				{
					tg_log ("mtproto: dc%d CONNECTION_NOT_INITED: %s sent again with initConnection", dc, r->fn ? r->fn->name : "?");
					r->reinit++;
					m_inited = false;
					sendReq (*r);
					return;
				}
				if (r->wrapped) m_inited = true;		// (an error of the query itself: the connection is initialised)
				int rid = r->id;
				drop (*r);
				if (L) L->onError (rid, code, msg);
				return;
			}
			if (!r) { tl::Val dv; tl::decode (scratch, b + 12, n - 12, dv); tg_log ("mtproto: dc%d an answer to no request (%llx, %s %s)", dc, req, dv.name (), dv["phone_code_hash"].str ()); return; }
			if (r->wrapped) m_inited = true;			// (the server read initConnection: the next requests go bare)
			tl::Val v;
			tl::Reader rd (res, rn);
			bool ok = tl::decode_result (scratch, rd, v, r->fn);
			int rid = r->id;
			const char *fname = r->fn ? r->fn->name : "?";
			drop (*r);
			if (!ok) { tg_log ("mtproto: undecodable answer to %s", fname); if (L) L->onError (rid, -1, "UNDECODABLE_ANSWER"); return; }
			tg_log ("mtproto: dc%d <- %s: %s", dc, fname, v.name ());
			if (L) L->onResult (rid, v);
			return;
		}
		case 0xedab447bu:				// bad_server_salt bad_msg_id bad_msg_seqno error_code new_server_salt
		{
			if (n < 28) return;
			long long bad = rd64 (b + 4);
			salt = rd64 (b + 20);
			Req *r = byMsg (bad);
			if (r) sendReq (*r);
			else resendAll ();
			return;
		}
		case 0xa7eff811u:				// bad_msg_notification bad_msg_id bad_msg_seqno error_code
		{
			if (n < 20) return;
			long long bad = rd64 (b + 4);
			int code = (int) rd32 (b + 16);
			tg_log ("mtproto: dc%d bad_msg_notification %d", dc, code);
			if (code == 16 || code == 17)		// our time is wrong: the server's from its msg_id
			{
				timeOffset = (long long) ((unsigned long long) mid >> 32) * 1000 - tg_unix_ms ();
				m_lastMsgId = 0;
			}
			else if (code == 32 || code == 33)	// seq_no: a new session
				newSession ();
			Req *r = byMsg (bad);
			if (r) sendReq (*r); else if (code == 32 || code == 33) resendAll ();
			return;
		}
		case 0x9ec20908u:				// new_session_created first_msg_id unique_id server_salt
			if (n >= 28) salt = rd64 (b + 20);
			if (L) L->onNewSession ();
			return;
		case 0x62d6b459u: return;			// msgs_ack
		case 0x347773c5u: return;			// pong
		case 0x276d3ec6u: if (n >= 20) ack (rd64 (b + 12)); return;	// msg_detailed_info: ack answer_msg_id
		case 0x809db6dfu: if (n >= 12) ack (rd64 (b + 4)); return;	// msg_new_detailed_info
		case 0xae500895u: return;			// future_salts
		}
		tl::Val v;
		if (!tl::decode (scratch, b, n, v)) { tg_log ("mtproto: undecodable message %08x", id); return; }
		if (L) L->onUpdates (v);
	}
};

} // namespace mt

#endif
