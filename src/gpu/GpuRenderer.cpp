#include "gpu/GpuRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

#include "as/Player.h"
#include "flash/DisplayObject.h"
#include "flash/Effects.h"
#include "flash/Morph.h"
#include "flash/Swf.h"
#include "flash/Text.h"

using namespace flash;

namespace {

// Positions are pixels of the surface drawn into, rows top down; `flip` is 1 when the
// surface's rows run bottom up (the window).
const char* kVertex = R"(
attribute vec2 pos;
uniform vec2 viewport;
uniform float flip;
varying vec2 dev;
void main() {
    dev = pos;
    float y = pos.y / viewport.y * 2.0;
    gl_Position = vec4(pos.x / viewport.x * 2.0 - 1.0, flip > 0.5 ? 1.0 - y : y - 1.0, 0.0, 1.0);
}
)";

const char* kStencilFragment = R"(
void main() { gl_FragColor = vec4(0.0); }
)";

// Paints a pixel (device position `dev`, its center) the way the software rasterizer's
// samplers do. Uploaded textures hold premultiplied ARGB words as bytes (B, G, R, A), so
// they are read .bgra. Modes: 0 solid color, 1 linear and 2 radial gradient (ramp in
// `tex`), 3 bitmap fill, 4 an uploaded texture placed 1:1 on device pixels.
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

// Filter passes read layers through at() and atAux(): texel q (integer coordinates, rows
// top down) of the layer's `size` content, transparent outside it, as the software
// filters treat pixels beyond the layer.
const char* kFilterCommon = R"(
varying vec2 dev;
uniform sampler2D src;
uniform sampler2D aux;
uniform vec2 texSize, auxTexSize;
uniform vec2 size;
vec4 at(sampler2D t, vec2 q) {
    if (q.x < 0.0 || q.y < 0.0 || q.x >= size.x || q.y >= size.y) return vec4(0.0);
    return texture2D(t, (q + 0.5) / texSize);
}
vec4 atAux(vec2 q) {
    if (q.x < 0.0 || q.y < 0.0 || q.x >= size.x || q.y >= size.y) return vec4(0.0);
    return texture2D(aux, (q + 0.5) / auxTexSize);
}
)";

// One box blur pass along `dir`: the mean of the 2r+1 texels around.
const char* kBlurFragment = R"(
uniform vec2 dir;
uniform float radius;
void main() {
    vec2 q = floor(dev);
    vec4 sum = vec4(0.0);
    for (int i = 0; i < 2048; i++) {
        float k = float(i) - radius;
        if (k > radius) break;
        sum += at(src, q + dir * k);
    }
    gl_FragColor = sum / (2.0 * radius + 1.0);
}
)";

// The alpha plane glows and bevels start from: shifted (shadows), inverted (inner glow).
const char* kAlphaFragment = R"(
uniform vec2 shift;
uniform bool inner;
void main() {
    float a = at(src, floor(dev) - shift).a;
    gl_FragColor = vec4(inner ? 1.0 - a : a);
}
)";

// Glow and drop shadow: the blurred alpha (aux) as a color, with the source (src).
const char* kGlowFragment = R"(
uniform vec4 color;  // straight
uniform float strength;
uniform bool inner, knockout, compositeSource;
void main() {
    vec2 q = floor(dev);
    vec4 s = at(src, q);
    float ga = min(1.0, atAux(q).a * strength) * color.a;
    vec4 o;
    if (ga <= 0.0 && !knockout && (!inner || compositeSource)) {
        o = compositeSource ? s : vec4(0.0);
    } else if (inner) {
        float m = ga * s.a;
        o = knockout ? vec4(color.rgb * m, m) : vec4(color.rgb * m + s.rgb * (1.0 - ga), s.a);
    } else if (knockout) {
        float m = ga * (1.0 - s.a);
        o = vec4(color.rgb * m, m);
    } else if (!compositeSource) {
        o = vec4(color.rgb * ga, ga);
    } else {
        float k = 1.0 - s.a;
        o = vec4(s.rgb + color.rgb * ga * k, s.a + ga * k);
    }
    o = clamp(o, 0.0, 1.0);
    gl_FragColor = vec4(min(o.rgb, vec3(o.a)), o.a);
}
)";

// Bevel: the blurred alpha (aux) compared across the light direction.
const char* kBevelFragment = R"(
uniform vec2 shift;
uniform float strength;
uniform vec4 highlight, shadow;  // straight
uniform bool full, inner, knockout;
void main() {
    vec2 q = floor(dev);
    vec4 s = at(src, q);
    float v = clamp((atAux(q + shift).a - atAux(q - shift).a) * strength, -1.0, 1.0);
    vec4 c = v >= 0.0 ? highlight : shadow;
    float mask = full ? 1.0 : inner ? s.a : 1.0 - s.a;
    float m = abs(v) * c.a * mask;
    vec4 bev = vec4(c.rgb * m, m);
    vec4 o;
    if (knockout) o = bev;
    else if (inner || full) o = vec4(bev.rgb + s.rgb * (1.0 - m), inner ? s.a : bev.a + s.a * (1.0 - m));
    else o = s + bev * (1.0 - s.a);
    o = clamp(o, 0.0, 1.0);
    gl_FragColor = vec4(min(o.rgb, vec3(o.a)), o.a);
}
)";

