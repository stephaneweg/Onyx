//
// onyx_tls.hpp -- TLS transport for Onyx user apps, built on mbedTLS over the kapi
// TCP sockets. This is the HTTPS backend for HttpClient (../http.hpp): define
// ONYX_HTTP_TLS before including http.hpp and link the mbedTLS libraries.
//
// It plugs mbedTLS onto Onyx's transport primitives:
//   * BIO send/recv -> kapi_tcp_send / kapi_tcp_recv (the latter is non-blocking, so
//     recv reports MBEDTLS_ERR_SSL_WANT_READ when there is nothing yet and we poll).
//   * Non-blocking handshake / read / write loops with a wall-clock timeout
//     (kapi_get_ticks) and kapi_msleep between polls -- the cooperative model.
//
// SECURITY / TODO:
//   * Entropy comes from the Pi's HARDWARE RNG via kapi_random (ABI v30, backed by
//     Circle's CBcmRandomNumberGenerator) -- see onyx_entropy below. Good seeding.
//   * Certificate verification: OPT-IN. start (s, sock, host) keeps the old behaviour
//     (MBEDTLS_SSL_VERIFY_NONE: the server identity is NOT checked -- open to MITM); an app
//     that sets the trusted roots (set_ca_bundle: a PEM bundle, NetSurf's SD:/res/ca-bundle)
//     and passes START_VERIFY to start () gets the chain checked against them, the host name
//     (SNI + the certificate's SAN / CN) and the validity dates against the Onyx clock
//     (mbedTLS is built without MBEDTLS_HAVE_TIME_DATE: the dates are checked here, and only
//     when the clock is set -- a year >= 2025). NetSurf does (user/netsurf/onyx_nstls.cpp).
//
// Built for mbedTLS 3.6.x configured bare-metal (no NET/FS/TIMING; TLS 1.2 and 1.3 -- 1.3 on
// PSA crypto, its random generator ours: mbedtls_psa_external_get_random below). See
// user/tls/README and the onyx_mbedtls_config.h that pins the configuration.
//
#ifndef ONYX_TLS_HPP
#define ONYX_TLS_HPP

#include "kapi.h"
#include <string.h>		// memcpy
#include <stdlib.h>		// malloc / free (the check's record, the roots), getenv
#include <stdio.h>		// (the debug lines)

#include <mbedtls/ssl.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <psa/crypto.h>

// (Onyx) TLS 1.3 runs on PSA crypto, built with MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG: its random
// bytes are the app's -- kapi_random, as the TLS 1.2 DRBG's seed (see the security note). Weak:
// every app that includes this header has it, the linker keeps one.
#if defined(MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG)
extern "C" __attribute__((weak)) psa_status_t mbedtls_psa_external_get_random (
	mbedtls_psa_external_random_context_t *ctx, uint8_t *out, size_t size, size_t *olen)
{
	(void) ctx;
	size_t got = 0;
	while (got < size) {
		unsigned want = size - got > 4096 ? 4096u : (unsigned) (size - got);
		int n = kapi_random (out + got, want);
		if (n <= 0) { *olen = got; return PSA_ERROR_INSUFFICIENT_ENTROPY; }
		got += (size_t) n;
	}
	*olen = got;
	return PSA_SUCCESS;
}
#endif

// These live in mbedtls/net_sockets.h, which is empty when MBEDTLS_NET_C is off
// (we provide our own BIO). Define the canonical values if absent.
#ifndef MBEDTLS_ERR_NET_RECV_FAILED
#define MBEDTLS_ERR_NET_RECV_FAILED -0x004C
#endif
#ifndef MBEDTLS_ERR_NET_SEND_FAILED
#define MBEDTLS_ERR_NET_SEND_FAILED -0x004E
#endif
#ifndef MBEDTLS_ERR_NET_CONN_RESET
#define MBEDTLS_ERR_NET_CONN_RESET -0x0050
#endif

namespace onyx_tls
{
	struct Verify;

