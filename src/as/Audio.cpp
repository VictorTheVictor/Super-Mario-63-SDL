#include "as/Audio.h"

#include <algorithm>
#include <cmath>

namespace as {

namespace {

audio::Source sourceOf(const flash::SoundCharacter& s) {
    audio::Source src;
    src.codec = s.format.codec;
    src.rate = s.format.rate;
    src.is16 = s.format.is16;
    src.stereo = s.format.stereo;
    src.data = s.data.data();
    src.size = s.data.size();
    src.sampleCount = s.sampleCount;
    src.skipSamples = s.seekSamples;
    return src;
}

const flash::SoundCharacter* soundById(const flash::Movie& m, uint16_t id) {
    auto* c = m.character(id);
    return c && c->kind == flash::Character::Sound ? static_cast<const flash::SoundCharacter*>(c) : nullptr;
}

}  // namespace

AudioBridge::AudioBridge(const flash::Movie& movie, audio::Mixer& mixer) : movie_(movie), mixer_(mixer) {
    setSoundHost(this);
}

AudioBridge::~AudioBridge() {
    setSoundHost(nullptr);
    mixer_.stopAll();
}

// ---------------------------------------------------------------- volume groups

int AudioBridge::groupForTarget(const flash::DisplayObject* target) {
    // No target, or the root: the global group.
    if (!target || target == stage().root.get()) return 0;
    for (auto& g : groups_)
        if (g.target.lock().get() == target) return g.id;
    groups_.push_back({const_cast<flash::DisplayObject*>(target)->shared_from_this(), nextGroup_++});
    return groups_.back().id;
}

int AudioBridge::groupForSound(Object* sound) {
    Value target = getMember(Value(sound), internLiteral("__target"));
    flash::DisplayObject* d = displayOf(target);
    if (!d && target.isString()) d = resolvePath(stage().root.get(), target.string());
    return groupForTarget(d);
}

int AudioBridge::groupForClip(const flash::DisplayObject* d) {
    // Timeline sounds use the nearest ancestor that has a Sound object volume.
    for (; d; d = d->parent())
        for (auto& g : groups_)
            if (g.target.lock().get() == d) return g.id;
    return 0;
}

void AudioBridge::holdWhilePaused(audio::Mixer::VoiceId v, const flash::DisplayObject* origin) {
    if (v && mixer_.paused() && !(playsWhilePaused && playsWhilePaused(origin))) mixer_.hold(v);
}

// ---------------------------------------------------------------- Sound objects

void AudioBridge::start(Object* sound, const std::string& linkage, double offsetSec, int loops) {
    auto* c = movie_.exported(linkage);
    if (!c || c->kind != flash::Character::Sound) return;
    auto* s = static_cast<const flash::SoundCharacter*>(c);
    audio::Mixer::Params p;
    p.loops = loops;
    p.offsetSeconds = std::max(0.0, offsetSec);
    p.group = groupForSound(sound);
    p.tag = s->id;
    volumeChanged(sound);
    audio::Mixer::VoiceId v = mixer_.play(audio::makeDecoder(sourceOf(*s)), p);
    Value target = getMember(Value(sound), internLiteral("__target"));
    holdWhilePaused(v, displayOf(target));
    if (v) owned_.push_back({v, sound, s->id});
    sound->setOwn(internLiteral("duration"), s->durationMs());
}

void AudioBridge::stop(Object* sound, const std::string& linkage) {
    int group = groupForSound(sound);
    if (linkage.empty()) {
        mixer_.stopGroup(group);
        if (group == 0) mixer_.stopAll();
    } else if (auto* c = movie_.exported(linkage)) {
        mixer_.stopTag(c->id, group == 0 ? -1 : group);
    }
    owned_.erase(std::remove_if(owned_.begin(), owned_.end(),
                                [&](const Owned& o) { return !mixer_.isPlaying(o.voice); }),
                 owned_.end());
}

void AudioBridge::stopAll() {
    mixer_.stopAll();
    owned_.clear();
    streams_.clear();
}

void AudioBridge::volumeChanged(Object* sound) {
    Value s(sound);
    double vol = getMember(s, internLiteral("__volume")).toNumber();
    double pan = getMember(s, internLiteral("__pan")).toNumber();
    if (std::isnan(vol)) vol = 100;
    if (std::isnan(pan)) pan = 0;
    auto t = audio::Mixer::Transform::fromPan((float)std::max(0.0, vol) / 100.f, (float)pan / 100.f);
    Value xf = getMember(s, internLiteral("__transform"));
    if (xf.isObject()) {
        auto pct = [&](const char* k, float def) {
            Value v = getMember(xf, intern(k));
            return v.isUndefined() ? def : (float)(v.toNumber() / 100);
        };
        t.ll = pct("ll", 1);
        t.lr = pct("lr", 0);
        t.rl = pct("rl", 0);
        t.rr = pct("rr", 1);
    }
    mixer_.setGroup(groupForSound(sound), t);
}

// ---------------------------------------------------------------- timeline sounds

void AudioBridge::startSound(flash::MovieClip& clip, const flash::SoundStart& s) {
    const flash::SoundCharacter* snd = soundById(movie_, s.soundId);
    if (!snd) return;
    if (s.stop) {
        mixer_.stopTag(s.soundId);
        return;
    }
    if (s.noMultiple && mixer_.isTagPlaying(s.soundId)) return;
    audio::Mixer::Params p;
    p.loops = s.loops;
    p.inPoint44 = s.hasInPoint ? s.inPoint : 0;
    p.outPoint44 = s.hasOutPoint ? s.outPoint : 0;
    for (auto& e : s.envelope) p.envelope.push_back({e.pos44, e.left / 32768.f, e.right / 32768.f});
    p.group = groupForClip(&clip);
    p.tag = s.soundId;
    holdWhilePaused(mixer_.play(audio::makeDecoder(sourceOf(*snd)), p), &clip);
}

void AudioBridge::streamBlock(flash::MovieClip& clip, uint32_t block) {
    const flash::Sprite* sp = clip.sprite();
    if (block >= sp->streamBlocks.size()) return;
    for (auto& st : streams_) {
        if (st.clip.lock().get() != &clip) continue;
        if (block == st.lastBlock + 1 && mixer_.isPlaying(st.voice)) {
            st.lastBlock = block;
            st.lastFrame = frame_;
            return;
        }
        mixer_.stop(st.voice);  // jumped: restart at the new position
        st.clip.reset();
    }
    streams_.erase(std::remove_if(streams_.begin(), streams_.end(), [](const Stream& s) { return s.clip.expired(); }),
                   streams_.end());
    audio::Source src;
    src.codec = sp->streamFormat.codec;
    src.rate = sp->streamFormat.rate;
    src.is16 = sp->streamFormat.is16;
    src.stereo = sp->streamFormat.stereo;
    src.data = sp->streamData.data() + sp->streamBlocks[block].offset;
    src.size = sp->streamData.size() - sp->streamBlocks[block].offset;
    src.skipSamples = block == 0 ? sp->streamLatencySeek : 0;
    audio::Mixer::Params p;
    p.group = groupForClip(&clip);
    audio::Mixer::VoiceId v = mixer_.play(audio::makeDecoder(src), p);
    holdWhilePaused(v, &clip);
    if (v) streams_.push_back({std::static_pointer_cast<flash::MovieClip>(clip.shared_from_this()), v, block, frame_});
}

// ---------------------------------------------------------------- per frame

void AudioBridge::update(uint64_t frame) {
    // Stream sounds stop when their clip stops, leaves the stage or runs out of blocks.
    for (auto& st : streams_) {
        auto clip = st.clip.lock();
        if (!clip || isRemoved(clip.get()) || st.lastFrame + 1 < frame) {
            mixer_.stop(st.voice);
            st.clip.reset();
        }
    }
    streams_.erase(std::remove_if(streams_.begin(), streams_.end(), [](const Stream& s) { return s.clip.expired(); }),
                   streams_.end());
    frame_ = frame + 1;

    std::vector<audio::Mixer::VoiceId> done = mixer_.takeFinished();
    std::vector<Object*> completed;
    for (auto it = owned_.begin(); it != owned_.end();) {
        if (std::find(done.begin(), done.end(), it->voice) != done.end()) {
            completed.push_back(it->sound);
            it = owned_.erase(it);
            continue;
        }
        it->sound->setOwn(internLiteral("position"), mixer_.positionMs(it->voice));
        ++it;
    }
    for (Object* so : completed) {
        Value s(so);
        if (auto* c = movie_.exported(getMember(s, internLiteral("__linkage")).toString()))
            if (c->kind == flash::Character::Sound)
                so->setOwn(internLiteral("position"), static_cast<const flash::SoundCharacter*>(c)->durationMs());
        Value fn = getMember(s, internLiteral("onSoundComplete"));
        callFunction(fn, s, {});
    }
}

void AudioBridge::trace(Gc& gc) {
    for (auto& o : owned_) gc.mark(o.sound);
}

}  // namespace as
