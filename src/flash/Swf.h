#pragma once
// SWF file loader: characters (shapes, bitmaps, sprites...) and timelines.
// Scripts (DoAction / clip actions) are recorded by location only; the game logic
// itself is provided by transpiled C++ code.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "flash/Geom.h"
#include "flash/Shape.h"

namespace flash {

struct Filter {
    enum Type : uint8_t {
        DropShadow = 0, Blur = 1, Glow = 2, Bevel = 3, GradientGlow = 4, Convolution = 5,
        ColorMatrix = 6, GradientBevel = 7,
    };
    uint8_t type = Blur;
    Color color, highlight;          // shadow/glow color, bevel highlight
    double blurX = 0, blurY = 0, angle = 0, distance = 0, strength = 1;
    bool inner = false, knockout = false, onTop = false, compositeSource = true;
    int passes = 1;
    float matrix[20] = {};           // ColorMatrix
    std::vector<GradientStop> gradient;
};

struct PlaceCommand {
    uint16_t depth = 0;
    bool move = false;
    bool hasCharacter = false, hasMatrix = false, hasCxform = false, hasRatio = false,
         hasName = false, hasClipDepth = false, hasFilters = false, hasBlendMode = false,
         hasVisible = false, hasClipActions = false;
    uint16_t characterId = 0;
    Matrix matrix;
    ColorTransform cxform;
    uint16_t ratio = 0;
    std::string name;
    uint16_t clipDepth = 0;
    std::vector<Filter> filters;
    uint8_t blendMode = 0;
    bool visible = true;
    uint32_t clipEventFlags = 0;  // union of onClipEvent flags (for script mapping)
};

struct FrameCommand {
    enum Kind : uint8_t { Place, Remove, Action, StartSound, StreamBlock };
    Kind kind;
    // into Sprite::places / ::actions / ::sounds / ::streamBlocks, or depth for Remove
    uint32_t index;
};

// SOUNDINFO of a StartSound tag. Positions are in 44.1 kHz samples.
struct SoundStart {
    uint16_t soundId = 0;
    bool stop = false, noMultiple = false;
    bool hasInPoint = false, hasOutPoint = false;
    uint32_t inPoint = 0, outPoint = 0;
    uint16_t loops = 1;
    struct EnvelopePoint {
        uint32_t pos44;
        uint16_t left, right;  // 0..32768
    };
    std::vector<EnvelopePoint> envelope;
};


struct Character {
    enum Kind : uint8_t { Shape, Sprite, Bitmap, MorphShape, Text, EditText, Font, Sound, Button };
    explicit Character(Kind k) : kind(k) {}
    virtual ~Character() = default;
    Kind kind;
    uint16_t id = 0;
    std::string exportName;
};

struct ShapeCharacter : Character {
    ShapeCharacter() : Character(Shape) {}
    ShapeDef def;
};

struct Bitmap : Character {
    Bitmap() : Character(Character::Bitmap) {}
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;  // premultiplied, 0xAARRGGBB
    bool placeholder = false;      // undecoded format (JPEG for now)
};

// Sound sample format shared by DefineSound and stream heads.
struct SoundFormat {
    enum Codec : uint8_t { RawNative = 0, Adpcm = 1, Mp3 = 2, RawLE = 3, Nellymoser16 = 4,
                           Nellymoser8 = 5, Nellymoser = 6, Speex = 11 };
    uint8_t codec = Mp3;
    int rate = 44100;
    bool is16 = true, stereo = false;
};

struct SoundCharacter : Character {
    SoundCharacter() : Character(Sound) {}
    SoundFormat format;
    uint32_t sampleCount = 0;   // per channel
    int seekSamples = 0;        // MP3: samples to skip at the start
    std::vector<uint8_t> data;  // MP3 frames / ADPCM / PCM
    double durationMs() const { return format.rate ? sampleCount * 1000.0 / format.rate : 0; }
};

struct Sprite : Character {
    Sprite() : Character(Character::Sprite) {}
    uint16_t frameCount = 1;
    // frames[i] = commands executed when entering frame i+1
    std::vector<std::vector<FrameCommand>> frames;
    std::vector<PlaceCommand> places;
    std::vector<std::vector<uint8_t>> actions;  // raw AVM1 bytecode per DoAction
    std::vector<SoundStart> sounds;
    std::map<std::string, int> labels;          // label -> 1-based frame

    // Timeline-synchronised stream sound (SoundStreamHead + SoundStreamBlock).
    bool hasStream = false;
    SoundFormat streamFormat;
    int streamLatencySeek = 0;
    struct StreamBlock {
        uint32_t offset, size;  // into streamData
        int frame;              // 1-based frame the block belongs to
    };
    std::vector<StreamBlock> streamBlocks;
    std::vector<uint8_t> streamData;  // concatenated block payloads
};

struct ButtonCharacter : Character {
    ButtonCharacter() : Character(Button) {}
    enum State : uint8_t { Up = 1, Over = 2, Down = 4, HitTest = 8 };
    struct Record {
        uint8_t states = 0;
        uint16_t characterId = 0;
        uint16_t depth = 0;
        Matrix matrix;
        ColorTransform cxform;
        std::vector<Filter> filters;
        uint8_t blendMode = 0;
    };
    bool trackAsMenu = false;
    std::vector<Record> records;
    std::vector<uint8_t> actions;  // BUTTONCONDACTIONs, raw (scripts milestone)
};

// Not yet decoded; kept so placements resolve to something.
struct OpaqueCharacter : Character {
    explicit OpaqueCharacter(Kind k) : Character(k) {}
    Rect bounds;
};

class Movie {
public:
    bool load(const std::string& path, std::string& error);
    // Parses a whole SWF file already in memory (e.g. read from an Android APK asset).
    bool loadFromMemory(std::vector<uint8_t> file, std::string& error);

    int version = 0;
    Rect stageRect;  // twips
    double frameRate = 12;
    Color background{255, 255, 255, 255};
    Sprite root;     // main timeline (id 0)

    Character* character(uint16_t id) const {
        auto it = characters_.find(id);
        return it == characters_.end() ? nullptr : it->second.get();
    }
    Character* exported(const std::string& name) const {
        auto it = exports_.find(name);
        return it == exports_.end() ? nullptr : character(it->second);
    }
    const std::unordered_map<std::string, uint16_t>& exports() const { return exports_; }
    const std::unordered_map<uint16_t, std::unique_ptr<Character>>& characters() const {
        return characters_;
    }

private:
    void parseTags(const uint8_t* data, size_t size, Sprite& timeline, bool isRoot);

    std::unordered_map<uint16_t, std::unique_ptr<Character>> characters_;
    std::unordered_map<std::string, uint16_t> exports_;
    std::vector<uint8_t> jpegTables_;
};

}  // namespace flash
