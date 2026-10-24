#pragma once
// DefineMorphShape / DefineMorphShape2: a shape interpolated between a start and an
// end state by the placement ratio (0..65535).

#include <map>
#include <vector>

#include "flash/Shape.h"
#include "flash/Swf.h"

namespace flash {

class Reader;

struct MorphShapeCharacter : Character {
    MorphShapeCharacter() : Character(MorphShape) {}

    void parse(Reader& r, int version);  // version 1 or 2, positioned after the id
    // Returns the interpolated shape (cached).
    const ShapeDef& at(uint16_t ratio);

    Rect startBounds, endBounds;
    std::vector<FillStyle> startFills, endFills;
    std::vector<LineStyle> startLines, endLines;
    std::vector<ShapeRecord> startRecords, endRecords;

private:
    std::map<uint16_t, ShapeDef> cache_;
};

}  // namespace flash
