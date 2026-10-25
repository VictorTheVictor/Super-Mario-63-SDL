#pragma once
// Window presentation (port): ground beyond a level's left and right ends, so a window
// wider than the stage doesn't show the terrain cut off at the level's edge.
//
// A course (`_root.Course`) is built from parts sharing one level-local origin, among them
// BackGFX (background shapes and objects) and FrontGFX (the terrain), which the camera
// scrolls. At each end, a 96 px slice of the terrain just inside the last tile (the
// rounded end cap) is repeated outward endlessly; the level's own terrain is cut where the
// repetition starts. Only terrain shapes repeat: not background art, objects (trees,
// coins, signs are clips) or small shapes (arrow signs, posts). The copies sit below
// BackGFX, so whatever the level itself draws beyond an end stays in front, and follow
// their part's position and color transform. Castle and other indoor courses get none.

#include <memory>
#include <utility>
#include <vector>

#include "flash/Geom.h"

namespace flash {
class DisplayObject;
class Movie;
class MovieClip;
}  // namespace flash
namespace as {
class Player;
}

class EdgeFill {
public:
    // After each tick: builds the fills when a course part has started, and shows them
    // only when `visible` (the window shows beyond the stage).
    void update(as::Player& player, bool visible);

private:
    void build(flash::MovieClip& course);

    std::weak_ptr<flash::DisplayObject> course_;   // the Course the fills were built for
    std::weak_ptr<flash::DisplayObject> pending_;  // a new Course, and ticks since it appeared
    int pendingTicks_ = 0;
    struct Holder {
        std::weak_ptr<flash::DisplayObject> holder, part;  // a fill and the course part it extends
    };
    std::vector<Holder> holders_;
    // The course's terrain shapes and the rectangle (parent space) they're cut to.
    std::vector<std::pair<std::weak_ptr<flash::DisplayObject>, flash::Rect>> clipped_;
};
