#pragma once
// Fonts compiled into the executable when present at build time (see cmake/embed_fonts.cmake):
// registers them as text layout fallbacks. Does nothing in builds without them.
void registerEmbeddedFonts();
