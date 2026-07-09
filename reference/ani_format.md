# Animations: .ani format + engine index (DEFINITIVE)

`.ani` (e.g. Models/K.ANI, paired with K.mdl = player). Text, one anim per line:
```
Name   FirstFrame   NumFrames   FPS   [Flag]
```
Loader: `game_load_animation_file_gamefiles` (0x401070). It reads each line and
matches Name via **strcmp against a hardcoded engine list**, writing
{FirstFrame, NumFrames, FPS, Flag} into `out[index]` where **index = the engine's
fixed slot order (NOT the .ani file line order)**, 4 dwords per entry.

`current_anim_pos` (skippy_catcher_struct, SkippyCatcherAnim) uses THIS index.

## Engine animation index (ground truth, from the strcmp->slot map)
| idx | name | | idx | name |
|-----|------|-|-----|------|
| 0 | walk_forward     | | 12 | paraglide |
| 1 | walk_backward    | | 13 | slide |
| 2 | speed_forward    | | 14 | idle1 |
| 3 | speed_backward   | | 15 | idle2 |
| 4 | slow_forward     | | 16 | field_stair_up |
| 5 | slow_backward    | | 17 | field_stair_down |
| 6 | celebration      | | 18 | stair_stair_up |
| 7 | jump             | | 19 | stair_stair_down |
| 8 | glue             | | 20 | stair_field_up |
| 9 | ghost            | | 21 | stair_field_down |
| 10 | ice             | | 22 | turn_left |
| 11 | fall            | | 23 | turn_right |

Frame data (FirstFrame/NumFrames/FPS) comes from the .ani file per model; empty
lines = slot uses defaults. Engine plays mdl frames [FirstFrame..+NumFrames) @ FPS.

## Notes / corrections
- The .ani FILE order != engine index order (engine reorders via the strcmp list).
- Corrected found_structs enum SkippyCatcherAnim: was FALL=5/GHOST=10 (wrong);
  now the full 0..23 table above. (Re-parse the ghidra header to sync.)
- tick sets current_anim_pos = 8 (glue) on glue tiles, 9 (ghost) on death, etc.
- Open: user recalled ghost=10 in-game vs exe's 9 — exe strcmp map is authoritative;
  reconcile if it matters.
