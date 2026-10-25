#include "gpu/GpuRenderer.h"

#include <algorithm>
#include <cmath>

#include "as/Player.h"
#include "flash/DisplayObject.h"
#include "flash/Effects.h"
#include "flash/Morph.h"
#include "flash/Swf.h"
#include "flash/Text.h"

using namespace flash;

namespace {

const char* kStencilVertex = R"(
attribute vec2 pos;
uniform vec2 viewport;
void main() { gl_Position = vec4(pos.x / viewport.x * 2.0 - 1.0, 1.0 - pos.y / viewport.y * 2.0, 0.0, 1.0); }
)";

const char* kStencilFragment = R"(
void main() { gl_FragColor = vec4(0.0); }
)";

const char* kPaintVertex = R"(
attribute vec2 pos;
uniform vec2 viewport;
varying vec2 dev;
void main() {
    dev = pos;
    gl_Position = vec4(pos.x / viewport.x * 2.0 - 1.0, 1.0 - pos.y / viewport.y * 2.0, 0.0, 1.0);
}
)";

// Paints a pixel (device position `dev`, its center) the way the software rasterizer's
// samplers do. Textures hold premultiplied ARGB words as uploaded bytes (B, G, R, A), so
// they are read .bgra. Modes: 0 solid color, 1 linear and 2 radial gradient (ramp in
// `tex`), 3 bitmap fill, 4 a texture placed 1:1 on device pixels.
const char* kPaintFragment = R"(
varying vec2 dev;
uniform int mode;
uniform vec4 color;
uniform vec3 inv0, inv1;
uniform int spread;
uniform sampler2D tex;
uniform vec2 texSize;
uniform bool repeating, smoothing, cxOn;
uniform vec4 cxMul, cxAdd;

vec4 texel(vec2 p) {
    if (repeating) p = p - texSize * floor(p / texSize);
    else p = clamp(p, vec2(0.0), texSize - 1.0);
    return texture2D(tex, (p + 0.5) / texSize).bgra;
}

vec4 transformed(vec4 c) {
    if (!cxOn) return c;
    if (c.a > 0.0) c.rgb /= c.a;
    c = clamp(c * cxMul + cxAdd, 0.0, 1.0);
    c.rgb *= c.a;
    return c;
}

void main() {
    vec3 p = vec3(dev, 1.0);
    if (mode == 0) {
        gl_FragColor = color;
    } else if (mode <= 2) {
        float gx = dot(inv0, p);
        float t = mode == 1 ? (gx + 16384.0) / 32768.0 : length(vec2(gx, dot(inv1, p))) / 16384.0;
        if (spread == 1) {
            t = mod(abs(t), 2.0);
            if (t > 1.0) t = 2.0 - t;
        } else if (spread == 2) {
            t -= floor(t);
        }
        float i = clamp(floor(t * 255.0 + 0.5), 0.0, 255.0);
        gl_FragColor = texture2D(tex, vec2((i + 0.5) / 256.0, 0.5)).bgra;
    } else if (mode == 3) {
        vec2 uv = vec2(dot(inv0, p), dot(inv1, p));
        vec4 c;
        if (smoothing) {
            uv -= 0.5;
            vec2 f = floor(uv), w = uv - f;
            c = mix(mix(texel(f), texel(f + vec2(1.0, 0.0)), w.x),
                    mix(texel(f + vec2(0.0, 1.0)), texel(f + vec2(1.0, 1.0)), w.x), w.y);
        } else {
            c = texel(floor(uv));
        }
        gl_FragColor = transformed(c);
    } else {
        vec2 uv = vec2(dot(inv0, p), dot(inv1, p));
        gl_FragColor = texture2D(tex, uv / texSize).bgra;
    }
}
)";

