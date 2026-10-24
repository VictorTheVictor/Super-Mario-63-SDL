#include "flash/Text.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>

#include "flash/Reader.h"
#include "flash/TrueType.h"

namespace flash {

// ---------------------------------------------------------------- UTF-8

std::u32string utf8Decode(const std::string& s) {
    std::u32string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        uint8_t c = (uint8_t)s[i];
        uint32_t cp;
        int extra;
        if (c < 0x80) cp = c, extra = 0;
        else if ((c >> 5) == 6) cp = c & 0x1F, extra = 1;
        else if ((c >> 4) == 14) cp = c & 0x0F, extra = 2;
        else if ((c >> 3) == 30) cp = c & 0x07, extra = 3;
        else {
            out.push_back(c);  // stray byte: treat as Latin-1
            i++;
            continue;
        }
        if (i + extra >= s.size() + (extra ? 0 : 1)) {
            out.push_back(c);
            i++;
            continue;
        }
        bool ok = true;
        for (int k = 1; k <= extra; k++) {
            uint8_t cc = (uint8_t)s[i + k];
            if ((cc >> 6) != 2) ok = false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) {
            out.push_back(c);
            i++;
            continue;
        }
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

std::string utf8Encode(const std::u32string& s) {
    std::string out;
    for (char32_t c : s) {
        if (c < 0x80) out.push_back((char)c);
        else if (c < 0x800) {
            out.push_back((char)(0xC0 | c >> 6));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back((char)(0xE0 | c >> 12));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | c >> 18));
            out.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

// ---------------------------------------------------------------- parsing

void FontCharacter::parse(Reader& r, int version) {
    uint8_t flags = r.u8();
    hasLayout = flags & 0x80;
    bool wideOffsets = flags & 0x08, wideCodes = flags & 0x04;
    italic = flags & 0x02;
    bold = flags & 0x01;
    r.u8();  // language
    int nameLen = r.u8();
    for (int i = 0; i < nameLen; i++) {
        char c = (char)r.u8();
        if (c) name.push_back(c);
    }
    int n = r.u16();
    emSize = version == 3 ? 1024.0 * 20 : 1024.0;
    size_t tableStart = r.pos();
    std::vector<uint32_t> offsets(n);
    for (auto& o : offsets) o = wideOffsets ? r.u32() : r.u16();
    uint32_t codeOffset = n ? (wideOffsets ? r.u32() : r.u16()) : 0;

    glyphs.resize(n);
    for (int i = 0; i < n; i++) {
        r.seek(tableStart + offsets[i]);
        std::vector<ShapeRecord> recs;
        readShapeRecords(r, 1, false, recs);
        buildPaths(recs, glyphs[i]);
    }
    if (!n) return;
    r.seek(tableStart + codeOffset);
    codes.resize(n);
    for (int i = 0; i < n; i++) {
        codes[i] = wideCodes ? r.u16() : r.u8();
        glyphForCode.emplace(codes[i], i);
    }
    if (hasLayout) {
        ascent = r.u16();
        descent = r.u16();
        leading = r.s16();
        advances.resize(n);
        for (auto& a : advances) a = r.s16();
    }
}

void StaticTextCharacter::parse(Reader& r, int version) {
    bounds = r.rect();
    matrix = r.matrix();
    int glyphBits = r.u8(), advanceBits = r.u8();
    Record cur;
    for (;;) {
        uint8_t flags = r.u8();
        if (!flags) break;
        Record rec = cur;
        rec.glyphs.clear();
        rec.hasX = rec.hasY = false;
        if (flags & 0x08) rec.fontId = r.u16();
        if (flags & 0x04) rec.color = version == 2 ? r.rgba() : r.rgb();
        if (flags & 0x01) rec.x = r.s16(), rec.hasX = true;
        if (flags & 0x02) rec.y = r.s16(), rec.hasY = true;
        if (flags & 0x08) rec.height = r.u16();
        int count = r.u8();
        for (int i = 0; i < count; i++) {
            Glyph g;
            g.index = (int)r.ub(glyphBits);
            g.advance = r.sb(advanceBits);
            rec.glyphs.push_back(g);
        }
        r.align();
        cur = rec;
        records.push_back(std::move(rec));
    }
}

void EditTextCharacter::parse(Reader& r) {
    bounds = r.rect();
    uint8_t f1 = r.u8(), f2 = r.u8();
    bool hasText = f1 & 0x80;
    wordWrap = f1 & 0x40;
    multiline = f1 & 0x20;
    password = f1 & 0x10;
    readOnly = f1 & 0x08;
    bool hasColor = f1 & 0x04, hasMaxLength = f1 & 0x02, hasFont = f1 & 0x01;
    bool hasFontClass = f2 & 0x80;
    autoSize = f2 & 0x40;
    bool hasLayout = f2 & 0x20;
    noSelect = f2 & 0x10;
    border = f2 & 0x08;
    html = f2 & 0x02;
    useOutlines = f2 & 0x01;
    if (hasFont) fontId = r.u16();
    if (hasFontClass) r.str();
    if (hasFont) fontHeight = r.u16();
    if (hasColor) color = r.rgba();
    if (hasMaxLength) maxLength = r.u16();
    if (hasLayout) {
        align = r.u8();
        leftMargin = r.u16();
        rightMargin = r.u16();
        indent = r.u16();
        leading = r.s16();
    }
    variableName = r.str();
    if (hasText) initialText = r.str();
}

// ---------------------------------------------------------------- rendering

void drawGlyph(Rasterizer& r, Surface& dst, const IntRect& clip, const ShapeDef& glyph,
               const Matrix& m, Color color, const ColorTransform& cx) {
    r.reset();
    for (const auto& layer : glyph.layers)
        for (const FillPath& fp : layer.fills) r.addPath(fp.edges, m);
    Paint p;
    p.color = color;
    p.cxform = cx;
    r.fill(dst, clip, p);
}

void StaticTextObject::render(Rasterizer& r, Surface& dst, const IntRect& clip, const Matrix& m,
                              const ColorTransform& cx, const Movie& movie) {
    auto* t = static_cast<StaticTextCharacter*>(def_);
    Matrix base = m * t->matrix;
    double x = 0, y = 0;
    for (const auto& rec : t->records) {
        auto* fc = movie.character(rec.fontId);
        if (!fc || fc->kind != Character::Font) continue;
        auto* font = static_cast<FontCharacter*>(fc);
        if (rec.hasX) x = rec.x;
        if (rec.hasY) y = rec.y;
        double s = rec.height / font->emSize;
        for (const auto& g : rec.glyphs) {
            if (g.index >= 0 && g.index < (int)font->glyphs.size()) {
                Matrix gm = base * Matrix{s, 0, 0, s, x, y};
                drawGlyph(r, dst, clip, font->glyphs[g.index], gm, rec.color, cx);
            }
            x += g.advance;
        }
    }
}

// ---------------------------------------------------------------- TextField

TextField::TextField(const Movie& movie, EditTextCharacter* def)
    : DisplayObject(def), movie_(movie), edit_(def) {
    bounds = def->bounds;
    border = background = def->border;
    variable = def->variableName;
    color_ = def->color;
    if (def->html) setHtmlText(def->initialText);
    else setText(def->initialText);
}

namespace {

// Whether the font has a glyph for every character of `text` but spaces and line breaks.
bool covers(const FontCharacter& font, const std::u32string& text) {
    for (char32_t c : text)
        if (c != ' ' && c != '\r' && c != '\n' && c != '\t' && font.glyphIndex(c) < 0) return false;
    return true;
}

}  // namespace

const FontCharacter* TextField::findFont(const std::string& face, bool bold, bool italic) const {
    const FontCharacter* nameOnly = nullptr;
    for (auto& [id, c] : movie_.characters()) {
        if (c->kind != Character::Font) continue;
        auto* f = static_cast<const FontCharacter*>(c.get());
        if (f->glyphs.empty() || f->name != face) continue;
        if (f->bold == bold && f->italic == italic) return f;
        if (!nameOnly || (f->bold == bold)) nameOnly = f;
    }
    return nameOnly;
}

void TextField::setTextColor(Color c) {
    color_ = c;
    for (auto& p : paragraphs_)
        for (auto& r : p.runs) r.format.color = c;
    dirty_ = true;
}

void TextField::setText(const std::string& plain) {
    text_ = plain;
    html_ = false;
    paragraphs_.clear();
    Format fmt;
    auto* fc = movie_.character(edit_->fontId);
    fmt.font = fc && fc->kind == Character::Font ? static_cast<FontCharacter*>(fc) : nullptr;
    fmt.size = edit_->fontHeight;
    fmt.color = color_;
    fmt.align = edit_->align;
    Paragraph p;
    p.align = edit_->align;
    p.runs.push_back({U"", fmt});
    for (char32_t c : utf8Decode(plain)) {
        if (c == '\r' || c == '\n') {
            paragraphs_.push_back(p);
            p.runs.back().text.clear();
            continue;
        }
        p.runs.back().text.push_back(c);
    }
    paragraphs_.push_back(p);
    dirty_ = true;
}

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::u32string decodeEntities(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '&') {
            out.push_back(s[i]);
            continue;
        }
        size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 8) {
            out.push_back('&');
            continue;
        }
        std::string ent = s.substr(i + 1, semi - i - 1);
        std::u32string rep;
        if (ent == "amp") out += "&";
        else if (ent == "lt") out += "<";
        else if (ent == "gt") out += ">";
        else if (ent == "quot") out += "\"";
        else if (ent == "apos") out += "'";
        else if (ent == "nbsp") out += "\xC2\xA0";
        else if (!ent.empty() && ent[0] == '#') {
            long cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')
                          ? std::strtol(ent.c_str() + 2, nullptr, 16)
                          : std::strtol(ent.c_str() + 1, nullptr, 10);
            out += utf8Encode(std::u32string(1, (char32_t)cp));
        } else {
            out.push_back('&');
            continue;
        }
        i = semi;
    }
    return utf8Decode(out);
}