	struct Session
	{
		int                       sock;	// kapi TCP handle
		mbedtls_ssl_context       ssl;
		mbedtls_ssl_config        conf;
		mbedtls_ctr_drbg_context  drbg;
		mbedtls_entropy_context   entropy;
		// RX stream buffer. Circle's CSocket::Receive returns ONE TCP segment per call and
		// DISCARDS the remainder if you read fewer bytes than the segment holds (socket.cpp:
		// truncate to nLength, then `delete pNetBuffer`). mbedTLS reads tiny 5-byte record
		// headers, so we must capture a WHOLE segment here and feed mbedTLS its small reads
		// from this buffer -- otherwise the rest of each segment is lost and the record
		// stream desyncs ("unknown record type"). Big enough for one MSS (~1460) -- and more:
		// the kernel gathers the segments that fit into one read (fewer round trips to the
		// network core), 16 KB holds a whole TLS record.
		unsigned char             rxbuf[16384];
		int                       rxlen, rxpos;
		Verify                   *vr;		// (Onyx) the check's record, or 0
		long long                 now;		// (Onyx) the clock in seconds since 1970, 0 unset (64 bits:
		                          		// Windows' long is 32 -- the dates past 2038 wrapped)
		volatile int             *cancel;	// (Onyx) set by another thread: stop waiting, fail
		int                       vrfy_calls;	// (Onyx) certificates the check looked at
		bool                      resumed;	// (Onyx) the handshake resumed a cached session
		char                      host[128];	// (Onyx) for the TLS 1.3 tickets that come later
		bool                      trusted;	// (Onyx) its certificate checked and good
	};

	// (Onyx) an app-wide "stop now" for the handshakes and writes in progress (NetSurf's end:
	// they give their sockets back at once instead of waiting up to 20 s)
	inline volatile int *&cancel_flag (void) { static volatile int *f; return f; }

	// ---- certificate verification (Onyx) ------------------------------------
	enum { START_VERIFY = 1,		// check the server's certificate (the roots: set_ca_bundle)
	       START_INSECURE = 2,		// ... but go on when it fails (the user said so)
	       START_ALPN_H2 = 4,		// offer HTTP/2 by ALPN ("h2", then "http/1.1")
	       START_ALPN_H1 = 8 };		// name HTTP/1.1 by ALPN
	enum { CHAIN_MAX = 8 };
	// What the check saw: each certificate of the chain it built ([0] the server's, the root
	// last), its DER copy (malloc'd) and its own mbedTLS MBEDTLS_X509_BADCERT_* flags; the
	// flags of the whole check (0: trusted). Free with verify_free.
	struct Verify
	{
		uint32_t flags;
		int depth;
		struct { uint32_t flags; unsigned char *der; size_t len; bool self_signed; } cert[CHAIN_MAX];
	};
	inline void verify_free (Verify *v)
	{
		for (int i = 0; i < CHAIN_MAX; i++) { free (v->cert[i].der); v->cert[i].der = 0; }
		v->depth = 0;
	}

	// The trusted roots: a PEM bundle's bytes, given once (set_ca_bundle, the app's own thread:
	// it reads the file), parsed at the first check (a handshake in any thread) under a lock and
	// shared read-only by every session from then on.
	struct Trust { mbedtls_x509_crt ca; unsigned char *pem; size_t len; int state; volatile int lk; };
	inline Trust *trust (void) { static Trust t; return &t; }	// zero-init: state 0
	// The PEM bytes (NUL-terminated, len counts the NUL, as mbedtls_x509_crt_parse wants them);
	// the trust takes the malloc'd buffer.
	inline void set_ca_bundle (unsigned char *pem, size_t len)
	{
		Trust *t = trust ();
		kapi_lock (&t->lk);
		if (t->state == 0 && t->pem == 0) { t->pem = pem; t->len = len; pem = 0; }
		kapi_unlock (&t->lk);
		free (pem);
	}
	inline mbedtls_x509_crt *ca_chain (void)
	{
		Trust *t = trust ();
		kapi_lock (&t->lk);
		if (t->state == 0) {
			mbedtls_x509_crt_init (&t->ca);
			// (> 0: that many certificates of the bundle not understood -- the rest kept)
			int r = t->pem != 0 ? mbedtls_x509_crt_parse (&t->ca, t->pem, t->len) : -1;
			t->state = r >= 0 && t->ca.raw.p != 0 ? 1 : -1;
			free (t->pem);
			t->pem = 0;
		}
		kapi_unlock (&t->lk);
		return t->state == 1 ? &t->ca : 0;
	}

