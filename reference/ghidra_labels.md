# Ghidra DB labels applied (via MCP)

Names pushed into the Ghidra project for `RUS_Karoo.exe`. Re-apply from here if
the DB is ever rebuilt. All confirmed live (names show up in decompiler output).

## Functions
| address | name | basis |
|---------|------|-------|
| `0x426f50` | `main_render_func` | per-frame pipeline (verified) |
| `0x4145c0` | `god_object_ctor` | new(0x51790d) + member sub-ctors (verified) |
| `0x426000` | `scene_init_load_assets` | builds camera/view/proj/lights, loads models+textures+fonts by literal path |
| `0x426c50` | `load_level_assets` | called from render gate |
| `0x406480` | `D3DMATRIX_Construct` | 16 floats -> 64-byte D3DMATRIX (verified) |
| `0x42cd60` | `make_vertex_d3dlvertex` | xyz + 5 dwords -> 32-byte vertex (verified) |
| `0x4095f0` | `draw_dynamic_3d_object` | per-object draw workhorse, 13x calls |
| `0x40aff0` | `draw_object_wrapper` | enemy/object draw wrapper |
| `0x408870` / `0x408920` | `draw_object_wrapper_a/b` | thin mesh-draw wrappers |
| `0x43cc00` | `render_skybox` | 6-face cube, Z off (verified; see comment in DB) |
| `0x43b790` | `draw_shadows` | shadow-quad pass |
| `0x4381d0` | `draw_attached_subpart` | attached sub-mesh |
| `0x420f50` | `fx_rotating_billboard` | 2pi/pi-2 angular anim, 2 textures |
| `0x408710` | `fx_quad_billboard` | single textured quad |
| `0x408280` | `fx_billboard_oriented` | quad + orientation matrices |
| `0x408a00` | `fx_overlay_large` | 11 renderstates + 1 draw |
| `0x404040` | `clock_seconds` | frame timer |
| `0x404120` / `0x404300` | `compute_gameplay_camera` / `compute_enemy_render_params` | player follow-cam / enemy render transforms (NOT sim tick — earlier update_player/update_enemies names were WRONG; DB already corrected) |
| `0x403770` / `0x403830` | `vec_cross` / `vec_cross2` | cross product (verified) |
| `0x4037e0` / `0x403810` | `vec_dot_len2` / `vec_dot` | dot/len2 (verified) |
| `0x407f70` | `vec_normalize` | normalize (verified) |
| `0x437bc0` | `load_model` | args "models\John.mdl", "models\Enemy.mdl" |
| `0x43f770` | `load_texture` | args ".../textures/shadow.tga", "karoo128.tga" |
| `0x413520` | `create_font` | args "fonts\font1.fon","numbers.fon"; fail->PostQuitMessage |
| `0x43de40` | `load_bitmap` | args "bitmaps\loading.bmp","demo.bmp" |
| `0x441b10` | `log_message` | (logger_obj, severity, msg string) — called everywhere |
| `0x450655` | `build_asset_path` | sprintf-style "%s\textures\..." path builder |
| sub-ctors | `sound_mgr_ctor 0x4430e0`, `highscore_mgr_ctor 0x41edf0`, `player_ctor 0x41f900`, `menu_mgr_ctor 0x41eb70`, `jjs_mgr_ctor 0x41d5e0`, `config_mgr_ctor 0x41d390`, `level_mgr_ctor 0x41f140` | from god_object_ctor offsets (verified) |
| overlays | `draw_menu_screen 0x42e000`, `draw_summary_screen 0x435420`, `draw_highscore_table 0x434f90` | state-gated screens (all VERIFIED; earlier overlay_* names were wrong) |
| sound | `snd_listener_begin 0x4453b0`, `snd_listener_orient 0x4453e0`, `snd_listener_commit 0x4453a0` | 3D audio listener |

## Data labels
| address | name |
|---------|------|
| `0x46c498` | `game_main_struct_instance` (the ~5MB god object ptr) |
| `0x46c4a0` / `0x46c4a4` / `0x46c4a8` | `camera_pos_x` / `_y` / `_z` |
| `0x46c4ac` / `0x46c4b0` / `0x46c4b4` | `camera_target_x` / `_y` / `_z` (NOTE: tentative — `0x46c4ac` is a copy of camera_pos_x; refine later) |
| `0x46c4c0` | `logger_obj` |

## Decompiler comments
- `0x426f50` — full 9-phase pipeline summary + SetRenderState-52 artifact warning.
- `0x43cc00` — 6-face cube skybox, texture order LF/UP/DN/FR/RT/BK, Z toggle.

## New findings from `scene_init_load_assets` (0x426000)
- Camera position computed from `game_main_struct_instance + 0x2ab727/0x2ab728`
  (bytes) scaled by `.rdata` constants — i.e. derived from level data.
- Same LookAt basis as the scripted camera (vec_cross/normalize/dot), then a
  projection matrix from FOV angle `_DAT_0045d470` (fcos/fsin).
- Loads fixed assets once: `models\John.mdl`, `models\Enemy.mdl`,
  `textures\shadow.tga`, `textures\karoo128.tga`, fonts `font1.fon`/`numbers.fon`.
- Confirms render-states `0x35-0x3b` (53-59) are texture/sampler states (set here
  with the same small-int values as main_render_func).

---

## Submodule map — derived from game_main_struct_constructor (0x4145c0)

The constructor calls each submodule's constructor on `this + offset`. Those
offsets are the authoritative *start* of each submodule's slice in
`skippy_main_game_struct`; the gap to the next start ≈ that submodule's span.
(Cross-checked against the IDA hand-named `game_main_struct_constructor`; call
order is identical.)

| start offset | submodule | constructor | approx span to next |
|--------------|-----------|-------------|---------------------|
| 0x00001c | (shared base subobj) | menu_manager_constructor (0x41eb70) | — |
| 0x02223f | cd_manager_obj        | cd_manager_constructor (0x403000) | ~0x20019 |
| 0x042258 | tsm_obj               | tsm_constructor (0x440280) | ~0x6940 |
| 0x048b98 | (unnamed @48b98)      | sub_423560 | ~0xF4010 (~1 MB — the big one) |
| 0x13cba8 | sound_manager_obj     | sound_manager_constructor (0x4430e0) | ~0x204 |
| 0x13cdac | (small obj)           | sub_420950 (size ~0xF) | ~0xF |
| 0x13cdbb | highscore_manager_obj | highscore_manager_constructor (0x41edf0) | ~0x33c92 |
| 0x170a6d | (small obj)           | sub_420950 | ~0xF |
| 0x170a7c | savegame_manager_obj  | savegame_manager_constructor (0x43b390) | ~0x474d |
| 0x1751c9 | skippy_obj (player)   | skippy_constructor (0x41f900) | ~0x34f |
| 0x175518 | menu_manager_obj      | menu_manager_constructor (0x41eb70) | ~0x2021d |
| 0x195735 | jjs_manager_obj       | jjs_manager_constructor (0x41d5e0) | ~0xf53f9 |
| 0x28ab2e | config_manager_obj    | config_manager_constructor (0x41d390) | ~0x20a5f |
| 0x2ab58d | level_manager_obj     | level_manager_constructor (0x41f140) | ~0x26c380 (to end 0x51790d) |

NOTE: these spans revise some sizes in docs/mappings/subsystems.md (e.g. TSM_obj
was listed 407521 but its ctor-to-next span is only ~0x6940 ≈ 27 KB; the ~1 MB
block at 0x48b98 belongs to the still-unnamed sub_423560 object).

## Init/load functions named this round (from constructor body)
sprintf (0x450655, corrected from build_asset_path), cd_manager_init (0x403040),
cdm_check_original_cd_in_drive (0x403420), cdm_cdaudio_stop_audio (0x402d50),
game_load_jj_gamefile (0x41cbf0), game_load/save_savegame_gamefiles (0x43b4a0/0x43b3d0),
savegame_manager_set_defaults (0x43b560), config_manager_load_cfg (0x41d3e0),
config_manager_set_defaults (0x41d520), game_load/save_highscore_gamefiles (0x41ef20/0x41efe0),
highscore_set_defaults (0x41ee30), game_load_level_by_name (0x418910),
skippy_game_start_level (0x416420). Data: log_file_obj (0x46c4c0).
Constructor renames updated to friendly *_constructor form (was *_ctor).

