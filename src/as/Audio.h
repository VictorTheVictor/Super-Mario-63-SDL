#pragma once
// Connects AS2 Sound objects and timeline sounds to the audio mixer.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "as/Stage.h"
#include "audio/Mixer.h"
#include "flash/DisplayObject.h"

namespace as {

class AudioBridge : public SoundHost {
public:
    AudioBridge(const flash::Movie& movie, audio::Mixer& mixer);
    ~AudioBridge() override;

    // SoundHost (Sound objects)
    void start(Object* sound, const std::string& linkage, double offsetSec, int loops) override;
    void stop(Object* sound, const std::string& linkage) override;
    void stopAll() override;
    void volumeChanged(Object* sound) override;

    // Timeline sounds
    void startSound(flash::MovieClip& clip, const flash::SoundStart& s);
    void streamBlock(flash::MovieClip& clip, uint32_t blockIndex);

    // Once per frame after scripts: stream sync, onSoundComplete, Sound.position.
    void update(uint64_t frame);
    // While the mixer is paused, sounds started by anything this doesn't accept (the
    // starting Sound object's target clip, or the clip of a timeline sound) are held too.
    std::function<bool(const flash::DisplayObject*)> playsWhilePaused;
    void trace(Gc& gc);

private:
    int groupForSound(Object* sound);
    void holdWhilePaused(audio::Mixer::VoiceId v, const flash::DisplayObject* origin);
    int groupForClip(const flash::DisplayObject* d);
    int groupForTarget(const flash::DisplayObject* target);

    const flash::Movie& movie_;
    audio::Mixer& mixer_;
    struct Group {
        std::weak_ptr<flash::DisplayObject> target;
        int id;
    };
    std::vector<Group> groups_;
    int nextGroup_ = 1;
    struct Owned {
        audio::Mixer::VoiceId voice;
        Object* sound;
        uint16_t soundId;
    };
    std::vector<Owned> owned_;
    struct Stream {
        std::weak_ptr<flash::MovieClip> clip;
        audio::Mixer::VoiceId voice;
        uint32_t lastBlock;
        uint64_t lastFrame;
    };
    std::vector<Stream> streams_;
    uint64_t frame_ = 0;
};

}  // namespace as
