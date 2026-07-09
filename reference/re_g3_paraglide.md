# Paraglide mechanic — RE spec (ground truth from RUS_Karoo.exe)

Reverse-engineered from `skippy_tick` (0x438770) and `skippy_pickup_effect_apply`
(0x41fcb0). This corrects the current rewrite: the original paraglide is a
**player-steered, grid-stepping mid-air descent**, NOT an auto forward-drift.

## TL;DR (deploy + control model)
- **Charge**: the Paraglide pickup (pickup byte **0x05**) increments a counter at
  catcher **+0xe9** (currently mis-named `bounce_count`; it is the **paraglide charge**).
- **Deploy**: AUTOMATIC. While free-falling, the *instant* the drop exceeds **2 cells**
  and a charge exists, one charge is spent and the glide-active flag **+0xea**
  (mis-named `airborne_flag`) is set to 1. No key press deploys it.
- **Control**: while glide-active, player input is **preserved** (free-fall normally
  wipes it every tick). The player steers by the normal move/turn keys and takes
  **discrete grid steps** through the air at walking speed — turn to change facing,
  forward/back to move one cell along facing. This is the "mid-air directional
  control until touching ground" the user described.
- **Descent**: constant slow sink `pos_z -= dt_ms * 0.004` (≈ **4.0 u/s**), toward the
  CURRENT cell's floor z. Not gravity.
- **End**: touching any solid tile ends the glide and — crucially — **skips the
  fall-damage/death branch**, so a glided landing is always survivable. Gliding off
  the map edge (grid z < ~2, or off-bounds) drops back into a lethal free fall.

---

## The two field names to fix
| offset | current name (found_structs_ghidra.h) | TRUE role |
|--------|----------------------------------------|-----------|
| +0xe9 (BYTE) | `bounce_count` | **paraglide_charges** — # of Paraglide pickups held; consumed 1 per glide |
| +0xea (DWORD) | `airborne_flag` | **glide_active** — 0 = free-fall parabola, 1 = paraglide descent in progress |

`is_falling` (+0x120) = airborne at all; `fall_velocity` (+0x5c); `arc_start_z`
(+0x111) = height the fall began (used for drop-depth); `buffered_move_command`
(+0x145) / `last_move_cmd` (+0x125) = queued input.

---

## 1. Charge acquisition — `skippy_pickup_effect_apply` @0x41fcb0
Standing on a cell whose `pickup_type == 0x05`, while **not** falling:
```c
if (tiles[x][y].pickup_type == 0x05 && this->is_falling == 0) {
    this->bounce_count += 1;               // +0xe9  == paraglide_charges++
    tiles[x][y].pickup_type = 0;           // consume the pickup
    /* score/sfx */
    this->last_pickup_type = 0x05;
}
```
So the pickup is a **stackable count** (`inv[5]` in the rewrite), not a timed buff.
(Contrast: pickups 0x08/0x0a/0x0b/0x0c/0x0d are the timed power-ups with duration
timers at the bottom of the same function; paraglide is not one of them.)

## 2. Deploy trigger — inside the free-fall arc in `skippy_tick`
Fall setup (when a walkable cell becomes empty and `is_falling==0`, `pad5[0]`==not-on-leaf):
```c
this->fall_velocity = -3.0;                // 0x45d6c4 half-g = 5.405
this->arc_start_z   = this->pos_z_1;       // launch grid height
this->is_falling    = 1;
this->pos_z         = (float)this->pos_z_1;
ride_start_time     = now_ms;              // pad1
this->buffered_move_command = 0;
```
Then each tick while `is_falling && airborne_flag(+0xea)==0` (FREE FALL):
```c
tau = elapsed_ms * 0.001;                                  // 0x45d308
this->pos_z = (fall_velocity - tau*5.405)*tau + arc_start_z;   // parabola
this->pos_z_1 = floor(pos_z) + 1;

/* ---- PARAGLIDE DEPLOY (automatic) ---- */
if (airborne_flag == 0
    && 2 < (int)(arc_start_z - pos_z_1)     // fell MORE THAN 2 cells
    && this->bounce_count != 0) {           // have a charge
        this->bounce_count -= 1;            // spend one charge (0xe9 += 0xff)
        this->airborne_flag = 1;            // glide-active ON
        /* deploy sfx: pad5[2], pad5[0xb] */
}
```
Note the `2.0f` float form (`0x45d3bc`) is also tested a line later to force the
paraglide anim on the same tick. **Deploy is purely fall-depth + charge gated — no
input.** The current rewrite already gets THIS part right (`GLIDE_TRIGGER 2.0`,
`inv[5]--`).

## 3. Control — the part the rewrite is missing
### 3a. Input gating (top of `skippy_tick`)
```c
if (this->is_falling != 0 && this->airborne_flag == 0) {   // FREE FALL only
    this->buffered_move_command = 0;
    this->last_move_cmd = 0;                                // wipe queued input
}
```
During **free fall** the queued move/turn is erased every tick → no steering.
During **glide** (`airborne_flag != 0`) this block is skipped → **input survives**.
(The free-fall branch additionally clears any forward/back command that lies on the
facing axis; the glide branch never touches it.)

### 3b. The glide grid-step (move-commit block, runs whenever `move_turn_state==0`)
Because input is preserved, a held direction becomes a real move:
```c
if (this->move_turn_state == 0 && this->buffered_move_command != 0) {
    this->move_turn_state = buffered_move_command; ... decode dx/dy/dz ...
    /* same bounds / wall / occupancy checks as ground movement */
    ...
    if (this->airborne_flag == 0) {          // ground turn anims
        if (last_move_cmd==2) current_anim_pos = 0x1e;   // turn R
        if (last_move_cmd==4) current_anim_pos = 0x1f;   // turn L
    } else {
        current_anim_pos = 0x05;             // PARAGLIDE anim during a glide-step
    }
}
```
`move_interval` (+0x132) is never overridden during glide, so horizontal speed =
**normal walking speed, one grid cell per hop**. Turning changes `facing_direction`;
forward/back steps one cell along facing. The render interpolation (switch on
`move_turn_state`) lerps the cell-to-cell motion exactly like a ground walk.

