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
Python 3 (standard library only) is needed for this step, not for building or running.

**Licensing:** the generated code is a translation of runouw's code. The upstream repository
has no license file, so keep it private unless runouw gives permission to publish it.
Without `src/game/generated`, the build still works and runs the timelines without scripts.

## Build

Requires a C++17 compiler, CMake and SDL2. `third_party/minimp3` (CC0) is vendored.

    cmake -S . -B build -G Ninja
    ninja -C build

## Run (milestones 1–3 and 5 done; game scripts not ported yet)

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
Game mode: input goes to the game; Shift+Escape quits.
