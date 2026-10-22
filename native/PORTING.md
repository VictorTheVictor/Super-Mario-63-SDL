# Area 1-1 milestone: source mapping and fidelity

## What is imported

`tools/import_level.py` reads these original XFL symbols under
`Super_Mario_63_2010/LIBRARY/Course Clips/1 - Bob-omb Battlefield/Areas 1-1 & 1-2/`:

- `1-1 Platforms.xml`: solid collision geometry, including invisible walls.
- `1-1 Bplatforms.xml`: one-way collision geometry.

The importer handles the static single-fill shapes present in these two files.
It converts twips to pixels, decodes signed 24.8 hexadecimal coordinates, flattens
quadratic edges to a 0.1-pixel chord-error tolerance, and rasterizes at pixel
centers using an even-odd fill per shape. Shapes are unioned into solid/one-way
bits. The result covers 2,616 x 589 world pixels as 1,594 generated scanline
spans, including negative coordinates. This is a collision mask, not a conversion
of the visual BackGFX/FrontGFX layers. No hand-designed replacement level is used.

The edge notation is consistent with the original implementation notes in
[SavageFlask's XFL format documentation](https://github.com/SasQ/SavageFlask/blob/master/doc/FLA.txt).
This is a deliberately limited importer: nested collision symbols, transforms,
dynamic platforms, multiple fill styles, and scripted changes must be supported
explicitly before using it for other areas. Unsupported input fails the build.

## Movement provenance

`Super_Mario_63_2010/DOMDocument.xml` is the source of:

| Source | Native behavior |
| --- | --- |
| `StartChar("1-1", 0, 0, ...)` | Initial/respawn position (0, 0) |
| `_root.walkspeed = 1.2` | Ground acceleration per tick, extracted at build time |
| `_root.Mariojumpspeed = 10` | Basic jump impulse, extracted at build time |
| `_root.airwalkspeed = 5`, `_root.airmaxWalkspeed = 20` | Air-control formula, constants extracted at build time |
| `groundFriction(a,b,c)` | Signed subtract-and-divide friction helper |
| Dry-ground branch of `UnpausedCharEngine` | Ground friction 0.3 / 1.15 before input |
| Airborne branch of `UnpausedCharEngine` | Gravity 1, descending drag 0.2 / 1.05, air drag 1.001 |
| `JumpPress = 7` and held `KeyUP()` check | Seven-tick press window, with no auto-jump when held |

## Deliberate approximations

The native solver uses a 16 x 34 pixel body, half-pixel movement substeps,
contact refinement, four-pixel step-up, and six-pixel slope-following. Flash uses
several point/shape probes, rotations, and additional state. These solvers are
not equivalent; visual geometry is rasterized to one-pixel cells rather than
evaluated through Flash `hitTest`. The basic normal-ground movement formulas
have been adapted; the full player engine has not been translated.

The camera is a simple bounded follow camera, now interpolated for presentation.
Leaving the imported bounds
respawns at the start rather than invoking original exits/death transitions.
Double/triple jumps and running are now implemented as described below. Diving,
spinning, wall jumps, moving platforms, powerups, water, hazards, and level
transitions are not present. FLUDD is absent. Long idle/tired sequences are not
implemented; stationary Mario uses the first gameplay walk frame.

## Mario movement and graphics pass

- `Mario (None) Walk.xml` advances its 80-frame track by signed horizontal speed;
  the native controller now follows that progression rather than a fixed-time
  cinema loop. `Mario Main.xml` supplies the parent offsets. Mirroring preserves
  Flash's convention: `_xscale = -100` faces right.
- `Mario Running.xml` supplies the six-tick startup, 1.5x ground acceleration,
  and running bitmap track. The controller recognizes the source's nine-tick
  double-tap window and cancels ground running on release, reversal, or jumping.
  Airborne run termination is simplified; it does not reproduce every attack
  transition and per-timeline event-order detail.
- The `doublejumpcount`/`doublejump` branches provide a nine-ground-tick chain
  window, impulses of 10, 12.5, and 15, and the third jump's horizontal boost
  toward speed 15. The third jump requires horizontal speed above 3. Holding
  the jump button does not retrigger jumps after landing.
- `Mario (None) Air - 2.xml` and `Air - 1.xml` supply rising/falling poses; the
  switch follows the source's `yspeed > 2` condition. The frontflip uses the
  original spin bitmap but a linear rotation around the body center, **not**
  the exact Flash tween/easing. Its 30-tick state duration follows the source.
- `Mario Ground Pound.xml` and `Mario Ground Pound Blur.xml` now supply a
  ten-span animation track, including composed nested transforms and landing
  squash. Z while airborne enters a twelve-tick hover, descent starting at speed
  eight, and fifteen-tick landing recovery. The descent applies the timeline's
  0.5 acceleration, 0.95 horizontal damping, and restricted air control. This is
  an explicit native state machine: Flash's outer-engine/timeline event ordering
  and all interacting gravity/drag effects have not been reproduced. Windup
  rotation is linear; other tween transforms currently hold their keyframes.
  Holding Z does not retrigger the move. Breaking objects, damage, sound, and
  impact effects are not yet implemented.

`import_art.py` now bakes static area artwork from the actual BackGFX/FrontGFX
XML: groups, bitmap instances, fill edges, repeating/clipped bitmap fills, solid
colors, and eligible static course decoration symbols. It preserves back/player/
front display order from `StartChar`. Selection flags are discarded as authoring
metadata. The generated `art-report.json` lists excluded interactive symbols
and vector strokes. Scripts, dynamic transforms, and gameplay objects are not
executed. The sky is a flat fallback color; original parallax backgrounds remain
pending. F1 switches to diagnostic collision geometry.

## Validation so far

Automated tests exercise actual area geometry, including spawn floor height,
basic jump height, no repeated held jump, ground acceleration/friction, walking
up a one-way slope, passing up through and landing on a floating platform,
blocking on a tall wall, high-speed ceiling contact, and out-of-bounds respawn.
Every simulated test step checks that the body is not embedded in solid ground.
All PNGs are uploaded through SDL in the smoke test. A real-window screenshot
has also been inspected on Linux.

Additional checks cover the run double-tap timeout/cancellation, double/triple
jump impulses and expiry, ground-pound suspension/descent/recovery, and pounding
onto a one-way ledge. The smoke replay renders the newly imported character
tracks against the baked artwork. Affine composition and source-track extraction
have importer tests. Debug/Ninja and optimized/Make builds are both checked.

No recorded-input or frame-by-frame comparison against the original SWF has
been performed. Passing these tests means the prototype behaves consistently,
not that it is a faithful full port.

## Dive and wall-contact milestone

Down now starts an airborne dive. `UnpausedCharEngine` supplies the boost toward
facing speed 35 (one fifth of the difference) and +3 vertical impulse.
`Mario (None) Dive.xml` supplies one-eighth air steering, dry slide friction
(0.2, 1.05), the moving diveflip impulse (toward 15, jump speed / 1.5),
and the low-speed reverse backflip (0.75 horizontal damping, toward opposite
30, jump speed + 2). Slow released slides play seven ticks of get-up frames.
Dive actions clear jump chains and cannot overlap ground pound. Native input
uses a fresh Down press to avoid repeated automatic dives.

The importer includes Dive, Diveflip, and Backflip bitmaps with Main transforms.
Backflip rotation is linear; other motion tweens hold keyframes. Source dive
rotation, slope acceleration, ice/water, Wing Cap, and exact event ordering
remain pending. The standing collision rectangle remains in use during dives;
low tunnels and attack contact geometry therefore do not yet match Flash.

Horizontal sweeps now binary-refine contact before applying a half-speed
rebound, reduced by 3 when pressing into the wall. Left/right handling is
symmetric in the native solver. The source has an asymmetric left-wall branch
and a separate high-speed `HitWall`/`hurt2` sequence: that sequence is not yet
ported, so high-speed native impacts currently rebound too. One-way landing
candidates are rejected when the landing body would overlap solid terrain.
Regression tests cover dive entry, weak steering, belly-slide landing,
diveflip/backflip, get-up recovery, and high-speed contact on both enclosure
walls, alongside the earlier terrain tests. Replay tick 150 displays the dive.

## First enemy milestone: Goombas

`import_enemies.py` imports the six Goomba instances from 1-1 Back Graphics and
walking/squish bitmap timelines. Parent registration offsets come from Goomba
(`clip` y=-8) and Goomba Frames (Squish x=.35, y=5.1). The controller uses source
patrol acceleration .5, friction (.3,1.1), 100-tick direction changes, gravity
(1 then *.98), notice-hop speed -5, and dive knockback/power-hit thresholds.
Native terrain sweeps use a 20x25 body and ledge probes. Relative swept player
contacts handle stomps and fast horizontal crossings. Side hits use the source
MarioHit arguments: one damage, 90 invulnerability ticks, -5 vertical knockback,
7 horizontal knockback and seven ticks of input suppression. Health and blinking
are provisional UI; zero health immediately resets this prototype's area.

This is a playable subset, not full enemy parity: eyesight is rectangular,
initial direction is deterministic, walk frames also represent airborne/tumbling
Goombas, tumble rotation and recovery are simplified, and stomp bounce happens
immediately (8 or 10 impulse) rather than through the source EJump/squish staging
and late-button 12 impulse. Dead enemies remain gone until area reset; source
camera-distance respawning, coin drops, water/lava, sound, enemy-to-enemy attacks,
and other enemy species remain pending. Enemies currently simulate offscreen.
Regression tests cover all six spawns over 500 terrain ticks, fast stomps and
side crossings, damage immunity, dive knockback, pound state cleanup and reset.

## Planned Flash comparison

The user supplied `sm63game.swf` at the repository root and installed the Adobe
Flash standalone player through the AUR. Preserve this reference. Later compare
recorded input against the native build for acceleration/braking, jump apex and
hang time, double/triple jump windows, dives, ground pounds, slopes, walls,
one-way landings and enemy contacts. Input mapping parity is a later task too.
No standalone-player physics comparison has been performed in this milestone.

## Coins and breakable boxes

`import_items.py` imports 34 gold/red coin placements and three orange boxes
from area 1-1, original spin bitmap tracks, the Switch Brick box texture, and
instance-script rewards (5, 3, and 30 coins). Gold/red spin cycles are 16/8 ticks.
The source OrangeBlockPL polygon supplies each dynamic solid rectangle:
x=-16..16, y=-31..-4.95 relative to its instance. These solids participate in
player, Goomba, and dropped-coin terrain queries. Removing a box removes only
its dynamic collider, leaving the original static terrain intact.

A falling ground pound (speed >3) or side impact (speed >9) breaks a box.
Horizontal contact coordinates are retained before rebound so the interaction
uses actual swept contact. Ground-pound continuation uses source vy*.75-3;
side impacts currently stop Mario for seven ticks instead of playing HitWall.
Boxes emit their exact reward once; Goombas emit one gold coin on entering the
squished state. Coins use swept collection, source values (gold=1, red=2), and
hpcoincount healing (5 gold or 1 red per health segment). Drops become collectible
after 12 ticks, blink after 9 seconds, and expire after 12 seconds at 32 Hz.
Collection has a short native rise/fade effect; breakage has simple native debris.

Box rewards now use source emission rates (one per tick for 3 coins, two for 5,
six for 30) and source randomized launch ranges. A fixed native PRNG seed keeps
replays reproducible; its sequence does not match Flash Math.random.
Remaining fidelity work: drop terrain responses are swept approximations. Sound,
full fragment/sparkle timelines, box reappearance at camera distance, save-state
persistence, the 100-coin life award, red-coin completion rewards, Star Coins,
blue coins and power-up boxes remain pending. R/death resets this area's items
and counters. The standalone Flash parity checks remain planned as future work,
not completed verification.

Tests cover imported counts/rewards, fast pickups, no duplicate collection,
healing, standing on a box, ordinary contact versus attacks, real swept side
impacts, collider removal, one-time rewards, drop pickup delay/expiry and reset.
Real-window screenshots were inspected for coins and the stacked boxes.

The box regression set also covers contact on the very first ground-pound
descent tick, impact speed captured before wall rebound, staggered small/large
rewards, exact emission totals, and resetting the reward random seed.

## Spin and audio milestone

X starts spin using `KeySPIN` and `Mario Main`'s Spin frame: initial speed 15,
activation only when vy>-3, and airborne vy*.5-5 lift. `Mario (none) Spin Attack`
supplies spin-speed decay, animation phase, air drag/lift, steering, and the Down
modifier. Native input uses a fresh X press; source held-X retriggering and the
alternating-left/right spin/recharge gesture are not implemented. Native dry
physics/timeline ordering remains approximate. Original no-FLUDD spin bitmaps
are imported, including fast/slow tracks. Spin uses a rectangular attack reach;
Goombas receive the source horizontal impulse (vx*.7 +/-20) and -1.5 vertical
impulse, and boxes break without cancelling the spin. Water/cap variants and
precise Flash attack shapes remain pending.

`import_audio.py` resolves linkage IDs against DOMSoundItem. Exact-length PCM
resources are decoded using their source rate/bit-depth/channel metadata.
The two music resources contain MP3 data of `dataLength` bytes, a one-byte cache
marker, and low-rate PCM preview data; only the MP3 payload is decoded. FFmpeg
runs at build time with decoder errors treated as failures and produces 48 kHz
stereo float WAVs. The output manifest records source linkage/resource names.
Runtime uses SDL3 audio streams with no FFmpeg or SDL_mixer dependency.

Music plays the source Bob-omb Battlefield intro once, then repeats its separate
loop track. A bounded 32-voice mixer combines effects with music, clips output,
and feeds a short queue from the main thread. P pauses playback position and
simulation; tools pause audio; M mutes the stream without stopping the music
clock; R restarts music and gameplay. Missing audio hardware logs a warning and
leaves gameplay available. Copy generated/audio alongside the baked art when
relocating a build.

Current effects: jump/double/triple voices, dive, damage, coin collection (source
four-tick jingle throttle), box break, Goomba notice/stomp/death, grass footsteps,
and a provisional ground-pound impact using MarioImpact. Native event timing,
footstep cadence, relative gains and lack of spatial panning are not yet Flash
parity. No dedicated spin whoosh was found in the inspected spin symbol, so no
replacement sound was invented. Full-game music transitions, ambience, voice
variation and unported-action effects remain pending. The Flash standalone
comparison is still outstanding; this milestone does not claim audio parity.

Audio tests load all decoded resources, mix simultaneous effects, validate sample
bounds, preserve paused music position, and cross intro/repeat boundaries multiple
times. SDL smoke uses a dummy audio device; a real-device launch also completed
without audio initialization errors. Spin has movement, enemy, and box regressions.

## Multi-area traversal milestone

`import_world.py` selects the default first frame of areas 1-1, 1-2 and 1-3,
imports solid/one-way geometry, supported objects and sign placements, and
transforms named `warp` rectangles from Blank Movieclip's +/-26.5 x +/-8 shape.
Artwork baking now supports per-area bounds and the source's inconsistent names
(`1-2 Font Graphics`, `1-3 Bplatfomrs`, `1-3 Back GFX`). Runtime terrain bounds,
respawn checks, camera clamping and diagnostic rendering follow the active area.
The camera still uses native bounds, not the source CamEdge scripts.

`areas.hpp` ports the routes among these three areas. StartChar negates its
arguments and places Char relative to course geometry: the last two position
arguments are Mario's local spawn, while the earlier two set camera placement.
Thus returning 1-2 -> 1-1 spawns at x=2100, not the camera's 1800. Upper routes
preserve the source x-offset formulas. Tiny overlapping spawn placements are
resolved upward within 64 pixels for the native AABB. Source feet hitTest is
approximated by a small rectangle, including the ascending-only 1-1 upper exit.
Transitions use a native fade and 24-tick exit cooldown, not the Flash wipes.
Music continues across area changes. A session cache preserves items/enemies;
health and coin/healing counters transfer globally. R clears the session;
death/fall uses the latest entry point and does not clear collected objects.

E reads signs and pauses simulation/audio until closed. Plain sign text comes
from instance scripts; illustrated tutorials use short native text substitutes,
with unimplemented abilities explicitly identified. Original sign artwork is
used, but registration is approximate and picture tutorials are not rendered.
The exit to 1-4 is detected but does not load an unported destination; its message
can be dismissed with E. This milestone does not port the remaining Bob-ombs,
birds, FLUDD, cannon, tipping log, moving platforms or objective machinery.
Default-mission source data is loaded; mission-dependent frames remain pending.

Tests exercise every supported route/exit, spawn clearance, dynamic terrain
bounds, visit-state persistence, fresh-run reset and the upper exit condition.
Real-window snapshots of 1-2 and 1-3 were inspected. These are loading/traversal
checks, not evidence that every route is reachable with currently ported abilities.

## Moving platforms and tipping logs

`import_platforms.py` imports the 12 green platforms/logs in the default frames
of areas 1-1 through 1-3, including per-instance size, direction, acceleration,
speed caps and reversal timers. It extracts original bitmap pieces and their
registration transforms from Green PL Clip and Tipping Log. Platform motion
and rendering run at the existing fixed 32 Hz with render interpolation.

`platforms.hpp` follows Moving Green Platform's timer-based reversals (distance
parameters are ticks), initial offsets and acceleration. Tipping Log supplies
angular friction, rider torque, restoring force, horizontal drift and 80-degree
limits. Native one-way surfaces use GreenPLcode and TippingLog PL dimensions.
They carry Mario before his usual physics step and avoid placing his body in
solid terrain. Platform state is cached with each visited area's items/enemies.
F1 displays the active support lines.

