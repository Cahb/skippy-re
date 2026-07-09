/* Renderer interface — the thin, swappable seam between the engine-agnostic
 * data (formats/sim) and whatever 3D backend draws it. Kept to a handful of
 * functions on purpose — the engine is deferred behind this thin interface.
 * The only backend today is renderer_raylib.c; nothing above this header knows
 * about raylib. Meshes/textures are opaque integer handles. */
#ifndef RENDERER_H
#define RENDERER_H

#include <stdbool.h>
#include <stdint.h>
#include "formats/mdl.h"

typedef struct { float x, y, z; } r_vec3;
typedef struct { uint8_t r, g, b, a; } r_color;

/* World is Z-UP, matching the original game (reference/jjs_format.md: world.X ~
 * grid, world.Z = height) and the .mdl models (model-space +Z is up). So:
 *   world.x = grid.x ,  world.y = grid.y ,  world.z = tile height (z_pos)
 * Meshes are drawn verbatim (no axis remap) and stand upright. "Yaw" (facing)
 * is rotation about the world up axis = Z. raylib is Y-up internally but doesn't
 * care: we just pass up = {0,0,1} to the camera. */
typedef struct {
    r_vec3 pos;      /* eye */
    r_vec3 target;   /* look-at point */
    r_vec3 up;       /* world up = {0,0,1} */
    float  fovy;     /* vertical FOV in degrees */
} r_camera;

typedef int r_mesh;   /* handle; <0 = invalid */
typedef int r_tex;    /* handle; <0 = invalid */
typedef int r_sound;  /* handle; <0 = invalid */

/* --- lifecycle --- */
bool r_init(int width, int height, const char *title);
void r_shutdown(void);
void r_reset(void);            /* unload all meshes + textures (for a level reload) */
bool r_should_close(void);     /* window close / ESC */

/* --- audio (SFX) --- */
void    r_audio_init(void);            /* open the audio device (after r_init) */
void    r_audio_shutdown(void);        /* unload sounds + close the device */
r_sound r_load_sound(const char *path);/* load a .wav; <0 on failure/no device */
void    r_play_sound(r_sound s);       /* fire-and-forget one-shot (centre, full volume) */
void    r_play_sound_ex(r_sound s, float vol, float pan); /* positional: vol 0..1, pan 0..1 (0.5=centre) */
void    r_stop_sound(r_sound s);       /* cut every playing voice of s (e.g. skipped narration) */
bool    r_sound_playing(r_sound s);    /* any voice of s still audible? (retrigger idiom: the
                                        * original re-Plays fuse/LastSeconds every tick, which
                                        * no-ops while playing and restarts the moment it ends) */
void    r_unload_sounds(void);         /* drop all sounds (for a level/theme reload) */

/* looping ambient via a streamed, seamlessly-looping Music track (no re-trigger click). */
typedef int r_music;                   /* handle; <0 = invalid */
r_music r_load_music(const char *path);/* load a looping music stream; <0 on failure */
void    r_music_update(r_music m, float vol, float pan); /* call each frame: vol 0..1, pan -1..1 (0=centre) */
void    r_unload_music(void);          /* stop + drop all music (for a level/theme reload) */

/* category gains (0..1), applied at the playback chokepoints — every trigger site
 * inherits them, no call-site churn. MASTER scales both; MUSIC covers the .leo
 * ambience and the background track; SFX covers the one-shots. */
enum { R_VOL_MASTER = 0, R_VOL_MUSIC, R_VOL_SFX };
void    r_set_volume(int kind, float vol);

/* streamed background-music track (one at a time; the CD-audio analogue). Unlike
 * r_load_music (fully-buffered short loops) this DISK-STREAMS, so 25-50MB CD rips
 * don't sit in RAM. The loop rejoin is a seek (raylib Music) — inaudible on a
 * multi-minute track. Volume = master*music, applied in r_track_update. */
bool    r_track_play(const char *path, bool loop);
void    r_track_stop(void);
void    r_track_set_paused(bool paused); /* hold/resume WITHOUT losing the position */
void    r_track_update(void);          /* pump once per frame */

/* --- resources (load once, after r_init) --- */
r_tex  r_load_texture_rgba(const uint8_t *rgba, int w, int h);
/* load an image FILE (bmp/png/...) straight to a texture — shell/loading screens */
r_tex  r_load_texture_file(const char *path);
/* colour BMP + companion mask BMP (mask R -> alpha), e.g. the menu panel */
r_tex  r_load_masked_bmp(const char *color_path, const char *mask_path);
/* Upload one mesh frame (mdl TRIANGLELIST, num_verts verts, no index buffer). */
r_mesh r_upload_mesh(const mdl_vertex *verts, int num_verts);
/* Upload a vertex-morph mesh: num_frames consecutive snapshots of num_verts. */
r_mesh r_upload_anim(const mdl_vertex *frames, int num_verts, int num_frames);

