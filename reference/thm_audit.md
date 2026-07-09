# .thm coverage audit (2026-07)

Every keyword used across all 7 `.thm` files, cross-referenced against the parser
(`src/formats/thm.c` → `thm_mesh`/`thm_tex`) and the consumers (`scene.c` +
`render_scene.c`). Usage counts are total occurrences across all themes.

Legend: ✅ handled · 🟡 parsed but ignored · ❌ not parsed · ➗ partial

## ✅ Handled
- **Structural**: `Model` `Field` `Texture` `ParticleSystem` `Billboard` `Sound` `Position` `SideHeight`
- **Mesh anim**: `RandomYAngle` (66) · `Rotate` (46) · `Oscillate` (36) · `Pump` (10)
- **Material**: `Environment` (37, spheremap shine) · `Pulse` (4, additive tile glow) · `Turn` (5, UV swirl) · `SrcBlend`/`DestBlend` (202, additive/alpha detect) · `Alpha` (cutout alpha-test)
- **Global**: `Sky` (skybox) · `Radar` (minimap)
- **`.par`**: gravity dir×magnitude, emit_rate, spawn cone/base spread (all just fixed)

## ➗ Partial
- **`Condition`** (90) — handled: `InActive`/`Active` (glue fresh/spent, destruct, exit-open), `Dead` (John angel). **NOT** clearly handled: `Alive` (25), `Paraglide` (3). Verify each object's condition-gated textures actually switch.
- **`Wobble`** (6) — driven for glue goo; **Egypt `teleporter3`** additive+Wobble overlay is not composited (needs the ordered multi-layer Field compositor).
- **`Explode`** (26, e.g. `Explode 2 0.1 200 0 0.4 0` on enemies/obstacles) — mesh-shatter FX **exists** but is game-triggered with hardcoded params; the `.thm` Explode parameters (count/spread/speed/…) are **not parsed or applied**.

## 🟡 Parsed but ignored (struct field exists, no renderer consumes it)
- **`Specular`** (46) — `thm_mesh.specular` set, never used. Specular highlight not applied.
- **`NoZWrite`** (22) — `thm_tex.nozwrite` set, never used. We globally manage depth-write in FX passes instead; some transparent world textures may sort wrong without honoring this per-texture.

## ❌ Not parsed at all (priority order)
1. **`Flash`** (14) — **exit-glow pulse** (user-reported). ATTEMPTED + RE'd + PARKED (too fiddly to match; revisit later). RE: `draw_dynamic_3d_object` @0x4095f0, outer `switch(fx_anim_type)` `case 1` = flash. It builds an **animated texture-COORD matrix** (D3D texture transform) that **zooms + rotates the glow's UVs** — the texture content swirls/converges (circle→smaller→spark), it is NOT a brightness fade or a quad-scale. `param[0]` (=10 all themes, so ALL exits pulse incl. Forest — `param[1]` 0.000/0.001 does NOT gate it) scales it; `(int)time` cycles 4 UV orientations; `param[2]` (0 / 3 / 4) = per-layer phase. The theme stacks the glow tex TWICE (same orientation, phases 3&4 — NOT mirrored; our old mirror was wrong). Faithful repro needs an animated-UV-matrix quad (extend `r_draw_quad_turn` w/ zoom) tuned to the frames — tried rotate+zoom, read as a "siren"/"4 dots"; the exact matrix constants weren't cleanly extractable from the mangled decomp. Frames captured in `x_drop_screenshots/`. **MED (parked)**
2. **`TextureAdress Wrap`** (17) — texture wrap mode. We only `r_texture_repeat` hardcoded for glue; should be generic per-texture. **MED**
3. **`Edge`** (7, one per theme, `textures\*\kante*.tga alpha`) — the level-border edge/skirt texture. Not drawn at all. **MED**
4. **`Lit`** (6, e.g. Candy keks destruct, Castle spinnweben) — per-mesh lighting flag. **LOW**
5. **`Scroll`** (2, `Scroll 0 0.0001`) — UV scroll rate. `r_draw_tile_scroll` exists (glue/ice) but isn't `.thm`-driven. **LOW**
6. **`Fog`** (1, Space `Fog exp2 0.06 00222244`) — exp2 distance fog + colour. **LOW**

## Other known gaps (not `.thm`-keyword)
- `.leo` `Zero`/`Zero` blend = invisible (the bees over-render).
- Global handedness (D3D LH vs raylib RH) — asymmetric props mirror-flipped. PARKED.

## Not applicable / out of scope (menu & HUD chrome)
`Menu*TextColors` (×many) · `HUD`/`HUDTextColors` · `Pointer` · `Slowdwon` (sic) ·
`InverseControl` · `MenuVideo*` — UI/global config, not the 3D world renderer.
