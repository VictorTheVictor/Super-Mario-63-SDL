#include "flash/Jpeg.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace flash {
namespace {

const uint8_t kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                             12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                             35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                             58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

struct HuffTable {
    bool defined = false;
    // Canonical decoding: for each length, the max code and the offset into `symbols`.
    int maxCode[18];
    int valPtr[17];
    int minCode[17];
    uint8_t symbols[256];

    void build(const uint8_t counts[16], const uint8_t* syms, int total) {
        std::memcpy(symbols, syms, total);
        int code = 0, k = 0;
        for (int len = 1; len <= 16; len++) {
            valPtr[len] = k;
            minCode[len] = code;
            code += counts[len - 1];
            k += counts[len - 1];
            maxCode[len] = counts[len - 1] ? code - 1 : -1;
            code <<= 1;
        }
        maxCode[17] = 0x7FFFFFFF;
        defined = true;
    }
};

struct Component {
    int id, h, v, tq;
    int td = 0, ta = 0;
    int dcPred = 0;
    int bw = 0, bh = 0;  // size in blocks (padded to MCU)
    std::vector<uint8_t> data;
};

class Decoder {
public:
    bool feed(const uint8_t* p, size_t n);
    bool finish(int& w, int& h, std::vector<uint32_t>& out);

private:
    bool parseSOF(const uint8_t* p, size_t len);
    bool decodeScan(const uint8_t* p, size_t n, size_t& consumed, const uint8_t* hdr, size_t hlen);
    int receive(int s);
    int decodeHuff(const HuffTable& t);
    void resetBits() {
        bitBuf_ = 0;
        bitCnt_ = 0;
    }
    int bit();

    uint16_t qt_[4][64] = {};
    HuffTable dc_[4], ac_[4];
    std::vector<Component> comps_;
    int width_ = 0, height_ = 0, hmax_ = 1, vmax_ = 1, mcux_ = 0, mcuy_ = 0;
    int restart_ = 0;
    bool frame_ = false, decoded_ = false;

    const uint8_t* sp_ = nullptr;
    const uint8_t* se_ = nullptr;
    uint32_t bitBuf_ = 0;
    int bitCnt_ = 0;
    bool hitMarker_ = false;
};

int Decoder::bit() {
    if (bitCnt_ == 0) {
        uint8_t b = 0;
        if (!hitMarker_ && sp_ < se_) {
            b = *sp_++;
            if (b == 0xFF) {
                uint8_t nx = sp_ < se_ ? *sp_ : 0;
                if (nx == 0x00) sp_++;
                else {
                    hitMarker_ = true;
                    sp_--;
                    b = 0;
                }
            }
        }
        bitBuf_ = b;
        bitCnt_ = 8;
    }
    bitCnt_--;
    return (bitBuf_ >> bitCnt_) & 1;
}

int Decoder::receive(int s) {
    int v = 0;
    for (int i = 0; i < s; i++) v = (v << 1) | bit();
    return v;
}

int Decoder::decodeHuff(const HuffTable& t) {
    int code = 0;
    for (int len = 1; len <= 16; len++) {
        code = (code << 1) | bit();
        if (t.maxCode[len] >= 0 && code <= t.maxCode[len])
            return t.symbols[t.valPtr[len] + code - t.minCode[len]];
    }
    return 0;
}

inline int extend(int v, int s) { return s && v < (1 << (s - 1)) ? v - (1 << s) + 1 : v; }

void idct8x8(const int in[64], uint8_t* out, int stride) {
    static float cosTable[8][8];
    static bool init = false;
    if (!init) {
        for (int x = 0; x < 8; x++)
            for (int u = 0; u < 8; u++) {
                float cu = u == 0 ? (float)M_SQRT1_2 : 1.f;
                cosTable[x][u] = cu * std::cos((2 * x + 1) * u * (float)M_PI / 16);
            }
        init = true;
    }
    float tmp[64];
    for (int y = 0; y < 8; y++)  // rows: v -> y
        for (int u = 0; u < 8; u++) {
            float s = 0;
            for (int v = 0; v < 8; v++) s += cosTable[y][v] * in[v * 8 + u];
            tmp[y * 8 + u] = s;
        }
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            float s = 0;
            for (int u = 0; u < 8; u++) s += cosTable[x][u] * tmp[y * 8 + u];
            int v = (int)std::lround(s / 4 + 128);
            out[y * stride + x] = (uint8_t)std::clamp(v, 0, 255);
        }
}

