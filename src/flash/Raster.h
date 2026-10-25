#pragma once
// Software rasterizer: anti-aliased nonzero fills via signed-area accumulation,
// with solid, gradient and bitmap paints. Output is premultiplied 0xAARRGGBB.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

#include "flash/Geom.h"
#include "flash/Shape.h"

namespace flash {

struct Bitmap;
class Movie;

struct Surface {
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;  // premultiplied ARGB

    void resize(int w, int h) {
        width = w;
        height = h;
        pixels.assign((size_t)w * h, 0);
    }
    void clear(uint32_t argb) { std::fill(pixels.begin(), pixels.end(), argb); }
};

struct IntRect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty() const { return x1 <= x0 || y1 <= y0; }
};

class LayerCache;
class DisplayObject;

// Settings for the frame being rendered.
struct RenderSettings {
    // Device pixels per stage pixel. Filters and minimum line widths are specified in
    // stage pixels, so they scale with this.
    double scale = 1;
    // Bitmaps drawn larger than their native size sample the nearest texel (crisp,
    // pixel-art look) instead of filtering bilinearly.
    bool crispBitmaps = true;
    // When set, filtered layers are rendered once per frame and shared between the
    // threads rendering bands of it (instead of each band redoing its padded strip).
    LayerCache* layerCache = nullptr;
    // Position between the previous game tick (0) and the current one (1) to draw
    // objects at, for rendering more often than the game ticks. 1 outside rendering, so
    // scripts and hit tests always see the real state.
    double interpolation = 1;
    // Objects left out of the frame, as if invisible (window presentation options).
    const std::vector<const DisplayObject*>* hidden = nullptr;

    bool isHidden(const DisplayObject* o) const {
        return hidden && std::find(hidden->begin(), hidden->end(), o) != hidden->end();
    }
};
extern RenderSettings renderSettings;

// Everything needed to color a covered pixel.
struct Paint {
    const FillStyle* style = nullptr;  // null => solid `color`
    Color color;
    Matrix toDevice;  // shape space (twips) -> device pixels
    ColorTransform cxform;
    const Bitmap* bitmap = nullptr;
};

class Rasterizer {
public:
    void reset() { segs_.clear(); }
    // Non-finite coordinates (from degenerate transforms) are dropped, huge ones clamped,
    // so scan conversion never indexes outside its buffer.
    void line(Point a, Point b);
    // Segments rejected because of NaN/infinite coordinates (diagnostics).
    static std::atomic<size_t> rejectedSegments;
    // Rows whose interpolated x fell outside the buffer before clamping (would have
    // written out of bounds without the clamp).
    static std::atomic<size_t> clampedRows;
    static std::atomic<double> maxOvershoot;  // largest distance outside [0, width] seen, in pixels
    void quad(Point a, Point c, Point b);  // flattened quadratic Bezier
    void closedPolygon(const Point* pts, int n);
    bool hasGeometry() const { return !segs_.empty(); }

    // Fills accumulated geometry (device pixel coordinates) into `dst`.
    void fill(Surface& dst, const IntRect& clip, const Paint& paint);

    // Adds a shape's fill path, transformed to device space.
    void addPath(const std::vector<Edge>& edges, const Matrix& m);
    // Adds the stroked outline of a polyline (round joins and caps).
    void addStroke(const std::vector<Edge>& edges, const Matrix& m, double halfWidthPx);

private:
    struct Seg {
        double x0, y0, x1, y1;
    };
    std::vector<Seg> segs_;
    std::vector<float> acc_;           // coverage accumulation; all zero between fills
    std::vector<int> rowMin_, rowMax_;  // touched cell range per row of the current fill
};

// Draws a whole shape definition with the given transform.
void drawShape(Rasterizer& r, Surface& dst, const IntRect& clip, const ShapeDef& shape,
               const Matrix& toDevice, const ColorTransform& cx, const Movie& movie);

}  // namespace flash
