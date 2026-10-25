#include "flash/DisplayObject.h"

#include <atomic>
#include <cmath>
#include <unordered_map>

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

namespace {

// The object's filters with sizes converted from stage pixels to device pixels.
std::vector<Filter> deviceFilters(const DisplayObject& o) {
    std::vector<Filter> filters = o.filters;
    for (Filter& f : filters) {
        f.blurX *= renderSettings.scale;
        f.blurY *= renderSettings.scale;
        f.distance *= renderSettings.scale;
    }
    return filters;
}

// The device-space rect an object's layer needs within `limit`, including the filters'
// reach (`pad`). Empty when there is nothing to draw.
IntRect layerArea(const DisplayObject& o, const Matrix& m, int pad, const IntRect& limit) {
    Rect b = m.apply(o.localBounds());
    if (b.empty() || !std::isfinite(b.xMin) || !std::isfinite(b.yMin) || !std::isfinite(b.xMax) ||
        !std::isfinite(b.yMax))
        return {};
    return {std::max((int)std::floor(b.xMin) - pad, limit.x0 - pad),
            std::max((int)std::floor(b.yMin) - pad, limit.y0 - pad),
            std::min((int)std::ceil(b.xMax) + pad, limit.x1 + pad),
            std::min((int)std::ceil(b.yMax) + pad, limit.y1 + pad)};
}

// Renders the object's content without its color transform into a layer covering `area`
// and filters it. With `parallel`, the content is drawn in row strips and the filters in
// chunks spread over its threads.
void renderLayer(DisplayObject& o, const Matrix& m, const IntRect& area, const std::vector<Filter>& filters,
                 Surface& layer, Rasterizer& r, const Movie& movie, const ParallelRender* parallel) {
    layer.resize(area.x1 - area.x0, area.y1 - area.y0);
    Matrix lm = Matrix::translate(-area.x0, -area.y0) * m;
    if (!parallel) {
        o.render(r, layer, {0, 0, layer.width, layer.height}, lm, ColorTransform{}, movie);
        applyFilters(layer, filters);
        return;
    }
    int strips = std::max(1, std::min(64, layer.height / 16));
    (*parallel)(strips, [&](int i, Rasterizer& pr) {
        IntRect strip{0, layer.height * i / strips, layer.width, layer.height * (i + 1) / strips};
        o.render(pr, layer, strip, lm, ColorTransform{}, movie);
    });
    applyFilters(layer, filters, [&](int n, const std::function<void(int)>& fn) {
        (*parallel)(n, [&](int i, Rasterizer&) { fn(i); });
    });
}

// Brings the cached layer of `o` (drawn with matrix `m` onto the cache's target) up to
// date for this frame: reused if nothing changed, rendered otherwise.
LayerCache::Layer& currentLayer(LayerCache& cache, DisplayObject& o, const Matrix& m, const Surface& target,
                                Rasterizer& r, const Movie& movie, const ParallelRender* parallel) {
    LayerCache::Layer& cached = cache.get(&o);
    std::lock_guard<std::mutex> lock(cached.lock);
    if (cached.ready) return cached;
    RenderHash h;
    o.hashForRender(h, false);  // its own matrix is part of `m`
    h.add((uint64_t)renderSettings.crispBitmaps);
    bool same = cached.valid && cached.hash == h.h && cached.targetWidth == target.width &&
                cached.targetHeight == target.height && cached.m.a == m.a && cached.m.b == m.b &&
                cached.m.c == m.c && cached.m.d == m.d && cached.m.tx == m.tx && cached.m.ty == m.ty;
    if (same) {
        cache.reused++;
    } else {
        std::vector<Filter> filters = deviceFilters(o);
        IntRect area = layerArea(o, m, filterPadding(filters), {0, 0, target.width, target.height});
        cached.pixels = Surface{};
        if (!area.empty()) renderLayer(o, m, area, filters, cached.pixels, r, movie, parallel);
        cached.x0 = area.x0;
        cached.y0 = area.y0;
        cached.valid = true;
        cached.hash = h.h;
        cached.m = m;
        cached.targetWidth = target.width;
        cached.targetHeight = target.height;
        cache.rendered++;
    }
    cached.ready = true;
    cached.usedFrame = cache.frame;
    return cached;
}

bool usesCachedLayer(const DisplayObject& o) {
    if (o.cacheAsLayer) return true;
    for (const Filter& f : o.filters)
        if (f.type != Filter::ColorMatrix && f.type != Filter::Convolution) return true;  // pads its layer
    return false;
}

}  // namespace

