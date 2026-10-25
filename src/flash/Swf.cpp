#include "flash/Swf.h"

#include <cstdio>

#include "flash/Inflate.h"
#include "flash/Jpeg.h"
#include "flash/Morph.h"
#include "flash/Text.h"
#include "flash/Reader.h"

namespace flash {
namespace {

enum Tag {
    TagEnd = 0, TagShowFrame = 1, TagDefineShape = 2, TagPlaceObject = 4, TagRemoveObject = 5,
    TagDefineBits = 6, TagDefineButton = 7, TagJPEGTables = 8, TagSetBackgroundColor = 9,
    TagDefineFont = 10, TagDefineText = 11, TagDoAction = 12, TagDefineSound = 14,
    TagStartSound = 15, TagSoundStreamHead = 18, TagSoundStreamBlock = 19, TagSoundStreamHead2 = 45, TagDefineBitsLossless = 20, TagDefineBitsJPEG2 = 21,
    TagDefineShape2 = 22, TagPlaceObject2 = 26, TagRemoveObject2 = 28, TagDefineShape3 = 32,
    TagDefineText2 = 33, TagDefineButton2 = 34, TagDefineBitsJPEG3 = 35,
    TagDefineBitsLossless2 = 36, TagDefineEditText = 37, TagDefineSprite = 39,
    TagFrameLabel = 43, TagDefineMorphShape = 46, TagDefineFont2 = 48, TagExportAssets = 56,
    TagDoInitAction = 59, TagPlaceObject3 = 70, TagDefineFont3 = 75, TagDefineShape4 = 83,
    TagDefineMorphShape2 = 84,
};

const int kSoundRates[4] = {5512, 11025, 22050, 44100};

std::vector<Filter> parseFilters(Reader& r) {
    int n = r.u8();
    std::vector<Filter> out(n);
    for (auto& f : out) {
        f.type = r.u8();
        switch (f.type) {
            case Filter::DropShadow: {
                f.color = r.rgba();
                f.blurX = r.fixed16();
                f.blurY = r.fixed16();
                f.angle = r.fixed16();
                f.distance = r.fixed16();
                f.strength = r.fixed8();
                uint8_t fl = r.u8();
                f.inner = fl & 0x80;
                f.knockout = fl & 0x40;
                f.compositeSource = fl & 0x20;
                f.passes = fl & 0x1F;
                break;
            }
            case Filter::Blur: {
                f.blurX = r.fixed16();
                f.blurY = r.fixed16();
                f.passes = r.u8() >> 3;
                break;
            }
            case Filter::Glow: {
                f.color = r.rgba();
                f.blurX = r.fixed16();
                f.blurY = r.fixed16();
                f.strength = r.fixed8();
                uint8_t fl = r.u8();
                f.inner = fl & 0x80;
                f.knockout = fl & 0x40;
                f.compositeSource = fl & 0x20;
                f.passes = fl & 0x1F;
                break;
            }
            case Filter::Bevel: {
                // The SWF spec lists the shadow color first, but Flash writes the highlight
                // first (as Ruffle reads it too).
                f.highlight = r.rgba();
                f.color = r.rgba();
                f.blurX = r.fixed16();
                f.blurY = r.fixed16();
                f.angle = r.fixed16();
                f.distance = r.fixed16();
                f.strength = r.fixed8();
                uint8_t fl = r.u8();
                f.inner = fl & 0x80;
                f.knockout = fl & 0x40;
                f.compositeSource = fl & 0x20;
                f.onTop = fl & 0x10;
                f.passes = fl & 0x0F;
                break;
            }
            case Filter::GradientGlow:
            case Filter::GradientBevel: {
                int nc = r.u8();
                f.gradient.resize(nc);
                for (auto& s : f.gradient) s.color = r.rgba();
                for (auto& s : f.gradient) s.ratio = r.u8();
                f.blurX = r.fixed16();
                f.blurY = r.fixed16();
                f.angle = r.fixed16();
                f.distance = r.fixed16();
                f.strength = r.fixed8();
                uint8_t fl = r.u8();
                f.inner = fl & 0x80;
                f.knockout = fl & 0x40;
                f.compositeSource = fl & 0x20;
                f.onTop = fl & 0x10;
                f.passes = fl & 0x0F;
                break;
            }
            case Filter::Convolution: {
                int mx = r.u8(), my = r.u8();
                r.f32();  // divisor
                r.f32();  // bias
                for (int i = 0; i < mx * my; i++) r.f32();
                r.rgba();
                r.u8();
                break;
            }
            case Filter::ColorMatrix:
                for (float& v : f.matrix) v = r.f32();
                break;
        }
    }
    return out;
}

PlaceCommand parsePlace(Reader& r, int tag, int swfVersion) {
    PlaceCommand p;
    uint8_t f1 = r.u8();
    uint8_t f2 = tag == TagPlaceObject3 ? r.u8() : 0;
    p.hasClipActions = f1 & 0x80;
    p.hasClipDepth = f1 & 0x40;
    p.hasName = f1 & 0x20;
    p.hasRatio = f1 & 0x10;
    p.hasCxform = f1 & 0x08;
    p.hasMatrix = f1 & 0x04;
    p.hasCharacter = f1 & 0x02;
    p.move = f1 & 0x01;
    bool hasImage = f2 & 0x10, hasClassName = f2 & 0x08, hasCacheAsBitmap = f2 & 0x04;
    p.hasBlendMode = f2 & 0x02;
    p.hasFilters = f2 & 0x01;
    p.hasVisible = f2 & 0x20;
    bool hasBackground = f2 & 0x40;

    p.depth = r.u16();
    if (hasClassName || (hasImage && p.hasCharacter)) r.str();
    if (p.hasCharacter) p.characterId = r.u16();
    if (p.hasMatrix) p.matrix = r.matrix();
    if (p.hasCxform) p.cxform = r.cxform(true);
    if (p.hasRatio) p.ratio = r.u16();
    if (p.hasName) p.name = r.str();
    if (p.hasClipDepth) p.clipDepth = r.u16();
    if (p.hasFilters) p.filters = parseFilters(r);
    if (p.hasBlendMode) p.blendMode = r.u8();
    if (hasCacheAsBitmap) r.u8();
    if (p.hasVisible) p.visible = r.u8() != 0;
    if (hasBackground) r.rgba();
    if (p.hasClipActions) {
        r.u16();
        p.clipEventFlags = swfVersion >= 6 ? r.u32() : r.u16();
    }
    return p;
}

std::unique_ptr<Bitmap> parseLossless(Reader& r, size_t tagEnd, bool hasAlpha) {
    auto bmp = std::make_unique<Bitmap>();
    bmp->id = r.u16();
    int format = r.u8();
    bmp->width = r.u16();
    bmp->height = r.u16();
    int tableSize = format == 3 ? r.u8() + 1 : 0;
    std::vector<uint8_t> data;
    zlibDecompress(r.ptr(), tagEnd - r.pos(), data);
    int w = bmp->width, h = bmp->height;
    bmp->pixels.assign((size_t)w * h, 0);

    auto pack = [&](uint32_t a, uint32_t rr, uint32_t g, uint32_t b) {
        if (!hasAlpha) a = 255;
        // Lossless2 data is premultiplied already; keep it consistent.
        if (rr > a) rr = a;
        if (g > a) g = a;
        if (b > a) b = a;
        return a << 24 | rr << 16 | g << 8 | b;
    };

    if (format == 3) {
        int bpe = hasAlpha ? 4 : 3;
        std::vector<uint32_t> table(256, 0);
        size_t p = 0;
        for (int i = 0; i < tableSize && p + bpe <= data.size(); i++, p += bpe)
            table[i] = pack(hasAlpha ? data[p + 3] : 255, data[p], data[p + 1], data[p + 2]);
        size_t stride = ((size_t)w + 3) & ~(size_t)3;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                size_t i = p + y * stride + x;
                if (i < data.size()) bmp->pixels[(size_t)y * w + x] = table[data[i]];
            }
    } else if (format == 4) {
        size_t stride = ((size_t)w * 2 + 3) & ~(size_t)3;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                size_t i = y * stride + x * 2;
                if (i + 1 >= data.size()) continue;
                uint32_t v = data[i] << 8 | data[i + 1];
                uint32_t rr = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
                bmp->pixels[(size_t)y * w + x] =
                    pack(255, rr << 3 | rr >> 2, g << 3 | g >> 2, b << 3 | b >> 2);
            }
    } else if (format == 5) {
        for (size_t i = 0; i < (size_t)w * h && i * 4 + 3 < data.size(); i++) {
            const uint8_t* q = &data[i * 4];
            bmp->pixels[i] = pack(q[0], q[1], q[2], q[3]);
        }
    }
    return bmp;
}

