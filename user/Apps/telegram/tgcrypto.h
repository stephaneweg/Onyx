//
// tgcrypto.h -- the cryptography MTProto 2.0 needs, on mbedTLS (third_party/mbedtls-3.6.3: the
// library Onyx's TLS already links): SHA-1 / SHA-256, AES-256 in IGE mode, RSA_PAD (the auth key's
// exchange), the factoring of pq, the 2048-bit Diffie-Hellman, SRP (the two-step verification's
// password), and the random generator (CTR_DRBG seeded from an entropy pool: tg_entropy_*).
//
// The randomness: Onyx's kapi_random is a timer-seeded software generator (kernel/sys/kapi.cpp), not a
// strong source. So the pool (SHA-256) gathers more than it: kapi_random, the jitter of the CPU's
// counter around memory work, the time, every key and pointer event the app sees (tg_entropy_add),
// and a seed file kept on the card from one launch to the next (each launch writes a new one): the
// key exchange reseeds the generator from the pool first. Not a hardware RNG -- see docs/03.
//
// MIT licence.
//
#ifndef TG_CRYPTO_H
#define TG_CRYPTO_H

#include <stdint.h>
#include <string.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include <mbedtls/aes.h>
#include <mbedtls/bignum.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/md.h>

// From the platform (tgplat.h): raw entropy (the OS's), a fine counter (its jitter), the seed file.
int  tg_platform_entropy (unsigned char *buf, int n);
unsigned long long tg_platform_counter ();
bool tg_seed_load (unsigned char *buf, int n);
void tg_seed_save (const unsigned char *buf, int n);

namespace tgc { inline void random (void *out, int n); }

// (Onyx's mbedTLS is built with MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG: PSA's random bytes are the app's --
// PBKDF2's message digests go through PSA. Weak: onyx_tls.hpp has the same.)
#if defined(MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG)
#include <psa/crypto.h>
extern "C" __attribute__((weak)) psa_status_t mbedtls_psa_external_get_random (
	mbedtls_psa_external_random_context_t *ctx, uint8_t *out, size_t size, size_t *olen)
{
	(void) ctx;
	tgc::random (out, (int) size);
	*olen = size;
	return PSA_SUCCESS;
}
#endif

