#pragma once
// Display list: DisplayObject tree with MovieClip timeline playback.

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "flash/Geom.h"
#include "flash/Raster.h"
#include "flash/Swf.h"

namespace flash {

class DisplayObject;
class MovieClip;

// Lets the script runtime observe the timeline without the display list depending on it.
struct TimelineHooks {
    virtual ~TimelineHooks() = default;
    // A new object entered the display list. `place` is the PlaceObject command that
    // created it (null for script-created objects). Called before a clip runs frame 1.
    virtual void onCreated(DisplayObject& obj, const PlaceCommand* place, uint32_t placeIndex) {}
    // An object (and implicitly its children) is about to leave the display list.
    virtual void onRemoved(DisplayObject& obj) {}
    // A frame's DoAction should run (queued by the runtime).
    virtual void onFrameAction(MovieClip& clip, uint32_t actionIndex) {}
    virtual void onStartSound(MovieClip& clip, const SoundStart& sound) {}
    // The clip entered a frame carrying stream-sound data (Sprite::streamBlocks index).
    virtual void onStreamBlock(MovieClip& clip, uint32_t blockIndex) {}
};
extern TimelineHooks* timelineHooks;

class DisplayObject : public std::enable_shared_from_this<DisplayObject> {
public:
    explicit DisplayObject(Character* def) : def_(def) {}
    virtual ~DisplayObject() = default;

    Character* definition() const { return def_; }
    MovieClip* parent() const { return parent_; }

    int depth = 0;
    std::string name;
    Matrix matrix;  // twips, relative to parent
    ColorTransform cxform;
    uint16_t ratio = 0;
    uint16_t clipDepth = 0;
    bool visible = true;
    uint8_t blendMode = 0;
    std::vector<Filter> filters;
    bool placedByTimeline = true;   // removable/replaceable by the timeline
    bool transformedByScript = false;  // timeline no longer moves it
    void* scriptObject = nullptr;   // owned by the script runtime

    // Scale/rotation as last set by script (Flash keeps these to avoid drift).
    struct Transform {
        double scaleX = 1, scaleY = 1, rotation = 0, skew = 0;  // factors, radians
    };
    const Transform& transform();
    void setTransform(const Transform& t);  // keeps the translation

    // Matrix from this object's space to the root (twips).
    Matrix worldMatrix() const;

    virtual void render(Rasterizer& r, Surface& dst, const IntRect& clip, const Matrix& m,
                        const ColorTransform& cx, const Movie& movie) {}
    // Bounds of the content in this object's own coordinate space (twips).
    virtual Rect localBounds() const { return {}; }
    // Shape-accurate hit test; `m` maps this object's space to the space of `p`.
    virtual bool hitTest(const Matrix& m, Point p) const { return false; }
    virtual void forEachChild(const std::function<void(DisplayObject&)>& fn) {}
    // Called once per frame tick (timeline advance, recursive; viewer mode only).
    virtual void advance() {}

    void invalidateTransform() { transformValid_ = false; }

protected:
    friend class MovieClip;
    Character* def_;
    MovieClip* parent_ = nullptr;
    Transform transform_;
    bool transformValid_ = false;
};

class ShapeObject : public DisplayObject {
public:
    using DisplayObject::DisplayObject;
    void render(Rasterizer& r, Surface& dst, const IntRect& clip, const Matrix& m,
                const ColorTransform& cx, const Movie& movie) override;
    Rect localBounds() const override;
    bool hitTest(const Matrix& m, Point p) const override;
};

class MorphObject : public DisplayObject {
public:
    using DisplayObject::DisplayObject;
    void render(Rasterizer& r, Surface& dst, const IntRect& clip, const Matrix& m,
                const ColorTransform& cx, const Movie& movie) override;
    Rect localBounds() const override;
    bool hitTest(const Matrix& m, Point p) const override;
};

// MovieClip.lineTo()/beginFill() drawing, rendered beneath the clip's children.
struct Drawing {
    ShapeDef shape;  // committed paths
    bool filling = false, lining = false;
    FillStyle fill;
    LineStyle line;
    std::vector<Edge> fillEdges, lineEdges;
    Point pen, fillStart;

    void clear();
    void lineStyle(double widthTwips, Color c);  // width < 0: no line
    void beginFill(Color c);
    void endFill();
    void moveTo(Point p);
    void lineTo(Point p);
    void curveTo(Point c, Point p);
    // Committed paths plus any pending (auto-closed) fill and line.
    ShapeDef snapshot() const;
    bool empty() const;

private:
    void flushLine();
};

class MovieClip : public DisplayObject {
public:
    MovieClip(const Movie& movie, Sprite* sprite);

