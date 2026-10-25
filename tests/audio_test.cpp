// Audio tests: decoders, mixer behaviour and the Sound API.
//
// Usage: audio_test game.swf [sine440.mp3]
//   sine440.mp3: optional reference MP3 used to check decoding against a known signal:
//     ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=22050:duration=1" -af volume=0.5 \
//            -ac 1 -c:a libmp3lame -b:a 64k sine440.mp3
//   (ffmpeg's sine source has amplitude 1/8, so the peak is about 32767/16 = 2048;
//   ffmpeg's own decoder reports 1946.)

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "as/Audio.h"
#include "as/Player.h"
#include "audio/Mixer.h"
#include "flash/Swf.h"

using namespace as;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        checks++;                                                       \
        if (!(cond)) {                                                  \
            failures++;                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)

// 16-bit little-endian mono sine, as raw SWF PCM.
static std::vector<uint8_t> sinePcm(int rate, double hz, double seconds, double amp) {
    std::vector<uint8_t> out;
    int n = (int)(rate * seconds);
    for (int i = 0; i < n; i++) {
        int16_t v = (int16_t)std::lround(std::sin(2 * M_PI * hz * i / rate) * amp * 32767);
        out.push_back((uint8_t)(v & 255));
        out.push_back((uint8_t)((v >> 8) & 255));
    }
    return out;
}

static audio::Source pcmSource(const std::vector<uint8_t>& d, int rate) {
    audio::Source s;
    s.codec = 3;
    s.rate = rate;
    s.is16 = true;
    s.stereo = false;
    s.data = d.data();
    s.size = d.size();
    return s;
}

static std::vector<int16_t> mixAll(audio::Mixer& m, size_t maxFrames) {
    std::vector<int16_t> out;
    int16_t buf[512 * 2];
    while (out.size() / 2 < maxFrames && m.voiceCount()) {
        m.mix(buf, 512);
        out.insert(out.end(), buf, buf + 1024);
    }
    return out;
}

static int peak(const std::vector<int16_t>& v, int channel) {
    int p = 0;
    for (size_t i = channel; i < v.size(); i += 2) p = std::max(p, std::abs((int)v[i]));
    return p;
}

// Counts upward zero crossings on one channel to estimate frequency.
static double frequency(const std::vector<int16_t>& v, int channel, int rate, size_t from, size_t to) {
    int crossings = 0;
    for (size_t i = from + 1; i < to && i * 2 + channel < v.size(); i++)
        if (v[(i - 1) * 2 + channel] < 0 && v[i * 2 + channel] >= 0) crossings++;
    return crossings * (double)rate / (to - from);
}

