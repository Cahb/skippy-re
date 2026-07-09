# G4 — Player movement timing & motion curves (RE, verified)

Reverse-engineered from `RUS_Karoo.exe` via Ghidra + raw `.rdata` reads
(file offset = VA − 0x400000). Everything below is read straight from the binary
unless flagged as a hypothesis. Authority = `skippy_tick` @0x438770 (per-entity per-frame),
`skippy_constructor` @0x41f900, `spawn_enemy` @0x4172d0, `skippy_pickup_effect_apply` @0x41fcb0,
input shims `skippy_move_forward`/`_backward`/`_turn_left`/`_turn_right` @0x41fa90..0x41fc90.

Time base: `*(double*)this->pad1` = current sim time in **milliseconds**; `*(double*)this->pad2`
= frame dt (ms). `move_interval` (@catcher+0x132, double) is therefore **milliseconds per cell**.

---

## 1. MOVE interval (ms per cell) — VERIFIED

`move_interval` is loaded each idle frame from the entity's default copy
`pad4b[0]/[1]` (= `default_move_interval` @+0x66, a double). Defaults:

| entity | set by | raw hi-dword | **ms/cell** |
|--------|--------|--------------|-------------|
| **Player** | `skippy_constructor` | `0x40690000` | **200.0** |
| Catcher (mode 2) | `spawn_enemy` param_5==2 | `0x407f4000` | **500.0** |
| Thrower (mode 3) | `spawn_enemy` param_5==3 | `0x4085e000` | **700.0** |
| Speed pickup (0x0a) | `skippy_pickup_effect_apply` | `0x40590000` | **100.0** (0.5×) |
| Slow pickup (0x0c) | `skippy_pickup_effect_apply` | `0x40790000` | **400.0** (2×) |
| Redirect/forced tile (0x10) | `skippy_tick` | `0x4062c000` | **150.0** (fast auto-slide) |

- Speed/slow **override** the default while active, then restore `0x40690000` (=200) on expiry.
- Effect duration (speed/slow/inverse/protection) = `_DAT_0045d430` = **10000 ms** (10 s).
  Freeze uses `_DAT_0045d2d8` = **5000 ms** (5 s).
- Ratios: catcher = 2.5× player, thrower = 3.5× player. So `ENEMY_SLOW=2.5` and the extra
  thrower `×1.4` (2.5×1.4 = 3.5) are already exactly right **once the player base is 200 ms**.

## 2. TURN duration — VERIFIED (= move_interval)

There is **no separate turn timer**. A turn is a `move_turn_state` cycle gated by the same
`if (move_interval <= now − last_move_time)` completion test, so:

> **Player turn = 200 ms** (same as a hop). Enemy turns = their move_interval (catcher 500, thrower 700).

Mechanism: `skippy_turn_right/left` set `buffered_move_command = rotate_direction(facing,dir) + 10`
(→ values **11..14**) and `last_move_cmd = 2` (right) / `4` (left). On consume, 11..14 do NOT match
the `state==1..4` delta assignment, so `move_dx=move_dy=move_dz=0` — **a turn commits no grid step**.
At completion `facing_direction = state − 10` (player has `pad8[0]==1`), i.e. reorient only.
The mesh plays `current_anim_pos` 0x1e (turn R) / 0x1f (turn L) for the duration.

## 3. Horizontal hop curve — LINEAR (no easing) — VERIFIED

Grid cell is committed **at move start** (`pos_x_1 += move_dx; pos_y_1 += move_dy; pos_z_1 += move_dz`),
then render pos interpolates old→new linearly. `frac = (now − last_move_time)/move_interval`:

```
state 1 (−Y): pos_x = (pos_y_1 + 1) − frac
state 2 (+X): pos_y = (pos_x_1 − 1) + frac
state 3 (+Y): pos_x = (pos_y_1 − 1) + frac
state 4 (−X): pos_y = (pos_x_1 + 1) − frac
```