namespace {

uint32_t orderStamp = 0;
thread_local int occlusionCutoff = 0;

// Drawn entirely before the calling band's cover, so nothing of it can show.
bool occluded(const DisplayObject& o) {
    return occlusionCutoff && o.orderStamp == orderStamp && o.orderEnd < occlusionCutoff;
}

bool opaqueFill(const FillStyle& f, const Movie& movie) {
    if (f.type == FillStyle::Solid) return f.color.a == 255;
    if (f.isGradient()) {
        if (f.stops.empty()) return false;
        for (const auto& s : f.stops)
            if (s.color.a != 255) return false;
        return true;
    }
    auto* c = movie.character(f.bitmapId);
    if (!c || c->kind != Character::Bitmap) return false;
    auto* b = static_cast<const Bitmap*>(c);
    if (b->opaque < 0) {
        b->opaque = b->width > 0 && b->height > 0 && !b->placeholder;
        for (uint32_t p : b->pixels)
            if ((p >> 24) != 255) {
                b->opaque = 0;
                break;
            }
    }
    return b->opaque == 1;
}

// The device rectangle a fill path covers, if it is an axis-aligned rectangle under `m`
// (no rotation or skew): straight horizontal and vertical edges whose every vertex lies on
// the bounding box, enclosing exactly the box's area.
bool rectangleFill(const FillPath& fp, const Matrix& m, Rect& out) {
    if (m.b != 0 || m.c != 0 || fp.edges.size() < 4) return false;
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300, area = 0;
    for (const Edge& e : fp.edges) {
        if (e.curved || (e.from.x != e.to.x && e.from.y != e.to.y)) return false;
        x0 = std::min({x0, e.from.x, e.to.x});
        x1 = std::max({x1, e.from.x, e.to.x});
        y0 = std::min({y0, e.from.y, e.to.y});
        y1 = std::max({y1, e.from.y, e.to.y});
        area += e.from.x * e.to.y - e.to.x * e.from.y;
    }
    for (const Edge& e : fp.edges)
        for (const Point& p : {e.from, e.to})
            if (p.x != x0 && p.x != x1 && p.y != y0 && p.y != y1) return false;
    double box = (x1 - x0) * (y1 - y0);
    if (box <= 0 || std::fabs(std::fabs(area / 2) - box) > box * 1e-9) return false;
    out = m.apply(Rect{x0, y0, x1, y1});
    return true;
}

void numberForOcclusion(DisplayObject& o, const Matrix& parentMatrix, const ColorTransform& parentCx,
                        bool direct, int& counter, const std::vector<IntRect>& bands, std::vector<int>& covers,
                        const Movie& movie) {
    o.orderStamp = orderStamp;
    o.orderBegin = ++counter;
    bool drawn = o.visible && !renderSettings.isHidden(&o);
    direct = direct && drawn && o.filters.empty() && o.blendMode < BlendLayer && !o.cacheAsLayer;
    Matrix m = parentMatrix * o.drawMatrix();
    ColorTransform cx = parentCx * o.cxform;
    if (direct && cx.aMul == 1 && cx.aAdd == 0 && dynamic_cast<ShapeObject*>(&o)) {
        const ShapeDef& def = static_cast<ShapeCharacter*>(o.definition())->def;
        for (const auto& layer : def.layers)
            for (const FillPath& fp : layer.fills) {
                Rect r;
                if (fp.style < 0 || fp.style >= (int)def.fills.size() || !opaqueFill(def.fills[fp.style], movie) ||
                    !rectangleFill(fp, m, r))
                    continue;
                for (size_t i = 0; i < bands.size(); i++) {
                    const IntRect& b = bands[i];
                    if (r.xMin <= b.x0 - 1 && r.xMax >= b.x1 + 1 && r.yMin <= b.y0 - 1 && r.yMax >= b.y1 + 1)
                        covers[i] = o.orderBegin;
                }
            }
    }
    // Masks and what they mask draw through mask layers, not straight onto the frame.
    int maskedUntil = -1;
    o.forEachChild([&](DisplayObject& c) {
        bool masked = c.clipDepth || c.depth <= maskedUntil;
        if (c.clipDepth) maskedUntil = c.clipDepth;
        numberForOcclusion(c, m, cx, direct && !masked, counter, bands, covers, movie);
    });
    o.orderEnd = counter;
}

}  // namespace

