# `main_render_func` @ `0x00426F50` — Findings & Notes

Companion to [`main_render_reconstructed.cpp`](./main_render_reconstructed.cpp).
This is my (Claude's) own read of the function — independent of the IDA-dump
naming, which I used only for orientation. Treat the `sub_*` intent guesses as
hypotheses, not facts.

---

## TL;DR — what this function is

It's the **entire per-frame loop** of a late-90s/early-2000s **Direct3D Immediate
Mode (D3D IM 3, over DirectDraw)** game. One function does: timing → simulation
tick → camera → audio listener → clear → begin scene → skybox → opaque world →
shadows → transparent FX → 2D HUD → overlays → end scene → flip.

**Why it's ~181 KB / 5563 lines:** it is *not* algorithmically deep. It's a
**fixed-function pipeline with everything unrolled inline** — every render state
is poked one-at-a-time via `SetRenderState`, and every object *category* (tiles,
glue, stairs, player, exit, ~10 pickup types, destructibles, 4 enemy types,
their shadows, particle/FX families, ~8 HUD elements) has its own hand-written
draw block instead of a data-driven loop. ~600 of the 5563 lines are just local
variable declarations spilled by the decompiler.

---

## Pipeline phases (verified against the dump)

| # | Phase | Lines (raw) | Key calls |
|---|-------|-------------|-----------|
| 0 | Level-load gate / early return | 599–604 | `sub_426C50` |
| 1 | Frame timing + sim tick | 605–628 | `sub_404040`, `sub_404120` (player), `sub_404300` (enemies) |
| 1a| Camera (scripted vs gameplay) | 629–750 | manual LookAt vs `camera_tick_set_transform` |
| 1b| 3D audio listener update | 751–865 | `sub_4453B0/E0/A0` |
| 2 | Clear viewport + `BeginScene` | 867–902 | viewport `Clear` (vtbl[1]), `BeginScene` |
| 3 | Baseline render state + skybox | 903–975 | `SetRenderState` ×N, `Bridge_RenderSkybox` |
| 4 | Opaque world geometry | 976–1463 | `game_draw_tile_objects_in_world`, `game_draw_dynamic_3d_object_in_world` |
| 4a| Pickups (grid switch) | 1258–1360 | `switch(pickup_type)` |
| 4c| Enemies | 1464–1582 | `game_draw_dynamic_3d_object_in_world`, `sub_448350/360` |
| 5 | Shadow pass | 1632–~2050 | `draw_shadows_sub_43B790` |
| 6 | Transparent / FX passes | ~2050–4300 | `DrawPrimitive(TRIANGLESTRIP)`, per-instance `SetTransform(WORLD)` |
| 7 | HUD / 2D text | 4310–4463 | `sprintf` + `sub_413690/413D00/413E30` |
| 8 | Full-screen overlays | 4827–4870 | `sub_42E000`, `sub_435420`, `sub_434F90` |
| 9 | `EndScene` + `Flip` | 4871–4893 | `EndScene`, `Surface4::Flip` |

---

## Decoded details

### The "magic floats" are screen-fraction coordinates
The HUD math like `x * 0.0015625` is just `x / 640`, and `y * 0.033333` is
`y / 30` (cell grid) — pixel positions normalized into device/UV space. So
`490 * v326 * 0.0015625` reads as "pixel column 490 (scaled by glyph cell
`v326`) expressed as a 0..1 fraction of a 640-wide screen." There is **no exotic
vertex math in the HUD** — it's all `pixel → [0,1]` normalization.

Common constants seen:
- `0.0015625` = 1/640 (horizontal)
- `0.025`     = 1/40, `0.033333` = 1/30 (cell/row pitch)
- `0.0093750` = 6/640
- `0.96041667`≈ 614.6/640 (right-aligned FPS readout)

### The float math that *is* real: the scripted-camera LookAt
Phase 1a builds a VIEW matrix by hand (only on `script_camera_active`). It's a
textbook Gram-Schmidt LookAt:
```
forward = normalize(target - eye)
right   = normalize(cross(up, forward))
up'     = normalize(cross(forward, right))
view    = [ right.x up'.x forward.x 0
            right.y up'.y forward.y 0
            right.z up'.z forward.z 0
           -dot(right,eye) -dot(up',eye) -dot(forward,eye) 1 ]
```
- `sub_403770` / `sub_403830` = cross product
- `sub_4037E0` = squared length / dot (used as `sqrt(sub_4037E0(v))` to get length)
- `sub_407F70` = normalize
- `sub_403810` = dot (the translation row `-dot(axis,eye)`)
- The `acos(...)`/`sqrt(...)` block computes yaw+pitch stored in
  `dword_46C4B8` (yaw) / `dword_46C4BC` (pitch), reused for the audio listener.

