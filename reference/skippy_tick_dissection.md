# skippy_tick (0x438770) — full dissection

`char __fastcall skippy_tick(skippy_catcher_struct *this)`. Per-entity grid tick
(player + enemies). 1249-line decompile. This is a faithful *structural* read;
`pad*` field meanings are INFERRED from usage (marked). The core loop is legible.

## Inferred field map (skippy_catcher_struct, from usage)
| field | inferred meaning |
|-------|------------------|
> ⚠ OFFSETS UPDATED. The catcher struct was re-modeled (current_anim_pos +3-drift fix
> + move_interval/last_move_time/move_turn_state carved out), so the OLD pad indices in
> earlier revisions of this table (pad6[0xc], pad6[0x34], pad9[0x16] …) are STALE. Below
> are the CURRENT absolute offsets — the SOLID ones are now named fields in
> found_structs_ghidra.h (source of truth); INFERRED ones are pre-remodel and not yet named.

**VERIFIED / now named (found_structs_ghidra.h):**
| abs off | field | meaning |
|---------|-------|---------|
| +0x25/29/2d | pos_y/pos_z/pos_x (float) | render coords (interp between cells) |
| +0x31/32/33 | pos_x_1/y_1/z_1 | grid cell coords (logic) |
| +0x14 | facing_direction | 1=TOP 2=RIGHT 3=DOWN 4=LEFT |
| +0x5c | fall_velocity (float) | fall/jump arc v0 (pure fall = -3.0) |
| +0x9a | current_anim_pos | RENDER-ID (not slot ordinal) — see below |
| +0xe9 | bounce_count | bounces remaining on hard landing |
| +0xea | airborne_flag | 0=free parabola, !=0=settling to tile top |
| +0x108 | stored_move_dir | re-issued dir for ice-slide / jump-pad exit |
| +0x111 | arc_start_z | launch/fall arc start height |
| +0x112 | ride_start_time (double) | when jump-pad/elevator ride began |
| +0x11a | ride_active_flag (dword) | elevator/jump-pad ride in progress |
| +0x11e | offs_011E | riding-platform id (0xFF = none) |
| +0x120 | is_falling | falling flag |
| +0x125 | last_move_cmd | move variant 1=fwd/2=turnR/3=bwd/4=turnL |
| +0x132 | move_interval (double) | time to cross one cell |
| +0x13f/40/41 | move_dx/dy/dz | grid step this move (dz signed, stairs/ladders) |
| +0x145 | buffered_move_command | queued move dir 1-4 |
| +0x146 | last_move_time (double) | when current move started |
| +0x14e | move_turn_state (int) | active move dir (0=idle) |
| +0x152 | entity_mode | 9=ghost/dead (AI + anim gates) |
| +0x38/x3c (pad1/pad2, dbl) | current sim time / frame dt | (loop time source) |

**INFERRED, pre-remodel offsets — NOT re-verified, NOT named (treat as candidates):**
| old ref | meaning |
|---------|---------|
| pad6 ~0x1e | on-ladder / climbing flag |
| pad6 ~0x21-0x24 | lose-tile(0x0f) state machine + timer |
| pad3 ~0x10/0x18 (dbl) | queued / max move time |
| pad5[*] (dwords) | per-effect ENABLE flags (sound/fx toggles from config) |
| pad8[0/1] | AI-controlled flag / special-move type; pad8[2](dbl) glue timer |

