/* Load-once asset caches + small theme loaders. The .leo reuses platte/bush meshes many
 * times, so meshes and textures are cached by path; reset per level via
 * assets_reset_caches(). `base` is the asset root (passed, not global). */
#ifndef GAME_ASSETS_H
#define GAME_ASSETS_H

#include <stdint.h>
#include "render/renderer.h"
#include "formats/thm.h"
#include "formats/tga.h"

/* drop all cached meshes/textures (call on level/scene load). */
void   assets_reset_caches(void);

/* cached mesh (frame 0) for a path; uploads + caches on first use. */
r_mesh mesh_cached(const char *base, const char *rel);

/* frame-0 model bbox of a cached mesh (must have been mesh_cached first). */
bool   mesh_bbox(const char *rel, r_vec3 *mn, r_vec3 *mx);

/* cached texture for a path ("" -> -1); loads + caches on first use. */
r_tex  tex_cached(const char *base, const char *rel);

/* one-shot (uncached) texture load: resolve+upload+free; NULL/"" -> -1. */
r_tex  load_tex_rel(const char *base, const char *rel);
/* like load_tex_rel but sets alpha = luminance first (black-bg sprites/glows). */
r_tex  load_tex_rel_alpha(const char *base, const char *rel);

/* load the first Model mesh (frame 0) + first texture of theme object `slot`. */
void   load_slot_model(const char *base, const thm_theme *th, int slot, r_mesh *mesh, r_tex *tex);

/* cached ALL-frames anim mesh for a path (dedup by path); *nframes = frame count. */
r_mesh leo_anim_mesh(const char *base, const char *rel, int *nframes);

/* first + a distinct later additive (SrcBlend One) glow texture of theme slot (or NULL). */
void   thm_slot_glow_texes(const thm_theme *th, int slot, const char **first, const char **last);

/* set a black-bg sprite's alpha = per-pixel luminance (transparent surround). */
void   alpha_from_luma(uint8_t *rgba, int w, int h);

/* load a particle sprite: RGB from `rel` + alpha from its companion map (or luminance). */
r_tex  load_particle_tex(const char *base, const char *rel);

/* vivid dominant colour of an image (for gem glow/spark tint); `fallback` if empty. */
r_color dominant_color(const tga_image *im, r_color fallback);

/* .wav mapped to a thm "Sound<event>" entry (case-insensitive), or -1. */
r_sound load_theme_sound(const char *base, const thm_theme *th, const char *event);

/* the same wav as a looping stream (sustained state loops), or -1. */
r_music load_theme_music(const char *base, const thm_theme *th, const char *event);

#endif /* GAME_ASSETS_H */