void findBandCovers(DisplayObject& root, const Matrix& rootMatrix, const std::vector<IntRect>& bands,
                    std::vector<int>& covers, const Movie& movie) {
    orderStamp++;
    covers.assign(bands.size(), 0);
    int counter = 0;
    numberForOcclusion(root, rootMatrix, ColorTransform{}, true, counter, bands, covers, movie);
}

void setOcclusionCutoff(int order) { occlusionCutoff = order; }

void renderDisplayObject(DisplayObject& o, Rasterizer& r, Surface& dst, const IntRect& fullClip,
                         const Matrix& parentMatrix, const ColorTransform& parentCx,
                         const Movie& movie, bool ignoreVisible) {
    if (occluded(o)) return;
    if ((!o.visible || renderSettings.isHidden(&o)) && !ignoreVisible) return;
    IntRect clip = fullClip;
    if (o.displayClip) {
        Rect d = parentMatrix.apply(*o.displayClip);
        clip = {std::max(clip.x0, (int)std::floor(std::max(d.xMin, -1e9))), std::max(clip.y0, (int)std::floor(std::max(d.yMin, -1e9))),
                std::min(clip.x1, (int)std::ceil(std::min(d.xMax, 1e9))), std::min(clip.y1, (int)std::ceil(std::min(d.yMax, 1e9)))};
        if (clip.empty()) return;
    }
    Matrix m = parentMatrix * o.drawMatrix();
    ColorTransform cx = parentCx * o.cxform;
    bool needsLayer = !o.filters.empty() || o.blendMode >= BlendLayer || o.cacheAsLayer;
    if (!needsLayer) {
        o.render(r, dst, clip, m, cx, movie);
        return;
    }
    // Render the object's content without its color transform into an offscreen layer,
    // filter it, then composite it with the color transform and blend mode. The color
    // transform therefore also affects glows and shadows, as in Flash.
    uint8_t mode = o.blendMode ? o.blendMode : (uint8_t)BlendNormal;
    LayerCache* cache = renderSettings.layerCache;
    if (cache && cache->target == &dst && usesCachedLayer(o)) {
        // Rendered for the whole target surface, reused by the other bands and, while
        // nothing in it changes, by later frames.
        LayerCache::Layer& cached = currentLayer(*cache, o, m, dst, r, movie, nullptr);
        if (cached.pixels.width) compositeLayer(dst, clip, cached.pixels, cached.x0, cached.y0, mode, cx);
        return;
    }
    std::vector<Filter> filters = deviceFilters(o);
    IntRect area = layerArea(o, m, filterPadding(filters), clip);
    if (area.empty()) return;
    Surface layer;
    renderLayer(o, m, area, filters, layer, r, movie, nullptr);
    compositeLayer(dst, clip, layer, area.x0, area.y0, mode, cx);
}

namespace {

// Finds the filtered objects drawn directly onto the frame surface (not inside another
// layer or a mask), with the matrices rendering will use for them.
void collectCachedLayers(DisplayObject& o, const Matrix& parentMatrix,
                         std::vector<std::pair<DisplayObject*, Matrix>>& out) {
    if (!o.visible || renderSettings.isHidden(&o)) return;
    Matrix m = parentMatrix * o.drawMatrix();
    if (!o.filters.empty() || o.blendMode >= BlendLayer || o.cacheAsLayer) {
        if (usesCachedLayer(o)) out.push_back({&o, m});
        return;  // its children draw into its layer
    }
    int maskedUntil = -1;
    o.forEachChild([&](DisplayObject& c) {
        if (c.clipDepth) {
            maskedUntil = c.clipDepth;  // the mask and what it masks draw into mask layers
            return;
        }
        if (c.depth <= maskedUntil) return;
        collectCachedLayers(c, m, out);
    });
}

}  // namespace

void prerenderLayers(DisplayObject& root, const Matrix& rootMatrix, const Surface& target, const Movie& movie,
                     LayerCache& cache, Rasterizer& r, const ParallelRender& parallel) {
    std::vector<std::pair<DisplayObject*, Matrix>> layers;
    collectCachedLayers(root, rootMatrix, layers);
    for (auto& [obj, m] : layers) currentLayer(cache, *obj, m, target, r, movie, &parallel);
}

