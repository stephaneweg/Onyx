//
// crypto_pc.cpp -- PC implementation of taatu::crypto (SHA-1, SHA-256/HMAC, base64, random).
// Self-contained, no OpenSSL. Compact public-domain-style implementations. Used by the
// Windows/Linux build and the desktop sim; the Onyx build uses mbedTLS instead.
//
#include "../../core/crypto.hpp"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

namespace { // ---- SHA-1 ----
struct Sha1 {
    uint32_t h[5]; uint64_t len; unsigned char buf[64]; int n;
    void init () { h[0]=0x67452301;h[1]=0xEFCDAB89;h[2]=0x98BADCFE;h[3]=0x10325476;h[4]=0xC3D2E1F0;len=0;n=0; }
    static uint32_t rol (uint32_t v, int b) { return (v << b) | (v >> (32 - b)); }
    void block (const unsigned char *p) {
        uint32_t w[80];
        for (int i=0;i<16;i++) w[i]=(p[i*4]<<24)|(p[i*4+1]<<16)|(p[i*4+2]<<8)|p[i*4+3];
        for (int i=16;i<80;i++) w[i]=rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
        for (int i=0;i<80;i++){
            uint32_t f,k;
            if(i<20){f=(b&c)|((~b)&d);k=0x5A827999;}
            else if(i<40){f=b^c^d;k=0x6ED9EBA1;}
            else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;}
            else {f=b^c^d;k=0xCA62C1D6;}
            uint32_t t=rol(a,5)+f+e+k+w[i]; e=d;d=c;c=rol(b,30);b=a;a=t;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;
    }
    void update (const unsigned char *p, size_t l) {
        len += l;
        while (l) { int c=64-n; if((int)l<c)c=(int)l; memcpy(buf+n,p,c); n+=c;p+=c;l-=c; if(n==64){block(buf);n=0;} }
    }
    void final (unsigned char out[20]) {
        uint64_t bits=len*8; unsigned char pad=0x80; update(&pad,1);
        unsigned char z=0; while(n!=56) update(&z,1);
        unsigned char lb[8]; for(int i=0;i<8;i++) lb[i]=(unsigned char)(bits>>(56-8*i)); update(lb,8);
        for(int i=0;i<5;i++){ out[i*4]=(unsigned char)(h[i]>>24);out[i*4+1]=(unsigned char)(h[i]>>16);out[i*4+2]=(unsigned char)(h[i]>>8);out[i*4+3]=(unsigned char)h[i]; }
    }
};

// ---- SHA-256 ----
struct Sha256 {
    uint32_t h[8]; uint64_t len; unsigned char buf[64]; int n;
    void init () {
        static const uint32_t I[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
        memcpy(h,I,sizeof h); len=0;n=0;
    }
    static uint32_t ror (uint32_t v,int b){return (v>>b)|(v<<(32-b));}
    void block (const unsigned char *p) {
        static const uint32_t K[64]={
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
        uint32_t w[64];
        for(int i=0;i<16;i++) w[i]=(p[i*4]<<24)|(p[i*4+1]<<16)|(p[i*4+2]<<8)|p[i*4+3];
        for(int i=16;i<64;i++){uint32_t s0=ror(w[i-15],7)^ror(w[i-15],18)^(w[i-15]>>3);uint32_t s1=ror(w[i-2],17)^ror(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for(int i=0;i<64;i++){
            uint32_t S1=ror(e,6)^ror(e,11)^ror(e,25);uint32_t ch=(e&f)^((~e)&g);uint32_t t1=hh+S1+ch+K[i]+w[i];
            uint32_t S0=ror(a,2)^ror(a,13)^ror(a,22);uint32_t maj=(a&b)^(a&c)^(b&c);uint32_t t2=S0+maj;
            hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }
    void update (const unsigned char *p,size_t l){len+=l;while(l){int c=64-n;if((int)l<c)c=(int)l;memcpy(buf+n,p,c);n+=c;p+=c;l-=c;if(n==64){block(buf);n=0;}}}
    void final (unsigned char out[32]){
        uint64_t bits=len*8;unsigned char pad=0x80;update(&pad,1);unsigned char z=0;while(n!=56)update(&z,1);
        unsigned char lb[8];for(int i=0;i<8;i++)lb[i]=(unsigned char)(bits>>(56-8*i));update(lb,8);
        for(int i=0;i<8;i++){out[i*4]=(unsigned char)(h[i]>>24);out[i*4+1]=(unsigned char)(h[i]>>16);out[i*4+2]=(unsigned char)(h[i]>>8);out[i*4+3]=(unsigned char)h[i];}
    }
};
} // anon

namespace taatu { namespace crypto {

void sha1 (const unsigned char *d, size_t n, unsigned char out[20])
{ Sha1 s; s.init (); s.update (d, n); s.final (out); }

void hmac_sha256 (const unsigned char *key, size_t klen, const unsigned char *msg, size_t mlen, unsigned char out[32])
{
    unsigned char k[64]; memset (k, 0, 64);
    if (klen > 64) { Sha256 s; s.init (); s.update (key, klen); s.final (k); }
    else memcpy (k, key, klen);
    unsigned char ipad[64], opad[64];
    for (int i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    unsigned char inner[32];
    { Sha256 s; s.init (); s.update (ipad, 64); s.update (msg, mlen); s.final (inner); }
    { Sha256 s; s.init (); s.update (opad, 64); s.update (inner, 32); s.final (out); }
}

size_t base64 (const unsigned char *d, size_t n, char *out, size_t cap)
{
    static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3)
    {
        unsigned v = d[i] << 16;
        if (i + 1 < n) v |= d[i + 1] << 8;
        if (i + 2 < n) v |= d[i + 2];
        if (o + 4 >= cap) break;
        out[o++] = T[(v >> 18) & 63];
        out[o++] = T[(v >> 12) & 63];
        out[o++] = (i + 1 < n) ? T[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < n) ? T[v & 63] : '=';
    }
    if (o < cap) out[o] = 0;
    return o;
}

void random_bytes (void *out, size_t n)
{
    // Dev-grade randomness (WS mask + 16-byte handshake nonce; not key material). Seeded
    // once from time + address entropy. The Onyx build uses the Pi hardware RNG.
    static unsigned long long s = 0;
    if (!s) { s = (unsigned long long) time (0) ^ (unsigned long long) (size_t) &s ^ ((unsigned long long) clock () << 21); if (!s) s = 0x9e3779b97f4a7c15ULL; }
    unsigned char *p = (unsigned char *) out;
    for (size_t i = 0; i < n; i++) { s = s * 6364136223846793005ULL + 1442695040888963407ULL; p[i] = (unsigned char) (s >> 33); }
}

} } // namespace taatu::crypto
