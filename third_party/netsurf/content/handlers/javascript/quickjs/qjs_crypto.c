/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Onyx: Web Crypto for the scripts, on mbedTLS (third_party/mbedtls-3.6.3, linked for TLS).
 *
 * crypto.js (compiled in as qjs_crypto_js.h) is the API -- Crypto (getRandomValues,
 * randomUUID), SubtleCrypto (its promises, the algorithms' parameters, the CryptoKey objects
 * and their usages, the key formats' JSON side) -- and these natives do the cryptography,
 * each on bytes (ArrayBuffers, typed arrays) and giving bytes back:
 *
 *  - the random bytes: a CTR-DRBG (AES-256) seeded from the best entropy there is -- the
 *    Pi's hardware RNG (kapi_random, as the TLS code), getrandom on the PC bench -- and
 *    reseeded by mbedTLS every 10000 requests;
 *  - digests (SHA-1 / 256 / 384 / 512), HMAC, AES-GCM / CBC / CTR / KW, PBKDF2, HKDF;
 *  - ECDSA and ECDH on P-256 / P-384 / P-521, RSASSA-PKCS1-v1_5, RSA-PSS and RSA-OAEP: a key
 *    is kept by crypto.js as its DER (PKCS#8 for a private key, SPKI for a public one) and
 *    parsed by mbedTLS at each use; the raw / JWK forms are made from and into DER here.
 *
 * A native's failure throws a DOMException of the standard's name (OperationError,
 * DataError...); crypto.js checks the parameters before.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "quickjs.h"

#include "javascript/quickjs/qjs_wasm.h"
#include "javascript/quickjs/qjs_net.h"	/* qjs_eval_cached */
#include "qjs_crypto_js.h"	/* crypto.js, as a C string (the build makes it) */

#include <mbedtls/md.h>
#include <mbedtls/aes.h>
#include <mbedtls/gcm.h>
#include <mbedtls/nist_kw.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/pk.h>
#include <mbedtls/ecp.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/rsa.h>
#include <mbedtls/asn1.h>
#include <mbedtls/asn1write.h>
#include <mbedtls/oid.h>
#include <mbedtls/constant_time.h>

#ifdef ONYX_HOST_SIM
#include <sys/random.h>
#else
#include "kapi.h"
#endif

static uint8_t *qc_bc;
static size_t qc_bc_len;

/** the largest DER this file writes (an RSA-8192 private key is ~4.7 KB) */
#define QC_DER_MAX 8192


/* ---- random bytes -------------------------------------------------------------------------- */

static mbedtls_entropy_context qc_entropy;
static mbedtls_ctr_drbg_context qc_drbg;
static int qc_seeded;	/* 0: not yet, 1: seeded, -1: failed */

/* the entropy: the Pi's hardware RNG (kapi_random, as user/tls/onyx_tls.hpp), getrandom on
 * the PC bench */
static int qc_entropy_source(void *data, unsigned char *out, size_t len, size_t *olen)
{
	(void) data;
#ifdef ONYX_HOST_SIM
	{
		ssize_t n = getrandom(out, len, 0);
		if (n <= 0) {
			*olen = 0;
			return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
		}
		*olen = (size_t) n;
	}
#else
	{
		int n = kapi_random(out, (unsigned) len);
		if (n <= 0) {
			*olen = 0;
			return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
		}
		*olen = (size_t) n;
	}
#endif
	return 0;
}

static bool qc_rng_ready(void)
{
	if (qc_seeded == 0) {
		mbedtls_entropy_init(&qc_entropy);
		mbedtls_ctr_drbg_init(&qc_drbg);
		qc_seeded = -1;
		if (mbedtls_entropy_add_source(&qc_entropy, qc_entropy_source, NULL, 32,
				MBEDTLS_ENTROPY_SOURCE_STRONG) == 0 &&
		    mbedtls_ctr_drbg_seed(&qc_drbg, mbedtls_entropy_func, &qc_entropy,
				(const unsigned char *) "onyx-webcrypto", 14) == 0)
			qc_seeded = 1;
	}
	return qc_seeded == 1;
}

static int qc_rng(void *p, unsigned char *out, size_t len)
{
	(void) p;
	if (!qc_rng_ready())
		return MBEDTLS_ERR_CTR_DRBG_ENTROPY_SOURCE_FAILED;
	/* (the DRBG gives 1 KB a request at most) */
	while (len > 0) {
		size_t n = len > MBEDTLS_CTR_DRBG_MAX_REQUEST ? MBEDTLS_CTR_DRBG_MAX_REQUEST : len;
		int r = mbedtls_ctr_drbg_random(&qc_drbg, out, n);
		if (r)
			return r;
		out += n;
		len -= n;
	}
	return 0;
}


/* ---- helpers ------------------------------------------------------------------------------- */

/** throws a DOMException named so (the global's; a TypeError without one) */
static JSValue qc_throw(JSContext *ctx, const char *name, const char *msg)
{
	JSValue g = JS_GetGlobalObject(ctx);
	JSValue dc = JS_GetPropertyStr(ctx, g, "DOMException");
	JSValue args[2], e;

	JS_FreeValue(ctx, g);
	if (!JS_IsFunction(ctx, dc)) {
		JS_FreeValue(ctx, dc);
		return JS_ThrowTypeError(ctx, "%s: %s", name, msg);
	}
	args[0] = JS_NewString(ctx, msg);
	args[1] = JS_NewString(ctx, name);
	e = JS_CallConstructor(ctx, dc, 2, (JSValueConst *) args);
	JS_FreeValue(ctx, args[0]);
	JS_FreeValue(ctx, args[1]);
	JS_FreeValue(ctx, dc);
	if (JS_IsException(e))
		return e;
	return JS_Throw(ctx, e);
}

#define qc_op_error(ctx, msg) qc_throw(ctx, "OperationError", msg)
#define qc_data_error(ctx, msg) qc_throw(ctx, "DataError", msg)

/** the bytes of an ArrayBuffer, a typed array or a DataView (NULL, len 0: none -- no
 * exception; JS_IsException via *bad) */
static const uint8_t *qc_bytes(JSContext *ctx, JSValueConst v, size_t *len, bool *bad)
{
	size_t off = 0, blen = 0, bpe, l = (size_t) -1;
	JSValue ab;
	uint8_t *p;

	*len = 0;
	*bad = false;
	if (JS_IsUndefined(v) || JS_IsNull(v))
		return NULL;
	if (JS_IsArrayBuffer(v)) {
		ab = JS_DupValue(ctx, v);
	} else if (JS_GetTypedArrayType(v) >= 0) {
		ab = JS_GetTypedArrayBuffer(ctx, v, &off, &l, &bpe);
		if (JS_IsException(ab)) {
			*bad = true;
			return NULL;
		}
	} else {
		JSValue x = JS_IsObject(v) ? JS_GetPropertyStr(ctx, v, "buffer") : JS_UNDEFINED;
		int64_t o = 0, n = 0;
		JSValue y;

		if (!JS_IsArrayBuffer(x)) {
			JS_FreeValue(ctx, x);
			JS_ThrowTypeError(ctx, "a BufferSource is expected");
			*bad = true;
			return NULL;
		}
		y = JS_GetPropertyStr(ctx, v, "byteOffset");
		JS_ToInt64(ctx, &o, y);
		JS_FreeValue(ctx, y);
		y = JS_GetPropertyStr(ctx, v, "byteLength");
		JS_ToInt64(ctx, &n, y);
		JS_FreeValue(ctx, y);
		ab = x;
		off = o > 0 ? (size_t) o : 0;
		l = n > 0 ? (size_t) n : 0;
	}
	p = JS_GetArrayBuffer(ctx, &blen, ab);
	JS_FreeValue(ctx, ab);	/* (the bytes stay: v holds its buffer) */
	if (p == NULL && JS_HasException(ctx)) {
		*bad = true;
		return NULL;
	}
	if (l == (size_t) -1)
		l = blen;
	if (off > blen || l > blen - off) {
		JS_ThrowTypeError(ctx, "the view is out of its buffer");
		*bad = true;
		return NULL;
	}
	*len = l;
	return p != NULL ? p + off : (const uint8_t *) "";
}

#define ARG_BYTES(i, p, n) \
	size_t n; \
	bool bad_##p; \
	const uint8_t *p = qc_bytes(ctx, argc > (i) ? argv[i] : JS_UNDEFINED, &n, &bad_##p); \
	if (bad_##p) \
		return JS_EXCEPTION

static const mbedtls_md_info_t *qc_md(JSContext *ctx, JSValueConst v)
{
	const char *s = JS_ToCString(ctx, v);
	mbedtls_md_type_t t = MBEDTLS_MD_NONE;

	if (s == NULL)
		return NULL;
	if (strcmp(s, "SHA-1") == 0)
		t = MBEDTLS_MD_SHA1;
	else if (strcmp(s, "SHA-256") == 0)
		t = MBEDTLS_MD_SHA256;
	else if (strcmp(s, "SHA-384") == 0)
		t = MBEDTLS_MD_SHA384;
	else if (strcmp(s, "SHA-512") == 0)
		t = MBEDTLS_MD_SHA512;
	JS_FreeCString(ctx, s);
	if (t == MBEDTLS_MD_NONE) {
		qc_throw(ctx, "NotSupportedError", "an unknown hash");
		return NULL;
	}
	return mbedtls_md_info_from_type(t);
}

static JSValue qc_ab(JSContext *ctx, const uint8_t *p, size_t n)
{
	return JS_NewArrayBufferCopy(ctx, p, n);
}


/* ---- random, digests, MACs, symmetric ciphers, KDFs ------------------------------------------ */

/* N.cryptoRandom(typedArray): its bytes filled; returns it */
static JSValue n_random(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	size_t n;
	bool bad;
	const uint8_t *p = qc_bytes(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &n, &bad);

	(void) this_val;
	if (bad)
		return JS_EXCEPTION;
	if (n > 0 && qc_rng(NULL, (uint8_t *) p, n) != 0)
		return qc_op_error(ctx, "no entropy source");
	return JS_DupValue(ctx, argv[0]);
}

/* N.cryptoDigest(hash, data) */
static JSValue n_digest(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const mbedtls_md_info_t *md = qc_md(ctx, argv[0]);
	uint8_t out[MBEDTLS_MD_MAX_SIZE];

	(void) this_val;
	if (md == NULL)
		return JS_EXCEPTION;
	{
		ARG_BYTES(1, p, n);
		if (mbedtls_md(md, p, n, out))
			return qc_op_error(ctx, "digest failed");
	}
	return qc_ab(ctx, out, mbedtls_md_get_size(md));
}

/* N.cryptoHmac(hash, key, data) */
static JSValue n_hmac(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const mbedtls_md_info_t *md = qc_md(ctx, argv[0]);
	uint8_t out[MBEDTLS_MD_MAX_SIZE];

	(void) this_val;
	if (md == NULL)
		return JS_EXCEPTION;
	{
		ARG_BYTES(1, k, kn);
		ARG_BYTES(2, p, n);
		if (mbedtls_md_hmac(md, k, kn, p, n, out))
			return qc_op_error(ctx, "HMAC failed");
	}
	return qc_ab(ctx, out, mbedtls_md_get_size(md));
}

/* N.cryptoEqual(a, b): constant-time comparison */
static JSValue n_equal(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	ARG_BYTES(0, a, an);
	ARG_BYTES(1, b, bn);

	(void) this_val;
	return JS_NewBool(ctx, an == bn && mbedtls_ct_memcmp(a, b, an) == 0);
}

/* N.cryptoAesGcm(encrypt, key, iv, data, aad, tagBytes) */
static JSValue n_aes_gcm(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int enc = JS_ToBool(ctx, argv[0]);
	int32_t tagn = 16;
	mbedtls_gcm_context g;
	uint8_t *out;
	JSValue r;
	int e;

	(void) this_val;
	JS_ToInt32(ctx, &tagn, argv[5]);
	{
		ARG_BYTES(1, k, kn);
		ARG_BYTES(2, iv, ivn);
		ARG_BYTES(3, p, n);
		ARG_BYTES(4, aad, aadn);

		if (tagn < 4 || tagn > 16)
			return qc_op_error(ctx, "invalid tag length");
		if (!enc && n < (size_t) tagn)
			return qc_op_error(ctx, "the data is shorter than its tag");
		out = malloc(n + 16 + 1);
		if (out == NULL)
			return JS_ThrowOutOfMemory(ctx);
		mbedtls_gcm_init(&g);
		e = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, k, (unsigned) kn * 8);
		if (!e && enc) {
			e = mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, n, iv, ivn, aad, aadn,
					p, out, (size_t) tagn, out + n);
			r = e ? JS_UNDEFINED : qc_ab(ctx, out, n + (size_t) tagn);
		} else if (!e) {
			size_t cn = n - (size_t) tagn;
			e = mbedtls_gcm_auth_decrypt(&g, cn, iv, ivn, aad, aadn, p + cn,
					(size_t) tagn, p, out);
			r = e ? JS_UNDEFINED : qc_ab(ctx, out, cn);
		}
		mbedtls_gcm_free(&g);
		free(out);
	}
	if (e)
		return qc_op_error(ctx, enc ? "AES-GCM encryption failed" :
				"AES-GCM decryption failed (the data or its tag is wrong)");
	return r;
}

/* N.cryptoAesCbc(encrypt, key, iv, data): PKCS#7 padding */
static JSValue n_aes_cbc(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int enc = JS_ToBool(ctx, argv[0]);
	mbedtls_aes_context a;
	uint8_t ivc[16], *buf;
	size_t outn;
	JSValue r = JS_UNDEFINED;
	int e;

	(void) this_val;
	{
		ARG_BYTES(1, k, kn);
		ARG_BYTES(2, iv, ivn);
		ARG_BYTES(3, p, n);

		if (ivn != 16)
			return qc_op_error(ctx, "the iv must be 16 bytes");
		if (!enc && (n == 0 || n % 16))
			return qc_op_error(ctx, "the data is not a whole number of blocks");
		outn = enc ? (n / 16 + 1) * 16 : n;
		buf = malloc(outn);
		if (buf == NULL)
			return JS_ThrowOutOfMemory(ctx);
		memcpy(ivc, iv, 16);
		mbedtls_aes_init(&a);
		if (enc) {
			uint8_t pad = (uint8_t) (outn - n);
			memcpy(buf, p, n);
			memset(buf + n, pad, pad);
			e = mbedtls_aes_setkey_enc(&a, k, (unsigned) kn * 8) ||
				mbedtls_aes_crypt_cbc(&a, MBEDTLS_AES_ENCRYPT, outn, ivc, buf, buf);
		} else {
			e = mbedtls_aes_setkey_dec(&a, k, (unsigned) kn * 8) ||
				mbedtls_aes_crypt_cbc(&a, MBEDTLS_AES_DECRYPT, n, ivc, p, buf);
			if (!e) {
				uint8_t pad = buf[n - 1];
				size_t i;
				if (pad == 0 || pad > 16)
					e = 1;
				for (i = 0; !e && i < pad; i++)
					if (buf[n - 1 - i] != pad)
						e = 1;
				outn = n - pad;
			}
		}
		mbedtls_aes_free(&a);
		if (!e)
			r = qc_ab(ctx, buf, outn);
		free(buf);
	}
	if (e)
		return qc_op_error(ctx, enc ? "AES-CBC encryption failed" :
				"AES-CBC decryption failed (bad padding)");
	return r;
}

/* N.cryptoAesCtr(key, counter, lengthBits, data): the counter's rightmost lengthBits bits
 * count the blocks (wrapping within them), as the standard says */
static JSValue n_aes_ctr(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	mbedtls_aes_context a;
	uint8_t ctr[16], ks[16], *buf;
	int32_t bits = 0;
	size_t i, j;
	JSValue r;
	int e;

	(void) this_val;
	JS_ToInt32(ctx, &bits, argv[2]);
	{
		ARG_BYTES(0, k, kn);
		ARG_BYTES(1, c, cn);
		ARG_BYTES(3, p, n);

		if (cn != 16 || bits < 1 || bits > 128)
			return qc_op_error(ctx, "invalid counter");
		if (bits < 64 && (uint64_t) ((n + 15) / 16) > ((uint64_t) 1 << bits))
			return qc_op_error(ctx, "the counter would repeat");
		buf = malloc(n + 1);
		if (buf == NULL)
			return JS_ThrowOutOfMemory(ctx);
		memcpy(ctr, c, 16);
		mbedtls_aes_init(&a);
		e = mbedtls_aes_setkey_enc(&a, k, (unsigned) kn * 8);
		for (i = 0; !e && i < n; i += 16) {
			e = mbedtls_aes_crypt_ecb(&a, MBEDTLS_AES_ENCRYPT, ctr, ks);
			for (j = 0; j < 16 && i + j < n; j++)
				buf[i + j] = p[i + j] ^ ks[j];
			/* the counter part: the low `bits` bits, incremented with a wrap */
			{
				int b = 15, left = bits, carry = 1;
				while (left > 0 && carry && b >= 0) {
					uint8_t mask = left >= 8 ? 0xff : (uint8_t) ((1u << left) - 1);
					uint8_t v = (uint8_t) ((ctr[b] & mask) + 1) & mask;
					carry = v == 0;
					ctr[b] = (uint8_t) ((ctr[b] & ~mask) | v);
					left -= 8;
					b--;
				}
			}
		}
		mbedtls_aes_free(&a);
		r = e ? JS_UNDEFINED : qc_ab(ctx, buf, n);
		free(buf);
	}
	if (e)
		return qc_op_error(ctx, "AES-CTR failed");
	return r;
}

/* N.cryptoAesKw(wrap, kek, data) */
static JSValue n_aes_kw(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int wrap = JS_ToBool(ctx, argv[0]);
	mbedtls_nist_kw_context kw;
	uint8_t *buf;
	size_t outn = 0;
	JSValue r = JS_UNDEFINED;
	int e;

	(void) this_val;
	{
		ARG_BYTES(1, k, kn);
		ARG_BYTES(2, p, n);

		if (n % 8 || n < (wrap ? 16u : 24u))
			return qc_op_error(ctx, "AES-KW needs a multiple of 8 bytes");
		buf = malloc(n + 8);
		if (buf == NULL)
			return JS_ThrowOutOfMemory(ctx);
		mbedtls_nist_kw_init(&kw);
		e = mbedtls_nist_kw_setkey(&kw, MBEDTLS_CIPHER_ID_AES, k, (unsigned) kn * 8, wrap);
		if (!e)
			e = wrap ? mbedtls_nist_kw_wrap(&kw, MBEDTLS_KW_MODE_KW, p, n, buf, &outn, n + 8) :
				mbedtls_nist_kw_unwrap(&kw, MBEDTLS_KW_MODE_KW, p, n, buf, &outn, n + 8);
		mbedtls_nist_kw_free(&kw);
		if (!e)
			r = qc_ab(ctx, buf, outn);
		free(buf);
	}
	if (e)
		return qc_op_error(ctx, wrap ? "AES-KW wrapping failed" :
				"AES-KW unwrapping failed (the data is not intact)");
	return r;
}

/* N.cryptoPbkdf2(hash, password, salt, iterations, bytes) */
static JSValue n_pbkdf2(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const mbedtls_md_info_t *md = qc_md(ctx, argv[0]);
	uint32_t iter = 0, outn = 0;
	uint8_t *out;
	JSValue r;
	int e;

	(void) this_val;
	if (md == NULL)
		return JS_EXCEPTION;
	JS_ToUint32(ctx, &iter, argv[3]);
	JS_ToUint32(ctx, &outn, argv[4]);
	{
		ARG_BYTES(1, pw, pwn);
		ARG_BYTES(2, salt, saltn);

		out = malloc(outn + 1);
		if (out == NULL)
			return JS_ThrowOutOfMemory(ctx);
		e = mbedtls_pkcs5_pbkdf2_hmac_ext(mbedtls_md_get_type(md), pw, pwn, salt, saltn,
				iter, outn, out);
		r = e ? JS_UNDEFINED : qc_ab(ctx, out, outn);
		free(out);
	}
	if (e)
		return qc_op_error(ctx, "PBKDF2 failed");
	return r;
}

/* N.cryptoHkdf(hash, key, salt, info, bytes) */
static JSValue n_hkdf(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const mbedtls_md_info_t *md = qc_md(ctx, argv[0]);
	uint32_t outn = 0;
	uint8_t *out;
	JSValue r;
	int e;

	(void) this_val;
	if (md == NULL)
		return JS_EXCEPTION;
	JS_ToUint32(ctx, &outn, argv[4]);
	{
		ARG_BYTES(1, k, kn);
		ARG_BYTES(2, salt, saltn);
		ARG_BYTES(3, info, infon);

		if (outn > 255u * mbedtls_md_get_size(md))
			return qc_op_error(ctx, "HKDF: the length is too large");
		out = malloc(outn + 1);
		if (out == NULL)
			return JS_ThrowOutOfMemory(ctx);
		e = mbedtls_hkdf(md, salt, saltn, k, kn, info, infon, out, outn);
		r = e ? JS_UNDEFINED : qc_ab(ctx, out, outn);
		free(out);
	}
	if (e)
		return qc_op_error(ctx, "HKDF failed");
	return r;
}


/* ---- keys: DER, PKCS#8, SPKI --------------------------------------------------------------- */

static mbedtls_ecp_group_id qc_curve(const char *name)
{
	if (strcmp(name, "P-256") == 0)
		return MBEDTLS_ECP_DP_SECP256R1;
	if (strcmp(name, "P-384") == 0)
		return MBEDTLS_ECP_DP_SECP384R1;
	if (strcmp(name, "P-521") == 0)
		return MBEDTLS_ECP_DP_SECP521R1;
	return MBEDTLS_ECP_DP_NONE;
}

static const char *qc_curve_name(mbedtls_ecp_group_id id)
{
	switch (id) {
	case MBEDTLS_ECP_DP_SECP256R1: return "P-256";
	case MBEDTLS_ECP_DP_SECP384R1: return "P-384";
	case MBEDTLS_ECP_DP_SECP521R1: return "P-521";
	default: return NULL;
	}
}

static mbedtls_ecp_group_id qc_arg_curve(JSContext *ctx, JSValueConst v)
{
	const char *s = JS_ToCString(ctx, v);
	mbedtls_ecp_group_id id;

	if (s == NULL)
		return MBEDTLS_ECP_DP_NONE;
	id = qc_curve(s);
	JS_FreeCString(ctx, s);
	if (id == MBEDTLS_ECP_DP_NONE)
		qc_throw(ctx, "NotSupportedError", "an unknown named curve");
	return id;
}

/** a parsed key (private: PKCS#8 / SEC1 / PKCS#1; else SPKI); 0: parsed */
static int qc_parse(mbedtls_pk_context *pk, const uint8_t *der, size_t n, bool priv)
{
	mbedtls_pk_init(pk);
	if (n == 0)
		return -1;
	if (priv)
		return mbedtls_pk_parse_key(pk, der, n, NULL, 0, qc_rng, NULL);
	return mbedtls_pk_parse_public_key(pk, der, n);
}

/** a private key as PKCS#8 (mbedTLS writes SEC1 / PKCS#1: wrapped here); the DER is at
 * the end of buf, its length returned (< 0: failed) */
static int qc_write_pkcs8(mbedtls_pk_context *pk, uint8_t *buf, size_t size)
{
	int inner = mbedtls_pk_write_key_der(pk, buf, size);
	unsigned char *c;
	const char *oid;
	size_t oidlen, len = 0;
	int r, par = 0;

	if (inner < 0)
		return inner;
	c = buf + size - inner;
	/* privateKey OCTET STRING { inner } */
	if ((r = mbedtls_asn1_write_len(&c, buf, (size_t) inner)) < 0)
		return r;
	len += (size_t) inner + (size_t) r;
	if ((r = mbedtls_asn1_write_tag(&c, buf, MBEDTLS_ASN1_OCTET_STRING)) < 0)
		return r;
	len += (size_t) r;
	/* AlgorithmIdentifier: rsaEncryption (NULL), id-ecPublicKey (the named curve) */
	if (mbedtls_pk_get_type(pk) == MBEDTLS_PK_RSA) {
		if (mbedtls_oid_get_oid_by_pk_alg(MBEDTLS_PK_RSA, &oid, &oidlen))
			return -1;
	} else {
		const char *coid;
		size_t coidlen;
		if (mbedtls_oid_get_oid_by_pk_alg(MBEDTLS_PK_ECKEY, &oid, &oidlen) ||
		    mbedtls_oid_get_oid_by_ec_grp(mbedtls_ecp_keypair_get_group_id(
				mbedtls_pk_ec(*pk)), &coid, &coidlen))
			return -1;
		if ((par = mbedtls_asn1_write_oid(&c, buf, coid, coidlen)) < 0)
			return par;
		len += (size_t) par;
	}
	if ((r = mbedtls_asn1_write_algorithm_identifier(&c, buf, oid, oidlen, (size_t) par)) < 0)
		return r;
	len += (size_t) r - (size_t) par;
	/* version 0 */
	if ((r = mbedtls_asn1_write_int(&c, buf, 0)) < 0)
		return r;
	len += (size_t) r;
	if ((r = mbedtls_asn1_write_len(&c, buf, len)) < 0)
		return r;
	len += (size_t) r;
	if ((r = mbedtls_asn1_write_tag(&c, buf, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE)) < 0)
		return r;
	len += (size_t) r;
	return (int) len;
}

/** [pkcs8, spki] of a key pair (ArrayBuffers) */
static JSValue qc_pair(JSContext *ctx, mbedtls_pk_context *pk)
{
	uint8_t *buf = malloc(QC_DER_MAX);
	JSValue a;
	int n;

	if (buf == NULL)
		return JS_ThrowOutOfMemory(ctx);
	a = JS_NewArray(ctx);
	n = qc_write_pkcs8(pk, buf, QC_DER_MAX);
	if (n < 0)
		goto fail;
	JS_SetPropertyUint32(ctx, a, 0, qc_ab(ctx, buf + QC_DER_MAX - n, (size_t) n));
	n = mbedtls_pk_write_pubkey_der(pk, buf, QC_DER_MAX);
	if (n < 0)
		goto fail;
	JS_SetPropertyUint32(ctx, a, 1, qc_ab(ctx, buf + QC_DER_MAX - n, (size_t) n));
	free(buf);
	return a;
fail:
	free(buf);
	JS_FreeValue(ctx, a);
	return qc_op_error(ctx, "the key could not be written");
}

/** the SPKI of a key (public, or the public part of a private one) */
static JSValue qc_spki(JSContext *ctx, mbedtls_pk_context *pk)
{
	uint8_t *buf = malloc(QC_DER_MAX);
	JSValue r;
	int n;

	if (buf == NULL)
		return JS_ThrowOutOfMemory(ctx);
	n = mbedtls_pk_write_pubkey_der(pk, buf, QC_DER_MAX);
	r = n < 0 ? qc_data_error(ctx, "the key could not be written") :
		qc_ab(ctx, buf + QC_DER_MAX - n, (size_t) n);
	free(buf);
	return r;
}

/* N.cryptoKeyInfo(der, private): { kind: 'ec' | 'rsa', curve, bits, e (bytes) }, the key
 * checked; a DataError if it is not one */
static JSValue n_key_info(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int priv = JS_ToBool(ctx, argv[1]);
	mbedtls_pk_context pk;
	JSValue o;
	int e;

	(void) this_val;
	{
		ARG_BYTES(0, der, n);
		e = qc_parse(&pk, der, n, priv);
	}
	if (e) {
		mbedtls_pk_free(&pk);
		return qc_data_error(ctx, priv ? "not a PKCS #8 private key" :
				"not a SubjectPublicKeyInfo");
	}
	o = JS_NewObject(ctx);
	if (mbedtls_pk_get_type(&pk) == MBEDTLS_PK_RSA) {
		mbedtls_rsa_context *rsa = mbedtls_pk_rsa(pk);
		mbedtls_mpi E;
		uint8_t eb[16];
		size_t en;

		mbedtls_mpi_init(&E);
		mbedtls_rsa_export(rsa, NULL, NULL, NULL, NULL, &E);
		en = mbedtls_mpi_size(&E);
		if (en > sizeof eb)
			en = sizeof eb;
		mbedtls_mpi_write_binary(&E, eb, en);
		mbedtls_mpi_free(&E);
		JS_SetPropertyStr(ctx, o, "kind", JS_NewString(ctx, "rsa"));
		JS_SetPropertyStr(ctx, o, "bits", JS_NewInt32(ctx, (int32_t) mbedtls_pk_get_bitlen(&pk)));
		JS_SetPropertyStr(ctx, o, "e", qc_ab(ctx, eb, en));
	} else if (mbedtls_pk_get_type(&pk) == MBEDTLS_PK_ECKEY ||
			mbedtls_pk_get_type(&pk) == MBEDTLS_PK_ECKEY_DH) {
		const char *cn = qc_curve_name(mbedtls_ecp_keypair_get_group_id(mbedtls_pk_ec(pk)));
		if (cn == NULL) {
			mbedtls_pk_free(&pk);
			JS_FreeValue(ctx, o);
			return qc_throw(ctx, "NotSupportedError", "an unknown named curve");
		}
		JS_SetPropertyStr(ctx, o, "kind", JS_NewString(ctx, "ec"));
		JS_SetPropertyStr(ctx, o, "curve", JS_NewString(ctx, cn));
	} else {
		JS_SetPropertyStr(ctx, o, "kind", JS_NewString(ctx, "?"));
	}
	/* a private key: its public part, as SPKI (the JWK and a generated pair need it) */
	if (priv)
		JS_SetPropertyStr(ctx, o, "spki", qc_spki(ctx, &pk));
	mbedtls_pk_free(&pk);
	return o;
}


/* ---- EC: generation, ECDSA, ECDH, the raw and JWK forms --------------------------------------- */

/* N.cryptoEcGenerate(curve): [pkcs8, spki] */
static JSValue n_ec_generate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	mbedtls_ecp_group_id id = qc_arg_curve(ctx, argv[0]);
	mbedtls_pk_context pk;
	JSValue r;

	(void) this_val;
	(void) argc;
	if (id == MBEDTLS_ECP_DP_NONE)
		return JS_EXCEPTION;
	mbedtls_pk_init(&pk);
	if (mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) ||
	    mbedtls_ecp_gen_key(id, mbedtls_pk_ec(pk), qc_rng, NULL)) {
		mbedtls_pk_free(&pk);
		return qc_op_error(ctx, "EC key generation failed");
	}
	r = qc_pair(ctx, &pk);
	mbedtls_pk_free(&pk);
	return r;
}

