#pragma once
// Window presentation (port): ground beyond a level's left and right ends, so a window
// wider than the stage doesn't show the terrain cut off at the level's edge.
//
// A course (`_root.Course`) is built from exported parts sharing one level-local origin:
// <level>BackGFX (background shapes and objects) and <level>FrontGFX (the terrain),
// among others. At each end of the terrain:
// - if a warp box there leads left/right into another course part (see EdgeLinks), that
//   part's terrain and background shapes are drawn beyond the edge, lined up with Mario's
//   kept height where the warp keeps it, else with the drawn ground on both sides;
// - otherwise the terrain shapes (and background shapes reaching the edge) are mirrored
//   about the edge.
// Only shapes are drawn, never objects (trees, coins, signs are clips), and each fill is
// masked to the far side of its edge. The fills live in clips added to the Course.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace flash {
class Movie;
class DisplayObject;
class MovieClip;
}  // namespace flash
namespace as {
class Player;
}

// A warp box (`warp<warp>` in BackGFX sprite `sprite`) leading into course part `course`,
// where Mario starts at level-local (spawnX, spawnY) (NAN when computed at run time). With
// keepsY he keeps his height: his level-local y there is his y here plus yShift.
struct EdgeLink {
    uint16_t sprite;
    uint8_t warp;
    bool right;  // a right transition (leaves through the right end)
    const char* course;
    double spawnX, spawnY;
    bool keepsY;
    double yShift;
};
const std::vector<EdgeLink>& edgeLinks();  // src/EdgeLinks.cpp (tools/edge_links.py)

class EdgeFill {
public:
    // After each tick: builds the fills when a course part has started, and shows them
    // only when `visible` (the window shows beyond the stage).
    void update(as::Player& player, const flash::Movie& movie, bool visible);

private:
    void build(flash::MovieClip& course, const flash::Movie& movie);

    std::weak_ptr<flash::DisplayObject> course_;   // the Course the fills were built for
    std::weak_ptr<flash::DisplayObject> pending_;  // a new Course, and ticks since it appeared
    int pendingTicks_ = 0;
    struct Holder {
        std::weak_ptr<flash::DisplayObject> holder, part;  // a fill and the course part it extends
    };
    std::vector<Holder> holders_;
};
