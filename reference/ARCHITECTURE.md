# Skippy / Ka'roo — engine architecture (reverse-engineered)

Master overview of `RUS_Karoo.exe` (ImageBase 0x400000, DirectDraw/D3D IM3, ~2001).
Detailed sub-docs referenced inline. All addresses are file VMAs. "verified" = proven
from disasm/cross-refs; "assumed" = inferred, treat with caution.

## The frame model
```
WinMain (0x42d100)
  new(0x51790d) -> game_main_struct_constructor (0x4145c0)   // the ~5MB god object
  init_player_input_controls (0x403940)                       // DirectInput + key bindings
  directdraw_d3d_init (0x412740)                              // DDraw4 + D3D3 exclusive fullscreen
  scene_init_load_assets (0x426000)                           // camera/view/proj, load models/textures/fonts
  loop: PeekMessage -> if !intro: main_render_func()
```
`main_render_func` (0x426f50) is BOTH halves of the frame:
1. **sim** = `game_tick` (0x414df0) — called at top as the per-frame update.
2. **render** = the rest of main_render (skybox, world, entities, shadows, FX, HUD, Flip).
(See claude/main_render_*.{cpp,md} for the render pipeline.)

## game_tick (0x414df0) — per-frame simulation  [verified]
Runs unconditionally each frame regardless of input. Drives:
- **game_state machine** @ god+0x2ab58c: 0=main menu, 1=playing, 2=gameover,
  3=level-complete, 4=intro, 5=paused, 6=highscore-entry, 7=exit.
  ENTER(0xd)/ESC(0x1b) via GetAsyncKeyState transition states.
- global timer (0x170a54 += dt), platform/elevator/teleporter/bomb sub-updates.
- **enemy loop**: per enemy -> pick target (default = player grid pos) ->
  enemy_ai_move_toward_target(...) -> catch when adjacent.
- menu states (0/5) -> update_script_camera (0x418c70) + menu_update (0x418d20).
- level-complete -> next level / game_load_level_by_name / game complete. Frame counter @+0x18.

## Control / input model  [verified]
Two input routes, same physical keyboard:
- **Gameplay**: `init_player_input_controls` (0x403940) sets up a DirectInput
  *action system*: named actions ("John Move Forward"...) -> callbacks on skippy_obj,
  bound to keys (arrows etc). Actions call the **movers**:
  - `skippy_move_forward` 0x41fa90, `skippy_move_backward` 0x41faf0,
    `skippy_turn_left` 0x41fb50, `skippy_turn_right` 0x41fbf0.
  Each sets `buffered_move_command` + `last_move_cmd` then calls `skippy_tick`.
- **Menus**: `menu_update` (0x418d20), called from game_tick in menu states, polls
  `GetAsyncKeyState` directly (UP 0x26/DOWN 0x28/LEFT 0x25/RIGHT 0x27/ENTER 0xd/ESC 0x1b).
  Handles nav, options (volumes/toggles), key-rebinding, level select, highscore entry.
  Render: `draw_menu_screen` (0x42e000, dispatches per-screen by menu id 0x195734).
  Menu manager layout (action table @+0x21b stride 0xff, selected item, item counts) is
  modeled in found_structs.h — see claude/menu_manager.md.

## Movement + entities  [verified; see claude/skippy_tick_dissection.md]
Every entity is a `skippy_catcher_struct` (player = skippy_obj @ god+0x1751c9; enemies
in catchers[]). Both player input and enemy AI converge:
```
buffered_move_command (0x145) → skippy_tick (0x438770) → grid step → set_entity_render_pos_from_grid (0x4429c0) → 3D
```
- **2D grid game, 3D is display only.** Logic pos = grid bytes (catcher +0x31/32/33);
  render pos = floats (+0x25/29/2d), interpolated between cells over `move_interval`.
- skippy_tick: decode command -> delta -> tile-type rules (glue/hole/ladder/teleport/
  lose) -> step; vertical is a real float arc (jump/fall/elevator); horizontal is pure grid.
- Key catcher fields: facing_direction 0x14 (1=TOP/2=RIGHT/3=DOWN/4=LEFT),
  current_anim_pos 0x9a (1 byte!), is_falling 0x120, last_move_cmd 0x125 (1=fwd/2=turnR/3=bwd/4=turnL),
  move_interval 0x132(dbl), buffered_move_command 0x145, last_move_time 0x146(dbl),
  move_turn_state 0x14e. (found_structs.h fixed: was +3 drift from a 4-byte-enum bug.)

## Enemy AI  [verified; see claude/enemy_ai.md]
A* pathfinding. game_tick -> enemy_ai_move_toward_target (0x412240) ->
ai_pathfind_step_toward (0x43a9d0) -> ai_compute_path (0x401c20) -> ai_pathfind_astar
(0x401db0, open/closed lists, squared-Euclidean heuristic, searches backward player->enemy).
Enemies chase every frame even if the player never moves.

## Tile / level model  [verified; see claude/jjm_format.md]
`level_manager_obj` (god+0x2ab58d). Grid = `tiles[100][100]`, cell = 0x7f (127) bytes,
first 4 = {z_pos, type, clip_rule, pickup_type}. Types 1-19 (wood/glue/ladder/hole/
leaf/exit...); clip_rule = walkability; pickup_type = item on cell.

## Asset formats  (all parsers mapped)
- `.gam` level manifest — claude/gam_format.md (magic 06 06 06, 256B/level, -5 obfusc)
- `.jjm` map — claude/jjm_format.md (dims + 4B tiles + trailing fields)
- `.mdl` model + `.ani` anims — claude/mdl_format.md, claude/ani_format.md (A* nvm; anim index from strcmp table)
- `.thm` theme (text) — claude/theme_format.md (objects -> mdl/tga/fx; skybox; HUD)
- savegame/highscore — claude/savegame_highscore_format.md (+55 obfusc)
- DirectDraw/D3D init — claude/directdraw_init.md

## God object  [verified; see claude/struct_layout_FINDINGS.md]
`skippy_main_game_struct`, size 0x51790d (5,339,405 B), ptr @ 0x46c498. Submodule map
(constructor 0x4145c0): cd_manager, tsm, LevelResources(1MB), sound, highscore,
savegame, skippy_obj, menu, jjs, config, level_manager. Layout in include/found_structs.h.

## Docs index
- Labels/renames log: claude/ghidra_labels.md
- Render: main_render_reconstructed.cpp / _FINDINGS.md / _WHITEBOX.md
- Movement: skippy_tick_dissection.md, skippy_tick_movement_rules.md
- AI: enemy_ai.md
- Struct: struct_layout_FINDINGS.md ; formats: {gam,jjm,mdl,ani,theme,savegame_highscore,directdraw}_*.md
- Ghidra MCP setup: ghidra_mcp_setup.md ; disasm/graphs: disasm/, graphs/
