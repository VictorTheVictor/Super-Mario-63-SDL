#pragma once
// OpenGL ES 2.0 renderer for the player's display list (F8 switches between it and the
// software renderer), drawing what the software renderer draws, the same way.
//
// Shapes (fills and strokes), morph shapes, static text, drawing-API shapes and bitmap
// fills: each fill's outline (flattened by the software rasterizer's own path code, so
// the geometry is the same) is drawn as a triangle fan into the stencil buffer counting
// nonzero winding, then a box over it is painted where the count isn't zero, with the
// fill's color, gradient or bitmap worked out per pixel by a shader. Edges are
// antialiased by multisampling. A mask sets the stencil's top bit where it covers, and
// the content it masks only paints there.
//
// An object with filters, a blend mode or a cached layer (cacheAsLayer) is drawn into
// an offscreen layer (a render target with its own stencil), filtered there by shader
// passes that do the software filters' arithmetic (box blurs, alpha shifts, glow, bevel
// and color matrix combines), and composited through its color transform and blend mode
// (modes other than normal and add read back what is behind them). Finished layers are
// kept while the object looks the same. A mask inside masked content draws its group
// in a layer too. Text fields draw their glyph shapes like static text.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "flash/DisplayObject.h"
#include "flash/Raster.h"
#include "gpu/Gl.h"

namespace as {
class Player;
}
namespace flash {
class DisplayObject;
class MovieClip;
class Movie;
struct ShapeDef;
struct Bitmap;
struct Filter;
}  // namespace flash

class GpuRenderer {
public:
    // Builds the shaders; needs a current context with gl::load() done. False on failure.
    bool init();

    // Draws the player's frame into the bound framebuffer (`width` x `height` pixels),
    // the stage at `scale` device pixels per stage pixel with its origin at (originX,
    // originY), like as::Player::render does on the CPU.
    void render(as::Player& player, const flash::Movie& movie, int width, int height, double interpolation,
                double scale, double originX, double originY);

    // A frame rendered on the CPU (premultiplied ARGB), shown over the whole viewport.
    void upload(const flash::Surface& frame);
    void present(int width, int height);

    // Last frame: layers drawn (and how many of those were redrawn rather than reused),
    // and text fields.
    int layers = 0, layersRedrawn = 0, textFields = 0;

private:
    // An offscreen render target: a texture, drawn into through a multisampled buffer
    // (resolved into it) where the driver can, with a stencil buffer when shapes are drawn
    // into it. Rows run top down, like the frame's.
    struct Target {
        GLuint fbo = 0, texture = 0, msFbo = 0, msColor = 0, stencil = 0;
        int texW = 0, texH = 0;  // allocated size (sizes are rounded up for reuse)
        int w = 0, h = 0;        // size in use
        bool withStencil = false, floating = false, busy = false;
    };
    // Where drawing goes: a framebuffer, its size, and whether rows run bottom up (the
    // window's, and the headless screenshot's) or top down (layers).
    struct Surface {
        GLuint fbo = 0;
        int w = 0, h = 0;
        bool bottomUp = false;
        Target* target = nullptr;
    };
    struct CachedLayer {
        Target* target = nullptr;  // owned; not in the pool
        uint64_t hash = 0;
        int x0 = 0, y0 = 0, w = 0, h = 0;
        uint64_t lastFrame = 0;
    };