bool Decoder::parseSOF(const uint8_t* p, size_t len) {
    if (len < 6) return false;
    height_ = p[1] << 8 | p[2];
    width_ = p[3] << 8 | p[4];
    int nc = p[5];
    if (len < 6 + (size_t)nc * 3 || nc < 1 || nc > 4) return false;
    comps_.clear();
    hmax_ = vmax_ = 1;
    for (int i = 0; i < nc; i++) {
        Component c;
        c.id = p[6 + i * 3];
        c.h = p[7 + i * 3] >> 4;
        c.v = p[7 + i * 3] & 15;
        c.tq = p[8 + i * 3] & 3;
        if (c.h < 1 || c.v < 1) return false;
        hmax_ = std::max(hmax_, c.h);
        vmax_ = std::max(vmax_, c.v);
        comps_.push_back(c);
    }
    mcux_ = (width_ + 8 * hmax_ - 1) / (8 * hmax_);
    mcuy_ = (height_ + 8 * vmax_ - 1) / (8 * vmax_);
    for (auto& c : comps_) {
        c.bw = mcux_ * c.h;
        c.bh = mcuy_ * c.v;
        c.data.assign((size_t)c.bw * 8 * c.bh * 8, 0);
    }
    frame_ = true;
    return true;
}

bool Decoder::decodeScan(const uint8_t* p, size_t n, size_t& consumed, const uint8_t* hdr,
                         size_t hlen) {
    int ns = hdr[0];
    std::vector<Component*> sc;
    for (int i = 0; i < ns; i++) {
        int cid = hdr[1 + i * 2];
        for (auto& c : comps_)
            if (c.id == cid) {
                c.td = hdr[2 + i * 2] >> 4;
                c.ta = hdr[2 + i * 2] & 15;
                sc.push_back(&c);
            }
    }
    sp_ = p;
    se_ = p + n;
    resetBits();
    hitMarker_ = false;
    for (auto* c : sc) c->dcPred = 0;

    auto decodeBlock = [&](Component& c, int bx, int by) {
        int coef[64] = {};
        int t = decodeHuff(dc_[c.td]);
        int diff = t ? extend(receive(t), t) : 0;
        c.dcPred += diff;
        coef[0] = c.dcPred * qt_[c.tq][0];
        for (int k = 1; k < 64;) {
            int rs = decodeHuff(ac_[c.ta]);
            int r = rs >> 4, s = rs & 15;
            if (s == 0) {
                if (r != 15) break;
                k += 16;
                continue;
            }
            k += r;
            if (k > 63) break;
            coef[kZigzag[k]] = extend(receive(s), s) * qt_[c.tq][k];
            k++;
        }
        size_t stride = (size_t)c.bw * 8;
        idct8x8(coef, &c.data[(size_t)by * 8 * stride + (size_t)bx * 8], (int)stride);
    };

    int total;
    bool single = sc.size() == 1;
    if (single) {
        Component& c = *sc[0];
        int bw = (width_ * c.h / hmax_ + 7) / 8, bh = (height_ * c.v / vmax_ + 7) / 8;
        total = bw * bh;
        for (int i = 0; i < total; i++) {
            if (restart_ && i && i % restart_ == 0) {
                resetBits();
                hitMarker_ = false;
                if (sp_ + 1 < se_ && sp_[0] == 0xFF && sp_[1] >= 0xD0 && sp_[1] <= 0xD7) sp_ += 2;
                c.dcPred = 0;
            }
            decodeBlock(c, i % bw, i / bw);
        }
    } else {
        total = mcux_ * mcuy_;
        for (int m = 0; m < total; m++) {
            if (restart_ && m && m % restart_ == 0) {
                resetBits();
                hitMarker_ = false;
                if (sp_ + 1 < se_ && sp_[0] == 0xFF && sp_[1] >= 0xD0 && sp_[1] <= 0xD7) sp_ += 2;
                for (auto* c : sc) c->dcPred = 0;
            }
            int mx = m % mcux_, my = m / mcux_;
            for (auto* c : sc)
                for (int v = 0; v < c->v; v++)
                    for (int h = 0; h < c->h; h++) decodeBlock(*c, mx * c->h + h, my * c->v + v);
        }
    }
    // Skip to the next marker that is not a restart marker.
    while (sp_ + 1 < se_ && !(sp_[0] == 0xFF && sp_[1] != 0 && !(sp_[1] >= 0xD0 && sp_[1] <= 0xD7)))
        sp_++;
    consumed = (size_t)(sp_ - p);
    decoded_ = true;
    return true;
}

