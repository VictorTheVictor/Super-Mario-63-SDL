// Structural checks over a real SWF (no rendering): morph shape pairing, fonts, text.
#include <cmath>
#include <cstdio>
#include "flash/Morph.h"
#include "flash/Swf.h"
#include "flash/Text.h"

using namespace flash;

static int edgeCount(const std::vector<ShapeRecord>& v) {
    int n = 0;
    for (auto& r : v) n += r.isEdge;
    return n;
}

static bool sameEdges(const ShapeDef& d, const std::vector<ShapeRecord>& recs, bool useEnd) {
    // Every edge endpoint of the interpolated shape must match the source records.
    double maxErr = 0;
    std::vector<Point> pts;
    for (auto& r : recs)
        if (r.isEdge) pts.push_back(r.edge.to);
    std::vector<Point> got;
    for (auto& l : d.layers) {
        for (auto& f : l.fills)
            for (auto& e : f.edges) got.push_back(e.to), got.push_back(e.from);
        for (auto& s : l.strokes)
            for (auto& pl : s.polylines)
                for (auto& e : pl) got.push_back(e.to);
    }
    for (auto& g : got) {
        double best = 1e30;
        for (auto& p : pts) best = std::min(best, std::hypot(g.x - p.x, g.y - p.y));
        maxErr = std::max(maxErr, best);
    }
    (void)useEnd;
    return maxErr < 0.5;
}

int main(int argc, char** argv) {
    Movie m;
    std::string err;
    if (argc < 2 || !m.load(argv[1], err)) {
        std::fprintf(stderr, "load failed %s\n", err.c_str());
        return 1;
    }
    int morphs = 0, mismatched = 0, badStart = 0, badEnd = 0;
    for (auto& [id, c] : m.characters()) {
        if (c->kind != Character::MorphShape) continue;
        auto* ms = static_cast<MorphShapeCharacter*>(c.get());
        morphs++;
        if (edgeCount(ms->startRecords) != edgeCount(ms->endRecords)) mismatched++;
        if (!sameEdges(ms->at(0), ms->startRecords, false)) badStart++;
        if (!sameEdges(ms->at(65535), ms->endRecords, true)) badEnd++;
    }
    std::printf("morphs %d, edge-count mismatches %d, ratio0 != start %d, ratio1 != end %d\n",
                morphs, mismatched, badStart, badEnd);

    int fonts = 0, glyphs = 0, emptyGlyphFonts = 0;
    for (auto& [id, c] : m.characters()) {
        if (c->kind != Character::Font) continue;
        auto* f = static_cast<FontCharacter*>(c.get());
        fonts++;
        glyphs += (int)f->glyphs.size();
        int withPaths = 0;
        for (auto& g : f->glyphs) withPaths += !g.layers.empty();
        // A font where no glyph has outlines means the glyph table was misread.
        if (!f->glyphs.empty() && withPaths == 0) emptyGlyphFonts++;
    }
    int texts = 0, badGlyphRefs = 0, missingFonts = 0;
    for (auto& [id, c] : m.characters()) {
        if (c->kind != Character::Text) continue;
        auto* t = static_cast<StaticTextCharacter*>(c.get());
        texts++;
        for (auto& rec : t->records) {
            auto* fc = m.character(rec.fontId);
            if (!fc || fc->kind != Character::Font) {
                missingFonts++;
                continue;
            }
            auto* f = static_cast<FontCharacter*>(fc);
            for (auto& g : rec.glyphs)
                if (g.index < 0 || g.index >= (int)f->glyphs.size()) badGlyphRefs++;
        }
    }
    int fields = 0, fieldsNoFont = 0, missingChars = 0, totalChars = 0;
    for (auto& [id, c] : m.characters()) {
        if (c->kind != Character::EditText) continue;
        auto* et = static_cast<EditTextCharacter*>(c.get());
        fields++;
        auto* fc = m.character(et->fontId);
        if (!fc || fc->kind != Character::Font) {
            fieldsNoFont++;
            continue;
        }
        TextField tf(m, et);
        for (char32_t ch : utf8Decode(tf.text())) {
            if (ch == '\r') continue;
            totalChars++;
            if (static_cast<FontCharacter*>(fc)->glyphIndex(ch) < 0) missingChars++;
        }
    }
    std::printf("fonts %d (%d glyphs, %d with no outlines), static texts %d (bad glyph refs %d, "
                "missing fonts %d)\n",
                fonts, glyphs, emptyGlyphFonts, texts, badGlyphRefs, missingFonts);
    std::printf("text fields %d (no font %d), initial chars %d, not in field font %d\n", fields,
                fieldsNoFont, totalChars, missingChars);
    int buttons = 0, buttonRecords = 0, buttonBadRefs = 0, buttonsNoUp = 0;
    for (auto& [id, c] : m.characters()) {
        if (c->kind != Character::Button) continue;
        auto* b = static_cast<ButtonCharacter*>(c.get());
        buttons++;
        bool up = false;
        for (auto& r : b->records) {
            buttonRecords++;
            if (!m.character(r.characterId)) buttonBadRefs++;
            up |= (r.states & ButtonCharacter::Up) != 0;
        }
        if (!up) buttonsNoUp++;
    }
    int jpegPlaceholders = 0;
    for (auto& [id, c] : m.characters())
        if (c->kind == Character::Bitmap && static_cast<Bitmap*>(c.get())->placeholder) jpegPlaceholders++;
    std::printf("buttons %d (%d records, bad refs %d, no Up state %d), undecoded bitmaps %d\n",
                buttons, buttonRecords, buttonBadRefs, buttonsNoUp, jpegPlaceholders);
    return mismatched || badStart || badEnd || emptyGlyphFonts || badGlyphRefs || missingFonts ||
           buttonBadRefs || jpegPlaceholders;
}
