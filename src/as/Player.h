#pragma once
// Runs a movie with scripts: per-frame AVM1 event order, the action queue, input
// dispatch, text field variable bindings and garbage collection.

#include <deque>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "as/Stage.h"
#include "flash/DisplayObject.h"
#include "flash/Text.h"
#include "flash/WorkerPool.h"

namespace as {

class AudioBridge;

class Player : public flash::TimelineHooks {
public:
    explicit Player(const flash::Movie& movie);
    ~Player() override;

    // Advances one frame (enterFrame events, timelines, queued scripts, intervals).
    void tick();
    // Renders the stage scaled to fill `dst`'s width, split into bands across threads.
    // `interpolation` places moving objects between the previous tick (0) and the
    // current one (1), for drawing more frames than the game ticks.
    void render(flash::Rasterizer& r, flash::Surface& dst, double interpolation = 1);
    // Render threads, including the caller (default: the machine's hardware threads).
    void setRenderThreads(int n);
    // Keeping filtered layers between frames (on by default; off for comparisons).
    void setLayerCaching(bool on) { layerCaching_ = on; }
    flash::MovieClip& root() { return *root_; }

    // Input, in Flash key codes and stage pixels.
    void keyDown(int code, int ascii);
    void keyUp(int code);
    void mouseMove(double x, double y);
    void mouseDown();
    void mouseUp();

    // Frame scripts that ran without a ported implementation (sprite id, action index).
    const std::set<std::pair<uint16_t, uint32_t>>& missingScripts() const { return missing_; }
    uint64_t frameCount() const { return frames_; }
    // Buttons and clips with mouse handlers, with their stage bounds in pixels.
    struct Clickable {
        std::string path;
        flash::Rect bounds;
        bool visible;
        int characterId;
        uint32_t handlerMask;  // clipevent flags of its onClipEvent()/on() handlers
        std::string methods;   // onPress/onRelease/... defined as properties
    };
    std::vector<Clickable> clickables();
    // Visible text fields whose text contains `needle`: (path, bound variable, text).
    struct TextHit {
        std::string path, variable, text;
    };
    std::vector<TextHit> textContaining(const std::string& needle);
    // Visible text fields whose glyphs extend past their bounds (they get clipped).
    struct TextOverflow {
        std::string path, text;
        flash::Rect bounds, extent;  // local pixels
        bool autoSize;
    };
    std::vector<TextOverflow> textOverflows();
    // Optional: timeline sounds and Sound objects play through this bridge.
    void setAudio(AudioBridge* audio) { audio_ = audio; }

    // flash::TimelineHooks
    void onCreated(flash::DisplayObject& obj, const flash::PlaceCommand* place, uint32_t placeIndex) override;
    void onRemoved(flash::DisplayObject& obj) override;
    void onFrameAction(flash::MovieClip& clip, uint32_t actionIndex) override;
    void onStartSound(flash::MovieClip& clip, const flash::SoundStart& sound) override;
    void onStreamBlock(flash::MovieClip& clip, uint32_t blockIndex) override;

private:
    enum Priority { Initialize, Construct, Normal, PriorityCount };
    enum class Kind { Frame, Event, Button };
    struct Action {
        std::shared_ptr<flash::DisplayObject> obj;
        Kind kind;
        uint32_t value;  // action index, event flag or button condition
    };

    void queue(flash::DisplayObject& obj, Kind kind, uint32_t value, Priority p = Normal);
    void runActions();
    void execute(const Action& a);
    void runEvent(flash::DisplayObject& obj, uint32_t event);
    void runButton(flash::DisplayObject& button, uint32_t condition);
    // Queues `event` for every live clip that handles it (clip event or method).
    void broadcastEvent(uint32_t event);
    std::vector<std::shared_ptr<flash::MovieClip>> liveClips();

    bool isInteractive(flash::DisplayObject& d);
    flash::DisplayObject* pick(flash::DisplayObject& d, const flash::Matrix& m, flash::Point p);
    void setButtonState(flash::DisplayObject* d, uint8_t state);
    void pointerEvent(flash::DisplayObject* d, uint32_t event, uint32_t buttonCond);

    void updateBindings();
    void markRoots(Gc& gc);
    void markTree(Gc& gc, flash::DisplayObject& d);

    const flash::Movie& movie_;
    std::shared_ptr<flash::MovieClip> root_;
    std::list<std::weak_ptr<flash::MovieClip>> execList_;  // newest first
    std::deque<Action> queue_[PriorityCount];
    struct Binding {
        std::weak_ptr<flash::TextField> field;
        std::string last;
        bool initialized = false;
    };
    std::vector<Binding> bindings_;
    std::unique_ptr<flash::WorkerPool> renderPool_;
    std::vector<flash::Rasterizer> rasterizers_;  // one per render thread but the caller
    flash::LayerCache layerCache_;
    bool layerCaching_ = true;
    void syncBinding(Binding& b);
    std::weak_ptr<flash::DisplayObject> hover_, pressed_;
    std::set<std::pair<uint16_t, uint32_t>> missing_;
    uint64_t frames_ = 0;
    int instanceCounter_ = 1;
    AudioBridge* audio_ = nullptr;
};

}  // namespace as
