/* 3D scene rendering: world tiles, John, enemies/movers/bombs, .leo props, the
 * transparent + additive passes and the death/angel overlay, plus the debug
 * click-picker. Reads the loaded scene (game/scene.h) + the per-frame inputs. */
#ifndef GAME_RENDER_SCENE_H
#define GAME_RENDER_SCENE_H

#include <stdbool.h>
#include "render/renderer.h"
#include "formats/ani.h"
#include "sim/sim.h"

/* what the debug click-picker last selected (x/y = tile cell or .leo index) */
enum { PICK_NONE, PICK_TILE, PICK_JOHN, PICK_CRYSTAL, PICK_LEO };
struct picker { int kind, x, y; };

/* per-frame inputs the 3D render needs from the game loop */
struct render_frame {
	r_camera view;         /* active camera */
	float    t;            /* global anim clock (t_accum) */
	r_vec3   jw;           /* John's world position (raised while dying) */
	int      anim_clip;    /* John's current animation clip */
	float    anim_frame;   /* John's current (fractional) frame */
	bool     dying;        /* death sequence active (angel rise) */
	float    death_t;      /* seconds into the death sequence */
};

/* movement clip for an animated entity's current step (stair-aware). Shared with
 * the player-animation selection in the game loop. */
int  move_clip_for(const sim_player *e, const ani_set *A);

/* draw the whole 3D scene + run the click-picker (updates *pick_*). Call after
 * r_begin_frame + camera math; this issues r_set_camera and ends with r_end_3d. */
void render_scene_3d(const char *base, const struct render_frame *rf,
                     struct picker *pick);

#endif /* GAME_RENDER_SCENE_H */