/* --- per-frame --- */
void r_begin_frame(r_color clear);
void r_set_camera(r_camera cam);         /* begins 3D mode for this frame */
/* draw a cubemap skybox centered on the current camera (call right after
 * r_set_camera). faces order: [0]=FR(+Y) [1]=BK(-Y) [2]=LF(-X) [3]=RT(+X)
 * [4]=UP(+Z) [5]=DN(-Z). A face <0 is skipped. */
enum { R_SKY_FR = 0, R_SKY_BK, R_SKY_LF, R_SKY_RT, R_SKY_UP, R_SKY_DN };
void r_draw_skybox(const r_tex faces[6]);
void r_draw_mesh(r_mesh m, r_tex tex, r_vec3 pos, r_vec3 scale, float yaw_deg); /* yaw about world up (Z); frame 0 */
/* draw a specific animation frame (clamped to the mesh's frame count) */
void r_draw_mesh_frame(r_mesh m, r_tex tex, r_vec3 pos, r_vec3 scale, float yaw_deg, int frame);
/* explode a mesh into its own triangles (theme Explode FX); t: 0=whole -> 1=gone */
void r_draw_mesh_shatter(r_mesh m, r_tex tex, r_vec3 pos, r_vec3 scale, float yaw_deg,
                         int frame, float t);
/* draw a mesh with sphere-mapped UVs from the view-space vertex normals (theme
 * Environment reflection): a metallic shine that tracks the camera + the model's spin.
 * Interpolates frames fa..fb by t (pass fa=fb=0,t=0 for a static mesh) so it rides an
 * animated body. Caller sets additive blend + depth-write-off; drawn over the base mesh. */
void r_draw_mesh_env(r_mesh m, r_tex envtex, r_vec3 pos, r_vec3 scale, float yaw_deg,
                     int fa, int fb, float t);
/* draw a vertex-morph blend between frames fa and fb by t (0..1) — smooths
 * low-fps clips at any render rate. */
void r_draw_mesh_lerp(r_mesh m, r_tex tex, r_vec3 pos, r_vec3 scale, float yaw_deg,
                      int fa, int fb, float t);
/* switch from the opaque pass to alpha-blended: flushes opaque geometry (drawn
 * with blending off) then enables alpha blend for subsequent draws (crystals,
 * FX). Call once, after all opaque draws, before transparent ones. */
void r_begin_transparent(void);
/* switch the blend equation for subsequent draws — only meaningful once blending
 * is on (i.e. after r_begin_transparent). Reset to R_BLEND_ALPHA before 2D/HUD. */
enum { R_BLEND_ALPHA = 0, R_BLEND_ADD };
void r_set_blend(int mode);
/* toggle depth writes (test stays on). Disable for additive glow/particles so
 * their transparent quads don't box-clip each other via the depth buffer. */
void r_depth_write(int on);
/* flush queued transparent geometry now (commit under current blend + depth state) */
void r_flush(void);
/* set a texture's wrap mode to REPEAT so UVs outside [0,1] tile (scrolling FX) */
void r_texture_repeat(r_tex tex);
/* camera-facing textured billboard (call in 3D / transparent pass; caller sets blend).
 * *_ex: diamond!=0 rotates the quad 45° (verts at edge midpoints) for speck-like FX. */
void r_draw_billboard(r_tex tex, r_vec3 pos, float size, r_color tint);
void r_draw_billboard_ex(r_tex tex, r_vec3 pos, float size, r_color tint, int diamond);
/* flat ground-plane textured quad (for decals like the exit glow). mirror!=0
 * flips U (maps the texture's other diagonal — e.g. the X-corner glow). */
void r_draw_quad_flat(r_tex tex, float cx, float cy, float z, float half, r_color c, int mirror);
/* horizontal tile-top quad with scrolling/tiling UVs (animate uoff/voff to "flow").
 * swap!=0 transposes the UVs (texture rotated 90°) — e.g. an X-axis bridge so its lines
 * run along the span. */
void r_draw_tile_scroll(r_tex tex, float cx, float cy, float z, float half,
                        float uoff, float voff, float tiles, r_color c, int swap);
/* horizontal tile-top quad with UVs rotated by `angle` (rad) about the texture centre —
 * the theme "Turn" swirl (e.g. teleporter base). Blend/pass are the caller's. */
void r_draw_quad_turn(r_tex tex, float cx, float cy, float z, float half, float angle, r_color c);
/* like _scroll but sine-WARPS the texture across a subdivided grid (fluid ripple);
 * ampu/ampv = per-axis UV warp amplitude (theme Wobble params) */
void r_draw_tile_wobble(r_tex tex, float cx, float cy, float z, float half,
                        float t, float ampu, float ampv, r_color c);
/* ONE composable tile-top layer (the generic .thm Field renderer): turn spin
 * (disc-inscribed) + swap transpose + scroll offsets + optional wobble warp,
 * in that UV order. Tint carries Pulse/Flash brightness; honors current blend. */
