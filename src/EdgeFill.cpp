#include "EdgeFill.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <regex>

#include "as/Player.h"
#include "as/Stage.h"
#include "flash/DisplayObject.h"
#include "flash/Shape.h"
#include "flash/Swf.h"

using flash::DisplayObject;
using flash::Matrix;
using flash::MovieClip;
using flash::Rect;
using flash::ShapeCharacter;

namespace {

// A shape and its matrix (twips) into course space, or level-local space for a part.
struct Placed {
    const ShapeCharacter* shape;
    Matrix m;
    Rect bounds() const { return m.apply(shape->def.bounds); }
};

// The shapes on frame 1 of an exported course part (frames differ only in objects).
std::vector<Placed> partShapes(const flash::Movie& movie, const std::string& name) {
    std::vector<Placed> out;
    auto* c = movie.exported(name);
    if (!c || c->kind != flash::Character::Sprite) return out;
    auto* sprite = static_cast<const flash::Sprite*>(c);
    if (sprite->frames.empty()) return out;
    std::map<int, std::pair<uint16_t, Matrix>> list;  // depth -> character, matrix
    for (const flash::FrameCommand& cmd : sprite->frames[0]) {
        if (cmd.kind == flash::FrameCommand::Remove) list.erase((int)cmd.index);
        if (cmd.kind != flash::FrameCommand::Place) continue;
        const flash::PlaceCommand& p = sprite->places[cmd.index];
        auto& entry = list[p.depth];
        if (p.hasCharacter) entry = {p.characterId, Matrix{}};
        if (p.hasMatrix) entry.second = p.matrix;
    }
    for (auto& [depth, entry] : list) {
        auto* k = movie.character(entry.first);
        if (k && k->kind == flash::Character::Shape) out.push_back({static_cast<const ShapeCharacter*>(k), entry.second});
    }
    return out;
}

// The shapes directly in a live course part, in level-local space (the part's own: the
// camera scrolls the parts, all placed at the same level origin).
std::vector<Placed> liveShapes(MovieClip& clip) {
    std::vector<Placed> out;
    clip.forEachChild([&](DisplayObject& c) {
        if (dynamic_cast<flash::ShapeObject*>(&c) && c.definition())
            out.push_back({static_cast<const ShapeCharacter*>(c.definition()), c.matrix});
    });
    return out;
}

// Horizontal extent (pixels) of a set of shapes.
std::pair<double, double> xExtent(const std::vector<Placed>& shapes) {
    double x0 = INFINITY, x1 = -INFINITY;
    for (const Placed& p : shapes) {
        Rect b = p.bounds();
        x0 = std::min(x0, b.xMin / 20);
        x1 = std::max(x1, b.xMax / 20);
    }
    return {x0, x1};
}

bool solidAt(const std::vector<Placed>& shapes, double x, double y) {
    flash::Point pt{x * 20, y * 20};
    for (const Placed& p : shapes) {
        Rect b = p.bounds();
        if (pt.x < b.xMin || pt.x > b.xMax || pt.y < b.yMin || pt.y > b.yMax || !p.m.invertible()) continue;
        if (flash::shapeContains(p.shape->def, p.m.inverse().apply(pt))) return true;
    }
    return false;
}

// Going down at x from yFrom to yTo: the first ground surface (empty above, solid below).
std::optional<double> surface(const std::vector<Placed>& shapes, double x, double yFrom, double yTo) {
    bool empty = false;
    for (double y = yFrom; y <= yTo; y += 2) {
        bool solid = solidAt(shapes, x, y);
        if (solid && empty) return y;
        empty = !solid;
    }
    return std::nullopt;
}

// Along row y from x (stepping by `step` up to `limit` pixels): the first solid column.
std::optional<double> firstSolid(const std::vector<Placed>& shapes, double y, double x, double step, double limit) {
    for (double d = 0; d <= limit; d += std::fabs(step), x += step)
        if (solidAt(shapes, x, y)) return x;
    return std::nullopt;
}

// The mask that limits a fill to the far side of its edge: a rectangle from x = 0 to the
// right (placed at the edge, mirrored for a left edge), tall enough for any course.
ShapeCharacter* maskShape() {
    static ShapeCharacter mask = [] {
        ShapeCharacter s;
        const double w = 30000 * 20, h = 30000 * 20;
        s.def.bounds = Rect{0, -h, w, h};
        flash::FillStyle fill;
        fill.color = {255, 255, 255, 255};
        s.def.fills.push_back(fill);
        flash::FillPath path;
        path.style = 0;
        flash::Point c[4] = {{0, -h}, {w, -h}, {w, h}, {0, h}};
        for (int i = 0; i < 4; i++) path.edges.push_back({c[i], {}, c[(i + 1) % 4], false});
        s.def.layers.emplace_back();
        s.def.layers.back().fills.push_back(path);
        return s;
    }();
    return &mask;
}

// A clip in the course holding `shapes` (each moved by `place`), masked to beyond `edgeX`.
std::shared_ptr<DisplayObject> makeHolder(MovieClip& course, int depth, const std::string& name, double edgeX,
                                          bool right, const std::vector<Placed>& shapes, const Matrix& place) {
    auto holder = course.createEmpty(depth, name);
    auto* clip = dynamic_cast<MovieClip*>(holder.get());
    if (!clip) return nullptr;
    auto mask = clip->attach(maskShape(), 1, "");
    mask->matrix = Matrix::translate(edgeX * 20, 0) * Matrix::scale(right ? 1 : -1, 1);
    mask->clipDepth = 65000;  // masks every shape below
    int depth2 = 2;
    for (const Placed& p : shapes) {
        auto obj = clip->attach(const_cast<ShapeCharacter*>(p.shape), depth2++, "");
        if (obj) obj->matrix = place * p.m;
    }
    return holder;
}

}  // namespace

