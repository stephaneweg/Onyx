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
//   * Certificate verification is still OFF (MBEDTLS_SSL_VERIFY_NONE) -- there is no CA
//     bundle on the system yet, so the server identity is NOT checked (open to MITM).
//     Ship a CA bundle on the SD card and switch to MBEDTLS_SSL_VERIFY_REQUIRED. This
//     is the remaining gap before HTTPS can be trusted.
//
// Built for mbedTLS 3.6.x configured bare-metal (no NET/FS/TIMING, TLS 1.2). See
// user/tls/README and the onyx_mbedtls_config.h that pins the configuration.
//
#ifndef ONYX_TLS_HPP
#define ONYX_TLS_HPP

#include "kapi.h"

#include <mbedtls/ssl.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>

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
		// stream desyncs ("unknown record type"). Big enough for one MSS (~1460).
		unsigned char             rxbuf[4096];
		int                       rxlen, rxpos;
	};

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
				if (kapi_get_ticks () - start_t > 20000) return MBEDTLS_ERR_NET_CONN_RESET;
				kapi_msleep (2);					// nothing yet -> yield
			}
		}
		int avail = s->rxlen - s->rxpos;
		int give = (int) len < avail ? (int) len : avail;
		for (int i = 0; i < give; i++) buf[i] = s->rxbuf[s->rxpos + i];
		s->rxpos += give;
		return give;
	}

	// ---- TLS session cache (resumption, network lever B) -------------------
	// Keyed by host. A NEW connection to a host we've already handshaked with can
	// RESUME -- skipping the full ECDHE + signature -- which is a big win for pages
	// that fetch many resources from the same host over separate connections (the
	// current per-resource-connection model). Per-app (one cache per linked app).
	enum { TLS_SESS_CACHE_N = 6 };
	struct SessCacheEntry { char host[128]; mbedtls_ssl_session sess; bool valid; };

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
		if (e == 0) {					// reuse host slot, else a free one, else evict #0
			for (int i = 0; i < TLS_SESS_CACHE_N; i++) if (!c[i].valid) { e = &c[i]; break; }
			if (e == 0) e = &c[0];
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

	// ---- session lifecycle -------------------------------------------------
	// Returns 0 on a completed handshake, -1 on any failure (caller closes the sock).
	inline int start (Session &s, int sock, const char *host)
	{
		s.sock = sock;
		s.rxlen = 0; s.rxpos = 0;		// reset the RX stream buffer
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

		mbedtls_ssl_conf_authmode (&s.conf, MBEDTLS_SSL_VERIFY_NONE);	// TODO: CA bundle
		mbedtls_ssl_conf_rng (&s.conf, mbedtls_ctr_drbg_random, &s.drbg);

		// The Pi 4's Cortex-A72 has NO ARMv8 crypto extensions, so AES-GCM is slow in
		// software (no hardware AES, and no PMULL for GHASH). Prefer ChaCha20-Poly1305 --
		// a pure-software AEAD that's fast without any crypto hardware -- and fall back to
		// AES-GCM only if the server doesn't offer ChaCha. (network lever 1)
		static const int s_ciphersuites[] = {
			MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
			MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
			MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
			MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
			MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
			MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
			0
		};
		mbedtls_ssl_conf_ciphersuites (&s.conf, s_ciphersuites);

		// Prefer X25519 for the ECDHE key exchange (fastest curve in pure software), then
		// secp256r1 as fallback. (network lever 3; MBEDTLS_HAVE_ASM -- the assembly bignum
		// -- stays enabled in the mbedTLS config for the rest of the handshake math.)
		static const uint16_t s_groups[] = {
			MBEDTLS_SSL_IANA_TLS_GROUP_X25519,
			MBEDTLS_SSL_IANA_TLS_GROUP_SECP256R1,
			0
		};
		mbedtls_ssl_conf_groups (&s.conf, s_groups);

		if (mbedtls_ssl_setup (&s.ssl, &s.conf) != 0) return -1;
		mbedtls_ssl_set_hostname (&s.ssl, host);			// SNI
		mbedtls_ssl_set_bio (&s.ssl, &s, bio_send, bio_recv, 0);	// ctx = Session* (buffered BIO)

		// Resume a cached session for this host if we have one (abbreviated handshake).
		{
			SessCacheEntry *resume = sess_find (host);
			if (resume != 0) mbedtls_ssl_set_session (&s.ssl, &resume->sess);
		}

		unsigned start_t = kapi_get_ticks ();
		int ret;
		while ((ret = mbedtls_ssl_handshake (&s.ssl)) != 0)
		{
			if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
			{
				if (kapi_get_ticks () - start_t > 20000) return -1;
				kapi_msleep (5);
				continue;
			}
			return -1;
		}
		sess_save (host, &s.ssl);		// cache the (resumable) session for next time
		return 0;
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
				if (kapi_get_ticks () - start_t > 15000) return -1;
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