// Reads width/height from the first SOF marker of a JPEG stream.
void jpegSize(const uint8_t* p, size_t n, int& w, int& h) {
    for (size_t i = 0; i + 8 < n; i++) {
        if (p[i] == 0xFF && (p[i + 1] == 0xC0 || p[i + 1] == 0xC1 || p[i + 1] == 0xC2)) {
            h = p[i + 5] << 8 | p[i + 6];
            w = p[i + 7] << 8 | p[i + 8];
            return;
        }
    }
}

}  // namespace

bool Movie::load(const std::string& path, std::string& error) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        error = "cannot open " + path;
        return false;
    }
    std::vector<uint8_t> file;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) file.insert(file.end(), buf, buf + n);
    std::fclose(f);
    return loadFromMemory(std::move(file), error);
}

bool Movie::loadFromMemory(std::vector<uint8_t> file, std::string& error) {
    if (file.size() < 8) {
        error = "file too small";
        return false;
    }
    version = file[3];
    uint32_t total = file[4] | file[5] << 8 | file[6] << 16 | (uint32_t)file[7] << 24;
    std::vector<uint8_t> body;
    if (file[0] == 'C' && file[1] == 'W' && file[2] == 'S') {
        if (!zlibDecompress(file.data() + 8, file.size() - 8, body, total - 8)) {
            error = "corrupt zlib stream";
            return false;
        }
    } else if (file[0] == 'F' && file[1] == 'W' && file[2] == 'S') {
        body.assign(file.begin() + 8, file.end());
    } else {
        error = "not a SWF (or LZMA-compressed, which is unsupported)";
        return false;
    }

    Reader r(body.data(), body.size());
    stageRect = r.rect();
    frameRate = r.u16() / 256.0;
    root.frameCount = r.u16();
    root.id = 0;
    parseTags(body.data() + r.pos(), body.size() - r.pos(), root, true);

    for (auto& [name, id] : exports_)
        if (auto* c = character(id)) c->exportName = name;
    return true;
}

