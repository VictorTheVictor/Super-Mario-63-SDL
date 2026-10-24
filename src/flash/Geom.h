#pragma once
// Flash geometry primitives. Coordinates inside SWF data are in twips (1/20 px).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace flash {

struct Point {
    double x = 0, y = 0;
};

struct Rect {
    double xMin = 0, yMin = 0, xMax = 0, yMax = 0;
    bool empty() const { return xMax <= xMin || yMax <= yMin; }
};

// x' = a*x + c*y + tx ; y' = b*x + d*y + ty  (same layout as flash.geom.Matrix)
struct Matrix {
    double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;

    Point apply(Point p) const { return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
    Point apply(double x, double y) const { return {a * x + c * y + tx, b * x + d * y + ty}; }

    // this * m : apply m first, then this
    Matrix operator*(const Matrix& m) const {
        return {a * m.a + c * m.b,         b * m.a + d * m.b,
                a * m.c + c * m.d,         b * m.c + d * m.d,
                a * m.tx + c * m.ty + tx,  b * m.tx + d * m.ty + ty};
    }

    // False for degenerate transforms (e.g. _xscale = 0): nothing can be hit through them.
    bool invertible() const { return std::fabs(a * d - b * c) > 1e-12; }

    Matrix inverse() const {
        double det = a * d - b * c;
        if (std::fabs(det) < 1e-12) return {0, 0, 0, 0, -tx, -ty};
        double id = 1.0 / det;
        Matrix r;
        r.a = d * id;
        r.b = -b * id;
        r.c = -c * id;
        r.d = a * id;
        r.tx = -(r.a * tx + r.c * ty);
        r.ty = -(r.b * tx + r.d * ty);
        return r;
    }

    Rect apply(const Rect& r) const {
        Point p[4] = {apply(r.xMin, r.yMin), apply(r.xMax, r.yMin), apply(r.xMin, r.yMax),
                      apply(r.xMax, r.yMax)};
        Rect o{p[0].x, p[0].y, p[0].x, p[0].y};
        for (auto& q : p) {
            o.xMin = std::min(o.xMin, q.x);
            o.yMin = std::min(o.yMin, q.y);
            o.xMax = std::max(o.xMax, q.x);
            o.yMax = std::max(o.yMax, q.y);
        }
        return o;
    }

    static Matrix scale(double sx, double sy) { return {sx, 0, 0, sy, 0, 0}; }
    static Matrix translate(double x, double y) { return {1, 0, 0, 1, x, y}; }
};

// Flash color transform: out = in * mult + add  (channels in 0..255)
struct ColorTransform {
    float rMul = 1, gMul = 1, bMul = 1, aMul = 1;
    float rAdd = 0, gAdd = 0, bAdd = 0, aAdd = 0;

    bool isIdentity() const {
        return rMul == 1 && gMul == 1 && bMul == 1 && aMul == 1 && rAdd == 0 && gAdd == 0 &&
               bAdd == 0 && aAdd == 0;
    }

    // this * c : apply c first, then this
    ColorTransform operator*(const ColorTransform& c) const {
        ColorTransform r;
        r.rMul = rMul * c.rMul;
        r.gMul = gMul * c.gMul;
        r.bMul = bMul * c.bMul;
        r.aMul = aMul * c.aMul;
        r.rAdd = rMul * c.rAdd + rAdd;
        r.gAdd = gMul * c.gAdd + gAdd;
        r.bAdd = bMul * c.bAdd + bAdd;
        r.aAdd = aMul * c.aAdd + aAdd;
        return r;
    }
};

// Straight (non-premultiplied) color, 0..255 per channel.
struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

inline Color applyCx(const ColorTransform& cx, Color c) {
    auto ch = [](float v, float m, float add) {
        float o = v * m + add;
        return (uint8_t)(o < 0 ? 0 : o > 255 ? 255 : o);
    };
    return {ch(c.r, cx.rMul, cx.rAdd), ch(c.g, cx.gMul, cx.gAdd), ch(c.b, cx.bMul, cx.bAdd),
            ch(c.a, cx.aMul, cx.aAdd)};
}

}  // namespace flash