static size_t qc_curve_bytes(mbedtls_ecp_group_id id)
{
	return id == MBEDTLS_ECP_DP_SECP521R1 ? 66 : id == MBEDTLS_ECP_DP_SECP384R1 ? 48 : 32;
}

/* N.cryptoEcdsaSign(pkcs8, hash, data): the signature as r | s (IEEE P1363, as browsers) */
static JSValue n_ecdsa_sign(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const mbedtls_md_info_t *md = qc_md(ctx, argv[1]);
	uint8_t hash[MBEDTLS_MD_MAX_SIZE], sig[MBEDTLS_ECDSA_MAX_LEN], raw[2 * 66];
	mbedtls_pk_context pk;
	size_t slen = 0, cb;
	int e;

	(void) this_val;
	if (md == NULL)
		return JS_EXCEPTION;
	{
		ARG_BYTES(0, der, dn);
		ARG_BYTES(2, p, n);
		if (qc_parse(&pk, der, dn, true)) {
			mbedtls_pk_free(&pk);
			return qc_data_error(ctx, "the private key is not valid");
		}
		e = mbedtls_md(md, p, n, hash);
	}
	cb = qc_curve_bytes(mbedtls_ecp_keypair_get_group_id(mbedtls_pk_ec(pk)));
	if (!e)
		e = mbedtls_ecdsa_write_signature(mbedtls_pk_ec(pk), mbedtls_md_get_type(md), hash,
				mbedtls_md_get_size(md), sig, sizeof sig, &slen, qc_rng, NULL);
	mbedtls_pk_free(&pk);
	if (!e) {
		/* DER SEQUENCE { INTEGER r, INTEGER s } to r | s */
		unsigned char *c = sig, *end = sig + slen;
		size_t len;
		mbedtls_mpi r, s;

		mbedtls_mpi_init(&r);
		mbedtls_mpi_init(&s);
		e = mbedtls_asn1_get_tag(&c, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE) ||
			mbedtls_asn1_get_mpi(&c, end, &r) || mbedtls_asn1_get_mpi(&c, end, &s) ||
			mbedtls_mpi_write_binary(&r, raw, cb) || mbedtls_mpi_write_binary(&s, raw + cb, cb);
		mbedtls_mpi_free(&r);
		mbedtls_mpi_free(&s);
	}
	if (e)
		return qc_op_error(ctx, "ECDSA signing failed");
	return qc_ab(ctx, raw, 2 * cb);
}