IntRect intersect(const IntRect& a, const IntRect& b) {
    return {std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
}

bool needsSoftware(const DisplayObject& o) {
    return !o.filters.empty() || o.blendMode >= BlendLayer || o.cacheAsLayer || dynamic_cast<const TextField*>(&o);
}

GLuint makeTexture() {
    GLuint t = 0;
    gl::GenTextures(1, &t);
    gl::BindTexture(GL_TEXTURE_2D, t);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

}  // namespace

GpuRenderer::~GpuRenderer() {
    // The context may already be gone at exit; textures go with it.
}

bool GpuRenderer::init() {
    stencilProgram_ = gl::program(kStencilVertex, kStencilFragment, {"pos"});
    paintProgram_ = gl::program(kPaintVertex, kPaintFragment, {"pos"});
    if (!stencilProgram_ || !paintProgram_) return false;
    stencilViewport_ = gl::GetUniformLocation(stencilProgram_, "viewport");
    GLuint p = paintProgram_;
    u_.viewport = gl::GetUniformLocation(p, "viewport");
    u_.mode = gl::GetUniformLocation(p, "mode");
    u_.color = gl::GetUniformLocation(p, "color");
    u_.inv0 = gl::GetUniformLocation(p, "inv0");
    u_.inv1 = gl::GetUniformLocation(p, "inv1");
    u_.spread = gl::GetUniformLocation(p, "spread");
    u_.tex = gl::GetUniformLocation(p, "tex");
    u_.texSize = gl::GetUniformLocation(p, "texSize");
    u_.repeat = gl::GetUniformLocation(p, "repeating");
    u_.smooth = gl::GetUniformLocation(p, "smoothing");
    u_.cxOn = gl::GetUniformLocation(p, "cxOn");
    u_.cxMul = gl::GetUniformLocation(p, "cxMul");
    u_.cxAdd = gl::GetUniformLocation(p, "cxAdd");
    rampTexture_ = makeTexture();
    gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return true;
}

// ---------------------------------------------------------------- frame

void GpuRenderer::render(as::Player& player, const Movie& movie, int width, int height, double interpolation,
                         double scale, double originX, double originY) {
    movie_ = &movie;
    width_ = width;
    height_ = height;
    frame_++;
    fallbacks = fallbacksRedrawn = 0;
    renderSettings.scale = scale;
    renderSettings.interpolation = std::clamp(interpolation, 0.0, 1.0);
    renderSettings.layerCache = nullptr;
    prepareForRender(player.root());
    Matrix m = Matrix::translate(originX, originY) * Matrix::scale(0.05 * scale, 0.05 * scale);
    std::vector<Matrix> backdropMatrices;
    std::vector<const DisplayObject*> hidden = player.hiddenObjects;
    DisplayObject* backdrop = player.backdrop;
    if (backdrop && backdrop->visible) {
        hidden.push_back(backdrop);
        Matrix parent = backdrop->parent() ? backdrop->parent()->worldMatrix() : Matrix{};
        for (Matrix p : player.backdropPlacements) {
            p.tx *= 20;
            p.ty *= 20;
            backdropMatrices.push_back(m * p * parent);
        }
    }
    renderSettings.hidden = hidden.empty() ? nullptr : &hidden;

    gl::Viewport(0, 0, width, height);
    gl::Enable(GL_SCISSOR_TEST);
    setClip({0, 0, width, height});
    gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl::StencilMask(0xFF);
    gl::ClearColor(movie.background.r / 255.f, movie.background.g / 255.f, movie.background.b / 255.f, 1.f);
    gl::ClearStencil(0);
    gl::Clear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    gl::Enable(GL_STENCIL_TEST);
    gl::Enable(GL_BLEND);
    gl::BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);  // premultiplied source over
    gl::EnableVertexAttribArray(0);
    gl::UseProgram(stencilProgram_);
    gl::Uniform2f(stencilViewport_, (float)width, (float)height);
    gl::UseProgram(paintProgram_);
    gl::Uniform2f(u_.viewport, (float)width, (float)height);
    gl::Uniform1i(u_.tex, 0);
    gl::ActiveTexture(GL_TEXTURE0);
    inMask_ = false;

    for (const Matrix& bm : backdropMatrices) drawObject(*backdrop, bm, ColorTransform{}, true);
    drawObject(player.root(), m, ColorTransform{});

    if (player.blackBeyond) {
        // Black over everything beyond the rectangle (letterbox bars).
        const Rect& s = *player.blackBeyond;
        int x0 = std::clamp((int)std::lround(originX + s.xMin / 20 * scale), 0, width);
        int x1 = std::clamp((int)std::lround(originX + s.xMax / 20 * scale), x0, width);
        int y0 = std::clamp((int)std::lround(originY + s.yMin / 20 * scale), 0, height);
        int y1 = std::clamp((int)std::lround(originY + s.yMax / 20 * scale), y0, height);
        gl::ClearColor(0, 0, 0, 1);
        for (IntRect r : {IntRect{0, 0, width, y0}, IntRect{0, y1, width, height}, IntRect{0, y0, x0, y1},
                          IntRect{x1, y0, width, y1}})
            if (!r.empty()) {
                setClip(r);
                gl::Clear(GL_COLOR_BUFFER_BIT);
            }
    }
    renderSettings.hidden = nullptr;
    renderSettings.interpolation = 1;  // scripts see real positions
    gl::Disable(GL_STENCIL_TEST);
    gl::Disable(GL_SCISSOR_TEST);

    // Software-drawn objects that didn't show for a while give their textures back.
    for (auto it = fallbackCache_.begin(); it != fallbackCache_.end();) {
        if (frame_ - it->second.lastFrame > 60) {
            gl::DeleteTextures(1, &it->second.texture);
            it = fallbackCache_.erase(it);
        } else {
            ++it;
        }
    }
}