Remaining fidelity work: jump-off velocity transfer/log departure kick, source
activation radius, crush response, wood footstep sounds and carrying enemies or
dropped coins. Angled support uses the highest point under Mario's native foot
width rather than Flash hitTest probes. This is not yet a standalone Flash
side-by-side physics comparison.

Regression checks cover source placement counts, two-axis carrying, reversal,
jump detachment, one-way passage/landing, log torque and settling, ceiling
clearance and area-state persistence. All eight suites pass in Debug/Ninja
and optimized Release/Make. A real SDL replay screenshot confirms original
green-platform artwork renders in the opening area.

## Full-course exploration milestone

The world importer now covers all five Bob-omb Battlefield areas, including
1-4 Ground/Ground 2 (solid/one-way) and 1-5 Ground/Bplatforms. Area 1-5 has no
front symbol, so its generated front image is transparent. Collision import
handles multiple fill styles separately and unions them at runtime. Artwork
baking and platform/object import select frames spanning the source's settled
gameplay timeline, retaining layers held across initialization. This restores
previously omitted exit/object placements. No ActionScript is executed.

Added source routes 1-2 -> 1-4, 1-4 -> 1-2 / 1-5, and 1-5 -> 1-4, with the
original character spawn coordinates. Every imported exit now has a destination.
Area 1-5's intro and music change remain pending. The scenery still uses the
existing flat sky backdrop rather than the source parallax/secret background.

