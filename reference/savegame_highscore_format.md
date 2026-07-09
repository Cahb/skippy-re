# Savegame + Highscore formats + the score formula

Per-byte obfuscation, but the keys DIFFER (both confirmed at the constructor call
sites, game_main_struct_constructor 0x414886/0x414967):
- **Savegames: stored = actual + 55 (0x37)** — `PUSH 0x37` before 0x43b4a0/0x43b3d0.
- **Highscores: stored = actual + 75 (0x4B, 'K')** — `PUSH 0x4b` before 0x41ef20, and
  write_level_report calls the saver with an explicit `'K'`. (The stock JJ.HSC's 0x4B
  filler bytes = zero bytes + 75; an earlier note claiming 55 for both was wrong —
  the key is a PARAMETER on both loader and saver.)

## Savegame — `game_load_savegame_gamefiles` @ 0x43b4a0
`this = savegame_manager_obj`, args (name, key=55).
- Path per slot: `%s\SavedGames\%s%d.sav` (asset_base_dir, name, index).
- Loops `i < this->num_of_savegames` (`+0x30`, set to 6 at 0x414899); each = 42 (0x2a)
  bytes, deobfuscated. Writer = `game_save_savegame_gamefiles` @ 0x43b3d0 (same key).
- `savegames[]` @ `this+0x31`, stride 42.

**Save struct (42 bytes)** — matches found_structs `skippy_savegame_manager_save_struct`:
| off | field |
|-----|-------|
| 0    | savename[10] |
| 14   | current_lvl |
| 15   | hearts_left |
| 16   | total_score (WORD) |

`savegame_manager`: `+0x30` num_of_savegames, `+0x31` savegames[6]. ✓ confirmed.

## Highscore — load 0x41ef20 / save 0x41efe0, key = 75 ('K')
`this = highscore_manager_obj` (@ main+0x13cdbb), args (name, key).
- Path: `%s\Highscores\%s.hsc`; the game uses `jj.hsc`.
- `(num_records) * 55` bytes; **num_records byte @ `this+0x3705`** = main+0x1404c0,
  set to **10** at boot (0x41496c).
- **Record layout (55 = 0x37 bytes) — RESOLVED** via draw_highscore_table 0x434f90
  (columns: name | level | score) and write_level_report's default-record seeding:

| off | field |
|-----|-------|
| 0    | name (NUL-terminated, up to 50 bytes; default seed "Bernie Boulder") |
| 50   | score (DWORD le) |
| 54   | level (BYTE, 1-based) |

- Defaults when no file: `highscore_set_defaults(10000, 1000, num_levels, 7)`
  @ 0x41ee30 (scores presumably 10000 stepping down by 1000), then saved with 'K'.

## Score formula — `game_calc_summary_scores` @ 0x41a760 (renamed from FUN_0041a760)
Fills the LEVEL-COMPLETE summary block (main+0x140502..0x14053e) drawn by the
state-3 overlay; matches the scoreboard screenshot rows exactly. `mode` param =
game_state; the time row only scores for mode 3 (level complete).

| row | points | count shown | source |
|-----|--------|-------------|--------|
| crystals          | min(collected, needed) × **5**  | min       | collected @+0x175406, needed @+0x2ab723 |
| extra crystals    | max(collected − needed, 0) × **10** | surplus | same |
| destroyed enemies | byte @+0x4224d × **50**         | the byte  | pad15+4; zeroed per level in skippy_game_start_level |
| time left         | secs_left × **2** (mode 3 only) | secs_left | limit @+0x2ab591 − elapsed_ms @+0x2ab595 / 1000 |
| Sisyphus bonus    | total_pickups × **5**, gated    | total     | WORD @+0x42250 = SUM of every collectible on the level (crystals + all bonus pickups, computed in skippy_game_start_level); awarded IFF WORD @+0x1753e3 (collected tally) ≥ total AND byte @+0x4220b clear — i.e. a **100%-collection bonus** |
| vitality          | byte @+0x170a64 × **1**         | the byte  | UNRESOLVED: zeroed at boot; candidates = hearts collected (the heart item texture is ENERGY.TGA) vs lives left. Screenshot shows 0 while alive → hearts-collected favoured |
| level score       | sum of the six                  |           | @+0x140536 |
| total score       | running_total += level score    |           | running total @+0x1753f5, copy @+0x14053a |

Open threads: the Sisyphus disqualifier byte @+0x4220b; vitality identity; the
game-over block @+0x1404c1.. (draw_summary_screen, state 2) is zeroed here and
accumulated elsewhere — its rows mirror the same multipliers for the whole run.

## GUI retype (to get field names in these two)
- `game_load_savegame_gamefiles` → Custom Storage → `this` → `skippy_savegame_manager_struct *`
- `game_load_highscore_gamefiles` → Custom Storage → `this` → `skippy_highscore_manager_struct *`
