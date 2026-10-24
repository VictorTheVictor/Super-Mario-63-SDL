# Super Mario 63 → C++/SDL2 port plan

## Goal

Run Super Mario 63 (2010 source, ActionScript 2, Flash Player 8, 450×300 @ 32 fps)
natively in C++ with SDL2 as the only platform dependency. The game logic should read
as close to the original ActionScript as possible and behave the same way.

## Ground rules

- **Runtime dependencies:** SDL2 plus vendored, single-file, public-domain C code only
  (minimp3 for MP3 audio). Everything else (inflate, JPEG, rasterizer, filters,
  script runtime) is our own code, so console ports only need a framebuffer, audio
  callback and input.
- **No game assets in this repository.** The runtime reads art, sounds and timelines
  from the user's own copy of `Super_Mario_63_2010.swf` (from runouw/Super-Mario-63).
  Generated script code comes from runouw's released AS2 source.
- **Ruffle is the behavioural reference** for SWF/AVM1 semantics (timeline gotos,
  event order, AS2 type coercions, filters).

## Architecture

```
Super_Mario_63_2010.swf ──► flash::Movie (SWF parser: shapes, bitmaps, sprites,
                                          sounds, fonts, exports)
                                │
                                ▼
                          flash::MovieClip display list + timelines
                                │            ▲
                                ▼            │ calls / property access
                          flash::Raster      game/*.cpp  (transpiled AS2 → C++)
                          (software, 450×300)     ▲
                                │                 │ tools/as2cpp (offline)
                                ▼                 │
                          SDL2 window/audio/input   XFL source (DOMDocument.xml, LIBRARY/*.xml)
```

### Script translation (tools/as2cpp)

1. Parse every `<script>` in the XFL (frame scripts and `onClipEvent` handlers).
2. Map each XFL symbol/frame/instance to the SWF sprite id/frame/depth it compiled to,
   by walking both trees from the main timeline (exported symbols anchor by linkage
   name) and verifying the strings in the SWF's AVM1 bytecode appear in the source.
3. Emit C++ using a dynamic `as::Value` type with operator overloads, so
   `_root.gravity = _root[a + "Gravity"];` becomes
   `_root["gravity"] = _root[a + "Gravity"];`. Comments are kept.
4. After the game runs, the generated C++ is committed and becomes the maintained source.

## Milestones

1. **Timeline player** — SWF loader (own inflate), shapes, lossless bitmaps,
   sprites, PlaceObject/RemoveObject, goto semantics, software rasterizer with
   anti-aliasing, solid/gradient/bitmap fills, strokes, color transforms. Debug viewer
   for any exported symbol. *(done — see status below)*
2. **Remaining visuals** — morph shapes, static/dynamic text with embedded fonts,
   JPEG (DefineBits + JPEGTables) decoder, filters (AdjustColor/ColorMatrix, Glow,
   Blur, Bevel, DropShadow), blend modes (invert, difference, add, layer), masks,
   buttons, `_quality` → anti-aliasing levels. *(done except `_quality`, which needs the
   script runtime — see status)*
3. **AS2 runtime** — `as::Value`, objects/prototypes, functions & closures, scope
   chain (incl. `with`/`tellTarget`), MovieClip API (`attachMovie`, `hitTest`,
   `gotoAndStop`, `swapDepths`, drawing API…), `Key`, `Math`, `Array`, `String`,
   `Sound`, `Color`, `SharedObject` (save files), `ContextMenu`, `Stage`.
   Event order matching AVM1 (enterFrame, frame scripts, load/unload). *(done — see status)*
4. **Transpiler** — XFL↔SWF mapping, AS2 parser, C++ emitter, registration tables.
   *(done — see status)*
5. **Audio** — event sounds, stream sounds, `Sound` objects (volume/pan),
   minimp3, ADPCM. *(done — see status)*
6. **Playable** — boot to title, play courses, compare against Ruffle. *(in progress)*
7. **Ownership & cleanup** — freeze generated C++, hand-tidy, port hooks for consoles.

## Status

- **Milestone 1 done.** The SWF loads in ~0.5 s and every count matches an independent scan:
  6,270 shapes, 2,322 sprites, 1,236 bitmaps, 117,653 placements, 2,617 frame scripts.