## Vtable pointers (renamed from PTR_FUN_0045d*)
Each submodule constructor stores its vtable as member 0 (`*this = &..._vtable`).
Pulled from the `mov [reg],0x45dXXX` at each ctor's entry.

| vtable addr | label |
|-------------|-------|
| 0x45d3b8 | main_game_struct_vtable |
| 0x45d2c4 | cd_manager_vtable |
| 0x45d724 | tsm_vtable |
| 0x45d414 | config_manager_vtable |
| 0x45d418 | jjs_manager_vtable |
| 0x45d41c | menu_manager_vtable |
| 0x45d420 | highscore_manager_vtable |
| 0x45d424 | level_manager_vtable |
| 0x45d428 | skippy_vtable |
| 0x45d6f4 | savegame_manager_vtable |
| 0x45d450 | obj_423560_vtable (the ~1 MB unnamed submodule) |
| 0x45d444 | obj_420950_vtable (small obj, constructed twice) |

NOTE: sound_manager has NO vtable at member 0 — its constructor builds sub-objects
(DirectSound wrappers at +0x14, +0x94, ...) each with their own vtables instead.
The vtables themselves (arrays of method ptrs at these addrs) are the next thing
to walk if we want to name each submodule's methods.

---

## Cross-check vs docs/mappings + identity fixes (this round)
Naming convention: **C-style snake_case** (no `Class::Method`). Old-doc names
translated on import.

- **obj_423560 identified = LevelResources** (your `LevelResources_obj`, the ~1 MB
  resource pool). Renamed: `level_resources_constructor` (0x423560),
  `level_resources_vtable` (0x45d450). New: `level_resources_load_level` (0x420c50).
- `jjs_load_script` (0x41d720) — loads/parses .jjs scripts.
- **Vtable cross-validation:** every vtable we found today matches docs/mappings/vtables.md
  (CDManager 0x45d2c4, Highscore 0x45d420, Menu 0x45d41c, Skippy 0x45d428, LevelResources 0x45d450). Good.
- **Naming discrepancy to note:** config.md calls 0x28ab2e "LargeDataBuffer" and
  0x41d390 "LargeDataBuffer::init". But game_main_struct_constructor loads
  `Karoo.cfg` into it via `config_manager_load_cfg` — so it's the **config
  manager**; the old "LargeDataBuffer" label looks like a superseded early guess.
- TODO: catcher base ctor (`skippy_animal_struct_constructor` in docs =
  CatcherComponent::init) — address not yet resolved; it's the first call inside
  skippy_constructor (0x41f900). Grab from there when convenient.

## DirectDraw/D3D init (recovered this round)
`directdraw_d3d_init` (0x412740), `report_init_error` (0x412f80), `fwrite` (0x4513c7).
Full sequence + graphics-wrapper struct field map: see claude/directdraw_init.md.

## WinMain + top level (0x42d100)
`WinMain` (0x42d100), `wnd_proc` (0x42ce20, class "Karoo"), `log_open` (0x4418b0, "JJ.log"),
`d3d_interface_constructor` (0x412680, new 0x238), `log_message_ex` (0x441d20, file/line variant).
Data: `direct3d_interface_obj` (0x4e04ac, the 0x238 D3D wrapper), `intro_playing_flag` (0x4dc7c0),
`asset_base_dir` = GetCurrentDirectory.
Loop: PeekMessage -> WM_QUIT exits, else dispatch; if !intro_playing_flag -> main_render_func().
**No separate game_tick** — sim runs in main_render_func's timing block (update_player/update_enemies).
Still-unnamed top-level init: FUN_00403940 (input?), FUN_004431f0 (sound init?), FUN_00412680 done,
FUN_0044f490/FUN_0044f5a0 (intro AVI via Quartz), FUN_00403fa0, FUN_00402ca0.
Loaders documented: claude/{gam,jjm,savegame_highscore}_format.md.

