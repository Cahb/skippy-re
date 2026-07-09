# .thm coverage map — mapped vs. not-mapped

Goal: every object's appearance/behaviour should come from the `.thm` (data-driven),
not from out-of-nowhere handwritten constants in the renderer. This tracks what we
currently parse, what we actually apply, and where we still hardcode. The renderer is
ours (raylib), so a few directives need our own implementation driven by the flag — that
hand-work is fine; inventing values that the `.thm` already specifies is not.

Legend: ✅ parsed **and** applied · 🟡 parsed but **ignored** (dead) · ❌ **not parsed**
(behaviour hardcoded or missing).

## Target architecture — the engine is a generic modifier factory (verified via RE)
`SkippyThemeObject` is **not** type-special: it's a `submesh[]` array where each submesh is
one of `{Model, Field, Billboard, ParticleSystem}` carrying a transform (Position/scale/
rotate), an `anim_clips[]` table, and a `textures[]` list where every texture holds its own
`Condition`, `Src/DestBlend`, `TextureAdress`, and flags (`Specular/RandomYAngle/Oscillate/
Pump/NoZWrite/NoShadow/...`). The renderer **walks submeshes and applies whatever each
declares** — "Exit"/"Teleporter"/"JumpPad" are just slot names whose theme entry composes
those components + modifiers. So the end-state is ONE `themed_object` (components with the
full modifier/texture-state set) + ONE renderer, no per-type branches.

**Strategy = (A) converge incrementally:** implement each remaining effect as a generic
component *technique* and fold the bespoke paths (`crystal`/`glue`/`exit`/`tile_tex`/
`tile_glow`) onto the unified model as we touch them. Once coverage hits ~100%, the clean
(B) consolidation rewrite is trivial (all the pieces exist and are proven). `obj_render`/
`draw_obj` is the seed of the unified renderer.

## Structural / assets
| keyword | status | notes |
|---|---|---|
| Object slot names | ✅ | `thm_slot_for` → `thm_object.slot` |
| `Model <mdl> [ani] [NoMoveStates]` | ✅ mesh, 🟡 `NoMoveStates` | mesh+anim loaded; `nomovestates` parsed, no consumer |
| `Field` | ✅ | engine quad (Plate/Side/glue) |
| `Texture <tga> [Alpha]` | ✅ tex[0] | **only `tex[0]` used by most loaders**; `tex[1..]` parsed but ignored except glue/John |
| `Condition Active/InActive/Dead/Alive` | ✅ | glue fresh/spent, John alive/dead |
| `ParticleSystem <par> {Texture,Position}` | ✅ | Step 2 generic emitter (`6766645`); `thm_particle.pos` 🟡 (we use the .par's own point) |
| `Sky/HUD/Radar/HUDTextColors/SideHeight/Sound` | ✅ | environment block |

## Materials / render-state (per Texture)
| keyword | status | notes |
|---|---|---|
| `SrcBlend` | 🟡 partial | only `== "one"` tested (additive detect); value not generally applied |
| `DestBlend` | 🟡 | parsed, no consumer |
| `Alpha` | ✅ | transparent-pass detection |
| `Wobble` | ✅ | glue UV warp |
| `NoZWrite` | 🟡 | parsed, no consumer (we choose depth-write per pass by hand) |
| `NoShadow` | 🟡 | parsed, no consumer |
| `Environment` | ✅ | sphere-mapped additive reflection via `r_draw_mesh_env`; applied to pickups (Life/Time) + John's helmet/armor. Any obj_render / John sub-mesh with the flag shines. |
| `TextureAdress Wrap` | ❌ | wrap mode; we hardcode `r_texture_repeat` on a few loaders instead |

## Transform / animation (per Model) — Stage A
All now parsed into `thm_mesh` and distilled to `struct mesh_anim`; `mesh_anim_eval()`
(render_scene) turns them into per-frame yaw/bob/scale. Theme rates are ~rad/ms (×1000
→ rad/s). Applied to the **crystal** so far (spin/bob/random-yaw are the .thm's, not
hand-coded); pickups + John still to route through it.
| keyword | uses | status | notes |
|---|---|---|---|
| `RandomYAngle` | 61 | ✅ parsed, 🟡 applied(crystal) | stable per-instance random yaw (seeded by tile) |
| `Rotate <x y z>` | 42 | ✅ parsed, 🟡 applied(crystal) | continuous spin; crystal now 0.0005 rad/ms not `rf->t*45` |
| `Oscillate <amp speed [random] [phase]>` | 33 | ✅ parsed, 🟡 applied(crystal) | bob; crystal 0.1/0.005 = what we hand-tuned |
| `Pump <amp ... speed>` | 9 | ✅ parsed+applied | amp=arg0, speed=LAST arg (0.005 rad/ms); Life throb |
| `Position <x y z>` | 46 | ✅ (obj_render objects) | per-mesh offset via draw_obj (stair/pickups); crystal/John still bespoke |
| `Specular` | 43 | ✅ parsed, ❌ applied | flag captured; renderer effect pending (Stage B) |

## Behaviour / other — updates
| keyword | uses | status | notes |
|---|---|---|---|
| `Billboard <size>` | 10 | ✅ parsed+applied(obj_render) | additive glow sprite (Time/Protection flare); loaded into obj_render.bb, drawn in the additive pass. NOTE: Crystal's own glow Billboard (Forest licht01) not yet drawn — crystal still on its bespoke path. |

## Behaviour / other
| keyword | uses | status | notes |
|---|---|---|---|
| `Explode` | 24 | ❌ | shatter params; we hardcode `r_draw_mesh_shatter` amounts |
| `Billboard` | 10 | ❌ | billboard-type objects not handled generically |
| `Flash` | 12 | ❌ | not applied |
| `Menu*TextColors` | many | ❌ | menu styling hardcoded; only `HUDTextColors` honored |

---

## Plan — make it data-driven, in stages

Central idea: a per-mesh **render descriptor** on `thm_mesh` (transform + anim + material
flags), a generic **applier** that turns it into the per-frame model matrix + passes, and
retire the handwritten constants. Each stage builds + is verifiable on its own.

- **Stage A — transform & animation** (kills the crystal/John hardcode)
  Parse `RandomYAngle` / `Rotate` / `Oscillate` / `Pump` / `Specular` and all `Position`s
  into `thm_mesh`. Add a helper computing per-instance spin (Rotate·t), bob (Oscillate),
  pump-scale (Pump), and a stable per-instance random Y (RandomYAngle, seeded by tile).
  Route crystal + pickups + John through it; delete the `rf->t*45` / `sinf(rf->t*4.2)`
  constants. *Renderer W/A: `Specular` becomes an additive rim/boost we implement.*

- **Stage B — materials** (the shine + honest render state)
  Parse `Environment` → `thm_texture.environment`; load `tex[1..]`; add `r_draw_mesh_env`
  (additive, spheremap UVs from view-space normals) for the reflection. Honor
  `TextureAdress Wrap`, `NoZWrite`, `DestBlend` generically instead of by-hand.

- **Stage C — generic object drawer**
  Fold the bespoke per-type draws (crystal, pickups, stair, movers, thrower, obstacle)
  into one drawer that reads the descriptor + applies A/B. Removes the remaining
  per-object hardcode; new objects render correctly with no new code.

- **Stage D — remainder** (as needed)
  `Explode` params, `Billboard` objects, `Flash`, menu colours.

Renderer-side hand-work that is legitimate (flag-driven, value from `.thm`): env-map
math, specular, depth-write/wrap state mapping. Not legitimate: inventing bob/spin/pump
rates the `.thm` already gives.
