/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 *
 * Onyx: Web Crypto, run by qjs_crypto.c with the natives N (mbedTLS underneath):
 *
 *  - crypto.getRandomValues (an integer typed array, 65536 bytes at most) and randomUUID
 *    from qjs_crypto.c's CTR-DRBG (the hardware RNG's entropy on the Pi);
 *  - crypto.subtle: digest, generateKey, importKey / exportKey (raw, jwk, spki, pkcs8),
 *    sign / verify, encrypt / decrypt, deriveBits / deriveKey, wrapKey / unwrapKey -- each a
 *    promise, rejected with the DOMException the standard names -- for SHA-1 / 256 / 384 /
 *    512, HMAC, AES-GCM / CBC / CTR / KW, ECDSA and ECDH (P-256 / P-384 / P-521),
 *    RSASSA-PKCS1-v1_5, RSA-PSS, RSA-OAEP, PBKDF2, HKDF;
 *  - CryptoKey (type, extractable, algorithm, usages) holding its key material in a
 *    WeakMap: a secret key's bytes, an asymmetric key's DER (PKCS#8 / SPKI), which the
 *    natives parse at each use.
 *
 * The same in a worker (qjs_worker_create runs it after net.js).
 */
(function (N) {
'use strict';

const G = globalThis;
const err = (name, message) => new G.DOMException(message, name);

/* ---- bytes ---- */
function isBufferSource(x) {
	return x instanceof ArrayBuffer || ArrayBuffer.isView(x);
}
/* a copy of a BufferSource's bytes (the standard: taken when the method is called) */
function bytes(x, what) {
	if (x instanceof ArrayBuffer)
		return new Uint8Array(x.slice(0));
	if (ArrayBuffer.isView(x))
		return new Uint8Array(x.buffer.slice(x.byteOffset, x.byteOffset + x.byteLength));
	throw new TypeError((what || 'data') + ': a BufferSource is expected');
}
const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';
function b64url(buf) {
	const b = new Uint8Array(buf);
	let s = '';
	for (let i = 0; i < b.length; i += 3) {
		const n = (b[i] << 16) | ((b[i + 1] || 0) << 8) | (b[i + 2] || 0);
		s += B64[n >> 18] + B64[(n >> 12) & 63];
		if (i + 1 < b.length) s += B64[(n >> 6) & 63];
		if (i + 2 < b.length) s += B64[n & 63];
	}
	return s;
}
function unb64url(s, what) {
	if (typeof s !== 'string' || !/^[A-Za-z0-9_-]*$/.test(s) || s.length % 4 === 1)
		throw err('DataError', 'the JWK member ' + what + ' is not base64url');
	const out = new Uint8Array(Math.floor(s.length * 3 / 4));
	let n = 0, bits = 0, o = 0;
	for (const c of s) {
		n = (n << 6) | B64.indexOf(c);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out[o++] = (n >> bits) & 255;
		}
	}
	return out;
}
function bigEndianNumber(u8) {
	let n = 0;
	for (const b of u8) n = n * 256 + b;
	return n;
}

/* ---- algorithms ---- */
const NAMES = ['SHA-1', 'SHA-256', 'SHA-384', 'SHA-512', 'HMAC', 'AES-GCM', 'AES-CBC', 'AES-CTR',
	'AES-KW', 'ECDSA', 'ECDH', 'RSASSA-PKCS1-v1_5', 'RSA-PSS', 'RSA-OAEP', 'PBKDF2', 'HKDF'];
const CANON = new Map(NAMES.map(n => [n.toUpperCase(), n]));
const OPS = {	/* the operations each algorithm has */
	digest: ['SHA-1', 'SHA-256', 'SHA-384', 'SHA-512'],
	generateKey: ['HMAC', 'AES-GCM', 'AES-CBC', 'AES-CTR', 'AES-KW', 'ECDSA', 'ECDH',
		'RSASSA-PKCS1-v1_5', 'RSA-PSS', 'RSA-OAEP'],
	importKey: ['HMAC', 'AES-GCM', 'AES-CBC', 'AES-CTR', 'AES-KW', 'ECDSA', 'ECDH',
		'RSASSA-PKCS1-v1_5', 'RSA-PSS', 'RSA-OAEP', 'PBKDF2', 'HKDF'],
	sign: ['HMAC', 'ECDSA', 'RSASSA-PKCS1-v1_5', 'RSA-PSS'],
	encrypt: ['AES-GCM', 'AES-CBC', 'AES-CTR', 'RSA-OAEP'],
	deriveBits: ['ECDH', 'PBKDF2', 'HKDF'],
	wrapKey: ['AES-GCM', 'AES-CBC', 'AES-CTR', 'AES-KW', 'RSA-OAEP'],
	get: ['HMAC', 'AES-GCM', 'AES-CBC', 'AES-CTR', 'AES-KW', 'PBKDF2', 'HKDF'],
};
OPS.verify = OPS.sign;
OPS.decrypt = OPS.encrypt;
OPS.unwrapKey = OPS.wrapKey;

/* the standard's "normalize an algorithm": a copy with the canonical name */
function normalize(op, alg) {
	if (typeof alg === 'string')
		alg = { name: alg };
	if (alg === null || typeof alg !== 'object')
		throw new TypeError('an algorithm is expected');
	if (alg.name === undefined)
		throw new TypeError('the algorithm has no name');
	const name = CANON.get(String(alg.name).toUpperCase());
	if (!name || !OPS[op].includes(name))
		throw err('NotSupportedError', 'the algorithm ' + alg.name + ' is not supported for ' + op);
	const a = Object.assign({}, alg, { name });
	if (a.hash !== undefined)
		a.hash = normalize('digest', a.hash);
	return a;
}
function need(a, member, type) {
	if (a[member] === undefined)
		throw new TypeError(a.name + ': the parameter ' + member + ' is required');
	if (type === 'bytes')
		return bytes(a[member], member);
	if (type === 'number') {
		const n = Number(a[member]);
		if (!Number.isFinite(n))
			throw new TypeError(a.name + ': ' + member + ' must be a number');
		return n;
	}
	return a[member];
}
const HASH_BITS = { 'SHA-1': 160, 'SHA-256': 256, 'SHA-384': 384, 'SHA-512': 512 };
const HASH_BLOCK = { 'SHA-1': 512, 'SHA-256': 512, 'SHA-384': 1024, 'SHA-512': 1024 };

/* ---- CryptoKey ---- */
const slots = new WeakMap();
class CryptoKey {
	constructor() { throw new TypeError('Illegal constructor'); }
	get type() { return slot(this).type; }
	get extractable() { return slot(this).extractable; }
	get algorithm() { return slot(this).algorithm; }
	get usages() { return slot(this).usages; }
}
Object.defineProperty(CryptoKey.prototype, Symbol.toStringTag, { value: 'CryptoKey', configurable: true });
function slot(k) {
	const s = slots.get(k);
	if (!s)
		throw new TypeError('a CryptoKey is expected');
	return s;
}
function makeKey(type, extractable, algorithm, usages, data) {
	const k = Object.create(CryptoKey.prototype);
	slots.set(k, { type, extractable: !!extractable, algorithm: Object.freeze(algorithm),
		usages: Object.freeze([...usages]), data });
	return k;
}
const USAGES = ['encrypt', 'decrypt', 'sign', 'verify', 'deriveKey', 'deriveBits', 'wrapKey', 'unwrapKey'];
function usageList(u) {
	if (u === null || typeof u !== 'object' || typeof u[Symbol.iterator] !== 'function')
		throw new TypeError('the key usages must be a sequence');
	const out = [];
	for (const x of u) {
		const s = String(x);
		if (!USAGES.includes(s))
			throw new TypeError('an unknown key usage: ' + s);
		if (!out.includes(s)) out.push(s);
	}
	return out;
}
function checkUsages(usages, allowed) {
	for (const u of usages)
		if (!allowed.includes(u))
			throw err('SyntaxError', 'the key usage ' + u + ' is not allowed here');
}
function useKey(key, name, usage) {
	const s = slot(key);
	if (s.algorithm.name !== name)
		throw err('InvalidAccessError', 'the key is not a ' + name + ' key');
	if (!s.usages.includes(usage))
		throw err('InvalidAccessError', 'the key\'s usages do not include ' + usage);
	return s;
}

/* what each kind of key may be used for */
function secretUsages(name) {
	return name === 'HMAC' ? ['sign', 'verify'] : name === 'AES-KW' ? ['wrapKey', 'unwrapKey'] :
		name === 'PBKDF2' || name === 'HKDF' ? ['deriveKey', 'deriveBits'] :
		['encrypt', 'decrypt', 'wrapKey', 'unwrapKey'];
}
function pairUsages(name) {	/* [private, public] */
	switch (name) {
	case 'ECDSA': case 'RSASSA-PKCS1-v1_5': case 'RSA-PSS': return [['sign'], ['verify']];
	case 'ECDH': return [['deriveKey', 'deriveBits'], []];
	default: return [['decrypt', 'unwrapKey'], ['encrypt', 'wrapKey']];	/* RSA-OAEP */
	}
}
function isRsa(name) { return name.startsWith('RSA'); }
function rsaAlgorithm(a, bits, e) {
	return { name: a.name, modulusLength: bits, publicExponent: new Uint8Array(e), hash: { name: a.hash.name } };
}

/* ---- key generation and import ---- */
function secretKey(a, raw, extractable, usages) {
	switch (a.name) {
	case 'HMAC': {
		const hash = need(a, 'hash');
		let length = raw.length * 8;
		if (a.length !== undefined) {
			length = Number(a.length);
			if (!(length > 0) || length > raw.length * 8 || length <= (raw.length - 1) * 8)
				throw err('DataError', 'HMAC: the length does not match the key');
		}
		if (length === 0)
			throw err('DataError', 'HMAC: an empty key');
		return makeKey('secret', extractable, { name: 'HMAC', hash: { name: hash.name }, length }, usages, raw);
	}
	case 'PBKDF2': case 'HKDF':
		if (extractable)
			throw err('SyntaxError', a.name + ' keys cannot be extractable');
		return makeKey('secret', false, { name: a.name }, usages, raw);
	default:
		if (![16, 24, 32].includes(raw.length))
			throw err('DataError', a.name + ': the key must be 128, 192 or 256 bits');
		return makeKey('secret', extractable, { name: a.name, length: raw.length * 8 }, usages, raw);
	}
}

function generateKey(a, extractable, usages) {
	if (a.name === 'HMAC') {
		const hash = need(a, 'hash');
		const length = a.length !== undefined ? Number(a.length) : HASH_BLOCK[hash.name];
		if (!(length > 0))
			throw err('OperationError', 'HMAC: an invalid length');
		checkUsages(usages, secretUsages('HMAC'));
		if (!usages.length) throw err('SyntaxError', 'a secret key needs usages');
		const raw = N.cryptoRandom(new Uint8Array(Math.ceil(length / 8)));
		if (length % 8) raw[raw.length - 1] &= 0xff << (8 - length % 8);
		return makeKey('secret', extractable, { name: 'HMAC', hash: { name: hash.name }, length }, usages, raw);
	}
	if (a.name.startsWith('AES')) {
		const length = need(a, 'length', 'number');
		if (![128, 192, 256].includes(length))
			throw err('OperationError', a.name + ': the length must be 128, 192 or 256');
		checkUsages(usages, secretUsages(a.name));
		if (!usages.length) throw err('SyntaxError', 'a secret key needs usages');
		return makeKey('secret', extractable, { name: a.name, length }, usages,
			N.cryptoRandom(new Uint8Array(length / 8)));
	}
	const [privU, pubU] = pairUsages(a.name);
	checkUsages(usages, privU.concat(pubU));
	let pair, algorithm;
	if (isRsa(a.name)) {
		const bits = need(a, 'modulusLength', 'number');
		const e = need(a, 'publicExponent', 'bytes');
		need(a, 'hash');
		pair = N.cryptoRsaGenerate(bits, bigEndianNumber(e));
		algorithm = rsaAlgorithm(a, bits, e);
	} else {
		const curve = need(a, 'namedCurve');
		pair = N.cryptoEcGenerate(String(curve));
		algorithm = { name: a.name, namedCurve: String(curve) };
	}
	const pu = usages.filter(u => privU.includes(u));
	if (!pu.length)
		throw err('SyntaxError', 'the private key would have no usages');
	return {
		publicKey: makeKey('public', true, Object.assign({}, algorithm, isRsa(a.name) ? { publicExponent: new Uint8Array(algorithm.publicExponent) } : {}),
			usages.filter(u => pubU.includes(u)), new Uint8Array(pair[1])),
		privateKey: makeKey('private', extractable, algorithm, pu, new Uint8Array(pair[0])),
	};
}

const JWK_ALG = {
	'AES-GCM': l => 'A' + l + 'GCM', 'AES-CBC': l => 'A' + l + 'CBC', 'AES-CTR': l => 'A' + l + 'CTR',
	'AES-KW': l => 'A' + l + 'KW',
};
function hmacJwkAlg(hash) { return { 'SHA-1': 'HS1', 'SHA-256': 'HS256', 'SHA-384': 'HS384', 'SHA-512': 'HS512' }[hash]; }
function rsaJwkAlg(name, hash) {
	const n = hash.slice(4);
	if (name === 'RSASSA-PKCS1-v1_5') return hash === 'SHA-1' ? 'RS1' : 'RS' + n;
	if (name === 'RSA-PSS') return hash === 'SHA-1' ? 'PS1' : 'PS' + n;
	return hash === 'SHA-1' ? 'RSA-OAEP' : 'RSA-OAEP-' + n;
}

function importKey(format, keyData, a, extractable, usages) {
	switch (format) {
	case 'raw': {
		const raw = bytes(keyData, 'keyData');
		if (a.name === 'ECDSA' || a.name === 'ECDH') {
			const curve = String(need(a, 'namedCurve'));
			checkUsages(usages, pairUsages(a.name)[1]);
			return makeKey('public', true, { name: a.name, namedCurve: curve }, usages,
				new Uint8Array(N.cryptoEcRawToSpki(curve, raw)));
		}
		if (isRsa(a.name))
			throw err('NotSupportedError', 'RSA keys have no raw format');
		checkUsages(usages, secretUsages(a.name));
		if (!usages.length) throw err('SyntaxError', 'a secret key needs usages');
		return secretKey(a, raw, extractable, usages);
	}
	case 'jwk': {
		const j = keyData;
		if (j === null || typeof j !== 'object' || isBufferSource(j))
			throw new TypeError('a JWK object is expected');
		if (j.ext === false && extractable)
			throw err('DataError', 'the JWK is not extractable');
		if (j.key_ops !== undefined) {
			if (!Array.isArray(j.key_ops)) throw err('DataError', 'key_ops is not an array');
			for (const u of usages)
				if (!j.key_ops.includes(u))
					throw err('DataError', 'the JWK\'s key_ops do not allow ' + u);
		}
		if (j.use !== undefined && usages.length) {
			const enc = ['encrypt', 'decrypt', 'wrapKey', 'unwrapKey'], sig = ['sign', 'verify'];
			if ((j.use === 'enc' && usages.some(u => !enc.includes(u) && u !== 'deriveKey' && u !== 'deriveBits')) ||
			    (j.use === 'sig' && usages.some(u => !sig.includes(u))))
				throw err('DataError', 'the JWK\'s use does not allow these usages');
		}
		if (j.kty === 'oct') {
			if (a.name === 'ECDSA' || a.name === 'ECDH' || isRsa(a.name))
				throw err('DataError', 'an oct JWK for an asymmetric algorithm');
			const raw = unb64url(j.k, 'k');
			if (j.alg !== undefined) {
				const want = a.name === 'HMAC' ? hmacJwkAlg(a.hash.name) :
					JWK_ALG[a.name] ? JWK_ALG[a.name](raw.length * 8) : undefined;
				if (want !== undefined && j.alg !== want)
					throw err('DataError', 'the JWK\'s alg ' + j.alg + ' does not match ' + a.name);
			}
			checkUsages(usages, secretUsages(a.name));
			if (!usages.length) throw err('SyntaxError', 'a secret key needs usages');
			return secretKey(a, raw, extractable, usages);
		}
		if (j.kty === 'EC') {
			if (a.name !== 'ECDSA' && a.name !== 'ECDH')
				throw err('DataError', 'an EC JWK for ' + a.name);
			const curve = String(need(a, 'namedCurve'));
			if (j.crv !== curve)
				throw err('DataError', 'the JWK\'s curve is not ' + curve);
			const priv = j.d !== undefined;
			const [privU, pubU] = pairUsages(a.name);
			checkUsages(usages, priv ? privU : pubU);
			if (priv && !usages.length) throw err('SyntaxError', 'a private key needs usages');
			const der = N.cryptoEcFromJwk(curve, unb64url(j.x, 'x'), unb64url(j.y, 'y'),
				priv ? unb64url(j.d, 'd') : null);
			return makeKey(priv ? 'private' : 'public', priv ? extractable : true,
				{ name: a.name, namedCurve: curve }, usages, new Uint8Array(der));
		}
		if (j.kty === 'RSA') {
			if (!isRsa(a.name))
				throw err('DataError', 'an RSA JWK for ' + a.name);
			need(a, 'hash');
			if (j.alg !== undefined && j.alg !== rsaJwkAlg(a.name, a.hash.name))
				throw err('DataError', 'the JWK\'s alg ' + j.alg + ' does not match');
			const priv = j.d !== undefined;
			const [privU, pubU] = pairUsages(a.name);
			checkUsages(usages, priv ? privU : pubU);
			if (priv && !usages.length) throw err('SyntaxError', 'a private key needs usages');
			const der = N.cryptoRsaFromJwk(unb64url(j.n, 'n'), unb64url(j.e, 'e'),
				priv ? unb64url(j.d, 'd') : null, priv && j.p !== undefined ? unb64url(j.p, 'p') : null,
				priv && j.q !== undefined ? unb64url(j.q, 'q') : null);
			const info = N.cryptoKeyInfo(der, priv);
			return makeKey(priv ? 'private' : 'public', priv ? extractable : true,
				rsaAlgorithm(a, info.bits, info.e), usages, new Uint8Array(der));
		}
		throw err('DataError', 'an unknown JWK kty: ' + j.kty);
	}
	case 'spki': case 'pkcs8': {
		const der = bytes(keyData, 'keyData');
		const priv = format === 'pkcs8';
		if (!isRsa(a.name) && a.name !== 'ECDSA' && a.name !== 'ECDH')
			throw err('NotSupportedError', a.name + ' keys have no ' + format + ' format');
		const info = N.cryptoKeyInfo(der, priv);
		const [privU, pubU] = pairUsages(a.name);
		checkUsages(usages, priv ? privU : pubU);
		if (priv && !usages.length) throw err('SyntaxError', 'a private key needs usages');
		let algorithm;
		if (isRsa(a.name)) {
			if (info.kind !== 'rsa') throw err('DataError', 'not an RSA key');
			need(a, 'hash');
			algorithm = rsaAlgorithm(a, info.bits, info.e);
		} else {
			if (info.kind !== 'ec') throw err('DataError', 'not an EC key');
			const curve = String(need(a, 'namedCurve'));
			if (info.curve !== curve) throw err('DataError', 'the key is not on ' + curve);
			algorithm = { name: a.name, namedCurve: curve };
		}
		return makeKey(priv ? 'private' : 'public', priv ? extractable : true, algorithm, usages, der);
	}
	default:
		throw new TypeError('an unknown key format: ' + format);
	}
}

function exportKey(format, key) {
	const s = slot(key);
	if (!s.extractable)
		throw err('InvalidAccessError', 'the key is not extractable');
	const name = s.algorithm.name;
	switch (format) {
	case 'raw':
		if (s.type === 'secret')
			return s.data.slice().buffer;
		if (s.type === 'public' && (name === 'ECDSA' || name === 'ECDH'))
			return N.cryptoEcToJwk(s.data, false).raw;
		throw err('InvalidAccessError', 'this key has no raw format');
	case 'spki':
		if (s.type !== 'public') throw err('InvalidAccessError', 'only a public key has an spki format');
		return s.data.slice().buffer;
	case 'pkcs8':
		if (s.type !== 'private') throw err('InvalidAccessError', 'only a private key has a pkcs8 format');
		return s.data.slice().buffer;
	case 'jwk': {
		let j;
		if (s.type === 'secret') {
			j = { kty: 'oct', k: b64url(s.data) };
			const alg = name === 'HMAC' ? hmacJwkAlg(s.algorithm.hash.name) :
				JWK_ALG[name] ? JWK_ALG[name](s.data.length * 8) : undefined;
			if (alg) j.alg = alg;
		} else if (name === 'ECDSA' || name === 'ECDH') {
			const c = N.cryptoEcToJwk(s.data, s.type === 'private');
			j = { kty: 'EC', crv: c.crv, x: b64url(c.x), y: b64url(c.y) };
			if (c.d) j.d = b64url(c.d);
		} else {
			const r = N.cryptoRsaToJwk(s.data, s.type === 'private');
			j = { kty: 'RSA', alg: rsaJwkAlg(name, s.algorithm.hash.name) };
			for (const m of ['n', 'e', 'd', 'p', 'q', 'dp', 'dq', 'qi'])
				if (r[m]) j[m] = b64url(r[m]);
		}
		j.key_ops = [...s.usages];
		j.ext = s.extractable;
		return j;
	}
	default:
		throw new TypeError('an unknown key format: ' + format);
	}
}

/* ---- the operations ---- */
function encrypt(enc, a, key, data) {
	const s = useKey(key, a.name, enc ? 'encrypt' : 'decrypt');
	return cipher(enc, a, s, data);
}
function cipher(enc, a, s, data) {
	switch (a.name) {
	case 'AES-GCM': {
		const iv = need(a, 'iv', 'bytes');
		const tag = a.tagLength === undefined ? 128 : Number(a.tagLength);
		if (![32, 64, 96, 104, 112, 120, 128].includes(tag))
			throw err('OperationError', 'AES-GCM: an invalid tagLength');
		const aad = a.additionalData !== undefined ? bytes(a.additionalData, 'additionalData') : null;
		return N.cryptoAesGcm(enc, s.data, iv, data, aad, tag / 8);
	}
	case 'AES-CBC':
		return N.cryptoAesCbc(enc, s.data, need(a, 'iv', 'bytes'), data);
	case 'AES-CTR': {
		const counter = need(a, 'counter', 'bytes');
		const length = need(a, 'length', 'number');
		if (counter.length !== 16 || !(length >= 1 && length <= 128))
			throw err('OperationError', 'AES-CTR: the counter must be 16 bytes and length 1 to 128');
		return N.cryptoAesCtr(s.data, counter, length, data);
	}
	case 'AES-KW':
		return N.cryptoAesKw(enc, s.data, data);
	case 'RSA-OAEP': {
		if (s.type !== (enc ? 'public' : 'private'))
			throw err('InvalidAccessError', 'RSA-OAEP: the wrong key type');
		const label = a.label !== undefined ? bytes(a.label, 'label') : null;
		return N.cryptoRsaOaep(enc, s.data, s.algorithm.hash.name, label, data);
	}
	}
	throw err('NotSupportedError', a.name + ' cannot encrypt');
}

function sign(a, key, data) {
	const s = useKey(key, a.name, 'sign');
	switch (a.name) {
	case 'HMAC':
		return N.cryptoHmac(s.algorithm.hash.name, s.data, data);
	case 'ECDSA':
		if (s.type !== 'private') throw err('InvalidAccessError', 'ECDSA signs with a private key');
		return N.cryptoEcdsaSign(s.data, need(a, 'hash').name, data);
	default:
		if (s.type !== 'private') throw err('InvalidAccessError', a.name + ' signs with a private key');
		return N.cryptoRsaSign(s.data, a.name === 'RSA-PSS', s.algorithm.hash.name,
			a.name === 'RSA-PSS' ? need(a, 'saltLength', 'number') : 0, data);
	}
}

function verify(a, key, sig, data) {
	const s = useKey(key, a.name, 'verify');
	switch (a.name) {
	case 'HMAC':
		return N.cryptoEqual(N.cryptoHmac(s.algorithm.hash.name, s.data, data), sig);
	case 'ECDSA':
		if (s.type !== 'public') throw err('InvalidAccessError', 'ECDSA verifies with a public key');
		return N.cryptoEcdsaVerify(s.data, need(a, 'hash').name, data, sig);
	default:
		if (s.type !== 'public') throw err('InvalidAccessError', a.name + ' verifies with a public key');
		return N.cryptoRsaVerify(s.data, a.name === 'RSA-PSS', s.algorithm.hash.name,
			a.name === 'RSA-PSS' ? need(a, 'saltLength', 'number') : 0, data, sig);
	}
}

function deriveBits(a, key, length, usage) {
	const s = useKey(key, a.name, usage);
	if (length !== null && length !== undefined) {
		length = Number(length);
		if (!(length >= 0) || length !== Math.floor(length))
			throw new TypeError('the length must be a number of bits');
	} else {
		length = null;
	}
	switch (a.name) {
	case 'ECDH': {
		const pub = need(a, 'public');
		const p = slot(pub);
		if (p.type !== 'public' || p.algorithm.name !== 'ECDH')
			throw err('InvalidAccessError', 'ECDH: the public member is not an ECDH public key');
		if (p.algorithm.namedCurve !== s.algorithm.namedCurve)
			throw err('InvalidAccessError', 'ECDH: the keys are on different curves');
		if (s.type !== 'private')
			throw err('InvalidAccessError', 'ECDH derives with a private key');
		const secret = new Uint8Array(N.cryptoEcdh(s.data, p.data));
		if (length === null)
			return secret.buffer;
		if (length > secret.length * 8)
			throw err('OperationError', 'ECDH: the length is longer than the secret');
		return truncate(secret, length);
	}
	case 'PBKDF2': {
		const salt = need(a, 'salt', 'bytes');
		const it = need(a, 'iterations', 'number');
		const hash = need(a, 'hash');
		if (length === null || length % 8)
			throw err('OperationError', 'PBKDF2: the length must be a multiple of 8');
		if (!(it > 0))
			throw err('OperationError', 'PBKDF2: iterations must be positive');
		if (length === 0) return new ArrayBuffer(0);
		return N.cryptoPbkdf2(hash.name, s.data, salt, it, length / 8);
	}
	case 'HKDF': {
		const salt = need(a, 'salt', 'bytes');
		const info = need(a, 'info', 'bytes');
		const hash = need(a, 'hash');
		if (length === null || length % 8)
			throw err('OperationError', 'HKDF: the length must be a multiple of 8');
		if (length === 0) return new ArrayBuffer(0);
		return N.cryptoHkdf(hash.name, s.data, salt, info, length / 8);
	}
	}
	throw err('NotSupportedError', a.name + ' cannot derive bits');
}
function truncate(u8, bits) {
	const out = u8.slice(0, Math.ceil(bits / 8));
	if (bits % 8) out[out.length - 1] &= 0xff << (8 - bits % 8);
	return out.buffer;
}

/* the length of the key deriveKey makes (the standard's "get key length") */
function keyLength(d) {
	switch (d.name) {
	case 'HMAC': {
		const hash = need(d, 'hash');
		return d.length !== undefined ? Number(d.length) : HASH_BLOCK[hash.name];
	}
	case 'PBKDF2': case 'HKDF':
		return null;
	default: {
		const l = need(d, 'length', 'number');
		if (![128, 192, 256].includes(l))
			throw err('OperationError', d.name + ': the length must be 128, 192 or 256');
		return l;
	}
	}
}

/* ---- SubtleCrypto and Crypto ---- */
/* each method: the arguments read and copied at once, the work in the promise */
function later(fn) {
	return new Promise(resolve => resolve(fn()));
}
function checkKey(k) { slot(k); return k; }

class SubtleCrypto {
	constructor() { throw new TypeError('Illegal constructor'); }
	digest(alg, data) {
		try {
			const a = normalize('digest', alg), d = bytes(data);
			return later(() => N.cryptoDigest(a.name, d));
		} catch (e) { return Promise.reject(e); }
	}
	generateKey(alg, extractable, usages) {
		try {
			const a = normalize('generateKey', alg), u = usageList(usages);
			return later(() => generateKey(a, extractable, u));
		} catch (e) { return Promise.reject(e); }
	}
	importKey(format, keyData, alg, extractable, usages) {
		try {
			const a = normalize('importKey', alg), u = usageList(usages);
			const kd = format === 'jwk' ? keyData : bytes(keyData, 'keyData');
			return later(() => importKey(format, kd, a, extractable, u));
		} catch (e) { return Promise.reject(e); }
	}
	exportKey(format, key) {
		try {
			checkKey(key);
			return later(() => exportKey(format, key));
		} catch (e) { return Promise.reject(e); }
	}
	sign(alg, key, data) {
		try {
			const a = normalize('sign', alg), d = bytes(data);
			checkKey(key);
			return later(() => sign(a, key, d));
		} catch (e) { return Promise.reject(e); }
	}
	verify(alg, key, signature, data) {
		try {
			const a = normalize('verify', alg), sg = bytes(signature, 'signature'), d = bytes(data);
			checkKey(key);
			return later(() => verify(a, key, sg, d));
		} catch (e) { return Promise.reject(e); }
	}
	encrypt(alg, key, data) {
		try {
			const a = normalize('encrypt', alg), d = bytes(data);
			checkKey(key);
			return later(() => encrypt(true, a, key, d));
		} catch (e) { return Promise.reject(e); }
	}
	decrypt(alg, key, data) {
		try {
			const a = normalize('decrypt', alg), d = bytes(data);
			checkKey(key);
			return later(() => encrypt(false, a, key, d));
		} catch (e) { return Promise.reject(e); }
	}
	deriveBits(alg, baseKey, length) {
		try {
			const a = normalize('deriveBits', alg);
			checkKey(baseKey);
			return later(() => deriveBits(a, baseKey, length, 'deriveBits'));
		} catch (e) { return Promise.reject(e); }
	}
	deriveKey(alg, baseKey, derivedKeyType, extractable, usages) {
		try {
			const a = normalize('deriveBits', alg);
			const d = normalize('get', derivedKeyType), di = normalize('importKey', derivedKeyType);
			const u = usageList(usages);
			checkKey(baseKey);
			return later(() => {
				const bits = deriveBits(a, baseKey, keyLength(d), 'deriveKey');
				return importKey('raw', new Uint8Array(bits), di, extractable, u);
			});
		} catch (e) { return Promise.reject(e); }
	}
	wrapKey(format, key, wrappingKey, wrapAlgorithm) {
		try {
			const a = normalize('wrapKey', wrapAlgorithm);
			checkKey(key);
			checkKey(wrappingKey);
			return later(() => {
				const s = useKey(wrappingKey, a.name, 'wrapKey');
				let data = exportKey(format, key);
				data = format === 'jwk' ? new TextEncoder().encode(JSON.stringify(data)) : new Uint8Array(data);
				return cipher(true, a, s, data);
			});
		} catch (e) { return Promise.reject(e); }
	}
	unwrapKey(format, wrappedKey, unwrappingKey, unwrapAlgorithm, unwrappedKeyAlgorithm, extractable, usages) {
		try {
			const a = normalize('unwrapKey', unwrapAlgorithm);
			const ia = normalize('importKey', unwrappedKeyAlgorithm);
			const w = bytes(wrappedKey, 'wrappedKey'), u = usageList(usages);
			checkKey(unwrappingKey);
			return later(() => {
				const s = useKey(unwrappingKey, a.name, 'unwrapKey');
				let key = new Uint8Array(cipher(false, a, s, w));
				if (format === 'jwk') {
					try {
						key = JSON.parse(new TextDecoder().decode(key));
					} catch (e) {
						throw err('DataError', 'the unwrapped key is not a JWK');
					}
				}
				return importKey(format, key, ia, extractable, u);
			});
		} catch (e) { return Promise.reject(e); }
	}
}
Object.defineProperty(SubtleCrypto.prototype, Symbol.toStringTag, { value: 'SubtleCrypto', configurable: true });

const INT_ARRAYS = [Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array, Int32Array,
	Uint32Array, BigInt64Array, BigUint64Array];
const subtle = Object.create(SubtleCrypto.prototype);
class Crypto {
	constructor() { throw new TypeError('Illegal constructor'); }
	get subtle() { return subtle; }
	getRandomValues(array) {
		if (!INT_ARRAYS.some(C => array instanceof C))
			throw err('TypeMismatchError', 'getRandomValues takes an integer typed array');
		if (array.byteLength > 65536)
			throw err('QuotaExceededError', 'getRandomValues: more than 65536 bytes asked');
		return N.cryptoRandom(array);
	}
	randomUUID() {
		const b = N.cryptoRandom(new Uint8Array(16));
		b[6] = (b[6] & 0x0f) | 0x40;
		b[8] = (b[8] & 0x3f) | 0x80;
		const h = [...b].map(x => x.toString(16).padStart(2, '0')).join('');
		return h.slice(0, 8) + '-' + h.slice(8, 12) + '-' + h.slice(12, 16) + '-' + h.slice(16, 20) + '-' + h.slice(20);
	}
}
Object.defineProperty(Crypto.prototype, Symbol.toStringTag, { value: 'Crypto', configurable: true });
const crypto = Object.create(Crypto.prototype);

for (const [k, v] of [['Crypto', Crypto], ['SubtleCrypto', SubtleCrypto], ['CryptoKey', CryptoKey]])
	Object.defineProperty(G, k, { value: v, writable: true, configurable: true });
Object.defineProperty(G, 'crypto', { get() { return crypto; }, configurable: true, enumerable: true });
})