F2 enables explicit development flight, freezes gameplay and bypasses collisions;
Page Up/Down visits areas directly. Resuming inside terrain returns Mario to the
entry checkpoint. `--explore` starts in this mode. The SDL exploration smoke
test switches through all five areas via keyboard events and resumes gameplay.

The eleven Green Circle Platform instances in 1-2 now import their source size,
radius, phase and speed. Orbit follows x=-sin(angle)*radius, y=cos(angle)*radius;
platform artwork stays upright. The existing moving one-way support carries
Mario. Tests cover a complete orbit; decorative dots/arms and source departure
velocity remain pending. See COURSE_OBJECTS.md for the remaining object work.

Validation: all nine CTest suites pass in Debug/Ninja and Release/Make, including
all five areas through the exploration event path. Real SDL screenshots of the
mountain entrance and secret interior were inspected. This verifies loading and
native behavior, not full Flash parity or completion of the course objectives.

## Ground regression, FLUDD and HUD

Foreground symbols can be shorter than their area's BackGFX timeline. Applying
the BackGFX gameplay frame directly made one-frame foregrounds empty. Each
symbol now clamps the requested frame to its own timeline length in artwork,
world-object and platform import. A regression test uses the actual 1-1 and
1-2 foreground clips and checks their selected frames retain elements. Real
SDL screenshots confirm terrain artwork in both areas.

