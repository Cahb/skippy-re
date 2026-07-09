# Gameplay mechanics — rewrite spec (SIM core)

Goal: everything a native rewrite needs to reproduce Skippy's simulation. All logic is a
**2D grid** sim; 3D is display only. The single authority per entity is
`skippy_tick` (0x438770, `__fastcall(skippy_catcher_struct*)`), run per entity per frame
from `game_tick`. Constants below are read straight from RUS_Karoo.exe `.rdata`
(file offset = VA − 0x400000) and are VERIFIED, not inferred.

> Status: **Movement + vertical physics + slide/tiles = DONE (this doc).**
> Moving platforms, tile-cell runtime byte-map, and animation playback are being
> filled in by parallel passes — sections marked TODO below.

## 1. Position model (per entity = skippy_catcher_struct)
- **Logic (grid) pos**: bytes `pos_x_1`/`pos_y_1`/`pos_z_1` @ +0x31/+0x32/+0x33.
- **Render pos**: floats `pos_x`/`pos_y`/`pos_z` @ +0x25/+0x29/+0x2d, pushed to 3D via
  `set_entity_render_pos_from_grid(x, z, -y, 1)` (0x4429c0). Note the axis mapping:
  render X ← grid Y, render Y ← grid Z (height), render Z ← −grid X.
- **facing_direction** @+0x14: 1=TOP, 2=RIGHT, 3=DOWN, 4=LEFT. `rotate_direction`
  (0x43ad40) rotates it (arg 2 = 180°).
- **move_turn_state** @+0x14e (int): the *active* in-progress move dir (1-4, 0=idle);
  drives interpolation + the grid-step commit.
- **move_interval** @+0x132 (double): seconds to cross one cell.
- **last_move_time** @+0x146 (double): sim-time the current move started.
- **buffered_move_command** @+0x145 / **last_move_cmd** @+0x125: queued input / last variant.
- Time source: `*(double*)this->pad1` = current sim time (seconds); `*(double*)this->pad2` = frame dt.

## 2. Horizontal movement (grid step + interpolation)  [VERIFIED]
A move is committed as a discrete grid step, then rendered by interpolating over `move_interval`.
- **Step commit** (once, when the move begins), guarded by bounds + `offs_011E==0xff`:
  `pos_x_1 += dx; pos_y_1 += dy; pos_z_1 += dz` where (dx,dy)=`pad9b[5]/[6]`, dz=`pad9b[7]`.
  dz is 0 (flat), −1/+1 (step), or ±more for ladders.
- **Render interpolation** (every frame while move_turn_state≠0), `frac=(t−last_move_time)/move_interval`:
  | move_turn_state | formula |
  |---|---|
  | 1 | `pos_x = (pos_y_1+1) − frac` |
  | 2 | `pos_y = (pos_x_1−1) + frac` |
  | 3 | `pos_x = (pos_y_1−1) + frac` |
  | 4 | `pos_y = (pos_x_1+1) − frac` |
- The move ends when `frac≥1`; move_turn_state→0, entity snaps to the new cell.
- Command→delta decode & tile-blocking (bounds, walls, occupied stairs via `pad1[0x77]`,
  ladders via `tile_type_is_ladder` 0x41f8a0) happen before commit; a blocked move sets
  move_turn_state=0 without stepping.

## 3. Vertical physics (the ONLY continuous float motion)  [VERIFIED w/ constants]
Time is in ms internally; τ (seconds) = `ms * 0.001` (`_DAT_45d308 = 0.001`).

### 3a. Free fall / jump arc  (when airborne, `pad6[0x10]==0`)
```
pos_z = z0 + v0·τ − 5.405·τ²          // z0 = launch height (pad6[0x37]); v0 = pad3[0x24] float
```
- Pure fall seeds **v0 = −3.0** (bytes 00 00 40 C0 @ pad3[0x24..27]); ½·g_fall = **5.405**
  (`_DAT_45d6c4`) ⇒ g_fall ≈ 10.81 u/s². `pos_z_1 = floor(pos_z)+1` each frame.
- **Bounce**: `pad6[0xf]` = bounces remaining. On landing a drop >2 cells with bounces
  left, decrement and relaunch (sets `pad6[0x10]=1`). Anim 8 for a 3–5 cell drop, anim 5 (FALL) settling.