LayerCache::Layer& LayerCache::get(const DisplayObject* obj) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& slot = layers_[obj];
    if (!slot) slot = std::make_unique<Layer>();
    return *slot;
}

void LayerCache::beginFrame(const Surface* newTarget) {
    std::lock_guard<std::mutex> lock(mutex_);
    target = newTarget;
    frame++;
    for (auto& [obj, layer] : layers_) layer->ready = false;
}

void LayerCache::endFrame() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = layers_.begin(); it != layers_.end();)
        it = it->second->usedFrame == frame ? std::next(it) : layers_.erase(it);
}

Matrix DisplayObject::drawMatrix() const {
    double t = renderSettings.interpolation;
    if (t >= 1 || !hasPrevMatrix) return displayMatrix();
    const Matrix a = displayTransform * prevMatrix;
    const Matrix b = displayMatrix();
    if (a.a == b.a && a.b == b.b && a.c == b.c && a.d == b.d && a.tx == b.tx && a.ty == b.ty) return b;
    // Jump instead of sweeping through flips (turning around) or across large moves
    // (teleports, camera cuts), which the game shows as instant.
    const double kJump = 100 * 20;  // twips
    if ((a.a * a.d - a.b * a.c) * (b.a * b.d - b.b * b.c) <= 0 || std::fabs(b.tx - a.tx) > kJump ||
        std::fabs(b.ty - a.ty) > kJump)
        return b;
    auto l = [t](double x, double y) { return x + (y - x) * t; };
    return {l(a.a, b.a), l(a.b, b.b), l(a.c, b.c), l(a.d, b.d), l(a.tx, b.tx), l(a.ty, b.ty)};
}

namespace {

struct DebugOverlay {
    enum Kind { Mesh, Clip, Text, Button, Mask, Hidden, KindCount };
    Rasterizer passes[KindCount];

    void outline(Kind k, const std::vector<Edge>& edges, const Matrix& m, double halfWidth) {
        passes[k].addStroke(edges, m, halfWidth);
    }
    void shape(const ShapeDef& s, const Matrix& m, bool hidden) {
        for (const auto& layer : s.layers) {
            for (const FillPath& fp : layer.fills) outline(hidden ? Hidden : Mesh, fp.edges, m, 0.5);
            for (const StrokePath& sp : layer.strokes)
                for (const auto& pl : sp.polylines) outline(hidden ? Hidden : Mesh, pl, m, 0.5);
        }
    }
    void box(Kind k, const Rect& b, const Matrix& m) {
        if (b.empty() || !std::isfinite(b.xMin) || !std::isfinite(b.yMax)) return;
        Point c[4] = {{b.xMin, b.yMin}, {b.xMax, b.yMin}, {b.xMax, b.yMax}, {b.xMin, b.yMax}};
        std::vector<Edge> edges;
        for (int i = 0; i < 4; i++) edges.push_back({c[i], c[i], c[(i + 1) % 4], false});
        outline(k, edges, m, 0.7);
    }
    void visit(DisplayObject& o, const Matrix& parent, bool hidden) {
        Matrix m = parent * o.drawMatrix();
        hidden = hidden || !o.visible;
        if (!m.invertible()) return;
        const ShapeDef* s = nullptr;
        if (o.definition() && o.definition()->kind == Character::Shape)
            s = &static_cast<ShapeCharacter*>(o.definition())->def;
        else if (dynamic_cast<MorphObject*>(&o))
            s = &static_cast<MorphShapeCharacter*>(o.definition())->at(o.ratio);
        else if (auto* mc = dynamic_cast<MovieClip*>(&o))
            s = mc->drawnShape();
        if (s) shape(*s, m, hidden);
        Kind k = hidden ? Hidden : o.clipDepth ? Mask : dynamic_cast<ButtonObject*>(&o) ? Button
               : dynamic_cast<MovieClip*>(&o) ? Clip : (dynamic_cast<TextField*>(&o) || dynamic_cast<StaticTextObject*>(&o)) ? Text
               : KindCount;
        if (k != KindCount && o.parent()) box(k, o.localBounds(), m);
        o.forEachChild([&](DisplayObject& c) { visit(c, m, hidden); });
    }
};

}  // namespace

