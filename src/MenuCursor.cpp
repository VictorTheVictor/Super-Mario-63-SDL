#include "MenuCursor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <regex>

#include "as/Player.h"
#include "as/Stage.h"
#include "flash/DisplayObject.h"
#include "flash/Text.h"

using flash::DisplayObject;
using flash::Point;
using flash::Rect;

namespace {

constexpr double kStageW = 450, kStageH = 300;

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Shown at all: every ancestor visible and not (nearly) transparent.
bool shown(const DisplayObject& d) {
    double alpha = 1;
    for (const DisplayObject* p = &d; p; p = p->parent()) {
        if (!p->visible) return false;
        alpha *= p->cxform.aMul;
    }
    return alpha > 0.05;
}

// The text of an object's subtree (text fields and static text) and its instance names.
std::string labelOf(DisplayObject& d) {
    static const std::regex generated("instance\\d+");
    std::string out;
    const flash::Movie* movie = as::stage().movie;
    std::function<void(DisplayObject&)> visit = [&](DisplayObject& o) {
        if (!o.visible) return;
        if (!o.name.empty() && !std::regex_match(o.name, generated)) out += o.name + " ";
        if (auto* tf = dynamic_cast<flash::TextField*>(&o)) out += tf->text() + " ";
        if (auto* st = dynamic_cast<flash::StaticTextObject*>(&o); st && movie) {
            auto* def = static_cast<flash::StaticTextCharacter*>(st->definition());
            for (const auto& rec : def->records) {
                auto* font = dynamic_cast<flash::FontCharacter*>(movie->character(rec.fontId));
                if (!font) continue;
                for (const auto& g : rec.glyphs)
                    if (g.index >= 0 && g.index < (int)font->codes.size() && font->codes[g.index] < 128)
                        out += (char)font->codes[g.index];
            }
            out += " ";
        }
        o.forEachChild(visit);
    };
    visit(d);
    return lower(out);
}

// Cancel and back buttons whose text is art: by library symbol (sprite id), the object's
// or one of its nearest ancestors'.
std::string roleOf(DisplayObject& d) {
    static const std::map<int, const char*> roles = {
        {9432, " cancel"},  // Menu-Story-Cancel (files, quality)
        {2122, " cancel"},  // Menu Delete No
        {9417, " back"},    // Menu-Back Button
        {9720, " back"},    // LDP Title Screen (level designer menu)
        {9727, " back"},    // LDP Back
        {8935, " back"},    // LD GO Back
        {666, " back"},     // pause menu Back (Options, Star Map)
    };
    int up = 0;
    for (DisplayObject* p = &d; p && up < 4; p = p->parent(), up++)
        if (p->definition())
            if (auto it = roles.find(p->definition()->id); it != roles.end()) return it->second;
    return "";
}

// The object or one of its nearest ancestors is this library symbol.
bool within(DisplayObject& d, int id, int levels = 4) {
    int up = 0;
    for (DisplayObject* p = &d; p && up < levels; p = p->parent(), up++)
        if (p->definition() && p->definition()->id == id) return true;
    return false;
}

// Links, which the controller never clicks (only the mouse opens them): the loading
// screen's Newgrounds banner and Runouw credit, the Runouw intro, a level's "view on site".
bool isLink(DisplayObject& d) {
    for (int id : {2097, 2092, 9239, 9763})
        if (within(d, id)) return true;
    return false;
}

// A point on the object's shape, as near its center as possible.
Point clickPoint(DisplayObject& d, const Rect& b) {
    flash::Matrix m = d.worldMatrix();
    Point c{(b.xMin + b.xMax) / 2, (b.yMin + b.yMax) / 2};
    if (d.hitTest(m, {c.x * 20, c.y * 20})) return c;
    Point best = c;
    double bestDist = 1e18;
    for (int j = 0; j <= 8; j++)
        for (int i = 0; i <= 8; i++) {
            Point p{b.xMin + (b.xMax - b.xMin) * i / 8, b.yMin + (b.yMax - b.yMin) * j / 8};
            double dist = std::hypot(p.x - c.x, p.y - c.y);
            if (dist < bestDist && d.hitTest(m, {p.x * 20, p.y * 20})) best = p, bestDist = dist;
        }
    return best;
}

}  // namespace