void EdgeFill::update(as::Player& player, const flash::Movie& movie, bool visible) {
    auto* course = dynamic_cast<MovieClip*>(as::resolvePath(&player.root(), "_root.Course"));
    if (!course) return;
    // Built a few ticks after the course part appears: BackGFX's script first moves it to
    // the frame (star count) that has the warp boxes. A transition covers the screen then.
    if (course_.lock().get() != course) {
        if (pending_.lock().get() != course) {
            pending_ = course->weak_from_this();
            pendingTicks_ = 0;
            holders_.clear();
        }
        if (++pendingTicks_ >= 4) build(*course, movie);
    }
    // Each fill moves with the part it extends (the camera scrolls the parts), including
    // where it was last tick, for in-between frames.
    for (Holder& h : holders_) {
        auto holder = h.holder.lock();
        auto part = h.part.lock();
        if (!holder || !part) continue;
        holder->visible = visible;
        holder->matrix = part->matrix;
        holder->prevMatrix = part->prevMatrix;
        holder->hasPrevMatrix = part->hasPrevMatrix;
    }
}

void EdgeFill::build(MovieClip& course, const flash::Movie& movie) {
    MovieClip *front = nullptr, *back = nullptr;
    course.forEachChild([&](DisplayObject& c) {
        auto* clip = dynamic_cast<MovieClip*>(&c);
        if (!clip) return;
        if (c.name == "FrontGFX") front = clip;
        if (c.name == "BackGFX") back = clip;
    });
    // The terrain is FrontGFX's shapes; some courses draw it in BackGFX instead.
    std::vector<Placed> terrain = front ? liveShapes(*front) : std::vector<Placed>{};
    std::vector<Placed> backShapes = back ? liveShapes(*back) : std::vector<Placed>{};
    bool terrainInBack = terrain.empty();
    if (terrainInBack) terrain = backShapes;
    if (terrain.empty()) return;  // not placed yet: try again next tick
    course_ = course.weak_from_this();
    auto [left, right] = xExtent(terrain);
    // Fills only show beyond everything the level draws (BackGFX can reach past the
    // terrain), so they never cover any of it.
    auto [backLeft, backRight] = backShapes.empty() ? std::pair{left, right} : xExtent(backShapes);
    double outerLeft = std::min(left, backLeft), outerRight = std::max(right, backRight);
    // A fill (if it has anything to show) in the course, moving with the part it extends.
    auto addHolder = [&](int depth, const std::string& name, double edge, bool atRight, const std::vector<Placed>& shapes,
                         const Matrix& place, MovieClip* part) {
        if (shapes.empty() || !part) return;
        if (auto holder = makeHolder(course, depth, name, edge, atRight, shapes, place))
            holders_.push_back({holder, part->weak_from_this()});
    };
    MovieClip* frontPart = terrainInBack || !front ? back : front;

    // The warp boxes (hidden clips named warp1, warp2, ... in BackGFX), level-local.
    struct Warp {
        int number;
        Rect box;
    };
    std::vector<Warp> warps;
    static const std::regex warpName("warp(\\d+)");
    if (back)
        back->forEachChild([&](DisplayObject& c) {
            std::smatch m;
            if (std::regex_match(c.name, m, warpName))
                warps.push_back({std::stoi(m[1]), c.matrix.apply(c.localBounds())});
        });
    uint16_t backSprite = back && back->definition() ? back->definition()->id : 0;

    for (bool atRight : {true, false}) {
        double edge = atRight ? right : left;
        double outer = atRight ? outerRight : outerLeft;
        const char* side = atRight ? "right" : "left";
        int depthBack = 16384 - 100 + atRight, depthFront = 16384 + 1000 + atRight;  // below BackGFX, above all
        // A warp box at this end leading the same way into another course part.
        const EdgeLink* link = nullptr;
        Rect box;
        for (const Warp& w : warps) {
            double x0 = w.box.xMin / 20, x1 = w.box.xMax / 20;
            bool atEdge = atRight ? x1 >= edge - 20 && (x0 + x1) / 2 > edge - 250
                                  : x0 <= edge + 20 && (x0 + x1) / 2 < edge + 250;
            if (!atEdge) continue;
            for (const EdgeLink& l : edgeLinks())
                if (l.sprite == backSprite && l.warp == w.number && l.right == atRight) link = &l, box = w.box;
            if (link) break;
        }
        if (link) {
            std::string part = link->course;
            std::vector<Placed> nextTerrain = partShapes(movie, part + "FrontGFX");
            std::vector<Placed> nextBack = partShapes(movie, part + "BackGFX");
            if (nextTerrain.empty()) nextTerrain = std::move(nextBack), nextBack.clear();
            std::optional<double> dy;
            double dx = 0, seam = edge;
            if (!nextTerrain.empty()) {
                auto [nextLeft, nextRight] = xExtent(nextTerrain);
                dx = atRight ? right - nextLeft : left - nextRight;  // its far end against this one
                if (link->keepsY) {
                    // Mario's level-local y there is his level-local y here plus yShift.
                    dy = -link->yShift;
                } else {
                    // Line up the drawn ground where the two meet: the top of this terrain in
                    // its last column below the warp box's top (the box can stop above the
                    // floor), and the top of the next terrain in its first column below the
                    // height Mario starts at. (The collision shapes have invisible walls at
                    // the ends, so they can't be used.)
                    double xHere = atRight ? right - 12 : left + 12;
                    double xThere = atRight ? nextLeft + 12 : nextRight - 12;
                    auto here = surface(terrain, xHere, box.yMin / 20, box.yMax / 20 + 400);
                    auto there = std::isfinite(link->spawnY) ? surface(nextTerrain, xThere, link->spawnY - 60, link->spawnY + 1500)
                                                              : surface(nextTerrain, xThere, -5000, 5000);
                    // Then where each floor actually ends and begins (the terrain's bounds can
                    // reach past it): this one's ground body, below its rounded end cap, and
                    // the next one's top, so the next floor starts right where this one ends.
                    std::optional<double> endHere, startThere;
                    if (here && there) {
                        endHere = firstSolid(terrain, *here + 40, edge, atRight ? -1 : 1, 300);
                        startThere = firstSolid(nextTerrain, *there + 6, atRight ? nextLeft : nextRight, atRight ? 1 : -1, 300);
                    }
                    if (endHere && startThere) {
                        double inHere = atRight ? -20 : 20;  // inside the floors, past rounded end caps
                        auto floorHere = surface(terrain, *endHere + inHere, *here - 120, *here + 60);
                        auto floorThere = surface(nextTerrain, *startThere - inHere, *there - 120, *there + 60);
                        // (A floor ending well inside the terrain is a door in a room, not
                        // the way out at its end.)
                        if (floorHere && floorThere && std::fabs(*endHere - edge) <= 40) {
                            dx = *endHere - *startThere + (atRight ? 1 : -1);  // overlap a pixel
                            dy = *floorHere - *floorThere;
                            seam = *endHere;
                        }
                    }
                }
            }
            if (dy) {
                Matrix place = Matrix::translate(dx * 20, *dy * 20);
                seam = atRight ? std::max(seam, outer) : std::min(seam, outer);
                addHolder(depthBack, std::string("PortEdgeBack_") + side, seam, atRight, nextBack, place, back);
                addHolder(depthFront, std::string("PortEdgeFront_") + side, seam, atRight, nextTerrain, place, frontPart);
                std::printf("edge fill: %s end at x %.0f shows %s from x %.0f (moved %.0f, %.0f)\n", side, edge, link->course,
                            seam, dx, *dy);
                continue;
            }
        }
        // No course part beyond: each layer mirrored about its own last column, shown beyond
        // everything the level draws. Background shapes count when they reach that column
        // (walls and hills behind the terrain).
        double backEdge = atRight ? backRight : backLeft;
        std::vector<Placed> mirroredBack;
        if (!terrainInBack)
            for (const Placed& p : backShapes) {
                Rect b = p.bounds();
                if (atRight ? b.xMax / 20 >= backEdge - 60 : b.xMin / 20 <= backEdge + 60) mirroredBack.push_back(p);
            }
        addHolder(depthBack, std::string("PortEdgeBack_") + side, outer, atRight, mirroredBack,
                  Matrix{-1, 0, 0, 1, 2 * backEdge * 20, 0}, back);
        addHolder(depthFront, std::string("PortEdgeFront_") + side, outer, atRight, terrain,
                  Matrix{-1, 0, 0, 1, 2 * edge * 20, 0}, frontPart);
        std::printf("edge fill: %s end at x %.0f mirrored%s\n", side, edge, link ? " (next part not lined up)" : "");
    }
}
