// Debug tool: describe a character's subtree (render features used), e.g. charinfo game.swf 9826
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <set>
#include "flash/Morph.h"
#include "flash/Swf.h"
using namespace flash;
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    Movie m;
    std::string err;
    if (!m.load(argv[1], err)) return 1;
    std::set<int> seen;
    std::function<void(int, int)> walk = [&](int id, int depth) {
        Character* c = m.character(id);
        if (!c || !seen.insert(id).second) return;
        const char* kinds[] = {"Shape", "Sprite", "Bitmap", "Morph", "Text", "EditText", "Font", "Sound", "Button"};
        std::printf("%*s%d %s", depth * 2, "", id, kinds[c->kind]);
        if (c->kind == Character::Shape) {
            auto& d = static_cast<ShapeCharacter*>(c)->def;
            int bitmapFills = 0, gradients = 0, solids = 0;
            for (auto& f : d.fills) f.isBitmap() ? bitmapFills++ : f.isGradient() ? gradients++ : solids++;
            std::printf(" bounds %.0fx%.0f px, fills: %d solid %d gradient %d bitmap, %zu layers", (d.bounds.xMax - d.bounds.xMin) / 20,
                        (d.bounds.yMax - d.bounds.yMin) / 20, solids, gradients, bitmapFills, d.layers.size());
            for (auto& f : d.fills) if (f.isBitmap()) std::printf(" [bitmap %d type 0x%x]", f.bitmapId, f.type);
        }
        if (c->kind == Character::Sprite) {
            auto* s = static_cast<Sprite*>(c);
            int filters = 0, blends = 0, masks = 0, cx = 0, moves = 0;
            for (auto& p : s->places) {
                filters += !p.filters.empty(); blends += p.hasBlendMode && p.blendMode > 1;
                masks += p.hasClipDepth; cx += p.hasCxform; moves += p.move && !p.hasCharacter;
            }
            std::printf(" %d frames, places %zu (filters %d, blend %d, masks %d, cxform %d, moves %d)", s->frameCount,
                        s->places.size(), filters, blends, masks, cx, moves);
        }
        std::printf("\n");
        if (c->kind == Character::Sprite)
            for (auto& p : static_cast<Sprite*>(c)->places)
                if (p.hasCharacter) walk(p.characterId, depth + 1);
    };
    for (int i = 2; i < argc; i++) walk(std::atoi(argv[i]), 0);
}
