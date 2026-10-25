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

// Horizontal extent (pixels) of what a shape actually draws: its fill outlines (curves at
// their extremes, not their control points) and strokes (with their width), which can stop
// short of its declared bounds.
std::pair<double, double> drawnExtent(const Placed& p) {
    double x0 = INFINITY, x1 = -INFINITY;
    auto add = [&](flash::Point q, double pad) {
        double x = p.m.apply(q).x / 20;
        x0 = std::min(x0, x - pad);
        x1 = std::max(x1, x + pad);
    };
    auto addEdge = [&](const flash::Edge& e, double pad) {
        add(e.from, pad);
        add(e.to, pad);
        if (!e.curved) return;
        for (int axis = 0; axis < 2; axis++) {  // extremes of the quadratic in x and y
            double a = axis ? e.from.y : e.from.x, b = axis ? e.ctrl.y : e.ctrl.x, c = axis ? e.to.y : e.to.x;
            double d = a - 2 * b + c;
            if (d == 0) continue;
            double t = (a - b) / d;
            if (t <= 0 || t >= 1) continue;
            double u = 1 - t;
            add({u * u * e.from.x + 2 * u * t * e.ctrl.x + t * t * e.to.x,
                 u * u * e.from.y + 2 * u * t * e.ctrl.y + t * t * e.to.y},
                pad);
        }
    };
    const flash::ShapeDef& def = p.shape->def;
    double scale = std::sqrt(std::fabs(p.m.a * p.m.d - p.m.b * p.m.c));
    for (const auto& layer : def.layers) {
        for (const flash::FillPath& fp : layer.fills)
            for (const flash::Edge& e : fp.edges) addEdge(e, 0);
        for (const flash::StrokePath& sp : layer.strokes) {
            double pad = sp.style >= 0 && sp.style < (int)def.lines.size() ? def.lines[sp.style].width * scale / 40 : 0;
            for (const auto& line : sp.polylines)
                for (const flash::Edge& e : line) addEdge(e, pad);
        }
    }
    return {x0, x1};
}

// Horizontal extent (pixels) of what a set of shapes draws.
std::pair<double, double> xExtent(const std::vector<Placed>& shapes) {
    double x0 = INFINITY, x1 = -INFINITY;
    for (const Placed& p : shapes) {
        auto [a, b] = drawnExtent(p);
        x0 = std::min(x0, a);
        x1 = std::max(x1, b);
    }
    return {x0, x1};
}

// Small shapes (arrow signs, posts, markers) aren't ground: they are left out of mirrors.
// Platforms and bridges are long, terrain, hills and clouds big.
bool groundLike(const Placed& p) {
    Rect b = p.bounds();
    double w = (b.xMax - b.xMin) / 20, h = (b.yMax - b.yMin) / 20;
    return std::max(w, h) >= 160 || std::min(w, h) >= 50;
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

// A clip in the course holding each shape mirrored about where it ends at that side (a
// hair inside, so the copy overlaps the original instead of both half-covering the seam's
// pixels), each masked to beyond its own end.
std::shared_ptr<DisplayObject> makeHolder(MovieClip& course, int depth, const std::string& name, bool right,
                                          const std::vector<Placed>& shapes) {
    auto holder = course.createEmpty(depth, name);
    auto* clip = dynamic_cast<MovieClip*>(holder.get());
    if (!clip) return nullptr;
    int d = 1;
    for (const Placed& p : shapes) {
        auto [x0, x1] = drawnExtent(p);
        double axis = right ? x1 - 0.5 : x0 + 0.5;
        auto mask = clip->attach(maskShape(), d, "");
        mask->matrix = Matrix::translate(axis * 20, 0) * Matrix::scale(right ? 1 : -1, 1);
        mask->clipDepth = d + 1;  // masks the copy right above it
        auto obj = clip->attach(const_cast<ShapeCharacter*>(p.shape), d + 1, "");
        if (obj) obj->matrix = Matrix{-1, 0, 0, 1, 2 * axis * 20, 0} * p.m;
        d += 2;
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
    // Only ground-like shapes are mirrored (not arrow signs or other small markers).
    auto groundOf = [](const std::vector<Placed>& shapes) {
        std::vector<Placed> out;
        for (const Placed& p : shapes)
            if (groundLike(p)) out.push_back(p);
        return out;
    };
    std::vector<Placed> frontGround = groundOf(terrain), backGround = terrainInBack ? std::vector<Placed>{} : groundOf(backShapes);
    MovieClip* frontPart = terrainInBack || !front ? back : front;
    for (bool atRight : {true, false}) {
        const char* side = atRight ? "right" : "left";
        // Each shape is mirrored about where its own drawing ends (the grass can reach
        // further than the ground under it). Background shapes count when they reach the
        // background's end (walls and hills behind the terrain). Both layers' copies sit
        // below BackGFX: whatever the level itself draws beyond an end stays in front.
        struct Layer {
            int depth;
            const char* name;
            std::vector<Placed> shapes;
            MovieClip* part;
        };
        // Only shapes reaching the layer's end at this side (a bridge in the middle of the
        // level isn't extended).
        auto nearEnd = [&](const std::vector<Placed>& shapes, double reach) {
            std::vector<Placed> out;
            if (shapes.empty()) return out;
            auto [l0, l1] = xExtent(shapes);
            for (const Placed& p : shapes) {
                auto [x0, x1] = drawnExtent(p);
                if (atRight ? x1 >= l1 - reach : x0 <= l0 + reach) out.push_back(p);
            }
            return out;
        };
        for (const Layer& l : {Layer{16384 - 100 + atRight, "PortEdgeBack_", nearEnd(backGround, 60), back},
                               Layer{16384 - 90 + atRight, "PortEdgeFront_", nearEnd(frontGround, 100), frontPart}}) {
            if (l.shapes.empty() || !l.part) continue;
            if (auto holder = makeHolder(course, l.depth, std::string(l.name) + side, atRight, l.shapes))
                holders_.push_back({holder, l.part->weak_from_this()});
        }
    }
}