// Parses `name="value" name2='v' name3=v` into lower-case keys.
std::unordered_map<std::string, std::string> parseAttrs(const std::string& s) {
    std::unordered_map<std::string, std::string> a;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
        size_t ks = i;
        while (i < s.size() && s[i] != '=' && !std::isspace((unsigned char)s[i])) i++;
        std::string key = lower(s.substr(ks, i - ks));
        while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
        std::string val;
        if (i < s.size() && s[i] == '=') {
            i++;
            while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
            if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
                char q = s[i++];
                size_t vs = i;
                while (i < s.size() && s[i] != q) i++;
                val = s.substr(vs, i - vs);
                i++;
            } else {
                size_t vs = i;
                while (i < s.size() && !std::isspace((unsigned char)s[i])) i++;
                val = s.substr(vs, i - vs);
            }
        }
        if (!key.empty()) a[key] = val;
    }
    return a;
}

}  // namespace

void TextField::setHtmlText(const std::string& src) {
    html_ = true;
    paragraphs_.clear();
    Format base;
    auto* fc = movie_.character(edit_->fontId);
    base.font = fc && fc->kind == Character::Font ? static_cast<FontCharacter*>(fc) : nullptr;
    base.size = edit_->fontHeight;
    base.color = color_;
    base.align = edit_->align;
    std::vector<Format> stack{base};
    std::string faceName = base.font ? base.font->name : "";
    std::vector<std::string> faces{faceName};

    Paragraph cur;
    cur.align = base.align;
    bool curHasContent = false;
    std::u32string plain;  // text_ mirrors the visible text
    auto newParagraph = [&]() {
        paragraphs_.push_back(cur);
        cur = Paragraph{};
        cur.align = stack.back().align;
        curHasContent = false;
        plain.push_back(U'\r');
    };
    auto addText = [&](const std::u32string& t) {
        for (char32_t c : t) {
            if (c == '\r' || c == '\n') {
                newParagraph();
                continue;
            }
            if (cur.runs.empty() || cur.runs.back().format.font != stack.back().font ||
                cur.runs.back().format.size != stack.back().size ||
                std::memcmp(&cur.runs.back().format.color, &stack.back().color, sizeof(Color)))
                cur.runs.push_back({U"", stack.back()});
            cur.runs.back().text.push_back(c);
            plain.push_back(c);
            curHasContent = true;
        }
    };

    size_t i = 0;
    bool lastWasCloseP = false;
    while (i < src.size()) {
        if (src[i] != '<') {
            size_t j = src.find('<', i);
            if (j == std::string::npos) j = src.size();
            addText(decodeEntities(src.substr(i, j - i)));
            lastWasCloseP = false;
            i = j;
            continue;
        }
        size_t j = src.find('>', i);
        if (j == std::string::npos) break;
        std::string tag = src.substr(i + 1, j - i - 1);
        i = j + 1;
        bool closing = !tag.empty() && tag[0] == '/';
        if (closing) tag = tag.substr(1);
        if (!tag.empty() && tag.back() == '/') tag.pop_back();
        size_t sp = 0;
        while (sp < tag.size() && !std::isspace((unsigned char)tag[sp])) sp++;
        std::string name = lower(tag.substr(0, sp));
        auto attrs = parseAttrs(tag.substr(sp));

        // <br> and Flash's <sbr> (soft break, Shift+Enter in the editor) end the line.
        if (name == "br" || name == "sbr") {
            newParagraph();
            continue;
        }
        if (name == "p") {
            if (closing) {
                newParagraph();
                lastWasCloseP = true;
            } else {
                if (curHasContent) newParagraph();
                auto a = attrs.find("align");
                if (a != attrs.end()) {
                    std::string v = lower(a->second);
                    cur.align = v == "right" ? 1 : v == "center" ? 2 : v == "justify" ? 3 : 0;
                }
                lastWasCloseP = false;
            }
            continue;
        }
        lastWasCloseP = false;
        if (closing) {
            if (stack.size() > 1) {
                stack.pop_back();
                faces.pop_back();
            }
            continue;
        }
        Format f = stack.back();
        std::string face = faces.back();
        if (name == "b") f.bold = true;
        if (name == "i") f.italic = true;
        if (name == "font") {
            if (attrs.count("face")) face = attrs["face"];
            if (attrs.count("size")) {
                const std::string& v = attrs["size"];
                double px = std::atof(v.c_str());
                if (!v.empty() && (v[0] == '+' || v[0] == '-')) f.size += px * 20;
                else if (px > 0) f.size = px * 20;
            }
            if (attrs.count("color")) {
                std::string v = attrs["color"];
                if (!v.empty() && v[0] == '#') v = v.substr(1);
                unsigned long rgb = std::strtoul(v.c_str(), nullptr, 16);
                f.color = {(uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb, 255};
            }
            if (attrs.count("letterspacing")) f.letterSpacing = std::atof(attrs["letterspacing"].c_str()) * 20;
        }
        if (name == "b" || name == "i" || name == "font") {
            if (auto* ff = findFont(face, f.bold, f.italic)) f.font = ff;
        }
        stack.push_back(f);
        faces.push_back(face);
    }
    if (curHasContent || !lastWasCloseP || paragraphs_.empty()) paragraphs_.push_back(cur);
    else if (!plain.empty() && plain.back() == U'\r') plain.pop_back();
    text_ = utf8Encode(plain);
    dirty_ = true;
}

void TextField::hashContent(RenderHash& h) {
    h.add(layoutVersion_);
    h.add((uint64_t)border << 1 | background);
    auto rgba = [](Color c) { return (uint64_t)(c.r | c.g << 8 | c.b << 16 | c.a << 24); };
    h.add(rgba(borderColor) << 32 | rgba(backgroundColor));
    for (double v : {bounds.xMin, bounds.yMin, bounds.xMax, bounds.yMax}) h.add(v);
}

void TextField::layout() {
    layoutVersion_ = nextContentVersion();
    glyphs_.clear();
    lines_.clear();
    const double gutter = 40;  // 2 px
    double left = bounds.xMin + gutter + edit_->leftMargin;
    double maxWidth = (bounds.xMax - bounds.xMin) - 2 * gutter - edit_->leftMargin - edit_->rightMargin;
    double y = bounds.yMin + gutter;

    struct Item {
        char32_t c;
        const Format* f;
        int glyph;
        double adv;
    };
    // Embedded fonts often hold only the characters of the field's initial text (Flash's
    // automatic embedding), and the game later shows other text in them: the star name
    // field's Comic Sans has just "Star Name". A run whose font lacks some of its characters
    // is drawn in another font that has them all: a fallback font of the same name (see
    // addFallbackFont: Comic Sans when the build embeds it), else an embedded font of the
    // same weight (Arial).
    std::deque<Format> substitutes;
    auto formatFor = [&](const Run& run) -> const Format* {
        const FontCharacter* font = run.format.font;
        if (!font || covers(*font, run.text)) return &run.format;
        const FontCharacter* best = nullptr;
        int bestScore = -1;
        auto consider = [&](const FontCharacter* f) {
            if (f->glyphs.empty() || !covers(*f, run.text)) return;
            int score = 8 * (f->name == font->name) + 4 * (f->bold == font->bold) + 2 * (f->italic == font->italic) +
                        (f->name == "Arial");
            if (score > bestScore) best = f, bestScore = score;
        };
        for (auto& f : fallbackFonts()) consider(f.get());
        for (auto& [id, c] : movie_.characters())
            if (c->kind == Character::Font) consider(static_cast<const FontCharacter*>(c.get()));
        if (!best) return &run.format;
        substitutes.push_back(run.format);
        substitutes.back().font = best;
        return &substitutes.back();
    };
    for (const Paragraph& p : paragraphs_) {
        std::vector<Item> items;
        const Format* lastFmt = p.runs.empty() ? nullptr : formatFor(p.runs.back());
        for (const Run& run : p.runs) {
            const Format* fmt = formatFor(run);
            for (char32_t c : run.text) {
                const FontCharacter* font = fmt->font;
                int g = font ? font->glyphIndex(c) : -1;
                double s = font ? fmt->size / font->emSize : 0;
                double adv = 0;
                if (g >= 0 && g < (int)font->advances.size()) adv = font->advances[g] * s;
                else if (c == ' ') adv = fmt->size / 4;
                items.push_back({c, fmt, g, adv + fmt->letterSpacing});
            }
        }

        // Break into lines.
        std::vector<std::pair<size_t, size_t>> lines;
        size_t start = 0;
        double w = 0;
        size_t lastSpace = std::string::npos;
        for (size_t k = 0; k < items.size(); k++) {
            if (edit_->wordWrap && w + items[k].adv > maxWidth && k > start && items[k].c != ' ') {
                size_t brk = lastSpace != std::string::npos && lastSpace > start ? lastSpace + 1 : k;
                lines.push_back({start, brk});
                start = brk;
                w = 0;
                for (size_t q = start; q <= k; q++) w += items[q].adv;
                lastSpace = std::string::npos;
                continue;
            }
            if (items[k].c == ' ') lastSpace = k;
            w += items[k].adv;
        }
        lines.push_back({start, items.size()});

        for (auto [a, b] : lines) {
            double asc = 0, desc = 0, width = 0;
            for (size_t k = a; k < b; k++) {
                const Format* f = items[k].f;
                if (!f->font) continue;
                double s = f->size / f->font->emSize;
                asc = std::max(asc, f->font->ascent * s);
                desc = std::max(desc, f->font->descent * s);
                width += items[k].adv;
            }
            // Trailing spaces do not count for alignment.
            for (size_t k = b; k > a && items[k - 1].c == ' '; k--) width -= items[k - 1].adv;
            if (a == b && lastFmt && lastFmt->font) {
                double s = lastFmt->size / lastFmt->font->emSize;
                asc = lastFmt->font->ascent * s;
                desc = lastFmt->font->descent * s;
            }
            double x = left;
            if (p.align == 1) x = left + maxWidth - width;
            else if (p.align == 2) x = left + (maxWidth - width) / 2;
            {
                std::u32string lt;
                for (size_t k = a; k < b; k++) lt.push_back(items[k].c);
                lines_.push_back({utf8Encode(lt), x, width});
            }
            double baseline = y + asc;
            for (size_t k = a; k < b; k++) {
                const Item& it = items[k];
                if (it.glyph >= 0 && edit_->password == false) {
                    double s = it.f->size / it.f->font->emSize;
                    glyphs_.push_back({&it.f->font->glyphs[it.glyph], x, baseline, s, it.f->color});
                }
                x += it.adv;
            }
            y += asc + desc + edit_->leading;
        }
    }
    dirty_ = false;
}

Rect TextField::textExtent() {
    if (dirty_) layout();
    Rect out;
    bool any = false;
    for (const PlacedGlyph& g : glyphs_)
        for (const auto& layer : g.shape->layers)
            for (const FillPath& fp : layer.fills)
                for (const Edge& e : fp.edges)
                    for (Point q : {e.from, e.to, e.ctrl}) {
                        double x = g.x + q.x * g.scale, y = g.y + q.y * g.scale;
                        if (!any) out = {x, y, x, y}, any = true;
                        out.xMin = std::min(out.xMin, x);
                        out.yMin = std::min(out.yMin, y);
                        out.xMax = std::max(out.xMax, x);
                        out.yMax = std::max(out.yMax, y);
                    }
    return out;
}

void TextField::render(Rasterizer& r, Surface& dst, const IntRect& clip, const Matrix& m,
                       const ColorTransform& cx, const Movie& movie) {
    if (dirty_) layout();
    // Text fields clip their contents to their bounds.
    Rect dev = m.apply(bounds);
    IntRect c{std::max(clip.x0, (int)std::floor(dev.xMin)), std::max(clip.y0, (int)std::floor(dev.yMin)),
              std::min(clip.x1, (int)std::ceil(dev.xMax)), std::min(clip.y1, (int)std::ceil(dev.yMax))};
    if (c.empty()) return;
    if (background || border) {
        Point q[4] = {m.apply(bounds.xMin, bounds.yMin), m.apply(bounds.xMax, bounds.yMin),
                      m.apply(bounds.xMax, bounds.yMax), m.apply(bounds.xMin, bounds.yMax)};
        Paint p;
        p.cxform = cx;
        if (background) {
            r.reset();
            r.closedPolygon(q, 4);
            p.color = backgroundColor;
            r.fill(dst, c, p);
        }
        if (border) {
            r.reset();
            for (int i = 0; i < 4; i++) {
                Point a = q[i], b = q[(i + 1) % 4];
                double dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
                if (len < 1e-9) continue;
                double hw = 0.5 * renderSettings.scale;  // one stage pixel wide
                double nx = -dy / len * hw, ny = dx / len * hw;
                Point s[4] = {{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}};
                r.closedPolygon(s, 4);
            }
            p.color = borderColor;
            r.fill(dst, c, p);
        }
    }
    for (const PlacedGlyph& g : glyphs_)
        drawGlyph(r, dst, c, *g.shape, m * Matrix{g.scale, 0, 0, g.scale, g.x, g.y}, g.color, cx);
}

}  // namespace flash
