# Super Mario 63 native port

Current milestone: a C++17 / SDL3 application built with CMake. The game starts
in the Mushroom Kingdom opening tutorial, with its first two areas connected.
Bob-omb Battlefield areas 1-1 through 1-5 and Snowman’s Land’s opening area
are also playable. Artwork and animations are imported from the original XFL.
The original Flash source remains unchanged.

The source exit regions now connect all five course areas. Each loads its own static
terrain, artwork, supported enemy/item placements and signs. Collected coins,
broken boxes and enemy states persist when revisiting areas during the run;
health and counters carry across exits. Death/falling respawns at the latest
entry point. The mountain (1-4) and Secret of the Mountain (1-5) are loaded. Snowman's Land's opening area (2-1)
is also playable with its original terrain, scenery, background and music.
Green moving platforms and tipping logs now use original artwork and source
motion parameters throughout the course, including circular green platforms in 1-2. Mario can ride them, jump through from
below, and land on top. Their state persists on area revisits. Cannons, sling stars, falling logs, rotating
blocks and touch-activated platforms now provide the remaining course traversal
objects. Castle return and mission variants remain pending.

For exploration, press **F2**, then use the arrows to fly through terrain.
**Page Up / Page Down** switches areas directly. F2 returns to gameplay; if Mario
is inside solid terrain, he returns to the current entry point. World simulation
is frozen while exploring. These are development controls, not original abilities.

Start directly with `./build/dev/sm63 --area 1-5 --explore` (areas 1-1 through 1-5).
Copy all generated asset folders, background PNGs and audio alongside the executable when relocating.
See [COURSE_OBJECTS.md](COURSE_OBJECTS.md) for remaining object work.

This is **not the full game**. The default view supports walking, double-tap
running, chained double/triple jumps, ground pound, diving and belly slides, solid/one-way terrain, slopes, a following
camera, and respawning. Mario uses the actual gameplay bitmap tracks for walking,
running, rising and falling, with imported parent transforms and a speed-driven
walk cycle. Static ground and trees are baked from the original area artwork.
Six Goombas use original area placements and walking/squish graphics. They patrol,
notice Mario, take stomps or dive knockback, and damage Mario on side contact.
The HUD shows the original health/life/coin icons, red and Silver Star counts,
and native water/power gauges. Lives start at four; death/falling costs a life,
and each 100 coins grants one. R starts a fresh run. Bob-ombs now chase, shove without contact damage, and explode on powered impacts or fuse expiry; Koopas become kickable shells, Shy Guys lose flight when stomped, and Goonies can carry Mario. Their source placements and animation artwork are imported throughout the course. King Bob-omb now throws bombs, grabs Mario and creates jump shockwaves; returning
bombs with a spin can defeat him and reveal his Shine. X spins on the ground or near the jump apex,
knocking Goombas away and breaking boxes. Original course music and effects
now play through SDL3; M toggles mute, and P pauses both simulation and audio. The area now has 34 gold/red coins and
three solid breakable boxes at their source placements. Fast side impacts
(over 9 pixels/tick) or ground pounds break boxes; their coin rewards bounce
into the level. Defeated Goombas also drop coins. Five gold coins or one red coin
restore one health segment; red coins add two to the coin counter. R resets
coins, boxes, enemies, and counters. See
[PORTING.md](PORTING.md) for provenance and fidelity limits.

Signs use their original illustrated panels or source text (including italic spans),
with Z-to-read, Mario's reading pose, opening/closing, and release-before-dismiss.
Music continues while signs freeze gameplay. The mountain secret uses its original dark gradient and Secret Course music; outdoor exits preserve playback. Panel animation/tween fidelity still
has limits; see PORTING.md. FLUDD jets now knock enemies back, and fast turbo
contact attacks them. Explosions and moving shells can also break boxes.

For visual testing, `--position X,Y` places Mario at world coordinates, and
`--read-sign N` opens the Nth sign in the selected area. For example:
`./build/dev/sm63 --area 1-2 --position 597,-15 --explore` shows the repaired log
below the first red coin, near Chain Chomp's gate.

## Requirements