## Session state / resume point
- Tile grid remodeled in found_structs.h: cell = 0x7f (127B) {z_pos,type,clip_rule,pickup_type,pad}, tiles[100][100] + tiles1[100][100] (size-preserving; sizeof main struct still 0x51790d). clip_rule = walkability (sticky/fall-through/clipped-invis/clipped-vis).
- PENDING: re-Parse claude/found_structs_ghidra.h into Ghidra to sync the tile remodel (Ghidra still has old 0x319c row-struct).
- Movement model recovered: WinMain(0x42d100) loop -> main_render_func (sim+render) ; skippy_tick(0x438770, __fastcall catcher*) = grid movement (reads last_move_cmd/facing_direction, checks tiles[x][y], steps grid pos, sets anim, calls set_entity_render_pos_from_grid(0x4429c0) = grid->3D bridge). compute_gameplay_camera(0x404120) = follow cam (NOT movement).
- Next-session targets for an SDL vertical slice: (1) skippy_tick tile-type->behavior rules (helpers FUN_0041f8a0/FUN_00442900 unnamed); (2) grid->quad render path (set_entity_render_pos_from_grid + how cells become quads).
- Format docs: claude/{gam,jjm,mdl,theme,savegame_highscore,directdraw_init}_format/*.md. Catcher fields verified: facing@0x14, render pos floats@0x25/29/2d, grid bytes@0x31/32/33, is_falling@0x120, last_move_cmd@0x125.

## Sim / control / AI / menu (later sessions) — see ARCHITECTURE.md
| addr | name | verified? |
|------|------|-----------|
| 0x414df0 | game_tick (per-frame sim; was thought to be MainWindow_EventsHandle) | ✓ |
| 0x403940 | init_player_input_controls (DirectInput action bindings) | ✓ |
| 0x41fa90 | skippy_move_forward | ✓ |
| 0x41faf0 | skippy_move_backward | ✓ |
| 0x41fb50 | skippy_turn_left | ✓ (L/R inferred) |
| 0x41fbf0 | skippy_turn_right | ✓ (L/R inferred) |
| 0x438770 | skippy_tick (grid movement/physics tick) | ✓ |
| 0x4429c0 | set_entity_render_pos_from_grid | ✓ |
| 0x41f8a0 | tile_type_is_ladder | ✓ |
| 0x43ad40 | rotate_direction | ✓ |
| 0x404120 | compute_gameplay_camera (was mis-guessed update_player) | ✓ |
| 0x404300 | compute_enemy_render_params (was mis-guessed update_enemies) | ✓ |
| 0x412240 | enemy_ai_move_toward_target | ✓ |
| 0x43a9d0 | ai_pathfind_step_toward | ✓ |
| 0x401c20 | ai_compute_path (guard wrapper) | ✓ |
| 0x401db0 | ai_pathfind_astar (A* search) | ✓ |
| 0x401ec0 | astar_pop_best_node | ✓ |
| 0x401ef0 | astar_expand_neighbors | ✓ |
| 0x401cb0 | grid_cell_id | ✓ |
| 0x401cd0 | is_cell_walkable | ✓ |
| 0x402870 | bomb_tick (fwd move + 3x3 explosion) | ✓ |
| 0x418d20 | menu_update (menu input/logic, GetAsyncKeyState) | ✓ |
| 0x42e000 | draw_menu_screen (was mis-named overlay_stats) | ✓ |
| 0x434f90 | draw_highscore_table (was mis-named overlay_pause_menu) | ✓ |
| 0x418c70 | update_script_camera (JJS scripted camera) | ✓ |
| 0x4507ff | calloc | ✓ |

NAMING CAVEATS (assumed, NOT verified — from main_render draw-gate guesses):
- draw_summary_screen (0x435420) — VERIFIED: end-of-level score summary (item counts x point multipliers). Was assumed overlay_endgame.
- Earlier draw-gate guesses proved ~1/3 right; verify by decompiling before trusting.
Per-menu-screen draws (dispatched by draw_menu_screen): FUN_0042e190(main),
0042e710, 0042e500, 00436550, 00431a60, 0042e9f0, 00430600, 00433dc0, 0042e880 — unnamed.
| 0x4184a0 | restore_level_from_pristine (tiles1 -> tiles on death-retry) | verified |

## Level report + menu internals + anti-piracy (latest) — see ARCHITECTURE.md, antipiracy_token_crystal.md
| addr | name | note |
|------|------|------|
| 0x41a280 | game_first_frame_init | one-time init (config save + SFX load); 'L' key -> write_level_report |
| 0x41b980 | write_level_report | DEBUG oracle: LevelReport.txt; column->offset map validates found_structs num_of_*_on_lvl |
| 0x451776 | fputs | write string to file |
| 0x416420 | skippy_game_start_level | level init + grid scan (fills num_of_* counts) + ANTI-PIRACY token-crystal removal |
| 0x41b810 | find_cell_of_type | grid search for a cell (used by AI seek + token-crystal find) |
| 0x4184a0 | restore_level_from_pristine | tiles1 -> tiles on death-retry |
| 0x418d20 | menu_update | menu input/logic (GetAsyncKeyState) |
| 0x42e000 | draw_menu_screen | menu render dispatcher (by menu_screen_id) |
| 0x41ec00 | menu_go_back | pop menu nav stack; reveals menu_manager layout (item table @+0x21c, NOT a TGA buffer) |
| 0x418c70 | update_script_camera | JJS scripted camera |

## found_structs.h fields added/fixed (latest)
- skippy_catcher_struct: current_anim_pos BYTE (was 4-byte enum bug, +3 drift fixed); +move_interval@0x132, last_move_time@0x146, move_turn_state@0x14e, buffered_move_command@0x145; SkippyCatcherAnim enum = full 0..23 engine order.
- skippy_level_manager_struct: tiles[100][100] + tiles1[100][100] (cell 0x7f); tiles=live, tiles1=pristine.
- skippy_main_game_struct: level_descriptions[256][256]@0x3215e; menu_screen_id@0x195734.
- menu_manager (pad31 region): [menu_id*0xff+item] action table @+0x21c (~128K), per-menu selection @+0x1e, nav stack @+0x2001d, menu_screen_id @+0x2021c. NOT a texture buffer (menu visuals = MENU_1..4.tga surfaces).

## menu_manager modeled + offsets corrected vs decompile (latest) — see claude/menu_manager.md
Re-decompiled menu_update(0x418d20); the approximate offsets above were off by a byte or two.
VERIFIED, now real fields in found_structs.h (abs / pad31-rel), size 0x51790d preserved, game_state@0x2ab58c unshifted:
- menu_confirmed_id  0x175520 (+0x07, WORD) — ENTER fires only when == menu_screen_id
- menu_selected_item 0x175535 (+0x1c, BYTE) — single scalar, NOT a +0x1e array
- menu_item_count_by_id[0xff] 0x175635 (+0x11c) — per-menu item count (up/down gate)
- menu_action_table[0x20000]  0x175734 (+0x21b) — action = table[menu_id*0xff + selected_item], stride 0xff
Action-code + per-screen switch (rebind 0x14-0x20, savegame slots 200+, level-complete 0x29, options) documented in claude/menu_manager.md.
Two-header rule reaffirmed: edit include/found_structs.h -> apply identical struct-body edit to claude/found_structs_ghidra.h (kept in sync; diff = only #include/#ifdef/enum->typedef transforms).

## Sim deep-dive: movers + tiles + animation (latest) — see claude/gameplay_mechanics.md
APPLIED (verified only, user-approved):
| addr | name | basis |
|------|------|-------|
| 0x401970 | anim_renderid_to_clip | self-decompiled switch: current_anim_pos(render-id)->clip-table offset |
| 0x411cb0 | elevator_tick | type 0x09 vertical mover; count@0x173b19, array@0x173719 |
| 0x43ae00 | platform_tick | type 0x0a/0x0b horiz mover; count@0x173718, array@0x173588 |
| 0x417b90 | register_elevator_cell | grid-scan register (type 0x09) |
| 0x417e20 | register_platform_cell | grid-scan register (type 0x0a/0x0b) |
| 0x437b40 | draw_mesh_frame | DrawPrimitive(TRIANGLELIST, FVF 0x212, anim_buf+frame*stride, num_verts) |

CANDIDATES (NOT applied — single-source, verify before renaming):
| 0x403d40 | destructible_tick? | type 0x0d two-phase collapse/regen |
| 0x43ec50 | slider_hazard_tick? | type 0x14; runtime spawner unknown |
| 0x411c50 / 0x43adb0 / 0x403ce0 | elevator_ctor? / platform_ctor? / destructible_ctor? | |
| 0x418240 | register_destructible_cell? | |
| 0x419ed0 | build_bridge_object? | switch(0x11)->bridge(0x12/0x13); pad1[0x54..56] |
| 0x4095f0 | draw_dynamic_3d_object? | frame math + world matrix per submesh |
| 0x437b80 | draw_mesh_frame_hint? | b40 variant (DrawPrimitive flags 0x18) |

REJECTED agent relabels (keep existing, per prior correction):
- 0x404120 stays compute_gameplay_camera (agent proposed update_player_position)
- 0x404300 stays compute_enemy_render_params (agent proposed update_enemies)

KEY CORRECTIONS this pass:
- Teleport dest = cell.pad1[0x4e]/[0x4f] (NOT 0x23/0x24 — those are moving-platform live grid pos). Fix stale skippy_tick_movement_rules.md too.
- current_anim_pos stores RENDER-IDs (walk=0x14, ice=3, glue=9, jump=0xb ...), not the .ani slot ordinal (anim_renderid_to_clip 0x401970 maps id->slot).
- SkippyCatcherAnim enum VERIFIED CORRECT (7 jump,8 glue,9 ghost,10 ice,11 fall) by reading loader strcmp target strings from binary — the earlier agent "slots 7-13 mislabeled" claim was WRONG (misread 6 non-monotonic string addrs). NO struct change needed. Ground truth: .ani file only lists names; loader (0x401070) enumerates via strcmp->offset.
- OPEN: tile type 0x0e game-role (jump-pad vs elevator vs hole) unresolved; mechanic = ballistic rise to pad1[0x51].

## skippy_tick comment refresh (latest)
- 0x438770 skippy_tick: replaced stale "no float physics" comment with full verified summary
  (horizontal grid interp + vertical float physics constants + tile handlers + current_anim_pos=render-id).
- Re-Parse found_structs_ghidra.h into Ghidra to auto-name tile-cell fields (telep_dest_x, platform_world_*, etc.) in skippy_tick + mover decompiles.
- OPEN (not done): map skippy_catcher_struct runtime pads (pad3/4/5/6/8/9b/10b = jump/fall/arc/ride timers+flags) into the struct like we did the tile cell — inference-heavy, defer / user-driven.

## skippy_catcher_struct runtime fields carved (latest) — found_structs_ghidra.h
Verified solid set named (size 0x161 preserved, compile-asserted, anchors is_falling@0x120/move_interval@0x132/size intact):
| abs off | field |
|---------|-------|
| +0x5c | fall_velocity (float) |
| +0xe9 | bounce_count |
| +0xea | airborne_flag |
| +0x108 | stored_move_dir |
| +0x111 | arc_start_z |
| +0x112 | ride_start_time (double) |
| +0x11a | ride_active_flag (dword) |
| +0x13f/40/41 | move_dx/dy/dz |
| +0x152 | entity_mode (9=ghost/dead) |
Left as pad (INFERRED, pre-remodel, unverified): ladder flag, lose-tile timer (pad6~0x21-24), pad3 move-time doubles, pad5[] fx-enable flags, pad8[] AI/glue.
Fixed stale skippy_tick_dissection.md field table (old pad6[0xc]/pad9[0x16] indices were pre-remodel).
Re-Parse found_structs_ghidra.h into Ghidra -> skippy_tick shows this->fall_velocity/bounce_count/move_dz etc.

## Field-width fixes: dword flags mis-modeled as BYTE+pad (latest)
The `x=1; pad[0]=0; pad[1]=0; pad[2]=0;` decompile pattern = a single dword store split by a too-narrow field. Widened (verified by access width, size 0x161 preserved):
- is_falling @0x120: BYTE -> DWORD (ASM: MOV dword ptr [ESI+0x120],EDI). Absorbed old pad8[0..2].
- airborne_flag @0xea: BYTE -> DWORD (read as *(int*)(pad6+0x10)). Absorbed 3 of pad6b.
Rule for future: if a field is read via *(int*) or written with a dword MOV, model it DWORD — don't leave it BYTE+pad (creates the phantom pad-zeroing lines). bounce_count/stored_move_dir/arc_start_z/entity_mode/move_d* stay BYTE (genuine byte access).

## Bulk dword-noise cleanup in skippy_catcher_struct (latest)
Whole pad regions were dword-access blocks rendered as char[] -> decompile showed 4-byte-zero clusters. Converted to DWORD arrays (size 0x161 preserved, compile-asserted):
- pad3a: char[36] -> DWORD[9] (@0x38) — kills clusters @0x40(x2),0x58
- pad4:  char[54] -> char pad4a[2]@0x64 + DWORD pad4b[13]@0x66 — kills @0x6e,0x7a(x4),0x82-0x8e (grid is +2 off pad base)
- pad5:  char[61] -> DWORD pad5[15]@0x9b + char pad5x[1]@0xd7 — the per-effect ENABLE/state flags; kills @0x9b..0xd7 (14 dwords)
- pad9a: char[12] -> DWORD[3]@0x126 — kills @0x126(x3)
LEFT as-is: pad6b @0xfb cluster (dword grid misaligned/mixed there; convert per-field only if needed).
Method: extracted every dword-store/`*(int*)` site from the fresh decompile, mapped padX[k]->abs offset via current layout, converted dword-dominant pads to DWORD[]. Decompile now shows this->pad5[i]=0 instead of 4 byte writes.

## sound_play + pad5 sound-enable block (latest)
- 0x442900 sound_play(this=Sound obj, flags): IDirectSoundBuffer::Play(0,0,flags) via vtable+0x30 on this->buf(+0x10); flags 0=once/1=loop; DSERR_BUFFERLOST restore+retry (0x442820). Verified via strings "Playing Sound failed"/"Soundpuffer of Sound '%s'".
- skippy_catcher_struct.pad5[15] @0x9b = per-effect SOUND-ENABLE flags (config toggles). pad5[N]!=0 gates effect N: [3,5,6,7,8,9,12]->sound_play, [4,10,11]->0x4429a0, [13]->0x442d90/0x442df0. pad5[0]=non-sound init flag. (23 guard sites in skippy_tick.)
- CANDIDATES (sound fn family, not named): 0x4429a0, 0x442d90, 0x442df0 (play/stop variants); 0x442820 restore-lost-buffer.
- 0x4429a0 sound_stop(this=Sound obj): IDirectSoundBuffer::Stop() via buf(+0x10) vtable+0x48. Counterpart to sound_play. (pad5[4,10,11] gate it.)

## Sound-event taxonomy (Rosetta Stone) — see claude/sound_events.md
Themes/*.thm define 26 named gameplay-sound events (identical order all themes), parsed by game_load_theme_gamefiles(0x40c110). Names = authoritative gameplay-action list. Confirms: 3 entity classes (JJ/Catcher/Thrower), 2 death types (Splat=squished vs Fall=fell), 2 slip surfaces (IceSliding vs Sliding) + Paragliding, JumpPad(tile 0x0e) != Elevator(tile 0x09) != Platform, Switch/Glue/Bridge/Crystal/Destruct(2-phase)/Bomb(tick->explode). Anim render-ids 3/4/5 align with events 3/4/5 (ice/slide/paraglide).
PENDING linkage: theme parser event->slot strcmp map; then tie each sound_play call site (ECX Sound obj) to its event to label obscure branches.

## Theme/level load flow (load_level_assets 0x426c50) — see theme_format.md
- Theme loaded lazily: strcmp world vs &skippy_theme_manager_obj (its first field=loaded theme name/cache key); only re-parses via game_load_theme_gamefiles(0x40c110) on world change.
- game_load_theme_gamefiles is HUGE (~5k decompiled lines, flat strcmp parser) -> times out over MCP, not worth dissecting; .thm files ARE the schema.
- CONFIRMED: the ~1MB unnamed submodule @god+0x48b98 = "LevelResources", populated by level_resources_load_level(0x420d38). (Was sub_423560/obj_423560_vtable in submodule map.)
- Per-world loading screen: bitmaps\<world>.bmp via load_bitmap+FUN_00425fc0.

## Theme object-slot map EXTRACTED (workaround for 5k-line parser) — see claude/theme_object_slots.md
game_load_theme_gamefiles (0x40c110) too big to decompile (times out). W/A: objdump -d the func range locally + python scan of the strcmp dispatch (mov eax,[theme_mgr]; add eax,0x104+idx*0x2ef0 after each lowercased object-name strcmp). Result: theme_mgr has 38 object-theme blocks, each 0x2ef0 bytes, @+0x104.
Slot map: 0 john,1 catcher,2 catcherfx,3 thrower,4 throwerfx,5 plate,6 side,7 platform,8 paraglide,9 paraglidefx,10 elevator,11 exit,12 glue,13 destructfield,14 destructfieldfx,15 jumppad,16 slide,17 stair,18 teleporter,19 crystal,20 crystalfx,21 ammunition,22 bomb,23 explosion,24 surprise,25 freeze,26 speed,27 speedfx,28 collfx,29 life,30 switch,31 time,32 ice,33 obstacle,34 obstaclefx,35 protection,36 protectionfx,37 bridge.
Enemies=slots1-4 (matches prior draw-site note). CORRECTS old guess paraglide=themes[36] -> actually slot 8. All 38 verified (37 by literal, bomb by structure; bomb keyword localized CP1251).
Also named: 0x4505b7 strlwr_token (was mistaken for allocator; it's _strlwr, lowercases tokens pre-strcmp). sound_stop 0x4429a0.

## Sound-event ID map + enums (latest) — see sound_events.md, theme_object_slots.md
- 0x4113e0 theme_parse_sound_line: parses .thm "Sound <event> <wav>"; strlwr+strcmp dispatch -> pushes ID, jmp common creator 0x411c34. Extracted via objdump+python (same W/A as theme objects).
- SkippySoundEvent IDs (categorized, gaps reserved): MoveCatcher0/MoveJJ1/MoveThrower2/IceSliding3/Sliding4/Paragliding5/JumpPad7 | Teleporter8/Elevator9/Platform10/Switch11/Glue12/Bridge13/DestructStart14/DestructRegen15/Obstacle16 | Crystal30 | SplatJJ40/Catcher41/Thrower42 | FallJJ50/Catcher51/Thrower52 | BombTick60/ExplosionBomb70/ExplosionCatcher71.
- Added to found_structs_ghidra.h: enum SkippyThemeObject (38 slots) + enum SkippySoundEvent. Compile-checked (sizes intact).

## theme_object block fully mapped -> structs (latest) — found_structs_ghidra.h, theme_object_slots.md
Read-only agent decoded the parser's sub-directive stores (objdump); I verified key offsets vs exe. Added structs (compile-asserted): theme_ani_clip(0x10), theme_texture(0x3C), theme_submesh(0x5DD), theme_object(0x2ef0=submesh[8]+8 tail).
- theme_submesh: type@8, model_handle@0xc, explode@0xb9, anim_clips[24]@0xc9 (== anim system cfg+0xc9), billboard/ps@0x249, position/scale/rotate@0x3a5, texture_count@0x3c9, textures[8]@0x3cd, lit@0x5ad/nomovestates@0x5b1(==cfg+0x5b1)/nozwrite/noshadow/specular/randomyangle, oscillate@0x5c9, pump@0x5d5.
- theme_texture(0x3C): condition@0(1act/2inact/3dead/4alive/5paraglide), tga_handle@4, src_blend@0xc, dest_blend@0x10, texaddr@0x14, fx_anim_type@0x18(flash/pulse/turn/wobble/environment/scroll), fx_param[3]@0x1c.
- Caveats baked as comments: submesh_count @obj+4 aliases submesh[0] head; Pump[2..3] overflow into next slot head (benign). Texture entries: 8/submesh, stride 0x3C @submesh+0x3cd, count @+0x3c9.
Named earlier this thread: theme_parse_sound_line(0x4113e0), strlwr_token(0x4505b7), sound_stop(0x4429a0), sound_play(0x442900).

## Theme structs CONSOLIDATED into existing referenced structs (correction)
The new theme_object/theme_submesh I added were DUPLICATES of pre-existing referenced structs. Folded the verified fields into the real ones instead:
- skippy_theme_manager_theme_cfg_struct (= a sub-mesh): fleshed out; size CORRECTED 0x5DC -> 0x5DD (off-by-one; parser ASM proves submesh_base = obj+idx*0x5DD via `add edx,ebx`).
- skippy_theme_manager_theme_struct (= object block, cfg[8]): tail pad 15 -> 7 to keep 0x2ef0.
- new element types theme_texture(0x3C), theme_ani_clip(0x10) used by cfg. Deleted duplicate theme_object/theme_submesh.
Now all theme fields are REFERENCED via skippy_theme_manager_struct.themes[38].cfg[8].textures[8]/.anim_clips[24]. Compile-asserted: cfg=0x5DD, object=0x2ef0, manager total 0x6fd90 PRESERVED, themes@0x104 (matches slot extraction). god/catcher intact.

## Struct-reference audit (found_structs_ghidra.h)
Grepped all struct defs for member-type references. Findings:
- ROOTS (referenced via Ghidra GLOBAL typing, not membership; "orphan" metric false-positive): skippy_main_game_struct (@0x46c498), skippy_theme_manager_struct (theme mgr global).
- WIRED this pass: skippy_mdl_struct -> now referenced by typing sub-mesh model_handle (@cfg+0xc) as skippy_mdl_struct* (also documents submesh->mesh).
- DOCUMENTATION-ONLY (valid mappings, not attached to a parent yet): 
  * skippy_savegame_manager_struct -> home is god+0x170a7c (currently pads in god object); embed only if we want it live in god-object decompile.
  * skippy_level_data_struct (per-level render geometry: floor/glue/stair/static tile vertex-buffer descriptor, ~0x12c) -> likely the LevelResources block header @god+0x48b98 (level_resources_load_level 0x420d38); confirm before wiring.
Policy: keep good mappings even if not member-referenced; they document real engine structs and are typable onto the relevant Ghidra load_model/savegame/levelres vars.

## level_data investigation: it's the world tile-render geometry (resolved)
- skippy_level_data_struct = per-tile-type BATCHED render geometry. NOT the LevelResources block (0x48b98 = .leo extra-objects array, filled by level_resources_load_level 0x420d38).
- Built by build_world_tile_geometry (0x404dd0, renamed from FUN_00404dd0): pass1 counts grid tiles by type into struct float-index fields; allocs vertex buffers; pass2 emits a quad per tile. Consumed by main_render_func.
- HOME: global world_tile_geometry @ 0x4e0070 (renamed from DAT_004e0070). TODO(GUI): type this global as skippy_level_data_struct to make it live in decompiles (MCP only renamed it).
- Resolved all 8 num_of_static_tiles_unkn_N -> tile types (verified via builder type->index): leaf(t9), platform(num_of_platforms_on_lvl), destruct(t0xd), jumppad(t0xe), teleporter(t0xf), switch(t0x11), forceddir(t0x10), ice(t0x15), obstacle(t0x17). Existing floor(t1)/glue(t2)/stairs(t5-8) confirmed correct. exit_transform (was pad1) = single Exit world pos+rot. Compile-asserted (size 0x12d, offsets exact).
- 0x406530 build_world_side_walls: finalizer; scans tile-height edges, builds vertical skirt geometry -> world_tile_geometry+0x124 (num_of_side_verts) / +0x128 (verticies_buf_ptr_side_walls). Resolves the old skippy_level_data_struct pad3[8] tail (theme "Side"/SideHeight).

## savegame_manager embedded into god object
Verified skippy_savegame_manager_struct layout vs savegame_manager_set_defaults (0x43b560): this->num_of_savegames drives loop, this->savegames[0] stride 0x2a — matches struct (no off-by-one). Embedded `struct skippy_savegame_manager_struct savegame_manager_obj` @god+0x170a7c (replaced offs_170a7c+pad23[11419] with struct(0x400)+pad23[10396]). Compile-asserted: god still 0x51790d, num_of_platforms_on_lvl@0x173718 / skippy_obj@0x1751c9 / game_state@0x2ab58c intact. Now a referenced member.
Orphan audit end-state: only skippy_main_game_struct + skippy_theme_manager_struct show 0 member-refs, and those are the two ROOT globals (typed onto globals, correct).
- CORRECTION: 3 structs show 0 member-refs but ALL are legit ROOT globals (typed onto globals, not members): skippy_main_game_struct (@0x46c498), skippy_theme_manager_struct (theme mgr global), skippy_level_data_struct (world_tile_geometry @0x4e0070). No true orphans remain — every struct has a home.

## Unknown-field campaign — Tier A (file-derived) done
- skippy_mdl_verticy_struct: unk1/unk2 -> u2_value/v2_value (FVF 0x212 = XYZ|NORMAL|TEX2, two UV sets; load_model 0x437bc0 reads all 10 floats). Vertex = pos3+normal3+uv0(2)+uv1(2).
- skippy_level_manager_tile_struct: already fully folded (§7 map) — only deep-usage pads remain.
- Confirmed layouts vs loaders: skippy_mdl_struct (load_model), skippy_savegame_manager_save_struct (game_load/save_savegame_gamefiles 0x43b4a0/0x43b3d0; 0x2a record, +55 obfusc; current_lvl@0xe/hearts@0xf/score@0x10).
- DEFERRED (buried-usage, NOT loader-written, don't guess): mdl pad1[96] (0x16-0x75, runtime scratch/bbox), theme_texture unk_08, savegame save_struct pad1[4]/pad2[23] (apply-savegame path).
- Tier B (usage-derived) running via 3 read-only agents: catcher remainder, level_manager+player, sound+theme-env.

## Tier B agents folded — catcher + level_manager (verified)
CATCHER (skippy_catcher_struct, ~55 fields now named, size 0x161 asserted; ctor 0x41f900 confirmed anchors 0x48=20.0, 0x66=200.0, 0x124=1, 0x156=1):
 sim_time_now@0x04 + sim_clock_ptr@0x0c + frame_dt_ptr@0x10; frame_dt@0x15; idle_anim_interval@0x38; anim_phase_flag@0x40; moving_back_flag@0x44; settle_threshold@0x48; move_completion_time@0x50; ice_slide_latch@0x58; queued_turn_cmd/id@0x60/61; ai_behavior_category@0x62; ai_pathfind_heading@0x64; default_move_interval@0x66; timing_dirty_latch@0x6e; anim_start_time@0x72; pause_{sub,complete,started,active}_flag@0x7a/7e/82/86; pause_start_time@0x8a; attack_target_lo/hi@0xdc/e0; attack_active_flag@0xe4; warp_entry_dir@0xee; interruptible_lock@0xef; forced_walk_flag@0xfb; warp_phase@0xff; warp_phase_start_time@0x100; anim_lock_phase@0x11f; facing_deriv_mode@0x124; glue_stuck_start_time@0x126; stair_transition_code@0x12e; sub_object_ptr@0x13b; control_reversal_flag@0x156; ai_target_x/y@0x15b/5c.
LEVEL_MANAGER: offs_0090->level_display_name (write_level_report "Levelname"), offs_0192->time_limit (par time; timeout->death), offs_0010->unused_meta_name (write-only). crystals_needed@0x196 confirmed.
PLAYER (skippy_player_struct) — DEFERRED, CONTESTED: agent found crystals_collected DWORD@0x232 (3 proofs), hearts_collected@0x22e, total_pickups@0x20f, power-up effect block @0x1cf-0x1ff, and a float@0x212 (existing num_of_crystals_actual mislabel). BUT game_tick copies an 8-byte sim-clock double to player+0x231 that ALIASES the crystals dword — needs live disambiguation before folding. Existing labels num_of_hearts_hud@0x231/num_of_crystals_collected@0x235 likely byte-shifted artifacts. DO NOT fold until verified.

## Tier B folded — sound_manager + theme-env (verified)
SOUND_MANAGER (skippy_sound_manager_struct, 0x204): vtable@0, directsound_device@4, owns_directsound@8, sound3d_enable@0xc, dup_pool@0x14(0x78), sound_enabled@0x8c, buffer_caps_flags@0x90(dflt2), static_sound_registry@0x94(list head@0x98), multi_sound_registry@0xa4(head@0xa8), lock@0xba(CRITICAL_SECTION). Sound obj node=0x58 (logger@4,name@8,buffer@0x10). ctor 0x4430e0.
THEME ENV (skippy_theme_manager_enviroment_struct, 0xfd): cfg[62] FULLY resolved = 10 texture handles (hud/menu/edge/radar/pointer/freeze/inversecontrol/protection/slowdown/speed icons @cfg0-9) + hud_text_color[2]@cfg10 + menu_text_colors[50]@cfg12 (25 Menu*TextColors pairs). fog_enabled@0xf8, skybox_cfg@0xf9 (6-face cube loader). InverseControl/Slowdown are HUD TEXTURES here, not gameplay bools.
PLAYER still deferred (contested alias — see prior note).

## Player struct RESOLVED (task #5 done) — no alias, agent offsets were stale-catcher-shifted
skippy_player_struct = catcher(0x161) + player-only; size 0x264 preserved (god 0x51790d intact). Verified from code (god-absolute, esi=god):
- lives @player+0x239 = god+0x175402 (heart pickup type7 ++, game_tick decrements on death). matches old lives note.
- crystals_collected @player+0x23d = god+0x175406 (pickup handler stores; game_tick cmp <= level_manager.crystals_needed@god+0x2ab723 for level complete; reset at level-complete). CROSS-CHECK: 0x1751c9+0x23d==0x175406 MATCH.
- total_pickups WORD @0x21a (pickup handler 0x41fcb0 inc every branch).
- level_start_time double @0x231 (game_tick copies sim-clock god+0x170a54).
- per_level_default float @0x212 (old num_of_crystals_actual was a MISLABEL — it's a float, fstp @0x416672).
- powerup_effects block @0x1ca-0x211 (speed/freeze/protect + variants; flag+timestamp dword pairs).
REMOVED wrong labels: num_of_crystals_actual(was DWORD@0x212, really float), num_of_hearts_hud(WORD@0x231, really the level_start_time double), num_of_crystals_collected(WORD@0x235). These were byte-shifted artifacts from an old 0x156 catcher size (real catcher 0x161; the 0xb gap shifted all player fields). The "crystals/timestamp alias" was the agent mixing god-absolute (correct) + stale-struct-relative (wrong) offsets — NOT a real overlap.
CAMPAIGN COMPLETE: catcher, level_manager, sound, theme-env, tile, mdl, player all folded + asserted. Only deferred: a few buried-usage pads (mdl pad1[96], theme_texture unk_08, savegame pad1/pad2) noted earlier.

## Pickup + ADD-sound + bomb/thrower campaign (latest, rewrite-driven)
Applied this session (renames + one comment):
- `0x41fcb0` FUN_0041fcb0 -> **skippy_pickup_effect_apply** — the per-frame pickup/effect
  handler. Reads current cell pickup_type, applies effect, bumps total_pickups@+0x21a,
  stamps last_pickup@+0x63, plays the ADD sound.
- `0x417700` FUN_00417700 -> **bomb_spawn** — allocates a 0x172-byte bomb entity (idx list
  count @+0x17460f, ptr array @+0x173e40), grid pos @+0x31/32/33, fuse spawn-time @+0x16a.
- `0x417a20` FUN_00417a20 -> **bomb_despawn** — removes a spent bomb from the list.
- `0x417250` FUN_00417250 -> **entity_index_slot_alloc** — slot allocator for the bomb/enemy
  index lists.
- `0x402870` **bomb_tick** (already named) — added decompiler comment: fuse windows 0-2000 fly /
  2000-2400 detonate 3x3 blast (writes z to cell+4 marker, destroys type-0x17 obstacles) /
  2400 clear / 2600 despawn; explosion death = pad7[0]=4, immune if entity_mode 3(protection)/9.
Already-named, confirmed relevant (no rename): `game_first_frame_init` 0x41a280 (loads the ADD
bank ADD01..10 x A/B/C, 3 dword slots each, base catcher+0x15e; ADD02 exit-open @sound_mgr.pad1+0xcc),
`enemy_ai_move_toward_target` 0x412240 (thrower=class+0x152==3; throws when player within
Chebyshev 1, 2000ms cooldown, sets pending flag +0xe4). `0x4208f0` "John Release Bomb" is a
LABEL inside game_tick region (not a function entry) — left as-is.

pickup_type BYTE enum (verified, see rewrite src/main.c PT_TO_ADD + sim try_collect):
1=crystal(theme snd), 5=paraglide(ADD01), 6=time(+5s,ADD07), 7=heart(+1hp,ADD06),
8=freeze(5s,ADD08), 9=bomb(+3 ammo player+0xe8,ADD04), 10=speed(ADD03), 11=inverse(ADD10),
12=slowdown(ADD05), 13=protection(ADD09), 0xFF=surprise(random 5..12). ADD variant = floor(t)%3.

## Thrower + bomb pipeline (latest, rewrite-driven) — verified, applied
- `0x402700` FUN_00402700 -> **bomb_entity_ctor** — constructs the 0x172-byte bomb
  entity (called by bomb_spawn 0x417700).
- `0x438770` **skippy_tick** — added comment: applies EXPLOSION death (cell+4 marker,
  z +-1) to player + catchers; entity_mode 3(protection)/9 exempt. Death types
  pad7[0]: 1=caught, 2=fall, 3=timer, 4=explosion.
Confirmed (already named, no rename): `enemy_ai_move_toward_target` 0x412240 — thrower =
entity class(+0x152)==3; throws when John within Chebyshev 1, sets pending +0xe4, 2000ms cd.
bomb flies forward via skippy_tick during the fuse; blast 3x3 @z-plane.
UNIFIED pipeline: player "John Release Bomb" (label 0x4208f0) + thrower both set +0xe4 ->
game_tick spawns via bomb_spawn -> bomb_tick (fuse 2000 / detonate 3x3 / clear 2400 / gone 2600).
Rewrite mirrors this (src/sim/sim.c spawn_bomb_at + bomb_update + bomb_explode + throwers_tick).

## 2026-07-06 — RENDER/SCRIPT CAMERA (observe-orbit) RE
Renamed:
- 0x404470  FUN -> build_orbit_camera_view   (orbit + view-matrix builder, per-frame; gameplay & scripts)
- 0x407b20  FUN -> matrix_look_at_lh         (D3DXMatrixLookAtLH-style: eye, at, up, roll)
- 0x41d920  FUN -> script_camera_vm_tick     (per-frame .jjs VM: move/spline interpolation)
- 0x41dbe0  FUN -> script_parse_command      (.jjs statement dispatch/parser)
- 0x418c70      -> update_script_camera      (copies script sub-object -> render cam each frame)
(already-named: compute_gameplay_camera 0x404120, game_tick 0x414df0, main_render_func 0x426f50)

CAMERA MODEL (definitive). ONE follow-cam path used by BOTH gameplay and .jjs:
  main_render_func 0x426f50 -> game_tick 0x414df0 (updates cam state; update_script_camera when scripted)
                            -> compute_gameplay_camera 0x404120 (packs params -> global 0x4e01a0)
                            -> build_orbit_camera_view 0x404470 -> matrix_look_at_lh 0x407b20 -> SetTransform(VIEW).
Orbit each frame:  eye = target + Rot(azimuth, elevation) * (0,0,-dist).
  target(0x46c4ac/b0/b4) = lerp toward focus[out+6..8] @0.004 (const 0x45d340)
  dist = lerp(cur, DESIRED[+0x28ab29] default 7.0, dt*0.005 @0x45d32c), clamp [1.0, 255.0]
  azimuth[+0x46c4b8] = lerp toward facing-yaw + offset[+0x2ab572] @0.006 (0x45d330), wrapped +-pi   <- slow CCW orbit
  elevation = elev_deg[+0x2ab576] default 60.0 (range [50,89]) -> pitch[+0x46c4bc] lerp @0.005, clamp <=89.9deg
  breathing: DESIRED = base[+0x13cca4](7.0) + 0.2*sin(t*0.0025)  (0x45d3e0 / 0x45d3e8), only when mode[+0x28ab2d]==0
Camera-mode flag +0x28ab2d (pad32+0xf53f7): 0 = normal follow-orbit; 2 = frozen (pause/gameover/complete/catch/jump).
Secondary EXPLICIT two-point branch in main_render_func (0x427059-, gated [+0x196086]!=0): eye/target taken
  from explicit script cam pos+target, yaw/pitch via atan. THIS is what setcamposxyz + splinexyz drive.

.jjs command -> camera mapping (script sub-object fields):
  observe N (+0xDAD)  = the CAMERA-MODE byte, NOT an actor index (CORRECTED 2026-07-10). No code reads N
                        as an actor; the look-at stays gotoxyz/movetoxyz-owned. update_script_camera 0x418c70
                        copies it into the mode flag +0x28ab2d each frame (base alias: script+0xdad == pad32+0xdac;
                        the VM gate *(int*)(pad32+0xdad) is the running flag script+0xdae, set by load/`again`,
                        cleared by `break`). Modes per compute_gameplay_camera 0x404120 + build_orbit 0x404470:
                        0 = gameplay follow (focus = player, azimuth eased toward facing+offset);
                        1 = desired yaw input held -> azimuth PARKED (static shot, e.g. behind John);
                        2 = azimuth = fmod(azimuth + dt_ms*0.001, 2pi) @0x404179 -> EXACTLY 1 rad/s CCW spin
                        (0x45d308=0.001, 0x45d300=2pi, 0x45103a=crt_fmod; the rotating crystal shot in
                        Castle TimeBonus ends `observe 2`, parked captions end `observe 1`).
  distance N (+0x9b5) = DESIRED orbit radius +0x28ab29; eye GLIDES to it (lerp .005), not a snap.
  gotoxyz (+0x999)    = set look-at point immediately.  movetoxyz (+0x989 speed) = look-at glides, speed-based
                        (look-at += dir*speed*dt*0.001, 0x45d308 = ms->unit).  Units = grid cells.
  setcamtargetxyz (+0xDA1/DA5/DA9, Y negated) = explicit look-at override.
  setcamposxyz (+0x931/935/939, Y negated)    = explicit EYE, but only reaches sound listener in orbit path;
                        real eye only in the two-point branch.  splinexyz (Bezier FUN_004024e0/00402330) = eye
                        along curve seeded from current cam pos.  anglexyz (+0x959/95d/961) = orbit-angle offset.
Key/manual cam handlers 0x419c30-0x419dd5: zoom base_dist +-dt*0.01 [2,20]; far toggle 40.0; azimuth +-dt*0.001;
  elevation +-dt*0.05 clamp [50,89].
REWRITE (src/formats/jjs.c): two-mode VM — orbit (eye=target+Rot(az,el)*dist, az auto-CCW, el 55deg, dist glide)
  vs explicit fly-through (setcampos/spline eye). observe=actor index (NOT a duration); splinexyz non-blocking
  (paired `wait` holds); movetoxyz blocks until arrival. Matches demo + level-intro scripts.

## 2026-07-07 — FALL/DEATH + OCCLUSION CAMERA RE
cam-mode byte god+0x28ab2d: 0 normal orbit / 1 void-fall / 2 frozen-dead. Focus god+0x20a51(X)/
0x20a55(height)/0x20a59(Z). player death-state pad7[0]: 0 alive /1 caught /2 transient /3 timeout.
FALL/DEATH (game_tick 0x414df0): void-fall sets mode 1 once (arc_start_z - pos_z) > 2.0 (0x45d3bc),
writes focus X/Z only (height frozen). alive+grounded refreshes focus X/height/Z + mode 0. end-of-tick
override 0x4163bb: pad7[0] not in {0,2} -> mode 2. Both modes: camera FROZEN — azimuth held (mode 2
fmod-normalized via FUN_0045103a then snapped to self; mode 1 keeps last global out[5]), pitch glides
to 60deg, dist to 7; body falls/rises out of a static frame. NOT an orbit. (our rewrite death-cam
CCW-orbits — that's a deviation to fix.)
OCCLUSION (build_orbit_camera_view 0x404470, only when mode==0): snaps pitch target to 1.569051 rad
(~89.9deg, 0x45d314), glided dt*0.005; distance/azimuth untouched. Two triggers:
- FUN_00423240 = ray-vs-OBB occlusion test; walks 3D object list DAT_0046c45c (per-node byte[0]==0
  enabled), per-object FUN_00422b90 = separating-axis segment-vs-OBB using obj xform + world pos
  +0x191/195/199, gated obj+0x1b9==0. Returns 1 -> top-down snap. THIS fires near a beehive
  (decoration is such an object).
- inline terrain scan ~0x404600: steps up height planes from player grid-z, indexes cached height
  grid at god+0x2ab729 (stride 0x7f, row 100; extents +0x2ab728 X /+0x2ab727 Y); tile flag byte
  +0x2ab72a !=0 & height byte +0x2ab729 == scan level -> snap. Gated god+0x175209==0.
3D object list DAT_0046c45c: built/torn in FUN_00420ee0 (from level_resources_load_level 0x420c6c;
registers via FUN_004254f0/004386e0/00440220); also consumed by fx_rotating_billboard 0x420f50.
Renamed: 0x423240 -> camera_occlusion_ray_test ; 0x422b90 -> segment_vs_obb ; 0x420ee0 -> rebuild_scene_object_list

## 2026-07-07 — GAMEPLAY MECHANICS RE (tile/pickup tail)
Renamed:
- 0x403d40 FUN -> destructfield_tick      (0x0d collapse/regen state machine)
- 0x43ec50 FUN -> bridge_tick             (extend/retract plank animation)
- 0x419ed0 FUN -> bridge_ctor             (register bridge, measure span)
- 0x41a200 FUN -> switch_toggle_bridge_group (flip a bridge group's passability)
- 0x4172d0 FUN -> spawn_enemy             (param5 = entity_mode: 2 catcher / 3 thrower)
- 0x43a9d0 FUN -> ai_pathfind_step_toward
- 0x401db0 FUN -> ai_pathfind_astar
- 0x41f8a0 FUN -> tile_type_is_stair      (returns 1 for types 5-8)
- 0x41f190 FUN -> level_manager_load_map_file (reads 4 bytes/tile {z,type,clip,pickup})
(already named by agents: enemy_ai_move_toward_target 0x412240, skippy_game_start_level 0x416420)

Facts (all verified, ms time base):
DESTRUCTFIELD 0x0d (destructfield_tick): step arms it; +1500ms (0x45d2e0) -> collapse (type->0,
DestructStart); +5000ms from arm (0x45d2d8) -> regen to 0x0d (DestructRegen). Hole open 3500ms.
Collapse doesn't kill directly; standing entity enters normal fall (dies only if nothing below).
THROWERS: enemies (spawn_enemy mode 3) sharing catcher chase AI; hop 700ms/cell vs catcher 500ms
(entity+0x66/+0x6a); throw at Chebyshev<=1, 2000ms cd; entity_mode!=3 gate in the catch loop means
throwers are HARMLESS on contact. No ExplosionThrower sound -> throwers are BOMB-IMMUNE (only
ExplosionCatcher exists; SplatThrower/FallThrower exist but enemies never fall in play).
STEP-UP (skippy_tick 0x438770): +1 onto a NON-stair floor is CANCELLED (move_turn_state=0, stay
put) @0x43a31f — not a climb, not a fall. Only stairs set move_dz=+1 (along axis). Drop >=3 = splat
death (@0x439640); drop <3 & landing z>1 = safe.
JUMPPAD 0x0e (skippy_tick @0x439c12): rest on pad -> rise IN PLACE via arc z=z0+v0*t-4.905*t^2,
v0=sqrt((zT-z0+7.2)*19.62); zT = clip_rule (0x45d6c0=7.2, 0x45d6bc=19.62, 0x45d6b8=4.905). At apex
buffers a forward hop onto the landing cell.
ENEMY FACTORY pickup 0x64 (game_tick @0x41518e): spawns catcher every clip*1000ms (clip<100), cap 5
alive, first fire staggered by index*1000ms; tile keeps pickup 0x64 across deaths (keeps producing).
PARAGLIDE pickup 5: banks charges (bounce_count); fall >2 with a charge -> airborne_flag=1, slow
STEERABLE descent (anim state 5), lands safely; one charge/glide. Parachute = theme Paraglide slot
(and John's Paraglide-Condition meshes).
TELEPORTER 0x0f (skippy_tick @0x439050): clip_rule = pair id, paired 1:1 at load (bidirectional,
first-match), dest baked per tile; step -> arm -> warp (facing preserved) -> auto-step-off; per-
player state pad6b[0x11], dwell 0x45d6e8.
SWITCH 0x11 / BRIDGE 0x12(X)/0x13(Y) / PLANK 0x14: switch clip_rule-1 = bridge id; step toggles it
(skippy_tick 0x438b90 -> game_tick 0x41552f -> switch_toggle_bridge_group). Bridge object array at
base+0x170643+id*4; extends/retracts ~100ms/cell writing plank type 0x14 over void cells at the
bridge z; Space theme renders Switch=slot30 (schalter) / Bridge=slot37 (space_bridge) as Fields.

## 2026-07-07 — JUMP PAD (tile 0x0e) + enemy AI RE
NOTE: Ghidra MCP was DOWN during this dig (port 8080 not listening); addresses below are
from docs/ida_dump/full_dump.c (IDA dump), NOT the Ghidra DB @0x400000 — DO NOT rename in
Ghidra with these until re-verified live + address-mapped. Recorded here so findings persist.
Shared per-entity movement/physics tick = game_tick(catcher*) @IDA 0x438770 (jumppad arc @0x439c12);
player skippy_tick calls it, enemy AI calls it at 0x24883/0x24918/0x24954 (dump lines).
Passability sub_41F500(tiles,toX,toY,fromX,fromY) @IDA 0x41f500 — jumppad branch: a move ONTO a
type-14 tile is allowed ONLY if the cell one-further-step in the same dir is at the pad's target
height (+X:+13112 / -X:-12288 / +Y:+539 / -Y:+285 vs pad target TILE+0x51); moving OFF a pad always
allowed. Enemy BFS: sub_401DB0 @0x401db0 -> expander sub_401EF0 @0x401ef0 -> sub_41F500. Enemy AI
modes: catcher(2)=sub_41B680 @0x41b680 (target 7), thrower(3)=sub_41B810 @0x41b810 (target 5).
JUMPPAD BEHAVIOR (reproduction-grade):
- Arm while resting on pad (tile.z_pos==pos_z): record launch time, ride flag.
- Rise IN PLACE (grid cell unchanged): v0=sqrt((target - z0 + 7.2)*19.62); pos_z=(v0 - 4.905*t)*t + z0.
- Land when pos_z>=target: snap pos_z to target height, queue exit hop = ENTRY travel dir (pad6[0x2e],
  set = current move dir at launch), overwriting any queued input (exit dir FORCED).
- Then ONE normal cell-step in that dir onto the beyond-cell (guaranteed target height by passability).
- CHAINING: if the beyond-cell is itself a pad at its z_pos, it re-arms -> launches again; chains can
  turn corners. A diagonal net displacement = multiple chained pads, NOT one launch.
- Sound: fires MoveJumpPad event on arm (sound_play); NONE in most themes -> silent (Space/Candy have wavs).
- Texture: pad = slot-15 mesh drawn on the tile; any glow/pulse is the mesh texture's FX-anim (theme data),
  no special hardcoded tile texture.
REWRITE GAPS to fix: (a) enemy passability must handle 0x0e as a directional portal (beyond-cell==target
height) + enemies run the shared launch; (b) verify pad chaining; direction/input-lock/sound already match.

### UPDATE (Ghidra back up): IDA-dump addresses ALIGNED with the Ghidra DB (same RUS_Karoo.exe @0x400000).
Verified: 0x41f500 has the exact type-0x0e neighbor-height branch (+0x3338/-0x3000/+0x21b/+0x11d). Applied:
- 0x41f500 -> tile_passable (+ decompiler comment: jumppad portal rule, tile offsets +0x19c z / +0x19d type)
- 0x41f8c0 -> dir_from_to_cells
Already named in DB: ai_pathfind_astar 0x401db0, astar_expand_neighbors 0x401ef0, find_cell_of_type 0x41b810,
skippy_tick 0x438770 (the shared per-entity movement/physics tick incl. the jumppad arc). 0x41b680 left as-is
(catcher AI target-finder; not renamed pending confirmation).

## 2026-07-08 — PARAGLIDE / MOVEMENT / ENEMY-AI / SLIDE RE (parallel read-only agents)
Full findings: rewrite/reference/re_g3_paraglide.md, re_g4_movement.md, re_g6_enemy_ai.md, re_g7_slide.md.
Agents ran read-only; DB deltas below are the only function-entry renames (both already present in DB — no
new writes needed this pass). Everything else anchors to sub-regions INSIDE skippy_tick/game_tick (labels, not
function entries) so it lives in the docs + as decompiler comments, not as renames.

DB function names (verified present, no change): ai_pathfind_step_toward 0x43a9d0 (BYTE __thiscall(this,
target_x, target_y, opts); body ..0x43ad37) -> ai_compute_path 0x401c20 (..0x401cac) -> ai_pathfind_astar
0x401db0 -> (if buffered_move_command != 0) skippy_tick 0x438770. enemy_ai_move_toward_target 0x412240 and
skippy_game_start_level 0x416420 also already named.

G3 PARAGLIDE (skippy_tick regions): pickup slot 5 banks charges (bounce_count). A fall of >2 z with a charge
sets airborne_flag=1 -> slow STEERABLE descent, anim state 5 (render-id 5 = "paraglide"); lands safely, one
charge consumed per glide. Parachute mesh = theme Paraglide slot 8 + John's Paraglide-Condition meshes.

G4 MOVEMENT (skippy_tick 0x438770; constructor 0x41f900; spawn_enemy 0x4172d0; input shims
skippy_move_forward/_backward/_turn_left/_turn_right 0x41fa90..0x41fc90): move_interval @catcher+0x132 (double)
= MS PER CELL, loaded each idle frame from default_move_interval pad4b @+0x66. Defaults: player 200ms,
catcher 500ms, thrower 700ms (thrower = 2.5x1.4). NO separate turn timer — a turn is a move_turn_state cycle
gated by the same move_interval completion test, so player turn = 200ms (= a hop); turn commits no grid step
(move_dx=dy=dz=0), plays current_anim_pos 0x1e turn-R / 0x1f turn-L. Effect duration _DAT_0045d430 = 10000ms
(speed/slow/inverse/protection); freeze _DAT_0045d2d8 = 5000ms. Render pos interpolates old->new linearly by
frac=(now-last_move_time)/move_interval.

G6 ENEMY AI: pathfind is BOUNDED best-first toward the player (ai_pathfind_step_toward -> ai_compute_path ->
ai_pathfind_astar), NOT global all-aggro. When the player is beyond the expansion budget the step returns none
(enemy idles) — this is the fix for the Space-bonus "everything aggros at once" break. Behaviour categories
(ai_behavior_category @+0x62) types 1-7 exist in-binary; rewrite currently implements chase-player only.

G7 SLIDE (tile type byte 0x10 = theme "Slide" slot 16; hit-tested in skippy_tick's cell-type switch alongside
0x02/0x09/0x0c/0x0e/0x0f/0x11/0x15...): a slide is a FORCED-direction, FORCED-one-z-down redirect. On a 0x10
cell the entity faces slide_forced_dir (from clip_rule) and auto-hops one cell in the chute dir + one z down;
src 0x10 && dst 0x10 -> current_anim_pos 0x04 ("slide", render-id 4 -> clip slot 13 via anim_renderid_to_clip
0x401970). Landing on another 0x10 re-arms; terminates when the next cell is not 0x10.

## Score / summary / highscore session (scoreboard RE)
| addr | name | note |
|------|------|------|
| 0x41a760 | game_calc_summary_scores | RENAMED (was FUN_0041a760): fills the level-complete summary block main+0x140502.. and the run total @+0x1753f5. Full formula + row mapping in savegame_highscore_format.md (crystals x5 / extra x10 / destroyed enemies x50 / time-left x2 / Sisyphus 100%-collection x5 / vitality x1). |
| 0x41aca0 | (cheat console dispatcher) | debug word commands typed in-game: "kaputo" (kill catchers), level-jump by name/number, "mausuruh"/"sportsman"/"boommaker" (bump score counters), "Bernie Boulder" default-record seeding lives in write_level_report. |

Corrections: .hsc obfuscation key = 0x4B ('K') on BOTH load 0x41ef20 and save 0x41efe0
(pushed at the call sites; the "key 55 same as savegames" note was wrong). .sav key 0x37
confirmed at 0x414886/0x4148cd. num_of_highscore_records = 10 (set at 0x41496c).
Highscore record: name@0 (<=50), score DWORD@50, level BYTE@54 (draw_highscore_table
columns + the seeding stride in write_level_report). WORD main+0x42250 = total pickups
on the level (summed in skippy_game_start_level; the Sisyphus bonus base). The
num_of_*_on_lvl block = STATIC level-content counts (skippy_game_start_level tile scan),
not player tallies — collected tallies live elsewhere (crystals @+0x175406 etc.).