/* N.cryptoEcdsaVerify(spki, hash, data, signature): true / false */
static JSValue n_ecdsa_verify(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const mbedtls_md_info_t *md = qc_md(ctx, argv[1]);
	uint8_t hash[MBEDTLS_MD_MAX_SIZE], der[MBEDTLS_ECDSA_MAX_LEN + 16];
	mbedtls_pk_context pk;
	size_t cb;
	int e, ok = 0;

	(void) this_val;
	if (md == NULL)
		return JS_EXCEPTION;
	{
		ARG_BYTES(0, key, kn);
		ARG_BYTES(2, p, n);
		ARG_BYTES(3, sig, sn);

		if (qc_parse(&pk, key, kn, false)) {
			mbedtls_pk_free(&pk);
			return qc_data_error(ctx, "the public key is not valid");
		}
		cb = qc_curve_bytes(mbedtls_ecp_keypair_get_group_id(mbedtls_pk_ec(pk)));
		e = mbedtls_md(md, p, n, hash);
		if (!e && sn == 2 * cb) {
			/* r | s to DER */
			mbedtls_mpi r, s;
			unsigned char *c = der + sizeof der;
			int len = 0, x;

			mbedtls_mpi_init(&r);
			mbedtls_mpi_init(&s);
			if (!mbedtls_mpi_read_binary(&r, sig, cb) &&
			    !mbedtls_mpi_read_binary(&s, sig + cb, cb) &&
			    (x = mbedtls_asn1_write_mpi(&c, der, &s)) > 0 && (len += x) &&
			    (x = mbedtls_asn1_write_mpi(&c, der, &r)) > 0 && (len += x) &&
			    (x = mbedtls_asn1_write_len(&c, der, (size_t) len)) > 0 && (len += x) &&
			    (x = mbedtls_asn1_write_tag(&c, der, MBEDTLS_ASN1_CONSTRUCTED |
					MBEDTLS_ASN1_SEQUENCE)) > 0 && (len += x))
				ok = mbedtls_ecdsa_read_signature(mbedtls_pk_ec(pk), hash,
						mbedtls_md_get_size(md), c, (size_t) len) == 0;
			mbedtls_mpi_free(&r);
			mbedtls_mpi_free(&s);
		}
	}
	mbedtls_pk_free(&pk);
	return JS_NewBool(ctx, ok);
}

