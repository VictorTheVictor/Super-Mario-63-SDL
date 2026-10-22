#pragma once

#include "level.hpp"
#include "fludd.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include <array>
#include <limits>

namespace sm63 {
class Terrain {
public:
    using Point=std::array<float,2>;
    using Polygon=std::vector<Point>;
    std::vector<Polygon> polygons;
    std::vector<level::Span> ice;
    struct Water {float left,top,right,bottom;bool cosmetic=false;};
    std::vector<Water> water;
    bool wet(float x,float y) const {for(const auto& w:water)if(x>=w.left && x<w.right && y>=w.top && y<w.bottom)return true;return false;}
    bool icy(float x,float y) const {
        for(const auto& span:ice)if(span.y==static_cast<int>(std::floor(y)) && x>=span.x0 && x<span.x1)return true;
        return false;
    }
    static bool overlaps(const Polygon& poly,float x,float y,float halfWidth,float height) {
        // SAT against the original convex XFL outline; touching is not penetration.
        const float cy=y-height/2;
        auto separated=[&](float ax,float ay) {
            float low=std::numeric_limits<float>::max(),high=-low;
            for(auto p:poly){float dot=p[0]*ax+p[1]*ay;low=std::min(low,dot);high=std::max(high,dot);}
            const float center=x*ax+cy*ay,extent=std::abs(ax)*halfWidth+std::abs(ay)*height/2;
            return center+extent<=low+.0001f || center-extent>=high-.0001f;
        };
        if(separated(1,0)||separated(0,1))return false;
        for(std::size_t i=0;i<poly.size();++i) {
            auto a=poly[i],b=poly[(i+1)%poly.size()];
            if(separated(a[1]-b[1],b[0]-a[0]))return false;
        }
        return true;
    }
    struct Block { float left, top, right, bottom; bool active = true; };
    std::vector<Block> blocks;
    struct Surface { float x1,y1,x2,y2; };
    std::vector<Surface> surfaces;
    static bool surfaceHeight(const Surface& s,float x,float& height) {
        const float left=std::max(x-7,s.x1),right=std::min(x+7,s.x2);
        if(left>right || s.x2-s.x1<.001f)return false;
        auto at=[&](float u){return s.y1+(s.y2-s.y1)*(u-s.x1)/(s.x2-s.x1);};
        height=std::min(at(left),at(right));return true;
    }
    int minX=level::minX,minY=level::minY,width=level::width,rows=level::height;
    Terrain() { load(minX,minY,width,rows,level::spans); }
    template<class Spans> void load(int x,int y,int w,int h,const Spans& spans) {
        minX=x;minY=y;width=w;rows=h;blocks.clear();surfaces.clear();polygons.clear();ice.clear();water.clear();cells_.assign(width*rows,0);
        for(const auto& span:spans)for(int col=span.x0;col<span.x1;++col)
            cells_[(span.y-minY)*width+col-minX]|=span.flag;
    }
    int at(int x, int y) const {
        for (const auto& b : blocks) if (b.active && x >= b.left && x < b.right && y >= b.top && y < b.bottom) return 1;
        x -= minX;
        y -= minY;
        if (x < 0 || y < 0 || x >= width || y >= rows) return 0;
        return cells_[y * width + x];
    }
    bool body(float x, float y, float halfWidth = 8, float height = 34) const {
        for(const auto& poly:polygons)if(overlaps(poly,x,y,halfWidth,height))return true;
        for (const auto& b : blocks)
            if (b.active && x+halfWidth > b.left && x-halfWidth < b.right && y > b.top && y-height < b.bottom) return true;
        x -= minX;
        y -= minY;
        const int firstRow = std::max(0, static_cast<int>(std::floor(y - height)));
        const int lastRow = std::min(rows, static_cast<int>(std::ceil(y)));
        const int firstCol = std::max(0, static_cast<int>(std::floor(x - halfWidth)));
        const int lastCol = std::min(width, static_cast<int>(std::ceil(x + halfWidth)));
        for (int row = firstRow; row < lastRow; ++row)
            for (int col = firstCol; col < lastCol; ++col)
                if (cells_[row * width + col] & 1) return true;
        return false;
    }
    bool oneWayLanding(float x,float from,float to,float& top) const {
        bool found=false;float best=to+1;
        for(const auto& surface:surfaces) {
            float y=0;
            if(surfaceHeight(surface,x,y) && from<=y+.001f && to>=y && y<best){best=y;found=true;}
        }
        for(int row=static_cast<int>(std::ceil(from-.001f));row<=static_cast<int>(std::floor(to)) && row<best;++row)
            for(int col=static_cast<int>(std::floor(x-7));col<=static_cast<int>(std::floor(x+7));++col)
                if((at(col,row)&2) && !(at(col,row-1)&2)){best=static_cast<float>(row);found=true;break;}
        if(found)top=best;
        return found;
    }

private:
    std::vector<unsigned char> cells_;
};

struct Input { bool left = false, right = false, jump = false, pound = false, dive = false, spin = false, fludd = false; };
enum class Pound { None, Windup, Falling, Landing };

struct Player {
    Fludd fludd;
    float x = 0, y = 0, vx = 0, vy = 0;
    bool respawned=false;
    int health = 8, invincibleTicks = 0, hurtTicks = 0;
    bool grounded = false, facingLeft = false;
    bool swimming=false;int oxygen=8,breathTicks=0,swimTicks=0;
    int wingTicks=0;bool flying=false,wingLaunch=false;float flightAngle=0;
    int slingTicks=0;
    int jumpBuffer = 0;
    bool wasJump = false;
    bool running = false;
    int tapDirection = 0, tapWindow = 0, previousDirection = 0, runTicks = 0;
    int jumpChain = 0, chainWindow = 0, flipTicks = 0;
    float walkPhase = 1, runPhase = 1;
    Pound pound = Pound::None;
    int poundTicks = 0;
    bool wasPound = false;

    float spinSpeed = 0, spinPhase = 0;
    bool wasSpin = false;
    bool diving = false, wasDive = false;
    int diveTicks = 0, diveRecovery = 0, diveFlipTicks = 0;
    bool backflip = false;
    float wallX = 0, wallY = 0, wallSpeed = 0;
    int wallSide = 0; // Contact this tick: -1 left, +1 right.

    void reset() { *this = Player{}; }
    static float friction(float speed, float subtract, float divide) {
        // Explicit sign branch follows the AS2 helper. Keep the release ceiling
        // regression: GCC 16 -O3 miscomputed the sweep count with copysign here.
        const float magnitude = std::max(0.0f, (speed < 0 ? -speed : speed) - subtract) / divide;
        return speed < 0 ? -magnitude : magnitude;
    }
    void tick(const Terrain& terrain, const Input& input);
};
} // namespace sm63