void drawDebugOverlay(DisplayObject& root, Surface& dst, const Matrix& rootMatrix) {
    DebugOverlay overlay;
    overlay.visit(root, rootMatrix, false);
    const Color colors[DebugOverlay::KindCount] = {
        {0, 220, 255, 170},  // shape edges
        {60, 255, 60, 220},  // clips
        {255, 230, 0, 230},  // text
        {255, 60, 255, 230}, // buttons
        {255, 150, 0, 230},  // masks
        {255, 40, 40, 200},  // invisible objects and their contents
    };
    IntRect all{0, 0, dst.width, dst.height};
    for (int k = 0; k < DebugOverlay::KindCount; k++) {
        Paint p;
        p.color = colors[k];
        overlay.passes[k].fill(dst, all, p);
    }
}

void snapshotTransforms(DisplayObject& o) {
    o.prevMatrix = o.matrix;
    o.hasPrevMatrix = true;
    o.forEachChild([](DisplayObject& c) { snapshotTransforms(c); });
}

uint64_t nextContentVersion() {
    static std::atomic<uint64_t> counter{0};
    return ++counter;
}

void DisplayObject::hashForRender(RenderHash& h, bool self) {
    h.add((uint64_t)(def_ ? def_->id : 0xFFFFFFu));
    if (self) {
        h.add(drawMatrix());
        h.add(cxform);
    }
    h.add((uint64_t)ratio << 32 | (uint64_t)clipDepth << 16 | (uint64_t)blendMode << 1 | visible);
    for (const Filter& f : filters) {
        h.add((uint64_t)f.type << 8 | f.inner << 3 | f.knockout << 2 | f.onTop << 1 | f.compositeSource);
        for (double v : {f.blurX, f.blurY, f.angle, f.distance, f.strength}) h.add(v);
        h.add((uint64_t)f.passes);
        h.add((uint64_t)(f.color.r | f.color.g << 8 | f.color.b << 16 | f.color.a << 24));
        h.add((uint64_t)(f.highlight.r | f.highlight.g << 8 | f.highlight.b << 16 | f.highlight.a << 24));
        for (float v : f.matrix) h.add(v);
    }
    hashContent(h);
    forEachChild([&h](DisplayObject& c) {
        h.add((uint64_t)(int64_t)c.depth);
        c.hashForRender(h);
    });
}

void MovieClip::hashContent(RenderHash& h) {
    if (hasDrawing()) h.add(drawing_->version);
}

void prepareForRender(DisplayObject& o) {
    if (auto* tf = dynamic_cast<TextField*>(&o)) tf->prepareForRender();
    if (auto* mc = dynamic_cast<MovieClip*>(&o); mc && mc->hasDrawing()) mc->drawing().cachedShape();
    if (dynamic_cast<MorphObject*>(&o)) {
        auto* def = static_cast<MorphShapeCharacter*>(o.definition());
        def->trimCache();
        def->at(o.ratio);
    }
    o.forEachChild([](DisplayObject& c) { prepareForRender(c); });
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
    for (auto& [d, c] : children_) out = unionRect(out, c->drawMatrix().apply(c->localBounds()));
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

void Drawing::clear() { *this = Drawing{}; }  // takes a new version too

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
    snapshotValid_ = false;
    version = nextContentVersion();
    flushLine();
    lining = widthTwips >= 0;
    line = LineStyle{};
    line.width = std::max(0.0, widthTwips);
    line.color = c;
}

void Drawing::beginFill(Color c) {
    snapshotValid_ = false;
    version = nextContentVersion();
    endFill();
    filling = true;
    fill = FillStyle{};
    fill.color = c;
    fillStart = pen;
}

void Drawing::endFill() {
    snapshotValid_ = false;
    version = nextContentVersion();
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
    snapshotValid_ = false;
    version = nextContentVersion();
    flushLine();
    pen = p;
    if (filling && fillEdges.empty()) fillStart = p;
}

void Drawing::lineTo(Point p) {
    snapshotValid_ = false;
    version = nextContentVersion();
    Edge e{pen, {(pen.x + p.x) / 2, (pen.y + p.y) / 2}, p, false};
    if (filling) fillEdges.push_back(e);
    if (lining) lineEdges.push_back(e);
    pen = p;
}