### 3b. Landing settle  (`pad6[0x10]!=0`)
```
pos_z -= dt · 0.004                    // _DAT_45d6c8 ; until within 0.5 of tile top, then snap
```
Snap to `tile.z_pos` when `z_pos−0.5 ≤ pos_z ≤ z_pos` (`_DAT_45d318 = 0.5`). Below pos_z_1<2 clears bounce state.

### 3c. Jump / launch pad (type 0x0e)  — ballistic leap [mechanic verified; role now consistent]
> Its arc sets anim `0xb` → the **jump** clip (earlier "glue" reading was from the anim-slot
> mix-up, now fixed) → consistent with a jump/launch pad. Role still worth an in-game glance
> (jump-pad vs elevator), but the animation no longer contradicts "jump pad".

On a `0x0e` cell, once aligned it drives the entity up to `cell.pad1[0x51]` (target z, a BYTE) as a projectile:
```
Δh = target_z − pos_z_1
v0 = SQRT((Δh + 7.2) · 19.62)           // 19.62 = 2·g,  7.2 = base-height bias
pos_z = z0 + (v0 − 4.905·τ)·τ           // 4.905 = ½·g  ⇒ g_launch = 9.81 u/s²
τ = (t − ride_start_time) · 0.001       // ride_start @ pad6[0x38] (double)
```
Anim 11 (FALL) during rise; on reaching target: snap to top, clear ride flags, set
`buffered_move_command = pad6[0x2e]` (queued exit dir), anim 12 (PARAGLIDE). Ride-active flag `pad6[0x40]`.

### 3d. Per-step hop / stair bob  (overlaid during a horizontal step)
While stepping, `pos_z` gets a small parabolic bob whose shape depends on `current_anim_pos`
(the walk/stair variant, §5). Base hop: `A=1.0/(move_interval·0.0005)`, parabola in
`elapsed=(t−last_move_time)`. Stair-up variants (0x16/0x18) add a rising ramp; stair-down
(0x17/0x1a/0x1b/0x19) subtract; flat walk (0x14/0x15) just the hop. Death-hop uses
`num_of_hops_from_death`. Constants: 1.0 (`_DAT_45d2e8`), 0.5 (`_DAT_45d6e0`), 1.5 (`_DAT_45d6a8`).

## 4. Tile-type behaviors (current cell `.type`, dispatched at top of skippy_tick)  [VERIFIED]
| type | name | behavior |
|------|------|----------|
| 0x00 | air/empty | fall (§3a) if z>0 |
| 0x02 | glue/honeycomb | sticky: timestamp-gated, stuck → anim 9 (GHOST) |
| 0x09 | leaf | mark "on special tile" |
| 0x0c | **moving platform** | ride only when platform world-pos aligns w/ render pos → sets `offs_011E`; see §6 (TODO) |
| 0x0e | **launch pad** | ballistic leap to `pad1[0x51]` (§3c) |
| 0x0f | lose/death tile | 2-phase timer → teleport to linked cell `pad1[0x4e]/[0x4f]` |
| 0x10 | **forced-direction** | force facing+move from `pad1[0x52]`; sets buffered_move_command |
| 0x11 | (crash per notes) | z-matched special |
| 0x15 | **ice / slippery** | auto-set `buffered_move_command = pad6[0x2e]` each tick (keep sliding, no steering); dir override in `pad3[0x20]`; anim 3 → **ice** clip |
| 0x16/0x17 | stair up/down | blocked if occupied (`pad1[0x77]`); picks stair anim + dz |
| ladder | (via tile_type_is_ladder) | vertical climb: pos_z_1 ±1 / +0xff |
- **Teleport** (type 0x0f): dest grid stored in source cell `pad1[0x4e]/[0x4f]` (pair id `pad1[0x4d]`). CORRECTED — the earlier `pad1[0x23]/[0x24]` was wrong; those (+ `0x26/0x2a/0x2e`) are the **moving-platform** live-position fields (§6). Confirmed independently by two passes.

## 5. Animation: current_anim_pos is a RENDER-ID (not the .ani slot ordinal)  [VERIFIED]
`current_anim_pos` (catcher +0x9a) holds a **render-id** that `anim_renderid_to_clip`
(0x401970) maps to a clip-table byte offset (table @ model cfg+0xc9, 24 × 0x10-byte entries
`{firstFrame,numFrames,fps,flag}`). The `SkippyCatcherAnim` enum in found_structs.h is the
**.ani clip-slot order** (a DIFFERENT numbering) — do not equate it with current_anim_pos.

