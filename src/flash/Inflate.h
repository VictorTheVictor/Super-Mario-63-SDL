#pragma once
// Minimal zlib/DEFLATE decoder (RFC 1950/1951). Used for compressed SWF bodies
// and DefineBitsLossless pixel data.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace flash {

// Decodes a zlib stream (2-byte header + DEFLATE data). `sizeHint` preallocates output.
// Returns false on corrupt data; `out` then holds whatever was decoded.
bool zlibDecompress(const uint8_t* data, size_t len, std::vector<uint8_t>& out,
                    size_t sizeHint = 0);

}  // namespace flash
