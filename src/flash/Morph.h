#pragma once
// DefineMorphShape / DefineMorphShape2: a shape interpolated between a start and an
// end state by the placement ratio (0..65535).

#include <map>
#include <mutex>
#include <vector>

#include "flash/Shape.h"
#include "flash/Swf.h"

namespace flash {

class Reader;

struct MorphShapeCharacter : Character {
    MorphShapeCharacter() : Character(MorphShape) {}

    void parse(Reader& r, int version);  // version 1 or 2, positioned after the id
    // Returns the interpolated shape (cached). Safe to call from several render threads:
    // entries are only added here, never removed.
    const ShapeDef& at(uint16_t ratio);
    // Bounds the cache; call only while no thread is rendering.
    void trimCache();

    Rect startBounds, endBounds;
    std::vector<FillStyle> startFills, endFills;
    std::vector<LineStyle> startLines, endLines;
    std::vector<ShapeRecord> startRecords, endRecords;

private:
    std::map<uint16_t, ShapeDef> cache_;
    std::mutex cacheMutex_;
};

}  // namespace flash
