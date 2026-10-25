#include "flash/Raster.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "flash/Swf.h"

namespace flash {
namespace {

inline uint32_t premultiply(Color c) {
    uint32_t a = c.a;
    uint32_t r = (c.r * a + 127) / 255, g = (c.g * a + 127) / 255, b = (c.b * a + 127) / 255;
    return a << 24 | r << 16 | g << 8 | b;
}

// Multiplies all four channels by k/255 (rounded), two channels per 32-bit lane.
inline uint32_t scale255(uint32_t p, uint32_t k) {
    uint32_t rb = (p & 0x00FF00FFu) * k;
    uint32_t ag = (p >> 8 & 0x00FF00FFu) * k;
    rb = (rb + 0x00800080u + (rb >> 8 & 0x00FF00FFu)) >> 8 & 0x00FF00FFu;
    ag = (ag + 0x00800080u + (ag >> 8 & 0x00FF00FFu)) & 0xFF00FF00u;
    return rb | ag;
}

inline uint32_t applyCxPremul(const ColorTransform& cx, uint32_t p) {
    if (cx.rMul == 1 && cx.gMul == 1 && cx.bMul == 1 && cx.rAdd == 0 && cx.gAdd == 0 && cx.bAdd == 0 &&
        cx.aAdd == 0 && cx.aMul >= 0 && cx.aMul <= 1)
        return scale255(p, (uint32_t)(cx.aMul * 255 + 0.5f));  // fades: premultiplied scale
    uint32_t a = p >> 24;
    if (a == 0 && cx.aAdd <= 0) return 0;
    Color c;
    c.a = (uint8_t)a;
    if (a) {
        c.r = (uint8_t)std::min(255u, ((p >> 16 & 255) * 255 + a / 2) / a);
        c.g = (uint8_t)std::min(255u, ((p >> 8 & 255) * 255 + a / 2) / a);
        c.b = (uint8_t)std::min(255u, ((p & 255) * 255 + a / 2) / a);
    }
    return premultiply(applyCx(cx, c));
}

// src-over of premultiplied `s` with coverage (0..256)
inline void blend(uint32_t& d, uint32_t s, uint32_t cov) {
    if (cov < 256) {
        // Scale by coverage: channels stay <= alpha, so the sum below cannot carry.
        s = ((s & 0x00FF00FFu) * cov >> 8 & 0x00FF00FFu) | ((s >> 8 & 0x00FF00FFu) * cov & 0xFF00FF00u);
    } else if ((s >> 24) == 255) {
        d = s;
        return;
    }
    d = s + scale255(d, 255 - (s >> 24));
}

// blend() at full coverage.
inline void blendFull(uint32_t& d, uint32_t s) {
    uint32_t a = s >> 24;
    if (a == 255) d = s;
    else if (a) d = s + scale255(d, 255 - a);
}

// Paints `n` fully covered pixels of one color.
inline void fillSpan(uint32_t* out, int n, uint32_t c) {
    uint32_t a = c >> 24;
    if (a == 255) std::fill(out, out + n, c);
    else if (a)
        for (int i = 0; i < n; i++) out[i] = c + scale255(out[i], 255 - a);
}

// Paints `n` fully covered pixels from a sampler's color() and step(), leaving it `n`
// pixels further along.
template <class Sampler>
inline void sampleSpan(Sampler& s, uint32_t* out, int n) {
    for (int i = 0; i < n; i++, s.step()) blendFull(out[i], s.color());
}

// floor() for values well inside the int range, without a libm call.
inline int floorInt(double v) {
    int i = (int)v;
    return i - (v < i);
}

// Wraps t into [0, n) for repeating bitmaps; `invN` is 1.0 / n.
inline int wrapIndex(int t, int n, double invN) {
    if ((unsigned)t < (unsigned)n) return t;
    t -= floorInt(t * invN) * n;
    return t < 0 ? t + n : t >= n ? t - n : t;
}

void buildRamp(const FillStyle& f, const ColorTransform& cx, uint32_t ramp[256]) {
    const auto& st = f.stops;
    if (st.empty()) {
        std::fill(ramp, ramp + 256, 0);
        return;
    }
    for (int i = 0; i < 256; i++) {
        Color c;
        if (i <= st.front().ratio) c = st.front().color;
        else if (i >= st.back().ratio) c = st.back().color;
        else {
            size_t k = 1;
            while (k < st.size() && st[k].ratio < i) k++;
            const auto& a = st[k - 1];
            const auto& b = st[k];
            double t = b.ratio == a.ratio ? 0 : double(i - a.ratio) / (b.ratio - a.ratio);
            auto lerp = [t](uint8_t x, uint8_t y) { return (uint8_t)std::lround(x + (y - x) * t); };
            c = {lerp(a.color.r, b.color.r), lerp(a.color.g, b.color.g), lerp(a.color.b, b.color.b),
                 lerp(a.color.a, b.color.a)};
        }
        ramp[i] = premultiply(applyCx(cx, c));
    }
}

inline int spreadIndex(double t, uint8_t spread) {
    if (spread == 1) {  // reflect
        t = std::fmod(std::fabs(t), 2.0);
        if (t > 1) t = 2 - t;
    } else if (spread == 2) {  // repeat
        t -= std::floor(t);
    }
    int i = (int)(t * 255 + 0.5);
    return i < 0 ? 0 : i > 255 ? 255 : i;
}

inline uint32_t texel(const Bitmap& b, int x, int y, bool repeat) {
    if (repeat) {
        x = wrapIndex(x, b.width, 1.0 / b.width);
        y = wrapIndex(y, b.height, 1.0 / b.height);
    } else {
        x = x < 0 ? 0 : x >= b.width ? b.width - 1 : x;
        y = y < 0 ? 0 : y >= b.height ? b.height - 1 : y;
    }
    return b.pixels[(size_t)y * b.width + x];
}

inline uint32_t sampleBilinear(const Bitmap& b, double u, double v, bool repeat) {
    u -= 0.5;
    v -= 0.5;
    int x = floorInt(u), y = floorInt(v);
    double fu = x, fv = y;
    uint32_t wx = (uint32_t)((u - fu) * 256), wy = (uint32_t)((v - fv) * 256);
    uint32_t p00 = texel(b, x, y, repeat), p10 = texel(b, x + 1, y, repeat);
    uint32_t p01 = texel(b, x, y + 1, repeat), p11 = texel(b, x + 1, y + 1, repeat);
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t c00 = p00 >> sh & 255, c10 = p10 >> sh & 255;
        uint32_t c01 = p01 >> sh & 255, c11 = p11 >> sh & 255;
        uint32_t top = c00 * (256 - wx) + c10 * wx;
        uint32_t bot = c01 * (256 - wx) + c11 * wx;
        uint32_t c = (top * (256 - wy) + bot * wy) >> 16;
        out |= std::min(c, 255u) << sh;
    }
    return out;
}

// Bitmaps with a color transform applied to every texel. A nearest-texel fill reads each
// texel as is, so painting from such a copy gives exactly what transforming every sampled
// texel would, without redoing the transform per pixel each frame (the castle's darkened
// sky). A copy is made once fills with that bitmap and transform have covered about as
// many pixels as it has texels, so transforms that change every frame (fades) don't pay
// for copies they never reuse.
class TransformedBitmaps {
public:
    std::shared_ptr<const Bitmap> get(const Bitmap& b, const ColorTransform& cx, int64_t pixels) {
        Key key{&b, {}};
        std::memcpy(key.cx, &cx, sizeof key.cx);
        std::lock_guard<std::mutex> lock(mutex_);
        Entry& e = entries_[key];
        e.lastUse = ++uses_;
        if (e.copy) return e.copy;
        e.pixels += pixels;
        if (e.pixels < (int64_t)b.width * b.height) {
            if (entries_.size() > kMaxEntries) trim();
            return nullptr;
        }
        auto copy = std::make_shared<Bitmap>();
        copy->width = b.width;
        copy->height = b.height;
        copy->pixels.resize(b.pixels.size());
        for (size_t i = 0; i < b.pixels.size(); i++) copy->pixels[i] = applyCxPremul(cx, b.pixels[i]);
        e.copy = copy;
        bytes_ += copy->pixels.size() * 4;
        if (bytes_ > kMaxBytes || entries_.size() > kMaxEntries) trim();
        return copy;
    }

private:
    struct Key {
        const Bitmap* bitmap;
        float cx[8];
        bool operator==(const Key& o) const { return bitmap == o.bitmap && !std::memcmp(cx, o.cx, sizeof cx); }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            uint64_t h = (uint64_t)(uintptr_t)k.bitmap * 0x9E3779B97F4A7C15ull;
            uint32_t bits[8];
            std::memcpy(bits, k.cx, sizeof bits);
            for (uint32_t v : bits) h = (h ^ v) * 0x100000001B3ull;
            return (size_t)h;
        }
    };
    struct Entry {
        std::shared_ptr<const Bitmap> copy;
        int64_t pixels = 0;
        uint64_t lastUse = 0;
    };
    static constexpr size_t kMaxBytes = 64u << 20, kMaxEntries = 4096;

