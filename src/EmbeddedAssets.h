#pragma once
// Assets compiled into the executable when present at build time (see
// cmake/embed_assets.cmake). Builds without them get no fonts and an empty sound.

#include <cstddef>
#include <utility>

// Registers the embedded fonts as text layout fallbacks.
void registerEmbeddedFonts();
// The MP3 played when the pause menu opens and closes, or {nullptr, 0}.
std::pair<const unsigned char*, size_t> embeddedPauseSound();
