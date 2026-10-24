#include "flash/Effects.h"

#include <algorithm>
#include <cmath>

namespace flash {
namespace {

using Plane = std::vector<float>;

struct RGBA {
    float r, g, b, a;  // premultiplied, 0..1
};

inline RGBA unpack(uint32_t p) {
    return {(p >> 16 & 255) / 255.f, (p >> 8 & 255) / 255.f, (p & 255) / 255.f, (p >> 24) / 255.f};
}

inline uint32_t pack(RGBA c) {
    auto q = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.f, 1.f) * 255.f); };
    uint32_t a = q(c.a);
    uint32_t r = std::min(q(c.r), a), g = std::min(q(c.g), a), b = std::min(q(c.b), a);
    return a << 24 | r << 16 | g << 8 | b;
}

// Box blur of radius r (window 2r+1) along one axis, `passes` times.
void boxBlur1D(float* data, int count, int stride, int r, std::vector<float>& tmp) {
    if (r <= 0 || count <= 0) return;
    tmp.resize(count);
    for (int i = 0; i < count; i++) tmp[i] = data[(size_t)i * stride];
    float inv = 1.f / (2 * r + 1);
    float sum = 0;
    for (int i = -r; i <= r; i++) sum += (i >= 0 && i < count) ? tmp[i] : 0;
    for (int i = 0; i < count; i++) {
        data[(size_t)i * stride] = sum * inv;
        int add = i + r + 1, sub = i - r;
        if (add < count) sum += tmp[add];
        if (sub >= 0) sum -= tmp[sub];
    }
}

void blurPlane(Plane& p, int w, int h, double blurX, double blurY, int passes) {
    int rx = (int)std::lround(blurX / 2), ry = (int)std::lround(blurY / 2);
    std::vector<float> tmp;
    for (int pass = 0; pass < std::max(1, passes); pass++) {
        if (rx > 0)
            for (int y = 0; y < h; y++) boxBlur1D(&p[(size_t)y * w], w, 1, rx, tmp);
        if (ry > 0)
            for (int x = 0; x < w; x++) boxBlur1D(&p[x], h, w, ry, tmp);
    }
}

inline float sampleShifted(const Plane& p, int w, int h, int x, int y, int dx, int dy, float outside) {
    int sx = x - dx, sy = y - dy;
    if (sx < 0 || sy < 0 || sx >= w || sy >= h) return outside;
    return p[(size_t)sy * w + sx];
}

Plane alphaPlane(const Surface& s) {
    Plane a(s.pixels.size());
    for (size_t i = 0; i < a.size(); i++) a[i] = (s.pixels[i] >> 24) / 255.f;
    return a;
}

void glowOrShadow(Surface& s, const Filter& f, bool shadow) {
    int w = s.width, h = s.height;
    Plane A = alphaPlane(s);
    int dx = 0, dy = 0;
    if (shadow) {
        dx = (int)std::lround(std::cos(f.angle) * f.distance);
        dy = (int)std::lround(std::sin(f.angle) * f.distance);
    }
    Plane G(A.size());
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float a = sampleShifted(A, w, h, x, y, dx, dy, 0.f);
            G[(size_t)y * w + x] = f.inner ? 1.f - a : a;
        }
    blurPlane(G, w, h, f.blurX, f.blurY, f.passes);

    float cr = f.color.r / 255.f, cg = f.color.g / 255.f, cb = f.color.b / 255.f, ca = f.color.a / 255.f;
    for (size_t i = 0; i < s.pixels.size(); i++) {
        RGBA src = unpack(s.pixels[i]);
        float ga = std::min(1.f, G[i] * (float)f.strength) * ca;
        RGBA out;
        if (f.inner) {
            // Glow inside the shape, drawn atop the source.
            float m = ga * A[i];
            if (f.knockout) out = {cr * m, cg * m, cb * m, m};
            else out = {cr * m + src.r * (1 - ga), cg * m + src.g * (1 - ga), cb * m + src.b * (1 - ga), src.a};
        } else {
            if (f.knockout) {
                float m = ga * (1 - A[i]);
                out = {cr * m, cg * m, cb * m, m};
            } else if (!f.compositeSource) {
                out = {cr * ga, cg * ga, cb * ga, ga};
            } else {
                // Source over glow.
                float k = 1 - src.a;
                out = {src.r + cr * ga * k, src.g + cg * ga * k, src.b + cb * ga * k, src.a + ga * k};
            }
        }
        s.pixels[i] = pack(out);
    }
}