	// Days since 1970-01-01 of a civil date (proleptic Gregorian).
	inline long long days_from_civil (long long y, int m, int d)
	{
		y -= m <= 2;
		long long era = (y >= 0 ? y : y - 399) / 400;
		long long yoe = y - era * 400;
		long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
		long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
		return era * 146097 + doe - 719468;
	}
	inline long long x509_secs (const mbedtls_x509_time &t)
	{
		return days_from_civil (t.year, t.mon, t.day) * 86400LL + t.hour * 3600LL + t.min * 60LL + t.sec;
	}
	// The clock (kapi_get_datetime: the local time the Setup app set) -> seconds, 0 when unset.
	inline long long clock_secs (void)
	{
		int y, mo, d, h, mi, se;
		if (kapi_get_datetime (&y, &mo, &d, &h, &mi, &se) == 0 || y < 2025) return 0;
		return days_from_civil (y, mo, d) * 86400LL + h * 3600LL + mi * 60LL + se;
	}
	// mbedTLS's callback for each certificate of the chain (the root first): the dates -- a day
	// of slack both ways: the clock is the local time, the certificates' UTC --, and the record.
	static int verify_cb (void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
	{
		Session *s = (Session *) ctx;
		s->vrfy_calls++;
		if (s->now != 0) {
			if (s->now - 86400LL > x509_secs (crt->valid_to)) *flags |= MBEDTLS_X509_BADCERT_EXPIRED;
			if (s->now + 86400LL < x509_secs (crt->valid_from)) *flags |= MBEDTLS_X509_BADCERT_FUTURE;
		}
		if (s->vr != 0 && depth >= 0 && depth < CHAIN_MAX) {
			Verify *v = s->vr;
			free (v->cert[depth].der);
			v->cert[depth].der = (unsigned char *) malloc (crt->raw.len);
			if (v->cert[depth].der != 0) memcpy (v->cert[depth].der, crt->raw.p, crt->raw.len);
			v->cert[depth].len = v->cert[depth].der != 0 ? crt->raw.len : 0;
			v->cert[depth].flags = *flags;
			v->cert[depth].self_signed = crt->subject_raw.len == crt->issuer_raw.len &&
				memcmp (crt->subject_raw.p, crt->issuer_raw.p, crt->subject_raw.len) == 0;
			if (depth + 1 > v->depth) v->depth = depth + 1;
		}
		return 0;
	}

	// ---- entropy: the Pi hardware RNG via kapi_random (ABI v30) ------------
	static int onyx_entropy (void *data, unsigned char *out, size_t len, size_t *olen)
	{
		(void) data;
		int n = kapi_random (out, (unsigned) len);
		if (n <= 0) { *olen = 0; return -1; }
		*olen = (size_t) n;
		return 0;
	}

	// ---- BIO: mbedTLS <-> kapi TCP (ctx = Session*) -------------------------
	static int bio_send (void *ctx, const unsigned char *buf, size_t len)
	{
		Session *s = (Session *) ctx;
		int n = kapi_tcp_send (s->sock, buf, (unsigned) len);	// blocking, all-or-error
		return n >= 0 ? n : MBEDTLS_ERR_NET_SEND_FAILED;
	}
	// Serve mbedTLS's (often tiny) reads from a whole-segment buffer. When empty, refill by
	// reading a FULL segment (large len) so Circle never discards a remainder. Cooperative-
	// blocking: yield with msleep until data or close, with an overall timeout.
	static int bio_recv (void *ctx, unsigned char *buf, size_t len)
	{
		Session *s = (Session *) ctx;
		if (s->rxpos >= s->rxlen)
		{
			unsigned start_t = kapi_get_ticks ();
			for (;;)
			{
				int n = kapi_tcp_recv (s->sock, s->rxbuf, (unsigned) sizeof s->rxbuf);
				if (n > 0) { s->rxlen = n; s->rxpos = 0; break; }
				if (n < 0) return MBEDTLS_ERR_NET_CONN_RESET;		// closed / error
				if ((kapi_get_ticks () - start_t) * 10 > 20000) return MBEDTLS_ERR_NET_CONN_RESET;
				kapi_msleep (2);					// nothing yet -> yield
			}
		}
		int avail = s->rxlen - s->rxpos;
		int give = (int) len < avail ? (int) len : avail;
		memcpy (buf, s->rxbuf + s->rxpos, (size_t) give);
		s->rxpos += give;
		return give;
	}

	// ---- TLS session cache (resumption, network lever B) -------------------
	// Keyed by host. A NEW connection to a host we've already handshaked with can
	// RESUME -- skipping the full ECDHE + signature -- which is a big win for pages
	// that fetch many resources from the same host over separate connections (the
	// current per-resource-connection model). Per-app (one cache per linked app).
	enum { TLS_SESS_CACHE_N = 32 };
	struct SessCacheEntry { char host[128]; mbedtls_ssl_session sess; bool valid; bool trusted; };

	inline bool sess_streq (const char *a, const char *b)
	{
		while (*a != '\0' && *a == *b) { a++; b++; }
		return *a == *b;
	}
	inline SessCacheEntry *sess_cache (void)
	{
		static SessCacheEntry s_cache[TLS_SESS_CACHE_N];	// zero-init -> valid=false
		return s_cache;
	}
	// The cache is shared by an app's threads (NetSurf: a handshake per fetch thread):
	// kapi_lock around every use (sess_find + set_session, sess_save).
	inline volatile int *sess_lock (void)
	{
		static volatile int s_lock;
		return &s_lock;
	}
	inline SessCacheEntry *sess_find (const char *host)
	{
		SessCacheEntry *c = sess_cache ();
		for (int i = 0; i < TLS_SESS_CACHE_N; i++)
			if (c[i].valid && sess_streq (c[i].host, host)) return &c[i];
		return 0;
	}
	// Cache the current (resumable) session of `ssl` under `host`, for next time.
	inline void sess_save (const char *host, mbedtls_ssl_context *ssl)
	{
		SessCacheEntry *c = sess_cache (), *e = sess_find (host);
		if (e == 0) {					// reuse host slot, else a free one, else in turn
			static unsigned s_next;
			for (int i = 0; i < TLS_SESS_CACHE_N; i++) if (!c[i].valid) { e = &c[i]; break; }
			if (e == 0) e = &c[s_next++ % TLS_SESS_CACHE_N];
		}
		if (e->valid) mbedtls_ssl_session_free (&e->sess);
		mbedtls_ssl_session_init (&e->sess);
		if (mbedtls_ssl_get_session (ssl, &e->sess) == 0) {
			unsigned i = 0;
			for (; host[i] != '\0' && i < sizeof e->host - 1; i++) e->host[i] = host[i];
			e->host[i] = '\0';
			e->valid = true;
		} else {
			mbedtls_ssl_session_free (&e->sess);
			e->valid = false;
		}
	}

	// (Onyx) NS_TLSDEBUG=1 (the PC bench): each handshake's version and resumption on stderr
	inline bool debug (void)
	{
		static int d = -1;
		if (d < 0) { const char *e = getenv ("NS_TLSDEBUG"); d = e != 0 && *e == '1'; }
		return d > 0;
	}

	// (Onyx) PSA crypto set up once per app (TLS 1.3's key exchange and record protection)
	inline bool psa_ready (void)
	{
		static volatile int s_lk, s_state;		// 0 not yet, 1 ready, -1 failed
		if (s_state == 0) {
			kapi_lock (&s_lk);
			if (s_state == 0) s_state = psa_crypto_init () == PSA_SUCCESS ? 1 : -1;
			kapi_unlock (&s_lk);
		}
		return s_state > 0;
	}

	// ---- session lifecycle -------------------------------------------------
	// Returns 0 on a completed handshake, -1 on any failure (caller closes the sock), -2 (Onyx,
	// START_VERIFY) the server's certificate refused (*vr says why; stop () the session).
	// opts: START_* (0: no check, as before); vr: the check's record, or 0.
	inline int start (Session &s, int sock, const char *host, unsigned opts, Verify *vr)
	{
		s.sock = sock;
		s.rxlen = 0; s.rxpos = 0;		// reset the RX stream buffer
		s.vr = vr;
		s.now = 0;
		s.cancel = cancel_flag ();
		s.vrfy_calls = 0;
		s.resumed = false;
		s.trusted = false;
		{
			unsigned i = 0;
			for (; host[i] != '\0' && i < sizeof s.host - 1; i++) s.host[i] = host[i];
			s.host[i] = '\0';
		}
		bool offered = false, offered_trusted = false;
		bool tls13 = psa_ready ();		// (no PSA: TLS 1.2 only)
		mbedtls_ssl_init (&s.ssl);
		mbedtls_ssl_config_init (&s.conf);
		mbedtls_ctr_drbg_init (&s.drbg);
		mbedtls_entropy_init (&s.entropy);

		if (mbedtls_entropy_add_source (&s.entropy, onyx_entropy, 0, 32,
						MBEDTLS_ENTROPY_SOURCE_STRONG) != 0) return -1;
		if (mbedtls_ctr_drbg_seed (&s.drbg, mbedtls_entropy_func, &s.entropy,
					   (const unsigned char *) "onyx-tls", 8) != 0) return -1;
		if (mbedtls_ssl_config_defaults (&s.conf, MBEDTLS_SSL_IS_CLIENT,
						 MBEDTLS_SSL_TRANSPORT_STREAM,
						 MBEDTLS_SSL_PRESET_DEFAULT) != 0) return -1;

		if (opts & START_VERIFY) {
			// (Onyx) OPTIONAL: the handshake completes and the result is judged below -- the
			// chain is kept for the caller's error page, "go on anyway" possible
			mbedtls_x509_crt *ca = ca_chain ();
			mbedtls_ssl_conf_authmode (&s.conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
			if (ca != 0) mbedtls_ssl_conf_ca_chain (&s.conf, ca, 0);
			mbedtls_ssl_conf_verify (&s.conf, verify_cb, &s);
			s.now = clock_secs ();
		} else {
			mbedtls_ssl_conf_authmode (&s.conf, MBEDTLS_SSL_VERIFY_NONE);
		}
		mbedtls_ssl_conf_rng (&s.conf, mbedtls_ctr_drbg_random, &s.drbg);
		// (Onyx) TLS 1.2 and 1.3, as the browsers
		mbedtls_ssl_conf_min_tls_version (&s.conf, MBEDTLS_SSL_VERSION_TLS1_2);
		mbedtls_ssl_conf_max_tls_version (&s.conf, tls13 ? MBEDTLS_SSL_VERSION_TLS1_3 :
						  MBEDTLS_SSL_VERSION_TLS1_2);
#if defined(MBEDTLS_SSL_PROTO_TLS1_3) && defined(MBEDTLS_SSL_SESSION_TICKETS)
		// the TLS 1.3 tickets (sent after the handshake) handed to recv: kept for the next time
		mbedtls_ssl_conf_tls13_enable_signal_new_session_tickets (&s.conf,
				MBEDTLS_SSL_TLS1_3_SIGNAL_NEW_SESSION_TICKETS_ENABLED);
#endif
		// (Onyx) ALPN, when asked: HTTP/2 offered, or HTTP/1.1 named
		static const char *s_alpn_h2[] = { "h2", "http/1.1", 0 };
		static const char *s_alpn_h1[] = { "http/1.1", 0 };
		if (opts & (START_ALPN_H2 | START_ALPN_H1))
			mbedtls_ssl_conf_alpn_protocols (&s.conf, (opts & START_ALPN_H2) ? s_alpn_h2 : s_alpn_h1);

		// (Onyx) the cipher suites in Chrome's order (its ClientHello on a desktop: the TLS 1.3
		// ones, then 1.2's) -- a client whose list is its own is told apart from the browser
		// it names in its User-Agent (Google's "unusual traffic", CDNs' 403). The Pi 4's
		// Cortex-A72 has no AES instructions: AES-GCM is slower than ChaCha20 in software,
		// still far faster than the network. (The suites this mbedTLS lacks are skipped.)
		static const int s_ciphersuites[] = {
#if defined(MBEDTLS_SSL_PROTO_TLS1_3)
			MBEDTLS_TLS1_3_AES_128_GCM_SHA256,
			MBEDTLS_TLS1_3_AES_256_GCM_SHA384,
			MBEDTLS_TLS1_3_CHACHA20_POLY1305_SHA256,
#endif
			MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
			MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
			MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
			MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
			MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
			MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
			MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
			MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
			MBEDTLS_TLS_RSA_WITH_AES_128_GCM_SHA256,
			MBEDTLS_TLS_RSA_WITH_AES_256_GCM_SHA384,
			MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA,
			MBEDTLS_TLS_RSA_WITH_AES_256_CBC_SHA,
			0
		};
		mbedtls_ssl_conf_ciphersuites (&s.conf, s_ciphersuites);
		// (Onyx) the signature algorithms in Chrome's order
		static const uint16_t s_sig_algs[] = {
			MBEDTLS_TLS1_3_SIG_ECDSA_SECP256R1_SHA256,
			MBEDTLS_TLS1_3_SIG_RSA_PSS_RSAE_SHA256,
			MBEDTLS_TLS1_3_SIG_RSA_PKCS1_SHA256,
			MBEDTLS_TLS1_3_SIG_ECDSA_SECP384R1_SHA384,
			MBEDTLS_TLS1_3_SIG_RSA_PSS_RSAE_SHA384,
			MBEDTLS_TLS1_3_SIG_RSA_PKCS1_SHA384,
			MBEDTLS_TLS1_3_SIG_RSA_PSS_RSAE_SHA512,
			MBEDTLS_TLS1_3_SIG_RSA_PKCS1_SHA512,
			MBEDTLS_TLS1_3_SIG_NONE
		};
		mbedtls_ssl_conf_sig_algs (&s.conf, s_sig_algs);

		// Prefer X25519 for the ECDHE key exchange (fastest curve in pure software; the TLS
		// 1.3 key share is sent for it), then secp256r1 and secp384r1 -- Chrome's groups (less
		// its post-quantum hybrid, X25519MLKEM768, which mbedTLS lacks). (network lever 3;
		// MBEDTLS_HAVE_ASM -- the assembly bignum -- stays enabled for the handshake math.)
		// (Onyx) with TLS 1.2 mbedTLS refuses a server certificate whose EC key is on a curve
		// not in this list (BADCERT_BAD_KEY): P-384 keys are common, P-521 ones rare.
		static const uint16_t s_groups[] = {
			MBEDTLS_SSL_IANA_TLS_GROUP_X25519,
			MBEDTLS_SSL_IANA_TLS_GROUP_SECP256R1,
			MBEDTLS_SSL_IANA_TLS_GROUP_SECP384R1,
			0
		};
		mbedtls_ssl_conf_groups (&s.conf, s_groups);

		if (mbedtls_ssl_setup (&s.ssl, &s.conf) != 0) return -1;
		mbedtls_ssl_set_hostname (&s.ssl, host);			// SNI
		mbedtls_ssl_set_bio (&s.ssl, &s, bio_send, bio_recv, 0);	// ctx = Session* (buffered BIO)

		// Resume a cached session for this host if we have one (abbreviated handshake).
		{
			kapi_lock (sess_lock ());
			SessCacheEntry *resume = sess_find (host);
			if (resume != 0) offered = mbedtls_ssl_set_session (&s.ssl, &resume->sess) == 0;
			offered_trusted = offered && resume->trusted;
			kapi_unlock (sess_lock ());
		}

		unsigned start_t = kapi_get_ticks ();
		int ret;
		while ((ret = mbedtls_ssl_handshake (&s.ssl)) != 0)
		{
			if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
			{
				if ((kapi_get_ticks () - start_t) * 10 > 20000) return -1;
				if (s.cancel != 0 && *s.cancel) return -1;
				kapi_msleep (5);
				continue;
			}
			return -1;
		}
		if (opts & START_VERIFY) {		// (Onyx) the check's verdict
			uint32_t f = mbedtls_ssl_get_verify_result (&s.ssl);
			// (a resumed session -- a TLS 1.3 ticket -- has no certificate to check: its first
			// connection's was, and only a trusted one is offered as trusted)
			if (offered && s.vrfy_calls == 0 && offered_trusted) f = 0;
			if (vr != 0) {
				vr->flags = f;
				// (the secondary checks -- the key's curve, its usage -- are the server's)
				if (f != 0 && vr->depth > 0) {
					uint32_t seen = 0;
					for (int i = 0; i < vr->depth && i < CHAIN_MAX; i++) seen |= vr->cert[i].flags;
					vr->cert[0].flags |= f & ~seen;
				}
			}
			if (f != 0 && !(opts & START_INSECURE)) return -2;
			// (Onyx) resumed: a cached session offered and no certificate to check (the
			// server took it: an abbreviated handshake)
			s.resumed = offered && s.vrfy_calls == 0;
		}
		if (debug ())
			fprintf (stderr, "ONYX-TLS %s: %s, a session offered %d (trusted %d), %d certificates\n",
				 host, mbedtls_ssl_get_version (&s.ssl), offered, offered_trusted, s.vrfy_calls);
		s.trusted = (opts & START_VERIFY) && (mbedtls_ssl_get_verify_result (&s.ssl) == 0 ||
				(s.resumed && offered_trusted));
		if (mbedtls_ssl_get_version_number (&s.ssl) != MBEDTLS_SSL_VERSION_TLS1_3) {
			// (TLS 1.3: the session comes later, as a ticket -- recv)
			kapi_lock (sess_lock ());
			sess_save (host, &s.ssl);		// cache the (resumable) session for next time
			SessCacheEntry *e = sess_find (host);	// (Onyx) kept on the card only if trusted
			if (e != 0) e->trusted = s.trusted;
			kapi_unlock (sess_lock ());
		}
		return 0;
	}

	// ---- (Onyx) the session cache kept across launches ---------------------------------
	// The trusted sessions written as bytes (the app saves them on the card and gives them back
	// at its next launch: a first connection to a known host resumes -- one round trip less,
	// no certificate chain to check): "OTLS1", then per session its host's length (1 byte), the
	// host, its data's length (4 bytes, little-endian), mbedtls_ssl_session_save's bytes.
	// sess_export (0, 0) -> the size needed; sess_export (buf, cap) -> the bytes written.
	inline size_t sess_export (unsigned char *buf, size_t cap)
	{
		size_t n = 5;
		kapi_lock (sess_lock ());
		SessCacheEntry *c = sess_cache ();
		if (buf != 0 && cap >= 5) memcpy (buf, "OTLS1", 5);
		for (int i = 0; i < TLS_SESS_CACHE_N; i++) {
			if (!c[i].valid || !c[i].trusted) continue;
			size_t hl = strlen (c[i].host), dl = 0;
			mbedtls_ssl_session_save (&c[i].sess, 0, 0, &dl);	// (the size)
			if (dl == 0 || hl > 127) continue;
			if (buf != 0) {
				if (n + 1 + hl + 4 + dl > cap) break;
				buf[n] = (unsigned char) hl;
				memcpy (buf + n + 1, c[i].host, hl);
				buf[n + 1 + hl] = (unsigned char) dl; buf[n + 2 + hl] = (unsigned char) (dl >> 8);
				buf[n + 3 + hl] = (unsigned char) (dl >> 16); buf[n + 4 + hl] = (unsigned char) (dl >> 24);
				size_t out = 0;
				if (mbedtls_ssl_session_save (&c[i].sess, buf + n + 5 + hl, dl, &out) != 0 || out != dl)
					continue;
			}
			n += 1 + hl + 4 + dl;
		}
		kapi_unlock (sess_lock ());
		return n;
	}
	inline void sess_import (const unsigned char *buf, size_t len)
	{
		if (len < 5 || memcmp (buf, "OTLS1", 5) != 0) return;
		size_t n = 5;
		kapi_lock (sess_lock ());
		SessCacheEntry *c = sess_cache ();
		for (int i = 0; i < TLS_SESS_CACHE_N && n < len; ) {
			size_t hl = buf[n];
			if (n + 1 + hl + 4 > len) break;
			size_t dl = buf[n + 1 + hl] | (size_t) buf[n + 2 + hl] << 8 |
				(size_t) buf[n + 3 + hl] << 16 | (size_t) buf[n + 4 + hl] << 24;
			if (n + 5 + hl + dl > len) break;
			if (!c[i].valid) {
				mbedtls_ssl_session_init (&c[i].sess);
				if (mbedtls_ssl_session_load (&c[i].sess, buf + n + 5 + hl, dl) == 0) {
					memcpy (c[i].host, buf + n + 1, hl);
					c[i].host[hl] = '\0';
					c[i].valid = c[i].trusted = true;
				} else {
					mbedtls_ssl_session_free (&c[i].sess);
				}
			}
			n += 5 + hl + dl;
			i++;
		}
		kapi_unlock (sess_lock ());
	}
	inline int start (Session &s, int sock, const char *host)
	{
		return start (s, sock, host, 0, 0);
	}

	// Write all `len` bytes. Returns len, or <0 on error.
	inline int send (Session &s, const void *buf, int len)
	{
		const unsigned char *p = (const unsigned char *) buf;
		int sent = 0;
		unsigned start_t = kapi_get_ticks ();
		while (sent < len)
		{
			int ret = mbedtls_ssl_write (&s.ssl, p + sent, (size_t) (len - sent));
			if (ret > 0) { sent += ret; start_t = kapi_get_ticks (); continue; }
			if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
			{
				if ((kapi_get_ticks () - start_t) * 10 > 15000) return -1;
				if (s.cancel != 0 && *s.cancel) return -1;
				kapi_msleep (5);
				continue;
			}
			return -1;
		}
		return sent;
	}

	// Read decrypted data. Matches kapi_tcp_recv's convention so HttpClient's loop is
	// unchanged: >0 bytes, 0 = nothing yet (poll), <0 = closed/error.
	inline int recv (Session &s, void *buf, int len)
	{
		int ret = mbedtls_ssl_read (&s.ssl, (unsigned char *) buf, (size_t) len);
		if (ret > 0) return ret;
		if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
#if defined(MBEDTLS_SSL_PROTO_TLS1_3) && defined(MBEDTLS_SSL_SESSION_TICKETS)
		if (ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
			// (Onyx) a TLS 1.3 ticket: the session to resume next time
			kapi_lock (sess_lock ());
			sess_save (s.host, &s.ssl);
			SessCacheEntry *e = sess_find (s.host);
			if (e != 0) e->trusted = s.trusted;
			kapi_unlock (sess_lock ());
			return 0;
		}
#endif
		return -1;	// PEER_CLOSE_NOTIFY, 0, or any error -> closed
	}

	inline void stop (Session &s)
	{
		mbedtls_ssl_close_notify (&s.ssl);
		mbedtls_ssl_free (&s.ssl);
		mbedtls_ssl_config_free (&s.conf);
		mbedtls_ctr_drbg_free (&s.drbg);
		mbedtls_entropy_free (&s.entropy);
	}
}

#endif // ONYX_TLS_HPP
