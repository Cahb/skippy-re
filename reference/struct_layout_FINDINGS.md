# God-object layout — verified against the constructor (ground truth)

`skippy_main_game_struct` (`game_main_struct_instance`, global ptr @ `0x46c498`).

## Ground truth sources (from the binary, not IDA)
- **Total size:** the caller at `0x42d215` does `push 0x51790d; call 0x450e9d`
  (operator new) then stores the result to `0x46c498`. So the engine allocates
  **`0x51790d` = 5,339,405 bytes**. That is the exact `sizeof`.
- **Member offsets:** the constructor `0x4145c0` (thiscall, `this`→`ebp`)
  initializes each sub-object via `lea ecx,[ebp+offset]; call <subctor>`. Those
  offsets are authoritative. Map:

| offset | sub-constructor | subsystem |
|--------|-----------------|-----------|
| `0x13cba8` | `0x4430e0` | sound_manager_obj |
| `0x13cdbb` | `0x41edf0` | highscore_manager_obj |
| `0x1751c9` | `0x41f900` | skippy_obj (player) |
| `0x175518` | `0x41eb70` | menu_manager_obj |
| `0x195735` | `0x41d5e0` | JJS_Manager |
| `0x28ab2e` | `0x41d390` | ConfigManager |
| `0x2ab58d` | `0x41f140` | level_manager_obj |

(Disasm saved: `claude/disasm/struct_constructor_4145c0.asm`.)

## What was wrong in `include/found_structs.h`
Measured with `gcc -m32 -c` + `__builtin_offsetof` (32-bit, header is already
`#pragma pack(1)`):

| anchor | was | should be | delta |
|--------|-----|-----------|-------|
| sound_manager_obj | `0x13cba8` | `0x13cba8` | 0 ✓ |
| highscore_manager_obj | `0x13cdbb` | `0x13cdbb` | 0 ✓ |
| skippy_obj | `0x1751c9` | `0x1751c9` | 0 ✓ |
| menu_manager_obj | `0x17551b` | `0x175518` | **+3** |
| JJS (`offs_195735`) | `0x195738` | `0x195735` | **+3** |
| Config (`offs_28ab2e`) | `0x28ab31` | `0x28ab2e` | **+3** |
| level_manager_obj | `0x2ab590` | `0x2ab58d` | **+3** |
| **total** | `0x517912` (5,339,410) | `0x51790d` (5,339,405) | **+5** |

Two independent over-counts:
1. **+3** introduced between `skippy_obj` and `menu_manager_obj` (carried through
   menu/JJS/config/level).
2. **+2** more inside `skippy_level_manager_struct` (its `sizeof` was 2 too big),
   pushing the total to +5.

## Fix applied (pads only — no shared sub-struct touched)
- `skippy_main_game_struct::pad30`  `[236] → [233]`  (removes the +3)
- `skippy_level_manager_struct::pad4` `[4] → [2]`     (removes the +2)

Deliberately **not** touched: `skippy_catcher_struct` (shared by `skippy_obj`
*and* every enemy in `catchers[]`) — the +3 was absorbed in main-struct filler
to avoid shifting enemy layouts on an unproven guess.

## After fix — all anchors exact
```
TOTAL  = 0x51790d   SKIPPY = 0x1751c9   MENU  = 0x175518
JJS    = 0x195735   CONFIG = 0x28ab2e   LEVEL = 0x2ab58d
```

## Re-verify any time
```sh
sed 's@#include <cstdint>@typedef unsigned char uint8_t;typedef unsigned short uint16_t;typedef unsigned int uint32_t;@' \
  include/found_structs.h > /tmp/fs.h
printf '#include "/tmp/fs.h"\ntemplate<int N>struct S;S<(int)sizeof(skippy_main_game_struct)> s;\n' > /tmp/v.cpp
gcc -m32 -nostdinc -c /tmp/v.cpp -o /dev/null 2>&1 | grep -oE 'S<[0-9]+>'   # expect S<5339405>
```

## Caveat
Only the **constructor-anchored** subsystem offsets + the total are proven. The
internal `offs_XXXXXX` byte-labels (e.g. `offs_17531d`) are hand-placed and some
are mislabeled (that one is ~272 off its name) — they're not ground truth, just
bookmarks. Sub-struct *internal* layouts (player, level, catcher members) are
not individually verified here; only their placement and the god-object total.