static void testDecodersAndMixer(const char* mp3Path) {
    // PCM round trip.
    auto pcm = sinePcm(22050, 440, 0.5, 0.5);
    {
        auto dec = audio::makeDecoder(pcmSource(pcm, 22050));
        std::vector<int16_t> got(22050);
        size_t n = dec->read(got.data(), got.size());
        CHECK(n == 11025);
        CHECK(got[100] == (int16_t)(pcm[200] | pcm[201] << 8));
        dec->rewind();
        CHECK(dec->read(got.data(), 10) == 10);
    }

    // Resampling 22050 -> 44100 doubles the length and keeps the pitch.
    {
        audio::Mixer m(44100);
        m.play(audio::makeDecoder(pcmSource(pcm, 22050)), {});
        auto out = mixAll(m, 100000);
        size_t frames = out.size() / 2;
        CHECK(frames >= 22050 && frames < 22050 + 1024);
        CHECK(std::fabs(frequency(out, 0, 44100, 1000, 20000) - 440) < 5);
        CHECK(std::abs(peak(out, 0) - 16383) < 400);
        auto finished = m.takeFinished();
        CHECK(finished.size() == 1);
    }

    // Volume, pan and groups.
    {
        audio::Mixer m(44100);
        audio::Mixer::Params p;
        p.group = 3;
        m.setGroup(3, audio::Mixer::Transform::fromPan(0.5f, -1.f));  // half volume, hard left
        m.play(audio::makeDecoder(pcmSource(pcm, 22050)), p);
        auto out = mixAll(m, 4096);
        CHECK(std::abs(peak(out, 0) - 8191) < 300);
        CHECK(peak(out, 1) == 0);
        m.setGroup(0, audio::Mixer::Transform::fromPan(0.f, 0.f));  // global mute
        m.play(audio::makeDecoder(pcmSource(pcm, 22050)), {});
        auto silent = mixAll(m, 4096);
        CHECK(peak(silent, 0) == 0 && peak(silent, 1) == 0);
    }

    // Loops, in/out points and envelopes (positions in 44.1 kHz samples).
    {
        audio::Mixer m(44100);
        audio::Mixer::Params p;
        p.loops = 3;
        m.play(audio::makeDecoder(pcmSource(pcm, 22050)), p);
        size_t frames = mixAll(m, 1000000).size() / 2;
        CHECK(frames >= 3 * 22050 && frames < 3 * 22050 + 1024);

        audio::Mixer::Params cut;
        cut.inPoint44 = 11025;   // start at 0.25 s
        cut.outPoint44 = 22050;  // end at 0.5 s (the whole sound)
        m.play(audio::makeDecoder(pcmSource(pcm, 22050)), cut);
        frames = mixAll(m, 1000000).size() / 2;
        CHECK(frames >= 11025 && frames < 11025 + 1024);

        audio::Mixer::Params fade;
        fade.envelope = {{0, 1.f, 1.f}, {22050, 0.f, 0.f}};  // fade out over 0.5 s
        m.play(audio::makeDecoder(pcmSource(pcm, 22050)), fade);
        auto out = mixAll(m, 1000000);
        std::vector<int16_t> head(out.begin(), out.begin() + 4000), tail(out.end() - 4000, out.end());
        CHECK(peak(head, 0) > 12000);
        CHECK(peak(tail, 0) < 1500);
    }

    // Stopping by tag.
    {
        audio::Mixer m(44100);
        audio::Mixer::Params p;
        p.tag = 7;
        p.loops = 100;
        m.play(audio::makeDecoder(pcmSource(pcm, 22050)), p);
        CHECK(m.isTagPlaying(7));
        m.stopTag(7);
        CHECK(!m.isTagPlaying(7) && m.voiceCount() == 0);
        CHECK(m.takeFinished().empty());  // explicit stops are not completions
    }

    // Pausing holds the voices playing then; one started while paused (a pause menu's
    // click) plays, and the held one resumes where it was afterwards.
    {
        audio::Mixer m(44100);
        audio::Mixer::VoiceId music = m.play(audio::makeDecoder(pcmSource(pcm, 22050)), {});
        std::vector<int16_t> buf(2048);
        m.mix(buf.data(), 1024);
        double before = m.positionMs(music);
        m.setPaused(true);
        m.mix(buf.data(), 1024);
        CHECK(peak(buf, 0) == 0 && m.positionMs(music) == before);
        m.play(audio::makeDecoder(pcmSource(pcm, 22050)), {});
        m.mix(buf.data(), 1024);
        CHECK(peak(buf, 0) > 12000 && m.positionMs(music) == before);
        m.setPaused(false);
        m.mix(buf.data(), 1024);
        CHECK(m.positionMs(music) > before);
    }

    // MP3 against a known 440 Hz sine.
    if (mp3Path) {
        std::ifstream f(mp3Path, std::ios::binary);
        std::vector<uint8_t> mp3((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK(!mp3.empty());
        audio::Source s;
        s.codec = 2;
        s.rate = 22050;
        s.data = mp3.data();
        s.size = mp3.size();
        audio::Mixer m(44100);
        m.play(audio::makeDecoder(s), {});
        auto out = mixAll(m, 1000000);
        size_t frames = out.size() / 2;
        CHECK(frames >= 44100 && frames < 44100 + 8192);  // 1 s plus encoder padding
        CHECK(std::fabs(frequency(out, 0, 44100, 5000, 40000) - 440) < 5);
        CHECK(peak(out, 0) > 1850 && peak(out, 0) < 2050);  // reference decode peaks at 1946
    }
}

// Every DefineSound in the game decodes to its declared length.
static void testGameSounds(const flash::Movie& movie) {
    int sounds = 0, ok = 0, adpcm = 0;
    double seconds = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (auto& [id, c] : movie.characters()) {
        if (c->kind != flash::Character::Sound) continue;
        auto* s = static_cast<flash::SoundCharacter*>(c.get());
        sounds++;
        audio::Source src;
        src.codec = s->format.codec;
        src.rate = s->format.rate;
        src.is16 = s->format.is16;
        src.stereo = s->format.stereo;
        src.data = s->data.data();
        src.size = s->data.size();
        src.sampleCount = s->sampleCount;
        src.skipSamples = s->seekSamples;
        auto dec = audio::makeDecoder(src);
        if (!dec) continue;
        if (s->format.codec == 1) adpcm++;
        std::vector<int16_t> buf(4096 * 2);
        size_t total = 0, n;
        int maxAbs = 0;
        while ((n = dec->read(buf.data(), 4096)) > 0) {
            total += n;
            for (size_t i = 0; i < n * dec->channels(); i++) maxAbs = std::max(maxAbs, std::abs((int)buf[i]));
        }
        seconds += total / (double)s->format.rate;
        // Allow the MP3 delay we skip to shorten the tail by up to two frames.
        if (total + 2304 >= s->sampleCount && total <= s->sampleCount && maxAbs > 0) ok++;
        else std::printf("  sound %d: decoded %zu of %u frames, peak %d\n", id, total, s->sampleCount, maxAbs);
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("game sounds: %d/%d decode to their declared length (%d ADPCM), %.0f s of audio in %.0f ms\n",
                ok, sounds, adpcm, seconds, ms);
    CHECK(ok == sounds);

    int streams = 0;
    for (auto& [id, c] : movie.characters())
        if (c->kind == flash::Character::Sprite && static_cast<flash::Sprite*>(c.get())->hasStream) streams++;
    CHECK(streams == 2);
}

static std::string firstSoundLinkage(const flash::Movie& movie) {
    for (auto& [name, id] : movie.exports())
        if (auto* c = movie.character(id); c && c->kind == flash::Character::Sound) {
            auto* s = static_cast<const flash::SoundCharacter*>(c);
            if (s->durationMs() < 2000) return name;  // short effect keeps the test quick
        }
    return "";
}

static void testSoundApi(const flash::Movie& movie) {
    audio::Mixer mixer(44100);
    AudioBridge bridge(movie, mixer);
    Player player(movie);
    player.setAudio(&bridge);
    Value G(Runtime::get().global);
    std::string linkage = firstSoundLinkage(movie);
    CHECK(!linkage.empty());

    Value root = valueOf(&player.root());
    Value snd = construct(G["Sound"], Args{&root, 1});
    snd["attachSound"](linkage.c_str());
    CHECK(snd["duration"].get().toNumber() > 0);
    root["completed"] = 0;
    // onSoundComplete (an AS2 function counting completions on _root)
    Activation scope(root, {}, stageObject(&player.root())->targetScope(), stageObject(&player.root()));
    snd["onSoundComplete"] = scope.function([](Activation& scope) -> Value {
        scope["_root"]["completed"] += 1;
        return undefined;
    });
    snd["setVolume"](50);
    CHECK(snd["getVolume"]() == 50);
    snd["start"](0, 2);
    CHECK(mixer.voiceCount() == 1);

    int16_t buf[1024 * 2];
    int guard = 0;
    while (mixer.voiceCount() && guard++ < 100000) mixer.mix(buf, 1024);
    player.tick();  // delivers onSoundComplete
    CHECK(root["completed"] == 1);

    // stop() on the Sound stops its sounds; stopAllSounds() stops everything.
    snd["start"](0, 50);
    CHECK(mixer.voiceCount() == 1);
    snd["stop"]();
    CHECK(mixer.voiceCount() == 0);
    snd["start"](0, 50);
    G["stopAllSounds"]();
    CHECK(mixer.voiceCount() == 0);

    // A Sound targeting a clip controls only that clip's volume group.
    Value clip = root["createEmptyMovieClip"]("holder", 1);
    Value local = construct(G["Sound"], Args{&clip, 1});
    local["attachSound"](linkage.c_str());
    local["setVolume"](0);
    local["start"]();
    mixer.mix(buf, 1024);
    int p = 0;
    for (int i = 0; i < 2048; i++) p = std::max(p, std::abs((int)buf[i]));
    CHECK(p == 0);
    snd["start"]();  // global group at volume 50: audible
    bool audible = false;
    for (int k = 0; k < 20 && !audible; k++) {
        mixer.mix(buf, 1024);
        for (int i = 0; i < 2048; i++) audible |= buf[i] != 0;
    }
    CHECK(audible);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: audio_test game.swf [sine440.mp3]\n");
        return 2;
    }
    testDecodersAndMixer(argc > 2 ? argv[2] : nullptr);
    flash::Movie movie;
    std::string err;
    if (!movie.load(argv[1], err)) {
        std::fprintf(stderr, "load failed: %s\n", err.c_str());
        return 1;
    }
    testGameSounds(movie);
    testSoundApi(movie);
    std::printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