void GpuRenderer::setClip(const IntRect& clip) {
    clip_ = clip;
    gl::Scissor(clip.x0, height_ - clip.y1, std::max(0, clip.x1 - clip.x0), std::max(0, clip.y1 - clip.y0));
}

// ---------------------------------------------------------------- display list

void GpuRenderer::drawObject(DisplayObject& o, const Matrix& parent, const ColorTransform& parentCx,
                             bool ignoreVisible) {
    if ((!o.visible || renderSettings.isHidden(&o)) && !ignoreVisible) return;
    IntRect saved = clip_;
    if (o.displayClip) {
        Rect d = parent.apply(*o.displayClip);
        IntRect c{(int)std::floor(std::max(d.xMin, -1e9)), (int)std::floor(std::max(d.yMin, -1e9)),
                  (int)std::ceil(std::min(d.xMax, 1e9)), (int)std::ceil(std::min(d.yMax, 1e9))};
        c = intersect(clip_, c);
        if (c.empty()) return;
        setClip(c);
    }
    if (needsSoftware(o)) {
        drawFallback(o, parent, parentCx);
    } else {
        Matrix m = parent * o.drawMatrix();
        ColorTransform cx = parentCx * o.cxform;
        if (auto* s = dynamic_cast<ShapeObject*>(&o)) {
            drawShape(static_cast<ShapeCharacter*>(s->definition())->def, m, cx);
        } else if (auto* mo = dynamic_cast<MorphObject*>(&o)) {
            drawShape(static_cast<MorphShapeCharacter*>(mo->definition())->at(mo->ratio), m, cx);
        } else if (dynamic_cast<StaticTextObject*>(&o)) {
            drawStaticText(o, m, cx);
        } else if (auto* mc = dynamic_cast<MovieClip*>(&o)) {
            if (const ShapeDef* d = mc->drawnShape()) drawShape(*d, m, cx);
            drawChildren(*mc, m, cx);
        } else if (dynamic_cast<ButtonObject*>(&o)) {
            o.forEachChild([&](DisplayObject& c) { drawObject(c, m, cx); });
        }
    }
    if (o.displayClip) setClip(saved);
}

