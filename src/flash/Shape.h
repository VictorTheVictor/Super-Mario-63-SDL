#pragma once
// Vector shape definitions (DefineShape 1-4) converted into per-style edge lists
// that the rasterizer can fill directly.

#include <cstdint>
#include <vector>

#include "flash/Geom.h"

namespace flash {

class Reader;
struct Bitmap;

struct GradientStop {
    uint8_t ratio = 0;
    Color color;
};

struct FillStyle {
    enum Type : uint8_t {
        Solid = 0x00,
        LinearGradient = 0x10,
        RadialGradient = 0x12,
        FocalGradient = 0x13,
        RepeatingBitmap = 0x40,
        ClippedBitmap = 0x41,
        RepeatingBitmapHard = 0x42,  // non-smoothed
        ClippedBitmapHard = 0x43,
    };
    uint8_t type = Solid;
    Color color;
    Matrix matrix;  // gradient square / bitmap pixels -> shape space
    std::vector<GradientStop> stops;
    uint8_t spread = 0, interpolation = 0;
    double focal = 0;
    uint16_t bitmapId = 0;

    bool isBitmap() const { return type >= RepeatingBitmap && type <= ClippedBitmapHard; }
    bool isGradient() const { return type >= LinearGradient && type <= FocalGradient; }
};

struct LineStyle {
    double width = 20;  // twips
    Color color;
    bool hasFill = false;
    FillStyle fill;
    uint8_t startCap = 0, endCap = 0, join = 0;  // 0 round, 1 none/bevel, 2 square/miter
    bool noHScale = false, noVScale = false, noClose = false;
    double miterLimit = 3;
};

// A path segment in shape space. Straight edges have ctrl == midpoint and curved == false.
struct Edge {
    Point from, ctrl, to;
    bool curved = false;
};

struct FillPath {
    int style;                // index into Shape::fills
    std::vector<Edge> edges;  // oriented so that nonzero winding gives the fill
};

struct StrokePath {
    int style;  // index into Shape::lines
    std::vector<std::vector<Edge>> polylines;  // connected runs
};

struct ShapeDef {
    Rect bounds;
    std::vector<FillStyle> fills;
    std::vector<LineStyle> lines;
    // Each NewStyles record starts a layer; a layer draws its fills, then its strokes.
    struct Layer {
        std::vector<FillPath> fills;
        std::vector<StrokePath> strokes;
    };
    std::vector<Layer> layers;
};

// One SHAPERECORD with absolute coordinates.
struct ShapeRecord {
    bool isEdge = false;
    Edge edge;
    // style change
    bool moveTo = false, fill0Change = false, fill1Change = false, lineChange = false,
         newStyles = false;
    Point move;
    int fill0 = 0, fill1 = 0, line = 0;
    std::vector<FillStyle> fills;  // when newStyles
    std::vector<LineStyle> lines;
};

// Reads SHAPE records (starting with NumFillBits/NumLineBits) until the end record.
void readShapeRecords(Reader& r, int version, bool withStyles, std::vector<ShapeRecord>& out);
// Builds per-style fill/stroke paths from records; `out` holds the initial style group.
void buildPaths(const std::vector<ShapeRecord>& records, ShapeDef& out);

std::vector<FillStyle> parseFillStyles(Reader& r, int version);
std::vector<LineStyle> parseLineStyles(Reader& r, int version);

// Parses SHAPEWITHSTYLE (or SHAPE when withStyles == false, for fonts) following the
// DefineShape header. `version` is 1..4.
void parseShape(Reader& r, int version, ShapeDef& out, bool withStyles = true);

// True if `p` (shape space, twips) is inside a fill (nonzero winding) or within half a
// stroke width of a line.
bool shapeContains(const ShapeDef& shape, Point p);

// Parses a single FILLSTYLE record.
FillStyle parseFillStyle(Reader& r, int version);

}  // namespace flash