    // Drops the least recently used half (copies still being painted from stay alive
    // through their shared pointers).
    void trim() {
        std::vector<uint64_t> uses;
        for (auto& [k, e] : entries_) uses.push_back(e.lastUse);
        std::nth_element(uses.begin(), uses.begin() + uses.size() / 2, uses.end());
        uint64_t cutoff = uses[uses.size() / 2];
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it->second.lastUse < cutoff) {
                if (it->second.copy) bytes_ -= it->second.copy->pixels.size() * 4;
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::mutex mutex_;
    std::unordered_map<Key, Entry, KeyHash> entries_;
    uint64_t uses_ = 0;
    size_t bytes_ = 0;
};

TransformedBitmaps transformedBitmaps;

// Adds a line's signed area coverage to the accumulation rows, widening each touched
// row's [rowMin, rowMax] cell range.
void accumulate(float* acc, int stride, int h, int* rowMin, int* rowMax, double x0, double y0, double x1,
                double y1) {
    if (y0 == y1) return;
    double dir = 1;
    if (y0 > y1) {
        std::swap(x0, x1);
        std::swap(y0, y1);
        dir = -1;
    }
    double dxdy = (x1 - x0) / (y1 - y0);
    double x = x0;
    if (y0 < 0) x -= y0 * dxdy;
    int ys = std::max(0, (int)std::floor(y0));
    int ye = std::min(h, (int)std::ceil(y1));
    for (int y = ys; y < ye; y++) {
        float* row = acc + (size_t)y * stride;
        double dy = std::min((double)y + 1, y1) - std::max((double)y, y0);
        double xn = x + dxdy * dy;
        // Interpolation can leave [0, width] by rounding (e.g. -1e-12): floor() would then
        // index one cell before the row, i.e. before the buffer on the first row.
        if (x < 0 || xn < 0 || x > stride - 2 || xn > stride - 2) {
            Rasterizer::clampedRows++;
            double over = std::max({-x, -xn, x - (stride - 2), xn - (stride - 2)});
            double seen = Rasterizer::maxOvershoot.load(std::memory_order_relaxed);
            while (over > seen && !Rasterizer::maxOvershoot.compare_exchange_weak(seen, over)) {
            }
        }
        x = std::clamp(x, 0.0, (double)(stride - 2));
        xn = std::clamp(xn, 0.0, (double)(stride - 2));
        double d = dy * dir;
        double xa = std::min(x, xn), xb = std::max(x, xn);
        double xaf = std::floor(xa);
        int xai = (int)xaf;
        double xbc = std::ceil(xb);
        int xbi = (int)xbc;
        rowMin[y] = std::min(rowMin[y], xai);
        rowMax[y] = std::max(rowMax[y], std::max(xbi, xai + 1));
        if (xbi <= xai + 1) {
            double xmf = 0.5 * (x + xn) - xaf;
            row[xai] += (float)(d - d * xmf);
            row[xai + 1] += (float)(d * xmf);
        } else {
            double s = 1.0 / (xb - xa);
            double xf = xa - xaf;
            double a0 = 0.5 * s * (1 - xf) * (1 - xf);
            double xbf = xb - xbc + 1;
            double am = 0.5 * s * xbf * xbf;
            row[xai] += (float)(d * a0);
            if (xbi == xai + 2) {
                row[xai + 1] += (float)(d * (1 - a0 - am));
            } else {
                double a1 = s * (1.5 - xf);
                row[xai + 1] += (float)(d * (a1 - a0));
                for (int xi = xai + 2; xi < xbi - 1; xi++) row[xi] += (float)(d * s);
                double a2 = a1 + (xbi - xai - 3) * s;
                row[xbi - 1] += (float)(d * (1 - a2 - am));
            }
            row[xbi] += (float)(d * am);
        }
        x = xn;
    }
}

}  // namespace

