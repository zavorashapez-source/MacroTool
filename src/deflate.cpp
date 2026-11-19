#include "deflate.h"
#include <cstring>

namespace {

constexpr int kMaxBits = 15;

const short kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                            35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const short kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const short kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                             193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const short kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

struct Huffman {
    short count[kMaxBits + 1];
    short symbol[288];
};

// Canonical Huffman decoding after Mark Adler's puff.c.
int Construct(Huffman& h, const short* length, int n) {
    for (int len = 0; len <= kMaxBits; len++) h.count[len] = 0;
    for (int s = 0; s < n; s++) h.count[length[s]]++;
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= kMaxBits; len++) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return left;
    }
    short offs[kMaxBits + 1];
    offs[1] = 0;
    for (int len = 1; len < kMaxBits; len++) offs[len + 1] = offs[len] + h.count[len];
    for (int s = 0; s < n; s++)
        if (length[s] != 0) h.symbol[offs[length[s]]++] = (short)s;
    return left;
}

struct Reader {
    const uint8_t* in;
    size_t n, pos = 0;
    uint64_t buf = 0;
    int cnt = 0;
    bool err = false;
    std::vector<uint8_t>* out;
    size_t maxOut;

    int Bits(int need) {
        uint64_t val = buf;
        while (cnt < need) {
            if (pos >= n) {
                err = true;
                return 0;
            }
            val |= (uint64_t)in[pos++] << cnt;
            cnt += 8;
        }
        buf = val >> need;
        cnt -= need;
        return (int)(val & ((1ull << need) - 1));
    }

    int Decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; len++) {
            code |= Bits(1);
            if (err) return -1;
            int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }

    bool Stored() {
        buf = 0;
        cnt = 0;
        if (pos + 4 > n) return false;
        unsigned len = in[pos] | (in[pos + 1] << 8);
        unsigned nlen = in[pos + 2] | (in[pos + 3] << 8);
        pos += 4;
        if (len != (~nlen & 0xFFFF)) return false;
        if (pos + len > n || out->size() + len > maxOut) return false;
        out->insert(out->end(), in + pos, in + pos + len);
        pos += len;
        return true;
    }

    bool Codes(const Huffman& lc, const Huffman& dc) {
        for (;;) {
            int sym = Decode(lc);
            if (sym < 0) return false;
            if (sym < 256) {
                if (out->size() >= maxOut) return false;
                out->push_back((uint8_t)sym);
            } else if (sym == 256) {
                return true;
            } else {
                sym -= 257;
                if (sym >= 29) return false;
                int len = kLenBase[sym] + Bits(kLenExtra[sym]);
                int ds = Decode(dc);
                if (ds < 0 || ds >= 30) return false;
                size_t dist = (size_t)kDistBase[ds] + Bits(kDistExtra[ds]);
                if (err || dist > out->size() || out->size() + len > maxOut) return false;
                size_t from = out->size() - dist;
                for (int i = 0; i < len; i++) out->push_back((*out)[from + i]);
            }
        }
    }

    bool Fixed() {
        static Huffman lc, dc;
        static bool built = false;
        if (!built) {
            short lengths[288];
            int s = 0;
            for (; s < 144; s++) lengths[s] = 8;
            for (; s < 256; s++) lengths[s] = 9;
            for (; s < 280; s++) lengths[s] = 7;
            for (; s < 288; s++) lengths[s] = 8;
            Construct(lc, lengths, 288);
            for (s = 0; s < 30; s++) lengths[s] = 5;
            Construct(dc, lengths, 30);
            built = true;
        }
        return Codes(lc, dc);
    }

    bool Dynamic() {
        static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        short lengths[320];
        int nlen = Bits(5) + 257, ndist = Bits(5) + 1, ncode = Bits(4) + 4;
        if (err || nlen > 286 || ndist > 30) return false;
        int index = 0;
        for (; index < ncode; index++) lengths[order[index]] = (short)Bits(3);
        for (; index < 19; index++) lengths[order[index]] = 0;
        Huffman lc, dc;
        if (Construct(lc, lengths, 19) != 0) return false;
        index = 0;
        while (index < nlen + ndist) {
            int sym = Decode(lc);
            if (sym < 0) return false;
            if (sym < 16) {
                lengths[index++] = (short)sym;
            } else {
                short len = 0;
                int rep;
                if (sym == 16) {
                    if (index == 0) return false;
                    len = lengths[index - 1];
                    rep = 3 + Bits(2);
                } else if (sym == 17) {
                    rep = 3 + Bits(3);
                } else {
                    rep = 11 + Bits(7);
                }
                if (err || index + rep > nlen + ndist) return false;
                while (rep--) lengths[index++] = len;
            }
        }
        if (lengths[256] == 0) return false;
        int e = Construct(lc, lengths, nlen);
        if (e && (e < 0 || nlen != lc.count[0] + lc.count[1])) return false;
        e = Construct(dc, lengths + nlen, ndist);
        if (e && (e < 0 || ndist != dc.count[0] + dc.count[1])) return false;
        return Codes(lc, dc);
    }
};

