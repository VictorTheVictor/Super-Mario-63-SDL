// Super Mario 63 port — milestone 1 timeline viewer.
//
// Usage: sm63 <Super_Mario_63_2010.swf> [--symbol <linkage name>] [--scale N] [--mute]
//             [--trace] [--frames N --dump [--watch _root.a,_root.b.c]]
//             [--input "2500:press:Enter;2600:click:225,150"] [--diagnose] [--sample N]
//             [--bench | --bench-from FRAME] [--call _root.fn,...] [--goto LABEL@FRAME]
//             [--course NAME@FRAME] [--tree PATH] [--probe PATH@X,Y;...] [--track-char ID,...]
//             [--set PATH=NUMBER@FRAME;...] [--find-text WORD]
//             [--frames N --screenshot out.bmp] [--stats]
//        sm63 --selftest out.bmp     (renders built-in test geometry, no SWF needed)
//
// Symbol viewer keys: Space play/pause, Left/Right step frame, Escape quit.
// Game mode: keys and mouse go to the game; Shift+Escape quits.

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <future>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <vector>
#include <string>

#include "as/Audio.h"
#include "as/Builtins.h"
#include "as/Deterministic.h"
#include "as/Diagnostics.h"
#include "as/Host.h"
#include "as/Player.h"
#include "flash/DisplayObject.h"
#include "game/Scripts.h"
#include "EmbeddedFonts.h"
#include "MissingSwfImage.h"
#include "flash/Effects.h"
#include "flash/Raster.h"
#include "flash/Swf.h"

#if defined(__linux__) && !defined(__ANDROID__)  // Android lacks backtrace() before API 33
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <sys/time.h>

#include <atomic>

// Sampling profiler for --profile: a CPU-time timer signal records the call stack;
// tools/profile.py turns the saved (module, offset) stacks into a report.
namespace prof {
constexpr int kDepth = 32, kMax = 40000;
void* g_stacks[kMax][kDepth];
int g_depths[kMax];
std::atomic<int> g_count{0};

void onSignal(int) {
    int i = g_count.fetch_add(1);
    if (i < kMax) g_depths[i] = backtrace(g_stacks[i], kDepth);
}

const char* g_path = nullptr;
void stop(const char* path);

void start(const char* path) {
    g_path = path;
    // A run killed by a timeout (e.g. a script stuck in a loop) still writes its samples.
    signal(SIGTERM, [](int) {
        stop(g_path);
        std::_Exit(143);
    });
    void* warm[4];
    backtrace(warm, 4);  // loads libgcc's unwinder outside the signal handler
    signal(SIGPROF, onSignal);
    itimerval t{{0, 1000}, {0, 1000}};  // every 1 ms of CPU time
    setitimer(ITIMER_PROF, &t, nullptr);
}

void stop(const char* path) {
    itimerval off{};
    setitimer(ITIMER_PROF, &off, nullptr);
    FILE* f = std::fopen(path, "w");
    if (!f) return;
    int n = std::min(g_count.load(), kMax);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < g_depths[i]; j++) {
            Dl_info d;
            if (!dladdr(g_stacks[i][j], &d) || !d.dli_fname) continue;
            // Return addresses point after the call; step back into it (except the leaf).
            uintptr_t off = (uintptr_t)g_stacks[i][j] - (uintptr_t)d.dli_fbase - (j ? 1 : 0);
            std::fprintf(f, "%s:%lx ", d.dli_fname, (unsigned long)off);
        }
        std::fputc('\n', f);
    }
    std::fclose(f);
    std::printf("profile: %d samples written to %s\n", n, path);
}
}  // namespace prof
#endif

using namespace flash;

namespace {

// SDL key -> Flash key code (Key.getCode values).
int flashKeyCode(SDL_Keycode k) {
    if (k >= SDLK_a && k <= SDLK_z) return 'A' + (k - SDLK_a);
    if (k >= SDLK_0 && k <= SDLK_9) return '0' + (k - SDLK_0);
    if (k >= SDLK_F1 && k <= SDLK_F12) return 112 + (k - SDLK_F1);
    switch (k) {
        case SDLK_LEFT: return as::keycode::Left;
        case SDLK_RIGHT: return as::keycode::Right;
        case SDLK_UP: return as::keycode::Up;
        case SDLK_DOWN: return as::keycode::Down;
        case SDLK_SPACE: return as::keycode::Space;
        case SDLK_RETURN: return as::keycode::Enter;
        case SDLK_ESCAPE: return as::keycode::Escape;
        case SDLK_BACKSPACE: return as::keycode::Backspace;
        case SDLK_TAB: return as::keycode::Tab;
        case SDLK_LSHIFT: case SDLK_RSHIFT: return as::keycode::Shift;
        case SDLK_LCTRL: case SDLK_RCTRL: return as::keycode::Control;
        case SDLK_LALT: case SDLK_RALT: return as::keycode::Alt;
        case SDLK_PAGEUP: return as::keycode::PageUp;
        case SDLK_PAGEDOWN: return as::keycode::PageDown;
        case SDLK_HOME: return as::keycode::Home;
        case SDLK_END: return as::keycode::End;
        case SDLK_INSERT: return as::keycode::Insert;
        case SDLK_DELETE: return as::keycode::Delete;
        case SDLK_PLUS: case SDLK_EQUALS: case SDLK_KP_PLUS: return 187;
        case SDLK_MINUS: case SDLK_KP_MINUS: return 189;
        default: return 0;
    }
}

int flashAscii(const SDL_Keysym& k) {
    if (k.sym >= 32 && k.sym < 127) {
        int c = k.sym;
        if ((k.mod & KMOD_SHIFT) && c >= 'a' && c <= 'z') c -= 32;
        return c;
    }
    if (k.sym == SDLK_RETURN) return 13;
    return 0;
}

// Scripted input for headless runs, e.g.
// "2490:move:225,150;2500:click:225,150;2600:press:Enter;2700:hold:Right;2800:release:Right"
struct InputEvent {
    int frame;
    enum Kind { KeyDown, KeyUp, Move, MouseDown, MouseUp } kind;
    int code = 0;
    double x = 0, y = 0;
};

int keyByName(const std::string& n) {
    static const std::map<std::string, int> names = {
        {"Enter", as::keycode::Enter}, {"Space", as::keycode::Space}, {"Left", as::keycode::Left},
        {"Right", as::keycode::Right}, {"Up", as::keycode::Up}, {"Down", as::keycode::Down},
        {"Shift", as::keycode::Shift}, {"Control", as::keycode::Control}, {"Escape", as::keycode::Escape},
        {"Tab", as::keycode::Tab}, {"Backspace", as::keycode::Backspace}};
    auto it = names.find(n);
    if (it != names.end()) return it->second;
    if (n.size() == 1 && std::isalnum((unsigned char)n[0])) return std::toupper((unsigned char)n[0]);
    return 0;
}

std::vector<InputEvent> parseInput(const std::string& spec) {
    std::vector<InputEvent> out;
    size_t start = 0;
    while (start < spec.size()) {
        size_t end = spec.find(';', start);
        std::string item = spec.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? spec.size() : end + 1;
        size_t a = item.find(':'), b = item.find(':', a + 1);
        if (a == std::string::npos || b == std::string::npos) continue;
        int frame = std::atoi(item.c_str());
        std::string what = item.substr(a + 1, b - a - 1), arg = item.substr(b + 1);
        if (what == "move") {
            double x = std::atof(arg.c_str()), y = std::atof(arg.c_str() + arg.find(',') + 1);
            out.push_back({frame, InputEvent::Move, 0, x, y});
        } else if (what == "click") {
            double x = std::atof(arg.c_str()), y = std::atof(arg.c_str() + arg.find(',') + 1);
            out.push_back({frame, InputEvent::Move, 0, x, y});
            out.push_back({frame, InputEvent::MouseDown, 0, x, y});
            out.push_back({frame + 1, InputEvent::MouseUp, 0, x, y});
        } else if (int code = keyByName(arg)) {
            if (what == "press" || what == "hold") out.push_back({frame, InputEvent::KeyDown, code});
            if (what == "press") out.push_back({frame + 2, InputEvent::KeyUp, code});
            if (what == "release") out.push_back({frame, InputEvent::KeyUp, code});
        } else {
            std::fprintf(stderr, "unknown input '%s'\n", item.c_str());
        }
    }
    return out;
}

// One line per object: name, kind (clip frame), depth, mask depth, world bounds in
// stage pixels, visibility, alpha, scale, blend mode, filters and color transform.
void printTree(FILE* out, flash::DisplayObject& d, int depth, int maxDepth) {
    flash::Rect b = d.worldMatrix().apply(d.localBounds());
    auto* mc = dynamic_cast<MovieClip*>(&d);
    char kind[32];
    std::snprintf(kind, sizeof kind, "clip f%d/%d", mc ? mc->currentFrame() : 0, mc ? mc->totalFrames() : 0);
    std::fprintf(out,
                 "  %*s%s [%s %d] depth %d clipDepth %d bounds (%.0f,%.0f)-(%.0f,%.0f) %s alpha %.2f xs %.2f ys %.2f"
                 " blend %d filters %zu cxadd %.0f,%.0f,%.0f mul %.2f,%.2f,%.2f\n",
                 depth * 2, "", d.name.empty() ? "-" : d.name.c_str(), mc ? kind : d.definition() ? "shape/text" : "?",
                 d.definition() ? d.definition()->id : -1, d.depth, d.clipDepth, b.xMin / 20, b.yMin / 20, b.xMax / 20,
                 b.yMax / 20, d.visible ? "visible" : "HIDDEN", d.cxform.aMul, d.matrix.a, d.matrix.d, d.blendMode,
                 d.filters.size(), d.cxform.rAdd, d.cxform.gAdd, d.cxform.bAdd, d.cxform.rMul, d.cxform.gMul,
                 d.cxform.bMul);
    if (depth < maxDepth) d.forEachChild([&](flash::DisplayObject& c) { printTree(out, c, depth + 1, maxDepth); });
}

// Pause menu (Esc / controller Start) helpers. The menu is the game's own SaveMessage
// prompt (shown after collecting a star) plus the high score screen's "Save & Exit"
// button, laid out like that screen's three buttons. These replace the prompt's SAVE,
// NOSAVE and SAVEXIT functions and only record the choice; the window loop acts on it.
as::Value pauseMenuSave(const as::Value& self, as::Args) {
    as::setMember(self, as::intern("portAction"), as::Value(std::string("save")));
    return as::Value();
}

as::Value pauseMenuSaveExit(const as::Value& self, as::Args) {
    as::setMember(self, as::intern("portAction"), as::Value(std::string("saveexit")));
    return as::Value();
}

as::Value pauseMenuQuit(const as::Value& self, as::Args) {
    as::setMember(self, as::intern("portAction"), as::Value(std::string("quit")));
    return as::Value();
}

// The prompt's own closing: fade out over 10 frames, then unpause and remove it.
as::Value pauseMenuFade(const as::Value& self, as::Args) {
    double count = as::getMember(self, as::intern("count")).toNumber() + 1;
    as::setMember(self, as::intern("count"), count);
    as::setMember(self, as::intern("_alpha"), as::getMember(self, as::intern("_alpha")).toNumber() - 10);
    if (count >= 10) {
        as::Value root = as::getMember(as::Value(as::Runtime::get().global), as::intern("_level0"));
        as::setMember(root, as::intern("PauseGame"), false);
        as::callFunction(as::getMember(self, as::intern("removeMovieClip")), self, {});
    }
    return as::Value();
}

// Reads a whole file through SDL, so the same code opens a file on disk or an Android
// APK asset. Empty if it cannot be read.
std::vector<uint8_t> readFile(const std::string& path) {
    std::vector<uint8_t> bytes;
    if (SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb")) {
        Sint64 size = SDL_RWsize(rw);
        if (size > 0) {
            bytes.resize((size_t)size);
            bytes.resize(SDL_RWread(rw, bytes.data(), 1, bytes.size()));
        }
        SDL_RWclose(rw);
    }
    return bytes;
}

// Shows the "SWF not found" message (pre-rendered in the game's sign font) until the user
// drops a file on the window. Returns the dropped path, or "" if the window was closed.
std::string waitForSwfDrop(int scale) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return "";
    SDL_Window* win = SDL_CreateWindow("Super Mario 63", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       450 * scale, 300 * scale, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!ren) return "";
    // Decode the run-length encoded gray+alpha image into a texture.
    std::vector<uint32_t> argb;
    argb.reserve((size_t)missing_swf::kWidth * missing_swf::kHeight);
    for (size_t i = 0; i + 2 < sizeof missing_swf::kRle; i += 3) {
        uint32_t g = missing_swf::kRle[i + 1], a = missing_swf::kRle[i + 2];
        argb.insert(argb.end(), missing_swf::kRle[i] + 1u, a << 24 | g << 16 | g << 8 | g);
    }
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                                         missing_swf::kWidth, missing_swf::kHeight);
    SDL_UpdateTexture(tex, nullptr, argb.data(), missing_swf::kWidth * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
    std::string dropped;
    for (bool open = true; open && dropped.empty();) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) open = false;
            if (e.type == SDL_DROPFILE) {
                dropped = e.drop.file;
                SDL_free(e.drop.file);
            }
        }
        int ow, oh;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        double k = std::min(ow * 0.92 / missing_swf::kWidth, (double)oh / missing_swf::kHeight);
        SDL_Rect dst{(int)(ow - missing_swf::kWidth * k) / 2, (int)(oh - missing_swf::kHeight * k) / 2,
                     (int)(missing_swf::kWidth * k), (int)(missing_swf::kHeight * k)};
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        SDL_RenderPresent(ren);
    }
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    return dropped;
}

