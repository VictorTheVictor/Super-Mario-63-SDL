# Super Mario 63 — C++/SDL2 port

A native port of runouw's Super Mario 63 (2010 ActionScript 2 source). See [PLAN.md](PLAN.md)
for the architecture and milestones.

This repository contains **no game assets**. At runtime the engine reads art, sounds and
timelines from your own copy of `Super_Mario_63_2010.swf`
(from https://github.com/runouw/Super-Mario-63, `Publish/`).

## Game scripts (tools/as2cpp)

The game logic is transpiled from runouw's ActionScript in the XFL source into C++
(`src/game/generated/`, one function per frame script or event handler, comments kept):

    python3 tools/as2cpp/as2cpp.py \
        --xfl path/to/Super-Mario-63/Super_Mario_63_2010 \
        --swf path/to/Super-Mario-63/Publish/Super_Mario_63_2010.swf \
        --out src/game/generated

It maps each script to the SWF location it was compiled to and verifies the mapping against
the compiled bytecode; `src/game/generated/REPORT.txt` lists anything it could not map.
Re-run `cmake -S . -B build` afterwards so the build picks up added or removed files.
Python 3 (standard library only) is needed for this step, not for building or running.

**Licensing:** the generated code is a translation of runouw's code. The upstream repository
has no license file, so keep it private unless runouw gives permission to publish it.
Without `src/game/generated`, the build still works and runs the timelines without scripts.

## Build

Requires a C++17 compiler, CMake and SDL2. `third_party/minimp3` (CC0) is vendored.

    cmake -S . -B build -G Ninja
    ninja -C build

## Run

    ./build/sm63 path/to/Super_Mario_63_2010.swf                  # game mode (script Player)
    ./build/sm63 path/to/Super_Mario_63_2010.swf --symbol NAME    # one exported symbol
    ./build/sm63 path/to/Super_Mario_63_2010.swf --stats          # SWF contents summary
    ./build/sm63 path/to/Super_Mario_63_2010.swf --check-exports  # render every export (smoke test)
    ./build/sm63 --selftest out.bmp                               # rasterizer + filter test images
    ./build/swf_check path/to/Super_Mario_63_2010.swf             # structural checks (morphs, fonts, text, buttons)
    ./build/jpeg_check in.jpg out.rgb [tables.bin]                # JPEG decoder, raw RGB output
    ./build/as_test                                               # AS2 runtime unit tests
    ./build/stage_test path/to/Super_Mario_63_2010.swf            # MovieClip API + event order
    ./build/audio_test path/to/Super_Mario_63_2010.swf [sine.mp3] # decoders, mixer, Sound API

Symbol viewer keys: Space play/pause, Left/Right step a frame, Escape quit.
Game mode: input goes to the game; Shift+Escape quits. F11 toggles fullscreen, F5 toggles
letterboxing (off: the game fills the window and shows what lies beyond the stage edges),
F4 toggles crisp (nearest-neighbor) and smooth scaling of enlarged bitmaps, F6 toggles
smooth motion. The game's fps counter shows the frames actually drawn per second.
Links (the Runouw and Newgrounds banners) open in the default browser, and the cursor
turns into a hand over anything clickable.

Game controllers (SDL GameController, e.g. Xbox layout): D-pad or left stick moves, A jumps,
B uses FLUDD, X spins, Y ground pounds and talks (Z), the bumpers also press Z. They press
the same keys the game reads from the keyboard (S, C, X, Z and the arrows).
Debugging: F2 toggles noclip (arrow keys fly Mario through walls, Shift for speed), F3
shows every object's outline over the frame (shape edges in cyan; boxes for clips in
green, text in yellow, buttons in magenta, masks in orange; invisible objects such as the
collision clips in red), F9 writes `sm63-dump-N.txt` (display tree) and `sm63-dump-N.bmp`.
The window title shows the rendered frames per second.

### Smooth motion

The game logic still runs at the SWF's 32 ticks per second, so physics and timing are
unchanged, but the window draws a frame every display refresh: moving objects are drawn
between their positions at the last two ticks (one tick of display latency). Flips and
jumps of more than 100 px snap instead of sliding. Scripts and hit tests always see the
real positions. Frames render on the worker threads while the previous one waits for
vsync. `--no-smooth-motion` (or F6) draws only at game ticks.

### Resolution and rendering

The game renders at the window's real pixel size (letterboxed to the stage's 3:2), so a
maximized or fullscreen window shows sharper vector art instead of an upscaled 450×300
image. Bitmaps drawn larger than their native size use nearest-neighbor sampling (crisp
pixels) unless `--smooth` is given or F4 is pressed.

Rendering splits each frame into horizontal bands drawn on all hardware threads
(`--threads N` to change). Filtered layers (glows, bevels, shadows) are kept between
frames and reused while nothing in them changes; changed ones are rendered with all
threads before the bands.

Headless options for testing and profiling (with `--frames N`):

    --screenshot out.bmp --render-scale 3   # render at 3× the stage size
    --bench / --bench-from F                # per-frame script and render timings
    --profile out.prof                      # sampling profiler (Linux); report with tools/profile.py
    --deterministic                         # fixed random seed and frame-driven clock
    --threads 1 --no-layer-cache            # reference renderer for comparisons
    --interpolate 0.5                       # screenshot halfway between the last two ticks
    --course C-1@F | --course C-1,lefttransition2,700,-250@F | --course StartInsideofCastle()@F
                                            # switch course at frame F (as the game does)
    --symbol '#2539'                        # view any character by id, scaled to fit