void GpuRenderer::drawChildren(MovieClip& mc, const Matrix& m, const ColorTransform& cx) {
    const auto& children = mc.children();
    for (auto it = children.begin(); it != children.end(); ++it) {
        DisplayObject& c = *it->second;
        if (!c.clipDepth) {
            drawObject(c, m, cx);
            continue;
        }
        // A mask: covers the siblings at depths up to its clipDepth.
        auto last = std::next(it);
        while (last != children.end() && last->first <= c.clipDepth) ++last;
        auto first = std::next(it);
        it = std::prev(last);
        if (first == last) continue;
        if (inMask_) {
            // A mask inside masked content: the software renderer draws the clip's mask
            // group (the mask and what it masks) into a layer, as it would.
            Rect b = (m * c.drawMatrix()).apply(c.localBounds());
            if (b.empty() || !std::isfinite(b.xMin) || !std::isfinite(b.yMax)) continue;
            IntRect area = intersect(clip_, {(int)std::floor(b.xMin) - 1, (int)std::floor(b.yMin) - 1,
                                             (int)std::ceil(b.xMax) + 1, (int)std::ceil(b.yMax) + 1});
            if (area.empty()) continue;
            Surface mask, content;
            mask.resize(area.x1 - area.x0, area.y1 - area.y0);
            content.resize(mask.width, mask.height);
            IntRect local{0, 0, mask.width, mask.height};
            Matrix lm = Matrix::translate(-area.x0, -area.y0) * m;
            renderDisplayObject(c, rast_, mask, local, lm, ColorTransform{}, *movie_, true);
            for (auto n = first; n != last; ++n) renderDisplayObject(*n->second, rast_, content, local, lm, cx, *movie_);
            applyMask(content, mask);
            GLuint t = makeTexture();
            gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, content.width, content.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                           content.pixels.data());
            gl::UseProgram(paintProgram_);
            gl::Uniform1i(u_.mode, 4);
            gl::Uniform3f(u_.inv0, 1, 0, (float)-area.x0);
            gl::Uniform3f(u_.inv1, 0, 1, (float)-area.y0);
            gl::Uniform2f(u_.texSize, (float)content.width, (float)content.height);
            gl::StencilFunc(GL_EQUAL, 0x80, 0x80);
            gl::StencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            drawBox(area);
            gl::DeleteTextures(1, &t);
            fallbacks++, fallbacksRedrawn++;
            continue;
        }
        IntRect box = drawMask(c, m);
        if (box.empty()) continue;
        inMask_ = true;
        for (auto n = first; n != last; ++n) drawObject(*n->second, m, cx);
        inMask_ = false;
        // Clear the mask's bit where it was set.
        IntRect saved = clip_;
        setClip(intersect(saved, box));
        gl::StencilMask(0x80);
        gl::ClearStencil(0);
        gl::Clear(GL_STENCIL_BUFFER_BIT);
        setClip(saved);
    }
}

// ---------------------------------------------------------------- shapes

void GpuRenderer::drawShape(const ShapeDef& shape, const Matrix& m, const ColorTransform& cx) {
    const Rect& sb = shape.bounds;
    if (!sb.empty()) {
        Rect b = m.apply(sb);
        double pad = 1 + renderSettings.scale;
        if (b.xMax + pad < clip_.x0 || b.xMin - pad > clip_.x1 || b.yMax + pad < clip_.y0 || b.yMin - pad > clip_.y1)
            return;
    }
    double scale = std::sqrt(std::fabs(m.a * m.d - m.b * m.c));
    for (const auto& layer : shape.layers) {
        for (const FillPath& fp : layer.fills) {
            if (fp.style < 0 || fp.style >= (int)shape.fills.size()) continue;
            const FillStyle& fs = shape.fills[fp.style];
            if (fs.isBitmap()) {
                auto* c = movie_->character(fs.bitmapId);
                if (!c || c->kind != Character::Bitmap) continue;  // "no bitmap" fills draw nothing
            }
            rast_.reset();
            rast_.addPath(fp.edges, m);
            fill(&fs, {}, m, cx);
        }
        for (const StrokePath& sp : layer.strokes) {
            if (sp.style < 0 || sp.style >= (int)shape.lines.size()) continue;
            const LineStyle& ls = shape.lines[sp.style];
            double w = ls.width * ((ls.noHScale || ls.noVScale) ? 0.05 * renderSettings.scale : scale);
            double hw = std::max(0.5 * renderSettings.scale, w / 2);
            rast_.reset();
            for (const auto& pl : sp.polylines) rast_.addStroke(pl, m, hw);
            fill(ls.hasFill && !ls.fill.isBitmap() ? &ls.fill : nullptr, ls.color, m, cx);
        }
    }
}

void GpuRenderer::drawStaticText(DisplayObject& o, const Matrix& m, const ColorTransform& cx) {
    auto* t = static_cast<StaticTextCharacter*>(o.definition());
    Matrix base = m * t->matrix;
    double x = 0, y = 0;
    for (const auto& rec : t->records) {
        auto* fc = movie_->character(rec.fontId);
        if (!fc || fc->kind != Character::Font) continue;
        auto* font = static_cast<FontCharacter*>(fc);
        if (rec.hasX) x = rec.x;
        if (rec.hasY) y = rec.y;
        double s = rec.height / font->emSize;
        for (const auto& g : rec.glyphs) {
            if (g.index >= 0 && g.index < (int)font->glyphs.size()) {
                Matrix gm = base * Matrix{s, 0, 0, s, x, y};
                rast_.reset();
                for (const auto& layer : font->glyphs[g.index].layers)
                    for (const FillPath& fp : layer.fills) rast_.addPath(fp.edges, gm);
                fill(nullptr, rec.color, gm, cx);
            }
            x += g.advance;
        }
    }
}

