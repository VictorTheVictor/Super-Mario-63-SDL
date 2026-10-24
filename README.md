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
Game mode: input goes to the game; Shift+Escape quits. F11 toggles fullscreen, F2 toggles
crisp (nearest-neighbor) and smooth scaling of enlarged bitmaps.

### Resolution and rendering

The game renders at the window's real pixel size (letterboxed to the stage's 3:2), so a
maximized or fullscreen window shows sharper vector art instead of an upscaled 450×300
image. Bitmaps drawn larger than their native size use nearest-neighbor sampling (crisp
pixels) unless `--smooth` is given or F2 is pressed.

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
