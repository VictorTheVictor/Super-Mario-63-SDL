// Counts shape fills that reference bitmap 65535 ("no bitmap") or a nonexistent bitmap.
#include <cstdio>
#include "flash/Swf.h"
using namespace flash;
int main(int argc, char** argv) {
    Movie m;
    std::string err;
    if (argc < 2 || !m.load(argv[1], err)) return 1;
    int shapes = 0, fills = 0, sentinel = 0, other = 0;
    for (auto& [id, c] : m.characters()) {
        if (c->kind != Character::Shape) continue;
        bool any = false;
        for (auto& f : static_cast<ShapeCharacter*>(c.get())->def.fills) {
            if (!f.isBitmap()) continue;
            Character* b = m.character(f.bitmapId);
            if (b && b->kind == Character::Bitmap) continue;
            any = true;
            fills++;
            (f.bitmapId == 65535 ? sentinel : other)++;
        }
        shapes += any;
    }
    std::printf("%d shapes have %d bitmap fills without a bitmap (%d use id 65535, %d other ids)\n", shapes, fills,
                sentinel, other);
}
