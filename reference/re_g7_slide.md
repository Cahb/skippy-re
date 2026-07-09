# SLIDE tile + "Slide" theme object (rutsche / chute) — RE findings

Source of truth: `skippy_tick` @ **0x438770** (full decompile read this session),
cross-checked against `reference/gameplay_mechanics.md` and the theme `.thm` files.

## TL;DR

- **SLIDE tile type byte = `0x10`.** This *is* the "Slide" theme object (slot 16,
  model stem `rutsche` = German "slide/chute"). Anim render-id **0x04 = "slide"**.
- The task brief's model ("slide is a third type, distinct from the 0x10
  redirect tile") does **not** match the binary. There is **no separate
  redirect tile**: `0x10` is a *forced-direction, forced-downward* slide, and it
  is the downward cousin of ice (`0x15`). I found exactly these tile-type
  comparisons in `skippy_tick`: `0x00,0x02,0x09,0x0c,0x0e,0x0f,0x10,0x11,0x15,
  0x16,0x17` — none is a horizontal-only "redirect".
- So: **ice = 0x15** (horizontal, keeps *your* momentum, anim 3);
  **slide = 0x10** (direction dictated by the tile, forces one step DOWN per
  cell, anim 4). "Like ice but downward-only, stacked back-to-back" = a run of
  0x10 tiles each one z-level lower.

---

## 1. The SLIDE tile (type 0x10) — exact rule

The slide branch runs only when the entity is resting on the tile
(`pos_z_1 == cell.z_pos`) and `move_turn_state == 0` (between hops). From the
decompile (field names per `found_structs_ghidra.h`):

```c
if (cell.type == 0x10) {                 /* current cell is a slide tile   */
    BVar17 = cell.slide_forced_dir;      /* cell +0x52: 1=TOP 2=RIGHT 3=DOWN 4=LEFT */
    if (slide_sound_enabled) { ...; sound_play(1); }   /* looping slide SFX (pad5[5]) */
    this->facing_direction     = slide_forced_dir;     /* face the chute dir       */
    this->pad6b[0xd]           = 1;      /* SLIDE-ACTIVE flag (drives dz + z lerp)  */
    this->pad6b[0xe..0x10]     = 0;
    this->buffered_move_command= slide_forced_dir;     /* auto-queue next hop      */
    this->move_interval        = <double 150.0>;       /* fixed slide interval*    */
                                         /* (low=0, high dword=0x4062C000)          */
} else {                                 /* first NON-slide tile ends the run       */
    this->move_interval = pad4b[0..1];   /* restore normal walk interval            */
    this->pad6b[0xd..0x10] = 0;          /* clear slide flag                        */
    if (slide_sound_enabled) sound_stop();
}
```

The `pad6b[0xd]` "slide-active" flag then does two things elsewhere in the tick:

1. **Forced downward step.** In the move-commit block:
   ```c
   this->move_dz = 0;
   ...
   if (this->pad6b[0xd] != 0) this->move_dz = -1;   /* step DOWN one z this hop */
   ```
   `slide_forced_dir` set `move_dx/move_dy`; the flag adds `dz = -1`. So each
   auto-hop moves one cell in the chute direction **and** one z down.

2. **Descending render interpolation.** In the render tail:
   ```c
   if (this->pad6b[0xd] != 0)
       this->pos_z = (pos_z_1 + 1) - (now - last_move_time)/move_interval;
   ```
   i.e. z slides smoothly from the old level down to the new one over the hop.

3. **Slide anim.** In the move-commit block, when *both* the source and the
   target cell are slide tiles:
   ```c
   if (src_type == 0x10 && dst_type == 0x10) this->current_anim_pos = 0x04; /* "slide" */
   ```
   (render-id 4 → clip slot 13 "slide" via `anim_renderid_to_clip` 0x401970).

**Auto-advance / termination.** Landing on a 0x10 tile re-arms
`buffered_move_command = cell.slide_forced_dir` every time, so the entity keeps
hopping cell-by-cell with no input. The run ends when:
- the next landing cell is **not** 0x10 (else-branch above clears the flag,
  restores walk interval, stops the loop SFX), or
- the next cell is out of bounds / void / blocked → normal fall / stop handling
  takes over (the generic gravity block, `pad7[0]=2` fall, etc.).

**Steering:** none. Direction is taken from the *tile* (`slide_forced_dir`) on
every landing and overwrites any queued input; `if (pad7[0]!=0) buffered_move_command=0`.
Because each cell carries its own forced dir, a chute can curve (turn corners)
while still forcing you down one z per cell.

**Who it applies to:** same code path for player and enemies (`entity_mode != 9`
gates only the death/reservation bookkeeping, not the slide itself).

\* The `move_interval = 150.0` write is verbatim from the binary
(`0x4062C000` as the high dword of the double). Flagged **low-confidence** on
*interpretation* — the value is odd for a "time to cross one cell" in seconds;
the *behavior* (forced dir + dz=-1 + descending lerp + anim 4) is solid. The
rewrite can use its normal hop duration; only the direction/dz/anim are load-bearing.

---

## 2. How SLIDE (0x10) differs from ICE (0x15) and everything else

