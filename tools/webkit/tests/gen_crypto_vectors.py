#!/usr/bin/env python3
# gen_crypto_vectors.py -- the test vectors of tools/webkit/tests/crypto1.html: each published vector is
# written here as published, CHECKED against OpenSSL (Python's "cryptography"), and the lot is
# written as JSON between the markers of crypto1.html. A vector that does not check stops the
# script: nothing unverified goes into the page. Needs Python's "cryptography"; the RSA-OAEP / PSS
# cross-check values are random, so they change at each run. Run by hand, not by test-webcrypto.sh.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see ../fetch.sh).
import base64, hashlib, hmac, json, re, sys, urllib.request
from cryptography.hazmat.primitives import hashes, padding as sympad, keywrap, serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa, padding, utils
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

PAGE = sys.argv[1]
H = lambda s: bytes.fromhex(re.sub(r"\s+", "", s))
hx = lambda b: b.hex()
b64u = lambda b: base64.urlsafe_b64encode(b).rstrip(b"=").decode()
unb64u = lambda s: base64.urlsafe_b64decode(s + "=" * (-len(s) % 4))
HASH = {"SHA-1": hashes.SHA1, "SHA-256": hashes.SHA256, "SHA-384": hashes.SHA384, "SHA-512": hashes.SHA512}
HL = {"SHA-1": "sha1", "SHA-256": "sha256", "SHA-384": "sha384", "SHA-512": "sha512"}
checked = 0
def ok(cond, what):
    global checked
    if not cond:
        sys.exit("VECTOR DOES NOT CHECK: " + what)
    checked += 1

V = {}

