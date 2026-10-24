#include "flash/Inflate.h"

#include <memory>

namespace flash {
namespace {

struct BitIn {
    const uint8_t* p;
    const uint8_t* end;
    uint64_t buf = 0;
    int cnt = 0;
    bool overrun = false;

    void need(int n) {
        while (cnt < n) {
            uint64_t b = 0;
            if (p < end) b = *p++;
            else overrun = true;
            buf |= b << cnt;
            cnt += 8;
        }
    }
    uint32_t peek(int n) {
        need(n);
        return (uint32_t)(buf & ((1ull << n) - 1));
    }
    void drop(int n) {
        buf >>= n;
        cnt -= n;
    }
    uint32_t bits(int n) {
        if (n == 0) return 0;
        uint32_t v = peek(n);
        drop(n);
        return v;
    }
    void alignByte() { drop(cnt % 8); }
};

// Canonical Huffman table with a full 15-bit lookup: entry = symbol << 4 | length.
struct Huffman {
    static constexpr int kBits = 15;
    uint16_t table[1 << kBits];

    bool build(const uint8_t* lens, int n) {
        int count[16] = {};
        for (int i = 0; i < n; i++) count[lens[i]]++;
        count[0] = 0;
        int next[16] = {};
        int code = 0;
        for (int len = 1; len <= 15; len++) {
            code = (code + count[len - 1]) << 1;
            next[len] = code;
        }
        std::fill(table, table + (1 << kBits), 0);
        for (int sym = 0; sym < n; sym++) {
            int len = lens[sym];
            if (!len) continue;
            int c = next[len]++;
            int rev = 0;
            for (int i = 0; i < len; i++) rev |= ((c >> i) & 1) << (len - 1 - i);
            for (int i = rev; i < (1 << kBits); i += 1 << len)
                table[i] = (uint16_t)(sym << 4 | len);
        }
        return true;
    }

    int decode(BitIn& in) const {
        uint16_t e = table[in.peek(kBits)];
        int len = e & 15;
        if (!len) return -1;
        in.drop(len);
        return e >> 4;
    }
};

const uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                               31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                               2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,  25,
                                33,   49,   65,   97,   129,  193,   257,   385,   513, 769,
                                1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
const uint8_t kClOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

bool inflateBlock(BitIn& in, std::vector<uint8_t>& out, const Huffman& lit, const Huffman& dist) {
    for (;;) {
        int sym = lit.decode(in);
        if (sym < 0 || in.overrun) return false;
        if (sym < 256) {
            out.push_back((uint8_t)sym);
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            size_t len = kLenBase[sym] + in.bits(kLenExtra[sym]);
            int ds = dist.decode(in);
            if (ds < 0 || ds >= 30) return false;
            size_t d = kDistBase[ds] + in.bits(kDistExtra[ds]);
            if (d > out.size()) return false;
            size_t from = out.size() - d;
            for (size_t i = 0; i < len; i++) out.push_back(out[from + i]);
        }
    }
}

}  // namespace

bool zlibDecompress(const uint8_t* data, size_t len, std::vector<uint8_t>& out, size_t sizeHint) {
    out.clear();
    if (len < 2) return false;
    out.reserve(sizeHint);
    BitIn in{data + 2, data + len};
    auto lit = std::make_unique<Huffman>();
    auto dist = std::make_unique<Huffman>();
    for (;;) {
        int final = in.bits(1);
        int type = in.bits(2);
        if (type == 0) {
            in.alignByte();
            uint32_t n = in.bits(16);
            uint32_t nn = in.bits(16);
            if ((n ^ 0xFFFF) != nn) return false;
            for (uint32_t i = 0; i < n; i++) out.push_back((uint8_t)in.bits(8));
        } else if (type == 1) {
            uint8_t lens[288];
            for (int i = 0; i < 144; i++) lens[i] = 8;
            for (int i = 144; i < 256; i++) lens[i] = 9;
            for (int i = 256; i < 280; i++) lens[i] = 7;
            for (int i = 280; i < 288; i++) lens[i] = 8;
            lit->build(lens, 288);
            uint8_t dl[30];
            for (auto& l : dl) l = 5;
            dist->build(dl, 30);
            if (!inflateBlock(in, out, *lit, *dist)) return false;
        } else if (type == 2) {
            int hlit = in.bits(5) + 257;
            int hdist = in.bits(5) + 1;
            int hclen = in.bits(4) + 4;
            uint8_t cl[19] = {};
            for (int i = 0; i < hclen; i++) cl[kClOrder[i]] = (uint8_t)in.bits(3);
            Huffman clh;
            clh.build(cl, 19);
            uint8_t lens[320] = {};
            int n = 0;
            while (n < hlit + hdist) {
                int sym = clh.decode(in);
                if (sym < 0) return false;
                if (sym < 16) {
                    lens[n++] = (uint8_t)sym;
                } else {
                    int rep = 0;
                    uint8_t val = 0;
                    if (sym == 16) {
                        if (n == 0) return false;
                        val = lens[n - 1];
                        rep = 3 + in.bits(2);
                    } else if (sym == 17) {
                        rep = 3 + in.bits(3);
                    } else {
                        rep = 11 + in.bits(7);
                    }
                    if (n + rep > hlit + hdist) return false;
                    while (rep--) lens[n++] = val;
                }
            }
            lit->build(lens, hlit);
            dist->build(lens + hlit, hdist);
            if (!inflateBlock(in, out, *lit, *dist)) return false;
        } else {
            return false;
        }
        if (final) return true;
    }
}

}  // namespace flash
