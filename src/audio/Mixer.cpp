#include "audio/Mixer.h"

#include <algorithm>
#include <cmath>

namespace audio {

Mixer::Transform Mixer::Transform::fromPan(float volume, float pan) {
    Transform t;
    t.volume = volume;
    pan = std::clamp(pan, -1.f, 1.f);
    t.ll = pan > 0 ? 1 - pan : 1;
    t.rr = pan < 0 ? 1 + pan : 1;
    return t;
}

void Mixer::startLoop(Voice& v, bool first) {
    if (!first) v.dec->rewind();
    v.sourceFramesThisLoop = 0;
    // Skip the in point (and the start offset on the first loop) by decoding.
    uint64_t skip = v.inSrc;
    if (first) skip += (uint64_t)(v.params.offsetSeconds * v.dec->rate());
    int16_t tmp[2048];
    while (skip > 0) {
        size_t n = v.dec->read(tmp, (size_t)std::min<uint64_t>(skip, 1024));
        if (!n) break;
        skip -= n;
        v.sourceFramesThisLoop += n;
    }
}

Mixer::VoiceId Mixer::play(std::unique_ptr<Decoder> dec, const Params& p) {
    if (!dec) return 0;
    Voice v;
    v.channels = dec->channels();
    v.step = (double)dec->rate() / outRate_;
    v.params = p;
    v.loopsLeft = std::max(1, p.loops);
    v.inSrc = (uint32_t)((uint64_t)p.inPoint44 * dec->rate() / 44100);
    v.outSrc = (uint32_t)((uint64_t)p.outPoint44 * dec->rate() / 44100);
    v.dec = std::move(dec);
    startLoop(v, true);
    std::lock_guard<std::mutex> lock(mu_);
    VoiceId id = next_++;
    if (next_ == 0) next_ = 1;
    voices_.emplace(id, std::move(v));
    return id;
}

void Mixer::stop(VoiceId id) {
    std::lock_guard<std::mutex> lock(mu_);
    voices_.erase(id);
}

void Mixer::stopTag(uint32_t tag, int group) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto it = voices_.begin(); it != voices_.end();)
        if (it->second.params.tag == tag && (group < 0 || it->second.params.group == group)) it = voices_.erase(it);
        else ++it;
}

void Mixer::stopGroup(int group) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto it = voices_.begin(); it != voices_.end();)
        if (it->second.params.group == group) it = voices_.erase(it);
        else ++it;
}

void Mixer::stopAll() {
    std::lock_guard<std::mutex> lock(mu_);
    voices_.clear();
}

bool Mixer::isPlaying(VoiceId id) const {
    std::lock_guard<std::mutex> lock(mu_);
    return voices_.count(id) != 0;
}

bool Mixer::isTagPlaying(uint32_t tag) const {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& [id, v] : voices_)
        if (v.params.tag == tag) return true;
    return false;
}

double Mixer::positionMs(VoiceId id) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = voices_.find(id);
    if (it == voices_.end()) return 0;
    const Voice& v = it->second;
    return (v.consumed + v.inSrc) * 1000.0 / v.dec->rate();
}

void Mixer::setGroup(int group, const Transform& t) {
    std::lock_guard<std::mutex> lock(mu_);
    groups_[group] = t;
}

std::vector<Mixer::VoiceId> Mixer::takeFinished() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<VoiceId> out;
    out.swap(finished_);
    return out;
}

size_t Mixer::voiceCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    return voices_.size();
}

Mixer::Transform Mixer::effective(int group) const {
    Transform g;
    auto global = groups_.find(0);
    if (global != groups_.end()) g = global->second;
    if (group == 0) return g;
    auto it = groups_.find(group);
    if (it == groups_.end()) return g;
    const Transform& t = it->second;
    Transform r;
    // Apply the group's transform, then the global one.
    r.ll = g.ll * t.ll + g.rl * t.lr;
    r.lr = g.lr * t.ll + g.rr * t.lr;
    r.rl = g.ll * t.rl + g.rl * t.rr;
    r.rr = g.lr * t.rl + g.rr * t.rr;
    r.volume = g.volume * t.volume;
    return r;
}