bool GpuRenderer::stencilOutline(IntRect& box) {
    const auto& segs = rast_.segments();
    if (segs.empty()) return false;
    double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30;
    for (const auto& s : segs) {
        minX = std::min({minX, s.x0, s.x1});
        maxX = std::max({maxX, s.x0, s.x1});
        minY = std::min({minY, s.y0, s.y1});
        maxY = std::max({maxY, s.y0, s.y1});
    }
    box = intersect(clip_, {(int)std::floor(minX), (int)std::floor(minY), (int)std::ceil(maxX), (int)std::ceil(maxY)});
    if (box.empty()) {
        rast_.reset();
        return false;
    }
    // A fan from one point through every segment: each pixel's count of the triangles
    // over it (up for one orientation, down for the other) is the outline's winding there.
    verts_.clear();
    float ax = (float)segs[0].x0, ay = (float)segs[0].y0;
    for (const auto& s : segs) {
        verts_.insert(verts_.end(), {ax, ay, (float)s.x0, (float)s.y0, (float)s.x1, (float)s.y1});
    }
    rast_.reset();
    gl::UseProgram(stencilProgram_);
    gl::ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    gl::StencilMask(0x7F);
    gl::StencilFunc(GL_ALWAYS, 0, 0xFF);
    gl::StencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR_WRAP);
    gl::StencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR_WRAP);
    gl::BindBuffer(GL_ARRAY_BUFFER, 0);
    gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts_.data());
    gl::DrawArrays(GL_TRIANGLES, 0, (GLsizei)(verts_.size() / 2));
    gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    return true;
}

void GpuRenderer::drawBox(const IntRect& box) {
    float x0 = (float)box.x0, y0 = (float)box.y0, x1 = (float)box.x1, y1 = (float)box.y1;
    const float quad[12] = {x0, y0, x1, y0, x1, y1, x0, y0, x1, y1, x0, y1};
    gl::BindBuffer(GL_ARRAY_BUFFER, 0);
    gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    gl::DrawArrays(GL_TRIANGLES, 0, 6);
}

void GpuRenderer::fill(const FillStyle* fs, Color color, const Matrix& toDevice, const ColorTransform& cx) {
    IntRect box;
    if (!stencilOutline(box)) return;
    gl::UseProgram(paintProgram_);
    auto setInverse = [&](const Matrix& inv) {
        gl::Uniform3f(u_.inv0, (float)inv.a, (float)inv.c, (float)inv.tx);
        gl::Uniform3f(u_.inv1, (float)inv.b, (float)inv.d, (float)inv.ty);
    };
    if (!fs || fs->type == FillStyle::Solid) {
        Color c = applyCx(cx, fs ? fs->color : color);
        float a = c.a / 255.f;
        gl::Uniform1i(u_.mode, 0);
        gl::Uniform4f(u_.color, c.r / 255.f * a, c.g / 255.f * a, c.b / 255.f * a, a);
    } else if (fs->isGradient()) {
        uint32_t ramp[256];
        gradientRamp(*fs, cx, ramp);
        gl::BindTexture(GL_TEXTURE_2D, rampTexture_);
        gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, ramp);
        gl::Uniform1i(u_.mode, fs->type == FillStyle::LinearGradient ? 1 : 2);
        gl::Uniform1i(u_.spread, fs->spread);
        setInverse((toDevice * fs->matrix).inverse());
    } else {
        auto* bmp = static_cast<const Bitmap*>(movie_->character(fs->bitmapId));
        if (bmp->width <= 0 || bmp->height <= 0 || bmp->pixels.empty()) {
            gl::StencilMask(0x7F);  // nothing to paint: just reset the counts below
            gl::Uniform1i(u_.mode, 0);
            gl::Uniform4f(u_.color, 0, 0, 0, 0);
        } else {
            Matrix inv = (toDevice * fs->matrix).inverse();
            bool repeat = fs->type == FillStyle::RepeatingBitmap || fs->type == FillStyle::RepeatingBitmapHard;
            bool smooth = fs->type == FillStyle::RepeatingBitmap || fs->type == FillStyle::ClippedBitmap;
            if (renderSettings.crispBitmaps && std::fabs(inv.a * inv.d - inv.b * inv.c) < 1) smooth = false;
            gl::BindTexture(GL_TEXTURE_2D, bitmapTexture(*bmp));
            gl::Uniform1i(u_.mode, 3);
            gl::Uniform2f(u_.texSize, (float)bmp->width, (float)bmp->height);
            gl::Uniform1i(u_.repeat, repeat);
            gl::Uniform1i(u_.smooth, smooth);
            bool on = !cx.isIdentity();
            gl::Uniform1i(u_.cxOn, on);
            if (on) {
                gl::Uniform4f(u_.cxMul, cx.rMul, cx.gMul, cx.bMul, cx.aMul);
                gl::Uniform4f(u_.cxAdd, cx.rAdd / 255.f, cx.gAdd / 255.f, cx.bAdd / 255.f, cx.aAdd / 255.f);
            }
            setInverse(inv);
        }
    }
    // Paint where the count isn't zero (and, under a mask, the mask's bit is set: then
    // the value is above 0x80), resetting the counts as it goes.
    gl::StencilMask(0x7F);
    if (inMask_) gl::StencilFunc(GL_LESS, 0x80, 0xFF);
    else gl::StencilFunc(GL_NOTEQUAL, 0, 0x7F);
    gl::StencilOp(GL_ZERO, GL_ZERO, GL_ZERO);
    drawBox(box);
}

