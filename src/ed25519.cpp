// ed25519 verify only (RFC 8032), based on TweetNaCl (public domain).
// curve constants get computed at startup.
#include "license.h"
#include <bcrypt.h>
#include <cstring>

namespace {

typedef long long i64;
typedef i64 gf[16];

const gf gf0 = {0};
const gf gf1 = {1};

void Set(gf r, const gf a) {
    for (int i = 0; i < 16; i++) r[i] = a[i];
}

void Carry(gf o) {
    for (int i = 0; i < 16; i++) {
        o[i] += (1LL << 16);
        i64 c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

void Sel(gf p, gf q, int b) {
    i64 c = ~(b - 1);
    for (int i = 0; i < 16; i++) {
        i64 t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

void Pack(uint8_t* o, const gf n) {
    gf m, t;
    for (int i = 0; i < 16; i++) t[i] = n[i];
    Carry(t);
    Carry(t);
    Carry(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        Sel(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i] = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

bool Neq(const gf a, const gf b) {
    uint8_t c[32], d[32];
    Pack(c, a);
    Pack(d, b);
    return memcmp(c, d, 32) != 0;
}

int Parity(const gf a) {
    uint8_t d[32];
    Pack(d, a);
    return d[0] & 1;
}

void Unpack(gf o, const uint8_t* n) {
    for (int i = 0; i < 16; i++) o[i] = n[2 * i] + ((i64)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}

void Add(gf o, const gf a, const gf b) {
    for (int i = 0; i < 16; i++) o[i] = a[i] + b[i];
}

void Sub(gf o, const gf a, const gf b) {
    for (int i = 0; i < 16; i++) o[i] = a[i] - b[i];
}

void Mul(gf o, const gf a, const gf b) {
    i64 t[31] = {0};
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    Carry(o);
    Carry(o);
}

void Sq(gf o, const gf a) { Mul(o, a, a); }

void Inv(gf o, const gf i) {
    gf c;
    Set(c, i);
    for (int a = 253; a >= 0; a--) {
        Sq(c, c);
        if (a != 2 && a != 4) Mul(c, c, i);
    }
    Set(o, c);
}

void Pow2523(gf o, const gf i) {
    gf c;
    Set(c, i);
    for (int a = 250; a >= 0; a--) {
        Sq(c, c);
        if (a != 1) Mul(c, c, i);
    }
    Set(o, c);
}

struct Consts {
    gf D, D2, I;
    gf B[4];
};

void AddPt(gf p[4], gf q[4], const Consts& k) {
    gf a, b, c, d, t, e, f, g, h;
    Sub(a, p[1], p[0]);
    Sub(t, q[1], q[0]);
    Mul(a, a, t);
    Add(b, p[0], p[1]);
    Add(t, q[0], q[1]);
    Mul(b, b, t);
    Mul(c, p[3], q[3]);
    Mul(c, c, k.D2);
    Mul(d, p[2], q[2]);
    Add(d, d, d);
    Sub(e, b, a);
    Sub(f, d, c);
    Add(g, d, c);
    Add(h, b, a);
    Mul(p[0], e, f);
    Mul(p[1], h, g);
    Mul(p[2], g, f);
    Mul(p[3], e, h);
}

void Cswap(gf p[4], gf q[4], int b) {
    for (int i = 0; i < 4; i++) Sel(p[i], q[i], b);
}

void PackPt(uint8_t* r, gf p[4]) {
    gf tx, ty, zi;
    Inv(zi, p[2]);
    Mul(tx, p[0], zi);
    Mul(ty, p[1], zi);
    Pack(r, ty);
    r[31] ^= (uint8_t)(Parity(tx) << 7);
}

void ScalarMult(gf p[4], gf q[4], const uint8_t* s, const Consts& k) {
    Set(p[0], gf0);
    Set(p[1], gf1);
    Set(p[2], gf1);
    Set(p[3], gf0);
    for (int i = 255; i >= 0; --i) {
        int b = (s[i / 8] >> (i & 7)) & 1;
        Cswap(p, q, b);
        AddPt(q, p, k);
        AddPt(p, p, k);
        Cswap(p, q, b);
    }
}

// decode + negate (verify needs -A)
bool UnpackNeg(gf r[4], const uint8_t p[32], const Consts& k) {
    gf t, chk, num, den, den2, den4, den6;
    Set(r[2], gf1);
    Unpack(r[1], p);
    Sq(num, r[1]);
    Mul(den, num, k.D);
    Sub(num, num, r[2]);
    Add(den, r[2], den);
    Sq(den2, den);
    Sq(den4, den2);
    Mul(den6, den4, den2);
    Mul(t, den6, num);
    Mul(t, t, den);
    Pow2523(t, t);
    Mul(t, t, num);
    Mul(t, t, den);
    Mul(t, t, den);
    Mul(r[0], t, den);
    Sq(chk, r[0]);
    Mul(chk, chk, den);
    if (Neq(chk, num)) Mul(r[0], r[0], k.I);
    Sq(chk, r[0]);
    Mul(chk, chk, den);
    if (Neq(chk, num)) return false;
    if (Parity(r[0]) == (p[31] >> 7)) Sub(r[0], gf0, r[0]);
    Mul(r[3], r[0], r[1]);
    return true;
}

const Consts& K() {
    static Consts k = [] {
        Consts c{};
        // d = -121665 / 121666
        gf a = {0xDB41, 1}, b = {0xDB42, 1}, ib;
        Inv(ib, b);
        Mul(c.D, a, ib);
        Sub(c.D, gf0, c.D);
        Add(c.D2, c.D, c.D);
        // sqrt(-1) = 2^((p-1)/4) = (2^(2^252-3))^2 * 2
        gf two = {2};
        Pow2523(c.I, two);
        Sq(c.I, c.I);
        Mul(c.I, c.I, two);
        // base point: y = 4/5, positive x; encoded as 0x58 0x66 * 31
        uint8_t enc[32];
        enc[0] = 0x58;
        memset(enc + 1, 0x66, 31);
        UnpackNeg(c.B, enc, c);
        Sub(c.B[0], gf0, c.B[0]);
        Mul(c.B[3], c.B[0], c.B[1]);
        return c;
    }();
    return k;
}

const i64 L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                   0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};

void ModL(uint8_t* r, i64 x[64]) {
    i64 carry;
    int i, j;
    for (i = 63; i >= 32; --i) {
        carry = 0;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * L[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry * 256;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (j = 0; j < 32; j++) {
        x[j] += carry - (x[31] >> 4) * L[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (j = 0; j < 32; j++) x[j] -= carry * L[j];
    for (i = 0; i < 32; i++) {
        x[i + 1] += x[i] >> 8;
        r[i] = (uint8_t)(x[i] & 255);
    }
}

void Reduce(uint8_t* r) {
    i64 x[64];
    for (int i = 0; i < 64; i++) x[i] = (i64)r[i];
    for (int i = 0; i < 64; i++) r[i] = 0;
    ModL(r, x);
}

bool Sha512(const uint8_t* data, size_t n, uint8_t out[64]) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA512_ALGORITHM, nullptr, 0) != 0) return false;
    NTSTATUS st = BCryptHash(alg, nullptr, 0, (PUCHAR)data, (ULONG)n, out, 64);
    BCryptCloseAlgorithmProvider(alg, 0);
    return st == 0;
}

// S must be < L
bool ScalarCanonical(const uint8_t* s) {
    for (int i = 31; i >= 0; i--) {
        if (s[i] < L[i]) return true;
        if (s[i] > L[i]) return false;
    }
    return false;
}

}  // namespace

bool Ed25519Verify(const uint8_t sig[64], const uint8_t* msg, size_t n, const uint8_t pk[32]) {
    const Consts& k = K();
    if (!ScalarCanonical(sig + 32)) return false;
    gf q[4], p[4];
    if (!UnpackNeg(q, pk, k)) return false;
    std::vector<uint8_t> buf(64 + n);
    memcpy(buf.data(), sig, 32);
    memcpy(buf.data() + 32, pk, 32);
    if (n) memcpy(buf.data() + 64, msg, n);
    uint8_t h[64];
    if (!Sha512(buf.data(), buf.size(), h)) return false;
    Reduce(h);
    ScalarMult(p, q, h, k);
    gf b[4];
    for (int i = 0; i < 4; i++) Set(b[i], k.B[i]);
    gf r[4];
    ScalarMult(r, b, sig + 32, k);
    AddPt(p, r, k);
    uint8_t t[32];
    PackPt(t, p);
    return memcmp(sig, t, 32) == 0;
}