void Drawing::curveTo(Point c, Point p) {
    snapshotValid_ = false;
    version = nextContentVersion();
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
            if (p.hasName) s.name = p.name, s.hasName = true;
            if (p.hasClipDepth) s.clipDepth = p.clipDepth;
            if (p.hasFilters) s.filters = p.filters, s.hasFilters = true;
            if (p.hasBlendMode) s.blendMode = p.blendMode, s.hasBlendMode = true;
            if (p.hasVisible) s.visible = p.visible, s.hasVisible = true;
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

    for (auto& entry : target) {
        // Plain references, not a structured binding: lambdas below capture `s`, which
        // older compilers (NDK r25's clang) only allow for ordinary variables.
        int d = entry.first;
        PlaceState& s = entry.second;
        DisplayObject* existing = childAt(d);
        if (existing && !existing->placedByTimeline) continue;  // depth owned by script
        // A new instance takes the whole timeline state. One that survives keeps what
        // scripts gave it (name, _visible, filters, blend mode) unless the timeline sets
        // that property itself, like Flash; e.g. hidden marker clips stay hidden.
        auto apply = [&s](DisplayObject& o, bool keepTransform, bool fresh) {
            if (!keepTransform) {
                o.matrix = s.matrix;
                o.cxform = s.cxform;
                o.invalidateTransform();
            }
            o.ratio = s.ratio;
            o.clipDepth = s.clipDepth;
            if (fresh || s.hasName) o.name = s.name;
            if (fresh || s.hasFilters) o.filters = s.filters;
            if (fresh || s.hasBlendMode) o.blendMode = s.blendMode;
            if (fresh || s.hasVisible) o.visible = s.visible;
        };
        if (existing) {
            apply(*existing, existing->transformedByScript, false);
            continue;
        }
        auto created = instantiate(s.characterId);
        if (!created) continue;
        apply(*created, false, true);
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
    if (hasDrawing()) out = drawing_->cachedShape().bounds;
    for (auto& [d, c] : children_) out = unionRect(out, c->drawMatrix().apply(c->localBounds()));
    return out;
}

bool MovieClip::hitTest(const Matrix& m, Point p) const {
    // A zero-scale clip has no area; Flash reports no hit (and its children inherit that).
    if (!m.invertible()) return false;
    if (hasDrawing() && shapeContains(drawing_->cachedShape(), m.inverse().apply(p))) return true;
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
    if (hasDrawing()) drawShape(r, dst, clip, drawing_->preparedShape(), m, cx, movie);
    for (auto it = children_.begin(); it != children_.end(); ++it) {
        DisplayObject& c = *it->second;
        if (!c.clipDepth) {
            renderDisplayObject(c, r, dst, clip, m, cx, movie);
            continue;
        }
        // Mask: covers siblings with depth in (c.depth, c.clipDepth]. Render the mask's
        // coverage and the masked content into layers the size of the mask's on-screen
        // bounds, then combine.
        auto last = std::next(it);
        while (last != children_.end() && last->first <= c.clipDepth) ++last;
        auto first = std::next(it);
        it = std::prev(last);
        if (occluded(*it->second)) continue;  // the whole group is under the band's cover
        Rect b = (m * c.drawMatrix()).apply(c.localBounds());
        if (b.empty() || !std::isfinite(b.xMin) || !std::isfinite(b.yMin) || !std::isfinite(b.xMax) ||
            !std::isfinite(b.yMax))
            continue;
        IntRect area{std::max(clip.x0, (int)std::floor(b.xMin) - 1), std::max(clip.y0, (int)std::floor(b.yMin) - 1),
                     std::min(clip.x1, (int)std::ceil(b.xMax) + 1), std::min(clip.y1, (int)std::ceil(b.yMax) + 1)};
        if (area.empty()) continue;  // nothing of the masked content can show
        Surface mask, content;
        mask.resize(area.x1 - area.x0, area.y1 - area.y0);
        content.resize(mask.width, mask.height);
        IntRect local{0, 0, mask.width, mask.height};
        Matrix lm = Matrix::translate(-area.x0, -area.y0) * m;
        // As in Flash, a mask is its shapes' coverage: their fills' alpha doesn't matter
        // (every fill counts as opaque; antialiased edges still soften). Invisible masks
        // still mask.
        renderDisplayObject(c, r, mask, local, lm, kMaskColor, movie, true);
        for (auto n = first; n != last; ++n) renderDisplayObject(*n->second, r, content, local, lm, cx, movie);
        applyMask(content, mask);
        compositeLayer(dst, clip, content, area.x0, area.y0, BlendNormal, ColorTransform{});
    }
}

}  // namespace flash
