#pragma once
// Streaming decoders for SWF sound data: MP3 (minimp3), SWF ADPCM and raw PCM.
// Decoders read from memory they do not own; the SWF data outlives them.

#include <cstddef>
#include <cstdint>
#include <memory>

namespace audio {

struct Source {
    uint8_t codec = 2;  // flash::SoundFormat::Codec
    int rate = 44100;
    bool is16 = true, stereo = false;
    const uint8_t* data = nullptr;
    size_t size = 0;
    uint32_t sampleCount = 0;  // per channel; 0 = until the data ends
    int skipSamples = 0;       // MP3 decoder delay to drop at the start
};

class Decoder {
public:
    virtual ~Decoder() = default;
    int channels() const { return channels_; }
    int rate() const { return rate_; }
    // Reads up to `frames` interleaved frames; returns the number read (0 at the end).
    virtual size_t read(int16_t* out, size_t frames) = 0;
    virtual void rewind() = 0;

protected:
    int channels_ = 1;
    int rate_ = 44100;
};

// Returns null for unsupported codecs (Nellymoser, Speex).
std::unique_ptr<Decoder> makeDecoder(const Source& src);

}  // namespace audio