/* N.cryptoEcdh(pkcs8, spki): the shared secret (the x coordinate, the curve's bytes) */
static JSValue n_ecdh(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	mbedtls_pk_context a, b;
	mbedtls_ecp_group g;
	mbedtls_mpi d, z, dd;
	mbedtls_ecp_point Q, QQ;
	uint8_t out[66];
	size_t cb = 0;
	int e;

	(void) this_val;
	{
		ARG_BYTES(0, priv, pn);
		ARG_BYTES(1, pub, qn);

		e = qc_parse(&a, priv, pn, true);
		if (!e)
			e = qc_parse(&b, pub, qn, false);
		else
			mbedtls_pk_init(&b);
	}
	if (e) {
		mbedtls_pk_free(&a);
		mbedtls_pk_free(&b);
		return qc_data_error(ctx, "the keys are not valid");
	}
	mbedtls_ecp_group_init(&g);
	mbedtls_mpi_init(&d);
	mbedtls_mpi_init(&z);
	mbedtls_mpi_init(&dd);
	mbedtls_ecp_point_init(&Q);
	mbedtls_ecp_point_init(&QQ);
	if (mbedtls_ecp_keypair_get_group_id(mbedtls_pk_ec(a)) !=
	    mbedtls_ecp_keypair_get_group_id(mbedtls_pk_ec(b))) {
		e = -2;
	} else {
		cb = qc_curve_bytes(mbedtls_ecp_keypair_get_group_id(mbedtls_pk_ec(a)));
		e = mbedtls_ecp_export(mbedtls_pk_ec(a), &g, &d, &QQ) ||
			mbedtls_ecp_export(mbedtls_pk_ec(b), &g, &dd, &Q) ||
			mbedtls_ecdh_compute_shared(&g, &z, &Q, &d, qc_rng, NULL) ||
			mbedtls_mpi_write_binary(&z, out, cb);
	}
	mbedtls_ecp_group_free(&g);
	mbedtls_mpi_free(&d);
	mbedtls_mpi_free(&z);
	mbedtls_mpi_free(&dd);
	mbedtls_ecp_point_free(&Q);
	mbedtls_ecp_point_free(&QQ);
	mbedtls_pk_free(&a);
	mbedtls_pk_free(&b);
	if (e == -2)
		return qc_throw(ctx, "InvalidAccessError", "the keys are on different curves");
	if (e)
		return qc_op_error(ctx, "ECDH failed");
	return qc_ab(ctx, out, cb);
}

