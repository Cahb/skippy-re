# .jjs instruction-script format (tutorial / menu-demo camera choreography)

`InstructionScripts/<world>/<level>.jjs` (+ top-level `DemoLevelForest.jjs` = the menu
attract-mode demo). Loaded per level by `jjs_load_script` (the jjs_manager submodule — the
~1 MB god-object block we'd left dark). Plain text, `//` comments, `;`-terminated statements,
CRLF. Drives a **scripted camera tour + tutorial voice/text** over a real loaded level.

## Command vocabulary (from grepping all .jjs; counts = frequency)
| cmd | args | meaning |
|-----|------|---------|
| `text` | (next line = message, `;`-terminated) | show tutorial text (Cyrillic/CP1251 in this RU build) |
| `wait` | N | pause N (seconds) |
| `observe` | mode | camera MODE, not an actor id (RE 0x41dbe0/0x418c70/0x404120): 0 = script camera off (gameplay follow resumes), 1 = azimuth parked (static shot), 2 = azimuth free-runs (orbiting shot). Never tracks an actor — the look-at stays whatever gotoxyz/movetoxyz set. Non-0 also ends a running spline |
| `distance` | f | camera follow distance |
| `setcamposxyz` | x,y,z | set camera position |
| `setcamtargetxyz` | x,y,z | set camera look-at target |
| `gotoxyz` | x,y,z | snap camera to xyz |
| `movetoxyz` | x,y,z | move camera to xyz (interpolated) |
| `splinexyz` | steps \n x,y,z | spline the camera over `steps` to xyz |
| `initwave` | path id | preload a voice/sound clip under slot `id` |
| `playwave` | id | play preloaded clip `id` |
| `break;` | — | end of script / section |

## Coordinate convention (VALIDATED)
Script xyz are WORLD coords: **world.X ≈ grid.Y, world.Y ≈ grid.X, world.Z = height (grid z_pos)**
— the same axis remap as `set_entity_render_pos_from_grid(x, z, -y)`. Proof: Forest\Start.jjs
parks the camera on the player with `setcamtargetxyz 10,9,5` / `gotoxyz 10,9,5` / `observe 1`,
and the level's spawn tile (type 3) is at grid (x=9, y=10, z=5) — z exact, x/y swapped. This
independently confirms type3=spawn and the grid↔world mapping.

## Relevance to the rewrite
NOT needed for the Day-1 gameplay slice (it's tutorial/menu polish → M7 "decorators"). But it's
a self-contained, tiny language — a straightforward parser + a camera-command interpreter later.
It also doubles as a **spawn/orientation oracle**: a level's script camera target ≈ its spawn tile.