RenderSettings renderSettings;

std::atomic<size_t> Rasterizer::rejectedSegments{0};
std::atomic<size_t> Rasterizer::clampedRows{0};
std::atomic<double> Rasterizer::maxOvershoot{0};

void Rasterizer::line(Point a, Point b) {
    if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y)) {
        rejectedSegments++;
        return;
    }
    const double kLimit = 1e6;  // device pixels; far outside any clip
    auto c = [kLimit](double v) { return std::clamp(v, -kLimit, kLimit); };
    segs_.push_back({c(a.x), c(a.y), c(b.x), c(b.y)});
}

namespace {
// Subdivision count for a quadratic curve with the given control deviation (pixels).
int curveSteps(double dd) {
    if (!std::isfinite(dd)) return 1;
    return std::max(1, std::min(64, (int)std::ceil(std::sqrt(std::min(dd, 1e6) * 2))));
}
}  // namespace

void Rasterizer::quad(Point a, Point c, Point b) {
    double ddx = a.x - 2 * c.x + b.x, ddy = a.y - 2 * c.y + b.y;
    int n = curveSteps(std::sqrt(ddx * ddx + ddy * ddy));
    Point prev = a;
    for (int i = 1; i <= n; i++) {
        double t = (double)i / n, u = 1 - t;
        Point p{u * u * a.x + 2 * u * t * c.x + t * t * b.x, u * u * a.y + 2 * u * t * c.y + t * t * b.y};
        line(prev, p);
        prev = p;
    }
}

