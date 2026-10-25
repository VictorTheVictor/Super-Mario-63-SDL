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
// their part's position and color transform. Only ends with a way on (a warp) are
// extended; at a dead end the window's view pans no further than the end instead (see
// deadEnd). The castle's rooms get none.

#include <memory>
#include <optional>
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
    // An end of the current level that leads nowhere (no warp there): its x (level-local
    // pixels), with the matrix from level-local twips to the stage as drawn this frame.
    std::optional<double> deadEnd(bool right, flash::Matrix& toStage) const;

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
    std::weak_ptr<flash::DisplayObject> part_;     // the terrain's course part
    std::optional<double> deadLeft_, deadRight_;   // ends that lead nowhere
};