Pure linear in time. **No accel/decel, no smoothstep.** (Render axis map: render-X←gridY, render-Z←−gridX.)

## 4. Vertical (z) motion — VERIFIED

### 4a. Flat walk = NO positional arc
For a normal flat step (`current_anim_pos` 0x14/0x15) the player's `pos_z` is **not touched** by the
hop block — the visible "jump" of Jumpin' John is entirely in the **walk vertex-morph animation clip**,
not the entity's z. (The z-parabola in the hop block runs only for `entity_mode==9`, the death/angel actor.)

### 4b. Stair transitions = LINEAR z-ramps (not parabolas)
With base `fVar24 = _DAT_0045d390 = 0.0`, `f = (now−last_move_time)/move_interval`,
amp `_DAT_0045d6e0 = 0.5`, amp2 `_DAT_0045d6a8 = 1.5`, unit `_DAT_0045d2e8 = 1.0`:

| current_anim_pos | pos_z − z1 | goes |
|---|---|---|
| 0x16 field-stair-up | `+0.5·f` | 0 → +0.5 |
| 0x17 field-stair-down | `+1.0 − 0.5·f` | +1.0 → +0.5 |
| 0x18 stair-stair-up | `−0.5 + 1.0·f` | −0.5 → +0.5 |
| 0x1a stair-up variant | `−0.5 + 0.5·f` | −0.5 → 0 |
| 0x1b stair-field | `+0.5 − 0.5·f` | +0.5 → 0 |
| 0x19 stair-down variant | `+1.5 − 1.0·f` | +1.5 → +0.5 |

(z1 = the already-committed integer target grid z. Stair surfaces render at `z_pos + 0.5`.)
The rewrite's linear `rz = zf + (zt−zf)·t` for steps is a faithful match of these ramps.

### 4c. Death-hop parabola (entity_mode 9 only) — for reference
`A = 1.0/(move_interval·0.0005)`, `dVar1 = move_interval·0.0005`, `u = (now−last_move_time)·0.001` (s):
```
pos_z = z1 + A·u − 0.5·(A/dVar1)·u²   ⟹  pos_z = z1 + 2·f·(1−f)   (f = frac)
```
Clean symmetric arc, **peak +0.5 at f=0.5**. This is the rising-angel bounce, NOT the normal walk.

### 4d. Fall / launch (already in gameplay_mechanics.md, unchanged)
Fall v0 = −3.0, ½g_fall = 5.405 (`_DAT_0045d6c4`); settle 0.004/ms; jump-pad v0 = √((Δh+7.2)·19.62),
½g = 4.905. Drop threshold `_DAT_0045d3bc` = 2.0 cells. All verified.

## 5. Why it feels less "stiff" — the smoothness machinery — VERIFIED

Three things the original does that the current rewrite does not:

1. **Same-tick chaining (zero inter-hop gap).** The input shims (`skippy_move_forward`, etc.) set
   `buffered_move_command` and then **call `skippy_tick` themselves**. Inside `skippy_tick`, when a
   move completes it sets `move_turn_state=0` and falls through (`goto LAB_00438b45`) to the
   buffered-command consumer **in the same call** → the next hop starts on the exact frame the previous
   ends, no dropped frame.

2. **Chained-hop back-dating (phase-lock).** On starting a hop:
   ```
   gap = now − pad3a[6]           // pad3a[6] = previous move's scheduled end = last_move_time+move_interval
   if (gap <= _DAT_0045d390(=0) || gap >= *(pad3a+4)(=20ms))  last_move_time = now      // clean restart
   else                                                        last_move_time = pad3a[6] // BACK-DATE
   ```
   For a held direction the gap is a small positive frame-jitter (< 20 ms), so `last_move_time` is
   back-dated to the previous hop's ideal end — the leftover dt is **carried forward** and the cadence
   stays locked to exact multiples of `move_interval` (no per-hop drift/micro-stutter).