GLuint GpuRenderer::bitmapTexture(const Bitmap& b) {
    auto it = bitmaps_.find(&b);
    if (it != bitmaps_.end()) return it->second;
    GLuint t = makeTexture();
    gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, b.width, b.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, b.pixels.data());
    bitmaps_[&b] = t;
    return t;
}

// ---------------------------------------------------------------- masks

void GpuRenderer::addMaskShape(const ShapeDef& shape, const Matrix& m, IntRect& box) {
    for (const auto& layer : shape.layers)
        for (const FillPath& fp : layer.fills) {
            rast_.reset();
            rast_.addPath(fp.edges, m);
            IntRect b;
            if (!stencilOutline(b)) continue;
            // Where the count isn't zero: set the mask's bit (and clear the count).
            gl::StencilMask(0xFF);
            gl::StencilFunc(GL_NOTEQUAL, 0x80, 0x7F);
            gl::StencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            gl::ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            drawBox(b);
            gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            box = box.empty() ? b : IntRect{std::min(box.x0, b.x0), std::min(box.y0, b.y0), std::max(box.x1, b.x1),
                                            std::max(box.y1, b.y1)};
        }
}

IntRect GpuRenderer::drawMask(DisplayObject& mask, const Matrix& parent) {
    IntRect box{};
    std::function<void(DisplayObject&, const Matrix&)> visit = [&](DisplayObject& o, const Matrix& pm) {
        Matrix m = pm * o.drawMatrix();
        if (auto* s = dynamic_cast<ShapeObject*>(&o)) {
            addMaskShape(static_cast<ShapeCharacter*>(s->definition())->def, m, box);
        } else if (auto* mo = dynamic_cast<MorphObject*>(&o)) {
            addMaskShape(static_cast<MorphShapeCharacter*>(mo->definition())->at(mo->ratio), m, box);
        } else if (auto* mc = dynamic_cast<MovieClip*>(&o)) {
            if (const ShapeDef* d = mc->drawnShape()) addMaskShape(*d, m, box);
            for (auto& [depth, c] : mc->children()) visit(*c, m);  // hidden parts of a mask still mask
        }
    };
    visit(mask, parent);  // invisible masks still mask
    return box;
}

// ---------------------------------------------------------------- software fallback

