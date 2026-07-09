# `.jjm` level format + loaders

Two functions, confirmed against `AssetParser.cpp::LoadMap` + `notes.txt`.

## `game_load_level_by_name` @ 0x418910 (orchestrator)
1. copies level name into `main_struct+0x173483`
2. builds `"%s\Levels\%s"` (asset_base_dir) → calls `level_manager_load_map_file`
   (fail → log error + `PostQuitMessage`)
3. post-load setup `FUN_00425210`; logs "level (Bonus %d) loaded" (bonus# @ level_manager+0xC)
4. sets `main_struct+0x10 = 1` if the loaded level name differs from the previous
5. builds `"%s\InstructionScripts\%s"` → `jjs_load_script`; success flag @ `main_struct+0x1960e6`

## `level_manager_load_map_file` @ 0x41f190 (the real parser)
`this = level_manager_obj`. Appends `".jjm"`, `fopen(,"rb")`, reads via getc/`_filbuf` (0x451974).

### File layout
```
[0..1]  dims (2 bytes): byte0 -> this+0x19b, byte1 -> this+0x19a
[2..]   tile grid: dim_x * dim_y tiles, 4 bytes each:
          tile[0]=z_pos  tile[1]=type  tile[2]=clip/unkn  tile[3]=pickup_type
        border tiles (row/col 0 and dim-1) are forced to 0 (reserved border,
        matches notes.txt "first 2 rows/columns reserved")
<then, in this order>
        +0x196  crystals_needed   (4)
        +0x192  offs_0192         (4)
        +0x110  world/theme name  (128)
        +0x90   offs_0090         (128)
        +0x10   offs_0010         (128)
        +0x0c   is_bonus/meta     (4)
```

### level_manager_obj offsets — CONFIRMED vs found_structs.h (all match)
| offset | field | note |
|--------|-------|------|
| `0x0c`  | is_bonus_lvl / meta (4) | |
| `0x10`,`0x90`,`0x110` | 128-byte fields; `0x110` = world/theme name | |
| `0x192` | offs_0192 (4) | |
| `0x196` | `crystals_needed` (4) | ✓ |
| `0x19a`/`0x19b` | `level_dim` bytes | see swap note |
| `0x19c` | `tiles[100][100]`, **cell = 0x7f (127 B)**; row stride = 100×127 = **0x319c** | ✓ |
| `0x13628c` | `tiles1[100][100]` = PRISTINE reset copy (`tiles+0x1360f0` = 100×100×127); restored by restore_level_from_pristine on death-retry | ✓ |

**Tile struct = 127 bytes** (confirmed: loader does `ptr += 0x7f` per cell). First 4
bytes = {z_pos, type, clip_rule, pickup_type} (from file); the remaining **123 bytes**
(`pad1[123]`) are runtime state — neighbor/teleport links, moving-platform world pos,
forced-facing, occupied flag, anim cursor (see claude/tile_runtime_layout.md). Tile type
+ pickup value tables are in notes.txt.
**NOTE:** the old "stride 0x319c / 0x3198 runtime" was WRONG — 0x319c is the ROW
stride (100 cells), not the cell size. The loader steps cells by 0x7f and rows by 100 cells.

### One discrepancy to resolve (minor)
found_structs labels `level_dim_x`@0x19a and `level_dim_Y`@0x19b, but the parser
stores file **byte0 → 0x19b** and **byte1 → 0x19a**. By the notes.txt / AssetParser
convention (byte0 = X), that means **x and y are swapped** in found_structs. It's a
label swap only (adjacent bytes, no layout impact). Left as-is pending a gameplay
confirmation of which axis is which.

## Named this round
`level_manager_load_map_file` (0x41f190), `_filbuf` (0x451974), `str_ext_jjm`
(0x4663f4, ".jjm"). Path strings: "%s\Levels\%s", "%s\InstructionScripts\%s".
`game_load_level_by_name` commented as orchestrator.

## Live vs pristine tile sets + death-retry
The .jjm loader fills BOTH `tiles` (live) and `tiles1` (pristine) identically. During
play, only `tiles` mutates (crystals/pickups removed, destructibles destroyed, switches).
On death with lives remaining (`lives @ main+0x175402 > 0`), `game_tick` decrements lives
and calls `restore_level_from_pristine` (0x4184a0) which copies `tiles1 -> tiles` per cell
(z/type/clip). Persistence: collected crystals(pickup 1)/hearts(7) stay collected;
destroyed obstacles (type 0x17) become type 1 permanently in BOTH sets; other pickups
restored. lives < 1 -> game_state 2 (game over -> draw_summary_screen).
