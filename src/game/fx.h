/* Lightweight particle FX: short-lived camera-facing billboards for sparkle bursts,
 * engine exhaust, fountain spray, etc. Additive (glow, fades via RGB) or alpha (masked
 * sprites like bees, fades via alpha). Self-contained; the caller sets the blend mode
 * before fx_draw and spawns via fx_spawn/fx_burst. */
#ifndef GAME_FX_H
#define GAME_FX_H

#include <stdbool.h>
#include "render/renderer.h"
#include "formats/par.h"

/* spawn one particle. blend = R_BLEND_ADD or R_BLEND_ALPHA. tex < 0 or full pool = no-op. */
void fx_spawn(r_vec3 pos, r_vec3 vel, float life, float sz0, float sz1, float gz,
              r_tex tex, r_color c, int blend);

/* emit particles from a decoded .par this frame, anchored at `anchor` (world) and
 * rotated `yaw_deg` about world-Z (so candles ride a stair's facing). *accum carries
 * the fractional spawn count across frames. Reads the .par's real spawn/velocity/life/
 * gravity/colour ranges and maps its Y-up space to our Z-up. blend passed to fx_spawn.
 * (This drives the MOVING, world-space emitters — candles/torches/exit/thruster/.leo.
 * A "notMovable" system that's bound to an animated object, e.g. the crystal spark, is
 * drawn attached at the object's live transform instead — see render_scene.) */
/* sz0/sz1 > 0 override the .par-derived spawn/death sizes (the bee-speck quirk). */
void par_emit(const par_system *ps, r_tex tex, int blend,
              r_vec3 anchor, float yaw_deg, float dt, float *accum,
              float sz0_ovr, float sz1_ovr);

/* one-shot .par burst: spawn `count` particles at once from the system (no rate/accum),
 * e.g. the debris crumbs when a step-and-break tile collapses. vjit>0 gives each a random
 * horizontal drift velocity so a point .par fans out into tile-wide debris over time. */
void par_burst(const par_system *ps, r_tex tex, int blend,
               r_vec3 anchor, float yaw_deg, int count, float vjit);

/* radial additive sparkle burst of n particles (crystal/pickup). */
void fx_burst(r_vec3 pos, int n, r_tex tex, r_color c);

/* drop all live particles (call on level/scene load). */
void fx_reset(void);

/* advance all live particles by dt (move + per-particle gravity, retire the dead). */
void fx_update(float dt);

/* draw particles matching `blend` as billboards — caller has that blend mode set. */
void fx_draw(int blend);

#endif /* GAME_FX_H */