- **Milestone 2 done.**
  - JPEG: own baseline decoder with libjpeg-style chroma upsampling; all 18 game JPEGs
    decode within ±3 levels of libjpeg (`jpeg_check` + Pillow comparison).
  - Morph shapes: all 234 pair start/end edges; ratio 0 and 65535 reproduce the exact
    start/end geometry (`swf_check`).
  - Text: 12 embedded fonts (352 glyphs), 280 static texts, 201 text fields with word wrap,
    alignment, and an HTML subset (`p`, `br`, `font`, `b`, `i`, entities). 9 of 4,630 initial
    characters are not in the field's default font (probably HTML runs in other fonts — check
    once text is visible in-game).
  - Filters (Glow, DropShadow, Bevel, Blur, ColorMatrix) and blend modes (all 14) via
    offscreen layers; the object's color transform applies after filtering. Masks via
    clipDepth. Visual check: `sm63 --selftest out.bmp` writes `out.bmp.fx.bmp`.
  - Buttons: Up state only; Over/Down/hit testing arrive with mouse input in milestone 3.
- **Milestone 3 done.** `src/as/`: `Value` (AVM1 conversions, operators, number formatting),
  objects/prototypes/functions/closures, arrays, scope chain with `with`/`tellTarget`,
  mark-and-sweep GC between frames, built-ins (Math, String, Array, Number, Key, Mouse,
  Stage, System, SharedObject saved to disk, intervals, LoadVars/XML/ContextMenu stubs),
  MovieClip/TextField/Button objects with magic properties and the MovieClip API
  (attachMovie, depths, hitTest with real shape geometry, getBounds, drawing API...),
  Color, Sound (API only, silent until milestone 5), and the Player frame loop (execution
  list newest-first, enterFrame then timeline, action queue, input and button events,
  text-field variable bindings). Tests: `as_test` (119 checks), `stage_test` (49 checks
  against the game SWF). Without ported scripts, 244 distinct frame scripts trigger in the
  first 200 frames.
  - Script registry for the transpiler: `registerFrameScript(spriteId, actionIndex, fn)`,
    `registerClipEvent(spriteId, placeIndex, events, fn)`, `registerButtonAction(...)`.
  - Known gaps: `on(keyPress ...)`, `addProperty`/`watch`, `arguments.callee`, typing into
    input text fields, `startDrag`, per-target shared Sound volume, case-insensitive
    `_X`-style property names. Event order follows Ruffle's AVM1 model but has not been
    compared side by side yet.
- **Milestone 5 done.** `src/audio/` (no platform code): streaming decoders — MP3 via vendored
  minimp3 (`third_party/minimp3`, CC0, commit ea99364), SWF ADPCM and PCM written here — and a
  mixer (44.1 kHz stereo, linear resampling, loops, in/out points, envelopes, Flash 2×2 sound
  transforms, volume groups). `src/as/Audio.*` connects `Sound` objects (per-target volume,
  `onSoundComplete`, `position`/`duration`), timeline `StartSound` (stop / no-multiple) and
  stream sounds that follow their clip. SDL output in `main.cpp` (`--mute` to disable).
  `audio_test`: 33 checks; all 182 game sounds decode to their declared length (2,285 s of
  audio in ~1.1 s). Not yet listened to side by side with Flash; stream sounds restart on
  jumps rather than resynchronising like Flash's sync mode.
- **Milestone 4 done.** `tools/as2cpp/` (Python 3 stdlib): `xfl.py` (library and
  timelines), `swf.py` (sprites, placements, AVM1 strings), `mapper.py` (pairs 2,289 symbols
  with SWF sprites by anchors + placement matching; maps 2,604/2,617 DoActions, 5,718/5,741
  clip-action placements, 6/7 buttons; every match checked against bytecode strings),
  `parser.py` (all 8,328 scripts parse), `emit.py` (C++ against `src/game/Prelude.h`).
  Output: 329 files, ~147k lines, 0 translation errors, compiles cleanly. Unmapped leftovers
  are listed in `src/game/generated/REPORT.txt` and show up at runtime as missing scripts.
  Flash quirks handled: `break` outside a loop compiles to a jump to the end of the
  function (verified in the bytecode), `arguments.callee`, `__constructor__`, `_url` is the
  SWF's real file URL (as Flash Player reports it), trace() silent unless `--trace`.
  **Result:** the game boots on its own — preloader (coin counter reaches 100 at ~frame 400),
  intro sequence, then the title screen, whose scripts build a demo course behind it
  (frame ~2400, 2 s headless). No unported script fired on the way.