void Movie::parseTags(const uint8_t* data, size_t size, Sprite& tl, bool isRoot) {
    tl.frames.assign(tl.frameCount ? tl.frameCount : 1, {});
    size_t frame = 0;
    Reader r(data, size);
    auto push = [&](FrameCommand c) {
        if (frame < tl.frames.size()) tl.frames[frame].push_back(c);
    };

    while (!r.eof()) {
        uint16_t header = r.u16();
        int code = header >> 6;
        size_t len = header & 0x3F;
        if (len == 0x3F) len = r.u32();
        size_t start = r.pos(), end = start + len;
        if (end > size) break;
        Reader t(data + start, len);

        switch (code) {
            case TagEnd:
                return;
            case TagShowFrame:
                frame++;
                break;
            case TagDefineShape:
            case TagDefineShape2:
            case TagDefineShape3:
            case TagDefineShape4: {
                int v = code == TagDefineShape ? 1 : code == TagDefineShape2 ? 2
                        : code == TagDefineShape3 ? 3 : 4;
                auto s = std::make_unique<ShapeCharacter>();
                s->id = t.u16();
                s->def.bounds = t.rect();
                if (v == 4) {
                    t.rect();  // edge bounds
                    t.u8();
                }
                parseShape(t, v, s->def);
                characters_[s->id] = std::move(s);
                break;
            }
            case TagDefineBitsLossless:
            case TagDefineBitsLossless2: {
                auto b = parseLossless(t, len, code == TagDefineBitsLossless2);
                characters_[b->id] = std::move(b);
                break;
            }
            case TagJPEGTables:
                jpegTables_.assign(data + start, data + end);
                break;
            case TagDefineBits:
            case TagDefineBitsJPEG2:
            case TagDefineBitsJPEG3: {
                auto b = std::make_unique<Bitmap>();
                b->id = t.u16();
                size_t jpegLen = len - 2;
                if (code == TagDefineBitsJPEG3) jpegLen = t.u32();
                const uint8_t* jpeg = t.ptr();
                std::vector<std::pair<const uint8_t*, size_t>> streams;
                if (code == TagDefineBits && !jpegTables_.empty())
                    streams.push_back({jpegTables_.data(), jpegTables_.size()});
                streams.push_back({jpeg, std::min(jpegLen, len - (size_t)(jpeg - (data + start)))});
                if (!decodeJpeg(streams, b->width, b->height, b->pixels)) {
                    jpegSize(jpeg, jpegLen, b->width, b->height);
                    b->placeholder = true;
                    b->pixels.assign((size_t)b->width * b->height, 0xFF808080);
                } else if (code == TagDefineBitsJPEG3) {
                    // Separate zlib-compressed 8-bit alpha plane; premultiply.
                    std::vector<uint8_t> alpha;
                    const uint8_t* ap = jpeg + jpegLen;
                    if (zlibDecompress(ap, (data + end) - ap, alpha) &&
                        alpha.size() >= b->pixels.size()) {
                        for (size_t i = 0; i < b->pixels.size(); i++) {
                            uint32_t a = alpha[i], px = b->pixels[i];
                            uint32_t r = (px >> 16 & 255) * a / 255, g = (px >> 8 & 255) * a / 255,
                                     bb = (px & 255) * a / 255;
                            b->pixels[i] = a << 24 | r << 16 | g << 8 | bb;
                        }
                    }
                }
                characters_[b->id] = std::move(b);
                break;
            }
            case TagDefineSprite: {
                auto s = std::make_unique<Sprite>();
                s->id = t.u16();
                s->frameCount = t.u16();
                Sprite* sp = s.get();
                characters_[s->id] = std::move(s);
                parseTags(data + start + 4, len - 4, *sp, false);
                break;
            }
            case TagPlaceObject2:
            case TagPlaceObject3:
                tl.places.push_back(parsePlace(t, code, version));
                push({FrameCommand::Place, (uint32_t)tl.places.size() - 1});
                break;
            case TagPlaceObject: {
                PlaceCommand p;
                p.characterId = t.u16();
                p.depth = t.u16();
                p.hasCharacter = p.hasMatrix = true;
                p.matrix = t.matrix();
                if (t.remaining()) {
                    p.hasCxform = true;
                    p.cxform = t.cxform(false);
                }
                tl.places.push_back(p);
                push({FrameCommand::Place, (uint32_t)tl.places.size() - 1});
                break;
            }
            case TagRemoveObject:
                t.u16();
                push({FrameCommand::Remove, t.u16()});
                break;
            case TagRemoveObject2:
                push({FrameCommand::Remove, t.u16()});
                break;
            case TagDoAction:
                tl.actions.emplace_back(data + start, data + end);
                push({FrameCommand::Action, (uint32_t)tl.actions.size() - 1});
                break;
            case TagFrameLabel:
                tl.labels[t.str()] = (int)frame + 1;
                break;
            case TagStartSound: {
                SoundStart s;
                s.soundId = t.u16();
                uint8_t fl = t.u8();
                s.stop = fl & 0x20;
                s.noMultiple = fl & 0x10;
                s.hasInPoint = fl & 0x01;
                s.hasOutPoint = fl & 0x02;
                if (s.hasInPoint) s.inPoint = t.u32();
                if (s.hasOutPoint) s.outPoint = t.u32();
                if (fl & 0x04) s.loops = t.u16();
                if (fl & 0x08) {
                    int n = t.u8();
                    for (int i = 0; i < n; i++) {
                        SoundStart::EnvelopePoint e;
                        e.pos44 = t.u32();
                        e.left = t.u16();
                        e.right = t.u16();
                        s.envelope.push_back(e);
                    }
                }
                tl.sounds.push_back(s);
                push({FrameCommand::StartSound, (uint32_t)tl.sounds.size() - 1});
                break;
            }
            case TagSoundStreamHead:
            case TagSoundStreamHead2: {
                t.u8();  // playback format (advisory)
                uint8_t f = t.u8();
                tl.streamFormat.codec = f >> 4;
                tl.streamFormat.rate = kSoundRates[(f >> 2) & 3];
                tl.streamFormat.is16 = f & 2;
                tl.streamFormat.stereo = f & 1;
                t.u16();  // average samples per block
                if (tl.streamFormat.codec == SoundFormat::Mp3 && t.remaining() >= 2) tl.streamLatencySeek = t.s16();
                break;
            }
            case TagSoundStreamBlock: {
                // MP3 blocks start with SampleCount (u16) and SeekSamples (s16).
                size_t skip = tl.streamFormat.codec == SoundFormat::Mp3 ? 4 : 0;
                if (len <= skip) break;
                tl.hasStream = true;
                Sprite::StreamBlock b{(uint32_t)tl.streamData.size(), (uint32_t)(len - skip), (int)frame + 1};
                tl.streamData.insert(tl.streamData.end(), data + start + skip, data + end);
                tl.streamBlocks.push_back(b);
                push({FrameCommand::StreamBlock, (uint32_t)tl.streamBlocks.size() - 1});
                break;
            }
            case TagExportAssets: {
                int count = t.u16();
                for (int i = 0; i < count; i++) {
                    uint16_t id = t.u16();
                    exports_[t.str()] = id;
                }
                break;
            }
            case TagSetBackgroundColor:
                background = t.rgb();
                break;
            case TagDefineMorphShape:
            case TagDefineMorphShape2: {
                auto m = std::make_unique<MorphShapeCharacter>();
                m->id = t.u16();
                m->parse(t, code == TagDefineMorphShape2 ? 2 : 1);
                characters_[m->id] = std::move(m);
                break;
            }
            case TagDefineFont2:
            case TagDefineFont3: {
                auto f = std::make_unique<FontCharacter>();
                f->id = t.u16();
                f->parse(t, code == TagDefineFont3 ? 3 : 2);
                characters_[f->id] = std::move(f);
                break;
            }
            case TagDefineText:
            case TagDefineText2: {
                auto tx = std::make_unique<StaticTextCharacter>();
                tx->id = t.u16();
                tx->parse(t, code == TagDefineText2 ? 2 : 1);
                characters_[tx->id] = std::move(tx);
                break;
            }
            case TagDefineEditText: {
                auto et = std::make_unique<EditTextCharacter>();
                et->id = t.u16();
                et->parse(t);
                characters_[et->id] = std::move(et);
                break;
            }
            case TagDefineButton2: {
                auto b = std::make_unique<ButtonCharacter>();
                b->id = t.u16();
                b->trackAsMenu = t.u8() & 1;
                size_t actionOffset = t.u16();
                size_t actionPos = actionOffset ? t.pos() - 2 + actionOffset : len;
                for (;;) {
                    uint8_t fl = t.u8();
                    if (!fl || t.eof()) break;
                    ButtonCharacter::Record rec;
                    rec.states = fl & 0x0F;
                    rec.characterId = t.u16();
                    rec.depth = t.u16();
                    rec.matrix = t.matrix();
                    rec.cxform = t.cxform(true);
                    if (fl & 0x10) rec.filters = parseFilters(t);
                    if (fl & 0x20) rec.blendMode = t.u8();
                    b->records.push_back(std::move(rec));
                }
                if (actionPos < len) b->actions.assign(data + start + actionPos, data + end);
                characters_[b->id] = std::move(b);
                break;
            }
            case TagDefineSound: {
                auto snd = std::make_unique<SoundCharacter>();
                snd->id = t.u16();
                uint8_t f = t.u8();
                snd->format.codec = f >> 4;
                snd->format.rate = kSoundRates[(f >> 2) & 3];
                snd->format.is16 = f & 2;
                snd->format.stereo = f & 1;
                snd->sampleCount = t.u32();
                if (snd->format.codec == SoundFormat::Mp3) snd->seekSamples = t.s16();
                snd->data.assign(t.ptr(), data + end);
                characters_[snd->id] = std::move(snd);
                break;
            }
            default:
                break;
        }
        r.seek(end);
    }
    (void)isRoot;
}

}  // namespace flash