// Ensures buf holds at least needFrames frames past the integer read position.
bool Mixer::fill(Voice& v, size_t needFrames) {
    size_t have = v.buf.size() / 2;
    size_t base = (size_t)v.pos;
    if (base > 8192) {  // drop consumed frames
        v.buf.erase(v.buf.begin(), v.buf.begin() + base * 2);
        v.pos -= base;
        have -= base;
        base = 0;
    }
    int16_t tmp[1024 * 2];
    while (have < base + needFrames && !v.ended) {
        size_t want = 1024;
        if (v.outSrc) {
            uint64_t left = v.outSrc > v.sourceFramesThisLoop ? v.outSrc - v.sourceFramesThisLoop : 0;
            want = (size_t)std::min<uint64_t>(want, left);
        }
        size_t n = want ? v.dec->read(tmp, want) : 0;
        if (n == 0) {
            if (--v.loopsLeft > 0) {
                startLoop(v, false);
                continue;
            }
            v.ended = true;
            break;
        }
        v.sourceFramesThisLoop += n;
        for (size_t i = 0; i < n; i++) {
            float l = tmp[i * v.channels] / 32768.f;
            float r = v.channels == 2 ? tmp[i * 2 + 1] / 32768.f : l;
            v.buf.push_back(l);
            v.buf.push_back(r);
        }
        have += n;
    }
    return have > base;
}

void Mixer::mix(int16_t* out, size_t frames) {
    if (paused_) {
        std::fill(out, out + frames * 2, int16_t(0));
        return;
    }
    std::lock_guard<std::mutex> lock(mu_);
    scratch_.assign(frames * 2, 0.f);
    for (auto it = voices_.begin(); it != voices_.end();) {
        Voice& v = it->second;
        Transform t = effective(v.params.group);
        bool alive = true;
        for (size_t i = 0; i < frames; i++) {
            if (!fill(v, 2)) {
                alive = false;
                break;
            }
            size_t idx = (size_t)v.pos;
            float frac = (float)(v.pos - idx);
            size_t have = v.buf.size() / 2;
            float l0 = v.buf[idx * 2], r0 = v.buf[idx * 2 + 1];
            float l1 = idx + 1 < have ? v.buf[idx * 2 + 2] : l0;
            float r1 = idx + 1 < have ? v.buf[idx * 2 + 3] : r0;
            float l = l0 + (l1 - l0) * frac, r = r0 + (r1 - r0) * frac;

            if (!v.params.envelope.empty()) {
                double pos44 = v.outFrames * 44100.0 / outRate_;
                const auto& env = v.params.envelope;
                float el = env.back().left, er = env.back().right;
                if (pos44 <= env.front().pos44) {
                    el = env.front().left;
                    er = env.front().right;
                } else {
                    for (size_t k = 1; k < env.size(); k++) {
                        if (pos44 <= env[k].pos44) {
                            double span = (double)env[k].pos44 - env[k - 1].pos44;
                            float a = span > 0 ? (float)((pos44 - env[k - 1].pos44) / span) : 1.f;
                            el = env[k - 1].left + (env[k].left - env[k - 1].left) * a;
                            er = env[k - 1].right + (env[k].right - env[k - 1].right) * a;
                            break;
                        }
                    }
                }
                l *= el;
                r *= er;
            }
            scratch_[i * 2] += (t.ll * l + t.rl * r) * t.volume;
            scratch_[i * 2 + 1] += (t.lr * l + t.rr * r) * t.volume;

            double before = v.pos;
            v.pos += v.step;
            v.consumed += (uint64_t)v.pos - (uint64_t)before;
            v.outFrames++;
        }
        if (!alive) {
            finished_.push_back(it->first);
            it = voices_.erase(it);
        } else {
            ++it;
        }
    }
    for (size_t i = 0; i < frames * 2; i++)
        out[i] = (int16_t)std::lround(std::clamp(scratch_[i], -1.f, 1.f) * 32767.f);
}

}  // namespace audio
