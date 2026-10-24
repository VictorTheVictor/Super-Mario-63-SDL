#pragma once
// A minimal TrueType (glyf outline) reader: turns a .ttf into a FontCharacter, so text
// can be drawn in a font the SWF did not embed completely (see addFallbackFont).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "flash/Text.h"

namespace flash {

// Reads the glyphs of every character in the font's Unicode BMP cmap. Returns null for
// data it does not understand (no glyf outlines, no Unicode cmap).
std::unique_ptr<FontCharacter> loadTrueType(const uint8_t* data, size_t size, const std::string& name, bool bold,
                                            bool italic);

// Fonts text layout may substitute when a field's embedded font lacks characters; one
// with the field font's name is preferred over the SWF's other embedded fonts.
void addFallbackFont(std::unique_ptr<FontCharacter> font);
const std::vector<std::unique_ptr<FontCharacter>>& fallbackFonts();

}  // namespace flash