FLUDD ports dry-land branches from DOMDocument's UnpausedCharEngine: hover
Hoverspeed .92, water -7 and power -1.5 per tick; turbo target 75 grounded / 37.5
airborne, approach divisor 30 and water -5 (diving -10). Rocket WaterBlast
supplies the 16-tick charge, 500-water cost, power reset, -25 launch adjusted
for downward velocity, and eight ticks of additional thrust. Ground contact
restores power; non-hover power recharges by one on released Space. Movement
still passes through the existing swept collision solver. Full dive-angle thrust,
swimming/puddles, equipped collision dimensions and FLUDD animation timelines
remain approximations/pending. Backpack art uses original bitmap pieces.

Nozzle boxes import at source placements, bounce Mario on a descending foot
contact, unlock a nozzle, release a collectible and reset after 312 ticks.
Released nozzles have gravity and terrain clearance; collecting equips/refills.
Small bottles add 1,500 water once. Supplies persist through the area cache.
Equipment and fuel survive transitions; death restores water/power. Box break,
nozzle pickup/switch and spray use original audio. Native water particles and
bottle graphics stand in for their Flash effects.

Space now controls FLUDD, Up jumps and Shift cycles unlocked nozzles/none.
`--nozzle hover|rocket|turbo` is an explicit developer grant for testing.
The HUD uses source Stats Clip positions for HP, lives and coins and adds native
water/power gauges. Lives start at four, decrement on death/fall, increment per
100 collected coins, and zero lives pauses with an R-to-restart message.
Silver Star pickups update a persistent count; revealing the Shine/cage remains
pending. The HUD is functional but not a pixel-identical Flash recreation.

