#include "flash/Morph.h"

#include <cmath>

#include "flash/Reader.h"

namespace flash {
namespace {

void parseMorphFill(Reader& r, FillStyle& a, FillStyle& b) {
    a.type = b.type = r.u8();
    if (a.type == FillStyle::Solid) {
        a.color = r.rgba();
        b.color = r.rgba();
    } else if (a.isGradient()) {
        a.matrix = r.matrix();
        b.matrix = r.matrix();
        uint8_t f = r.u8();
        a.spread = b.spread = f >> 6;
        a.interpolation = b.interpolation = (f >> 4) & 3;
        int n = f & 15;
        a.stops.resize(n);
        b.stops.resize(n);
        for (int i = 0; i < n; i++) {
            a.stops[i].ratio = r.u8();
            a.stops[i].color = r.rgba();
            b.stops[i].ratio = r.u8();
            b.stops[i].color = r.rgba();
        }
        if (a.type == FillStyle::FocalGradient) {
            a.focal = r.fixed8();
            b.focal = r.fixed8();
        }
    } else if (a.isBitmap()) {
        a.bitmapId = b.bitmapId = r.u16();
        a.matrix = r.matrix();
        b.matrix = r.matrix();
    }
}

double lerp(double a, double b, double t) { return a + (b - a) * t; }
Point lerp(Point a, Point b, double t) { return {lerp(a.x, b.x, t), lerp(a.y, b.y, t)}; }
Color lerp(Color a, Color b, double t) {
    auto c = [t](uint8_t x, uint8_t y) { return (uint8_t)std::lround(x + (y - x) * t); };
    return {c(a.r, b.r), c(a.g, b.g), c(a.b, b.b), c(a.a, b.a)};
}
Matrix lerp(const Matrix& a, const Matrix& b, double t) {
    return {lerp(a.a, b.a, t), lerp(a.b, b.b, t), lerp(a.c, b.c, t),
            lerp(a.d, b.d, t), lerp(a.tx, b.tx, t), lerp(a.ty, b.ty, t)};
}
FillStyle lerp(const FillStyle& a, const FillStyle& b, double t) {
    FillStyle f = a;
    f.color = lerp(a.color, b.color, t);
    f.matrix = lerp(a.matrix, b.matrix, t);
    for (size_t i = 0; i < f.stops.size() && i < b.stops.size(); i++) {
        f.stops[i].ratio = (uint8_t)std::lround(lerp(a.stops[i].ratio, b.stops[i].ratio, t));
        f.stops[i].color = lerp(a.stops[i].color, b.stops[i].color, t);
    }
    f.focal = lerp(a.focal, b.focal, t);
    return f;
}

// Straight edges are stored with a midpoint control, so interpolating a straight edge
// against a curved one already yields the right curve.
Edge lerp(const Edge& a, const Edge& b, double t) {
    return {lerp(a.from, b.from, t), lerp(a.ctrl, b.ctrl, t), lerp(a.to, b.to, t),
            a.curved || b.curved};
}

}  // namespace

void MorphShapeCharacter::parse(Reader& r, int version) {
    startBounds = r.rect();
    endBounds = r.rect();
    if (version == 2) {
        r.rect();  // start edge bounds
        r.rect();  // end edge bounds
        r.u8();
    }
    uint32_t offset = r.u32();
    size_t endEdgesPos = r.pos() + offset;

    int nf = r.u8();
    if (nf == 0xFF) nf = r.u16();
    startFills.resize(nf);
    endFills.resize(nf);
    for (int i = 0; i < nf; i++) parseMorphFill(r, startFills[i], endFills[i]);

    int nl = r.u8();
    if (nl == 0xFF) nl = r.u16();
    startLines.resize(nl);
    endLines.resize(nl);
    for (int i = 0; i < nl; i++) {
        LineStyle& a = startLines[i];
        LineStyle& b = endLines[i];
        a.width = r.u16();
        b.width = r.u16();
        if (version == 1) {
            a.color = r.rgba();
            b.color = r.rgba();
            continue;
        }
        r.align();
        a.startCap = (uint8_t)r.ub(2);
        a.join = (uint8_t)r.ub(2);
        a.hasFill = r.flag();
        a.noHScale = r.flag();
        a.noVScale = r.flag();
        r.ub(1);
        r.ub(5);
        a.noClose = r.flag();
        a.endCap = (uint8_t)r.ub(2);
        if (a.join == 2) a.miterLimit = r.fixed8();
        LineStyle keep = a;
        if (a.hasFill) {
            parseMorphFill(r, a.fill, b.fill);
            a.color = a.fill.color;
            b.color = b.fill.color;
        } else {
            a.color = r.rgba();
            b.color = r.rgba();
        }
        double bw = b.width;
        Color bc = b.color;
        FillStyle bf = b.fill;
        b = keep;
        b.width = bw;
        b.color = bc;
        b.fill = bf;
    }

    readShapeRecords(r, 3, false, startRecords);
    r.seek(endEdgesPos);
    readShapeRecords(r, 3, false, endRecords);
}

void MorphShapeCharacter::trimCache() {
    if (cache_.size() > 256) cache_.clear();
}

const ShapeDef& MorphShapeCharacter::at(uint16_t ratio) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(ratio);
    if (it != cache_.end()) return it->second;

    double t = ratio / 65535.0;
    ShapeDef def;
    def.bounds = {lerp(startBounds.xMin, endBounds.xMin, t), lerp(startBounds.yMin, endBounds.yMin, t),
                  lerp(startBounds.xMax, endBounds.xMax, t), lerp(startBounds.yMax, endBounds.yMax, t)};
    for (size_t i = 0; i < startFills.size(); i++) def.fills.push_back(lerp(startFills[i], endFills[i], t));
    for (size_t i = 0; i < startLines.size(); i++) {
        LineStyle l = startLines[i];
        l.width = lerp(startLines[i].width, endLines[i].width, t);
        l.color = lerp(startLines[i].color, endLines[i].color, t);
        if (l.hasFill) l.fill = lerp(startLines[i].fill, endLines[i].fill, t);
        def.lines.push_back(l);
    }

    // Pair each start edge with the next end edge. End records carry their own
    // move-tos, which are already folded into their absolute edge coordinates.
    std::vector<ShapeRecord> mixed;
    mixed.reserve(startRecords.size());
    size_t j = 0;
    for (const ShapeRecord& s : startRecords) {
        if (!s.isEdge) {
            mixed.push_back(s);
            continue;
        }
        while (j < endRecords.size() && !endRecords[j].isEdge) j++;
        ShapeRecord m = s;
        if (j < endRecords.size()) m.edge = lerp(s.edge, endRecords[j++].edge, t);
        mixed.push_back(std::move(m));
    }
    buildPaths(mixed, def);
    return cache_.emplace(ratio, std::move(def)).first->second;
}

}  // namespace flash
