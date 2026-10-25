#pragma once
// Controller menu cursor (port): plays the game's menus with a controller by driving the
// mouse. The game's menu buttons are mostly clips whose scripts hit-test the mouse
// position themselves each frame (hitTest(_root._xmouse, _root._ymouse, ...)), plus a few
// Flash buttons and clips with mouse handlers; the targets are whatever did either during
// the last tick (see as::StageContext::mouseProbes). The D-pad moves a selection between
// them (the mouse hovers the selected one), A clicks it, B clicks the screen's Cancel,
// Back or similar button.

#include <memory>
#include <string>
#include <vector>

#include "flash/Geom.h"

namespace flash {
class DisplayObject;
}
namespace as {
class Player;
}

class MenuCursor {
public:
    struct Target {
        std::weak_ptr<flash::DisplayObject> obj;
        flash::Rect bounds;  // stage pixels
        flash::Point point;  // where to click (stage pixels): on the shape, near the center
        std::string label;   // its text and instance names, lowercase
    };

    // After each tick: the targets the menu offered during it (and releases a click).
    void afterTick(as::Player& player);
    const std::vector<Target>& targets() const { return targets_; }
    const Target* selected() const;

    // Controller presses; each returns whether the menu took it.
    bool select(as::Player& player);            // A: select the first target, then click it
    bool move(as::Player& player, int dx, int dy);  // D-pad: the nearest target that way
    bool back(as::Player& player);              // B: the screen's cancel/back button
    bool activate(as::Player& player);          // clicks the selection, or else the first target
    // Clicks the first target whose label contains one of `words` (in order of preference).
    bool clickLabeled(as::Player& player, const std::vector<std::string>& words);
    void clear() { selected_.reset(), selecting_ = false; }

private:
    void hover(as::Player& player, const Target& t);
    const Target* first() const;  // where a new selection starts
    void click(as::Player& player, const Target& t);

    std::vector<Target> targets_;
    std::weak_ptr<flash::DisplayObject> selected_;
    bool selecting_ = false;  // a selection was made (it may have gone away since)
    bool clicked_ = false;    // the selection was clicked
    flash::Point lastPoint_;  // where the selection last was
    // A click: the mouse moves onto the target, and the button goes down a tick later (the
    // game's buttons notice the mouse over them in their frame scripts first) and comes
    // up the tick after.
    int pressIn_ = 0, releaseIn_ = 0;  // ticks until the mouse button goes down / up
    // Buttons clicked to get where the menu is (most recent last); after B, the cursor
    // returns to the latest one that shows up again within a few seconds.
    struct Visit {
        std::weak_ptr<flash::DisplayObject> obj;
        flash::Point point;
        std::string label;
    };
    std::vector<Visit> visits_;
    int returnFor_ = 0;  // ticks left to find it after B
};