void bevel(Surface& s, const Filter& f) {
    int w = s.width, h = s.height;
    Plane A = alphaPlane(s);
    Plane B = A;
    blurPlane(B, w, h, f.blurX, f.blurY, f.passes);
    int dx = (int)std::lround(std::cos(f.angle) * f.distance / 2);
    int dy = (int)std::lround(std::sin(f.angle) * f.distance / 2);
    bool full = f.onTop;
    bool inner = f.inner && !full;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            size_t i = (size_t)y * w + x;
            // Lit side faces against the offset direction.
            float v = (sampleShifted(B, w, h, x, y, -dx, -dy, 0) - sampleShifted(B, w, h, x, y, dx, dy, 0)) *
                      (float)f.strength;
            v = std::clamp(v, -1.f, 1.f);
            const Color& c = v >= 0 ? f.highlight : f.color;
            float ba = std::fabs(v) * c.a / 255.f;
            float mask = full ? 1.f : inner ? A[i] : 1.f - A[i];
            float m = ba * mask;
            RGBA bev{c.r / 255.f * m, c.g / 255.f * m, c.b / 255.f * m, m};
            RGBA src = unpack(s.pixels[i]);
            RGBA out;
            if (f.knockout) out = bev;
            else if (inner || full) {
                // Bevel atop/over the source.
                float k = 1 - m;
                out = {bev.r + src.r * k, bev.g + src.g * k, bev.b + src.b * k, inner ? src.a : bev.a + src.a * k};
            } else {
                float k = 1 - src.a;  // outer bevel sits under the source
                out = {src.r + bev.r * k, src.g + bev.g * k, src.b + bev.b * k, src.a + bev.a * k};
            }
            s.pixels[i] = pack(out);
        }
}

void blurAll(Surface& s, const Filter& f) {
    int w = s.width, h = s.height;
    Plane ch[4];
    for (int c = 0; c < 4; c++) {
        ch[c].resize(s.pixels.size());
        for (size_t i = 0; i < s.pixels.size(); i++) ch[c][i] = (s.pixels[i] >> (c * 8) & 255) / 255.f;
        blurPlane(ch[c], w, h, f.blurX, f.blurY, f.passes);
    }
    for (size_t i = 0; i < s.pixels.size(); i++)
        s.pixels[i] = pack({ch[2][i], ch[1][i], ch[0][i], ch[3][i]});
}

void colorMatrix(Surface& s, const Filter& f) {
    const float* m = f.matrix;
    for (uint32_t& p : s.pixels) {
        float a = (p >> 24);
        float r = 0, g = 0, b = 0;
        if (a > 0) {
            r = (p >> 16 & 255) * 255.f / a;
            g = (p >> 8 & 255) * 255.f / a;
            b = (p & 255) * 255.f / a;
        }
        float o[4];
        for (int row = 0; row < 4; row++) {
            const float* k = m + row * 5;
            o[row] = std::clamp(k[0] * r + k[1] * g + k[2] * b + k[3] * a + k[4], 0.f, 255.f);
        }
        float na = o[3] / 255.f;
        p = pack({o[0] / 255.f * na, o[1] / 255.f * na, o[2] / 255.f * na, na});
    }
}

inline uint32_t applyCx(const ColorTransform& cx, uint32_t p) {
    RGBA c = unpack(p);
    float r = 0, g = 0, b = 0;
    if (c.a > 0) r = c.r / c.a * 255, g = c.g / c.a * 255, b = c.b / c.a * 255;
    float a = std::clamp(c.a * 255 * cx.aMul + cx.aAdd, 0.f, 255.f) / 255.f;
    r = std::clamp(r * cx.rMul + cx.rAdd, 0.f, 255.f) / 255.f;
    g = std::clamp(g * cx.gMul + cx.gAdd, 0.f, 255.f) / 255.f;
    b = std::clamp(b * cx.bMul + cx.bAdd, 0.f, 255.f) / 255.f;
    return pack({r * a, g * a, b * a, a});
}