void GpuRenderer::drawFallback(DisplayObject& o, const Matrix& parent, const ColorTransform& parentCx) {
    Matrix m = parent * o.drawMatrix();
    std::vector<Filter> filters = o.filters;
    for (Filter& f : filters) {
        f.blurX *= renderSettings.scale;
        f.blurY *= renderSettings.scale;
        f.distance *= renderSettings.scale;
    }
    int pad = filterPadding(filters);
    Rect b = m.apply(o.localBounds());
    if (b.empty() || !std::isfinite(b.xMin) || !std::isfinite(b.yMin) || !std::isfinite(b.xMax) ||
        !std::isfinite(b.yMax))
        return;
    IntRect area = intersect(clip_, {(int)std::floor(b.xMin) - pad - 1, (int)std::floor(b.yMin) - pad - 1,
                                     (int)std::ceil(b.xMax) + pad + 1, (int)std::ceil(b.yMax) + pad + 1});
    if (area.empty()) return;
    fallbacks++;
    RenderHash h;
    o.hashForRender(h, false);
    h.add(parent);
    h.add(o.drawMatrix());
    h.add(parentCx);
    h.add(o.cxform);
    for (int v : {area.x0, area.y0, area.x1, area.y1}) h.add((uint64_t)(int64_t)v);
    h.add((uint64_t)renderSettings.crispBitmaps);
    Fallback& f = fallbackCache_[&o];
    f.lastFrame = frame_;
    int w = area.x1 - area.x0, hgt = area.y1 - area.y0;
    if (!f.texture || f.hash != h.h || f.w != w || f.h != hgt) {
        fallbacksRedrawn++;
        Surface s;
        s.resize(w, hgt);
        Matrix lp = Matrix::translate(-area.x0, -area.y0) * parent;
        renderDisplayObject(o, rast_, s, {0, 0, w, hgt}, lp, parentCx, *movie_, true);
        if (!f.texture) f.texture = makeTexture();
        gl::BindTexture(GL_TEXTURE_2D, f.texture);
        if (f.texW != w || f.texH != hgt) {
            gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, hgt, 0, GL_RGBA, GL_UNSIGNED_BYTE, s.pixels.data());
            f.texW = w;
            f.texH = hgt;
        } else {
            gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, hgt, GL_RGBA, GL_UNSIGNED_BYTE, s.pixels.data());
        }
        f.hash = h.h;
        f.x0 = area.x0;
        f.y0 = area.y0;
        f.w = w;
        f.h = hgt;
    }
    gl::UseProgram(paintProgram_);
    gl::BindTexture(GL_TEXTURE_2D, f.texture);
    gl::Uniform1i(u_.mode, 4);
    gl::Uniform3f(u_.inv0, 1, 0, (float)-f.x0);
    gl::Uniform3f(u_.inv1, 0, 1, (float)-f.y0);
    gl::Uniform2f(u_.texSize, (float)f.texW, (float)f.texH);
    if (inMask_) {
        gl::StencilFunc(GL_EQUAL, 0x80, 0x80);
    } else {
        gl::StencilFunc(GL_ALWAYS, 0, 0xFF);
    }
    gl::StencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    drawBox(area);
}

// ---------------------------------------------------------------- presenting CPU frames

void GpuRenderer::upload(const Surface& frame) {
    if (!frameTexture_) frameTexture_ = makeTexture();
    gl::BindTexture(GL_TEXTURE_2D, frameTexture_);
    if (frame.width != frameW_ || frame.height != frameH_) {
        frameW_ = frame.width;
        frameH_ = frame.height;
        gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frameW_, frameH_, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame.pixels.data());
    } else {
        gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frameW_, frameH_, GL_RGBA, GL_UNSIGNED_BYTE, frame.pixels.data());
    }
}

void GpuRenderer::present(int width, int height) {
    width_ = width;
    height_ = height;
    gl::Viewport(0, 0, width, height);
    gl::Disable(GL_SCISSOR_TEST);
    gl::Disable(GL_STENCIL_TEST);
    gl::Disable(GL_BLEND);
    gl::ClearColor(0, 0, 0, 1);
    gl::Clear(GL_COLOR_BUFFER_BIT);
    if (!frameTexture_) return;
    gl::UseProgram(paintProgram_);
    gl::Uniform2f(u_.viewport, (float)width, (float)height);
    gl::Uniform1i(u_.tex, 0);
    gl::ActiveTexture(GL_TEXTURE0);
    gl::BindTexture(GL_TEXTURE_2D, frameTexture_);
    gl::Uniform1i(u_.mode, 4);
    // The frame is the window's size; if the window changed since, it is stretched.
    gl::Uniform3f(u_.inv0, (float)frameW_ / width, 0, 0);
    gl::Uniform3f(u_.inv1, 0, (float)frameH_ / height, 0);
    gl::Uniform2f(u_.texSize, (float)frameW_, (float)frameH_);
    gl::EnableVertexAttribArray(0);
    drawBox({0, 0, width, height});
}