Verified render-id → clip (from anim_renderid_to_clip 0x401970 offsets + the literal .ani
slot strings read from the binary — GROUND TRUTH, cross-checked):
| render-id | clip | render-id | clip |
|-----------|------|-----------|------|
| 0x14 / 0x15 | walk fwd / back | 0x0a | ghost |
| 0x03 | ice | 0x0b | jump |
| 0x04 | slide | 0x16–0x1b | stair variants |
| 0x05 | paraglide | 0x1e / 0x1f | turn R / L |
| 0x08 | fall | 0xfa / 0xfb | idle1 / idle2 |
| 0x09 | glue | 0x00 | (no clip) |
.ani clip-table SLOT order (loader strcmp→offset in 0x401070, verified by reading each target
string's bytes — NOT the .ani file line order): 0 walk_forward,1 walk_backward,2 speed_forward,
3 speed_backward,4 slow_forward,5 slow_backward,6 celebration,**7 jump,8 glue,9 ghost,10 ice,
11 fall**,12 paraglide,13 slide,14 idle1,15 idle2,16 field_stair_up,17 field_stair_down,
18 stair_stair_up,19 stair_stair_down,20 stair_field_up,21 stair_field_down,22 turn_left,23 turn_right.
> RESOLVED: found_structs.h `SkippyCatcherAnim` enum is CORRECT (matches the loader byte-for-byte).
> The earlier "slots 7–13 mislabeled" claim (from an agent that guessed 6 non-monotonic strings)
> was WRONG. Sanity check: glue tile(0x02)→anim9→glue, ice tile(0x15)→anim3→ice, jump-pad(0x0e)
> rise→anim0xb→jump — all consistent. NOTE: current_anim_pos stores render-ids, the enum is the
> clip-slot order — two different numberings (see anim_renderid_to_clip).

## 6. Moving platforms / elevators / teleporters  [VERIFIED, 2-pass cross-checked]
Three ticked mover kinds (dispatched from game_tick) + teleporter (static, in skippy_tick).
Motion = **linear ping-pong**, speed `0.005` grid-units/ms (`_DAT_45d384`), **1500 ms dwell**
at each end (`_DAT_45d2e0`).
| mover | cell type | count @god | ptr-array @god | tick | register |
|-------|-----------|-----------|----------------|------|----------|
| elevator (vertical) | 0x09 | 0x173b19 | 0x173719 | `elevator_tick` 0x411cb0 | `register_elevator_cell` 0x417b90 |
| platform (horiz X/Y) | 0x0a/0x0b | 0x173718 | 0x173588 | `platform_tick` 0x43ae00 | `register_platform_cell` 0x417e20 |
| destructible | 0x0d | 0x173e3e | 0x173b1e | 0x403d40 (cand.) | 0x418240 (cand.) |
| slider hazard | 0x14 | 0x170a43 | (runtime) | 0x43ec50 (cand.) | — (runtime spawn, unknown) |
- **Elevator**: worldZ = segStart + (now−t0)·0.005 up/down; writes its live world-Z into its cell
  `pad1[6]` (float) and grid z into `cell.z_pos`.
- **Platform**: stamps type **0x0c** on its current cell; writes live pos into its ANCHOR cell:
  `pad1[0x23]/[0x24]` = current grid X/Y, `pad1[0x26]/[0x2a]/[0x2e]` = world Y/Z/X (floats).
  Run cells carry a back-ref `pad1[0x20]/[0x21]` → anchor, and `pad1[0x0a]` = platform id.
- **Rider carry** (skippy_tick): on a 0x0c cell, if render XY within `0.25` of anchor world pos &
  `pos_z ≥ cell.z_pos` → `offs_011E = pad1[0x0a]` (riding id). While riding & not falling
  (`pad7[0]==0`), snap grid `pos_x_1/pos_y_1 ← pad1[0x23]/[0x24]` and render `pos_y/pos_z/pos_x
  ← pad1[0x26]/[0x2a]/[0x2e]`. Elevator carry: grid/render z ← `cell.z_pos`/`cell.pad1[6]`.
- **Teleporter** (0x0f): instant relocate to `pad1[0x4e]/[0x4f]`; 500 ms cooldown (`_DAT_45d6e8`).
- Candidate names (destructible/slider) NOT applied — single-source, pending verification.

## 7. Tile-cell runtime byte-map (123-byte `pad1`)  [VERIFIED offsets; some writers external]
`pad1[k]` = cell offset `4+k`. Initialized in `skippy_game_start_level` (0x416420) grid scan.
| pad1 idx | type | field | meaning |
|----------|------|-------|---------|
| 5 | byte | entity_reservation | cell entered/reserved by an entity (blocks pathing) |
| 6 | float | floor_render_z | cell floor world-Z; fall settles to it |
| 0x0a | byte | platform_id | moving-platform (0x0c) id → rider `offs_011E` |
| 0x1c | dword | walkable_override | nonzero ⇒ bridge makes empty cell walkable |
| 0x20/0x21 | byte | platform_master_x/y | back-ref to platform anchor cell |
| 0x23/0x24 | byte | platform_cur_x/y | platform current grid pos (on anchor cell) |
| 0x26/0x2a/0x2e | float | platform_world_y/z/x | platform live world pos (rider snap) |
| 0x4d/0x4e/0x4f | byte | telep_pair_id / dest_x / dest_y | teleporter (0x0f) |
| 0x51 | byte | elevator_target_z | type 0x0e rise target |
| 0x52 | byte | slide_forced_dir | forced dir on slide/redirect tile (0x10) |
| 0x53 | byte | switch_group_id | switch (0x11) → bridge linkage |
| 0x54/0x55/0x56 | b/b/dw | bridge_obj_index / orientation / state | bridges (0x12 X / 0x13 Y), built by 0x419ed0 |
| 0x62 | byte | obstacle_hidden_pickup | pickup under destructible (0x17); restored on retry |
| 0x63 | dword | pickup_collected_flag | render skip-draw when set |
| 0x6f | dword | render_instance_handle | cached render object for the cell |
| 0x73 | float | pickup_spin_phase | per-pickup random bob/spin phase |
| 0x77 | dword | occupied_by_entity | occupancy; walkability + glue/obstacle blocking |
Unknown: `pad1[0]` (overhead/bridge surface z), `pad1[1..4]` (bridge scratch) — writers external.
NEW subsystem surfaced: **switches (0x11) → bridges (0x12/0x13)** via `build_bridge_object`
(0x419ed0) — inference-heavy, unverified, flagged for review.

## 8. Animation playback (clip → frame → draw)  [VERIFIED]
Models are **vertex-morph** (each .mdl frame = a full vertex snapshot); NO skeletal, NO tween.
- `.mdl` (load_model 0x437bc0): `num_frames`, `num_verts`; then per frame a 24-byte header +
  `num_verts × 40B` vertices (pos+normal+2×UV). Frame N block = `anim_buf + N·num_verts·0x28`.
- `.ani` (game_load_animation_file_gamefiles 0x401070): text `Name First Num FPS [Flag]`, placed
  by strcmp into the 24-slot table (§5). Flag = optional one-shot marker.
- Per frame: `clip = anim_renderid_to_clip(cfg+0xc9, current_anim_pos)`; then
  `frame = clip.flag ? (clip.first − clip.num·t) : (clip.first + clip.num·t)` (t = motion phase);
  clamp `frame < num_frames` else 0. Walk phase = `(now−last_move_time)/move_interval`; ambient
  models free-run off `fps·wallclock`. Draw: `draw_mesh_frame` (0x437b40) → one
  `DrawPrimitive(TRIANGLELIST, FVF 0x212, anim_buf + frame·stride, num_verts)`. No per-entity
  frame cursor is stored — frame is recomputed each render from the phase.
- Attachment meshes gated on current_anim_pos (e.g. `!=0xa` draws theme[36] rig, `==5` theme[9]).

## Constants table (from .rdata, verified)
| VA | value | role |
|----|-------|------|
| 0x45d308 | 0.001 (f) | ms→s |
| 0x45d318 | 0.5 (f) | land-snap epsilon |
| 0x45d3bc | 2.0 (f) | drop threshold (cells) |
| 0x45d3e0 | 0.2 (d) | z-mismatch epsilon |
| 0x45d6a8 | 1.5 (d) | stair-hop amp |
| 0x45d6b0 | 0.0005 (d) | interval→hop scale |
| 0x45d6b8 | 4.905 (f) | ½·g (launch) |
| 0x45d6bc | 19.62 (f) | 2·g (launch) |
| 0x45d6c0 | 7.2 (f) | launch base-height bias |
| 0x45d6c4 | 5.405 (f) | ½·g (fall) |
| 0x45d6c8 | 0.004 (f) | settle gravity/frame |
| 0x45d6e0 | 0.5 (d) | hop amplitude |
| 0x45d2e8 | 1.0 (d) | hop unit |