    void drawObject(flash::DisplayObject& o, const flash::Matrix& parent, const flash::ColorTransform& parentCx,
                    bool ignoreVisible = false);
    void drawContent(flash::DisplayObject& o, const flash::Matrix& m, const flash::ColorTransform& cx);
    void drawChildren(flash::MovieClip& mc, const flash::Matrix& m, const flash::ColorTransform& cx);
    // `key` (with `variant`) identifies the shape for its cached outlines; null: build them
    // each time (drawing-API shapes, which change).
    void drawShape(const flash::ShapeDef& shape, const flash::Matrix& m, const flash::ColorTransform& cx,
                   const void* key, uint32_t variant);
    void drawGlyph(const flash::ShapeDef& glyph, const flash::Matrix& m, flash::Color color,
                   const flash::ColorTransform& cx);
    void drawStaticText(flash::DisplayObject& o, const flash::Matrix& m, const flash::ColorTransform& cx);
    // Fills the rasterizer's accumulated outline with a paint (`style` null: solid `color`).
    void fill(const flash::FillStyle* style, flash::Color color, const flash::Matrix& toDevice,
              const flash::ColorTransform& cx);
    // Paints `box` where the stencil counted an outline, resetting the counts.
    void paint(const flash::IntRect& box, const flash::FillStyle* style, flash::Color color,
               const flash::Matrix& toDevice, const flash::ColorTransform& cx);
    // An object with filters, a blend mode or cacheAsLayer: its layer, composited.
    void drawLayer(flash::DisplayObject& o, const flash::Matrix& parent, const flash::ColorTransform& parentCx);
    // A text field: its background, border and glyphs, clipped to its bounds.
    void drawTextField(flash::DisplayObject& o, const flash::Matrix& m, const flash::ColorTransform& cx);
    // A mask group under a mask: drawn in a layer of its own, then composited.
    void drawMaskGroupLayer(flash::MovieClip& mc, flash::DisplayObject& mask,
                            std::map<int, std::shared_ptr<flash::DisplayObject>>::const_iterator first,
                            std::map<int, std::shared_ptr<flash::DisplayObject>>::const_iterator last,
                            const flash::Matrix& m, const flash::ColorTransform& cx);
    // Masks: the mask's shapes set the stencil's top bit (returns the device box they cover).
    flash::IntRect drawMask(flash::DisplayObject& mask, const flash::Matrix& parent);
    void addMaskShape(const flash::ShapeDef& shape, const flash::Matrix& m, flash::IntRect& box);
    // Stencil-counts the accumulated outline; false (and nothing drawn) if it is empty or
    // outside the clip. `box` is the device box it covers.
    bool stencilOutline(flash::IntRect& box);
    // An outline flattened once: its fan triangles (device pixels, translation left out)
    // in a vertex buffer, and their bounds.
    struct GeoKey {
        const void* shape;
        uint32_t variant, part;
        double a, b, c, d, halfWidth;
        bool operator==(const GeoKey& o) const {
            return shape == o.shape && variant == o.variant && part == o.part && a == o.a && b == o.b && c == o.c &&
                   d == o.d && halfWidth == o.halfWidth;
        }
    };
    struct GeoKeyHash {
        size_t operator()(const GeoKey& k) const {
            flash::RenderHash h;
            h.add((uint64_t)(uintptr_t)k.shape);
            h.add((uint64_t)k.variant << 32 | k.part);
            for (double v : {k.a, k.b, k.c, k.d, k.halfWidth}) h.add(v);
            return (size_t)h.h;
        }
    };
    struct Geometry {
        GLuint vbo = 0;
        GLsizei count = 0;
        double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        uint64_t lastFrame = 0;
        bool built = false;
    };
    const Geometry& geometry(const GeoKey& key, const std::function<void(flash::Rasterizer&)>& build);
    bool stencilGeometry(const Geometry& g, double ox, double oy, flash::IntRect& box);
    void startStencil();
    void drawBox(const flash::IntRect& box);
    void setClip(const flash::IntRect& clip);
    GLuint bitmapTexture(const flash::Bitmap& b);

    // Render targets.
    Target* acquire(int w, int h, bool withStencil, bool floating);
    void release(Target* t);
    bool allocate(Target& t, int w, int h);
    void bind(const Surface& s);
    // Starts drawing into `t` (cleared); returns what was being drawn into, for finish().
    Surface beginTarget(Target* t);
    void finishTarget(Target* t, const Surface& previous);  // resolves, then draws into `previous` again
    // Runs a shader pass over all of `out` (blending, stencil and scissor off).
    void pass(GLuint program, Target* out);
    // Applies filters to `layer` (contents w x h); returns the target holding the result
    // (`layer` itself or another; the other one is released).
    Target* applyFilters(Target* layer, const std::vector<flash::Filter>& filters);
    Target* blur(Target* src, double blurX, double blurY, int passes);
    // Composites a texture (its top-left w x h, placed at device x0, y0) onto the current
    // surface through a color transform and blend mode.
    // `bgra`: the texture was uploaded from ARGB words (bytes B, G, R, A).
    void composite(GLuint texture, int texW, int texH, int x0, int y0, int w, int h,
                   const flash::ColorTransform& cx, uint8_t blendMode, bool bgra);

    GLuint paintProgram_ = 0;
    GLuint blurProgram_ = 0, alphaProgram_ = 0, glowProgram_ = 0, bevelProgram_ = 0, matrixProgram_ = 0,
           compositeProgram_ = 0;
    struct {
        GLint viewport, flip, offset, mode, color, inv0, inv1, spread, tex, texSize, repeat, smooth, cxOn, cxMul, cxAdd;
    } u_{};
    GLuint rampTexture_ = 0, frameTexture_ = 0;
    int frameW_ = 0, frameH_ = 0;
    bool msaaLayers_ = false, floatLayers_ = false;
    GLenum stencilFormat_ = 0;

    const flash::Movie* movie_ = nullptr;
    flash::Rasterizer rast_;
    std::vector<float> verts_;
    Surface surface_;  // being drawn into
    flash::IntRect clip_;
    bool inMask_ = false;  // drawing content under a mask (the stencil's top bit)
    uint64_t frame_ = 0;
    std::unordered_map<const flash::Bitmap*, GLuint> bitmaps_;
    std::vector<Target*> pool_;
    std::unordered_map<const flash::DisplayObject*, CachedLayer> cache_;
    std::unordered_map<GeoKey, Geometry, GeoKeyHash> geometry_;
    std::unordered_map<std::string, GLint> uniforms_;  // "program/name" -> location
    GLint loc(GLuint program, const char* name);
};
