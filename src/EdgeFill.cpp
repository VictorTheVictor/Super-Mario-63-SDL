#include "EdgeFill.h"

#include <cmath>

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

// A shape and its matrix (twips) in level-local space.
struct Placed {
    const ShapeCharacter* shape;
    Matrix m;
    Rect bounds() const { return m.apply(shape->def.bounds); }
};

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

// Castle rooms and other indoor courses (in Bowser's castle, the Bowser trap) end in walls,
// not open ground: they get no fill. Known by name and by their far background.
bool indoor(const std::string& course, const std::string& farBackground) {
    return course.rfind("C-", 0) == 0 || course.rfind("Castle", 0) == 0 || farBackground == "CastleColoredFarBG" ||
           farBackground == "BowsersTrap1BG" || farBackground == "C-13FarBG" || farBackground == "Black-BG";
}

}  // namespace

void EdgeFill::update(as::Player& player, bool visible) {
    auto* course = dynamic_cast<MovieClip*>(as::resolvePath(&player.root(), "_root.Course"));
    if (!course) return;
    // Built a few ticks after the course part appears, once its parts have settled on the
    // frame its scripts pick. A transition covers the screen then.
    if (course_.lock().get() != course) {
        if (pending_.lock().get() != course) {
            pending_ = course->weak_from_this();
            pendingTicks_ = 0;
            holders_.clear();
        }
        if (++pendingTicks_ >= 4) {
            as::Value root = as::valueOf(&player.root());
            std::string name = as::getMember(root, as::intern("playingcourse")).toString();
            std::string far = as::getMember(root, as::intern(name + "FarBackground")).toString();
            if (indoor(name, far)) course_ = course->weak_from_this();
            else build(*course);
        }
    }
    // Each fill moves with the part it extends (the camera scrolls the parts), including
    // where it was last tick for in-between frames, and takes its tint.
    for (Holder& h : holders_) {
        auto holder = h.holder.lock();
        auto part = h.part.lock();
        if (!holder || !part) continue;
        holder->visible = visible;
        holder->matrix = part->matrix;
        holder->prevMatrix = part->prevMatrix;
        holder->hasPrevMatrix = part->hasPrevMatrix;
        holder->cxform = part->cxform;
    }
}

void EdgeFill::build(MovieClip& course) {
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
    MovieClip* frontPart = terrainInBack || !front ? back : front;
    for (bool atRight : {true, false}) {
        const char* side = atRight ? "right" : "left";
        double edge = atRight ? right : left;
        double backEdge = atRight ? backRight : backLeft;
        double outer = atRight ? std::max(edge, backEdge) : std::min(edge, backEdge);
        // Each layer mirrored about its own last column. Background shapes count when they
        // reach that column (walls and hills behind the terrain).
        std::vector<Placed> mirroredBack;
        if (!terrainInBack)
            for (const Placed& p : backShapes) {
                Rect b = p.bounds();
                if (atRight ? b.xMax / 20 >= backEdge - 60 : b.xMin / 20 <= backEdge + 60) mirroredBack.push_back(p);
            }
        // Back fills below BackGFX, front ones above everything in the course.
        struct Layer {
            int depth;
            const char* name;
            const std::vector<Placed>& shapes;
            double axis;
            MovieClip* part;
        };
        for (const Layer& l : {Layer{16384 - 100 + atRight, "PortEdgeBack_", mirroredBack, backEdge, back},
                               Layer{16384 + 1000 + atRight, "PortEdgeFront_", terrain, edge, frontPart}}) {
            if (l.shapes.empty() || !l.part) continue;
            Matrix mirror{-1, 0, 0, 1, 2 * l.axis * 20, 0};
            if (auto holder = makeHolder(course, l.depth, std::string(l.name) + side, outer, atRight, l.shapes, mirror))
                holders_.push_back({holder, l.part->weak_from_this()});
        }
    }
}