// Color matrix, in 0..255 units on straight color, as Flash defines it.
const char* kMatrixFragment = R"(
uniform mat4 matrix;
uniform vec4 offset;
void main() {
    vec4 p = at(src, floor(dev)) * 255.0;
    vec4 c = vec4(0.0, 0.0, 0.0, p.a);
    if (p.a > 0.0) c.rgb = p.rgb * 255.0 / p.a;
    vec4 o = clamp(matrix * c + offset, 0.0, 255.0) / 255.0;
    gl_FragColor = vec4(o.rgb * o.a, o.a);
}
)";

// Composites a layer (texture `src`, its texel (0, 0) at device `origin`) through a color
// transform and blend mode. Modes needing what is behind read it from `aux` (a copy of
// that area, texel (0, 0) at `origin` too, rows flipped when `auxFlip`).
const char* kCompositeFragment = R"(
varying vec2 dev;
uniform sampler2D src;
uniform sampler2D aux;
uniform vec2 texSize, auxSize, origin, size;
uniform bool bgra, cxOn, auxFlip;
uniform vec4 cxMul, cxAdd;
uniform int mode;

float blendChannel(float s, float d, float sa, float da) {
    if (mode == 3) return s * d + s * (1.0 - da) + d * (1.0 - sa);
    if (mode == 4) return s + d - s * d;
    if (mode == 5) return max(s * da, d * sa) + s * (1.0 - da) + d * (1.0 - sa);
    if (mode == 6) return min(s * da, d * sa) + s * (1.0 - da) + d * (1.0 - sa);
    if (mode == 7) return s + d - 2.0 * min(s * da, d * sa);
    if (mode == 9) return max(0.0, d - s);
    if (mode == 10) return sa * (da - d) + d * (1.0 - sa);
    if (mode == 13 || mode == 14) {
        float base = mode == 14 ? s : d;
        float baseA = mode == 14 ? sa : da;
        float t = 2.0 * base <= baseA ? 2.0 * s * d : sa * da - 2.0 * (da - d) * (sa - s);
        return t + s * (1.0 - da) + d * (1.0 - sa);
    }
    return s + d * (1.0 - sa);
}

void main() {
    vec2 q = floor(dev) - origin;
    vec4 s = texture2D(src, (q + 0.5) / texSize);
    if (bgra) s = s.bgra;
    if (cxOn) {
        if (s.a > 0.0) s.rgb /= s.a;
        s = clamp(s * cxMul + cxAdd, 0.0, 1.0);
        s.rgb *= s.a;
    }
    if (mode <= 2 || mode == 8 || mode == 11 || mode == 12) {
        gl_FragColor = s;  // blending does the rest
        return;
    }
    vec2 dq = auxFlip ? vec2(q.x, size.y - 1.0 - q.y) : q;
    vec4 d = texture2D(aux, (dq + 0.5) / auxSize);
    vec4 o = vec4(blendChannel(s.r, d.r, s.a, d.a), blendChannel(s.g, d.g, s.a, d.a),
                  blendChannel(s.b, d.b, s.a, d.a), 0.0);
    o.a = (mode == 9 || mode == 10) ? d.a : s.a + d.a * (1.0 - s.a);
    o = clamp(o, 0.0, 1.0);
    gl_FragColor = vec4(min(o.rgb, vec3(o.a)), o.a);
}
)";

IntRect intersect(const IntRect& a, const IntRect& b) {
    return {std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
}

bool needsLayer(const DisplayObject& o) { return !o.filters.empty() || o.blendMode >= BlendLayer || o.cacheAsLayer; }

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

std::vector<Filter> deviceFilters(const DisplayObject& o) {
    std::vector<Filter> filters = o.filters;
    for (Filter& f : filters) {
        f.blurX *= renderSettings.scale;
        f.blurY *= renderSettings.scale;
        f.distance *= renderSettings.scale;
    }
    return filters;
}

}  // namespace

GLint GpuRenderer::loc(GLuint program, const char* name) {
    std::string key = std::to_string(program) + "/" + name;
    auto it = uniforms_.find(key);
    if (it != uniforms_.end()) return it->second;
    GLint l = gl::GetUniformLocation(program, name);
    uniforms_[key] = l;
    return l;
}