/** a public key of a curve from its uncompressed point (0x04 | x | y); 0: done */
static int qc_ec_public(mbedtls_pk_context *pk, mbedtls_ecp_group_id id, const uint8_t *pt,
		size_t n)
{
	mbedtls_ecp_group g;
	mbedtls_ecp_point Q;
	int e;

	mbedtls_pk_init(pk);
	mbedtls_ecp_group_init(&g);
	mbedtls_ecp_point_init(&Q);
	e = mbedtls_pk_setup(pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) ||
		mbedtls_ecp_group_load(&g, id) ||
		mbedtls_ecp_point_read_binary(&g, &Q, pt, n) ||
		mbedtls_ecp_check_pubkey(&g, &Q) ||
		mbedtls_ecp_set_public_key(id, mbedtls_pk_ec(*pk), &Q);
	mbedtls_ecp_group_free(&g);
	mbedtls_ecp_point_free(&Q);
	return e;
}

/* N.cryptoEcRawToSpki(curve, point) */
static JSValue n_ec_raw_to_spki(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	mbedtls_ecp_group_id id = qc_arg_curve(ctx, argv[0]);
	mbedtls_pk_context pk;
	JSValue r;
	int e;

	(void) this_val;
	if (id == MBEDTLS_ECP_DP_NONE)
		return JS_EXCEPTION;
	{
		ARG_BYTES(1, pt, n);
		if (n != 1 + 2 * qc_curve_bytes(id) || pt[0] != 4)
			return qc_data_error(ctx, "not an uncompressed point of the curve");
		e = qc_ec_public(&pk, id, pt, n);
	}
	r = e ? qc_data_error(ctx, "not a point of the curve") : qc_spki(ctx, &pk);
	mbedtls_pk_free(&pk);
	return r;
}

