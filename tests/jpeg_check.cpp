// Decodes a JPEG file (optionally preceded by a separate tables file, as in SWF
// DefineBits) and writes raw RGB for comparison with a reference decoder.
#include <cstdio>
#include <vector>
#include "flash/Jpeg.h"

static std::vector<uint8_t> slurp(const char* p) {
    std::vector<uint8_t> v;
    FILE* f = std::fopen(p, "rb");
    if (!f) return v;
    int c;
    while ((c = std::fgetc(f)) != EOF) v.push_back((uint8_t)c);
    std::fclose(f);
    return v;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: jpeg_check in.jpg out.rgb [tables.bin]\n");
        return 2;
    }
    auto img = slurp(argv[1]);
    std::vector<uint8_t> tables = argc > 3 ? slurp(argv[3]) : std::vector<uint8_t>{};
    std::vector<std::pair<const uint8_t*, size_t>> streams;
    if (!tables.empty()) streams.push_back({tables.data(), tables.size()});
    streams.push_back({img.data(), img.size()});
    int w, h;
    std::vector<uint32_t> px;
    if (!flash::decodeJpeg(streams, w, h, px)) {
        std::fprintf(stderr, "decode failed\n");
        return 1;
    }
    FILE* f = std::fopen(argv[2], "wb");
    for (uint32_t p : px) {
        uint8_t rgb[3] = {(uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
    std::printf("%d %d\n", w, h);
    return 0;
}
