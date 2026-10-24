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
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include <string>

#include "as/Audio.h"
#include "as/Builtins.h"
#include "as/Diagnostics.h"
#include "as/Player.h"
#include "flash/DisplayObject.h"
#include "game/Scripts.h"
#include "flash/Effects.h"
#include "flash/Raster.h"
#include "flash/Swf.h"

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
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: %s <game.swf> [--symbol NAME] [--scale N] [--frames N --screenshot "
                     "out.bmp] [--stats]\n       %s --selftest out.bmp\n",
                     argv[0], argv[0]);
        return 1;
    }
    std::string swfPath = argv[1], symbol, screenshot, watch, inputSpec, calls, courseSpec, treePath, probes,
                gotoSpec, trackSpec, setSpec, findText;
    int scale = 2, frames = 0, sampleEvery = 0, benchFrom = 0;
    bool stats = false, checkExports = false, mute = false, dump = false, traces = false,
         diagnose = false, bench = false;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--symbol" && i + 1 < argc) symbol = argv[++i];
        else if (a == "--scale" && i + 1 < argc) scale = std::max(1, std::atoi(argv[++i]));
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

    Movie movie;
    std::string err;
    Uint32 t0 = SDL_GetTicks();
    if (!movie.load(swfPath, err)) {
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
        Character* def = movie.exported(symbol);
        if (!def) {
            std::fprintf(stderr, "no exported symbol '%s'\n", symbol.c_str());
            return 1;
        }
        symbolStage = std::make_shared<MovieClip>(movie, &movie.root);
        symbolStage->stop();
        auto obj = symbolStage->attach(def, 1, "viewed");
        obj->matrix = Matrix::translate(W * 10, H * 10);
        viewed = dynamic_cast<MovieClip*>(obj.get());
    } else {
        // _url as Flash Player reports it: the SWF's own file URL.
        if (char* abs = realpath(swfPath.c_str(), nullptr)) {
            as::stage().url = std::string("file://") + abs;
            std::free(abs);
        }
        // Like the standalone Flash Player, trace() prints nothing unless asked.
        if (!traces) as::Runtime::get().traceOutput = [](const std::string&) {};
        as::diagnostics().enabled = diagnose;
        game::registerScripts();
        audioBridge = std::make_unique<as::AudioBridge>(movie, mixer);
        player = std::make_unique<as::Player>(movie);
        player->setAudio(audioBridge.get());
        viewed = &player->root();
    }

    Surface surf;
    surf.resize(W, H);
    Rasterizer rast;
    const Color bg = movie.background;
    auto renderFrame = [&]() {
        if (player) {
            player->render(rast, surf);
            return;
        }
        surf.clear(0xFF000000u | bg.r << 16 | bg.g << 8 | bg.b);
        renderDisplayObject(*symbolStage, rast, surf, {0, 0, W, H}, Matrix::scale(0.05, 0.05),
                            ColorTransform{}, movie);
    };
    auto tick = [&]() {
        if (player) player->tick();
        else symbolStage->advance();
    };
    auto reportMissing = [&]() {
        if (player && !player->missingScripts().empty())
            std::printf("%zu frame scripts ran without a ported implementation\n",
                        player->missingScripts().size());
    };

    if (!screenshot.empty() || dump || bench || diagnose) {
        std::vector<InputEvent> events = parseInput(inputSpec);
        std::vector<double> tickMs, renderMs;
        for (int i = 0; i < frames; i++) {
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
            // --course NAME@FRAME: switch course part through the game's own path:
            // _root.changecourse("StarIn", NAME, 0, 0, 0, 0, true)  (RemoveCourse + StartChar)
            if (player && !courseSpec.empty() && i == std::atoi(courseSpec.c_str() + courseSpec.rfind('@') + 1)) {
                as::Value root = as::valueOf(&player->root());
                as::Value argv2[7] = {"StarIn", as::Value(courseSpec.substr(0, courseSpec.rfind('@'))), 0, 0, 0, 0, true};
                as::callFunction(root["changecourse"].get(), root, as::Args{argv2, 7});
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
                renderFrame();
                renderMs.push_back((SDL_GetPerformanceCounter() - t1) * 1000.0 / SDL_GetPerformanceFrequency());
                tickMs.push_back((t1 - t0) * 1000.0 / SDL_GetPerformanceFrequency());
            }
        }
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
                        Rasterizer::rejectedSegments, Rasterizer::clampedRows, Rasterizer::maxOvershoot);
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
                std::function<void(flash::DisplayObject&, int)> tree = [&](flash::DisplayObject& d, int depth) {
                    flash::Rect b = d.worldMatrix().apply(d.localBounds());
                    const char* kind = dynamic_cast<MovieClip*>(&d) ? "clip" : d.definition() ? "shape/text" : "?";
                    std::printf("  %*s%s [%s %d] depth %d clipDepth %d bounds (%.0f,%.0f)-(%.0f,%.0f) %s alpha %.2f xs %.2f ys %.2f\n",
                                depth * 2, "", d.name.empty() ? "-" : d.name.c_str(), kind, d.definition() ? d.definition()->id : -1, d.depth, d.clipDepth,
                                b.xMin / 20, b.yMin / 20, b.xMax / 20,
                                b.yMax / 20, d.visible ? "visible" : "HIDDEN", d.cxform.aMul, d.matrix.a, d.matrix.d);
                    if (depth < 4) d.forEachChild([&](flash::DisplayObject& c) { tree(c, depth + 1); });
                };
                if (auto* d = as::resolvePath(&r, treePath)) tree(*d, 0);
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
        renderFrame();
        if (!saveBmp(surf, screenshot.c_str())) return 1;
        std::printf("wrote %s (frame %d)\n", screenshot.c_str(), viewed ? viewed->currentFrame() : 0);
        reportMissing();
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO | (player && !mute ? SDL_INIT_AUDIO : 0)) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("Super Mario 63", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, W * scale, H * scale,
                                       SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, W, H);  // also maps mouse events to stage pixels
    SDL_Texture* tex =
        SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);

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
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (player) {
                switch (e.type) {
                    case SDL_KEYDOWN:
                        if (e.key.keysym.sym == SDLK_ESCAPE && (e.key.keysym.mod & KMOD_SHIFT)) running = false;
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
                    case SDL_MOUSEMOTION: player->mouseMove(e.motion.x, e.motion.y); break;
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
                char title[96];
                std::snprintf(title, sizeof title, "Super Mario 63 — frame %d/%d%s",
                              viewed->currentFrame(), viewed->totalFrames(),
                              viewed->playing ? "" : " (paused)");
                SDL_SetWindowTitle(win, title);
            }
        }
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
        renderFrame();
        SDL_UpdateTexture(tex, nullptr, surf.pixels.data(), W * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
    }
    reportMissing();
    if (audioDevice) SDL_CloseAudioDevice(audioDevice);
    as::flushSharedObjects();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