3. **One-deep input buffer (`pad3b`).** A turn pressed mid-move is stashed in `pad3b[0]/[1]` and
   re-issued as `buffered_move_command` on completion (player = `entity_mode==4` restore block). Lets you
   pre-queue the next turn/step without frame-perfect timing.

There is **no dwell/cooldown between hops** — holding a direction is seamless back-to-back motion.
(1500 ms `_DAT_0045d2e0` dwell = moving-platform end-pause; 5000 ms `_DAT_0045d2d8` = idle→idle-anim delay;
neither gates walking.)

---

## TO MATCH — concrete edits to `src/sim/sim.c`

- **MOVE_DUR: `0.22f` → `0.20f`** (200 ms/cell; current 220 ms is ~10% too slow). *(line ~12)*
- **TURN_DUR: keep `0.20f`** — already 200 ms, correct (turn = move_interval in the original).
- **Curve: keep linear** — `rx/ry` lerp and `rz` step-lerp already match the RE exactly. Do NOT add easing.
- **Vertical: keep flat-walk with no arc** — the hop lives in the animation clip. Stair z-lerp is correct.
- **Speed/slow: keep 0.5×/2×** (verified 100/400 ms). But **speed_t should be 10 s, not 5 s**
  (`_DAT_0045d430 = 10000`; slow/inverse/protect already 10 s; enemy_freeze 5 s is correct). *(line ~497)*
- **Thrower turn**: `tdur` is not scaled by the thrower ×1.4 — original thrower turn = 700 ms, rewrite
  gives 500 ms. Apply the same `×1.4` to `tdur` for throwers to be exact. *(minor; line ~1084)*
- **W = buffering + carry-over (the real anti-stiffness fix):**
  - In `tick_entity`'s moving branch, when `move_t >= 1.0`: after `arrive_entity`, if a move is queued
    (held key / buffered command), **start the next move with `move_t = move_t − 1.0`** (carry the
    overshoot) instead of resetting to 0 — mirrors the `pad3a[6]` back-dating within the 20 ms window.
  - Add a **one-deep input buffer**: let `step_entity`/`turn_entity` stash a command while `p->moving`
    and have `arrive_entity` consume it immediately (same-tick chaining). This removes the 1-frame gap
    between hops when a direction is held and lets turns/steps be pre-queued.
  - Ensure `main` re-issues held-direction each frame (or relies on the buffer) so chaining is continuous.

## Constants used (verified from .rdata)
| VA | value | role |
|----|-------|------|
| 0x45d2d8 | 5000.0 (d) | idle→idle-anim delay; also freeze duration (5 s) |
| 0x45d2e0 | 1500.0 (d) | mover end-of-run dwell |
| 0x45d2e8 | 1.0 (d) | hop unit |
| 0x45d308 | 0.001 (f) | ms→s |
| 0x45d318 | 0.5 (f) | stair half-height / land-snap |
| 0x45d368 | 0.001 (d) | ms→s for death-hop parabola |
| 0x45d390 | 0.0 (d) | hop base; back-date lower bound |
| 0x45d3bc | 2.0 (f) | fall drop threshold (cells) |
| 0x45d430 | 10000.0 (d) | speed/slow/inverse/protection duration |
| 0x45d6a8 | 1.5 (d) | stair-hop amp (0x19) |
| 0x45d6b0 | 0.0005 (d) | interval→hop scale (death-hop) |
| 0x45d6d8 | 3000.0 (d) | glue stick duration (3 s) |
| 0x45d6e0 | 0.5 (d) | hop/stair amplitude |
| 0x45d6e8 | 500.0 (d) | teleporter cooldown |
| pad3a+4 | 20.0 (d) | chained-hop back-date window (ms) |

Hypotheses flagged: `entity_mode` map (2=catcher, 3=thrower, 4=player, 9=death/angel) is inferred from
usage in `skippy_tick`/`spawn_enemy`/`skippy_pickup_effect_apply` and is consistent across all three, but
not independently confirmed by a debug string.