/* N.cryptoEcToJwk(der, private): { crv, x, y, d } (bytes) */
static JSValue n_ec_to_jwk(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int priv = JS_ToBool(ctx, argv[1]);
	mbedtls_pk_context pk;
	uint8_t pt[1 + 2 * 66], d[66];
	size_t n = 0, cb;
	mbedtls_ecp_group_id id;
	JSValue o;
	int e;

	(void) this_val;
	{
		ARG_BYTES(0, der, dn);
		e = qc_parse(&pk, der, dn, priv);
	}
	if (e || (mbedtls_pk_get_type(&pk) != MBEDTLS_PK_ECKEY &&
			mbedtls_pk_get_type(&pk) != MBEDTLS_PK_ECKEY_DH)) {
		mbedtls_pk_free(&pk);
		return qc_data_error(ctx, "not an EC key");
	}
	id = mbedtls_ecp_keypair_get_group_id(mbedtls_pk_ec(pk));
	cb = qc_curve_bytes(id);
	e = mbedtls_ecp_write_public_key(mbedtls_pk_ec(pk), MBEDTLS_ECP_PF_UNCOMPRESSED, &n,
			pt, sizeof pt);
	if (!e && priv) {
		size_t dl = 0;
		uint8_t tmp[66];
		e = mbedtls_ecp_write_key_ext(mbedtls_pk_ec(pk), &dl, tmp, sizeof tmp);
		if (!e) {	/* (left-padded to the curve's size) */
			memset(d, 0, cb);
			memcpy(d + cb - dl, tmp, dl);
		}
	}
	mbedtls_pk_free(&pk);
	if (e || n != 1 + 2 * cb)
		return qc_op_error(ctx, "the key could not be written");
	o = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, o, "crv", JS_NewString(ctx, qc_curve_name(id)));
	JS_SetPropertyStr(ctx, o, "x", qc_ab(ctx, pt + 1, cb));
	JS_SetPropertyStr(ctx, o, "y", qc_ab(ctx, pt + 1 + cb, cb));
	if (priv)
		JS_SetPropertyStr(ctx, o, "d", qc_ab(ctx, d, cb));
	/* (the raw point: the raw export of a public key) */
	JS_SetPropertyStr(ctx, o, "raw", qc_ab(ctx, pt, n));
	return o;
}

/* N.cryptoEcFromJwk(curve, x, y, d | null): the DER (PKCS#8 with d, else SPKI) */
static JSValue n_ec_from_jwk(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	mbedtls_ecp_group_id id = qc_arg_curve(ctx, argv[0]);
	mbedtls_pk_context pk;
	uint8_t pt[1 + 2 * 66];
	size_t cb;
	JSValue r;
	int e;

	(void) this_val;
	if (id == MBEDTLS_ECP_DP_NONE)
		return JS_EXCEPTION;
	cb = qc_curve_bytes(id);
	{
		ARG_BYTES(1, x, xn);
		ARG_BYTES(2, y, yn);
		ARG_BYTES(3, d, dn);

		if (xn != cb || yn != cb || (d != NULL && dn != cb))
			return qc_data_error(ctx, "the JWK's coordinates are not of the curve's size");
		pt[0] = 4;
		memcpy(pt + 1, x, cb);
		memcpy(pt + 1 + cb, y, cb);
		e = qc_ec_public(&pk, id, pt, 1 + 2 * cb);
		if (!e && d != NULL) {
			/* the private scalar, checked against the public point */
			mbedtls_ecp_keypair kp;
			mbedtls_ecp_keypair_init(&kp);
			e = mbedtls_ecp_read_key(id, &kp, d, dn) ||
				mbedtls_ecp_keypair_calc_public(&kp, qc_rng, NULL) ||
				mbedtls_ecp_check_pub_priv(mbedtls_pk_ec(pk), &kp, qc_rng, NULL);
			if (!e) {
				mbedtls_pk_free(&pk);
				mbedtls_pk_init(&pk);
				e = mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) ||
					mbedtls_ecp_read_key(id, mbedtls_pk_ec(pk), d, dn) ||
					mbedtls_ecp_keypair_calc_public(mbedtls_pk_ec(pk), qc_rng, NULL);
			}
			mbedtls_ecp_keypair_free(&kp);
		}
		if (e) {
			mbedtls_pk_free(&pk);
			return qc_data_error(ctx, "the JWK is not a valid key of the curve");
		}
		if (d != NULL) {
			uint8_t *buf = malloc(QC_DER_MAX);
			int n = buf != NULL ? qc_write_pkcs8(&pk, buf, QC_DER_MAX) : -1;
			r = n < 0 ? qc_data_error(ctx, "the key could not be written") :
				qc_ab(ctx, buf + QC_DER_MAX - n, (size_t) n);
			free(buf);
		} else {
			r = qc_spki(ctx, &pk);
		}
	}
	mbedtls_pk_free(&pk);
	return r;
}