bool Decoder::feed(const uint8_t* p, size_t n) {
    size_t i = 0;
    while (i + 1 < n) {
        if (p[i] != 0xFF) {
            i++;
            continue;
        }
        uint8_t m = p[i + 1];
        i += 2;
        if (m == 0xD8 || m == 0xD9 || m == 0xFF || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) {
            if (m == 0xFF) i--;
            continue;
        }
        if (i + 2 > n) return false;
        size_t len = (size_t)(p[i] << 8 | p[i + 1]);
        if (len < 2 || i + len > n) return false;
        const uint8_t* seg = p + i + 2;
        size_t slen = len - 2;
        switch (m) {
            case 0xDB: {  // DQT
                size_t k = 0;
                while (k < slen) {
                    int pq = seg[k] >> 4, tq = seg[k] & 3;
                    k++;
                    for (int j = 0; j < 64; j++) {
                        if (pq) {
                            qt_[tq][j] = (uint16_t)(seg[k] << 8 | seg[k + 1]);
                            k += 2;
                        } else {
                            qt_[tq][j] = seg[k++];
                        }
                    }
                }
                break;
            }
            case 0xC4: {  // DHT
                size_t k = 0;
                while (k + 17 <= slen) {
                    int tc = seg[k] >> 4, th = seg[k] & 3;
                    const uint8_t* counts = seg + k + 1;
                    int total = 0;
                    for (int j = 0; j < 16; j++) total += counts[j];
                    if (k + 17 + total > slen || total > 256) return false;
                    (tc ? ac_[th] : dc_[th]).build(counts, seg + k + 17, total);
                    k += 17 + total;
                }
                break;
            }
            case 0xDD:  // DRI
                restart_ = seg[0] << 8 | seg[1];
                break;
            case 0xC0:
            case 0xC1:
                if (!parseSOF(seg, slen)) return false;
                break;
            case 0xC2:
                return false;  // progressive: not used by this game
            case 0xDA: {  // SOS
                if (!frame_) return false;
                size_t consumed = 0;
                decodeScan(p + i + len, n - i - len, consumed, seg, slen);
                i += len + consumed;
                continue;
            }
            default:
                break;
        }
        i += len;
    }
    return true;
}

bool Decoder::finish(int& w, int& h, std::vector<uint32_t>& out) {
    if (!frame_ || !decoded_) return false;
    w = width_;
    h = height_;
    out.assign((size_t)w * h, 0xFF000000);
    // Subsampled planes are upsampled with a centred triangle filter (bilinear),
    // like libjpeg's "fancy upsampling".
    auto sample = [&](const Component& c, int x, int y) {
        size_t stride = (size_t)c.bw * 8;
        if (c.h == hmax_ && c.v == vmax_) return (int)c.data[(size_t)y * stride + x];
        int cw = (width_ * c.h + hmax_ - 1) / hmax_, ch = (height_ * c.v + vmax_ - 1) / vmax_;
        float fx = (x + 0.5f) * c.h / hmax_ - 0.5f, fy = (y + 0.5f) * c.v / vmax_ - 0.5f;
        int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
        float tx = fx - x0, ty = fy - y0;
        auto at = [&](int sx, int sy) {
            sx = std::clamp(sx, 0, cw - 1);
            sy = std::clamp(sy, 0, ch - 1);
            return (float)c.data[(size_t)sy * stride + sx];
        };
        float top = at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx;
        float bot = at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx;
        return (int)std::lround(top * (1 - ty) + bot * ty);
    };
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int r, g, b;
            if (comps_.size() >= 3) {
                int Y = sample(comps_[0], x, y), cb = sample(comps_[1], x, y) - 128,
                    cr = sample(comps_[2], x, y) - 128;
                r = Y + (int)std::lround(1.402 * cr);
                g = Y - (int)std::lround(0.344136 * cb + 0.714136 * cr);
                b = Y + (int)std::lround(1.772 * cb);
            } else {
                r = g = b = sample(comps_[0], x, y);
            }
            r = std::clamp(r, 0, 255);
            g = std::clamp(g, 0, 255);
            b = std::clamp(b, 0, 255);
            out[(size_t)y * w + x] = 0xFF000000u | r << 16 | g << 8 | b;
        }
    return true;
}

}  // namespace

bool decodeJpeg(const std::vector<std::pair<const uint8_t*, size_t>>& streams, int& width,
                int& height, std::vector<uint32_t>& pixels) {
    Decoder d;
    for (auto& [p, n] : streams)
        if (!d.feed(p, n)) return false;
    return d.finish(width, height, pixels);
}

}  // namespace flash