NOTE: current_anim_pos stores a RENDER-ID (walk=0x14, ice=3, glue=9, jump=0xb, stairs
0x16-0x1b, turns 0x1e/0x1f, idle 0xfa/0xfb) — anim_renderid_to_clip(0x401970) maps it to a
SkippyCatcherAnim clip slot. It is NOT the .ani slot ordinal. (The old "0x14-0x1b beyond the
24 slots" note was the render-id/slot mix-up; both numberings are real & now pinned.)

## Control flow (phases)
1. **Idle/reset**: clear anim to 0 if not doing anything; handle glue-stuck timer (pad8[2]).
2. **Current-cell type handlers** (on tiles[pos_x_1][pos_y_1].type):
   - `0x02` glue: stick (timestamp gate), anim 9 (ghost) if stuck.
   - `0x10` (bush/forced): force facing from cell.pad1[0x52], set climb flag.
   - `0x0f` lose-tile: 2-phase timer -> teleport to linked cell (cell.pad1[0x4e]/[0x4f]).
   - `0x0c` moving platform: ride it (cell stores platform world pos pad1[0x26/0x2a/0x2e]).
   - `0x15` stored-anim tile: play the anim stored in the cell, current_anim_pos=3.
   - `0x09` leaf: mark "on special tile".
3. **Teleporter follow** (pad6[0x41]!=-1): snap to target cell + its precomputed
   world coords (cell.pad1[0x23/0x24]=dest grid, +0x26/2a/2e=dest world xyz).
4. **Move / gravity block** (when a command pending OR mid-arc):
   - Decode command -> delta: 1->dy=-1, 2->dx=+1, 3->dy=+1, 4->dx=-1 (cmds >0x14 are a
     second variant, -0x14). Store dx/dy in pad9[0x16/0x17].
   - Inspect target cell (pos+delta): bounds vs level_dim_x/Y; type gates
     (0x16/0x17 stair blocked if occupied via pad1[0x77]; 0x0e hole; 0x09 leaf; 0x10);
     ladder via `tile_type_is_ladder` (0x41f8a0) -> +0.5 z.
   - Height delta (cur z vs target z) picks walk/stair anim (0x14-0x1b) and pad9[0x18]=dz.
   - Commit grid step if in-bounds & not blocked: pos_x_1+=dx; pos_y_1+=dy; pos_z_1+=dz.
   - Rate-limited by pad9[9] (interval) / pad9[0x1d] (last-move time).
   - **Vertical physics** (the only float physics): jump/fall arc (pad6[0x43]) computes
     pos_z as a parabola over time; elevator rise (type 0x0e) uses sqrt curve to pad1[0x51].
     Landing -> anim 0, clear arc.
5. **Render interpolation (tail)**: smoothly lerp pos_x/pos_y/pos_z between the old and
   new cell over pad9[9], fraction = (now - pad9[0x1d]) / pad9[9]. Stair anims (0x16-0x1b)
   get parabolic Z step arcs. Idle for _DAT_0045d2d8 sec -> current_anim_pos=0xfa (idle-timeout).

## Direction helpers (named)
- `rotate_direction(dir, quarters)` (0x43ad40): dir 1..4 rotated; `rotate_direction(d,2)`
  = opposite. Used as `cmd==facing || cmd==opposite(facing)` = move along facing axis.
- `tile_type_is_ladder(type)` (0x41f8a0): true for ladder/half-height tiles -> +0.5 z & stair anim.

## Sound/fx helpers (inferred, unnamed)
FUN_00442900(flag), FUN_00442df0(x,z,-y,1)+FUN_00442d90(0), FUN_004429a0() cluster
around set_entity_render_pos_from_grid calls and are gated by pad5[*] enable flags
-> almost certainly positional sound play/stop (step/jump/land). Not renamed (medium confidence).

## For the SDL slice — this is enough
Horizontal = pure grid step (delta from command, bounds + tile-type/ladder checks).
Vertical = per-cell z snap + parabolic arcs for jump/fall/elevator. Render = lerp between
cells over a fixed interval. The pad5[*] gates are just "is this sound/fx enabled" — safely
ignorable for a first slice.

## CROSS-CHECK via compute_enemy_render_params (0x404300) — ABSOLUTE offsets
Enemies use the same skippy_catcher_struct; this func reads raw offsets, giving
GROUND-TRUTH absolute offsets (supersede the pad-relative guesses above):
- +0x14  facing_direction  (confirmed, same facing->yaw as player)
- +0x25/0x29/0x2d  render pos x/y/z (confirmed)
- +0x125 last_move_cmd (confirmed; ==2/==4 = turn variants)
- +0x132 move_interval (double)   <-- was pad9-relative guess, now absolute-confirmed
- +0x146 last_move_time (double)  <-- ditto
- +0x14e move/turn-state (int)
- +0x152 dir byte ; +0x9a anim-state byte (>=0xfa = idle-timeout)

CAVEAT: my earlier pad-relative reads in skippy_tick (e.g. "pad10[5]=move cmd",
"pad9[0x16..18]=delta") are UNRELIABLE — Ghidra's pad boundaries don't align with
these absolute offsets (last_move_time@0x146 double would overlap pad10[5]@0x14b).
Trust ABSOLUTE offsets from raw-pointer functions, not pad+N reads.

OPEN: enemy follow-player AI (pathfinding / move-cmd generation) is a separate,
still-unfound function. Finding it would confirm the move-delta/command fields.
