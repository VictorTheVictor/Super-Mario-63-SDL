#include "flash/DisplayObject.h"

#include <cmath>

#include "flash/Effects.h"
#include "flash/Morph.h"
#include "flash/Text.h"

namespace flash {

TimelineHooks* timelineHooks = nullptr;

std::shared_ptr<DisplayObject> createDisplayObject(const Movie& movie, Character* def) {
    if (!def) return nullptr;
    switch (def->kind) {
        case Character::Sprite:
            return std::make_shared<MovieClip>(movie, static_cast<Sprite*>(def));
        case Character::Shape:
            return std::make_shared<ShapeObject>(def);
        case Character::MorphShape:
            return std::make_shared<MorphObject>(def);
        case Character::Text:
            return std::make_shared<StaticTextObject>(def);
        case Character::Button:
            return std::make_shared<ButtonObject>(movie, static_cast<ButtonCharacter*>(def));
        case Character::EditText:
            return std::make_shared<TextField>(movie, static_cast<EditTextCharacter*>(def));
        default:
            return std::make_shared<DisplayObject>(def);
    }
}

// ---------------------------------------------------------------- DisplayObject

const DisplayObject::Transform& DisplayObject::transform() {
    if (!transformValid_) {
        const Matrix& m = matrix;
        transform_.scaleX = std::sqrt(m.a * m.a + m.b * m.b);
        transform_.scaleY = std::sqrt(m.c * m.c + m.d * m.d);
        transform_.rotation = std::atan2(m.b, m.a);
        transform_.skew = std::atan2(-m.c, m.d) - transform_.rotation;
        transformValid_ = true;
    }
    return transform_;
}

void DisplayObject::setTransform(const Transform& t) {
    transform_ = t;
    transformValid_ = true;
    double r = t.rotation, rs = t.rotation + t.skew;
    matrix.a = t.scaleX * std::cos(r);
    matrix.b = t.scaleX * std::sin(r);
    matrix.c = -t.scaleY * std::sin(rs);
    matrix.d = t.scaleY * std::cos(rs);
}

Matrix DisplayObject::worldMatrix() const {
    Matrix m = matrix;
    for (const DisplayObject* p = parent_; p; p = p->parent_) m = p->matrix * m;
    return m;
}

// ---------------------------------------------------------------- shapes

void ShapeObject::render(Rasterizer& r, Surface& dst, const IntRect& clip, const Matrix& m,
                         const ColorTransform& cx, const Movie& movie) {
    drawShape(r, dst, clip, static_cast<ShapeCharacter*>(def_)->def, m, cx, movie);
}

Rect ShapeObject::localBounds() const { return static_cast<ShapeCharacter*>(def_)->def.bounds; }

bool ShapeObject::hitTest(const Matrix& m, Point p) const {
    if (!m.invertible()) return false;
    return shapeContains(static_cast<ShapeCharacter*>(def_)->def, m.inverse().apply(p));
}

void MorphObject::render(Rasterizer& r, Surface& dst, const IntRect& clip, const Matrix& m,
                         const ColorTransform& cx, const Movie& movie) {
    drawShape(r, dst, clip, static_cast<MorphShapeCharacter*>(def_)->at(ratio), m, cx, movie);
}

Rect MorphObject::localBounds() const {
    auto* mc = static_cast<MorphShapeCharacter*>(def_);
    double t = ratio / 65535.0;
    auto l = [t](double a, double b) { return a + (b - a) * t; };
    return {l(mc->startBounds.xMin, mc->endBounds.xMin), l(mc->startBounds.yMin, mc->endBounds.yMin),
            l(mc->startBounds.xMax, mc->endBounds.xMax), l(mc->startBounds.yMax, mc->endBounds.yMax)};
}

bool MorphObject::hitTest(const Matrix& m, Point p) const {
    if (!m.invertible()) return false;
    return shapeContains(static_cast<MorphShapeCharacter*>(def_)->at(ratio), m.inverse().apply(p));
}

Rect unionRect(const Rect& a, const Rect& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return {std::min(a.xMin, b.xMin), std::min(a.yMin, b.yMin), std::max(a.xMax, b.xMax),
            std::max(a.yMax, b.yMax)};
}

void renderDisplayObject(DisplayObject& o, Rasterizer& r, Surface& dst, const IntRect& clip,
                         const Matrix& parentMatrix, const ColorTransform& parentCx,
                         const Movie& movie) {
    if (!o.visible) return;
    Matrix m = parentMatrix * o.matrix;
    ColorTransform cx = parentCx * o.cxform;
    bool needsLayer = !o.filters.empty() || o.blendMode >= BlendLayer;
    if (!needsLayer) {
        o.render(r, dst, clip, m, cx, movie);
        return;
    }
    // Render the object's content without its color transform into an offscreen layer,
    // filter it, then composite it with the color transform and blend mode. The color
    // transform therefore also affects glows and shadows, as in Flash.
    int pad = filterPadding(o.filters);
    Rect b = m.apply(o.localBounds());
    if (b.empty() || !std::isfinite(b.xMin) || !std::isfinite(b.yMin) || !std::isfinite(b.xMax) ||
        !std::isfinite(b.yMax))
        return;
    IntRect area{(int)std::floor(b.xMin) - pad, (int)std::floor(b.yMin) - pad,
                 (int)std::ceil(b.xMax) + pad, (int)std::ceil(b.yMax) + pad};
    area.x0 = std::max(area.x0, clip.x0 - pad);
    area.y0 = std::max(area.y0, clip.y0 - pad);
    area.x1 = std::min(area.x1, clip.x1 + pad);
    area.y1 = std::min(area.y1, clip.y1 + pad);
    if (area.empty()) return;
    Surface layer;
    layer.resize(area.x1 - area.x0, area.y1 - area.y0);
    o.render(r, layer, {0, 0, layer.width, layer.height},
             Matrix::translate(-area.x0, -area.y0) * m, ColorTransform{}, movie);
    applyFilters(layer, o.filters);
    compositeLayer(dst, clip, layer, area.x0, area.y0, o.blendMode ? o.blendMode : (uint8_t)BlendNormal, cx);
}

// ---------------------------------------------------------------- Button

ButtonObject::ButtonObject(const Movie& movie, ButtonCharacter* def)
    : DisplayObject(def), movie_(movie) {
    hitChildren_ = build(ButtonCharacter::HitTest);
    setState(ButtonCharacter::Up);
}

std::map<int, std::shared_ptr<DisplayObject>> ButtonObject::build(uint8_t state) const {
    std::map<int, std::shared_ptr<DisplayObject>> out;
    for (const auto& rec : static_cast<ButtonCharacter*>(def_)->records) {
        if (!(rec.states & state)) continue;
        auto obj = createDisplayObject(movie_, movie_.character(rec.characterId));
        if (!obj) continue;
        obj->matrix = rec.matrix;
        obj->cxform = rec.cxform;
        obj->filters = rec.filters;
        obj->blendMode = rec.blendMode;
        obj->depth = rec.depth;
        if (auto* mc = dynamic_cast<MovieClip*>(obj.get())) mc->initialize();
        out[rec.depth] = obj;
    }
    return out;
}

void ButtonObject::setState(uint8_t state) {
    if (state == state_) return;
    state_ = state;
    children_ = build(state);
}

void ButtonObject::render(Rasterizer& r, Surface& dst, const IntRect& clip, const Matrix& m,
                          const ColorTransform& cx, const Movie& movie) {
    for (auto& [d, c] : children_) renderDisplayObject(*c, r, dst, clip, m, cx, movie);
}

Rect ButtonObject::localBounds() const {
    Rect out;
    for (auto& [d, c] : children_) out = unionRect(out, c->matrix.apply(c->localBounds()));
    return out;
}

bool ButtonObject::hitTest(const Matrix& m, Point p) const {
    if (!m.invertible()) return false;
    for (auto& [d, c] : hitChildren_)
        if (c->hitTest(m * c->matrix, p)) return true;
    return false;
}

void ButtonObject::forEachChild(const std::function<void(DisplayObject&)>& fn) {
    for (auto& [d, c] : children_) fn(*c);
}

void ButtonObject::advance() {
    for (auto& [d, c] : children_) c->advance();
}

// ---------------------------------------------------------------- Drawing

void Drawing::clear() { *this = Drawing{}; }

bool Drawing::empty() const {
    return shape.layers.empty() && fillEdges.empty() && lineEdges.empty();
}

void Drawing::flushLine() {
    if (lineEdges.empty()) return;
    ShapeDef::Layer layer;
    shape.lines.push_back(line);
    layer.strokes.push_back({(int)shape.lines.size() - 1, {lineEdges}});
    shape.layers.push_back(std::move(layer));
    lineEdges.clear();
}

void Drawing::lineStyle(double widthTwips, Color c) {
    flushLine();
    lining = widthTwips >= 0;
    line = LineStyle{};
    line.width = std::max(0.0, widthTwips);
    line.color = c;
}

void Drawing::beginFill(Color c) {
    endFill();
    filling = true;
    fill = FillStyle{};
    fill.color = c;
    fillStart = pen;
}

void Drawing::endFill() {
    if (filling && !fillEdges.empty()) {
        if (pen.x != fillStart.x || pen.y != fillStart.y)
            fillEdges.push_back({pen, {(pen.x + fillStart.x) / 2, (pen.y + fillStart.y) / 2}, fillStart, false});
        flushLine();
        ShapeDef::Layer layer;
        shape.fills.push_back(fill);
        layer.fills.push_back({(int)shape.fills.size() - 1, fillEdges});
        shape.layers.push_back(std::move(layer));
    }
    fillEdges.clear();
    filling = false;
}

void Drawing::moveTo(Point p) {
    flushLine();
    pen = p;
    if (filling && fillEdges.empty()) fillStart = p;
}

void Drawing::lineTo(Point p) {
    Edge e{pen, {(pen.x + p.x) / 2, (pen.y + p.y) / 2}, p, false};
    if (filling) fillEdges.push_back(e);
    if (lining) lineEdges.push_back(e);
    pen = p;
}

void Drawing::curveTo(Point c, Point p) {
    Edge e{pen, c, p, true};
    if (filling) fillEdges.push_back(e);
    if (lining) lineEdges.push_back(e);
    pen = p;
}

ShapeDef Drawing::snapshot() const {
    Drawing copy = *this;
    copy.endFill();
    copy.flushLine();
    ShapeDef s = copy.shape;
    Rect b;
    for (auto& l : s.layers) {
        for (auto& f : l.fills)
            for (auto& e : f.edges) {
                // Grow by points (not per-edge rects): axis-aligned edges have zero area.
                for (Point q : {e.from, e.to, e.ctrl}) {
                    if (b.xMin == 0 && b.xMax == 0 && b.yMin == 0 && b.yMax == 0) b = {q.x, q.y, q.x, q.y};
                    b.xMin = std::min(b.xMin, q.x);
                    b.yMin = std::min(b.yMin, q.y);
                    b.xMax = std::max(b.xMax, q.x);
                    b.yMax = std::max(b.yMax, q.y);
                }
            }
        for (auto& st : l.strokes)
            for (auto& pl : st.polylines)
                for (auto& e : pl) b = unionRect(b, {std::min(e.from.x, e.to.x) - 20, std::min(e.from.y, e.to.y) - 20, std::max(e.from.x, e.to.x) + 20, std::max(e.from.y, e.to.y) + 20});
    }
    s.bounds = b;
    return s;
}

// ---------------------------------------------------------------- MovieClip

MovieClip::MovieClip(const Movie& movie, Sprite* sprite)
    : DisplayObject(sprite), movie_(movie), sprite_(sprite) {}

Drawing& MovieClip::drawing() {
    if (!drawing_) drawing_ = std::make_unique<Drawing>();
    return *drawing_;
}

void MovieClip::initialize() {
    if (currentFrame_ == 0) {
        currentFrame_ = 1;
        runFrame(1, true);
    }
}

DisplayObject* MovieClip::childAt(int depth) const {
    auto it = children_.find(depth);
    return it == children_.end() ? nullptr : it->second.get();
}

DisplayObject* MovieClip::childByName(const std::string& n) const {
    for (auto& [d, c] : children_)
        if (c->name == n) return c.get();
    return nullptr;
}

void MovieClip::setChild(int depth, std::shared_ptr<DisplayObject> obj) {
    auto it = children_.find(depth);
    if (it != children_.end() && it->second != obj) {
        if (timelineHooks) timelineHooks->onRemoved(*it->second);
        it->second->parent_ = nullptr;
    }
    if (!obj) {
        if (it != children_.end()) children_.erase(it);
        return;
    }
    obj->parent_ = this;
    obj->depth = depth;
    children_[depth] = std::move(obj);
}

void MovieClip::adopt(int depth, std::shared_ptr<DisplayObject> obj, const PlaceCommand* place) {
    setChild(depth, obj);
    if (timelineHooks) timelineHooks->onCreated(*obj, place, placeIndex(place));
    if (auto* mc = dynamic_cast<MovieClip*>(obj.get())) mc->initialize();
}

uint32_t MovieClip::placeIndex(const PlaceCommand* p) const {
    return p ? (uint32_t)(p - sprite_->places.data()) : 0;
}

std::shared_ptr<DisplayObject> MovieClip::instantiate(uint16_t characterId) {
    return createDisplayObject(movie_, movie_.character(characterId));
}

std::shared_ptr<DisplayObject> MovieClip::attach(Character* def, int depth, const std::string& n) {
    auto obj = createDisplayObject(movie_, def);
    if (!obj) return nullptr;
    obj->name = n;
    obj->placedByTimeline = false;
    adopt(depth, obj, nullptr);
    return obj;
}

std::shared_ptr<DisplayObject> MovieClip::createEmpty(int depth, const std::string& n) {
    static Sprite empty;  // one frame, no commands
    if (empty.frames.empty()) empty.frames.resize(1);
    return attach(&empty, depth, n);
}

void MovieClip::removeChild(int depth) { setChild(depth, nullptr); }

void MovieClip::swapChildDepth(int from, int to) {
    if (from == to) return;
    auto a = children_.find(from);
    if (a == children_.end()) return;
    std::shared_ptr<DisplayObject> moving = a->second;
    children_.erase(a);
    auto b = children_.find(to);
    if (b != children_.end()) {
        std::shared_ptr<DisplayObject> other = b->second;
        other->depth = from;
        other->placedByTimeline = false;
        children_[from] = other;
    }
    moving->depth = to;
    moving->placedByTimeline = false;
    children_[to] = moving;
}

static void applyProps(DisplayObject& o, const PlaceCommand& p, bool keepTransform) {
    if (!keepTransform) {
        if (p.hasMatrix) {
            o.matrix = p.matrix;
            o.invalidateTransform();
        }
        if (p.hasCxform) o.cxform = p.cxform;
    }
    if (p.hasRatio) o.ratio = p.ratio;
    if (p.hasName) o.name = p.name;
    if (p.hasClipDepth) o.clipDepth = p.clipDepth;
    if (p.hasFilters) o.filters = p.filters;
    if (p.hasBlendMode) o.blendMode = p.blendMode;
    if (p.hasVisible) o.visible = p.visible;
}

void MovieClip::applyPlace(const PlaceCommand& p) {
    DisplayObject* existing = childAt(p.depth);
    if (p.hasCharacter && (!p.move || !existing)) {
        if (existing && !existing->placedByTimeline) return;  // depth owned by script
        auto obj = instantiate(p.characterId);
        if (!obj) return;
        applyProps(*obj, p, false);
        adopt(p.depth, obj, &p);
        return;
    }
    if (!existing || !existing->placedByTimeline) return;
    if (p.hasCharacter && existing->definition() &&
        existing->definition()->id != p.characterId) {
        // Character swap keeps the existing transform unless the command overrides it.
        auto obj = instantiate(p.characterId);
        if (!obj) return;
        obj->matrix = existing->matrix;
        obj->cxform = existing->cxform;
        obj->name = existing->name;
        obj->clipDepth = existing->clipDepth;
        applyProps(*obj, p, false);
        adopt(p.depth, obj, &p);
        return;
    }
    applyProps(*existing, p, existing->transformedByScript);
}

void MovieClip::runFrame(int frame, bool runActions) {
    if (frame < 1 || frame > (int)sprite_->frames.size()) return;
    for (const FrameCommand& c : sprite_->frames[frame - 1]) {
        switch (c.kind) {
            case FrameCommand::Place:
                applyPlace(sprite_->places[c.index]);
                break;
            case FrameCommand::Remove: {
                DisplayObject* o = childAt((int)c.index);
                if (o && o->placedByTimeline) removeChild((int)c.index);
                break;
            }
            case FrameCommand::Action:
                if (runActions && timelineHooks) timelineHooks->onFrameAction(*this, c.index);
                break;
            case FrameCommand::StartSound:
                if (runActions && timelineHooks)
                    timelineHooks->onStartSound(*this, sprite_->sounds[c.index]);
                break;
            case FrameCommand::StreamBlock:
                if (runActions && timelineHooks) timelineHooks->onStreamBlock(*this, c.index);
                break;
        }
    }
}

void MovieClip::gotoFrame(int frame, bool andPlay) {
    playing = andPlay;
    int total = std::max<int>(1, sprite_->frameCount);
    frame = std::clamp(frame, 1, total);
    if (frame == currentFrame_) return;

    if (frame > currentFrame_) {
        // Intermediate frames' display commands run; only the destination's scripts do.
        for (int f = currentFrame_ + 1; f <= frame; f++) runFrame(f, f == frame);
        currentFrame_ = frame;
        return;
    }

    // Rewind: rebuild the timeline state for frames 1..frame, then reconcile with the
    // live display list so that objects that persist keep their instance (and state).
    std::map<int, PlaceState> target;
    for (int f = 1; f <= frame; f++) {
        for (const FrameCommand& c : sprite_->frames[f - 1]) {
            if (c.kind == FrameCommand::Remove) {
                target.erase((int)c.index);
                continue;
            }
            if (c.kind != FrameCommand::Place) continue;
            const PlaceCommand& p = sprite_->places[c.index];
            auto it = target.find(p.depth);
            if (p.hasCharacter && (!p.move || it == target.end() ||
                                   p.characterId != it->second.characterId)) {
                PlaceState s = it != target.end() ? it->second : PlaceState{};
                s.place = &p;
                s.characterId = p.characterId;
                target[p.depth] = s;
                it = target.find(p.depth);
            } else if (it == target.end()) {
                continue;
            }
            PlaceState& s = it->second;
            if (p.hasMatrix) s.matrix = p.matrix;
            if (p.hasCxform) s.cxform = p.cxform;
            if (p.hasRatio) s.ratio = p.ratio;
            if (p.hasName) s.name = p.name;
            if (p.hasClipDepth) s.clipDepth = p.clipDepth;
            if (p.hasFilters) s.filters = p.filters;
            if (p.hasBlendMode) s.blendMode = p.blendMode;
            if (p.hasVisible) s.visible = p.visible;
        }
    }

    // Remove timeline objects that do not survive.
    std::vector<int> drop;
    for (auto& [d, c] : children_) {
        if (!c->placedByTimeline) continue;
        auto it = target.find(d);
        if (it == target.end() || !c->definition() ||
            c->definition()->id != it->second.characterId)
            drop.push_back(d);
    }
    for (int d : drop) removeChild(d);

    for (auto& [d, s] : target) {
        DisplayObject* existing = childAt(d);
        if (existing && !existing->placedByTimeline) continue;  // depth owned by script
        auto apply = [&s](DisplayObject& o, bool keepTransform) {
            if (!keepTransform) {
                o.matrix = s.matrix;
                o.cxform = s.cxform;
                o.invalidateTransform();
            }
            o.ratio = s.ratio;
            o.name = s.name;
            o.clipDepth = s.clipDepth;
            o.filters = s.filters;
            o.blendMode = s.blendMode;
            o.visible = s.visible;
        };
        if (existing) {
            apply(*existing, existing->transformedByScript);
            continue;
        }
        auto created = instantiate(s.characterId);
        if (!created) continue;
        apply(*created, false);
        adopt(d, created, s.place);
    }
    currentFrame_ = frame;
    // The destination frame's scripts run again after a rewind.
    for (const FrameCommand& c : sprite_->frames[frame - 1])
        if (c.kind == FrameCommand::Action && timelineHooks) timelineHooks->onFrameAction(*this, c.index);
}

bool MovieClip::gotoLabel(const std::string& label, bool andPlay) {
    auto it = sprite_->labels.find(label);
    if (it == sprite_->labels.end()) return false;
    gotoFrame(it->second, andPlay);
    return true;
}

void MovieClip::runFrameSelf() {
    if (!playing || sprite_->frameCount <= 1) return;
    if (currentFrame_ >= sprite_->frameCount) gotoFrame(1, true);
    else {
        currentFrame_++;
        runFrame(currentFrame_, true);
    }
}

void MovieClip::advance() {
    // Snapshot first: clips created by this frame's commands start on frame 1 and
    // do not advance until the next tick.
    std::vector<std::shared_ptr<DisplayObject>> existing;
    existing.reserve(children_.size());
    for (auto& [d, c] : children_) existing.push_back(c);
    runFrameSelf();
    for (auto& c : existing)
        if (c->parent() == this) c->advance();
}

Rect MovieClip::localBounds() const {
    Rect out;
    if (hasDrawing()) out = drawing_->snapshot().bounds;
    for (auto& [d, c] : children_) out = unionRect(out, c->matrix.apply(c->localBounds()));
    return out;
}

bool MovieClip::hitTest(const Matrix& m, Point p) const {
    // A zero-scale clip has no area; Flash reports no hit (and its children inherit that).
    if (!m.invertible()) return false;
    if (hasDrawing() && shapeContains(drawing_->snapshot(), m.inverse().apply(p))) return true;
    // Invisible children still count: games commonly hit-test hidden collision clips.
    // Masks (clipDepth) are not hit themselves, but limit the siblings they cover.
    int maskedUntil = -1;
    bool maskHit = true;
    for (auto& [d, c] : children_) {
        if (c->clipDepth) {
            maskedUntil = c->clipDepth;
            maskHit = c->hitTest(m * c->matrix, p);
            continue;
        }
        if (d <= maskedUntil && !maskHit) continue;
        if (c->hitTest(m * c->matrix, p)) return true;
    }
    return false;
}

bool MovieClip::maskBlocks(int depth, const Matrix& m, Point p) const {
    for (auto& [d, c] : children_) {
        if (d >= depth) break;
        if (c->clipDepth && depth <= c->clipDepth && !c->hitTest(m * c->matrix, p)) return true;
    }
    return false;
}

void MovieClip::forEachChild(const std::function<void(DisplayObject&)>& fn) {
    for (auto& [d, c] : children_) fn(*c);
}

void MovieClip::render(Rasterizer& r, Surface& dst, const IntRect& clip, const Matrix& m,
                       const ColorTransform& cx, const Movie& movie) {
    if (hasDrawing()) drawShape(r, dst, clip, drawing_->snapshot(), m, cx, movie);
    for (auto it = children_.begin(); it != children_.end(); ++it) {
        DisplayObject& c = *it->second;
        if (!c.clipDepth) {
            renderDisplayObject(c, r, dst, clip, m, cx, movie);
            continue;
        }
        // Mask: covers siblings with depth in (c.depth, c.clipDepth]. Render the mask's
        // coverage and the masked content into stage-sized layers, then combine.
        Surface mask, content;
        mask.resize(dst.width, dst.height);
        content.resize(dst.width, dst.height);
        bool wasVisible = c.visible;
        c.visible = true;  // invisible masks still mask
        renderDisplayObject(c, r, mask, clip, m, ColorTransform{}, movie);
        c.visible = wasVisible;
        auto next = std::next(it);
        while (next != children_.end() && next->first <= c.clipDepth) {
            renderDisplayObject(*next->second, r, content, clip, m, cx, movie);
            it = next++;
        }
        applyMask(content, mask);
        compositeLayer(dst, clip, content, 0, 0, BlendNormal, ColorTransform{});
    }
}

}  // namespace flash