    Sprite* sprite() const { return sprite_; }
    const Movie& movie() const { return movie_; }
    int currentFrame() const { return currentFrame_; }  // 1-based, 0 before first frame
    int totalFrames() const { return sprite_->frameCount; }
    bool playing = true;

    void play() { playing = true; }
    void stop() { playing = false; }
    // Runs the destination frame's commands now and queues only its scripts.
    void gotoFrame(int frame, bool andPlay);
    bool gotoLabel(const std::string& label, bool andPlay);
    void nextFrame() { gotoFrame(currentFrame_ + 1, false); }
    void prevFrame() { gotoFrame(currentFrame_ - 1, false); }

    // Enters frame 1 (called right after creation).
    void initialize();
    bool initialized() const { return currentFrame_ > 0; }
    // Advances this clip's own timeline by one frame if playing (no recursion).
    void runFrameSelf();
    void advance() override;

    const std::map<int, std::shared_ptr<DisplayObject>>& children() const { return children_; }
    DisplayObject* childAt(int depth) const;
    DisplayObject* childByName(const std::string& name) const;

    // Script-side display list operations (depths are internal depths).
    std::shared_ptr<DisplayObject> attach(Character* def, int depth, const std::string& name);
    std::shared_ptr<DisplayObject> createEmpty(int depth, const std::string& name);
    void removeChild(int depth);
    // Moves a child to another depth, swapping with whatever is there.
    void swapChildDepth(int fromDepth, int toDepth);

    Drawing& drawing();
    bool hasDrawing() const { return drawing_ && !drawing_->empty(); }
    // True if a mask layer (clipDepth) covers `depth` but not point `p` (m: this clip's
    // space -> p's space). Masked content only counts where its mask does.
    bool maskBlocks(int depth, const Matrix& m, Point p) const;

    void render(Rasterizer& r, Surface& dst, const IntRect& clip, const Matrix& m,
                const ColorTransform& cx, const Movie& movie) override;
    Rect localBounds() const override;
    bool hitTest(const Matrix& m, Point p) const override;
    void forEachChild(const std::function<void(DisplayObject&)>& fn) override;

private:
    struct PlaceState {
        const PlaceCommand* place = nullptr;  // command that created the instance
        uint16_t characterId = 0;
        Matrix matrix;
        ColorTransform cxform;
        uint16_t ratio = 0;
        std::string name;
        uint16_t clipDepth = 0;
        std::vector<Filter> filters;
        uint8_t blendMode = 0;
        bool visible = true;
    };

    void runFrame(int frame, bool runActions);
    void applyPlace(const PlaceCommand& p);
    std::shared_ptr<DisplayObject> instantiate(uint16_t characterId);
    // Inserts a created object and notifies hooks, then enters frame 1 for clips.
    void adopt(int depth, std::shared_ptr<DisplayObject> obj, const PlaceCommand* place);
    void setChild(int depth, std::shared_ptr<DisplayObject> obj);
    uint32_t placeIndex(const PlaceCommand* p) const;

    const Movie& movie_;
    Sprite* sprite_;
    int currentFrame_ = 0;
    std::map<int, std::shared_ptr<DisplayObject>> children_;
    std::unique_ptr<Drawing> drawing_;
};

// Button: shows the children of its current state (Up/Over/Down).
class ButtonObject : public DisplayObject {
public:
    ButtonObject(const Movie& movie, ButtonCharacter* def);
    void setState(uint8_t state);  // ButtonCharacter::State
    uint8_t state() const { return state_; }
    void render(Rasterizer& r, Surface& dst, const IntRect& clip, const Matrix& m,
                const ColorTransform& cx, const Movie& movie) override;
    Rect localBounds() const override;
    bool hitTest(const Matrix& m, Point p) const override;
    void forEachChild(const std::function<void(DisplayObject&)>& fn) override;
    void advance() override;

private:
    std::map<int, std::shared_ptr<DisplayObject>> build(uint8_t state) const;
    const Movie& movie_;
    uint8_t state_ = 0;
    std::map<int, std::shared_ptr<DisplayObject>> children_;
    std::map<int, std::shared_ptr<DisplayObject>> hitChildren_;
};

// Renders `obj` under its parent's transform, applying its own matrix, color transform,
// filters and blend mode (offscreen layers when needed).
void renderDisplayObject(DisplayObject& obj, Rasterizer& r, Surface& dst, const IntRect& clip,
                         const Matrix& parentMatrix, const ColorTransform& parentCx,
                         const Movie& movie);

// Union of two rects, treating empty ones as absent.
Rect unionRect(const Rect& a, const Rect& b);

// Creates the right DisplayObject subclass for a character (MovieClip for sprites).
std::shared_ptr<DisplayObject> createDisplayObject(const Movie& movie, Character* def);

}  // namespace flash