- **Milestone 6 (in progress).** Headless tooling in `sm63`: `--input` (scripted keys and
  mouse with hover/click), `--sample N`, `--dump`/`--watch`/`--call`, `--diagnose`
  (calls to non-functions and reads on undefined, by name), `--bench`/`--bench-from N`,
  plus gdb hooks `sm63_debug_watch(paths)` and `sm63_debug_hittest(path, x, y)`.
  - The full new-game flow works with scripted input: title -> file select -> file A ->
    start -> intro (skippable) -> first tutorial course (`8-E1-1`). In the course the
    character dismisses the sign (Z), walks both ways (arrows) and jumps (Up/A/S/D).
    Saves are written through SharedObject.
  - Fixed on the way: hit tests through a zero-scale transform returned true for every
    point (Flash: no hit). The course starts at `coursescale` 0 for one frame, so the
    character's push-out-of-ground loop never ended. Regression check in `stage_test`.
  - Fixed a heap-corruption crash (reported when opening Options): when scan conversion
    re-derived an edge's x on rows above the buffer, rounding could give e.g. -1.7e-13,
    and floor() then wrote one cell before the row (before the buffer on row 0). Each row's
    x is now clamped to the buffer (measured: 1,201 rows per Options session, all
    overshoots below 2e-13 px). Also hardened: non-finite/huge geometry is dropped or
    clamped before rasterizing, layers with non-finite bounds are skipped, and transform,
    frame and depth setters ignore NaN/Infinity. Regression checks in `stage_test`.
  - Fixed from play-testing reports:
    - "undefined" in the stage banner and dialogs: AS2's global `newline` was missing
      (used 69 times).
    - Water too tall / swimming in the air: shape hit tests ignored masks. The water clips'
      tall translucent rectangles are masked by the scripted wave shape; masked content now
      only hits where its mask does (also used for mouse picking).
    - Credits text cut off: Flash's `<sbr>` soft-break tag was ignored, merging lines.
      All 49 HTML fields use only p/font/b/i/a/sbr, all handled now.
    - Bowser airship flicker (very likely): 10,170 fills in 2,484 shapes reference
      bitmap 65535 ("no bitmap", exported along broken-apart bitmap edges). They were drawn
      as debug magenta; they now draw nothing, as in Flash. Zero magenta pixels in the
      cutscene now; not yet confirmed visually.
    - Not bugs: `mergeColor()` is never defined in the source; `OEF`/`OEF2`/`enterframe`/
      `CloseFunction`/`Move` are optional per-instance hooks (e.g. Toad: `if (OEF ==
      undefined) OEF = null`) and are no-ops in Flash too when an instance has none.
  - New headless tools: `--course NAME@F` (switch part via the game's `changecourse`),
    `--goto LABEL@F`, `--tree PATH`, `--probe PATH@X,Y`, `--track-char IDS`; text checks in
    `--dump` (overflowing fields) and `--diagnose` (fields showing "undefined"); helper
    programs `charinfo` and `missing_bitmaps`.
  - Timing: course frames 1.5 ms scripts + 17 ms render (p95 18 ms) — within the 31 ms
    budget. The title screen renders at 33 ms average (max 66 ms): needs the filter/layer
    caching below.
  - Open leads from `--diagnose` (not yet known whether they are bugs or game behaviour):
    `setVolume`/`setPan` called on movie clips; `<undefined>.hitTest()` and `.charClip`
    reads on undefined (title demo and course); undefined functions `OEF`, `OEF2`,
    `CloseFunction`, `Move`, `enterframe`, `mergeColor` called every frame.
- Not yet verified against Ruffle frame-by-frame; filter maths (box-blur radius, bevel
  offsets, glow strength) are approximations to tune during side-by-side comparison.
- Performance: the heaviest single symbol (full-screen water) takes ~34 ms per frame, just
  over the 31 ms budget at 32 fps. Filtered objects are re-filtered every frame. Planned:
  cache filtered layers while the object's content and transform scale are unchanged
  (Flash did this too), cache flattened paths, and skip clips outside the clip rect.
