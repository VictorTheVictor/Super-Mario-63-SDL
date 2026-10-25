#pragma once
// Software mixer: resamples voices to the output rate and mixes them into
// interleaved stereo int16. Platform-independent; the host calls mix() from its
// audio callback. All public methods are thread-safe.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "audio/Decoder.h"

namespace audio {

class Mixer {
public:
    using VoiceId = uint32_t;

    // Flash sound transform: out.left = ll*in.left + rl*in.right, out.right = lr*in.left + rr*in.right.
    struct Transform {
        float ll = 1, lr = 0, rl = 0, rr = 1;
        float volume = 1;
        static Transform fromPan(float volume, float pan);  // pan -1..1
    };

    struct EnvelopePoint {
        uint32_t pos44;  // position in 44.1 kHz samples since the sound started
        float left, right;  // 0..1
    };

    struct Params {
        int loops = 1;
        uint32_t inPoint44 = 0, outPoint44 = 0;  // 0 = whole sound
        std::vector<EnvelopePoint> envelope;
        double offsetSeconds = 0;                // Sound.start(offset)
        int group = 0;                           // volume group (0 = global)
        uint32_t tag = 0;                        // caller-defined (e.g. sound id)
    };

    explicit Mixer(int outputRate = 44100) : outRate_(outputRate) {}

    VoiceId play(std::unique_ptr<Decoder> decoder, const Params& p);
    void stop(VoiceId id);
    void stopTag(uint32_t tag, int group = -1);  // group -1 = any
    void stopGroup(int group);
    void stopAll();
    // Pausing holds every voice playing at that moment (silent, keeping its position) until
    // unpaused; voices started while paused (a pause menu's sounds) play normally.
    void setPaused(bool paused);
    bool paused() const { return paused_; }
    // While paused: holds a voice started meanwhile too (released by unpausing).
    void hold(VoiceId id);
    bool isPlaying(VoiceId id) const;
    bool isTagPlaying(uint32_t tag) const;
    double positionMs(VoiceId id) const;
    // Group 0 is global and applies to every voice; other groups multiply with it.
    void setGroup(int group, const Transform& t);
    // Voices that ended on their own since the last call (not ones stopped explicitly).
    std::vector<VoiceId> takeFinished();
    size_t voiceCount() const;

    // Mixes `frames` stereo frames into `out` (overwrites).
    void mix(int16_t* out, size_t frames);
    int outputRate() const { return outRate_; }

private:
    struct Voice {
        std::unique_ptr<Decoder> dec;
        Params params;
        int channels = 1;
        double step = 1;            // source frames per output frame
        std::vector<float> buf;     // decoded stereo frames (L,R)
        double pos = 0;             // fractional read position in buf (frames)
        uint64_t sourceFramesThisLoop = 0;  // frames pulled from the decoder this loop
        uint64_t consumed = 0;      // source frames consumed overall (for position)
        uint64_t outFrames = 0;     // output frames produced (for envelopes)
        int loopsLeft = 1;
        bool ended = false;         // decoder exhausted, no loops left
        bool held = false;          // paused: skipped by mix()
        uint32_t inSrc = 0, outSrc = 0;
    };
    bool fill(Voice& v, size_t needFrames);
    void startLoop(Voice& v, bool first);
    Transform effective(int group) const;

    int outRate_;
    mutable std::mutex mu_;
    std::atomic<bool> paused_{false};
    std::unordered_map<VoiceId, Voice> voices_;
    std::unordered_map<int, Transform> groups_;
    std::vector<VoiceId> finished_;
    VoiceId next_ = 1;
    std::vector<float> scratch_;
};

}  // namespace audio