const MenuCursor::Target* MenuCursor::selected() const {
    auto sel = selected_.lock();
    if (!sel) return nullptr;
    for (const Target& t : targets_)
        if (t.obj.lock() == sel) return &t;
    return nullptr;
}

void MenuCursor::afterTick(as::Player& player) {
    if (releaseIn_ > 0 && --releaseIn_ == 0) player.mouseUp();
    if (pressIn_ > 0 && --pressIn_ == 0) {
        player.mouseDown();
        releaseIn_ = 1;
    }
    std::vector<std::shared_ptr<DisplayObject>> found;
    auto& probes = as::stage().mouseProbes;
    for (auto& w : probes)
        if (auto d = w.lock()) found.push_back(d);
    probes.clear();
    for (const auto& c : player.clickables())
        if (c.visible)
            if (auto* d = as::resolvePath(&player.root(), c.path)) found.push_back(d->shared_from_this());

    std::vector<Target> all;
    for (auto& d : found) {
        if (as::isRemoved(d.get()) || !shown(*d) || isLink(*d)) continue;
        if (std::any_of(all.begin(), all.end(), [&](const Target& t) { return t.obj.lock() == d; })) continue;
        Rect w = d->worldMatrix().apply(d->localBounds());
        Rect b{w.xMin / 20, w.yMin / 20, w.xMax / 20, w.yMax / 20};
        if (b.empty() || b.xMax <= 0 || b.yMax <= 0 || b.xMin >= kStageW || b.yMin >= kStageH) continue;
        // On the stage only, so the click lands where the player could click.
        Rect onStage{std::max(b.xMin, 0.0), std::max(b.yMin, 0.0), std::min(b.xMax, kStageW), std::min(b.yMax, kStageH)};
        Point p = clickPoint(*d, onStage);
        // One button can be several targets (a clip testing the mouse around a Flash
        // button): they are one.
        auto same = std::find_if(all.begin(), all.end(), [&](const Target& t) {
            return std::fabs(t.point.x - p.x) < 4 && std::fabs(t.point.y - p.y) < 4;
        });
        // The button's text is often beside its hit area (a clip "bg"), in their parent.
        std::string label = labelOf(*d) + roleOf(*d);
        if (DisplayObject* parent = d->parent(); parent && parent != &player.root()) {
            Rect pw = parent->worldMatrix().apply(parent->localBounds());
            auto area = [](const Rect& r) { return (r.xMax - r.xMin) * (r.yMax - r.yMin); };
            if (area(pw) <= area(w) * 3) label += labelOf(*parent);
        }
        if (same != all.end()) {
            same->label += label;
            continue;
        }
        all.push_back({d, b, p, label});
    }
    // Screen-sized areas (click anywhere to skip, a panel's background) only when
    // nothing else is offered.
    std::vector<Target> out;
    for (const Target& t : all) {
        double area = (std::min(t.bounds.xMax, kStageW) - std::max(t.bounds.xMin, 0.0)) *
                      (std::min(t.bounds.yMax, kStageH) - std::max(t.bounds.yMin, 0.0));
        if (area < kStageW * kStageH * 0.6) out.push_back(t);
    }
    if (out.empty()) out = all;
    // Reading order: rows top to bottom, left to right within a row; back and cancel last.
    auto leaves = [](const Target& t) {
        return t.label.find(" back") != std::string::npos || t.label.find(" cancel") != std::string::npos;
    };
    std::stable_sort(out.begin(), out.end(), [&](const Target& a, const Target& b) {
        if (leaves(a) != leaves(b)) return leaves(b);
        if (std::fabs(a.point.y - b.point.y) > 12) return a.point.y < b.point.y;
        return a.point.x < b.point.x;
    });
    // A screen opened over another (the quality choices, a story file's Play/Delete/Cancel)
    // takes the cursor: only its buttons, up to its Cancel, are offered.
    auto cancel = std::find_if(out.begin(), out.end(), [](const Target& t) {
        return t.label.find("cancel") != std::string::npos;
    });
    if (cancel != out.end()) {
        // The story files (their buttons share a clip with the chosen file's Play, Copy,
        // Delete and Cancel) and the screen's Back wait for the Cancel.
        out.erase(std::remove_if(out.begin(), out.end(), [](const Target& t) {
                      auto o = t.obj.lock();
                      return o && (within(*o, 9414) || within(*o, 9417));
                  }),
                  out.end());
        cancel = std::find_if(out.begin(), out.end(), [](const Target& t) {
            return t.label.find("cancel") != std::string::npos;
        });
    }
    if (cancel != out.end())
        if (auto c = cancel->obj.lock()) {
            // The smallest ancestor of the Cancel button holding another target.
            for (DisplayObject* group = c->parent(); group && group != &player.root(); group = group->parent()) {
                auto inGroup = [&](const Target& t) {
                    auto o = t.obj.lock();
                    for (DisplayObject* p = o.get(); p; p = p->parent())
                        if (p == group) return true;
                    return false;
                };
                if (std::count_if(out.begin(), out.end(), inGroup) < 2) continue;
                out.erase(std::remove_if(out.begin(), out.end(), [&](const Target& t) { return !inGroup(t); }), out.end());
                break;
            }
        }
    targets_ = std::move(out);
    // The selected button went away (or is no longer offered): the nearest one instead,
    // unless it was clicked (the screen changes: A then selects its first button).
    if (selecting_ && !selected() && clicked_) clear();
    if (selecting_ && !selected() && !targets_.empty()) {
        const Target* nearest = &targets_.front();
        for (const Target& t : targets_)
            if (std::hypot(t.point.x - lastPoint_.x, t.point.y - lastPoint_.y) <
                std::hypot(nearest->point.x - lastPoint_.x, nearest->point.y - lastPoint_.y))
                nearest = &t;
        hover(player, *nearest);
    }
    // Keep the mouse on the selection as it moves (buttons grow and slide in).
    if (const Target* t = selected()) {
        lastPoint_ = t->point;
        const auto& s = as::stage();
        if (releaseIn_ == 0 && pressIn_ == 0 && std::hypot(s.mouseX - t->point.x, s.mouseY - t->point.y) > 2) hover(player, *t);
    }
}