Tests cover hover braking/exhaustion, water use, ground recharge, empty tanks,
rocket charge/launch and ceiling collision, turbo thrust, nozzle cycling, box
release/pickup, bottles, transition persistence, and foreground selection.

## Enemy roster, water attacks, signs and artwork registration

The default gameplay timelines now import Bob-ombs (3/5/0/3/0 per area),
Koopas (0/1/7/3/0), Shy Guys (0/0/5/4/0), and Goonies (4/0/16/0/0), in addition
to Goombas. Flying flags and Goonie facing directions come from instance scripts.
Enemy artwork is baked from the original visual clips, not their collision helpers.
Bob-ombs light a 96-tick fuse on noticing Mario, chase, accept knockback and explode;
blast contact costs two health and can knock other enemies away or break boxes.
Original fuse-light and explosion audio is decoded. Koopas lose wings on stomps,
then enter a shell state; shells can be kicked and hit enemies/boxes. Shy Guys
lose flight before being squished. Goonies fly, periodically dive, and carry Mario
on top without contact damage. Area caching preserves these states.

FLUDD water uses native rectangular jet bounds matching the drawn downward or
rearward stream. Source impulse constants are used: vertical -1.5; horizontal
velocity times .9 plus facing*3, or rearward*6 for turbo/diving hover. Sustained
water builds power hits. Turbo body contact at speed >=7 applies dive-like
knockback without contact damage. This is not Flash's exact animated hitTest:
water occlusion, scaled enemies, source random AI timing, detailed shell response,
Goonie camera-bound wrapping and enemy offscreen respawn remain pending. Boss logic
was subsequently added in the course-object milestone below.
Native swept body collision is retained, with a shorter shell body.