bool GpuRenderer::init() {
    std::string filter = std::string(kFilterCommon);
    stencilProgram_ = gl::program(kVertex, kStencilFragment, {"pos"});
    paintProgram_ = gl::program(kVertex, kPaintFragment, {"pos"});
    blurProgram_ = gl::program(kVertex, (filter + kBlurFragment).c_str(), {"pos"});
    alphaProgram_ = gl::program(kVertex, (filter + kAlphaFragment).c_str(), {"pos"});
    glowProgram_ = gl::program(kVertex, (filter + kGlowFragment).c_str(), {"pos"});
    bevelProgram_ = gl::program(kVertex, (filter + kBevelFragment).c_str(), {"pos"});
    matrixProgram_ = gl::program(kVertex, (filter + kMatrixFragment).c_str(), {"pos"});
    compositeProgram_ = gl::program(kVertex, kCompositeFragment, {"pos"});
    if (!stencilProgram_ || !paintProgram_ || !blurProgram_ || !alphaProgram_ || !glowProgram_ || !bevelProgram_ ||
        !matrixProgram_ || !compositeProgram_)
        return false;
    GLuint p = paintProgram_;
    u_.viewport = loc(p, "viewport");
    u_.flip = loc(p, "flip");
    u_.mode = loc(p, "mode");
    u_.color = loc(p, "color");
    u_.inv0 = loc(p, "inv0");
    u_.inv1 = loc(p, "inv1");
    u_.spread = loc(p, "spread");
    u_.tex = loc(p, "tex");
    u_.texSize = loc(p, "texSize");
    u_.repeat = loc(p, "repeating");
    u_.smooth = loc(p, "smoothing");
    u_.cxOn = loc(p, "cxOn");
    u_.cxMul = loc(p, "cxMul");
    u_.cxAdd = loc(p, "cxAdd");
    stencilViewport_ = loc(stencilProgram_, "viewport");
    stencilFlip_ = loc(stencilProgram_, "flip");
    for (GLuint prog : {blurProgram_, alphaProgram_, glowProgram_, bevelProgram_, matrixProgram_, compositeProgram_}) {
        gl::UseProgram(prog);
        gl::Uniform1i(loc(prog, "src"), 0);
        gl::Uniform1i(loc(prog, "aux"), 1);
    }
    gl::UseProgram(paintProgram_);
    gl::Uniform1i(u_.tex, 0);
    rampTexture_ = makeTexture();
    gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 4);

    // What offscreen layers can be: multisampled (resolved by copying), float (for filter
    // intermediates), and which stencil format the driver takes.
    msaaLayers_ = gl::RenderbufferStorageMultisample && gl::BlitFramebuffer;
    floatLayers_ = !gl::isEs();
    auto findStencil = [&] {
        for (GLenum format : {gl::kDepth24Stencil8, (GLenum)GL_STENCIL_INDEX8}) {
            stencilFormat_ = format;
            Target probe;
            probe.withStencil = true;
            bool ok = allocate(probe, 16, 16);
            release(&probe);  // not in the pool: frees it
            if (ok) return;
        }
        stencilFormat_ = 0;
    };
    findStencil();
    if (!stencilFormat_ && msaaLayers_) {
        msaaLayers_ = false;  // try again without multisampling
        findStencil();
    }
    if (floatLayers_) {
        Target probe;
        probe.floating = true;
        floatLayers_ = allocate(probe, 16, 16);
        release(&probe);
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    std::printf("GPU layers: %s, %s stencil, %s filter passes\n", msaaLayers_ ? "multisampled" : "single-sampled",
                stencilFormat_ == gl::kDepth24Stencil8 ? "packed" : stencilFormat_ ? "8-bit" : "no",
                floatLayers_ ? "float" : "8-bit");
    return stencilFormat_ != 0;
}

// ---------------------------------------------------------------- render targets

bool GpuRenderer::allocate(Target& t, int w, int h) {
    t.texW = w;
    t.texH = h;
    t.texture = makeTexture();
    if (t.floating) gl::TexImage2D(GL_TEXTURE_2D, 0, gl::kRgba16f, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    else gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl::GenFramebuffers(1, &t.fbo);
    gl::BindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture, 0);
    bool ok = gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok && t.withStencil) {
        GLuint fb = t.fbo;
        if (msaaLayers_) {
            gl::GenFramebuffers(1, &t.msFbo);
            gl::BindFramebuffer(GL_FRAMEBUFFER, t.msFbo);
            gl::GenRenderbuffers(1, &t.msColor);
            gl::BindRenderbuffer(GL_RENDERBUFFER, t.msColor);
            gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, 4, gl::kRgba8, w, h);
            gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, t.msColor);
            fb = t.msFbo;
        }
        gl::GenRenderbuffers(1, &t.stencil);
        gl::BindRenderbuffer(GL_RENDERBUFFER, t.stencil);
        if (msaaLayers_) gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, 4, stencilFormat_, w, h);
        else gl::RenderbufferStorage(GL_RENDERBUFFER, stencilFormat_, w, h);
        gl::BindFramebuffer(GL_FRAMEBUFFER, fb);
        if (stencilFormat_ == gl::kDepth24Stencil8)
            gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.stencil);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, t.stencil);
        ok = gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    while (gl::GetError()) {
    }  // a failed probe leaves errors behind
    return ok;
}