void MenuCursor::hover(as::Player& player, const Target& t) {
    if (t.obj.lock() != selected_.lock())
        if (auto o = t.obj.lock()) std::printf("menu cursor: %s \"%s\"\n", as::targetPath(o.get()).c_str(), t.label.c_str());
    if (t.obj.lock() != selected_.lock()) clicked_ = false;
    selected_ = t.obj;
    selecting_ = true;
    lastPoint_ = t.point;
    player.mouseMove(t.point.x, t.point.y);
}

void MenuCursor::click(as::Player& player, const Target& t) {
    if (pressIn_ || releaseIn_) return;  // a click under way
    hover(player, t);
    clicked_ = true;
    pressIn_ = 1;
}

bool MenuCursor::activate(as::Player& player) {
    if (targets_.empty()) return false;
    const Target* t = selected();
    click(player, t ? *t : targets_.front());
    return true;
}

bool MenuCursor::select(as::Player& player) {
    if (targets_.empty()) return false;
    if (const Target* t = selected()) click(player, *t);
    else hover(player, targets_.front());
    return true;
}

bool MenuCursor::move(as::Player& player, int dx, int dy) {
    if (targets_.empty()) return false;
    const Target* from = selected();
    if (!from) {
        hover(player, targets_.front());
        return true;
    }
    // The nearest target in that direction, favoring ones in line with the selection.
    const Target* best = nullptr;
    double bestScore = 1e18;
    for (const Target& t : targets_) {
        if (&t == from) continue;
        double vx = t.point.x - from->point.x, vy = t.point.y - from->point.y;
        double along = vx * dx + vy * dy, across = std::fabs(vx * dy - vy * dx);
        if (along < 4) continue;
        double score = along + across * 3;
        if (score < bestScore) best = &t, bestScore = score;
    }
    if (best) hover(player, *best);
    return true;
}

bool MenuCursor::clickLabeled(as::Player& player, const std::vector<std::string>& words) {
    for (const std::string& w : words)
        for (const Target& t : targets_)
            if (t.label.find(w) != std::string::npos) {
                click(player, t);
                return true;
            }
    return false;
}

bool MenuCursor::back(as::Player& player) {
    return clickLabeled(player, {"cancel", "back", "return", "title screen", "close"});
}