bool saveBmp(const Surface& s, const char* path) {
    SDL_Surface* out = SDL_CreateRGBSurfaceWithFormatFrom(
        (void*)s.pixels.data(), s.width, s.height, 32, s.width * 4, SDL_PIXELFORMAT_ARGB8888);
    if (!out) return false;
    int rc = SDL_SaveBMP(out, path);
    SDL_FreeSurface(out);
    return rc == 0;
}

// Renders hand-built geometry that exercises fills, gradients, curves and strokes.
int selfTest(const char* outPath) {
    Movie movie;  // empty: no bitmaps
    Surface surf;
    surf.resize(450, 300);
    surf.clear(0xFFFFFFFF);
    Rasterizer r;
    IntRect clip{0, 0, 450, 300};

    ShapeDef s;
    FillStyle red;
    red.color = {220, 40, 40, 255};
    FillStyle grad;
    grad.type = FillStyle::LinearGradient;
    grad.stops = {{0, {20, 60, 200, 255}}, {255, {250, 220, 40, 255}}};
    grad.matrix = Matrix::scale(100 * 20 / 32768.0, 1) * Matrix{};
    grad.matrix.tx = 250 * 20;
    FillStyle radial;
    radial.type = FillStyle::RadialGradient;
    radial.stops = {{0, {255, 255, 255, 255}}, {255, {40, 160, 60, 0}}};
    radial.matrix = Matrix::scale(60 * 20 / 16384.0, 60 * 20 / 16384.0);
    radial.matrix.tx = 360 * 20;
    radial.matrix.ty = 220 * 20;
    s.fills = {red, grad, radial};
    LineStyle line;
    line.width = 6 * 20;
    line.color = {30, 30, 30, 255};
    s.lines = {line};

    auto P = [](double x, double y) { return Point{x * 20, y * 20}; };
    auto L = [&](Point a, Point b) { return Edge{a, {(a.x + b.x) / 2, (a.y + b.y) / 2}, b, false}; };
    auto Q = [&](Point a, Point c, Point b) { return Edge{a, c, b, true}; };

    ShapeDef::Layer layer;
    // Circle-ish blob from four quadratic curves.
    Point c{100 * 20, 100 * 20};
    double rr = 60 * 20, k = rr * 1.0;
    std::vector<Edge> blob = {
        Q({c.x + rr, c.y}, {c.x + k, c.y + k}, {c.x, c.y + rr}),
        Q({c.x, c.y + rr}, {c.x - k, c.y + k}, {c.x - rr, c.y}),
        Q({c.x - rr, c.y}, {c.x - k, c.y - k}, {c.x, c.y - rr}),
        Q({c.x, c.y - rr}, {c.x + k, c.y - k}, {c.x + rr, c.y}),
    };
    layer.fills.push_back({0, blob});
    // Gradient rectangle with a triangular hole (opposite winding cancels).
    std::vector<Edge> rect = {L(P(200, 40), P(300, 40)), L(P(300, 40), P(300, 160)),
                              L(P(300, 160), P(200, 160)), L(P(200, 160), P(200, 40)),
                              L(P(230, 70), P(250, 130)), L(P(250, 130), P(270, 70)),
                              L(P(270, 70), P(230, 70))};
    layer.fills.push_back({1, rect});
    std::vector<Edge> square = {L(P(310, 170), P(410, 170)), L(P(410, 170), P(410, 270)),
                                L(P(410, 270), P(310, 270)), L(P(310, 270), P(310, 170))};
    layer.fills.push_back({2, square});
    StrokePath sp{0, {{L(P(40, 250), P(120, 200)), Q(P(120, 200), P(200, 150), P(260, 260))}}};
    layer.strokes.push_back(sp);
    s.layers.push_back(layer);

    drawShape(r, surf, clip, s, Matrix{}.scale(0.05, 0.05), ColorTransform{}, movie);

    // Same shape, rotated, scaled and tinted half-transparent.
    double ang = 0.4;
    Matrix rot{std::cos(ang), std::sin(ang), -std::sin(ang), std::cos(ang), 300, 10};
    ColorTransform tint;
    tint.aMul = 0.5f;
    tint.bAdd = 120;
    drawShape(r, surf, clip, s, rot * Matrix::scale(0.025, 0.025), tint, movie);

    if (!saveBmp(surf, outPath)) {
        std::fprintf(stderr, "failed to write %s\n", outPath);
        return 1;
    }
    std::printf("wrote %s\n", outPath);

    // Second image: filters and blend modes on simple discs, one effect per cell.
    Surface fx;
    fx.resize(450, 300);
    // Checkerboard background so alpha and blend modes are visible.
    for (int y = 0; y < 300; y++)
        for (int x = 0; x < 450; x++)
            fx.pixels[(size_t)y * 450 + x] = ((x / 15 + y / 15) & 1) ? 0xFFDDDDDD : 0xFF9999AA;
    ShapeDef disc;
    FillStyle orange;
    orange.color = {240, 140, 30, 255};
    disc.fills = {orange};
    ShapeDef::Layer dl;
    double R = 30 * 20;
    dl.fills.push_back({0, {Q({R, 0}, {R, R}, {0, R}), Q({0, R}, {-R, R}, {-R, 0}),
                            Q({-R, 0}, {-R, -R}, {0, -R}), Q({0, -R}, {R, -R}, {R, 0})}});
    disc.layers.push_back(dl);

    auto cell = [&](int i, std::vector<Filter> filters, uint8_t blend, ColorTransform cx) {
        int cx0 = 75 + (i % 4) * 100, cy0 = 75 + (i / 4) * 150;
        int pad = filterPadding(filters);
        Surface layer;
        layer.resize(60 + 2 * pad, 60 + 2 * pad);
        drawShape(r, layer, {0, 0, layer.width, layer.height}, disc,
                  Matrix::translate(30 + pad, 30 + pad) * Matrix::scale(0.05, 0.05), ColorTransform{}, movie);
        applyFilters(layer, filters);
        compositeLayer(fx, {0, 0, 450, 300}, layer, cx0 - 30 - pad, cy0 - 30 - pad, blend, cx);
    };
    Filter glow;
    glow.type = Filter::Glow;
    glow.color = {40, 200, 255, 255};
    glow.blurX = glow.blurY = 12;
    glow.strength = 2;
    glow.passes = 2;
    Filter innerGlow = glow;
    innerGlow.inner = true;
    Filter shadow;
    shadow.type = Filter::DropShadow;
    shadow.color = {0, 0, 0, 200};
    shadow.blurX = shadow.blurY = 6;
    shadow.distance = 8;
    shadow.angle = M_PI / 4;
    shadow.passes = 1;
    Filter bevelF;
    bevelF.type = Filter::Bevel;
    bevelF.color = {0, 0, 0, 255};
    bevelF.highlight = {255, 255, 255, 255};
    bevelF.blurX = bevelF.blurY = 6;
    bevelF.distance = 6;
    bevelF.angle = M_PI / 4;
    bevelF.inner = true;
    bevelF.passes = 1;
    Filter blurF;
    blurF.type = Filter::Blur;
    blurF.blurX = blurF.blurY = 10;
    blurF.passes = 2;
    Filter gray;
    gray.type = Filter::ColorMatrix;
    float g[20] = {0.3f, 0.59f, 0.11f, 0, 0, 0.3f, 0.59f, 0.11f, 0, 0, 0.3f, 0.59f, 0.11f, 0, 0, 0, 0, 0, 1, 0};
    std::copy(g, g + 20, gray.matrix);
    ColorTransform half;
    half.aMul = 0.5f;
    cell(0, {glow}, BlendNormal, {});
    cell(1, {innerGlow}, BlendNormal, {});
    cell(2, {shadow}, BlendNormal, {});
    cell(3, {bevelF}, BlendNormal, {});
    cell(4, {blurF}, BlendNormal, {});
    cell(5, {gray, glow}, BlendNormal, half);
    cell(6, {}, BlendInvert, {});
    cell(7, {}, BlendDifference, {});
    std::string fxPath = std::string(outPath) + ".fx.bmp";
    if (!saveBmp(fx, fxPath.c_str())) return 1;
    std::printf("wrote %s\n", fxPath.c_str());
    return 0;
}