- C++17 compiler and CMake 3.24 or newer
- Ninja or Make
- FFmpeg for build-time decoding of original audio resources
- Fontconfig (`fc-match`) and Arial or a compatible substitute for sign text baking
- SDL 3.4 or newer development package (including its CMake package)
- Python 3.8 or newer, used only during the build to read XFL
- Pillow 9.1 or newer in the selected Python interpreter, for build-time artwork baking

SDL 3.4 provides PNG loading directly, so SDL_image is not required. Dependencies
are discovered locally, not automatically downloaded. If SDL is installed in a
custom prefix, pass `-DCMAKE_PREFIX_PATH=/path/to/sdl` when configuring.

## Build and run

From the repository root, using Ninja:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./build/dev/sm63
```

For an optimized Ninja build, use `release` in both configure/build commands.
Alternatively, use Make:

```sh
cmake -S . -B build/make -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build/make -j
ctest --test-dir build/make --output-on-failure
./build/make/sm63
```

The development executable defaults to this checkout's absolute `LIBRARY` path,
so it can run from another working directory. To move the executable and assets
elsewhere, copy `Super_Mario_63_2010/LIBRARY` plus the generated `back.png` and
`front.png` and the `audio/` directory from the build's `generated/` directory, and supply their locations:

```sh
./sm63 --assets /path/to/LIBRARY --data /path/to/generated
```

Only Linux has been built and exercised so far. Windows/macOS builds and
redistributable packaging remain to be verified. On Windows, the SDL3 runtime
DLL must be available to the executable.

## Controls

| Key | Action |
| --- | --- |
| Left / Right | Walk; reposition the sprite in animation preview |
| Double tap Left / Right | Run in that direction; release or reverse to cancel |
| Up | Jump |
| Space | Activate FLUDD; pauses in the tools |
| Shift | Cycle unlocked nozzles and unequip |
| Jump shortly after landing | Double jump; a third jump while moving performs a frontflip |
| Z while airborne | Ground pound (windup, descent, landing recovery) |
| Down while airborne | Dive; hold to remain in a slow belly slide |
| Jump during a slide | Diveflip while moving; reverse + jump at low speed backflips |
| X | Spin attack (grounded or when vertical speed is above -3); Down modifies an air spin |
| Left / Right inside a cannon | Aim; Space fires |
| X / Space while caught by a sling star | Launch |
| Up / Down underwater | Swim upward / dive downward |
| Up / Down during Wing Cap flight | Pitch; Z exits into a ground pound |
| Z during boss dialogue or a Star Coin message | Continue after releasing Z |
| M | Mute/unmute music and effects |
| P | Pause/resume game and audio |
| F3 | Cycle opening tutorial / Battlefield / Snowman’s Land (fresh run) |
| F1 | Toggle diagnostic terrain, player collision rectangle and coordinates |
| Z near a sign or Toad | Read/talk; release Z, then press again to close |
| R | Restart the selected course and clear its progress |
| Tab | Cycle level / animation preview / asset browser |
| [ / ] | Select previous / next PNG |
| Escape | Quit |

Rendering uses the original 450 x 300 logical stage with letterboxing and
nearest-neighbor texture sampling. Simulation updates at the source document's
32 Hz, independently of presentation. Display positions interpolate between
updates. Long stalls are capped at 250 ms.

## Verification

CTest includes importer tests for signed hexadecimal coordinates, curves, holes,
and unsupported syntax; deterministic physics checks on the actual imported
area for landing, jumping, held input, friction, slopes, one-way platforms,
walls, ceilings, running, jump chains, ground-pound timing/landing, dive entry/exits/recovery, bidirectional wall rebounds, and respawning; character track/transform
tests; and an SDL smoke test. The latter uses SDL's
dummy video driver/software renderer, uploads every PNG, and renders all three
views. These tests do not establish original gameplay parity or Windows support.

To capture a real-window render and exit automatically:

```sh
./build/dev/sm63 --frames 180 --screenshot build/dev/level-preview.png
```

`--smoke-test` is the bounded asset/render test used by CTest. `--help` lists CLI
options. Failures return a nonzero exit code and identify the SDL/asset error.
`--replay` plays a deterministic 180-tick walk/run/jump/ground-pound/dive sequence, useful with
`--frames` and `--screenshot` for repeatable graphics checks (e.g. frame 65 for jumping or 150 for diving). Extending the replay to 320 ticks reaches the stacked boxes.

## Source layout and next work

- `tools/import_preview.py` reads the original document and the symbol
  `Cinema 2 Mario (No fludd) Walking.xml`. It resolves library names to PNG
  exports and generates `assets.hpp` in the build directory. Original frame
  durations and registration offsets are preserved. It deliberately rejects
  scripts, tweens, multiple layers, and transforms beyond translation rather
  than silently claiming to support them.
- `native/src/main.cpp` owns SDL resources, input, fixed-step timing, animation
  playback, the level display, asset browsing, and smoke-test execution.
- `tools/import_level.py` imports static collision fills from area 1-1 and
  movement constants from `DOMDocument.xml`, generating `level.hpp` at build time.
- `tools/import_mario.py` extracts the gameplay sprite tracks and composes their
  transforms with `Mario Main`, producing `mario.hpp`.
- `tools/import_art.py` bakes static BackGFX/FrontGFX fill geometry and bitmap
  instances into PNGs. Omitted interactive symbols/strokes are recorded in the
  build's `generated/art-report.json`.
- `native/src/physics.hpp` and `physics.cpp` contain the SDL-independent collision
  grid and player, built as a library shared by the app and physics tests.
- Root CMake files define the native build; all outputs live under `build/`.

Next milestone: compare movement/collision against the included SWF and implement
the remaining spin attacks, wall-impact recovery, and slope-dependent sliding. General timeline execution, remaining enemy types, saves, menus, cutscenes,
and the level designer remain future work.

## FLUDD and HUD

Stomp a nozzle box and collect the released nozzle: blue is hover, red is rocket,
and gray is turbo. Space activates it; Shift cycles unlocked nozzles (including
unequipped). Nozzle pickups fill the 10,000-unit water tank; small bottles restore
1,500 units. Hover consumes air power, restored on landing. Rocket needs a full
power meter and a charge before launching; turbo requires full power. Equipment,
water and collected supplies persist across area transitions.

For quick development testing: `./build/dev/sm63 --nozzle hover` (or `rocket`,
`turbo`). This explicitly grants a nozzle; normal starts require finding a box.
A replay with a granted nozzle activates it during ticks 80-124.

Silver Stars can now be collected and counted, but their Shine/cage objective
sequence is still pending. HUD gauges/text and bottle/water effects are native
approximations; the original health, lives, coins, stars and nozzle art is used.
Full FLUDD character animation tracks, swimming/puddle refills and exact dive
nozzle angles are not yet ported.

Star Coins keep their source IDs across area visits. Blue coins count as five
coins, restore three health segments and replenish FLUDD power. This course's
Rainbow Star grants a 45-second Wing Cap: dive while rising quickly to start
flight. Running through a spinning heart while grounded gradually restores
health; the heart is reusable. See COURSE_OBJECTS.md for the remaining fidelity
work and PORTING.md for source/implementation details.

Start Snowman's Land directly with `./build/dev/sm63 --area 2-1`. F3 switches
through the opening tutorial, Battlefield and Snowman, resetting run progress and equipment. Page Up/Down
switches areas within the selected course; Snowman currently has only its opening
area. Unported exits show a notice instead of loading an unrelated map.
Penguins now walk, bob in flight and bump Mario without contact damage. Toads
use the sign dialogue panel, and ice reduces traction. Later areas and mission
scripting remain pending. Its existing coins, enemies, cannon, platforms, signs, FLUDD box
and 30-second Wing Cap use source placements. Copy `area5/` and
`snow-background.png` with the generated assets when relocating the game.

The opening tutorial (`--area tutorial` or `--area 8-E1-1`) includes source signs,
Toad, enemies, coins, ponds and castle-theme music. Read the controls sign with Z
to remove the opening barrier. Its exit connects to `8-E1-1-2`; the next tutorial
section and castle are not yet ported. Up strokes upward in water; Down dives.
The air counter drains underwater and recovers above the surface. Opening
cinematics, animated water effects and the full drowning sequence remain pending.
Wing Cap wings now use the original animation attachment matrices and rotate
with Mario’s body, including mirrored facing and flap poses.