# ---- digests: FIPS 180-4's "abc" (NIST's example values)
V["digest"] = []
for h, d in [("SHA-1", "a9993e364706816aba3e25717850c26c9cd0d89d"),
             ("SHA-256", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
             ("SHA-384", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7"),
             ("SHA-512", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f")]:
    ok(hashlib.new(HL[h], b"abc").hexdigest() == d, "digest " + h)
    V["digest"].append({"name": "digest-" + h.lower(), "source": "FIPS 180-4 example, message \"abc\"", "hash": h, "data": hx(b"abc"), "digest": d})

# ---- HMAC: RFC 4231 (SHA-2) test cases 1, 2 and 6 (a key longer than the block); RFC 2202 (SHA-1) test case 1
V["hmac"] = []
def add_hmac(name, source, h, key, data, mac):
    ok(hmac.new(key, data, HL[h]).hexdigest() == mac, name)
    V["hmac"].append({"name": name, "source": source, "hash": h, "key": hx(key), "data": hx(data), "mac": mac})
k1, d1 = b"\x0b" * 20, b"Hi There"
add_hmac("hmac-sha1-rfc2202-1", "RFC 2202 section 3, test case 1", "SHA-1", k1, d1, "b617318655057264e28bc0b6fb378c8ef146be00")
add_hmac("hmac-sha256-rfc4231-1", "RFC 4231 section 4.2", "SHA-256", k1, d1, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7")
add_hmac("hmac-sha384-rfc4231-1", "RFC 4231 section 4.2", "SHA-384", k1, d1, "afd03944d84895626b0825f4ab46907f15f9dadbe4101ec682aa034c7cebc59cfaea9ea9076ede7f4af152e8b2fa9cb6")
add_hmac("hmac-sha512-rfc4231-1", "RFC 4231 section 4.2", "SHA-512", k1, d1, "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cdedaa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854")
k2, d2 = b"Jefe", b"what do ya want for nothing?"
add_hmac("hmac-sha256-rfc4231-2", "RFC 4231 section 4.3", "SHA-256", k2, d2, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843")
add_hmac("hmac-sha512-rfc4231-2", "RFC 4231 section 4.3", "SHA-512", k2, d2, "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737")
k6, d6 = b"\xaa" * 131, b"Test Using Larger Than Block-Size Key - Hash Key First"
add_hmac("hmac-sha256-rfc4231-6", "RFC 4231 section 4.7", "SHA-256", k6, d6, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54")

# ---- AES-CBC, AES-CTR, AES-CFB8: NIST SP 800-38A appendix F (the four-block example plain text)
K128 = H("2b7e151628aed2a6abf7158809cf4f3c")
K256 = H("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4")
IV = H("000102030405060708090a0b0c0d0e0f")
PT = H("6bc1bee22e409f96e93d7e117393172a ae2d8a571e03ac9c9eb76fac45af8e51 30c81c46a35ce411e5fbc1191a0a52ef f69f2445df4f9b17ad2b417be66c3710")
def cbc(key, iv, pt, pad):
    if pad:
        p = sympad.PKCS7(128).padder(); pt = p.update(pt) + p.finalize()
    e = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor(); return e.update(pt) + e.finalize()
V["aescbc"] = []
for name, src, key, ct in [
    ("aes-cbc-128-sp800-38a-f21", "NIST SP 800-38A F.2.1 (CBC-AES128.Encrypt)", K128,
     "7649abac8119b246cee98e9b12e9197d 5086cb9b507219ee95db113a917678b2 73bed6b8e3c1743b7116e69e22229516 3ff1caa1681fac09120eca307586e1a7"),
    ("aes-cbc-256-sp800-38a-f25", "NIST SP 800-38A F.2.5 (CBC-AES256.Encrypt)", K256,
     "f58c4c04d6e5f1ba779eabfb5f7bfbd6 9cfc4e967edb808d679f777bc6702c7d 39f23369a9d9bacfa530e26304231461 b2eb05e2c39be9fcda6c19078c6a9d1b")]:
    ct = H(ct)
    ok(cbc(key, IV, PT, False) == ct, name)
    full = cbc(key, IV, PT, True)  # WebCrypto pads (PKCS #7): one more block, which the vector does not have
    ok(full[:64] == ct and len(full) == 80, name + " padded")
    V["aescbc"].append({"name": name, "source": src, "key": hx(key), "iv": hx(IV), "plaintext": hx(PT), "ciphertext": hx(ct),
                        "paddingBlock": hx(full[64:]), "paddingBlockSource": "the PKCS #7 padding block WebCrypto adds: not in the vector, computed with OpenSSL"})

CTR0 = H("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff")
def ctr_webcrypto(key, counter, length, data):
    # the WebCrypto counter: the block's last `length` bits count, the others stay
    ecb = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
    c = int.from_bytes(counter, "big"); mask = (1 << length) - 1; out = b""
    for i in range(0, len(data), 16):
        block = ((c & ~mask) | ((c + i // 16) & mask)).to_bytes(16, "big")
        ks = ecb.update(block)
        out += bytes(a ^ b for a, b in zip(data[i:i + 16], ks))
    return out
V["aesctr"] = []
for name, src, key, ct in [
    ("aes-ctr-128-sp800-38a-f51", "NIST SP 800-38A F.5.1 (CTR-AES128.Encrypt)", K128,
     "874d6191b620e3261bef6864990db6ce 9806f66b7970fdff8617187bb9fffdff 5ae4df3edbd5d35e5b4f09020db03eab 1e031dda2fbe03d1792170a0f3009cee"),
    ("aes-ctr-256-sp800-38a-f55", "NIST SP 800-38A F.5.5 (CTR-AES256.Encrypt)", K256,
     "601ec313775789a5b7a7f504bbf3d228 f443e3ca4d62b59aca84e990cacaf5c5 2b0930daa23de94ce87017ba2d84988d dfc9c58db67aada613c2dd08457941a6")]:
    ct = H(ct)
    e = Cipher(algorithms.AES(key), modes.CTR(CTR0)).encryptor()
    ok(e.update(PT) + e.finalize() == ct, name)
    ok(ctr_webcrypto(key, CTR0, 64, PT) == ct, name + " model")
    V["aesctr"].append({"name": name, "source": src, "key": hx(key), "counter": hx(CTR0), "length": 64, "plaintext": hx(PT), "ciphertext": hx(ct)})
# the counter wrapping inside its `length` bits (no published vector: SP 800-38A's key and text, computed with OpenSSL's AES)
for name, length in [("aes-ctr-128-wrap-length4", 4), ("aes-ctr-128-wrap-length8", 8), ("aes-ctr-128-nowrap-length128", 128)]:
    data = PT + PT[:7]  # (and a last partial block)
    ct = ctr_webcrypto(K128, CTR0, length, data)
    full = Cipher(algorithms.AES(K128), modes.CTR(CTR0)).encryptor().update(data)
    ok((ct == full) == (length == 128), name + " differs from the 128-bit counter exactly when it wraps")
    V["aesctr"].append({"name": name, "source": "derived (no published vector): SP 800-38A's key and plain text, the counter ...ff wrapping within its last %d bits as the WebCrypto specification says; expected value computed block by block with OpenSSL's AES" % length,
                        "key": hx(K128), "counter": hx(CTR0), "length": length, "plaintext": hx(data), "ciphertext": hx(ct)})

V["aescfb8"] = []
pt8, ct8 = H("6bc1bee22e409f96e93d7e117393172aae2d"), H("3b79424c9c0dd436bace9e0ed4586a4f32b9")
e = Cipher(algorithms.AES(K128), modes.CFB8(IV)).encryptor()
ok(e.update(pt8) + e.finalize() == ct8, "cfb8")
V["aescfb8"].append({"name": "aes-cfb8-128-sp800-38a-f37", "source": "NIST SP 800-38A F.3.7 (CFB8-AES128.Encrypt)", "key": hx(K128), "iv": hx(IV), "plaintext": hx(pt8), "ciphertext": hx(ct8)})

# ---- AES-GCM: the GCM specification's test cases (McGrew & Viega, "The Galois/Counter Mode of Operation", appendix B)
V["aesgcm"] = []
GK128 = H("feffe9928665731c6d6a8f9467308308")
GK256 = GK128 + GK128
GIV = H("cafebabefacedbaddecaf888")
GP = H("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255")
GA = H("feedfacedeadbeeffeedfacedeadbeefabaddad2")
def add_gcm(name, tc, key, iv, pt, aad, ct, tag):
    ct, tag = H(ct), H(tag)
    ok(AESGCM(key).encrypt(iv, pt, aad or None) == ct + tag, name)
    V["aesgcm"].append({"name": name, "source": "GCM specification (McGrew & Viega), appendix B, test case %d" % tc,
                        "key": hx(key), "iv": hx(iv), "plaintext": hx(pt), "aad": hx(aad), "ciphertext": hx(ct), "tag": hx(tag)})
add_gcm("aes-gcm-128-tc1", 1, bytes(16), bytes(12), b"", b"", "", "58e2fccefa7e3061367f1d57a4e7455a")
add_gcm("aes-gcm-128-tc2", 2, bytes(16), bytes(12), bytes(16), b"", "0388dace60b6a392f328c2b971b2fe78", "ab6e47d42cec13bdf53a67b21257bddf")
add_gcm("aes-gcm-128-tc3", 3, GK128, GIV, GP, b"",
        "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091473f5985", "4d5c2af327cd64a62cf35abd2ba6fab4")
add_gcm("aes-gcm-128-tc4", 4, GK128, GIV, GP[:60], GA,
        "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091", "5bc94fbc3221a5db94fae95ae7121a47")
add_gcm("aes-gcm-128-tc5-iv8", 5, GK128, H("cafebabefacedbad"), GP[:60], GA,
        "61353b4c2806934a777ff51fa22a4755699b2a714fcdc6f83766e5f97b6c742373806900e49f24b22b097544d4896b424989b5e1ebac0f07c23f4598", "3612d2e79e3b0785561be14aaca2fccb")
add_gcm("aes-gcm-256-tc15", 15, GK256, GIV, GP, b"",
        "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662898015ad", "b094dac5d93471bdec1a502270e3cc6c")
add_gcm("aes-gcm-256-tc16", 16, GK256, GIV, GP[:60], GA,
        "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662", "76fc6ece0f4e1768cddf8853bb2d551b")

# ---- AES-KW: RFC 3394 section 4
V["aeskw"] = []
def add_kw(name, sec, kek, key, wrapped):
    kek, key, wrapped = H(kek), H(key), H(wrapped)
    ok(keywrap.aes_key_wrap(kek, key) == wrapped, name)
    V["aeskw"].append({"name": name, "source": "RFC 3394 section " + sec, "kek": hx(kek), "key": hx(key), "wrapped": hx(wrapped)})
add_kw("aes-kw-128-128-rfc3394-41", "4.1", "000102030405060708090a0b0c0d0e0f", "00112233445566778899aabbccddeeff", "1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5")
add_kw("aes-kw-192-128-rfc3394-42", "4.2", "000102030405060708090a0b0c0d0e0f1011121314151617", "00112233445566778899aabbccddeeff", "96778b25ae6ca435f92b5b97c050aed2468ab8a17ad84e5d")
add_kw("aes-kw-256-128-rfc3394-43", "4.3", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", "00112233445566778899aabbccddeeff", "64e8c3f9ce0f5ba263e9777905818a2a93c8191e7d6e8ae7")
add_kw("aes-kw-256-256-rfc3394-46", "4.6", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", "00112233445566778899aabbccddeeff000102030405060708090a0b0c0d0e0f",
       "28c9f404c4b810f4cbccb35cfb87f8263f5786e2d80ed326cbc7f0e71a99f43bfb988b9b7a02dd21")

# ---- PBKDF2: RFC 6070 (HMAC-SHA-1); RFC 7914 section 11 (HMAC-SHA-256)
V["pbkdf2"] = []
def add_pbkdf2(name, src, h, pw, salt, c, out):
    out = H(out)
    ok(hashlib.pbkdf2_hmac(HL[h], pw, salt, c, len(out)) == out, name)
    V["pbkdf2"].append({"name": name, "source": src, "hash": h, "password": hx(pw), "salt": hx(salt), "iterations": c, "output": hx(out)})
add_pbkdf2("pbkdf2-sha1-rfc6070-c1", "RFC 6070 section 2, first vector", "SHA-1", b"password", b"salt", 1, "0c60c80f961f0e71f3a9b524af6012062fe037a6")
add_pbkdf2("pbkdf2-sha1-rfc6070-c2", "RFC 6070 section 2, second vector", "SHA-1", b"password", b"salt", 2, "ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957")
add_pbkdf2("pbkdf2-sha1-rfc6070-c4096", "RFC 6070 section 2, third vector", "SHA-1", b"password", b"salt", 4096, "4b007901b765489abead49d926f721d065a429c1")
add_pbkdf2("pbkdf2-sha1-rfc6070-c4096-long", "RFC 6070 section 2, fifth vector (25 bytes)", "SHA-1", b"passwordPASSWORDpassword", b"saltSALTsaltSALTsaltSALTsaltSALTsalt", 4096,
           "3d2eec4fe41c849b80c8d83662c0e44a8b291a964cf2f07038")
add_pbkdf2("pbkdf2-sha256-rfc7914-c1", "RFC 7914 section 11, first vector", "SHA-256", b"passwd", b"salt", 1,
           "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783")

# ---- HKDF: RFC 5869 appendix A
V["hkdf"] = []
def add_hkdf(name, sec, h, ikm, salt, info, okm):
    ikm, salt, info, okm = H(ikm), H(salt), H(info), H(okm)
    ok(HKDF(HASH[h](), len(okm), salt, info).derive(ikm) == okm, name)
    V["hkdf"].append({"name": name, "source": "RFC 5869 appendix " + sec, "hash": h, "ikm": hx(ikm), "salt": hx(salt), "info": hx(info), "okm": hx(okm)})
add_hkdf("hkdf-sha256-rfc5869-a1", "A.1", "SHA-256", "0b" * 22, "000102030405060708090a0b0c", "f0f1f2f3f4f5f6f7f8f9",
         "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865")
add_hkdf("hkdf-sha256-rfc5869-a2", "A.2", "SHA-256", "".join("%02x" % i for i in range(0x00, 0x50)), "".join("%02x" % i for i in range(0x60, 0xb0)), "".join("%02x" % i for i in range(0xb0, 0x100)),
         "b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c59045a99cac7827271cb41c65e590e09da3275600c2f09b8367793a9aca3db71cc30c58179ec3e87c14c01d5c1f3434f1d87")
add_hkdf("hkdf-sha256-rfc5869-a3", "A.3", "SHA-256", "0b" * 22, "", "",
         "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8")
add_hkdf("hkdf-sha1-rfc5869-a4", "A.4", "SHA-1", "0b" * 11, "000102030405060708090a0b0c", "f0f1f2f3f4f5f6f7f8f9",
         "085a01ea1b10f36933068b56efa5ad81a4f14b822f5b091568a9cdd4f155fda2c22e422478d305f3f896")

# ---- ECDSA: RFC 6979 appendix A.2.5 (P-256), A.2.6 (P-384), A.2.7 (P-521): the key pairs, and the signatures of "sample"
CURVE = {"P-256": (ec.SECP256R1(), 32), "P-384": (ec.SECP384R1(), 48), "P-521": (ec.SECP521R1(), 66)}
def ec_key(curve, d, x, y):
    c, n = CURVE[curve]
    d, x, y = int(re.sub(r"\s+", "", d), 16), int(re.sub(r"\s+", "", x), 16), int(re.sub(r"\s+", "", y), 16)
    priv = ec.derive_private_key(d, c)
    pn = priv.public_key().public_numbers()
    ok(pn.x == x and pn.y == y, "EC public key of " + curve)
    raw = b"\x04" + x.to_bytes(n, "big") + y.to_bytes(n, "big")
    return priv, {"curve": curve, "d": b64u(d.to_bytes(n, "big")), "x": b64u(x.to_bytes(n, "big")), "y": b64u(y.to_bytes(n, "big")), "raw": hx(raw),
                  # the same key as OpenSSL writes it (DER): what the exports are compared with
                  "spki": hx(priv.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)),
                  "pkcs8": hx(priv.private_bytes(serialization.Encoding.DER, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))}
V["ecdsa"] = []
def add_ecdsa(name, sec, curve, h, d, x, y, r, s):
    c, n = CURVE[curve]
    priv, key = ec_key(curve, d, x, y)
    r, s = int(re.sub(r"\s+", "", r), 16), int(re.sub(r"\s+", "", s), 16)
    priv.public_key().verify(utils.encode_dss_signature(r, s), b"sample", ec.ECDSA(HASH[h]()))  # raises if wrong
    ok(True, name)
    V["ecdsa"].append({"name": name, "source": "RFC 6979 appendix " + sec + ", message \"sample\" (the signature is r || s, each as long as the curve's order)",
                       "hash": h, "key": key, "message": hx(b"sample"), "signature": hx(r.to_bytes(n, "big") + s.to_bytes(n, "big"))})
P256 = ("C9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721",
        "60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6", "7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299")
add_ecdsa("ecdsa-p256-sha256-rfc6979", "A.2.5", "P-256", "SHA-256", *P256,
          "EFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716", "F7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8")
add_ecdsa("ecdsa-p256-sha512-rfc6979", "A.2.5", "P-256", "SHA-512", *P256,
          "8496A60B5E9B47C825488827E0495B0E3FA109EC4568FD3F8D1097678EB97F00", "2362AB1ADBE2B8ADF9CB9EDAB740EA6049C028114F2460F96554F61FAE3302FE")
P384 = ("6B9D3DAD2E1B8C1C05B19875B6659F4DE23C3B667BF297BA9AA47740787137D8 96D5724E4C70A825F872C9EA60D2EDF5",
        "EC3A4E415B4E19A4568618029F427FA5DA9A8BC4AE92E02E06AAE5286B300C64 DEF8F0EA9055866064A254515480BC13",
        "8015D9B72D7D57244EA8EF9AC0C621896708A59367F9DFB9F54CA84B3F1C9DB1 288B231C3AE0D4FE7344FD2533264720")
add_ecdsa("ecdsa-p384-sha384-rfc6979", "A.2.6", "P-384", "SHA-384", *P384,
          "94EDBB92A5ECB8AAD4736E56C691916B3F88140666CE9FA73D64C4EA95AD133C 81A648152E44ACF96E36DD1E80FABE46",
          "99EF4AEB15F178CEA1FE40DB2603138F130E740A19624526203B6351D0A3A94F A329C145786E679E7B82C71A38628AC8")
add_ecdsa("ecdsa-p384-sha256-rfc6979", "A.2.6", "P-384", "SHA-256", *P384,
          "21B13D1E013C7FA1392D03C5F99AF8B30C570C6F98D4EA8E354B63A21D3DAA33 BDE1E888E63355D92FA2B3C36D8FB2CD",
          "F3AA443FB107745BF4BD77CB3891674632068A10CA67E3D45DB2266FA7D1FEEB EFDC63ECCD1AC42EC0CB8668A4FA0AB0")
P521 = ("0FAD06DAA62BA3B25D2FB40133DA757205DE67F5BB0018FEE8C86E1B68C7E75C AA896EB32F1F47C70855836A6D16FCC1466F6D8FBEC67DB89EC0C08B0E996B83 538",
        "1894550D0785932E00EAA23B694F213F8C3121F86DC97A04E5A7167DB4E5BCD3 71123D46E45DB6B5D5370A7F20FB633155D38FFA16D2BD761DCAC474B9A2F502 3A4",
        "0493101C962CD4D2FDDF782285E64584139C2F91B47F87FF82354D6630F746A2 8A0DB25741B5B34A828008B22ACC23F924FAAFBD4D33F81EA66956DFEAA2BFDF CF5")
add_ecdsa("ecdsa-p521-sha512-rfc6979", "A.2.7", "P-521", "SHA-512", *P521,
          "0C328FAFCBD79DD77850370C46325D987CB525569FB63C5D3BC53950E6D4C5F1 74E25A1EE9017B5D450606ADD152B534931D7D4E8455CC91F9B15BF05EC36E37 7FA",
          "0617CCE7CF5064806C467F678D3B4080D6F1CC50AF26CA209417308281B68AF2 82623EAA63E5B5C0723D8B8C37FF0777B1A20F8CCB1DCCC43997F1EE0E44DA4A 67A")
add_ecdsa("ecdsa-p521-sha256-rfc6979", "A.2.7", "P-521", "SHA-256", *P521,
          "1511BB4D675114FE266FC4372B87682BAECC01D3CC62CF2303C92B3526012659 D16876E25C7C1E57648F23B73564D67F61C6F14D527D54972810421E7D87589E 1A7",
          "04A171143A83163D6DF460AAF61522695F207A58B95C0644D87E52AA1A347916 E4F7A72930B1BC06DBE22CE3F58264AFD23704CBB63B29B931F7DE6C9D949A7E CFC")

# ---- ECDH: RFC 5903 section 8 (the initiator's and the responder's key pairs, the shared secret g^ir's x)
V["ecdh"] = []
def add_ecdh(name, sec, curve, i, gix, giy, r, grx, gry, girx):
    c, n = CURVE[curve]
    ipriv, ikey = ec_key(curve, i, gix, giy)
    rpriv, rkey = ec_key(curve, r, grx, gry)
    secret = int(re.sub(r"\s+", "", girx), 16).to_bytes(n, "big")
    ok(ipriv.exchange(ec.ECDH(), rpriv.public_key()) == secret and rpriv.exchange(ec.ECDH(), ipriv.public_key()) == secret, name)
    V["ecdh"].append({"name": name, "source": "RFC 5903 section " + sec, "initiator": ikey, "responder": rkey, "secret": hx(secret)})
add_ecdh("ecdh-p256-rfc5903", "8.1", "P-256",
         "C88F01F5 10D9AC3F 70A292DA A2316DE5 44E9AAB8 AFE84049 C62A9C57 862D1433",
         "DAD0B653 94221CF9 B051E1FE CA5787D0 98DFE637 FC90B9EF 945D0C37 72581180",
         "5271A046 1CDB8252 D61F1C45 6FA3E59A B1F45B33 ACCF5F58 389E0577 B8990BB3",
         "C6EF9C5D 78AE012A 011164AC B397CE20 88685D8F 06BF9BE0 B283AB46 476BEE53",
         "D12DFB52 89C8D4F8 1208B702 70398C34 2296970A 0BCCB74C 736FC755 4494BF63",
         "56FBF3CA 366CC23E 8157854C 13C58D6A AC23F046 ADA30F83 53E74F33 039872AB",
         "D6840F6B 42F6EDAF D13116E0 E1256520 2FEF8E9E CE7DCE03 812464D0 4B9442DE")
add_ecdh("ecdh-p384-rfc5903", "8.2", "P-384",
         "099F3C70 34D4A2C6 99884D73 A375A67F 7624EF7C 6B3C0F16 0647B674 14DCE655 E35B5380 41E649EE 3FAEF896 783AB194",
         "667842D7 D180AC2C DE6F74F3 7551F557 55C7645C 20EF73E3 1634FE72 B4C55EE6 DE3AC808 ACB4BDB4 C88732AE E95F41AA",
         "9482ED1F C0EEB9CA FC498462 5CCFC23F 65032149 E0E144AD A0241815 35A0F38E EB9FCFF3 C2C947DA E69B4C63 4573A81C",
         "41CB0779 B4BDB85D 47846725 FBEC3C94 30FAB46C C8DC5060 855CC9BD A0AA2942 E0308312 916B8ED2 960E4BD5 5A7448FC",
         "E558DBEF 53EECDE3 D3FCCFC1 AEA08A89 A987475D 12FD950D 83CFA417 32BC509D 0D1AC43A 0336DEF9 6FDA41D0 774A3571",
         "DCFBEC7A ACF31964 72169E83 8430367F 66EEBE3C 6E70C416 DD5F0C68 759DD1FF F83FA401 42209DFF 5EAAD96D B9E6386C",
         "11187331 C279962D 93D60424 3FD592CB 9D0A926F 422E4718 7521287E 7156C5C4 D6031355 69B9E9D0 9CF5D4A2 70F59746")
add_ecdh("ecdh-p521-rfc5903", "8.3", "P-521",
         "0037ADE9 319A89F4 DABDB3EF 411AACCC A5123C61 ACAB57B5 393DCE47 608172A0 95AA85A3 0FE1C295 2C6771D9 37BA9777 F5957B26 39BAB072 462F68C2 7A57382D 4A52",
         "0015417E 84DBF28C 0AD3C278 713349DC 7DF153C8 97A1891B D98BAB43 57C9ECBE E1E3BF42 E00B8E38 0AEAE57C 2D107564 94188594 2AF5A7F4 601723C4 195D176C ED3E",
         "017CAE20 B6641D2E EB695786 D8C94614 6239D099 E18E1D5A 514C739D 7CB4A10A D8A78801 5AC405D7 799DC75E 7B7D5B6C F2261A6A 7F150743 8BF01BEB 6CA3926F 9582",
         "0145BA99 A847AF43 793FDD0E 872E7CDF A16BE30F DC780F97 BCCC3F07 8380201E 9C677D60 0B343757 A3BDBF2A 3163E4C2 F869CCA7 458AA4A4 EFFC311F 5CB15168 5EB9",
         "00D0B397 5AC4B799 F5BEA16D 5E13E9AF 971D5E9B 984C9F39 728B5E57 39735A21 9B97C356 436ADC6E 95BB0352 F6BE64A6 C2912D4E F2D0433C ED2B6171 640012D9 460F",
         "015C6822 6383956E 3BD066E7 97B623C2 7CE0EAC2 F551A10C 2C724D98 52077B87 220B6536 C5C408A1 D2AEBB8E 86D678AE 49CB5709 1F473229 6579AB44 FCD17F0F C56A",
         "01144C7D 79AE6956 BC8EDB8E 7C787C45 21CB086F A64407F9 7894E5E6 B2D79B04 D1427E73 CA4BAA24 0A347868 59810C06 B3C715A3 A8CC3151 F2BEE417 996D19F3 DDEA")

# ---- other encodings of an EC key (RFC 5903's P-256 initiator), built here byte by byte: what an importer must take, and refuse
def tlv(tag, content):
    n = len(content)
    return bytes([tag]) + (bytes([n]) if n < 128 else bytes([0x81, n]) if n < 256 else bytes([0x82, n >> 8, n & 255])) + content
ki, kr = V["ecdh"][0]["initiator"], V["ecdh"][0]["responder"]
d_i, raw_i, raw_r = unb64u(ki["d"]), H(ki["raw"]), H(kr["raw"])
OID_EC, OID_ECDH, OID_P256 = H("06072a8648ce3d0201"), H("06052b8104010c"), H("06082a8648ce3d030107")
ver = lambda v: bytes([2, 1, v])
def p8(ecpriv, alg=OID_EC):
    return tlv(0x30, ver(0) + tlv(0x30, alg + OID_P256) + tlv(0x04, ecpriv))
pub1 = lambda raw: tlv(0xA1, tlv(0x03, b"\x00" + raw))
canonical = p8(tlv(0x30, ver(1) + tlv(0x04, d_i) + pub1(raw_i)))
ok(hx(canonical) == ki["pkcs8"], "the DER built here is OpenSSL's PKCS #8")
no_public = p8(tlv(0x30, ver(1) + tlv(0x04, d_i)))
with_parameters = p8(tlv(0x30, ver(1) + tlv(0x04, d_i) + tlv(0xA0, OID_P256) + pub1(raw_i)))
wrong_public = p8(tlv(0x30, ver(1) + tlv(0x04, d_i) + pub1(raw_r)))        # a point of the curve, but not d * G
sec1_only = tlv(0x30, ver(1) + tlv(0x04, d_i) + tlv(0xA0, OID_P256) + pub1(raw_i))  # an ECPrivateKey without the PKCS #8 envelope
for name, der in (("no public key", no_public), ("with parameters", with_parameters)):
    k = serialization.load_der_private_key(der, None)
    ok(k.private_numbers().private_value == int.from_bytes(d_i, "big"), "EC PKCS #8 " + name + " (OpenSSL reads it as the same key)")
spki_ecdh = tlv(0x30, tlv(0x30, OID_ECDH + OID_P256) + tlv(0x03, b"\x00" + raw_i))
ok(hx(tlv(0x30, tlv(0x30, OID_EC + OID_P256) + tlv(0x03, b"\x00" + raw_i))) == ki["spki"], "the DER built here is OpenSSL's SPKI")
compressed = bytes([2 + (raw_i[-1] & 1)]) + raw_i[1:33]
ok(ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), compressed).public_numbers().y == int.from_bytes(raw_i[33:], "big"), "compressed point")
V["ecEncodings"] = {
    "source": "NOT published vectors: other DER / SEC 1 encodings of RFC 5903's P-256 initiator key (in ecdh[0]), built byte by byte; OpenSSL reads the valid ones as that same key",
    "pkcs8NoPublicKey": hx(no_public), "pkcs8WithParameters": hx(with_parameters), "pkcs8WrongPublicKey": hx(wrong_public), "sec1WithoutEnvelope": hx(sec1_only),
    "spkiIdEcDH": hx(spki_ecdh), "rawCompressed": hx(compressed),
}

# ---- RSA: RFC 7515 (JSON Web Signature) appendix A.2: a 2048-bit key as a JWK, and its RSASSA-PKCS1-v1_5 SHA-256
# signature of the JWS signing input. The key and the signature are read from the RFC's text (rfc-editor.org).
text = urllib.request.urlopen("https://www.rfc-editor.org/rfc/rfc7515.txt", timeout=60).read().decode()
a2 = text[text.rindex("A.2.1.  Encoding"):text.rindex("A.2.2.  Validating")]  # (the last ones: not the table of contents')
a2 = re.sub(r"\n\nJones, et al\.[^\n]*\n\x0c\nRFC 7515[^\n]*\n\n", "\n", a2)  # (page breaks)
m = re.search(r'\{"kty":"RSA".*?\}', a2, re.S)
jwk = json.loads(re.sub(r"\s+", "", m.group(0)))
sig_text = a2[a2.index("Encoding the signature as BASE64URL(JWS Signature)"):a2.index("Concatenating these values")]
sig = unb64u("".join(re.findall(r"^\s{5}([A-Za-z0-9_-]+)\s*$", sig_text, re.M)))
signing_input = b"eyJhbGciOiJSUzI1NiJ9.eyJpc3MiOiJqb2UiLA0KICJleHAiOjEzMDA4MTkzODAsDQogImh0dHA6Ly9leGFtcGxlLmNvbS9pc19yb290Ijp0cnVlfQ"
num = lambda k: int.from_bytes(unb64u(jwk[k]), "big")
rsa_priv = rsa.RSAPrivateNumbers(num("p"), num("q"), num("d"), num("dp"), num("dq"), num("qi"), rsa.RSAPublicNumbers(num("e"), num("n"))).private_key()
ok(num("n") == num("p") * num("q") and len(sig) == 256, "RFC 7515 key")
rsa_priv.public_key().verify(sig, signing_input, padding.PKCS1v15(), hashes.SHA256())  # raises if wrong
ok(rsa_priv.sign(signing_input, padding.PKCS1v15(), hashes.SHA256()) == sig, "RFC 7515 signature (PKCS #1 v1.5 is deterministic)")
oaep_pt, oaep_label = b"Onyx: RSA-OAEP cross-check", b"a label"
pss_msg = b"Onyx: RSA-PSS cross-check"
V["rsa"] = {
    "source": "RFC 7515 appendix A.2 (the RSA key as a JWK; the RSASSA-PKCS1-v1_5 SHA-256 signature of the JWS signing input)",
    "jwk": {k: jwk[k] for k in ("kty", "n", "e", "d", "p", "q", "dp", "dq", "qi")},
    "message": hx(signing_input), "signature": hx(sig),
    "derSource": "the same key as OpenSSL writes it (DER): what the exports are compared with",
    "spki": hx(rsa_priv.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)),
    "pkcs8": hx(rsa_priv.private_bytes(serialization.Encoding.DER, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())),
    "crossSource": "NOT published vectors: made with OpenSSL (Python's cryptography) with the RFC 7515 key, to check RSA-OAEP and RSA-PSS against another implementation (both are randomized: no known answer to compare a result with)",
    "oaep": [], "pss": [],
}
for h in ("SHA-1", "SHA-256"):
    for label in (b"", oaep_label):
        ct = rsa_priv.public_key().encrypt(oaep_pt, padding.OAEP(mgf=padding.MGF1(HASH[h]()), algorithm=HASH[h](), label=label or None))
        ok(rsa_priv.decrypt(ct, padding.OAEP(mgf=padding.MGF1(HASH[h]()), algorithm=HASH[h](), label=label or None)) == oaep_pt, "oaep")
        V["rsa"]["oaep"].append({"name": "rsa-oaep-%s-%s-openssl" % (h.lower(), "label" if label else "nolabel"), "hash": h, "label": hx(label), "plaintext": hx(oaep_pt), "ciphertext": hx(ct)})
for h, salt in (("SHA-256", 32), ("SHA-256", 0), ("SHA-384", 48), ("SHA-1", 20), ("SHA-512", 64)):
    s = rsa_priv.sign(pss_msg, padding.PSS(mgf=padding.MGF1(HASH[h]()), salt_length=salt), HASH[h]())
    rsa_priv.public_key().verify(s, pss_msg, padding.PSS(mgf=padding.MGF1(HASH[h]()), salt_length=salt), HASH[h]())
    ok(True, "pss")
    V["rsa"]["pss"].append({"name": "rsa-pss-%s-salt%d-openssl" % (h.lower(), salt), "hash": h, "saltLength": salt, "message": hx(pss_msg), "signature": hx(s)})

# ---- into the page
page = open(PAGE, encoding="utf-8", newline="").read()
begin, end = '<script type="application/json" id="vectors">', "</script>"
i = page.index(begin) + len(begin)
j = page.index(end, i)
body = json.dumps(V, indent=1).replace("</", "<\\/")
open(PAGE, "w", encoding="utf-8", newline="").write(page[:i] + "\n" + body + "\n" + page[j:])
print("gen_vectors.py: %d checks against OpenSSL passed; vectors written into %s (%d bytes of JSON)" % (checked, PAGE, len(body)))