/* ---- RSA ------------------------------------------------------------------------------------- */

/* N.cryptoRsaGenerate(bits, exponent): [pkcs8, spki] */
static JSValue n_rsa_generate(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	mbedtls_pk_context pk;
	int32_t bits = 0;
	int64_t exp = 0;
	JSValue r;

	(void) this_val;
	(void) argc;
	JS_ToInt32(ctx, &bits, argv[0]);
	JS_ToInt64(ctx, &exp, argv[1]);
	if (bits < 1024 || bits > 8192 || bits % 8 || exp < 3 || !(exp & 1) || exp > 0x7fffffff)
		return qc_op_error(ctx, "unsupported RSA modulus length or public exponent");
	mbedtls_pk_init(&pk);
	if (mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)) ||
	    mbedtls_rsa_gen_key(mbedtls_pk_rsa(pk), qc_rng, NULL, (unsigned) bits, (int) exp)) {
		mbedtls_pk_free(&pk);
		return qc_op_error(ctx, "RSA key generation failed");
	}
	r = qc_pair(ctx, &pk);
	mbedtls_pk_free(&pk);
	return r;
}

static int qc_rsa_key(JSContext *ctx, mbedtls_pk_context *pk, JSValueConst v, bool priv)
{
	size_t n;
	bool bad;
	const uint8_t *der = qc_bytes(ctx, v, &n, &bad);

	if (bad)
		return -1;
	if (qc_parse(pk, der, n, priv) || mbedtls_pk_get_type(pk) != MBEDTLS_PK_RSA) {
		mbedtls_pk_free(pk);
		qc_data_error(ctx, "not an RSA key");
		return -1;
	}
	return 0;
}

/* N.cryptoRsaSign(pkcs8, pss, hash, saltLength, data) */
static JSValue n_rsa_sign(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int pss = JS_ToBool(ctx, argv[1]);
	const mbedtls_md_info_t *md = qc_md(ctx, argv[2]);
	uint8_t hash[MBEDTLS_MD_MAX_SIZE], *sig;
	int32_t salt = 0;
	mbedtls_pk_context pk;
	mbedtls_rsa_context *rsa;
	size_t klen;
	JSValue r;
	int e;

	(void) this_val;
	if (md == NULL)
		return JS_EXCEPTION;
	JS_ToInt32(ctx, &salt, argv[3]);
	if (qc_rsa_key(ctx, &pk, argv[0], true))
		return JS_EXCEPTION;
	rsa = mbedtls_pk_rsa(pk);
	klen = mbedtls_rsa_get_len(rsa);
	sig = malloc(klen);
	{
		ARG_BYTES(4, p, n);
		e = sig == NULL || mbedtls_md(md, p, n, hash);
	}
	if (!e && pss) {
		e = mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V21, mbedtls_md_get_type(md)) ||
			mbedtls_rsa_rsassa_pss_sign_ext(rsa, qc_rng, NULL, mbedtls_md_get_type(md),
				mbedtls_md_get_size(md), hash, salt, sig);
	} else if (!e) {
		e = mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE) ||
			mbedtls_rsa_rsassa_pkcs1_v15_sign(rsa, qc_rng, NULL, mbedtls_md_get_type(md),
				mbedtls_md_get_size(md), hash, sig);
	}
	r = e ? qc_op_error(ctx, "RSA signing failed") : qc_ab(ctx, sig, klen);
	free(sig);
	mbedtls_pk_free(&pk);
	return r;
}

/* N.cryptoRsaVerify(spki, pss, hash, saltLength, data, signature): true / false */
static JSValue n_rsa_verify(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int pss = JS_ToBool(ctx, argv[1]);
	const mbedtls_md_info_t *md = qc_md(ctx, argv[2]);
	uint8_t hash[MBEDTLS_MD_MAX_SIZE];
	int32_t salt = 0;
	mbedtls_pk_context pk;
	mbedtls_rsa_context *rsa;
	int ok = 0;

	(void) this_val;
	if (md == NULL)
		return JS_EXCEPTION;
	JS_ToInt32(ctx, &salt, argv[3]);
	if (qc_rsa_key(ctx, &pk, argv[0], false))
		return JS_EXCEPTION;
	rsa = mbedtls_pk_rsa(pk);
	{
		ARG_BYTES(4, p, n);
		ARG_BYTES(5, sig, sn);

		if (sn == mbedtls_rsa_get_len(rsa) && !mbedtls_md(md, p, n, hash)) {
			if (pss)
				ok = !mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V21,
						mbedtls_md_get_type(md)) &&
					!mbedtls_rsa_rsassa_pss_verify_ext(rsa, mbedtls_md_get_type(md),
						mbedtls_md_get_size(md), hash, mbedtls_md_get_type(md),
						salt, sig);
			else
				ok = !mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE) &&
					!mbedtls_rsa_rsassa_pkcs1_v15_verify(rsa, mbedtls_md_get_type(md),
						mbedtls_md_get_size(md), hash, sig);
		}
	}
	mbedtls_pk_free(&pk);
	return JS_NewBool(ctx, ok);
}

/* N.cryptoRsaOaep(encrypt, der, hash, label, data) */
static JSValue n_rsa_oaep(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int enc = JS_ToBool(ctx, argv[0]);
	const mbedtls_md_info_t *md = qc_md(ctx, argv[2]);
	mbedtls_pk_context pk;
	mbedtls_rsa_context *rsa;
	uint8_t *out;
	size_t klen, outn = 0;
	JSValue r;
	int e;

	(void) this_val;
	if (md == NULL)
		return JS_EXCEPTION;
	if (qc_rsa_key(ctx, &pk, argv[1], !enc))
		return JS_EXCEPTION;
	rsa = mbedtls_pk_rsa(pk);
	klen = mbedtls_rsa_get_len(rsa);
	out = malloc(klen);
	e = out == NULL || mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V21, mbedtls_md_get_type(md));
	{
		ARG_BYTES(3, label, ln);
		ARG_BYTES(4, p, n);

		if (!e && enc) {
			e = n > klen - 2 * mbedtls_md_get_size(md) - 2 ||
				mbedtls_rsa_rsaes_oaep_encrypt(rsa, qc_rng, NULL, label, ln, n, p, out);
			outn = klen;
		} else if (!e) {
			e = n != klen ||
				mbedtls_rsa_rsaes_oaep_decrypt(rsa, qc_rng, NULL, label, ln, &outn, p,
					out, klen);
		}
	}
	r = e ? qc_op_error(ctx, enc ? "RSA-OAEP encryption failed" : "RSA-OAEP decryption failed") :
		qc_ab(ctx, out, outn);
	free(out);
	mbedtls_pk_free(&pk);
	return r;
}

