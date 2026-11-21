// SHA-256 (FIPS 180-4), HMAC (RFC 2104) and AWS Signature Version 4: see cc_Sign.h.
#include "cc_Sign.h"
#include <string.h>
#include <stdio.h>

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static inline uint32_t ror(uint32_t x, int n){ return (x >> n) | (x << (32 - n)); }

static void block(Cc_Sha256_t& s, const uint8_t* p){
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++){
        const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s.h[0], b = s.h[1], c = s.h[2], d = s.h[3], e = s.h[4], f = s.h[5], g = s.h[6], h = s.h[7];
    for (int i = 0; i < 64; i++){
        const uint32_t t1 = h + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        const uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s.h[0] += a; s.h[1] += b; s.h[2] += c; s.h[3] += d; s.h[4] += e; s.h[5] += f; s.h[6] += g; s.h[7] += h;
}

void cc_Sha256_Begin(Cc_Sha256_t& s){
    static const uint32_t IV[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s.h, IV, sizeof(IV));
    s.bytes = 0;
    s.used = 0;
}

void cc_Sha256_Add(Cc_Sha256_t& s, const void* data, size_t n){
    const uint8_t* p = (const uint8_t*)data;
    s.bytes += n;
    while (n){
        const size_t take = 64 - s.used < n ? 64 - s.used : n;
        memcpy(s.block + s.used, p, take);
        s.used += take; p += take; n -= take;
        if (s.used == 64){ block(s, s.block); s.used = 0; }
    }
}

void cc_Sha256_End(Cc_Sha256_t& s, uint8_t out[32]){
    const uint64_t bits = s.bytes * 8;
    const uint8_t one = 0x80, zero = 0;
    cc_Sha256_Add(s, &one, 1);
    while (s.used != 56) cc_Sha256_Add(s, &zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
    cc_Sha256_Add(s, len, 8);
    for (int i = 0; i < 8; i++){ out[4 * i] = (uint8_t)(s.h[i] >> 24); out[4 * i + 1] = (uint8_t)(s.h[i] >> 16); out[4 * i + 2] = (uint8_t)(s.h[i] >> 8); out[4 * i + 3] = (uint8_t)s.h[i]; }
}

void cc_Sha256(const void* data, size_t n, uint8_t out[32]){
    Cc_Sha256_t s;
    cc_Sha256_Begin(s);
    cc_Sha256_Add(s, data, n);
    cc_Sha256_End(s, out);
}

void cc_Hmac_Sha256(const void* key, size_t keyN, const void* data, size_t n, uint8_t out[32]){
    uint8_t k[64] = {0}, pad[64], inner[32];
    if (keyN > 64) cc_Sha256(key, keyN, k); else memcpy(k, key, keyN);
    Cc_Sha256_t s;
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    cc_Sha256_Begin(s); cc_Sha256_Add(s, pad, 64); cc_Sha256_Add(s, data, n); cc_Sha256_End(s, inner);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    cc_Sha256_Begin(s); cc_Sha256_Add(s, pad, 64); cc_Sha256_Add(s, inner, 32); cc_Sha256_End(s, out);
    memset(k, 0, sizeof(k)); memset(pad, 0, sizeof(pad));
}

void cc_Hex(const uint8_t in[32], char out[65]){
    static const char D[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++){ out[2 * i] = D[in[i] >> 4]; out[2 * i + 1] = D[in[i] & 15]; }
    out[64] = '\0';
}

bool cc_Aws_Authorization(const char* accessKey, const char* secretKey, const char* region, const char* service,
                          const char* host, const char* contentType, const char* target, const char* amzDate,
                          const char* body, size_t bodyN, char* out, size_t outN){
    char date[9];
    memcpy(date, amzDate, 8);
    date[8] = '\0';
    uint8_t hash[32];
    char bodyHex[65], canonHex[65];
    cc_Sha256(body, bodyN, hash);
    cc_Hex(hash, bodyHex);
    static const char SIGNED[] = "content-type;host;x-amz-date;x-amz-target";
    char canon[640];
    int n = snprintf(canon, sizeof(canon), "POST\n/\n\ncontent-type:%s\nhost:%s\nx-amz-date:%s\nx-amz-target:%s\n\n%s\n%s",
                     contentType, host, amzDate, target, SIGNED, bodyHex);
    if (n < 0 || (size_t)n >= sizeof(canon)) return false;
    cc_Sha256(canon, (size_t)n, hash);
    cc_Hex(hash, canonHex);
    char scope[96], toSign[256];
    snprintf(scope, sizeof(scope), "%s/%s/%s/aws4_request", date, region, service);
    n = snprintf(toSign, sizeof(toSign), "AWS4-HMAC-SHA256\n%s\n%s\n%s", amzDate, scope, canonHex);
    if (n < 0 || (size_t)n >= sizeof(toSign)) return false;
    // The signing key: HMAC chained over the date, the region, the service and "aws4_request".
    char first[80];
    const int fn = snprintf(first, sizeof(first), "AWS4%s", secretKey);
    if (fn < 0 || (size_t)fn >= sizeof(first)) return false;
    uint8_t k[32];
    cc_Hmac_Sha256(first, (size_t)fn, date, strlen(date), k);
    cc_Hmac_Sha256(k, 32, region, strlen(region), k);
    cc_Hmac_Sha256(k, 32, service, strlen(service), k);
    cc_Hmac_Sha256(k, 32, "aws4_request", 12, k);
    cc_Hmac_Sha256(k, 32, toSign, (size_t)n, hash);
    memset(first, 0, sizeof(first)); memset(k, 0, sizeof(k));
    char sig[65];
    cc_Hex(hash, sig);
    n = snprintf(out, outN, "AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=%s, Signature=%s", accessKey, scope, SIGNED, sig);
    return n > 0 && (size_t)n < outN;
}
