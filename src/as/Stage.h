#pragma once
// Bridge between the display list and AS2: MovieClip/TextField/Button objects,
// their properties and methods, Color and Sound, and the script registry that ported
// game code fills in.

#include <memory>
#include <string>
#include <vector>

#include "as/Runtime.h"
#include "as/Scripts.h"
#include "flash/DisplayObject.h"

namespace flash {
class TextField;
}

namespace as {

// ---------------------------------------------------------------- stage objects

class StageObject : public Object {
public:
    StageObject(const std::shared_ptr<flash::DisplayObject>& d, Object* proto);
    ~StageObject() override;

    // The live display object; re-resolves by path if the original was removed and a
    // new object took its place (AVM1 reference semantics). May return null.
    flash::DisplayObject* display();
    flash::MovieClip* clip();
    flash::TextField* textField();

    std::weak_ptr<flash::DisplayObject> ref;
    std::string path;  // last known target path, for re-resolution
    std::vector<ClipHandler> handlers;
    uint32_t handlerMask() const;
    Scope* targetScope();  // [Target(this), Global]

    bool getOwn(Atom key, Value& out) override;
    bool setOwn(Atom key, const Value& v) override;
    bool hasOwn(Atom key) override;
    void ownKeys(std::vector<Atom>& out) override;
    const char* typeName() const override;
    std::string defaultString() const override;
    void trace(Gc& gc) override;

private:
    bool getMagic(flash::DisplayObject* d, Atom key, Value& out);
    bool setMagic(flash::DisplayObject* d, Atom key, const Value& v);
    Scope* targetScope_ = nullptr;
    bool isClip_ = false;
};

// Shared stage state, owned by the Player.
struct StageContext {
    const flash::Movie* movie = nullptr;
    std::shared_ptr<flash::MovieClip> root;
    double mouseX = 0, mouseY = 0;  // stage pixels
    std::string quality = "HIGH";
    std::string url = "file:///Super_Mario_63_2010.swf";  // MovieClip._url
};
StageContext& stage();

StageObject* stageObject(flash::DisplayObject* d);  // creates on demand
Value valueOf(flash::DisplayObject* d);
flash::DisplayObject* displayOf(const Value& v);
bool isRemoved(const flash::DisplayObject* d);
std::string targetPath(const flash::DisplayObject* d);  // "_level0.a.b"
// Resolves "a.b", "_root.a", "_parent.x", "/a/b", "../a" relative to `base`.
flash::DisplayObject* resolvePath(flash::DisplayObject* base, const std::string& path);

// Script-facing depth helpers (AS depth = internal depth - 16384).
constexpr int kDepthOffset = 16384;

// Hooks for the audio milestone. Default implementation is silent.
struct SoundHost {
    virtual ~SoundHost() = default;
    virtual void start(Object* sound, const std::string& linkage, double offsetSec, int loops) {}
    virtual void stop(Object* sound, const std::string& linkage) {}
    virtual void stopAll() {}
    virtual void volumeChanged(Object* sound) {}
};
void setSoundHost(SoundHost* host);

void installStageBuiltins(Runtime& rt);

}  // namespace as