void r_draw_layer(r_tex tex, float cx, float cy, float z, float half,
                  float turn, float uoff, float voff,
                  float wob_t, float wob_au, float wob_av, r_color c, int swap);
void r_draw_box(r_vec3 center, r_vec3 size, r_color fill);       /* debug tile geom */
void r_draw_box_wires(r_vec3 center, r_vec3 size, r_color line);
/* a floating platform tile: top face uses `top` texture, exposed side faces use
 * `side` (wrapped), modulated by `tint`. center/size in world units (Z-up).
 * side_mask selects which of the 4 sides to draw (bit0=-Y bit1=+Y bit2=-X bit3=+X)
 * so internal edges shared with same-height neighbours emit no seam. */
enum { R_SIDE_NY = 1, R_SIDE_PY = 2, R_SIDE_NX = 4, R_SIDE_PX = 8, R_SIDE_ALL = 15 };
/* tile slab; top_turn (rad) rotates the top face's UVs in place (theme "Turn" swirl), 0 = none;
 * draw_bottom=0 skips the underside face (vine-curtain platforms have no solid dirt bottom). */
void r_draw_tile(r_vec3 center, r_vec3 size, r_tex top, r_tex side, r_color tint, int side_mask,
                 float top_turn, int draw_bottom);
/* wrap alpha-marked (theme "Alpha") draws in the opaque pass: on=1 discards transparent
 * fragments (cutout), on=0 restores the default shader. */
void r_alpha_test(int on);
/* exposed side faces only, full-texture V-mapping — a full-image side (vine curtain),
 * drawn under r_alpha_test after the opaque tops. */
void r_draw_tile_sides(r_vec3 center, r_vec3 size, r_tex side, r_color tint, int side_mask);
void r_end_3d(void);                     /* end 3D mode (before 2D HUD) */
void r_draw_text(const char *s, int x, int y, int px, r_color c); /* screen-space HUD */

/* --- 2D / HUD primitives (screen pixels; call after r_end_3d) --- */
/* blit a src-rect (pixels) of a texture into a dst-rect (pixels), modulated by tint */
void r_draw_sprite(r_tex tex, float dx, float dy, float dw, float dh,
                   float sx, float sy, float sw, float sh, r_color tint);
void r_draw_rect(float x, float y, float w, float h, r_color c);      /* filled */
void r_draw_circle(float cx, float cy, float rad, r_color c);         /* filled */
void r_draw_line(float x1, float y1, float x2, float y2, float w, r_color c);
void r_screen_size(int *w, int *h);      /* current framebuffer size */
void r_draw_fullscreen(r_tex tex, r_color tint);  /* stretch a texture over the whole screen */

void r_end_frame(void);                  /* r_present / swap */

/* frame timing helper (seconds since last frame) */
float r_frame_time(void);

/* save the current framebuffer to a PNG (debug) */
void r_screenshot(const char *path);

/* --- debug input (engine-agnostic key ids) --- */
typedef enum {
	R_KEY_W = 'W', R_KEY_A = 'A', R_KEY_S = 'S', R_KEY_D = 'D',
	R_KEY_Q = 'Q', R_KEY_E = 'E', R_KEY_R = 'R', R_KEY_F = 'F', R_KEY_G = 'G',
	R_KEY_SPACE = ' ', R_KEY_ENTER = '\n',
	R_KEY_F1 = 1001, R_KEY_F2, R_KEY_F3, R_KEY_F4, R_KEY_F5, R_KEY_F6,
	R_KEY_F7, R_KEY_F8, R_KEY_F9, R_KEY_F10, R_KEY_F11, R_KEY_F12,
	R_KEY_SHIFT = 1101, R_KEY_CTRL = 1102,
	R_KEY_UP = 1103, R_KEY_DOWN, R_KEY_LEFT, R_KEY_RIGHT,
	R_KEY_ESC = 1107, R_KEY_BACKSPACE = 1108,
} r_key;

bool  r_any_key_pressed(void);      /* any key or left-click this frame (splash skip) */
bool  r_key_pressed(int key);       /* went down this frame (edge) */
bool  r_key_down(int key);          /* held */
int   r_char_pressed(void);         /* next queued typed char (0 = none) — name entry */
void  r_poll_input(void);           /* refresh key edges on a frame that skips drawing
                                       (scene reloads) — else the old edges fire twice */
float r_mouse_wheel(void);          /* wheel delta this frame */
bool  r_mouse_down(int button);     /* 0=left 1=right 2=middle */
bool  r_mouse_pressed(int button);  /* clicked this frame (edge) */
void  r_mouse_delta(float *dx, float *dy);
/* ray from the camera through the mouse cursor (call after r_set_camera) */
void  r_mouse_ray(r_vec3 *origin, r_vec3 *dir);

#endif /* RENDERER_H */
