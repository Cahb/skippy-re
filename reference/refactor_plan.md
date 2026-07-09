# main.c Refactor Plan (staged, behavior-preserving)

src/main.c is ~2700 lines: a monolithic hub (level load, asset caches, theme setup,
FX, HUD, camera, render loop, input). Split into src/game/*.c modules, each stage a
PURE move/rename (no logic change), built with `make game` + committed separately.
New modules: "game/<name>.h" (matches formats/ sim/ render/ scheme, -Isrc).

## Threading model
Bundle the ~40 per-level asset globals (+ lvl, sim) into ONE `struct scene` / global
`g_scene`; field accesses become `g_scene.x` (mechanical rename). `base` + `g` (manifest)
stay as two documented set-once globals. Helpers take `const struct scene*` / `const
jjm_level*` / `const sim_state*`.

## Staged order
1. game/fx.c/.h        — fx_spawn/burst/update/draw (+ fx_particle, FX_MAX). ZERO coupling. First.
2. game/hud_text.c/.h  — hud_num/font2_text/font2_text_lines/text_line_count; pass r_tex as params.
3. game/camera.c/.h    — freecam struct + freecam_sync/view/update (follow/occlusion/death-cam later).
4. easy wins IN main.c — load_tex_rel() helper (20+ TGA-load sites), reset_after_level_load()
                         (5 dup sites), anim_frame_pair() (3 dup sites), THM_SLOT_* enum.
5. game/assets.c/.h    — mesh_cached/mesh_bbox/tex_cached/leo_anim_mesh/load_slot_model/
                         load_particle_tex/load_theme_sound/glow_texes/alpha_from_luma/dominant_color;
                         caches file-static + assets_reset_caches().
6. struct scene + game/scene.c/.h — define struct scene, rename accesses (RISKY: ~40 globals,
                         do rename commit separately from file move); move load_scene_named (~560 lines,
                         optionally split into load_tiles/hud/sounds/fx_tex/theme_objects/leo/john).
7. game/render_scene.c/.h — ray_aabb/edge_exposed + catmull3/leo_spline_pos/leo_draw_one/leo_is_alpha +
                         world/entity/transparent/additive render (split render_tiles/entities/
                         transparent/fx_pass/picker), take const struct scene* + view + t.
8. game/audio.c/.h     — audio_pos + ambience mix + SFX dispatch; then move follow/occlusion/death-cam
                         into camera.c.

End: main.c ~400-500 lines = gam load + r_init + session state + screen SM + input + emitter calls +
HUD layout + loop wiring.

## Per-stage verification
No visual diff available -> each stage is byte-for-byte behavior-preserving (no reordered
side-effects, no changed literals/bounds); check = clean `make game` + diff-review (renames only).
Stage-4 helper extractions each get their own commit for bisectability. Stage 6 field-rename is the
one silent-behavior-change risk: review field-by-field, commit before the file move.

## Easy wins detail
- load_tex_rel(base,rel) [+_repeat / +_alpha variants] collapses the asset_resolve+tga_load+
  r_load_texture_rgba+tga_free idiom (20+ copies in load_scene_named).
- Route crystal/stair/glue-mesh loaders through load_slot_model (near-identical loops).
- reset_after_level_load(): cam_fx/fy, anim_clip/frame, idle_timer, level_time, last_sec, pick_kind.
- anim_frame_pair(clip, prog, &fa,&fb,&t): dup in John/catcher/thrower render.
- THM_SLOT_* enum (JOHN=0 CATCHER=1 THROWER=3 PLATE=5 SIDE=6 PLATFORM=7 PARAGLIDE=8 ELEVATOR=10
  EXIT=11 GLUE=12 JUMPPAD=15 STAIR=17 CRYSTAL=19).
- Move leo_is_alpha out of the HUD cluster. snd_exit always -1 (parked); jscale always 1.0.
