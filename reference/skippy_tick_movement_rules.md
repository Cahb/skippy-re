# skippy_tick (0x438770) — movement rules (faithful extract)

`__fastcall skippy_tick(skippy_catcher_struct *this)`. Per-entity grid tick
(player + enemies). This documents the **movement/tile logic only** — the
faithful part. The rest of the 1249-line decompile is a state machine over
UNMAPPED catcher fields (pad4/pad5/pad6/pad8/pad9/pad10 = timers, jump/fall
state, animation transitions). Those are NOT reconstructed here (would be guessing).

## Position model
- Logic pos: grid bytes `pos_x_1/pos_y_1/pos_z_1` (@catcher +0x31/32/33).
- Render pos: floats @+0x25/29/2d, set from grid via
  `set_entity_render_pos_from_grid((float)x, (float)z, -(float)y, 1)` (0x4429c0).
- Current cell = `this->level_manager_obj->tiles[pos_x_1][pos_y_1]`
  (cell = 127B; fields .z_pos/.type/.clip_rule/.pickup_type + pad1[] runtime state).

## Movement (grid stepping)
- Facing (1=TOP/2=RIGHT/3=DOWN/4=LEFT) -> a delta vector cached in
  `pad9[0x16]` (dx) / `pad9[0x17]` (dy). Step:
  `pos_x_1 += pad9[0x16]; pos_y_1 += pad9[0x17]; pos_z_1 += dz`.
- Landing height snaps to target cell `.z_pos`.
- Ladder / vertical: `pos_z_1 = z_pos+1` (up) or `pos_z_1 += 0xff` (-1, down).
- last_move_cmd (@+0x125) is the queued input; consumed here.

## Tile-type rules (target cell `.type`)
| type | notes.txt meaning | tick behavior (observed) |
|------|-------------------|--------------------------|
| 0x02 | glue/honeycomb | sticky handling (timestamp gate, anim 9) |
| 0x0e (14) | hole | fall-through path |
| 0x0f (15) | game-end / lose tile | triggers lose/teleport-away branch |
| 0x10 (16) | bush/honeycomb (non-walkable) | blocked / special |
| 0x11 (17) | (crashes per notes) | z-matched special handling |
| 0x15 (21) | non-textured clipped | checked (`==`/`!=`) as a gate |

Walkability also gated by `cell.clip_rule` (sticky/fall-through/clipped).

## Teleporter / cell links
- **CORRECTED**: teleport dest = source cell `pad1[0x4e]/[0x4f]` (pair id `pad1[0x4d]`).
  The earlier `pad1[0x23]/[0x24]` was WRONG — those are the moving-platform live grid
  coords (with `pad1[0x26]/[0x2a]/[0x2e]` world pos). See claude/gameplay_mechanics.md §6/§7
  for the full tile-cell runtime byte-map.

## Anim states set (SkippyCatcherAnim)
0 (idle/default), 3, 4, 8, 9 (5=fall, 10=ghost per enum elsewhere).

## For the SDL slice
This is enough to implement grid movement: read input -> set facing+delta ->
check target cell type/clip_rule/z_pos -> step or block -> handle
teleport(pad1[0x23/24]) / ladder(z±1) / hole(fall) -> project to render pos.
The pad-state (jump arcs, anim timing) can be approximated first, refined later.