void printStats(const Movie& m) {
    int counts[16] = {};
    size_t places = 0, frames = 0, actions = 0, placeholders = 0;
    for (auto& [id, c] : m.characters()) {
        counts[c->kind]++;
        if (c->kind == Character::Sprite) {
            auto* s = static_cast<Sprite*>(c.get());
            places += s->places.size();
            frames += s->frames.size();
            actions += s->actions.size();
        }
        if (c->kind == Character::Bitmap && static_cast<Bitmap*>(c.get())->placeholder)
            placeholders++;
    }
    const char* names[] = {"Shape", "Sprite", "Bitmap", "MorphShape", "Text",
                           "EditText", "Font", "Sound", "Button"};
    std::printf("SWF v%d, stage %gx%g px, %.2f fps, root frames %d\n", m.version,
                (m.stageRect.xMax - m.stageRect.xMin) / 20, (m.stageRect.yMax - m.stageRect.yMin) / 20,
                m.frameRate, m.root.frameCount);
    for (int i = 0; i < 9; i++) std::printf("  %-10s %d\n", names[i], counts[i]);
    std::printf("  exports %zu, sprite frames %zu, placements %zu, frame scripts %zu\n",
                m.exports().size(), frames, places, actions);
    std::printf("  root: placements %zu, frame scripts %zu, labels %zu\n", m.root.places.size(),
                m.root.actions.size(), m.root.labels.size());
    std::printf("  JPEG bitmaps not decoded yet: %zu\n", placeholders);
}

}  // namespace

// Debug hook, callable from gdb while the game is running or hung:
//   call sm63_debug_watch("_root.Course.Char._y,_root.coursescale")
extern "C" void sm63_debug_watch(const char* paths) {
    flash::DisplayObject* root = as::stage().root.get();
    if (!root) return;
    std::string watch = paths;
    size_t start = 0;
    while (start < watch.size()) {
        size_t comma = watch.find(',', start);
        std::string path = watch.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        start = comma == std::string::npos ? watch.size() : comma + 1;
        size_t dot = path.rfind('.');
        flash::DisplayObject* base = dot == std::string::npos ? root : as::resolvePath(root, path.substr(0, dot));
        as::Value v = base ? as::getMember(as::valueOf(base), as::intern(path.substr(dot + 1))) : as::Value();
        std::printf("  %s = %s (%s)%s\n", path.c_str(), v.toString().c_str(), v.typeOf(),
                    base && as::isRemoved(base) ? " [base removed]" : "");
    }
    std::fflush(stdout);
}

// Debug hook: describe a text field's definition and current text shape.
extern "C" void sm63_debug_textfield(const char* path) {
    auto* tf = dynamic_cast<flash::TextField*>(as::resolvePath(as::stage().root.get(), path));
    if (!tf) {
        std::printf("  %s: not a text field\n", path);
        return;
    }
    const flash::EditTextCharacter* e = tf->edit();
    std::string t = tf->text();
    size_t lines = 1;
    for (char c : t) lines += c == '\r' || c == '\n';
    std::printf("  text field %s: char %d, wordWrap %d multiline %d html %d autoSize %d readOnly %d "
                "font %d size %.1f px align %d margins %.0f/%.0f leading %.0f; text %zu chars, %zu lines\n",
                path, e->id, e->wordWrap, e->multiline, e->html, e->autoSize, e->readOnly, e->fontId,
                e->fontHeight / 20, e->align, e->leftMargin / 20, e->rightMargin / 20, e->leading / 20, t.size(), lines);
    double boxW = (tf->bounds.xMax - tf->bounds.xMin) / 20;
    int shown = 0;
    for (const auto& l : tf->lines())
        if (l.width / 20 > boxW - 4 && shown++ < 6)
            std::printf("    line %.0f px wide (box %.0f): \"%.40s\"\n", l.width / 20, boxW, l.text.c_str());
    std::fflush(stdout);
}

