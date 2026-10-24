#pragma once
// Baseline (sequential, Huffman) JPEG decoder for DefineBits/DefineBitsJPEG2/3.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace flash {

// Decodes one or more concatenated JPEG segment streams (e.g. the JPEGTables tag
// followed by a DefineBits payload). Output pixels are opaque 0xFFRRGGBB.
bool decodeJpeg(const std::vector<std::pair<const uint8_t*, size_t>>& streams, int& width,
                int& height, std::vector<uint32_t>& pixels);

}  // namespace flash