// Separable blend function on premultiplied components.
inline float blendChannel(uint8_t mode, float s, float d, float sa, float da) {
    switch (mode) {
        case BlendMultiply: return s * d + s * (1 - da) + d * (1 - sa);
        case BlendScreen: return s + d - s * d;
        case BlendLighten: return std::max(s * da, d * sa) + s * (1 - da) + d * (1 - sa);
        case BlendDarken: return std::min(s * da, d * sa) + s * (1 - da) + d * (1 - sa);
        case BlendDifference: return s + d - 2 * std::min(s * da, d * sa);
        case BlendAdd: return std::min(1.f, s + d);
        case BlendSubtract: return std::max(0.f, d - s);
        case BlendInvert: return sa * (da - d) + d * (1 - sa);
        case BlendOverlay:
        case BlendHardlight: {
            bool useS = mode == BlendHardlight;
            float base = useS ? s : d, baseA = useS ? sa : da;
            float t = 2 * base <= baseA ? 2 * s * d : sa * da - 2 * (da - d) * (sa - s);
            return t + s * (1 - da) + d * (1 - sa);
        }
        default: return s + d * (1 - sa);
    }
}

}  // namespace

int filterPadding(const std::vector<Filter>& filters) {
    double pad = 0;
    for (const Filter& f : filters) {
        if (f.type == Filter::ColorMatrix || f.type == Filter::Convolution) continue;
        double blur = std::max(f.blurX, f.blurY) / 2 * std::max(1, f.passes);
        pad += blur + std::fabs(f.distance) + 1;
    }
    return (int)std::ceil(pad);
}

void applyFilters(Surface& layer, const std::vector<Filter>& filters) {
    for (const Filter& f : filters) {
        switch (f.type) {
            case Filter::Blur: blurAll(layer, f); break;
            case Filter::Glow: glowOrShadow(layer, f, false); break;
            case Filter::DropShadow: glowOrShadow(layer, f, true); break;
            case Filter::Bevel: bevel(layer, f); break;
            case Filter::ColorMatrix: colorMatrix(layer, f); break;
            default: break;  // gradient glow/bevel, convolution: unused by the game
        }
    }
}

void compositeLayer(Surface& dst, const IntRect& clip, const Surface& layer, int x0, int y0,
                    uint8_t mode, const ColorTransform& cx) {
    bool identity = cx.isIdentity();
    int ys = std::max(clip.y0, y0), ye = std::min(clip.y1, y0 + layer.height);
    int xs = std::max(clip.x0, x0), xe = std::min(clip.x1, x0 + layer.width);
    for (int y = ys; y < ye; y++) {
        const uint32_t* src = &layer.pixels[(size_t)(y - y0) * layer.width];
        uint32_t* out = &dst.pixels[(size_t)y * dst.width];
        for (int x = xs; x < xe; x++) {
            uint32_t sp = src[x - x0];
            if (!identity) sp = applyCx(cx, sp);
            if (mode == BlendAlpha || mode == BlendErase) {
                float sa = (sp >> 24) / 255.f;
                float k = mode == BlendAlpha ? sa : 1 - sa;
                RGBA d = unpack(out[x]);
                out[x] = pack({d.r * k, d.g * k, d.b * k, d.a * k});
                continue;
            }
            if (!(sp >> 24)) continue;
            if (mode <= BlendLayer) {
                RGBA s = unpack(sp), d = unpack(out[x]);
                float k = 1 - s.a;
                out[x] = pack({s.r + d.r * k, s.g + d.g * k, s.b + d.b * k, s.a + d.a * k});
                continue;
            }
            RGBA s = unpack(sp), d = unpack(out[x]);
            RGBA o{blendChannel(mode, s.r, d.r, s.a, d.a), blendChannel(mode, s.g, d.g, s.a, d.a),
                   blendChannel(mode, s.b, d.b, s.a, d.a), 0};
            o.a = (mode == BlendSubtract || mode == BlendInvert) ? d.a : s.a + d.a * (1 - s.a);
            out[x] = pack(o);
        }
    }
}

void applyMask(Surface& layer, const Surface& mask) {
    for (size_t i = 0; i < layer.pixels.size() && i < mask.pixels.size(); i++) {
        float k = (mask.pixels[i] >> 24) / 255.f;
        RGBA c = unpack(layer.pixels[i]);
        layer.pixels[i] = pack({c.r * k, c.g * k, c.b * k, c.a * k});
    }
}

}  // namespace flash