GpuRenderer::Target* GpuRenderer::acquire(int w, int h, bool withStencil, bool floating) {
    floating = floating && floatLayers_;
    Target* best = nullptr;
    for (Target* t : pool_)
        if (!t->busy && t->withStencil == withStencil && t->floating == floating && t->texW >= w && t->texH >= h &&
            (!best || t->texW * t->texH < best->texW * best->texH))
            best = t;
    if (!best) {
        best = new Target;
        best->withStencil = withStencil;
        best->floating = floating;
        // Rounded up, so a layer that grows a little reuses its target.
        allocate(*best, (w + 63) / 64 * 64, (h + 63) / 64 * 64);
        pool_.push_back(best);
    }
    best->busy = true;
    best->w = w;
    best->h = h;
    return best;
}

void GpuRenderer::release(Target* t) {
    if (!t) return;
    if (std::find(pool_.begin(), pool_.end(), t) != pool_.end()) {
        t->busy = false;
        return;
    }
    // Not pooled: free it.
    if (t->texture) gl::DeleteTextures(1, &t->texture);
    if (t->fbo) gl::DeleteFramebuffers(1, &t->fbo);
    if (t->msFbo) gl::DeleteFramebuffers(1, &t->msFbo);
    if (t->msColor) gl::DeleteRenderbuffers(1, &t->msColor);
    if (t->stencil) gl::DeleteRenderbuffers(1, &t->stencil);
    bool stencil = t->withStencil, floating = t->floating;
    *t = Target{};
    t->withStencil = stencil;
    t->floating = floating;
}

