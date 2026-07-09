# Text-Format Coverage Cross-Check

Covered / partial / not-covered for every directive in the game's plain-text asset
formats, enumerated straight from the real files and checked against our parser AND our
consumer code. Generated 2026-07-07 from a 3-way audit (.thm / .leo / .jjs).

- **COVERED** = parsed into the struct AND used at runtime.
- **PARTIAL** = parsed but unused, OR used with a hardcoded/simplified fallback.
- **NOT-COVERED** = not parsed at all.

Binary formats (`.par`, `.jjm`, `.gam`, `.cfg`) are out of scope here. `.txt`/`.ini` are
dev exports / config, not behavior.

---

## Progress (2026-07-07, part 2 — tile/pickup tail, all RE-driven)
Implemented: DestructField 0x0d (collapse/regen), throwers (chase like enemies, harmless on
contact), +1 step-up now refuses (stay put, not fall-death), JumpPad 0x0e (ballistic launch to
clip_rule z), Enemy Factory pickup 100 (periodic spawner, cap 5), Paraglide pickup 5 (deploy on a
big fall -> slow steerable safe descent + parachute mesh), Teleporter 0x0f (clip_rule-paired warp),
Switch 0x11 + Bridges 0x12/0x13 (extend/retract planks 0x14 via plank_z overlay). Also: destruct vs
obstacle sounds split, Teleporter sound, red debug box suppressed for factory tiles.
Switch/bridge/plank tiles use their themed Field textures (Space slot 30 schalter / slot 37
space_bridge); their additive GLOW layers are not yet drawn (theme-glow gap #9). Teleporter tiles
still render as plain slabs (theme Teleporter mesh not wired).
Deferred/notes: enemies don't trigger switches (player-only); teleporter/switch fire instantly (no
RE dwell); paraglide has no mid-glide steering-turn (drifts in the deploy facing).

## Progress (2026-07-07)
DONE: gap #1 (.leo ANI parse bug), #2 (.leo positional ambience — looping distance-mixed playback),
#5 (.leo UNLIT recognised), #6 (.leo Model transparency — alpha props in transparent pass, fixes
butterfly/fish black box), #7 (.leo SPLINE_DYNAMIC closed-loop motion, Catmull-Rom + travel-facing),
#8 (.jjs multi-line captions). Camera fall/death (frozen departure-spot cam) + occlusion 89.9°.
Batch D: .leo/.thm ANI playback (butterflies flap, flags wave, fish/ray swim), SPLINE motion, and
Water-John helmblubber helmet bubbles — all done. #4 partial: DestructStart + ExplosionCatcher SFX
wired (only two of the 17 with a live trigger; rest await their features).
DEFERRED: .leo-object OBB occlusion (beehive) — needs an occluder heuristic to avoid tipping
overhead near every bush/crate.
STILL OPEN: #3 (observe-2 second actor), #9-16 (.thm visual dirs: SrcBlend apply, Billboard glows,
UV anim dirs, Fog, bonus HUD ring icons, Menu*TextColors), the 15 unwired .thm SFX (feature-gated).

## Priority gap list (all formats, worst first)