Signs preserve picture labels and HTML text instead of native replacement hints.
The original sign artwork, illustrated panels, nested bitmap animation, gradients,
and wrapped text are baked by import_signs.py. Native logic uses source-sized
proximity, no horizontal input/attack, Z-to-read, seven alignment frames, opening,
release-before-Z-to-close and closing. Music continues; world simulation freezes.
Mario uses the original Mario Sign bitmap and registration. Full Flash timeline
scripts, tween easing, masks/filters, font rasterizer parity and mission-specific
picture changes remain pending; this is a source-based recreation, not a SWF VM.
Fontconfig chooses Arial (or its installed substitute) at build time.

Some XFL DOMShape members repeat their enclosing group's matrix. Applying both
shifted the ledge above the 1-1 break-box sign and the upright log beneath the
first red coin by Chain Chomp's gate in 1-2. Raw repeated shape transforms are now
applied once; distinct transforms still compose. The log's corrected world bounds
are x=590.45..608.45, y=4.6..34.35. Collision data is unchanged. Regression tests
check that source placement and distinct-transform handling; real SDL captures
check the repaired ledge/log and illustrated sign panel.

Tests additionally cover sign release gating, water/turbo attacks, Bob-omb fuse
and blast immunity, winged stomps, shell kicks, Goonie landing, enemy blast chains,
and box reward emission under repeated blast frames. Runtime Flash/native input
comparison has not yet been performed.

Validation for this milestone: all 12 CTest suites pass in Debug/Ninja and
Release/Make. A Goonie carry regression also ensures solid terrain blocks the
rider rather than allowing the bird to push Mario into a wall.

## Bob-omb fidelity and presentation corrections

Bomb omb Code calls MarioHit(0,0,0,+/-3,0,"none") on ordinary body contact:
this now pushes Mario without health loss, hurt animation or invulnerability.
Spin still launches the bomb with a power hit; a strong wall/ground impact or
settling after a powered tumble now detonates it before its fuse ends. Regression
tests cover harmless contact, a spun wall impact, a hard landing and a weak bump.
Explosion contact remains a two-health-point attack.

The red region around the Silver Stars was the invisible 1-2 Camera Zoom Out
Clip. Static artwork now omits instances whose source script hides them. Cannon
Platform is explicitly included in the static symbol set: its raised stone
artwork now matches the already-imported collision near the first cannon. Cannon aiming and launching were added in the course-object milestone below. Nozzle box artwork uses the
source bitmap origin (-16,-20), correcting the former 12-pixel upward offset.

Hover, Rocket and Turbo dive/recovery tracks now import the dedicated Mario
Slide (Hover), Mario Rocket Slide and Mario Slide (Turbo) bitmap timelines and
Mario Main parent transforms. These sprites already include the turned FLUDD;
the extra upright backpack is suppressed in these poses. Other equipment poses
still use the earlier simplified overlay; full equipped animation parity remains
pending.