struct Writer {
    std::vector<uint8_t> out;
    uint32_t buf = 0;
    int cnt = 0;
    void Put(uint32_t bits, int n) {
        buf |= bits << cnt;
        cnt += n;
        while (cnt >= 8) {
            out.push_back((uint8_t)buf);
            buf >>= 8;
            cnt -= 8;
        }
    }
    // Huffman codes are stored most-significant bit first.
    void Huff(uint32_t code, int n) {
        uint32_t r = 0;
        for (int i = 0; i < n; i++) r |= ((code >> i) & 1) << (n - 1 - i);
        Put(r, n);
    }
    void Lit(int s) {
        if (s < 144) Huff(0x30 + s, 8);
        else if (s < 256) Huff(0x190 + (s - 144), 9);
        else if (s < 280) Huff(s - 256, 7);
        else Huff(0xC0 + (s - 280), 8);
    }
    void Flush() {
        if (cnt > 0) out.push_back((uint8_t)buf);
        buf = 0;
        cnt = 0;
    }
};

}  // namespace

bool Inflate(const uint8_t* src, size_t n, std::vector<uint8_t>& out, size_t maxOut) {
    out.clear();
    Reader r{src, n};
    r.out = &out;
    r.maxOut = maxOut;
    int last;
    do {
        last = r.Bits(1);
        int type = r.Bits(2);
        if (r.err) return false;
        bool ok = type == 0 ? r.Stored() : type == 1 ? r.Fixed() : type == 2 ? r.Dynamic() : false;
        if (!ok || r.err) return false;
    } while (!last);
    return true;
}

std::vector<uint8_t> Deflate(const uint8_t* src, size_t n) {
    // one fixed huffman block + greedy lz77, good enough for share codes
    constexpr int kWin = 32768, kHash = 1 << 15;
    std::vector<int> head(kHash, -1), prev(n ? n : 1, -1);
    auto hash = [&](size_t i) { return ((src[i] << 10) ^ (src[i + 1] << 5) ^ src[i + 2]) & (kHash - 1); };
    Writer w;
    w.Put(1, 1);  // final block
    w.Put(1, 2);  // fixed Huffman
    size_t i = 0;
    auto insert = [&](size_t at) {
        if (at + 2 < n) {
            int h = hash(at);
            prev[at] = head[h];
            head[h] = (int)at;
        }
    };
    while (i < n) {
        int bestLen = 0, bestDist = 0;
        if (i + 2 < n) {
            int cand = head[hash(i)];
            for (int chain = 0; cand >= 0 && chain < 96; chain++, cand = prev[cand]) {
                size_t dist = i - (size_t)cand;
                if (dist > kWin) break;
                int len = 0;
                while (len < 258 && i + len < n && src[cand + len] == src[i + len]) len++;
                if (len > bestLen) {
                    bestLen = len;
                    bestDist = (int)dist;
                    if (len == 258) break;
                }
            }
        }
        if (bestLen >= 3) {
            int li = 28;
            if (bestLen < 258)
                for (li = 27; li > 0 && kLenBase[li] > bestLen; li--) {}
            w.Lit(257 + li);
            if (kLenExtra[li]) w.Put(bestLen - kLenBase[li], kLenExtra[li]);
            int di = 29;
            while (di > 0 && kDistBase[di] > bestDist) di--;
            w.Huff(di, 5);
            if (kDistExtra[di]) w.Put(bestDist - kDistBase[di], kDistExtra[di]);
            for (int k = 0; k < bestLen; k++) insert(i + k);
            i += bestLen;
        } else {
            w.Lit(src[i]);
            insert(i);
            i++;
        }
    }
    w.Lit(256);
    w.Flush();
    return w.out;
}

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string Base64UrlEncode(const std::vector<uint8_t>& d) {
    std::string o;
    size_t i = 0;
    for (; i + 2 < d.size(); i += 3) {
        uint32_t v = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2];
        o += kB64[v >> 18];
        o += kB64[(v >> 12) & 63];
        o += kB64[(v >> 6) & 63];
        o += kB64[v & 63];
    }
    if (i + 1 == d.size()) {
        uint32_t v = d[i] << 16;
        o += kB64[v >> 18];
        o += kB64[(v >> 12) & 63];
    } else if (i + 2 == d.size()) {
        uint32_t v = (d[i] << 16) | (d[i + 1] << 8);
        o += kB64[v >> 18];
        o += kB64[(v >> 12) & 63];
        o += kB64[(v >> 6) & 63];
    }
    return o;
}

bool Base64Decode(const std::string& text, std::vector<uint8_t>& out) {
    out.clear();
    uint32_t acc = 0;
    int bits = 0, pad = 0;
    size_t chars = 0;
    for (char c : text) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        if (c == '=') {
            pad++;
            continue;
        }
        if (pad) return false;  // data after padding
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '-' || c == '+') v = 62;
        else if (c == '_' || c == '/') v = 63;
        else return false;
        acc = (acc << 6) | v;
        bits += 6;
        chars++;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)(acc >> bits));
        }
    }
    if (chars % 4 == 1 || pad > 2) return false;
    return true;
}
