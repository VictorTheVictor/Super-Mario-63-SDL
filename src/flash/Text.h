#pragma once
// Embedded fonts (DefineFont2/3), static text (DefineText) and text fields
// (DefineEditText) with a minimal HTML subset.

#include <string>
#include <unordered_map>
#include <vector>

#include "flash/DisplayObject.h"
#include "flash/Shape.h"
#include "flash/Swf.h"

namespace flash {

class Reader;

struct FontCharacter : Character {
    FontCharacter() : Character(Font) {}
    void parse(Reader& r, int version);  // after the id

    std::string name;
    bool bold = false, italic = false, hasLayout = false;
    double emSize = 1024;  // glyph units per em (DefineFont3 uses 20x resolution)
    std::vector<ShapeDef> glyphs;
    std::vector<uint16_t> codes;
    std::vector<double> advances;  // glyph units
    double ascent = 0, descent = 0, leading = 0;
    std::unordered_map<uint32_t, int> glyphForCode;

    int glyphIndex(uint32_t code) const {
        auto it = glyphForCode.find(code);
        return it == glyphForCode.end() ? -1 : it->second;
    }
};

struct StaticTextCharacter : Character {
    StaticTextCharacter() : Character(Text) {}
    void parse(Reader& r, int version);  // after the id; version 1 = RGB, 2 = RGBA

    struct Glyph {
        int index;
        double advance;
    };
    struct Record {
        uint16_t fontId = 0;
        Color color;
        double x = 0, y = 0, height = 0;
        bool hasX = false, hasY = false;
        std::vector<Glyph> glyphs;
    };
    Rect bounds;
    Matrix matrix;
    std::vector<Record> records;
};

struct EditTextCharacter : Character {
    EditTextCharacter() : Character(EditText) {}
    void parse(Reader& r);  // after the id

    Rect bounds;
    bool wordWrap = false, multiline = false, password = false, readOnly = false,
         autoSize = false, noSelect = false, border = false, html = false, useOutlines = false;
    uint16_t fontId = 0;
    double fontHeight = 240;  // twips
    Color color{0, 0, 0, 255};
    int maxLength = 0;
    uint8_t align = 0;  // 0 left, 1 right, 2 center, 3 justify
    double leftMargin = 0, rightMargin = 0, indent = 0, leading = 0;
    std::string variableName;
    std::string initialText;
};

class StaticTextObject : public DisplayObject {
public:
    using DisplayObject::DisplayObject;
    void render(Rasterizer& r, Surface& dst, const IntRect& clip, const Matrix& m,
                const ColorTransform& cx, const Movie& movie) override;
    // DefineText bounds are already in character space (they include the text matrix);
    // verified against the glyph layout of all 280 static texts in the game.
    Rect localBounds() const override { return static_cast<StaticTextCharacter*>(def_)->bounds; }
    bool hitTest(const Matrix& m, Point p) const override {
        if (!m.invertible()) return false;
        Rect b = localBounds();
        Point q = m.inverse().apply(p);
        return q.x >= b.xMin && q.x <= b.xMax && q.y >= b.yMin && q.y <= b.yMax;
    }
};

// TextField runtime object. Text is UTF-8.
class TextField : public DisplayObject {
public:
    TextField(const Movie& movie, EditTextCharacter* def);

    const std::string& text() const { return text_; }
    void setText(const std::string& plain);
    void setHtmlText(const std::string& html);
    bool html() const { return html_; }
    void hashContent(RenderHash& h) override;
    void prepareForRender() {
        if (dirty_) layout();
    }

    Rect bounds;  // twips, local
    std::string variable;  // bound variable path ("score", "_root.lives")
    Color textColor() const { return color_; }
    void setTextColor(Color c);
    bool border = false, background = false;
    Color borderColor{0, 0, 0, 255}, backgroundColor{255, 255, 255, 255};

    void render(Rasterizer& r, Surface& dst, const IntRect& clip, const Matrix& m,
                const ColorTransform& cx, const Movie& movie) override;
    Rect localBounds() const override { return bounds; }
    // Extent of the laid-out glyph outlines (local twips); empty if no text.
    Rect textExtent();
    // Laid-out lines (for diagnostics): text, x offset and width in twips.
    struct LineInfo {
        std::string text;
        double x, width;
    };
    const std::vector<LineInfo>& lines() {
        if (dirty_) layout();
        return lines_;
    }
    bool hitTest(const Matrix& m, Point p) const override {
        if (!m.invertible()) return false;
        Point q = m.inverse().apply(p);
        return q.x >= bounds.xMin && q.x <= bounds.xMax && q.y >= bounds.yMin && q.y <= bounds.yMax;
    }
    const EditTextCharacter* edit() const { return edit_; }

    struct Format {
        const FontCharacter* font = nullptr;
        double size = 240;  // twips
        Color color;
        bool bold = false, italic = false;
        uint8_t align = 0;
        double letterSpacing = 0;
    };

    // The laid-out glyphs: shapes in glyph units, placed at (x, y) local twips, scaled.
    struct PlacedGlyph {
        const ShapeDef* shape;
        double x, y, scale;
        Color color;
    };
    const std::vector<PlacedGlyph>& placedGlyphs() {
        if (dirty_) layout();
        return glyphs_;
    }

private:
    struct Run {
        std::u32string text;
        Format format;
    };
    struct Paragraph {
        std::vector<Run> runs;
        uint8_t align = 0;
    };
    void layout();
    const FontCharacter* findFont(const std::string& face, bool bold, bool italic) const;

    const Movie& movie_;
    EditTextCharacter* edit_;
    std::string text_;
    bool html_ = false;
    Color color_;
    std::vector<Paragraph> paragraphs_;
    std::vector<PlacedGlyph> glyphs_;
    std::vector<LineInfo> lines_;
    bool dirty_ = true;
    uint64_t layoutVersion_ = 0;  // nextContentVersion() of the last layout
};

// Fills all of a glyph's paths with a solid color.
void drawGlyph(Rasterizer& r, Surface& dst, const IntRect& clip, const ShapeDef& glyph,
               const Matrix& m, Color color, const ColorTransform& cx);

// UTF-8 helpers
std::u32string utf8Decode(const std::string& s);
std::string utf8Encode(const std::u32string& s);

// Draws one font glyph (glyph units mapped to device pixels by `m`) in a solid color.
void drawGlyph(Rasterizer& r, Surface& dst, const IntRect& clip, const ShapeDef& glyph, const Matrix& m,
               Color color, const ColorTransform& cx);

}  // namespace flash