void GpuRenderer::bind(const Surface& s) {
    surface_ = s;
    gl::BindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    gl::Viewport(0, 0, s.w, s.h);
    float flip = s.bottomUp ? 1.f : 0.f;
    for (GLuint p : {stencilProgram_, paintProgram_, compositeProgram_}) {
        gl::UseProgram(p);
        gl::Uniform2f(loc(p, "viewport"), (float)s.w, (float)s.h);
        gl::Uniform1f(loc(p, "flip"), flip);
    }
    gl::Enable(GL_SCISSOR_TEST);
    gl::Enable(GL_STENCIL_TEST);
    gl::Enable(GL_BLEND);
    gl::BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

GpuRenderer::Surface GpuRenderer::beginTarget(Target* t) {
    Surface previous = surface_;
    Surface s;
    s.fbo = t->msFbo ? t->msFbo : t->fbo;
    s.w = t->w;
    s.h = t->h;
    s.bottomUp = false;
    s.target = t;
    bind(s);
    setClip({0, 0, t->w, t->h});
    gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl::StencilMask(0xFF);
    gl::ClearColor(0, 0, 0, 0);
    gl::ClearStencil(0);
    gl::Clear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    return previous;
}

void GpuRenderer::finishTarget(Target* t, const Surface& previous) {
    if (t->msFbo) {
        gl::Disable(GL_SCISSOR_TEST);
        gl::BindFramebuffer(gl::kReadFramebuffer, t->msFbo);
        gl::BindFramebuffer(gl::kDrawFramebuffer, t->fbo);
        gl::BlitFramebuffer(0, 0, t->w, t->h, 0, 0, t->w, t->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    bind(previous);
}

void GpuRenderer::pass(GLuint program, Target* out) {
    gl::BindFramebuffer(GL_FRAMEBUFFER, out->fbo);
    gl::Viewport(0, 0, out->w, out->h);
    gl::Disable(GL_SCISSOR_TEST);
    gl::Disable(GL_STENCIL_TEST);
    gl::Disable(GL_BLEND);
    gl::UseProgram(program);
    gl::Uniform2f(loc(program, "viewport"), (float)out->w, (float)out->h);
    gl::Uniform1f(loc(program, "flip"), 0);
    gl::Uniform2f(loc(program, "size"), (float)out->w, (float)out->h);
    float x1 = (float)out->w, y1 = (float)out->h;
    const float quad[12] = {0, 0, x1, 0, x1, y1, 0, 0, x1, y1, 0, y1};
    gl::BindBuffer(GL_ARRAY_BUFFER, 0);
    gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    gl::DrawArrays(GL_TRIANGLES, 0, 6);
}

// ---------------------------------------------------------------- frame

void GpuRenderer::render(as::Player& player, const Movie& movie, int width, int height, double interpolation,
                         double scale, double originX, double originY) {
    movie_ = &movie;
    frame_++;
    layers = layersRedrawn = software = 0;
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

    GLint bound = 0;
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
    Surface root;
    root.fbo = (GLuint)bound;
    root.w = width;
    root.h = height;
    root.bottomUp = true;
    bind(root);
    gl::EnableVertexAttribArray(0);
    gl::ActiveTexture(GL_TEXTURE0);
    setClip({0, 0, width, height});
    gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl::StencilMask(0xFF);
    gl::ClearColor(movie.background.r / 255.f, movie.background.g / 255.f, movie.background.b / 255.f, 1.f);
    gl::ClearStencil(0);
    gl::Clear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
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

    // Layers of objects that didn't show for a while give their targets back, and spare
    // pooled targets beyond a few are freed.
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (frame_ - it->second.lastFrame > 60) {
            release(it->second.target);
            if (it->second.textField) delete it->second.target;
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
    int spare = 0;
    for (auto it = pool_.begin(); it != pool_.end();) {
        if (!(*it)->busy && ++spare > 8) {
            Target* t = *it;
            it = pool_.erase(it);
            release(t);  // no longer pooled: frees it
            delete t;
        } else {
            ++it;
        }
    }
}

void GpuRenderer::setClip(const IntRect& clip) {
    clip_ = clip;
    int y = surface_.bottomUp ? surface_.h - clip.y1 : clip.y0;
    gl::Scissor(clip.x0, y, std::max(0, clip.x1 - clip.x0), std::max(0, clip.y1 - clip.y0));
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
    if (dynamic_cast<TextField*>(&o)) drawTextField(o, parent, parentCx);
    else if (needsLayer(o)) drawLayer(o, parent, parentCx);
    else drawContent(o, parent * o.drawMatrix(), parentCx * o.cxform);
    if (o.displayClip) setClip(saved);
}

void GpuRenderer::drawContent(DisplayObject& o, const Matrix& m, const ColorTransform& cx) {
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
            drawMaskGroupLayer(mc, c, first, last, m, cx);
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

void GpuRenderer::drawMaskGroupLayer(MovieClip&, DisplayObject& mask,
                                     std::map<int, std::shared_ptr<DisplayObject>>::const_iterator first,
                                     std::map<int, std::shared_ptr<DisplayObject>>::const_iterator last,
                                     const Matrix& m, const ColorTransform& cx) {
    Rect b = (m * mask.drawMatrix()).apply(mask.localBounds());
    if (b.empty() || !std::isfinite(b.xMin) || !std::isfinite(b.yMin) || !std::isfinite(b.xMax) ||
        !std::isfinite(b.yMax))
        return;
    IntRect area = intersect(clip_, {(int)std::floor(b.xMin) - 1, (int)std::floor(b.yMin) - 1,
                                     (int)std::ceil(b.xMax) + 1, (int)std::ceil(b.yMax) + 1});
    if (area.empty()) return;
    layers++, layersRedrawn++;
    Target* t = acquire(area.x1 - area.x0, area.y1 - area.y0, true, false);
    IntRect savedClip = clip_;
    bool savedMask = inMask_;
    Surface previous = beginTarget(t);
    inMask_ = false;
    Matrix lm = Matrix::translate(-area.x0, -area.y0) * m;
    IntRect box = drawMask(mask, lm);
    if (!box.empty()) {
        inMask_ = true;
        for (auto n = first; n != last; ++n) drawObject(*n->second, lm, cx);
    }
    finishTarget(t, previous);
    inMask_ = savedMask;
    setClip(savedClip);
    composite(t->texture, t->texW, t->texH, area.x0, area.y0, t->w, t->h, ColorTransform{}, BlendNormal, false);
    release(t);
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
    for (const auto& s : segs) verts_.insert(verts_.end(), {ax, ay, (float)s.x0, (float)s.y0, (float)s.x1, (float)s.y1});
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
    gl::ActiveTexture(GL_TEXTURE0);
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
            gl::Uniform1i(u_.mode, 0);  // nothing to paint: just reset the counts below
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

// ---------------------------------------------------------------- layers and filters

void GpuRenderer::drawLayer(DisplayObject& o, const Matrix& parent, const ColorTransform& parentCx) {
    Matrix m = parent * o.drawMatrix();
    std::vector<Filter> filters = deviceFilters(o);
    int pad = filterPadding(filters);
    Rect b = m.apply(o.localBounds());
    if (b.empty() || !std::isfinite(b.xMin) || !std::isfinite(b.yMin) || !std::isfinite(b.xMax) ||
        !std::isfinite(b.yMax))
        return;
    // As the software renderer's layerArea(): the bounds and the filters' reach, within
    // the clip widened by that reach.
    IntRect area{std::max((int)std::floor(b.xMin) - pad, clip_.x0 - pad),
                 std::max((int)std::floor(b.yMin) - pad, clip_.y0 - pad),
                 std::min((int)std::ceil(b.xMax) + pad, clip_.x1 + pad),
                 std::min((int)std::ceil(b.yMax) + pad, clip_.y1 + pad)};
    if (area.empty()) return;
    int w = area.x1 - area.x0, h = area.y1 - area.y0;
    layers++;
    RenderHash hash;
    o.hashForRender(hash, false);
    hash.add(m);
    for (int v : {area.x0, area.y0, area.x1, area.y1}) hash.add((uint64_t)(int64_t)v);
    hash.add((uint64_t)renderSettings.crispBitmaps);
    CachedLayer& c = cache_[&o];
    c.lastFrame = frame_;
    if (!c.target || c.hash != hash.h || c.w != w || c.h != h) {
        layersRedrawn++;
        Target* t = acquire(w, h, true, false);
        IntRect savedClip = clip_;
        bool savedMask = inMask_;
        Surface previous = beginTarget(t);
        inMask_ = false;
        // The content without the object's own color transform (applied when compositing,
        // so it also colors glows and shadows, as in Flash).
        drawContent(o, Matrix::translate(-area.x0, -area.y0) * m, ColorTransform{});
        finishTarget(t, previous);
        inMask_ = savedMask;
        Target* result = applyFilters(t, filters);
        setClip(savedClip);
        if (c.target && c.target != result) release(c.target);
        c.target = result;
        c.hash = hash.h;
        c.x0 = area.x0;
        c.y0 = area.y0;
        c.w = w;
        c.h = h;
    }
    uint8_t mode = o.blendMode ? o.blendMode : (uint8_t)BlendNormal;
    composite(c.target->texture, c.target->texW, c.target->texH, c.x0, c.y0, c.w, c.h, parentCx * o.cxform, mode,
              false);
}

GpuRenderer::Target* GpuRenderer::blur(Target* src, double blurX, double blurY, int passes) {
    int rx = (int)std::lround(blurX / 2), ry = (int)std::lround(blurY / 2);
    int w = src->w, h = src->h;
    for (int p = 0; p < std::max(1, passes); p++)
        for (int axis = 0; axis < 2; axis++) {
            int r = axis == 0 ? rx : ry;
            if (r <= 0) continue;
            Target* out = acquire(w, h, false, true);
            gl::ActiveTexture(GL_TEXTURE0);
            gl::BindTexture(GL_TEXTURE_2D, src->texture);
            gl::UseProgram(blurProgram_);
            gl::Uniform2f(loc(blurProgram_, "texSize"), (float)src->texW, (float)src->texH);
            gl::Uniform2f(loc(blurProgram_, "dir"), axis == 0 ? 1.f : 0.f, axis == 0 ? 0.f : 1.f);
            gl::Uniform1f(loc(blurProgram_, "radius"), (float)r);
            pass(blurProgram_, out);
            release(src);
            src = out;
        }
    return src;
}

GpuRenderer::Target* GpuRenderer::applyFilters(Target* layer, const std::vector<Filter>& filters) {
    int w = layer->w, h = layer->h;
    auto twoTextures = [&](GLuint program, Target* src, Target* aux) {
        gl::ActiveTexture(GL_TEXTURE1);
        gl::BindTexture(GL_TEXTURE_2D, aux ? aux->texture : 0);
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(GL_TEXTURE_2D, src->texture);
        gl::UseProgram(program);
        gl::Uniform2f(loc(program, "texSize"), (float)src->texW, (float)src->texH);
        if (aux) gl::Uniform2f(loc(program, "auxTexSize"), (float)aux->texW, (float)aux->texH);
    };
    // The alpha plane of `src`, shifted by (dx, dy) and inverted for inner glows.
    auto alphaPlane = [&](Target* src, int dx, int dy, bool inner) {
        Target* a = acquire(w, h, false, true);
        twoTextures(alphaProgram_, src, nullptr);
        gl::Uniform2f(loc(alphaProgram_, "shift"), (float)dx, (float)dy);
        gl::Uniform1i(loc(alphaProgram_, "inner"), inner);
        pass(alphaProgram_, a);
        return a;
    };
    for (const Filter& f : filters) {
        switch (f.type) {
            case Filter::Blur: layer = blur(layer, f.blurX, f.blurY, f.passes); break;
            case Filter::Glow:
            case Filter::DropShadow: {
                bool shadow = f.type == Filter::DropShadow;
                int dx = shadow ? (int)std::lround(std::cos(f.angle) * f.distance) : 0;
                int dy = shadow ? (int)std::lround(std::sin(f.angle) * f.distance) : 0;
                Target* g = blur(alphaPlane(layer, dx, dy, f.inner), f.blurX, f.blurY, f.passes);
                Target* out = acquire(w, h, false, false);
                twoTextures(glowProgram_, layer, g);
                gl::Uniform4f(loc(glowProgram_, "color"), f.color.r / 255.f, f.color.g / 255.f, f.color.b / 255.f,
                              f.color.a / 255.f);
                gl::Uniform1f(loc(glowProgram_, "strength"), (float)f.strength);
                gl::Uniform1i(loc(glowProgram_, "inner"), f.inner);
                gl::Uniform1i(loc(glowProgram_, "knockout"), f.knockout);
                gl::Uniform1i(loc(glowProgram_, "compositeSource"), f.compositeSource);
                pass(glowProgram_, out);
                release(g);
                release(layer);
                layer = out;
                break;
            }
            case Filter::Bevel: {
                Target* bl = blur(alphaPlane(layer, 0, 0, false), f.blurX, f.blurY, f.passes);
                Target* out = acquire(w, h, false, false);
                twoTextures(bevelProgram_, layer, bl);
                int dx = (int)std::lround(std::cos(f.angle) * f.distance / 2);
                int dy = (int)std::lround(std::sin(f.angle) * f.distance / 2);
                gl::Uniform2f(loc(bevelProgram_, "shift"), (float)dx, (float)dy);
                gl::Uniform1f(loc(bevelProgram_, "strength"), (float)f.strength);
                gl::Uniform4f(loc(bevelProgram_, "highlight"), f.highlight.r / 255.f, f.highlight.g / 255.f,
                              f.highlight.b / 255.f, f.highlight.a / 255.f);
                gl::Uniform4f(loc(bevelProgram_, "shadow"), f.color.r / 255.f, f.color.g / 255.f, f.color.b / 255.f,
                              f.color.a / 255.f);
                gl::Uniform1i(loc(bevelProgram_, "full"), f.onTop);
                gl::Uniform1i(loc(bevelProgram_, "inner"), f.inner && !f.onTop);
                gl::Uniform1i(loc(bevelProgram_, "knockout"), f.knockout);
                pass(bevelProgram_, out);
                release(bl);
                release(layer);
                layer = out;
                break;
            }
            case Filter::ColorMatrix: {
                Target* out = acquire(w, h, false, false);
                twoTextures(matrixProgram_, layer, nullptr);
                // Rows of Flash's 4x5 matrix; GLSL matrices are given column by column.
                float cols[16];
                for (int row = 0; row < 4; row++)
                    for (int col = 0; col < 4; col++) cols[col * 4 + row] = f.matrix[row * 5 + col];
                gl::UniformMatrix4fv(loc(matrixProgram_, "matrix"), 1, GL_FALSE, cols);
                gl::Uniform4f(loc(matrixProgram_, "offset"), f.matrix[4], f.matrix[9], f.matrix[14], f.matrix[19]);
                pass(matrixProgram_, out);
                release(layer);
                layer = out;
                break;
            }
            default: break;  // gradient glow/bevel, convolution: unused by the game
        }
    }
    bind(surface_);  // back to the surface being drawn (pass() changed state)
    return layer;
}

void GpuRenderer::composite(GLuint texture, int texW, int texH, int x0, int y0, int w, int h,
                            const ColorTransform& cx, uint8_t mode, bool bgra) {
    IntRect area = intersect(clip_, {x0, y0, x0 + w, y0 + h});
    if (area.empty()) return;
    bool readsBehind = !(mode <= BlendLayer || mode == BlendAdd || mode == BlendAlpha || mode == BlendErase);
    Target* behind = nullptr;
    if (readsBehind) {
        // A copy of what is behind the layer (its whole box, rows top down where possible).
        behind = acquire(w, h, false, false);
        int sy0 = surface_.bottomUp ? surface_.h - (y0 + h) : y0;
        if (gl::BlitFramebuffer) {
            gl::Disable(GL_SCISSOR_TEST);
            gl::BindFramebuffer(gl::kReadFramebuffer, surface_.fbo);
            gl::BindFramebuffer(gl::kDrawFramebuffer, behind->fbo);
            // Flipped while copying when the surface's rows run bottom up.
            if (surface_.bottomUp)
                gl::BlitFramebuffer(x0, sy0, x0 + w, sy0 + h, 0, h, w, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            else
                gl::BlitFramebuffer(x0, sy0, x0 + w, sy0 + h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            bind(surface_);
        } else {
            gl::ActiveTexture(GL_TEXTURE0);
            gl::BindTexture(GL_TEXTURE_2D, behind->texture);
            gl::CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x0, sy0, w, h);
        }
        setClip(clip_);
    }
    GLuint p = compositeProgram_;
    gl::UseProgram(p);
    gl::ActiveTexture(GL_TEXTURE1);
    gl::BindTexture(GL_TEXTURE_2D, behind ? behind->texture : 0);
    gl::ActiveTexture(GL_TEXTURE0);
    gl::BindTexture(GL_TEXTURE_2D, texture);
    gl::Uniform2f(loc(p, "texSize"), (float)texW, (float)texH);
    gl::Uniform2f(loc(p, "auxSize"), behind ? (float)behind->texW : 1.f, behind ? (float)behind->texH : 1.f);
    gl::Uniform2f(loc(p, "origin"), (float)x0, (float)y0);
    gl::Uniform2f(loc(p, "size"), (float)w, (float)h);
    gl::Uniform1i(loc(p, "bgra"), bgra);
    gl::Uniform1i(loc(p, "auxFlip"), behind && !gl::BlitFramebuffer && surface_.bottomUp);
    gl::Uniform1i(loc(p, "mode"), mode);
    bool on = !cx.isIdentity();
    gl::Uniform1i(loc(p, "cxOn"), on);
    if (on) {
        gl::Uniform4f(loc(p, "cxMul"), cx.rMul, cx.gMul, cx.bMul, cx.aMul);
        gl::Uniform4f(loc(p, "cxAdd"), cx.rAdd / 255.f, cx.gAdd / 255.f, cx.bAdd / 255.f, cx.aAdd / 255.f);
    }
    if (inMask_) gl::StencilFunc(GL_EQUAL, 0x80, 0x80);
    else gl::StencilFunc(GL_ALWAYS, 0, 0xFF);
    gl::StencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    if (readsBehind) gl::Disable(GL_BLEND);
    else if (mode == BlendAdd) gl::BlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    else if (mode == BlendAlpha) gl::BlendFunc(GL_ZERO, GL_SRC_ALPHA);
    else if (mode == BlendErase) gl::BlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
    drawBox(area);
    gl::Enable(GL_BLEND);
    gl::BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (behind) release(behind);
}

// ---------------------------------------------------------------- text fields

void GpuRenderer::drawTextField(DisplayObject& o, const Matrix& parent, const ColorTransform& parentCx) {
    Matrix m = parent * o.drawMatrix();
    Rect b = m.apply(o.localBounds());
    if (b.empty() || !std::isfinite(b.xMin) || !std::isfinite(b.yMin) || !std::isfinite(b.xMax) ||
        !std::isfinite(b.yMax))
        return;
    IntRect area = intersect(clip_, {(int)std::floor(b.xMin) - 2, (int)std::floor(b.yMin) - 2,
                                     (int)std::ceil(b.xMax) + 2, (int)std::ceil(b.yMax) + 2});
    if (area.empty()) return;
    software++;
    RenderHash h;
    o.hashForRender(h, false);
    h.add(m);
    h.add(parentCx * o.cxform);
    for (int v : {area.x0, area.y0, area.x1, area.y1}) h.add((uint64_t)(int64_t)v);
    CachedLayer& c = cache_[&o];
    c.lastFrame = frame_;
    c.textField = true;
    int w = area.x1 - area.x0, hgt = area.y1 - area.y0;
    if (!c.target) c.target = new Target;
    if (!c.target->texture || c.hash != h.h || c.w != w || c.h != hgt) {
        flash::Surface s;
        s.resize(w, hgt);
        Matrix lp = Matrix::translate(-area.x0, -area.y0) * parent;
        renderDisplayObject(o, rast_, s, {0, 0, w, hgt}, lp, parentCx, *movie_, true);
        if (!c.target->texture) c.target->texture = makeTexture();
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(GL_TEXTURE_2D, c.target->texture);
        if (c.target->texW != w || c.target->texH != hgt) {
            gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, hgt, 0, GL_RGBA, GL_UNSIGNED_BYTE, s.pixels.data());
            c.target->texW = w;
            c.target->texH = hgt;
        } else {
            gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, hgt, GL_RGBA, GL_UNSIGNED_BYTE, s.pixels.data());
        }
        c.hash = h.h;
        c.x0 = area.x0;
        c.y0 = area.y0;
        c.w = w;
        c.h = hgt;
    }
    // Uploaded words are B, G, R, A bytes; the color transform is already applied.
    composite(c.target->texture, c.target->texW, c.target->texH, c.x0, c.y0, c.w, c.h, ColorTransform{}, BlendNormal,
              true);
}

// ---------------------------------------------------------------- presenting CPU frames

void GpuRenderer::upload(const flash::Surface& frame) {
    if (!frameTexture_) frameTexture_ = makeTexture();
    gl::ActiveTexture(GL_TEXTURE0);
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
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl::Viewport(0, 0, width, height);
    gl::Disable(GL_SCISSOR_TEST);
    gl::Disable(GL_STENCIL_TEST);
    gl::Disable(GL_BLEND);
    gl::ClearColor(0, 0, 0, 1);
    gl::Clear(GL_COLOR_BUFFER_BIT);
    if (!frameTexture_) return;
    gl::UseProgram(paintProgram_);
    gl::Uniform2f(u_.viewport, (float)width, (float)height);
    gl::Uniform1f(u_.flip, 1);
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
