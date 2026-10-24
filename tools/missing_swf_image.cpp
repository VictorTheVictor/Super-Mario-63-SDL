// Renders the "SWF not found" message with the game's sign font (the embedded Arial the
// in-game signs use) and writes it as a C++ header, so the game can show it without the
// SWF. White text with a dark outline, like the signs.
//
// usage: missing_swf_image game.swf out.h
//
// The header holds a run-length encoded image: pairs of (run length - 1, value) bytes
// over grayscale+alpha pixels, decoded by src/MissingSwf.cpp.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "flash/Raster.h"
#include "flash/Swf.h"
#include "flash/Text.h"

using namespace flash;

static const char* kMessage =
    "Super_Mario_63_2010.swf not found. Please place Super_Mario_63_2010.swf in the same directory or drag it "
    "into the executable file.";

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s game.swf out.h\n", argv[0]);
        return 2;
    }
    Movie movie;
    std::string err;
    if (!movie.load(argv[1], err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    // The sign and message box text fields use this font (an embedded Arial subset); pick
    // the embedded Arial that has every character of the message.
    const FontCharacter* font = nullptr;
    for (int id : {155, 187, 9463}) {
        auto* c = movie.character((uint16_t)id);
        if (!c || c->kind != Character::Font) continue;
        auto* f = static_cast<const FontCharacter*>(c);
        bool complete = true;
        for (const char* p = kMessage; *p; p++)
            if (*p != ' ' && f->glyphIndex((unsigned char)*p) < 0) {
                std::fprintf(stderr, "font %d lacks '%c'\n", id, *p);
                complete = false;
            }
        if (complete) {
            font = f;
            std::printf("using font %d (%s)\n", id, f->name.c_str());
            break;
        }
    }
    if (!font) return 1;

    // Layout: word wrap to a fixed width, lines centered.
    const double size = 34;              // pixels per em
    const double k = size / font->emSize;
    const int width = 860, pad = 6;      // outline room
    auto advance = [&](char ch) {
        int g = font->glyphIndex((unsigned char)ch);
        return g < 0 ? size * 0.28 : font->advances[g] * k;
    };
    std::vector<std::string> lines(1);
    std::string word, text = std::string(kMessage) + " ";
    auto widthOf = [&](const std::string& s) {
        double w = 0;
        for (char ch : s) w += advance(ch);
        return w;
    };
    for (char ch : text) {
        if (ch != ' ') {
            word += ch;
            continue;
        }
        std::string candidate = lines.back().empty() ? word : lines.back() + " " + word;
        if (!lines.back().empty() && widthOf(candidate) > width - 2 * pad) lines.push_back(word);
        else lines.back() = candidate;
        word.clear();
    }
    double ascent = font->ascent > 0 ? font->ascent * k : size * 0.9;
    double lineHeight = size * 1.25;
    int height = (int)std::ceil(lines.size() * lineHeight + 2 * pad + size * 0.25);

    // Glyphs in white, then an outline by dilating their coverage.
    Surface glyphs;
    glyphs.resize(width, height);
    Rasterizer r;
    IntRect all{0, 0, width, height};
    for (size_t i = 0; i < lines.size(); i++) {
        double x = (width - widthOf(lines[i])) / 2, baseline = pad + ascent + i * lineHeight;
        for (char ch : lines[i]) {
            int g = font->glyphIndex((unsigned char)ch);
            if (g >= 0) {
                Matrix m{k, 0, 0, k, x, baseline};
                drawGlyph(r, glyphs, all, font->glyphs[g], m, Color{255, 255, 255, 255}, ColorTransform{});
            }
            x += advance(ch);
        }
    }
    std::vector<uint8_t> text_a(width * height), outline_a(width * height);
    for (int i = 0; i < width * height; i++) text_a[i] = glyphs.pixels[i] >> 24;
    const int radius = 3;
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            int best = 0;
            for (int dy = -radius; dy <= radius; dy++)
                for (int dx = -radius; dx <= radius; dx++) {
                    if (dx * dx + dy * dy > radius * radius + 1) continue;
                    int sx = x + dx, sy = y + dy;
                    if (sx < 0 || sy < 0 || sx >= width || sy >= height) continue;
                    best = std::max<int>(best, text_a[sy * width + sx]);
                }
            outline_a[y * width + x] = (uint8_t)best;
        }
    // Gray + alpha: white text over a dark (#202020) outline.
    std::vector<uint8_t> la(width * height * 2);
    for (int i = 0; i < width * height; i++) {
        double t = text_a[i] / 255.0, o = outline_a[i] / 255.0;
        double alpha = t + o * (1 - t);
        double gray = alpha > 0 ? (t * 255 + o * (1 - t) * 32) / alpha : 0;
        la[2 * i] = (uint8_t)std::lround(gray);
        la[2 * i + 1] = (uint8_t)std::lround(alpha * 255);
    }
    // Run-length encode 2-byte pixels: (count - 1, gray, alpha) triples.
    std::vector<uint8_t> rle;
    for (size_t i = 0; i < la.size() / 2;) {
        size_t j = i + 1;
        while (j < la.size() / 2 && j - i < 256 && la[2 * j] == la[2 * i] && la[2 * j + 1] == la[2 * i + 1]) j++;
        rle.push_back((uint8_t)(j - i - 1));
        rle.push_back(la[2 * i]);
        rle.push_back(la[2 * i + 1]);
        i = j;
    }

    FILE* out = std::fopen(argv[2], "w");
    if (!out) return 1;
    std::fprintf(out,
                 "// Generated by tools/missing_swf_image.cpp from the game's sign font: the message\n"
                 "// shown when Super_Mario_63_2010.swf is missing. Run-length encoded gray+alpha pixels\n"
                 "// as (count - 1, gray, alpha) triples.\n"
                 "#pragma once\n#include <cstdint>\n\n"
                 "namespace missing_swf {\nconstexpr int kWidth = %d, kHeight = %d;\n"
                 "constexpr uint8_t kRle[] = {",
                 width, height);
    for (size_t i = 0; i < rle.size(); i++) std::fprintf(out, "%s%u,", i % 24 ? "" : "\n    ", rle[i]);
    std::fprintf(out, "\n};\n}  // namespace missing_swf\n");
    std::fclose(out);
    std::printf("%zu lines, %dx%d, %zu bytes encoded\n", lines.size(), width, height, rle.size());
    return 0;
}