// Debug hook: hit-test a stage point against a clip, as MovieClip.hitTest(x, y, shape) would.
//   call sm63_debug_hittest("_root.Course.Platforms", 225, 150)
extern "C" void sm63_debug_hittest(const char* path, double x, double y) {
    flash::DisplayObject* d = as::resolvePath(as::stage().root.get(), path);
    if (!d) {
        std::printf("  %s: not found\n", path);
        return;
    }
    flash::Rect b = d->worldMatrix().apply(d->localBounds());
    bool shape = d->hitTest(d->worldMatrix(), {x * 20, y * 20});
    std::printf("  hitTest(%s, %.2f, %.2f): shape %s; bounds (%.1f,%.1f)-(%.1f,%.1f)\n", path, x, y,
                shape ? "TRUE" : "false", b.xMin / 20, b.yMin / 20, b.xMax / 20, b.yMax / 20);
    if (auto* mc = dynamic_cast<flash::MovieClip*>(d))
        for (auto& [depth, c] : mc->children())
            if (c->hitTest(d->worldMatrix() * c->matrix, {x * 20, y * 20}))
                std::printf("    hit by child '%s' depth %d char %d%s\n", c->name.c_str(), depth,
                            c->definition() ? c->definition()->id : -1, c->visible ? "" : " (invisible)");
    std::fflush(stdout);
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // line-buffered: partial runs still report
    if (argc >= 3 && !std::strcmp(argv[1], "--selftest")) return selfTest(argv[2]);
#ifdef __ANDROID__
    // The APK carries the user's SWF as an asset (see android/README.md); saves go to the
    // app's private storage.
    if (argc < 2) {
        static char asset[] = "Super_Mario_63_2010.swf";
        static char* defaultArgs[] = {argv[0], asset, nullptr};
        argc = 2;
        argv = defaultArgs;
    }
    if (const char* dir = SDL_AndroidGetInternalStoragePath()) as::setSaveDirectory(dir);
#endif
    if (argc >= 2 && (!std::strcmp(argv[1], "--help") || !std::strcmp(argv[1], "-h"))) {
        std::printf("usage: %s [game.swf] [--symbol NAME] [--scale N] [--frames N --screenshot "
                    "out.bmp [--render-scale X]] [--smooth] [--threads N] [--stats]\n       %s --selftest out.bmp\n"
                    "Without a path, Super_Mario_63_2010.swf next to the executable is used.\n",
                    argv[0], argv[0]);
        return 0;
    }
    // The SWF: the first argument (also what dropping a file onto the executable passes),
    // or Super_Mario_63_2010.swf next to the executable, as in a release zip.
    int firstOption = 2;
    std::string swfPath;
    if (argc >= 2 && std::strncmp(argv[1], "--", 2) != 0) {
        swfPath = argv[1];
    } else {
        firstOption = 1;
        char* base = SDL_GetBasePath();
        swfPath = std::string(base ? base : "") + "Super_Mario_63_2010.swf";
        SDL_free(base);
    }
    std::string symbol, screenshot, watch, inputSpec, calls, courseSpec, treePath, probes,
                gotoSpec, trackSpec, setSpec, findText;
    int scale = 2, frames = 0, sampleEvery = 0, benchFrom = 0;
    double renderScale = 1;  // headless output size, in multiples of the stage size
    int viewOption = 1, windowW = 0, windowH = 0;  // headless: --view N --window WxH
    int renderThreads = 0;   // 0: one per hardware thread
    bool noLayerCache = false, noOcclusion = false;
    bool noInterpolation = false;  // --no-smooth-motion: draw only at game ticks
    bool debugOverlay = false;     // --debug-overlay: start with the F3 view on
    std::string pauseAt;           // --pause-at F1,F2: press Esc (pause menu) at these frames
    double headlessInterpolation = 1;  // --interpolate T: screenshot between the last two ticks
    std::string profilePath;  // --profile: sample stacks from --bench-from on
    bool stats = false, checkExports = false, mute = false, dump = false, traces = false,
         diagnose = false, bench = false;
    for (int i = firstOption; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--symbol" && i + 1 < argc) symbol = argv[++i];
        else if (a == "--scale" && i + 1 < argc) scale = std::max(1, std::atoi(argv[++i]));
        else if (a == "--view" && i + 1 < argc) viewOption = std::clamp(std::atoi(argv[++i]), 1, 8);
        else if (a == "--window" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &windowW, &windowH);
        else if (a == "--render-scale" && i + 1 < argc) renderScale = std::max(0.25, std::atof(argv[++i]));
        else if (a == "--smooth") flash::renderSettings.crispBitmaps = false;
        else if (a == "--deterministic") as::setDeterministic(63);
        else if (a == "--no-layer-cache") noLayerCache = true;
        else if (a == "--no-occlusion") noOcclusion = true;
        else if (a == "--no-smooth-motion") noInterpolation = true;
        else if (a == "--debug-overlay") debugOverlay = true;
        else if (a == "--pause-at" && i + 1 < argc) pauseAt = argv[++i];
        else if (a == "--interpolate" && i + 1 < argc) headlessInterpolation = std::atof(argv[++i]);
        else if (a == "--profile" && i + 1 < argc) profilePath = argv[++i], bench = true;
        else if (a == "--threads" && i + 1 < argc) renderThreads = std::max(1, std::atoi(argv[++i]));
        else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
        else if (a == "--stats") stats = true;
        else if (a == "--mute") mute = true;
        else if (a == "--dump") dump = true;
        else if (a == "--trace") traces = true;
        else if (a == "--input" && i + 1 < argc) inputSpec = argv[++i];
        else if (a == "--diagnose") diagnose = true;
        else if (a == "--bench") bench = true;
        else if (a == "--bench-from" && i + 1 < argc) bench = true, benchFrom = std::atoi(argv[++i]);
        else if (a == "--sample" && i + 1 < argc) sampleEvery = std::max(1, std::atoi(argv[++i])), dump = true;
        else if (a == "--watch" && i + 1 < argc) watch = argv[++i], dump = true;
        else if (a == "--call" && i + 1 < argc) calls = argv[++i], dump = true;
        else if (a == "--course" && i + 1 < argc) courseSpec = argv[++i];
        else if (a == "--tree" && i + 1 < argc) treePath = argv[++i], dump = true;
        else if (a == "--probe" && i + 1 < argc) probes = argv[++i], dump = true;
        else if (a == "--goto" && i + 1 < argc) gotoSpec = argv[++i];
        else if (a == "--set" && i + 1 < argc) setSpec = argv[++i];
        else if (a == "--find-text" && i + 1 < argc) findText = argv[++i], dump = true;
        else if (a == "--track-char" && i + 1 < argc) trackSpec = argv[++i], dump = true;
        else if (a == "--check-exports") checkExports = true;
    }

    registerEmbeddedFonts();
    Movie movie;
    std::string err;
    Uint32 t0 = SDL_GetTicks();
    std::vector<uint8_t> swfBytes = readFile(swfPath);
    bool headless = !screenshot.empty() || dump || bench || diagnose || stats || checkExports;
    // In a window, a missing SWF shows a message until one is dropped on it.
    while (swfBytes.empty() && !headless) {
        std::fprintf(stderr, "cannot open %s\n", swfPath.c_str());
        std::string dropped = waitForSwfDrop(scale);
        if (dropped.empty()) return 1;  // window closed
        swfPath = dropped;
        swfBytes = readFile(swfPath);
    }
    if (swfBytes.empty()) err = "cannot open " + swfPath;
    if (swfBytes.empty() || !movie.loadFromMemory(std::move(swfBytes), err)) {
        std::fprintf(stderr, "load failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("loaded %s in %u ms\n", swfPath.c_str(), SDL_GetTicks() - t0);
    if (stats) {
        printStats(movie);
        if (screenshot.empty()) return 0;
    }

    int W = (int)std::lround((movie.stageRect.xMax - movie.stageRect.xMin) / 20);
    int H = (int)std::lround((movie.stageRect.yMax - movie.stageRect.yMin) / 20);

    if (checkExports) {
        // Instantiates every exported symbol and renders a few frames of it: a crash and
        // performance smoke test over the whole library.
        Surface s;
        s.resize(W, H);
        Rasterizer rr;
        size_t drawn = 0, n = 0;
        double worstMs = 0;
        std::string worst;
        for (auto& [name, id] : movie.exports()) {
            auto stage = std::make_shared<MovieClip>(movie, &movie.root);
            stage->stop();
            auto obj = stage->attach(movie.character(id), 1, "x");
            if (!obj) continue;
            obj->matrix = Matrix::translate(W * 10, H * 10);
            bool any = false;
            for (int f = 0; f < 3; f++) {
                Uint64 a = SDL_GetPerformanceCounter();
                s.clear(0xFF000000);
                renderDisplayObject(*stage, rr, s, {0, 0, W, H}, Matrix::scale(0.05, 0.05),
                                    ColorTransform{}, movie);
                double ms = (SDL_GetPerformanceCounter() - a) * 1000.0 / SDL_GetPerformanceFrequency();
                if (ms > worstMs) worstMs = ms, worst = name;
                for (uint32_t p : s.pixels)
                    if (p != 0xFF000000) {
                        any = true;
                        break;
                    }
                stage->advance();
            }
            n++;
            if (any) drawn++;
        }
        std::printf("exports rendered: %zu, non-empty: %zu, slowest frame %.2f ms (%s)\n", n,
                    drawn, worstMs, worst.c_str());
        return 0;
    }

    // Symbol mode: a single exported symbol centred, timeline only (no scripts).
    // Game mode: the root timeline run by the script Player.
    std::shared_ptr<MovieClip> symbolStage;
    MovieClip* viewed = nullptr;
    // Declared before the player so they outlive it.
    audio::Mixer mixer(44100);
    std::unique_ptr<as::AudioBridge> audioBridge;
    std::unique_ptr<as::Player> player;
    if (!symbol.empty()) {
        // NAME is an export name, or #ID for any character (scaled to fit the stage).
        bool byId = symbol[0] == '#';
        Character* def = byId ? movie.character((uint16_t)std::atoi(symbol.c_str() + 1)) : movie.exported(symbol);
        if (!def) {
            std::fprintf(stderr, "no exported symbol '%s'\n", symbol.c_str());
            return 1;
        }
        symbolStage = std::make_shared<MovieClip>(movie, &movie.root);
        symbolStage->stop();
        auto obj = symbolStage->attach(def, 1, "viewed");
        obj->matrix = Matrix::translate(W * 10, H * 10);
        if (byId) {
            Rect b = obj->localBounds();
            double k = std::min(W * 20 / std::max(1.0, b.xMax - b.xMin), H * 20 / std::max(1.0, b.yMax - b.yMin));
            obj->matrix = Matrix::translate(-b.xMin * k, -b.yMin * k) * Matrix::scale(k, k);
        }
        viewed = dynamic_cast<MovieClip*>(obj.get());
    } else {
        // _url as Flash Player reports it: the SWF's own file URL.
#ifdef _WIN32
        if (char* abs = _fullpath(nullptr, swfPath.c_str(), 0)) {
            std::string path = abs;
            for (char& c : path)
                if (c == '\\') c = '/';
            as::stage().url = "file:///" + path;
            std::free(abs);
        }
#else
        if (char* abs = realpath(swfPath.c_str(), nullptr)) {
            as::stage().url = std::string("file://") + abs;
            std::free(abs);
        }
#endif
        // Like the standalone Flash Player, trace() prints nothing unless asked.
        if (!traces) as::Runtime::get().traceOutput = [](const std::string&) {};
        as::diagnostics().enabled = diagnose;
        game::registerScripts();
        audioBridge = std::make_unique<as::AudioBridge>(movie, mixer);
        player = std::make_unique<as::Player>(movie);
        if (renderThreads) player->setRenderThreads(renderThreads);
        if (noLayerCache) player->setLayerCaching(false);
        if (noOcclusion) player->setOcclusionCulling(false);
        player->debugOverlay = debugOverlay;
        player->setAudio(audioBridge.get());
        viewed = &player->root();
    }

    Surface surf;
    surf.resize((int)std::lround(W * renderScale), (int)std::lround(H * renderScale));
    Rasterizer rast;
    const Color bg = movie.background;
    double interpolation = 1;  // window mode: how far between the last two ticks to draw
    double viewScale = 0, viewX = 0, viewY = 0;  // window mode: stage placement in `surf`
    // How the window shows what lies beyond the stage (F5 cycles; --view N headless). Outside
    // the letterbox the HUD's dark frame around the stage (a 25% black border meant to dim
    // off-stage content) and the black rectangle behind the stage are hidden, and the sky
    // (_root.FarBackground, roughly stage-sized; each course attaches its own) is extended
    // to the window in one of these ways.
    enum ViewMode { Letterbox = 1, SkyRepeat, SkyMirror, SkyExtendLeft, SkyExtendRight, SkyZoom, SkyStretch, SkyBlack,
                    ViewModes = SkyBlack };
    static const char* const viewNames[] = {"", "letterboxed", "sky repeated", "sky repeated, flipped above and below",
                                            "sky extended by a mirrored copy on the left, centered",
                                            "sky extended by a mirrored copy on the right, centered",
                                            "sky zoomed to fill", "sky stretched to fill", "sky as is, black beyond"};
    int viewMode = Letterbox;
    // Objects shown differently from where the game put them (displayTransform): HUD
    // pieces at the window's edges, the screen filter stretched over the whole window.
    std::vector<std::weak_ptr<flash::DisplayObject>> adjusted;
    auto applyView = [&]() {
        if (!player) return;
        player->hiddenObjects.clear();
        player->backdrop = nullptr;
        player->backdropPlacements.clear();
        for (auto& w : adjusted)
            if (auto d = w.lock()) d->displayTransform = {};
        adjusted.clear();
        player->blackBeyondStage = false;
        if (viewMode == Letterbox || viewScale <= 0) return;
        // Door and course transitions (the star wipe and others) cover only the stage: show
        // them letterboxed.
        auto* transition = as::resolvePath(&player->root(), "_root.Transition");
        player->blackBeyondStage = transition && transition->visible;
        // The window in stage pixels.
        double wx0 = -viewX / viewScale, wy0 = -viewY / viewScale;
        double ww = surf.width / viewScale, wh = surf.height / viewScale;
        if (auto* stats = dynamic_cast<MovieClip*>(as::resolvePath(&player->root(), "_root.Stats")))
            stats->forEachChild([&](flash::DisplayObject& c) {
                int id = c.definition() ? c.definition()->id : 0;
                if (id == 799) player->hiddenObjects.push_back(&c);
                // The screen filter (a flat stage-sized black overlay that darkens the castle
                // until enough stars are collected, and fades for effects) stretches from the
                // stage to the whole window.
                if (id == 85) {
                    flash::Matrix toWindow = flash::Matrix::translate(wx0 * 20, wy0 * 20) * flash::Matrix::scale(ww / W, wh / H);
                    flash::Matrix parent = stats->worldMatrix();
                    c.displayTransform = parent.inverse() * toWindow * parent;
                    adjusted.push_back(c.weak_from_this());
                    return;
                }
                // The counters, icons, hint line and quality/fps box move to the nearer window
                // edge; the power meter, message boxes and pictures stay centered.
                static const std::set<int> sides = {17, 160, 180, 188, 189, 190, 191, 192, 193,
                                                    196, 197, 198, 232, 237, 621, 797};
                if (!sides.count(id)) return;
                flash::Rect b = c.worldMatrix().apply(c.localBounds());
                bool left = (b.xMin + b.xMax) / 2 < W * 10;  // center left of the stage's middle
                c.displayTransform = flash::Matrix::translate(left ? wx0 * 20 : (wx0 + ww - W) * 20, 0);
                adjusted.push_back(c.weak_from_this());
            });
        player->root().forEachChild([&](flash::DisplayObject& c) {
            if (c.definition() && c.definition()->id == 2102) player->hiddenObjects.push_back(&c);
        });
        auto* sky = as::resolvePath(&player->root(), "_root.FarBackground");
        if (!sky || viewMode == SkyBlack) return;
        flash::Rect b = sky->worldMatrix().apply(sky->localBounds());
        double x0 = b.xMin / 20, y0 = b.yMin / 20, bw = (b.xMax - b.xMin) / 20, bh = (b.yMax - b.yMin) / 20;
        if (bw < 1 || bh < 1) return;
        player->backdrop = sky;
        auto& out = player->backdropPlacements;
        if (viewMode == SkyRepeat || viewMode == SkyMirror) {
            // Tiles covering the window; with SkyMirror every other row is flipped vertically,
            // so the sky's top and bottom edges meet copies of themselves.
            for (int j = (int)std::floor((wy0 - y0) / bh); y0 + j * bh < wy0 + wh; j++)
                for (int i = (int)std::floor((wx0 - x0) / bw); x0 + i * bw < wx0 + ww; i++) {
                    if (viewMode == SkyMirror && (j & 1))
                        out.push_back(flash::Matrix::translate(i * bw, j * bh + 2 * y0 + bh) * flash::Matrix::scale(1, -1));
                    else
                        out.push_back(flash::Matrix::translate(i * bw, j * bh));
                }
        } else if (viewMode == SkyExtendLeft || viewMode == SkyExtendRight) {
            // The sky beside a copy of itself mirrored left to right (so the edges where they
            // meet match), the pair centered horizontally in the window.
            double left = wx0 + (ww - 2 * bw) / 2;  // the pair's left edge
            double sky = viewMode == SkyExtendLeft ? left + bw : left, mirror = viewMode == SkyExtendLeft ? left : left + bw;
            out.push_back(flash::Matrix::translate(sky - x0, 0));
            out.push_back(flash::Matrix::translate(mirror + bw + x0, 0) * flash::Matrix::scale(-1, 1));
        } else if (viewMode == SkyZoom) {
            // Uniformly scaled to cover the window, centered (overflow cropped).
            double k = std::max(ww / bw, wh / bh);
            out.push_back(flash::Matrix::translate(wx0 + (ww - bw * k) / 2, wy0 + (wh - bh * k) / 2) *
                          flash::Matrix::scale(k, k) * flash::Matrix::translate(-x0, -y0));
        } else if (viewMode == SkyStretch) {
            out.push_back(flash::Matrix::translate(wx0, wy0) * flash::Matrix::scale(ww / bw, wh / bh) *
                          flash::Matrix::translate(-x0, -y0));
        }
    };
    auto renderFrame = [&]() {
        if (player) {
            player->render(rast, surf, interpolation, viewScale, viewX, viewY);
            return;
        }
        surf.clear(0xFF000000u | bg.r << 16 | bg.g << 8 | bg.b);
        double s = (double)surf.width / W;
        renderSettings.scale = s;
        renderDisplayObject(*symbolStage, rast, surf, {0, 0, surf.width, surf.height},
                            Matrix::scale(0.05 * s, 0.05 * s), ColorTransform{}, movie);
    };
    // Debug noclip (F2): Mario flies with the arrow keys (Shift: faster), and his position
    // is pinned before and after every tick so the game's physics and collisions can't
    // move him. The camera follows him as usual. He is also invulnerable: enemies skip him
    // (_root.Invincible, as with the game's own power-ups) and his health is refilled
    // every tick against everything else (spikes, lava).
    bool noclip = false;
    double noclipX = 0, noclipY = 0;
    as::Value invincibleBeforeNoclip;
    // Directions currently held on a game controller (D-pad or left stick).
    bool padLeft = false, padRight = false, padUp = false, padDown = false;
    auto charValue = [&]() -> as::Value {
        flash::DisplayObject* c = player ? as::resolvePath(&player->root(), "_root.Course.Char") : nullptr;
        return c ? as::valueOf(c) : as::Value();
    };
    auto pinChar = [&]() {
        as::Value c = charValue();
        if (!c.isObject()) return;
        as::setMember(c, as::intern("_x"), noclipX);
        as::setMember(c, as::intern("_y"), noclipY);
        as::setMember(c, as::intern("xspeed"), 0);
        as::setMember(c, as::intern("yspeed"), 0);
        as::Value root = as::valueOf(&player->root());
        as::setMember(root, as::intern("Invincible"), true);
        as::Value full = as::getMember(root, as::intern("TotalPower"));
        if (full.isNumber()) as::setMember(root, as::intern("Power"), full);
    };
    auto tick = [&]() {
        if (!player) {
            symbolStage->advance();
            return;
        }
        if (noclip) {
            const Uint8* k = SDL_GetKeyboardState(nullptr);
            double speed = (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) ? 24 : 8;
            noclipX += speed * ((k[SDL_SCANCODE_RIGHT] || padRight) - (k[SDL_SCANCODE_LEFT] || padLeft));
            noclipY += speed * ((k[SDL_SCANCODE_DOWN] || padDown) - (k[SDL_SCANCODE_UP] || padUp));
            pinChar();
        }
        player->tick();
        if (noclip) pinChar();
    };

    // Pause menu: Esc or controller Start in a level (not on the title or menus) pauses
    // the game and its sound and shows three buttons: Save (and resume), Save & Exit, and
    // Don't Save (exits without saving).
    std::weak_ptr<flash::DisplayObject> pauseMenu;
    bool pauseMenuClosing = false;
    bool quitRequested = false;  // the menu's Quit
    auto rootValue = [&]() { return as::valueOf(&player->root()); };
    auto closePauseMenu = [&](flash::DisplayObject& menu) {
        as::Value m = as::valueOf(&menu);
        as::setMember(m, as::intern("clicked"), true);  // its buttons ignore input now
        as::setMember(m, as::intern("count"), 0);
        as::setMember(m, as::intern("onEnterFrame"),
                      as::Value(as::Runtime::get().newNative(pauseMenuFade, "onEnterFrame")));
        pauseMenuClosing = true;
    };
    // Returns false when there is no level to pause (the key then goes to the game).
    auto togglePauseMenu = [&]() -> bool {
        if (!player || !as::resolvePath(&player->root(), "_root.Course.Char")) return false;
        if (auto menu = pauseMenu.lock(); menu && !as::isRemoved(menu.get())) {
            if (!pauseMenuClosing) closePauseMenu(*menu);
            return true;
        }
        as::Value root = rootValue();
        if (as::getMember(root, as::intern("PauseGame")).toBoolean()) return true;  // the game's own pause or prompt
        double depth = as::callFunction(as::getMember(root, as::intern("getNextHighestDepth")), root, {}).toNumber();
        as::Value init = as::obj({{"_x", as::getMember(root, as::intern("screensizeX")).toNumber() / 2},
                                  {"_y", as::getMember(root, as::intern("screensizeY")).toNumber() / 2}});
        as::Value args[4] = {as::Value(std::string("SaveMessage")), as::Value(std::string("PortPauseMenu")), depth, init};
        as::Value menu = as::callFunction(as::getMember(root, as::intern("attachMovie")), root, as::Args{args, 4});
        flash::DisplayObject* d = as::displayOf(menu);
        if (!d) return true;
        as::setMember(root, as::intern("PauseGame"), true);
        // The third button: the high score screen's green "Save & Exit" (not exported, so
        // attached by character), which calls _parent.SAVEXIT().
        if (auto* mc = dynamic_cast<MovieClip*>(d); mc && movie.character(7506))
            mc->attach(movie.character(7506), 17 - as::kDepthOffset, "3");
        pauseMenu = d->shared_from_this();
        pauseMenuClosing = false;
        return true;
    };
    // Each frame while the menu exists: hide the option text, act on a choice, keep the
    // sound paused until it is gone.
    auto updatePauseMenu = [&]() {
        auto menu = pauseMenu.lock();
        bool open = menu && !as::isRemoved(menu.get());
        mixer.setPaused(open);
        if (!open) return;
        as::Value m = as::valueOf(menu.get());
        // Its frame 1 script (queued when it was attached) defines SAVE and NOSAVE; replace
        // them once it has run.
        auto rewire = [&](const char* name, as::Function::Native fn) {
            auto* f = dynamic_cast<as::Function*>(as::getMember(m, as::intern(name)).object());
            if (f && f->native != fn) as::setMember(m, as::intern(name), as::Value(as::Runtime::get().newNative(fn, name)));
        };
        rewire("SAVE", pauseMenuSave);
        rewire("SAVEXIT", pauseMenuSaveExit);
        rewire("NOSAVE", pauseMenuQuit);
        // The high score screen's spacing, in this order: Save, Save & Exit, Don't Save. While
        // the prompt's intro slides its two buttons in, they are only shown moved (setting
        // their position would stop the timeline animating them) and Save & Exit follows
        // Save; afterwards the positions are set for real (the buttons only take input then).
        auto* menuClip = dynamic_cast<MovieClip*>(menu.get());
        as::Value first = as::getMember(m, as::intern("1"));
        as::Value third = as::getMember(m, as::intern("3"));
        bool introDone = menuClip && menuClip->currentFrame() == menuClip->totalFrames();
        if (first.isObject()) {
            for (auto [option, x] : {std::pair{"1", -145.5}, {"3", -2.8}, {"2", 143.2}}) {
                as::Value button = as::getMember(m, as::intern(option));
                flash::DisplayObject* d = as::displayOf(button);
                if (!d) continue;
                double current = as::getMember(button, as::intern("_x")).toNumber();
                if (*option == '3' || introDone) {
                    d->displayTransform = {};
                    if (current != x) as::setMember(button, as::intern("_x"), x);
                } else {
                    d->displayTransform = flash::Matrix::translate((x - current) * 20, 0);
                }
            }
            if (third.isObject()) {
                // Its height always; its intro scale and fade too (afterwards its own script
                // scales it when highlighted).
                for (const char* p : {"_y", "_xscale", "_yscale", "_alpha"}) {
                    if (introDone && std::strcmp(p, "_y") != 0) break;
                    double v = as::getMember(first, as::intern(p)).toNumber();
                    if (as::getMember(third, as::intern(p)).toNumber() != v) as::setMember(third, as::intern(p), v);
                }
                as::setMember(third, as::intern("_visible"), true);
            }
        } else if (third.isObject()) {
            as::setMember(third, as::intern("_visible"), false);  // until Save is there to follow
        }
        std::string action = as::getMember(m, as::intern("portAction")).toString();
        if (action == "save" || action == "saveexit") {
            as::setMember(m, as::intern("portAction"), as::Value());
            as::Value root = rootValue();
            as::callFunction(as::getMember(root, as::intern("SaveFile")), root, {});
            if (action == "saveexit") {
                std::printf("pause menu: saved, quitting\n");
                quitRequested = true;
            } else {
                std::printf("pause menu: saved\n");
                if (!pauseMenuClosing) closePauseMenu(*menu);
            }
        } else if (action == "quit") {
            as::setMember(m, as::intern("portAction"), as::Value());
            std::printf("pause menu: quit\n");
            quitRequested = true;
        }
    };

    auto reportMissing = [&]() {
        if (player && !player->missingScripts().empty())
            std::printf("%zu frame scripts ran without a ported implementation\n",
                        player->missingScripts().size());
    };

    if (!screenshot.empty() || dump || bench || diagnose) {
        // --view N --window WxH: frames (screenshot, --bench) drawn as a window of that size
        // in that F5 mode, laid out as fitSurface does.
        viewMode = viewOption;
        bool windowView = windowW > 0 && windowH > 0 && viewMode != Letterbox;
        if (windowView) {
            surf.resize(windowW, windowH);
            viewScale = std::min((double)windowW / W, (double)windowH / H);
            viewX = std::floor((windowW - W * viewScale) / 2);
            viewY = std::floor((windowH - H * viewScale) / 2);
        }
        std::vector<InputEvent> events = parseInput(inputSpec);
        std::vector<double> tickMs, renderMs;
        for (int i = 0; i < frames; i++) {
#if defined(__linux__) && !defined(__ANDROID__)
            if (!profilePath.empty() && i == benchFrom) prof::start(profilePath.c_str());
#endif
            // --pause-at: the Esc key's pause menu, at the listed frames.
            for (size_t ps = 0; player && ps < pauseAt.size();) {
                size_t comma = pauseAt.find(',', ps);
                if (std::atoi(pauseAt.c_str() + ps) == i)
                    std::printf("frame %d: pause menu %s\n", i, togglePauseMenu() ? "toggled" : "not available");
                ps = comma == std::string::npos ? pauseAt.size() : comma + 1;
            }
            if (player) updatePauseMenu();
            if (quitRequested) break;
            if (player)
                for (const InputEvent& e : events) {
                    if (e.frame != i) continue;
                    switch (e.kind) {
                        case InputEvent::KeyDown: player->keyDown(e.code, 0); break;
                        case InputEvent::KeyUp: player->keyUp(e.code); break;
                        case InputEvent::Move: player->mouseMove(e.x, e.y); break;
                        case InputEvent::MouseDown: player->mouseDown(); break;
                        case InputEvent::MouseUp: player->mouseUp(); break;
                    }
                }
            if (player && sampleEvery && i % sampleEvery == 0) {
                // One line per sample: root frame, then each root child as name@depth:frame.
                MovieClip& r = player->root();
                std::printf("t=%-6d root %d", i, r.currentFrame());
                for (auto& [d, c] : r.children())
                    if (auto* mc = dynamic_cast<MovieClip*>(c.get()))
                        std::printf("  %s@%d:%d", c->name.c_str(), d - 16384, mc->currentFrame());
                std::printf("\n");
            }
            // --course NAME[,TRANSITION,X,Y]@FRAME: switch course part through the game's own path:
            // _root.changecourse("StarIn", NAME, 0, 0, 0, 0)  (RemoveCourse + StartChar), called
            // like the game's own scripts do. (A 7th argument `true` leaves coursescale at 0,
            // which makes scripts that probe the level with it loop forever.)
            if (player && !courseSpec.empty() && i == std::atoi(courseSpec.c_str() + courseSpec.rfind('@') + 1)) {
                as::Value root = as::valueOf(&player->root());
                // NAME or NAME,TRANSITION,X,Y (e.g. C-1,lefttransition2,700,-250, as a door does),
                // or FUNCTION() to call one of the game's own starters (StartInsideofCastle()).
                std::string spec = courseSpec.substr(0, courseSpec.rfind('@'));
                if (spec.size() > 2 && spec.compare(spec.size() - 2, 2, "()") == 0) {
                    as::callFunction(root[spec.substr(0, spec.size() - 2).c_str()].get(), root, {});
                } else {
                    std::vector<std::string> parts;
                    for (size_t a = 0, b; a <= spec.size(); a = b + 1) {
                        b = spec.find(',', a);
                        if (b == std::string::npos) b = spec.size();
                        parts.push_back(spec.substr(a, b - a));
                    }
                    double cx = parts.size() > 2 ? std::atof(parts[2].c_str()) : 0;
                    double cy = parts.size() > 3 ? std::atof(parts[3].c_str()) : 0;
                    as::Value argv2[6] = {as::Value(parts.size() > 1 ? parts[1] : std::string("StarIn")),
                                          as::Value(parts[0]), cx, cy, cx, cy};
                    as::callFunction(root["changecourse"].get(), root, as::Args{argv2, 6});
                    // The game runs Mario and the camera from _root.onEnterFrame = function() {
                    // CharCode(); } once a course has started (see <main> frame 10).
                    root["onEnterFrame"] = as::Value(as::Runtime::get().newNative(
                        [](const as::Value& self, as::Args) {
                            return as::callFunction(as::getMember(self, as::intern("CharCode")), self, {});
                        },
                        "onEnterFrame"));
                }
            }
            // --set "_root.Course.Char._x=600@2000;...": assign numbers at given frames
            for (size_t ss = 0; player && ss < setSpec.size();) {
                size_t semi = setSpec.find(';', ss);
                std::string item = setSpec.substr(ss, semi == std::string::npos ? std::string::npos : semi - ss);
                ss = semi == std::string::npos ? setSpec.size() : semi + 1;
                size_t eq = item.find('='), at = item.rfind('@');
                if (eq == std::string::npos || at == std::string::npos || std::atoi(item.c_str() + at + 1) != i) continue;
                std::string path = item.substr(0, eq);
                size_t dot = path.rfind('.');
                flash::DisplayObject* base = as::resolvePath(&player->root(), path.substr(0, dot));
                if (base) as::setMember(as::valueOf(base), as::intern(path.substr(dot + 1)),
                                        std::atof(item.c_str() + eq + 1));
            }
            // --goto LABEL@FRAME: _root.gotoAndStop(LABEL) at that frame
            if (player && !gotoSpec.empty() && i == std::atoi(gotoSpec.c_str() + gotoSpec.rfind('@') + 1))
                player->root().gotoLabel(gotoSpec.substr(0, gotoSpec.rfind('@')), false);
            Uint64 t0 = SDL_GetPerformanceCounter();
            tick();
            Uint64 t1 = SDL_GetPerformanceCounter();
            // --track-char 9826,9828: every instance of those characters, each frame
            if (player && !trackSpec.empty()) {
                std::vector<int> ids;
                for (size_t ts = 0; ts < trackSpec.size();) {
                    ids.push_back(std::atoi(trackSpec.c_str() + ts));
                    size_t c = trackSpec.find(',', ts);
                    ts = c == std::string::npos ? trackSpec.size() : c + 1;
                }
                std::string line;
                std::function<void(flash::DisplayObject&, bool)> find = [&](flash::DisplayObject& d, bool vis) {
                    vis = vis && d.visible;
                    if (d.definition() && std::find(ids.begin(), ids.end(), d.definition()->id) != ids.end()) {
                        flash::Point p = d.worldMatrix().apply(0, 0);
                        char buf[96];
                        std::snprintf(buf, sizeof buf, " %d@(%.0f,%.0f)%s a%.2f", d.definition()->id, p.x / 20,
                                      p.y / 20, vis ? "" : " HIDDEN", d.cxform.aMul);
                        line += buf;
                    }
                    d.forEachChild([&](flash::DisplayObject& c) { find(c, vis); });
                };
                find(player->root(), true);
                if (!line.empty()) std::printf("f%d%s\n", i, line.c_str());
            }
            if (player && !findText.empty())
                for (const auto& h : player->textContaining(findText))
                    std::printf("frame %d: %s (variable '%s') shows \"%s\"\n", i, h.path.c_str(), h.variable.c_str(),
                                h.text.c_str());
            if (diagnose && player) {
                // Report text fields showing "undefined" (first frames only, to keep it short).
                static int reported = 0;
                for (const auto& h : player->textContaining("undefined"))
                    if (reported++ < 20)
                        std::printf("frame %d: text field %s (variable '%s') shows \"%s\"\n", i, h.path.c_str(),
                                    h.variable.c_str(), h.text.c_str());
            }
            if (bench && i >= benchFrom) {
                if (windowView) applyView();
                renderFrame();
                renderMs.push_back((SDL_GetPerformanceCounter() - t1) * 1000.0 / SDL_GetPerformanceFrequency());
                tickMs.push_back((t1 - t0) * 1000.0 / SDL_GetPerformanceFrequency());
            }
        }
#if defined(__linux__) && !defined(__ANDROID__)
        if (!profilePath.empty()) prof::stop(profilePath.c_str());
#endif
        if (bench && !tickMs.empty()) {
            auto stats = [](std::vector<double> v, const char* what) {
                std::sort(v.begin(), v.end());
                double sum = 0;
                for (double x : v) sum += x;
                std::printf("%s: avg %.2f ms, p95 %.2f ms, max %.2f ms\n", what, sum / v.size(),
                            v[v.size() * 95 / 100], v.back());
            };
            stats(tickMs, "scripts+timeline");
            stats(renderMs, "render");
            std::printf("rasterizer: %zu non-finite segments rejected, %zu out-of-range rows clamped "
                        "(max overshoot %.3g px)\n",
                        Rasterizer::rejectedSegments.load(), Rasterizer::clampedRows.load(), Rasterizer::maxOvershoot.load());
        }
        if (diagnose) as::diagnostics().print(stdout);
        if (dump && player) {
            // Where is the game? Root timeline state and its child clips (names only).
            MovieClip& r = player->root();
            std::string label;
            for (auto& [name, f] : r.sprite()->labels)
                if (f <= r.currentFrame() && (label.empty() || r.sprite()->labels.at(label) < f)) label = name;
            std::printf("root: frame %d/%d%s, last label '%s', %zu children, %llu ticks\n", r.currentFrame(),
                        r.totalFrames(), r.playing ? " playing" : " stopped", label.c_str(), r.children().size(),
                        (unsigned long long)player->frameCount());
            std::function<void(MovieClip&, int)> walk = [&](MovieClip& mc, int depth) {
                for (auto& [d, c] : mc.children()) {
                    auto* child = dynamic_cast<MovieClip*>(c.get());
                    if (!child) continue;
                    std::printf("%*s%s [depth %d] frame %d/%d%s%s\n", depth * 2 + 2, "", c->name.c_str(),
                                d - 16384, child->currentFrame(), child->totalFrames(),
                                child->playing ? "" : " stopped", c->visible ? "" : " hidden");
                    if (depth < 1) walk(*child, depth + 1);
                }
            };
            walk(r, 0);
            for (const auto& t : player->textOverflows())
                std::printf("  text overflow %s \"%s\" box (%.0f,%.0f)-(%.0f,%.0f) glyphs (%.0f,%.0f)-(%.0f,%.0f)%s\n",
                            t.path.c_str(), t.text.c_str(), t.bounds.xMin, t.bounds.yMin, t.bounds.xMax,
                            t.bounds.yMax, t.extent.xMin, t.extent.yMin, t.extent.xMax, t.extent.yMax,
                            t.autoSize ? " [autoSize]" : "");
            for (const auto& c : player->clickables())
                std::printf("  clickable %s at (%.0f,%.0f)-(%.0f,%.0f)%s  char %d, events 0x%x, methods [%s]\n",
                            c.path.c_str(), c.bounds.xMin, c.bounds.yMin, c.bounds.xMax, c.bounds.yMax,
                            c.visible ? "" : " hidden", c.characterId, c.handlerMask, c.methods.c_str());
            // --tree PATH: subtree with world bounds (stage px), visibility and scale
            if (!treePath.empty()) {
                if (auto* d = as::resolvePath(&r, treePath)) printTree(stdout, *d, 0, 8);
                else std::printf("  %s: not found\n", treePath.c_str());
            }
            for (const auto& t : player->textOverflows()) sm63_debug_textfield(t.path.c_str());
            // --probe "_root.Course.Water@700,200;...": shape hit tests at stage points
            for (size_t ps = 0; ps < probes.size();) {
                size_t semi = probes.find(';', ps);
                std::string item = probes.substr(ps, semi == std::string::npos ? std::string::npos : semi - ps);
                ps = semi == std::string::npos ? probes.size() : semi + 1;
                size_t at = item.find('@'), comma = item.find(',', at);
                if (at == std::string::npos || comma == std::string::npos) continue;
                sm63_debug_hittest(item.substr(0, at).c_str(), std::atof(item.c_str() + at + 1),
                                   std::atof(item.c_str() + comma + 1));
            }
            // --call _root.KeyRIGHT,_root.KeyZ : call zero-argument functions and print results
            for (size_t cs = 0; cs < calls.size();) {
                size_t comma = calls.find(',', cs);
                std::string path = calls.substr(cs, comma == std::string::npos ? std::string::npos : comma - cs);
                cs = comma == std::string::npos ? calls.size() : comma + 1;
                size_t dot = path.rfind('.');
                flash::DisplayObject* base = dot == std::string::npos ? &r : as::resolvePath(&r, path.substr(0, dot));
                as::Value self = base ? as::valueOf(base) : as::Value();
                as::Value fn = as::getMember(self, as::intern(path.substr(dot + 1)));
                as::Value result = as::callFunction(fn, self, {});
                std::printf("  %s() = %s (%s)\n", path.c_str(), result.toString().c_str(), result.typeOf());
            }
            // --watch _root.percentloaded,_root.preloader.TotalCoins
            size_t start = 0;
            while (start < watch.size()) {
                size_t comma = watch.find(',', start);
                std::string path = watch.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                start = comma == std::string::npos ? watch.size() : comma + 1;
                size_t dot = path.rfind('.');
                flash::DisplayObject* base = dot == std::string::npos ? &r : as::resolvePath(&r, path.substr(0, dot));
                as::Value v = base ? as::getMember(as::valueOf(base), as::intern(path.substr(dot + 1)))
                                   : as::Value();
                std::printf("  %s = %s (%s)\n", path.c_str(), v.toString().c_str(), v.typeOf());
            }
        }
        if (screenshot.empty()) return 0;
        interpolation = headlessInterpolation;
        if (windowView) applyView();
        renderFrame();
        if (!saveBmp(surf, screenshot.c_str())) return 1;
        std::printf("wrote %s (frame %d)\n", screenshot.c_str(), viewed ? viewed->currentFrame() : 0);
        reportMissing();
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | (player && !mute ? SDL_INIT_AUDIO : 0)) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    Uint32 windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#ifdef __ANDROID__
    // Full screen: SDL then puts the activity in immersive mode (status and navigation
    // bars hidden).
    windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
    SDL_Window* win = SDL_CreateWindow("Super Mario 63", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, W * scale, H * scale, windowFlags);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    bool vsync = true;
    SDL_RenderSetLogicalSize(ren, W, H);  // letterboxes, and maps mouse events to stage pixels
    SDL_Texture* tex = nullptr;
    // F5 cycles viewMode: letterboxed (black bars, only the stage), or filling the window
    // and showing what lies beyond the stage edges at the same scale (like Ruffle's
    // letterbox option), with the sky extended in one of five ways.
    // The frame is rendered at the real pixel size of the letterboxed viewport (or the
    // whole window), so vector art stays sharp in a large or fullscreen window.
    auto fitSurface = [&]() {
        int ow = W, oh = H;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        double s = std::max(0.25, std::min((double)ow / W, (double)oh / H));
        int sw = std::max(1, (int)(W * s)), sh = std::max(1, (int)(H * s));
        double vx = 0, vy = 0;
        if (viewMode != Letterbox) {
            sw = std::max(1, ow);
            sh = std::max(1, oh);
            vx = std::floor((ow - W * s) / 2);
            vy = std::floor((oh - H * s) / 2);
        }
        bool sameView = viewScale == s && viewX == vx && viewY == vy;
        viewScale = s;
        viewX = vx;
        viewY = vy;
        if (tex && sw == surf.width && sh == surf.height) return !sameView;
        if (tex) SDL_DestroyTexture(tex);
        surf.resize(sw, sh);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, sw, sh);
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);  // already 1:1; avoid rounding blur
        return true;
    };
    bool redraw = true;  // the frame changed since it was last rendered
    // Draw every display refresh, with moving objects placed between the last two game
    // ticks; the game itself still ticks at its own frame rate.
    bool smooth = !noInterpolation;

    // Game controllers press the game's own keys: D-pad / left stick = arrows, A = S
    // (jump), B = C (FLUDD), X = X (spin), Y = Z (ground pound, also talk), bumpers = Z.
    // Each frame the held keys are compared with the last frame and only changes are sent.
    std::set<int> padKeys;

    // Links (the loading screen's Newgrounds banner, the Runouw intro) open with the OS's
    // default handler on desktops; on phones a tap on them would leave the game, so they
    // do nothing there. The cursor turns into a hand over clickable things, as in Flash.
#ifndef __ANDROID__
    as::host().openUrl = [](const std::string& url) {
        std::printf("opening %s\n", url.c_str());
        if (SDL_OpenURL(url.c_str()) != 0) std::fprintf(stderr, "could not open %s: %s\n", url.c_str(), SDL_GetError());
    };
#endif
    SDL_Cursor* arrowCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    SDL_Cursor* handCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    bool showingHand = false;
    auto updateCursor = [&]() {
        bool hand = player && player->wantsHandCursor();
        if (hand == showingHand) return;
        showingHand = hand;
        SDL_SetCursor(hand ? handCursor : arrowCursor);
    };
    auto updatePad = [&]() {
        std::set<int> held;
        padLeft = padRight = padUp = padDown = false;
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            // Opened in SDL_CONTROLLERDEVICEADDED; null for joysticks that aren't controllers.
            SDL_GameController* pad = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
            if (!pad) continue;
            auto button = [pad](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(pad, b) != 0; };
            double lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0;
            double ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0;
            // Vertical needs a firmer push: up also jumps, so a sideways push shouldn't.
            padLeft |= button(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || lx < -0.35;
            padRight |= button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx > 0.35;
            padUp |= button(SDL_CONTROLLER_BUTTON_DPAD_UP) || ly < -0.6;
            padDown |= button(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ly > 0.6;
            if (button(SDL_CONTROLLER_BUTTON_A)) held.insert('S');
            if (button(SDL_CONTROLLER_BUTTON_B)) held.insert('C');
            if (button(SDL_CONTROLLER_BUTTON_X)) held.insert('X');
            if (button(SDL_CONTROLLER_BUTTON_Y)) held.insert('Z');
            if (button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) || button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))
                held.insert('Z');
        }
        static bool startWasDown = false;
        bool startDown = false;
        for (int i = 0; i < SDL_NumJoysticks(); i++)
            if (SDL_GameController* pad = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i)))
                startDown |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START) != 0;
        if (startDown && !startWasDown) togglePauseMenu();
        startWasDown = startDown;
        if (!noclip) {  // in noclip the directions fly Mario instead
            if (padLeft) held.insert(as::keycode::Left);
            if (padRight) held.insert(as::keycode::Right);
            if (padUp) held.insert(as::keycode::Up);
            if (padDown) held.insert(as::keycode::Down);
        }
        for (int code : padKeys)
            if (!held.count(code)) player->keyUp(code);
        for (int code : held)
            if (!padKeys.count(code)) player->keyDown(code, code >= 'A' && code <= 'Z' ? code - 'A' + 'a' : 0);
        padKeys = std::move(held);
    };

    // Audio output: the mixer runs on SDL's audio thread.
    SDL_AudioDeviceID audioDevice = 0;
    if (player && !mute) {
        SDL_AudioSpec want{}, have{};
        want.freq = mixer.outputRate();
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;
        want.userdata = &mixer;
        want.callback = [](void* user, Uint8* stream, int len) {
            static_cast<audio::Mixer*>(user)->mix(reinterpret_cast<int16_t*>(stream), (size_t)len / 4);
        };
        audioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (audioDevice) SDL_PauseAudioDevice(audioDevice, 0);
        else std::fprintf(stderr, "no audio: %s\n", SDL_GetError());
    }

    const double frameMs = 1000.0 / movie.frameRate;
    double acc = 0;
    Uint64 last = SDL_GetPerformanceCounter();
    bool running = true;
    // Frames render on the worker threads while this thread waits for the display's
    // vsync to present the previous one, so slow frames don't drop to every other refresh.
    // Input and game ticks only run between renders: scripts change the display list.
    std::future<void> rendering;
    int framesShown = 0;
    Uint64 fpsSince = SDL_GetPerformanceCounter();
    double renderFps = 0;  // frames actually drawn per second, measured over ~1 s
    // The game's own fps counter (Menu/Framerate Counter) measures game ticks, which stay
    // at 32 now that frames are drawn between them; show the frames drawn instead. The
    // game's calculation still runs (its auto-quality uses it), only the text changes.
    std::weak_ptr<flash::DisplayObject> fpsCounter;
    int fpsCounterSearchIn = 0;  // ticks until the next search when there is no counter
    if (player)
        player->beforeBindings = [&]() {
            if (renderFps <= 0) return;
            auto counter = fpsCounter.lock();
            if (!counter || as::isRemoved(counter.get())) {
                counter.reset();
                if (fpsCounterSearchIn-- > 0) return;
                fpsCounterSearchIn = 32;
                std::function<void(flash::DisplayObject&)> find = [&](flash::DisplayObject& d) {
                    if (counter) return;
                    if (dynamic_cast<MovieClip*>(&d) &&
                        !as::getMember(as::valueOf(&d), as::intern("frtxt")).isUndefined()) {
                        counter = d.shared_from_this();
                        return;
                    }
                    d.forEachChild(find);
                };
                find(player->root());
                fpsCounter = counter;
                if (!counter) return;
            }
            // Same format as the game: one decimal, "fps: 07.5" below 10.
            double fps = std::round(renderFps * 10) / 10;
            char text[32];
            std::snprintf(text, sizeof text, "fps: %s%.1f", fps < 10 ? "0" : "", fps);
            as::setMember(as::valueOf(counter.get()), as::intern("frtxt"), as::Value(std::string(text)));
        };
    auto finishRendering = [&]() {
        if (!rendering.valid()) return;
        rendering.get();
        SDL_UpdateTexture(tex, nullptr, surf.pixels.data(), surf.width * 4);
        framesShown++;
        // Rendered frames per second in the title (the game's own counter shows its ticks).
        double secs = (SDL_GetPerformanceCounter() - fpsSince) / (double)SDL_GetPerformanceFrequency();
        if (player && secs >= 0.5) renderFps = framesShown / secs;
        if (player && secs >= 1) {
            char title[128];
            std::snprintf(title, sizeof title, "Super Mario 63 — %.0f fps, %dx%d%s%s%s", framesShown / secs, surf.width,
                          surf.height, smooth ? "" : " (smooth motion off)", noclip ? " [NOCLIP]" : "",
                          vsync ? "" : " (vsync off)");
            SDL_SetWindowTitle(win, title);
            framesShown = 0;
            fpsSince = SDL_GetPerformanceCounter();
        }
    };
    while (running) {
        finishRendering();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            // Controllers: open them as they appear (including ones present at startup).
            if (e.type == SDL_CONTROLLERDEVICEADDED) {
                if (SDL_GameController* pad = SDL_GameControllerOpen(e.cdevice.which))
                    std::printf("controller connected: %s\n", SDL_GameControllerName(pad));
            } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
                if (SDL_GameController* pad = SDL_GameControllerFromInstanceID(e.cdevice.which))
                    SDL_GameControllerClose(pad);
            }
            if (player) {
                switch (e.type) {
                    case SDL_KEYDOWN:
                        if (e.key.keysym.sym == SDLK_ESCAPE && (e.key.keysym.mod & KMOD_SHIFT)) running = false;
                        if (e.key.keysym.sym == SDLK_AC_BACK) running = false;  // Android back button
                        if (e.key.keysym.sym == SDLK_ESCAPE && !(e.key.keysym.mod & KMOD_SHIFT) && !e.key.repeat &&
                            togglePauseMenu())
                            break;  // the pause menu, not a key for the game
                        // Port-only shortcuts, not passed to the game.
                        if (e.key.keysym.sym == SDLK_F11 && !e.key.repeat) {
                            bool full = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                            SDL_SetWindowFullscreen(win, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                            break;
                        }
                        if (e.key.keysym.sym == SDLK_F9 && !e.key.repeat) {
                            // Debug snapshot: the whole display tree plus the current frame.
                            static int dumps = 0;
                            dumps++;
                            char path[64];
                            std::snprintf(path, sizeof path, "sm63-dump-%d.txt", dumps);
                            if (FILE* f = std::fopen(path, "w")) {
                                for (const char* v : {"_root.coursescale", "_root.Course._x", "_root.Course._y",
                                                      "_root.Course.Char._x", "_root.Course.Char._y"}) {
                                    std::string p = v;
                                    size_t dot = p.rfind('.');
                                    flash::DisplayObject* base = as::resolvePath(&player->root(), p.substr(0, dot));
                                    as::Value val = base ? as::getMember(as::valueOf(base), as::intern(p.substr(dot + 1)))
                                                         : as::Value();
                                    std::fprintf(f, "%s = %s\n", v, val.toString().c_str());
                                }
                                printTree(f, player->root(), 0, 14);
                                std::fclose(f);
                            }
                            std::snprintf(path, sizeof path, "sm63-dump-%d.bmp", dumps);
                            saveBmp(surf, path);
                            std::printf("wrote sm63-dump-%d.txt and .bmp\n", dumps);
                            break;
                        }
                        if (e.key.keysym.sym == SDLK_F5 && !e.key.repeat) {
                            viewMode = viewMode % ViewModes + 1;
                            bool letterbox = viewMode == Letterbox;
                            SDL_RenderSetLogicalSize(ren, letterbox ? W : 0, letterbox ? H : 0);
                            std::printf("view %d: %s\n", viewMode, viewNames[viewMode]);
                            redraw = true;
                            break;
                        }
                        if (e.key.keysym.sym == SDLK_F3 && !e.key.repeat) {
                            player->debugOverlay = !player->debugOverlay;
                            redraw = true;
                            break;
                        }
                        if (e.key.keysym.sym == SDLK_F6 && !e.key.repeat) {
                            smooth = !smooth;
                            redraw = true;
                            break;
                        }
                        // F7: vsync off draws as fast as the renderer can (the title's fps then
                        // measures rendering speed; expect tearing).
                        if (e.key.keysym.sym == SDLK_F7 && !e.key.repeat) {
                            vsync = !vsync;
                            if (SDL_RenderSetVSync(ren, vsync) != 0)
                                std::fprintf(stderr, "could not change vsync: %s\n", SDL_GetError());
                            std::printf("vsync %s\n", vsync ? "on" : "off");
                            break;
                        }
                        if (e.key.keysym.sym == SDLK_F2 && !e.key.repeat) {
                            noclip = !noclip;
                            as::Value root = as::valueOf(&player->root());
                            if (noclip) invincibleBeforeNoclip = as::getMember(root, as::intern("Invincible"));
                            else as::setMember(root, as::intern("Invincible"), invincibleBeforeNoclip);
                            as::Value c = charValue();
                            if (c.isObject()) {
                                noclipX = as::getMember(c, as::intern("_x")).toNumber();
                                noclipY = as::getMember(c, as::intern("_y")).toNumber();
                            }
                            std::printf("noclip %s\n", noclip ? "on (arrows fly, Shift faster)" : "off");
                            break;
                        }
                        if (e.key.keysym.sym == SDLK_F4 && !e.key.repeat) {
                            renderSettings.crispBitmaps = !renderSettings.crispBitmaps;
                            redraw = true;
                            break;
                        }
                        if (noclip && (e.key.keysym.sym == SDLK_LEFT || e.key.keysym.sym == SDLK_RIGHT ||
                                       e.key.keysym.sym == SDLK_UP || e.key.keysym.sym == SDLK_DOWN))
                            break;  // noclip flies with the arrows; the game doesn't see them
                        if (!e.key.repeat) {
                            int code = flashKeyCode(e.key.keysym.sym);
                            if (code) player->keyDown(code, flashAscii(e.key.keysym));
                        }
                        break;
                    case SDL_KEYUP: {
                        int code = flashKeyCode(e.key.keysym.sym);
                        if (code) player->keyUp(code);
                        break;
                    }
                    case SDL_MOUSEMOTION: {
                        double mx = e.motion.x, my = e.motion.y;
                        if (viewMode != Letterbox) {
                            // Window coordinates -> output pixels -> stage pixels.
                            int ww, wh, ow, oh;
                            SDL_GetWindowSize(win, &ww, &wh);
                            SDL_GetRendererOutputSize(ren, &ow, &oh);
                            mx = (mx * ow / std::max(1, ww) - viewX) / viewScale;
                            my = (my * oh / std::max(1, wh) - viewY) / viewScale;
                        }
                        player->mouseMove(mx, my);
                        break;
                    }
                    case SDL_MOUSEBUTTONDOWN:
                        if (e.button.button == SDL_BUTTON_LEFT) player->mouseDown();
                        break;
                    case SDL_MOUSEBUTTONUP:
                        if (e.button.button == SDL_BUTTON_LEFT) player->mouseUp();
                        break;
                }
                continue;
            }
            if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_SPACE: viewed->playing = !viewed->playing; break;
                    case SDLK_RIGHT: viewed->gotoFrame(viewed->currentFrame() + 1, false); break;
                    case SDLK_LEFT: viewed->gotoFrame(viewed->currentFrame() - 1, false); break;
                    default: break;
                }
                redraw = true;
                char title[96];
                std::snprintf(title, sizeof title, "Super Mario 63 — frame %d/%d%s",
                              viewed->currentFrame(), viewed->totalFrames(),
                              viewed->playing ? "" : " (paused)");
                SDL_SetWindowTitle(win, title);
            }
        }
        if (player) updatePad();
        if (player) updatePauseMenu();
        if (quitRequested) running = false;
        updateCursor();
        Uint64 now = SDL_GetPerformanceCounter();
        acc += (now - last) * 1000.0 / SDL_GetPerformanceFrequency();
        last = now;
        int steps = 0;
        while (acc >= frameMs && steps < 4) {
            tick();
            acc -= frameMs;
            steps++;
        }
        if (steps == 4) acc = 0;
        // Without smoothing, render only when the game advanced (32 fps) or the window size
        // changed; the display may refresh more often than that.
        interpolation = smooth && player ? std::clamp(acc / frameMs, 0.0, 1.0) : 1.0;
        if (fitSurface() || steps || redraw || (smooth && player)) {
            applyView();  // after ticks: objects may have been replaced
            rendering = std::async(std::launch::async, renderFrame);
            redraw = false;
        }
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
    }
    if (rendering.valid()) rendering.get();
    reportMissing();
    if (audioDevice) SDL_CloseAudioDevice(audioDevice);
    as::flushSharedObjects();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
