# Mid-pipeline draw passes — mapped (asm, radare2)

The "section 6" unknowns from the reconstruction. Each was disassembled
(`claude/disasm/sub_*.asm`) and classified by its **D3D method profile** +
constants. Two distinct kinds emerged: **mesh wrappers** (delegate to the model
drawer) vs **self-contained quad/billboard FX** (drive D3D directly).

| func | size | calls | D3D profile | verdict |
|------|------|-------|-------------|---------|
| `0x408870` | 60 L | `game_draw_dynamic_3d_object_in_world` ×1 | none direct | **mesh-draw wrapper** — draws one themed object category via the standard model drawer. Not an effect. |
| `0x408920` | 70 L | model-drawer ×1 + helper | none direct | **mesh-draw wrapper** — same, another object category. |
| `0x408710` | 134 L | — (fully inline) | RS×7, SetTransform×1, **SetTexture×1, DrawPrimitive×1** | **single textured billboard quad** — `push 0x1e2(FVF); push 4(verts); DrawPrimitive` = 4-vert strip. |
| `0x408280` | 340 L | `D3DMATRIX_Construct` ×3 | RS×7, SetTransform×2, SetTexture×1, DrawPrimitive×1 | **billboard FX with orientation** — builds 3 matrices (place + rotate), one textured quad. |
| `0x408a00` | 637 L | `D3DMATRIX_Construct` ×1 + helper | RS×11, SetTransform×1, SetTexture×1, DrawPrimitive×1 | **larger direct-draw FX/overlay** — heavy state setup (11 render states), one textured draw. |
| `0x420f50` | 1020 L | `dot/len²`(0x4037e0) ×8, `D3DMATRIX_Construct` ×2, CRT | RS×8, SetTransform×2, **SetTexture×2**, DrawPrimitive×1 | **rotational/animated effect** — uses **2π** (`0x45d2f8`) + **π/2** (`0x45d2cc`) with `fsubr`/`fadd` angle-wrap and 8 dot products; two textures. A spinning/orbiting billboard driven by a per-frame angular cycle (portal/sun/swirl-class). The heaviest of the group. |

### Supporting helpers (also mapped this round)
| func | size | what |
|------|------|------|
| `0x406480` | 37 L | **`D3DMATRIX_Construct`** — 16 dwords → 64-byte `D3DMATRIX`. Every matrix in the engine routes through it (22× in main_render). |
| `0x42cd60` | 30 L | **vertex builder** — copies XYZ from a vec ptr + 5 dwords → a **32-byte vertex** (`D3DLVERTEX`/`D3DTLVERTEX`: x,y,z,color,specular,tu,tv). Called 17× to assemble hand-built quad verts (HUD, fade, FX). `ret 0x18`. |

## Constants decoded (from .rdata, used by `0x420f50`)
| VMA | value | meaning |
|-----|-------|---------|
| `0x45d2f8` | `6.283185` | **2π** — full-circle angle wrap |
| `0x45d2cc` | `1.570796` | **π/2** — quadrant offset |
| `0x45d2e8` | `1.0` (f64) | unit |
| `0x45d448` | `10.0` (f64) | speed/radius scalar |

## Takeaways for the reconstruction
- Section 6's "transparent/FX passes" are really a handful of **per-effect
  functions**, not one inline blob. `0x408710`/`0x408280` are simple billboards;
  `0x408a00` is a bigger overlay; `0x420f50` is the one with genuine animated
  math (angular cycle + dot products).
- `0x408870`/`0x408920` are **opaque mesh draws**, not FX — they belong up in
  section 4 (world geometry), each rendering a specific object category.
- All hand-built geometry uses the 32-byte-vertex + `D3DMATRIX_Construct` pair;
  the 4-vert `DrawPrimitive` strips are quads/billboards.

## Still open (needs runtime correlation, not static analysis)
- The *specific* visual each FX is (which is the portal vs sun vs sparkle).
  Cheap to resolve via the proxy: log the `SetTexture` arg (texture handle/name)
  at each pass's call site and match against the loaded texture set.