void Rasterizer::closedPolygon(const Point* pts, int n) {
    // Orient counter-clockwise (positive area) so overlapping pieces add up.
    double area = 0;
    for (int i = 0; i < n; i++) {
        const Point& p = pts[i];
        const Point& q = pts[(i + 1) % n];
        area += p.x * q.y - q.x * p.y;
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        if (area >= 0) line(pts[i], pts[j]);
        else line(pts[j], pts[i]);
    }
}

void Rasterizer::addPath(const std::vector<Edge>& edges, const Matrix& m) {
    for (const Edge& e : edges) {
        if (e.curved) quad(m.apply(e.from), m.apply(e.ctrl), m.apply(e.to));
        else line(m.apply(e.from), m.apply(e.to));
    }
}

void Rasterizer::addStroke(const std::vector<Edge>& edges, const Matrix& m, double hw) {
    // Flatten into device-space points.
    std::vector<Point> pts;
    for (const Edge& e : edges) {
        Point a = m.apply(e.from), b = m.apply(e.to);
        if (pts.empty()) pts.push_back(a);
        if (e.curved) {
            Point c = m.apply(e.ctrl);
            double ddx = a.x - 2 * c.x + b.x, ddy = a.y - 2 * c.y + b.y;
            int n = curveSteps(std::sqrt(ddx * ddx + ddy * ddy));
            for (int i = 1; i <= n; i++) {
                double t = (double)i / n, u = 1 - t;
                pts.push_back({u * u * a.x + 2 * u * t * c.x + t * t * b.x,
                               u * u * a.y + 2 * u * t * c.y + t * t * b.y});
            }
        } else {
            pts.push_back(b);
        }
    }
    if (!std::isfinite(hw)) return;
    int circleSegs = std::max(6, std::min(32, (int)std::min(hw * 2, 64.0)));
    auto disc = [&](Point c) {
        Point ring[32];
        for (int i = 0; i < circleSegs; i++) {
            double ang = 2 * M_PI * i / circleSegs;
            ring[i] = {c.x + hw * std::cos(ang), c.y + hw * std::sin(ang)};
        }
        closedPolygon(ring, circleSegs);
    };
    for (size_t i = 0; i + 1 < pts.size(); i++) {
        Point a = pts[i], b = pts[i + 1];
        double dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-9) continue;
        double nx = -dy / len * hw, ny = dx / len * hw;
        Point q[4] = {{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}};
        closedPolygon(q, 4);
    }
    if (hw >= 0.75)
        for (const Point& p : pts) disc(p);
}