Area 1-5 imports the original SecretLevelBG black-to-#362552 screen gradient and
Secret Course - Intro / Secret Course - Repeat audio. Outdoor area changes retain
music playback. Entering the secret starts its intro, which proceeds to its loop;
returning to area 1-4 starts Bombomb Battlefield Repeat directly, matching its
source music assignment. Area changes clear queued old music. Pause and mute
remain in effect; sound effects continue to mix through the existing system.
Tests exercise outdoor continuity, secret intro-to-loop, repeated same-area
selection, return music and switching while paused. Screenshots verify the sky,
nozzle box, raised cannon platform, equipped slide and secret background.

## Shine pickups, death and equipped recovery poses

World kind 14 imports each area's Shine Sprite Clip and its original starnum.
The default course has one in each area. Native rewards use the source red-coin
(8), Silver Star (5), sky and secret conditions. The boss Shine unlocks after the native King Bob-omb defeat dialogue.
A run-wide Shine mask survives cached area visits and prevents duplicate pickups;
R clears it. Star Coins and Rainbow Stars are separate pickups, implemented in the next milestone.

Sequence freezes enemies, moving platforms, pickups, exits and ordinary controls.
A Shine aligns Mario with the pickup, halves vertical speed, waits for landing,
plays Star Swish then GetStar, holds pickup pose 1 for 71 ticks (2.2 seconds),
then shows pose 2 until the decoded fanfare finishes. All four equipped variants
are baked from the original Mario Main transforms. It resumes in place without
a castle transition or save system. Source scripted light rotation/zoom remains
pending; the source glow artwork is retained.

Health death follows Mario (none) Faint's actual nes branch: Mario Fiant GFX Clip
reacts for 16 ticks, launches with -13 velocity and +0.7 gravity per tick, rotates
15 degrees per tick on descent, fades at tick 47 and loses a life at tick 59.
The front FLUDD artwork is selected per nozzle instead of playing its four
configuration frames as an animation. Pit death uses Mario-Fall and a fade;
checkpoint respawn and the existing game-over screen remain native placeholders.
Camera stays anchored during death, preventing it from chasing the falling sprite.
Music is suspended while scene audio plays and resumes afterward.

Hover, Rocket and Turbo now also use their dedicated diveflip/backflip bitmap
tracks, suppressing the upright overlay throughout recovery. Walking and other
poses still use simplified equipment overlays. CLI --preview-sequence shine|death
works with --nozzle, --position, --replay and --screenshot for visual checks.
Regression coverage includes reward thresholds, boss gating, duplicate/revisit
collection, pickup/landing timing, death motion/fade, and music suspension/resume.


## Course transport, hazards, boss and remaining pickups

`import_objects.py` reads the original cannon/sling placements and parameters,
bowling-ball timeline paths, and King Bob-omb placement. Runtime interaction is
in `course_objects.hpp` and `boss.hpp`. Cannon entry/rise/aim/fire and sling
capture/windup/cooldown use source timings and velocities; transport capture
currently freezes the remaining world simulation. Rolling balls use sampled
source timeline positions and distinguish harmless visual path segments.

`import_platforms.py` now reads falling-log, rotating-block and touch-platform
parameters, including fractional expressions and offsets. Rotating collision
uses a convex outline derived from the source rounded block, transformed each
tick, with native rider displacement and overlap resolution. Shaking is
repeatable native motion. Falling logs reset after fading; touch platforms begin
moving only when ridden and then disappear/reappear. Exact Flash probe collision
and moving-platform crush behavior still need standalone comparison.

King Bob-omb has source dialogue, attack poses, bomb throws, grabs/throws, jump
shockwaves and three bomb-hit health points. Defeat unlocks Shine 1. The native
AI uses a deterministic attack cycle rather than source random selection; full
arena barriers, solid body interaction and source camera/tween choreography are
not yet reproduced. Boss music and hurt/landing sounds use the original audio.

Star Coins preserve source IDs across area visits and show a native pickup
message. Blue coins add five coins, fifteen healing points and fifty FLUDD power.
This course's Rainbow Star overrides generic invincibility with a 45-second Wing
Cap; flight steering follows Mario Flying's source equations. Flying sprites,
wing artwork and Wing Cap intro/loop music are imported; full wing flapping and
equipped flight transforms remain pending. The heart follows Heart Clip1's
running-contact trigger, rotation accumulation, gradual speed decay and healing.
It remains reusable, and standing inside cannot continually retrigger it.

Object artwork is baked from original symbols. Mask/guide helper layers are
excluded rather than fully reproducing Flash clipping; scripted tweens and
some pickup effects remain approximations. Tests cover transport, platform
lifecycles/collision, ball damage, boss attacks/defeat, collectible persistence,
Wing Cap flight and reusable heart healing. These checks verify native behavior,
not complete parity with the standalone Flash player.

