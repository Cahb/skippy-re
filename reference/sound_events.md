# Sound events = gameplay-action taxonomy (Rosetta Stone)

Theme files (`Themes/*.thm`) define named sound EVENTS → wav paths, in a fixed order
(identical across all 8 themes). The event NAMES are the authoritative list of gameplay
actions the engine distinguishes — a labeling key for otherwise-obscure code branches.
Parsed by `game_load_theme_gamefiles` (0x40c110); play via `sound_play` (0x442900) /
`sound_stop` (0x4429a0), gated per-effect by `skippy_catcher.pad5[]`.

> RESOLVED: the authoritative event IDs are extracted from `theme_parse_sound_line` (0x4113e0)
> — a lowercased-strcmp dispatch that pushes an ID and jmps to a common creator @0x411c34.
> The IDs are a CATEGORIZED space with reserved gaps (NOT the .thm file order). Enum
> `SkippySoundEvent` in found_structs_ghidra.h. Verified IDs:
> MoveCatcher=0, MoveJJ=1, MoveThrower=2, MoveIceSliding=3, MoveSliding=4, MoveParagliding=5,
> MoveJumpPad=7 (6 reserved) | Teleporter=8, Elevator=9, Platform=10, Switch=11, Glue=12,
> Bridge=13, DestructStart=14, DestructRegen=15, Obstacle=16 | Crystal=30 |
> SplatJJ=40, SplatCatcher=41, SplatThrower=42 | FallJJ=50, FallCatcher=51, FallThrower=52 |
> BombTick=60, ExplosionBomb=70, ExplosionCatcher=71.
> Category bases: Move 0-7, tile/env 8-16, pickup 30, splat-death 40s, fall-death 50s, bomb 60/70s.

## The 26 events (theme-file order)
| # | event | gameplay action | maps to (our model) |
|---|-------|-----------------|---------------------|
| 0 | MoveJJ | player hop/step | skippy_tick move commit (player) |
| 1 | MoveCatcher | catcher-enemy hop | enemy move (catchers[]) |
| 2 | MoveThrower | thrower-enemy move | throwers[256] entity |
| 3 | MoveIceSliding | sliding on ice | tile 0x15 (ice), anim render-id 3 (ice clip) |
| 4 | MoveSliding | plain slide | anim render-id 4 (slide clip) — distinct surface from ice |
| 5 | MoveParagliding | paraglide descent | anim render-id 5 (paraglide clip) |
| 6 | MoveJumpPad | using a jump pad | **tile 0x0e** (ballistic rise to pad1[0x51], jump anim 0xb) |
| 7 | Teleporter | teleport | tile 0x0f (dest pad1[0x4e]/[0x4f]) |
| 8 | Elevator | riding elevator | **tile 0x09** vertical mover (elevator_tick 0x411cb0) — NOT the jump pad |
| 9 | Platform | riding moving platform | tile 0x0a/0x0b (platform_tick 0x43ae00); rider stamps 0x0c |
| 10 | Switch | activating a switch | tile 0x11 (pad1[0x53] group id) |
| 11 | Glue | stuck on glue | tile 0x02 (anim 9=glue) |
| 12 | Bridge | bridge extend/activate | tiles 0x12/0x13 (build_bridge_object 0x419ed0) |
| 13 | DestructStart | destructible begins collapse | tile 0x0d phase 1 (~1500ms; destructible_tick 0x403d40) |
| 14 | DestructRegen | destructible regenerates | tile 0x0d phase 2 (~5000ms) |
| 15 | Obstacle | (usually NONE) | tile 0x17 obstacle |
| 16 | Crystal | collect crystal | pickup_type crystal; crystals_needed |
| 17 | SplatJJ | player squished-death | death variant A (squish) — vs Fall |
| 18 | SplatCatcher | catcher squished | |
| 19 | SplatThrower | thrower squished | |
| 20 | FallJJ | player fell-death | death variant B (fell into hole / off edge); wav=Tarzan.wav |
| 21 | FallCatcher | catcher fell | |
| 22 | FallThrower | thrower fell | |
| 23 | BombTick | bomb fuse ticking | bomb_tick (0x402870); player B-key drops bomb |
| 24 | ExplosionBomb | bomb detonates | |
| 25 | ExplosionCatcher | catcher killed by explosion | |

## What this establishes for the rewrite
- **3 entity classes**: JJ (player), Catcher (chases/catches), Thrower (throws) — each with
  its own Move / Splat / Fall events. Confirms catchers[] + throwers[] arrays.
- **Two death causes**: Splat (squished — by catch/bomb/obstacle) vs Fall (into hole / off edge).
  A rewrite needs both death paths distinct.
- **Two horizontal-slip surfaces**: ice-slide vs plain slide (different friction/anim/sound).
- **Jump pad ≠ elevator ≠ platform** — three separate vertical/carry mechanics, each its own event.
- **Destructibles are 2-phase** (collapse → regenerate), matching the mover timers.
- The Move* events (JJ/Catcher/Thrower + Ice/Slide/Paraglide/JumpPad) show the engine emits a
  per-locomotion-mode sound → each is a distinct movement STATE in skippy_tick.

## Cross-check that strengthens confidence
Anim render-ids 3/4/5 (ice/slide/paraglide) line up with sound events 3/4/5
(MoveIceSliding/MoveSliding/MoveParagliding) — the movement-state numbering is shared.

## DONE: event → ID map (above; SkippySoundEvent enum in found_structs_ghidra.h)
Extracted via local objdump of `theme_parse_sound_line` (0x4113e0) + python scan of the push
immediates before `jmp 0x411c34` — same W/A as the theme object slots (parser too big to decompile).

## Remaining (optional): tie ID → sound_play call site
Each `sound_play`/`sound_stop` call in skippy_tick/bomb_tick uses a Sound object looked up by
one of these IDs. Resolving which ID each call site uses (via the ECX Sound obj / the lookup that
precedes it) would label the obscure branches by action ("this = FallJJ death", "this = MoveJumpPad").
Now trivial to confirm against the ID enum above.
