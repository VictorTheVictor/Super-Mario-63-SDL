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
Re-run `cmake -S . -B build/debug` afterwards so the build picks up added or removed files.
Python 3 (standard library only) is needed for this step, not for building or running.

**Licensing:** the generated code is a translation of runouw's code. The upstream repository
has no license file, so keep it private unless runouw gives permission to publish it.
Without `src/game/generated`, the build still works and runs the timelines without scripts.

## Build

Requires a C++17 compiler, CMake and SDL2. `third_party/minimp3` (CC0) is vendored.

    cmake -S . -B build/debug -G Ninja      # RelWithDebInfo (default): optimized, with debug info
    ninja -C build/debug

Release builds for other people go in `build/release`, so the two never overwrite each
other. On Linux, with the SDL2 2.32.10 source (see android/README.md):

    scripts/build-linux-release.sh path/to/SDL2-2.32.10 [path/to/Super_Mario_63_2010.swf]

builds a stripped `build/release/sm63` with SDL2 and the C++ runtime linked in (only glibc
and the desktop libraries SDL loads at runtime come from the system) and packages
`dist/sm63-linux-x86_64.tar.gz`. `src/compat/` binds it to old glibc symbol versions, so
it runs on glibc 2.34 and newer (Ubuntu 22.04, Debian 12, Fedora 35 and later) even when
built on a current distribution.

### Windows (cross-compiled, Windows XP and newer, 32- and 64-bit)

Needs MinGW-w64 (Arch: `mingw-w64-gcc`) and the SDL2 2.32.10 source (see android/README.md):

    scripts/build-windows.sh path/to/SDL2-2.32.10 [path/to/Super_Mario_63_2010.swf]

This writes `dist/sm63-win32.zip` and `dist/sm63-win64.zip`, each with `sm63/sm63.exe`
(SDL2 and the C++ runtime linked in; no DLLs needed) and the SWF if one was given.

The executables link the system `msvcrt.dll` (present since Windows XP) instead of the
Universal CRT that current MinGW-w64 builds default to (Vista and later).
`scripts/check-xp-imports.py dist-exe` statically checks that an exe imports no DLL or
known function that Windows XP lacks; it cannot replace a test on a real XP machine.

## Run

