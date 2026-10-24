#include "flash/Shape.h"

#include <cmath>
#include <map>

#include "flash/Reader.h"

namespace flash {
namespace {

void parseGradient(Reader& r, int version, FillStyle& f) {
    r.align();
    f.spread = (uint8_t)r.ub(2);
    f.interpolation = (uint8_t)r.ub(2);
    int n = r.ub(4);
    f.stops.resize(n);
    for (auto& s : f.stops) {
        s.ratio = r.u8();
        s.color = version >= 3 ? r.rgba() : r.rgb();
    }
}

}  // namespace

std::vector<FillStyle> parseFillStyles(Reader& r, int version) {
    int n = r.u8();
    if (n == 0xFF && version >= 2) n = r.u16();
    std::vector<FillStyle> v;
    v.reserve(n);
    for (int i = 0; i < n; i++) v.push_back(parseFillStyle(r, version));
    return v;
}

std::vector<LineStyle> parseLineStyles(Reader& r, int version) {
    int n = r.u8();
    if (n == 0xFF) n = r.u16();
    std::vector<LineStyle> v(n);
    for (auto& l : v) {
        l.width = r.u16();
        if (version < 4) {
            l.color = version >= 3 ? r.rgba() : r.rgb();
            continue;
        }
        r.align();
        l.startCap = (uint8_t)r.ub(2);
        l.join = (uint8_t)r.ub(2);
        l.hasFill = r.flag();
        l.noHScale = r.flag();
        l.noVScale = r.flag();
        r.ub(1);  // pixel hinting
        r.ub(5);
        l.noClose = r.flag();
        l.endCap = (uint8_t)r.ub(2);
        if (l.join == 2) l.miterLimit = r.fixed8();
        if (l.hasFill) {
            l.fill = parseFillStyle(r, version);
            l.color = l.fill.color;
        } else {
            l.color = r.rgba();
        }
    }
    return v;
}

FillStyle parseFillStyle(Reader& r, int version) {
    FillStyle f;
    f.type = r.u8();
    switch (f.type) {
        case FillStyle::Solid:
            f.color = version >= 3 ? r.rgba() : r.rgb();
            break;
        case FillStyle::LinearGradient:
        case FillStyle::RadialGradient:
        case FillStyle::FocalGradient:
            f.matrix = r.matrix();
            parseGradient(r, version, f);
            if (f.type == FillStyle::FocalGradient) f.focal = r.fixed8();
            break;
        default:
            if (f.isBitmap()) {
                f.bitmapId = r.u16();
                f.matrix = r.matrix();
            }
            break;
    }
    return f;
}

void readShapeRecords(Reader& r, int version, bool withStyles, std::vector<ShapeRecord>& out) {
    r.align();
    int fillBits = r.ub(4);
    int lineBits = r.ub(4);
    double x = 0, y = 0;
    for (;;) {
        bool isEdge = r.flag();
        if (!isEdge) {
            ShapeRecord rec;
            bool newStyles = r.flag();
            rec.lineChange = r.flag();
            rec.fill1Change = r.flag();
            rec.fill0Change = r.flag();
            rec.moveTo = r.flag();
            if (!newStyles && !rec.lineChange && !rec.fill1Change && !rec.fill0Change && !rec.moveTo)
                break;
            if (rec.moveTo) {
                int n = r.ub(5);
                x = r.sb(n);
                y = r.sb(n);
                rec.move = {x, y};
            }
            if (rec.fill0Change) rec.fill0 = r.ub(fillBits);
            if (rec.fill1Change) rec.fill1 = r.ub(fillBits);
            if (rec.lineChange) rec.line = r.ub(lineBits);
            if (newStyles && withStyles && version >= 2) {
                rec.newStyles = true;
                rec.fills = parseFillStyles(r, version);
                rec.lines = parseLineStyles(r, version);
                r.align();
                fillBits = r.ub(4);
                lineBits = r.ub(4);
            }
            out.push_back(std::move(rec));
            continue;
        }
        ShapeRecord rec;
        rec.isEdge = true;
        Edge& e = rec.edge;
        bool straight = r.flag();
        int n = r.ub(4) + 2;
        e.from = {x, y};
        if (straight) {
            double dx = 0, dy = 0;
            if (r.flag()) {
                dx = r.sb(n);
                dy = r.sb(n);
            } else if (r.flag()) {
                dy = r.sb(n);
            } else {
                dx = r.sb(n);
            }
            x += dx;
            y += dy;
            e.to = {x, y};
            e.ctrl = {(e.from.x + x) / 2, (e.from.y + y) / 2};
        } else {
            double cx = x + r.sb(n);
            double cy = y + r.sb(n);
            x = cx + r.sb(n);
            y = cy + r.sb(n);
            e.ctrl = {cx, cy};
            e.to = {x, y};
            e.curved = true;
        }
        out.push_back(std::move(rec));
    }
}

void buildPaths(const std::vector<ShapeRecord>& records, ShapeDef& out) {
    // Style indices in the records are 1-based into the current style group; the first
    // group is whatever `out` already holds.
    int fillBase = 0, lineBase = 0;

    // Edges are collected per global style index, then emitted in style order.
    std::map<int, std::vector<Edge>> fillEdges;
    std::map<int, StrokePath> strokes;
    auto flushLayer = [&]() {
        ShapeDef::Layer layer;
        for (auto& [style, edges] : fillEdges) layer.fills.push_back({style, std::move(edges)});
        for (auto& [style, sp] : strokes) layer.strokes.push_back(std::move(sp));
        fillEdges.clear();
        strokes.clear();
        if (!layer.fills.empty() || !layer.strokes.empty()) out.layers.push_back(std::move(layer));
    };

    int fill0 = 0, fill1 = 0, line = 0;  // 0 = none
    bool penMoved = true;               // starts a new stroke polyline

    for (const ShapeRecord& rec : records) {
        if (!rec.isEdge) {
            if (rec.newStyles) {
                flushLayer();
                fillBase = (int)out.fills.size();
                lineBase = (int)out.lines.size();
                out.fills.insert(out.fills.end(), rec.fills.begin(), rec.fills.end());
                out.lines.insert(out.lines.end(), rec.lines.begin(), rec.lines.end());
                // A new style group resets the current styles unless this record sets them.
                fill0 = fill1 = line = 0;
            }
            if (rec.fill0Change) fill0 = rec.fill0;
            if (rec.fill1Change) fill1 = rec.fill1;
            if (rec.lineChange) line = rec.line;
            penMoved = true;
            continue;
        }
        const Edge& e = rec.edge;
        if (fill1) fillEdges[fillBase + fill1 - 1].push_back(e);
        if (fill0) fillEdges[fillBase + fill0 - 1].push_back({e.to, e.ctrl, e.from, e.curved});
        if (line) {
            auto& sp = strokes[lineBase + line - 1];
            sp.style = lineBase + line - 1;
            if (penMoved || sp.polylines.empty()) sp.polylines.emplace_back();
            else {
                const Edge& last = sp.polylines.back().back();
                if (last.to.x != e.from.x || last.to.y != e.from.y) sp.polylines.emplace_back();
            }
            sp.polylines.back().push_back(e);
            penMoved = false;
        }
    }
    flushLayer();
}

namespace {

// Calls fn(a, b) for each straight segment of an edge, flattening curves.
template <typename F>
void forEachSegment(const Edge& e, F&& fn) {
    if (!e.curved) {
        fn(e.from, e.to);
        return;
    }
    Point prev = e.from;
    const int n = 8;
    for (int i = 1; i <= n; i++) {
        double t = (double)i / n, u = 1 - t;
        Point q{u * u * e.from.x + 2 * u * t * e.ctrl.x + t * t * e.to.x,
                u * u * e.from.y + 2 * u * t * e.ctrl.y + t * t * e.to.y};
        fn(prev, q);
        prev = q;
    }
}

double segmentDistance(Point a, Point b, Point p) {
    double dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0;
    t = std::max(0.0, std::min(1.0, t));
    return std::hypot(a.x + dx * t - p.x, a.y + dy * t - p.y);
}

}  // namespace

bool shapeContains(const ShapeDef& shape, Point p) {
    if (p.x < shape.bounds.xMin || p.x > shape.bounds.xMax || p.y < shape.bounds.yMin ||
        p.y > shape.bounds.yMax)
        return false;
    for (const auto& layer : shape.layers) {
        for (const FillPath& fp : layer.fills) {
            int winding = 0;
            for (const Edge& e : fp.edges)
                forEachSegment(e, [&](Point a, Point b) {
                    // Ray towards +x; count signed crossings.
                    if ((a.y <= p.y) != (b.y <= p.y)) {
                        double x = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
                        if (x > p.x) winding += b.y > a.y ? 1 : -1;
                    }
                });
            if (winding) return true;
        }
        for (const StrokePath& sp : layer.strokes) {
            double hw = std::max(10.0, (sp.style < (int)shape.lines.size() ? shape.lines[sp.style].width : 20) / 2);
            for (const auto& pl : sp.polylines)
                for (const Edge& e : pl) {
                    bool hit = false;
                    forEachSegment(e, [&](Point a, Point b) { hit |= segmentDistance(a, b, p) <= hw; });
                    if (hit) return true;
                }
        }
    }
    return false;
}

void parseShape(Reader& r, int version, ShapeDef& out, bool withStyles) {
    if (withStyles) {
        out.fills = parseFillStyles(r, version);
        out.lines = parseLineStyles(r, version);
    }
    std::vector<ShapeRecord> records;
    readShapeRecords(r, version, withStyles, records);
    buildPaths(records, out);
}

}  // namespace flash
