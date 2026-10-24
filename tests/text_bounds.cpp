// Which coordinate space are DefineText bounds in? For every static text, lay out the
// glyphs as the renderer does and test whether the raw bounds or matrix-applied bounds
// contain them.
#include <cmath>
#include <cstdio>
#include "flash/Text.h"
using namespace flash;
int main(int argc, char** argv) {
    Movie m;
    std::string err;
    if (argc < 2 || !m.load(argv[1], err)) return 1;
    int texts = 0, rawFits = 0, matrixFits = 0, measured = 0;
    double rawErr = 0, matErr = 0;
    for (auto& [id, c] : m.characters()) {
        if (c->kind != Character::Text) continue;
        auto* t = static_cast<StaticTextCharacter*>(c.get());
        texts++;
        // Glyph extent in text space (before the text matrix), as the renderer lays it out.
        Rect ext;
        bool any = false;
        double x = 0, y = 0;
        for (auto& rec : t->records) {
            auto* fc = m.character(rec.fontId);
            if (!fc || fc->kind != Character::Font) continue;
            auto* font = static_cast<FontCharacter*>(fc);
            if (rec.hasX) x = rec.x;
            if (rec.hasY) y = rec.y;
            double s = rec.height / font->emSize;
            for (auto& g : rec.glyphs) {
                if (g.index >= 0 && g.index < (int)font->glyphs.size())
                    for (auto& layer : font->glyphs[g.index].layers)
                        for (auto& fp : layer.fills)
                            for (auto& e : fp.edges)
                                for (Point q : {e.from, e.to}) {
                                    Point p = t->matrix.apply(x + q.x * s, y + q.y * s);
                                    if (!any) ext = {p.x, p.y, p.x, p.y}, any = true;
                                    ext.xMin = std::min(ext.xMin, p.x); ext.xMax = std::max(ext.xMax, p.x);
                                    ext.yMin = std::min(ext.yMin, p.y); ext.yMax = std::max(ext.yMax, p.y);
                                }
                x += g.advance;
            }
        }
        if (!any) continue;
        measured++;
        Rect raw = t->bounds, mat = t->matrix.apply(t->bounds);
        auto err = [&](const Rect& b) {  // how far glyphs stick out of b (twips)
            return std::max({b.xMin - ext.xMin, ext.xMax - b.xMax, b.yMin - ext.yMin, ext.yMax - b.yMax, 0.0});
        };
        double re = err(raw), me = err(mat);
        rawErr = std::max(rawErr, re);
        matErr = std::max(matErr, me);
        rawFits += re <= 40;  // within 2 px
        matrixFits += me <= 40;
    }
    if (rawFits != measured) {
        std::printf("FAIL: ");
    }
    std::printf("%d static texts (%d with glyphs): glyphs inside RAW bounds for %d, inside "
                "MATRIX-applied bounds for %d; worst overhang raw %.1f px, matrix %.1f px\n",
                texts, measured, rawFits, matrixFits, rawErr / 20, matErr / 20);
    return rawFits == measured ? 0 : 1;
}