### The two nested 4×4 loops (phase 1b) = matrix multiply
Not vertex skinning — it's `mat4 * mat4` (and a `mat4 * vec4`) to compose the
listener orientation from a yaw and a pitch rotation. The `if (w != 1.0) v /= w`
afterward is a perspective de-homogenize.

### `SetRenderState(52..57, …)` — IDA mis-named these
IDA decoded the numeric state IDs as ORed `D3DRENDERSTATE_*` flag names
(`RANGEFOGENABLE|TEXTUREPERSPECTIVE`, etc.). In DX5/DX6 IM, **52–57 are the
texture-stage filter/address states**, not fog. Effective meaning:
trilinear filtering + UV wrap. So those blocks = "configure the sampler",
toggled off for skybox/HUD and on for textured world geometry.

### Enemy & theme indexing
- `byte_4DC7C8[29 * enemyIndex]` is the enemy **type** byte; values 2 and 3 map
  to `themes[1]/[3]` (and `[2]/[4]` for a sub-variant). Stride 29 = per-enemy
  record size in that side table.
- `pad31[i]` = enemy **draw order / index list**; `catchers[pad31[i]]` is the
  actual entity. ("catcher" = your name for the shared entity base.)
- A per-enemy flag (`+122` / `+130`) selects an **alternate animation** (active
  vs idle); `sub_448350` vs `sub_448360` advance the respective anim.

### HUD contents (confirmed via format strings)
- `"%02.0f:%02.0f;%d"` → level timer **mm:ss;cc** (separator is `;` before
  centiseconds — verified in binary, see WHITEBOX doc); turns **red ≤ 10 s** left.
- `"%d"`   → hearts (`skippy.num_of_hearts_hud`)
- `"%d/%d"`→ crystals **collected / needed**
- `"%d"`  → current **level number** (`pad25[11014] + 1`)
- `"%dx"` / `"%d"` → two more counters (`catcher.pad6[14]/[15]` — lives? ammo?)
- `"%.1f fps"` → FPS overlay, **only while VK_F1 (0x70) held**.

### Present path
Normal frames `Flip` the DirectDraw back buffer. `game_state == 7` is a special
"static screen" path that blits a background bitmap instead of flipping —
likely the loading/exit screen.

---

## Open questions / TODO for future passes

1. **Unidentified draw passes:** `sub_420F50`, `sub_408870`, `sub_408920`,
   `sub_408A00`. They sit between opaque world and shadows — candidates:
   projectiles, bridges, teleporters, water. Worth dumping each.
2. **The 2050–4300 middle** is the least-mapped. It's clearly repeated
   "translucent billboard family" blocks (rotation matrices via cos/sin +
   `DrawPrimitive(TRIANGLESTRIP)`), but the *specific* effects (which pickup
   glows, teleporter swirl, ice sheen) aren't separated yet.
3. **`game_state` enum** — observed values: `0,1,2,3,5,6,7`. Inferred:
   `1`=playing (timer counts down), `2`=level-complete/end screen,
   `7`=loading/exit. `0/5` share the fade-quad + stats overlay (menu/paused?).
   Confirming this enum would unlock a lot of the state-gated branches.
4. **`pad6[14]/[15]`** counters in the HUD — need the skippy/catcher struct
   offsets to name them (lives, bombs, keys?).
5. Confirm `sub_403770` vs `sub_403830` are genuinely two cross-product
   variants (arg order) vs one being something else.

---

## How I'd verify any of this (cheap experiments)

- **Theme index map:** the proxy already hooks D3D. Log every
  `game_draw_dynamic_3d_object_in_world` call with its `theme*` arg and the
  bound texture name → builds a definitive theme→object table without guessing.
- **State enum:** log `game->game_state` once per frame in the proxy; correlate
  with on-screen events (menu, death, level end).
- **Sampler states:** confirm 52–57 by logging the actual `(state, value)` pairs
  passed to `SetRenderState` and comparing against the DX6 `d3dtypes.h` enum.