/* N.cryptoRsaToJwk(der, private): { n, e, d, p, q, dp, dq, qi } (bytes) */
static JSValue n_rsa_to_jwk(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int priv = JS_ToBool(ctx, argv[1]);
	static const char *const names[8] = { "n", "e", "d", "p", "q", "dp", "dq", "qi" };
	mbedtls_pk_context pk;
	mbedtls_rsa_context *rsa;
	mbedtls_mpi v[8];
	uint8_t *buf;
	JSValue o;
	int i, e;

	(void) this_val;
	(void) argc;
	if (qc_rsa_key(ctx, &pk, argv[0], priv))
		return JS_EXCEPTION;
	rsa = mbedtls_pk_rsa(pk);
	for (i = 0; i < 8; i++)
		mbedtls_mpi_init(&v[i]);
	/* (mbedtls_rsa_export's order: N, P, Q, D, E) */
	e = priv ? mbedtls_rsa_export(rsa, &v[0], &v[3], &v[4], &v[2], &v[1]) ||
			mbedtls_rsa_export_crt(rsa, &v[5], &v[6], &v[7]) :
		mbedtls_rsa_export(rsa, &v[0], NULL, NULL, NULL, &v[1]);
	o = e ? JS_UNDEFINED : JS_NewObject(ctx);
	buf = malloc(1024);
	for (i = 0; !e && i < (priv ? 8 : 2); i++) {
		size_t n = mbedtls_mpi_size(&v[i]);
		if (buf == NULL || n > 1024 || mbedtls_mpi_write_binary(&v[i], buf, n)) {
			e = 1;
			break;
		}
		JS_SetPropertyStr(ctx, o, names[i], qc_ab(ctx, buf, n));
	}
	free(buf);
	for (i = 0; i < 8; i++)
		mbedtls_mpi_free(&v[i]);
	mbedtls_pk_free(&pk);
	if (e) {
		JS_FreeValue(ctx, o);
		return qc_op_error(ctx, "the key could not be written");
	}
	return o;
}

/* N.cryptoRsaFromJwk(n, e, d, p, q): the DER (PKCS#8 with d, else SPKI), the key checked */
static JSValue n_rsa_from_jwk(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	mbedtls_pk_context pk;
	mbedtls_rsa_context *rsa;
	JSValue r;
	int e;

	(void) this_val;
	{
		ARG_BYTES(0, nb, nn);
		ARG_BYTES(1, eb, en);
		ARG_BYTES(2, db, dn);
		ARG_BYTES(3, pb, pn);
		ARG_BYTES(4, qb, qn);

		mbedtls_pk_init(&pk);
		e = nb == NULL || eb == NULL ||
			mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
		if (!e) {
			rsa = mbedtls_pk_rsa(pk);
			e = mbedtls_rsa_import_raw(rsa, nb, nn, pb, pb ? pn : 0, qb, qb ? qn : 0,
					db, db ? dn : 0, eb, en) ||
				mbedtls_rsa_complete(rsa) ||
				(db != NULL ? mbedtls_rsa_check_privkey(rsa) : mbedtls_rsa_check_pubkey(rsa));
		}
		if (e) {
			mbedtls_pk_free(&pk);
			return qc_data_error(ctx, "the JWK is not a valid RSA key");
		}
		if (db != NULL) {
			uint8_t *buf = malloc(QC_DER_MAX);
			int n = buf != NULL ? qc_write_pkcs8(&pk, buf, QC_DER_MAX) : -1;
			r = n < 0 ? qc_data_error(ctx, "the key could not be written") :
				qc_ab(ctx, buf + QC_DER_MAX - n, (size_t) n);
			free(buf);
		} else {
			r = qc_spki(ctx, &pk);
		}
	}
	mbedtls_pk_free(&pk);
	return r;
}


/* ---- setup ------------------------------------------------------------------------------------ */

static const JSCFunctionListEntry qc_natives[] = {
	JS_CFUNC_DEF("cryptoRandom", 1, n_random),
	JS_CFUNC_DEF("cryptoDigest", 2, n_digest),
	JS_CFUNC_DEF("cryptoHmac", 3, n_hmac),
	JS_CFUNC_DEF("cryptoEqual", 2, n_equal),
	JS_CFUNC_DEF("cryptoAesGcm", 6, n_aes_gcm),
	JS_CFUNC_DEF("cryptoAesCbc", 4, n_aes_cbc),
	JS_CFUNC_DEF("cryptoAesCtr", 4, n_aes_ctr),
	JS_CFUNC_DEF("cryptoAesKw", 3, n_aes_kw),
	JS_CFUNC_DEF("cryptoPbkdf2", 5, n_pbkdf2),
	JS_CFUNC_DEF("cryptoHkdf", 5, n_hkdf),
	JS_CFUNC_DEF("cryptoKeyInfo", 2, n_key_info),
	JS_CFUNC_DEF("cryptoEcGenerate", 1, n_ec_generate),
	JS_CFUNC_DEF("cryptoEcdsaSign", 3, n_ecdsa_sign),
	JS_CFUNC_DEF("cryptoEcdsaVerify", 4, n_ecdsa_verify),
	JS_CFUNC_DEF("cryptoEcdh", 2, n_ecdh),
	JS_CFUNC_DEF("cryptoEcRawToSpki", 2, n_ec_raw_to_spki),
	JS_CFUNC_DEF("cryptoEcToJwk", 2, n_ec_to_jwk),
	JS_CFUNC_DEF("cryptoEcFromJwk", 4, n_ec_from_jwk),
	JS_CFUNC_DEF("cryptoRsaGenerate", 2, n_rsa_generate),
	JS_CFUNC_DEF("cryptoRsaSign", 5, n_rsa_sign),
	JS_CFUNC_DEF("cryptoRsaVerify", 6, n_rsa_verify),
	JS_CFUNC_DEF("cryptoRsaOaep", 5, n_rsa_oaep),
	JS_CFUNC_DEF("cryptoRsaToJwk", 2, n_rsa_to_jwk),
	JS_CFUNC_DEF("cryptoRsaFromJwk", 5, n_rsa_from_jwk),
};

/* exported interface documented in qjs_wasm.h */
void qjs_crypto_setup(JSContext *ctx, JSValueConst natives)
{
	JSValue fn, r;

	JS_SetPropertyFunctionList(ctx, natives, qc_natives, sizeof qc_natives / sizeof qc_natives[0]);
	fn = qjs_eval_cached(ctx, qjs_crypto_js, sizeof(qjs_crypto_js) - 1, "crypto.js", &qc_bc,
			&qc_bc_len);
	if (JS_IsException(fn)) {
		JSValue e = JS_GetException(ctx);
		const char *msg = JS_ToCString(ctx, e);
		fprintf(stderr, "JS crypto.js: %s\n", msg ? msg : "?");
		JS_FreeCString(ctx, msg);
		JS_FreeValue(ctx, e);
		return;
	}
	r = JS_Call(ctx, fn, JS_UNDEFINED, 1, &natives);
	if (JS_IsException(r)) {
		JSValue e = JS_GetException(ctx);
		const char *msg = JS_ToCString(ctx, e);
		fprintf(stderr, "JS crypto.js setup: %s\n", msg ? msg : "?");
		JS_FreeCString(ctx, msg);
		JS_FreeValue(ctx, e);
	}
	JS_FreeValue(ctx, r);
	JS_FreeValue(ctx, fn);
}