Without a path, the game loads `Super_Mario_63_2010.swf` from the executable's folder,
so a release is just the executable next to the SWF. If it is missing, the window says
so (in the game's sign font) and starts the game when the SWF is dropped on it; dropping
the SWF onto the executable passes it as the path too.

    ./build/debug/sm63 path/to/Super_Mario_63_2010.swf                  # game mode (script Player)
    ./build/debug/sm63 path/to/Super_Mario_63_2010.swf --symbol NAME    # one exported symbol
    ./build/debug/sm63 path/to/Super_Mario_63_2010.swf --stats          # SWF contents summary
    ./build/debug/sm63 path/to/Super_Mario_63_2010.swf --check-exports  # render every export (smoke test)
    ./build/debug/sm63 --selftest out.bmp                               # rasterizer + filter test images
    ./build/debug/swf_check path/to/Super_Mario_63_2010.swf             # structural checks (morphs, fonts, text, buttons)
    ./build/debug/jpeg_check in.jpg out.rgb [tables.bin]                # JPEG decoder, raw RGB output
    ./build/debug/as_test                                               # AS2 runtime unit tests
    ./build/debug/stage_test path/to/Super_Mario_63_2010.swf            # MovieClip API + event order
    ./build/debug/audio_test path/to/Super_Mario_63_2010.swf [sine.mp3] # decoders, mixer, Sound API

Symbol viewer keys: Space play/pause, Left/Right step a frame, Escape quit.
Game mode: input goes to the game; Shift+Escape quits. P opens the game's own pause menu
(Star Map, Options, FLUDD, Exit to leave a level), and so do Escape and a controller's
Start button; the sound playing when it opens, and anything the game starts meanwhile
(ambient loops), is held until it closes. A pause sound plays as it opens and closes when
the build embeds one: put `SM64_Pause.oga.mp3` (an MP3) at the repository root (ignored by
git) before building. The options screen
has two placeholder buttons beside Quality (Controls and Advanced, unlabeled for now; they
do nothing yet). F11 toggles fullscreen. F5 cycles
how the window is filled:

1. letterboxed (only the stage, black bars);
2. sky repeated in every direction;
3. sky repeated, with the copies above and below flipped vertically;
4. sky extended by a mirrored copy on its left, the pair centered;
5. sky extended by a mirrored copy on its right, the pair centered;
6. sky zoomed to fill the window (cropped);
7. sky stretched to fill the window;
8. sky as is, black beyond it;

then back to 1. Modes 2–8 fill the window and show what lies beyond the stage edges; they
hide the HUD's 25% black frame around the stage and the black rectangle behind it, move
the HUD's counters, hint line and quality box to the window's edges, stretch the screen's
dark overlay (the castle before enough stars) over the whole window, show door and
course transitions letterboxed, fill menus with their own background color (the course
select's white page), and continue the ground past a level's left and right ends by
repeating a slice of the terrain next to each end that leads on (the ground only, not
trees, signs or other objects; not in the castle's rooms; see src/EdgeFill.h); at an end
that leads nowhere the view pans no further than the end instead. Fades (to and from white
or black) cover the window; the wipes in a level stay letterboxed. The game's
pause menu's white panel covers the window and its counters move to the edges too. F4 toggles crisp (nearest-neighbor) and smooth scaling of enlarged bitmaps, F7 toggles vsync (off: draw as fast as possible, to measure rendering), F8 toggles building
nested filter layers once (on by default; off draws them per strip as before, slower, and can
differ by 1/255 in a few pixels), F6 toggles
smooth motion. The game's fps counter shows the frames actually drawn per second.
Links (the Runouw and Newgrounds banners) open in the default browser (not on Android),
and the cursor turns into a hand over anything clickable.

Text the SWF's embedded fonts can't fully draw (they often hold only the letters of a
field's initial text, e.g. the star names' "Star Name" Comic Sans) is drawn in another
font: the real font when the build embeds it, else the closest embedded one. The game's
fields use Arial (bold for signs and messages, which lack J, Q, q, ";" and '"'; regular;
italic for the music info) and Comic Sans MS bold (the star names). These are Microsoft's
fonts and not in this repository: put your own `arialbd.ttf`, `arial.ttf`, `ariali.ttf`,
`comicbd.ttf` and/or `comic.ttf` in `fonts/` (ignored by git) and CMake compiles them into
the executable (cmake/embed_assets.cmake, which also embeds the pause sound). From a Windows 7 ISO, for example:

    7z e win7.iso sources/install.wim
    7z e -ofonts install.wim 1/Windows/Fonts/{arial,arialbd,ariali,comic,comicbd}.ttf

Game controllers (SDL GameController, e.g. Xbox layout): D-pad or left stick moves, A jumps,
B uses FLUDD, X spins, Y ground pounds and talks (Z), the bumpers also press Z. They press
the same keys the game reads from the keyboard (S, C, X, Z and the arrows). The right stick
zooms the camera: up zooms in, down zooms out (the game's + and - keys).
Debugging: F2 toggles noclip (arrow keys fly Mario through walls, Shift for speed, easing to a stop; he is invulnerable meanwhile), F3
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
    --threads 1 --no-layer-cache --no-occlusion   # reference renderer for comparisons
    --no-shared-layers                      # nested filter layers per strip (F8 off)
    --no-edge-fill                          # nothing drawn past the level ends
    --interpolate 0.5                       # screenshot halfway between the last two ticks
    --view 3 --window 1920x1080             # screenshot a window of that size in F5 mode 3
    --course C-1@F | --course C-1,lefttransition2,700,-250@F | --course StartInsideofCastle()@F
    --course gotoCourseSelect(1)@F          # a starter function with a number argument
                                            # switch course at frame F (as the game does)
    --symbol '#2539'                        # view any character by id, scaled to fit