void Rasterizer::fill(Surface& dst, const IntRect& clip, const Paint& paint) {
    if (segs_.empty()) return;
    double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30;
    for (const Seg& s : segs_) {
        minX = std::min({minX, s.x0, s.x1});
        maxX = std::max({maxX, s.x0, s.x1});
        minY = std::min({minY, s.y0, s.y1});
        maxY = std::max({maxY, s.y0, s.y1});
    }
    IntRect bb{std::max(clip.x0, (int)std::floor(minX)), std::max(clip.y0, (int)std::floor(minY)),
               std::min(clip.x1, (int)std::ceil(maxX)), std::min(clip.y1, (int)std::ceil(maxY))};
    if (bb.empty()) {
        segs_.clear();
        return;
    }
    int bw = bb.x1 - bb.x0, bh = bb.y1 - bb.y0, stride = bw + 2;
    // acc_ is all zeros between fills (rows are cleared as they are painted), so it only
    // needs to grow, not to be cleared for the whole box.
    if (acc_.size() < (size_t)stride * bh) acc_.resize((size_t)stride * bh, 0.f);
    rowMin_.assign(bh, stride);
    rowMax_.assign(bh, -1);

    // Clip horizontally to [0, bw]; parts outside collapse onto the border, which keeps
    // the winding contribution for pixels inside.
    auto emit = [&](double x0, double y0, double x1, double y1) {
        x0 = std::clamp(x0, 0.0, (double)bw);
        x1 = std::clamp(x1, 0.0, (double)bw);
        accumulate(acc_.data(), stride, bh, rowMin_.data(), rowMax_.data(), x0, y0, x1, y1);
    };
    for (const Seg& s : segs_) {
        double x0 = s.x0 - bb.x0, y0 = s.y0 - bb.y0, x1 = s.x1 - bb.x0, y1 = s.y1 - bb.y0;
        if ((y0 < 0 && y1 < 0) || (y0 >= bh && y1 >= bh)) continue;
        double cuts[2];
        int nc = 0;
        for (double bx : {0.0, (double)bw}) {
            if ((x0 < bx && x1 > bx) || (x0 > bx && x1 < bx)) cuts[nc++] = (bx - x0) / (x1 - x0);
        }
        if (nc == 2 && cuts[0] > cuts[1]) std::swap(cuts[0], cuts[1]);
        double px = x0, py = y0;
        for (int i = 0; i < nc; i++) {
            double qx = x0 + (x1 - x0) * cuts[i], qy = y0 + (y1 - y0) * cuts[i];
            emit(px, py, qx, qy);
            px = qx;
            py = qy;
        }
        emit(px, py, x1, y1);
    }
    segs_.clear();

    // Paint setup. A sampler gives the color of consecutive pixels along a row:
    // row(x, py) positions it at pixel x, step() moves one pixel right, color() samples,
    // and span(out, n) paints n fully covered pixels from there and moves past them.
    const FillStyle* fs = paint.style;
    const Bitmap* bmp = paint.bitmap;
    bool identityCx = paint.cxform.isIdentity();
    auto paintRows = [&](auto& sampler) {
        for (int y = 0; y < bh; y++) {
            int x0 = rowMin_[y], x1 = rowMax_[y];
            if (x0 > x1) continue;  // nothing touched this row
            float* row = acc_.data() + (size_t)y * stride;
            uint32_t* out = dst.pixels.data() + (size_t)(bb.y0 + y) * dst.width + bb.x0;
            float sum = 0;
            sampler.row(bb.x0 + x0, bb.y0 + y + 0.5);
            for (int x = x0; x < bw;) {
                sum += row[x];
                float a = std::fabs(sum);
                uint32_t cov = a >= 1.f ? 256 : (uint32_t)(a * 256.f);
                if (cov < 2) {
                    if (x > x1) break;  // past the last edge: the rest of the row is empty
                    x++, sampler.step();
                    continue;
                }
                if (cov == 256) {
                    // Inside the shape: cells between edges (and past the row's last one)
                    // accumulate nothing, so coverage stays full until the next edge.
                    int end = x + 1;
                    while (end < bw && (end > x1 || row[end] == 0.f)) end++;
                    sampler.span(out + x, end - x);
                    x = end;
                    continue;
                }
                uint32_t c = sampler.color();
                if (c >> 24) blend(out[x], c, cov);
                x++, sampler.step();
            }
            std::fill(row + x0, row + std::min(x1 + 1, stride), 0.f);
        }
    };
    auto clearRows = [&] {
        for (int y = 0; y < bh; y++)
            if (rowMin_[y] <= rowMax_[y]) {
                float* row = acc_.data() + (size_t)y * stride;
                std::fill(row + rowMin_[y], row + std::min(rowMax_[y] + 1, stride), 0.f);
            }
    };

    if (!fs || fs->type == FillStyle::Solid) {
        struct {
            uint32_t c;
            void row(int, double) {}
            void step() {}
            uint32_t color() const { return c; }
            void span(uint32_t* out, int n) const { fillSpan(out, n, c); }
        } solid{premultiply(applyCx(paint.cxform, fs ? fs->color : paint.color))};
        if (solid.c >> 24) paintRows(solid);
        else clearRows();
        return;
    }
    // Samplers evaluate the fill's inverse matrix at each pixel center exactly as
    // inv.apply(px, py) would (accumulating per-pixel steps instead drifts and flips
    // texels that sit exactly on a pixel boundary).
    if (fs->isGradient()) {
        uint32_t ramp[256];
        buildRamp(*fs, paint.cxform, ramp);
        Matrix inv = (paint.toDevice * fs->matrix).inverse();
        bool linear = fs->type == FillStyle::LinearGradient;
        struct {
            const Matrix& inv;
            const uint32_t* ramp;
            uint8_t spread;
            bool linear;
            double px = 0, py = 0;
            void row(int x, double y) {
                px = x + 0.5;
                py = y;
            }
            void step() { px += 1; }
            uint32_t color() const {
                double gx = inv.a * px + inv.c * py + inv.tx;
                if (linear) return ramp[spreadIndex((gx + 16384) / 32768, spread)];
                double gy = inv.b * px + inv.d * py + inv.ty;
                return ramp[spreadIndex(std::sqrt(gx * gx + gy * gy) / 16384, spread)];
            }
            void span(uint32_t* out, int n) {
                // A linear gradient whose ramp runs vertically has one color per row.
                if (linear && inv.a == 0) {
                    fillSpan(out, n, color());
                    px += n;
                } else {
                    sampleSpan(*this, out, n);
                }
            }
        } gradient{inv, ramp, fs->spread, linear};
        paintRows(gradient);
        return;
    }
    if (!fs->isBitmap() || !bmp || bmp->width <= 0 || bmp->height <= 0) {
        clearRows();
        return;  // missing bitmap: nothing to draw (drawShape normally skips these)
    }
    // Bitmap fill matrices map bitmap pixels to twips.
    Matrix inv = (paint.toDevice * fs->matrix).inverse();
    bool repeat = fs->type == FillStyle::RepeatingBitmap || fs->type == FillStyle::RepeatingBitmapHard;
    bool smooth = fs->type == FillStyle::RepeatingBitmap || fs->type == FillStyle::ClippedBitmap;
    // Magnified: one device pixel spans less than one texel.
    if (renderSettings.crispBitmaps && std::fabs(inv.a * inv.d - inv.b * inv.c) < 1) smooth = false;
    if (smooth) {
        struct {
            const Matrix& inv;
            const Bitmap& bmp;
            bool repeat, identityCx;
            const ColorTransform& cx;
            double px = 0, py = 0;
            void row(int x, double y) {
                px = x + 0.5;
                py = y;
            }
            void step() { px += 1; }
            uint32_t color() const {
                double u = inv.a * px + inv.c * py + inv.tx;
                double v = inv.b * px + inv.d * py + inv.ty;
                uint32_t c = sampleBilinear(bmp, u, v, repeat);
                return identityCx ? c : applyCxPremul(cx, c);
            }
            void span(uint32_t* out, int n) { sampleSpan(*this, out, n); }
        } bilinear{inv, *bmp, repeat, identityCx, paint.cxform};
        paintRows(bilinear);
        return;
    }
    // Nearest texel. For repeating fills, `baseX`/`baseY` track which repeat of the
    // bitmap the row is in, so wrapping is usually a compare instead of a division. With a
    // color transform, texels come from a transformed copy once it pays off.
    std::shared_ptr<const Bitmap> transformed;
    if (!identityCx && (transformed = transformedBitmaps.get(*bmp, paint.cxform, (int64_t)bw * bh))) {
        bmp = transformed.get();
        identityCx = true;
    }
    struct {
        const Matrix& inv;
        const Bitmap& bmp;
        bool repeat, identityCx;
        const ColorTransform& cx;
        double invW, invH;
        double px = 0, py = 0;
        int baseX = 0, baseY = 0;
        void row(int x, double y) {
            px = x + 0.5;
            py = y;
        }
        void step() { px += 1; }
        static int wrap(int t, int n, double invN, int& base) {
            int r = t - base;
            if ((unsigned)r >= (unsigned)n) {
                base = floorInt(t * invN) * n;
                r = t - base;
                if (r < 0) r += n, base -= n;
                else if (r >= n) r -= n, base += n;
            }
            return r;
        }
        uint32_t color() {
            int tx = floorInt(inv.a * px + inv.c * py + inv.tx);
            int ty = floorInt(inv.b * px + inv.d * py + inv.ty);
            if (repeat) {
                tx = wrap(tx, bmp.width, invW, baseX);
                ty = wrap(ty, bmp.height, invH, baseY);
            } else {
                tx = std::clamp(tx, 0, bmp.width - 1);
                ty = std::clamp(ty, 0, bmp.height - 1);
            }
            uint32_t c = bmp.pixels[(size_t)ty * bmp.width + tx];
            return identityCx ? c : applyCxPremul(cx, c);
        }
        void span(uint32_t* out, int n) {
            if (inv.b != 0 || !identityCx) {
                sampleSpan(*this, out, n);
                return;
            }
            // Unrotated: the texel row is the same along the span (the same expression
            // color() evaluates, so the same texels).
            int ty = floorInt(inv.b * px + inv.d * py + inv.ty);
            ty = repeat ? wrap(ty, bmp.height, invH, baseY) : std::clamp(ty, 0, bmp.height - 1);
            const uint32_t* texels = bmp.pixels.data() + (size_t)ty * bmp.width;
            for (int i = 0; i < n; i++, px += 1) {
                int tx = floorInt(inv.a * px + inv.c * py + inv.tx);
                tx = repeat ? wrap(tx, bmp.width, invW, baseX) : std::clamp(tx, 0, bmp.width - 1);
                blendFull(out[i], texels[tx]);
            }
        }
    } nearest{inv, *bmp, repeat, identityCx, paint.cxform, 1.0 / bmp->width, 1.0 / bmp->height};
    paintRows(nearest);
}

