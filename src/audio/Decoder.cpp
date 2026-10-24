#include "audio/Decoder.h"

#include <algorithm>
#include <cstring>

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

namespace audio {
namespace {

// Applies the per-sound sample limit common to all decoders.
class LimitedDecoder : public Decoder {
protected:
    explicit LimitedDecoder(const Source& s) : src_(s) {
        channels_ = s.stereo ? 2 : 1;
        rate_ = s.rate;
    }
    size_t remainingLimit(size_t want) const {
        if (!src_.sampleCount) return want;
        return produced_ >= src_.sampleCount ? 0 : std::min<size_t>(want, src_.sampleCount - produced_);
    }
    Source src_;
    size_t produced_ = 0;
};

// ---------------------------------------------------------------- MP3

class Mp3Decoder : public LimitedDecoder {
public:
    explicit Mp3Decoder(const Source& s) : LimitedDecoder(s) { rewind(); }

    void rewind() override {
        mp3dec_init(&dec_);
        pos_ = 0;
        pcmFrames_ = pcmPos_ = 0;
        produced_ = 0;
        skip_ = std::max(0, src_.skipSamples);
    }

    size_t read(int16_t* out, size_t frames) override {
        frames = remainingLimit(frames);
        size_t done = 0;
        while (done < frames) {
            if (pcmPos_ >= pcmFrames_ && !decodeFrame()) break;
            size_t n = std::min(frames - done, pcmFrames_ - pcmPos_);
            if (skip_) {
                size_t s = std::min<size_t>(n, (size_t)skip_);
                skip_ -= (int)s;
                pcmPos_ += s;
                continue;
            }
            std::memcpy(out + done * channels_, pcm_ + pcmPos_ * channels_, n * channels_ * sizeof(int16_t));
            pcmPos_ += n;
            done += n;
        }
        produced_ += done;
        return done;
    }

private:
    bool decodeFrame() {
        while (pos_ < src_.size) {
            mp3dec_frame_info_t info;
            int16_t raw[MINIMP3_MAX_SAMPLES_PER_FRAME];
            int samples = mp3dec_decode_frame(&dec_, src_.data + pos_, (int)(src_.size - pos_), raw, &info);
            if (info.frame_bytes == 0) return false;  // no more frames
            pos_ += info.frame_bytes;
            if (samples == 0) continue;  // skipped data (e.g. ID3) or decoder warm-up
            // Convert the frame's channel layout to the declared one.
            for (int i = 0; i < samples; i++) {
                if (channels_ == info.channels) {
                    for (int c = 0; c < channels_; c++) pcm_[i * channels_ + c] = raw[i * info.channels + c];
                } else if (channels_ == 2) {
                    pcm_[i * 2] = pcm_[i * 2 + 1] = raw[i];
                } else {
                    pcm_[i] = (int16_t)((raw[i * 2] + raw[i * 2 + 1]) / 2);
                }
            }
            pcmFrames_ = (size_t)samples;
            pcmPos_ = 0;
            return true;
        }
        return false;
    }

    mp3dec_t dec_;
    size_t pos_ = 0;
    int16_t pcm_[MINIMP3_MAX_SAMPLES_PER_FRAME];
    size_t pcmFrames_ = 0, pcmPos_ = 0;
    int skip_ = 0;
};

// ---------------------------------------------------------------- SWF ADPCM

const int kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
const int kIndex2[] = {-1, 2};
const int kIndex3[] = {-1, -1, 2, 4};
const int kIndex4[] = {-1, -1, -1, -1, 2, 4, 6, 8};
const int kIndex5[] = {-1, -1, -1, -1, -1, -1, -1, -1, 1, 2, 4, 6, 8, 10, 13, 16};
const int* const kIndexTables[] = {kIndex2, kIndex3, kIndex4, kIndex5};

class AdpcmDecoder : public LimitedDecoder {
public:
    explicit AdpcmDecoder(const Source& s) : LimitedDecoder(s) { rewind(); }

    void rewind() override {
        bitPos_ = 0;
        produced_ = 0;
        packetLeft_ = 0;
        bits_ = (int)readBits(2) + 2;
    }

    size_t read(int16_t* out, size_t frames) override {
        frames = remainingLimit(frames);
        size_t done = 0;
        const int* indexTable = kIndexTables[bits_ - 2];
        int signMask = 1 << (bits_ - 1);
        for (; done < frames; done++) {
            if (packetLeft_ == 0) {
                // Packet header: initial sample and step index per channel, 4096 samples.
                if (bitPos_ + (size_t)channels_ * 22 > src_.size * 8) break;
                for (int c = 0; c < channels_; c++) {
                    int32_t v = (int32_t)readBits(16);
                    sample_[c] = (int16_t)v;
                    index_[c] = (int)readBits(6);
                    out[done * channels_ + c] = sample_[c];
                }
                packetLeft_ = 4095;
                continue;
            }
            if (bitPos_ + (size_t)channels_ * bits_ > src_.size * 8) break;
            for (int c = 0; c < channels_; c++) {
                int code = (int)readBits(bits_);
                int step = kStepTable[index_[c]];
                int diff = step >> (bits_ - 1);
                for (int b = bits_ - 2, s = step; b >= 0; b--, s >>= 1)
                    if (code & (1 << b)) diff += s;
                int v = sample_[c] + ((code & signMask) ? -diff : diff);
                sample_[c] = (int16_t)std::clamp(v, -32768, 32767);
                index_[c] = std::clamp(index_[c] + indexTable[code & (signMask - 1)], 0, 88);
                out[done * channels_ + c] = sample_[c];
            }
            packetLeft_--;
        }
        produced_ += done;
        return done;
    }

private:
    uint32_t readBits(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; i++, bitPos_++) {
            size_t byte = bitPos_ >> 3;
            int bit = byte < src_.size ? (src_.data[byte] >> (7 - (bitPos_ & 7))) & 1 : 0;
            v = (v << 1) | bit;
        }
        // Sign-extend the 16-bit initial samples.
        if (n == 16 && (v & 0x8000)) v |= 0xFFFF0000u;
        return v;
    }

    size_t bitPos_ = 0;
    int bits_ = 4;
    int packetLeft_ = 0;
    int16_t sample_[2] = {};
    int index_[2] = {};
};

// ---------------------------------------------------------------- PCM

class PcmDecoder : public LimitedDecoder {
public:
    explicit PcmDecoder(const Source& s) : LimitedDecoder(s) {}
    void rewind() override {
        pos_ = 0;
        produced_ = 0;
    }
    size_t read(int16_t* out, size_t frames) override {
        frames = remainingLimit(frames);
        size_t bytesPerFrame = (src_.is16 ? 2 : 1) * channels_;
        size_t avail = (src_.size - pos_) / bytesPerFrame;
        size_t n = std::min(frames, avail);
        for (size_t i = 0; i < n * channels_; i++) {
            if (src_.is16) {
                out[i] = (int16_t)(src_.data[pos_] | src_.data[pos_ + 1] << 8);
                pos_ += 2;
            } else {
                out[i] = (int16_t)((src_.data[pos_++] - 128) << 8);
            }
        }
        produced_ += n;
        return n;
    }

private:
    size_t pos_ = 0;
};

}  // namespace

std::unique_ptr<Decoder> makeDecoder(const Source& src) {
    if (!src.data || !src.size) return nullptr;
    switch (src.codec) {
        case 2: return std::make_unique<Mp3Decoder>(src);
        case 1: return std::make_unique<AdpcmDecoder>(src);
        case 0:
        case 3: return std::make_unique<PcmDecoder>(src);
        default: return nullptr;
    }
}

}  // namespace audio