| | ICE `0x15` | SLIDE `0x10` |
|---|---|---|
| Direction source | `stored_move_dir` — the entity's **own** entry momentum (kept for the whole run) | `cell.slide_forced_dir` (+0x52) — dictated by the **tile**, re-read each landing |
| Vertical | none (horizontal only) | **forced `dz = -1` every hop** (downward chute) |
| Anim (render-id) | `0x03` ("ice") | `0x04` ("slide") |
| Steer? | No (locked to entry dir) | No (locked to tile dir) |
| Interval | normal walk interval | fixed slide constant (see note above) |
| Loop SFX | `sound_play(1)` gated by `pad5[4]` | `sound_play(1)` gated by `pad5[5]` |
| Ends when | first non-ice tile / block | first non-0x10 tile / void / block |

ICE decompile for contrast:
```c
if (cell.type == 0x15) {
    this->buffered_move_command = this->stored_move_dir;  /* keep own momentum   */
    this->last_move_cmd = 0;
    this->current_anim_pos = 0x03;                        /* ice anim            */
    this->pad3a[8] = this->stored_move_dir;               /* remember run dir    */
}
```

Other nearby tile types (for disambiguation, all seen in `skippy_tick`):
`0x02` glue, `0x09` "on-special" (elevator/leaf ride), `0x0c` moving platform,
`0x0e` hole/fall, `0x0f` teleporter (`telep_dest_x/y`), `0x11` switch/plate
(`switch_group_id`), `0x16/0x17` stairs (`tile_type_is_stair`).

### Mapping to the existing rewrite (`src/sim/sim.c`)
The sim already has ice in `land_effects()` / `arrive_entity()`
(`TT_ICE`, `p->sliding`, `p->slide_dir`, `SIM_EV_SLIDE`). Slide is the same
loop with three deltas:

- Add `TT_SLIDE = 0x10`. In `land_effects`, when `tt == TT_SLIDE`:
  - set `p->slide_dir = cell.slide_forced_dir` (from the tile, **not** momentum),
  - `p->sliding = true`,
  - `step_entity(s, p, p->slide_dir)` like ice, but the step must also drop the
    entity one grid-z (`dz = -1`) — the target slide tile is one level lower, so
    if `step_entity` already snaps z to the destination cell's floor this is
    automatic; otherwise force `-1`.
  - emit `SIM_EV_SLIDE` and use the "slide" anim (clip 13 / render-id 4) instead
    of the ice anim (clip 10 / render-id 3).
- End the run on the first non-slide tile exactly like ice (`p->sliding=false`).
- You need `slide_forced_dir` in the cell struct (already parsed — see below).

---

## 3. Cell field & anim references

- **`slide_forced_dir`** = tile cell byte **+0x52** (already named in
  `found_structs_ghidra.h` / `gameplay_mechanics.md` §"cell layout": *"forced
  dir on slide/redirect tile (0x10)"*). Values 1..4 (TOP/RIGHT/DOWN/LEFT).
- **Slide-active runtime flag** = catcher `pad6b[0xd]` (int) — not yet a named
  field; drives `move_dz=-1` and the descending z-lerp.
- **Anim**: `current_anim_pos = 0x04` (render-id) → `anim_renderid_to_clip`
  (0x401970) → SkippyCatcher clip slot **13 = "slide"**.

---

## 4. Theme "Slide" object (slot 16) — render spec

Slot 16 in the 38-entry theme object array (`theme_mgr + 0x104 + 16*0x2ef0`),
keyword `Slide`. Model stem is **`rutsche`** (German "slide/chute"). Extracted
from `game_root/SkippyAdventure/Themes/*.thm`:

| theme | model(s) | texture(s) | modifiers |
|-------|----------|------------|-----------|
| **Candy** | `models\candy\rutsche.MDL` | `textures\candy\rutsche64.tga` + `textures\reflect_mittel32.tga` | 2nd tex = `Environment` env-map, additive `SrcBlend One / DestBlend One` reflection layer |
| **Egypt** | `models\egypt\rutsche.MDL` | `textures\egypt\treppe.tga` | none (reuses the "treppe"=stairs texture) |
| **Space** | `models\space\rutsche01.MDL` (×2) + `models\space\rutsche01_innen.MDL` | slot1: `space\rutsche64.tga`; slot2: `space\rutsche_glow64.tga`; slot3: `space\rutsche64.tga` + `reflect_stark32.tga` | slot2 = `Lit`, glow tex `Pulse 0.001`, additive `One/One`; slot3 (`_innen` = inner surface) = `Environment` strong env-map, additive `One/One` |
| **Forest** | — | — | empty: `//not in forest` |
| **back_Forest** | — | — | empty: `//not in forest` |
| **Castle** | — | — | empty: `//not in castle` |
| **Water** | — | — | empty: `//` |

Notes for the loader/renderer:
- A theme object can hold **multiple `Model {...}` sub-meshes** (Space uses 3 —
  base, additive pulsing glow, env-mapped inner). Draw them in order, stacked.
- Modifiers observed here: **`Environment`** (spherical env-map reflection),
  **`Lit`**, **`Pulse <rate>`** (animated intensity), and additive blending via
  **`SrcBlend One` / `DestBlend One`**. These map to the sub-mesh FX flags /
  texture blend fields already documented in `theme_object_slots.md` /
  `theme_format.md`.
- The `Slide` block has **no** position/scale/rotate or `NoMoveStates`
  directives in any theme — mesh is placed by tile position only.
- Only Candy / Egypt / Space ship slide tiles; the other four themes leave the
  slot empty, so a level in those themes will never place a 0x10 tile.