void gradientRamp(const FillStyle& f, const ColorTransform& cx, uint32_t ramp[256]) { buildRamp(f, cx, ramp); }

void drawShape(Rasterizer& r, Surface& dst, const IntRect& clip, const ShapeDef& shape,
               const Matrix& m, const ColorTransform& cx, const Movie& movie) {
    const Rect& sb = shape.bounds;
    if (!sb.empty()) {
        Rect b = m.apply(sb);
        // Bounds include stroke widths; allow for hairlines and antialiasing.
        double pad = 1 + renderSettings.scale;
        if (b.xMax + pad < clip.x0 || b.xMin - pad > clip.x1 || b.yMax + pad < clip.y0 || b.yMin - pad > clip.y1)
            return;
    }
    double scale = std::sqrt(std::fabs(m.a * m.d - m.b * m.c));
    for (const auto& layer : shape.layers) {
        for (const FillPath& fp : layer.fills) {
            if (fp.style < 0 || fp.style >= (int)shape.fills.size()) continue;
            const FillStyle& fs = shape.fills[fp.style];
            Paint p;
            p.style = &fs;
            p.toDevice = m;
            p.cxform = cx;
            if (fs.isBitmap()) {
                // Flash exports "no bitmap" fills (id 65535) along edges of broken-apart
                // bitmaps; they draw nothing.
                auto* c = movie.character(fs.bitmapId);
                if (!c || c->kind != Character::Bitmap) continue;
                p.bitmap = static_cast<const Bitmap*>(c);
            }
            r.reset();
            r.addPath(fp.edges, m);
            r.fill(dst, clip, p);
        }
        for (const StrokePath& sp : layer.strokes) {
            if (sp.style < 0 || sp.style >= (int)shape.lines.size()) continue;
            const LineStyle& ls = shape.lines[sp.style];
            double w = ls.width * ((ls.noHScale || ls.noVScale) ? 0.05 * renderSettings.scale : scale);
            double hw = std::max(0.5 * renderSettings.scale, w / 2);
            r.reset();
            for (const auto& pl : sp.polylines) r.addStroke(pl, m, hw);
            Paint p;
            p.color = ls.color;
            p.cxform = cx;
            p.toDevice = m;
            if (ls.hasFill && !ls.fill.isBitmap()) p.style = &ls.fill;
            r.fill(dst, clip, p);
        }
    }
}

}  // namespace flash
