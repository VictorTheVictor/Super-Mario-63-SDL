#include "flash/Raster.h"

#include <cmath>

#include "flash/Swf.h"

namespace flash {
namespace {

inline uint32_t premultiply(Color c) {
    uint32_t a = c.a;
    uint32_t r = (c.r * a + 127) / 255, g = (c.g * a + 127) / 255, b = (c.b * a + 127) / 255;
    return a << 24 | r << 16 | g << 8 | b;
}

inline uint32_t applyCxPremul(const ColorTransform& cx, uint32_t p) {
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

// src-over with coverage (0..256)
inline void blend(uint32_t& d, uint32_t s, uint32_t cov) {
    if (cov >= 256 && (s >> 24) == 255) {
        d = s;
        return;
    }
    uint32_t sa = ((s >> 24) * cov) >> 8;
    uint32_t sr = ((s >> 16 & 255) * cov) >> 8;
    uint32_t sg = ((s >> 8 & 255) * cov) >> 8;
    uint32_t sb = ((s & 255) * cov) >> 8;
    uint32_t inv = 255 - sa;
    uint32_t da = (d >> 24) * inv / 255 + sa;
    uint32_t dr = (d >> 16 & 255) * inv / 255 + sr;
    uint32_t dg = (d >> 8 & 255) * inv / 255 + sg;
    uint32_t db = (d & 255) * inv / 255 + sb;
    d = std::min(da, 255u) << 24 | std::min(dr, 255u) << 16 | std::min(dg, 255u) << 8 |
        std::min(db, 255u);
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
        x %= b.width;
        y %= b.height;
        if (x < 0) x += b.width;
        if (y < 0) y += b.height;
    } else {
        x = x < 0 ? 0 : x >= b.width ? b.width - 1 : x;
        y = y < 0 ? 0 : y >= b.height ? b.height - 1 : y;
    }
    return b.pixels[(size_t)y * b.width + x];
}

inline uint32_t sampleBilinear(const Bitmap& b, double u, double v, bool repeat) {
    u -= 0.5;
    v -= 0.5;
    double fu = std::floor(u), fv = std::floor(v);
    int x = (int)fu, y = (int)fv;
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

void accumulate(float* acc, int stride, int h, double x0, double y0, double x1, double y1) {
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
            Rasterizer::maxOvershoot = std::max(Rasterizer::maxOvershoot, over);
        }
        x = std::clamp(x, 0.0, (double)(stride - 2));
        xn = std::clamp(xn, 0.0, (double)(stride - 2));
        double d = dy * dir;
        double xa = std::min(x, xn), xb = std::max(x, xn);
        double xaf = std::floor(xa);
        int xai = (int)xaf;
        double xbc = std::ceil(xb);
        int xbi = (int)xbc;
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

size_t Rasterizer::rejectedSegments = 0;
size_t Rasterizer::clampedRows = 0;
double Rasterizer::maxOvershoot = 0;

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
    acc_.assign((size_t)stride * bh, 0.f);

    // Clip horizontally to [0, bw]; parts outside collapse onto the border, which keeps
    // the winding contribution for pixels inside.
    auto emit = [&](double x0, double y0, double x1, double y1) {
        x0 = std::clamp(x0, 0.0, (double)bw);
        x1 = std::clamp(x1, 0.0, (double)bw);
        accumulate(acc_.data(), stride, bh, x0, y0, x1, y1);
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

    // Paint setup.
    const FillStyle* fs = paint.style;
    uint32_t solid = 0;
    uint32_t ramp[256];
    Matrix inv;
    const Bitmap* bmp = paint.bitmap;
    bool repeat = false, smooth = false, identityCx = paint.cxform.isIdentity();
    enum { kSolid, kLinear, kRadial, kBitmap } mode = kSolid;
    if (!fs || fs->type == FillStyle::Solid) {
        solid = premultiply(applyCx(paint.cxform, fs ? fs->color : paint.color));
    } else if (fs->isGradient()) {
        buildRamp(*fs, paint.cxform, ramp);
        inv = (paint.toDevice * fs->matrix).inverse();
        mode = fs->type == FillStyle::LinearGradient ? kLinear : kRadial;
    } else if (fs->isBitmap() && bmp && bmp->width > 0 && bmp->height > 0) {
        // Bitmap fill matrices map bitmap pixels to twips.
        inv = (paint.toDevice * fs->matrix).inverse();
        repeat = fs->type == FillStyle::RepeatingBitmap || fs->type == FillStyle::RepeatingBitmapHard;
        smooth = fs->type == FillStyle::RepeatingBitmap || fs->type == FillStyle::ClippedBitmap;
        mode = kBitmap;
    } else {
        return;  // missing bitmap: nothing to draw (drawShape normally skips these)
    }

    for (int y = 0; y < bh; y++) {
        float* row = acc_.data() + (size_t)y * stride;
        uint32_t* out = dst.pixels.data() + (size_t)(bb.y0 + y) * dst.width + bb.x0;
        float sum = 0;
        double py = bb.y0 + y + 0.5;
        for (int x = 0; x < bw; x++) {
            sum += row[x];
            float a = std::fabs(sum);
            uint32_t cov = a >= 1.f ? 256 : (uint32_t)(a * 256.f);
            if (cov < 2) continue;
            double px = bb.x0 + x + 0.5;
            uint32_t c = solid;
            switch (mode) {
                case kSolid:
                    break;
                case kLinear: {
                    double gx = inv.a * px + inv.c * py + inv.tx;
                    c = ramp[spreadIndex((gx + 16384) / 32768, fs->spread)];
                    break;
                }
                case kRadial: {
                    double gx = inv.a * px + inv.c * py + inv.tx;
                    double gy = inv.b * px + inv.d * py + inv.ty;
                    c = ramp[spreadIndex(std::sqrt(gx * gx + gy * gy) / 16384, fs->spread)];
                    break;
                }
                case kBitmap: {
                    double u = inv.a * px + inv.c * py + inv.tx;
                    double v = inv.b * px + inv.d * py + inv.ty;
                    c = smooth ? sampleBilinear(*bmp, u, v, repeat)
                               : texel(*bmp, (int)std::floor(u), (int)std::floor(v), repeat);
                    if (!identityCx) c = applyCxPremul(paint.cxform, c);
                    break;
                }
            }
            if (c >> 24) blend(out[x], c, cov);
        }
    }
}

void drawShape(Rasterizer& r, Surface& dst, const IntRect& clip, const ShapeDef& shape,
               const Matrix& m, const ColorTransform& cx, const Movie& movie) {
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
            double w = ls.width * ((ls.noHScale || ls.noVScale) ? 0.05 : scale);
            double hw = std::max(0.5, w / 2);
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
