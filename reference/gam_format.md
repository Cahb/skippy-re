# `.gam` file format + loader — `game_load_jj_gamefile` @ 0x41cbf0

The game "manifest": lists the levels a game file contains. Confirmed against
`src/AssetParser.cpp` (`LoadGamManifest`) and `old_data/notes.txt`, and matched
to the engine loader in Ghidra.

## Binary format
```
offset  size   meaning
0x00    3      magic: 06 06 06
0x03    1      num_of_levels (N)
0x04    N*256  level table: N entries, 256 bytes each (NUL-padded string),
               each byte OBFUSCATED by +5 on disk (loader subtracts 5)
```
(There is also a **text fallback**: if the first 3 bytes aren't `06 06 06`, the
loader parses the file line-by-line via `fgets`, one level name per line, until a
line beginning with `*`. Line count − 1 = num_of_levels.)

Note the obfuscation delta differs per asset type: `.gam` uses **+5**, savegames
use **+0x37 (55)** (see notes.txt).

## Loader flow (0x41cbf0)
1. `sprintf(path, "%s\\%s.gam", asset_base_dir, name)`  (`asset_base_dir` = DAT_004e01c4)
2. `fopen(path, "r")`  → returns FILE* (EOF tested via `FILE+0xc & 0x10` = _IOEOF)
3. `fread(hdr, 4, 1)`; check `06 06 06`; `hdr[3]` → `num_of_levels`
4. loop to EOF: `fread(byte,1,1)`; `byte -= 5`; store into `level_descriptions` stream
5. `fclose`

## Struct fields recovered (now in found_structs.h)
| offset | field | notes |
|--------|-------|-------|
| `0x3215e` | `char level_descriptions[256][256]` | the level table; 256 bytes/level; fixed 256-level capacity (0x3215e..0x4215e = 0x10000) |
| `0x4215e` | `BYTE num_of_levels` | from .gam byte[3] |
| `0x4215f` | `char gamefile_name[128]` | already named; the loaded game name |

Struct edit was size-preserving (`pad9` 130846 → 65310 + `level_descriptions[256][256]`);
`sizeof` still `0x51790d`, offsets verified exact.

## CRT wrappers named this round (used by all loaders)
`fopen` (0x4507ba), `fread` (0x45158a), `fgets` (0x450743), `fseek` (0x4517b9),
`fclose` (0x4504cb). Data: `asset_base_dir` (0x4e01c4), `sprintf` (0x450655),
`fwrite` (0x4513c7). These unlock every other `game_load_*_gamefiles` loader.

## Next loaders to walk (same pattern → more struct fields)
`game_load_level_by_name` (0x418910), `game_load_savegame_gamefiles` (0x43b4a0),
`game_load_highscore_gamefiles` (0x41ef20), plus theme/model/texture/font loaders
listed in full_dump.c. Each will name fields in level_manager / savegame /
highscore / model structs.