| # | sev | format | gap | kind |
|---|-----|--------|-----|------|
| 1 | **BUG** | .leo | `Model … ANI <path> …` optional field shifts token positions → 23 animated props (butterflies, castle flags, fish, ray) read a **blend keyword as their texture** (load fails) and `lit` is always true | live rendering bug |
| 2 | HIGH | .leo | `Sound <wav> Y,X,Z` blocks (bee buzz, bird, cuckoo, fountain, wind, door, laugh, statue) parsed as count only — never stored/played | missing feature (faithful) |
| 3 | HIGH | .jjs | `observe 2` (80 shots) frames the kangaroo instead of the intended **second actor** (guide/enemy) — `main.c` only ever passes the kangaroo as `actor_pos` | correctness |
| 4 | HIGH | .thm | 17 parsed-but-unwired `Sound<event>`s (SplatCatcher, FallCatcher/Thrower, MoveThrower, ExplosionCatcher, DestructStart/Regen, Teleporter, Elevator, Platform, Switch, Bridge, Obstacle, …) — data already in the table, just needs load+trigger | missing SFX (cheap) |
| 5 | MED | .leo | `UNLIT` (102 entries) not recognized (only `NOLIT` compared) → treated as lit | correctness (latent) |
| 6 | MED | .leo | `Model` `srcblend`/`destblend` not stored → transparent props (flags, butterflies, fish: SRCALPHA/INVSRCALPHA) render opaque | visual |
| 7 | MED | .leo | `SPLINE_DYNAMIC <ms> <wp>…` (submarine, 2 fish, ray, asteroid) unparsed → 5 moving backdrop objects render frozen | missing motion |
| 8 | MED | .jjs | multi-line `text` bodies overrun — `font2_text` treats embedded `\n` as a space, not a line break | visual |
| 9 | MED | .thm | `SrcBlend`/`DestBlend` parsed but not applied (only `== "one"` additive detected) → non-Exit glow layers (switch, jumppad, ice, teleporter, slide) wrong | visual |
| 10 | MED | .thm | `Billboard { … }` blocks silently skipped → Crystal energy-glow, Time flare, Protection halo dropped | visual |
| 11 | LOW | .thm | animation directives unparsed (`Rotate`,`Oscillate`,`Pump`,`Pulse`,`Flash`,`Scroll`,`Turn`,`Position`,`RandomYAngle`) → approximated with hardcoded constants or absent | visual polish |
| 12 | LOW | .thm | `NoMoveStates` parsed but never read → sea creatures (qualle/krake) may not free-run their swim `.ani` | visual (fish "swim") |
| 13 | LOW | .thm | non-Exit `ParticleSystem`s ignored (incl. Water John's `helmblubber.par` helmet bubbles); trailing `Movable2`/`notMovable` flags dropped | missing FX |
| 14 | LOW | .thm | env cosmetics: `Fog` (active in Water), 5 bonus-timer HUD ring icons, `Edge`, `Pointer`, all `Menu*TextColors`, `HUDTextColors` (parsed, unused) | cosmetic |
| 15 | LOW | .leo | bees `ZERO/ZERO` should be invisible (audio-only); we draw them (user chose black diamonds — deviation, keep) | intentional deviation |
| 16 | LOW | .leo | `WRAP` texture-repeat token unparsed (tower walls, fish); Model rx/ry rotation unused (butterfly tilt) | minor |
| — | DATA | .thm | shipped typo: `Space.thm` → `BombExplosion.wav.wav` (double ext) fails to resolve | upstream data bug |

Needs-RE (unresolved by data alone): `.leo` Sound loop-vs-oneshot; `SPLINE_DYNAMIC` curve
(linear vs Catmull-Rom); `.jjs` `splinexyz N` unit (seconds assumed, correlates with paired
`wait`); what actor index `observe 2` maps to.

---

## .leo — "Level 3D Extra Objects" (33 files, 3 blocks)

Grammar (confirmed complete across all 7 worlds + demo):
```
Model    <mdl> <gridY,gridX,Z> <rx,ry,rz> [ANI <ani>] <src> <dst> <LIT|NOLIT|UNLIT> <tex> [WRAP] [SPLINE_DYNAMIC <ms> <wp>…]
Particle <par> <gridY,gridX,Z> <offX,offY,offZ> <src> <dst> <tex>
Sound    <wav> <gridY,gridX,Z>
```
Blend vocab (both fields): `NONE`,`SRCALPHA`,`INVSRCALPHA`,`ONE`,`ZERO`. Coords = (Y,X,Z)
swap; consumer un-swaps + adds 0.5 (cell centre). Counts: Model 273, Particle 28, Sound 14.

| field | status | note |
|---|---|---|
| Model keyword/mdl/pos/rot.rz(yaw)/tex | COVERED | static mesh, yaw only |
| Model rx/ry | PARTIAL | parsed, unused (butterfly tilt wrong) |
| Model `ANI <path>` | **NOT-COVERED / BUG #1** | 23 entries; also corrupts field alignment |
| Model `WRAP` | NOT-COVERED | tiling lost |
| Model `SPLINE_DYNAMIC` | NOT-COVERED | 5 objects static |
| Model src/destblend | NOT-COVERED | opaque render |
| Model LIT/NOLIT/**UNLIT** | PARTIAL (broken) | `lit` stored, never read; `UNLIT` misread |
| Particle keyword/par/pos/off/src/tex | COVERED | ONE→additive, else alpha |
| Particle destblend | PARTIAL | stored, never read |
| Particle ZERO/ZERO (bees) | PARTIAL | drawn (should be invisible; user deviation) |
| Particle >16/file | PARTIAL | `LEO_FX_MAX`=16 drops 2 of JJ.LEO's 18 fireworks |
| Sound wav/pos | **NOT-COVERED** | only `num_sounds++` |

Ambient Sound catalog: `bienen`(bees,Forest), `Vogel01`(bird), `Kukuk`(cuckoo),
`Springbrunnen`(castle fountain,×4), `door`, `GruselLach`(spooky laugh), `Wind`,
`egypt\statue`. Fountain/bee/wind/bird likely looped positional; door/laugh/statue ambiguous.

Parser: `src/formats/leo.c` (Model 101-112, Particle 80-95, Sound 97-100); struct `leo.h:19-41`;
consumers `main.c` (Model 1955-1965, Particle 919-937/1461-1492, Sound never read, reset 908).

---

## .thm — Theme (8 files)

Object slots: all 38 `SkippyThemeObject` keywords parse; consumption selective. Fully rendered:
John, Catcher, Thrower, Plate, Side, Exit, Glue, Stair, Crystal(mesh), Ice(tex[0]). Bonuses +
Switch/JumpPad/Teleporter/Platform/Elevator/Slide/Obstacle/Bridge + all `*FX` = PARTIAL
(mesh/tex[0] only; glow layers, particles, per-object anims not applied).

Environment: `Sky`/`HUD`/`Radar`/`SideHeight` COVERED. `HUDTextColors` parsed-unused. `Menu`
via hardcoded path (not directive). NOT-COVERED: `Fog` (active in Water!), bonus HUD ring icons
(Freeze/Speed/Slowdwon/Protection/InverseControl), `Edge`, `Pointer`, 24 `Menu*TextColors`.

Mesh sub-directives: `Model`(+ani), `Field` COVERED. `NoMoveStates` parsed-unused.
`ParticleSystem` only Exit's consumed (first path only; flags dropped). `Billboard{}` skipped.

Texture flags: `Condition`, `Wobble`(glue only) COVERED. `SrcBlend` only `=="one"` tested;
`DestBlend`/`NoZWrite`/`NoShadow`/`alpha` parsed-unused. NOT-COVERED: `TextureAdress`,
`Environment`(env-map), `Specular`, `Lit`, `Flash`, `Pulse`, `Turn`, `Scroll`, `Rotate`,
`Oscillate`, `Position`, `RandomYAngle`, `Pump`, `Explode`.

Sound map: 27 events parse into `sounds[]`; **10 wired** (MoveJJ, MoveCatcher, Crystal, SplatJJ,
FallJJ, Glue, MoveIceSliding, BombTick, ExplosionBomb) + reuse. **17 PARTIAL** (parsed, not
loaded) — see gap #4. Undocumented event found: `MoveParaglidingStart` (2 themes).

Parser `src/formats/thm.{h,c}`; consumers `main.c` + `renderer_raylib.c` (sim.c reads none).

---

## .jjs — Instruction scripts (74 files, 14 commands)

Complete command set (no hidden ones; `zehn`/`aber`/`zeitbonus` are German words inside `text`
bodies). `observe` only ever 1 or 2. No `//` comments in any file.

| command | status | note |
|---|---|---|
| distance, setcamposxyz, setcamtargetxyz, gotoxyz, movetoxyz, wait, initwave, fromhere, again, break | COVERED | movetoxyz blocks per-arrival; goto=instant target |
| observe | **PARTIAL** | index recorded; host always passes kangaroo → `observe 2` wrong actor (gap #3) |
| splinexyz | PARTIAL | N treated as SECONDS (correlates w/ paired `wait`); unit needs-RE |
| text | PARTIAL | empty clears OK; multi-line `\n` not wrapped (gap #8); CP1251 |
| playwave | PARTIAL | only fires in INTRO not MENU (harmless — demo has none) |
| anglexyz | dead branch | appears in ZERO files; our handler is speculative |

VM `src/formats/jjs.{c,h}`; consumer `main.c` (load/seed 1157-1173/1240-1245, tick 1284-1306,
camera apply 1674-1678, caption 2135-2141, font2_text 1094-1110).

---

## Camera behavior (fall/death, near-3D-object) — RE COMPLETE (2026-07-07)

Both flow through build_orbit_camera_view @0x404470. cam-mode byte god+0x28ab2d: 0=normal
orbit, 1=void-fall, 2=frozen/dead. Focus point god+0x20a51/55/59 (X/height/Z).

### (A) Fall / death camera — we currently do this WRONG
The original does NOT orbit the death spot. It FREEZES: azimuth locked (no drift), pitch eases
to the plain 60° default, distance glides to 7, and the orbit focus LATCHES on the spot the
player left — the body just falls (or the angel rises) out of frame.
- Void fall (mode 1): fires once dropped > 2.0 grid-units below the jump-arc start
  (const 0x45d3bc=2.0). Focus horizontal = fall column; focus HEIGHT is frozen at last-ground z.
- Caught / crushed / timeout (mode 2): player-state pad7[0] ∈ {1 caught (enemy within 0.5),
  3 timeout}. Focus frozen at last-alive 3D pos; azimuth explicitly fmod-normalized then held.
- Occlusion / breathing / tile-scan all disabled while cam-mode != 0.
OUR BUG: main.c death cam orbits CCW (`death_a0 + death_t*0.6`) around the death tile. FIX:
make it a frozen orbit — hold azimuth, pitch ~60°, dist ~7, look at the latched departure point;
let the angel rise / body fall through the static frame.

### (B) Near-3D-object (beehive) — our occlusion is right but incomplete
It IS a dedicated occlusion-avoidance system (only in normal mode), and it does exactly what we
implemented: snap the pitch target toward ~89.9° top-down (const 0x45d314=1.569051 rad), glided
@ dt*0.005. Two independent triggers:
1. Ray-vs-OBB (FUN_00423240) against the 3D decoration/object list (DAT_0046c45c, built by
   FUN_00420ee0) — THIS is the beehive case. We do NOT test .leo object OBBs → beehive won't
   trigger our tip-overhead. Gap: extend our occlusion raycast to .leo Model bounding boxes.
2. Terrain height-map tile scan — matches our current grid raymarch.
Tweaks: our PIT_OVER is ~85°, original is 89.9°; our ease is expf(-6*dt) vs their dt*0.005.
Neither trigger changes distance or azimuth — pitch only.

### Camera fix list
- FALL/DEATH: replace the CCW death-orbit with a frozen orbit latched on the departure spot (60°/7).
- OCCLUSION: bump overhead target to ~89.9°; add .leo-object-OBB raycast so decorations (beehive)
  trigger the tip-overhead, not just terrain.
