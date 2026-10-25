#pragma once
// OpenGL ES 2.0 renderer for the player's display list (F8 switches between it and the
// software renderer).
//
// Shapes (fills and strokes), morph shapes, static text, drawing-API shapes and bitmap
// fills draw on the GPU: each fill's outline (flattened by the software rasterizer's own
// path code, so the geometry is the same) is drawn as a triangle fan into the stencil
// buffer counting nonzero winding, then a box over it is painted where the count isn't
// zero, with the fill's color, gradient or bitmap worked out per pixel by a shader.
// Edges are antialiased by multisampling. A mask sets the stencil's top bit where it
// covers, and the content it masks only paints there.
//
// What the GPU path doesn't do yet (filters, blend modes, text fields, masks inside
// masks) is drawn by the software renderer into a texture, kept while that object looks
// the same, and drawn as a textured box.

#include <cstdint>
#include <unordered_map>
#include <vector>

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
}  // namespace flash

class GpuRenderer {
public:
    ~GpuRenderer();
    // Builds the shaders; needs a current context with gl::load() done. False on failure.
    bool init();

    // Draws the player's frame into the current framebuffer (`width` x `height` pixels),
    // the stage at `scale` device pixels per stage pixel with its origin at (originX,
    // originY), like as::Player::render does on the CPU.
    void render(as::Player& player, const flash::Movie& movie, int width, int height, double interpolation,
                double scale, double originX, double originY);

    // A frame rendered on the CPU (premultiplied ARGB), shown over the whole viewport.
    void upload(const flash::Surface& frame);
    void present(int width, int height);

    // Objects drawn by the software renderer in the last frame, and how many of those
    // were redrawn rather than reused.
    int fallbacks = 0, fallbacksRedrawn = 0;

private:
    struct Fallback {
        GLuint texture = 0;
        int texW = 0, texH = 0;
        int x0 = 0, y0 = 0, w = 0, h = 0;
        uint64_t hash = 0;
        uint64_t lastFrame = 0;
    };

    void drawObject(flash::DisplayObject& o, const flash::Matrix& parent, const flash::ColorTransform& parentCx,
                    bool ignoreVisible = false);
    void drawChildren(flash::MovieClip& mc, const flash::Matrix& m, const flash::ColorTransform& cx);
    void drawShape(const flash::ShapeDef& shape, const flash::Matrix& m, const flash::ColorTransform& cx);
    void drawStaticText(flash::DisplayObject& o, const flash::Matrix& m, const flash::ColorTransform& cx);
    // Fills the rasterizer's accumulated outline with a paint (`style` null: solid `color`).
    void fill(const flash::FillStyle* style, flash::Color color, const flash::Matrix& toDevice,
              const flash::ColorTransform& cx);
    // Draws the object (under `parent`) with the software renderer into a texture.
    void drawFallback(flash::DisplayObject& o, const flash::Matrix& parent, const flash::ColorTransform& parentCx);
    // Masks: the mask's shapes set the stencil's top bit (returns the device box they cover).
    flash::IntRect drawMask(flash::DisplayObject& mask, const flash::Matrix& parent);
    void addMaskShape(const flash::ShapeDef& shape, const flash::Matrix& m, flash::IntRect& box);
    // Stencil-counts the accumulated outline; false (and nothing drawn) if it is empty or
    // outside the clip. `box` is the device box it covers.
    bool stencilOutline(flash::IntRect& box);
    void drawBox(const flash::IntRect& box);
    void setClip(const flash::IntRect& clip);
    GLuint bitmapTexture(const flash::Bitmap& b);

    GLuint stencilProgram_ = 0, paintProgram_ = 0;
    struct {
        GLint viewport, mode, color, inv0, inv1, spread, tex, texSize, repeat, smooth, cxOn, cxMul, cxAdd;
    } u_{};
    GLint stencilViewport_ = -1;
    GLuint rampTexture_ = 0, frameTexture_ = 0;
    int frameW_ = 0, frameH_ = 0;

    const flash::Movie* movie_ = nullptr;
    flash::Rasterizer rast_;
    std::vector<float> verts_;
    int width_ = 0, height_ = 0;
    flash::IntRect clip_;
    bool inMask_ = false;  // drawing content under a mask (the stencil's top bit)
    uint64_t frame_ = 0;
    std::unordered_map<const flash::Bitmap*, GLuint> bitmaps_;
    std::unordered_map<const flash::DisplayObject*, Fallback> fallbackCache_;
};