namespace tgc {

// ---- hashes ------------------------------------------------------------------------------------------

struct Part { const void *p; int n; };

inline void sha1 (unsigned char out[20], Part a, Part b = Part { 0, 0 }, Part c = Part { 0, 0 }, Part d = Part { 0, 0 })
{
	mbedtls_sha1_context x;
	mbedtls_sha1_init (&x);
	mbedtls_sha1_starts (&x);
	Part ps[4] = { a, b, c, d };
	for (int i = 0; i < 4; i++) if (ps[i].n > 0) mbedtls_sha1_update (&x, (const unsigned char *) ps[i].p, (size_t) ps[i].n);
	mbedtls_sha1_finish (&x, out);
	mbedtls_sha1_free (&x);
}

inline void sha256 (unsigned char out[32], Part a, Part b = Part { 0, 0 }, Part c = Part { 0, 0 }, Part d = Part { 0, 0 },
		    Part e = Part { 0, 0 }, Part f = Part { 0, 0 })
{
	mbedtls_sha256_context x;
	mbedtls_sha256_init (&x);
	mbedtls_sha256_starts (&x, 0);
	Part ps[6] = { a, b, c, d, e, f };
	for (int i = 0; i < 6; i++) if (ps[i].n > 0) mbedtls_sha256_update (&x, (const unsigned char *) ps[i].p, (size_t) ps[i].n);
	mbedtls_sha256_finish (&x, out);
	mbedtls_sha256_free (&x);
}

// ---- AES-256-IGE -------------------------------------------------------------------------------------
// iv: 32 bytes (the first 16 the previous ciphertext block's, the last 16 the previous plaintext's).
// n: a multiple of 16. in and out may be the same buffer. (bits: 128 for the tests' known vector)

inline void aes_ige (bool enc, const unsigned char key[32], const unsigned char iv[32], const unsigned char *in, unsigned char *out, int n, int bits = 256)
{
	mbedtls_aes_context a;
	mbedtls_aes_init (&a);
	if (enc) mbedtls_aes_setkey_enc (&a, key, (unsigned) bits); else mbedtls_aes_setkey_dec (&a, key, (unsigned) bits);
	unsigned char x[16], y[16], t[16], blk[16];
	// encrypt: c = E(p ^ c_prev) ^ p_prev;  decrypt: p = D(c ^ p_prev) ^ c_prev
	if (enc) { memcpy (x, iv, 16); memcpy (y, iv + 16, 16); }	// x = c_prev, y = p_prev
	else { memcpy (x, iv + 16, 16); memcpy (y, iv, 16); }	// x = p_prev, y = c_prev
	for (int o = 0; o + 16 <= n; o += 16)
	{
		memcpy (blk, in + o, 16);
		for (int i = 0; i < 16; i++) t[i] = blk[i] ^ x[i];
		mbedtls_aes_crypt_ecb (&a, enc ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT, t, t);
		for (int i = 0; i < 16; i++) t[i] ^= y[i];
		memcpy (out + o, t, 16);
		memcpy (x, t, 16);		// (the output: the next block's "previous" on its side)
		memcpy (y, blk, 16);
	}
	mbedtls_aes_free (&a);
}

// ---- randomness --------------------------------------------------------------------------------------

struct Rng
{
	mbedtls_sha256_context pool;
	mbedtls_ctr_drbg_context drbg;
	bool ready;
	unsigned long long last;
	Rng () : ready (false), last (0) {}
};
inline Rng &rng () { static Rng r; return r; }

// Something unpredictable (an event's time and value): mixed into the pool.
inline void entropy_add (const void *p, int n)
{
	Rng &r = rng ();
	if (!r.ready) return;
	unsigned long long c = tg_platform_counter ();
	mbedtls_sha256_update (&r.pool, (const unsigned char *) &c, sizeof c);
	if (n > 0) mbedtls_sha256_update (&r.pool, (const unsigned char *) p, (size_t) n);
}

// The CPU counter's jitter around some memory work (a few bits a sample, many samples).
inline void entropy_jitter (int samples)
{
	static unsigned char scratch[4096];
	unsigned long long prev = tg_platform_counter ();
	unsigned acc = 0;
	for (int s = 0; s < samples; s++)
	{
		for (int k = 0; k < 64; k++)
		{
			unsigned i = (acc * 2654435761u + (unsigned) k * 97u) & 4095u;
			scratch[i] = (unsigned char) (scratch[i] + acc + k);
			acc += scratch[(i * 7u) & 4095u];
		}
		unsigned long long c = tg_platform_counter ();
		unsigned d = (unsigned) (c - prev);
		prev = c;
		Rng &r = rng ();
		mbedtls_sha256_update (&r.pool, (const unsigned char *) &d, sizeof d);
	}
	mbedtls_sha256_update (&rng ().pool, (const unsigned char *) &acc, sizeof acc);
}

// The pool's digest so far (the pool goes on: its state is kept and the digest fed back into it).
inline void pool_out (unsigned char out[32])
{
	Rng &r = rng ();
	mbedtls_sha256_context c;
	mbedtls_sha256_init (&c);
	mbedtls_sha256_clone (&c, &r.pool);
	mbedtls_sha256_finish (&c, out);
	mbedtls_sha256_free (&c);
	mbedtls_sha256_update (&r.pool, out, 32);
}

inline int drbg_entropy (void *ctx, unsigned char *out, size_t n)
{
	(void) ctx;
	unsigned char d[32];
	for (size_t o = 0; o < n; o += 32)
	{
		entropy_jitter (64);
		unsigned char raw[32];
		int got = tg_platform_entropy (raw, 32);
		if (got > 0) mbedtls_sha256_update (&rng ().pool, raw, (size_t) got);
		pool_out (d);
		size_t m = n - o < 32 ? n - o : 32;
		memcpy (out + o, d, m);
	}
	return 0;
}

// Once, at start: the pool filled (the OS, the jitter, the seed file), the generator seeded, a
// new seed file written.
inline void rng_init ()
{
	Rng &r = rng ();
	if (r.ready) return;
	mbedtls_sha256_init (&r.pool);
	mbedtls_sha256_starts (&r.pool, 0);
	r.ready = true;
	unsigned char b[64];
	int n = tg_platform_entropy (b, sizeof b);
	if (n > 0) mbedtls_sha256_update (&r.pool, b, (size_t) n);
	if (tg_seed_load (b, 64)) mbedtls_sha256_update (&r.pool, b, 64);
	entropy_jitter (2048);
	mbedtls_ctr_drbg_init (&r.drbg);
	static const char pers[] = "Onyx Telegram";
	mbedtls_ctr_drbg_seed (&r.drbg, drbg_entropy, 0, (const unsigned char *) pers, sizeof pers - 1);
	mbedtls_ctr_drbg_random (&r.drbg, b, 64);
	tg_seed_save (b, 64);
}

// Before a secret is made (a DH exponent, SRP's a): the generator reseeded from the pool.
inline void reseed ()
{
	Rng &r = rng ();
	if (!r.ready) rng_init ();
	entropy_jitter (256);
	mbedtls_ctr_drbg_reseed (&r.drbg, 0, 0);
}

inline void random (void *out, int n)
{
	Rng &r = rng ();
	if (!r.ready) rng_init ();
	unsigned char *p = (unsigned char *) out;
	while (n > 0)
	{
		int m = n > 1024 ? 1024 : n;
		mbedtls_ctr_drbg_random (&r.drbg, p, (size_t) m);
		p += m; n -= m;
	}
}
inline long long random64 () { long long x; random (&x, 8); return x; }

// ---- big numbers -------------------------------------------------------------------------------------

struct Mpi
{
	mbedtls_mpi m;
	Mpi () { mbedtls_mpi_init (&m); }
	~Mpi () { mbedtls_mpi_free (&m); }
	Mpi (const void *be, int n) { mbedtls_mpi_init (&m); mbedtls_mpi_read_binary (&m, (const unsigned char *) be, (size_t) n); }
	bool hex (const char *h) { return mbedtls_mpi_read_string (&m, 16, h) == 0; }
	void set (const void *be, int n) { mbedtls_mpi_read_binary (&m, (const unsigned char *) be, (size_t) n); }
	void out (unsigned char *be, int n) const { mbedtls_mpi_write_binary (&m, be, (size_t) n); }	// left-padded
	int bytes () const { return (int) mbedtls_mpi_size (&m); }
private:
	Mpi (const Mpi &);
	Mpi &operator= (const Mpi &);
};

// x = a^e mod n
inline bool powmod (Mpi &x, const Mpi &a, const Mpi &e, const Mpi &n)
{
	return mbedtls_mpi_exp_mod (&x.m, &a.m, &e.m, &n.m, 0) == 0;
}

// ---- the server's RSA keys ---------------------------------------------------------------------------
// tdesktop's mtproto_dc_options.cpp (the production key and the test servers' key), the modulus in hex,
// e = 65537. The fingerprint is computed (SHA-1 of the TL strings n and e, its last 8 bytes).

struct RsaKey { const char *n_hex; long long fp; unsigned char n[256]; bool made; };

inline RsaKey *rsa_keys (int *count)
{
	static RsaKey k[2] = {
		{ "e8bb3305c0b52c6cf2afdf7637313489e63e05268e5badb601af417786472e5f93b85438968e20e6729a301c0afc121bf7151f834436f7fda680847a66bf64accec78ee21c0b316f0edafe2f41908da7bd1f4a5107638eeb67040ace472a14f90d9f7c2b7def99688ba3073adb5750bb02964902a359fe745d8170e36876d4fd8a5d41b2a76cbff9a13267eb9580b2d06d10357448d20d9da2191cb5d8c93982961cdfdeda629e37f1fb09a0722027696032fe61ed663db7a37f6f263d370f69db53a0dc0a1748bdaaff6209d5645485e6e001d1953255757e4b8e42813347b11da6ab500fd0ace7e6dfa3736199ccaf9397ed0745a427dcfa6cd67bcb1acff3", 0, { 0 }, false },
		{ "c8c11d635691fac091dd9489aedced2932aa8a0bcefef05fa800892d9b52ed03200865c9e97211cb2ee6c7ae96d3fb0e15aeffd66019b44a08a240cfdd2868a85e1f54d6fa5deaa041f6941ddf302690d61dc476385c2fa655142353cb4e4b59f6e5b6584db76fe8b1370263246c010c93d011014113ebdf987d093f9d37c2be48352d69a1683f8f6e6c2167983c761e3ab169fde5daaa12123fa1beab621e4da5935e9c198f82f35eae583a99386d8110ea6bd1abb0f568759f62694419ea5f69847c43462abef858b4cb5edc84e7b9226cd7bd7e183aa974a712c079dde85b9dc063b8a5c08e8f859c0ee5dcd824c7807f20153361a7f63cfd2a433a1be7f5", 0, { 0 }, false } };
	for (int i = 0; i < 2; i++)
	{
		if (k[i].made) continue;
		for (int j = 0; j < 256; j++)
		{
			const char *h = k[i].n_hex + j * 2;
			int hi = h[0] <= '9' ? h[0] - '0' : h[0] - 'a' + 10, lo = h[1] <= '9' ? h[1] - '0' : h[1] - 'a' + 10;
			k[i].n[j] = (unsigned char) (hi * 16 + lo);
		}
		unsigned char ser[264 + 4 + 4], dig[20];
		ser[0] = 254; ser[1] = 0; ser[2] = 1; ser[3] = 0;	// (TL string of 256 bytes)
		memcpy (ser + 4, k[i].n, 256);
		ser[260] = 3; ser[261] = 1; ser[262] = 0; ser[263] = 1;	// (TL string of 3 bytes: 01 00 01)
		sha1 (dig, Part { ser, 264 });
		long long fp = 0;
		for (int j = 0; j < 8; j++) fp |= (long long) dig[12 + j] << (8 * j);
		k[i].fp = fp;
		k[i].made = true;
	}
	*count = 2;
	return k;
}

// RSA_PAD (MTProto 2.0): data (up to 144 bytes) -> 256 bytes encrypted with the key.
inline bool rsa_pad (const unsigned char *data, int n, const RsaKey &key, unsigned char out[256])
{
	if (n > 144) return false;
	unsigned char padded[192], rev[192], temp[32], dh[224], enc[224], kae[256], h[32];
	memcpy (padded, data, (size_t) n);
	random (padded + n, 192 - n);
	for (int i = 0; i < 192; i++) rev[i] = padded[191 - i];
	Mpi mod (key.n, 256), e, m, r;
	mbedtls_mpi_lset (&e.m, 65537);
	for (int tries = 0; tries < 32; tries++)
	{
		random (temp, 32);
		memcpy (dh, rev, 192);
		sha256 (dh + 192, Part { temp, 32 }, Part { padded, 192 });
		static const unsigned char zero_iv[32] = { 0 };
		aes_ige (true, temp, zero_iv, dh, enc, 224);
		sha256 (h, Part { enc, 224 });
		for (int i = 0; i < 32; i++) kae[i] = temp[i] ^ h[i];
		memcpy (kae + 32, enc, 224);
		m.set (kae, 256);
		if (mbedtls_mpi_cmp_mpi (&m.m, &mod.m) >= 0) continue;
		if (!powmod (r, m, e, mod)) return false;
		r.out (out, 256);
		return true;
	}
	return false;
}

// ---- pq --------------------------------------------------------------------------------------------

inline uint64_t mulmod (uint64_t a, uint64_t b, uint64_t m) { return (uint64_t) ((unsigned __int128) a * b % m); }
inline uint64_t gcd (uint64_t a, uint64_t b) { while (b) { uint64_t t = a % b; a = b; b = t; } return a; }

// pq (a product of two primes, < 2^63) -> p < q. Pollard's rho, Brent's variant.
inline bool factor (uint64_t pq, uint64_t *p, uint64_t *q)
{
	if (pq < 4) return false;
	if (!(pq & 1)) { *p = 2; *q = pq / 2; return true; }
	for (uint64_t c = 1; c < 64; c++)
	{
		uint64_t y = 2 + c, x = y, g = 1, r = 1, qq = 1, ys = y;
		const uint64_t m = 128;
		while (g == 1)
		{
			x = y;
			for (uint64_t i = 0; i < r; i++) y = (mulmod (y, y, pq) + c) % pq;
			uint64_t k = 0;
			while (k < r && g == 1)
			{
				ys = y;
				for (uint64_t i = 0; i < m && i < r - k; i++)
				{
					y = (mulmod (y, y, pq) + c) % pq;
					qq = mulmod (qq, x > y ? x - y : y - x, pq);
				}
				g = gcd (qq, pq);
				k += m;
			}
			r *= 2;
			if (r > (1ull << 26)) break;
		}
		if (g == pq)
		{
			do { ys = (mulmod (ys, ys, pq) + c) % pq; g = gcd (x > ys ? x - ys : ys - x, pq); } while (g == 1);
		}
		if (g != 1 && g != pq)
		{
			uint64_t a = g, b = pq / g;
			*p = a < b ? a : b; *q = a < b ? b : a;
			return true;
		}
	}
	return false;
}

// ---- Diffie-Hellman checks ---------------------------------------------------------------------------

// The 2048-bit safe prime Telegram's servers send (checked by value; another one is tested for
// primality, slower, then kept for this run).
inline bool check_dh (const unsigned char *prime, int n, int g)
{
	static const char known[] =
		"c71caeb9c6b1c9048e6c522f70f13f73980d40238e3e21c14934d037563d930f48198a0aa7c14058229493d22530f4dbfa336f6e0ac925139543aed44cce7c3720fd51f69458705ac68cd4fe6b6b13abdc9746512969328454f18faf8c595f642477fe96bb2a941d5bcd1d4ac8cc49880708fa9b378e3c4f3a9060bee67cf9a4a4a695811051907e162753b56b0f6b410dba74d8a84b2a14b3144e0ef1284754fd17ed950d5965b4b9dd46582db1178d169c6bc465b0d6ff9ca3928fef5b9ae4e418fc15e83ebea0f87fa9ff5eed70050ded2849f47bf959d956850ce929851f0d8115f635b105ee2e4e15d04b2454bf6f4fadf034b10403119cd8e3b92fcc5b";
	static unsigned char checked[256];
	static bool have = false;
	if (n != 256 || g < 2 || g > 7) return false;
	if (have && !memcmp (checked, prime, 256)) return true;
	Mpi k, p (prime, n);
	k.hex (known);
	bool ok = mbedtls_mpi_cmp_mpi (&k.m, &p.m) == 0;
	if (!ok)
	{
		// p prime, (p - 1) / 2 prime (both probable), 2^2047 < p < 2^2048
		if (mbedtls_mpi_bitlen (&p.m) != 2048) return false;
		if (mbedtls_mpi_is_prime_ext (&p.m, 20, [] (void *, unsigned char *o, size_t l) { random (o, (int) l); return 0; }, 0) != 0) return false;
		Mpi h;
		mbedtls_mpi_copy (&h.m, &p.m);
		mbedtls_mpi_shift_r (&h.m, 1);
		if (mbedtls_mpi_is_prime_ext (&h.m, 20, [] (void *, unsigned char *o, size_t l) { random (o, (int) l); return 0; }, 0) != 0) return false;
		ok = true;
	}
	// g generates the subgroup of order (p - 1) / 2: p mod (4g)'s conditions (MTProto's list)
	mbedtls_mpi_uint r = 0;
	switch (g)
	{
	case 2: mbedtls_mpi_mod_int (&r, &p.m, 8); ok = ok && r == 7; break;
	case 3: mbedtls_mpi_mod_int (&r, &p.m, 3); ok = ok && r == 2; break;
	case 4: break;
	case 5: mbedtls_mpi_mod_int (&r, &p.m, 5); ok = ok && (r == 1 || r == 4); break;
	case 6: mbedtls_mpi_mod_int (&r, &p.m, 24); ok = ok && (r == 19 || r == 23); break;
	case 7: mbedtls_mpi_mod_int (&r, &p.m, 7); ok = ok && (r == 3 || r == 5 || r == 6); break;
	}
	if (ok) { memcpy (checked, prime, 256); have = true; }
	return ok;
}

// 1 < x < p - 1, and 2^{2048-64} <= x <= p - 2^{2048-64} (g_a, g_b)
inline bool check_g (const Mpi &x, const Mpi &p)
{
	Mpi lo, hi;
	mbedtls_mpi_lset (&lo.m, 1);
	mbedtls_mpi_shift_l (&lo.m, 2048 - 64);
	mbedtls_mpi_sub_mpi (&hi.m, &p.m, &lo.m);
	return mbedtls_mpi_cmp_mpi (&x.m, &lo.m) >= 0 && mbedtls_mpi_cmp_mpi (&x.m, &hi.m) <= 0;
}

// ---- SRP: the cloud password (account.password's current_algo: SHA256 SHA256 PBKDF2-HMAC-SHA512
// 100000 SHA256 ModPow) -> A and M1 for inputCheckPasswordSRP. False: the parameters are not safe.

inline void sh (unsigned char out[32], const void *d, int n, const void *salt, int sn)
{
	sha256 (out, Part { salt, sn }, Part { d, n }, Part { salt, sn });
}

// The password's x (the hash PH2), as both sides compute it.
inline void srp_ph2 (unsigned char x[32], const char *password, const unsigned char *salt1, int s1, const unsigned char *salt2, int s2)
{
	unsigned char a[32], b[32], pb[64];
	sh (a, password, (int) strlen (password), salt1, s1);
	sh (b, a, 32, salt2, s2);
	mbedtls_pkcs5_pbkdf2_hmac_ext (MBEDTLS_MD_SHA512, b, 32, salt1, (size_t) s1, 100000, 64, pb);
	sh (x, pb, 64, salt2, s2);
}

inline bool srp (const char *password, const unsigned char *salt1, int s1, const unsigned char *salt2, int s2,
		 int g, const unsigned char *p_be, int pn, const unsigned char *B_be, int bn,
		 unsigned char A_out[256], unsigned char M1[32])
{
	if (pn != 256 || !check_dh (p_be, pn, g)) return false;
	unsigned char xh[32];
	srp_ph2 (xh, password, salt1, s1, salt2, s2);
	Mpi p (p_be, 256), gg, x (xh, 32), B (B_be, bn), v, a, A, k, u, t, s, e, tmp;
	mbedtls_mpi_lset (&gg.m, g);
	if (mbedtls_mpi_cmp_int (&B.m, 0) <= 0 || mbedtls_mpi_cmp_mpi (&B.m, &p.m) >= 0) return false;
	unsigned char gpad[256], Bpad[256], Apad[256], h[32];
	gg.out (gpad, 256);
	B.out (Bpad, 256);
	powmod (v, gg, x, p);
	// k = H(p | g)
	sha256 (h, Part { p_be, 256 }, Part { gpad, 256 });
	k.set (h, 32);
	reseed ();
	for (int tries = 0; tries < 8; tries++)
	{
		unsigned char ab[256];
		random (ab, 256);
		a.set (ab, 256);
		powmod (A, gg, a, p);
		if (check_g (A, p)) break;
	}
	A.out (Apad, 256);
	sha256 (h, Part { Apad, 256 }, Part { Bpad, 256 });		// u = H(A | B)
	u.set (h, 32);
	if (mbedtls_mpi_cmp_int (&u.m, 0) == 0) return false;
	// t = (B - k v) mod p
	mbedtls_mpi_mul_mpi (&tmp.m, &k.m, &v.m);
	mbedtls_mpi_mod_mpi (&tmp.m, &tmp.m, &p.m);
	mbedtls_mpi_sub_mpi (&t.m, &B.m, &tmp.m);
	mbedtls_mpi_mod_mpi (&t.m, &t.m, &p.m);			// (mbedTLS: the result >= 0)
	// s = t ^ (a + u x) mod p
	mbedtls_mpi_mul_mpi (&e.m, &u.m, &x.m);
	mbedtls_mpi_add_mpi (&e.m, &e.m, &a.m);
	powmod (s, t, e, p);
	unsigned char spad[256], K[32], hp[32], hg[32], hs1[32], hs2[32], hx[32];
	s.out (spad, 256);
	sha256 (K, Part { spad, 256 });
	sha256 (hp, Part { p_be, 256 });
	sha256 (hg, Part { gpad, 256 });
	for (int i = 0; i < 32; i++) hx[i] = hp[i] ^ hg[i];
	sha256 (hs1, Part { salt1, s1 });
	sha256 (hs2, Part { salt2, s2 });
	sha256 (M1, Part { hx, 32 }, Part { hs1, 32 }, Part { hs2, 32 }, Part { Apad, 256 }, Part { Bpad, 256 }, Part { K, 32 });
	memcpy (A_out, Apad, 256);
	return true;
}

// A new password's verifier v = g^x mod p (256 bytes): account.updatePasswordSettings (the tests).
inline bool srp_verifier (const char *password, const unsigned char *salt1, int s1, const unsigned char *salt2, int s2,
			  int g, const unsigned char *p_be, unsigned char v_out[256])
{
	unsigned char xh[32];
	srp_ph2 (xh, password, salt1, s1, salt2, s2);
	Mpi p (p_be, 256), gg, x (xh, 32), v;
	mbedtls_mpi_lset (&gg.m, g);
	if (!powmod (v, gg, x, p)) return false;
	v.out (v_out, 256);
	return true;
}

} // namespace tgc

#endif