Validation: all 14 CTest suites passed in Debug/Ninja and Release/Make on Linux.
Rendered checks covered the cannon, rotating blocks, boss introduction, Wing Cap,
heart and Star Coin message. A visual check caught and fixed an area fade being
held open by the Star Coin popup. Windows builds and standalone Flash comparison
remain unverified.


## Boss movement, dialogue baseline and Snowman's Land opening

King Bob-omb now settles against terrain with the source foot probes. The old
35-pixel-wide body rejection blocked his walking on the actual rounded summit.
His 0.7 acceleration, 0.7 damping and forward ground probe are retained; a test
now requires real horizontal displacement on imported 1-4 geometry. His attack
selection remains deterministic rather than matching Flash random choices.
Shockwave damage probes travel at 5.3125 pixels/tick and last 70 ticks; as in
Flash, they are invisible. The original Ground Shockwaves Front GFX and Back
Graphics provide the expanding visible ring, layered around the boss. Width
and opacity interpolate source keyframe values through 81 ticks; full shape
morphing is still approximated by scaling the original ring artwork.

Boss dialogue and Star Coin messages share the sign panel, font, opening scale
and Z prompt. Boss speech comes from the original script. Star Coin messages use
source collection wording in the requested sign-style presentation, without
claiming unavailable level-designer unlocks or offering persistent save buttons.
The original Flash Star Coin popup actually uses its own save-menu panel.
Text-card words now use a shared ascent/baseline anchor: previously each word's
ink was independently top-aligned, causing capitals/lowercase and descenders to
sit unevenly. This was an importer bug, not intended Flash animation.

Area 2-1 is imported from Snowman's Land's settled first-mission frame, with
static grouped collision, solid/one-way terrain, gradient artwork, original
background/music, and existing supported objects. Group/shape matrices that
repeat an XFL transform are applied once, as in the artwork importer. The Wing
Cap reads its per-instance duration (30 seconds here, 45 in Battlefield).
F3 starts a fresh run in the other course; R restarts the selected course.
Later Snowman areas, penguins, Toads, ice friction and mission scripts remain
pending. Neither the source Flash project nor SWF was changed.

Validation for these corrections: all 14 CTest suites pass in Debug/Ninja and
Release/Make. Importer coverage now includes 16 Python tests, including repeated
group transforms and word baselines. Screenshots checked the Snowman spawn,
boss dialogue, Star Coin panel and a text sign. The course exploration smoke
test exercises F3 in both directions.


## Wing attachments, Snowman objects and opening tutorial

Wing Cap wings now use the active Wing Code instance matrices in each Mario
animation, composed with Mario Main. They use the same body pivot and facing
transform as Mario, plus the original Wing Clip flap poses. Full front/back
wing layering and all Flash tween interpolation remain approximate.

Snowman’s opening imports eight penguins, including the flying and stationary
variants. Source speeds, harmless body bumps, head bounces, spin shoves and
turbo tumbles are represented; idle choices are deterministic rather than random.
Toad’s M1Txt dialogue uses the shared sign panel and original idle artwork.
Ice volumes are separate from solid collision and reduce acceleration/friction.
Toad mood animations and exact running traction still need comparison.

The default start is now 8-E1-1, connected bidirectionally to 8-E1-1-2. The
importer selects settled gameplay frames, ignores commented assignments, and
joins ActionScript newline text expressions. The controls card opens the source
gate persistently for the run. Original illustrated cards, static scenery,
background and Princess Peach’s Castle intro/loop are imported. F3 cycles the
tutorial, Battlefield and Snowman; R restarts the selected course.

Water Code instances become pool volumes. Mario has swim strokes, drag,
FLUDD refill and an air counter; Cheep Cheeps patrol and chase with underwater
steering and out-of-water gravity. These are initial native implementations,
not frame-perfect Flash parity. Dynamic water surfaces/wakes, special drowning
animation, opening cinematic/forced lesson, later tutorial sections and castle
flow are deferred. Unsupported onward exits show a notice.

Validation: all 16 CTest suites passed in Debug/Ninja and Release/Make; the
importer suite has 17 passing Python tests. Final renderer smoke checks passed
after the wing layering correction. Screenshots covered the opening spawn,
controls card and dismissal, underwater Mario/Cheep Cheeps, Snowman penguins,
and Wing Cap jump/dive poses. Standalone Flash side-by-side parity remains
unverified. Neither original Flash source nor SWF was modified.
