#pragma once
// Bitmap filters and blend-mode compositing on premultiplied ARGB layers.

#include <vector>

#include "flash/Geom.h"
#include "flash/Raster.h"
#include "flash/Swf.h"

namespace flash {

enum BlendMode : uint8_t {
    BlendNormal = 1, BlendLayer = 2, BlendMultiply = 3, BlendScreen = 4, BlendLighten = 5,
    BlendDarken = 6, BlendDifference = 7, BlendAdd = 8, BlendSubtract = 9, BlendInvert = 10,
    BlendAlpha = 11, BlendErase = 12, BlendOverlay = 13, BlendHardlight = 14,
};

// Extra pixels a filter list can spread content by, per side.
int filterPadding(const std::vector<Filter>& filters);

// Applies filters in order. `layer` must already be padded by filterPadding().
void applyFilters(Surface& layer, const std::vector<Filter>& filters);

// Composites `layer` (positioned with its top-left at x0,y0 in dst) onto `dst`
// through the color transform and blend mode, limited to `clip`.
void compositeLayer(Surface& dst, const IntRect& clip, const Surface& layer, int x0, int y0,
                    uint8_t blendMode, const ColorTransform& cx);

// Multiplies each layer pixel by the alpha of `mask` (same size and origin).
void applyMask(Surface& layer, const Surface& mask);

}  // namespace flash
