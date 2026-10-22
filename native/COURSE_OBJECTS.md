# Bob-omb Battlefield object status

All five areas load their static terrain, scenery and source exits. The importer
selects settled gameplay frames (2, 1, 3, 2, 1, zero-based). The following requested
objects now have native gameplay implementations, added in this order:

1. Cannons: automatic entry, aiming with Left/Right, firing with Space.
2. Sling stars: airborne capture, X/Space activation, source launch velocities.
3. Falling logs: rider activation, shaking, falling, fading and respawning.
4. Rotating blocks: source movement/rotation parameters and transformed collision.
5. Touch-activated platforms: movement on contact, disappearance and reset.
6. Rolling balls: imported timeline paths and contact damage.
7. King Bob-omb: dialogue, walking, bomb throws, grabs, jump shockwaves, three-hit
   defeat and the boss Shine. Spin thrown bombs back into him.
8. Star Coins and blue coins: run-wide Star Coin IDs and blue coin healing/power.
9. Rainbow Star: this course's source override grants a 45-second Wing Cap.
   Dive while rising quickly to enter flight; Up/Down pitch, Z exits to a pound.
10. Spinning hearts: running through while grounded spins the heart and gradually
    restores health. Hearts remain available for repeated use.

Existing supported objects include Goombas, Bob-ombs, Koopas, Shy Guys, rideable
Goonies, gold/red coins, boxes, signs, green moving/circular platforms, tipping
logs, all three FLUDD nozzles and boxes, water bottles, Silver Stars and Shines.
Object state and collected rewards persist across area visits during a run.

## Remaining work

| Area | Remaining objects or presentation |
| --- | --- |
| 1-1 | Decorative butterflies |
| 1-2 | Silver Star cage reveal, circular platform decorations |
| 1-3 | Moving clouds, full Wing Cap animation layering |
| 1-4 | Boss arena barriers, exact random AI and camera/tween choreography |
| 1-5 | Castle/completion flow |

Mission variants and scripted spawns are not fully represented. Source camera
controls, outdoor parallax, the secret-area intro, complete sign/pickup tweens
and Flash transitions also remain. The source artwork baker does not fully
reproduce masks or scripted animation. Functional native implementations do not
establish frame-perfect Flash parity; standalone-player comparison is pending.

Shines 2 (sky) and 5 (secret) appear at source positions. Shine 4 unlocks with
eight red coins; Shine 3 with five Silver Stars; Shine 1 after King Bob-omb's
defeat dialogue. Shine collection plays the pickup poses and fanfare, then
resumes in the course. Castle return and persistent saving are deferred.
R starts a fresh run. F2 flight and Page Up/Down remain available for inspection.

The generated `art-report.json` lists symbols excluded from static baking,
including objects rendered by native code; it is not a missing-gameplay list.


Snowman's Land 2-1 is playable with F3 or `--area 2-1`.
F3 starts a fresh run. Its terrain, background/music, penguins, Toad dialogue,
ice friction and existing supported objects are imported. Exits to later areas
and mission variants remain pending. Boss and Star Coin panels now share the sign presentation;
King Bob-omb walks on the actual summit and his shockwaves use original artwork.


The default opening tutorial includes areas 8-E1-1 and 8-E1-1-2, their source
exits, illustrated signs, the controls-sign gate, Toad dialogue, swimming and
Cheep Cheeps. Original castle-theme music continues between these areas.
The following tutorial section, castle, opening cinematic, decorative animated
symbols and full water/drowning presentation remain unported. F3 cycles from
the tutorial to Battlefield to Snowman’s Land.
