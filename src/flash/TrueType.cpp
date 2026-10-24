#include "flash/TrueType.h"

#include <cstring>
#include <functional>

namespace flash {

namespace {

struct Bytes {
    const uint8_t* data;
    size_t size;
    bool ok(size_t at, size_t n) const { return at <= size && n <= size - at; }
    uint16_t u16(size_t at) const { return ok(at, 2) ? (uint16_t)(data[at] << 8 | data[at + 1]) : 0; }
    int16_t s16(size_t at) const { return (int16_t)u16(at); }
    uint32_t u32(size_t at) const { return (uint32_t)u16(at) << 16 | u16(at + 2); }
};

struct Point2 {
    double x, y;
    bool on;
};

}  // namespace

std::unique_ptr<FontCharacter> loadTrueType(const uint8_t* data, size_t size, const std::string& name, bool bold,
                                            bool italic) {
    Bytes b{data, size};
    // Table directory.
    size_t head = 0, hhea = 0, hmtx = 0, maxp = 0, cmap = 0, loca = 0, glyf = 0;
    size_t glyfSize = 0;
    int tables = b.u16(4);
    for (int i = 0; i < tables; i++) {
        size_t rec = 12 + 16 * i;
        if (!b.ok(rec, 16)) return nullptr;
        char tag[5] = {};
        std::memcpy(tag, data + rec, 4);
        size_t offset = b.u32(rec + 8), length = b.u32(rec + 12);
        if (!b.ok(offset, length)) return nullptr;
        if (!std::strcmp(tag, "head")) head = offset;
        else if (!std::strcmp(tag, "hhea")) hhea = offset;
        else if (!std::strcmp(tag, "hmtx")) hmtx = offset;
        else if (!std::strcmp(tag, "maxp")) maxp = offset;
        else if (!std::strcmp(tag, "cmap")) cmap = offset;
        else if (!std::strcmp(tag, "loca")) loca = offset;
        else if (!std::strcmp(tag, "glyf")) glyf = offset, glyfSize = length;
    }
    if (!head || !hhea || !hmtx || !maxp || !cmap || !loca || !glyf) return nullptr;
    int unitsPerEm = b.u16(head + 18);
    bool longLoca = b.s16(head + 50) == 1;
    int numGlyphs = b.u16(maxp + 4);
    int numMetrics = b.u16(hhea + 34);
    if (unitsPerEm <= 0 || numGlyphs <= 0 || numMetrics <= 0) return nullptr;

    auto glyphRange = [&](int g, size_t& start, size_t& end) {
        start = longLoca ? b.u32(loca + 4 * g) : 2u * b.u16(loca + 2 * g);
        end = longLoca ? b.u32(loca + 4 * g + 4) : 2u * b.u16(loca + 2 * g + 2);
        return start <= end && end <= glyfSize;
    };
    auto advance = [&](int g) { return b.u16(hmtx + 4 * std::min(g, numMetrics - 1)); };

    // Outline contours of glyph g in font units (y up), composites flattened.
    std::function<void(int, double, double, int, std::vector<std::vector<Point2>>&)> outline =
        [&](int g, double dx, double dy, int nesting, std::vector<std::vector<Point2>>& out) {
            size_t start, end;
            if (g < 0 || g >= numGlyphs || nesting > 4 || !glyphRange(g, start, end) || end - start < 10) return;
            size_t at = glyf + start;
            int contours = b.s16(at);
            if (contours >= 0) {
                std::vector<int> ends(contours);
                for (int c = 0; c < contours; c++) ends[c] = b.u16(at + 10 + 2 * c);
                int points = contours ? ends.back() + 1 : 0;
                size_t p = at + 10 + 2 * contours;
                p += 2 + b.u16(p);  // instructions
                std::vector<uint8_t> flags;
                while ((int)flags.size() < points && b.ok(p, 1)) {
                    uint8_t f = data[p++];
                    flags.push_back(f);
                    if (f & 8 && b.ok(p, 1))
                        for (int r = data[p++]; r > 0 && (int)flags.size() < points; r--) flags.push_back(f);
                }
                if ((int)flags.size() < points) return;
                std::vector<Point2> pts(points);
                int v = 0;
                for (int i = 0; i < points; i++) {  // x
                    uint8_t f = flags[i];
                    if (f & 2) v += (f & 16 ? 1 : -1) * (b.ok(p, 1) ? data[p++] : 0);
                    else if (!(f & 16)) v += b.s16(p), p += 2;
                    pts[i].x = v + dx;
                    pts[i].on = f & 1;
                }
                v = 0;
                for (int i = 0; i < points; i++) {  // y
                    uint8_t f = flags[i];
                    if (f & 4) v += (f & 32 ? 1 : -1) * (b.ok(p, 1) ? data[p++] : 0);
                    else if (!(f & 32)) v += b.s16(p), p += 2;
                    pts[i].y = v + dy;
                }
                for (int c = 0, first = 0; c < contours; first = ends[c] + 1, c++)
                    if (ends[c] >= first && ends[c] < points)
                        out.emplace_back(pts.begin() + first, pts.begin() + ends[c] + 1);
                return;
            }
            // Composite: components placed by offsets (scaled components are drawn unscaled,
            // which the Latin glyphs this is for don't use).
            size_t p = at + 10;
            for (bool more = true; more && b.ok(p, 4);) {
                uint16_t flags = b.u16(p), component = b.u16(p + 2);
                p += 4;
                double ox = 0, oy = 0;
                if (flags & 1) ox = b.s16(p), oy = b.s16(p + 2), p += 4;
                else ox = (int8_t)data[p], oy = (int8_t)data[p + 1], p += 2;
                if (flags & 8) p += 2;
                else if (flags & 0x40) p += 4;
                else if (flags & 0x80) p += 8;
                if (!(flags & 2)) ox = oy = 0;  // point matching: unsupported
                outline(component, dx + ox, dy + oy, nesting + 1, out);
                more = flags & 0x20;
            }
        };

    auto font = std::make_unique<FontCharacter>();
    font->name = name;
    font->bold = bold;
    font->italic = italic;
    font->hasLayout = true;
    font->emSize = unitsPerEm;
    font->ascent = b.s16(hhea + 4);
    font->descent = -b.s16(hhea + 6);
    font->leading = b.s16(hhea + 8);
    auto addGlyph = [&](uint32_t code, int g) {
        if (font->glyphForCode.count(code)) return;
        std::vector<std::vector<Point2>> contours;
        outline(g, 0, 0, 0, contours);
        ShapeDef def;
        def.layers.emplace_back();
        FillPath path;
        path.style = 1;
        // Quadratic B-spline contours to edges; y flipped to Flash's y-down. Between two
        // off-curve points lies an implied on-curve point at their midpoint.
        for (auto& c : contours) {
            size_t n = c.size();
            if (n < 2) continue;
            // Start on an on-curve point (or, if there is none, the implied one between the
            // last and first points) and walk the rest of the contour back to it.
            size_t s = 0;
            while (s < n && !c[s].on) s++;
            bool allOff = s == n;
            Point start = allOff ? Point{(c[n - 1].x + c[0].x) / 2, -(c[n - 1].y + c[0].y) / 2} : Point{c[s].x, -c[s].y};
            Point cur = start, ctrl;
            bool haveCtrl = false;
            for (size_t k = allOff ? 0 : 1; k <= n - (allOff ? 1 : 0); k++) {
                const Point2& q = c[(s % n + k) % n];
                Point pt{q.x, -q.y};
                if (q.on) {
                    path.edges.push_back(haveCtrl ? Edge{cur, ctrl, pt, true} : Edge{cur, {}, pt, false});
                    cur = pt;
                    haveCtrl = false;
                } else if (haveCtrl) {
                    Point mid{(ctrl.x + pt.x) / 2, (ctrl.y + pt.y) / 2};
                    path.edges.push_back({cur, ctrl, mid, true});
                    cur = mid;
                    ctrl = pt;
                } else {
                    ctrl = pt;
                    haveCtrl = true;
                }
            }
            if (haveCtrl) path.edges.push_back({cur, ctrl, start, true});
            else if (cur.x != start.x || cur.y != start.y) path.edges.push_back({cur, {}, start, false});
        }
        if (!path.edges.empty()) def.layers.back().fills.push_back(std::move(path));
        font->glyphForCode[code] = (int)font->glyphs.size();
        font->glyphs.push_back(std::move(def));
        font->codes.push_back((uint16_t)code);
        font->advances.push_back(advance(g));
    };

    // cmap: the Windows Unicode BMP (format 4) subtable.
    int subtables = b.u16(cmap + 2);
    size_t format4 = 0;
    for (int i = 0; i < subtables; i++) {
        size_t rec = cmap + 4 + 8 * i;
        int platform = b.u16(rec), encoding = b.u16(rec + 2);
        size_t at = cmap + b.u32(rec + 4);
        if (b.u16(at) == 4 && (platform == 3 && encoding == 1 || platform == 0)) format4 = at;
    }
    if (!format4) return nullptr;
    int segments = b.u16(format4 + 6) / 2;
    size_t endCodes = format4 + 14, startCodes = endCodes + 2 * segments + 2;
    size_t deltas = startCodes + 2 * segments, rangeOffsets = deltas + 2 * segments;
    for (int i = 0; i < segments; i++) {
        uint32_t first = b.u16(startCodes + 2 * i), last = b.u16(endCodes + 2 * i);
        int delta = b.s16(deltas + 2 * i);
        size_t rangeAt = rangeOffsets + 2 * i;
        int rangeOffset = b.u16(rangeAt);
        for (uint32_t code = first; code <= last && code != 0xFFFF; code++) {
            int g;
            if (!rangeOffset) g = (code + delta) & 0xFFFF;
            else {
                g = b.u16(rangeAt + rangeOffset + 2 * (code - first));
                if (g) g = (g + delta) & 0xFFFF;
            }
            if (g > 0 && g < numGlyphs) addGlyph(code, g);
        }
    }
    return font->glyphs.empty() ? nullptr : std::move(font);
}

namespace {
std::vector<std::unique_ptr<FontCharacter>>& fallbacks() {
    static std::vector<std::unique_ptr<FontCharacter>> fonts;
    return fonts;
}
}  // namespace

void addFallbackFont(std::unique_ptr<FontCharacter> font) {
    if (font) fallbacks().push_back(std::move(font));
}

const std::vector<std::unique_ptr<FontCharacter>>& fallbackFonts() { return fallbacks(); }

}  // namespace flash