So: the player **steers cell-by-cell through the air** (discrete grid steps, not free
continuous drift), while sinking. There is **no automatic forward drift** — no input =
descend in place.

## 4. Descent physics (glide-active branch)
```c
// airborne_flag != 0
zt = tiles[x][y].z_pos;                          // current cell floor
if ((float)zt < pos_z || pos_z < zt - 0.5) {     // 0.5 = 0x45d318
    this->pos_z -= (float)dt_ms * 0.004;         // 0x45d6c8 — CONSTANT slow sink
    this->pos_z_1 = floor(pos_z) + 1;
} else {                                          // reached floor band
    this->pos_z_1 = zt;
    this->pos_z   = (float)zt;
}
if ((char)pos_z_1 < 2) this->airborne_flag = 0;  // near ground → drop out of glide
current_anim_pos = 0x05;                          // paraglide (render-id 5)
```
Descent rate = `dt_ms * 0.004` = `dt_sec * 4.0` ⇒ **~4.0 units/second**, constant
(gravity is NOT applied while gliding). The sink target tracks the CURRENT cell, so
grid-stepping onto a higher platform mid-glide lets you land there.

## 5. End condition & safe landing (the "landed" branch of `skippy_tick`)
When the entity's cell resolves onto a solid tile at/below its z:
```c
if (this->is_falling != 0) {                     // just landed
    ... clear ride/occupancy state ...
    if (this->airborne_flag == 0) {              // FREE-FALL landing ONLY
        // fall-damage: drop >=3 cells => pad7[0]=2 (DEATH). small drop => safe.
    }
    // (glide skips the whole damage block above)
    this->airborne_flag = 0;
    this->current_anim_pos = 0;
    this->is_falling = 0;
    ... snap pos_x/pos_y/pos_z to the cell ...
}
```
Because the fall-damage test is inside `if (airborne_flag == 0)`, a glided landing
(airborne_flag==1) is **always survivable regardless of height**. Free fall of ≥3
cells without a glide kills. Steering the glide off the map edge / over a bottomless
column (pos_z_1 goes < 0, or airborne cleared at z<2 over void) resumes a lethal free
fall (`pad7[0]=2`). So the glide guarantees a safe landing *only if you steer onto a
tile*.

---

## Constants (from .rdata, VA = file off + 0x400000)
| VA | value | role |
|----|-------|------|
| 0x45d6c4 | 5.405 (f) | ½ g_fall (free-fall parabola) |
| — literal | -3.0 | fall v0 seed |
| 0x45d3bc | 2.0 (f) | drop-depth glide trigger (also `2 <` integer test) |
| 0x45d6c8 | 0.004 (f) | glide sink per ms of dt → ~4.0 u/s |
| 0x45d318 | 0.5 (f) | floor-snap band |
| 0x45d308 | 0.001 (f) | ms→s |

---

## How this maps to / contradicts the current rewrite (`src/sim/sim.c`)

Current code (lines ~1097-1126), fields `p->gliding`, `s->inv[5]`,
`GLIDE_TRIGGER/DESCENT/FWD`:

| aspect | current rewrite | original (RE) | verdict |
|--------|-----------------|---------------|---------|
| charge = `inv[5]`, from pickup 5 | yes | yes (+0xe9) | ✅ correct |
| deploy = fall-depth > 2 && charge, auto | yes (`GLIDE_TRIGGER 2.0`, `inv[5]--`) | yes (`2 < arc_start_z-pos_z_1`) | ✅ correct |
| horizontal motion | **auto forward-drift** `rx += fwd*GLIDE_FWD*dt` (2.2 cells/s), continuous, uncontrollable | **player-steered discrete grid steps** at walk speed; turn+forward; no auto drift | ❌ WRONG — replace |
| descent rate | `GLIDE_DESCENT 1.3` u/s | `dt_ms*0.004` ≈ **4.0 u/s** | ❌ tune to ~4.0 |
| safe landing on any floor | yes (`falling=gliding=false`) | yes (damage branch skipped) | ✅ correct |
| off-map → resume fall | yes | yes (airborne cleared / z<2) | ✅ correct |

### What to change in the rewrite
1. **Delete the auto forward-drift** (`GLIDE_FWD`, the `p->rx/ry += fwd_* ...` lines).
   During glide, run the *normal* input→buffered-move→grid-step path that ground
   movement uses, but keep the vertical sink separate. The player must actively press
   forward/turn to move; each press = one interpolated grid hop at walking `move_dur`.
2. **Preserve input while gliding.** Wherever the rewrite discards queued
   input/steering during `p->falling`, gate that on `!p->gliding` (mirror
   `is_falling && airborne_flag==0`).
3. **Retarget the sink to the current cell each step** and bump `GLIDE_DESCENT` to
   ~4.0 u/s (dt_ms × 0.004). Snap when within 0.5 of the cell floor.
4. Deploy trigger and charge spend are already correct — leave them.

### Function / address index
- `skippy_tick` @ **0x438770** — deploy, control gating, glide descent, landing.
- `skippy_pickup_effect_apply` @ **0x41fcb0** — paraglide charge collection (pickup 0x05).
- Paraglide anim = render-id **0x05** (`current_anim_pos`), maps via
  `anim_renderid_to_clip` (0x401970) to clip slot 12 "paraglide".
