// Integration tests for the MovieClip API and AVM1 event order, run against the game's
// SWF (needed only for its library symbols). Test scripts are registered on the root
// timeline's first frame action, replacing the game's own script there.
//
// Usage: stage_test path/to/Super_Mario_63_2010.swf

#include <cmath>
#include <cstdio>
#include <string>

#include "as/Builtins.h"
#include "as/Player.h"
#include "flash/Raster.h"
#include "flash/Swf.h"

using namespace as;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        checks++;                                                       \
        if (!(cond)) {                                                  \
            failures++;                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)
#define NEAR(a, b) (std::fabs(Value(a).toNumber() - (double)(b)) < 1e-6)

static std::string g_symbol;  // an exported sprite with content and several frames

static Value log() { return Value(Runtime::get().global)["log"]; }

static void rootFrame1(Activation& scope) {
    scope["stop"]();  // unqualified call resolves to this timeline's stop()
    Value root = scope["_root"];
    root["log"] = arr();
    root["booted"] = true;

    Value mc = root["attachMovie"](g_symbol.c_str(), "probe", 5, obj({{"_x", 100}, {"_y", 50}}));
    mc["onLoad"] = scope.function([](Activation& scope) -> Value {
        scope["_root"]["log"]["push"]("load");
        return undefined;
    });
    mc["onEnterFrame"] = scope.function([](Activation& scope) -> Value {
        scope["_root"]["log"]["push"]("enterFrame:" + scope.self["_name"]);
        return undefined;
    });
    mc["onUnload"] = scope.function([](Activation& scope) -> Value {
        scope["_root"]["log"]["push"]("unload");
        return undefined;
    });
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: stage_test game.swf\n");
        return 2;
    }
    flash::Movie movie;
    std::string err;
    if (!movie.load(argv[1], err)) {
        std::fprintf(stderr, "load failed: %s\n", err.c_str());
        return 1;
    }
    // Pick an exported multi-frame sprite with visible content.
    for (auto& [name, id] : movie.exports()) {
        auto* c = movie.character(id);
        if (c && c->kind == flash::Character::Sprite && static_cast<flash::Sprite*>(c)->frameCount >= 3 &&
            static_cast<flash::Sprite*>(c)->places.size() >= 2) {
            g_symbol = name;
            break;
        }
    }
    CHECK(!g_symbol.empty());

    registerFrameScript(0, 0, rootFrame1);
    Player player(movie);
    Value root = valueOf(&player.root());
    Value G = Value(Runtime::get().global);

    // Frame 1 ran: stop() held the root, the init object positioned the clip.
    CHECK(root["booted"] == true);
    player.tick();
    player.tick();
    CHECK(player.root().currentFrame() == 1);
    Value mc = root["probe"];
    CHECK(mc.typeOf() == std::string("movieclip"));
    CHECK(NEAR(mc["_x"], 100) && NEAR(mc["_y"], 50));
    CHECK(mc.toString() == "_level0.probe");
    CHECK(mc["_parent"] == root);
    CHECK(mc["getDepth"]() == 5);

    // Event order: onLoad once, then one onEnterFrame per tick.
    Value l = root["log"];
    CHECK(l[0] == "load");
    CHECK(l[1] == "enterFrame:probe");
    CHECK(l["length"] == 3);

    // Transform properties round-trip like Flash (cached scale/rotation).
    mc["_xscale"] = -100;
    CHECK(NEAR(mc["_xscale"], -100));
    mc["_rotation"] = 270;
    CHECK(NEAR(mc["_rotation"], -90));
    mc["_rotation"] = 0;
    mc["_xscale"] = 100;
    mc["_alpha"] = 40;
    CHECK(NEAR(mc["_alpha"], 40));
    mc["_alpha"] = 100;
    mc["_visible"] = false;
    CHECK(mc["_visible"] == false);
    mc["_visible"] = true;
    double w = mc["_width"].get().toNumber();
    mc["_xscale"] = 200;
    CHECK(NEAR(mc["_width"], 2 * w));
    mc["_xscale"] = 100;

    // Timeline control.
    mc["gotoAndStop"](3);
    CHECK(mc["_currentframe"] == 3);
    mc["prevFrame"]();
    CHECK(mc["_currentframe"] == 2);
    player.tick();
    CHECK(mc["_currentframe"] == 2);  // stopped
    mc["play"]();
    player.tick();
    CHECK(mc["_currentframe"] == 3);

    // Script-moved clips ignore later timeline matrix updates.
    mc["_x"] = 123;
    mc["gotoAndStop"](1);
    mc["gotoAndStop"](3);
    CHECK(NEAR(mc["_x"], 123));

    // Depths.
    CHECK(root["getNextHighestDepth"]() == 6);
    Value other = root["attachMovie"](g_symbol.c_str(), "other", 20);
    CHECK(root["getNextHighestDepth"]() == 21);
    mc["swapDepths"](other);
    CHECK(mc["getDepth"]() == 20 && other["getDepth"]() == 5);
    CHECK(root["getInstanceAtDepth"](20) == mc);
    mc["swapDepths"](30);
    CHECK(mc["getDepth"]() == 30);

    // hitTest: bounds vs shape, both in stage pixels.
    Value b = mc["getBounds"](root);
    double cx = (b["xMin"].get().toNumber() + b["xMax"].get().toNumber()) / 2;
    double cy = (b["yMin"].get().toNumber() + b["yMax"].get().toNumber()) / 2;
    CHECK(mc["hitTest"](cx, cy) == true);
    CHECK(mc["hitTest"](b["xMax"].get().toNumber() + 50, cy) == false);
    bool shapeHit = mc["hitTest"](cx, cy, true).toBoolean();
    bool boundsHit = mc["hitTest"](cx, cy, false).toBoolean();
    CHECK(!shapeHit || boundsHit);  // shape hits are always inside the bounds
    CHECK(mc["hitTest"](-1000, -1000, true) == false);
    other["_x"] = mc["_x"];
    other["_y"] = mc["_y"];
    CHECK(mc["hitTest"](other) == true);
    other["_x"] = -5000;
    CHECK(mc["hitTest"](other) == false);

    // localToGlobal / globalToLocal invert each other.
    Value pt = obj({{"x", 10}, {"y", 20}});
    mc["localToGlobal"](pt);
    CHECK(NEAR(pt["x"], 133) && NEAR(pt["y"], 70));
    mc["globalToLocal"](pt);
    CHECK(NEAR(pt["x"], 10) && NEAR(pt["y"], 20));

    // Colors.
    Value color = construct(G["Color"], Args{&mc, 1});
    color["setRGB"](0xFF8000);
    CHECK(color["getRGB"]() == 0xFF8000);

    // Drawing API and empty clips.
    Value box = root["createEmptyMovieClip"]("box", 40);
    box["beginFill"](0x00FF00, 100);
    box["moveTo"](0, 0);
    box["lineTo"](10, 0);
    box["lineTo"](10, 10);
    box["lineTo"](0, 10);
    box["endFill"]();
    box["_x"] = 300;
    CHECK(NEAR(box["_width"], 10));
    CHECK(box["hitTest"](305, 5, true) == true);
    CHECK(box["hitTest"](315, 5, true) == false);
    // A zero-scale clip has no area: nothing hits it, not even its collapsed origin
    // (regression: degenerate transforms used to map every point inside the shape).
    box["_xscale"] = 0;
    CHECK(box["hitTest"](300, 5, true) == false);
    CHECK(box["hitTest"](250, 5, true) == false);
    box["_xscale"] = 100;
    CHECK(box["hitTest"](305, 5, true) == true);

    // Masked content only hits where its mask does (regression: water collision used the
    // full, mostly masked-away water rectangles, so Mario swam above the surface).
    {
        Value holder = root["createEmptyMovieClip"]("maskHolder", 70);
        holder["_x"] = 0;
        Value maskClip = holder["createEmptyMovieClip"]("maskShape", 1);   // small square 0..10
        maskClip["beginFill"](0, 100);
        maskClip["moveTo"](0, 0);
        maskClip["lineTo"](10, 0);
        maskClip["lineTo"](10, 10);
        maskClip["lineTo"](0, 10);
        maskClip["endFill"]();
        Value content = holder["createEmptyMovieClip"]("content", 2);     // big square 0..100
        content["beginFill"](0xFF0000, 100);
        content["moveTo"](0, 0);
        content["lineTo"](100, 0);
        content["lineTo"](100, 100);
        content["lineTo"](0, 100);
        content["endFill"]();
        CHECK(holder["hitTest"](50, 50, true) == true);  // unmasked: the big square counts
        displayOf(maskClip)->clipDepth = (uint16_t)displayOf(content)->depth;  // mask covers it
        CHECK(holder["hitTest"](50, 50, true) == false);  // outside the mask
        CHECK(holder["hitTest"](5, 5, true) == true);     // inside the mask
        holder["removeMovieClip"]();
    }

    // Absurd transforms must not corrupt memory when rendered (regression: NaN/Infinity
    // coordinates reached the rasterizer and indexed outside its coverage buffer).
    {
        flash::Rasterizer rast;
        flash::Surface surf;
        surf.resize(450, 300);
        Value wild = root["attachMovie"](g_symbol.c_str(), "wild", 60);
        const double values[] = {INFINITY, -INFINITY, NAN, 1e300, -1e300, 1e-300, 0};
        for (double v : values) {
            wild["_rotation"] = v;
            wild["_xscale"] = v;
            wild["_yscale"] = v;
            wild["_x"] = v;
            wild["_width"] = v;
            wild["gotoAndStop"](v);
            wild["swapDepths"](v);
            player.render(rast, surf);
            player.tick();
        }
        CHECK(wild["_x"].get().toNumber() == wild["_x"].get().toNumber());  // still a number
        wild["removeMovieClip"]();
    }

    // Removal: onUnload runs, references to the removed clip go dead.
    size_t before = l["length"].get().toNumber();
    mc["removeMovieClip"]();
    player.tick();
    CHECK(root["probe"].get().isUndefined());
    bool sawUnload = false;
    for (size_t i = before; i < (size_t)l["length"].get().toNumber(); i++) sawUnload |= l[(int)i] == "unload";
    CHECK(sawUnload);
    CHECK(mc["hitTest"](cx, cy) == false);

    // A new clip at the same path takes over old references (AVM1 semantics).
    Value again = root["attachMovie"](g_symbol.c_str(), "probe", 5);
    CHECK(mc["getDepth"]() == 5);
    CHECK(again["getDepth"]() == 5);

    // with / tellTarget against stage objects.
    {
        Activation scope(root, {}, stageObject(&player.root())->targetScope(), stageObject(&player.root()));
        {
            TellTarget t(scope, Value("probe"));
            scope["marker"] = 7;  // created on the tellTarget clip
            scope["gotoAndStop"](2);
        }
        CHECK(again["marker"] == 7);
        CHECK(again["_currentframe"] == 2);
    }

    // GC keeps everything reachable from the stage and frees detached objects.
    for (int i = 0; i < 300; i++) root["attachMovie"](g_symbol.c_str(), "tmp", 100)["removeMovieClip"]();
    player.tick();
    size_t freed = Runtime::get().collectGarbage();
    CHECK(freed > 0);
    CHECK(root["booted"] == true);
    CHECK(again["marker"] == 7);
    CHECK(root["log"][0] == "load");

    std::printf("%d/%d checks passed (symbol %s)\n", checks - failures, checks, g_symbol.c_str());
    return failures ? 1 : 0;
}
